#include "concurrency/deadlock_handler.h"
#include "db/db_connector.h"
#include <algorithm>
#include <iostream>

namespace concurrency {

DeadlockHandler& DeadlockHandler::getInstance() {
    static DeadlockHandler instance;
    return instance;
}

const char* DeadlockHandler::policyName(VictimPolicy p) {
    return p == VictimPolicy::VALUE_BASED ? "VALUE_BASED" : "YOUNGEST";
}

bool DeadlockHandler::parsePolicy(const std::string& s, VictimPolicy& out) {
    if (s == "YOUNGEST") { out = VictimPolicy::YOUNGEST; return true; }
    if (s == "VALUE_BASED") { out = VictimPolicy::VALUE_BASED; return true; }
    return false;
}

void DeadlockHandler::registerTxn(int txnId, const TxnInfo& info) {
    std::lock_guard<std::mutex> lk(info_mu_);
    info_[txnId] = info;
}

void DeadlockHandler::unregisterTxn(int txnId) {
    std::lock_guard<std::mutex> lk(info_mu_);
    info_.erase(txnId);
}

void DeadlockHandler::noteHeld(int txnId, int held) {
    std::lock_guard<std::mutex> lk(info_mu_);
    auto it = info_.find(txnId);
    if (it != info_.end()) it->second.held = held;
}

void DeadlockHandler::setWeights(const VictimWeights& w) {
    std::lock_guard<std::mutex> lk(info_mu_);
    weights_ = w;
}

VictimWeights DeadlockHandler::weights() const {
    std::lock_guard<std::mutex> lk(info_mu_);
    return weights_;
}

double DeadlockHandler::abortCostLocked(int txnId) const {
    auto it = info_.find(txnId);
    if (it == info_.end()) return 0.0;  // unknown: cheapest to lose
    const TxnInfo& t = it->second;
    const double ageMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t.origin).count();
    return weights_.priority * t.priority + weights_.retries * t.retries +
           weights_.agePer100ms * (ageMs / 100.0) + weights_.amountPer100 * (t.amount / 100.0) +
           weights_.held * t.held;
}

double DeadlockHandler::abortCost(int txnId) const {
    std::lock_guard<std::mutex> lk(info_mu_);
    return abortCostLocked(txnId);
}

int DeadlockHandler::selectVictimNaiveYoungest(const std::vector<int>& cycle) const {
    if (cycle.empty()) return -1;
    return *std::max_element(cycle.begin(), cycle.end());  // youngest = largest id
}

int DeadlockHandler::selectVictimValueBased(const std::vector<int>& cycle) const {
    if (cycle.empty()) return -1;
    std::lock_guard<std::mutex> lk(info_mu_);
    int victim = cycle.front();
    double best = abortCostLocked(victim);
    for (int t : cycle) {
        const double c = abortCostLocked(t);
        // Lowest cost loses; on a tie the youngest (largest id) loses.
        if (c < best || (c == best && t > victim)) { best = c; victim = t; }
    }
    return victim;
}

int DeadlockHandler::selectVictim(const std::vector<int>& cycle) const {
    return policy() == VictimPolicy::VALUE_BASED ? selectVictimValueBased(cycle)
                                                 : selectVictimNaiveYoungest(cycle);
}

int DeadlockHandler::resolveDeadlock(const std::vector<int>& cycleTxns,
                                     const std::vector<std::string>& cycleAccounts) {
    if (cycleTxns.empty()) return -1;
    int victim = selectVictim(cycleTxns);

    std::string txns, accts;
    for (size_t i = 0; i < cycleTxns.size(); ++i)
        txns += std::to_string(cycleTxns[i]) + (i + 1 < cycleTxns.size() ? "->" : "");
    for (size_t i = 0; i < cycleAccounts.size(); ++i)
        accts += cycleAccounts[i] + (i + 1 < cycleAccounts.size() ? "," : "");

    std::cout << "[DeadlockHandler] Cycle [" << txns << "] on accounts {" << accts
              << "}. Victim: Txn " << victim << " (" << policyName(policy()) << ")" << std::endl;

    db::DBConnector::getInstance().executeParameterized(
        "INSERT INTO DeadlockLog (victim_txn_id, cycle_accounts) VALUES (?, ?);",
        {std::to_string(victim), accts});

    ++resolved_;
    return victim;
}

std::vector<std::map<std::string, std::string>> DeadlockHandler::getDeadlockLogs(int limit) {
    return db::DBConnector::getInstance().queryParameterized(
        "SELECT * FROM DeadlockLog ORDER BY dl_id DESC LIMIT ?;", {std::to_string(limit)});
}

}  // namespace concurrency
