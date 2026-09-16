# GIS Cup 2026 | Geometric antenna placement

**C++17 · computational geometry · combinatorial optimization · verification**

A solver for the [ACM SIGSPATIAL 2026 GIS Cup](https://sigspatial2026.sigspatial.org/giscup.html). Given building footprints, an antenna budget **k**, and a visibility threshold **τ**, it chooses sites **on building boundaries** to maximize the number of buildings with at least τ of their perimeter visible. Boundary tangencies do not obstruct a line of sight. This is a *planar visibility and placement* problem, not a radio-propagation simulator.

**Engineering highlights**

- **Geometry:** a rotational sweep computes visible building-boundary intervals; an independent brute-force sampler and the organizers' illustrated examples provide checks.
- **Optimization:** a spatial index and precomputed candidate contributions support selection and local-search approaches across the nine `(τ, k)` cases.
- **Verification:** placement claims are recomputed before submission; a separate format gate rejects missing or malformed submissions rather than silently skipping the check.

**Recorded evidence:** the [baseline commit](https://github.com/omarsaqr12/giscup-2026/commit/50e3fa4e5d0163a58a611e23041c0d8aacf6a440) reports a *post-submission* comparison against the organizers' evaluator with agreement on **42,505 claims across nine blocks**. That is a historical report, not a fresh reproduction, an optimum proof, or a competition ranking. See the [experiment log](FINDINGS.md) for the tested approaches, including negative results.

## Start here: build, inspect, verify

A C++17 compiler with OpenMP, `make`, and Python 3 are required; the core solver has no third-party C++ dependencies. Run from the repository root:

```bash
make portable test_figures
./test_figures tests/figures_groundtruth.txt
bash tests/test_format_gate.sh
```

For a small **noncompetitive** solve, rather than the full nine-case optimization:

```bash
./giscup solve --data data/tiny/tiny40.geojson \
  --tau 0.5 --k 3 --lns-sec 0 --no-auto --power 1 --out trial.txt
make check-format SUBMISSION=trial.txt BLOCKS=1
```

The tiny-instance command demonstrates the pipeline; it does **not** validate solution quality or the official grader. A full sample run uses `data/GIS-cup-sample-dataset.geojson` and can take substantially longer. See [`plan.md`](plan.md) for historical run configurations; do not treat them as a lightweight quickstart.

## Architecture

| Stage | Implementation | What it does |
| --- | --- | --- |
| Load and index | [`geojson.hpp`](src/geojson.hpp), [`scene.hpp`](src/scene.hpp) | Parse GeoJSON, normalize footprint rings, index edges |
| Generate and measure | [`pipeline.hpp`](src/pipeline.hpp), [`visibility.hpp`](src/visibility.hpp) | Generate boundary candidates and sweep visible perimeter arcs |
| Aggregate and search | [`coverage.cpp`](src/coverage.cpp), [`solvers.hpp`](src/solvers.hpp) | Union visible intervals, build contribution maps, select placements |
| Verify and export | [`main.cpp`](src/main.cpp), [`archive.hpp`](src/archive.hpp) | Re-evaluate claims and write the competition submission format |

The CLI entry point is [`src/main.cpp`](src/main.cpp). [`src/bruteforce.hpp`](src/bruteforce.hpp) supplies an independent, sampled geometry reference; it is *not* proof of correctness on arbitrary polygon inputs.

## Verification and what it establishes

| Command | Scope |
| --- | --- |
| `make check` | Figure-oracle regression and randomized visibility cross-check on sample data; not an optimality guarantee |
| `bash tests/test_format_gate.sh` | Missing, valid, and malformed submission-format cases; no C++ solve required |
| `make check-format SUBMISSION=trial.txt BLOCKS=1` | Requires an actual nonempty submission and runs the **local** format parser; not the full official evaluator |
| `make check-robustness` | 14 transformed-input solve/verify cases; opt-in and more expensive |
| `./giscup exact ...` | Exhaustive placement search on *small* instances with k≤3; not a large-instance optimum |

The `make check` target does **not** implicitly run format validation or robustness. A positive `--verify-radius` truncates the final sweep, so its sufficiency is dataset-dependent; the default nonpositive radius is uncapped. The [organizers' evaluator](https://github.com/alowe/gis-cup-2026-evaluator) is a separate external check. A basic GitHub Actions workflow runs the portable build, figure fixtures, and format regression on proposed changes; it does not run the full optimizer or external evaluator.

## Repository guide and limitations

- [`FINDINGS.md`](FINDINGS.md): measured experiments, comparisons, and failed approaches; [`results/`](results/): retained run artifacts.
- [`plan.md`](plan.md), [`PC2_WORK_ORDER.md`](PC2_WORK_ORDER.md), [`RUNDAY_OTHERPC.md`](RUNDAY_OTHERPC.md): historical execution notes, not current setup requirements.
- [`tests/`](tests/): geometric fixtures, input robustness, and submission-format checks; [`tools/`](tools/): analysis and run-day utilities.
- [`data/`](data/): sample/tiny GeoJSON and figure assets. The [`archive/index.tsv`](archive/index.tsv) is tracked, but its referenced placement files are **not** in this checkout; a fresh clone cannot necessarily reconstruct the historical best-of archive.

The implementation is tailored to the competition's planar-footprint assumptions. It does not establish correctness for arbitrary invalid polygons, holes, overlapping footprints, or every floating-point degeneracy. The historical 42,505-claim report was not reproduced for this documentation update. No leaderboard placement or individual-versus-team contribution is asserted without supporting evidence.
