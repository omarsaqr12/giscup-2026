# Task 0-B -- conformance with the official evaluator

Evaluator pinned: github.com/alowe/gis-cup-2026-evaluator @ 9203c0d (tag v0.1.0)
  evaluator version 0.1.0 | @arcgis/core 5.1.0 | rbush 4.0.1
  SPATIAL_TOLERANCE_METERS = 0.001 | DISTANCE_TIE_TOLERANCE_METERS = 1e-9
  verdict: visibleLengthMeters >= tau * perimeterMeters   (LENGTH form, exact >=)
  NOTE: pnpm unavailable here, so dependencies resolved via npm rather than
  --frozen-lockfile. The evaluator's own suite (11 files, 73 tests) passes, so
  the resolution is sound, but it is not the pinned lockfile.

## Harness self-check (official fixture)
  GIS-cup-sample-submission-50-antennas.txt -> claimed 140, VERIFIED 140, 0 warnings
  matches the value asserted in the repo's own benchmark.

## Differential: our archive-best submission through the official grader

  block  tau     k     claimed  VERIFIED  flips   grader time
  1      0.25    50      2,426     2,426      0        23 s
  2      0.25   500     10,581    10,581      0       176 s
  3      0.25  1000     12,809    12,809      0       337 s
  4      0.50    50        868       868      0        13 s
  5      0.50   500      6,148     6,148      0       180 s
  6      0.50  1000     10,161    10,161      0       333 s
  7      0.75    50        393       393      0        12 s
  8      0.75   500      2,924     2,924      0       149 s
  9      0.75  1000      5,534     5,534      0       313 s
  TOTAL                 51,844    51,844      0      1,537 s (25.6 min)

Zero verdict flips across 51,844 claims. Our exact-arc engine and the
organizers' ArcGIS-backed radial sweep agree on every one.

## Run-day consequence

The official filter costs ~26 min for all nine blocks at sample scale, scaling
with k (k=50 ~15s, k=500 ~170s, k=1000 ~330s). Affordable as the FINAL claim
filter before packaging; far too slow for any inner loop. Policy: our engine
drives the search, the official harness is the last gate before upload.

## The defect this task existed to find

Our writer emitted the parameter line as "0.25,50". The official parser matches
/^\(\s*([^,]*)\s*,\s*([^,]*)\s*\)$/ -- anchored, parentheses required -- so the
bare form yields INVALID_TAU + INVALID_K, action "Score this subproblem as
zero". All nine blocks would have scored zero with every internal gate green,
because every internal gate used our own reader. Fixed; tests/conformance.py
now transcribes the grader's acceptance rules into make check.
