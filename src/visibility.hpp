// Exact visible-arc computation by rotational plane sweep.
//
// For an antenna at p we sweep a ray through 2*pi, maintaining the set of edges
// the ray currently crosses. Over each angular span between two events the
// nearest active edge is exactly the piece of the world that is visible, so the
// sweep directly emits the visible sub-arcs of every nearby building at once --
// no boundary sampling, no discretisation error in the coverage ratio.
//
// Two facts about Definition 1 drive the special cases here:
//
//   * A segment that only grazes a boundary does not block. An edge incident to
//     p subtends zero angle, so it blocks nothing -- but it is itself *fully*
//     visible from p, because the connecting segment runs along the boundary and
//     never enters an interior. This is why a vertex antenna covers both of its
//     incident edges for free, which is the single most important lever in the
//     whole problem.
//
//   * A building's own interior blocks its own boundary. The host building is
//     therefore a normal blocker for all of its non-incident edges. (Verified
//     against Figure 4: antenna `a` sits on building 1 and sees only three of
//     its eight edges.)
#pragma once
#include "scene.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

namespace gc {

struct ArcInterval {
    int32_t building;
    double s0, s1;  // arc coordinates along that building's boundary, s0 < s1
};

// Scratch buffers reused across candidates so the hot loop does no allocation.
struct VisScratch {
    std::vector<int32_t> near_edges;
    std::vector<int32_t> active;
    struct Event { double ang; int type; int32_t ei; Vec2 dir; };
    std::vector<Event> events;
    std::vector<ArcInterval> raw;
    // Angular wedges pointing into the interior of a building the antenna is
    // mounted on. Nothing is visible there: the connecting segment would leave
    // through that building's own interior.
    struct Wedge { Vec2 from, to; bool reflex; };
    std::vector<Wedge> dark;
    long long collinear_skipped = 0;  // diagnostic: exact edge-on non-incident edges
};

// Is direction d strictly inside the wedge swept counter-clockwise from `from`
// to `to`? Handles reflex wedges (> pi) via the sign of cross(from, to).
inline bool in_wedge(const VisScratch::Wedge& w, const Vec2& d) {
    double c1 = cross(w.from, d), c2 = cross(d, w.to);
    return w.reflex ? (c1 > 0 || c2 > 0) : (c1 > 0 && c2 > 0);
}

class Visibility {
public:
    explicit Visibility(const Scene& sc) : sc_(sc) {}

    // How close a point must be to an edge to count as mounted on it.
    //
    // A point in the interior of an edge is essentially never *exactly* on it in
    // doubles: the organizers' own Figure 5 antennas miss exact collinearity by
    // ~1e-13. So incidence has to be a tolerance test, and any verifier must do
    // the same. 1e-6 m is ~3 orders above the double spacing at UTM magnitudes
    // and ~6 orders below the shortest edge in the data.
    //
    // We still prefer to *emit* antennas at exact input vertices, where the
    // question does not arise at all.
    double on_edge_eps = 1e-6;

