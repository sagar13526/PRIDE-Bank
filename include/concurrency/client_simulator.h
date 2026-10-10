#pragma once
#include "integration/transaction_orchestrator.h"

namespace concurrency {

struct StressStats {
    int committed = 0;
    int failed = 0;
    int retries = 0;          // transfers re-run after a deadlock-victim abort
    int deadlockVictims = 0;  // transfers that hit at least one victim abort
    double elapsedMs = 0.0;
};

// Spawns one std::thread per simulated user doing random transfers between
// ACC001..ACC005 (the seeded accounts).
class ClientSimulator {
public:
    static StressStats runStressTest(int numThreads, int numTransfersPerThread,
                                     integration::ExecutionMode mode);
    // Two threads transferring ACC001->ACC002 and ACC002->ACC001 at once, with a
    // long hold between the two lock requests so they deadlock on purpose.
    static void runForceDeadlockDemo();
};

}  // namespace concurrency
