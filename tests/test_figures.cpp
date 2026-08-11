// Regression test against the worked examples the organizers published.
//
// Figures 4 and 5 of the problem statement are SVGs that encode the exact
// building polygons, the exact antenna positions, and -- crucially -- the
// per-building coverage percentage the organizers computed for each. That makes
// them a ground-truth oracle for the visibility engine: if we reproduce all 30
// published percentages and both stated service scores, the engine agrees with
// the organizers' own interpretation of Definitions 1-4.
//
// This is the test that pins down the two subtleties that are easy to get
// backwards: a vertex antenna covers both incident edges (grazing does not
// block), and a building's own interior *does* occlude its own boundary.
#include "../src/geojson.hpp"
#include "../src/coverage.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace gc;

int main(int argc, char** argv) {
    const char* fixture = argc > 1 ? argv[1] : "tests/figures_groundtruth.txt";
    std::ifstream in(fixture);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", fixture); return 2; }

    int failures = 0;
    std::string tok;
    while (in >> tok) {
        if (tok != "FIGURE") continue;
        std::string name;
        double tau;
        int expect_score;
        in >> name >> tau >> expect_score;

        int nb;
        in >> tok >> nb;  // "NB"
        std::vector<std::vector<Vec2>> rings;
        std::vector<std::string> ids;
        for (int i = 0; i < nb; ++i) {
            int n;
            in >> tok >> n;  // "B"
            std::vector<Vec2> r(n);
            for (int j = 0; j < n; ++j) in >> r[j].x >> r[j].y;
            rings.push_back(r);
            ids.push_back(std::to_string(i + 1));
        }
        int na;
        in >> tok >> na;  // "NA"
        std::vector<Vec2> ants(na);
        for (int i = 0; i < na; ++i) { in >> tok >> ants[i].x >> ants[i].y; }
        in >> tok;  // "COV"
        std::vector<double> expect(nb);
        for (int i = 0; i < nb; ++i) in >> expect[i];

        Scene sc;
        sc.build_from_rings(rings, ids);
        sc.build_index(50.0);
        Visibility vis(sc);
        Evaluator ev(sc, vis);
        auto cov = ev.coverage(ants, -1.0);  // uncapped
        int score = Evaluator::service_score(cov, tau);

        std::printf("\n%s  (tau=%.2f, k=%d)\n", name.c_str(), tau, na);
        std::printf("  %-4s %10s %10s %8s\n", "bldg", "expected", "computed", "delta");
        int bad = 0;
        for (int i = 0; i < nb; ++i) {
            // The published labels are rounded to 0.1%, so that is the tolerance.
            double d = std::fabs(cov[i] - expect[i]);
            bool ok = d <= 5e-4 + 1e-9;
            if (!ok) ++bad;
            std::printf("  %-4d %9.1f%% %9.1f%% %7.3f%% %s\n", i + 1, 100 * expect[i],
                        100 * cov[i], 100 * d, ok ? "" : "  <-- MISMATCH");
        }
        std::printf("  service score: expected %d, computed %d  %s\n", expect_score, score,
                    score == expect_score ? "OK" : "<-- MISMATCH");
        if (bad || score != expect_score) ++failures;
    }
    std::printf("\n%s\n", failures == 0 ? "ALL FIGURE TESTS PASSED" : "FIGURE TESTS FAILED");
    return failures ? 1 : 0;
}
