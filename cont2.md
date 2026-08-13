# Task brief: GIS Cup 2026 — final round before the evaluation drop

## Mission

**The goal is to win the competition.** Not to explore ideas, not to build elegant
tools — to have the highest total score across nine sub-problems scored
`ours / best submission`. Every decision below follows from three consequences of
that scoring:

1. **A catastrophic failure on one block costs more than any algorithm gains.**
   Being near-best on eight blocks and zero on one loses the cup. Robustness and
   run-day discipline outrank marginal quality everywhere.
2. **Points live where methods separate.** High-τ blocks swing 1.5–5.6× with
   method choice; near-saturated low-τ blocks will be a near-tie across all
   serious entries. Spend compute where the variance is.
3. **The archive ratchets, so experiments are asymmetric bets.** A verified
   placement can only raise the submission, never lower it. Run everything,
   promote nothing by hand.

Today is **13 Aug 2026**. The evaluation dataset — **together with the actual
three τ values and three k values, which are NOT promised to equal the sample's**
— publishes **15 Aug at 16:00 UTC (18:00 CEST)** as
`GIS-cup-competition-dataset.geojson` plus `competition-parameters.txt` **in the
organizers' evaluator repository** (github.com/alowe/gis-cup-2026-evaluator), so
run day begins with a `git pull`. Submission closes **16 Aug 16:00 UTC (18:00
CEST)** via **EasyChair** (conf=giscup2026) as a single .zip/.tgz/.tar/.gz
archive containing both the nine-block results file and the source directory
with build/run instructions. **Code freeze is the evening of 14 Aug.** Day 1 is
for building, Day 2 is for measuring and rehearsing, Day 3 is for executing a
playbook, not for judgement calls.

## Read first, and what is already settled

Read `FINDINGS.md` end to end before writing any code, then `plan.md` and
`README.md`. It is the authoritative record. Do **not** re-litigate anything it
has measured and rejected:

- Construction-side diversification is dead three times over: GRASP (§5.12),
  beam search (§5.17), Lagrangian pricing (§5.19). §7.6's conclusion stands:
  construction is not the bottleneck; the repair operator, the radii, and the
  bound are.
- The focused MIP (§5.18) **fails its own correctness gate** — it reported a
  proven-optimal 22 on an instance whose true optimum is 20 and whose placement
  scores 18. It stays disabled. Do not debug it this round, do not cite its dual
  bound for anything, ever.
- The engine's semantics (§3.1, §3.2) are pinned to the organizers' published
  figures. Never modify them; if you believe they are wrong, stop and report.
- **The organizers have published their actual grading code** — the GIS Cup 2026
  evaluator (github.com/alowe/gis-cup-2026-evaluator, hosted at
  alowe.github.io/gis-cup-2026-evaluator). They commit to consistency with it,
  reserving only bug fixes. It is therefore the de facto ground truth, above the
  figure oracle. Its measured semantics (verified by reading `src/core`):
  **no radius cap anywhere** — every building is an occluder for every antenna;
  coverage is computed **only for claimed buildings**, so an unclaimed
  serviceable building scores zero (§4.4's design is load-bearing, not
  paranoia); the verdict is the raw double comparison
  `visibleLengthMeters >= tau * perimeterMeters` — length currency, exact
  equality passes; spatial tolerance **0.001 m** — antennas within 1 mm snap to
  the boundary, farther ones are silently dropped (the claim can then fail);
  only the **first k antennas** of a line are read, and a valid later antenna
  never replaces an invalid earlier one; duplicates are deduplicated, unknown
  ids ignored with warnings; sweep tie tolerance 1e-9 m; edges incident to or
  within tolerance of the antenna route to an ArcGIS-backed classifier — the
  degenerate zone where our exact-arithmetic engine is most likely to differ.
- **The sample dataset was corrected**: building 9448's inner ring was the
  organizers' mistake and the fixed file is published. Re-download it, confirm
  our loader's largest-ring handling made our effective geometry identical, and
  re-run every gate against the corrected file before trusting any number.
- Vertices-only candidate sites stay (§5.2 measured no gain from edge-interior
  sites). The 1 mm snap tolerance makes edge-interior antennas *valid* now, but
  validity was never the measured reason — do not re-litigate.
