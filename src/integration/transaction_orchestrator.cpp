#include "integration/transaction_orchestrator.h"
#include "concurrency/std_thread_compat.h"
#include "concurrency/lock_manager.h"
#include "txn/transaction_manager.h"
#include "predict/predictor_service.h"

#include <chrono>
#include <exception>
#include <random>
#include <thread>

namespace integration {

const char* modeName(ExecutionMode m) {
    switch (m) {
        case ExecutionMode::REACTIVE_DFS: return "REACTIVE_DFS";
        case ExecutionMode::BANKERS_AVOIDANCE: return "BANKERS_AVOIDANCE";
        case ExecutionMode::PREDICTIVE_HYBRID: return "PREDICTIVE_HYBRID";
        case ExecutionMode::ORDERED_LOCKING: return "ORDERED_LOCKING";
    }
    return "REACTIVE_DFS";
}

bool parseMode(const std::string& s, ExecutionMode& out) {
    if (s == "REACTIVE_DFS") { out = ExecutionMode::REACTIVE_DFS; return true; }
    if (s == "BANKERS_AVOIDANCE") { out = ExecutionMode::BANKERS_AVOIDANCE; return true; }
    if (s == "PREDICTIVE_HYBRID") { out = ExecutionMode::PREDICTIVE_HYBRID; return true; }
    if (s == "ORDERED_LOCKING") { out = ExecutionMode::ORDERED_LOCKING; return true; }
    return false;
}

TransactionOrchestrator& TransactionOrchestrator::getInstance() {
    static TransactionOrchestrator instance;
    return instance;
}

TransactionOrchestrator::Attempt TransactionOrchestrator::runOnce(
    const std::string& from, const std::string& to, double amount, ExecutionMode mode,
    int holdDelayMs, int attempt, int priority,
    std::chrono::steady_clock::time_point origin, TransferResult& res) {

    auto& tm = txn::TransactionManager::getInstance();
    auto& locks = concurrency::LockManager::getInstance();
    // PREDICTIVE_HYBRID: the Markov predictor decides, per transfer, whether the current
    // load is risky enough to pay for Banker's avoidance.
    const bool avoid = (mode == ExecutionMode::BANKERS_AVOIDANCE) ||
                       (mode == ExecutionMode::PREDICTIVE_HYBRID &&
                        predict::PredictorService::getInstance().shouldAvoid());
    const auto lockMode = avoid ? concurrency::LockMode::BankersAvoidance
                                : concurrency::LockMode::DetectAndRecover;
    res.usedBankers = (lockMode == concurrency::LockMode::BankersAvoidance);

    // 1. BEGIN (Transactions row + BEGIN record in the WAL)
    int txnId = tm.beginTransaction(from, to, amount);
    if (txnId <= 0) {
        res.message = "Failed to initialize transaction in database.";
        return Attempt::Done;
    }
    res.txnId = txnId;

    concurrency::TxnInfo info;
    info.amount = amount;
    info.priority = priority;
    info.retries = attempt;
    info.origin = origin;
    locks.beginTxn(txnId, {from, to}, info);

    // ORDERED_LOCKING (deadlock prevention): every transfer takes the lower id first, so the
    // wait-for graph can never contain a cycle. The debit/credit direction is unchanged.
    const bool ordered = (mode == ExecutionMode::ORDERED_LOCKING);
    const std::string& first = (ordered && to < from) ? to : from;
    const std::string& second = (ordered && to < from) ? from : to;

    // Rollback while still holding the locks, then release them. Never throws.
    auto abortTxn = [&]() {
        try { tm.rollback(txnId); } catch (...) {}
        locks.releaseAll(txnId);
    };

    // Whatever happens below, the locks are released: an exception must not leave
    // accounts locked forever.
    try {
        // 2. Growing phase: lock source, simulated work, lock destination.
        if (locks.acquire(txnId, first, lockMode) == concurrency::LockResult::Aborted) {
            abortTxn();
            res.deadlockDetected = true;
            res.victimTxnId = txnId;
            res.message = "Aborted as deadlock victim.";
            return Attempt::Victim;
        }
        if (holdDelayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(holdDelayMs));
        if (locks.acquire(txnId, second, lockMode) == concurrency::LockResult::Aborted) {
            abortTxn();
            res.deadlockDetected = true;
            res.victimTxnId = txnId;
            res.message = "Aborted as deadlock victim.";
            return Attempt::Victim;
        }

        // 3. Both locks held: WAL-logged debit and credit, then commit.
        if (!tm.debit(txnId, from, amount)) {
            abortTxn();
            res.message = "Debit failed (insufficient funds, unknown account, or DB error).";
            return Attempt::Done;
        }
        if (!tm.credit(txnId, to, amount)) {
            abortTxn();
            res.message = "Credit failed (unknown destination account or DB error); debit rolled back.";
            return Attempt::Done;
        }
        if (!tm.commit(txnId)) {
            abortTxn();
            res.message = "Commit failed; transfer rolled back.";
            return Attempt::Done;
        }

        // 4. Shrinking phase: only now release both locks.
        locks.releaseAll(txnId);
        res.success = true;
        res.message = "Fund transfer completed successfully.";
        return Attempt::Done;
    } catch (const std::exception& e) {
        abortTxn();
        res.message = std::string("Internal error, transfer rolled back: ") + e.what();
        return Attempt::Done;
    } catch (...) {
        abortTxn();
        res.message = "Internal error, transfer rolled back.";
        return Attempt::Done;
    }
}

TransferResult TransactionOrchestrator::executeTransfer(
    const std::string& fromAcct, const std::string& toAcct, double amount,
    ExecutionMode mode, int holdDelayMs, int maxRetries, int priority) {

    TransferResult res;
    if (fromAcct.empty() || toAcct.empty()) { res.message = "Account ids must not be empty."; return res; }
    if (fromAcct == toAcct) { res.message = "Source and destination must differ."; return res; }
    if (!(amount > 0.0)) { res.message = "Amount must be positive."; return res; }
    if (priority < 0) priority = 0;
    if (priority > 9) priority = 9;

    struct ActiveGuard {
        std::atomic<int>& c;
        explicit ActiveGuard(std::atomic<int>& x) : c(x) { ++c; }
        ~ActiveGuard() { --c; }
    } guard(active_);

    const auto origin = std::chrono::steady_clock::now();
    thread_local std::mt19937 rng{std::random_device{}()};
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        if (runOnce(fromAcct, toAcct, amount, mode, holdDelayMs, attempt, priority, origin, res) ==
            Attempt::Done) {
            res.latencyMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - origin).count();
            return res;
        }
        // Victim: back off a random few ms (avoids re-colliding in lockstep), retry.
        if (attempt == maxRetries) break;
        ++res.retries;
        std::this_thread::sleep_for(std::chrono::milliseconds(5 + rng() % 20));
    }
    res.success = false;
    res.message = "Gave up after repeated deadlock aborts.";
    res.latencyMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - origin).count();
    return res;
}

}  // namespace integration
