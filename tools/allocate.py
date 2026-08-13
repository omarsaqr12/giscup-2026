#!/usr/bin/env python3
"""Allocate a wall-clock budget across the nine sub-problems, and emit the runbook.

Scoring is `ours / best` summed over nine independently scored sub-problems, so a
second of polish is worth what it buys in *relative* score, not in buildings. The
weights below are therefore the measured relative gain the polish delivered per
sub-problem on the sample (FINDINGS.md §6 run, `lns +N` column over final score),
not a guess:

    (0.75,   50)  +120 / 377    = 31.8%      <- most sensitive
    (0.75,  500)  +539 / 2863   = 18.8%
    (0.50,   50)  +165 / 846    = 19.5%
    (0.75, 1000)  +714 / 5391   = 13.2%
    (0.50,  500)  +631 / 6075   = 10.4%
    (0.50, 1000)  +660 / 10116  =  6.5%
    (0.25,   50)   +82 / 2371   =  3.5%
    (0.25,  500)  +267 / 10461  =  2.6%
    (0.25, 1000)  +123 / 12802  =  1.0%      <- near-saturated, worth little

The pattern is the one §6 predicts: τ=0.25 is ~99.5% saturated and will be a
near-tie across serious entries, so it is where you spend least.

    python3 tools/allocate.py HOURS [--precompute SEC] [--tau ...] [--k ...]
"""
import argparse
import sys

# Measured relative polish gain (see docstring). Re-derive on the real dataset
# if its saturation profile differs -- `tools/runday.sh` reports enough to tell.
WEIGHT = {
    (0.25, 50): 3.5, (0.25, 500): 2.6, (0.25, 1000): 1.0,
    (0.50, 50): 19.5, (0.50, 500): 10.4, (0.50, 1000): 6.5,
    (0.75, 50): 31.8, (0.75, 500): 18.8, (0.75, 1000): 13.2,
}
MIN_POLISH = 30.0     # below this the polish barely gets a destroy-repair round in
TUNE_OVERHEAD = 25.0  # exponent sweep + finalists, per sub-problem, order of


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hours", type=float)
    ap.add_argument("--precompute", type=float, default=300.0,
                    help="seconds for the shared contribution map (scales with dataset size)")
    ap.add_argument("--reserve", type=float, default=0.25,
                    help="fraction held back for export, verification and mistakes")
    ap.add_argument("--data", default="<DATA>")
    ap.add_argument("--radius", default="<RADIUS>")
    ap.add_argument("--verify-radius", default="<VRAD>")
    a = ap.parse_args()

    total = a.hours * 3600
    reserve = total * a.reserve
    usable = total - reserve - a.precompute - TUNE_OVERHEAD * 9
    if usable <= 0:
        print(f"budget too small: {a.hours}h leaves nothing after "
              f"{a.precompute:.0f}s precompute + {reserve:.0f}s reserve")
        return 1

    wsum = sum(WEIGHT.values())
    alloc = {kk: max(MIN_POLISH, usable * w / wsum) for kk, w in WEIGHT.items()}
    # Renormalise after the floor, so the floors do not silently overspend.
    over = sum(alloc.values()) - usable
    if over > 0:
        free = {kk: v for kk, v in alloc.items() if v > MIN_POLISH}
        fsum = sum(free.values())
        for kk in free:
            alloc[kk] = max(MIN_POLISH, alloc[kk] - over * alloc[kk] / fsum)

    print(f"budget            {a.hours:.1f} h  ({total:,.0f}s)")
    print(f"  reserve         {reserve:,.0f}s  ({a.reserve*100:.0f}% for export, verify, mistakes)")
    print(f"  precompute      {a.precompute:,.0f}s  (shared by all nine)")
    print(f"  tuning overhead {TUNE_OVERHEAD*9:,.0f}s")
    print(f"  polish budget   {usable:,.0f}s\n")
    print(f"{'tau':<6}{'k':<7}{'weight':>8}{'polish':>10}")
    for (tau, k), w in sorted(WEIGHT.items(), key=lambda x: -x[1]):
        print(f"{tau:<6}{k:<7}{w:>7.1f}%{alloc[(tau,k)]:>9.0f}s")
    print(f"{'':<13}{'':>8}{sum(alloc.values()):>9.0f}s total\n")

    print("# runbook -- one line per sub-problem, each ratcheting into the archive")
    for (tau, k) in sorted(alloc, key=lambda x: (x[0], x[1])):
        print(f"./giscup solve --data {a.data} --tau {tau} --k {k} \\\n"
              f"    --radius {a.radius} --verify-radius {a.verify_radius} \\\n"
              f"    --lns-sec {alloc[(tau,k)]:.0f} --swap 400 --archive-add \\\n"
              f"    --method 'runday' --out /dev/null")
    print(f"\n# assemble from archive best, never from the last run")
    print(f"./giscup archive export-submission --data {a.data} --out submission.txt")
    print(f"./giscup verify --data {a.data} --out submission.txt")
    return 0


sys.exit(main())