- `--claim-epsilon` stays at 1e-9 as internal hygiene (§5.16: 0 lost claims;
  1e-6 costs 671), but it is no longer the last line of defence — the official
  evaluator itself is (Task 0-B). Internal verify must mirror the grader's
  exact expression, `visible >= tau * perimeter`, in that operand form — a
  ratio-form comparison can differ in the last ulp.

## Non-negotiable rules

1. `make check` and `tests/robustness.sh` pass after every task. Never adjust a
   test to fit a change.
2. Every new mechanism is opt-in behind a flag, default off, until measured.
3. Nothing enters the archive without passing `giscup verify`; the submission is
   assembled from archive bests only (§5.15 semantics).
4. Every claim of improvement appears in a table with the command that produced
   it, in the voice of `FINDINGS.md` — failures included, with mechanism.
5. Full re-measurement every time: primaries **(0.75, 50), (0.75, 500),
   (0.75, 1000)**; regression canaries **(0.5, 50)** (the §5.8 9% incident) and
   **(0.25, 500)** (the §7.3 swap cost). Never just the block being optimised.
6. Any new search mechanism must hold ratio 1.00 on the `giscup exact` k≤3
   oracle instances before it is measured at scale.
7. One task at a time, commit per task, report numbers before moving on.

---

## Task 0 — τ/k are runtime inputs, not constants (DO THIS FIRST, TODAY)

The competition page states the eval publishes *both* the buildings *and* the
three τ and three k values, and explicitly encourages testing other values —
i.e. **0.25/0.5/0.75 × 50/500/1000 are sample examples, not the contest.** Every
piece of the pipeline that silently assumes them is a loaded gun aimed at all
nine blocks at once.

1. Audit the entire tree for hardcoded τ/k literals: the auto-tuner, `allocate.py`
   (its 31.8%/1.0% split is a function of the sample values), `runday.sh`,
   archive keys, submission writer, any τ-conditional logic. Everything must take
   the nine (τ, k) pairs as run-day inputs.
2. Add a robustness case that runs the **full path** — tune → solve → polish →
   archive → export → verify — with alien values, e.g. τ ∈ {0.4, 0.7, 0.9},
   k ∈ {100, 750, 2000}, on the sample geometry. It must produce a verified
   9-block submission with zero manual edits.
3. Re-express §6's "where the competition is decided" as a *rule*, not a list:
   at run time, rank the nine actual (τ, k) blocks by measured method-variance
   (e.g. truncated-vs-best ratio from a quick pass) and let `allocate.py` derive
   the compute split from that, fresh, on the real values.

