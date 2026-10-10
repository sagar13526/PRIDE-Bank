#pragma once
#include <atomic>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace concurrency {

// Picks the victim of a detected cycle and writes the audit row to DeadlockLog.
//
// It deliberately does NOT roll anything back: the victim's own thread sees
// LockResult::Aborted from LockManager::acquire() and rolls itself back while it
// still holds its locks (see TransactionOrchestrator).
//
// Victim policy is the naive "abort the youngest" rule (largest txn id). All
// selection goes through selectVictim(), the single place to change later.
class DeadlockHandler {
public:
    static DeadlockHandler& getInstance();

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

    std::atomic<std::size_t> resolved_{0};
};

}  // namespace concurrency
