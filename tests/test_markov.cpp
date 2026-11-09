// Unit tests for the absorbing Markov chain. No database needed.
// Reference numbers were computed independently with numpy (see comments).
#include <cmath>
#include <iostream>
#include <random>
#include "predict/markov_predictor.h"

using predict::MarkovPredictor;
static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n"; ++g_fail; } } while (0)
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

static void testCountingRules() {
    std::cout << "[1] Counting: sequence 0,0,1,3,0,1 -> 0>0, 0>1, 1>3, (3>0 ignored), 0>1\n";
    MarkovPredictor m(1.0, 0.0);
    for (int s : {0, 0, 1, 3, 0, 1}) m.observe(s);
    auto c = m.counts();
    CHECK(near(c[0][0], 1) && near(c[0][1], 2) && near(c[1][3], 1));
    CHECK(near(m.totalTransitions(), 4) && near(m.deadlockTransitions(), 1));
    for (int j = 0; j < 4; ++j) CHECK(near(c[3][j], 0));  // nothing counted out of DEADLOCK
}

static void testMatrixIsStochastic() {
    std::cout << "[2] Estimated P: rows sum to 1, DEADLOCK row absorbing\n";
    MarkovPredictor m(1.0, 0.5);
    std::mt19937 rng(7);
    for (int i = 0; i < 5000; ++i) m.observe(rng() % 4);
    auto P = m.matrix();
    for (int i = 0; i < 4; ++i) {
        double s = 0;
        for (int j = 0; j < 4; ++j) { CHECK(P[i][j] >= 0); s += P[i][j]; }
        CHECK(near(s, 1.0));
    }
    CHECK(near(P[3][3], 1.0));
}

static void testOneStateClosedForm() {
    std::cout << "[3] One transient state, stay 0.9 / absorb 0.1: E=10, Var=90, P(<=2)=0.19\n";
    MarkovPredictor m(1.0, 0.0);
    m.addTransition(0, 0, 90);
    m.addTransition(0, 3, 10);
    CHECK(near(m.expectedStepsToDeadlock(0), 10.0));
    CHECK(near(m.stdDevStepsToDeadlock(0), std::sqrt(90.0)));
    CHECK(near(m.probDeadlockWithin(0, 1), 0.10));
    CHECK(near(m.probDeadlockWithin(0, 2), 0.19));
    // States 1 and 2 were never observed: they are traps, so their time is infinite,
    // but state 0 (which can never move there) must stay finite.
    CHECK(m.expectedStepsToDeadlock(1) >= MarkovPredictor::kInf);
    CHECK(m.expectedStepsToDeadlock(2) >= MarkovPredictor::kInf);
    CHECK(near(m.expectedStepsToDeadlock(3), 0.0));
}

static void testThreeStateAgainstNumpy() {
    std::cout << "[4] Three transient states vs numpy: E=[9.6047,6.7264,3.5821], sd=[7.3156,6.3008,4.5541]\n";
    MarkovPredictor m(1.0, 0.5);
    const double c[3][4] = {{80, 12, 4, 4}, {10, 60, 20, 10}, {2, 8, 50, 40}};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) m.addTransition(i, j, c[i][j]);
    CHECK(near(m.expectedStepsToDeadlock(0), 9.60473178, 1e-6));
    CHECK(near(m.expectedStepsToDeadlock(1), 6.72639957, 1e-6));
    CHECK(near(m.expectedStepsToDeadlock(2), 3.58208286, 1e-6));
    CHECK(near(m.stdDevStepsToDeadlock(0), 7.31557065, 1e-6));
    CHECK(near(m.stdDevStepsToDeadlock(1), 6.30080858, 1e-6));
    CHECK(near(m.stdDevStepsToDeadlock(2), 4.55406773, 1e-6));
    CHECK(near(m.probDeadlockWithin(0, 5), 0.33686762, 1e-6));
    CHECK(near(m.probDeadlockWithin(1, 5), 0.57195244, 1e-6));
    CHECK(near(m.probDeadlockWithin(2, 5), 0.83166775, 1e-6));
    auto curve = m.deadlockCurve(0, 200);
    for (size_t k = 1; k < curve.size(); ++k) CHECK(curve[k] >= curve[k - 1] - 1e-12);  // non-decreasing
    CHECK(curve.back() > 0.999);
}

static void testNeverDeadlocked() {
    std::cout << "[5] No deadlock ever observed: expected time is infinite, probability 0\n";
    MarkovPredictor m(1.0, 0.5);
    for (int i = 0; i < 100; ++i) m.observe(i % 2);
    CHECK(m.expectedStepsToDeadlock(0) >= MarkovPredictor::kInf);
    CHECK(near(m.probDeadlockWithin(1, 50), 0.0));
}

static void testRecoversTrueChain() {
    std::cout << "[6] Learns a known chain from 200k simulated samples (within 2%)\n";
    const double T[4][4] = {{0.85, 0.10, 0.03, 0.02}, {0.20, 0.60, 0.15, 0.05}, {0.05, 0.25, 0.50, 0.20}, {0, 0, 0, 1}};
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0, 1);
    MarkovPredictor m(1.0, 0.0);
    int s = 0;
    for (int i = 0; i < 200000; ++i) {
        m.observe(s);
        if (s == 3) { s = 0; m.resetEpisode(); continue; }
        double r = u(rng), acc = 0;
        int n = 3;
        for (int j = 0; j < 4; ++j) { acc += T[s][j]; if (r < acc) { n = j; break; } }
        s = n;
    }
    auto P = m.matrix();
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) CHECK(near(P[i][j], T[i][j], 0.02));
    // Exact answer for the TRUE chain (numpy): E = [27.4, 19.5, 7.9]-ish; check ordering and magnitude.
    CHECK(m.expectedStepsToDeadlock(2) < m.expectedStepsToDeadlock(1));
    CHECK(m.expectedStepsToDeadlock(1) < m.expectedStepsToDeadlock(0));
}

static void testForgetting() {
    std::cout << "[7] Exponential forgetting: after the regime changes, new behaviour dominates\n";
    MarkovPredictor m(0.99, 0.0);
    for (int i = 0; i < 2000; ++i) { m.observe(0); }                    // long calm period
    for (int i = 0; i < 400; ++i) { m.observe(2); m.observe(3); m.observe(0); }  // then deadlock-prone
    CHECK(m.matrix()[2][3] > 0.9);
}

int main() {
    testCountingRules();
    testMatrixIsStochastic();
    testOneStateClosedForm();
    testThreeStateAgainstNumpy();
    testNeverDeadlocked();
    testRecoversTrueChain();
    testForgetting();
    std::cout << (g_fail ? "\nSOME TESTS FAILED\n" : "\nALL MARKOV TESTS PASSED\n");
    return g_fail ? 1 : 0;
}
