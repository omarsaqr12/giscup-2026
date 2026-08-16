# GIS Cup 2026 — second-machine work order

You are a SECOND machine joining a run day already in progress. Another machine
(PC1) is running the k=9 experiments right now. Your job is the k=49 and k=484
columns. Do not duplicate PC1's work.

**Hard deadline: send results back by 13:00 UTC.** Competition closes 16:00 UTC.

## Situation

A verified submission is already uploaded and safe (42,100 buildings serviced,
9/9 clean verify, conformance 0 warnings). Everything you do is upside only —
if your runs produce nothing, we lose nothing.

Current best per block (this is what you must beat):

| tau  | k   | current |
|------|-----|---------|
| 0.32 | 9   | 1399    |
| 0.32 | 49  | 4035    |
| 0.32 | 484 | 16004   |
| 0.49 | 9   | 800     |
| 0.49 | 49  | 2531    |
| 0.49 | 484 | 10458   |
| 0.68 | 9   | 252     |
| 0.68 | 49  | 1154    |
| 0.68 | 484 | 5553    |

Read `FINDINGS.md` §5.25 through §5.25.3 before starting. Settled, do NOT re-test:

- **Radius: r3000 is the answer.** Totals by radius 38,634 → 40,487 → 41,485 →
  41,976 → 42,060; the last delta is +84, a genuine turnover. Exception:
  (0.68, 484) prefers r2500.
- **Exponent: flat above ~16.** Extended to 512; scores barely move and tau=0.68
  actively collapses when pushed high. Do not sweep exponents.
- **GRASP restarts: zero effect at k>=49.** Do not sweep restarts on your blocks.
- **`--finalists`: null result** (+6 total over 78 min). Leave at default.
- **`--alns` and `--tabu`: rejected** (§5.24). Never enable.
- **Edge-interior candidates: rejected** (§5.2). Never enable `--edge-spacing`.

## Setup

```bash
# 1. repo (WARNING: reset --hard discards local changes; your copy is stale
#    and has nothing unique, but check `git status` first if unsure)
cd ~ && git clone https://github.com/omarsaqr12/giscup-2026.git giscup 2>/dev/null
cd ~/giscup && git fetch origin && git checkout main && git reset --hard origin/main

# 2. dataset (public repo, no auth needed)
git clone https://github.com/alowe/gis-cup-2026-evaluator ../evaluator 2>/dev/null
EVAL=../evaluator/datasets/GIS-cup-competition-dataset.geojson
ls -l "$EVAL"        # must exist, ~13 MB

# 3. build and prove the engine is sane
make
./test_figures tests/figures_groundtruth.txt    # must print ALL FIGURE TESTS PASSED
free -h                                          # need ~2 GB free; it uses ~0.25 GB
```

`params.txt` is already in the repo (the nine tau/k pairs). Do not regenerate it.

## What to run

Run these **sequentially, in this order** — never two solves at once, they share
the archive. Each writes its own output file. Note precompute at r3000 costs
~21 min per invocation on an i9-11900F (there is no precompute cache), so expect
each run to be that plus its solve time.

### Run 1 — long polish at k=484 (highest value, do this first)

k=484 is the only column where polish budget reliably pays: it gained +466 going
from 20 s to 300 s, and has never been given more than 300 s. This is the single
biggest untested lever left.

```bash
./giscup solve --data "$EVAL" --tau 0.32,0.49,0.68 --k 484 \
    --radius 3000 --verify-radius -1 \
    --lns-sec 1800 --swap 400 --lns-destroy --swap-plateau 8 \
    --powers 3,4,6,8,12,16,32,96 \
    --archive-add --method pc2-longpolish --out exp1.txt
```

### Run 2 — shortlist attack at k=49

`--swap 400` was chosen for a k=50 world on the *sample*. At k=49 on this
instance the neighbourhood is far larger than 400 and has never been widened.
This mirrors the k=9 work PC1 is doing, one size up.

```bash
./giscup solve --data "$EVAL" --tau 0.32,0.49,0.68 --k 49 \
    --radius 3000 --verify-radius -1 \
    --lns-sec 900 --swap 20000 --lns-destroy --swap-plateau 8 \
    --powers 3,4,6,8,12,16,32,96 \
    --archive-add --method pc2-swap20k --out exp2.txt
```

### Run 3 — (0.68, 484) at its preferred radius, with long polish

This is the one block that prefers r2500 over r3000. It has never had both its
preferred radius and a long budget at the same time.

```bash
./giscup solve --data "$EVAL" --tau 0.68 --k 484 \
    --radius 2500 --verify-radius -1 \
    --lns-sec 2400 --swap 400 --lns-destroy --swap-plateau 8 \
    --powers 2.5,3,4,6,8 \
    --archive-add --method pc2-r2500-long --out exp3.txt
```

### Run 4 — only if time remains before 13:00 UTC

```bash
./giscup solve --data "$EVAL" --tau 0.32,0.49,0.68 --k 49 \
    --radius 3000 --verify-radius -1 \
    --lns-sec 900 --swap 100000 --lns-destroy --swap-plateau 8 \
    --powers 3,4,6,8,12,16,32,96 \
    --archive-add --method pc2-swap100k --out exp4.txt
```

Run each detached so a closed terminal cannot kill hours of compute:

```bash
setsid nohup bash -c '<the command above>' > run1.log 2>&1 < /dev/null &
```

## Sending results back

Do this **as each run finishes** — do not wait for all of them.

```bash
cd ~/giscup
git checkout -b pc2-results 2>/dev/null || git checkout pc2-results
git add -f exp*.txt
git commit -m "pc2 experiment outputs"
git push -u origin pc2-results        # force-push is fine on re-push: git push -f
```

Then tell PC1 which files are on the branch and what scores you saw.

Do **not** push to `main` and do **not** open a PR — PC1 is committing to main
continuously and you will collide.

## Rules

1. Never hand-edit a results file. Everything enters through the solver.
2. Do not modify any source file. A formatting bug that passes every internal
   check can still zero all nine blocks (§5.20). New flag values only.
3. Report the numbers the solver prints, including runs that lost. A measured
   negative is a result; a guess is not.
4. If a run is killed mid-way, nothing is lost — completed blocks are already in
   your local archive. Just report what finished.

PC1 re-verifies every imported placement with an exact uncapped sweep before it
can enter the submission, so an over-claiming or malformed file is caught rather
than trusted. Send whatever you have by 13:00 UTC.
