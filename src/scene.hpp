// Scene: building footprints, arc-length parameterisation, and a uniform grid
// index over edges.
//
// Every building's boundary is mapped to an arc coordinate s in [0, P_b) so a
// set of antennas induces a set of intervals per building; coverage is then the
// measure of their union over P_b. Working in exact arc intervals (rather than
// sampling the boundary) means the coverage ratios we compute are the same
// numbers the organizers will compute, with no discretisation gap to eat into.
#pragma once
#include "geom.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace gc {

struct Edge {
    Vec2 a, b;
    int32_t building;   // index into Scene::buildings
    int32_t local;      // edge index within that building's ring
    double s0;          // arc coordinate of `a` along the building boundary
    double len;
};

struct Building {
    std::string id;         // as written in the GeoJSON "id" property
    int32_t first_edge;     // index of first edge in Scene::edges
    int32_t n_edges;
    double perimeter;
    Vec2 centroid;
    double minx, miny, maxx, maxy;
};

// ---------------------------------------------------------------------------
// Minimal JSON value scanner. The dataset is a plain FeatureCollection; we pull
// out each feature's "id" property and its Polygon coordinates without pulling
// in a dependency (the submission must compile from source on the organizers'
// machine, so fewer moving parts is better).
// ---------------------------------------------------------------------------
class JsonScanner {
public:
    explicit JsonScanner(const std::string& text) : s_(text) {}

    // Advance to the next occurrence of `key` used as an object key.
    bool seek_key(const char* key, size_t& pos) const {
        std::string pat = std::string("\"") + key + "\"";
        size_t p = s_.find(pat, pos);
        if (p == std::string::npos) return false;
        pos = p + pat.size();
        return true;
    }
    const std::string& text() const { return s_; }

private:
    const std::string& s_;
};

class Scene {
public:
    std::vector<Building> buildings;
    std::vector<Edge> edges;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;

    void load_geojson(const std::string& path);
    // Build directly from rings (used by the figure regression tests).
    void build_from_rings(const std::vector<std::vector<Vec2>>& rings,
                          const std::vector<std::string>& ids);

    void build_index(double cell);

    // Append indices of every edge whose bounding box comes within `r` of p.
    void query_edges(const Vec2& p, double r, std::vector<int32_t>& out) const;
    // Every edge, unfiltered (used for the exact uncapped verification pass).
    size_t edge_count() const { return edges.size(); }

    double cell_size() const { return cell_; }

private:
    double cell_ = 0;
    int nx_ = 0, ny_ = 0;
    std::vector<int32_t> cell_start_;  // CSR offsets, size nx_*ny_+1
    std::vector<int32_t> cell_items_;
};

inline void Scene::build_from_rings(const std::vector<std::vector<Vec2>>& rings,
                                    const std::vector<std::string>& ids) {
    buildings.clear();
    edges.clear();
    minx = miny = 1e300;
    maxx = maxy = -1e300;
    for (size_t bi = 0; bi < rings.size(); ++bi) {
        // Normalise every ring to counter-clockwise so "the interior is on the
        // left of each directed edge" holds globally. The visibility sweep
        // relies on that to know which directions from an antenna point into
        // the wall it is mounted on. (The 2026 sample ships clockwise rings.)
        std::vector<Vec2> r = rings[bi];
        double a2 = 0;
        for (size_t i = 0; i < r.size(); ++i) {
            const Vec2& u = r[i];
            const Vec2& v = r[(i + 1) % r.size()];
            a2 += u.x * v.y - v.x * u.y;
        }
        if (a2 < 0) std::reverse(r.begin(), r.end());
        Building B;
        B.id = ids[bi];
        B.first_edge = (int32_t)edges.size();
        B.perimeter = 0;
        B.minx = B.miny = 1e300;
        B.maxx = B.maxy = -1e300;
        double cx = 0, cy = 0;
        size_t n = r.size();
        double s = 0;
        for (size_t i = 0; i < n; ++i) {
            Vec2 a = r[i], b = r[(i + 1) % n];
            double L = dist(a, b);
            if (L == 0.0) continue;  // drop duplicate vertices
            Edge e;
            e.a = a; e.b = b;
            e.building = (int32_t)bi;
            e.local = (int32_t)(edges.size() - B.first_edge);
            e.s0 = s;
            e.len = L;
            s += L;
            edges.push_back(e);
            B.minx = std::min(B.minx, std::min(a.x, b.x));
            B.maxx = std::max(B.maxx, std::max(a.x, b.x));
            B.miny = std::min(B.miny, std::min(a.y, b.y));
            B.maxy = std::max(B.maxy, std::max(a.y, b.y));
            cx += a.x; cy += a.y;
        }
        B.n_edges = (int32_t)edges.size() - B.first_edge;
        B.perimeter = s;
        if (B.n_edges > 0) { B.centroid = Vec2(cx / B.n_edges, cy / B.n_edges); }
        buildings.push_back(B);
        minx = std::min(minx, B.minx); maxx = std::max(maxx, B.maxx);
        miny = std::min(miny, B.miny); maxy = std::max(maxy, B.maxy);
    }
}

