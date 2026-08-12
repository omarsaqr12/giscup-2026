# SIGSPATIAL 2026 GIS Cup — approach, evidence, and run-day playbook

**Status:** the system described here is built, validated against the organizers'
own published numbers, benchmarked across all nine sub-problems, and scale-tested.
Everything below that states a number is a measurement, not an estimate.

**Clock:** sample dataset has been out since 31 Mar. Evaluation dataset drops
**15 Aug**, submission due **16 Aug** — a 24-hour execute-and-submit window.
Section 9 is the playbook for that day.

---

## 1. What changed from the first draft of this plan

The first draft was a reasonable sketch, but four of its load-bearing claims were
wrong, and each would have cost real points. Recording them because the reasoning
matters more than the conclusions:

| First draft said | Reality | Consequence |
|---|---|---|
| "exclude the antenna's own building from blocking consideration" | A building's own interior **does** occlude its own boundary. Verified against Figure 4: antenna `a` sits on building 1 and sees only 3 of its 8 edges. | Would have inflated every coverage figure, producing a submission full of buildings we cannot actually serve. |
| "the service score is monotone submodular, so greedy has the classical guarantee" | Coverage *length* is submodular; the **thresholded count is not**. Greedy on the raw objective is also blind — lifting a building from 10% to 60% at τ=0.75 scores zero marginal gain. | No guarantee, and a measurably worse heuristic. Fixed by optimising a truncated surrogate (§4). |
| "discretize every boundary into sample points and test visibility per sample" | Unnecessary. A rotational plane sweep yields the visible **arcs exactly**, in closed form. | Sampling puts a discretisation gap between our score and the organizers'; on a threshold objective that gap costs buildings at the margin. |
| "run greedy once per τ and prefix-slice for each k" | The expensive part is the visibility precompute, which is shared across **all nine** sub-problems anyway. Selection is seconds. Prefix-slicing just forfeits per-k tuning for no saving. | Left ~10–40% on the table at small k (§7). |

One thing the first draft got right and worth keeping: precompute once, solve
many. It just identified the wrong thing as the shared work.

---

## 2. The spec, verified

Pulled from the problem page and cross-checked against the figures.

- **Definition 1 (visibility):** `p` sees `q` iff segment `pq` does not intersect
  the **interior** of any building. Tangency and vertex-grazing do **not** block.
- **Definition 3 (coverage):** `C(b)` = (length of visible boundary) / (perimeter).
- **Definition 4 (service score):** `#{b : C(b) ≥ τ}`.
- **Definition 5:** place `k` points **on building boundaries** maximising that count.
- **Scoring:** per sub-problem, `your score / best score across all teams`, summed
  over 9 sub-problems. So the target is not "good" — it is "within a whisker of
  whatever the best team submits, on every one of the nine".
- **Submission:** zip with a text file (3 lines per sub-problem: `τ,k`; the `k`
  coordinates; the claimed serviced building ids) plus source and build
  instructions. Results must hold at IEEE-754 double precision.

### Sample dataset facts (measured)

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

Three data quirks worth carrying into run day:

1. **All exterior rings are clockwise**, the opposite of the GeoJSON convention.
   The sweep needs consistent orientation to know which directions point into a
   wall, so rings are normalised to counter-clockwise on load.
2. **One feature (id 9448) has a second ring** — a small hole — despite the spec
   promising none. The loader takes the largest-area ring and moves on rather
   than crashing.
3. **No two buildings share a vertex**, and no three consecutive vertices are
   exactly collinear. Both were assumptions the sweep would otherwise rely on
   silently; now they are checked.

---

## 3. The geometry that actually decides this problem

Two consequences of Definition 1 dominate everything else, and both were
confirmed against the organizers' published figures before being built on.

### 3.1 A vertex antenna covers both of its incident edges, free

The segment from a point on edge `e` to any other point on `e` runs **along the
boundary**. It never enters an interior, so by Definition 1 it is visible. An
antenna mounted at a **vertex** lies on two edges at once and therefore covers
both of them entirely, at zero marginal cost.

