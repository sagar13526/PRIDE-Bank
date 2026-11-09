#pragma once
#include <array>
#include <mutex>
#include <vector>

namespace predict {

// Absorbing Markov chain over system load states.
//   0 NORMAL      no transaction is blocked
//   1 CONTENTION  some transactions wait for a lock
//   2 HIGH_RISK   long wait chains or many waiters (a cycle can close any moment)
//   3 DEADLOCK    a deadlock was just resolved  (absorbing)
//
// The transition matrix P is ESTIMATED from observed state sequences (maximum
// likelihood: P[i][j] = count(i->j) / count(i->*), with optional exponential forgetting
// and a small additive smoothing on the transient columns). The answers then come from
// standard absorbing-chain theory, not from a fitted curve:
//   Q  = transient block of P (states 0..2),  R = column to the absorbing state
//   N  = (I - Q)^-1              expected visits to each transient state before absorption
//   t  = N * 1                   expected steps to deadlock
//   var = (2N - I) t - t o t     variance of the steps to deadlock
//   P(deadlock within k steps) = (P^k)[s][3]
// A state from which a deadlock is never observed has an INFINITE expected time; this is
// reported as such, not hidden behind smoothing.
class MarkovPredictor {
public:
    static constexpr int kStates = 4;
    static constexpr int kDeadlock = 3;
    static constexpr double kInf = 1e18;

    using Matrix = std::array<std::array<double, kStates>, kStates>;

    // decay: every observed transition first multiplies all counts by this (1 = no forgetting).
    // smoothing: pseudo-count added to each TRANSIENT target (0..2) of a row that has data.
    explicit MarkovPredictor(double decay = 1.0, double smoothing = 0.5);

    // Feed the state sequence one sample at a time. A transition out of DEADLOCK is not
    // counted (the chain is absorbed; the next sample starts a new episode).
    void observe(int state);
    void addTransition(int from, int to, double weight = 1.0);  // bulk / offline training
    void resetEpisode();                                        // forget the "previous state"
    void clear();

    // ---- model outputs (all thread-safe) ----
    Matrix matrix() const;                                   // estimated P
    std::array<std::array<double, kStates>, kStates> counts() const;
    double rowSamples(int state) const;                      // observations behind row `state`
    double totalTransitions() const;
    double deadlockTransitions() const;                      // observed X -> DEADLOCK count

    double expectedStepsToDeadlock(int state) const;         // kInf if never reaches it
    double stdDevStepsToDeadlock(int state) const;           // kInf if infinite
    double probDeadlockWithin(int state, int steps) const;   // (P^k)[state][3]
    std::vector<double> deadlockCurve(int state, int maxSteps) const;  // k = 1..maxSteps

    int currentState() const;
    double decay() const { return decay_; }
    double smoothing() const { return smoothing_; }

    // Pure helpers, exposed for the tests.
    static Matrix estimate(const std::array<std::array<double, kStates>, kStates>& counts, double smoothing);
    static bool expectedSteps(const Matrix& P, std::array<double, kStates>& steps,
                              std::array<double, kStates>& variance);

private:
    mutable std::mutex mu_;
    double decay_, smoothing_;
    std::array<std::array<double, kStates>, kStates> counts_{};
    int prev_ = -1;
};

}  // namespace predict
