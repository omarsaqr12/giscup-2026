// Persistent archive of verified placements, keyed by (tau, k).
//
// Every placement this project has ever produced is recorded here together with
// its *independently verified* score, the method that produced it, and a
// timestamp. Two properties follow, and both matter more than they look:
//
//   Ratchet. The submission is assembled from the archive best, never from
//   whatever the last pipeline run happened to emit. An experiment that turns
//   out worse than the incumbent therefore cannot damage the submission -- so
//   every remaining experiment is free to fail.
//
//   No silent regressions. FINDINGS.md 5.12 records an integration that
//   overwrote a tuned configuration and shipped a 7% regression from something
//   meant to be a pure maximum. It was caught only because a previous score had
//   been written down. Here that bookkeeping is structural rather than a habit.
//
// The index is append-only: it is a history, not a cache. Claims are never
// stored -- they are re-derived by an exact uncapped sweep at export time, so a
// submission can never carry a claim that was not verified against the geometry
// it ships with.
#pragma once
#include "scene.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <algorithm>

namespace gc {

struct ArchiveEntry {
    double tau = 0;
    int k = 0;
    int score = 0;
    std::string method;
    std::string stamp;
    std::string file;    // placement file, relative to the archive directory
};

class Archive {
public:
    explicit Archive(std::string dir) : dir_(std::move(dir)) {}

    const std::string& dir() const { return dir_; }
    std::string index_path() const { return dir_ + "/index.tsv"; }

    bool load() {
        entries_.clear();
        std::ifstream in(index_path());
        if (!in) return false;
        std::string line;
        std::getline(in, line);  // header
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            ArchiveEntry e;
            std::string tau_s, k_s, sc_s;
            if (!std::getline(ss, tau_s, '\t')) continue;
            if (!std::getline(ss, k_s, '\t')) continue;
            if (!std::getline(ss, sc_s, '\t')) continue;
            if (!std::getline(ss, e.method, '\t')) continue;
            if (!std::getline(ss, e.stamp, '\t')) continue;
            if (!std::getline(ss, e.file, '\t')) continue;
            e.tau = std::atof(tau_s.c_str());
            e.k = std::atoi(k_s.c_str());
            e.score = std::atoi(sc_s.c_str());
            entries_.push_back(e);
        }
        return true;
    }

    const std::vector<ArchiveEntry>& entries() const { return entries_; }

    // Best verified entry for a sub-problem, or nullptr.
    const ArchiveEntry* best(double tau, int k) const {
        const ArchiveEntry* b = nullptr;
        for (const auto& e : entries_) {
            if (e.k != k || std::fabs(e.tau - tau) > 1e-12) continue;
            if (!b || e.score > b->score) b = &e;
        }
        return b;
    }

    // Append a verified placement. The caller is responsible for having
    // verified `score` against the scene -- nothing reaches the index unchecked.
    bool add(double tau, int k, int score, const std::string& method,
             const std::vector<Vec2>& ants, std::string* out_file = nullptr) {
        ensure_dir();
        char stamp[32];
        std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", std::localtime(&t));

        char name[256];
        std::snprintf(name, sizeof name, "placements/tau%g_k%d_s%d_%ld.txt", tau, k, score,
                      (long)t);
        std::string rel(name);
        std::string full = dir_ + "/" + rel;
        // Collisions only happen when two placements for the same sub-problem
        // land in the same second with the same score; disambiguate rather than
        // silently overwrite a distinct placement.
        int salt = 0;
        while (std::ifstream(full)) {
            std::snprintf(name, sizeof name, "placements/tau%g_k%d_s%d_%ld_%d.txt", tau, k,
                          score, (long)t, ++salt);
            rel = name;
            full = dir_ + "/" + rel;
        }

        FILE* f = std::fopen(full.c_str(), "w");
        if (!f) { std::fprintf(stderr, "archive: cannot write %s\n", full.c_str()); return false; }
        for (const Vec2& p : ants) std::fprintf(f, "%.17g %.17g\n", p.x, p.y);
        std::fclose(f);

        bool fresh = !std::ifstream(index_path());
        FILE* ix = std::fopen(index_path().c_str(), "a");
        if (!ix) { std::fprintf(stderr, "archive: cannot append index\n"); return false; }
        if (fresh) std::fprintf(ix, "tau\tk\tscore\tmethod\ttimestamp\tfile\n");
        std::fprintf(ix, "%g\t%d\t%d\t%s\t%s\t%s\n", tau, k, score, method.c_str(), stamp,
                     rel.c_str());
        std::fclose(ix);

        ArchiveEntry e{tau, k, score, method, stamp, rel};
        entries_.push_back(e);
        if (out_file) *out_file = rel;
        return true;
    }

    std::vector<Vec2> read_placement(const ArchiveEntry& e) const {
        std::vector<Vec2> out;
        std::ifstream in(dir_ + "/" + e.file);
        double x, y;
        while (in >> x >> y) out.push_back({x, y});
        return out;
    }

private:
    void ensure_dir() const {
        std::string cmd = "mkdir -p '" + dir_ + "/placements'";
        if (std::system(cmd.c_str()) != 0)
            std::fprintf(stderr, "archive: mkdir failed for %s\n", dir_.c_str());
    }

    std::string dir_;
    std::vector<ArchiveEntry> entries_;
};

}  // namespace gc
