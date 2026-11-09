// Crash-recovery tests: real MySQL + WAL. A "crash" is simulated by stopping a
// transaction part-way (no commit, no rollback) and then calling recoverOnRestart(),
// exactly what the server does at start-up.
// The database name defaults to "pride_bank_test" and MUST contain "test" (tables are dropped).
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "db/db_connector.h"
#include "txn/recovery_manager.h"
#include "txn/transaction_manager.h"

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) { std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n"; ++g_fail; } \
    } while (0)

static db::DBConnector& DB() { return db::DBConnector::getInstance(); }
static txn::TransactionManager& TM() { return txn::TransactionManager::getInstance(); }
static txn::RecoveryManager& RM() { return txn::RecoveryManager::getInstance(); }

static double bal(const std::string& id) {
    auto r = DB().queryParameterized("SELECT balance FROM Accounts WHERE id = ?;", {id});
    return r.empty() ? -1 : std::stod(r[0]["balance"]);
}
static double total() { return std::stod(DB().query("SELECT SUM(balance) AS s FROM Accounts;")[0]["s"]); }
static std::string status(int id) {
    auto r = DB().queryParameterized("SELECT status FROM Transactions WHERE txn_id = ?;", {std::to_string(id)});
    return r.empty() ? "" : r[0]["status"];
}
static bool approx(double a, double b) { return std::fabs(a - b) < 0.01; }

static void reset() {
    DB().execute("DELETE FROM DeadlockLog; DELETE FROM TransactionLog; DELETE FROM Transactions;");
    DB().execute("UPDATE Accounts SET balance=5000.00,version=1 WHERE id='ACC001';"
                 "UPDATE Accounts SET balance=3500.50,version=1 WHERE id='ACC002';"
                 "UPDATE Accounts SET balance=10000.00,version=1 WHERE id='ACC003';"
                 "UPDATE Accounts SET balance=7500.25,version=1 WHERE id='ACC004';"
                 "UPDATE Accounts SET balance=2000.00,version=1 WHERE id='ACC005';");
}

static void testCrashAfterBeginOnly() {
    std::cout << "[1] Crash right after BEGIN: nothing to undo, marked ROLLED_BACK\n";
    reset();
    int id = TM().beginTransaction("ACC001", "ACC002", 100.0);
    CHECK(id > 0);
    CHECK(RM().recoverOnRestart() == 1);
    CHECK(status(id) == "ROLLED_BACK");
    CHECK(approx(bal("ACC001"), 5000.0) && approx(bal("ACC002"), 3500.50));
}

static void testCrashAfterDebitAndAfterCredit() {
    std::cout << "[2] Crash after DEBIT, and after DEBIT+CREDIT: both undone\n";
    reset();
    double t0 = total();
    int a = TM().beginTransaction("ACC001", "ACC002", 100.0);
    CHECK(TM().debit(a, "ACC001", 100.0));
    int b = TM().beginTransaction("ACC003", "ACC004", 250.0);
    CHECK(TM().debit(b, "ACC003", 250.0) && TM().credit(b, "ACC004", 250.0));
    CHECK(RM().recoverOnRestart() == 2);
    CHECK(approx(bal("ACC001"), 5000.0) && approx(bal("ACC003"), 10000.0) && approx(bal("ACC004"), 7500.25));
    CHECK(approx(total(), t0));
    CHECK(status(a) == "ROLLED_BACK" && status(b) == "ROLLED_BACK");
}

static void testCommittedSurvivesRecovery() {
    std::cout << "[3] Committed transfers are untouched by recovery\n";
    reset();
    int a = TM().beginTransaction("ACC001", "ACC002", 100.0);
    CHECK(TM().debit(a, "ACC001", 100.0) && TM().credit(a, "ACC002", 100.0) && TM().commit(a));
    int b = TM().beginTransaction("ACC003", "ACC004", 50.0);
    CHECK(TM().debit(b, "ACC003", 50.0));  // crash here
    CHECK(RM().recoverOnRestart() == 1);
    CHECK(approx(bal("ACC001"), 4900.0) && approx(bal("ACC002"), 3600.50));
    CHECK(approx(bal("ACC003"), 10000.0));
    CHECK(status(a) == "COMMITTED");
}

static void testCrashBetweenCommitRecordAndStatus() {
    std::cout << "[4] Crash after the WAL COMMIT record but before the status update\n";
    reset();
    int a = TM().beginTransaction("ACC001", "ACC002", 100.0);
    CHECK(TM().debit(a, "ACC001", 100.0) && TM().credit(a, "ACC002", 100.0));
    CHECK(RM().logOperation(a, "COMMIT", "", "") > 0);  // status still PENDING
    CHECK(status(a) == "PENDING");
    int n = RM().recoverOnRestart();
    CHECK(n == 0);                                   // must not be undone
    CHECK(approx(bal("ACC001"), 4900.0) && approx(bal("ACC002"), 3600.50));
    CHECK(status(a) == "COMMITTED");                 // bookkeeping finished
}

static void testCrashDuringRollback() {
    std::cout << "[5] Crash in the middle of a rollback: recovery finishes the undo\n";
    reset();
    double t0 = total();
    int a = TM().beginTransaction("ACC001", "ACC002", 300.0);
    CHECK(TM().debit(a, "ACC001", 300.0) && TM().credit(a, "ACC002", 300.0));
    // Rollback started: the CREDIT was undone, then the process died before the DEBIT undo.
    DB().executeParameterized("UPDATE Accounts SET balance = ? WHERE id = ?;", {"3500.5", "ACC002"});
    CHECK(RM().recoverOnRestart() == 1);  // no ABORT record was written, so recovery redoes the undo
    CHECK(approx(bal("ACC001"), 5000.0) && approx(bal("ACC002"), 3500.50));
    CHECK(approx(total(), t0));
}

static void testAbortIsLoggedAfterUndo() {
    std::cout << "[6] Normal rollback writes ABORT as the last WAL record\n";
    reset();
    int a = TM().beginTransaction("ACC001", "ACC002", 100.0);
    CHECK(TM().debit(a, "ACC001", 100.0) && TM().credit(a, "ACC002", 100.0));
    CHECK(TM().rollback(a));
    auto logs = DB().queryParameterized("SELECT op FROM TransactionLog WHERE txn_id = ? ORDER BY lsn DESC LIMIT 1;",
                                        {std::to_string(a)});
    CHECK(!logs.empty() && logs[0]["op"] == "ABORT");
    CHECK(approx(bal("ACC001"), 5000.0) && approx(bal("ACC002"), 3500.50));
    CHECK(status(a) == "ABORTED");
    CHECK(RM().recoverOnRestart() == 0);  // already finished, nothing to do
}

static void testRecoveryIsIdempotent() {
    std::cout << "[7] Running recovery twice (crash during recovery) gives the same result\n";
    reset();
    int a = TM().beginTransaction("ACC001", "ACC002", 100.0);
    CHECK(TM().debit(a, "ACC001", 100.0) && TM().credit(a, "ACC002", 100.0));
    // Simulate a crash inside recovery: undo applied, but ROLLED_BACK not recorded yet.
    DB().executeParameterized("UPDATE Accounts SET balance = '5000' WHERE id = 'ACC001';", {});
    DB().executeParameterized("UPDATE Accounts SET balance = '3500.5' WHERE id = 'ACC002';", {});
    CHECK(RM().recoverOnRestart() == 1);
    CHECK(RM().recoverOnRestart() == 0);
    CHECK(approx(bal("ACC001"), 5000.0) && approx(bal("ACC002"), 3500.50));
}

static void testUniqueIdsUnderConcurrency() {
    std::cout << "[8] 8 threads x 40 BEGINs: every txn id and LSN is unique\n";
    reset();
    std::vector<std::vector<int>> ids(8);
    std::vector<std::thread> th;
    for (int t = 0; t < 8; ++t)
        th.emplace_back([&, t] {
            for (int i = 0; i < 40; ++i) ids[t].push_back(TM().beginTransaction("ACC001", "ACC002", 1.0));
        });
    for (auto& x : th) x.join();
    std::set<int> seen;
    for (auto& v : ids) for (int id : v) { CHECK(id > 0); seen.insert(id); }
    CHECK(seen.size() == 320);
    auto r = DB().query("SELECT COUNT(*) AS c, COUNT(DISTINCT lsn) AS d FROM TransactionLog;");
    CHECK(r[0]["c"] == r[0]["d"] && r[0]["c"] == "320");
    CHECK(RM().recoverOnRestart() == 320);
}

// Random crash points over many transfers: after recovery the total is conserved and every
// committed transfer is applied exactly once, every crashed one not at all.
static void testRandomCrashInjection() {
    std::cout << "[9] Random crash injection over 200 transfers\n";
    reset();
    const std::vector<std::string> acc = {"ACC001", "ACC002", "ACC003", "ACC004", "ACC005"};
    double t0 = total();
    std::mt19937 rng(12345);
    std::vector<double> expect;
    for (auto& a : acc) expect.push_back(bal(a));
    for (int i = 0; i < 200; ++i) {
        int f = rng() % 5, t = (f + 1 + rng() % 4) % 5;
        double amt = 1 + rng() % 40;
        if (expect[f] < amt) continue;
        int stage = rng() % 5;  // 0 BEGIN, 1 +DEBIT, 2 +CREDIT, 3 +COMMIT record only, 4 full commit
        int id = TM().beginTransaction(acc[f], acc[t], amt);
        if (stage >= 1) CHECK(TM().debit(id, acc[f], amt));
        if (stage >= 2) CHECK(TM().credit(id, acc[t], amt));
        if (stage == 3) RM().logOperation(id, "COMMIT", "", "");
        if (stage == 4) CHECK(TM().commit(id));
        if (stage >= 3) { expect[f] -= amt; expect[t] += amt; }
        // Crashed transfers (stage < 3) leave debit/credit applied on disk, so the next
        // transfer would build on dirty data. Recover after every crash, like a restart would.
        if (stage < 3) RM().recoverOnRestart();
    }
    RM().recoverOnRestart();
    for (size_t i = 0; i < acc.size(); ++i) CHECK(approx(bal(acc[i]), expect[i]));
    CHECK(approx(total(), t0));
    CHECK(std::stoi(DB().query("SELECT COUNT(*) AS c FROM Transactions WHERE status = 'PENDING';")[0]["c"]) == 0);
}

static bool runSqlFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open " << path << " (run from the project root)\n"; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return DB().execute(ss.str());
}

int main() {
    db::DBConfig cfg = db::DBConfig::fromEnv();
    if (!std::getenv("PRIDEBANK_DB_NAME")) cfg.database = "pride_bank_test";
    if (cfg.database.find("test") == std::string::npos && cfg.database.find("bench") == std::string::npos) {
        std::cerr << "Refusing to run: database '" << cfg.database << "' must contain 'test'.\n";
        return 2;
    }
    if (!DB().init(cfg)) { std::cerr << "cannot connect to MySQL\n"; return 2; }
    if (!runSqlFile("sql/drop_all.sql") || !runSqlFile("sql/schema.sql") || !runSqlFile("sql/seed_data.sql")) return 2;

    testCrashAfterBeginOnly();
    testCrashAfterDebitAndAfterCredit();
    testCommittedSurvivesRecovery();
    testCrashBetweenCommitRecordAndStatus();
    testCrashDuringRollback();
    testAbortIsLoggedAfterUndo();
    testRecoveryIsIdempotent();
    testUniqueIdsUnderConcurrency();
    testRandomCrashInjection();

    std::cout << (g_fail ? "\nSOME TESTS FAILED\n" : "\nALL RECOVERY TESTS PASSED\n");
    return g_fail ? 1 : 0;
}
