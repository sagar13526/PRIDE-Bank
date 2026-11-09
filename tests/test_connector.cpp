// Connector tests: injection-safe parameter binding, multi-statement scripts, auto-reconnect.
// Uses its own throw-away database "pride_bank_conntest" (needs the same PRIDEBANK_DB_* env vars).
#include "db/db_connector.h"
#include <iostream>
#include <cstdlib>
#include <string>
static int fails=0;
#define CHECK(c) do{ if(!(c)){ std::cerr<<"FAIL line "<<__LINE__<<": " #c "\n"; ++fails; } }while(0)
int main(){
  auto& db = db::DBConnector::getInstance();
  db::DBConfig cfg = db::DBConfig::fromEnv(); cfg.database="pride_bank_conntest";   // created if the user may CREATE; dropped at the end
  CHECK(db.init(cfg));
  db.execute("DROP TABLE IF EXISTS t; CREATE TABLE t (id INT AUTO_INCREMENT PRIMARY KEY, s TEXT) ENGINE=InnoDB;");
  // injection attempts must be stored literally, not executed
  std::string evil = "x'); DROP TABLE t; --";
  CHECK(db.executeParameterized("INSERT INTO t (s) VALUES (?);", {evil}));
  CHECK(db.executeParameterized("INSERT INTO t (s) VALUES (?);", {"O'Brien \\ \"q\" \n end"}));
  auto r = db.query("SELECT s FROM t ORDER BY id;");
  CHECK(r.size()==2 && r[0]["s"]==evil && r[1]["s"]=="O'Brien \\ \"q\" \n end");
  // '?' inside a quoted literal is not a placeholder
  auto q = db.queryParameterized("SELECT '?' AS lit, ? AS p;", {"v"});
  CHECK(q.size()==1 && q[0]["lit"]=="?" && q[0]["p"]=="v");
  // LIMIT ? gets a bare integer; a non-numeric LIMIT param must fail instead of injecting
  CHECK(db.queryParameterized("SELECT * FROM t LIMIT ?;", {"1"}).size()==1);
  CHECK(db.queryParameterized("SELECT * FROM t LIMIT ?;", {"1; DROP TABLE t"}).empty());
  CHECK(db.query("SELECT COUNT(*) AS c FROM t;")[0]["c"]=="2");
  // wrong parameter count is rejected
  CHECK(!db.executeParameterized("INSERT INTO t (s) VALUES (?);", {"a","b"}));
  // reconnect after the server kills our connection
  unsigned long myId = mysql_thread_id(db.getRawHandle());
  MYSQL* killer = mysql_init(nullptr);   // a second connection of the same user may KILL ours
  CHECK(mysql_real_connect(killer, cfg.host.c_str(), cfg.user.c_str(), cfg.password.c_str(), nullptr, cfg.port, nullptr, 0));
  mysql_query(killer, ("KILL " + std::to_string(myId)).c_str());
  mysql_close(killer);
  CHECK(db.query("SELECT COUNT(*) AS c FROM t;")[0]["c"]=="2");   // transparently reconnected
  db.execute("DROP DATABASE pride_bank_conntest;");
  std::cout << (fails? "CONNECTOR TESTS FAILED\n" : "CONNECTOR TESTS PASSED\n");
  return fails?1:0;
}
