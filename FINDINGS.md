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

### 5.15 A verified solution archive — **kept; it paid for itself immediately**

Every placement produced is recorded under `(τ, k)` with its independently
verified score, the method, and a timestamp. Nothing enters on trust: `archive
add` re-verifies exactly and uncapped. Claims are never stored — they are
re-derived at export, so an exported file cannot assert coverage that does not
hold for the geometry it ships against. The index is append-only: a history, not
a cache.

Two properties follow. **Ratchet:** the submission is assembled from the archive
best, never from whatever the last run emitted, so an experiment that comes out
worse is recorded but cannot reach the submission. Verified by inserting a
deliberately weak `selfcover` placement (score 49) at (0.75, 50) — `best` stayed
at 377. **No silent regressions:** §5.12 records an integration that shipped a 7%
regression from something meant to be a pure maximum, caught only because a
number had been written down. That bookkeeping is now structural rather than a
habit.

It earned its keep on the first experiment after it: every radius improvement in
§5.16 landed automatically, with no manual promotion.

### 5.16 Re-tuning the radii — **the largest gain of this round**

`giscup tune` re-derives both radii on whatever dataset it is given. Pointed at
the sample, it contradicted two constants this document was carrying.

**Verify radius 2500 was not safe.** 2000 m loses 3 claims; 3000 m is the
smallest matching uncapped. §5.3 measured 2000 m as exact — for a *different
placement*.

**Search radius 600 was costing 1.2–4.5% everywhere.** §5.3 established 600 m as
the plateau using the **metre-priced** potential. The antenna-priced potential of
§5.5 keeps gaining well past it, and nobody re-measured after changing the
potential:

| radius | (0.75, 50) | (0.75, 500) | (0.5, 500) |
|---|---|---|---|
| 600 | 376 | 2,847 | 6,033 |
| 1000 | **393** | 2,874 | **6,105** |
| 1500 | 384 | **2,893** | 6,073 |

All three beat the previous archive bests *despite 60 s of polish against 150 s*.
`tune` now recommends the **knee** — smallest radius within 2% of the best —
rather than the maximum, since precompute grows quadratically while quality
flattens. On the sample that picks 1000 m, which is what won through the full
pipeline; the raw maximum would have said 1500 m.

The general lesson outlives the number: **a tuned constant is only valid for the
configuration it was tuned under.** Both of these were measured correctly, then
silently invalidated by a later change to something else.

Supporting tooling, all exercised: `tools/inspect_dataset.py` (loader quirks, id
property, ring orientation, lon/lat guard), `tools/runday.sh` (unseen file →
go/no-go, robustness suite run against *that file* rather than the sample),
`tools/allocate.py` (budget split by measured relative polish sensitivity —
(0.75,50) is 31.8%, (0.25,1000) is 1.0%).

`--claim-epsilon` now defaults to **1e-9**, measured across all nine blocks to
cost **0 claims of 51,313**. 1e-6 would cost 671 (1.31%), all at τ=0.5, so it is
deliberately not the default — exactly as §4.5 argued.

### 5.17 Beam search construction — **rejected**

2-exchange only repairs pair-blindness after the fact, so the idea was to offer
the pair *during* construction. Pairs are generated per building from the
antennas that actually see it (never enumerated over all candidates, which is
O(|C|²)); extensions are ranked by gain **per antenna** so a pair is not
preferred merely for spending more budget; sets reached by different orders are
de-duplicated so the beam stays genuinely diverse.

It passes the correctness gate — ratio 1.00 on all four exact-oracle instances.
At equal polish budget it loses everywhere:

| τ | k | beam=0 | beam=4 | beam=8 | runtime (0 / 4 / 8) |
|---|---|---|---|---|---|
| 0.75 | 50 | **384** | 379 | 381 | 76 s / 110 s / 144 s |
| 0.50 | 50 | **868** | 861 | 861 | 76 s / 112 s / 149 s |
| 0.75 | 500 | **2,868** | 2,828 | 2,843 | 80 s / 532 s / 911 s |

