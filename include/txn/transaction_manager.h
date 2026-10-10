#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include "concurrency/std_thread_compat.h"

namespace txn {

enum class TxnStatus {
    PENDING,
    COMMITTED,
    ABORTED,
    ROLLED_BACK
};

class TransactionManager {
public:
    static TransactionManager& getInstance();

    int beginTransaction(const std::string& from_acct, const std::string& to_acct, double amount);
    bool debit(int txn_id, const std::string& acct_id, double amount);
    bool credit(int txn_id, const std::string& acct_id, double amount);
    bool commit(int txn_id);
    bool rollback(int txn_id);

    std::vector<std::map<std::string, std::string>> getAccounts();
    std::vector<std::map<std::string, std::string>> getTransactions(int limit = 50);

private:
    TransactionManager() = default;
    std::mutex txn_mutex_;
};

} // namespace txn

#endif // TRANSACTION_MANAGER_H
