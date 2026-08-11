// GeoJSON FeatureCollection reader for building footprints.
//
// Deliberately dependency-free: the submission has to compile from source on the
// organizers' machine, so the fewer third-party pieces the better. Only the
// subset of JSON that a footprint FeatureCollection uses is handled.
#pragma once
#include "scene.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace gc {

namespace detail {

struct Cursor {
    const char* p;
    const char* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool eat(char c) { ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    void expect(char c) {
        if (!eat(c)) throw std::runtime_error(std::string("GeoJSON: expected '") + c + "'");
    }
    std::string str() {
        ws();
        expect('"');
        std::string s;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) { s.push_back(p[1]); p += 2; }
            else s.push_back(*p++);
        }
        expect('"');
        return s;
    }
    double num() {
        ws();
        char* e = nullptr;
        double v = std::strtod(p, &e);
        if (e == p) throw std::runtime_error("GeoJSON: bad number");
        p = e;
        return v;
    }
    // Skip an arbitrary JSON value.
    void skip_value() {
        ws();
        if (p >= end) return;
        if (*p == '"') { str(); return; }
        if (*p == '{' || *p == '[') {
            char open = *p, close = (open == '{') ? '}' : ']';
            int depth = 0;
            while (p < end) {
                if (*p == '"') { str(); continue; }
                if (*p == open) ++depth;
                else if (*p == close) { --depth; if (depth == 0) { ++p; return; } }
                ++p;
            }
            return;
        }
        while (p < end && *p != ',' && *p != '}' && *p != ']') ++p;
    }
};

// Read a scalar JSON value (string / number / bool / null) as text -- used to
// accept building ids whether they arrive quoted or numeric.
inline std::string scalar_as_string(Cursor& c) {
    c.ws();
    if (c.p < c.end && *c.p == '"') return c.str();
    const char* s = c.p;
    while (c.p < c.end && *c.p != ',' && *c.p != '}' && *c.p != ']' &&
           *c.p != ' ' && *c.p != '\n' && *c.p != '\r' && *c.p != '\t') ++c.p;
    std::string t(s, c.p);
    // Trim a trailing ".0" so an id read as 17.0 still writes back as "17".
    if (t.size() > 2 && t.substr(t.size() - 2) == ".0") t = t.substr(0, t.size() - 2);
    return t;
}

}  // namespace detail

// Loads polygons. MultiPolygon and multi-ring Polygons are accepted: the ring
// with the largest absolute area becomes the footprint. (The 2026 sample has one
// feature with a stray second ring even though the spec promises no holes, so
// being tolerant here costs nothing and avoids a run-day surprise.)
inline void Scene::load_geojson(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();

    detail::Cursor c{text.data(), text.data() + text.size()};
    std::vector<std::vector<Vec2>> rings;
    std::vector<std::string> ids;

    auto read_ring = [&](detail::Cursor& cur, std::vector<Vec2>& ring) {
        ring.clear();
        cur.expect('[');
        cur.ws();
        if (cur.eat(']')) return;
        do {
            cur.expect('[');
            double x = cur.num();
            cur.expect(',');
            double y = cur.num();
            cur.ws();
            while (cur.p < cur.end && *cur.p != ']') ++cur.p;  // ignore z / m
            cur.expect(']');
            ring.push_back({x, y});
        } while (cur.eat(','));
        cur.expect(']');
        // GeoJSON rings repeat the first point; drop it.
        if (ring.size() > 1 && ring.front() == ring.back()) ring.pop_back();
    };

    auto ring_area2 = [](const std::vector<Vec2>& r) {
        double a = 0;
        for (size_t i = 0; i < r.size(); ++i) {
            const Vec2& u = r[i];
            const Vec2& v = r[(i + 1) % r.size()];
            a += u.x * v.y - v.x * u.y;
        }
        return std::fabs(a);
    };

    // Walk features by scanning for the tokens we care about, so unrelated
    // top-level members (crs, bbox, name...) are simply skipped.
    size_t auto_id = 0;
    while (true) {
        size_t pos = (size_t)(c.p - text.data());
        size_t f = text.find("\"geometry\"", pos);
        if (f == std::string::npos) break;

        // The id lives in "properties" which precedes "geometry" in this layout;
        // search backwards from the feature start for the nearest id-like key.
        std::string id;
        size_t feat_start = text.rfind('{', f);
        size_t pstart = text.rfind("\"properties\"", f);
        if (pstart != std::string::npos && (feat_start == std::string::npos || pstart > feat_start - 200)) {
            for (const char* key : {"\"id\"", "\"ID\"", "\"building_id\"", "\"fid\"", "\"OBJECTID\"", "\"osm_id\""}) {
                size_t kp = text.find(key, pstart);
                if (kp != std::string::npos && kp < f) {
                    detail::Cursor ic{text.data() + kp + std::char_traits<char>::length(key),
                                      text.data() + text.size()};
                    ic.ws();
                    if (ic.eat(':')) { id = detail::scalar_as_string(ic); }
                    break;
                }
            }
        }
        if (id.empty()) id = std::to_string(++auto_id);

        size_t cpos = text.find("\"coordinates\"", f);
        if (cpos == std::string::npos) break;
        detail::Cursor gc_{text.data() + cpos + 13, text.data() + text.size()};
        gc_.ws();
        gc_.expect(':');
        gc_.ws();
        // Depth 3 = Polygon ([[ [x,y], ... ]]), depth 4 = MultiPolygon.
        const char* probe = gc_.p;
        int depth = 0;
        while (probe < gc_.end && *probe == '[') {
            ++depth; ++probe;
            while (probe < gc_.end && (*probe == ' ' || *probe == '\n' || *probe == '\r' ||
                                       *probe == '\t')) ++probe;
        }

        std::vector<std::vector<Vec2>> cand;
        std::vector<Vec2> ring;
        if (depth >= 4) {
            gc_.expect('[');
            do {
                gc_.expect('[');
                do { read_ring(gc_, ring); if (ring.size() >= 3) cand.push_back(ring); } while (gc_.eat(','));
                gc_.expect(']');
            } while (gc_.eat(','));
            gc_.expect(']');
        } else {
            gc_.expect('[');
            do { read_ring(gc_, ring); if (ring.size() >= 3) cand.push_back(ring); } while (gc_.eat(','));
            gc_.expect(']');
        }
        if (!cand.empty()) {
            size_t best = 0;
            double ba = -1;
            for (size_t i = 0; i < cand.size(); ++i) {
                double a = ring_area2(cand[i]);
                if (a > ba) { ba = a; best = i; }
            }
            rings.push_back(cand[best]);
            ids.push_back(id);
        }
        c.p = gc_.p;
    }

    build_from_rings(rings, ids);
}

}  // namespace gc
