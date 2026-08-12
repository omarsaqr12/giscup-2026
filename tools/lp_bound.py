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

Knapsack-cover strengthening. The relaxation above is weak for the reason
Carr, Fleischer, Leung & Phillips (SODA 2000) identified: a covering constraint
whose coefficients are large relative to the demand can be satisfied by taking a
sliver of one big item. The remedy is to contract a subset A of items, compute
the residual demand, and cap every remaining coefficient at that residual.

For building b, the coverage requirement relaxes to a knapsack cover on the
antennas directly -- if S services b then

    sum_{c in S} w_c  >=  cov_b(S)  >=  tau*P_b            (union <= sum)

where w_c is what antenna c alone delivers to b. Applying the KC construction to
that constraint, for any A subset of the antennas seeing b:

    sum_{c not in A} min(w_c, R) * y_c  >=  R * z_b,   R = tau*P_b - cov_b(A)

Two problem-specific notes. The z_b on the right is sound because the constraint
only binds when b is claimed (z_b = 0 makes it vacuous). And the residual uses
the *union* cov_b(A) rather than the generic sum(w_c for c in A); since the union
is no larger, R is no smaller, so this is strictly stronger than the textbook
form while remaining valid -- the coverage function's submodularity buys that.

    python3 tools/lp_bound.py dump.txt TAU K [--kc J]
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


def measure(iv):
    """Total length of a union of intervals."""
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
    path, tau, k = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
    kc_depth = 0
    if "--kc" in sys.argv:
        kc_depth = int(sys.argv[sys.argv.index("--kc") + 1])
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

    blocks, rhs = [A1, A2, A3], [b1, b2, b3]

    # --- knapsack-cover cuts -------------------------------------------------
    if kc_depth:
        per_bc = {}
        for c, b, s0, s1 in arcs:
            per_bc.setdefault(b, {}).setdefault(c, []).append((s0, s1))
        rows_c, cols_c, vals_c, rows_z, cols_z, vals_z, rhs_kc = [], [], [], [], [], [], []
        nrow = 0
        for b, cmap in per_bc.items():
            w = sorted(((measure(v), c) for c, v in cmap.items()), reverse=True)
            demand = tau * per[b]
            for j in range(1, kc_depth + 1):
                if j > len(w):
                    break
                A_set = [c for _wc, c in w[:j]]
                covA = measure([iv for c in A_set for iv in cmap[c]])
                R = demand - covA
                if R <= 1e-9:
                    break
                Aset = set(A_set)
                any_term = False
                for wc, c in w:
                    if c in Aset:
                        continue
                    coef = min(wc, R)
                    if coef <= 0:
                        continue
                    rows_c.append(nrow); cols_c.append(c); vals_c.append(-coef)
                    any_term = True
                if not any_term:
                    continue
                rows_z.append(nrow); cols_z.append(b); vals_z.append(R)
                rhs_kc.append(0.0)
                nrow += 1
        if nrow:
            Kc = sp.coo_matrix((vals_c, (rows_c, cols_c)), shape=(nrow, ncand))
            Kz = sp.coo_matrix((vals_z, (rows_z, cols_z)), shape=(nrow, nb))
            blocks.append(sp.hstack([Kc.tocsr(), sp.csr_matrix((nrow, na)), Kz.tocsr()],
                                    format="csr"))
            rhs.append(np.array(rhs_kc))
            print(f"  + {nrow:,} knapsack-cover cuts (depth {kc_depth})")

    A = sp.vstack(blocks, format="csr")
    bub = np.concatenate(rhs)
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

# Validity of the strengthened cut, spelled out because it departs from the
# textbook form (which assumes a linear knapsack; ours is a union).
#
#   Claim. For any A subset of N(b),
#       sum_{c not in A} min(w_c, R) y_c  >=  R z_b,   R = tau*P_b - cov_b(A)
#   is valid for every integral placement.
#
#   z_b = 0: right side is 0, all coefficients are non-negative.       [ok]
#   z_b = 1: let S = {c : y_c = 1}, so cov_b(S) >= tau*P_b. Coverage is
#       monotone and subadditive, so
#           tau*P_b <= cov_b(S) <= cov_b(S∩A) + sum_{c in S\A} w_c
#                              <= cov_b(A)   + sum_{c in S\A} w_c
#       hence sum_{c in S\A} w_c >= R. Capping at R preserves this: either some
#       single c in S\A already has w_c >= R, contributing exactly R, or every
#       w_c < R and the capped sum equals the uncapped one.               [ok]
#
#   Strength. The textbook residual is tau*P_b - sum_{c in A} w_c. Since the
#   union cov_b(A) is no larger than that sum, our R is no smaller. Dividing the
#   cut by R gives sum min(w_c/R, 1) y_c >= z_b, whose coefficients shrink as R
#   grows -- so the larger residual is the stronger cut. Submodularity of the
#   coverage function is what buys the improvement.
