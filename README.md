# SIGSPATIAL 2026 GIS Cup — antenna placement

Place `k` antennas on building boundaries to maximise the number of buildings
whose perimeter is at least a fraction `τ` visible from at least one antenna.

See [plan.md](plan.md) for the approach, the geometry that drives it, and the
measured results.

## Build

```bash
make            # builds ./giscup and ./test_figures
make portable   # same, without -march=native (use for the submitted source)
```

Requires a C++17 compiler with OpenMP. No third-party libraries.

## Verify

```bash
make check
```

Runs two independent checks:

1. **`test_figures`** — reproduces all 30 per-building coverage percentages and
   both service scores that the organizers published in Figures 4 and 5 of the
   problem statement. This pins the engine to the organizers' own reading of
   Definitions 1–4.
2. **`giscup crosscheck`** — compares the rotational-sweep visibility engine
   against an independent brute-force sampler on random neighbourhoods of the
   real dataset.
3. **`bash tests/robustness.sh`** — mutates the sample into 14 shapes the real
   dataset might arrive in (id under other property names or absent,
   MultiPolygon, reversed rings, unclosed rings, XYZ coordinates, no CRS,
   shifted origin, duplicate vertices, k exceeding the candidate count) and
   checks each still solves and verifies identically.
4. **`giscup exact`** — exhaustive optimum for k≤3, for measuring the true
   optimality gap on tiny instances.

## Documents

| file | contents |
|---|---|
| [`FINDINGS.md`](FINDINGS.md) | experiment log: the problem, every approach tried, and its measured result — including the ones that failed |
| [`plan.md`](plan.md) | approach and run-day playbook |

## Run

```bash
./giscup solve --data data/GIS-cup-sample-dataset.geojson \
               --tau 0.25,0.5,0.75 --k 50,500,1000 \
               --radius 600 --lns-sec 150 --swap 400 \
               --verify-radius 2500 --out submission.txt
```

Writes the 9-block submission file. Useful flags:

| flag | meaning |
|---|---|
| `--radius R` | visibility cutoff during search, metres (600 is the measured sweet spot) |
| `--powers a,b,c` | convexity exponents to auto-tune over, per sub-problem |
| `--no-auto` | disable auto-tuning; use a single `--power` |
| `--lns-sec S` | seconds of large-neighbourhood polish per sub-problem |
| `--swap N` | 2-exchange local search with an N-candidate shortlist (400 works well) |
| `--swap-passes N` | cap on 2-exchange passes per polish call (default 200) |
| `--lns-destroy` | randomised-destroy LNS: ruin ρ∈{5,10,15}% of the incumbent (uniform / spatial-cluster) and rebuild; the coupled multi-antenna move 2-exchange cannot make. Opt-in, default off (see FINDINGS §5.23) |
| `--swap-plateau N` | accept ≤N score-equal 2-exchange swaps that raise the truncated secondary measure between strict gains, to cross objective plateaus. Opt-in, default off |
| `--verify-radius R` | cap the verification sweep; 2500 is exact here and 2x faster |
| `--restarts N` | GRASP restarts (helps only on tiny instances; default off) |
| `--edge-spacing D` | also place candidate sites every D metres along edges |
| `--algo NAME` | `selfcover`, `bundle`, `truncated`, `potential`, `potential+lns` |

Other subcommands: `bench` (compare algorithms across all nine sub-problems),
`crosscheck` (brute-force validation), `verify` (re-score a finished submission;
`--rewrite` repairs its claim lines), `exact` (exhaustive optimum, k<=3),
`dump` (contribution map, for `tools/lp_bound.py`).

## Layout

```
src/geom.hpp        exact orientation predicate, ray/segment primitives
src/scene.hpp       footprints, arc-length parameterisation, uniform grid index
src/geojson.hpp     dependency-free GeoJSON reader
src/visibility.hpp  rotational plane sweep -> exact visible arcs
src/coverage.*      arc-interval union, exact evaluator
src/pipeline.hpp    candidate sites, contribution map, inverse index
src/solvers.hpp     selection algorithms
src/bruteforce.hpp  independent reference implementation
tests/              figure regression oracle
tools/              dataset scaling, figure extraction
```
