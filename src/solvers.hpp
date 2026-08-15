// Antenna selection.
//
// The objective f(S) = #{b : coverage_b(S) >= tau} is monotone but NOT
// submodular -- the threshold destroys diminishing returns, so plain greedy on
// f carries no approximation guarantee and, worse, is blind: adding an antenna
// that lifts a building from 10% to 60% scores zero marginal gain at tau=0.75.
//
// Two principled ways around that, both implemented here so they can be
// compared rather than guessed at:
//
//   TRUNCATED  Optimise the surrogate  g(S) = sum_b min(cov_b(S), tau*P_b).
//              Truncating a coverage function at a constant preserves monotone
//              submodularity, so lazy greedy (Minoux) keeps the (1-1/e)
//              guarantee on g, and g is maximised exactly when every building
//              is pushed to -- but not past -- its threshold.
//
//   BUNDLE     Optimise f directly with a ratio greedy over *completion
//              bundles*: for each unserviced building work out the cheapest set
//              of extra antennas that would finish it, then repeatedly spend
//              budget on the bundle with the best (buildings completed) /
//              (antennas used). This is the partial-cover / budgeted-maximum-
//              coverage view, and it is the one that understands "two antennas
//              are worthless separately but complete this building together".
//
// The stale keys in both heaps are valid upper bounds, so lazy re-evaluation is
// sound in each case (see notes at the loops).
#pragma once
#include "pipeline.hpp"
#include "coverage.hpp"
#include <queue>
#include <random>
#include <chrono>
#include <cstring>

namespace gc {

enum class Algo { SelfCover, Truncated, Bundle, BundleLNS, Potential, PotentialLNS,
                  Focus, FocusLNS, CostAware, CostAwareLNS, Beam, BeamLNS,
                  Lagrangian, LagrangianLNS };

inline const char* algo_name(Algo a) {
    switch (a) {
        case Algo::SelfCover: return "selfcover";
        case Algo::Truncated: return "truncated";
        case Algo::Bundle: return "bundle";
        case Algo::BundleLNS: return "bundle+lns";
        case Algo::Potential: return "potential";
        case Algo::PotentialLNS: return "potential+lns";
        case Algo::Focus: return "focus";
        case Algo::FocusLNS: return "focus+lns";
        case Algo::CostAware: return "costaware";
        case Algo::CostAwareLNS: return "costaware+lns";
        case Algo::Beam: return "beam";
        case Algo::BeamLNS: return "beam+lns";
        case Algo::Lagrangian: return "lagrangian";
        case Algo::LagrangianLNS: return "lagrangian+lns";
    }
    return "?";
}

class Solver {
public:
    Solver(const Scene& sc, const std::vector<Candidate>& cands, const Contribs& C,
           const InvIndex& I, double tau, double power = 1.0, bool normalise = false,
           bool cost_mode = false)
        : sc_(sc), cands_(cands), C_(C), I_(I), tau_(tau), power_(power), norm_(normalise),
          cost_mode_(cost_mode) {
        size_t nb = sc.buildings.size();
        cov_.resize(nb);
        serviced_.assign(nb, 0);
        target_.resize(nb);
        for (size_t b = 0; b < nb; ++b) target_[b] = tau * sc.buildings[b].perimeter;
        chosen_.assign(cands.size(), 0);
        active_.assign(nb, 1);
        // reach_[b] = the largest slice of b's boundary any single antenna
        // delivers. It is the exchange rate between metres and antennas, and it
        // is what lets the potential below price a building's remaining work in
        // the same currency as the budget.
        holders_.assign(nb, {});
        reach_.assign(nb, 0.0);
        for (size_t c = 0; c + 1 < C.start.size(); ++c) {
            int64_t a = C.start[c], z = C.start[c + 1];
            for (int64_t j = a; j < z;) {
                int32_t b = C.bld[j];
                int64_t e = j;
                double tot = 0;
                while (e < z && C.bld[e] == b) { tot += C.s1[e] - C.s0[e]; ++e; }
                if (tot > reach_[b]) reach_[b] = tot;
                j = e;
            }
        }
    }

    void reset() {
        for (auto& h : holders_) h.clear();
        for (auto& a : cov_) a.clear();
        std::fill(serviced_.begin(), serviced_.end(), 0);
        std::fill(chosen_.begin(), chosen_.end(), 0);
        picked_.clear();
        score_ = 0;
    }

    int score() const { return score_; }
    void set_random(double eps, unsigned seed) { rcl_eps_ = eps; rng_seed_ = seed; }
    void set_two_exchange(bool on, int shortlist, int passes = 200) {
        two_exchange_on_ = on;
        two_exchange_shortlist_ = shortlist;
        if (passes > 0) two_exchange_passes_ = passes;
    }
    // Task 2 (cont2.md): randomised-destroy LNS around the converged incumbent.
    // Adds diversity *of* the polish, not of construction -- ruin a fraction of
    // the incumbent and rebuild, which is the coupled multi-antenna replacement
    // that neither 2-exchange (a radius-1 move) nor the redundant-antenna
    // destroy of lns() can make. Default off; the intended run-day compute sink.
    void set_lns_destroy(bool on) { lns_destroy_on_ = on; }
    // Plateau moves in 2-exchange: accept score-*equal* swaps that raise the
    // truncated secondary measure sum_b min(cov_b, tau*P_b), with a bounded
    // chain length between strict improvements to stop cycling. chain<=0 is off.
    void set_swap_plateau(bool on, int chain) {
        swap_plateau_on_ = on;
        swap_plateau_chain_ = chain;
    }
    const std::vector<int32_t>& picked() const { return picked_; }

    // Per-building potential. u is progress toward the threshold, in [0,1].
    //
    //   power = 1, normalise = false  ->  the truncated surrogate
    //       g(S) = sum_b min(cov_b, tau*P_b). Monotone submodular, so lazy
    //       greedy carries the (1-1/e) guarantee.
    //
    //   normalise = true              ->  each building contributes at most 1
    //       regardless of its perimeter. Still submodular (a positive rescale
    //       of each term), and better aligned with the objective, which counts
    //       buildings rather than metres.
    //
    //   power > 1                     ->  convex in u, so the last stretch to
    //       the threshold is worth more than the first. This deliberately
    //       trades the submodularity guarantee for an objective that cares
    //       about *finishing* buildings, which is what actually scores.
    double phi(double u) const { return power_ == 1.0 ? u : std::pow(u, power_); }