    // Visible arcs from p, merged per building and sorted by (building, s0).
    // `radius` <= 0 means "use every edge in the scene" (exact, uncapped).
    void visible_arcs(const Vec2& p, double radius, VisScratch& sr,
                      std::vector<ArcInterval>& out) const;

private:
    const Scene& sc_;
};

inline void Visibility::visible_arcs(const Vec2& p, double radius, VisScratch& sr,
                                     std::vector<ArcInterval>& out) const {
    out.clear();
    sr.raw.clear();
    sr.events.clear();
    sr.active.clear();
    sr.dark.clear();

    const auto& E = sc_.edges;
    const int32_t* ids;
    size_t n_ids;
    std::vector<int32_t> all;
    if (radius > 0) {
        sc_.query_edges(p, radius, sr.near_edges);
        ids = sr.near_edges.data();
        n_ids = sr.near_edges.size();
    } else {
        all.resize(E.size());
        for (size_t i = 0; i < E.size(); ++i) all[i] = (int32_t)i;
        ids = all.data();
        n_ids = all.size();
    }

    // ---- pass 1: classify edges, emit sweep events -------------------------
    struct Incident { int32_t building; Vec2 dir; };
    std::vector<Incident> inc_out, inc_in;  // at most a handful
    const double eps2 = on_edge_eps * on_edge_eps;
    for (size_t t = 0; t < n_ids; ++t) {
        int32_t ei = ids[t];
        const Edge& e = E[ei];
        Vec2 va = e.a - p, vb = e.b - p;

        if (point_seg_dist2(p, e.a, e.b) <= eps2) {
            // Incident edge: fully visible (the connecting segment runs along
            // the boundary and never enters an interior), and it blocks nothing
            // because it subtends zero angle. Record which way it leaves p so
            // the interior wedge can be reconstructed below.
            sr.raw.push_back({e.building, e.s0, e.s0 + e.len});
            bool at_a = norm2(va) <= eps2;
            bool at_b = norm2(vb) <= eps2;
            if (at_a && !at_b) inc_out.push_back({e.building, e.b - p});
            else if (at_b && !at_a) inc_in.push_back({e.building, e.a - p});
            else if (!at_a && !at_b) {
                // Mid-edge mount: the whole interior half-plane is dark.
                sr.dark.push_back({e.b - e.a, e.a - e.b, false});
            }
            continue;
        }

        double cr = cross(va, vb);
        if (cr == 0.0) {
            // Exactly edge-on but not incident. Subtends zero angle, so the
            // sweep cannot represent it; it is grazing-visible in principle.
            // Measured as absent from this dataset -- counted so a different
            // dataset would surface it rather than silently losing length.
            sr.collinear_skipped++;
            continue;
        }

        // Angular interval, traversed counter-clockwise from `start` to `end`.
        Vec2 ds = (cr > 0) ? va : vb;
        Vec2 de = (cr > 0) ? vb : va;
        double as = std::atan2(ds.y, ds.x);
        double ae = std::atan2(de.y, de.x);
        sr.events.push_back({as, 0, ei, ds});
        sr.events.push_back({ae, 1, ei, de});
        if (as > ae) sr.active.push_back(ei);  // interval wraps the -pi seam
    }

    // A vertex mount: the interior wedge runs counter-clockwise from the
    // outgoing edge direction round to the reversed incoming edge direction.
    // Rings are normalised counter-clockwise, so the interior is on the left of
    // every directed edge and this holds for convex and reflex vertices alike.
    for (const Incident& o : inc_out)
        for (const Incident& i : inc_in)
            if (o.building == i.building)
                sr.dark.push_back({o.dir, i.dir, cross(o.dir, i.dir) < 0});

    auto is_dark = [&](const Vec2& d) {
        for (const auto& w : sr.dark) if (in_wedge(w, d)) return true;
        return false;
    };

    if (!sr.events.empty()) {
        // Split the sweep at every wedge boundary so no emitted span straddles
        // the edge of a dark region.
        for (const auto& w : sr.dark) {
            sr.events.push_back({std::atan2(w.from.y, w.from.x), 2, -1, w.from});
            sr.events.push_back({std::atan2(w.to.y, w.to.x), 2, -1, w.to});
        }
    }
    if (!sr.events.empty()) {
        // Insertions before removals at an identical angle keeps the front
        // continuous where two edges of one polygon meet at a shared vertex.
        std::sort(sr.events.begin(), sr.events.end(),
                  [](const VisScratch::Event& x, const VisScratch::Event& y) {
                      if (x.ang != y.ang) return x.ang < y.ang;
                      return x.type < y.type;
                  });

        // Front-most active edge along direction `dir`.
        auto front_of = [&](const Vec2& dir) -> int32_t {
            int32_t best = -1;
            double bestd = 1e300;
            for (int32_t ei : sr.active) {
                const Edge& e = E[ei];
                double ts, tr;
                if (!ray_segment_param(p, dir, e.a, e.b, ts, tr)) continue;
                if (tr <= 0) continue;
                if (ts < -1e-12 || ts > 1 + 1e-12) continue;
                if (tr < bestd) { bestd = tr; best = ei; }
            }
            return best;
        };

        Vec2 prev_dir(-1.0, 0.0);  // the -pi seam
        double prev_ang = -M_PI;

        auto emit_span = [&](const Vec2& d1, const Vec2& d2, double a1, double a2) {
            if (sr.active.empty() || a2 <= a1) return;
            // A direction strictly inside the span; the span is < pi wide so the
            // normalised bisector is safe.
            double n1 = norm(d1), n2 = norm(d2);
            if (n1 == 0 || n2 == 0) return;
            Vec2 mid = d1 * (1.0 / n1) + d2 * (1.0 / n2);
            if (norm2(mid) < 1e-24) {  // near-antipodal: fall back to mid-angle
                double am = 0.5 * (a1 + a2);
                mid = Vec2(std::cos(am), std::sin(am));
            }
            if (is_dark(mid)) return;  // points into a wall we are mounted on
            int32_t f = front_of(mid);
            if (f < 0) return;
            const Edge& e = E[f];
            double t1, t2, tr;
            if (!ray_segment_param(p, d1, e.a, e.b, t1, tr)) return;
            if (!ray_segment_param(p, d2, e.a, e.b, t2, tr)) return;
            if (t1 > t2) std::swap(t1, t2);
            t1 = std::max(0.0, std::min(1.0, t1));
            t2 = std::max(0.0, std::min(1.0, t2));
            if (t2 <= t1) return;
            sr.raw.push_back({e.building, e.s0 + t1 * e.len, e.s0 + t2 * e.len});
        };

        size_t i = 0;
        while (i < sr.events.size()) {
            double a = sr.events[i].ang;
            Vec2 dir = sr.events[i].dir;
            emit_span(prev_dir, dir, prev_ang, a);
            // Apply every event at this angle: insertions, then removals.
            size_t j = i;
            while (j < sr.events.size() && sr.events[j].ang == a) {
                if (sr.events[j].type == 0) sr.active.push_back(sr.events[j].ei);
                ++j;
            }
            for (size_t q = i; q < j; ++q) {
                if (sr.events[q].type == 1) {
                    auto it = std::find(sr.active.begin(), sr.active.end(), sr.events[q].ei);
                    if (it != sr.active.end()) {
                        *it = sr.active.back();
                        sr.active.pop_back();
                    }
                }
            }
            prev_ang = a;
            prev_dir = dir;
            i = j;
        }
        // Close the sweep back at the +pi seam.
        emit_span(prev_dir, Vec2(-1.0, 0.0), prev_ang, M_PI);
    }

    // ---- merge into per-building disjoint intervals -------------------------
    if (sr.raw.empty()) return;
    std::sort(sr.raw.begin(), sr.raw.end(), [](const ArcInterval& x, const ArcInterval& y) {
        if (x.building != y.building) return x.building < y.building;
        return x.s0 < y.s0;
    });
    for (const ArcInterval& iv : sr.raw) {
        if (!out.empty() && out.back().building == iv.building && iv.s0 <= out.back().s1 + 1e-9) {
            out.back().s1 = std::max(out.back().s1, iv.s1);
        } else {
            out.push_back(iv);
        }
    }
    // An arc spanning s = 0 arrives as two disjoint intervals (one ending at P,
    // one starting at 0). They are disjoint, so the covered measure is already
    // correct; no seam fusing is needed.
}

}  // namespace gc
