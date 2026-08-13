#!/usr/bin/env bash
# Per-sub-problem radius sweep, with a wall-clock budget.
#
# Radius is the only lever that has paid twice (FINDINGS §5.16: +531 buildings
# from re-tuning; the 4x rehearsal: verify-radius 3000 -> 5000). Every
# algorithmic idea tried since has failed. §7.4 also records 1500 m beating
# 1000 m at (0.75, 500) even with a 60 s polish handicap, so the best radius is
# per-block, not global.
#
# Nothing here needs conditional logic or a winner-picking rule: every result is
# verified and archived, and the archive keeps the best per block by
# construction. A variant that loses is simply never exported. That also
# subsumes §7.3 (2-exchange costing a hair at saturated tau) -- the with/without
# --swap axis is swept and the archive decides.
#
#   bash tools/radius_sweep.sh <data> <params> <budget_sec> [radii] [polish_sec]
set -uo pipefail

DATA=${1:?usage: radius_sweep.sh <data> <params> <budget_sec> [radii] [polish]}
PARAMS=${2:?}
BUDGET=${3:-7200}
RADII=${4:-1000,1500,2000}
POLISH=${5:-150}
BIN=${BIN:-./giscup}
VRAD=${VRAD:-5000}

START=$(date +%s)
left() { echo $(( BUDGET - ($(date +%s) - START) )); }

mapfile -t BLOCKS < "$PARAMS"
IFS=',' read -ra RS <<< "$RADII"

echo "sweep: ${#BLOCKS[@]} blocks x ${#RS[@]} radii x 2 swap settings, budget ${BUDGET}s"
printf '%-7s %-7s %-7s %-6s %10s %8s\n' tau k radius swap score sec

for r in "${RS[@]}"; do
  for swap in 400 0; do
    for b in "${BLOCKS[@]}"; do
      [ -z "$b" ] && continue
      tau=$(echo "$b" | awk '{print $1}'); k=$(echo "$b" | awk '{print $2}')
      if [ "$(left)" -lt $(( POLISH + 120 )) ]; then
        echo "budget exhausted with $(left)s left; stopping cleanly"
        exit 0
      fi
      t0=$(date +%s)
      swapflag=""; [ "$swap" != 0 ] && swapflag="--swap $swap"
      s=$($BIN solve --data "$DATA" --tau "$tau" --k "$k" \
            --radius "$r" --verify-radius "$VRAD" --lns-sec "$POLISH" $swapflag \
            --archive-add --method "r${r},swap${swap},lns${POLISH}" \
            --out /dev/null 2>/dev/null | grep -E "^$tau" | tail -1 | awk '{print $5}')
      printf '%-7s %-7s %-7s %-6s %10s %7ss\n' "$tau" "$k" "$r" "$swap" "${s:-FAIL}" \
        $(( $(date +%s) - t0 ))
    done
  done
done
echo "sweep complete with $(left)s of budget unused"
