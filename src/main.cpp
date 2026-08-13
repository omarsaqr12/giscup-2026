// SIGSPATIAL 2026 GIS Cup -- antenna placement.
//
// Pipeline:
//   load footprints -> candidate sites -> visibility contribution map (once,
//   shared by all nine sub-problems) -> per-(tau,k) selection -> exact uncapped
//   verification -> submission file.
//
// The search runs against a radius-capped contribution map for speed; the
// buildings we actually *claim* are decided by a final uncapped sweep, so the
// submission never asserts coverage the search only approximated.
#include "geojson.hpp"
#include "pipeline.hpp"
#include "solvers.hpp"
#include "bruteforce.hpp"
#include "archive.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <fstream>
#include <map>

using namespace gc;

static double now_s() {
    static auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

static std::vector<double> parse_list(const std::string& s) {
    std::vector<double> v;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        v.push_back(std::atof(s.substr(i, j - i).c_str()));
        i = j + 1;
    }
    return v;
}

struct Args {
    std::string data = "data/GIS-cup-sample-dataset.geojson";
    std::string out = "submission.txt";
    std::string cmd = "solve";
    std::vector<double> taus{0.25, 0.5, 0.75};
    std::vector<double> ks{50, 500, 1000};
    double radius = 300.0;
    double min_frac = 0.01;
    double edge_spacing = 0.0;
    double lns_sec = 20.0;
    double verify_radius = -1.0;  // <=0 means uncapped (exact)
    double cell = 40.0;
    Algo algo = Algo::FocusLNS;
    double power = 1.0;
    bool normalise = true;
    unsigned seed = 12345;
    bool archive_add = false;
    double claim_epsilon = 0.0;   // required margin above tau before claiming
    std::string archive_dir = "archive";
    std::string method = "solve";
    std::string placement;
    std::string from_submission;
    std::string rewrite;
    int subset = 0;
    bool verbose = true;
    bool autotune = true;
    int finalists = 2;
    int restarts = 0;      // GRASP restarts, run concurrently
    double rcl_eps = 0.15;
    int swap_shortlist = 0;   // 2-exchange addition shortlist; 0 disables  // exponents promoted from the cheap sweep to the focused run
    std::vector<double> powers{1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 6.0, 8.0};
};

// The exponent sweep runs the un-polished variant; the winner is then polished.
static Algo base_algo(Algo a) {
    if (a == Algo::CostAwareLNS || a == Algo::CostAware) return Algo::CostAware;
    if (a == Algo::FocusLNS || a == Algo::Focus) return Algo::Focus;
    if (a == Algo::PotentialLNS || a == Algo::Potential) return Algo::Potential;
    return a;
}
static Algo polish_algo(Algo a) {
    if (a == Algo::CostAware || a == Algo::CostAwareLNS) return Algo::CostAwareLNS;
    if (a == Algo::Focus || a == Algo::FocusLNS) return Algo::FocusLNS;
    if (a == Algo::Potential || a == Algo::PotentialLNS) return Algo::PotentialLNS;
    return a;
}

static Algo parse_algo(const std::string& s) {
    if (s == "selfcover") return Algo::SelfCover;
    if (s == "truncated") return Algo::Truncated;
    if (s == "bundle") return Algo::Bundle;
    if (s == "bundle+lns") return Algo::BundleLNS;
    if (s == "potential") return Algo::Potential;
    if (s == "focus") return Algo::Focus;
    if (s == "costaware") return Algo::CostAware;
    if (s == "costaware+lns") return Algo::CostAwareLNS;
    if (s == "potential+lns") return Algo::PotentialLNS;
    return Algo::FocusLNS;
}

// ---------------------------------------------------------------------------

static void write_submission(const std::string& path, const Scene& sc,
                             const std::vector<std::tuple<double, int, std::vector<Vec2>,
                                                          std::vector<int32_t>>>& results) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    for (const auto& r : results) {
        double tau = std::get<0>(r);
        int k = std::get<1>(r);
        const auto& ants = std::get<2>(r);
        const auto& ids = std::get<3>(r);
        std::fprintf(f, "%g,%d\n", tau, k);
        for (size_t i = 0; i < ants.size(); ++i) {
            // %.17g round-trips an IEEE-754 double exactly, so the coordinates
            // the organizers parse are bit-identical to the ones we scored.
            std::fprintf(f, "%s(%.17g,%.17g)", i ? "," : "", ants[i].x, ants[i].y);
        }
        std::fputc('\n', f);
        for (size_t i = 0; i < ids.size(); ++i)
            std::fprintf(f, "%s%s", i ? "," : "", sc.buildings[ids[i]].id.c_str());
        std::fputc('\n', f);
    }
    std::fclose(f);
}

// Exact, uncapped re-evaluation of a placement. This is what decides the claim.
struct Verdict {
    int score;
    std::vector<int32_t> claimed;
    int near_miss;    // within 1e-9 relative below tau
    int near_hit;     // within 1e-9 relative above tau
    double gain_vs_search;
};

