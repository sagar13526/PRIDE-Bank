#pragma once
#include "concurrency/std_thread_compat.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace concurrency {

struct Edge {
    int waitingTxnId;
    int holdingTxnId;
    std::string accountId;
};

// Wait-For Graph: node = transaction, edge T1 -> T2 means "T1 waits for a lock
// that T2 holds". A cycle == a deadlock. Internally synchronized.
class WaitForGraph {
public:
    static WaitForGraph& getInstance();

    void addEdge(int waitingTxnId, int holdingTxnId, const std::string& accountId);
    void removeEdge(int waitingTxnId, int holdingTxnId);
    void removeOutgoingEdges(int txnId);   // txn stopped waiting
    void removeAllEdgesForTxn(int txnId);  // txn finished: drop in- and out-edges

    // Any cycle in the graph (used by the dashboard).
    bool detectCycle(std::vector<int>& outCycle);
    // A cycle that passes through startTxnId (used right after startTxnId starts
    // waiting: any new deadlock must contain the new edge).
    // outCycle = [start, ..., last], where last has an edge back to start.
    bool detectCycleFrom(int startTxnId, std::vector<int>& outCycle);

    std::vector<Edge> getActiveEdges();
    std::unordered_set<int> getActiveNodes();
    void clear();

private:
    WaitForGraph() = default;
    WaitForGraph(const WaitForGraph&) = delete;
    WaitForGraph& operator=(const WaitForGraph&) = delete;

    bool dfsCycleCheck(int node, std::unordered_map<int, bool>& visited,
                       std::unordered_map<int, bool>& recStack,
                       std::vector<int>& currentPath, std::vector<int>& outCycle);
    bool dfsReach(int node, int target, std::unordered_set<int>& visited,
                  std::vector<int>& path);

    std::mutex graph_mutex_;
    std::unordered_map<int, std::vector<std::pair<int, std::string>>> adj_list_;
};

}  // namespace concurrency