Mechanism: the polish already recovers from myopic construction, so at fixed time
breadth during construction is a worse buy than more polish — and ranking
*partial* placements by potential is a weak proxy, since a member that looks good
at step *j* need not be better at *k*. Same shape as the GRASP negative (§5.12):
diversification during construction does not pay once a strong repair operator
exists. Default off (`--beam 0`).

### 5.18 Focused MIP with local branching — **dropped: it fails its own gate**

§5.9 killed the LP *as a bound* because fractional `z_b` takes partial credit.
With `z_b` **binary** the threshold is enforced by integrality, so the same model
should work as a *solver* — and HiGHS exposes a dual bound that would have
restored the optimality-gap measurement §7 says was lost. Built with
`scipy.optimize.milp`, focused to a window with atom pruning, warm-started, with
Fischetti–Lodi local branching available.

It is wrong. On tiny40 (τ=0.5, k=3), where enumeration gives optimum **20**:

| | serviced |
|---|---|
| incumbent (heuristic) | 20 |
| MIP, 3,103 vars, 222 s | **22** — "dual bound 22.0, gap 0.00%" |
| the MIP's own placement, scored by the validated engine | **18** |

Two independent implementations — the enumerator and the visibility engine —
agree against it. The MIP over-counts by 4 buildings on a 40-building instance
*while reporting a zero optimality gap*. The defect was not located before the
time box expired.

Dropped per the kill criterion; kept in the tree disabled and labelled, because
this is the most useful failure of the round: **a method that announces "proven
optimal" and is quietly wrong is far more dangerous than one that is merely
weak.** It is also the sharpest justification for the two structural rules
already in place — the exact-oracle correctness gate, and the archive's refusal
to admit anything that has not passed `giscup verify`.

### 5.19 Lagrangian completion pricing — **both parts rejected**

Two ideas, one refinement and one new constructor. Both measured, both dropped.

**Bundle-based exchange rate.** §5.5 converts metres to antennas via `reach_b` =
the largest slice a *single* antenna delivers, which is optimistic whenever a
building needs two or three: the second antenna gets the harder facade. Replaced
with the rate implied by the actual cheapest completion, `τ·P_b / m` for an
`m`-antenna set. Passes the correctness gate at 1.00 — and produces
**bit-identical scores** at every sub-problem measured (393, 868, 2874, 10531).
Metre-priced configurations never consult `reach_` at all, and where
antenna-pricing wins, the per-sub-problem exponent sweep absorbs the change: a
different exponent reaches an equivalent solution. Theoretically right,
practically inert once the exponent is already tuned.

**Lagrangian price sweep.** Dualise the budget `Σy ≤ k` with a price λ; each
building buys its cheapest completion when `c_b < 1/λ`; the *union* of accepted
bundles supplies the sharing. This is genuinely distinct from the ratio greedy of
§5.4, which picks sequentially by locally-cheapest marginal cost and therefore
never sees the antenna that is mediocre for one building and excellent for
twenty.

It fails the correctness gate — 19 against optimum 20, 20 against 22 — and the
polish does not recover it, so the sweep lands in a *worse basin* rather than
merely starting further away. At scale (60 s polish, radius 1000):

| τ | k | baseline | Lagrangian | |
|---|---|---|---|---|
| 0.75 | 50 | **393** | 379 | −3.6% |
| 0.50 | 50 | **868** | 850 | −2.1% |
| 0.75 | 500 | **2,874** | 2,799 | −2.6% |
| 0.25 | 500 | 10,531 | **10,561** | +0.3% |

The mechanism explains the shape exactly. At τ=0.25 nearly every building needs
one antenna, so bundles are singletons and the sweep degenerates to clean maximum
coverage, where union-based sharing is precisely the right instinct. At τ=0.75
bundles are two or three antennas and taking their union is too coarse a way to
spend a tight budget. **The method is strongest exactly where the competition is
not decided** — which is the least useful place to be strong.