4. The nine (τ, k) pairs arrive on run day as `competition-parameters.txt` in
   the evaluator repo. Write a parser for it (mirror the official
   `solution-parser`'s `(tau, k)` conventions), make it the single source the
   whole pipeline reads, and include a parameters *file* — not flags — in the
   alien-values robustness case.

**Acceptance:** the alien-values robustness case passes end-to-end from a
parameters file; grep shows no load-bearing sample literals; `allocate.py`
derives splits from measurements of whatever values it is handed.

## Task 0-B — Official-evaluator conformance (CO-FIRST with Task 0)

The grading code is public and runnable. Every hour spent making our verdicts
provably identical to *its* verdicts is worth more than any algorithm, because a
disagreement discovered after submission is unfixable and a disagreement
discovered now is a bug fix.

1. **Vendor and pin.** Clone the evaluator, pin the commit hash,
   `pnpm install --frozen-lockfile`, record the evaluator and ArcGIS versions in
   `FINDINGS.md`. On run day, pull and re-pin — the organizers may push fixes.
2. **Headless harness.** The core is plain Node-importable (their own
   benchmarks run it under vitest without a browser). Write a small script that
   takes (dataset.geojson, submission.txt), runs dataset-loader →
   solution-parser → submission-validator → evaluation-engine with
   **fullDiagnosticCoverage on**, and emits per-building coverage JSON plus the
   verified score. Measure its runtime at 12,860 buildings × k=1000.
3. **Conformance gates, added to `make check`:** the repo's own fixtures pass
   under our harness (the 50-antenna sample submission: τ=0.5, k=50, 140
   claims, 0 warnings; the full-evaluation benchmark; the six ui-smoke cases —
   which pin exact-equality passing, 0.0005 m snap, 0.002 m rejection, dedup,
   and first-k truncation). Our submission writer's output must parse in the
   official parser with **zero warnings**, exactly k antennas per block,
   full-precision round-trip coordinates.
4. **Differential test, the heart of the task:** per-building coverage, our
   engine vs the official harness, on every archive-best submission across the
   nine sample blocks, plus targeted degenerate cases — antennas at vertices of
   claimed buildings, grazing/tangent rays, near-collinear geometry — probing
   the 1 mm tolerance and the ArcGIS incident-edge classifier. Bucket the
   differences (exact / <1e-9 / <1e-6 / larger). **Any verdict flip (serviced
   vs not) is a stop-the-line finding**: diagnose it before any other task
   continues, and if our engine cannot be made to agree, the official verdict
   wins by definition.
5. **Run-day claim policy:** the final claim filter before export is the
   official harness itself — claim exactly the buildings *it* verifies, for
   each block. If its runtime is too slow for inner loops, it remains the final
   filter with our engine (now differentially validated) as the inner loop.

**Acceptance:** conformance gates green in `make check`; a differential report
across all nine blocks with zero unexplained verdict flips; `export-submission`
runs the official filter and the exported file re-verifies through the official
harness with zero warnings.

## Task 1 — Per-sub-problem radius sweep (start today, runs in background)

§7.4, with evidence already in hand: 1500 m beat 1000 m at (0.75, 500) even with
a 60 s polish handicap (2,893 vs 2,874).

1. A harness that sweeps search radius over {1000, 1500, 2000} per sub-problem
   at full polish budget, archives every verified result, and reports the
   per-block winner. Include a with/without `--swap` axis (this subsumes §7.3 —
   the archive picks the winner, no conditional logic needed).
2. Wire the sweep into `runday.sh` so the same map is re-derived **on the real
   geometry and the real (τ, k) values** — §5.16's core lesson is that a tuned
   constant is only valid for the configuration and city it was tuned under.
3. Verify-radius stays re-derived per dataset as `tune` already does (§5.16
   showed 2500 was unsafe on the very geometry it was tuned for). The official
   evaluator is **uncapped** — verified in its source — so any internal cap is
   a speed-up that must remain provably equivalent to uncapped, and Task 0-B's
   final filter catches it if it ever is not.

**Acceptance:** archive bests improve or hold on every swept block; the sweep is
a single command with a wall-clock budget parameter, present in the run-day path.

## Task 2 — Randomized-destroy LNS + plateau moves (the one algorithmic build)

Rationale, so it is not confused with the three dead construction ideas: all
three failed because polish recovers from any reasonable start, so diversity
*before* polish is wasted. This adds diversity *of* the polish. §5.13 itself
notes the existing destroy only frees antennas "provably holding nothing up",
and 2-exchange is a radius-1 move — so **coupled multi-antenna replacement is
the one move class nothing in the stack can currently make.** It is the
heuristic successor to what §5.18's r≥4 local branching was supposed to deliver
before the model failed its gate.

1. **Destroy-repair loop around the converged incumbent.** After polish
   converges: remove ⌈ρk⌉ chosen antennas with ρ ∈ {0.05, 0.10, 0.15}, by two
   operators — (a) uniform random, (b) spatial cluster (a random chosen antenna
   plus its m nearest chosen neighbours). Rebuild with the tuned constructor,
   run repair under **both** masks and keep the better (§5.8), re-polish with
   2-exchange, keep-best incumbent, archive every verified candidate. Seeded,
   time-budgeted, so it can run for minutes or hours — this is the intended
   run-day compute sink. Flag: `--lns-destroy`.
2. **Plateau moves in 2-exchange.** The objective is a step function; strict
   improvement stalls on its plateaus. Accept score-*equal* swaps that raise a
   secondary progress measure `Σ_b min(cov_b, τ·P_b)`, with a bounded chain
   length between strict improvements to prevent cycling. Flag:
   `--swap-plateau N`.
3. **Sweep the never-measured knobs:** 2-exchange shortlist size and pass count
   at 2–3 settings each, at fixed wall-clock.

Gates: exact-oracle ratio stays 1.00 for every variant; measure primaries and
canaries per rule 5. Honest framing for the report: this is the fourth
plausible idea in a log that kills plausible ideas — parts 2 and 3 are
near-free either way, and part 1 is the bet. If part 1 loses at equal
wall-clock, record the mechanism and default it off, exactly as §5.17 did.

**Acceptance:** a measured table (operator × ρ × budget) on the three
primaries; canaries clean; keep/reject decision per block recorded in
`FINDINGS.md`.

## Task 3 — Polish-time scaling curve (cheap; it dictates run-day allocation)

§6 used 150 s per sub-problem. Run day offers hours. Measure 150 / 600 / 1800 s
(baseline polish, and with Task 2's loop if it survives) on the τ=0.75 trio.

- Still climbing at 1800 s → the window itself is the best remaining algorithm;
  allocation should be re-derived at realistic budgets and the τ=0.75 trio gets
  the bulk of 24 hours.
- Flat by 600 s → run-day hours belong to radius variants and LNS restarts
  instead, and the schedule should say so.

**Acceptance:** the curve is in `FINDINGS.md` and `allocate.py`'s run-day
recommendation cites it.

## Task 4 — Dress rehearsal, packaging, freeze (14 Aug, then stop)

1. **Full rehearsal on geometry the pipeline has never touched** — the 4× tiled
   dataset is ideal because it also stress-tests memory (2.5 GB at 4×, 7.6 GB at
   16×, §5.14) — run with **alien τ/k values**, via `runday.sh` alone, through to
   a verified 9-block file. No manual intervention allowed; anything that needed
   a human hand becomes a bug to fix.
2. **Memory decision rule, written down:** projected RSS from building count
   (§5.14 scaling), and the pre-decided fallback (radius cap, verify cap,
   streaming order) if the eval city threatens RAM. A rule, not a judgement
   call.
3. **Packaging step to the EasyChair spec:** one command produces a single
   .zip (or .tgz/.tar/.gz) containing the nine-block results file **and** the
   source directory with build/run instructions — both in the same archive, as
   required. The results file must pass the official parser with zero warnings
   before packaging.
4. **Write the 3 a.m. playbook into `plan.md`,** in CEST local times for an
   18:00→18:00 window: `git pull` the evaluator repo (re-pin the commit) →
   read `competition-parameters.txt` → inspect → robustness-on-real-file →
   tune (both radii, per-block) → allocate → solve per-block → Task 2 loop on
   the high-variance blocks → export through the official filter (Task 0-B) →
   package → upload. **Upload a verified conservative archive early in the
   window, then replace it with improved versions** — EasyChair submissions
   are normally updatable until the deadline, and a good-but-submitted score
   beats a great-but-unsubmitted one. Expected durations and go/no-go checks
   on every step. Then **freeze**.

**Acceptance:** rehearsal passes end-to-end twice (two different alien
configurations); the playbook has been executed once exactly as written.

## Task 5 — ONLY if Tasks 0–4 are done early: the k=4–5 oracle

§7.1's route and no other: exhaustive enumeration **over the validated
visibility engine** with branch-and-bound pruning — bound = current score +
count of buildings still completable within the remaining budget — on the
tiny40/tiny70 instances. Purpose: does 2-exchange (+ Task 2) still hold ratio
1.00 at k=4–5, or is there a next mechanism? The §5.11→§5.13 chain is the
precedent for why this can pay. The §5.18 lesson is why it must be built on the
engine, not on a second model. If time does not allow, skip without guilt — it
is an instrument, not points.

## Kill order if time runs short

Sacrifice in this order: Task 5 → Task 2 part 1 (keep parts 2–3) → Task 3 →
Task 1's larger sweep values. **Never** sacrifice Task 0, Task 0-B, or Task 4 —
those are the win conditions. Under relative scoring, the team that ships nine
clean blocks whose claims the official grader confirms beats the team with a
brilliant algorithm and one disagreement.

## Deliverables

New numbered `FINDINGS.md` sections in the established voice (measured tables,
commands, mechanisms for failures) — including the evaluator-conformance
differential report with its version pins — an updated §6 on the corrected
sample as configs improve, an updated §7, and the frozen run-day playbook in
`plan.md`.