static Verdict verify(const Scene& sc, const Visibility& vis, const std::vector<Vec2>& ants,
                      double tau, double margin, double radius = -1.0) {
    Evaluator ev(sc, vis);
    auto cov = ev.coverage(ants, radius);
    Verdict v{0, {}, 0, 0, 0};
    for (size_t b = 0; b < cov.size(); ++b) {
        double d = cov[b] - tau;
        if (d >= margin) { v.claimed.push_back((int32_t)b); ++v.score; }
        if (d < 0 && d > -1e-9) ++v.near_miss;
        if (d >= 0 && d < 1e-9) ++v.near_hit;
    }
    return v;
}

static std::vector<double> ev_coverage(const Scene& sc, const Visibility& vis,
                                       const std::vector<Vec2>& ants) {
    Evaluator ev(sc, vis);
    return ev.coverage(ants, -1.0);
}

// ---------------------------------------------------------------------------

static int cmd_solve(const Args& A) {
    Scene sc;
    std::fprintf(stderr, "[load] %s\n", A.data.c_str());
    sc.load_geojson(A.data);
    std::fprintf(stderr, "[load] %zu buildings, %zu edges, %.2fs\n", sc.buildings.size(),
                 sc.edges.size(), now_s());
    sc.build_index(A.cell);
    Visibility vis(sc);

    auto cands = generate_candidates(sc, A.edge_spacing);
    std::fprintf(stderr, "[cand] %zu candidate sites\n", cands.size());

    ContribOpts co;
    co.radius = A.radius;
    co.min_frac = A.min_frac;
    double t0 = now_s();
    Contribs C = build_contributions(sc, vis, cands, co);
    std::fprintf(stderr, "[contrib] %.2fs\n", now_s() - t0);
    InvIndex I = build_inverse(sc, C, cands.size());

    std::vector<std::tuple<double, int, std::vector<Vec2>, std::vector<int32_t>>> results;
    std::printf("%-6s %-6s %-5s %-6s %9s %9s %7s %s\n", "tau", "k", "pow", "price",
                "exact", "search", "sec", "note");
    for (double tau : A.taus) {
        for (double kd : A.ks) {
            int k = (int)kd;
            double ts = now_s();
            // The exponent that suits tau=0.25 is not the one that suits 0.75,
            // and scoring is relative *per sub-problem*, so each of the nine is
            // tuned on its own. The contribution map is shared, so a sweep only
            // costs selection time.
            // Two-stage tuning. The exponent sweep runs the cheap unfocused
            // greedy -- it only has to *rank* exponents, not produce the final
            // answer. The top few then get the expensive focused treatment, and
            // the winner of that gets the polish budget. Sweeping every exponent
            // with focus would cost 5x for a ranking we already have.
            // Two ways to price a building's remaining work, swept together
            // with the convexity exponent:
            //
            //   metres   phi(u) = u^q on fraction-of-threshold covered
            //   antennas 1/(1+need/reach)^q on antenna-units outstanding
            //
            // Neither dominates -- antenna-pricing wins by 3% at (0.75, 500)
            // and loses by 14% at (0.5, 50) -- and scoring is relative per
            // sub-problem, so the choice is made per sub-problem by measurement
            // rather than by argument.
            std::vector<double> pows = A.autotune ? A.powers : std::vector<double>{A.power};
            struct Cfg { double pw; bool cost; };
            std::vector<std::pair<int, Cfg>> ranked;
            for (bool cost : {false, true}) {
                if (cost && !A.autotune) continue;
                for (double pw : pows) {
                    Solver S(sc, cands, C, I, tau, pw, A.normalise, cost);
                    S.run(Algo::Potential, k, 0.0, A.seed, false);
                    ranked.push_back({S.score(), Cfg{pw, cost}});
                }
            }
            std::sort(ranked.begin(), ranked.end(),
                      [](const std::pair<int, Cfg>& x, const std::pair<int, Cfg>& y) {
                          return x.first > y.first;
                      });
            size_t finalists = std::min<size_t>(ranked.size(), A.finalists);

            Cfg best_cfg = ranked.empty() ? Cfg{A.power, false} : ranked[0].second;
            int best_score = -1, best_search = -1;
            std::vector<Vec2> best_ants;
            std::vector<int32_t> best_claim;
            auto harvest = [&](Solver& S, Cfg cfg) {
                std::vector<Vec2> ants;
                for (int32_t c : S.picked()) ants.push_back(cands[c].p);
                for (size_t i = ants.size(); i < (size_t)k; ++i)
                    ants.push_back(cands[(A.seed * 2654435761u + (unsigned)i) % cands.size()].p);
                Verdict v = verify(sc, vis, ants, tau, 0.0, A.verify_radius);
                if (v.score > best_score) {
                    best_score = v.score; best_search = S.score(); best_cfg = cfg;
                    best_ants = ants; best_claim = v.claimed;
                }
            };
            for (size_t i = 0; i < finalists; ++i) {
                Cfg cfg = ranked[i].second;
                Solver S(sc, cands, C, I, tau, cfg.pw, A.normalise, cfg.cost);
                S.run(base_algo(A.algo), k, 0.0, A.seed, false);
                harvest(S, cfg);
            }
            // GRASP restarts. Selection is otherwise single-threaded while all
            // cores idle, so these are close to free wall-clock.
            if (A.restarts > 0 && !ranked.empty()) {
                Cfg cfg = ranked[0].second;
                std::vector<std::vector<int32_t>> picks(A.restarts);
                std::vector<int> scores(A.restarts, -1);
#pragma omp parallel for schedule(dynamic, 1)
                for (int r = 0; r < A.restarts; ++r) {
                    Solver S(sc, cands, C, I, tau, cfg.pw, A.normalise, cfg.cost);
                    S.set_random(A.rcl_eps, A.seed + 7919u * (unsigned)(r + 1));
                    S.run(base_algo(A.algo), k, 0.0, A.seed + (unsigned)r, false);
                    picks[r] = S.picked();
                    scores[r] = S.score();
                }
                int bi = 0;
                for (int r = 1; r < A.restarts; ++r) if (scores[r] > scores[bi]) bi = r;
                std::vector<Vec2> ants;
                for (int32_t c : picks[bi]) ants.push_back(cands[c].p);
                for (size_t i = ants.size(); i < (size_t)k; ++i)
                    ants.push_back(cands[(A.seed * 2654435761u + (unsigned)i) % cands.size()].p);
                Verdict v = verify(sc, vis, ants, tau, 0.0, A.verify_radius);
                // Contribute a solution only. Letting this also overwrite
                // best_cfg would hand the polish stage a different
                // configuration from the one the finalists round chose, which
                // measured *worse* at (0.75, 50): 298 -> 277.
                if (v.score > best_score) {
                    best_score = v.score; best_search = scores[bi];
                    best_ants = ants; best_claim = v.claimed;
                }
            }
            std::printf("%-6g %-6d %-5.1f %-6s %9d %9d %7.1f %s\n", tau, k, best_cfg.pw,
                        best_cfg.cost ? "ant" : "metre", best_score, best_search, now_s() - ts,
                        A.autotune ? "tuned" : "");
            std::fflush(stdout);

            if (A.lns_sec > 0) {
                double ts2 = now_s();
                int before = best_score;
                Solver S(sc, cands, C, I, tau, best_cfg.pw, A.normalise, best_cfg.cost);
                S.set_two_exchange(A.swap_shortlist > 0, A.swap_shortlist);
                S.run(polish_algo(A.algo), k, A.lns_sec, A.seed, false);
                harvest(S, best_cfg);
                std::printf("%-6g %-6d %-5.1f %-6s %9d %9d %7.1f %s\n", tau, k, best_cfg.pw,
                            best_cfg.cost ? "ant" : "metre", best_score,
                            best_search, now_s() - ts2,
                            best_score > before ? ("lns +" + std::to_string(best_score - before)).c_str()
                                                : "lns no gain");
                std::fflush(stdout);
            }
            // The capped radius is a search speed-up, not a scoring decision.
            // Re-derive the claimed set exactly before writing it out, so we
            // never decline to claim a building we can demonstrably serve.
            if (A.verify_radius > 0) {
                Verdict exact = verify(sc, vis, best_ants, tau, 0.0, -1.0);
                if (exact.score > best_score) {
                    std::printf("%-6g %-6d %-5.1f %-6s %9d %9s %7s exact claim +%d\n", tau, k,
                                best_cfg.pw, best_cfg.cost ? "ant" : "metre", exact.score, "-",
                                "-", exact.score - best_score);
                    std::fflush(stdout);
                }
                best_claim = exact.claimed;
            }
            if (A.archive_add) {
                // Feed every run into the archive. Experiments can then only
                // ratchet the submission upward -- a run that comes out worse
                // than the incumbent is recorded but never exported.
                char m[128];
                std::snprintf(m, sizeof m, "%s,p%.1f,%s,lns%gs%s", algo_name(A.algo),
                              best_cfg.pw, best_cfg.cost ? "ant" : "metre", A.lns_sec,
                              A.swap_shortlist ? ",swap" : "");
                Archive ar(A.archive_dir);
                ar.load();
                const ArchiveEntry* prev = ar.best(tau, k);
                ar.add(tau, k, best_score, m, best_ants);
                if (prev && best_score > prev->score)
                    std::printf("%-6g %-6d %-5s %-6s %9d %9s %7s archive NEW BEST (was %d)\n",
                                tau, k, "-", "-", best_score, "-", "-", prev->score);
            }
            results.emplace_back(tau, k, best_ants, best_claim);
        }
    }
    write_submission(A.out, sc, results);
    std::fprintf(stderr, "[out] %s (%.1fs total)\n", A.out.c_str(), now_s());
    return 0;
}

