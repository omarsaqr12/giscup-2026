#!/usr/bin/env python3
"""LP relaxation of the service-score problem -- an upper bound on the optimum.

Everything else in this repo compares the solver against other heuristics, which
says how much the later work added but nothing about how much is left. This says
how much is left.

Formulation. Cut each building's boundary at every arc endpoint any candidate
contributes, giving *atoms* -- maximal intervals that are either wholly visible
from a given antenna or wholly not. Coverage then becomes linear in the atoms:

    maximise    sum_b z_b
    subject to  x_a  <=  sum_{c covers a} y_c        for every atom a
                sum_{a in b} len(a) * x_a  >=  tau * P_b * z_b
                sum_c y_c  <=  k
                0 <= x, y, z <= 1

Any integral solution maps to a feasible LP point with the same objective, so
the LP optimum upper-bounds the true optimum. It is a relaxation in two places
(fractional antennas, and atoms coverable "partially"), so the bound is loose --
but a loose upper bound still brackets the answer, which is more than we had.

    python3 tools/lp_bound.py dump.txt TAU K
"""
import sys
import numpy as np
import scipy.sparse as sp
from scipy.optimize import linprog


def load(path):
    per, arcs = [], []
    with open(path) as f:
        n = int(f.readline().split()[1])
        for _ in range(n):
            _id, p = f.readline().split()
            per.append(float(p))
        ncand = int(f.readline().split()[1])
        narc = int(f.readline().split()[1])
        for _ in range(narc):
            c, b, s0, s1 = f.readline().split()
            arcs.append((int(c), int(b), float(s0), float(s1)))
    return np.array(per), ncand, arcs


def main():
    path, tau, k = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
    per, ncand, arcs = load(path)
    nb = len(per)

    # --- cut each boundary into atoms at every arc endpoint ------------------
    cuts = [set([0.0, p]) for p in per]
    for _c, b, s0, s1 in arcs:
        cuts[b].add(s0)
        cuts[b].add(s1)
    atom_of = []        # per building: sorted cut positions
    atom_base = [0] * (nb + 1)
    for b in range(nb):
        xs = sorted(cuts[b])
        atom_of.append(xs)
        atom_base[b + 1] = atom_base[b] + max(0, len(xs) - 1)
    natom = atom_base[nb]

    atom_len = np.zeros(natom)
    for b in range(nb):
        xs = atom_of[b]
        for i in range(len(xs) - 1):
            atom_len[atom_base[b] + i] = xs[i + 1] - xs[i]

    # --- atom <- candidate incidence ----------------------------------------
    rows, cols = [], []
    for c, b, s0, s1 in arcs:
        xs = atom_of[b]
        lo = np.searchsorted(xs, s0 - 1e-12)
        hi = np.searchsorted(xs, s1 - 1e-12)
        for i in range(lo, hi):
            rows.append(atom_base[b] + i)
            cols.append(c)
    cover = sp.coo_matrix((np.ones(len(rows)), (rows, cols)),
                          shape=(natom, ncand)).tocsr()

    # Atoms nobody can see are dead weight; drop them.
    seen = np.asarray(cover.sum(axis=1)).ravel() > 0
    keep = np.flatnonzero(seen)
    cover = cover[keep]
    atom_len_k = atom_len[keep]
    atom_bld = np.zeros(natom, dtype=np.int64)
    for b in range(nb):
        atom_bld[atom_base[b]:atom_base[b + 1]] = b
    atom_bld_k = atom_bld[keep]
    na = len(keep)

    # Variable order: [y (ncand)] [x (na)] [z (nb)]
    NY, NX, NZ = ncand, na, nb
    N = NY + NX + NZ

    # x_a - sum_c y_c <= 0
    A1 = sp.hstack([-cover, sp.identity(na, format="csr"),
                    sp.csr_matrix((na, nb))], format="csr")
    b1 = np.zeros(na)

    # tau*P_b*z_b - sum_a len(a) x_a <= 0
    Ax = sp.coo_matrix((atom_len_k, (atom_bld_k, np.arange(na))), shape=(nb, na))
    A2 = sp.hstack([sp.csr_matrix((nb, ncand)), -Ax.tocsr(),
                    sp.diags(tau * per)], format="csr")
    b2 = np.zeros(nb)

    # sum_c y_c <= k
    A3 = sp.hstack([sp.csr_matrix(np.ones((1, ncand))),
                    sp.csr_matrix((1, na + nb))], format="csr")
    b3 = np.array([float(k)])

    A = sp.vstack([A1, A2, A3], format="csr")
    bub = np.concatenate([b1, b2, b3])
    c = np.zeros(N)
    c[NY + NX:] = -1.0  # maximise sum z

    print(f"LP: {N:,} vars ({ncand:,} antennas, {na:,} atoms, {nb:,} buildings), "
          f"{A.shape[0]:,} constraints")
    res = linprog(c, A_ub=A, b_ub=bub, bounds=(0, 1), method="highs")
    if not res.success:
        print("LP failed:", res.message)
        return
    print(f"tau={tau} k={k}   LP upper bound on optimum = {-res.fun:.1f}"
          f"   (of {nb} buildings)")


main()
