# Task 4 -- dress rehearsal: PASSED

Unseen geometry (4x tiled, 51,440 buildings) with unseen parameters
(tau 0.35/0.6/0.85, k 250/1200/3000), driven from a parameters FILE through
tools/runday.sh alone. No manual intervention at any step.

Readiness    GO. radius 1000, verify-radius 5000, marginal returns DECREASING
             (17.21 buildings/antenna over the second 500 -- no repeat of the
             §4.1 misallocation pathology at 4x scale).

Solve        9/9 blocks, 130,015 serviced, 45s polish each.
Export       archive -> submission, claims re-derived exactly.
Conformance  PASSED (official parser rules, 9 blocks).
Verify       9/9 blocks, 0 false claims, 0 missed, 0 unknown ids.
Package      PACKAGE READY, gates enforced.
Peak RSS     2,591,912 kB = 2.59 GB.

Two findings the rehearsal produced that the sample could not:

1. verify-radius 5000 is required here; 3000 -- correct for the sample -- loses
   5 claims. Third instance of a tuned constant being valid only for the
   configuration it was tuned under (§5.16 was the first two). The playbook
   re-derives both radii on the real file; never inherit them.

2. The memory rule shipped hours earlier carried a (radius/600)^2 term and
   over-predicted 7.14 GB against 2.59 GB measured. Memory is linear in building
   count and essentially FLAT in radius (600->1000 moved RSS 3.6%), because the
   contribution map's entry count grew only 8%. Corrected in plan.md §9. Had it
   shipped, a run-day operator would have dropped radius for no reason and given
   up real score.

Also observed: export re-verification found one MORE serviced building than the
archive recorded at (0.85, 1200) -- 4,649 vs 4,648 -- because the archive stored
a capped-verify score while export re-derives uncapped. The design of §4.4
working as intended.
