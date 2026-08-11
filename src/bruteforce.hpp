// Independent, deliberately naive reference implementation of Definitions 1-3.
//
// The sweep in visibility.hpp is fast but subtle. This one is slow and obvious:
// densely sample each boundary, and for every sample test the connecting segment
// against the local geometry directly. Agreement between two implementations
// that share no code path is the strongest evidence available that what we
// submit is what the organizers will score.
//
// Note the case that a "does the segment cross any edge?" test alone gets wrong:
// a segment from one point on a building's boundary to another point on the
// same building, passing straight through its interior, crosses no edge
// properly -- it starts and ends *on* the boundary. That case is exactly the
// self-occlusion that dominates this problem, so the reference tests interior
// containment along the segment as well.
#pragma once
#include "scene.hpp"
#include <vector>

namespace gc {

// Is `p` strictly inside the interior of one of the listed buildings?
inline bool point_in_interior(const Scene& sc, const std::vector<int32_t>& blds,
                              const Vec2& p, double eps) {
    for (int32_t b : blds) {
        const Building& B = sc.buildings[b];
        if (p.x < B.minx - eps || p.x > B.maxx + eps ||
            p.y < B.miny - eps || p.y > B.maxy + eps) continue;
        bool inside = false, on_boundary = false;
        for (int32_t i = 0; i < B.n_edges; ++i) {
            const Edge& e = sc.edges[B.first_edge + i];
            if (point_seg_dist2(p, e.a, e.b) <= eps * eps) { on_boundary = true; break; }
            if ((e.a.y > p.y) != (e.b.y > p.y)) {
                double x = e.a.x + (p.y - e.a.y) / (e.b.y - e.a.y) * (e.b.x - e.a.x);
                if (x > p.x) inside = !inside;
            }
        }
        if (!on_boundary && inside) return true;
    }
    return false;
}

// Does segment [p,q] meet the interior of any listed building?
// Touching an edge or passing through a vertex does not block (Definition 1).
inline bool segment_blocked(const Scene& sc, const std::vector<int32_t>& blds,
                            const Vec2& p, const Vec2& q, double eps) {
    double slox = std::min(p.x, q.x), shix = std::max(p.x, q.x);
    double sloy = std::min(p.y, q.y), shiy = std::max(p.y, q.y);
    for (int32_t b : blds) {
        const Building& B = sc.buildings[b];
        if (B.minx > shix || B.maxx < slox || B.miny > shiy || B.maxy < sloy) continue;
        for (int32_t i = 0; i < B.n_edges; ++i) {
            const Edge& e = sc.edges[B.first_edge + i];
            // A wall that an endpoint sits on never blocks: the connecting
            // segment runs along the boundary, not through an interior.
            if (point_seg_dist2(p, e.a, e.b) <= eps * eps) continue;
            if (point_seg_dist2(q, e.a, e.b) <= eps * eps) continue;
            int o1 = orient_sign(p, q, e.a);
            int o2 = orient_sign(p, q, e.b);
            int o3 = orient_sign(e.a, e.b, p);
            int o4 = orient_sign(e.a, e.b, q);
            if (o1 * o2 < 0 && o3 * o4 < 0) return true;  // proper crossing
        }
    }
    // No proper crossing still leaves the boundary-to-boundary-through-the-
    // interior case, and segments that enter through a vertex. Sample along.
    const int NS = 19;
    Vec2 d = q - p;
    for (int i = 1; i <= NS; ++i) {
        Vec2 m = p + d * ((double)i / (NS + 1));
        if (point_in_interior(sc, blds, m, eps)) return true;
    }
    return false;
}

// Coverage of building `b` from `antennas`, by dense sampling of its boundary.
// `blds` scopes which buildings may act as blockers -- callers pass every
// building that could possibly lie between the antennas and the target.
inline double brute_coverage(const Scene& sc, const std::vector<int32_t>& blds,
                             const std::vector<Vec2>& antennas, int32_t b,
                             int samples_per_edge, double eps = 1e-6) {
    const Building& B = sc.buildings[b];
    double visible = 0, total = 0;
    for (int32_t i = 0; i < B.n_edges; ++i) {
        const Edge& e = sc.edges[B.first_edge + i];
        int n = samples_per_edge;
        double dl = e.len / n;
        for (int s = 0; s < n; ++s) {
            double t = (s + 0.5) / n;
            Vec2 q = e.a + (e.b - e.a) * t;
            total += dl;
            for (const Vec2& p : antennas) {
                if (!segment_blocked(sc, blds, p, q, eps)) { visible += dl; break; }
            }
        }
    }
    return total > 0 ? visible / total : 0.0;
}

}  // namespace gc
