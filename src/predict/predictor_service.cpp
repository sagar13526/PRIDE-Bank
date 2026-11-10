#include "predict/predictor_service.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "db/db_connector.h"

namespace predict {

PredictorService& PredictorService::getInstance() {
    static PredictorService s;
    return s;
}

void PredictorService::attach() {
    if (attached_.exchange(true)) return;
    TelemetryMonitor::getInstance().setListener([](const Sample& s) { getInstance().onSample(s); });
}

HybridConfig PredictorService::config() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cfg_;
}

void PredictorService::setConfig(const HybridConfig& c) {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = c;
    if (cfg_.lo > cfg_.hi) cfg_.lo = cfg_.hi;
    if (cfg_.horizon < 1) cfg_.horizon = 1;
}

void PredictorService::reset() {
    model_.clear();
    avoid_ = false;
    risk_ = 0.0;
    seen_ = 0;
}

void PredictorService::onSample(const Sample& s) {
    record(s);
    HybridConfig cfg = config();
    // Idle time says nothing about deadlocks and would drown the chain in NORMAL->NORMAL
    // self-loops: an empty system just ends the episode.
    if (s.active == 0 && s.state == 0) {
        model_.resetEpisode();
        risk_ = 0.0;
        if (avoid_.exchange(false)) ++switches_;
        return;
    }
    model_.observe(s.state);
    ++seen_;
    if (model_.totalTransitions() < cfg.minSamples) { risk_ = 0.0; return; }
    const double r = model_.probDeadlockWithin(s.state, cfg.horizon);
    risk_ = r;
    if (!cfg.enabled) { avoid_ = false; return; }
    const bool was = avoid_.load();
    // Hysteresis: two thresholds stop the system flapping between modes.
    if (!was && r >= cfg.hi) { avoid_ = true; ++switches_; }
    else if (was && r <= cfg.lo) { avoid_ = false; ++switches_; }
}

// ---------------------------------------------------------------- MySQL storage

static std::string dstr(double v) {
    std::ostringstream o;
    o << std::setprecision(12) << v;
    return o.str();
}

bool PredictorService::saveCounts(const std::string& model) {
    const auto C = model_.counts();
    std::string sql = "REPLACE INTO markov_counts (model, from_state, to_state, cnt) VALUES ";
    std::vector<std::string> p;
    bool first = true;
    for (int i = 0; i < MarkovPredictor::kDeadlock; ++i)          // DEADLOCK is absorbing: no row
        for (int j = 0; j < MarkovPredictor::kStates; ++j) {
            sql += first ? "(?,?,?,?)" : ",(?,?,?,?)";
            first = false;
            p.push_back(model);
            p.push_back(std::to_string(i));
            p.push_back(std::to_string(j));
            p.push_back(dstr(C[i][j]));
        }
    return db::DBConnector::getInstance().executeParameterized(sql, p);   // one statement = atomic
}

bool PredictorService::loadCounts(const std::string& model, double keep) {
    if (!(keep > 0.0)) return false;
    if (keep > 1.0) keep = 1.0;
    auto rows = db::DBConnector::getInstance().queryParameterized(
        "SELECT from_state, to_state, cnt FROM markov_counts WHERE model = ?;", {model});
    if (rows.empty()) return false;
    model_.clear();
    for (auto& r : rows)
        model_.addTransition(std::stoi(r["from_state"]), std::stoi(r["to_state"]), std::stod(r["cnt"]) * keep);
    return true;
}

bool PredictorService::saveSamples(const std::string& dataset, int runId, const std::vector<Sample>& samples) {
    auto& d = db::DBConnector::getInstance();
    if (!d.executeParameterized("INSERT IGNORE INTO telemetry_run (dataset, run_id) VALUES (?, ?);",
                                {dataset, std::to_string(runId)}))
        return false;
    const size_t kBatch = 100;
    for (size_t at = 0; at < samples.size(); at += kBatch) {
        const size_t end = std::min(samples.size(), at + kBatch);
        std::string sql =
            "INSERT INTO telemetry_sample (dataset, run_id, t_sec, active, waiting, max_chain, locked, "
            "new_deadlocks, new_waits, state) VALUES ";
        std::vector<std::string> p;
        for (size_t k = at; k < end; ++k) {
            const Sample& s = samples[k];
            sql += k == at ? "(?,?,?,?,?,?,?,?,?,?)" : ",(?,?,?,?,?,?,?,?,?,?)";
            p.push_back(dataset);
            p.push_back(std::to_string(runId));
            p.push_back(dstr(s.tSec));
            p.push_back(std::to_string(s.active));
            p.push_back(std::to_string(s.waiting));
            p.push_back(std::to_string(s.maxChain));
            p.push_back(std::to_string(s.locked));
            p.push_back(std::to_string(s.newDeadlocks));
            p.push_back(std::to_string(s.newWaits));
            p.push_back(std::to_string(s.state));
        }
        if (!d.executeParameterized(sql, p)) return false;
    }
    return true;
}

