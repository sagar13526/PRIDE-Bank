#include "concurrency/client_simulator.h"
#include "concurrency/std_thread_compat.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace concurrency {

StressStats ClientSimulator::runStressTest(int numThreads, int numTransfersPerThread,
                                           integration::ExecutionMode mode) {
    std::cout << "[ClientSimulator] Stress test: threads=" << numThreads
              << ", transfers/thread=" << numTransfersPerThread << std::endl;

    const std::vector<std::string> accounts = {"ACC001", "ACC002", "ACC003", "ACC004", "ACC005"};
    std::atomic<int> committed{0}, failed{0}, retries{0}, victims{0};

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            std::mt19937 rng(std::random_device{}() + static_cast<unsigned>(i));
            std::uniform_int_distribution<size_t> acctDist(0, accounts.size() - 1);
            std::uniform_real_distribution<double> amtDist(10.0, 250.0);

            for (int t = 0; t < numTransfersPerThread; ++t) {
                size_t f = acctDist(rng), to = acctDist(rng);
                while (to == f) to = acctDist(rng);
                double amount = std::round(amtDist(rng) * 100.0) / 100.0;

                auto r = integration::TransactionOrchestrator::getInstance()
                             .executeTransfer(accounts[f], accounts[to], amount, mode);
                (r.success ? committed : failed)++;
                retries += r.retries;
                if (r.deadlockDetected) ++victims;
            }
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