This is not a rounding detail. On this dataset one vertex antenna delivers a
median ~40% of its host building's perimeter immediately. Two antennas at
opposite corners of a quadrilateral cover **100%** of it.

Measured consequence — the minimum number of antennas to service a building
using only its own vertices, with no help from any neighbour:

| τ | 1 antenna | 2 | 3 | 4+ |
|---|---|---|---|---|
| 0.25 | **98.5%** | 1.5% | 0.05% | — |
| 0.50 | 59.3% | 38.3% | 2.0% | 0.4% |
| 0.75 | — | **74.8%** | 20.0% | 5.2% |

So a placement that ignores cross-building visibility entirely still services
≈ k buildings at τ=0.25 and ≈ k/2 at τ=0.75. That is the floor any serious
submission must clear, and it is why candidate sites are polygon **vertices**
rather than points sampled along edges.

There is a second, unrelated reason to prefer vertices: **a point in the interior
of an edge is essentially never exactly on that edge in doubles**. The
organizers' own Figure 5 antennas miss exact collinearity by ~1e-13. Vertex
coordinates are bit-identical to the input, so "the antenna lies on a boundary"
is exactly true rather than true-to-tolerance. (The engine still accepts a 1e-6 m
incidence tolerance, because any verifier must.)

### 3.2 A building occludes its own boundary

The flip side. From a point on building `B`, directions pointing into `B`'s
interior see nothing — the connecting segment would cross `B`'s own interior. For
a **convex** building this means an antenna on its wall sees *only* the edges it
physically lies on; the rest of its own perimeter is dark. Concave buildings can
see round their own notches, because the connecting segment passes through
exterior space.

The sweep handles this by computing the interior angular wedge at the mount point
(from the outgoing edge direction round to the reversed incoming direction) and
suppressing it. Getting this wrong in the obvious direction — excluding the host
building from occlusion, as the first draft proposed — inflates coverage by tens
of percent per building.

### 3.3 Long-range visibility is worth more than expected

Measured at τ=0.5, k=500, by capping the search radius:

| search radius | 150 m | 300 m | 600 m | 1000 m | 1500 m |
|---|---|---|---|---|---|
| service score | 4,132 | 4,524 | **4,731** | 4,725 | 4,707 |
| precompute | 0.6 s | 1.0 s | 4.7 s | 14.6 s | 31.5 s |

Quality climbs steeply to 600 m and then plateaus, while cost keeps growing
quadratically. **600 m is the operating point.** Anyone who caps at ~150 m
because "cities are dense" gives up 13%.

---

## 4. The objective is not submodular, and that matters

Let `cov_b(S)` be the covered boundary length of building `b`. `cov_b` is
monotone submodular in the antenna set `S`. But the score is

```
f(S) = #{ b : cov_b(S) ≥ τ·P_b }
```

and thresholding destroys submodularity. Greedy on `f` has no approximation
guarantee, and worse, it is *blind*: an antenna that lifts a building from 10% to
60% has zero marginal gain at τ=0.75, so greedy will never place it, even though
the building is now one antenna from paying off.

The standard repair is to optimise a **truncated surrogate**

```
g(S) = Σ_b min( cov_b(S), τ·P_b )
```

Truncating a coverage function at a constant preserves monotone submodularity, so
lazy greedy (Minoux) recovers the `(1 − 1/e)` guarantee **on g**, and `g` is
maximised exactly when every building is pushed to — but not past — its
threshold. Stale heap keys remain valid upper bounds, so lazy evaluation is
sound.

Two further refinements, both measured rather than assumed:

**Normalise per building.** `g` as written is denominated in metres, so a
1,000 m-perimeter warehouse outranks ten small houses. The score counts
buildings, not metres. Rescaling each term by `1/(τ·P_b)` — so every building
contributes at most 1 — is still submodular (a positive rescale of each term) and
better aligned with the objective.

