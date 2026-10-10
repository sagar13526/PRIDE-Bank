#include "concurrency/wait_for_graph.h"
#include <algorithm>

namespace concurrency {

WaitForGraph& WaitForGraph::getInstance() {
    static WaitForGraph instance;
    return instance;
}

void WaitForGraph::addEdge(int waitingTxnId, int holdingTxnId, const std::string& accountId) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    if (waitingTxnId == holdingTxnId) return;

    auto& neighbors = adj_list_[waitingTxnId];
    for (const auto& p : neighbors) {
        if (p.first == holdingTxnId && p.second == accountId) return;
    }
    neighbors.push_back({holdingTxnId, accountId});
}

void WaitForGraph::removeEdge(int waitingTxnId, int holdingTxnId) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    auto it = adj_list_.find(waitingTxnId);
    if (it == adj_list_.end()) return;

    auto& neighbors = it->second;
    neighbors.erase(std::remove_if(neighbors.begin(), neighbors.end(),
                        [holdingTxnId](const std::pair<int, std::string>& p) {
                            return p.first == holdingTxnId;
                        }),
                    neighbors.end());
    if (neighbors.empty()) adj_list_.erase(it);
}

void WaitForGraph::removeOutgoingEdges(int txnId) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    adj_list_.erase(txnId);
}

void WaitForGraph::removeAllEdgesForTxn(int txnId) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    adj_list_.erase(txnId);
    for (auto it = adj_list_.begin(); it != adj_list_.end();) {
        auto& neighbors = it->second;
        neighbors.erase(std::remove_if(neighbors.begin(), neighbors.end(),
                            [txnId](const std::pair<int, std::string>& p) {
                                return p.first == txnId;
                            }),
                        neighbors.end());
        if (neighbors.empty()) it = adj_list_.erase(it);
        else ++it;
    }
}

bool WaitForGraph::dfsCycleCheck(int node, std::unordered_map<int, bool>& visited,
                                 std::unordered_map<int, bool>& recStack,
                                 std::vector<int>& currentPath, std::vector<int>& outCycle) {
    visited[node] = true;
    recStack[node] = true;
    currentPath.push_back(node);

    auto it = adj_list_.find(node);
    if (it != adj_list_.end()) {
        for (const auto& edge : it->second) {
            int neighbor = edge.first;
            if (!visited[neighbor]) {
                if (dfsCycleCheck(neighbor, visited, recStack, currentPath, outCycle)) return true;
            } else if (recStack[neighbor]) {
                auto start = std::find(currentPath.begin(), currentPath.end(), neighbor);
                outCycle.assign(start, currentPath.end());
                return true;
            }
        }
    }

    recStack[node] = false;
    currentPath.pop_back();
    return false;
}

bool WaitForGraph::detectCycle(std::vector<int>& outCycle) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    outCycle.clear();

    std::unordered_map<int, bool> visited, recStack;
    std::vector<int> currentPath;
    for (const auto& kv : adj_list_) {
        if (!visited[kv.first] &&
            dfsCycleCheck(kv.first, visited, recStack, currentPath, outCycle))
            return true;
    }
    return false;
}

bool WaitForGraph::dfsReach(int node, int target, std::unordered_set<int>& visited,
                            std::vector<int>& path) {
    visited.insert(node);
    path.push_back(node);
    auto it = adj_list_.find(node);
    if (it != adj_list_.end()) {
        for (const auto& edge : it->second) {
            int next = edge.first;
            if (next == target) return true;  // back at start: cycle
            if (!visited.count(next) && dfsReach(next, target, visited, path)) return true;
        }
    }
    path.pop_back();
    return false;
}

bool WaitForGraph::detectCycleFrom(int startTxnId, std::vector<int>& outCycle) {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    std::unordered_set<int> visited;
    std::vector<int> path;
    if (dfsReach(startTxnId, startTxnId, visited, path)) {
        outCycle = std::move(path);
        return true;
    }
    outCycle.clear();
    return false;
}

std::vector<Edge> WaitForGraph::getActiveEdges() {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    std::vector<Edge> edges;
    for (const auto& kv : adj_list_)
        for (const auto& p : kv.second) edges.push_back({kv.first, p.first, p.second});
    return edges;
}

std::unordered_set<int> WaitForGraph::getActiveNodes() {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    std::unordered_set<int> nodes;
    for (const auto& kv : adj_list_) {
        nodes.insert(kv.first);
        for (const auto& p : kv.second) nodes.insert(p.first);
    }
    return nodes;
}

void WaitForGraph::clear() {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    adj_list_.clear();
}

}  // namespace concurrency
