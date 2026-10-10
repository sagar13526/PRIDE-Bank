#include "concurrency/bankers_algorithm.h"
#include <stdexcept>

namespace concurrency {

BankersAlgorithm& BankersAlgorithm::getInstance() {
    static BankersAlgorithm instance;
    return instance;
}

void BankersAlgorithm::registerTransaction(int txnId, const std::vector<std::string>& neededAccounts) {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    max_need_[txnId] = AcctSet(neededAccounts.begin(), neededAccounts.end());
    allocation_[txnId];
}

void BankersAlgorithm::unregisterTransaction(int txnId) {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    max_need_.erase(txnId);
    allocation_.erase(txnId);
}

void BankersAlgorithm::recordAllocation(int txnId, const std::string& accountId) {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    allocation_[txnId].insert(accountId);
}

void BankersAlgorithm::releaseAllocation(int txnId, const std::string& accountId) {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    auto it = allocation_.find(txnId);
    if (it != allocation_.end()) it->second.erase(accountId);
}

bool BankersAlgorithm::safeState(const TxnMap& maxNeed, const TxnMap& allocation) {
    // Universe = every account anyone may still need; available = not held.
    AcctSet universe, held;
    for (const auto& kv : maxNeed) universe.insert(kv.second.begin(), kv.second.end());
    for (const auto& kv : allocation) held.insert(kv.second.begin(), kv.second.end());

    AcctSet available;
    for (const auto& a : universe)
        if (!held.count(a)) available.insert(a);

    std::unordered_set<int> unfinished;
    for (const auto& kv : maxNeed) unfinished.insert(kv.first);

    static const AcctSet kEmpty;
    bool progress = true;
    while (!unfinished.empty() && progress) {
        progress = false;
        for (auto it = unfinished.begin(); it != unfinished.end();) {
            int t = *it;
            auto aIt = allocation.find(t);
            const AcctSet& alloc = (aIt != allocation.end()) ? aIt->second : kEmpty;

            // remaining need = claim - allocation; must all be available
            bool canFinish = true;
            for (const auto& a : maxNeed.at(t)) {
                if (!alloc.count(a) && !available.count(a)) { canFinish = false; break; }
            }
            if (canFinish) {
                available.insert(alloc.begin(), alloc.end());  // t releases everything
                it = unfinished.erase(it);
                progress = true;
            } else {
                ++it;
            }
        }
    }
    return unfinished.empty();
}

bool BankersAlgorithm::isSafe(int txnId, const std::string& accountId) {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    TxnMap hypothetical = allocation_;
    hypothetical[txnId].insert(accountId);
    return safeState(max_need_, hypothetical);
}

bool BankersAlgorithm::canGrant(int txnId, const std::string& accountId) {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    auto cIt = max_need_.find(txnId);
    if (cIt == max_need_.end() || !cIt->second.count(accountId))
        throw std::invalid_argument("Banker's: account not in transaction's declared claim");

    for (const auto& kv : allocation_)
        if (kv.second.count(accountId)) return kv.first == txnId;  // held by someone

    TxnMap hypothetical = allocation_;
    hypothetical[txnId].insert(accountId);
    return safeState(max_need_, hypothetical);
}

void BankersAlgorithm::reset() {
    std::lock_guard<std::mutex> lock(bankers_mutex_);
    allocation_.clear();
    max_need_.clear();
}

}  // namespace concurrency