**Reward finishing.** Define progress `u_b = min(cov_b, τP_b)/(τP_b) ∈ [0,1]` and
optimise `Σ_b u_b^p`. At `p = 1` this is the guaranteed submodular case. At
`p > 1` the function is convex in `u`, so the last stretch to the threshold is
worth more than the first — deliberately trading the guarantee for an objective
that cares about *finishing* buildings, which is what actually scores.

The best `p` depends strongly on τ, which is why it is tuned per sub-problem
(§7). Too much convexity is actively harmful: at τ=0.75, `p=2` scores 4,090 while
`p=5` scores 2,624.

### 4.1 Knowing when the solution is bad: the marginal-returns test

Scoring is relative, so "is 4,090 good?" has no internal answer — there is no
optimum to compare against and no other team's number to look at. But there *is*
a usable self-diagnostic.

Plot service score against `k` for a fixed τ. At τ=0.75 the first version of this
system produced:

| antennas | 0 → 1000 | 1000 → 2000 | 2000 → 4000 |
|---|---|---|---|
| buildings gained per antenna | 4.09 | **4.38** | 2.20 |

Marginal returns that *increase* are a tell. A best-first greedy should exhaust
its best opportunities first; if antennas 1001–2000 are each worth more than
antennas 1–1000, the early ones were misallocated. The mechanism is
complementarity: the potential rewards progress toward a threshold, so early
antennas get spent part-covering buildings the budget will never finish, and that
investment only pays off at a `k` we do not have.

This is a cheap, general test — it costs three extra runs — and it says
"keep optimising" without needing to know the optimum. It is what motivated the
next section.

### 4.2 Pricing the remaining work in antennas, not metres

The potential above measures progress in **metres of perimeter**. The budget is
denominated in **antennas**. Those are different currencies, and conflating them
is the same misallocation §4.1 diagnosed, seen from the other side: a building at
90% coverage that still needs two antennas is worth far less than one at 90% that
needs one, and `u^p` cannot tell them apart.

So price the remaining work in the currency of the constraint. For building `b`
let `reach_b` be the largest slice of its boundary any single antenna delivers —
the exchange rate between metres and antennas. Then

```
units_b = (tau*P_b - covered_b) / reach_b      antenna-units still outstanding
phi_b   = 1 / (1 + units_b)^q
```

An antenna that drags a building from three-still-needed to two now earns
something; one that polishes a building nobody will ever finish earns almost
nothing. This is cost-benefit greedy in the sense of Khuller–Moss–Naor: value
per unit of the actually-binding resource.

Measured against metre-pricing, before any polish:

| τ | k | metre-priced | antenna-priced |
|---|---|---|---|
| 0.75 | 500 | 2,254 | **2,324** |
| 0.75 | 50 | 249 | **257** |
| 0.75 | 1000 | 4,591 | **4,638** |
| 0.50 | 500 | 5,409 | **5,444** |
| 0.50 | 50 | **664** | 572 |

Neither dominates — antenna-pricing wins by 3% at (0.75, 500) and loses by 14%
at (0.5, 50) — so both are swept together with the exponent and the choice is
made **per sub-problem by measurement**. That is not a cop-out: scoring is
relative per sub-problem, so the only thing that matters is being best on each
one independently, and the sweep is cheap because the contribution map is shared.
Widening the config space this way also found configurations neither pricing
reached alone (4,677 at (0.75, 1000) versus 4,638 and 4,591).

### 4.2b What an LP relaxation does and does not tell us

Every comparison above is against our own earlier baselines, which says how much
the later work added but nothing about how much is left. The standard way to get
a real bound is a linear relaxation. Cut each boundary at every arc endpoint into
*atoms*, so coverage becomes linear:

```
max  sum_b z_b
s.t. x_a <= sum_{c covers a} y_c                     for each atom a
     sum_{a in b} len(a)*x_a >= tau*P_b*z_b
     sum_c y_c <= k ,   0 <= x,y,z <= 1
```

Solved with HiGHS on a 240-building crop (41k variables, 40k constraints) at
τ=0.75, k=20: **LP bound 204.3**, our solution **92**.

