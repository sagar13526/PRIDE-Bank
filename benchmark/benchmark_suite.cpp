// PRIDE-Bank benchmark. Compares four strategies on the same random-transfer workload:
//   ORDERED           deadlock prevention: always lock the lower account id first (no cycles possible)
//   NATIVE_MYSQL      InnoDB's own row locks: SELECT ... FOR UPDATE, one connection per thread, InnoDB's
//                     built-in deadlock detector, retry on error 1213 (no WAL/Banker's/WFG of ours)
//   DETECT_YOUNGEST   Wait-For Graph detection, victim = youngest transaction
//   DETECT_VALUE      Wait-For Graph detection, victim = lowest abort cost (priority, retries, age ...)
//   BANKERS           Banker's avoidance (never enters an unsafe state)
//   HYBRID            Markov predictor switches between detection and Banker's
//
// It RESETS accounts and logs before every run, so it only runs against a database whose name
// contains "bench" or "test" (default: pride_bank_bench). Run from the project root.
//
//   ./benchmark_suite [reps=3] [experiments=ABC] [out=benchmark_results]
//        A threads, B accounts, C hot spot, D hold time, E hybrid thresholds; writes <out>.csv/.json
//   ./benchmark_suite --collect [runs=81] [out=telemetry] [seedBase=1000] [mixed|light]
//        data collection for the offline predictor validation: many detect-mode runs with
//        varying load; writes <out>_samples.csv (telemetry) and <out>_runs.csv (one row per run)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <atomic>
#include <iomanip>
#include <mutex>
#include <random>
#include <thread>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "concurrency/client_simulator.h"
#include "concurrency/deadlock_handler.h"
#include "concurrency/wait_for_graph.h"
#include "db/db_connector.h"
#include "integration/transaction_orchestrator.h"
#include "predict/predictor_service.h"
#include "predict/telemetry.h"

using concurrency::VictimPolicy;
using concurrency::Workload;
using integration::ExecutionMode;

static db::DBConnector& DB() { return db::DBConnector::getInstance(); }

static bool runSqlFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open " << path << " (run from the project root)\n"; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return DB().execute(ss.str());
}

static std::string accId(int i) {
    char b[16];
    std::snprintf(b, sizeof b, "ACC%03d", i + 1);
    return b;
}

static void ensureAccounts(int n) {
    for (int i = 5; i < n; ++i)
        DB().executeParameterized(
            "INSERT IGNORE INTO Accounts (id, owner_name, balance, version) VALUES (?, ?, 5000.00, 1);",
            {accId(i), "Bench account " + std::to_string(i + 1)});
}

static void resetDb() {
    DB().execute("DELETE FROM DeadlockLog; DELETE FROM TransactionLog; DELETE FROM Transactions;");
    DB().execute("UPDATE Accounts SET balance=5000.00,version=1 WHERE id NOT IN ('ACC001','ACC002','ACC003','ACC004','ACC005');"
                 "UPDATE Accounts SET balance=5000.00,version=1 WHERE id='ACC001';"
                 "UPDATE Accounts SET balance=3500.50,version=1 WHERE id='ACC002';"
                 "UPDATE Accounts SET balance=10000.00,version=1 WHERE id='ACC003';"
                 "UPDATE Accounts SET balance=7500.25,version=1 WHERE id='ACC004';"
                 "UPDATE Accounts SET balance=2000.00,version=1 WHERE id='ACC005';");
    concurrency::WaitForGraph::getInstance().clear();
}

static double totalMoney() {
    auto r = DB().query("SELECT SUM(balance) AS s FROM Accounts;");
    return r.empty() ? -1 : std::stod(r[0]["s"]);
}

static db::DBConfig g_cfg;