static int cmd_bench(const Args& A) {
    Scene sc;
    sc.load_geojson(A.data);
    sc.build_index(A.cell);
    Visibility vis(sc);
    auto cands = generate_candidates(sc, A.edge_spacing);
    ContribOpts co;
    co.radius = A.radius;
    co.min_frac = A.min_frac;
    Contribs C = build_contributions(sc, vis, cands, co);
    InvIndex I = build_inverse(sc, C, cands.size());

    const Algo algos[] = {Algo::SelfCover, Algo::Bundle, Algo::Truncated, Algo::Potential,
                          Algo::PotentialLNS};
    std::printf("%-6s %-6s %-12s %9s %9s %8s\n", "tau", "k", "algo", "exact", "vs_best", "sec");
    for (double tau : A.taus) {
        for (double kd : A.ks) {
            int k = (int)kd;
            int best = 0;
            std::vector<std::pair<const char*, int>> row;
            std::vector<double> secs;
            for (Algo a : algos) {
                double pw = (a == Algo::Truncated) ? 1.0 : A.power;
                bool nm = (a == Algo::Truncated) ? false : A.normalise;
                Solver S(sc, cands, C, I, tau, pw, nm);
                double ts = now_s();
                S.run(a, k, A.lns_sec, A.seed, false);
                std::vector<Vec2> ants;
                for (int32_t c : S.picked()) ants.push_back(cands[c].p);
                Verdict v = verify(sc, vis, ants, tau, 0.0, A.verify_radius);
                row.emplace_back(algo_name(a), v.score);
                secs.push_back(now_s() - ts);
                best = std::max(best, v.score);
            }
            for (size_t i = 0; i < row.size(); ++i)
                std::printf("%-6g %-6d %-12s %9d %9.4f %8.1f\n", tau, k, row[i].first,
                            row[i].second, best ? (double)row[i].second / best : 0.0, secs[i]);
            std::fflush(stdout);
        }
    }
    return 0;
}