That looks alarming until you notice what the LP is actually maximising. Nothing
stops it setting `z_b = covered_b / (tau*P_b)`, so its objective is really
`sum_b min(1, cov_b/(tau*P_b))` — the *fractional* form of the surrogate. It
cannot tell "148 buildings at 70%" from "104 buildings finished". Evaluating our
own integral solution in that same currency gives **176.3** against the LP's
204.3: we are already at **86% of the LP optimum in the LP's own objective**.

So the 92-vs-204 gap is overwhelmingly the threshold/integrality gap of the
relaxation, not evidence that the solution is at 45% of optimal. **This LP is a
weak bound for this problem and should not be quoted as an optimality gap.**

The principled fix is known: **knapsack-cover inequalities** (Carr, Fleischer,
Leung, Phillips 2000), which exist precisely to repair covering LPs whose natural
relaxation is destroyed by threshold constraints. That is the next thing to try
if a real optimality gap is wanted — see §10.

### 4.3 Target-set refinement ("focus")

Stop pretending every building is reachable. Run once to learn which buildings
the budget can plausibly finish, restrict the objective to that set plus a
margin, and re-solve. Antennas then concentrate on buildings that will actually
cross the line rather than being sprinkled across ones that will not. Iterate
over several margins, keep the best.

The score still counts every serviced building, including ones outside the target
set that get finished incidentally — the mask shapes the search, it does not
narrow the reward.

Measured effect, before any polish:

| τ | k | potential | **+ focus** | gain |
|---|---|---|---|---|
| 0.75 | 1000 | 4,090 | **4,591** | +12.3% |
| 0.75 | 500 | 1,792 | **2,254** | +25.8% |
| 0.75 | 50 | 193 | **238** | +23.3% |
| 0.50 | 50 | 593 | **612** | +3.2% |
| 0.50 | 500 | 5,379 | **5,409** | +0.6% |
| 0.25 | 500 | 10,129 | 10,129 | 0 |

Exactly the shape the diagnosis predicts: the gain is concentrated where
completion-coupling binds, and vanishes at τ=0.25 where 98.5% of buildings finish
with a single antenna and there is nothing to misallocate.

### 4.4 The polish needs both neighbourhoods, not a choice between them

Focus exposed a silent bug and then a subtler trade-off, in that order.

The bug: the polish repairs a damaged solution by re-greedying, and it was
re-greedying **unfocused**. An unfocused repair can never beat a focused solution
— it just re-proposes the myopic placement focus had improved on — so the polish
reported "no gain" and quietly did nothing. Carrying the winning mask into the
repair recovered +178 at (0.75, 500).

The trade-off: carrying the mask is also a wall. It hides every building outside
the target set, and the polish can no longer discover that a building focus wrote
off is reachable after all. Measured at a 90 s budget:

| repair neighbourhood | (0.75, 500) | (0.5, 50) |
|---|---|---|
| masked (focus target set) | **2,432** | 666 |
| unmasked (all buildings) | 2,254 | **735** |
| alternating within one search | — | 708 |
| **both, keep the better** | 2,426 | 703 |

Neither neighbourhood dominates, and alternating inside a single search splits the
difference rather than taking the maximum. Running both from the same start and
keeping the winner lands within ~1% of the better one at each sub-problem without
ever collapsing to the worse one. The residual shortfall is only that each half
gets half the clock — it shrinks as the budget grows, which on run day it does.

This one is worth flagging as a process point, not just a result: the regression
at (0.5, 50) was caught **only** because an earlier run had recorded 735 for that
combo. A change that improved the headline sub-problems while quietly costing 9%
on another would otherwise have shipped looking like a win. Keep the per-combo
history.

### What did not work, and why it is still in the tree

A **bundle / ratio greedy** that directly targets `f`: for each unserviced
building compute the cheapest set of extra antennas that would finish it, then
repeatedly spend budget on the bundle with the best (buildings completed) /
(antennas used). This is the textbook partial-cover framing and it is the only
one of these methods that understands "two antennas are useless separately but
complete this building together".

