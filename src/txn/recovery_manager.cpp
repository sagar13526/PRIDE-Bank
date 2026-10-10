#include "txn/recovery_manager.h"
#include "db/db_connector.h"
#include <iostream>
#include <sstream>

namespace txn {

RecoveryManager& RecoveryManager::getInstance() {
    static RecoveryManager instance;
    return instance;
}

long long RecoveryManager::logOperation(int txn_id, const std::string& op,
                                        const std::string& before_val, const std::string& after_val) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    auto& db = db::DBConnector::getInstance();
    
    std::string sql = "INSERT INTO TransactionLog (txn_id, op, before_val, after_val) VALUES (?, ?, ?, ?);";
    bool success = db.executeParameterized(sql, {std::to_string(txn_id), op, before_val, after_val});
    if (!success) {
        std::cerr << "[RecoveryManager] Failed to write WAL entry for txn: " << txn_id << std::endl;
        return -1;
    }

    auto rows = db.query("SELECT MAX(lsn) AS lsn FROM TransactionLog;");
    if (!rows.empty() && rows[0].find("lsn") != rows[0].end()) {
        try {
            return std::stoll(rows[0].at("lsn"));
        } catch (...) {
            return 1;
        }
    }
    return 1;
}

int RecoveryManager::recoverOnRestart() {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    auto& db = db::DBConnector::getInstance();
    
    std::cout << "[RecoveryManager] Starting WAL Crash Recovery Analysis..." << std::endl;

    // Find all transaction IDs that do NOT have a COMMIT or ABORT log entry
    std::string uncommittedSql = 
        "SELECT DISTINCT txn_id FROM TransactionLog WHERE txn_id NOT IN ("
        "  SELECT txn_id FROM TransactionLog WHERE op IN ('COMMIT', 'ABORT', 'ROLLED_BACK')"
        ");";

    auto uncommittedRows = db.query(uncommittedSql);
    int recoveredTxns = 0;

    for (const auto& row : uncommittedRows) {
        if (row.find("txn_id") == row.end()) continue;
        int txnId = std::stoi(row.at("txn_id"));

        std::cout << "[RecoveryManager] Found incomplete transaction ID: " << txnId << ". Replaying WAL in reverse LSN..." << std::endl;

        // Fetch logs for this transaction in REVERSE LSN order
        std::string logSql = "SELECT * FROM TransactionLog WHERE txn_id = ? ORDER BY lsn DESC;";
        auto logs = db.queryParameterized(logSql, {std::to_string(txnId)});

        for (const auto& logRow : logs) {
            std::string op = logRow.count("op") ? logRow.at("op") : "";
            std::string beforeVal = logRow.count("before_val") ? logRow.at("before_val") : "";

            if ((op == "DEBIT" || op == "CREDIT") && !beforeVal.empty()) {
                // Parse before_val format "ACCOUNT_ID:BALANCE"
                size_t colonPos = beforeVal.find(':');
                if (colonPos != std::string::npos) {
                    std::string acctId = beforeVal.substr(0, colonPos);
                    std::string oldBalance = beforeVal.substr(colonPos + 1);

                    std::string restoreSql = "UPDATE Accounts SET balance = ? WHERE id = ?;";
                    db.executeParameterized(restoreSql, {oldBalance, acctId});
                    std::cout << "  [WAL Replay] Restored Account " << acctId << " -> Balance: $" << oldBalance << std::endl;
                }
            }
        }

        // Mark transaction as ROLLED_BACK in DB & WAL
        db.executeParameterized("UPDATE Transactions SET status = 'ROLLED_BACK' WHERE txn_id = ?;", {std::to_string(txnId)});
        db.executeParameterized("INSERT INTO TransactionLog (txn_id, op, before_val, after_val) VALUES (?, 'ROLLED_BACK', 'CRASH_RECOVERY', 'UNDO_COMPLETE');", {std::to_string(txnId)});
        recoveredTxns++;
    }

    std::cout << "[RecoveryManager] Crash Recovery complete! Recovered & undone " << recoveredTxns << " uncommitted transactions." << std::endl;
    return recoveredTxns;
}

std::vector<LogEntry> RecoveryManager::getAllLogs(int limit) {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    auto& db = db::DBConnector::getInstance();
    
    std::string sql = "SELECT * FROM TransactionLog ORDER BY lsn DESC LIMIT ?;";
    auto rows = db.queryParameterized(sql, {std::to_string(limit)});

    std::vector<LogEntry> result;
    for (const auto& r : rows) {
        LogEntry entry;
        entry.lsn = r.count("lsn") ? std::stoll(r.at("lsn")) : 0;
        entry.txn_id = r.count("txn_id") ? std::stoi(r.at("txn_id")) : 0;
        entry.op = r.count("op") ? r.at("op") : "";
        entry.before_val = r.count("before_val") ? r.at("before_val") : "";
        entry.after_val = r.count("after_val") ? r.at("after_val") : "";
        entry.ts = r.count("ts") ? r.at("ts") : "";
        result.push_back(entry);
    }
    return result;
}

} // namespace txn
