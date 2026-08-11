// Basic planar geometry primitives, all in IEEE-754 doubles.
//
// The competition requires results to hold at 64-bit precision, so nothing here
// ever narrows to float, and the orientation predicate has an exact fallback
// (Shewchuk-style adaptive expansion) for the near-degenerate cases that decide
// whether a grazing line "blocks" or not.
#pragma once
#include <cmath>
#include <algorithm>

namespace gc {

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double norm2(const Vec2& a) { return dot(a, a); }
inline double norm(const Vec2& a) { return std::sqrt(dot(a, a)); }
inline double dist(const Vec2& a, const Vec2& b) { return norm(b - a); }

// ---------------------------------------------------------------------------
// Exact orientation predicate.
//
// orient2d(a,b,c) > 0  <=>  c is strictly left of the directed line a->b.
// The floating-point filter handles the overwhelming majority of calls; when the
// error bound cannot certify the sign we fall back to an exact two-product /
// two-sum expansion. Getting this right matters here because Definition 1 turns
// on whether a segment touches a boundary (allowed) or crosses the interior
// (blocks) -- a sign flip silently changes a building's coverage.
// ---------------------------------------------------------------------------

// Dekker/Knuth error-free transformations.
inline void two_sum(double a, double b, double& s, double& err) {
    s = a + b;
    double bv = s - a;
    err = (a - (s - bv)) + (b - bv);
}
inline void two_diff(double a, double b, double& s, double& err) {
    s = a - b;
    double bv = a - s;
    err = (a - (s + bv)) + (bv - b);
}
inline void two_product(double a, double b, double& p, double& err) {
    p = a * b;
    err = std::fma(a, b, -p);  // exact residual; hardware FMA
}

// Exact sign of (ax-cx)*(by-cy) - (ay-cy)*(bx-cx) using a 4-term expansion.
inline double orient2d_exact(const Vec2& a, const Vec2& b, const Vec2& c) {
    double acx, acxe, acy, acye, bcx, bcxe, bcy, bcye;
    two_diff(a.x, c.x, acx, acxe);
    two_diff(a.y, c.y, acy, acye);
    two_diff(b.x, c.x, bcx, bcxe);
    two_diff(b.y, c.y, bcy, bcye);
    double p1, e1, p2, e2;
    two_product(acx, bcy, p1, e1);
    two_product(acy, bcx, p2, e2);
    // Sum p1 - p2 plus all correction terms in increasing magnitude order.
    double corr = (e1 - e2) + (acxe * bcy + acx * bcye) - (acye * bcx + acy * bcxe);
    double s, err;
    two_diff(p1, p2, s, err);
    return s + (err + corr);
}

inline double orient2d(const Vec2& a, const Vec2& b, const Vec2& c) {
    double detleft = (a.x - c.x) * (b.y - c.y);
    double detright = (a.y - c.y) * (b.x - c.x);
    double det = detleft - detright;
    double detsum = std::fabs(detleft) + std::fabs(detright);
    // 3.33e-16 ~= (3 + 16*eps) * eps, the standard static filter bound.
    if (std::fabs(det) >= 3.3306690738754716e-16 * detsum) return det;
    return orient2d_exact(a, b, c);
}

inline int orient_sign(const Vec2& a, const Vec2& b, const Vec2& c) {
    double d = orient2d(a, b, c);
    return (d > 0) - (d < 0);
}

// ---------------------------------------------------------------------------
// Ray / segment helpers used by the visibility sweep.
// ---------------------------------------------------------------------------

// Parametric position along segment [a,b] where the ray p + t*dir hits it.
// Returns false when the ray is parallel to the segment or misses it.
inline bool ray_segment_param(const Vec2& p, const Vec2& dir, const Vec2& a,
                              const Vec2& b, double& t_seg, double& t_ray) {
    Vec2 ab = b - a;
    double den = cross(dir, ab);
    if (den == 0.0) return false;
    Vec2 ap = a - p;
    // Solve p + t_ray*dir == a + t_seg*ab.
    t_seg = cross(ap, dir) / den;   // position along [a,b], 0..1
    t_ray = cross(ap, ab) / den;    // distance multiplier along dir
    return true;
}

// Squared distance from p to segment [a,b].
inline double point_seg_dist2(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a, ap = p - a;
    double L2 = norm2(ab);
    if (L2 == 0.0) return norm2(ap);
    double t = dot(ap, ab) / L2;
    t = std::max(0.0, std::min(1.0, t));
    return norm2(ap - ab * t);
}

}  // namespace gc
