#include "predict/telemetry.h"
#include "concurrency/deadlock_handler.h"
#include "concurrency/lock_manager.h"
#include "integration/transaction_orchestrator.h"
#include <fstream>

namespace predict {

int classifyState(const Sample& s, const ClassifierConfig& cfg) {
    if (s.newDeadlocks > 0) return 3;
    if (s.waiting <= 0) return 0;
    const bool chain = cfg.highChain > 0 && s.maxChain >= cfg.highChain;
    const bool crowd = s.waiting >= 2 && s.active > 0 &&
                       static_cast<double>(s.waiting) >= cfg.highWaitRatio * s.active;
    return (chain || crowd) ? 2 : 1;
}

const char* stateName(int s) {
    switch (s) {
        case 0: return "NORMAL";
        case 1: return "CONTENTION";
        case 2: return "HIGH_RISK";
        case 3: return "DEADLOCK";
    }
    return "?";
}

TelemetryMonitor& TelemetryMonitor::getInstance() {
    static TelemetryMonitor m;
    return m;
}

void TelemetryMonitor::setConfig(const ClassifierConfig& c) {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = c;
}
ClassifierConfig TelemetryMonitor::config() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cfg_;
}
void TelemetryMonitor::setListener(std::function<void(const Sample&)> fn) {
    std::lock_guard<std::mutex> lk(mu_);
    listener_ = std::move(fn);
}
Sample TelemetryMonitor::latest() const {
    std::lock_guard<std::mutex> lk(mu_);
    return last_;
}
std::vector<Sample> TelemetryMonitor::samples(size_t maxCount) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (maxCount == 0 || maxCount >= buf_.size()) return buf_;
    return std::vector<Sample>(buf_.end() - maxCount, buf_.end());
}
size_t TelemetryMonitor::sampleCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return buf_.size();
}
void TelemetryMonitor::clearSamples() {
    std::lock_guard<std::mutex> lk(mu_);
    buf_.clear();
}

void TelemetryMonitor::start(int intervalMs) {
    if (running_.exchange(true)) return;
    intervalMs_ = intervalMs < 1 ? 1 : intervalMs;
    th_ = std::thread([this] { loop(); });
}

void TelemetryMonitor::stop() {
    if (!running_.exchange(false)) return;
    { std::lock_guard<std::mutex> lk(sleepMu_); }
    sleepCv_.notify_all();
    if (th_.joinable()) th_.join();
}

void TelemetryMonitor::loop() {
    using clock = std::chrono::steady_clock;
    auto& locks = concurrency::LockManager::getInstance();
    auto& dh = concurrency::DeadlockHandler::getInstance();
    auto& orch = integration::TransactionOrchestrator::getInstance();
    const auto t0 = clock::now();
    unsigned long long prevWaits = locks.getStats().waitEvents;
    size_t prevDl = dh.deadlocksResolved();
    auto next = t0;

    while (running_.load()) {
        next += std::chrono::milliseconds(intervalMs_);
        {
            std::unique_lock<std::mutex> lk(sleepMu_);
            sleepCv_.wait_until(lk, next, [this] { return !running_.load(); });
        }
        if (!running_.load()) break;

        concurrency::LockStats st = locks.getStats();
        size_t dl = dh.deadlocksResolved();
        Sample s;
        s.tSec = std::chrono::duration<double>(clock::now() - t0).count();
        s.active = orch.activeTransfers();
        s.waiting = st.waiting;
        s.maxChain = st.maxChain;
        s.locked = st.locked;
        s.newDeadlocks = static_cast<int>(dl - prevDl);
        s.newWaits = static_cast<int>(st.waitEvents - prevWaits);
        prevDl = dl;
        prevWaits = st.waitEvents;

        std::function<void(const Sample&)> fn;
        {
            std::lock_guard<std::mutex> lk(mu_);
            s.state = classifyState(s, cfg_);
            last_ = s;
            if (buf_.size() >= kMaxSamples) buf_.erase(buf_.begin(), buf_.begin() + kMaxSamples / 10);
            buf_.push_back(s);
            fn = listener_;
        }
        if (fn) fn(s);
    }
}

bool TelemetryMonitor::exportCsv(const std::string& path, int runId, bool append) const {
    std::ofstream f(path, append ? std::ios::app : std::ios::trunc);
    if (!f) return false;
    if (!append) f << "run,tSec,active,waiting,maxChain,locked,newDeadlocks,newWaits,state\n";
    for (const auto& s : samples())
        f << runId << ',' << s.tSec << ',' << s.active << ',' << s.waiting << ',' << s.maxChain << ','
          << s.locked << ',' << s.newDeadlocks << ',' << s.newWaits << ',' << s.state << '\n';
    return true;
}

}  // namespace predict
