#pragma once
#include "concurrency/std_thread_compat.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace concurrency {

enum class LockMode {
    DetectAndRecover,  // grant freely; detect cycles in the Wait-For Graph; abort a victim
    BankersAvoidance   // grant only if the resulting state is safe (no deadlock can form)
};

enum class LockResult { Granted, Aborted };

// One exclusive lock per account, owned by a TRANSACTION and held until the
// transaction ends (Two-Phase Locking: acquire from, acquire to, release only
// via releaseAll() at commit/abort).
//
// Why not one raw std::mutex per account: a thread blocked in mutex::lock() is
// invisible (no "T1 waits for T2" edge) and cannot be woken to abort. Here each
// account has an owner record, and every wait happens on ONE condition variable
// guarded by ONE mutex, so every wait is observable and interruptible.
//
// Contract:  beginTxn(id, {from,to});  acquire(id,from);  acquire(id,to);
//            ... debit / credit / commit ...;  releaseAll(id);
// If acquire() returns Aborted: roll the transaction back, THEN releaseAll(id).
class LockManager {
public:
    static LockManager& getInstance();

    void beginTxn(int txnId, const std::vector<std::string>& claim);
    LockResult acquire(int txnId, const std::string& accountId, LockMode mode);
    void releaseAll(int txnId);  // also clears WFG edges + Banker's registration

    bool isAccountLocked(const std::string& accountId);
    int getLockHolder(const std::string& accountId);  // 0 if free
    std::unordered_map<std::string, std::string> getLockStateMap();  // account -> txn id

private:
    LockManager() = default;
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    struct LockRecord { int owner = 0; };

    std::mutex mu_;
    std::condition_variable cv_;
    std::unordered_map<std::string, LockRecord> table_;
    std::unordered_map<int, std::vector<std::string>> held_;
    std::unordered_map<int, std::string> waitingOn_;
    std::unordered_set<int> aborted_;  // chosen as victim, not yet cleaned up
};

}  // namespace concurrency
