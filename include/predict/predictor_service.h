#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include "predict/markov_predictor.h"
#include "predict/telemetry.h"

namespace predict {

struct HybridConfig {
    int horizon = 5;         // samples ahead (10 ms each) the risk is computed over
    double hi = 0.60;        // switch to Banker's when P(deadlock within horizon) >= hi
    double lo = 0.35;        // switch back to detection when it falls to <= lo (hysteresis)
    int minSamples = 200;    // transitions needed before the predictor may switch anything on
    bool enabled = true;
};

// Glue between telemetry, the Markov chain and the lock mode. Every sample updates the
// chain; the risk of the CURRENT state drives a two-threshold switch.
class PredictorService {
public:
    static PredictorService& getInstance();

    void attach();                  // registers the telemetry listener (idempotent)
    void onSample(const Sample& s);

    // Used by the orchestrator for every PREDICTIVE_HYBRID transfer.
    bool shouldAvoid() const { int o = override_.load(); return o >= 0 ? o == 1 : avoid_.load(); }
    void forceMode(int v) { override_ = v; }   // -1 = predictor decides, 0 = detect, 1 = Banker's (tests/demo)

    MarkovPredictor& model() { return model_; }
    HybridConfig config() const;
    void setConfig(const HybridConfig& c);
    void reset();                   // forget the learned chain and the switch state

    double currentRisk() const { return risk_.load(); }
    unsigned long long switches() const { return switches_.load(); }
    unsigned long long samplesSeen() const { return seen_.load(); }

    // Status as JSON text (matrix, counts, per-state expected time and risk, switch state).
    std::string statusJson() const;

private:
    PredictorService() : model_(0.9999, 0.5) {}
    MarkovPredictor model_;
    mutable std::mutex mu_;
    HybridConfig cfg_;
    std::atomic<bool> avoid_{false};
    std::atomic<double> risk_{0.0};
    std::atomic<unsigned long long> switches_{0}, seen_{0};
    std::atomic<bool> attached_{false};
    std::atomic<int> override_{-1};
};

}  // namespace predict