It loses, badly and consistently (§7). The reason is instructive: it only ever
considers, per building, the candidate that is locally best *for that building*.
It therefore never finds the antenna that is mediocre for any single building but
excellent for twenty of them — which, in a city, is most of the good placements.
It is kept as `--algo bundle` because the comparison is the evidence for the
design, and because it is genuinely the strongest method at τ=0.75 with very
small k, where completion coupling really does dominate.

---

## 5. Architecture as built

```
GeoJSON ──▶ Scene: rings normalised CCW, arc-length parameterised,
            uniform grid index over edges
                │
                ▼
            Candidate sites = polygon vertices  (78,727)
                │
                ▼
            Contribution map — ONE rotational plane sweep per candidate,
            radius-capped, emitting exact visible arcs.  Shared by all
            nine sub-problems.                        ~5 s, 0.05 GB
                │
                ▼
            Per (τ,k):
              stage 1  rank convexity exponents with the cheap greedy
              stage 2  re-solve the top exponents with target-set refinement
              stage 3  large-neighbourhood polish, inheriting the target set
                │
                ▼
            EXACT re-evaluation  ──▶ the claimed building list
                │
                ▼
            submission.txt  ──▶  independent re-verification
```

The three-stage split matters for time allocation: ranking exponents only needs
a *ranking*, not a final answer, so it runs the cheap unfocused greedy. Focus
costs ~5× per solve and is spent only on the finalists; the polish budget goes to
the single winner.

**The visibility primitive is a rotational plane sweep**, not point sampling. For
an antenna `p`, sort every nearby edge endpoint by angle and sweep a ray through
2π, maintaining the set of edges the ray currently crosses. Over each angular
span between events, the nearest active edge is exactly what is visible — so one
sweep emits the visible sub-arcs of *every* nearby building at once, in closed
form. Cost is `O(m log m)` in the number of nearby edges.

Why this beats sampling on more than elegance: the active set is small. Average
angular measure summed over edges within 600 m works out to ~6–7 edges in front
at any given bearing, so the "which edge is nearest" scan is a short linear pass,
not a balanced-tree insertion. The whole 78,727-candidate precompute is ~5 s.

Coverage is then a union of exact arc intervals per building. No discretisation
gap between our number and the organizers'.

**Search is capped, verification is not.** The contribution map is built at 600 m
for speed; the buildings we actually *claim* are decided by a final uncapped
sweep over the chosen antennas. The submission therefore never asserts coverage
that only the approximation supported. This also means the exact score reliably
comes in *above* the search's internal estimate.

---

## 6. Validation

Two independent oracles, both wired into `make check`.

**1. The organizers' own numbers.** Figures 4 and 5 of the problem statement are
SVGs that encode the exact building polygons, the exact antenna positions, and
the per-building coverage percentage the organizers computed for each. Extracted
into a fixture, they are a ground-truth regression test:

```
figure4 (τ=0.50, k=3): all 15 coverages match, service score 8 = 8   OK
figure5 (τ=0.60, k=3): all 15 coverages match, service score 4 = 4   OK
```

All 30 published percentages reproduced to within the 0.05% that the labels are
rounded to. This is what pins the engine to the organizers' reading of
Definitions 1–4 — in particular the two subtleties in §3.

**2. An independent brute-force implementation.** Densely sample each boundary
and test every connecting segment directly against local geometry, sharing no
code path with the sweep. On random neighbourhoods of the real dataset:

```
crosscheck: 200 building/placement pairs, worst |sweep − brute| = 0.00274
```

which is within the brute force's own sampling resolution.

Writing the reference caught a bug — in the reference. A "does the segment cross
any edge?" test misses the case that matters most here: a segment from one point
on a building's boundary to another point on the *same* building, straight
through its interior, crosses no edge properly, because it starts and ends *on*
the boundary. That is exactly the self-occlusion of §3.2. Worth knowing about:
any team that validates with a naive crossing test will conclude their engine is
correct while it over-reports every convex building.

