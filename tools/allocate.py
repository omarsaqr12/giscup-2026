#!/usr/bin/env python3
"""Allocate a wall-clock budget across the sub-problems, from measured variance.

Scoring is `ours / best` summed over independently scored sub-problems, so a
second of polish is worth what it buys in *relative* score. The split must
therefore follow how much each block actually responds to effort.

Earlier versions hardcoded that response from the sample's nine blocks. That was
a latent trap: the contest publishes its own tau and k values, and a table keyed
to 0.25/0.5/0.75 x 50/500/1000 silently mis-allocates the entire window if the
real values differ. Weights are now *derived*, from a cheap two-point probe of
whatever blocks are handed to it:

    weight(block) = (polished - cheap) / polished

i.e. the fraction of the block's score that came from spending time on it. A
near-saturated block responds barely at all and is cheap to serve; a block where
method choice swings the answer responds strongly and earns the clock.

    python3 tools/allocate.py HOURS --params canonical.txt [--variance probe.tsv]

`probe.tsv` is `tau<TAB>k<TAB>cheap<TAB>polished`, produced by
`tools/runday.sh` (it runs the probe as part of readiness). Without it, the
allocator falls back to an equal split and says so -- it will not invent a
prior.
"""
import argparse
import sys

MIN_POLISH = 30.0
TUNE_OVERHEAD = 25.0


def load_blocks(path):
    out = []
    for line in open(path):
        parts = line.split()
        if len(parts) >= 2:
            out.append((float(parts[0]), int(float(parts[1]))))
    return out


def load_variance(path):
    w = {}
    for line in open(path):
        if line.startswith("#"):
            continue
        f = line.replace("\t", " ").split()
        if len(f) < 4:
            continue
        tau, k, cheap, pol = float(f[0]), int(float(f[1])), float(f[2]), float(f[3])
        w[(tau, k)] = max(0.0, (pol - cheap) / pol) if pol > 0 else 0.0
    return w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hours", type=float)
    ap.add_argument("--params", required=True, help="canonical 'tau k' per line")
    ap.add_argument("--variance", help="tau k cheap polished, per block")
    ap.add_argument("--precompute", type=float, default=300.0)
    ap.add_argument("--reserve", type=float, default=0.25)
    ap.add_argument("--data", default="<DATA>")
    ap.add_argument("--radius", default="<RADIUS>")
    ap.add_argument("--verify-radius", default="<VRAD>")
    a = ap.parse_args()

    blocks = load_blocks(a.params)
    if not blocks:
        print("no blocks in params file", file=sys.stderr)
        return 2

    if a.variance:
        w = load_variance(a.variance)
        missing = [b for b in blocks if b not in w]
        if missing:
            print(f"variance file is missing {len(missing)} block(s): {missing}", file=sys.stderr)
            return 2
        source = "measured"
    else:
        w = {b: 1.0 for b in blocks}
        source = "EQUAL SPLIT (no --variance given; no prior assumed)"

    floor = max(w.values()) * 0.02 if max(w.values()) > 0 else 1.0
    w = {b: max(v, floor) for b, v in w.items()}

    total = a.hours * 3600
    reserve = total * a.reserve
    usable = total - reserve - a.precompute - TUNE_OVERHEAD * len(blocks)
    if usable <= 0:
        print(f"budget too small: {a.hours}h leaves nothing after precompute + reserve")
        return 1

    wsum = sum(w.values())
    alloc = {b: max(MIN_POLISH, usable * w[b] / wsum) for b in blocks}
    over = sum(alloc.values()) - usable
    if over > 0:
        free = {b: v for b, v in alloc.items() if v > MIN_POLISH}
        fsum = sum(free.values()) or 1.0
        for b in free:
            alloc[b] = max(MIN_POLISH, alloc[b] - over * alloc[b] / fsum)

    print(f"budget          {a.hours:.1f} h ({total:,.0f}s)   weights: {source}")
    print(f"  reserve       {reserve:,.0f}s   precompute {a.precompute:,.0f}s   "
          f"tuning {TUNE_OVERHEAD*len(blocks):,.0f}s")
    print(f"  polish        {usable:,.0f}s over {len(blocks)} blocks\n")
    print(f"{'tau':<8}{'k':<8}{'weight':>9}{'polish':>10}")
    for b in sorted(blocks, key=lambda x: -w[x]):
        print(f"{b[0]:<8g}{b[1]:<8d}{w[b]*100:>8.1f}%{alloc[b]:>9.0f}s")
    print()
    print("# runbook -- each line ratchets into the archive; safe to interrupt")
    for b in sorted(blocks, key=lambda x: -w[x]):
        print(f"./giscup solve --data {a.data} --tau {b[0]:g} --k {b[1]} \\\n"
              f"    --radius {a.radius} --verify-radius {a.verify_radius} \\\n"
              f"    --lns-sec {alloc[b]:.0f} --swap 400 --archive-add "
              f"--method runday --out /dev/null")
    print(f"\n./giscup archive export-submission --data {a.data} "
          f"--params {a.params} --out submission.txt")
    print(f"python3 tests/conformance.py submission.txt {len(blocks)}")
    return 0


sys.exit(main())
