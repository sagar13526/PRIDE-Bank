#include "predict/predictor_service.h"
#include <cmath>
#include <iomanip>
#include <sstream>

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
