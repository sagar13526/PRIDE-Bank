#pragma once
#include <vector>
#include "integration/transaction_orchestrator.h"

namespace concurrency {

struct StressStats {
    int committed = 0;
    int failed = 0;
    int retries = 0;          // transfers re-run after a deadlock-victim abort
    int deadlockVictims = 0;  // transfers that hit at least one victim abort
    double elapsedMs = 0.0;
    std::vector<double> latenciesMs;  // per transfer, including retries
    double percentile(double p) const;  // p in [0,1]; 0 if no data
};

struct Workload {
    int threads = 8;
    int transfersPerThread = 25;
    integration::ExecutionMode mode = integration::ExecutionMode::REACTIVE_DFS;
    int accounts = 5;           // ACC001..ACCnnn must exist
    double hotProb = 0.0;       // probability that an endpoint is drawn from the hot set
    int hotAccounts = 2;        // size of the hot set (the first accounts)
    int holdDelayMs = 20;       // simulated work between the two lock requests
    bool randomPriority = false;  // priority 0..9 at random (else all 5)
    unsigned seed = 0;          // 0 = random
};

// Spawns one std::thread per simulated user doing random transfers between
// ACC001..ACC005 (the seeded accounts).
class ClientSimulator {
public:
    static StressStats runStressTest(int numThreads, int numTransfersPerThread,
                                     integration::ExecutionMode mode);
    static StressStats run(const Workload& w);
    // Two threads transferring ACC001->ACC002 and ACC002->ACC001 at once, with a
    // long hold between the two lock requests so they deadlock on purpose.
    static void runForceDeadlockDemo();
};

}  // namespace concurrency
