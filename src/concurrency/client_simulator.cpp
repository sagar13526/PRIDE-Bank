#include "concurrency/client_simulator.h"
#include "concurrency/std_thread_compat.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace concurrency {

double StressStats::percentile(double p) const {
    if (latenciesMs.empty()) return 0.0;
    std::vector<double> v = latenciesMs;
    std::sort(v.begin(), v.end());
    const double pos = std::min(1.0, std::max(0.0, p)) * (v.size() - 1);
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - lo);
}

StressStats ClientSimulator::runStressTest(int numThreads, int numTransfersPerThread,
                                           integration::ExecutionMode mode) {
    Workload w;
    w.threads = numThreads;
    w.transfersPerThread = numTransfersPerThread;
    w.mode = mode;
    return run(w);
}

StressStats ClientSimulator::run(const Workload& w) {
    std::cout << "[ClientSimulator] Stress test: threads=" << w.threads
              << ", transfers/thread=" << w.transfersPerThread << ", accounts=" << w.accounts
              << ", hot=" << w.hotProb << std::endl;

    const int nAcc = std::max(2, w.accounts);
    auto accId = [](int i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "ACC%03d", i + 1);
        return std::string(buf);
    };
    const int hotN = std::max(2, std::min(w.hotAccounts, nAcc));
    std::atomic<int> committed{0}, failed{0}, retries{0}, victims{0};
    std::mutex latMu;
    std::vector<double> lat;
    lat.reserve(static_cast<size_t>(w.threads) * w.transfersPerThread);

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < w.threads; ++i) {
        threads.emplace_back([&, i]() {
            std::mt19937 rng(w.seed ? w.seed * 7919u + static_cast<unsigned>(i)
                                    : std::random_device{}() + static_cast<unsigned>(i));
            std::uniform_real_distribution<double> u(0.0, 1.0);
            std::uniform_int_distribution<int> allD(0, nAcc - 1), hotD(0, hotN - 1), prioD(0, 9);
            std::uniform_real_distribution<double> amtDist(10.0, 250.0);
            auto pick = [&] { return u(rng) < w.hotProb ? hotD(rng) : allD(rng); };
            std::vector<double> mine;

            for (int t = 0; t < w.transfersPerThread; ++t) {
                int f = pick(), to = pick();
                while (to == f) to = pick();
                double amount = std::round(amtDist(rng) * 100.0) / 100.0;
                int prio = w.randomPriority ? prioD(rng) : 5;

                auto r = integration::TransactionOrchestrator::getInstance().executeTransfer(
                    accId(f), accId(to), amount, w.mode, w.holdDelayMs, 3, prio);
                (r.success ? committed : failed)++;
                retries += r.retries;
                if (r.deadlockDetected) ++victims;
                mine.push_back(r.latencyMs);
            }
            std::lock_guard<std::mutex> lk(latMu);
            lat.insert(lat.end(), mine.begin(), mine.end());
        });
    }
    for (auto& t : threads) t.join();
    auto t1 = std::chrono::steady_clock::now();

    StressStats s;
    s.committed = committed;
    s.failed = failed;
    s.retries = retries;
    s.deadlockVictims = victims;
    s.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    s.latenciesMs = std::move(lat);
    std::cout << "[ClientSimulator] Done: committed=" << s.committed << " failed=" << s.failed
              << " retries=" << s.retries << " time=" << s.elapsedMs << "ms" << std::endl;
    return s;
}

void ClientSimulator::runForceDeadlockDemo() {
    std::cout << "[ClientSimulator] Forcing deadlock between 2 threads..." << std::endl;
    using integration::ExecutionMode;
    auto& orch = integration::TransactionOrchestrator::getInstance();

    std::thread t1([&] { orch.executeTransfer("ACC001", "ACC002", 100.0, ExecutionMode::REACTIVE_DFS, 150); });
    std::thread t2([&] { orch.executeTransfer("ACC002", "ACC001", 150.0, ExecutionMode::REACTIVE_DFS, 150); });
    t1.join();
    t2.join();
    std::cout << "[ClientSimulator] Force-deadlock demo complete." << std::endl;
}

}  // namespace concurrency
