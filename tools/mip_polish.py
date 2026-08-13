#!/usr/bin/env python3
"""Focused MIP polish: exact reallocation of a window of antennas.

FINDINGS.md §5.9 killed the *LP* as a bound, and correctly: with `z_b`
fractional the relaxation takes partial credit and cannot tell "148 buildings at
70%" from "104 finished". That is a defect of the relaxation, and it is exactly
what branch-and-bound repairs -- with `z_b` binary the threshold is enforced by
integrality. So the same model that is useless as a bound can still be useful as
a *solver*. This is a different object from what §5.9 tested.

What makes it tractable is never building the model over the full dataset. Two
reductions:

  * a **spatial window** -- the antennas outside it stay pinned at their
    incumbent positions, and only buildings the window can actually influence
    enter the model at all;
  * **atom pruning** -- boundary atoms are cut only at arc endpoints of the
    candidates that survive the window, so a building contributes a handful of
    atoms rather than hundreds.

Two modes:

  free           reallocate every antenna inside the window optimally
  localbranch    Fischetti & Lodi (2003) Hamming ball around the incumbent,
                 sum_{j in S}(1-y_j) + sum_{j not in S} y_j <= r, with sum y = k.
                 r=2 is the optimal single swap over the modelled candidates,
                 r=4 the optimal double swap -- the pair move §5.11's oracle
                 identified as the missing capability, solved rather than
                 heuristically approximated. Model size scales with the window,
                 not with r.

Both are warm-startable from the incumbent and the result is only ever accepted
if it beats it, so this can never make the shipped solution worse.

The second deliverable is the **dual bound**: because `z_b` is binary, HiGHS's
bound is a true optimality gap on the windowed objective. That is the
measurement §7 says was lost when 2-exchange saturated the k<=3 oracle.

    python3 tools/mip_polish.py dump.txt placement.txt TAU K [options]
"""
import argparse
import sys
import time

import numpy as np
import scipy.sparse as sp
from scipy.optimize import milp, LinearConstraint, Bounds


def load_dump(path):
    per, cand_xy, arcs = [], [], []
    with open(path) as f:
        nb = int(f.readline().split()[1])
        for _ in range(nb):
            _id, p = f.readline().split()
            per.append(float(p))
        nc = int(f.readline().split()[1])
        for _ in range(nc):
            x, y = f.readline().split()
            cand_xy.append((float(x), float(y)))
        na = int(f.readline().split()[1])
        for _ in range(na):
            c, b, s0, s1 = f.readline().split()
            arcs.append((int(c), int(b), float(s0), float(s1)))
    return np.array(per), np.array(cand_xy), arcs


