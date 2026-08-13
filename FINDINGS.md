# SIGSPATIAL 2026 GIS Cup — experiment log

A record of everything tried on the antenna-placement problem, with measured
results, including the approaches that did not work. Every number here was
produced by a command in this repository; nothing is estimated.

Companion documents: [`plan.md`](plan.md) is the approach and run-day playbook;
[`README.md`](README.md) is build and usage.

---

## 1. The problem

**Competition page:** <https://sigspatial2026.sigspatial.org/giscup.html>
**Sample dataset:** <https://sigspatial2026.sigspatial.org/img/GIS-cup-sample-dataset.geojson>
(12,860 building footprints, EPSG:32611, mirrored in [`data/`](data/))

**Worked examples** — the SVG sources encode exact polygons, exact antenna
positions and the organizers' own per-building coverage percentages, which is
what makes them usable as a test oracle (§4.1):

| figure | source |
|---|---|
| Fig. 1 — visibility / tangency | [`LOS.svg`](https://sigspatial2026.sigspatial.org/img/LOS.svg) |
| Fig. 2 — segment visibility | [`segment-vis.svg`](https://sigspatial2026.sigspatial.org/img/segment-vis.svg) |
| Fig. 3 — building coverage | [`visibility.svg`](https://sigspatial2026.sigspatial.org/img/visibility.svg) |
| Fig. 4 — k=3, τ=0.5, score 8 | [`los-scene-composite.svg`](https://sigspatial2026.sigspatial.org/img/los-scene-composite.svg) |
| Fig. 5 — k=3, τ=0.6, score 4 | [`los-scene-composite-2.svg`](https://sigspatial2026.sigspatial.org/img/los-scene-composite-2.svg) |

**Organizers:** Aaron Lowe (`alowe`) and Ashwin Shashidharan (`ashashidharan`),
both at esri.com.

### Definitions, as given

- **D1 (visibility)** — `p` sees `q` iff segment `pq` does not intersect the
  **interior** of any building. Tangency and vertex-grazing do **not** block.
- **D2 (segment visibility)** — a segment is visible from a point set if every
  point on it is visible from at least one point of the set.
- **D3 (coverage)** — `C(b)` = (visible boundary length) / (perimeter of `b`).
- **D4 (service score)** — `#{b : C(b) ≥ τ}`.
- **D5 (the task)** — place `k` points **on building boundaries** maximising D4.

### Scoring, and what it implies

Per sub-problem: `your score / best score across all submissions`, summed over
9 sub-problems (3 τ × 3 k). Two consequences drove most decisions here:

1. The target is not "good", it is "within a whisker of the best submission **on
   every one of the nine**". Being excellent on eight and mediocre on one costs
   about as much as being mediocre on two.
2. Because each sub-problem is scored independently, **per-sub-problem tuning is
   free real estate** — there is no penalty for using a different configuration
   for each, and §5.5/§5.6 show the best configuration genuinely differs.

### Dates

| | |
|---|---|
| Sample dataset | 31 Mar 2026 |
| Evaluation dataset published | 15 Aug 2026 |
| Submission deadline | 16 Aug 2026 (24-hour window) |
| Results & paper invitations | 15 Sep 2026 |

---

## 2. Dataset facts (measured)

| property | value |
|---|---|
| buildings | 12,860 |
| vertices / edges | 78,727 |
| CRS | EPSG:32611 (UTM 11N, metres — planar, no geodesic maths) |
| extent | 4,946 m × 4,264 m |
| density | 610 buildings / km² |
| perimeter | median 62.1 m, mean 66.8 m, max 1,066 m |
| vertices per building | median 6, max 38; **46% are quadrilaterals** |
| id property | `"id"`, integer, 1…12,860, unique |

Three quirks that would each have caused a silent failure:

1. **All exterior rings are clockwise** — the opposite of the GeoJSON
   convention. The visibility sweep needs consistent orientation to know which
   directions point *into* the wall an antenna is mounted on, so rings are
   normalised to counter-clockwise at load.
2. **Feature id 9448 has a second ring** (a small hole) despite the spec
   promising none. The loader takes the largest-area ring rather than crashing.
3. **No two buildings share a vertex**, and no three consecutive vertices are
   exactly collinear — both were assumptions the sweep would otherwise have
   relied on silently. Now checked rather than assumed.

---

## 3. The geometry that decides the problem

Two consequences of D1 dominate everything else. Both were confirmed against the
organizers' published figures *before* anything was built on them.

### 3.1 A vertex antenna covers both incident edges, free

The segment from a point on edge `e` to any other point on `e` runs **along the
boundary**, never entering an interior — so by D1 it is visible. An antenna at a
**vertex** lies on two edges at once and therefore covers both entirely, at zero
marginal cost.

On this dataset one vertex antenna delivers a median **~40%** of its host
building's perimeter immediately. Two antennas at opposite corners of a
quadrilateral cover **100%** of it.

Minimum antennas to service a building using only its own vertices, with no help
from any neighbour:

| τ | 1 antenna | 2 | 3 | 4+ |
|---|---|---|---|---|
| 0.25 | **98.5%** | 1.5% | 0.05% | — |
| 0.50 | 59.3% | 38.3% | 2.0% | 0.4% |
| 0.75 | — | **74.8%** | 20.0% | 5.2% |

So even ignoring cross-building visibility entirely, a placement services ≈ k
buildings at τ=0.25 and ≈ k/2 at τ=0.75. That is the floor any serious entry
clears, and it is why candidate sites are polygon **vertices** rather than points
sampled along edges (§5.2).

A second, independent reason to prefer vertices: **a point in the interior of an
edge is essentially never exactly on that edge in doubles.** The organizers' own
Figure 5 antennas miss exact collinearity by ~1e-13 (measured with exact rational
arithmetic). Vertex coordinates are bit-identical to the input, so "the antenna
lies on a boundary" is exactly true rather than true-to-tolerance.

### 3.2 A building occludes its own boundary

From a point on building `B`, directions pointing into `B`'s interior see
nothing — the connecting segment would cross `B`'s own interior. For a **convex**
building an antenna on its wall therefore sees *only* the edges it physically
lies on; the rest of its own perimeter is dark. Concave buildings can see around
their own notches, because the connecting segment passes through exterior space.

This is the single easiest thing to get backwards. The first draft of the plan
proposed excluding the host building from occlusion checks, which inflates
coverage by tens of percent per building and would have produced a submission
full of buildings we cannot actually serve.

---

## 4. Validation

Three independent gates, all run by `make check` plus `tests/robustness.sh`.

### 4.1 The organizers' own numbers

Figures 4 and 5 are SVGs encoding the exact polygons, exact antenna positions,
and the coverage percentage the organizers computed for **each of 15 buildings**
in each figure. `tests/extract_figures.py` pulls them into a fixture;
`test_figures` reproduces them.

**Result: all 30 per-building coverage values reproduced to within the 0.05%
rounding of the published labels, and both stated service scores (8 and 4) exact.**

This is the gate that pins the engine to the organizers' reading of D1–D4, and
it is what caught both subtleties in §3.

### 4.2 An independent implementation

`giscup crosscheck` compares the rotational-sweep engine against a deliberately
naive brute-force sampler that shares no code path: dense boundary sampling plus
direct segment tests.

**Result: worst disagreement 0.0014–0.0027 over ~200 building/placement pairs**,
consistent with the sampler's own resolution.

> Writing the reference caught a bug **in the reference**, worth recording
> because any team validating this way will hit it: a "does the segment cross
> any edge?" test misses self-occlusion entirely. A segment from one point on a
> building's boundary to another point on the same building crosses no edge
> *properly* — it starts and ends **on** the boundary. Such a checker silently
> over-reports every convex building.

### 4.3 Run-day input robustness

`tests/robustness.sh` mutates the sample into 14 shapes the real dataset might
plausibly arrive in and checks each still solves and verifies identically: id
under `ID` / `building_id` / string-valued / absent, extra properties containing
commas, MultiPolygon, counter-clockwise rings, unclosed rings, XYZ coordinates,
no CRS, shifted origin, duplicated vertices, and k exceeding the candidate count.

**Result: 14/14 pass with identical scores.**

This exists because the failure mode it guards against is catastrophic and
asymmetric: a wrong id-property name invalidates *all nine blocks*, in a
24-hour window with no second attempt. It dominates any few-percent algorithmic
gain.

### 4.4 Submission integrity

The claimed building list is re-derived by an **uncapped** exact sweep before
writing — the search radius is a speed-up, not a scoring decision. Skipping that
step left 4 serviceable buildings unclaimed across the nine blocks.

**Result: 9/9 blocks, 0 false claims, 0 unclaimed-but-serviceable, 0 unknown ids.**

### 4.5 Numerical margin audit

How close do our claims sit to the threshold? A building claimed at `τ + 1e-15`
is one the organizers' evaluator might compute at `τ - 1e-15`.

| margin above τ | < 1e-12 | < 1e-9 | < 1e-6 |
|---|---|---|---|
| claims, all 9 blocks | **0** | **1** | 660 |

The τ=0.5 blocks hold the 660 (99–309 per block). That is the geometry of §3.1
showing up: two adjacent edges of a near-rectangle are *almost exactly* half its
perimeter. But 1e-6 is ~9 orders above floating-point noise, and the genuinely
knife-edge bucket is empty.

Separately checked: does normalising rings CCW change a perimeter through
summation order? **Bit-identical on all 12,860 buildings** — zero risk from that
transformation.

---

## 5. What was tried

### 5.1 Exact visible arcs instead of boundary sampling — **kept**

The obvious approach is to sample each boundary densely and test visibility per
sample. Instead, a **rotational plane sweep** from each antenna yields the
visible sub-arcs **exactly, in closed form**: sort all nearby edge endpoints by
angle, sweep, and the nearest active edge over each angular span is precisely
what is visible there.

Why it matters beyond elegance: on a *threshold* objective, any discretisation
gap between our coverage ratio and the organizers' costs buildings at the
margin. Exact arcs remove that gap entirely.

Special cases the sweep must handle, both derived from §3: edges incident to the
antenna subtend zero angle so they block nothing, yet are *fully* visible; and
the interior angular wedge at the mount point is dark.

### 5.2 Edge-interior candidate sites — **rejected, measured**

Does adding candidates along edges, not just at vertices, help?

| spacing | candidates | τ=0.75, k=1000 | τ=0.5, k=500 | precompute |
|---|---|---|---|---|
| vertices only | 78,727 | **4,090** | **5,379** | 3.7 s |
| + every 15 m | 97,430 | 4,085 | 5,379 | 5.8 s |
| + every 8 m | 146,189 | 4,088 | 5,379 | 8.4 s |

**No gain at either threshold, for ~2× the candidates and ~2× the precompute.**
Exactly what §3.1 predicts: an edge-interior point covers one edge where a vertex
covers two, so it starts a full edge-length behind and rarely makes it up on
external visibility. Vertices-only is not a shortcut, it is the right answer.

### 5.3 Visibility radius — **600 m**

| search radius | 150 m | 300 m | 600 m | 1000 m | 1500 m |
|---|---|---|---|---|---|
| service score (τ=0.5, k=500) | 4,132 | 4,524 | **4,731** | 4,725 | 4,707 |
| precompute | 0.6 s | 1.0 s | 4.7 s | 14.6 s | 31.5 s |

Quality climbs steeply to 600 m then plateaus while cost keeps growing
quadratically. Anyone capping at ~150 m because "cities are dense" gives up 13%.

Separately, for the *verification* sweep:

| verify radius | ∞ | 3000 m | 2000 m | 1200 m | 600 m |
|---|---|---|---|---|---|
| score (τ=0.5, k=1000) | 9,322 | 9,322 | **9,322** | 9,308 | 9,212 |
| time | 1.6 s | 1.5 s | 0.8 s | 0.4 s | 0.2 s |

2000 m is bit-identical to uncapped and 2× faster — but this is a property of
this city's geometry, not a theorem, and must be re-checked on the real dataset.

### 5.4 The objective is not submodular

Coverage *length* `cov_b(S)` is monotone submodular. The score

```
f(S) = #{ b : cov_b(S) ≥ τ·P_b }
```

is **not** — the threshold destroys diminishing returns. Greedy on `f` carries no
guarantee and is also blind: lifting a building from 10% to 60% at τ=0.75 scores
zero marginal gain.

Four selection strategies were implemented and compared (`giscup bench`):

| τ | k | selfcover | bundle | bundle+lns | truncated |
|---|---|---|---|---|---|
| 0.25 | 50 | 522 | 619 | 1,242 | **2,104** |
| 0.25 | 1000 | 5,906 | 8,444 | 9,355 | **12,525** |
| 0.50 | 500 | 2,166 | 2,434 | 2,798 | **4,731** |
| 0.75 | 50 | 49 | 104 | **153** | 70 |
| 0.75 | 500 | 698 | 1,070 | **1,335** | 1,262 |
| 0.75 | 1000 | 1,395 | 2,381 | 2,789 | **3,398** |

- **`selfcover`** — own-vertices-only knapsack. The §3.1 floor. Never competitive
  but a useful sanity bound.
- **`truncated`** — greedy on the submodular surrogate `Σ_b min(cov_b, τP_b)`.
  Truncating a coverage function preserves submodularity, so lazy greedy keeps
  the (1−1/e) guarantee *on the surrogate*. Best of the four almost everywhere.
- **`bundle`** — ratio greedy over per-building "completion bundles", the
  textbook partial-cover framing. **It lost**, and the reason is instructive: it
  only ever considers each building's locally-cheapest completion, so it never
  finds the antenna that is mediocre for any single building but excellent for
  twenty. Retained in the tree because the comparison is the evidence.

### 5.5 Two currencies for pricing progress — **both kept, chosen per sub-problem**

The truncated surrogate prices progress in **metres of perimeter**. But the
budget is denominated in **antennas**. A building at 90% coverage still needing
two antennas is worth far less than one at 90% needing one, and a
metre-denominated potential cannot tell them apart.

So a second potential prices the remaining work in the currency of the binding
constraint (cost-benefit greedy in the sense of Khuller–Moss–Naor):

```
units_b = (τ·P_b − covered_b) / reach_b      antenna-units outstanding
φ_b     = 1 / (1 + units_b)^q
```

where `reach_b` is the largest slice any single antenna delivers to `b` — the
exchange rate between metres and antennas.

| τ | k | metre-priced | antenna-priced |
|---|---|---|---|
| 0.75 | 500 | 2,254 | **2,324** |
| 0.75 | 50 | 249 | **257** |
| 0.75 | 1000 | 4,591 | **4,638** |
| 0.50 | 500 | 5,409 | **5,444** |
| 0.50 | 50 | **664** | 572 |

Neither dominates — antenna-pricing wins 3% at (0.75, 500) and loses 14% at
(0.5, 50). Both are swept together with the convexity exponent `q`, and the
winner is chosen **per sub-problem by measurement**. The selection pattern is
itself a finding: metre-pricing wins at every τ=0.25 sub-problem, antenna-pricing
at every τ=0.5 and 0.75 one. At τ=0.25 nearly every building finishes with one
antenna, so there is no antenna-scarcity to reason about.

### 5.6 The marginal-returns diagnostic

Scoring is relative and there is no competitor data, so "is 4,090 good?" has no
internal answer. But there is a usable self-diagnostic: plot score against `k`.

| antennas (τ=0.75) | 0 → 1000 | 1000 → 2000 | 2000 → 4000 |
|---|---|---|---|
| buildings gained per antenna | 4.09 | **4.38** | 2.20 |

Marginal returns that **increase** are a tell. A best-first greedy should exhaust
its best opportunities first; if antennas 1001–2000 are each worth more than
antennas 1–1000, the early ones were misallocated. The mechanism is
complementarity — early antennas get spent part-covering buildings the budget
will never finish.

Costs three extra runs, needs no knowledge of the optimum, and motivated §5.7.

### 5.7 Target-set refinement ("focus") — **kept**

Stop pretending every building is reachable. Solve once to learn which buildings
the budget can plausibly finish, restrict the objective to that set plus a
margin, re-solve, iterate over several margins keeping the best. The score still
counts every serviced building — the mask shapes the search, not the reward.

| τ | k | before | **+ focus** | |
|---|---|---|---|---|
| 0.75 | 1000 | 4,090 | **4,591** | +12.3% |
| 0.75 | 500 | 1,792 | **2,254** | +25.8% |
| 0.75 | 50 | 193 | **238** | +23.3% |
| 0.50 | 50 | 593 | **612** | +3.2% |
| 0.25 | 500 | 10,129 | 10,129 | 0 |

Exactly the shape §5.6 predicts: gains concentrate where completion-coupling
binds, and vanish at τ=0.25 where 98.5% of buildings finish with one antenna and
there is nothing to misallocate.

### 5.8 The polish needs both neighbourhoods — **kept**

Large-neighbourhood search frees antennas that provably hold no building above
threshold, then re-spends the budget. Focus exposed a silent bug and then a
subtler trade-off.

**The bug:** the polish repaired *unfocused*. An unfocused repair can never
improve a focused solution — it just re-proposes the myopic placement focus had
beaten — so the polish reported "no gain" and quietly did nothing. Inheriting the
mask recovered +178 at (0.75, 500).

**The trade-off:** the mask is also a wall, hiding every building outside the
target set. Measured at a 90 s budget:

| repair neighbourhood | (0.75, 500) | (0.5, 50) |
|---|---|---|
| masked | **2,432** | 666 |
| unmasked | 2,254 | **735** |
| alternating within one search | — | 708 |
| **both, keep the better** | 2,426 | 703 |

Neither dominates, and alternating splits the difference rather than taking the
max. Running both from the same start and keeping the winner lands within ~1% of
the better at each, without ever collapsing to the worse. The residual shortfall
is only that each half gets half the clock, which shrinks as the budget grows.

> The (0.5, 50) regression was caught **only** because an earlier run had
> recorded 735 for that combo. A change that improved the headline sub-problems
> while quietly costing 9% elsewhere would otherwise have shipped looking like a
> win. Keep the per-combo history.

### 5.9 LP relaxation for an upper bound — **failed**

Every comparison above is against our own baselines. To get a real bound: cut
each boundary at every arc endpoint into *atoms* so coverage becomes linear, then
solve with HiGHS (`tools/lp_bound.py`).

```
max  Σ_b z_b
s.t. x_a ≤ Σ_{c covers a} y_c                    for each atom a
     Σ_{a∈b} len(a)·x_a ≥ τ·P_b·z_b
     Σ_c y_c ≤ k ,   0 ≤ x,y,z ≤ 1
```

On a 240-building crop (41k variables, 40k constraints) at τ=0.75, k=20:
**bound 204.3**, our solution **92**. That looks like a 45% gap. It is not.

Nothing stops the LP setting `z_b = covered_b/(τ·P_b)`, so it really maximises
`Σ_b min(1, cov_b/(τ·P_b))` and cannot distinguish "148 buildings at 70%" from
"104 finished". Our own solution scores **176.3** in that same currency — **86%
of the LP optimum**. The gap is the relaxation's, not the solver's.

**Conclusion: this LP is not a usable optimality gap and should not be quoted as
one.**

### 5.10 Knapsack-cover inequalities — **failed**

The textbook repair for a weak covering LP
([Carr, Fleischer, Leung & Phillips, SODA 2000](https://dl.acm.org/doi/10.5555/338219.338241);
construction quoted from [Chekuri & Quanrud, arXiv:1807.11538](https://arxiv.org/pdf/1807.11538)):
contract a subset `A`, compute the residual demand, cap every remaining
coefficient at it.

It does apply to our structure. Our coverage requirement relaxes to a knapsack
cover on the antennas directly (union ≤ sum), and the union structure permits a
**stronger** cut than the textbook form — the residual uses `cov_b(A)` rather
than `Σ_{c∈A} w_c`, and since the union is no larger the residual is no smaller,
which makes the cut tighter. Validity argument is written into
[`tools/lp_bound.py`](tools/lp_bound.py).

| cuts added | none | 240 (depth 1) | 675 (depth 3) |
|---|---|---|---|
| bound | 204.3 | 204.3 | 204.3 |

**Not one unit.** The technique is correct; the diagnosis was wrong. KC
inequalities repair a covering constraint whose **demand is fixed**. Ours is not
— the LP sets `z_b = 0.6`, achieves 60% of the coverage, and satisfies
`Σ min(w_c,R)·y_c ≥ 0.6R` with 60% of the antenna mass. The cut scales linearly
with `z_b` and binds nothing. The disease is partial credit in the *objective*,
not weakness in the *constraints*.

### 5.11 Exhaustive optimum on tiny instances — **worked**

`giscup exact` enumerates every antenna set of size `k` and evaluates exactly.
Slow by construction and viable only for `k ≤ 3`, but it is the only measurement
here that is not a comparison against ourselves.

| buildings | τ | k | heuristic | **optimum** | ratio |
|---|---|---|---|---|---|
| 40 | 0.50 | 2 | 15 | 15 | 1.00 |
| 40 | 0.75 | 2 | 4 | 4 | 1.00 |
| 40 | 0.50 | 3 | 17 | **20** | 0.85 |
| 40 | 0.75 | 3 | 6 | **8** | 0.75 |
| 70 | 0.50 | 2 | 12 | **15** | 0.80 |
| 70 | 0.75 | 2 | 4 | 4 | 1.00 |
| 70 | 0.50 | 3 | 17 | **22** | 0.77 |

Losing 20% on a **two-antenna** problem is textbook greedy pair-blindness: the
solver commits to the best single next antenna and cannot see the pair that
beats it. It also lands precisely on the small-`k` sub-problems that decide the
competition.

### 5.12 GRASP randomised multi-start — **works small, does not transfer**

Sample uniformly among near-best choices instead of always taking the argmax,
restart many times, keep the best. Restarts are independent and run concurrently
on cores otherwise idle through selection.

Against the known optima it works well — mean ratio **0.79 → 0.93**, one
instance solved exactly. On the real dataset at k=50 with 128 restarts:

| τ | k | without | with 128 restarts |
|---|---|---|---|
| 0.25 | 50 | 2,336 | 2,336 |
| 0.50 | 50 | 717 | 717 |
| 0.75 | 50 | 298 | 298 |

**Nothing.** At k=3 the solver makes three choices and pair-blindness dominates,
so randomising them explores a meaningful fraction of the space. At k=50 over
78,727 candidates it makes fifty choices, and 128 restarts sample a vanishing
corner. Kept behind `--restarts`, default off.

The lesson: **the tiny-instance oracle diagnoses a mechanism, it does not predict
which remedy pays at scale.**

> On first integration the restart branch also overwrote the tuned
> configuration, so the polish afterwards ran a different potential than the
> tuner had chosen — (0.75, 50) scored 277 against 298, an apparent 7%
> regression from something meant to be a pure max. Caught only because 298 was
> already recorded. Restarts now contribute a solution only.

### 5.13 2-exchange local search — **kept, the largest single gain**

§5.11 diagnosed the mechanism; §5.12 failed to fix it by sampling. This attacks
it directly: withdraw each chosen antenna in turn, try every member of a
shortlist of currently-highest-gain candidates in its place, keep the swap if the
true score rises.

This is the move destroy-repair structurally **cannot** make — that polish only
frees antennas *provably holding nothing up*, so an antenna that is genuinely
needed yet still the wrong choice is never reconsidered.

Restricting additions to a shortlist keeps a pass at `O(k · shortlist)` rather
than `O(k · |C|)`, so unlike restarts the cost scales with the **budget** rather
than with the size of the search space — which is why it transfers where GRASP
did not.

Against the known optima:

| buildings | τ | k | before | **+ 2-exchange** | optimum |
|---|---|---|---|---|---|
| 40 | 0.50 | 3 | 17 | **20** | 20 |
| 40 | 0.75 | 3 | 6 | **8** | 8 |
| 70 | 0.50 | 2 | 12 | **15** | 15 |
| 70 | 0.50 | 3 | 17 | **22** | 22 |

**Mean ratio to optimum 0.79 → 1.00** — every verifiable instance solved
exactly, where GRASP reached 0.93. And on the full dataset:

| τ | k | before | **+ 2-exchange** | |
|---|---|---|---|---|
| 0.75 | 50 | 298 | **376** | +26% |
| 0.50 | 50 | 725 | **848** | +17% |
| 0.75 | 500 | 2,571 | **2,847** | +11% |

### 5.14 Scale testing

Synthetically tiled datasets (`tools/scale_dataset.py`), one sub-problem, single
exponent, no polish:

| dataset | buildings | edges | contribution map | total | peak RSS |
|---|---|---|---|---|---|
| sample | 12,860 | 78,727 | 4.7 s | 7 s | 0.6 GB |
| 4× | 51,440 | 314,908 | 16.7 s | 30 s | 2.5 GB |
| 16× | 205,760 | 1,259,632 | 78.8 s | 174 s | 7.6 GB |

The contribution map scales essentially **linearly** in building count — a
per-candidate sweep costs what local density says it costs, not what the total
extent says. Extrapolating, a 1M-building dataset is roughly 8 minutes of
precompute. The term that grows faster is exact verification (`O(k·E log E)`),
which is what the capped verify radius of §5.3 addresses.

---

## 6. Final results

Sample dataset, radius 600, verify-radius 2500, 150 s polish per sub-problem,
2-exchange enabled, potential and exponent auto-tuned per sub-problem.

| τ | k | selfcover | bundle | truncated | **final** | final/truncated | pricing |
|---|---|---|---|---|---|---|---|
| 0.25 | 50 | 522 | 619 | 2,104 | **2,373** | 1.13 | metre |
| 0.25 | 500 | 3,464 | 4,725 | 9,705 | **10,462** | 1.08 | metre |
| 0.25 | 1000 | 5,906 | 8,444 | 12,525 | **12,802** | 1.02 | metre |
| 0.50 | 50 | 194 | 215 | 469 | **848** | 1.81 | antenna |
| 0.50 | 500 | 2,166 | 2,434 | 4,731 | **6,080** | 1.29 | antenna |
| 0.50 | 1000 | 4,258 | 5,117 | 8,971 | **10,117** | 1.13 | antenna |
| 0.75 | 50 | 49 | 104 | 70 | **377** | 5.39 | antenna |
| 0.75 | 500 | 698 | 1,070 | 1,262 | **2,863** | 2.27 | antenna |
| 0.75 | 1000 | 1,395 | 2,381 | 3,398 | **5,391** | 1.59 | antenna |

Under the competition's relative scoring, a `truncated` submission would earn
**6.27 / 9** against ours.

**How to read that number honestly:** it measures how much the later work added
over our own earlier baseline. It is *not* evidence that 5,391 is near-optimal at
(0.75, 1000) — §5.9 showed the obvious way to establish that does not work, and
§5.11's oracle stops at k=3.

Tuned configuration per sub-problem:

| τ \ k | 50 | 500 | 1000 |
|---|---|---|---|
| 0.25 | 8.0 metre | 6.0 metre | 8.0 metre |
| 0.50 | 8.0 antenna | 3.0 antenna | 3.0 antenna |
| 0.75 | 3.0 antenna | 2.0 antenna | 1.0 antenna |

Higher τ wants *less* convexity, and antenna-pricing takes over exactly where
buildings start needing more than one antenna.

### Where the competition is decided

τ=0.25 is nearly saturated — **12,802 of 12,860 at k=1000 (99.5%)** — and will
likely be a near-tie across serious entries, worth almost nothing
competitively. The sub-problems that separate the field are **(0.75, 50)**,
**(0.75, 500)** and **(0.75, 1000)**, where method choice swings the score
1.5–5.4×. Budget run-day compute there.

---

## 7. What is still open

1. **The oracle has run out of resolution.** 2-exchange now solves every
   verifiable instance exactly, so the tiny-instance test can no longer see any
   gap — which is *not* the same as being optimal at k=1000. Extending
   exhaustive verification to k=4–5 by branch and bound would restore the
   measurement. Without it there is no longer any signal saying whether more
   search pays.
2. **3-exchange, or a smarter shortlist.** The natural next move if the oracle
   is restored and still shows a gap.
3. **2-exchange costs a hair at saturated τ** — (0.25, 500) came out 10,462
   against 10,466 without it, by spending polish budget where almost every
   building is already serviced. Harmless, but argues for making `--swap`
   τ-conditional.
4. **Re-run the marginal-returns diagnostic (§5.6)** on the final configuration.
   It is cheap and it is the only remaining signal that needs no oracle.
5. **Re-run the robustness suite against the real dataset**, not against
   mutations of the sample, the moment it lands.

---

## 8. Reproducing everything here

```bash
make check                                    # figure oracle + brute-force crosscheck
bash tests/robustness.sh                      # 14 input-shape mutations
./giscup bench   --data data/GIS-cup-sample-dataset.geojson   # §5.4 table
./giscup exact   --data tiny.geojson --tau 0.75 --k 3         # §5.11 optimum
./giscup solve   --data data/GIS-cup-sample-dataset.geojson \
                 --radius 600 --verify-radius 2500 \
                 --lns-sec 150 --swap 400 --out submission.txt
./giscup verify  --data data/GIS-cup-sample-dataset.geojson --out submission.txt
python3 tools/lp_bound.py dump.txt 0.75 20 --kc 3             # §5.9, §5.10
```

Raw outputs behind every table are in [`results/`](results/).
