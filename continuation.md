# Task brief: GIS Cup 2026 — final experiment round

You are working in the SIGSPATIAL 2026 GIS Cup repository (antenna placement on
building boundaries). Before writing any code, read `FINDINGS.md` end to end,
then `plan.md` and `README.md`. `FINDINGS.md` is the measured experiment log and
it is authoritative — it records which ideas worked, which failed, and *why*.
Several plausible-sounding approaches have already been tried and measured as
failures (GRASP multi-start at scale, the LP relaxation as a bound, knapsack-cover
inequalities, edge-interior candidate sites). Do not re-litigate those.

## Situation and constraints

- The evaluation dataset publishes **15 Aug 2026**; submission closes **16 Aug 2026**
  in a **24-hour window with no second attempt**.
- Nine independently scored sub-problems (3 τ × 3 k). Score per sub-problem is
  `ours / best submission`, summed. **Per-sub-problem tuning is free** — a method
  that only wins at one (τ, k) is still worth shipping for that block alone.
- Per `FINDINGS.md` §6, the competition is decided at **(0.75, 50)**,
  **(0.75, 500)**, **(0.75, 1000)** and secondarily τ=0.5 at k≥500. τ=0.25 is
  near-saturated (99.5% at k=1000) and is worth almost nothing competitively.
  Spend effort accordingly.
- Time is the binding constraint. Prefer a working measured improvement over an
  elegant unfinished one.

## Non-negotiable rules

1. **`make check` and `tests/robustness.sh` must pass after every task.** These are
   the figure oracle (§4.1) and the brute-force crosscheck (§4.2). If a change
   breaks them, the change is wrong — do not adjust the tests to fit.
2. **Do not modify the visibility engine's semantics.** §3.1 (a vertex antenna
   covers both incident edges) and §3.2 (a building occludes its own boundary) are
   validated against the organizers' published per-building numbers. If you believe
   the engine is wrong, stop and report rather than editing it.
3. **Every new method is opt-in behind a flag and defaults OFF** until it has been
   measured to win on a specific sub-problem.
4. **Never report an improvement you have not measured.** Every claim goes in a
   table with the command that produced it, in the style of `FINDINGS.md`.
5. **Keep per-combo history.** §5.8 records a change that improved headline
   sub-problems while silently costing 9% at (0.5, 50), caught only because an
   earlier run had recorded 735. Always re-measure the full set, never just the
   sub-problem you are optimising.
6. Work one task at a time, commit per task, and report measured numbers before
   moving on.

---

## Task 0 — Solution archive (do this first, ~1 hour)

A persistent, verified archive of every placement ever produced, keyed by
`(τ, k)`, storing the placement, its **independently verified** score, the method
and config that produced it, and a timestamp.

- New placements are inserted only after passing `giscup verify`.
- The submission writer assembles each of the nine blocks from the **archive
  best**, never from whatever the final pipeline run happened to emit.
- Add `giscup archive` subcommands: `add`, `best`, `list`, `export-submission`.

Rationale: this makes the whole remaining effort a ratchet — every experiment
below, including the ones that fail, can only improve the submission. It also
structurally prevents the §5.12 failure mode where an integration overwrote a
tuned configuration and shipped a 7% regression from something meant to be a pure
maximum.

**Acceptance:** archive populated with the current §6 results; `export-submission`
reproduces a valid 9-block file that passes `giscup verify` with 0 false claims,
0 unclaimed-but-serviceable, 0 unknown ids.

---

## Task 1 — Run-day hardening

The largest risk is not algorithmic, it is a silent failure on unseen data inside
a one-shot window.

1. **`tools/rundayreadme.sh`** (or equivalent single entry point) that, given a new
   dataset path, runs in order: loader quirk checks (§2 — ring orientation, multi-ring
   features, id property name/type), the robustness suite against the *real* file,
   the radius sweep (§5.3), the verify-radius sweep (§5.3), and the marginal-returns
   diagnostic (§5.6). It prints a go/no-go summary.
2. **Re-tune radius and verify-radius on the eval data, do not inherit 600/2500.**
   §5.3 explicitly warns the 2000 m verify-radius equivalence is a property of this
   city's geometry, not a theorem. A too-small cap silently truncates quality with
   no error raised. Make both sweeps a first-class command with a short runtime.
3. **Threshold margin guard.** §4.5 found the <1e-12 bucket empty but 660 claims
   within 1e-6 at τ=0.5. Add `--claim-epsilon`, and set it to require margin
   **≥ 1e-9** (which §4.5 says costs ~1 claim). Do **not** default to 1e-6 — that
   would sacrifice hundreds of legitimate claims to guard against a discrepancy the
   figure oracle in §4.1 gives no evidence for. Measure and report the exact cost.
4. **Compute budget allocator**: given a wall-clock budget for the window, allocate
   polish time per sub-problem weighted toward τ=0.75 and τ=0.5/k≥500, and run
   τ=0.25 cheaply.

**Acceptance:** one command takes an unseen GeoJSON to a verified 9-block
submission with all diagnostics logged, and the whole path has been exercised on
at least two mutated datasets that were never used during development.

---

## Task 2 — Beam search construction

Current construction is myopic single-antenna greedy; 2-exchange (§5.13) only
repairs pair-blindness *after* the fact. Attack it during construction.

- Maintain a beam of width `B` partial placements. Extend each not only by the
  argmax single antenna but by the strongest **pair** completions.
- Feed every beam leaf through the existing 2-exchange polish; insert all results
  into the archive.
- Flag: `--beam B --beam-pairs N`.

