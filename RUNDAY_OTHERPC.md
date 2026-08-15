# GIS Cup 2026 — Run-Day Instructions (other PC)

Target machine: **Ubuntu 24.04 LTS · Intel i9-11900F (8 cores / 16 threads)**.
Real Linux + a modern multi-core CPU is exactly what this pipeline wants — the
GPU (RTX 3090) is not used, this is CPU-only C++.

Follow the sections top to bottom. Every `<PLACEHOLDER>` must be replaced with a
real value. Commands assume you start in your home directory.

> **The one rule that outranks everything:** upload a *verified* submission to
> EasyChair **early** (even a cheap one), then replace it with better versions.
> EasyChair is updatable until the deadline. A submitted-good file beats an
> unsubmitted-great one. The worst defect found in development was a *formatting*
> error that scored zero with every internal check green — so trust the gates in
> steps 7–8, not the solver's own output.

---

## 0. Switch GitHub to YOUR account (shared machine — do this first)

```bash
# install prerequisites
sudo apt-get update && sudo apt-get install -y gh git g++ make python3 zip unzip

# who is currently logged in on this shared machine?
gh auth status || true

# sign the current person OUT (if prompted, choose github.com)
gh auth logout --hostname github.com 2>/dev/null || gh auth logout || true

# wipe any cached git credentials left behind (old token in keyring / file)
git credential-cache exit 2>/dev/null || true
rm -f ~/.git-credentials 2>/dev/null || true
git config --global --unset credential.helper 2>/dev/null || true

# log in as YOU
gh auth login
#   → GitHub.com  →  HTTPS  →  authenticate Git: Yes  →
#     "Login with a web browser" (a browser opens; or type the one-time code)
gh auth setup-git

# set YOUR commit identity (replace with your own)
git config --global user.name  "<YOUR_GITHUB_USERNAME>"
git config --global user.email "<YOUR_GITHUB_EMAIL>"

# CONFIRM it is now your account before continuing
gh auth status
gh api user --jq .login          # must print YOUR username
```

---

## 1. Clone the code (your private repo)

```bash
cd ~
gh repo clone <YOUR_GITHUB_USERNAME>/giscup-2026 giscup && cd giscup
```

## 2. Build (uses all 16 threads automatically)

```bash
make
./test_figures tests/figures_groundtruth.txt        # must print: ALL FIGURE TESTS PASSED
free -h                                              # note total RAM (for the memory rule)
```

## 3. Get the eval dataset + parameters (public evaluator repo — no auth)

```bash
git clone https://github.com/alowe/gis-cup-2026-evaluator ../evaluator
ls ../evaluator | grep -iE 'competition|dataset|parameters'

# set these two to the REAL file paths you see above:
EVAL=../evaluator/GIS-cup-competition-dataset.geojson
RAW=../evaluator/competition-parameters.txt
```

## 4. Normalize the parameters into the 9 (tau, k) blocks

```bash
python3 tools/parse_params.py "$RAW" > params.txt
cat params.txt          # expect 9 lines "tau k"
```

If the parser refuses, it is telling you the file is not a layout it recognises —
**read the file** and pass values explicitly instead (do not guess); e.g.
`--tau 0.25,0.5,0.75 --k 50,500,1000` on the solve/export commands below.

## 5. Readiness check + re-tune radii on the REAL geometry (GO / NO-GO)

```bash
bash tools/runday.sh "$EVAL" "$RAW"
```

This inspects loader quirks, runs the input-robustness suite against the *real*
file, **re-derives `--radius` and `--verify-radius` on the real geometry**, and
runs the marginal-returns diagnostic. It prints **GO** or **NO-GO** and the two
radii. Do NOT reuse the sample's radii — they are properties of one city.

```bash
R=<radius from runday output> ; V=<verify-radius from runday output>
```

**Memory rule:** projected RAM ≈ `0.05 GB × (buildings / 1000)` (building count is
printed by the inspect step). If that exceeds ~75% of `free -h`, add
`--min-frac 0.02` to the solve commands in step 6.

## 6. Solve all 9 blocks — archive-ratcheted (keeps the per-block best)

