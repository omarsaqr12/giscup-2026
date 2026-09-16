# GIS Cup 2026 · Boundary-constrained antenna placement

A C++17 solver for the [ACM SIGSPATIAL 2026 GIS Cup](https://sigspatial2026.sigspatial.org/giscup.html). Given two-dimensional building footprints, a coverage threshold `τ`, and an antenna budget `k`, it places antennas **on building boundaries** to maximize the number of buildings with at least `τ` of their perimeter visible. A line touching a boundary without entering a building's interior is not blocked.

**What is implemented:** a dependency-free GeoJSON loader; a rotational visibility sweep that emits boundary arc intervals; an indexed candidate/contribution pipeline; multiple selection and local-search strategies; an uncapped geometric recheck before writing claimed building IDs; and independent regression/cross-check utilities. This is a competition research solver, **not** a wireless propagation or deployment simulator.

**Where to start:** [`src/main.cpp`](src/main.cpp) (CLI and end-to-end pipeline) → [`src/visibility.hpp`](src/visibility.hpp) (visibility geometry) → [`src/coverage.hpp`](src/coverage.hpp) (interval union and coverage) → [`src/solvers.hpp`](src/solvers.hpp) (optimization). See the [experiment log](FINDINGS.md) for measurements **including failed approaches** and the [run-day plan](plan.md) for historical tuning decisions.

## Build and run

Requires a C++17 compiler with OpenMP, `make`, and Python 3 for submission-format validation. No third-party C++ libraries are required for the core solver.

```bash
make portable                       # portable C++ build; produces ./giscup
make check                          # figure oracle + independent geometry cross-check
./giscup solve --data data/GIS-cup-sample-dataset.geojson \
  --tau 0.25,0.5,0.75 --k 50,500,1000 \
  --radius 600 --lns-sec 150 --swap 400 \
  --verify-radius 2500 --out submission.txt
make check-format SUBMISSION=submission.txt BLOCKS=9
```

The solve command above is a **substantial optimization run**, not a quick smoke test. `--lns-sec` is a per-subproblem search budget. For a small, deliberately noncompetitive trial, use `--tau 0.5 --k 3 --lns-sec 0 --no-auto --power 1 --out trial.txt`; validate it with `make check-format SUBMISSION=trial.txt BLOCKS=1`. The quick trial tests the command path, not solution quality. `make` (without `portable`) uses `-march=native`, so binaries from that build may not run on other machines.

`make check` runs **two** checks: `test_figures` against the organizers' illustrated coverage examples, and `giscup crosscheck` against an independent brute-force visibility sampler. They test geometry, not optimality. The heavier `make check-robustness` separately runs 14 transformed-input solve/verify cases. `make check-format SUBMISSION=...` requires an actual nonempty result file, runs the local parser-conformance checker, and fails on malformed or missing input; it does **not** substitute for the organizers' full ArcGIS evaluator. Neither submission-format validation nor the robustness suite is silently counted as part of `make check`.

Other CLI subcommands include `bench`, `crosscheck`, `verify`, `exact` (small `k≤3` instances), `dump`, and `archive`. Search may use a distance cutoff (`--radius`), whereas the final claims are recomputed with `--verify-radius` (a positive cutoff must itself be justified for the dataset; the default nonpositive value is uncapped). Recheck and compare against the [official evaluator](https://github.com/alowe/gis-cup-2026-evaluator) before treating any score as externally verified.

## How the solution works

1. [`src/geojson.hpp`](src/geojson.hpp) and [`src/scene.hpp`](src/scene.hpp) load and index building edges; [`src/pipeline.hpp`](src/pipeline.hpp) creates candidate sites and their coverage contributions.
2. [`src/visibility.hpp`](src/visibility.hpp) sweeps edge events around each antenna and emits visible perimeter intervals; [`src/coverage.cpp`](src/coverage.cpp) combines intervals over antennas.
3. [`src/solvers.hpp`](src/solvers.hpp) selects sites, then optional neighborhood search refines the selection for each `(τ,k)`.
4. [`src/main.cpp`](src/main.cpp) recomputes which buildings are serviced before emitting the competition's three-line-per-case submission format.

The full [research record](FINDINGS.md) explains search-radius trade-offs, objective variants, small-instance exact checks, and failures. The source tree also contains [`src/bruteforce.hpp`](src/bruteforce.hpp), which supplies a separate approximate geometric reference for randomized cross-checking. A brute-force sampling agreement is **not** proof of exact correctness on every possible polygon.

## Evidence and boundaries

The baseline commit records a **post-submission** run through the organizers' evaluator reporting agreement on 42,505 claims across nine blocks. This is a historical report in the [repository history](https://github.com/omarsaqr12/giscup-2026/commit/50e3fa4e5d0163a58a611e23041c0d8aacf6a440), **not a result reproduced by this README** or proof of an optimal placement. Inspect [`results/`](results/) and [`FINDINGS.md`](FINDINGS.md) for recorded runs and their provenance. The tracked [`archive/index.tsv`](archive/index.tsv) is an index; its referenced placement files are not present in this checkout, so a fresh clone cannot necessarily export a complete historical best-of archive. Do not treat that archive as a reproducible result bundle.

The competition specified simple planar polygons and a 0.001 m evaluator tolerance. Geometry code contains dataset-specific numeric tolerances and heuristics; cross-checks and figure oracles do not establish correctness for arbitrary invalid polygons, holes, overlapping buildings, or all degeneracies. Some historical notes describe an earlier sample-dataset anomaly; consult the [competition update](https://sigspatial2026.sigspatial.org/giscup.html) before comparing results across sample versions. No placement, leaderboard rank, award, or team contribution beyond what the committed evidence documents is asserted here.

## Repository map

| Path | Purpose |
| --- | --- |
| [`src/`](src/) | Geometry, visibility, scoring, optimizer, CLI and archived-placement support |
| [`tests/`](tests/) | Figure oracle, brute-force-linked checks, parser conformance and input robustness |
| [`tools/`](tools/) | Dataset inspection, parameter handling, exploratory optimization and historical packaging |
| [`data/`](data/) | Sample GeoJSON, small instances and organizers' figure assets |
| [`results/`](results/) | Recorded benchmark and run-day artifacts; not a substitute for rerunning experiments |
| [`FINDINGS.md`](FINDINGS.md), [`plan.md`](plan.md) | Experimental record and historical execution plan |

**Project description for GitHub:** `C++17 GIS Cup antenna-placement solver with rotational visibility sweeps, local search, and independent geometry checks.`