def measure(iv):
    if not iv:
        return 0.0
    iv = sorted(iv)
    tot, cs, ce = 0.0, iv[0][0], iv[0][1]
    for a, b in iv[1:]:
        if a > ce:
            tot += ce - cs
            cs, ce = a, b
        else:
            ce = max(ce, b)
    return tot + (ce - cs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("placement")
    ap.add_argument("tau", type=float)
    ap.add_argument("k", type=int)
    ap.add_argument("--mode", choices=["free", "localbranch"], default="free")
    ap.add_argument("--r", type=int, default=4, help="Hamming radius for localbranch")
    ap.add_argument("--window", type=int, default=400, help="buildings in the focused window")
    ap.add_argument("--cand-cap", type=int, default=1200, help="max candidates modelled")
    ap.add_argument("--time", type=float, default=600.0)
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    per, cand_xy, arcs = load_dump(a.dump)
    nb_all, nc_all = len(per), len(cand_xy)

    inc_xy = [tuple(map(float, l.split())) for l in open(a.placement) if l.strip()]
    lut = {xy: i for i, xy in enumerate(map(tuple, cand_xy))}
    incumbent = [lut[xy] for xy in inc_xy if xy in lut]
    if len(incumbent) != len(inc_xy):
        print(f"warning: {len(inc_xy)-len(incumbent)} incumbent antennas not in the candidate "
              f"set (different radius/min-frac?); proceeding with {len(incumbent)}")

    # --- incumbent coverage, and which buildings it already services ---------
    by_bc = {}
    for c, b, s0, s1 in arcs:
        by_bc.setdefault(b, {}).setdefault(c, []).append((s0, s1))
    inc_set = set(incumbent)
    cov_inc, serviced = {}, set()
    for b, cmap in by_bc.items():
        iv = [x for c, v in cmap.items() if c in inc_set for x in v]
        m = measure(iv)
        cov_inc[b] = m
        if m >= a.tau * per[b] - 1e-12:
            serviced.add(b)
    print(f"incumbent: {len(incumbent)} antennas, {len(serviced)} serviced "
          f"(of {nb_all} buildings)")

    # --- focus window: unserviced buildings closest to their threshold -------
    # These are where a reallocation can plausibly buy something; buildings far
    # from threshold cannot be flipped by a handful of antennas and only bloat
    # the model.
    prog = sorted(((cov_inc.get(b, 0.0) / (a.tau * per[b]), b)
                   for b in range(nb_all) if b not in serviced), reverse=True)
    window = {b for _, b in prog[:a.window]} | serviced
    # Candidates that touch the window, capped by how much of it they reach.
    touch = {}
    for c, b, s0, s1 in arcs:
        if b in window:
            touch[c] = touch.get(c, 0.0) + (s1 - s0)
    cands = sorted(touch, key=lambda c: -touch[c])[:a.cand_cap]
    cands = sorted(set(cands) | inc_set)
    cidx = {c: i for i, c in enumerate(cands)}
    cset = set(cands)

    # Buildings whose status cannot change under the modelled candidates get
    # fixed and their atoms deleted -- the single biggest size reduction here.
    blds = sorted(b for b in window if any(c in cset for c in by_bc.get(b, {})))
    bidx = {b: i for i, b in enumerate(blds)}
    fixed_serviced = len(serviced - set(blds))

    # --- atoms ---------------------------------------------------------------
    cuts = {b: {0.0, per[b]} for b in blds}
    for c, b, s0, s1 in arcs:
        if b in cuts and c in cset:
            cuts[b].add(s0); cuts[b].add(s1)
    atom_pos, atom_base, natom = {}, {}, 0
    for b in blds:
        xs = sorted(cuts[b])
        atom_pos[b] = xs
        atom_base[b] = natom
        natom += max(0, len(xs) - 1)
    atom_len = np.zeros(natom)
    atom_b = np.zeros(natom, dtype=np.int64)
    for b in blds:
        xs = atom_pos[b]
        for i in range(len(xs) - 1):
            atom_len[atom_base[b] + i] = xs[i + 1] - xs[i]
            atom_b[atom_base[b] + i] = bidx[b]

    rows, cols = [], []
    for c, b, s0, s1 in arcs:
        if b not in cuts or c not in cset:
            continue
        xs = atom_pos[b]
        lo = np.searchsorted(xs, s0 - 1e-12)
        hi = np.searchsorted(xs, s1 - 1e-12)
        for i in range(lo, hi):
            rows.append(atom_base[b] + i); cols.append(cidx[c])
    cover = sp.coo_matrix((np.ones(len(rows)), (rows, cols)),
                          shape=(natom, len(cands))).tocsr()

    NY, NX, NZ = len(cands), natom, len(blds)
    N = NY + NX + NZ
    print(f"model: {NY:,} antennas, {NX:,} atoms, {NZ:,} buildings -> {N:,} vars "
          f"({fixed_serviced} buildings fixed-serviced outside the model)")

    cons = []
    # x_a <= sum_{c covers a} y_c
    cons.append(LinearConstraint(
        sp.hstack([-cover, sp.identity(natom, format="csr"),
                   sp.csr_matrix((natom, NZ))], format="csr"), -np.inf, 0.0))
    # tau*P_b*z_b - sum len(a) x_a <= 0
    Ax = sp.coo_matrix((atom_len, (atom_b, np.arange(natom))), shape=(NZ, natom)).tocsr()
    cons.append(LinearConstraint(
        sp.hstack([sp.csr_matrix((NZ, NY)), -Ax,
                   sp.diags(a.tau * np.array([per[b] for b in blds]))], format="csr"),
        -np.inf, 0.0))
    # sum y = k  (every antenna must be placed somewhere in the window)
    ones = sp.hstack([sp.csr_matrix(np.ones((1, NY))), sp.csr_matrix((1, NX + NZ))],
                     format="csr")
    cons.append(LinearConstraint(ones, float(a.k), float(a.k)))

    x0 = np.zeros(N)
    for c in incumbent:
        if c in cidx:
            x0[cidx[c]] = 1.0

    if a.mode == "localbranch":
        # sum_{j in S}(1-y_j) + sum_{j not in S} y_j <= r
        coef = np.where(x0[:NY] > 0.5, -1.0, 1.0)
        rhs = a.r - float((x0[:NY] > 0.5).sum())
        cons.append(LinearConstraint(
            sp.hstack([sp.csr_matrix(coef.reshape(1, -1)), sp.csr_matrix((1, NX + NZ))],
                      format="csr"), -np.inf, rhs))
        print(f"local branching: Hamming radius r={a.r} around the incumbent "
              f"({'single swap' if a.r == 2 else 'double swap' if a.r == 4 else f'{a.r//2}-swap'})")

    c_obj = np.zeros(N)
    c_obj[NY + NX:] = -1.0
    integrality = np.zeros(N)
    integrality[:NY] = 1          # antennas binary
    integrality[NY + NX:] = 1     # buildings binary -- this is what §5.9 lacked

    t0 = time.time()
    res = milp(c=c_obj, constraints=cons, integrality=integrality,
               bounds=Bounds(0, 1),
               options={"time_limit": a.time, "mip_rel_gap": 0.0, "presolve": True})
    dt = time.time() - t0

    if res.x is None:
        print(f"no solution in {dt:.0f}s ({res.message})")
        return 1

    win = int(round(-res.fun))
    total = win + fixed_serviced
    inc_win = len([b for b in blds if b in serviced])
    print(f"\nwindowed objective: incumbent {inc_win} -> MIP {win}   "
          f"(total {len(serviced)} -> {total})")
    if res.mip_dual_bound is not None:
        db = -res.mip_dual_bound
        print(f"dual bound {db:.1f} on the window, gap {res.mip_gap*100:.2f}%  [{dt:.0f}s]")
        print(f"  -> at most {db + fixed_serviced:.0f} serviced with this window fixed")
    if total > len(serviced) and a.out:
        chosen = [cands[i] for i in range(NY) if res.x[i] > 0.5]
        with open(a.out, "w") as f:
            for c in chosen:
                f.write(f"{cand_xy[c][0]!r} {cand_xy[c][1]!r}\n")
        print(f"improved placement -> {a.out}  ({len(chosen)} antennas)")
    elif total <= len(serviced):
        print("no improvement over the incumbent; nothing written")
    return 0


sys.exit(main())