    double gain(int32_t c) const {
        double g = 0;
        int64_t a = C_.start[c], z = C_.start[c + 1];
        for (int64_t j = a; j < z;) {
            int32_t b = C_.bld[j];
            int64_t e = j;
            double fresh = 0;
            while (e < z && C_.bld[e] == b) {
                fresh += cov_[b].probe(C_.s0[e], C_.s1[e]);
                ++e;
            }
            if (!serviced_[b] && active_[b] && fresh > 0) {
                double T = target_[b];
                if (cost_mode_) {
                    // Price the remaining work in antennas, not metres.
                    //
                    //   need  = boundary still required
                    //   reach = what one antenna can deliver here
                    //   need/reach = antenna-units still outstanding
                    //
                    // Value a building at 1/(1 + units)^q: full credit when it is
                    // finished, and steeply diminishing credit the more antennas
                    // it would still take. An antenna that drags a building from
                    // three-still-needed to two now earns something, and one that
                    // polishes a building nobody will ever finish earns almost
                    // nothing -- which is precisely the misallocation the
                    // marginal-returns test exposed.
                    double r = reach_[b] > 0 ? reach_[b] : T;
                    double n0 = std::max(0.0, T - cov_[b].measure) / r;
                    double n1 = std::max(0.0, T - cov_[b].measure - fresh) / r;
                    g += std::pow(1.0 / (1.0 + n1), power_) - std::pow(1.0 / (1.0 + n0), power_);
                } else {
                    double u0 = std::min(cov_[b].measure, T) / T;
                    double u1 = std::min(cov_[b].measure + fresh, T) / T;
                    double d = phi(u1) - phi(u0);
                    g += norm_ ? d : d * T;
                }
            }
            j = e;
        }
        return g;
    }

    // How many currently-unserviced buildings candidate c would finish outright.
    int completions(int32_t c) const {
        int m = 0;
        int64_t a = C_.start[c], z = C_.start[c + 1];
        for (int64_t j = a; j < z;) {
            int32_t b = C_.bld[j];
            int64_t e = j;
            double fresh = 0;
            while (e < z && C_.bld[e] == b) { fresh += cov_[b].probe(C_.s0[e], C_.s1[e]); ++e; }
            if (!serviced_[b] && cov_[b].measure + fresh >= target_[b]) ++m;
            j = e;
        }
        return m;
    }

    // Commit a candidate; returns the buildings it newly serviced.
    int apply(int32_t c, std::vector<int32_t>* touched = nullptr) {
        if (chosen_[c]) return 0;
        chosen_[c] = 1;
        picked_.push_back(c);
        int gained = 0;
        int64_t a = C_.start[c], z = C_.start[c + 1];
        for (int64_t j = a; j < z;) {
            int32_t b = C_.bld[j];
            int64_t e = j;
            while (e < z && C_.bld[e] == b) { cov_[b].add(C_.s0[e], C_.s1[e]); ++e; }
            holders_[b].push_back(c);
            if (touched) touched->push_back(b);
            if (!serviced_[b] && cov_[b].measure >= target_[b]) {
                serviced_[b] = 1;
                ++score_;
                ++gained;
            }
            j = e;
        }
        return gained;
    }

    // A beam has to fork a partial solution. Copying whole Solvers is wasteful;
    // this captures just the mutable search state. Most buildings have empty
    // coverage for most of a run, so the copy is far cheaper than it looks.
    struct State {
        std::vector<ArcSet> cov;
        std::vector<char> serviced, chosen;
        std::vector<std::vector<int32_t>> holders;
        std::vector<int32_t> picked;
        int score = 0;
    };
    State snapshot() const { return State{cov_, serviced_, chosen_, holders_, picked_, score_}; }
    void restore(const State& s) {
        cov_ = s.cov; serviced_ = s.serviced; chosen_ = s.chosen;
        holders_ = s.holders; picked_ = s.picked; score_ = s.score;
    }

    // One extension of a partial placement: a single antenna, or a pair that
    // completes a building neither member completes alone.
    struct Ext { int32_t a = -1, b = -1; double gain = 0; int n = 1; };
    void top_extensions(int n_single, int n_pair, std::vector<Ext>& out);

    void run(Algo algo, int k, double time_budget_sec, unsigned seed, bool verbose);
    void set_beam(int width, int ns, int np) { beam_w_ = width; beam_single_ = ns; beam_pair_ = np; }
    void set_reach_from_bundles(bool on) { reach_from_bundles_ = on; }

    // Recompute coverage from scratch for the current pick set.
    void rebuild();

private:
    void run_greedy(int k, bool verbose);
    void run_bundle(int k, bool verbose);
    void run_selfcover(int k, bool verbose);
    void run_focus(int k, bool verbose);
    void run_beam(int k, bool verbose);
    void run_lagrangian(int k, bool verbose);
    void rebuild_reach_from_bundles();
    bool two_exchange_pass(int shortlist);
    // Generalised 2-exchange step: 0 = no move, 1 = strict score gain,
    // 2 = score-equal plateau move (only when allow_plateau). two_exchange_pass
    // is the strict-only wrapper, so existing callers are unchanged.
    int two_exchange_step(int shortlist, bool allow_plateau, double removal_eps);
    void run_2exchange(std::chrono::steady_clock::time_point t0, double budget);
    double trunc_add_gain(int32_t c) const;
    void withdraw(int32_t c);
    int lns(int k, double time_budget_sec, unsigned seed, bool verbose,
            const std::vector<char>& mask);
    int lns_destroy_loop(int k, double time_budget_sec, unsigned seed, bool verbose,
                         const std::vector<char>& mask);

    // Cheapest set of extra candidates that would finish building b, found by
    // greedy set cover over the candidates that see b. Returns cost, or -1 if
    // b cannot be finished with everything available.
    int completion_bundle(int32_t b, std::vector<int32_t>& out, int cost_cap,
                          bool host_only = false) const;

