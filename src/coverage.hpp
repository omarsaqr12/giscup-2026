// Coverage accounting: union of visible arcs per building, and the service score.
#pragma once
#include "visibility.hpp"
#include <vector>
#include <map>

namespace gc {

// Disjoint arc intervals accumulated for one building, kept sorted by start.
struct ArcSet {
    std::vector<std::pair<double, double>> iv;
    double measure = 0;

    // Adds [a,b); returns the newly covered length.
    double add(double a, double b) {
        if (b <= a) return 0;
        size_t i = 0;
        while (i < iv.size() && iv[i].second < a) ++i;
        if (i == iv.size() || iv[i].first > b) {
            iv.insert(iv.begin() + i, {a, b});
            measure += b - a;
            return b - a;
        }
        double old = 0;
        size_t j = i;
        double lo = std::min(a, iv[i].first), hi = b;
        while (j < iv.size() && iv[j].first <= b) {
            old += iv[j].second - iv[j].first;
            hi = std::max(hi, iv[j].second);
            ++j;
        }
        iv.erase(iv.begin() + i, iv.begin() + j);
        iv.insert(iv.begin() + i, {lo, hi});
        double gained = (hi - lo) - old;
        measure += gained;
        return gained;
    }

    // Length of [a,b) not already covered, without mutating.
    double probe(double a, double b) const {
        if (b <= a) return 0;
        double covered = 0;
        for (const auto& s : iv) {
            if (s.second <= a) continue;
            if (s.first >= b) break;
            covered += std::min(b, s.second) - std::max(a, s.first);
        }
        return (b - a) - covered;
    }

    void clear() { iv.clear(); measure = 0; }
};

// Exact per-building coverage for a given antenna set, computed with the
// uncapped sweep. This is the authoritative evaluator: it is what decides which
// buildings we are willing to claim in the submission.
class Evaluator {
public:
    Evaluator(const Scene& sc, const Visibility& vis) : sc_(sc), vis_(vis) {}

    // Returns coverage ratio per building.
    std::vector<double> coverage(const std::vector<Vec2>& antennas, double radius) const;

    // Raw visible boundary length per building, in metres.
    //
    // The official evaluator's verdict is
    //     visibleLengthMeters >= tau * perimeterMeters
    // -- a *length* comparison. Computing coverage as a ratio and comparing that
    // to tau is the same thing in exact arithmetic and NOT the same thing in
    // doubles: the two can disagree in the last ulp. Since the grader scores
    // coverage only for buildings we actually claim, disagreeing in the
    // conservative direction still loses a point. So the verdict is taken in the
    // grader's operand form, from these lengths.
    std::vector<double> visible_lengths(const std::vector<Vec2>& antennas, double radius) const;

    static int service_score(const std::vector<double>& cov, double tau) {
        int n = 0;
        for (double c : cov) if (c >= tau) ++n;
        return n;
    }

private:
    const Scene& sc_;
    const Visibility& vis_;
};

}  // namespace gc
