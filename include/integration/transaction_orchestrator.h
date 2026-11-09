#pragma once
#include "concurrency/std_thread_compat.h"
#include <atomic>
#include <chrono>
#include <string>

namespace integration {

enum class ExecutionMode {
    REACTIVE_DFS,        // detect deadlocks with the Wait-For Graph, abort a victim, retry
    BANKERS_AVOIDANCE,   // never enter an unsafe state, so no deadlock can form
    PREDICTIVE_HYBRID,   // a Markov-chain predictor chooses between the two per transfer
    ORDERED_LOCKING      // prevention: always lock the lower account id first, so no cycle can form
};

const char* modeName(ExecutionMode m);
bool parseMode(const std::string& s, ExecutionMode& out);

struct TransferResult {
    bool success = false;
    int txnId = -1;               // id of the final attempt
    std::string message;
    bool deadlockDetected = false;  // true if any attempt was aborted as a deadlock victim
    int victimTxnId = -1;         // txn id of the (last) attempt that was aborted as victim
    int retries = 0;              // attempts repeated after a victim abort
    double latencyMs = 0.0;       // wall time of the whole transfer, including retries
    bool usedBankers = false;     // true if the final attempt ran under Banker's avoidance
};

// Runs one transfer: BEGIN -> lock from -> lock to -> debit -> credit -> COMMIT,
// strict 2PL (both locks released only at commit/abort), WAL via TransactionManager.
// If this transaction is chosen as a deadlock victim it is rolled back and retried
// (new txn id) up to maxRetries times.
class TransactionOrchestrator {
public:
    static TransactionOrchestrator& getInstance();

    // holdDelayMs: pause between taking the two locks (simulated work); widens
    // the deadlock window for the demo.
    // priority: 0..9, used by the VALUE_BASED victim policy (higher = protected).
    TransferResult executeTransfer(const std::string& fromAcct, const std::string& toAcct,
                                   double amount, ExecutionMode mode,
                                   int holdDelayMs = 20, int maxRetries = 3, int priority = 5);

    int activeTransfers() const { return active_.load(); }

private:
    TransactionOrchestrator() = default;
    TransactionOrchestrator(const TransactionOrchestrator&) = delete;
    TransactionOrchestrator& operator=(const TransactionOrchestrator&) = delete;

    enum class Attempt { Done, Victim };
    Attempt runOnce(const std::string& from, const std::string& to, double amount, ExecutionMode mode,
                    int holdDelayMs, int attempt, int priority,
                    std::chrono::steady_clock::time_point origin, TransferResult& res);

    std::atomic<int> active_{0};
};

}  // namespace integration
