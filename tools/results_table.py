#!/usr/bin/env python3
"""Render the measured results table for plan.md from a solve log.

Reads results/solve_sample.txt (the `giscup solve` stdout) plus the earlier
algorithm benchmark, and emits the markdown table so the plan never drifts from
what was actually run.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_solve(path):
    """Final score per (tau, k): the last line for that combo wins."""
    out = {}
    for line in pathlib.Path(path).read_text().splitlines():
        f = line.split()
        if len(f) < 5 or not re.match(r"^[\d.]+$", f[0]):
            continue
        tau, k, _pow, exact = float(f[0]), int(f[1]), float(f[2]), int(f[3])
        out[(tau, k)] = (exact, _pow)
    return out


def read_bench(path):
    """Score per (tau, k, algo) from the four-algorithm benchmark."""
    out = {}
    for line in pathlib.Path(path).read_text().splitlines():
        f = line.split()
        if len(f) < 4 or not re.match(r"^[\d.]+$", f[0]):
            continue
        out[(float(f[0]), int(f[1]), f[2])] = int(f[3])
    return out


def main():
    solve = read_solve(ROOT / "results/solve_sample.txt")
    bench = read_bench(ROOT / "results/bench_r600.txt")

    print("| τ | k | selfcover | bundle | truncated | **final** | final/truncated |")
    print("|---|---|---|---|---|---|---|")
    total = 0.0
    for tau in (0.25, 0.5, 0.75):
        for k in (50, 500, 1000):
            fin, _ = solve.get((tau, k), (0, 0))
            sc = bench.get((tau, k, "selfcover"), 0)
            bu = bench.get((tau, k, "bundle"), 0)
            tr = bench.get((tau, k, "truncated"), 0)
            ratio = fin / tr if tr else 0
            total += tr / fin if fin else 0
            print(f"| {tau} | {k} | {sc:,} | {bu:,} | {tr:,} | **{fin:,}** | {ratio:.2f} |")
    print()
    print(f"Relative score a `truncated` submission would earn against ours: "
          f"**{total:.2f} / 9**")
    print()
    print("Tuned exponent per sub-problem:")
    print()
    print("| τ \\ k | 50 | 500 | 1000 |")
    print("|---|---|---|---|")
    for tau in (0.25, 0.5, 0.75):
        row = " | ".join(f"{solve.get((tau, k), (0, 0))[1]:.1f}" for k in (50, 500, 1000))
        print(f"| {tau} | {row} |")


main()