// Cross-check the sweep against the independent brute-force sampler.
static int cmd_crosscheck(const Args& A) {
    Scene sc;
    sc.load_geojson(A.data);
    sc.build_index(A.cell);
    Visibility vis(sc);
    Evaluator ev(sc, vis);
    std::mt19937 rng(A.seed);
    int n_ant = 12, trials = A.subset ? A.subset : 25, samples = 120;

    double worst = 0;
    int checked = 0;
    for (int t = 0; t < trials; ++t) {
        // Place a few antennas at random vertices in one neighbourhood, so the
        // sampled buildings actually have interesting partial coverage.
        std::uniform_int_distribution<size_t> pick(0, sc.edges.size() - 1);
        size_t seed_edge = pick(rng);
        Vec2 hub = sc.edges[seed_edge].a;
        std::vector<int32_t> near;
        sc.query_edges(hub, 120.0, near);
        if (near.size() < 20) continue;
        std::vector<Vec2> ants;
        for (int i = 0; i < n_ant; ++i)
            ants.push_back(sc.edges[near[rng() % near.size()]].a);

        auto cov = ev.coverage(ants, -1.0);
        // Compare on the buildings around the hub. Blockers are scoped to a
        // generous disc: antennas and targets all lie within 120 m of the hub,
        // so nothing outside 600 m can lie between them.
        std::vector<int32_t> bs, blockers;
        for (int32_t ei : near) bs.push_back(sc.edges[ei].building);
        std::sort(bs.begin(), bs.end());
        bs.erase(std::unique(bs.begin(), bs.end()), bs.end());
        {
            std::vector<int32_t> wide;
            sc.query_edges(hub, 600.0, wide);
            for (int32_t ei : wide) blockers.push_back(sc.edges[ei].building);
            std::sort(blockers.begin(), blockers.end());
            blockers.erase(std::unique(blockers.begin(), blockers.end()), blockers.end());
        }
        size_t nb_chk = std::min<size_t>(bs.size(), 8);
#pragma omp parallel for schedule(dynamic, 1)
        for (long long i = 0; i < (long long)nb_chk; ++i) {
            int32_t b = bs[i];
            double ref = brute_coverage(sc, blockers, ants, b, samples);
            double d = std::fabs(ref - cov[b]);
#pragma omp critical
            {
                worst = std::max(worst, d);
                ++checked;
                if (d > 0.01)
                    std::printf("  MISMATCH building %s: sweep=%.4f brute=%.4f delta=%.4f\n",
                                sc.buildings[b].id.c_str(), cov[b], ref, d);
            }
        }
    }
    std::printf("crosscheck: %d building/placement pairs, worst |sweep-brute| = %.5f\n", checked,
                worst);
    // The brute force samples edge midpoints, so it can disagree by up to one
    // sample spacing per visible arc endpoint; 1% is a generous bound at 400
    // samples per edge.
    std::printf("%s\n", worst <= 0.01 ? "CROSSCHECK PASSED" : "CROSSCHECK FAILED");
    return worst <= 0.01 ? 0 : 1;
}


