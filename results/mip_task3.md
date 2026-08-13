# Task 3 -- focused MIP: DROPPED (fails its own correctness gate)

Rationale was sound: FINDINGS §5.9 killed the LP *as a bound* because fractional
z_b takes partial credit. With z_b binary, branch-and-bound enforces the
threshold, so the same model could work as a *solver*. Built with
scipy.optimize.milp (HiGHS), which exposes mip_dual_bound and mip_gap.

Validation on tiny40, tau=0.5, k=3, where `giscup exact` gives optimum 20:

  incumbent (heuristic)                 20 serviced
  MIP free solve, 3103 vars, 222s       22 serviced, dual bound 22.0, gap 0.00%
  SAME PLACEMENT scored by the
  validated engine (giscup verify)      18 serviced      <-- MIP over-reports

Three exact-claiming methods, two agreeing:
  giscup exact (enumeration)  optimum = 20
  visibility engine           MIP placement = 18
  MIP                         claims 22, "gap 0.00%"

The MIP is wrong, by 4 buildings on a 40-building instance, while reporting a
zero optimality gap. Its atom model over-counts coverage somewhere; the defect
was not located before the time box expired.

DROPPED per the brief's kill criterion. The tool stays in the tree, disabled and
labelled, because the failure is the useful part: a method that reports "proven
optimal, gap 0.00%" and is silently wrong is far more dangerous than one that is
merely weak. Nothing it produces may enter the archive without passing
`giscup verify` first -- which is exactly the gate Task 0 makes unavoidable.

Consequence for FINDINGS §7 item 1: the optimality-gap measurement is still
missing. The MIP dual bound was the candidate for restoring it and it cannot be
trusted. A bespoke k=4-5 branch-and-bound oracle over the validated engine
remains the fallback, as §7 anticipated.
