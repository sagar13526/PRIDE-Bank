#include "api/dashboard_api.h"
#include "db/db_connector.h"
#include "txn/recovery_manager.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }

static bool runSqlFile(db::DBConnector& db, const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return db.execute(ss.str());
}

static void initializeDatabase() {
    auto& db = db::DBConnector::getInstance();
    db::DBConfig cfg = db::DBConfig::fromEnv();
    std::cout << "[Init] MySQL " << cfg.user << "@" << cfg.host << ":" << cfg.port
              << "/" << cfg.database << std::endl;

    if (!db.init(cfg)) {
        std::cerr << "Fatal: cannot connect to MySQL. Is the server running, and are the\n"
                     "BANKSAFE_DB_HOST / _PORT / _USER / _PASSWORD / _NAME variables correct?"
                  << std::endl;
        std::exit(1);
    }

    // schema.sql is idempotent (CREATE TABLE IF NOT EXISTS), so data survives restarts.
    if (!runSqlFile(db, "sql/schema.sql")) {
        std::cerr << "Fatal: could not apply sql/schema.sql (run from the project root)." << std::endl;
        std::exit(1);
    }
    std::cout << "[Init] Schema ready." << std::endl;

    auto rows = db.query("SELECT COUNT(*) AS cnt FROM Accounts;");
    if (rows.empty() || rows[0]["cnt"] == "0") {
        if (!runSqlFile(db, "sql/seed_data.sql")) {
            std::cerr << "Fatal: could not apply sql/seed_data.sql." << std::endl;
            std::exit(1);
        }
        std::cout << "[Init] Seed data inserted." << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "==========================================================\n"
              << "          PRIDE-Bank - Phase 2 Baseline\n"
              << "      Team Tech Titans (OS & DBMS PBL Integration)\n"
              << "==========================================================" << std::endl;

    const int apiPort = argc > 1 ? std::atoi(argv[1]) : 8080;

    initializeDatabase();

    // Crash recovery: undo every transaction that never committed or aborted.
    txn::RecoveryManager::getInstance().recoverOnRestart();

    if (!api::DashboardAPI::getInstance().startServer(apiPort)) return 1;

    std::cout << "\nEndpoints:\n"
                 "  GET  /api/accounts | /api/transactions | /api/wait-for-graph\n"
                 "  GET  /api/wal-logs | /api/deadlock-logs\n"
                 "  POST /api/transfer | /api/stress-test | /api/force-deadlock\n"
                 "  POST /api/simulate-crash | /api/recover | /api/reset\n"
                 "\nPress Ctrl+C to stop." << std::endl;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nShutting down..." << std::endl;
    api::DashboardAPI::getInstance().stopServer();
    return 0;
}