// Re-read a finished submission and score it from scratch. Run-day gate: it
// catches a malformed block, an antenna that drifted off a boundary, and above
// all a building we claimed but cannot actually serve.
static int cmd_verify(const Args& A) {
    Scene sc;
    sc.load_geojson(A.data);
    sc.build_index(A.cell);
    Visibility vis(sc);
    std::map<std::string, int32_t> by_id;
    for (size_t b = 0; b < sc.buildings.size(); ++b) by_id[sc.buildings[b].id] = (int32_t)b;

    std::ifstream in(A.out);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", A.out.c_str()); return 2; }
    FILE* out_fixed = A.rewrite.empty() ? nullptr : std::fopen(A.rewrite.c_str(), "w");
    std::string l1, l2, l3;
    int blocks = 0, problems = 0;
    while (std::getline(in, l1) && std::getline(in, l2) && std::getline(in, l3)) {
        if (l1.empty()) continue;
        ++blocks;
        double tau = 0; int k = 0;
        if (std::sscanf(l1.c_str(), "%lf,%d", &tau, &k) != 2) {
            std::printf("block %d: unparseable header %s\n", blocks, l1.c_str());
            ++problems; continue;
        }
        std::vector<Vec2> ants;
        for (size_t i = 0; i + 1 < l2.size(); ++i) {
            if (l2[i] != '(') continue;
            double x, y;
            if (std::sscanf(l2.c_str() + i, "(%lf,%lf)", &x, &y) == 2) ants.push_back({x, y});
        }
        std::vector<std::string> ids;
        {
            size_t i = 0;
            while (i <= l3.size()) {
                size_t j = l3.find(',', i);
                if (j == std::string::npos) j = l3.size();
                std::string t = l3.substr(i, j - i);
                if (!t.empty()) ids.push_back(t);
                i = j + 1;
            }
        }

        // 1. antenna count
        if ((int)ants.size() != k) {
            std::printf("block %d (tau=%g,k=%d): %zu antennas, expected %d\n", blocks, tau, k,
                        ants.size(), k);
            ++problems;
        }
        // 2. every antenna on a boundary
        int off = 0;
        double worst_off = 0;
        for (const Vec2& p : ants) {
            std::vector<int32_t> near;
            sc.query_edges(p, 1.0, near);
            double best = 1e300;
            for (int32_t ei : near)
                best = std::min(best, point_seg_dist2(p, sc.edges[ei].a, sc.edges[ei].b));
            best = std::sqrt(std::max(0.0, best));
            worst_off = std::max(worst_off, best);
            if (best > vis.on_edge_eps) ++off;
        }
        if (off) {
            std::printf("block %d (tau=%g,k=%d): %d antennas off-boundary (worst %.3g m)\n",
                        blocks, tau, k, off, worst_off);
            ++problems;
        }
        // 3. recompute coverage and compare against the claim
        auto cov = ev_coverage(sc, vis, ants);
        std::vector<char> claimed(sc.buildings.size(), 0);
        int unknown = 0;
        for (const std::string& s2 : ids) {
            auto it = by_id.find(s2);
            if (it == by_id.end()) { ++unknown; continue; }
            claimed[it->second] = 1;
        }
        int false_claim = 0, missed = 0, ok = 0;
        double worst_short = 0;
        // The LP relaxation's objective in disguise: it can set z_b to
        // (covered)/(tau*P_b), so its bound is really sum_b min(1, cov/target).
        // Reporting the same quantity for our integral solution shows how much
        // of any LP gap is the threshold relaxation rather than solution quality.
        double lp_value = 0;
        for (size_t b = 0; b < cov.size(); ++b)
            lp_value += std::min(1.0, cov[b] / tau);
        for (size_t b = 0; b < cov.size(); ++b) {
            bool real = cov[b] >= tau;
            if (claimed[b] && !real) { ++false_claim; worst_short = std::max(worst_short, tau - cov[b]); }
            else if (!claimed[b] && real) ++missed;
            else if (claimed[b]) ++ok;
        }
        std::printf("block %d  tau=%-5g k=%-5d antennas=%-5zu claimed=%-6zu verified=%-6d"
                    " false=%-4d missed=%-4d unknown_id=%d\n",
                    blocks, tau, k, ants.size(), ids.size(), ok, false_claim, missed, unknown);
        std::printf("          fractional-surrogate value of this solution = %.1f\n", lp_value);
        // How close are our claims to the line? A building we claim at
        // tau + 1e-15 is a building the organizers' evaluator may compute at
        // tau - 1e-15. Anything in the tightest bucket is a coin-flip claim.
        int m12 = 0, m9 = 0, m6 = 0, m3 = 0;
        for (size_t b = 0; b < cov.size(); ++b) {
            if (cov[b] < tau) continue;
            double d = cov[b] - tau;
            if (d < 1e-12) ++m12;
            else if (d < 1e-9) ++m9;
            else if (d < 1e-6) ++m6;
            else if (d < 1e-3) ++m3;
        }
        std::printf("          claim margin above tau:  <1e-12:%d  <1e-9:%d  <1e-6:%d  <1e-3:%d\n",
                    m12, m9, m6, m3);
        if (out_fixed) {
            std::fprintf(out_fixed, "%g,%d\n", tau, k);
            for (size_t i = 0; i < ants.size(); ++i)
                std::fprintf(out_fixed, "%s(%.17g,%.17g)", i ? "," : "", ants[i].x, ants[i].y);
            std::fputc('\n', out_fixed);
            bool first = true;
            for (size_t b = 0; b < cov.size(); ++b) {
                if (cov[b] < tau) continue;
                std::fprintf(out_fixed, "%s%s", first ? "" : ",", sc.buildings[b].id.c_str());
                first = false;
            }
            std::fputc('\n', out_fixed);
        }
        if (false_claim) {
            std::printf("          worst shortfall %.6f below tau\n", worst_short);
            ++problems;
        }
        if (unknown) ++problems;
    }
    if (out_fixed) {
        std::fclose(out_fixed);
        std::printf("rewrote claim lines -> %s\n", A.rewrite.c_str());
    }
    if (blocks != 9) { std::printf("expected 9 blocks, found %d\n", blocks); ++problems; }
    std::printf("\n%s\n", problems ? "SUBMISSION HAS PROBLEMS" : "SUBMISSION OK");
    return problems ? 1 : 0;
}


