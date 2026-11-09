#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace predict {

// One observation of the lock system.
struct Sample {
    double tSec = 0;          // seconds since the monitor started
    int active = 0;           // transfers in flight
    int waiting = 0;          // transactions blocked behind a lock owner
    int maxChain = 0;         // longest wait chain
    int locked = 0;           // accounts currently locked
    int newDeadlocks = 0;     // deadlocks resolved since the previous sample
    int newWaits = 0;         // transactions that started waiting since the previous sample
    int state = 0;            // 0 NORMAL, 1 CONTENTION, 2 HIGH_RISK, 3 DEADLOCK
};

// Thresholds that map raw signals to a load state. They are plain numbers on purpose:
// the chain learns what each state *means* for deadlocks from data, the classifier only
// has to put similar load levels into the same bucket.
struct ClassifierConfig {
    int capacity = 16;          // thread count the system is sized for (reported, not used for the state)
    int highChain = 0;          // wait chain at least this long -> HIGH_RISK (0 = off; see note)
    double highWaitRatio = 0.75; // waiting / active at least this -> HIGH_RISK (needs >= 2 waiters)
    // Note: on the collected data a long wait chain did NOT predict a deadlock (long chains are
    // acyclic by definition); the number of blocked transactions did. Hence ratio-based, chain off.
};

int classifyState(const Sample& s, const ClassifierConfig& cfg);
const char* stateName(int state);

// Background sampler. Reads LockManager statistics and the deadlock counter every
// intervalMs, classifies the state and hands it to a listener (the online predictor).
class TelemetryMonitor {
public:
    static TelemetryMonitor& getInstance();

    void start(int intervalMs = 10);
    void stop();
    bool running() const { return running_.load(); }

    void setConfig(const ClassifierConfig& c);
    ClassifierConfig config() const;
    int intervalMs() const { return intervalMs_; }

    // Called from the sampler thread for every sample (keep it fast).
    void setListener(std::function<void(const Sample&)> fn);

    Sample latest() const;
    std::vector<Sample> samples(size_t maxCount = 0) const;  // newest last
    size_t sampleCount() const;
    void clearSamples();

    // CSV: run,tSec,active,waiting,maxChain,locked,newDeadlocks,newWaits,state
    bool exportCsv(const std::string& path, int runId, bool append) const;

private:
    TelemetryMonitor() = default;
    void loop();

    mutable std::mutex mu_;
    std::vector<Sample> buf_;          // bounded ring (oldest dropped)
    std::function<void(const Sample&)> listener_;
    ClassifierConfig cfg_;
    Sample last_;
    std::thread th_;
    std::atomic<bool> running_{false};
    std::mutex sleepMu_;
    std::condition_variable sleepCv_;
    int intervalMs_ = 10;
    static constexpr size_t kMaxSamples = 200000;
};

}  // namespace predict