**3. Submission round-trip.** `giscup verify` re-reads the finished file, checks
every antenna is on a boundary, recomputes coverage from scratch, and compares
against the claim. Current sample submission: 9/9 blocks, **0 false claims, 0
unknown ids, 0 missed**.

---

## 7. Measured results

All on the sample dataset, radius 600 m, 30 s of polish per sub-problem, 32
threads.

### Algorithm comparison

Service score (higher is better). `selfcover` = own-vertices-only baseline;
`truncated` = the submodular surrogate a careful reading of the problem statement
naturally produces, and what the first draft of this plan described; `final` =
tuned potential + target-set refinement + dual-neighbourhood polish.

| τ | k | selfcover | bundle | truncated | **final** | final/truncated | pricing |
|---|---|---|---|---|---|---|---|
| 0.25 | 50 | 522 | 619 | 2,104 | **2,330** | 1.11 | metre |
| 0.25 | 500 | 3,464 | 4,725 | 9,705 | **10,466** | 1.08 | metre |
| 0.25 | 1000 | 5,906 | 8,444 | 12,525 | **12,788** | 1.02 | metre |
| 0.50 | 50 | 194 | 215 | 469 | **725** | 1.55 | antenna |
| 0.50 | 500 | 2,166 | 2,434 | 4,731 | **5,756** | 1.22 | antenna |
| 0.50 | 1000 | 4,258 | 5,117 | 8,971 | **9,874** | 1.10 | antenna |
| 0.75 | 50 | 49 | 104 | 70 | **298** | 4.26 | antenna |
| 0.75 | 500 | 698 | 1,070 | 1,262 | **2,586** | 2.05 | antenna |
| 0.75 | 1000 | 1,395 | 2,381 | 3,398 | **5,037** | 1.48 | antenna |

The pricing column is itself a finding: metre-pricing is chosen at every τ=0.25
sub-problem and antenna-pricing at every τ=0.5 and τ=0.75 one. At τ=0.25 almost
every building finishes with one antenna, so there is no antenna-scarcity to
reason about and the simpler potential wins; as τ rises and buildings start
needing two or three, the currency of the constraint starts to matter.

A caveat on how to read that last column, because it is easy to oversell: it
compares this system against *our own* earlier baseline, not against another
team. Under the competition's relative scoring a `truncated` submission would
earn **6.58 / 9** against ours — but that is a statement about how much the
later work added, not evidence that 5,037 is near-optimal at (0.75, 1000).
There is no external reference point — and §4.2b shows the obvious way to get
one, an LP relaxation, does not work for this objective. §4.1 is the substitute.

What the table does say clearly is *where* the work matters. τ=0.25 is nearly
saturated (12,788 of 12,860 at k=1000, 99.4%) and will likely be a near-tie
across serious teams, worth almost nothing competitively. The sub-problems that
separate the field are **(0.75, 50)**, **(0.75, 500)** and **(0.75, 1000)**,
where method choice swings the score by 1.5–4×. Budget run-day compute there.

### Tuned exponent per sub-problem

| τ \ k | 50 | 500 | 1000 |
|---|---|---|---|
| 0.25 | 8.0 metre | 6.0 metre | 8.0 metre |
| 0.50 | 8.0 ant | 3.0 ant | 3.0 ant |
| 0.75 | 3.0 ant | 2.0 ant | 1.0 ant |

Monotone structure: higher τ wants *less* convexity. The sweep is cheap, so it
runs per sub-problem rather than trusting this table — but the table is a decent
prior if time is short.

### Polish

Large-neighbourhood search frees antennas that provably hold no building above
threshold, then re-spends the budget under both neighbourhoods of §4.4. Gains at
a 150 s budget are consistent and largest where it matters: **+272** at
(0.25,500), **+416** at (0.5,1000), **+360** at (0.75,1000). It keeps the best
solution seen, so it is safe to stop at any moment.

### Submission integrity

The claimed building list is re-derived by an **uncapped** sweep before writing,
not by the capped radius the search used — the cap is a speed-up, not a scoring
decision. Skipping that step left 4 serviceable buildings unclaimed across the
nine blocks. Independent re-verification of the final file:

