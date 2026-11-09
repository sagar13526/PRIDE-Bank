// Integration tests: real MySQL + WAL + locks + Wait-For Graph + Banker's.
// Run from the project root (needs sql/*.sql). Connection comes from PRIDEBANK_DB_*
// env vars; the database name defaults to "pride_bank_test" and MUST contain "test",
// because this program DROPs and recreates all tables.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "concurrency/client_simulator.h"
#include "concurrency/deadlock_handler.h"
#include "concurrency/lock_manager.h"
#include "concurrency/wait_for_graph.h"
#include "db/db_connector.h"
#include "integration/transaction_orchestrator.h"
#include "predict/predictor_service.h"
#include "predict/telemetry.h"
#include "txn/recovery_manager.h"
#include "txn/transaction_manager.h"

using integration::ExecutionMode;
using integration::TransactionOrchestrator;

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) { std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n"; ++g_fail; } \
    } while (0)

static db::DBConnector& DB() { return db::DBConnector::getInstance(); }

static double balanceOf(const std::string& id) {
    auto r = DB().queryParameterized("SELECT balance FROM Accounts WHERE id = ?;", {id});
    return r.empty() ? -1 : std::stod(r[0]["balance"]);
}
static double totalMoney() { return std::stod(DB().query("SELECT SUM(balance) AS s FROM Accounts;")[0]["s"]); }
static int countWhere(const std::string& where) {
    return std::stoi(DB().query("SELECT COUNT(*) AS c FROM Transactions WHERE " + where + ";")[0]["c"]);
}
static bool approx(double a, double b) { return std::fabs(a - b) < 0.01; }

static void resetDb() {
    DB().execute("DELETE FROM DeadlockLog; DELETE FROM TransactionLog; DELETE FROM Transactions;");
    DB().execute("UPDATE Accounts SET balance=5000.00,version=1 WHERE id='ACC001';"
                 "UPDATE Accounts SET balance=3500.50,version=1 WHERE id='ACC002';"
                 "UPDATE Accounts SET balance=10000.00,version=1 WHERE id='ACC003';"
                 "UPDATE Accounts SET balance=7500.25,version=1 WHERE id='ACC004';"
                 "UPDATE Accounts SET balance=2000.00,version=1 WHERE id='ACC005';");
    concurrency::WaitForGraph::getInstance().clear();
}

static void expectClean() {
    CHECK(concurrency::LockManager::getInstance().getLockStateMap().empty());
    CHECK(concurrency::WaitForGraph::getInstance().getActiveEdges().empty());
    CHECK(countWhere("status = 'PENDING'") == 0);
}

static void testWaitForGraphUnit() {
    std::cout << "[1] WaitForGraph cycle logic\n";
    auto& g = concurrency::WaitForGraph::getInstance();
    g.clear();
    std::vector<int> c;
    g.addEdge(1, 2, "A"); g.addEdge(2, 3, "B");
    CHECK(!g.detectCycle(c));
    g.addEdge(3, 1, "C");
    CHECK(g.detectCycle(c) && c.size() == 3);
    CHECK(g.detectCycleFrom(2, c) && c.front() == 2);
    g.removeAllEdgesForTxn(3);
    CHECK(!g.detectCycle(c));
    g.clear();
}

static void testForcedDeadlock() {
    std::cout << "[2] Forced deadlock (REACTIVE): detected, one victim, both transfers finish\n";
    resetDb();
    auto& dh = concurrency::DeadlockHandler::getInstance();
    size_t before = dh.deadlocksResolved();
    double total = totalMoney();

    integration::TransferResult r1, r2;
    std::thread t1([&] { r1 = TransactionOrchestrator::getInstance().executeTransfer("ACC001", "ACC002", 100.0, ExecutionMode::REACTIVE_DFS, 150); });
    std::thread t2([&] { r2 = TransactionOrchestrator::getInstance().executeTransfer("ACC002", "ACC001", 150.0, ExecutionMode::REACTIVE_DFS, 150); });
    t1.join(); t2.join();

    CHECK(dh.deadlocksResolved() - before == 1);
    CHECK(r1.success && r2.success);
    CHECK((r1.deadlockDetected ? 1 : 0) + (r2.deadlockDetected ? 1 : 0) == 1);  // exactly one victim
    CHECK(approx(balanceOf("ACC001"), 5050.00));
    CHECK(approx(balanceOf("ACC002"), 3450.50));
    CHECK(approx(totalMoney(), total));
    CHECK(countWhere("status = 'ABORTED'") == 1);
    CHECK(countWhere("status = 'COMMITTED'") == 2);
    CHECK(std::stoi(DB().query("SELECT COUNT(*) AS c FROM DeadlockLog;")[0]["c"]) == 1);
    expectClean();
}

static void testBankersAvoidsSameScenario() {
    std::cout << "[3] Same scenario under Banker's: no deadlock ever forms\n";
    resetDb();
    auto& dh = concurrency::DeadlockHandler::getInstance();
    size_t before = dh.deadlocksResolved();

    integration::TransferResult r1, r2;
    std::thread t1([&] { r1 = TransactionOrchestrator::getInstance().executeTransfer("ACC001", "ACC002", 100.0, ExecutionMode::BANKERS_AVOIDANCE, 150); });
    std::thread t2([&] { r2 = TransactionOrchestrator::getInstance().executeTransfer("ACC002", "ACC001", 150.0, ExecutionMode::BANKERS_AVOIDANCE, 150); });
    t1.join(); t2.join();

    CHECK(dh.deadlocksResolved() == before);
    CHECK(r1.success && r2.success && !r1.deadlockDetected && !r2.deadlockDetected);
    CHECK(approx(balanceOf("ACC001"), 5050.00));
    CHECK(approx(balanceOf("ACC002"), 3450.50));
    expectClean();
}

static void testStress(ExecutionMode mode, const char* name) {
    std::cout << "[stress] " << name << ": 8 threads x 25 transfers\n";
    resetDb();
    double total = totalMoney();
    size_t before = concurrency::DeadlockHandler::getInstance().deadlocksResolved();

    auto s = concurrency::ClientSimulator::runStressTest(8, 25, mode);
    size_t dl = concurrency::DeadlockHandler::getInstance().deadlocksResolved() - before;
    std::cout << "    committed=" << s.committed << " failed=" << s.failed << " retries=" << s.retries
              << " deadlocks=" << dl << " time=" << s.elapsedMs << "ms\n";

    CHECK(s.committed + s.failed == 200);
    CHECK(approx(totalMoney(), total));          // no lost updates, no lost money
    CHECK(countWhere("status = 'COMMITTED'") == s.committed);
    if (mode == ExecutionMode::BANKERS_AVOIDANCE) CHECK(dl == 0);
    else CHECK(dl > 0);                         // contention on 5 accounts should deadlock at least once
    // WAL sanity: every committed txn has exactly one COMMIT record
    CHECK(std::stoi(DB().query("SELECT COUNT(*) AS c FROM TransactionLog WHERE op='COMMIT';")[0]["c"]) == s.committed);
    expectClean();
}

static void testCrashRecovery() {
    std::cout << "[6] Crash after DEBIT, before COMMIT -> recovery restores balance\n";
    resetDb();
    auto& tm = txn::TransactionManager::getInstance();
    double before = balanceOf("ACC003");
    int id = tm.beginTransaction("ACC003", "ACC004", 200.0);
    CHECK(id > 0 && tm.debit(id, "ACC003", 200.0));
    CHECK(approx(balanceOf("ACC003"), before - 200.0));
    int recovered = txn::RecoveryManager::getInstance().recoverOnRestart();
    CHECK(recovered == 1);
    CHECK(approx(balanceOf("ACC003"), before));
    CHECK(countWhere("status = 'ROLLED_BACK'") == 1);
}

static void testFailuresRollBackCleanly() {
    std::cout << "[7] Insufficient funds / bad input leave no trace\n";
    resetDb();
    auto& o = TransactionOrchestrator::getInstance();
    auto r = o.executeTransfer("ACC005", "ACC001", 999999.0, ExecutionMode::REACTIVE_DFS, 0);
    CHECK(!r.success && approx(balanceOf("ACC005"), 2000.0) && approx(balanceOf("ACC001"), 5000.0));
    CHECK(countWhere("status = 'ABORTED'") == 1);
    r = o.executeTransfer("ACC001", "NOPE", 10.0, ExecutionMode::REACTIVE_DFS, 0);  // unknown destination
    CHECK(!r.success && approx(balanceOf("ACC001"), 5000.0));
    CHECK(!o.executeTransfer("ACC001", "ACC001", 10.0, ExecutionMode::REACTIVE_DFS).success);
    CHECK(!o.executeTransfer("ACC001", "ACC002", -5.0, ExecutionMode::REACTIVE_DFS).success);
    expectClean();
}

static void testValueBasedVictim() {
    std::cout << "[8] VALUE_BASED victim policy: the low-priority transfer is always the victim\n";
    auto& dh = concurrency::DeadlockHandler::getInstance();
    auto& o = TransactionOrchestrator::getInstance();
    dh.setPolicy(concurrency::VictimPolicy::VALUE_BASED);
    int lowVictim = 0, highVictim = 0;
    for (int trial = 0; trial < 6; ++trial) {
        resetDb();
        double total = totalMoney();
        integration::TransferResult lo, hi;
        // Alternate which one starts first, so the low-priority one is sometimes the
        // older and sometimes the younger transaction.
        auto runLo = [&] { lo = o.executeTransfer("ACC001", "ACC002", 100.0, ExecutionMode::REACTIVE_DFS, 150, 3, 1); };
        auto runHi = [&] { hi = o.executeTransfer("ACC002", "ACC001", 150.0, ExecutionMode::REACTIVE_DFS, 150, 3, 9); };
        std::thread a, b;
        if (trial % 2 == 0) { a = std::thread(runLo); std::this_thread::sleep_for(std::chrono::milliseconds(30)); b = std::thread(runHi); }
        else                { a = std::thread(runHi); std::this_thread::sleep_for(std::chrono::milliseconds(30)); b = std::thread(runLo); }
        a.join(); b.join();
        CHECK(lo.success && hi.success);
        if (lo.deadlockDetected) ++lowVictim;
        if (hi.deadlockDetected) ++highVictim;
        CHECK(approx(totalMoney(), total));
        expectClean();
    }
    CHECK(lowVictim == 6);
    CHECK(highVictim == 0);

    // Sanity: the YOUNGEST policy ignores priority (the older transaction survives).
    dh.setPolicy(concurrency::VictimPolicy::YOUNGEST);
    resetDb();
    integration::TransferResult first, second;
    std::thread a([&] { first = o.executeTransfer("ACC001", "ACC002", 100.0, ExecutionMode::REACTIVE_DFS, 150, 3, 9); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::thread b([&] { second = o.executeTransfer("ACC002", "ACC001", 150.0, ExecutionMode::REACTIVE_DFS, 150, 3, 1); });
    a.join(); b.join();
    CHECK(first.success && second.success && second.deadlockDetected && !first.deadlockDetected);
    expectClean();
}

static void testTelemetryAndHybrid() {
    std::cout << "[9] Telemetry sampler + PREDICTIVE_HYBRID with the lock mode flipping under load\n";
    resetDb();
    auto& mon = predict::TelemetryMonitor::getInstance();
    auto& svc = predict::PredictorService::getInstance();
    svc.reset();
    svc.attach();
    mon.clearSamples();
    mon.start(5);

    std::atomic<bool> stop{false};
    std::thread flipper([&] {  // worst case for the lock manager: Banker's and detect transactions mixed
        int v = 0;
        while (!stop) { svc.forceMode(v ^= 1); std::this_thread::sleep_for(std::chrono::milliseconds(13)); }
    });
    double total = totalMoney();
    size_t dlBefore = concurrency::DeadlockHandler::getInstance().deadlocksResolved();
    auto s = concurrency::ClientSimulator::runStressTest(8, 20, ExecutionMode::PREDICTIVE_HYBRID);
    stop = true; flipper.join();
    svc.forceMode(-1);
    mon.stop();
    size_t dl = concurrency::DeadlockHandler::getInstance().deadlocksResolved() - dlBefore;

    CHECK(s.committed + s.failed == 160);          // nothing hung
    CHECK(approx(totalMoney(), total));
    CHECK(countWhere("status = 'COMMITTED'") == s.committed);
    expectClean();

    auto samples = mon.samples();
    int seenWaiting = 0, deadlockSamples = 0, deadlockSum = 0;
    for (auto& x : samples) {
        if (x.waiting > 0) ++seenWaiting;
        if (x.state == 3) ++deadlockSamples;
        deadlockSum += x.newDeadlocks;
        CHECK(x.state >= 0 && x.state <= 3);
    }
    std::cout << "    samples=" << samples.size() << " waiting>0 in " << seenWaiting
              << " deadlock-samples=" << deadlockSamples << " deadlocks=" << dl << "\n";
    CHECK(samples.size() > 50);
    CHECK(seenWaiting > 0);
    CHECK(static_cast<size_t>(deadlockSum) <= dl);   // sampler never invents deadlocks
    CHECK(svc.model().totalTransitions() > 50);
    CHECK(!svc.statusJson().empty());
    svc.reset();
}

static void testOrderedLocking() {
    std::cout << "[10] ORDERED_LOCKING: opposite transfers never deadlock, money conserved\n";
    resetDb();
    double total = totalMoney();
    size_t dl0 = concurrency::DeadlockHandler::getInstance().deadlocksResolved();
    integration::TransferResult a, b;
    auto& o = TransactionOrchestrator::getInstance();
    std::thread ta([&] { a = o.executeTransfer("ACC001", "ACC002", 100.0, ExecutionMode::ORDERED_LOCKING, 100, 3, 5); });
    std::thread tb([&] { b = o.executeTransfer("ACC002", "ACC001", 150.0, ExecutionMode::ORDERED_LOCKING, 100, 3, 5); });
    ta.join(); tb.join();
    CHECK(a.success && b.success);
    CHECK(concurrency::DeadlockHandler::getInstance().deadlocksResolved() == dl0);
    CHECK(approx(totalMoney(), total));
    CHECK(approx(balanceOf("ACC001"), 5050.0) && approx(balanceOf("ACC002"), 3450.50));
    auto s = concurrency::ClientSimulator::runStressTest(8, 15, ExecutionMode::ORDERED_LOCKING);
    CHECK(s.committed + s.failed == 120);
    CHECK(concurrency::DeadlockHandler::getInstance().deadlocksResolved() == dl0);
    expectClean();
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
    if (cfg.database.find("test") == std::string::npos) {
        std::cerr << "Refusing to run: database '" << cfg.database
                  << "' does not contain 'test' and this suite drops all tables.\n";
        return 2;
    }
    if (!DB().init(cfg)) { std::cerr << "cannot connect to MySQL\n"; return 2; }
    if (!runSqlFile("sql/drop_all.sql") || !runSqlFile("sql/schema.sql") || !runSqlFile("sql/seed_data.sql"))
        return 2;

    testWaitForGraphUnit();
    testForcedDeadlock();
    testBankersAvoidsSameScenario();
    testStress(ExecutionMode::REACTIVE_DFS, "REACTIVE_DFS");
    testStress(ExecutionMode::BANKERS_AVOIDANCE, "BANKERS_AVOIDANCE");
    testCrashRecovery();
    testFailuresRollBackCleanly();
    testValueBasedVictim();
    testTelemetryAndHybrid();
    testOrderedLocking();

    std::cout << (g_fail ? "\nSOME TESTS FAILED\n" : "\nALL TESTS PASSED\n");
    return g_fail ? 1 : 0;
}
