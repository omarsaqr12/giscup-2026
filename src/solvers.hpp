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
                  Focus, FocusLNS, CostAware, CostAwareLNS };

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
        for (auto& a : cov_) a.clear();
        std::fill(serviced_.begin(), serviced_.end(), 0);
        std::fill(chosen_.begin(), chosen_.end(), 0);
        picked_.clear();
        score_ = 0;
    }

    int score() const { return score_; }
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

    void run(Algo algo, int k, double time_budget_sec, unsigned seed, bool verbose);

    // Recompute coverage from scratch for the current pick set.
    void rebuild();

private:
    void run_greedy(int k, bool verbose);
    void run_bundle(int k, bool verbose);
    void run_selfcover(int k, bool verbose);
    void run_focus(int k, bool verbose);
    int lns(int k, double time_budget_sec, unsigned seed, bool verbose,
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
    bool cost_mode_ = false;  // price remaining work in antennas rather than metres
    std::vector<int32_t> picked_;
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
    while ((int)picked_.size() < k && !pq.empty()) {
        Node n = pq.top();
        pq.pop();
        if (chosen_[n.c]) continue;
        if (n.stamp == iter) {           // gain is fresh for this round -> take it
            apply(n.c);
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

inline void Solver::rebuild() {
    for (auto& a : cov_) a.clear();
    std::fill(serviced_.begin(), serviced_.end(), 0);
    score_ = 0;
    for (int32_t c : picked_) {
        int64_t a = C_.start[c], z = C_.start[c + 1];
        for (int64_t j = a; j < z; ++j) cov_[C_.bld[j]].add(C_.s0[j], C_.s1[j]);
    }
    for (size_t b = 0; b < sc_.buildings.size(); ++b)
        if (cov_[b].measure >= target_[b]) { serviced_[b] = 1; ++score_; }
}

inline int Solver::lns(int k, double time_budget_sec, unsigned seed, bool verbose,
                       const std::vector<char>& mask) {
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