void PredictorService::startRecording(const std::string& dataset, int runId) {
    std::lock_guard<std::mutex> lk(recMu_);
    recording_ = true;
    recDataset_ = dataset;
    recRun_ = runId;
    recBuf_.clear();
}

void PredictorService::stopRecording() {
    std::vector<Sample> rest;
    std::string ds;
    int run;
    {
        std::lock_guard<std::mutex> lk(recMu_);
        if (!recording_) return;
        recording_ = false;
        rest.swap(recBuf_);
        ds = recDataset_;
        run = recRun_;
    }
    if (!rest.empty()) saveSamples(ds, run, rest);
}

void PredictorService::record(const Sample& s) {
    std::vector<Sample> full;
    std::string ds;
    int run = 0;
    {
        std::lock_guard<std::mutex> lk(recMu_);
        if (!recording_) return;
        recBuf_.push_back(s);
        if (recBuf_.size() < 200) return;
        full.swap(recBuf_);
        ds = recDataset_;
        run = recRun_;
    }
    saveSamples(ds, run, full);   // outside the lock; the DB connector serializes its own calls
}

static void num(std::ostringstream& o, double v) {
    if (v >= MarkovPredictor::kInf) o << "null";
    else o << std::setprecision(6) << v;
}

std::string PredictorService::statusJson() const {
    HybridConfig cfg = config();
    auto P = const_cast<MarkovPredictor&>(model_).matrix();
    auto C = model_.counts();
    std::ostringstream o;
    o << "{\"samples\":" << seen_.load() << ",\"transitions\":" << model_.totalTransitions()
      << ",\"deadlockTransitions\":" << model_.deadlockTransitions()
      << ",\"currentState\":" << model_.currentState()
      << ",\"risk\":" << risk_.load() << ",\"avoiding\":" << (avoid_.load() ? "true" : "false")
      << ",\"switches\":" << switches_.load()
      << ",\"config\":{\"horizon\":" << cfg.horizon << ",\"hi\":" << cfg.hi << ",\"lo\":" << cfg.lo
      << ",\"minSamples\":" << cfg.minSamples << ",\"enabled\":" << (cfg.enabled ? "true" : "false") << "}"
      << ",\"intervalMs\":" << TelemetryMonitor::getInstance().intervalMs()
      << ",\"states\":[\"NORMAL\",\"CONTENTION\",\"HIGH_RISK\",\"DEADLOCK\"],\"matrix\":[";
    for (int i = 0; i < 4; ++i) {
        o << (i ? "," : "") << "[";
        for (int j = 0; j < 4; ++j) { o << (j ? "," : ""); num(o, P[i][j]); }
        o << "]";
    }
    o << "],\"counts\":[";
    for (int i = 0; i < 4; ++i) {
        o << (i ? "," : "") << "[";
        for (int j = 0; j < 4; ++j) { o << (j ? "," : ""); num(o, C[i][j]); }
        o << "]";
    }
    o << "],\"perState\":[";
    for (int i = 0; i < 4; ++i) {
        const double e = model_.expectedStepsToDeadlock(i);
        const double sd = model_.stdDevStepsToDeadlock(i);
        o << (i ? "," : "") << "{\"expectedSteps\":"; num(o, e);
        o << ",\"stdDevSteps\":"; num(o, sd);
        o << ",\"expectedMs\":"; num(o, e >= MarkovPredictor::kInf ? e : e * TelemetryMonitor::getInstance().intervalMs());
        o << ",\"riskWithinHorizon\":" << model_.probDeadlockWithin(i, cfg.horizon)
          << ",\"rowSamples\":" << model_.rowSamples(i) << "}";
    }
    o << "]}";
    return o.str();
}

}  // namespace predict