// Dump the contribution map so an external LP relaxation can be built.
//
// The relaxation gives what nothing else here can: an *upper bound* on the true
// optimum, and therefore a real optimality gap rather than a comparison against
// our own earlier baselines.
static int cmd_dump(const Args& A) {
    Scene sc;
    sc.load_geojson(A.data);
    sc.build_index(A.cell);
    Visibility vis(sc);
    auto cands = generate_candidates(sc, A.edge_spacing);
    ContribOpts co;
    co.radius = A.radius;
    co.min_frac = A.min_frac;
    Contribs C = build_contributions(sc, vis, cands, co);

    FILE* f = std::fopen(A.out.c_str(), "w");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", A.out.c_str()); return 2; }
    std::fprintf(f, "BUILDINGS %zu\n", sc.buildings.size());
    for (const auto& b : sc.buildings) std::fprintf(f, "%s %.17g\n", b.id.c_str(), b.perimeter);
    std::fprintf(f, "CANDS %zu\n", cands.size());
    std::fprintf(f, "ARCS %zu\n", C.entries());
    for (size_t c = 0; c + 1 < C.start.size(); ++c)
        for (int64_t j = C.start[c]; j < C.start[c + 1]; ++j)
            std::fprintf(f, "%zu %d %.17g %.17g\n", c, C.bld[j], C.s0[j], C.s1[j]);
    std::fclose(f);
    std::fprintf(stderr, "[dump] %zu buildings, %zu candidates, %zu arcs -> %s\n",
                 sc.buildings.size(), cands.size(), C.entries(), A.out.c_str());
    return 0;
}


// Exhaustive optimum for a tiny instance.
//
// The LP relaxation (tools/lp_bound.py) turned out to be useless as a quality
// measure, knapsack-cover cuts included. This is the brute-force alternative:
// on an instance small enough to enumerate, compute the true optimum and
// measure the heuristic's real gap. Slow by construction, and only viable for
// k <= 3 or so, but it is the one number here that is not a comparison against
// ourselves.
static int cmd_exact(const Args& A) {
    Scene sc;
    sc.load_geojson(A.data);
    sc.build_index(A.cell);
    Visibility vis(sc);
    auto cands = generate_candidates(sc, A.edge_spacing);
    ContribOpts co;
    co.radius = A.radius;
    co.min_frac = 0.0;
    co.verbose = false;
    Contribs C = build_contributions(sc, vis, cands, co);

    size_t nc = cands.size(), nb = sc.buildings.size();
    int k = (int)A.ks[0];
    double tau = A.taus[0];
    std::vector<double> target(nb);
    for (size_t b = 0; b < nb; ++b) target[b] = tau * sc.buildings[b].perimeter;

    std::fprintf(stderr, "[exact] %zu buildings, %zu candidates, k=%d, tau=%g\n", nb, nc, k, tau);
    if (k > 3) { std::fprintf(stderr, "[exact] k>3 is not enumerable here\n"); return 2; }

    int best = -1;
    std::vector<int32_t> best_set;

    auto score_of = [&](const int32_t* pick, int n) {
        static thread_local std::vector<ArcSet> arcs;
        static thread_local std::vector<int32_t> touched;
        if (arcs.size() != nb) arcs.assign(nb, ArcSet());
        touched.clear();
        for (int i = 0; i < n; ++i) {
            int64_t a = C.start[pick[i]], z = C.start[pick[i] + 1];
            for (int64_t j = a; j < z; ++j) {
                if (arcs[C.bld[j]].iv.empty()) touched.push_back(C.bld[j]);
                arcs[C.bld[j]].add(C.s0[j], C.s1[j]);
            }
        }
        int sc2 = 0;
        for (int32_t b : touched) {
            if (arcs[b].measure >= target[b]) ++sc2;
            arcs[b].clear();
        }
        return sc2;
    };

#pragma omp parallel
    {
        int lbest = -1;
        std::vector<int32_t> lset;
        int32_t pick[3];
#pragma omp for schedule(dynamic, 1)
        for (long long i = 0; i < (long long)nc; ++i) {
            pick[0] = (int32_t)i;
            if (k == 1) {
                int v = score_of(pick, 1);
                if (v > lbest) { lbest = v; lset.assign(pick, pick + 1); }
                continue;
            }
            for (size_t j = i + 1; j < nc; ++j) {
                pick[1] = (int32_t)j;
                if (k == 2) {
                    int v = score_of(pick, 2);
                    if (v > lbest) { lbest = v; lset.assign(pick, pick + 2); }
                    continue;
                }
                for (size_t l = j + 1; l < nc; ++l) {
                    pick[2] = (int32_t)l;
                    int v = score_of(pick, 3);
                    if (v > lbest) { lbest = v; lset.assign(pick, pick + 3); }
                }
            }
        }
#pragma omp critical
        if (lbest > best) { best = lbest; best_set = lset; }
    }
    std::printf("EXACT tau=%g k=%d  optimum = %d  (of %zu buildings)\n", tau, k, best, nb);
    return 0;
}


