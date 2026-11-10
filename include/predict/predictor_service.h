#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
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

    // ---- storage in MySQL (tables telemetry_run, telemetry_sample, markov_counts) ----
    // Save the learned transition counts under a model name (overwrites the previous save).
    bool saveCounts(const std::string& model = "default");
    // Load a saved model. `keep` (0..1] scales every stored count, so old data weighs less than
    // what the system learns next. Replaces the current chain; returns false (and changes
    // nothing) if no such model is stored.
    bool loadCounts(const std::string& model = "default", double keep = 0.5);
    // Store samples of one run (creates the telemetry_run row if needed), in batches.
    bool saveSamples(const std::string& dataset, int runId, const std::vector<Sample>& samples);
    // Record every telemetry sample into MySQL until stopRecording() (flushed in batches of 200).
    void startRecording(const std::string& dataset, int runId);
    void stopRecording();

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

    void record(const Sample& s);
    std::mutex recMu_;
    bool recording_ = false;
    std::string recDataset_;
    int recRun_ = 0;
    std::vector<Sample> recBuf_;
};

}  // namespace predict