// Baseline: the same workload on InnoDB's own locking, one connection per thread.
static concurrency::StressStats runNative(const Workload& w, std::atomic<int>& deadlocks) {
    const int nAcc = std::max(2, w.accounts);
    const int hotN = std::max(2, std::min(w.hotAccounts, nAcc));
    std::atomic<int> committed{0}, failed{0}, retries{0}, victims{0};
    std::mutex latMu;
    std::vector<double> lat;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> th;
    for (int i = 0; i < w.threads; ++i)
        th.emplace_back([&, i] {
            MYSQL* c = mysql_init(nullptr);
            if (!mysql_real_connect(c, g_cfg.host.c_str(), g_cfg.user.c_str(), g_cfg.password.c_str(),
                                    g_cfg.database.c_str(), g_cfg.port, nullptr, 0)) {
                std::cerr << "native connect failed: " << mysql_error(c) << "\n";
                failed += w.transfersPerThread;
                return;
            }
            std::mt19937 rng(w.seed ? w.seed * 7919u + static_cast<unsigned>(i) : std::random_device{}() + i);
            std::uniform_real_distribution<double> u(0, 1), amtD(10.0, 250.0);
            std::uniform_int_distribution<int> allD(0, nAcc - 1), hotD(0, hotN - 1);
            auto pick = [&] { return u(rng) < w.hotProb ? hotD(rng) : allD(rng); };
            std::vector<double> mine;
            auto q = [&](const std::string& sql) { return mysql_query(c, sql.c_str()) == 0; };
            for (int t = 0; t < w.transfersPerThread; ++t) {
                int f = pick(), to = pick();
                while (to == f) to = pick();
                const double amount = std::round(amtD(rng) * 100.0) / 100.0;
                const auto start = std::chrono::steady_clock::now();
                bool ok = false;
                for (int attempt = 0; attempt <= 3; ++attempt) {
                    bool dead = false, insufficient = false;
                    auto fail1213 = [&] { if (mysql_errno(c) == 1213) { dead = true; ++deadlocks; ++victims; } };
                    if (!q("START TRANSACTION")) break;
                    double bal = 0;
                    if (!q("SELECT balance FROM Accounts WHERE id='" + accId(f) + "' FOR UPDATE")) fail1213();
                    else {
                        MYSQL_RES* r = mysql_store_result(c);
                        MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
                        if (row) bal = std::atof(row[0]);
                        if (r) mysql_free_result(r);
                        if (w.holdDelayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(w.holdDelayMs));
                        if (!q("SELECT balance FROM Accounts WHERE id='" + accId(to) + "' FOR UPDATE")) fail1213();
                        else {
                            MYSQL_RES* r2 = mysql_store_result(c);
                            if (r2) mysql_free_result(r2);
                            if (bal < amount) insufficient = true;
                            else if (!q("UPDATE Accounts SET balance=balance-" + std::to_string(amount) + ",version=version+1 WHERE id='" + accId(f) + "'") ||
                                     !q("UPDATE Accounts SET balance=balance+" + std::to_string(amount) + ",version=version+1 WHERE id='" + accId(to) + "'"))
                                fail1213();
                        }
                    }
                    if (dead) {
                        q("ROLLBACK");
                        ++retries;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5 + rng() % 20));
                        continue;
                    }
                    if (insufficient || mysql_errno(c) != 0) { q("ROLLBACK"); break; }
                    if (q("COMMIT")) ok = true; else q("ROLLBACK");
                    break;
                }
                (ok ? committed : failed)++;
                mine.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            }
            mysql_close(c);
            std::lock_guard<std::mutex> lk(latMu);
            lat.insert(lat.end(), mine.begin(), mine.end());
        });
    for (auto& x : th) x.join();
    concurrency::StressStats s;
    s.committed = committed; s.failed = failed; s.retries = retries; s.deadlockVictims = victims;
    s.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    s.latenciesMs = std::move(lat);
    return s;
}