// ---------------------------------------------------------------------------
// Archive: verified placements keyed by (tau, k). See src/archive.hpp for why.
// ---------------------------------------------------------------------------

// Parse the antenna coordinates out of a submission line "(x,y),(x,y),..."
static std::vector<Vec2> parse_coord_line(const std::string& l) {
    std::vector<Vec2> ants;
    for (size_t i = 0; i + 1 < l.size(); ++i) {
        if (l[i] != '(') continue;
        double x, y;
        if (std::sscanf(l.c_str() + i, "(%lf,%lf)", &x, &y) == 2) ants.push_back({x, y});
    }
    return ants;
}

static int cmd_archive(const Args& A, const std::string& sub) {
    Archive ar(A.archive_dir);
    ar.load();

    if (sub == "list") {
        std::printf("%-6s %-6s %9s  %-22s %-20s %s\n", "tau", "k", "score", "method",
                    "timestamp", "file");
        auto es = ar.entries();
        std::sort(es.begin(), es.end(), [](const ArchiveEntry& x, const ArchiveEntry& y) {
            if (x.tau != y.tau) return x.tau < y.tau;
            if (x.k != y.k) return x.k < y.k;
            return x.score > y.score;
        });
        for (const auto& e : es)
            std::printf("%-6g %-6d %9d  %-22s %-20s %s\n", e.tau, e.k, e.score,
                        e.method.c_str(), e.stamp.c_str(), e.file.c_str());
        std::printf("\n%zu entries\n", es.size());
        return 0;
    }

    if (sub == "best") {
        std::printf("%-6s %-6s %9s  %-22s %s\n", "tau", "k", "score", "method", "timestamp");
        int total = 0, missing = 0;
        for (double tau : A.taus)
            for (double kd : A.ks) {
                const ArchiveEntry* e = ar.best(tau, (int)kd);
                if (!e) { std::printf("%-6g %-6d %9s\n", tau, (int)kd, "-- none --"); ++missing; }
                else {
                    std::printf("%-6g %-6d %9d  %-22s %s\n", e->tau, e->k, e->score,
                                e->method.c_str(), e->stamp.c_str());
                    total += e->score;
                }
            }
        std::printf("\ntotal serviced across 9 sub-problems: %d%s\n", total,
                    missing ? "  (INCOMPLETE)" : "");
        return missing ? 1 : 0;
    }

    // add / export-submission both need the scene.
    Scene sc;
    sc.load_geojson(A.data);
    sc.build_index(A.cell);
    Visibility vis(sc);

    if (sub == "add") {
        struct Pending { double tau; int k; std::vector<Vec2> ants; };
        std::vector<Pending> pend;
        if (!A.from_submission.empty()) {
            std::ifstream in(A.from_submission);
            if (!in) { std::fprintf(stderr, "cannot open %s\n", A.from_submission.c_str()); return 2; }
            std::string l1, l2, l3;
            while (std::getline(in, l1) && std::getline(in, l2) && std::getline(in, l3)) {
                if (l1.empty()) continue;
                double tau = 0; int k = 0;
                if (std::sscanf(l1.c_str(), "%lf,%d", &tau, &k) != 2) continue;
                pend.push_back({tau, k, parse_coord_line(l2)});
            }
        } else if (!A.placement.empty()) {
            std::ifstream in(A.placement);
            if (!in) { std::fprintf(stderr, "cannot open %s\n", A.placement.c_str()); return 2; }
            std::vector<Vec2> ants;
            double x, y;
            while (in >> x >> y) ants.push_back({x, y});
            pend.push_back({A.taus[0], (int)A.ks[0], ants});
        } else {
            std::fprintf(stderr, "archive add: need --placement or --from-submission\n");
            return 2;
        }

        int added = 0, rejected = 0;
        for (const Pending& p : pend) {
            if ((int)p.ants.size() != p.k) {
                std::printf("REJECT tau=%g k=%d: %zu antennas, expected %d\n", p.tau, p.k,
                            p.ants.size(), p.k);
                ++rejected;
                continue;
            }
            // Nothing enters the archive on trust: re-verify exactly, uncapped.
            Verdict v = verify(sc, vis, p.ants, p.tau, A.claim_epsilon, -1.0);
            const ArchiveEntry* prev = ar.best(p.tau, p.k);
            int old = prev ? prev->score : -1;
            ar.add(p.tau, p.k, v.score, A.method, p.ants);
            std::printf("added  tau=%-5g k=%-5d score=%-6d method=%-18s %s\n", p.tau, p.k,
                        v.score, A.method.c_str(),
                        old < 0 ? "(first)" : (v.score > old ? "NEW BEST" : "not best"));
            ++added;
        }
        std::printf("\n%d added, %d rejected\n", added, rejected);
        return rejected ? 1 : 0;
    }

    if (sub == "export-submission") {
        std::vector<std::tuple<double, int, std::vector<Vec2>, std::vector<int32_t>>> results;
        int total = 0;
        std::printf("%-6s %-6s %9s  %-22s %s\n", "tau", "k", "score", "method", "timestamp");
        for (double tau : A.taus)
            for (double kd : A.ks) {
                int k = (int)kd;
                const ArchiveEntry* e = ar.best(tau, k);
                if (!e) {
                    std::fprintf(stderr, "archive: no entry for tau=%g k=%d -- cannot export\n",
                                 tau, k);
                    return 2;
                }
                auto ants = ar.read_placement(*e);
                // Claims are never stored; they are re-derived exactly here, so
                // the file can only ever assert coverage that holds for the
                // geometry it ships against.
                Verdict v = verify(sc, vis, ants, tau, A.claim_epsilon, -1.0);
                if (v.score != e->score)
                    std::printf("  note tau=%g k=%d: archived %d, re-verified %d\n", tau, k,
                                e->score, v.score);
                std::printf("%-6g %-6d %9d  %-22s %s\n", tau, k, v.score, e->method.c_str(),
                            e->stamp.c_str());
                total += v.score;
                results.emplace_back(tau, k, ants, v.claimed);
            }
        write_submission(A.out, sc, results);
        std::printf("\ntotal %d serviced -> %s\n", total, A.out.c_str());
        return 0;
    }

    std::fprintf(stderr, "archive: unknown subcommand '%s' "
                 "(add|best|list|export-submission)\n", sub.c_str());
    return 2;
}