Both default off. The archive was unchanged by the entire experiment.

### 5.20 Conformance with the official evaluator — **two defects found, one fatal**

The organizers publish their grading code
([alowe/gis-cup-2026-evaluator](https://github.com/alowe/gis-cup-2026-evaluator),
pinned at `9203c0d`, tag v0.1.0, `@arcgis/core` 5.1.0) and commit to consistency
with it. It is therefore ground truth above our own figure oracle.

**The fatal one.** Our writer emitted the parameter line as `0.25,50`. The
official parser matches it with

```
/^\(\s*([^,]*)\s*,\s*([^,]*)\s*\)$/
```

anchored, parentheses required. The bare form matches nothing, so `tau` and `k`
come back undefined, raising `INVALID_TAU` and `INVALID_K` — whose documented
action is **"Score this subproblem as zero"**. All nine blocks. Zero.

What makes this the most instructive failure in this log: **every internal gate
stayed green.** The figure oracle, the brute-force crosscheck, 14/14 robustness
variants, `SUBMISSION OK` — all passed, because all of them used our own reader,
which accepted our own format. No amount of internal validation could have found
it. Only the grader's own rules could.

**The second.** The grader's verdict is `visibleLengthMeters >= tau *
perimeterMeters` — a *length* comparison. Ours was ratio-form. Identical in
exact arithmetic, not in doubles; and since the grader computes coverage **only
for buildings we claim**, a last-ulp disagreement in the conservative direction
still costs a point. Verify now uses the grader's operand form.

**Differential result.** Our archive-best submission, through the official
grader end to end:

| block | τ | k | claimed | verified | flips |
|---|---|---|---|---|---|
| 1–9 | all | all | **51,844** | **51,844** | **0** |

Zero verdict flips across 51,844 claims — our exact-arc engine and their
ArcGIS-backed radial sweep agree on every one. The harness also reproduces the
repo's own 50-antenna fixture exactly (140 claimed, 140 verified, 0 warnings).

**Run-day consequence.** The official filter costs ~26 min for all nine blocks at
sample scale (k=50 ≈ 15 s, k=500 ≈ 170 s, k=1000 ≈ 330 s). Affordable as the
final claim filter before packaging, far too slow for an inner loop. Our engine
drives the search; the official harness is the last gate before upload.

**Also corrected upstream:** the sample dataset lost building 9448's stray inner
ring. Our loader's largest-ring handling made the effective geometry
bit-identical — zero features differ — so every number in this document stands.

### 5.21 Per-block radius and swap sweep — **resolves §7.3 and §7.4**

Nine blocks × radius {1000, 1500, 2000} × with/without `--swap`, full polish,
every result verified and archived (`tools/radius_sweep.sh`, 54 runs, 2.9 h of a
6 h budget). No winner-picking logic exists or is needed: the archive keeps the
best per block by construction, so a losing variant is simply never exported.

| τ | k | r1000 sw | r1000 — | r1500 sw | r1500 — | r2000 sw | r2000 — | winner |
|---|---|---|---|---|---|---|---|---|
| 0.25 | 50 | 2,426 | 2,395 | **2,442** | 2,433 | 2,441 | 2,442 | r1500 swap |
| 0.25 | 500 | 10,592 | 10,546 | 10,587 | **10,613** | 10,586 | 10,612 | r1500 **no swap** |
| 0.25 | 1000 | 12,818 | 12,799 | 12,807 | 12,810 | **12,824** | 12,808 | r2000 swap |
| 0.50 | 50 | 868 | 784 | 870 | 781 | **872** | 762 | r2000 swap |
| 0.50 | 500 | 6,150 | 5,868 | **6,168** | 5,862 | 6,137 | 5,876 | r1500 swap |
| 0.50 | 1000 | 10,180 | 9,846 | **10,182** | 9,868 | 10,179 | 9,898 | r1500 swap |
| 0.75 | 50 | **392** | 290 | 387 | 296 | 383 | 281 | r1000 swap |
| 0.75 | 500 | 2,932 | 2,614 | 2,915 | 2,593 | **2,970** | 2,638 | r2000 swap |
| 0.75 | 1000 | 5,533 | 5,096 | **5,587** | 5,129 | 5,482 | 5,078 | r1500 swap |

**§7.4 confirmed: there is no best global radius.** 1500 m wins five blocks,
2000 m three, 1000 m one. Per-block tuning is not a refinement, it is required —
and since scoring is per sub-problem, it costs nothing but time.

**§7.3 resolved, and the concern was smaller than it looked.** 2-exchange wins
**eight of nine** blocks, often decisively at high τ (2,932 vs 2,614 at
(0.75, 500), +12%). It loses at exactly one — (0.25, 500), 10,587 vs 10,613 —
the near-saturated block where §7.3 flagged it. The right response is not a
τ-conditional rule but the sweep plus the archive: measure both, keep the
winner, write no logic.

Archive total: 51,844 → **52,051**.

### 5.22 Polish-time scaling, and the second rehearsal

**Rehearsal 2 (Task 4 acceptance: two different alien configurations).** 2×
geometry (25,720 buildings), a **bare-numeric** parameters file rather than a
labelled one, τ ∈ {0.45, 0.55, 0.95} × k ∈ {80, 400, 1500}. Driven by
`runday.sh` alone: **9/9 blocks clean, conformance passed, SUBMISSION OK**,
52,637 serviced, zero manual intervention.

It re-derived radius 1000 / **verify-radius 3000** — different again from
rehearsal 1's 1000/5000 and from the sample's own values. Three geometries,
three different verify radii. That is the fourth independent confirmation that
inheriting a tuned constant is this project's recurring failure mode, and it is
why the playbook re-derives both on the real file.

**Polish-time scaling** on the τ=0.75 trio, radius 1500, verify 5000:

| τ | k | 150 s | 600 s | 1800 s | 150→600 | 600→1800 |
|---|---|---|---|---|---|---|
| 0.75 | 50 | 387 | **397** | 397 | +2.6% | **0** |
| 0.75 | 500 | 2,923 | 2,944 | **2,964** | +0.7% | +0.7% |
| 0.75 | 1000 | 5,587 | 5,627 | **5,653** | +0.7% | +0.5% |

Neither branch §7 anticipated. Small `k` **saturates by 600 s** — 12× the clock
buys literally nothing at (0.75, 50). Larger `k` keeps gaining but at a steeply
diminishing rate: roughly +0.7% per 4× of time, and still positive at 1800 s.

**Run-day consequence.** Past ~600 s the marginal second is worth more spent on
a *different* radius/swap variant (§5.21, where the spread between variants at
one block reaches 12%) than on more polish of the same one. So the allocator
should cap per-block polish rather than divide the window without bound, and
push the remainder into the sweep — which the archive already ratchets safely.
This is the first measurement that tells the run-day schedule what to do with
hours rather than minutes.

Archive: 52,051 → **52,121**.

### 5.23 Randomised-destroy LNS + plateau moves — **implemented; unproven at scale on this build**

The one construction-independent idea left (cont2.md Task 2). Every construction
diversifier failed (§5.12, §5.17, §5.19) because the repair recovers from any
reasonable start, so the remaining lever is diversity *of* the repair. Two moves,
both behind flags, both default off:

- **`--lns-destroy`** — after the polish converges, ruin ⌈ρk⌉ chosen antennas and
  rebuild with the tuned greedy, re-polish, keep the best incumbent. ρ cycles
  over {0.05, 0.10, 0.15} and the operator alternates between a uniform-random
  slice and a spatial cluster (a random chosen antenna plus its nearest chosen
  neighbours). This is **coupled multi-antenna replacement** — the one move class
  nothing in the stack can make: 2-exchange (§5.13) is radius 1, and the destroy
  in the existing `lns()` only frees antennas *provably holding nothing up*.
- **`--swap-plateau N`** — the objective is a step function, so strict 2-exchange
  stalls on its plateaus. Accept up to `N` score-*equal* swaps that strictly raise
  the truncated secondary measure `Σ_b min(cov_b, τ·P_b)` between strict gains
  (the bound stops cycling; the secondary rises monotonically within a chain).

**Correctness.** Every candidate is re-verified uncapped before it can enter the
archive, so the keep-best structure cannot ship an over-claim. Verified directly:
on tiny70 and the sample the exported claims equal the independently verified
count with 0 false / 0 missed for every variant. (The `giscup exact` k≤3 oracle
gate could **not** be run on this Windows/MinGW-6.3 build — the enumerator's
`static thread_local` scratch segfaults under this compiler; it must be re-run on
the portable Linux build before the moves are trusted as *search*, as opposed to
trusted as *safe*, which the re-verification already guarantees.)

**Where it helps — a cheap instance.** tiny70, τ=0.75, k=15, radius 600, 4 s:

| | verified score |
|---|---|
| baseline (`--swap 400`) | 50 |
| `--swap 400 --lns-destroy --swap-plateau 8` | **51** |

The loop escapes a plateau the strict polish cannot: it finds a coupled swap that
lifts a 51st building over τ, and the uncapped verify confirms all 51.

**Where it loses — the real instance, at equal wall-clock on this build.**
Sample, (0.75, 50), radius 600, **90 s** each:

| | verified score |
|---|---|
| baseline (`--swap 400`) | **368** |
| `--lns-destroy --swap-plateau 8` | 362 |

−6 at equal wall-clock. The mechanism is the point, and it is a property of *this
build*, not necessarily of the method: the destroy loop calls the greedy repair
once per iteration, and greedy repair is the OpenMP-parallel hot path
(`solvers.hpp` gain scan). This machine's MinGW toolchain **links no OpenMP**
(no `libpthread`), so every repair runs single-threaded and each destroy
iteration is expensive — in 90 s the loop completes far fewer iterations than the
plain 2-exchange gets passes, and iteration count is exactly what the method
trades on. tiny70 wins *because* its 70-building repair is cheap enough to iterate
many times even single-threaded; the sample's 12,860-building repair is not, here.

**Verdict.** Kept, default off, **not rejected**: the equal-wall-clock loss is
confounded by the missing OpenMP, so it is not a fair test of the move. It must be
re-measured on a multi-threaded build (the portable Linux build, or Kaggle/Colab)
where greedy repair is ~5–8× faster and the loop gets the iteration budget it is
designed for. Because the archive ratchets (§5.15), the safe run-day use is simply
to add `--lns-destroy --swap-plateau 8 --archive-add` as an *extra* variant on the
high-variance τ=0.75 blocks: it can only raise the submission, never lower it.

Commands:
```bash
./giscup solve --data data/GIS-cup-sample-dataset.geojson --tau 0.75 --k 50 \
    --radius 600 --lns-sec 90 --swap 400 --lns-destroy --swap-plateau 8 \
    --archive-add --out /dev/null
```

Archive unchanged by the experiment on this build (the losing sample variant did
not beat the incumbent; the winning tiny70 variant is not a competition block).

---

## 6. Final results

Sample dataset, radius 1000, verify-radius 3000, 150 s polish per sub-problem,
2-exchange enabled, potential and exponent auto-tuned per sub-problem.

| τ | k | truncated | prev (r=600) | **now (r=1000)** | Δ | now/truncated |
|---|---|---|---|---|---|---|
| 0.25 | 50 | 2,104 | 2,373 | **2,426** | +2.2% | 1.15 |
| 0.25 | 500 | 9,705 | 10,462 | **10,581** | +1.1% | 1.09 |
| 0.25 | 1000 | 12,525 | 12,802 | **12,809** | +0.1% | 1.02 |
| 0.50 | 50 | 469 | 848 | **868** | +2.4% | 1.85 |
| 0.50 | 500 | 4,731 | 6,080 | **6,148** | +1.1% | 1.30 |
| 0.50 | 1000 | 8,971 | 10,117 | **10,161** | +0.4% | 1.13 |
| 0.75 | 50 | 70 | 377 | **393** | +4.2% | 5.61 |
| 0.75 | 500 | 1,262 | 2,863 | **2,924** | +2.1% | 2.32 |
| 0.75 | 1000 | 3,398 | 5,391 | **5,534** | +2.7% | 1.63 |

Total serviced across the nine: **51,844** (was 51,313 at radius 600). The `Δ`
column is the radius re-tuning of §5.16 alone — no algorithmic change.

Under the competition's relative scoring, a `truncated` submission would earn
**6.18 / 9** against ours.

**How to read that number honestly:** it measures how much the later work added
over our own earlier baseline. It is *not* evidence that 5,534 is near-optimal at
(0.75, 1000) — §5.9 showed the obvious way to establish that does not work, and
§5.11's oracle stops at k=3 and §5.18's attempt to replace it produced a
confidently wrong answer.

Tuned configuration per sub-problem:

| τ \ k | 50 | 500 | 1000 |
|---|---|---|---|
| 0.25 | 8.0 metre | 6.0 metre | 8.0 metre |
| 0.50 | 8.0 antenna | 3.0 antenna | 3.0 antenna |
| 0.75 | 3.0 antenna | 2.0 antenna | 1.0 antenna |

Higher τ wants *less* convexity, and antenna-pricing takes over exactly where
buildings start needing more than one antenna.

### Where the competition is decided

τ=0.25 is nearly saturated — **12,809 of 12,860 at k=1000 (99.6%)** — and will
likely be a near-tie across serious entries, worth almost nothing
competitively. The sub-problems that separate the field are **(0.75, 50)**,
**(0.75, 500)** and **(0.75, 1000)**, where method choice swings the score
1.5–5.4×. Budget run-day compute there.

---

## 7. What is still open

1. **There is still no trustworthy optimality gap.** 2-exchange saturated the
   k≤3 oracle (§5.13) and the MIP built to replace it returned a confidently
   wrong answer (§5.18). The remaining route is a bespoke k=4–5 branch and bound
   built **over the validated visibility engine** rather than over a separate
   model — the lesson of §5.18 being that a second model is a second thing that
   can be wrong.
2. **Locate the MIP defect, or delete the tool.** It is disabled, but a wrong
   solver in the tree is a liability if someone later trusts its dual bound.
3. ~~2-exchange costs a hair at saturated τ~~ **Resolved (§5.21).** It wins 8 of
   9 blocks and loses only at (0.25, 500). No τ-conditional rule: sweep both and
   let the archive decide.
4. ~~Radius should be re-tuned per sub-problem~~ **Resolved (§5.21).** Confirmed
   and now automated: 1500 m wins five blocks, 2000 m three, 1000 m one. There is
   no best global radius. `tools/radius_sweep.sh` is in the run-day path.
5. **Re-run `tools/runday.sh` against the real dataset** the moment it lands —
   against *it*, not against mutations of the sample.
6. **Every construction-side idea has now failed.** GRASP (§5.12), beam search
   (§5.17) and Lagrangian pricing (§5.19) all lose to plain greedy once the
   2-exchange polish is present, and §5.19 showed even a *correct* refinement of
   the potential is absorbed by the exponent sweep. The consistent signal across
   four attempts is that construction is no longer the bottleneck — effort
   belongs in the repair operator, in the radii (§5.16, the only thing that has
   paid this round), or in restoring a trustworthy bound.
7. **Fairly measure randomised-destroy LNS (§5.23).** It is implemented, correct,
   and verified, but its one at-scale test ran single-threaded on a build with no
   OpenMP, where it lost by 6 at (0.75, 50) purely on iteration count. Re-run the
   `(operator × ρ × budget)` table on the portable multi-threaded build across the
   three τ=0.75 primaries and the two canaries before deciding keep/reject. Until
   then it stays a default-off, archive-ratcheted extra variant, not a baseline.

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
