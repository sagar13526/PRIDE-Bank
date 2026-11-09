#include "predict/markov_predictor.h"
#include <algorithm>
#include <cmath>

namespace predict {

using Matrix = MarkovPredictor::Matrix;
static constexpr int S = MarkovPredictor::kStates;
static constexpr int D = MarkovPredictor::kDeadlock;
static constexpr int T = S - 1;  // number of transient states

MarkovPredictor::MarkovPredictor(double decay, double smoothing)
    : decay_(decay), smoothing_(smoothing) {}

void MarkovPredictor::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& r : counts_) r.fill(0.0);
    prev_ = -1;
}

void MarkovPredictor::resetEpisode() {
    std::lock_guard<std::mutex> lk(mu_);
    prev_ = -1;
}

void MarkovPredictor::addTransition(int from, int to, double w) {
    if (from < 0 || from >= T || to < 0 || to >= S || w <= 0) return;  // none out of DEADLOCK
    std::lock_guard<std::mutex> lk(mu_);
    counts_[from][to] += w;
}

void MarkovPredictor::observe(int state) {
    if (state < 0 || state >= S) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (prev_ >= 0 && prev_ < D) {
        if (decay_ < 1.0)
            for (auto& r : counts_) for (auto& c : r) c *= decay_;
        counts_[prev_][state] += 1.0;
    }
    prev_ = state;
}

int MarkovPredictor::currentState() const {
    std::lock_guard<std::mutex> lk(mu_);
    return prev_;
}

Matrix MarkovPredictor::estimate(const std::array<std::array<double, S>, S>& c, double smoothing) {
    Matrix P{};
    for (int i = 0; i < T; ++i) {
        double rowSum = 0;
        for (int j = 0; j < S; ++j) rowSum += c[i][j];
        if (rowSum <= 0) { P[i][i] = 1.0; continue; }  // never seen: unknown, treated as "stays"
        const double denom = rowSum + smoothing * T;
        for (int j = 0; j < S; ++j) P[i][j] = (c[i][j] + (j < D ? smoothing : 0.0)) / denom;
    }
    P[D][D] = 1.0;
    return P;
}

Matrix MarkovPredictor::matrix() const {
    std::lock_guard<std::mutex> lk(mu_);
    return estimate(counts_, smoothing_);
}

std::array<std::array<double, S>, S> MarkovPredictor::counts() const {
    std::lock_guard<std::mutex> lk(mu_);
    return counts_;
}

double MarkovPredictor::rowSamples(int state) const {
    if (state < 0 || state >= S) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    double s = 0;
    for (int j = 0; j < S; ++j) s += counts_[state][j];
    return s;
}

double MarkovPredictor::totalTransitions() const {
    std::lock_guard<std::mutex> lk(mu_);
    double s = 0;
    for (auto& r : counts_) for (double c : r) s += c;
    return s;
}

double MarkovPredictor::deadlockTransitions() const {
    std::lock_guard<std::mutex> lk(mu_);
    double s = 0;
    for (int i = 0; i < T; ++i) s += counts_[i][D];
    return s;
}

// Solves A x = B for several right-hand sides, Gauss-Jordan with partial pivoting.
// Returns false if A is (numerically) singular.
static bool solve(std::vector<std::vector<double>> A, std::vector<std::vector<double>>& X) {
    const int n = static_cast<int>(A.size());
    const int m = static_cast<int>(X.front().size());
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r) if (std::fabs(A[r][c]) > std::fabs(A[piv][c])) piv = r;
        if (std::fabs(A[piv][c]) < 1e-12) return false;
        std::swap(A[c], A[piv]);
        std::swap(X[c], X[piv]);
        const double d = A[c][c];
        for (int k = 0; k < n; ++k) A[c][k] /= d;
        for (int k = 0; k < m; ++k) X[c][k] /= d;
        for (int r = 0; r < n; ++r) {
            if (r == c) continue;
            const double f = A[r][c];
            if (f == 0) continue;
            for (int k = 0; k < n; ++k) A[r][k] -= f * A[c][k];
            for (int k = 0; k < m; ++k) X[r][k] -= f * X[c][k];
        }
    }
    return true;
}

bool MarkovPredictor::expectedSteps(const Matrix& P, std::array<double, S>& steps,
                                    std::array<double, S>& variance) {
    steps.fill(kInf);
    variance.fill(kInf);
    steps[D] = 0;
    variance[D] = 0;

    // reach[i]: can state i reach DEADLOCK through transitions with positive probability?
    std::array<bool, S> reach{};
    reach[D] = true;
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < T; ++i)
            if (!reach[i])
                for (int j = 0; j < S; ++j)
                    if (P[i][j] > 0 && reach[j]) { reach[i] = true; changed = true; break; }
    }
    // A state has a finite expected time only if it can never wander into a state that
    // cannot reach DEADLOCK (such a state would be a trap and the time would be infinite).
    std::array<bool, S> fin{};
    for (int i = 0; i < T; ++i) fin[i] = reach[i];
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < T; ++i)
            if (fin[i])
                for (int j = 0; j < T; ++j)
                    if (P[i][j] > 0 && !fin[j]) { fin[i] = false; changed = true; break; }
    }
    std::vector<int> idx;
    for (int i = 0; i < T; ++i) if (fin[i]) idx.push_back(i);
    const int n = static_cast<int>(idx.size());
    if (n == 0) return true;

    // (I - Q_F) t = 1   and   N = (I - Q_F)^-1  (needed for the variance)
    std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0));
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b) A[a][b] = (a == b ? 1.0 : 0.0) - P[idx[a]][idx[b]];
    std::vector<std::vector<double>> X(n, std::vector<double>(n, 0.0));
    for (int a = 0; a < n; ++a) X[a][a] = 1.0;
    if (!solve(A, X)) return false;  // cannot happen for a valid P; kept as a guard

    std::vector<double> t(n, 0.0);
    for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) t[a] += X[a][b];
    for (int a = 0; a < n; ++a) {
        double nt = 0;  // (N t)[a]
        for (int b = 0; b < n; ++b) nt += X[a][b] * t[b];
        steps[idx[a]] = t[a];
        variance[idx[a]] = std::max(0.0, 2.0 * nt - t[a] - t[a] * t[a]);  // (2N - I)t - t o t
    }
    return true;
}

double MarkovPredictor::expectedStepsToDeadlock(int state) const {
    if (state < 0 || state >= S) return kInf;
    std::array<double, S> st, var;
    expectedSteps(matrix(), st, var);
    return st[state];
}

double MarkovPredictor::stdDevStepsToDeadlock(int state) const {
    if (state < 0 || state >= S) return kInf;
    std::array<double, S> st, var;
    expectedSteps(matrix(), st, var);
    return var[state] >= kInf ? kInf : std::sqrt(var[state]);
}

std::vector<double> MarkovPredictor::deadlockCurve(int state, int maxSteps) const {
    std::vector<double> out;
    if (state < 0 || state >= S || maxSteps <= 0) return out;
    const Matrix P = matrix();
    std::array<double, S> v{};
    v[state] = 1.0;
    for (int k = 1; k <= maxSteps; ++k) {
        std::array<double, S> w{};
        for (int i = 0; i < S; ++i)
            if (v[i] != 0) for (int j = 0; j < S; ++j) w[j] += v[i] * P[i][j];
        v = w;
        out.push_back(v[D]);
    }
    return out;
}

double MarkovPredictor::probDeadlockWithin(int state, int steps) const {
    auto c = deadlockCurve(state, steps);
    return c.empty() ? 0.0 : c.back();
}

}  // namespace predict
