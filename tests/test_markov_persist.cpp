// Tests for the data side of the Markov predictor: MySQL tables, save/load of the learned
// counts, telemetry samples, recording, and the SQL views.
// The database name defaults to "pride_bank_test" and MUST contain "test" (tables are dropped).
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "db/db_connector.h"
#include "predict/predictor_service.h"

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) { std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n"; ++g_fail; } \
    } while (0)

static db::DBConnector& DB() { return db::DBConnector::getInstance(); }
static predict::PredictorService& SVC() { return predict::PredictorService::getInstance(); }
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

static bool runSqlFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open " << path << " (run from the project root)\n"; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return DB().execute(ss.str());
}

static long long count(const std::string& sql) {
    auto r = DB().query(sql);
    return r.empty() ? -1 : std::stoll(r[0]["n"]);
}

static void feedKnownCounts() {
    auto& m = SVC().model();
    m.clear();
    m.addTransition(0, 0, 40); m.addTransition(0, 1, 10);
    m.addTransition(1, 0, 8);  m.addTransition(1, 1, 20); m.addTransition(1, 2, 6);
    m.addTransition(2, 1, 4);  m.addTransition(2, 2, 12); m.addTransition(2, 3, 5);
}

static void testRoundTrip() {
    std::cout << "[1] Save then load gives the same counts and the same transition matrix\n";
    feedKnownCounts();
    auto C0 = SVC().model().counts();
    auto P0 = SVC().model().matrix();
    CHECK(SVC().saveCounts("t1"));
    SVC().model().clear();
    CHECK(SVC().model().totalTransitions() == 0);
    CHECK(SVC().loadCounts("t1", 1.0));
    auto C1 = SVC().model().counts();
    auto P1 = SVC().model().matrix();
    bool same = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) same = same && near(C0[i][j], C1[i][j]) && near(P0[i][j], P1[i][j]);
    CHECK(same);
    CHECK(near(SVC().model().expectedStepsToDeadlock(2), 0.0) == false);
}

static void testKeepFactor() {
    std::cout << "[2] Loading with keep=0.5 halves every stored count\n";
    feedKnownCounts();
    auto C0 = SVC().model().counts();
    CHECK(SVC().saveCounts("t2"));
    CHECK(SVC().loadCounts("t2", 0.5));
    auto C1 = SVC().model().counts();
    bool half = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) half = half && near(C1[i][j], C0[i][j] * 0.5);
    CHECK(half);
    CHECK(near(SVC().model().totalTransitions(), 0.5 * 105));
}

static void testOverwriteAndUnknown() {
    std::cout << "[3] A second save replaces the first; an unknown model changes nothing\n";
    feedKnownCounts();
    CHECK(SVC().saveCounts("t3"));
    SVC().model().clear();
    SVC().model().addTransition(0, 1, 7);
    CHECK(SVC().saveCounts("t3"));
    CHECK(count("SELECT COUNT(*) AS n FROM markov_counts WHERE model='t3';") == 12);
    CHECK(SVC().loadCounts("t3", 1.0));
    CHECK(near(SVC().model().totalTransitions(), 7));
    SVC().model().clear();
    SVC().model().addTransition(2, 3, 3);
    CHECK(!SVC().loadCounts("no_such_model", 1.0));
    CHECK(near(SVC().model().totalTransitions(), 3));            // untouched
    CHECK(!SVC().loadCounts("t3", 0.0));                         // keep must be positive
}

static std::vector<predict::Sample> makeRun(size_t n) {
    std::vector<predict::Sample> v;
    // idle, then a busy stretch that goes 0,1,1,2,1,2,3 and repeats, with one idle gap in the middle
    const int pattern[] = {0, 1, 1, 2, 1, 2, 3};
    for (size_t i = 0; i < n; ++i) {
        predict::Sample s;
        s.tSec = 0.01 * static_cast<double>(i + 1);
        const bool idle = (i < 5) || (i >= 100 && i < 110);
        s.state = idle ? 0 : pattern[i % 7];
        s.active = idle ? 0 : 4;
        s.waiting = s.state;
        s.maxChain = s.state;
        s.locked = 2;
        s.newDeadlocks = s.state == 3 ? 1 : 0;
        s.newWaits = s.state == 1 ? 1 : 0;
        v.push_back(s);
    }
    return v;
}

