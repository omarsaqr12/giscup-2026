// Candidate antenna sites and the precomputed visibility contribution map.
//
// Candidate policy is driven by one structural fact about this problem: an
// antenna at a polygon *vertex* covers both of its incident edges outright,
// while an antenna in the middle of an edge covers only that one edge. On the
// 2026 sample that means a single vertex antenna already delivers a median
// ~40% of its host building's perimeter for free. Vertices therefore dominate
// interior-edge points for self-coverage, and they have a second advantage:
// their coordinates are bit-identical to the input, so "the antenna lies on a
// building boundary" is exactly true rather than true-to-1e-13.
//
// Edge subdivision points are still available (--edge-spacing) because they can
// win on *external* coverage; whether they pay for themselves is measured, not
// assumed.
#pragma once
#include "visibility.hpp"
#include <vector>
#include <cstdio>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace gc {

struct Candidate {
    Vec2 p;
    int32_t host;      // building the site sits on
    int32_t vertex;    // vertex index within the host ring, or -1 for edge points
};

inline std::vector<Candidate> generate_candidates(const Scene& sc, double edge_spacing) {
    std::vector<Candidate> out;
    out.reserve(sc.edges.size() * 2);
    for (size_t b = 0; b < sc.buildings.size(); ++b) {
        const Building& B = sc.buildings[b];
        for (int32_t i = 0; i < B.n_edges; ++i) {
            const Edge& e = sc.edges[B.first_edge + i];
            out.push_back({e.a, (int32_t)b, i});          // the vertex itself
            if (edge_spacing > 0 && e.len > edge_spacing) {
                int n = (int)(e.len / edge_spacing);
                for (int s = 1; s <= n; ++s) {
                    double t = (double)s / (n + 1);
                    out.push_back({e.a + (e.b - e.a) * t, (int32_t)b, -1});
                }
            }
        }
    }
    return out;
}

// Contribution map in CSR form: for candidate c, entries [start[c], start[c+1])
// are the visible arcs it contributes, grouped and sorted by building.
struct Contribs {
    std::vector<int64_t> start;
    std::vector<int32_t> bld;
    std::vector<double> s0, s1;

    size_t entries() const { return bld.size(); }
    size_t bytes() const {
        return start.size() * 8 + bld.size() * 4 + s0.size() * 8 + s1.size() * 8;
    }
};

struct ContribOpts {
    double radius = 300.0;      // blocker/target cutoff during search, metres
    double min_frac = 0.01;     // drop a building whose total arc < this fraction of its perimeter
    bool verbose = true;
};

inline Contribs build_contributions(const Scene& sc, const Visibility& vis,
                                    const std::vector<Candidate>& cands,
                                    const ContribOpts& opt) {
    size_t nc = cands.size();
    std::vector<std::vector<ArcInterval>> per(nc);
    long long collinear = 0;

#pragma omp parallel reduction(+ : collinear)
    {
        VisScratch sr;
        std::vector<ArcInterval> arcs;
#pragma omp for schedule(dynamic, 256)
        for (long long i = 0; i < (long long)nc; ++i) {
            vis.visible_arcs(cands[i].p, opt.radius, sr, arcs);
            // Keep a building only if this one antenna delivers a meaningful
            // slice of it. Slivers of far-away facades cost memory and greedy
            // time but essentially never decide a threshold; the exact,
            // uncapped verification pass at the end recovers anything real.
            auto& dst = per[i];
            size_t j = 0;
            while (j < arcs.size()) {
                size_t e = j;
                double tot = 0;
                while (e < arcs.size() && arcs[e].building == arcs[j].building) {
                    tot += arcs[e].s1 - arcs[e].s0;
                    ++e;
                }
                double P = sc.buildings[arcs[j].building].perimeter;
                if (tot >= opt.min_frac * P)
                    dst.insert(dst.end(), arcs.begin() + j, arcs.begin() + e);
                j = e;
            }
        }
        collinear += sr.collinear_skipped;
    }

    Contribs C;
    C.start.resize(nc + 1, 0);
    for (size_t i = 0; i < nc; ++i) C.start[i + 1] = C.start[i] + (int64_t)per[i].size();
    size_t total = (size_t)C.start[nc];
    C.bld.resize(total);
    C.s0.resize(total);
    C.s1.resize(total);
#pragma omp parallel for schedule(static)
    for (long long i = 0; i < (long long)nc; ++i) {
        int64_t o = C.start[i];
        for (const ArcInterval& a : per[i]) {
            C.bld[o] = a.building;
            C.s0[o] = a.s0;
            C.s1[o] = a.s1;
            ++o;
        }
        per[i].clear();
        per[i].shrink_to_fit();
    }
    if (opt.verbose) {
        std::fprintf(stderr,
                     "[contrib] candidates=%zu entries=%zu (%.1f per candidate) mem=%.2f GB\n",
                     nc, total, (double)total / (double)nc, C.bytes() / 1073741824.0);
        if (collinear)
            std::fprintf(stderr,
                         "[contrib] WARNING %lld exactly edge-on non-incident edges skipped;\n"
                         "          this dataset has grazing-visible walls the sweep drops.\n",
                         collinear);
    }
    return C;
}

// Inverse index: for building b, which candidates see it and where.
struct InvIndex {
    std::vector<int64_t> start;   // nb+1
    std::vector<int32_t> cand;    // candidate id
    std::vector<int64_t> e0, e1;  // that candidate's arc range in Contribs
};

inline InvIndex build_inverse(const Scene& sc, const Contribs& C, size_t ncand) {
    size_t nb = sc.buildings.size();
    InvIndex I;
    I.start.assign(nb + 1, 0);
    // Count runs (one run = one candidate's arcs on one building).
    std::vector<int64_t> cnt(nb, 0);
    for (size_t c = 0; c < ncand; ++c) {
        int64_t a = C.start[c], z = C.start[c + 1];
        for (int64_t j = a; j < z;) {
            int64_t e = j;
            while (e < z && C.bld[e] == C.bld[j]) ++e;
            cnt[C.bld[j]]++;
            j = e;
        }
    }
    for (size_t b = 0; b < nb; ++b) I.start[b + 1] = I.start[b] + cnt[b];
    size_t tot = (size_t)I.start[nb];
    I.cand.resize(tot);
    I.e0.resize(tot);
    I.e1.resize(tot);
    std::vector<int64_t> fill(nb, 0);
    for (size_t c = 0; c < ncand; ++c) {
        int64_t a = C.start[c], z = C.start[c + 1];
        for (int64_t j = a; j < z;) {
            int64_t e = j;
            while (e < z && C.bld[e] == C.bld[j]) ++e;
            int32_t b = C.bld[j];
            int64_t o = I.start[b] + fill[b]++;
            I.cand[o] = (int32_t)c;
            I.e0[o] = j;
            I.e1[o] = e;
            j = e;
        }
    }
    return I;
}

}  // namespace gc
