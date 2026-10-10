#include "concurrency/deadlock_handler.h"
#include "db/db_connector.h"
#include <algorithm>
#include <iostream>

namespace concurrency {

DeadlockHandler& DeadlockHandler::getInstance() {
    static DeadlockHandler instance;
    return instance;
}

int DeadlockHandler::selectVictimNaiveYoungest(const std::vector<int>& cycle) const {
    if (cycle.empty()) return -1;
    return *std::max_element(cycle.begin(), cycle.end());  // youngest = largest id
}

int DeadlockHandler::selectVictim(const std::vector<int>& cycle) const {
    return selectVictimNaiveYoungest(cycle);
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
              << "}. Victim: Txn " << victim << std::endl;

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
