#include "concurrency/lock_manager.h"
#include "concurrency/bankers_algorithm.h"
#include "concurrency/deadlock_handler.h"
#include "concurrency/wait_for_graph.h"

namespace concurrency {

LockManager& LockManager::getInstance() {
    static LockManager instance;
    return instance;
}

void LockManager::beginTxn(int txnId, const std::vector<std::string>& claim, const TxnInfo& info) {
    BankersAlgorithm::getInstance().registerTransaction(txnId, claim);
    DeadlockHandler::getInstance().registerTxn(txnId, info);
}

LockResult LockManager::acquire(int txnId, const std::string& accountId, LockMode mode) {
    auto& wfg = WaitForGraph::getInstance();
    auto& handler = DeadlockHandler::getInstance();
    auto& bankers = BankersAlgorithm::getInstance();

    std::unique_lock<std::mutex> lk(mu_);
    LockRecord& rec = table_[accountId];  // element references survive rehash
    if (rec.owner == txnId) return LockResult::Granted;

    while (true) {
        // Picked as a deadlock victim while sleeping.
        if (aborted_.count(txnId)) {
            wfg.removeOutgoingEdges(txnId);
            waitingOn_.erase(txnId);
            return LockResult::Aborted;
        }

        wfg.removeOutgoingEdges(txnId);  // recompute our wait edge each pass

        if (rec.owner == 0) {
            if (mode == LockMode::DetectAndRecover || bankers.canGrant(txnId, accountId)) {
                rec.owner = txnId;
                held_[txnId].push_back(accountId);
                ++grants_;
                handler.noteHeld(txnId, static_cast<int>(held_[txnId].size()));
                bankers.recordAllocation(txnId, accountId);
                waitingOn_.erase(txnId);
                return LockResult::Granted;
            }
            // Free, but granting would be unsafe (Banker's): wait, no edge.
            waitingOn_[txnId] = accountId;
        } else {
            if (!waitingOn_.count(txnId)) ++waitEvents_;
            waitingOn_[txnId] = accountId;
            wfg.addEdge(txnId, rec.owner, accountId);

            // Cycle detection runs for EVERY waiter, whatever its mode: a hybrid system can
            // mix Banker's and detect-mode transactions, and a cycle through both must still
            // be found by whoever closes it. (In a pure Banker's system no cycle ever forms.)
            {
                std::vector<int> cycle;
                if (wfg.detectCycleFrom(txnId, cycle)) {
                    bool alreadyHandled = false;
                    for (int t : cycle)
                        if (aborted_.count(t)) { alreadyHandled = true; break; }

                    if (!alreadyHandled) {
                        std::vector<std::string> accts;
                        for (int t : cycle) {
                            auto w = waitingOn_.find(t);
                            if (w != waitingOn_.end()) accts.push_back(w->second);
                        }
                        int victim = handler.resolveDeadlock(cycle, accts);
                        if (victim == txnId) {
                            wfg.removeOutgoingEdges(txnId);
                            waitingOn_.erase(txnId);
                            return LockResult::Aborted;
                        }
                        aborted_.insert(victim);  // victim wakes up and aborts itself
                        cv_.notify_all();
                    }
                }
            }
        }
        cv_.wait(lk);
    }
}

void LockManager::releaseAll(int txnId) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = held_.find(txnId);
        if (it != held_.end()) {
            for (const auto& a : it->second) table_[a].owner = 0;
            held_.erase(it);
        }
        WaitForGraph::getInstance().removeAllEdgesForTxn(txnId);
        BankersAlgorithm::getInstance().unregisterTransaction(txnId);
        DeadlockHandler::getInstance().unregisterTxn(txnId);
        aborted_.erase(txnId);
        waitingOn_.erase(txnId);
    }
    cv_.notify_all();  // waiters re-check
}

bool LockManager::isAccountLocked(const std::string& accountId) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = table_.find(accountId);
    return it != table_.end() && it->second.owner != 0;
}

int LockManager::getLockHolder(const std::string& accountId) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = table_.find(accountId);
    return it == table_.end() ? 0 : it->second.owner;
}

std::unordered_map<std::string, std::string> LockManager::getLockStateMap() {
    std::lock_guard<std::mutex> lk(mu_);
    std::unordered_map<std::string, std::string> out;
    for (const auto& kv : table_)
        if (kv.second.owner != 0) out[kv.first] = std::to_string(kv.second.owner);
    return out;
}

LockStats LockManager::getStats() {
    std::lock_guard<std::mutex> lk(mu_);
    LockStats st;
    for (const auto& kv : table_) if (kv.second.owner != 0) ++st.locked;
    st.waitEvents = waitEvents_;
    st.grants = grants_;
    // Only transactions blocked behind a current owner are "waiting"; one that waits for a
    // free-but-unsafe lock (Banker's) has no wait edge and is not counted.
    for (const auto& w : waitingOn_) {
        auto it = table_.find(w.second);
        if (it == table_.end() || it->second.owner == 0) continue;
        ++st.waiting;
        int depth = 1, cur = it->second.owner;
        while (depth <= static_cast<int>(waitingOn_.size())) {
            auto nw = waitingOn_.find(cur);
            if (nw == waitingOn_.end()) break;
            auto nt = table_.find(nw->second);
            if (nt == table_.end() || nt->second.owner == 0) break;
            cur = nt->second.owner;
            ++depth;
        }
        if (depth > st.maxChain) st.maxChain = depth;
    }
    return st;
}

}  // namespace concurrency
