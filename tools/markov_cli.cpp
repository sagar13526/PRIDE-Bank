// Prints the absorbing-chain answers for a 3x4 count matrix given on the command line.
//   markov_cli <12 counts row-major> [smoothing=0.5] [horizon=20]
// Used to cross-check the C++ model against the Python validation (numpy).
#include <cstdio>
#include <cstdlib>
#include "predict/markov_predictor.h"
int main(int argc, char** argv) {
    if (argc < 13) { std::fprintf(stderr, "need 12 counts\n"); return 2; }
    const double sm = argc > 13 ? std::atof(argv[13]) : 0.5;
    const int k = argc > 14 ? std::atoi(argv[14]) : 20;
    predict::MarkovPredictor m(1.0, sm);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) m.addTransition(i, j, std::atof(argv[1 + i * 4 + j]));
    for (int s = 0; s < 3; ++s)
        std::printf("%d %.9g %.9g %.9g\n", s, m.expectedStepsToDeadlock(s), m.stdDevStepsToDeadlock(s), m.probDeadlockWithin(s, k));
    return 0;
}
