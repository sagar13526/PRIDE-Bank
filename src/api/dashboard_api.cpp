#include "api/dashboard_api.h"
#include "httplib.h"
#include "nlohmann/json.hpp"

#include "concurrency/bankers_algorithm.h"
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

#include <atomic>
#include <iostream>

using json = nlohmann::json;

namespace api {

namespace {

std::atomic<bool> g_stressRunning{false};

void setCorsHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}


void sendJson(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

}  // namespace

DashboardAPI& DashboardAPI::getInstance() {
    static DashboardAPI instance;
    return instance;
}

DashboardAPI::~DashboardAPI() { stopServer(); }

bool DashboardAPI::startServer(int port) {
    if (running_) return true;

    server_ = std::make_unique<httplib::Server>();
    auto& svr = *server_;

    // CORS preflight
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        res.status = 200;
    });

    // 1. GET /api/accounts
    svr.Get("/api/accounts", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        json j = json::array();
        for (const auto& a : txn::TransactionManager::getInstance().getAccounts()) {
            j.push_back({{"id", a.count("id") ? a.at("id") : ""},
                         {"owner_name", a.count("owner_name") ? a.at("owner_name") : ""},
                         {"balance", a.count("balance") ? std::stod(a.at("balance")) : 0.0},
                         {"version", a.count("version") ? std::stoi(a.at("version")) : 1}});
        }
        sendJson(res, j);
    });

    // 2. GET /api/transactions
    svr.Get("/api/transactions", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        json j = json::array();
        for (const auto& t : txn::TransactionManager::getInstance().getTransactions(50)) {
            j.push_back({{"txn_id", t.count("txn_id") ? std::stoi(t.at("txn_id")) : 0},
                         {"from_acct", t.count("from_acct") ? t.at("from_acct") : ""},
                         {"to_acct", t.count("to_acct") ? t.at("to_acct") : ""},
                         {"amount", t.count("amount") ? std::stod(t.at("amount")) : 0.0},
                         {"status", t.count("status") ? t.at("status") : ""},
                         {"created_at", t.count("created_at") ? t.at("created_at") : ""}});
        }
        sendJson(res, j);
    });

    // 3. GET /api/wait-for-graph   ("locks" maps account -> txn id holding it)
    svr.Get("/api/wait-for-graph", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        auto& wfg = concurrency::WaitForGraph::getInstance();
        auto edges = wfg.getActiveEdges();
        auto nodes = wfg.getActiveNodes();
        auto lockMap = concurrency::LockManager::getInstance().getLockStateMap();

        std::vector<int> cycle;
        bool hasCycle = wfg.detectCycle(cycle);

        json jNodes = json::array();
        for (int n : nodes) jNodes.push_back(n);
        json jEdges = json::array();
        for (const auto& e : edges)
            jEdges.push_back({{"waitingTxn", e.waitingTxnId},
                              {"holdingTxn", e.holdingTxnId},
                              {"account", e.accountId}});
        json jLocks = json::object();
        for (const auto& kv : lockMap) jLocks[kv.first] = kv.second;

        sendJson(res, {{"nodes", jNodes},
                       {"edges", jEdges},
                       {"locks", jLocks},
                       {"hasCycle", hasCycle},
                       {"cycle", cycle},
                       {"deadlocksResolved", concurrency::DeadlockHandler::getInstance().deadlocksResolved()}});
    });

    // 4. GET /api/wal-logs
    svr.Get("/api/wal-logs", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        json j = json::array();
        for (const auto& l : txn::RecoveryManager::getInstance().getAllLogs(50)) {
            j.push_back({{"lsn", l.lsn}, {"txn_id", l.txn_id}, {"op", l.op},
                         {"before_val", l.before_val}, {"after_val", l.after_val}, {"ts", l.ts}});
        }
        sendJson(res, j);
    });

    // 5. GET /api/deadlock-logs
    svr.Get("/api/deadlock-logs", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        json j = json::array();
        for (const auto& l : concurrency::DeadlockHandler::getInstance().getDeadlockLogs(50)) {
            j.push_back({{"dl_id", l.count("dl_id") ? std::stoi(l.at("dl_id")) : 0},
                         {"victim_txn_id", l.count("victim_txn_id") ? std::stoi(l.at("victim_txn_id")) : 0},
                         {"cycle_accounts", l.count("cycle_accounts") ? l.at("cycle_accounts") : ""},
                         {"detected_at", l.count("detected_at") ? l.at("detected_at") : ""}});
        }
        sendJson(res, j);
    });

    // 6. POST /api/transfer   body: {from, to, amount, mode?}
    svr.Post("/api/transfer", [](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = json::parse(req.body);
            std::string from = body.value("from", "");
            std::string to = body.value("to", "");
            double amount = body.value("amount", 0.0);
            integration::ExecutionMode mode;
            if (!integration::parseMode(body.value("mode", "REACTIVE_DFS"), mode))
                return sendJson(res, {{"error", "mode must be REACTIVE_DFS, BANKERS_AVOIDANCE or PREDICTIVE_HYBRID"}}, 400);

            int priority = body.value("priority", 5);
            if (priority < 0 || priority > 9)
                return sendJson(res, {{"error", "priority must be 0-9"}}, 400);

            auto r = integration::TransactionOrchestrator::getInstance().executeTransfer(
                from, to, amount, mode, 20, 3, priority);
            sendJson(res, {{"success", r.success}, {"txnId", r.txnId}, {"message", r.message},
                           {"deadlockDetected", r.deadlockDetected}, {"victimTxnId", r.victimTxnId},
                           {"retries", r.retries}, {"latencyMs", r.latencyMs},
                           {"usedBankers", r.usedBankers}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", e.what()}}, 400);
        }
    });

    // 7. POST /api/stress-test   body: {threads, transfers, mode}
    svr.Post("/api/stress-test", [](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = json::parse(req.body);
            int threads = body.value("threads", 4);
            int transfers = body.value("transfers", 10);
            integration::ExecutionMode mode;
            if (!integration::parseMode(body.value("mode", "REACTIVE_DFS"), mode))
                return sendJson(res, {{"error", "mode must be REACTIVE_DFS, BANKERS_AVOIDANCE or PREDICTIVE_HYBRID"}}, 400);
            if (threads < 1 || threads > 64 || transfers < 1 || transfers > 1000)
                return sendJson(res, {{"error", "threads must be 1-64 and transfers 1-1000"}}, 400);

            bool expected = false;
            if (!g_stressRunning.compare_exchange_strong(expected, true))
                return sendJson(res, {{"error", "A stress test is already running."}}, 409);

            std::thread([threads, transfers, mode]() {
                concurrency::ClientSimulator::runStressTest(threads, transfers, mode);
                g_stressRunning = false;
            }).detach();
            sendJson(res, {{"status", "started"}, {"message", "Stress test launched in background."}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", e.what()}}, 400);
        }
    });

    // 7b. GET/POST /api/policy: deadlock victim policy (YOUNGEST or VALUE_BASED)
    auto policyJson = []() {
        auto& dh = concurrency::DeadlockHandler::getInstance();
        auto w = dh.weights();
        return json{{"victimPolicy", concurrency::DeadlockHandler::policyName(dh.policy())},
                    {"weights", {{"priority", w.priority}, {"retries", w.retries},
                                 {"agePer100ms", w.agePer100ms}, {"amountPer100", w.amountPer100},
                                 {"held", w.held}}}};
    };
    svr.Get("/api/policy", [policyJson](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        sendJson(res, policyJson());
    });
    svr.Post("/api/policy", [policyJson](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = json::parse(req.body);
            auto& dh = concurrency::DeadlockHandler::getInstance();
            if (body.contains("victimPolicy")) {
                concurrency::VictimPolicy p;
                if (!concurrency::DeadlockHandler::parsePolicy(body.value("victimPolicy", ""), p))
                    return sendJson(res, {{"error", "victimPolicy must be YOUNGEST or VALUE_BASED"}}, 400);
                dh.setPolicy(p);
            }
            if (body.contains("weights") && body["weights"].is_object()) {
                auto w = dh.weights();
                const auto& j = body["weights"];
                w.priority = j.value("priority", w.priority);
                w.retries = j.value("retries", w.retries);
                w.agePer100ms = j.value("agePer100ms", w.agePer100ms);
                w.amountPer100 = j.value("amountPer100", w.amountPer100);
                w.held = j.value("held", w.held);
                dh.setWeights(w);
            }
            sendJson(res, policyJson());
        } catch (const std::exception& e) {
            sendJson(res, {{"error", e.what()}}, 400);
        }
    });

    // 7c. Predictor and telemetry
    svr.Get("/api/predictor", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        res.set_content(predict::PredictorService::getInstance().statusJson(), "application/json");
    });
    svr.Post("/api/predictor/config", [](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = json::parse(req.body);
            auto& svc = predict::PredictorService::getInstance();
            auto c = svc.config();
            c.horizon = body.value("horizon", c.horizon);
            c.hi = body.value("hi", c.hi);
            c.lo = body.value("lo", c.lo);
            c.minSamples = body.value("minSamples", c.minSamples);
            c.enabled = body.value("enabled", c.enabled);
            if (c.hi <= 0 || c.hi > 1 || c.lo < 0 || c.lo > c.hi || c.horizon < 1 || c.horizon > 1000)
                return sendJson(res, {{"error", "need 0 <= lo <= hi <= 1 and 1 <= horizon <= 1000"}}, 400);
            svc.setConfig(c);
            res.set_content(svc.statusJson(), "application/json");
        } catch (const std::exception& e) {
            sendJson(res, {{"error", e.what()}}, 400);
        }
    });
    svr.Post("/api/predictor/reset", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        predict::PredictorService::getInstance().reset();
        res.set_content(predict::PredictorService::getInstance().statusJson(), "application/json");
    });
    svr.Get("/api/telemetry", [](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        size_t n = 100;
        if (req.has_param("n")) { try { n = std::min<size_t>(2000, std::stoul(req.get_param_value("n"))); } catch (...) {} }
        auto& mon = predict::TelemetryMonitor::getInstance();
        json arr = json::array();
        for (const auto& x : mon.samples(n))
            arr.push_back({{"t", x.tSec}, {"active", x.active}, {"waiting", x.waiting},
                           {"maxChain", x.maxChain}, {"locked", x.locked},
                           {"newDeadlocks", x.newDeadlocks}, {"state", x.state},
                           {"stateName", predict::stateName(x.state)}});
        sendJson(res, {{"running", mon.running()}, {"intervalMs", mon.intervalMs()}, {"samples", arr}});
    });
    svr.Post("/api/telemetry", [](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = req.body.empty() ? json::object() : json::parse(req.body);
            auto& mon = predict::TelemetryMonitor::getInstance();
            std::string action = body.value("action", "start");
            if (action == "stop") mon.stop();
            else if (action == "start") {
                int ms = body.value("intervalMs", 10);
                if (ms < 1 || ms > 1000) return sendJson(res, {{"error", "intervalMs must be 1-1000"}}, 400);
                predict::PredictorService::getInstance().attach();
                mon.start(ms);
            } else return sendJson(res, {{"error", "action must be start or stop"}}, 400);
            sendJson(res, {{"running", mon.running()}, {"intervalMs", mon.intervalMs()}});
        } catch (const std::exception& e) {
            sendJson(res, {{"error", e.what()}}, 400);
        }
    });

    // 8. POST /api/force-deadlock
    svr.Post("/api/force-deadlock", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        std::thread([]() { concurrency::ClientSimulator::runForceDeadlockDemo(); }).detach();
        sendJson(res, {{"status", "started"}, {"message", "Forced deadlock scenario triggered."}});
    });

    // 9. POST /api/simulate-crash: a real debit through the WAL path, never committed.
    svr.Post("/api/simulate-crash", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        if (integration::TransactionOrchestrator::getInstance().activeTransfers() > 0)
            return sendJson(res, {{"error", "Transfers are in flight; try again when idle."}}, 409);
        auto& tm = txn::TransactionManager::getInstance();
        int txnId = tm.beginTransaction("ACC003", "ACC004", 200.00);
        bool debited = txnId > 0 && tm.debit(txnId, "ACC003", 200.00);
        sendJson(res, {{"status", debited ? "crashed" : "failed"}, {"txnId", txnId},
                       {"message", debited ? "Simulated crash: DEBIT applied and WAL-logged, no COMMIT. Call /api/recover."
                                           : "Could not set up the crash scenario (insufficient funds?)."}});
    });

    // 10. POST /api/recover  (undoes every txn that has no COMMIT/ABORT record)
    svr.Post("/api/recover", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        if (integration::TransactionOrchestrator::getInstance().activeTransfers() > 0)
            return sendJson(res, {{"error", "Transfers are in flight; recovery would undo live transactions."}}, 409);
        int recovered = txn::RecoveryManager::getInstance().recoverOnRestart();
        sendJson(res, {{"status", "recovered"}, {"recoveredTxns", recovered},
                       {"message", "WAL crash recovery complete."}});
    });

    // 11. POST /api/reset
    svr.Post("/api/reset", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        if (integration::TransactionOrchestrator::getInstance().activeTransfers() > 0)
            return sendJson(res, {{"error", "Transfers are in flight; try again when idle."}}, 409);
        auto& db = db::DBConnector::getInstance();
        db.execute("DELETE FROM DeadlockLog;");
        db.execute("DELETE FROM TransactionLog;");
        db.execute("DELETE FROM Transactions;");
        db.execute("UPDATE Accounts SET balance = 5000.00, version = 1 WHERE id = 'ACC001';");
        db.execute("UPDATE Accounts SET balance = 3500.50, version = 1 WHERE id = 'ACC002';");
        db.execute("UPDATE Accounts SET balance = 10000.00, version = 1 WHERE id = 'ACC003';");
        db.execute("UPDATE Accounts SET balance = 7500.25, version = 1 WHERE id = 'ACC004';");
        db.execute("UPDATE Accounts SET balance = 2000.00, version = 1 WHERE id = 'ACC005';");
        concurrency::WaitForGraph::getInstance().clear();
        sendJson(res, {{"status", "reset"}, {"message", "Accounts and logs reset."}});
    });

    if (!svr.bind_to_port("0.0.0.0", port)) {
        std::cerr << "[DashboardAPI] Could not bind port " << port << " (already in use?)" << std::endl;
        server_.reset();
        return false;
    }
    running_ = true;
    server_thread_ = std::make_unique<std::thread>([this]() {
        server_->listen_after_bind();
        running_ = false;
    });
    std::cout << "[DashboardAPI] REST server running on http://localhost:" << port << std::endl;
    return true;
}

void DashboardAPI::stopServer() {
    if (server_) server_->stop();
    if (server_thread_ && server_thread_->joinable()) server_thread_->join();
    server_thread_.reset();
    server_.reset();
    running_ = false;
}

}  // namespace api
