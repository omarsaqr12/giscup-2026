# Task 4 -- Lagrangian completion pricing: BOTH PARTS REJECTED

## Part 1: bundle-based exchange rate (reach_b) -- neutral, not enabled

5.5's antenna-priced potential converts metres to antennas via reach_b = the
largest slice a *single* antenna delivers. That is optimistic whenever a
building needs two or three, since the second antenna gets the harder facade.
Replaced with the rate implied by the building's actual cheapest completion:
tau*P_b / m, where the cheapest set has m antennas (--reach-bundles).

Correctness gate: PASSES, ratio 1.00 on all four exact-oracle instances.

  tau    k     baseline   reach-bundles
  0.75   50         393             393
  0.50   50         868             868
  0.75  500        2874            2874
  0.25  500       10531           10531

Identical everywhere -- not merely close, bit-identical scores. Two reasons:
metre-priced configurations never consult reach_ at all (and metre wins at
(0.75,50) and (0.5,50)), and where antenna-pricing does win, the per-sub-problem
exponent sweep absorbs the change -- a different exponent reaches an equivalent
solution. The correction is theoretically right and practically inert once the
exponent is already being tuned. Not enabled: no gain, extra precompute.

## Part 2: Lagrangian price sweep constructor -- rejected

Dualise sum(y) <= k with price lambda; each building buys its cheapest
completion when c_b < 1/lambda; the union of accepted bundles supplies the
sharing. Distinct from 5.4's ratio greedy, which picks sequentially by locally
cheapest marginal cost and so never sees the antenna that is mediocre for one
building and excellent for twenty.

Correctness gate: FAILS.
  n=40 tau=0.5  k=3 -> 19  (optimum 20)
  n=70 tau=0.5  k=3 -> 20  (optimum 22)
  n=40 tau=0.75 k=3 ->  8  (optimum  8)  OK
  n=70 tau=0.5  k=2 -> 15  (optimum 15)  OK

The polish could not recover, so the price sweep lands in a worse basin rather
than merely starting further from the optimum.

At scale (60s polish, radius 1000):
  tau    k     baseline   lagrangian
  0.75   50         393          379    -3.6%
  0.50   50         868          850    -2.1%
  0.75  500        2874         2799    -2.6%
  0.25  500       10531        10561    +0.3%

It loses on all three deciding sub-problems and wins only at tau=0.25, which is
~99% saturated and worth almost nothing competitively. Its 10,561 also does not
beat the archive's 10,581, so nothing ratcheted.

Mechanism, and it is consistent: at tau=0.25 nearly every building needs exactly
one antenna, so completion bundles are singletons and the price sweep degenerates
to a clean max-coverage selection where union-based sharing is exactly right. At
tau=0.75 bundles are two or three antennas and taking their union is too coarse
a way to spend a tight budget. The method is strongest precisely where the
competition is not decided.

Both parts default OFF. Archive unchanged: 51,844.