```bash
rm -rf archive

# winning config: 2-exchange + destroy+plateau. Big polish budget — you have the window.
./giscup solve --data "$EVAL" --params params.txt \
    --radius $R --verify-radius $V --lns-sec 600 --swap 400 \
    --lns-destroy --swap-plateau 8 --archive-add --method dpl --out /dev/null

# plain baseline too (wins some low-variance blocks; the archive takes the max)
./giscup solve --data "$EVAL" --params params.txt \
    --radius $R --verify-radius $V --lns-sec 600 --swap 400 \
    --archive-add --method base --out /dev/null

# OPTIONAL, if you have hours: wider radius + a second seed (bigger radius sometimes wins)
for RR in $((R+500)) $((R+1000)); do
  ./giscup solve --data "$EVAL" --params params.txt --radius $RR --verify-radius $V \
      --lns-sec 600 --swap 400 --lns-destroy --swap-plateau 8 --seed 2 \
      --archive-add --method dpl-r$RR --out /dev/null
done
```

**Do NOT use `--alns` or `--tabu`.** Both were measured and rejected
(FINDINGS §5.24) — ALNS loses at scale, tabu fails the correctness gate.

Every `--archive-add` run can only ratchet the archive upward, so a bad radius or
seed is simply never exported. If the machine is interrupted mid-solve, nothing is
lost — whatever finished is in the archive; jump straight to step 7.

## 7. Assemble + verify (always from the archive, never a single run)

```bash
./giscup archive export-submission --data "$EVAL" --params params.txt --out results.txt
./giscup verify   --data "$EVAL" --out results.txt      # want: false=0  missed=0  on all 9
python3 tests/conformance.py results.txt 9              # official parser rules, 0 warnings
```

The export prints the winning method per block, so you can see which
radius/config won each. `verify` re-derives every claim with an exact, uncapped
sweep; `conformance.py` checks the file against the official evaluator's parser.

## 8. Package for EasyChair

```bash
bash tools/package.sh results.txt giscup2026-submission.zip 9
```

It refuses to write the archive unless conformance **and** `make check` pass.
Output: `giscup2026-submission.zip` (contains `results.txt` + full source).

## 9. Upload — early, then improve

Upload `giscup2026-submission.zip` to **EasyChair (conf=giscup2026)** the moment
step 8 passes. Then re-run steps 6–8 with more budget / more radii and re-upload
the improved archive. Submissions are updatable until the deadline.

## 10. (Optional) Official grader — final gate before the last upload

If Node is available:

```bash
cd ../evaluator && npm install --no-audit
npx vite-node ../giscup/tools/official_eval.mjs -- . "$EVAL" ../giscup/results.txt
cd ../giscup
```

Slow (~25+ min at sample scale, more for a larger city) — use it as the *final*
gate only, not in a loop. If it can't finish in time, ship the internally-verified
file: our engine agreed with the official grader on 51,844/51,844 claims (§5.20).

---

## When finished on this shared machine

```bash
gh auth logout --hostname github.com     # don't leave your token on a shared PC
```

## Troubleshooting

| symptom | action |
|---|---|
| `parse_params.py` refuses the file | read it; pass `--tau`/`--k` explicitly. Do not edit the parser. |
| `runday.sh` says NO-GO on the id property | loader fell back to sequential ids — confirm they match the file's own ids before trusting any claim. |
| precompute slow / RAM pressure | apply the memory rule (step 5): add `--min-frac 0.02`. Radius is **not** the memory lever. |
| a solve is killed mid-run | nothing lost — the archive holds every finished block; go to step 7. |
| score looks implausibly high | `giscup verify` is the arbiter, never the solver's own printed number. |
| official grader too slow at the end | ship the internally-verified file; it is the final gate, not a hard requirement. |

## Notes

- The eval dataset may be a different size / city than the sample — that is exactly
  why step 5 re-tunes radii and checks memory on the real file rather than trusting
  our sample numbers.
- The i9-11900F has 16 threads; precompute and selection parallelise across them,
  so large `--lns-sec` budgets are cheap here. The 2-exchange polish itself is
  serial, so its runtime is whatever `--lns-sec` you set.
- Full method write-up and every measured number: `FINDINGS.md`. Run-day playbook
  in prose: `plan.md` §9.
