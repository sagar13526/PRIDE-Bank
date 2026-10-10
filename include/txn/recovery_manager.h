#ifndef RECOVERY_MANAGER_H
#define RECOVERY_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include "concurrency/std_thread_compat.h"

namespace txn {

struct LogEntry {
    long long lsn;
    int txn_id;
    std::string op;
    std::string before_val;
    std::string after_val;
    std::string ts;
};

class RecoveryManager {
public:
    static RecoveryManager& getInstance();

    long long logOperation(int txn_id, const std::string& op, 
                           const std::string& before_val, const std::string& after_val);

    int recoverOnRestart();

    std::vector<LogEntry> getAllLogs(int limit = 50);

private:
    RecoveryManager() = default;
    std::mutex recovery_mutex_;
};

} // namespace txn

#endif // RECOVERY_MANAGER_H