static void testSamplesAndViews() {
    std::cout << "[4] Samples are stored in batches; SQL views agree with a count done in C++\n";
    auto run = makeRun(250);
    CHECK(SVC().saveSamples("unit", 1, run));
    CHECK(count("SELECT COUNT(*) AS n FROM telemetry_sample WHERE dataset='unit' AND run_id=1;") == 250);
    CHECK(count("SELECT COUNT(*) AS n FROM telemetry_run WHERE dataset='unit' AND run_id=1;") == 1);
    CHECK(!DB().executeParameterized(          // CHECK constraint: state must be 0..3
        "INSERT INTO telemetry_sample (dataset,run_id,t_sec,active,waiting,max_chain,locked,new_deadlocks,new_waits,state)"
        " VALUES ('unit',1,1,1,1,1,1,0,0,9);", {}));

    auto rs = DB().query("SELECT samples, deadlocks FROM v_run_summary WHERE dataset='unit' AND run_id=1;");
    CHECK(rs.size() == 1 && rs[0]["samples"] == "250");
    int dl = 0;
    for (auto& s : run) dl += s.newDeadlocks;
    CHECK(!rs.empty() && std::stoi(rs[0]["deadlocks"]) == dl);

    double expect[3][4] = {};                  // same rules as the predictor
    for (size_t i = 1; i < run.size(); ++i) {
        const auto& a = run[i - 1];
        const auto& b = run[i];
        const bool aIdle = a.active == 0 && a.state == 0, bIdle = b.active == 0 && b.state == 0;
        if (!aIdle && !bIdle && a.state < 3) expect[a.state][b.state] += 1;
    }
    auto tr = DB().query("SELECT from_state, to_state, cnt FROM v_transition_counts WHERE dataset='unit';");
    double got[3][4] = {};
    for (auto& r : tr) got[std::stoi(r["from_state"])][std::stoi(r["to_state"])] = std::stod(r["cnt"]);
    bool ok = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) ok = ok && near(expect[i][j], got[i][j]);
    CHECK(ok);

    auto ss = DB().query("SELECT SUM(samples) AS n FROM v_state_summary WHERE dataset='unit';");
    CHECK(!ss.empty() && ss[0]["n"] == "250");
}

static void testRecording() {
    std::cout << "[5] Recording stores every sample, including the last partial batch\n";
    SVC().reset();
    SVC().startRecording("live", 7);
    for (auto& s : makeRun(450)) SVC().onSample(s);
    SVC().stopRecording();
    CHECK(count("SELECT COUNT(*) AS n FROM telemetry_sample WHERE dataset='live' AND run_id=7;") == 450);
    CHECK(count("SELECT COUNT(*) AS n FROM telemetry_run WHERE dataset='live' AND run_id=7;") == 1);
    SVC().onSample(makeRun(1)[0]);             // not recording any more
    CHECK(count("SELECT COUNT(*) AS n FROM telemetry_sample WHERE dataset='live';") == 450);
    SVC().stopRecording();                     // calling it twice is harmless
}

static void testCascade() {
    std::cout << "[6] Deleting a run removes its samples\n";
    CHECK(DB().execute("DELETE FROM telemetry_run WHERE dataset='unit' AND run_id=1;"));
    CHECK(count("SELECT COUNT(*) AS n FROM telemetry_sample WHERE dataset='unit';") == 0);
}

int main() {
    db::DBConfig cfg = db::DBConfig::fromEnv();
    if (!std::getenv("PRIDEBANK_DB_NAME")) cfg.database = "pride_bank_test";
    if (cfg.database.find("test") == std::string::npos && cfg.database.find("bench") == std::string::npos) {
        std::cerr << "Refusing to run: database '" << cfg.database << "' must contain 'test'.\n";
        return 2;
    }
    if (!DB().init(cfg)) { std::cerr << "cannot connect to MySQL\n"; return 2; }
    if (!runSqlFile("sql/drop_all.sql") || !runSqlFile("sql/schema.sql") ||
        !runSqlFile("sql/analysis_views.sql")) return 2;

    testRoundTrip();
    testKeepFactor();
    testOverwriteAndUnknown();
    testSamplesAndViews();
    testRecording();
    testCascade();

    if (g_fail) { std::cout << "\n" << g_fail << " CHECK(S) FAILED\n"; return 1; }
    std::cout << "\nALL PERSISTENCE TESTS PASSED\n";
    return 0;
}