    const Scene& sc_;
    const std::vector<Candidate>& cands_;
    const Contribs& C_;
    const InvIndex& I_;
    double tau_;
    double power_ = 1.0;
    bool norm_ = false;
    std::vector<ArcSet> cov_;
    std::vector<char> serviced_;
    std::vector<char> chosen_;
    std::vector<char> active_;  // buildings the objective is allowed to care about
    std::vector<double> target_;
    std::vector<double> reach_;
    int beam_w_ = 4, beam_single_ = 24, beam_pair_ = 24;
    bool reach_from_bundles_ = false;
    bool two_exchange_on_ = false;
    int two_exchange_shortlist_ = 400;
    int two_exchange_passes_ = 200;   // cap on 2-exchange passes per polish call
    bool lns_destroy_on_ = false;
    bool swap_plateau_on_ = false;
    int swap_plateau_chain_ = 0;      // max equal-score moves between strict gains
    double rcl_eps_ = 0.0;   // randomised greedy: accept any gain within (1-eps) of best
    unsigned rng_seed_ = 0;
    bool cost_mode_ = false;  // price remaining work in antennas rather than metres
    std::vector<int32_t> picked_;
    std::vector<std::vector<int32_t>> holders_;  // chosen antennas touching each building
    int score_ = 0;
};

// ---------------------------------------------------------------------------

inline int Solver::completion_bundle(int32_t b, std::vector<int32_t>& out, int cost_cap,
                                     bool host_only) const {
    out.clear();
    if (serviced_[b]) return 0;
    // Work on a scratch copy of this building's covered arcs.
    ArcSet work = cov_[b];
    double need = target_[b] - work.measure;
    int64_t a = I_.start[b], z = I_.start[b + 1];
    while (need > 0) {
        int32_t best = -1;
        double best_gain = 0;
        for (int64_t t = a; t < z; ++t) {
            int32_t c = I_.cand[t];
            if (chosen_[c]) continue;
            if (host_only && cands_[c].host != b) continue;
            bool already = false;
            for (int32_t q : out) if (q == c) { already = true; break; }
            if (already) continue;
            double fresh = 0;
            for (int64_t e = I_.e0[t]; e < I_.e1[t]; ++e) fresh += work.probe(C_.s0[e], C_.s1[e]);
            if (fresh > best_gain) { best_gain = fresh; best = c; }
        }
        if (best < 0) return -1;                       // cannot be finished
        // Apply the winner to the scratch set.
        for (int64_t t = a; t < z; ++t) {
            if (I_.cand[t] != best) continue;
            for (int64_t e = I_.e0[t]; e < I_.e1[t]; ++e) work.add(C_.s0[e], C_.s1[e]);
        }
        out.push_back(best);
        need = target_[b] - work.measure;
        if ((int)out.size() >= cost_cap && need > 0) return -1;  // too expensive to care
    }
    return (int)out.size();
}

inline void Solver::run_greedy(int k, bool verbose) {
    struct Node { double gain; int32_t c; int stamp; };
    struct Cmp { bool operator()(const Node& x, const Node& y) const { return x.gain < y.gain; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;

    // Initial gains against empty coverage, computed in parallel.
    size_t nc = cands_.size();
    std::vector<double> g0(nc);
#pragma omp parallel for schedule(static)
    for (long long c = 0; c < (long long)nc; ++c) {
        double g = 0;
        int64_t a = C_.start[c], z = C_.start[c + 1];
        for (int64_t j = a; j < z;) {
            int32_t b = C_.bld[j];
            int64_t e = j;
            double tot = 0;
            while (e < z && C_.bld[e] == b) { tot += C_.s1[e] - C_.s0[e]; ++e; }
            if (active_[b]) {
                double T = target_[b];
                if (cost_mode_) {
                    double r = reach_[b] > 0 ? reach_[b] : T;
                    double n0 = T / r;
                    double n1 = std::max(0.0, T - tot) / r;
                    g += std::pow(1.0 / (1.0 + n1), power_) -
                         std::pow(1.0 / (1.0 + n0), power_);
                } else {
                    double d = phi(std::min(tot, T) / T);
                    g += norm_ ? d : d * T;
                }
            }
            j = e;
        }
        g0[c] = g;
    }
    std::vector<Node> init;
    init.reserve(nc);
    for (size_t c = 0; c < nc; ++c) if (g0[c] > 0) init.push_back({g0[c], (int32_t)c, -1});
    pq = std::priority_queue<Node, std::vector<Node>, Cmp>(Cmp{}, std::move(init));

    long long evals = 0;
    int iter = 0;
    std::mt19937 rng(rng_seed_ * 2654435761u + 12345u);
    std::vector<Node> rcl;   // restricted candidate list, for the randomised variant
    while ((int)picked_.size() < k && !pq.empty()) {
        Node n = pq.top();
        pq.pop();
        if (chosen_[n.c]) continue;
        if (n.stamp == iter) {           // gain is fresh for this round
            if (rcl_eps_ <= 0) { apply(n.c); ++iter; continue; }
            // GRASP: gather the fresh near-best candidates and pick one at
            // random. Exhaustive search on tiny instances showed plain greedy
            // reaching only 75-85% of the optimum -- it commits to the single
            // best next antenna and cannot see the pair that beats it. Sampling
            // near-best choices explores different trajectories; restarts run
            // concurrently and the best is kept, so this only ever helps.
            rcl.clear();
            rcl.push_back(n);
            double thresh = n.gain * (1.0 - rcl_eps_);
            while ((int)rcl.size() < 8 && !pq.empty()) {
                Node m = pq.top();
                if (chosen_[m.c]) { pq.pop(); continue; }
                if (m.gain < thresh) break;
                pq.pop();
                if (m.stamp != iter) {
                    m.gain = gain(m.c);
                    ++evals;
                    m.stamp = iter;
                    if (m.gain > 0) pq.push(m);
                    continue;
                }
                rcl.push_back(m);
            }
            size_t pickpos = rng() % rcl.size();
            for (size_t q = 0; q < rcl.size(); ++q)
                if (q != pickpos) pq.push(rcl[q]);
            apply(rcl[pickpos].c);
            ++iter;
            continue;
        }
        // Stale keys are upper bounds (g is submodular), so recomputing and
        // re-inserting is the standard lazy-greedy step.
        n.gain = gain(n.c);
        ++evals;
        n.stamp = iter;
        if (n.gain > 0) pq.push(n);
    }
    if (verbose)
        std::fprintf(stderr, "[greedy p=%.1f] tau=%.2f k=%d picked=%zu score=%d (%lld evals)\n", power_,
                     tau_, k, picked_.size(), score_, evals);
}

inline void Solver::run_bundle(int k, bool verbose) {
    // Ratio greedy over completion bundles.
    struct Node { double ratio; int m; int cost; int32_t b; int stamp; };
    struct Cmp { bool operator()(const Node& x, const Node& y) const { return x.ratio < y.ratio; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;

    size_t nb = sc_.buildings.size();
    std::vector<int> stamp(nb, -1);
    const int COST_CAP = 6;

    // Seed with each building's own completion cost, in parallel.
    std::vector<int> cost0(nb, -1);
#pragma omp parallel
    {
        std::vector<int32_t> bundle;
#pragma omp for schedule(dynamic, 64)
        for (long long b = 0; b < (long long)nb; ++b) {
            if (serviced_[b]) continue;
            cost0[b] = completion_bundle((int32_t)b, bundle, COST_CAP);
        }
    }
    std::vector<Node> init;
    for (size_t b = 0; b < nb; ++b)
        if (cost0[b] > 0) init.push_back({1.0 / cost0[b], 1, cost0[b], (int32_t)b, -1});
    pq = std::priority_queue<Node, std::vector<Node>, Cmp>(Cmp{}, std::move(init));

    std::vector<int32_t> bundle, touched;
    int round = 0;
    while ((int)picked_.size() < k && !pq.empty()) {
        Node n = pq.top();
        pq.pop();
        if (serviced_[n.b]) continue;
        int budget = k - (int)picked_.size();

        if (n.stamp != round) {
            // Recompute. Cost only falls as coverage grows and m only falls as
            // buildings get serviced, so the stale ratio was an upper bound and
            // this lazy re-evaluation is sound.
            int cost = completion_bundle(n.b, bundle, std::min(COST_CAP, budget));
            if (cost <= 0) continue;
            // How many buildings this bundle finishes in total, not just n.b.
            int m = 0;
            {
                // Simulate on scratch: accumulate the bundle's fresh length per
                // building, then count threshold crossings.
                static thread_local std::vector<int32_t> bs;
                static thread_local std::vector<double> add;
                bs.clear();
                for (int32_t c : bundle) {
                    int64_t a = C_.start[c], z = C_.start[c + 1];
                    for (int64_t j = a; j < z; ++j) bs.push_back(C_.bld[j]);
                }
                std::sort(bs.begin(), bs.end());
                bs.erase(std::unique(bs.begin(), bs.end()), bs.end());
                for (int32_t b2 : bs) {
                    if (serviced_[b2]) continue;
                    ArcSet w = cov_[b2];
                    for (int32_t c : bundle) {
                        int64_t a = C_.start[c], z = C_.start[c + 1];
                        for (int64_t j = a; j < z; ++j)
                            if (C_.bld[j] == b2) w.add(C_.s0[j], C_.s1[j]);
                    }
                    if (w.measure >= target_[b2]) ++m;
                }
            }
            if (m == 0) continue;
            pq.push({(double)m / cost, m, cost, n.b, round});
            continue;
        }

        if (n.cost > budget) continue;   // cannot afford; a cheaper one may exist
        int cost = completion_bundle(n.b, bundle, std::min(COST_CAP, budget));
        if (cost <= 0) continue;
        for (int32_t c : bundle) apply(c, &touched);
        ++round;
    }

    // Budget left over that cannot finish anything: spend it on the submodular
    // surrogate so the exact uncapped verification has the best chance of
    // finding extra buildings the radius-capped search could not see.
    if ((int)picked_.size() < k) run_greedy(k, false);
    if (verbose)
        std::fprintf(stderr, "[bundle] tau=%.2f k=%d picked=%zu score=%d\n", tau_, k,
                     picked_.size(), score_);
}

inline void Solver::run_selfcover(int k, bool verbose) {
    // Baseline: ignore cross-building help entirely and just buy the cheapest
    // self-serviceable buildings. Each vertex antenna covers its two incident
    // edges, so per building this is maximum coverage on a cycle where vertex i
    // covers edges i-1 and i. Restricting the bundle search to the building's
    // own vertices gives that, and sorting buildings by cost is then an exact
    // knapsack because every item has value 1.
    size_t nb = sc_.buildings.size();
    struct Item { int cost; int32_t b; };
    std::vector<Item> items;
    std::vector<std::vector<int32_t>> bundles(nb);
#pragma omp parallel
    {
        std::vector<int32_t> bundle;
#pragma omp for schedule(dynamic, 64)
        for (long long b = 0; b < (long long)nb; ++b) {
            int c = completion_bundle((int32_t)b, bundle, 8, /*host_only=*/true);
            if (c > 0) bundles[b] = bundle;
        }
    }
    for (size_t b = 0; b < nb; ++b)
        if (!bundles[b].empty()) items.push_back({(int)bundles[b].size(), (int32_t)b});
    std::sort(items.begin(), items.end(),
              [](const Item& x, const Item& y) { return x.cost < y.cost; });
    for (const Item& it : items) {
        if ((int)picked_.size() + it.cost > k) continue;
        if (serviced_[it.b]) continue;
        for (int32_t c : bundles[it.b]) apply(c);
    }
    if (verbose)
        std::fprintf(stderr, "[selfcover] tau=%.2f k=%d picked=%zu score=%d\n", tau_, k,
                     picked_.size(), score_);
}


// Target-set refinement ("focus").
//
// Diagnosis this exists to fix: at high tau and tight k the greedy shows
// *increasing* marginal returns -- 4.09 buildings per antenna over the first
// thousand, then 4.38 over the second. For a best-first greedy that is a tell.
// The objective rewards progress toward a threshold, so early antennas get spent
// part-covering buildings that the budget will never actually finish, and that
// investment only pays off at a k we do not have.
//
// The fix is to stop pretending every building is reachable. Run once to learn
// which buildings the budget can plausibly finish, restrict the objective to
// that set plus a margin, and re-solve. Antennas then concentrate on buildings
// that will actually cross the line. Iterate, keeping the best.
//
// Note the score still counts every serviced building, including ones outside
// the target set that get finished incidentally -- the mask shapes the search,
// it does not narrow the reward.
inline void Solver::run_focus(int k, bool verbose) {
    std::fill(active_.begin(), active_.end(), 1);
    run_greedy(k, false);
    int best = score_;
    std::vector<int32_t> best_pick = picked_;
    std::vector<char> best_active = active_;
    size_t nb = sc_.buildings.size();

    // Rank buildings by how far the last round got them toward the threshold.
    std::vector<std::pair<double, int32_t>> rank(nb);

    for (double grow : {1.00, 1.15, 1.35, 1.60}) {
        for (int b = 0; b < (int)nb; ++b) {
            double u = std::min(cov_[b].measure, target_[b]) / target_[b];
            rank[b] = {serviced_[b] ? 2.0 : u, (int32_t)b};
        }
        std::sort(rank.begin(), rank.end(),
                  [](const std::pair<double, int32_t>& x, const std::pair<double, int32_t>& y) {
                      return x.first > y.first;
                  });
        size_t take = std::min(nb, (size_t)(best * grow) + 1);
        std::fill(active_.begin(), active_.end(), 0);
        for (size_t i = 0; i < take; ++i) active_[rank[i].second] = 1;

        reset();
        run_greedy(k, false);
        if (score_ > best) {
            best = score_;
            best_pick = picked_;
            best_active = active_;
        }
        if (verbose)
            std::fprintf(stderr, "[focus] grow=%.2f target=%zu -> score=%d (best %d)\n", grow,
                         take, score_, best);
    }

    // Keep the winning mask in place. The polish that runs after this repairs by
    // re-greedying, and an unfocused repair can never beat a focused solution --
    // it would just propose the same myopic placement that focus improved on.
    // The score itself never consults the mask, so this only shapes the search.
    active_ = best_active;
    picked_ = best_pick;
    std::fill(chosen_.begin(), chosen_.end(), 0);
    for (int32_t c : picked_) chosen_[c] = 1;
    rebuild();
}


// Withdraw a chosen antenna, rebuilding coverage of the buildings it touched
// from whatever other chosen antennas still reach them.
inline void Solver::withdraw(int32_t c) {
    if (!chosen_[c]) return;
    chosen_[c] = 0;
    picked_.erase(std::find(picked_.begin(), picked_.end(), c));
    int64_t a = C_.start[c], z = C_.start[c + 1];
    for (int64_t j = a; j < z;) {
        int32_t b = C_.bld[j];
        int64_t e = j;
        while (e < z && C_.bld[e] == b) ++e;
        auto& h = holders_[b];
        h.erase(std::find(h.begin(), h.end(), c));
        cov_[b].clear();
        for (int32_t o : h) {
            int64_t oa = C_.start[o], oz = C_.start[o + 1];
            for (int64_t q = oa; q < oz; ++q)
                if (C_.bld[q] == b) cov_[b].add(C_.s0[q], C_.s1[q]);
        }
        if (serviced_[b] && cov_[b].measure < target_[b]) { serviced_[b] = 0; --score_; }
        j = e;
    }
}

// One first-improvement 2-exchange pass: withdraw a chosen antenna, put a
// different one in its place, keep the swap if the true score rises.
//
// This exists because exhaustive search on tiny instances showed the solver
// losing 20% on a *two-antenna* problem -- it commits to the best single next
// antenna and cannot see the pair that beats it. Destroy-repair does not fix
// that: it only ever frees antennas that are provably holding nothing up, so an
// antenna that is genuinely needed but nonetheless the wrong choice is never
// reconsidered. This move reconsiders exactly those.
//
// Additions are restricted to a shortlist of the currently highest-gain
// candidates, which keeps a pass at O(k * shortlist) rather than O(k * |C|).
// Secondary progress measure used to break plateaus: the increase in
// sum_b min(cov_b, tau*P_b) from adding candidate c. Unlike gain() this counts
// every building c touches, serviced or not, with no convexity -- it is the
// truncated surrogate's exact marginal, so two placements with the same service
// score are ordered by how close their *other* buildings sit to the threshold.
inline double Solver::trunc_add_gain(int32_t c) const {
    double g = 0;
    int64_t a = C_.start[c], z = C_.start[c + 1];
    for (int64_t j = a; j < z;) {
        int32_t b = C_.bld[j];
        int64_t e = j;
        double fresh = 0;
        while (e < z && C_.bld[e] == b) { fresh += cov_[b].probe(C_.s0[e], C_.s1[e]); ++e; }
        double T = target_[b];
        g += std::min(cov_[b].measure + fresh, T) - std::min(cov_[b].measure, T);
        j = e;
    }
    return g;
}

// One 2-exchange step: withdraw a chosen antenna, put a different one in its
// place. A strict move (return 1) keeps the swap if the true service score
// rises -- the pair-blindness repair of FINDINGS 5.13. A plateau move
// (return 2, only when allow_plateau) keeps a score-*equal* swap that strictly
// raises the truncated secondary measure, so the polish can cross the flat tops
// of the step-function objective toward a state one antenna from more finishes.
//
// Additions are restricted to a shortlist of the currently highest-gain
// candidates, which keeps a step at O(k * shortlist) rather than O(k * |C|).
inline int Solver::two_exchange_step(int shortlist, bool allow_plateau, double removal_eps) {
    // Shortlist: best current marginal gain among unchosen candidates.
    std::vector<std::pair<double, int32_t>> top;
    top.reserve(cands_.size() / 8 + 1);
    for (size_t c = 0; c < cands_.size(); ++c) {
        if (chosen_[c]) continue;
        if (C_.start[c] == C_.start[c + 1]) continue;
        double g = gain((int32_t)c);
        if (g > 0) top.emplace_back(g, (int32_t)c);
    }
    if ((int)top.size() > shortlist) {
        std::nth_element(top.begin(), top.begin() + shortlist, top.end(),
                         [](const std::pair<double, int32_t>& x,
                            const std::pair<double, int32_t>& y) { return x.first > y.first; });
        top.resize(shortlist);
    }
    if (top.empty()) return 0;

    std::vector<int32_t> order = picked_;
    std::vector<std::pair<int32_t, double>> held;   // out's buildings and their pre-withdraw m0
    for (int32_t out : order) {
        if (!chosen_[out]) continue;
        int before = score_;

        // For the plateau branch, record how much truncated coverage this
        // antenna is the reason for, measured as the drop when it leaves.
        if (allow_plateau) {
            held.clear();
            int64_t a = C_.start[out], z = C_.start[out + 1];
            for (int64_t j = a; j < z;) {
                int32_t b = C_.bld[j];
                int64_t e = j;
                while (e < z && C_.bld[e] == b) ++e;
                held.emplace_back(b, std::min(cov_[b].measure, target_[b]));
                j = e;
            }
        }

        withdraw(out);
        int lost = before - score_;

        int best_gain = 0;
        int32_t best_in = -1;
        for (const auto& t : top) {
            if (chosen_[t.second]) continue;
            int g = completions(t.second);
            if (g > best_gain) { best_gain = g; best_in = t.second; }
        }
        if (best_in >= 0 && score_ + best_gain > before) {
            apply(best_in);
            return 1;                    // strict improvement; caller re-runs
        }

        if (allow_plateau) {
            double removal_loss = 0;
            for (const auto& hb : held)
                removal_loss += hb.second - std::min(cov_[hb.first].measure, target_[hb.first]);
            // Accept a score-equal swap only if it strictly raises the secondary
            // measure net of what leaving `out` cost -- otherwise it is a lateral
            // move that could cycle. The bounded chain length in run_2exchange is
            // the second guard.
            int32_t pin = -1;
            double pbest = removal_loss + removal_eps;
            for (const auto& t : top) {
                if (chosen_[t.second]) continue;
                if (completions(t.second) != lost) continue;   // net service score unchanged
                double ag = trunc_add_gain(t.second);
                if (ag > pbest) { pbest = ag; pin = t.second; }
            }
            if (pin >= 0) {
                apply(pin);
                return 2;                // score-equal, secondary strictly up
            }
        }
        apply(out);                      // no move; put it back
    }
    return 0;
}

inline bool Solver::two_exchange_pass(int shortlist) {
    return two_exchange_step(shortlist, false, 0.0) == 1;
}

// Run 2-exchange to convergence (or the deadline), honouring the plateau
// setting. Strict moves reset the plateau chain; plateau moves extend it up to
// swap_plateau_chain_, after which a strict move is required to continue.
inline void Solver::run_2exchange(std::chrono::steady_clock::time_point t0, double budget) {
    auto left = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < budget;
    };
    const int shortlist = two_exchange_shortlist_;
    const bool plateau = swap_plateau_on_ && swap_plateau_chain_ > 0;
    int chain = 0, guard = 0;
    while (left() && guard < two_exchange_passes_) {
        int r = two_exchange_step(shortlist, plateau && chain < swap_plateau_chain_, 1e-9);
        if (r == 1) { chain = 0; ++guard; }
        else if (r == 2) { ++chain; ++guard; }
        else break;
    }
}


// Candidate extensions of the current partial placement.
//
// Singles are the usual best-marginal-gain choices. Pairs exist because the
// exhaustive oracle (FINDINGS 5.11) showed plain greedy losing 20% on a
// *two-antenna* instance: it commits to the best single next antenna and cannot
// see the pair that beats it. 2-exchange (5.13) repairs that after the fact;
// this offers the pair during construction, before the budget is committed.
//
// Pairs are not enumerated over all candidates -- that is O(|C|^2). They are
// generated per building, from the antennas that actually see it, which is what
// makes the move affordable.
inline void Solver::top_extensions(int n_single, int n_pair, std::vector<Ext>& out) {
    out.clear();
    std::vector<std::pair<double, int32_t>> sing;
    sing.reserve(cands_.size() / 4 + 1);
    for (size_t c = 0; c < cands_.size(); ++c) {
        if (chosen_[c] || C_.start[c] == C_.start[c + 1]) continue;
        double g = gain((int32_t)c);
        if (g > 0) sing.emplace_back(g, (int32_t)c);
    }
    if ((int)sing.size() > n_single) {
        std::nth_element(sing.begin(), sing.begin() + n_single, sing.end(),
                         [](const std::pair<double, int32_t>& x,
                            const std::pair<double, int32_t>& y) { return x.first > y.first; });
        sing.resize(n_single);
    }
    std::sort(sing.begin(), sing.end(),
              [](const std::pair<double, int32_t>& x, const std::pair<double, int32_t>& y) {
                  return x.first > y.first;
              });
    for (const auto& t : sing) out.push_back({t.second, -1, t.first, 1});

    if (n_pair <= 0) return;
    // Buildings that a pair could plausibly finish: rank unserviced ones by how
    // close two antennas' worth of reach would get them.
    std::vector<std::pair<double, int32_t>> near;
    for (size_t b = 0; b < sc_.buildings.size(); ++b) {
        if (serviced_[b] || !active_[b]) continue;
        double need = target_[b] - cov_[b].measure;
        double r = reach_[b] > 0 ? reach_[b] : target_[b];
        if (need <= 0 || need > 2.0 * r) continue;   // unreachable by a pair
        near.emplace_back(need / r, (int32_t)b);
    }
    if ((int)near.size() > n_pair) {
        std::nth_element(near.begin(), near.begin() + n_pair, near.end(),
                         [](const std::pair<double, int32_t>& x,
                            const std::pair<double, int32_t>& y) { return x.first < y.first; });
        near.resize(n_pair);
    }
    std::vector<int32_t> bundle;
    for (const auto& nb : near) {
        int cost = completion_bundle(nb.second, bundle, 2);
        if (cost != 2) continue;                       // 1 is already a single
        double g = gain(bundle[0]);
        // Value the pair by what it achieves together, per antenna spent.
        State st = snapshot();
        apply(bundle[0]);
        g += gain(bundle[1]);
        restore(st);
        out.push_back({bundle[0], bundle[1], g, 2});
    }
}

// Beam search construction.
//
// Why this is not a repeat of the GRASP failure (FINDINGS 5.12): GRASP sampled
// randomly, and at k=50 over 78,727 candidates 128 restarts covered a vanishing
// corner of the space. A beam keeps deterministic breadth along the *whole*
// trajectory -- diversity is maintained at every step rather than only at the
// seed -- so its coverage does not decay with candidate count the same way.
inline void Solver::run_beam(int k, bool verbose) {
    std::vector<State> beam;
    beam.push_back(snapshot());
    std::vector<Ext> exts;
    long long forks = 0, pairs_taken = 0;

    while (true) {
        struct Cand { int parent; Ext e; double key; };
        std::vector<Cand> pool;
        bool any = false;
        for (size_t i = 0; i < beam.size(); ++i) {
            if ((int)beam[i].picked.size() >= k) continue;
            restore(beam[i]);
            int room = k - (int)picked_.size();
            top_extensions(beam_single_, room >= 2 ? beam_pair_ : 0, exts);
            for (const Ext& e : exts) {
                if (e.n > room) continue;
                // Rank by gain per antenna so a pair is not automatically
                // preferred just for spending more budget.
                pool.push_back({(int)i, e, e.gain / e.n});
                any = true;
            }
        }
        if (!any) break;
        std::sort(pool.begin(), pool.end(),
                  [](const Cand& x, const Cand& y) { return x.key > y.key; });

        std::vector<State> next;
        std::vector<std::vector<int32_t>> seen;
        for (const Cand& c : pool) {
            if ((int)next.size() >= beam_w_) break;
            restore(beam[c.parent]);
            apply(c.e.a);
            if (c.e.b >= 0) { apply(c.e.b); ++pairs_taken; }
            // Two parents can reach the same set by different orders; keep the
            // beam genuinely diverse rather than holding duplicates.
            std::vector<int32_t> key = picked_;
            std::sort(key.begin(), key.end());
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            next.push_back(snapshot());
            ++forks;
        }
        if (next.empty()) break;
        beam.swap(next);
        bool done = true;
        for (const auto& st : beam) if ((int)st.picked.size() < k) done = false;
        if (done) break;
    }

    int bi = 0;
    for (size_t i = 1; i < beam.size(); ++i) if (beam[i].score > beam[bi].score) bi = (int)i;
    restore(beam[bi]);
    if (verbose)
        std::fprintf(stderr, "[beam] w=%d k=%d forks=%lld pairs=%lld score=%d\n", beam_w_, k,
                     forks, pairs_taken, score_);
}


// Task 4, part 1: a multi-antenna exchange rate.
//
// The antenna-priced potential (FINDINGS 5.5) converts metres to antennas via
// reach_b = the largest slice any *single* antenna delivers. That is optimistic
// whenever a building actually needs two or three: the second and third antenna
// each deliver less than the first, because the easy facade is already taken.
//
// Replace it with the rate implied by the building's actual cheapest completion:
// if the cheapest set that lifts b to tau has m antennas, the honest per-antenna
// rate is tau*P_b / m. On this dataset 74.8% of buildings need two or more at
// tau=0.75 (3.1), so the correction is not a detail there.
inline void Solver::rebuild_reach_from_bundles() {
    size_t nb = sc_.buildings.size();
    std::vector<int> cost(nb, -1);
#pragma omp parallel
    {
        std::vector<int32_t> bundle;
#pragma omp for schedule(dynamic, 64)
        for (long long b = 0; b < (long long)nb; ++b)
            cost[b] = completion_bundle((int32_t)b, bundle, 8);
    }
    for (size_t b = 0; b < nb; ++b)
        if (cost[b] > 1) reach_[b] = target_[b] / cost[b];   // cost 1 already exact
}

// Task 4, part 2: Lagrangian completion pricing.
//
// Dualise the budget sum(y) <= k with a price lambda per antenna. Each building
// then answers independently: "is my cheapest completion worth buying at this
// price?" -- worth it when 1 - lambda*c_b > 0, i.e. c_b < 1/lambda. Sweep lambda
// until the union of the accepted bundles fits the budget.
//
// This differs from the ratio greedy of 5.4 in where the sharing comes from. The
// ratio greedy picks buildings sequentially by locally-cheapest marginal cost,
// so it never sees the antenna that is mediocre for any one building and
// excellent for twenty. Here every building bids at the same price and the
// *union* discovers the shared antennas by itself. It also respects the
// threshold natively: a building is completed or it is not, and partial
// coverage earns nothing, which is the opposite bias to truncated-greedy.
inline void Solver::run_lagrangian(int k, bool verbose) {
    size_t nb = sc_.buildings.size();
    std::vector<std::vector<int32_t>> bundle(nb);
    std::vector<int> cost(nb, -1);
    const int CAP = 6;

    reset();
#pragma omp parallel
    {
        std::vector<int32_t> b2;
#pragma omp for schedule(dynamic, 64)
        for (long long b = 0; b < (long long)nb; ++b) {
            int c = completion_bundle((int32_t)b, b2, CAP);
            if (c > 0) { cost[b] = c; bundle[b] = b2; }
        }
    }

    // Sweep the price downward. Integer costs make the sweep discrete: at each
    // level admit every building of that cost, cheapest first, stopping the
    // moment the union would exceed the budget. Within a level, prefer the
    // buildings whose bundles overlap what is already bought -- that is the
    // sharing the dual is supposed to expose.
    std::vector<char> taken(cands_.size(), 0);
    int used = 0, admitted = 0;
    for (int level = 1; level <= CAP && used < k; ++level) {
        std::vector<int32_t> tier;
        for (size_t b = 0; b < nb; ++b) if (cost[b] == level) tier.push_back((int32_t)b);
        // Order by marginal cost against what is already bought.
        bool progress = true;
        while (progress && used < k) {
            progress = false;
            int best_b = -1, best_marg = INT32_MAX;
            for (int32_t b : tier) {
                if (b < 0 || serviced_[b]) continue;
                int marg = 0;
                for (int32_t c : bundle[b]) if (!taken[c]) ++marg;
                if (marg > 0 && marg < best_marg && used + marg <= k) {
                    best_marg = marg; best_b = b;
                }
            }
            if (best_b < 0) break;
            for (int32_t c : bundle[best_b]) {
                if (taken[c]) continue;
                taken[c] = 1;
                apply(c);
                ++used;
            }
            ++admitted;
            progress = true;
            for (auto& x : tier) if (x == best_b) x = -1;
        }
    }

    // Any budget the price sweep could not spend goes to the ordinary greedy,
    // which is better than leaving antennas unplaced.
    if ((int)picked_.size() < k) run_greedy(k, false);
    if (verbose)
        std::fprintf(stderr, "[lagrangian] tau=%.2f k=%d admitted=%d antennas=%zu score=%d\n",
                     tau_, k, admitted, picked_.size(), score_);
}

inline void Solver::rebuild() {
    for (auto& a : cov_) a.clear();
    std::fill(serviced_.begin(), serviced_.end(), 0);
    score_ = 0;
    for (auto& h : holders_) h.clear();
    for (int32_t c : picked_) {
        int64_t a = C_.start[c], z = C_.start[c + 1];
        for (int64_t j = a; j < z;) {
            int32_t b = C_.bld[j];
            int64_t e = j;
            while (e < z && C_.bld[e] == b) { cov_[b].add(C_.s0[e], C_.s1[e]); ++e; }
            holders_[b].push_back(c);
            j = e;
        }
    }
    for (size_t b = 0; b < sc_.buildings.size(); ++b)
        if (cov_[b].measure >= target_[b]) { serviced_[b] = 1; ++score_; }
}

inline int Solver::lns(int k, double time_budget_sec, unsigned seed, bool verbose,
                       const std::vector<char>& mask) {
    if (lns_destroy_on_) return lns_destroy_loop(k, time_budget_sec, seed, verbose, mask);
    const bool two_exch = two_exchange_on_;
    // Large-neighbourhood search: free antennas that provably are not holding
    // any building above threshold, then re-spend the budget with the greedy.
    // Keeps the best solution seen, so it is safe to stop at any time.
    //
    // The repair inherits whatever target-set mask is in place. That matters:
    // repairing *unfocused* can never beat a focused solution, so an inherited
    // mask is what makes this useful on top of run_focus rather than a no-op.
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    std::mt19937 rng(seed);
    std::vector<int32_t> best = picked_;
    int best_score = score_;
    int rounds = 0, improved = 0;

    active_ = mask;

    while (elapsed() < time_budget_sec) {
        ++rounds;
        // --- destroy -------------------------------------------------------
        // An antenna is provably removable if, for every building it touches,
        // that building stays above threshold even after discarding *all* of
        // this antenna's contribution (a lower bound on coverage without it).
        std::vector<int32_t> keep;
        keep.reserve(picked_.size());
        std::vector<int32_t> freed;
        for (int32_t c : picked_) {
            bool needed = false;
            int64_t a = C_.start[c], z = C_.start[c + 1];
            for (int64_t j = a; j < z && !needed;) {
                int32_t b = C_.bld[j];
                int64_t e = j;
                double mine = 0;
                while (e < z && C_.bld[e] == b) { mine += C_.s1[e] - C_.s0[e]; ++e; }
                if (serviced_[b] && cov_[b].measure - mine < target_[b]) needed = true;
                j = e;
            }
            if (needed) keep.push_back(c); else freed.push_back(c);
        }
        // Also drop a random slice, so the search can escape a local optimum
        // even when nothing is provably redundant.
        if (rounds > 1 && !keep.empty()) {
            std::shuffle(keep.begin(), keep.end(), rng);
            size_t drop = std::max<size_t>(1, keep.size() / 10);
            for (size_t i = 0; i < drop && !keep.empty(); ++i) keep.pop_back();
        }
        if (freed.empty() && rounds > 1 && keep.size() == picked_.size()) break;

        // --- repair --------------------------------------------------------
        picked_ = keep;
        for (auto& f : chosen_) f = 0;
        for (int32_t c : picked_) chosen_[c] = 1;
        rebuild();
        run_greedy(k, false);

        // Interleave 2-exchange: destroy-repair only ever frees antennas that
        // hold nothing up, so a needed-but-wrong antenna is never reconsidered.
        if (two_exch) run_2exchange(t0, time_budget_sec);
        if (score_ > best_score) {
            best_score = score_;
            best = picked_;
            ++improved;
        } else {
            picked_ = best;
            for (auto& f : chosen_) f = 0;
            for (int32_t c : picked_) chosen_[c] = 1;
            rebuild();
        }
    }
    picked_ = best;
    for (auto& f : chosen_) f = 0;
    for (int32_t c : picked_) chosen_[c] = 1;
    rebuild();
    if (verbose)
        std::fprintf(stderr, "[lns] tau=%.2f k=%d rounds=%d improved=%d score=%d\n", tau_, k,
                     rounds, improved, score_);
    return best_score;
}

// Randomised-destroy LNS (cont2.md Task 2). Polish the current construction to
// an incumbent, then repeatedly ruin a fraction of it and rebuild with the
// tuned greedy. The move class is coupled multi-antenna replacement, which the
// rest of the stack cannot make: 2-exchange is radius 1, and the destroy in
// lns() only ever frees antennas provably holding nothing up. Keeps the best
// seen, so it is safe to stop at any time and only ratchets upward.
//
// The sweep is internal: rho cycles over {0.05, 0.10, 0.15} and the operator
// alternates between a uniform-random slice and a spatial cluster (a random
// chosen antenna plus its nearest chosen neighbours), so one time-budgeted run
// covers the whole portfolio the brief specifies -- the intended compute sink.
inline int Solver::lns_destroy_loop(int k, double time_budget_sec, unsigned seed, bool verbose,
                                    const std::vector<char>& mask) {
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    std::mt19937 rng(seed);
    active_ = mask;

    // Establish the incumbent by polishing the construction that is already in
    // picked_ (run_focus / run_greedy ran before lns() was called).
    if (two_exchange_on_) run_2exchange(t0, time_budget_sec);
    std::vector<int32_t> best = picked_;
    int best_score = score_;

    const double rhos[3] = {0.05, 0.10, 0.15};
    std::vector<char> is_victim(cands_.size(), 0);
    int iter = 0, improved = 0;
    while (elapsed() < time_budget_sec && best.size() > 1) {
        // Restore the incumbent to ruin from.
        picked_ = best;
        std::fill(chosen_.begin(), chosen_.end(), 0);
        for (int32_t c : picked_) chosen_[c] = 1;
        rebuild();

        double rho = rhos[iter % 3];
        bool cluster = ((iter / 3) & 1);
        int nd = std::max(1, (int)std::ceil(rho * (double)picked_.size()));
        if (nd >= (int)picked_.size()) nd = (int)picked_.size() - 1;

        // --- destroy: choose nd chosen antennas to remove --------------------
        std::vector<int32_t> victims;
        victims.reserve(nd);
        if (!cluster) {
            std::vector<int32_t> pool = picked_;
            std::shuffle(pool.begin(), pool.end(), rng);
            victims.assign(pool.begin(), pool.begin() + nd);
        } else {
            int32_t seedc = picked_[rng() % picked_.size()];
            Vec2 sp = cands_[seedc].p;
            std::vector<std::pair<double, int32_t>> d;
            d.reserve(picked_.size());
            for (int32_t c : picked_) {
                double dx = cands_[c].p.x - sp.x, dy = cands_[c].p.y - sp.y;
                d.emplace_back(dx * dx + dy * dy, c);
            }
            std::nth_element(d.begin(), d.begin() + nd, d.end(),
                             [](const std::pair<double, int32_t>& a,
                                const std::pair<double, int32_t>& b) { return a.first < b.first; });
            for (int i = 0; i < nd; ++i) victims.push_back(d[i].second);
        }

        for (int32_t c : victims) is_victim[c] = 1;
        std::vector<int32_t> keep;
        keep.reserve(best.size());
        for (int32_t c : best) if (!is_victim[c]) keep.push_back(c);
        for (int32_t c : victims) is_victim[c] = 0;

        // --- repair with the tuned greedy, then re-polish --------------------
        picked_ = keep;
        std::fill(chosen_.begin(), chosen_.end(), 0);
        for (int32_t c : picked_) chosen_[c] = 1;
        rebuild();
        run_greedy(k, false);
        if (two_exchange_on_) run_2exchange(t0, time_budget_sec);

        if (score_ > best_score) { best_score = score_; best = picked_; ++improved; }
        ++iter;
    }

    picked_ = best;
    std::fill(chosen_.begin(), chosen_.end(), 0);
    for (int32_t c : picked_) chosen_[c] = 1;
    rebuild();
    if (verbose)
        std::fprintf(stderr, "[lns-destroy] tau=%.2f k=%d iters=%d improved=%d score=%d\n",
                     tau_, k, iter, improved, score_);
    return best_score;
}

inline void Solver::run(Algo algo, int k, double time_budget_sec, unsigned seed, bool verbose) {
    reset();
    switch (algo) {
        case Algo::SelfCover: run_selfcover(k, verbose); break;
        case Algo::Truncated:
        case Algo::Potential: run_greedy(k, verbose); break;
        case Algo::Bundle: run_bundle(k, verbose); break;
        case Algo::BundleLNS:
            run_bundle(k, verbose);
            lns(k, time_budget_sec, seed, verbose, std::vector<char>(active_.size(), 1));
            break;
        case Algo::PotentialLNS:
            run_greedy(k, verbose);
            lns(k, time_budget_sec, seed, verbose, std::vector<char>(active_.size(), 1));
            break;
        case Algo::Focus: run_focus(k, verbose); break;
        case Algo::Beam: run_beam(k, verbose); break;
        case Algo::Lagrangian:
            if (reach_from_bundles_) rebuild_reach_from_bundles();
            run_lagrangian(k, verbose);
            break;
        case Algo::LagrangianLNS: {
            if (reach_from_bundles_) rebuild_reach_from_bundles();
            run_lagrangian(k, verbose);
            std::vector<char> full(active_.size(), 1);
            lns(k, time_budget_sec, seed, verbose, full);
            break;
        }
        case Algo::BeamLNS: {
            run_beam(k, verbose);
            std::vector<char> full(active_.size(), 1);
            lns(k, time_budget_sec, seed, verbose, full);
            break;
        }
        case Algo::CostAware: cost_mode_ = true; run_focus(k, verbose); break;
        case Algo::CostAwareLNS:
            cost_mode_ = true;
            run(Algo::FocusLNS, k, time_budget_sec, seed, verbose);
            return;
        case Algo::FocusLNS: {
            run_focus(k, verbose);
            // Polish under both neighbourhoods and keep the better.
            //
            // Repairing under the focused mask is what lets the polish improve
            // on a focused solution at all -- an unfocused repair just
            // re-proposes the myopic placement focus already beat. But the mask
            // is also a wall: it hides every building outside the target set.
            // Measured, neither dominates -- masked wins by 178 at
            // (0.75, 500), unmasked wins by 69 at (0.5, 50) -- and alternating
            // within one search splits the difference instead of taking the
            // max. So run both from the same start and keep the winner.
            const std::vector<char> focus_mask = active_;
            const std::vector<char> full_mask(active_.size(), 1);
            std::vector<int32_t> start = picked_;

            int a_score = lns(k, time_budget_sec * 0.5, seed, verbose, focus_mask);
            std::vector<int32_t> a_pick = picked_;

            picked_ = start;
            std::fill(chosen_.begin(), chosen_.end(), 0);
            for (int32_t c : picked_) chosen_[c] = 1;
            active_ = full_mask;
            rebuild();
            int b_score = lns(k, time_budget_sec * 0.5, seed + 1, verbose, full_mask);

            if (a_score > b_score) {
                picked_ = a_pick;
                std::fill(chosen_.begin(), chosen_.end(), 0);
                for (int32_t c : picked_) chosen_[c] = 1;
                rebuild();
            }
            std::fill(active_.begin(), active_.end(), 1);
            break;
        }
    }
}

}  // namespace gc
