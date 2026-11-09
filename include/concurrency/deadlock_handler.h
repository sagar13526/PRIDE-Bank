#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace concurrency {

// What the victim policy knows about a running transaction. Registered by the lock
// manager at beginTxn() and removed at releaseAll().
struct TxnInfo {
    double amount = 0.0;   // transfer amount
    int priority = 5;      // business priority 0 (lowest) .. 9 (highest)
    int retries = 0;       // how many times this transfer was already a victim
    // When the FIRST attempt of this transfer started. A retry keeps the original time,
    // so a transfer that has been waiting a long time is not mistaken for a young one.
    std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
    int held = 0;          // locks currently held (kept up to date by the lock manager)
};

enum class VictimPolicy {
    YOUNGEST,    // Phase 2 rule: abort the largest transaction id
    VALUE_BASED  // Phase 3 rule: abort the transaction that is cheapest to lose
};

// Weights of the abort-cost score (see abortCost()).
struct VictimWeights {
    double priority = 15.0;       // per priority level
    double retries = 10.0;        // per earlier abort of the same transfer
    double agePer100ms = 1.0;     // per 100 ms since the first attempt started
    double amountPer100 = 1.0;    // per 100 currency units moved
    double held = 1.0;            // per lock already held
};

// Picks the victim of a detected cycle and writes the audit row to DeadlockLog.
//
// It deliberately does NOT roll anything back: the victim's own thread sees
// LockResult::Aborted from LockManager::acquire() and rolls itself back while it
// still holds its locks (see TransactionOrchestrator).
//
// Two policies, switchable at run time (all selection goes through selectVictim()):
//   YOUNGEST     abort the largest transaction id (Phase 2 baseline).
//   VALUE_BASED  abort the transaction with the LOWEST abort cost:
//                  cost = wP*priority + wR*retries + wA*age/100ms + wM*amount/100 + wH*held
//                Ties go to the youngest id. High-priority, long-waiting, already-retried
//                and large transfers are protected, which also fights starvation.
class DeadlockHandler {
public:
    static DeadlockHandler& getInstance();

    // Transaction metadata used by VALUE_BASED.
    void registerTxn(int txnId, const TxnInfo& info);
    void unregisterTxn(int txnId);
    void noteHeld(int txnId, int held);

    void setPolicy(VictimPolicy p) { policy_.store(static_cast<int>(p)); }
    VictimPolicy policy() const { return static_cast<VictimPolicy>(policy_.load()); }
    static const char* policyName(VictimPolicy p);
    static bool parsePolicy(const std::string& s, VictimPolicy& out);

    void setWeights(const VictimWeights& w);
    VictimWeights weights() const;

    // Cost of aborting txnId right now (0 if it is not registered).
    double abortCost(int txnId) const;

    int selectVictim(const std::vector<int>& cycle) const;
    int resolveDeadlock(const std::vector<int>& cycleTxns,
                        const std::vector<std::string>& cycleAccounts);

    std::vector<std::map<std::string, std::string>> getDeadlockLogs(int limit);
    std::size_t deadlocksResolved() const { return resolved_.load(); }

private:
    DeadlockHandler() = default;
    DeadlockHandler(const DeadlockHandler&) = delete;
    DeadlockHandler& operator=(const DeadlockHandler&) = delete;

    int selectVictimNaiveYoungest(const std::vector<int>& cycle) const;
    int selectVictimValueBased(const std::vector<int>& cycle) const;
    double abortCostLocked(int txnId) const;  // info_mu_ must be held

    std::atomic<std::size_t> resolved_{0};
    std::atomic<int> policy_{static_cast<int>(VictimPolicy::YOUNGEST)};

    mutable std::mutex info_mu_;
    std::unordered_map<int, TxnInfo> info_;
    VictimWeights weights_;
};

}  // namespace concurrency