int main(int argc, char** argv) {
    Args A;
    if (argc > 1 && argv[1][0] != '-') A.cmd = argv[1];
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--data") A.data = next();
        else if (a == "--out") A.out = next();
        else if (a == "--tau") A.taus = parse_list(next());
        else if (a == "--k") A.ks = parse_list(next());
        else if (a == "--radius") A.radius = std::atof(next().c_str());
        else if (a == "--min-frac") A.min_frac = std::atof(next().c_str());
        else if (a == "--edge-spacing") A.edge_spacing = std::atof(next().c_str());
        else if (a == "--lns-sec") A.lns_sec = std::atof(next().c_str());
        else if (a == "--cell") A.cell = std::atof(next().c_str());
        else if (a == "--algo") A.algo = parse_algo(next());
        else if (a == "--seed") A.seed = (unsigned)std::atoi(next().c_str());
        else if (a == "--power") A.power = std::atof(next().c_str());
        else if (a == "--raw") A.normalise = false;
        else if (a == "--powers") A.powers = parse_list(next());
        else if (a == "--no-auto") A.autotune = false;
        else if (a == "--finalists") A.finalists = std::atoi(next().c_str());
        else if (a == "--verify-radius") A.verify_radius = std::atof(next().c_str());
        else if (a == "--subset") A.subset = std::atoi(next().c_str());
        else if (a == "--rewrite") A.rewrite = next();
        else if (a == "--archive") A.archive_dir = next();
        else if (a == "--archive-add") A.archive_add = true;
        else if (a == "--claim-epsilon") A.claim_epsilon = std::atof(next().c_str());
        else if (a == "--method") A.method = next();
        else if (a == "--placement") A.placement = next();
        else if (a == "--from-submission") A.from_submission = next();
        else if (a == "--restarts") A.restarts = std::atoi(next().c_str());
        else if (a == "--rcl-eps") A.rcl_eps = std::atof(next().c_str());
        else if (a == "--swap") A.swap_shortlist = std::atoi(next().c_str());
    }
    now_s();
    try {
        if (A.cmd == "bench") return cmd_bench(A);
        if (A.cmd == "archive") {
            std::string sub = argc > 2 && argv[2][0] != '-' ? argv[2] : "list";
            return cmd_archive(A, sub);
        }
        if (A.cmd == "verify") return cmd_verify(A);
        if (A.cmd == "dump") return cmd_dump(A);
        if (A.cmd == "exact") return cmd_exact(A);
        if (A.cmd == "crosscheck") return cmd_crosscheck(A);
        return cmd_solve(A);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
