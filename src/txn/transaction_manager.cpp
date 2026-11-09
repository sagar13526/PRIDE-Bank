#include "txn/transaction_manager.h"
#include "txn/recovery_manager.h"
#include "db/db_connector.h"
#include <iostream>

namespace txn {

TransactionManager& TransactionManager::getInstance() {
    static TransactionManager instance;
    return instance;
}

int TransactionManager::beginTransaction(const std::string& from_acct, const std::string& to_acct, double amount) {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto& db = db::DBConnector::getInstance();

    std::string sql = "INSERT INTO Transactions (from_acct, to_acct, amount, status) VALUES (?, ?, ?, 'PENDING');";
    int txn_id = static_cast<int>(
        db.insertParameterized(sql, {from_acct, to_acct, std::to_string(amount)}));
    if (txn_id <= 0) return -1;

    if (txn_id > 0) {
        RecoveryManager::getInstance().logOperation(txn_id, "BEGIN", "", 
            "from=" + from_acct + ",to=" + to_acct + ",amount=" + std::to_string(amount));
    }

    return txn_id;
}

bool TransactionManager::debit(int txn_id, const std::string& acct_id, double amount) {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto& db = db::DBConnector::getInstance();

    // Query current balance
    auto rows = db.queryParameterized("SELECT balance FROM Accounts WHERE id = ?;", {acct_id});
    if (rows.empty()) return false;

    double curBalance = std::stod(rows[0].at("balance"));
    if (curBalance < amount) {
        std::cerr << "[TransactionManager] Insufficient balance for account " << acct_id << std::endl;
        return false;
    }

    double newBalance = curBalance - amount;
    std::string beforeVal = acct_id + ":" + std::to_string(curBalance);
    std::string afterVal = acct_id + ":" + std::to_string(newBalance);

    // 1. WAL entry BEFORE data modification
    if (RecoveryManager::getInstance().logOperation(txn_id, "DEBIT", beforeVal, afterVal) < 0) return false;

    // 2. Perform DB update
    std::string sql = "UPDATE Accounts SET balance = balance - ?, version = version + 1 WHERE id = ?;";
    return db.executeParameterized(sql, {std::to_string(amount), acct_id});
}

bool TransactionManager::credit(int txn_id, const std::string& acct_id, double amount) {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto& db = db::DBConnector::getInstance();

    // Query current balance
    auto rows = db.queryParameterized("SELECT balance FROM Accounts WHERE id = ?;", {acct_id});
    if (rows.empty()) return false;

    double curBalance = std::stod(rows[0].at("balance"));
    double newBalance = curBalance + amount;

    std::string beforeVal = acct_id + ":" + std::to_string(curBalance);
    std::string afterVal = acct_id + ":" + std::to_string(newBalance);

    // 1. WAL entry BEFORE data modification
    if (RecoveryManager::getInstance().logOperation(txn_id, "CREDIT", beforeVal, afterVal) < 0) return false;

    // 2. Perform DB update
    std::string sql = "UPDATE Accounts SET balance = balance + ?, version = version + 1 WHERE id = ?;";
    return db.executeParameterized(sql, {std::to_string(amount), acct_id});
}

bool TransactionManager::commit(int txn_id) {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto& db = db::DBConnector::getInstance();

    if (RecoveryManager::getInstance().logOperation(txn_id, "COMMIT", "", "") < 0) return false;
    return db.executeParameterized("UPDATE Transactions SET status = 'COMMITTED' WHERE txn_id = ?;", {std::to_string(txn_id)});
}

bool TransactionManager::rollback(int txn_id) {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    auto& db = db::DBConnector::getInstance();

    // Fetch WAL logs for this txn in REVERSE LSN
    auto rows = db.queryParameterized("SELECT * FROM TransactionLog WHERE txn_id = ? ORDER BY lsn DESC;", {std::to_string(txn_id)});
    for (const auto& logRow : rows) {
        std::string op = logRow.count("op") ? logRow.at("op") : "";
        std::string beforeVal = logRow.count("before_val") ? logRow.at("before_val") : "";

        if ((op == "DEBIT" || op == "CREDIT") && !beforeVal.empty()) {
            size_t colonPos = beforeVal.find(':');
            if (colonPos != std::string::npos) {
                std::string acctId = beforeVal.substr(0, colonPos);
                std::string oldBal = beforeVal.substr(colonPos + 1);
                db.executeParameterized("UPDATE Accounts SET balance = ? WHERE id = ?;", {oldBal, acctId});
            }
        }
    }

    // The ABORT record goes in only AFTER every before-image is restored. If we crash
    // mid-undo there is no ABORT record yet, so recovery redoes the whole (idempotent) undo.
    RecoveryManager::getInstance().logOperation(txn_id, "ABORT", "", "");
    return db.executeParameterized("UPDATE Transactions SET status = 'ABORTED' WHERE txn_id = ?;", {std::to_string(txn_id)});
}

std::vector<std::map<std::string, std::string>> TransactionManager::getAccounts() {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    return db::DBConnector::getInstance().query("SELECT * FROM Accounts ORDER BY id ASC;");
}

std::vector<std::map<std::string, std::string>> TransactionManager::getTransactions(int limit) {
    std::lock_guard<std::mutex> lock(txn_mutex_);
    return db::DBConnector::getInstance().queryParameterized("SELECT * FROM Transactions ORDER BY txn_id DESC LIMIT ?;", {std::to_string(limit)});
}

} // namespace txn
