#pragma once
#include "concurrency/std_thread_compat.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace concurrency {

// Banker's Algorithm for single-instance resources (each account = one
// resource). Every transaction declares the accounts it MAY need (its claim).
// A lock is granted only if the resulting state is SAFE: some order exists in
// which every transaction can still finish. Safe states never deadlock.
class BankersAlgorithm {
public:
    static BankersAlgorithm& getInstance();

    void registerTransaction(int txnId, const std::vector<std::string>& neededAccounts);
    void unregisterTransaction(int txnId);
    void recordAllocation(int txnId, const std::string& accountId);
    void releaseAllocation(int txnId, const std::string& accountId);

    // Pure safety simulation: if txn were granted account, would the state be safe?
    bool isSafe(int txnId, const std::string& accountId);
    // What the lock manager asks: false if another txn holds the account,
    // otherwise isSafe(). Throws std::invalid_argument if txn is unregistered or
    // the account is outside its declared claim.
    bool canGrant(int txnId, const std::string& accountId);

    void reset();

private:
    BankersAlgorithm() = default;
    BankersAlgorithm(const BankersAlgorithm&) = delete;
    BankersAlgorithm& operator=(const BankersAlgorithm&) = delete;

    using AcctSet = std::unordered_set<std::string>;
    using TxnMap = std::unordered_map<int, AcctSet>;
    static bool safeState(const TxnMap& maxNeed, const TxnMap& allocation);

    std::mutex bankers_mutex_;
    TxnMap max_need_;    // claim per txn
    TxnMap allocation_;  // currently held per txn
};

}  // namespace concurrency
