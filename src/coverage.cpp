#include "coverage.hpp"
#ifdef _OPENMP
#include <omp.h>
#endif

namespace gc {

std::vector<double> Evaluator::coverage(const std::vector<Vec2>& antennas, double radius) const {
    size_t nb = sc_.buildings.size();
    std::vector<ArcSet> arcs(nb);

    // Antennas are few (k <= a few thousand) but each uncapped sweep is heavy,
    // so parallelise over antennas and merge the per-thread arc sets after.
    int nthreads = 1;
#ifdef _OPENMP
    nthreads = omp_get_max_threads();
#endif
    std::vector<std::vector<ArcInterval>> per_thread(nthreads);

#pragma omp parallel
    {
        int tid = 0;
#ifdef _OPENMP
        tid = omp_get_thread_num();
#endif
        VisScratch sr;
        std::vector<ArcInterval> local, mine;
#pragma omp for schedule(dynamic, 1) nowait
        for (long long i = 0; i < (long long)antennas.size(); ++i) {
            vis_.visible_arcs(antennas[i], radius, sr, local);
            mine.insert(mine.end(), local.begin(), local.end());
        }
        per_thread[tid] = std::move(mine);
    }

    for (const auto& v : per_thread)
        for (const ArcInterval& a : v)
            arcs[a.building].add(a.s0, a.s1);

    std::vector<double> cov(nb, 0.0);
    for (size_t b = 0; b < nb; ++b) {
        double P = sc_.buildings[b].perimeter;
        cov[b] = P > 0 ? std::min(1.0, arcs[b].measure / P) : 0.0;
    }
    return cov;
}

}  // namespace gc