```
9/9 blocks, 0 false claims, 0 unclaimed-but-serviceable, 0 unknown ids
```

---

## 8. Cost and scaling

On the 12,860-building sample, 32 threads:

| stage | time |
|---|---|
| load + index | 0.05 s |
| contribution map (radius 600) | 4.7 s |
| selection, per sub-problem, k=1000 | ~2 s |
| exponent sweep (8 values), per sub-problem | ~12 s |
| exact uncapped verification, k=1000 | ~1 s |
| **all nine sub-problems, tuned + 30 s polish each** | **~7 min** |

Memory: 0.05 GB for the contribution map.

### Scale test

Synthetically tiled datasets (`tools/scale_dataset.py`), one sub-problem
(τ=0.5, k=1000), single exponent, no polish:

| dataset | buildings | edges | contribution map | total | peak RSS |
|---|---|---|---|---|---|
| sample | 12,860 | 78,727 | 4.7 s | 7 s | 0.6 GB |
| 4× | 51,440 | 314,908 | 16.7 s | 30 s | 2.5 GB |
| 16× | 205,760 | 1,259,632 | 78.8 s | 174 s | 7.6 GB |

The contribution map scales essentially **linearly** in building count — a
per-candidate sweep costs what local density says it costs, not what the total
extent says. Extrapolating, a 1M-building dataset is roughly 8 minutes of
precompute and ~40 GB, which fits comfortably.

The term that grows faster is the **exact uncapped verification**, which is
`O(k · E log E)` because every antenna sweeps against every edge. That is what
pushed the 16× selection column to 94 s. Measured fix: verification at a large
but finite radius is bit-identical to uncapped here —

| verify radius | ∞ | 5000 m | 3000 m | 2000 m | 1200 m | 600 m |
|---|---|---|---|---|---|---|
| score (τ=0.5, k=1000) | 9,322 | 9,322 | 9,322 | **9,322** | 9,308 | 9,212 |
| time | 1.6 s | 1.7 s | 1.5 s | 0.8 s | 0.4 s | 0.2 s |

so `--verify-radius 2500` halves verification cost at zero accuracy loss. **This
must be re-checked on the real dataset** (run one sub-problem both ways and
confirm the scores match) before relying on it — it is a property of this city's
geometry, not a theorem.

The parameters that trade quality for time, in the order to reach for them:
`--lns-sec`, then the `--powers` list, then `--verify-radius`, then `--radius`.

---

## 9. Run-day playbook (15–16 Aug)

The whole point of the preceding work is that 15 Aug should be boring.

**Before the drop**
- `make portable && make check` — figure oracle and crosscheck both green.
- Confirm the source zip builds from clean checkout with no third-party deps.

**When the dataset lands**
1. `./giscup verify --data <new>.geojson --out /dev/null` fails fast on a
   surprise; instead just load it and read the banner: building count, edge
   count, and any warning about grazing-collinear walls or multi-ring features.
2. **Confirm the id property name.** The sample uses `"id"`; the loader also
   accepts `ID`, `building_id`, `fid`, `OBJECTID`, `osm_id` and falls back to
   1-based ordinals. Getting this wrong invalidates every claimed-building line,
   so check the first block of the output against the raw file by eye.
3. **Confirm the submission format** against whatever the organizers publish
   alongside the data — the page says the exact format will be specified then.
   `write_submission` is one function; adjust and re-run.
4. Sanity-run on a 2,000-building crop first. Two minutes, and it catches a
   pathological CRS or a degenerate geometry before an hour is burnt.
5. Full run: `./giscup solve --data <new>.geojson --tau <given> --k <given>
   --radius 600 --lns-sec <budget> --out submission.txt`. If the dataset is large
   enough that verification dominates, first confirm `--verify-radius 2500`
   reproduces the uncapped score on one sub-problem (§8), then use it throughout.
6. `./giscup verify --data <new>.geojson --out submission.txt` — **must** print
   `SUBMISSION OK` with zero false claims. This is the gate; do not submit
   without it.