inline void Scene::build_index(double cell) {
    cell_ = cell;
    nx_ = std::max(1, (int)((maxx - minx) / cell) + 1);
    ny_ = std::max(1, (int)((maxy - miny) / cell) + 1);
    size_t ncell = (size_t)nx_ * ny_;
    std::vector<int32_t> counts(ncell + 1, 0);

    auto cells_of = [&](const Edge& e, int& i0, int& i1, int& j0, int& j1) {
        double lo_x = std::min(e.a.x, e.b.x), hi_x = std::max(e.a.x, e.b.x);
        double lo_y = std::min(e.a.y, e.b.y), hi_y = std::max(e.a.y, e.b.y);
        i0 = std::max(0, (int)((lo_x - minx) / cell_));
        i1 = std::min(nx_ - 1, (int)((hi_x - minx) / cell_));
        j0 = std::max(0, (int)((lo_y - miny) / cell_));
        j1 = std::min(ny_ - 1, (int)((hi_y - miny) / cell_));
    };

    for (const auto& e : edges) {
        int i0, i1, j0, j1; cells_of(e, i0, i1, j0, j1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) counts[(size_t)j * nx_ + i + 1]++;
    }
    cell_start_.assign(ncell + 1, 0);
    for (size_t c = 0; c < ncell; ++c) cell_start_[c + 1] = cell_start_[c] + counts[c + 1];
    cell_items_.assign(cell_start_[ncell], 0);
    std::vector<int32_t> fill(ncell, 0);
    for (size_t ei = 0; ei < edges.size(); ++ei) {
        int i0, i1, j0, j1; cells_of(edges[ei], i0, i1, j0, j1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                size_t c = (size_t)j * nx_ + i;
                cell_items_[cell_start_[c] + fill[c]++] = (int32_t)ei;
            }
    }
}

inline void Scene::query_edges(const Vec2& p, double r, std::vector<int32_t>& out) const {
    out.clear();
    int i0 = std::max(0, (int)((p.x - r - minx) / cell_));
    int i1 = std::min(nx_ - 1, (int)((p.x + r - minx) / cell_));
    int j0 = std::max(0, (int)((p.y - r - miny) / cell_));
    int j1 = std::min(ny_ - 1, (int)((p.y + r - miny) / cell_));
    double r2 = r * r;
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            size_t c = (size_t)j * nx_ + i;
            for (int32_t t = cell_start_[c]; t < cell_start_[c + 1]; ++t) {
                int32_t ei = cell_items_[t];
                const Edge& e = edges[ei];
                if (point_seg_dist2(p, e.a, e.b) <= r2) out.push_back(ei);
            }
        }
    }
    // An edge can span several cells; de-duplicate.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

}  // namespace gc
