#ifndef DB_CONNECTOR_H
#define DB_CONNECTOR_H

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "concurrency/std_thread_compat.h"  // must come before mysql.h on Windows (winsock)

// Works with libmysqlclient (MySQL), and with MariaDB Connector/C (same C API).
#if defined(__has_include)
#  if __has_include(<mysql/mysql.h>)
#    include <mysql/mysql.h>
#  elif __has_include(<mariadb/mysql.h>)
#    include <mariadb/mysql.h>
#  else
#    include <mysql.h>
#  endif
#else
#  include <mysql.h>
#endif

namespace db {

struct DBConfig {
    std::string host = "127.0.0.1";   // use an IP/hostname, not "localhost" (forces TCP on all OSes)
    unsigned int port = 3306;
    std::string user = "root";
    std::string password;
    std::string database = "pride";  // created automatically if missing and the user may create it

    // BANKSAFE_DB_HOST / _PORT / _USER / _PASSWORD / _NAME override the defaults.
    static DBConfig fromEnv();
};

// One MySQL connection guarded by a mutex (every call is serialized, exactly like
// the old SQLite connector). Autocommit is ON: each statement commits by itself,
// and atomicity of a transfer comes from the application-level WAL, not from SQL
// transactions.
//
// Parameters ('?') are substituted client-side with mysql_real_escape_string and
// quoted, so values can never alter the SQL. Only a '?' directly after LIMIT or
// OFFSET is emitted as a bare integer (and only if it is all digits).
class DBConnector {
public:
    static DBConnector& getInstance();

    bool init(const DBConfig& cfg = DBConfig::fromEnv());
    void close();

    // May contain several ';'-separated statements (schema.sql, seed_data.sql).
    bool execute(const std::string& sql);
    bool executeParameterized(const std::string& sql, const std::vector<std::string>& params);

    std::vector<std::map<std::string, std::string>> query(const std::string& sql);
    std::vector<std::map<std::string, std::string>> queryParameterized(const std::string& sql, const std::vector<std::string>& params);

    MYSQL* getRawHandle() { return conn_; }
    std::string lastError();

    ~DBConnector();

private:
    DBConnector() : conn_(nullptr) {}
    DBConnector(const DBConnector&) = delete;
    DBConnector& operator=(const DBConnector&) = delete;

    typedef std::map<std::string, std::string> Row;

    bool connectLocked(bool selectDatabase);
    bool reconnectLocked();
    void closeLocked();
    bool runLocked(const std::string& sql, std::vector<Row>* rows);
    bool bindLocked(const std::string& sql, const std::vector<std::string>& params, std::string& out);

    MYSQL* conn_;
    DBConfig cfg_;
    std::thread::id init_thread_;
    std::string last_error_;
    std::mutex db_mutex_;
};

} // namespace db

#endif // DB_CONNECTOR_H