7. Spend the remaining hours raising `--lns-sec` on the three sub-problems that
   separate the field (§7), re-verifying each time. Best-so-far is always on
   disk, so an overrun costs nothing.

**Failure modes to watch**
- Dataset much larger than expected → drop `--radius` to 300 first (costs ~4%),
  not the polish budget.
- Memory pressure → raise `--min-frac`; measured to be nearly free up to 0.05.
- Anything claiming a building it cannot serve → `verify` catches it; never
  hand-edit the claimed list.

---

## 10. Ideas not yet exhausted

Ranked by expected value against remaining effort. The first two are the ones
worth doing before 15 Aug.

1. **Parallel multi-start (GRASP).** This is the top item, because selection is
   currently *single-threaded* — the 32 cores are saturated during the
   contribution precompute and then sit idle for the entire search. Every
   sub-problem starts from one deterministic greedy; running many randomised
   starts concurrently and keeping the best is close to free wall-clock. It also
   directly addresses the measured weakness above: the two polish
   neighbourhoods currently split one budget, and with real parallelism both
   could have the whole clock.
2. **Knapsack-cover inequalities for a usable upper bound.** §4.2b showed the
   natural LP relaxation is nearly worthless as a quality measure -- our solution
   already attains 86% of it in the LP's own currency, because the LP cannot
   reward finishing a building. Knapsack-cover inequalities (Carr–Fleischer–
   Leung–Phillips) are the standard repair for covering LPs with exactly this
   defect. Without them there is no honest optimality gap for this problem, only
   comparisons against our own baselines. An alternative that sidesteps the LP
   entirely: solve a *tiny* instance (≈40 buildings, k=3) exhaustively and
   measure the true gap there.
3. **Re-run the marginal-returns test (§4.1) on the final configuration.** It is
   the only optimality signal available without a competitor baseline. If the
   buildings-per-antenna curve is still rising at the operating `k`, budget is
   still being misallocated and there is more to take. If it has flattened, the
   remaining gap is candidate quality, not search — which points at item 4
   instead.
4. **A completion-aware polish move.** LNS currently frees redundant antennas and
   re-greedies. A targeted move — for each building just below τ, find the single
   cheapest candidate that would finish it, and swap it against a provably
   redundant antenna — attacks precisely the (0.75, small k) regime where the
   spread between methods is widest.
5. ~~**Edge-interior candidate sites.**~~ **Tested, and the answer is no.**
   Adding candidates every 15 m and every 8 m along edges:

   | spacing | candidates | τ=0.75, k=1000 | τ=0.5, k=500 | precompute |
   |---|---|---|---|---|
   | vertices only | 78,727 | **4,090** | **5,379** | 3.7 s |
   | + every 15 m | 97,430 | 4,085 | 5,379 | 5.8 s |
   | + every 8 m | 146,189 | 4,088 | 5,379 | 8.4 s |

   No gain at either threshold, for ~2× the candidates and ~2× the precompute.
   This is exactly what §3.1 predicts: an edge-interior point covers one edge
   where a vertex covers two, so it starts a full edge-length behind and rarely
   makes that up on external visibility. Vertices-only is not a shortcut, it is
   the right candidate set.

   The principled version of this idea — restricting viewpoints to an arrangement
   of "critical constraints" (lines through pairs of nearby vertices, intersected
   with the host edge) rather than to a uniform grid — remains untested, but the
   negative result above makes it a low-probability bet.
6. **Exact per-building completion by ILP.** For the buildings that matter,
   completion is a tiny set-cover instance; greedy set cover is used now. An
   exact solve would tighten the bundle machinery — but that machinery is the one
   that lost, so this is speculative.
7. **Exactly-collinear grazing walls.** The sweep drops non-incident edges that
   are exactly edge-on, since they subtend zero angle. Measured absent from the
   sample dataset, and a counter is wired in so a different dataset would surface
   it rather than silently losing length. If the counter fires on 15 Aug, those
   walls need an explicit grazing pass.