Note explicitly why this is not a repeat of §5.12's failure: GRASP sampled
randomly, and at k=50 over 78,727 candidates 128 restarts covered a vanishing
corner of the space. A beam is deterministic breadth with diversity preserved
along the whole trajectory, so its coverage does not decay with candidate count
the same way.

**Validate on:** the `giscup exact` k≤3 oracle (§5.11 — must stay at ratio 1.00),
then measure on (0.75, 50), (0.75, 500), and (0.5, 50) as the regression canary.

**Acceptance:** measured table of beam width vs score vs runtime on the three
deciding sub-problems. Keep only if it wins somewhere; keep per-sub-problem.

---

## Task 3 — Focused MIP: exact solve and local branching (TIME-BOXED)

**Hard kill criterion: if this is not beating the 2-exchange baseline at
(0.75, 50) by the end of day one, stop and drop it.** It is the highest-upside and
highest-risk item.

§5.9 correctly killed the *LP* as a bound, because with fractional `z_b` the
relaxation takes partial credit and cannot distinguish "148 buildings at 70%" from
"104 finished". That is a defect of the *relaxation*, and it is exactly what
branch-and-bound fixes: with `z_b` binary, the threshold is enforced by
integrality. So the same model that is useless as a bound may be useful as a
**solver**. This is a different object from what §5.9 tested — do not skip it on
the strength of that section.

**Critical model reduction (this is what makes it feasible at all):** never build
the model over the full dataset. §5.9's 240-building crop already produced 41k
variables; the full instance would be millions of atoms.

- Restrict to the **focused instance** (§5.7 target set plus margin), with
  candidate antennas restricted to those within the tuned radius of that set.
- For any building whose coverage provably cannot change under the move being
  considered, **fix `z_b` to its incumbent value and delete its atom variables and
  constraints.**

Two modes, both warm-started from the current best placement so the result can
never be worse than what you would otherwise ship:

- **Mode A — free solve.** Time-capped MIP on the focused instance. Try in order:
  (0.75, 50), (0.5, 50), then (0.75, 500) only if the first two solve comfortably.
- **Mode B — local branching** (Fischetti & Lodi 2003). Add the Hamming ball
  constraint around incumbent `S`:
  `Σ_{j∈S}(1 − y_j) + Σ_{j∉S} y_j ≤ r`, together with `Σ_j y_j = k`.
  With the cardinality constraint, `r = 2` is the provably optimal **single swap
  over all candidates** — strictly stronger than the shortlisted 2-exchange of
  §5.13 — and `r = 4` is the optimal **double swap**, i.e. exactly the pair move
  §5.11's oracle identified as the missing capability, solved exactly rather than
  heuristically. Escalate `r` while budget allows. Model size scales with `r`, not
  with `|C|`, which is why this transfers where GRASP did not.

HiGHS is already a dependency via `tools/lp_bound.py`. Try SCIP if HiGHS presolve
proves weak on this structure.

**Second deliverable, equally valuable:** the branch-and-bound **dual bound on the
focused instance is a true optimality gap on the real objective** (because `z_b` is
binary). This restores the measurement §7 item 1 says has been lost, and it is
what tells you whether Tasks 2 and 4 are adding value or polishing something
already optimal. Report the gap for every sub-problem where the MIP runs. If it
delivers this, it likely makes a bespoke k=4–5 branch-and-bound oracle redundant —
but if the MIP path dies on the kill criterion, fall back to building that oracle,
because without *some* real bound there is no signal left.

**Acceptance:** for at least (0.75, 50), either a placement that beats the archive
best, or a proven optimality gap on the focused instance. Both is ideal.

---

## Task 4 — Lagrangian completion pricing (only if Tasks 0–3 are done)

§5.5's antenna-priced potential uses `reach_b` = the largest single-antenna slice
as the metre↔antenna exchange rate. That is a one-antenna approximation of what is
often a multi-antenna completion.

1. **Better potential:** replace `reach_b` with the *actual* cheapest antenna set
   that lifts `b` to τ — a tiny exact set-cover per building, tractable because only
   the handful of antennas that see `b`'s walls are involved.
2. **Distinct constructor:** dualize the budget `Σ y ≤ k` with multiplier λ; each
   building independently answers "is my cheapest completion profitable at price λ
   per antenna?"; sweep λ to hit `k`. This respects the threshold natively — each
   building is completed or not, no partial credit — so it explores the space very
   differently from truncated-greedy and may win on the completion-coupled τ=0.75
   blocks.

Treat as a portfolio contributor: keep per sub-problem by measurement, feed
everything into the archive.

---

## Measurement protocol (applies to every task)

- Primary sub-problems: **(0.75, 50)**, **(0.75, 500)**, **(0.75, 1000)**.
- Regression canaries that must be re-measured every time: **(0.5, 50)** (the §5.8
  9% regression), **(0.25, 500)** (the §7 item 3 case where 2-exchange costs a hair).
- Correctness gate for any new search method: `giscup exact` on the tiny instances,
  ratio to optimum must stay at 1.00.
- Every result goes into the archive and into a `results/` table with its command.
- Report in the voice of `FINDINGS.md`: state what was tried, the measured numbers,
  and whether it is kept or rejected — **including failures, with the mechanism**.
  A well-diagnosed failure is a real deliverable here; several sections of the
  existing log are exactly that and they are what stopped later effort being wasted.

## Final deliverable

A `FINDINGS.md` update (new numbered sections continuing §5) covering each task
attempted, plus an updated §6 results table and §7 open-questions list, and a
run-day playbook in `plan.md` that a tired person can follow at 3 a.m. without
making a judgement call.