struct Strategy {
    const char* name; ExecutionMode mode; VictimPolicy policy;
    bool native = false; double hi = 0, lo = 0;  // hi/lo > 0: override the hybrid thresholds
};
static const Strategy kStrategies[] = {
    {"DETECT_YOUNGEST", ExecutionMode::REACTIVE_DFS, VictimPolicy::YOUNGEST},
    {"DETECT_VALUE", ExecutionMode::REACTIVE_DFS, VictimPolicy::VALUE_BASED},
    {"BANKERS", ExecutionMode::BANKERS_AVOIDANCE, VictimPolicy::YOUNGEST},
    {"HYBRID", ExecutionMode::PREDICTIVE_HYBRID, VictimPolicy::VALUE_BASED},
    {"ORDERED", ExecutionMode::ORDERED_LOCKING, VictimPolicy::YOUNGEST},
    {"NATIVE_MYSQL", ExecutionMode::REACTIVE_DFS, VictimPolicy::YOUNGEST, true},
};

struct Run {
    double ms = 0, p50 = 0, p95 = 0, p99 = 0;
    int committed = 0, failed = 0, retries = 0, victims = 0, deadlocks = 0, switches = 0;
    bool conserved = true;
};

static Run runOnce(Workload w, const Strategy& st) {
    resetDb();
    w.mode = st.mode;
    concurrency::DeadlockHandler::getInstance().setPolicy(st.policy);
    auto& svc = predict::PredictorService::getInstance();
    const double before = totalMoney();
    const size_t dl0 = concurrency::DeadlockHandler::getInstance().deadlocksResolved();
    const auto sw0 = svc.switches();
    std::atomic<int> nativeDl{0};
    if (st.hi > 0) {
        auto c = svc.config();
        c.hi = st.hi; c.lo = st.lo;
        svc.setConfig(c);
    }
    auto s = st.native ? runNative(w, nativeDl) : concurrency::ClientSimulator::run(w);
    if (st.hi > 0) svc.setConfig(predict::HybridConfig());
    Run r;
    r.ms = s.elapsedMs;
    r.committed = s.committed; r.failed = s.failed; r.retries = s.retries; r.victims = s.deadlockVictims;
    r.p50 = s.percentile(0.50); r.p95 = s.percentile(0.95); r.p99 = s.percentile(0.99);
    r.deadlocks = st.native ? nativeDl.load() : static_cast<int>(concurrency::DeadlockHandler::getInstance().deadlocksResolved() - dl0);
    r.switches = static_cast<int>(svc.switches() - sw0);
    r.conserved = std::fabs(totalMoney() - before) < 0.01;
    return r;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

struct Row {
    std::string experiment, strategy;
    int threads, per, accounts, reps, hold;
    double hot;
    std::vector<double> msAll;
    double ms, tps, p50, p95, p99, committed, failed, retries, victims, deadlocks, switches;
    bool conserved;
};

static bool g_allConserved = true;

static Row measure(const std::string& exp, const Workload& w, const Strategy& st, int reps) {
    std::vector<double> ms, tps, p50, p95, p99, com, fail, ret, vic, dl, sw;
    bool cons = true;
    for (int i = 0; i < reps; ++i) {
        Run r = runOnce(w, st);
        ms.push_back(r.ms);
        tps.push_back(r.ms > 0 ? r.committed / (r.ms / 1000.0) : 0);
        p50.push_back(r.p50); p95.push_back(r.p95); p99.push_back(r.p99);
        com.push_back(r.committed); fail.push_back(r.failed); ret.push_back(r.retries);
        vic.push_back(r.victims); dl.push_back(r.deadlocks); sw.push_back(r.switches);
        cons = cons && r.conserved;
    }
    g_allConserved = g_allConserved && cons;
    std::string label = st.name;
    if (st.hi > 0) {
        char b[48];
        std::snprintf(b, sizeof b, "HYBRID_%.2f_%.2f", st.hi, st.lo);
        label = b;
    }
    return {exp, label, w.threads, w.transfersPerThread, w.accounts, reps, w.holdDelayMs, w.hotProb, ms,
            median(ms), median(tps), median(p50), median(p95), median(p99), median(com), median(fail),
            median(ret), median(vic), median(dl), median(sw), cons};
}

static void printRows(const std::string& title, const std::vector<Row>& rows) {
    std::cout << "\n=== " << title << " ===\n"
              << std::left << std::setw(22) << "strategy" << std::right << std::setw(5) << "thr"
              << std::setw(6) << "accts" << std::setw(6) << "hot" << std::setw(5) << "hold" << std::setw(9) << "ms" << std::setw(8) << "txn/s"
              << std::setw(8) << "p50" << std::setw(8) << "p95" << std::setw(8) << "p99" << std::setw(7) << "fail"
              << std::setw(8) << "retry" << std::setw(7) << "dlk" << std::setw(7) << "switch" << std::setw(7) << "money" << "\n";
    for (const auto& r : rows)
        std::cout << std::left << std::setw(22) << r.strategy << std::right << std::setw(5) << r.threads
                  << std::setw(6) << r.accounts << std::setw(6) << std::fixed << std::setprecision(1) << r.hot
                  << std::setprecision(0) << std::setw(5) << r.hold << std::setw(9) << r.ms << std::setprecision(1) << std::setw(8) << r.tps
                  << std::setprecision(0) << std::setw(8) << r.p50 << std::setw(8) << r.p95 << std::setw(8) << r.p99
                  << std::setw(7) << r.failed << std::setw(8) << r.retries << std::setw(7) << r.deadlocks
                  << std::setw(7) << r.switches << std::setw(7) << (r.conserved ? "ok" : "LOST") << "\n";
}

static void writeResults(const std::vector<Row>& rows, const std::string& out) {
    std::ofstream csv(out + ".csv");
    csv << "experiment,strategy,threads,transfers_per_thread,accounts,hot_prob,hold_ms,reps,median_ms,min_ms,max_ms,"
           "median_tps,p50_ms,p95_ms,p99_ms,committed,failed,retries,victims,deadlocks,switches,money_conserved\n";
    for (const auto& r : rows) {
        const double mn = *std::min_element(r.msAll.begin(), r.msAll.end());
        const double mx = *std::max_element(r.msAll.begin(), r.msAll.end());
        csv << r.experiment << ',' << r.strategy << ',' << r.threads << ',' << r.per << ',' << r.accounts << ','
            << r.hot << ',' << r.hold << ',' << r.reps << ',' << r.ms << ',' << mn << ',' << mx << ',' << r.tps << ','
            << r.p50 << ',' << r.p95 << ',' << r.p99 << ',' << r.committed << ',' << r.failed << ',' << r.retries << ','
            << r.victims << ',' << r.deadlocks << ',' << r.switches << ',' << (r.conserved ? 1 : 0) << '\n';
    }
    std::ofstream js(out + ".json");
    js << "[\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        js << "  {\"experiment\":\"" << r.experiment << "\",\"strategy\":\"" << r.strategy << "\",\"threads\":" << r.threads
           << ",\"transfersPerThread\":" << r.per << ",\"accounts\":" << r.accounts << ",\"hotProb\":" << r.hot
           << ",\"holdMs\":" << r.hold << ",\"reps\":" << r.reps << ",\"elapsedMs\":" << r.ms << ",\"msAll\":[";
        for (size_t k = 0; k < r.msAll.size(); ++k) js << (k ? "," : "") << r.msAll[k];
        js << "],\"throughputTps\":" << r.tps
           << ",\"p50Ms\":" << r.p50 << ",\"p95Ms\":" << r.p95 << ",\"p99Ms\":" << r.p99
           << ",\"committed\":" << r.committed << ",\"failed\":" << r.failed << ",\"retries\":" << r.retries
           << ",\"victims\":" << r.victims << ",\"deadlocks\":" << r.deadlocks << ",\"switches\":" << r.switches
           << ",\"moneyConserved\":" << (r.conserved ? "true" : "false") << "}" << (i + 1 < rows.size() ? "," : "") << "\n";
    }
    js << "]\n";
}

// ---------------------------------------------------------------- data collection
static int collect(int runs, const std::string& out, unsigned seedBase, bool light) {
    auto& mon = predict::TelemetryMonitor::getInstance();
    mon.start(10);
    std::ofstream rf(out + "_runs.csv");
    rf << "run,threads,accounts,hot_prob,transfers_per_thread,seed,deadlocks,elapsed_ms,committed,failed\n";
    const int threadsMixed[] = {4, 8, 16}, accMixed[] = {5, 10, 20};
    const int threadsLight[] = {2, 4, 8}, accLight[] = {10, 20, 50};
    const double hotMixed[] = {0.0, 0.5, 0.9}, hotLight[] = {0.0, 0.3, 0.6};
    const int* threadsV = light ? threadsLight : threadsMixed;
    const int* accV = light ? accLight : accMixed;
    const double* hotV = light ? hotLight : hotMixed;
    int run = 0;
    for (int i = 0; run < runs; ++i) {
        Workload w;
        w.threads = threadsV[i % 3];
        w.accounts = accV[(i / 3) % 3];
        w.hotProb = hotV[(i / 9) % 3];
        w.transfersPerThread = 20;
        w.randomPriority = true;
        w.seed = seedBase + static_cast<unsigned>(run);
        ++run;
        resetDb();
        concurrency::DeadlockHandler::getInstance().setPolicy(VictimPolicy::YOUNGEST);
        w.mode = ExecutionMode::REACTIVE_DFS;
        const size_t dl0 = concurrency::DeadlockHandler::getInstance().deadlocksResolved();
        mon.clearSamples();
        auto s = concurrency::ClientSimulator::run(w);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const size_t dl = concurrency::DeadlockHandler::getInstance().deadlocksResolved() - dl0;
        mon.exportCsv(out + "_samples.csv", run, run > 1);
        rf << run << ',' << w.threads << ',' << w.accounts << ',' << w.hotProb << ',' << w.transfersPerThread << ','
           << w.seed << ',' << dl << ',' << s.elapsedMs << ',' << s.committed << ',' << s.failed << '\n';
        rf.flush();
        std::cout << "[collect] run " << run << "/" << runs << " threads=" << w.threads << " accounts=" << w.accounts
                  << " hot=" << w.hotProb << " deadlocks=" << dl << std::endl;
    }
    mon.stop();
    std::cout << "Wrote " << out << "_samples.csv and " << out << "_runs.csv\n";
    return 0;
}

int main(int argc, char** argv) {
    const bool collectMode = argc > 1 && std::strcmp(argv[1], "--collect") == 0;
    const int arg1 = argc > (collectMode ? 2 : 1) ? std::max(1, std::atoi(argv[collectMode ? 2 : 1])) : (collectMode ? 81 : 3);

    db::DBConfig cfg = db::DBConfig::fromEnv();
    if (!std::getenv("PRIDEBANK_DB_NAME")) cfg.database = "pride_bank_bench";
    if (cfg.database.find("bench") == std::string::npos && cfg.database.find("test") == std::string::npos) {
        std::cerr << "Refusing to run: database '" << cfg.database
                  << "' must contain 'bench' or 'test' because every run resets all accounts and logs.\n";
        return 2;
    }
    g_cfg = cfg;
    if (!DB().init(cfg)) { std::cerr << "cannot connect to MySQL\n"; return 2; }
    if (!runSqlFile("sql/schema.sql")) return 2;
    auto cnt = DB().query("SELECT COUNT(*) AS c FROM Accounts;");
    if ((cnt.empty() || cnt[0]["c"] == "0") && !runSqlFile("sql/seed_data.sql")) return 2;
    ensureAccounts(100);

    if (collectMode) return collect(arg1, argc > 3 ? argv[3] : "telemetry", argc > 4 ? static_cast<unsigned>(std::atoi(argv[4])) : 1000u,
                       argc > 5 && std::strcmp(argv[5], "light") == 0);

    const int reps = arg1;
    const std::string exps = argc > 2 ? argv[2] : "ABC";
    const std::string outName = argc > 3 ? argv[3] : "benchmark_results";
    auto& mon = predict::TelemetryMonitor::getInstance();
    predict::PredictorService::getInstance().attach();
    mon.start(10);

    // The predictor learns online, so it is warmed up with unmeasured detect-mode load first.
    {
        std::cout << "[warm-up] training the Markov predictor on detect-mode load...\n";
        Workload w; w.threads = 8; w.transfersPerThread = 20; w.mode = ExecutionMode::REACTIVE_DFS;
        for (int i = 0; i < 4; ++i) runOnce(w, kStrategies[0]);
        for (int i = 0; i < 2; ++i) { w.threads = 16; runOnce(w, kStrategies[0]); }
    }

    std::vector<Row> all;
    auto has = [&](char c) { return exps.find(c) != std::string::npos; };
    auto section = [&](const char* title, const char* name, std::vector<Workload> ws, const std::vector<Strategy>& sts) {
        std::vector<Row> rows;
        for (const auto& w : ws)
            for (const auto& st : sts) rows.push_back(measure(name, w, st, reps));
        printRows(title, rows);
        all.insert(all.end(), rows.begin(), rows.end());
    };
    std::vector<Strategy> six(std::begin(kStrategies), std::end(kStrategies));
    auto base = [] { Workload w; w.threads = 8; w.transfersPerThread = 25; w.accounts = 5; w.randomPriority = true; return w; };

    if (has('A')) {
        std::vector<Workload> ws;
        for (int th : {2, 4, 8, 16}) { auto w = base(); w.threads = th; ws.push_back(w); }
        section("A. Thread scaling (5 accounts)", "thread_scaling", ws, six);
    }
    if (has('B')) {
        std::vector<Workload> ws;
        for (int acc : {5, 20, 100}) { auto w = base(); w.accounts = acc; ws.push_back(w); }
        section("B. Account count (8 threads)", "account_scaling", ws, six);
    }
    if (has('C')) {
        std::vector<Workload> ws;
        for (double hot : {0.0, 0.5, 0.9}) { auto w = base(); w.accounts = 20; w.hotProb = hot; w.hotAccounts = 2; ws.push_back(w); }
        section("C. Hot spot: share of endpoints on 2 of 20 accounts (8 threads)", "hot_spot", ws, six);
    }
    if (has('D')) {
        std::vector<Workload> ws;
        for (int hold : {0, 5, 20, 50}) { auto w = base(); w.transfersPerThread = 20; w.holdDelayMs = hold; ws.push_back(w); }
        section("D. Hold time between the two lock requests (8 threads, 5 accounts)", "hold_time", ws, six);
    }
    if (has('E')) {
        std::vector<Workload> ws;
        { auto w = base(); w.threads = 16; ws.push_back(w); }
        std::vector<Strategy> sts = {kStrategies[1], kStrategies[2]};
        for (auto hl : std::vector<std::pair<double, double>>{{0.3, 0.1}, {0.45, 0.25}, {0.6, 0.35}, {0.8, 0.5}, {0.95, 0.7}}) {
            Strategy h = kStrategies[3];
            h.hi = hl.first; h.lo = hl.second;
            sts.push_back(h);
        }
        section("E. Hybrid thresholds (16 threads, 5 accounts)", "hybrid_thresholds", ws, sts);
    }

    mon.stop();
    writeResults(all, outName);
    std::cout << "\nWrote " << outName << ".csv and " << outName << ".json (median of " << reps << " runs)\n"
              << (g_allConserved ? "Money was conserved in every run.\n" : "WARNING: money was NOT conserved in some run!\n");
    return g_allConserved ? 0 : 1;
}
