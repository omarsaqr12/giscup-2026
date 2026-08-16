#!/usr/bin/env bash
# Run-day readiness check: unseen dataset -> go/no-go, in one command.
#
# The competition is a 24-hour window with one submission and no second attempt,
# so the expensive failure is not a weak score, it is a silent failure on data
# nobody has looked at. Everything here exists because the 2026 sample already
# violated an assumption the spec implied (FINDINGS.md §2), or because a tuned
# constant turned out to be a property of one city rather than a theorem (§5.3).
#
#   bash tools/runday.sh <dataset.geojson> [probe_tau] [probe_k]
#
# Reads nothing from the sample. Everything is re-derived from the file given.
set -uo pipefail

DATA=${1:?usage: runday.sh <dataset.geojson> [competition-parameters.txt]}
PARAMS=${2:-}

# The nine (tau, k) pairs are run-day inputs. If the organizers' parameters file
# is given, it is the single source; the probe block is drawn from it rather
# than assumed. Falling back to sample values is explicitly announced, because
# silently assuming them is the failure this guards against.
if [ -n "$PARAMS" ]; then
    python3 tools/parse_params.py "$PARAMS" > /tmp/giscup-params.txt || exit 2
    PTAU=$(head -1 /tmp/giscup-params.txt | awk '{print $1}')
    PK=$(head -1 /tmp/giscup-params.txt | awk '{print $2}')
    echo "parameters: $(grep -c . /tmp/giscup-params.txt) blocks from $PARAMS"
    sed 's/^/    /' /tmp/giscup-params.txt
else
    PTAU=0.5; PK=500
    echo "WARNING: no parameters file given; probing with tau=$PTAU k=$PK."
    echo "         On run day pass competition-parameters.txt as argument 2."
fi
BIN=${BIN:-./giscup}
LOG=${LOG:-results/runday}
mkdir -p "$LOG"
FAIL=0
step() { printf '\n\033[1m=== %s ===\033[0m\n' "$1"; }

[ -f "$BIN" ] || { echo "no $BIN -- run make first"; exit 2; }

step "1/5  dataset inspection (loader quirks)"
if python3 tools/inspect_dataset.py "$DATA" | tee "$LOG/inspect.txt"; then
    echo "  -> no blocking problems"
else
    echo "  -> PROBLEMS FOUND (above). Fix the loader before proceeding."
    FAIL=1
fi

step "2/5  input robustness against THIS file"
# Mutates the real dataset, not the sample: the point is to confirm the loader
# normalises whatever this file happens to be.
if SRC="$DATA" bash tests/robustness.sh 2>&1 | tee "$LOG/robustness.txt" | tail -3; then
    echo "  -> all input shapes handled"
else
    echo "  -> ROBUSTNESS FAILURES"
    FAIL=1
fi

step "3/5  re-tune search and verify radius on THIS geometry"
echo "  (inherited 600/3000 are properties of the sample city, not theorems)"
$BIN tune --data "$DATA" --tau "$PTAU" --k "$PK" 2>/dev/null | tee "$LOG/tune.txt"
RADIUS=$(grep -oE 'recommended --radius [0-9.]+' "$LOG/tune.txt" | awk '{print $3}' | tail -1)
VRAD=$(grep -oE 'recommended --verify-radius [0-9.]+' "$LOG/tune.txt" | awk '{print $3}' | tail -1)
if [ -z "${VRAD:-}" ]; then
    echo "  -> no capped verify radius matched uncapped; will run UNCAPPED"
    VRAD=-1
fi
: "${RADIUS:=600}"
# A censored sweep is the failure that cost us radius 2000 on run day: the ladder
# stopped at 1500, so 1500 was "recommended" for being the last rung rather than
# the best one. The tuner now says so out loud; make sure run day cannot miss it.
if grep -q 'still climbing' "$LOG/tune.txt"; then
    echo
    echo "  *** RADIUS SWEEP CENSORED -- the score had not turned over at $RADIUS m."
    echo "      Solve at this radius AND at larger ones; the archive keeps the best"
    echo "      per block, so a losing radius costs nothing. Re-run tune with a"
    echo "      larger --tune-max-radius to find where it actually flattens."
    echo
fi
echo "  -> radius=$RADIUS  verify-radius=$VRAD"

step "4/5  marginal-returns diagnostic (§4.1 of FINDINGS)"
# Increasing buildings-per-antenna means the budget is being misallocated:
# early antennas part-cover buildings the budget will never finish.
{
  prev=0; prevk=0
  for k in "$PK" $((PK*2)) $((PK*4)); do
    s=$($BIN solve --data "$DATA" --tau "$PTAU" --k "$k" --radius "$RADIUS" \
          --verify-radius "$VRAD" --lns-sec 0 --powers 2 --out /dev/null 2>/dev/null \
        | grep -E "^$PTAU" | tail -1 | awk '{print $5}')
    [ -z "$s" ] && s=0
    awk -v a="$prevk" -v b="$k" -v p="$prev" -v s="$s" \
        'BEGIN{printf "  antennas %6d-%6d: %6.2f buildings/antenna  (score %d)\n",a,b,(s-p)/(b-a),s}'
    prev=$s; prevk=$k
  done
} | tee "$LOG/marginal.txt"
awk 'NR==1{a=$4} NR==2{b=$4} END{
  if (b>a) print "  -> marginal returns INCREASING: budget is misallocated, focus/2-exchange matter here"
  else     print "  -> marginal returns decreasing: construction is allocating sanely"
}' "$LOG/marginal.txt"

step "5/5  summary"
printf '  dataset        %s\n' "$DATA"
printf '  radius         %s\n' "$RADIUS"
printf '  verify-radius  %s\n' "$VRAD"
printf '  logs           %s/\n' "$LOG"
echo
if [ "$FAIL" -eq 0 ]; then
    cat <<EOF
  GO. Solve with:

    ./giscup solve --data $DATA \\
        --tau <given> --k <given> \\
        --radius $RADIUS --verify-radius $VRAD \\
        --lns-sec <budget> --swap 400 --archive-add --out submission.txt

  then assemble from the archive (never from the run directly):

    ./giscup archive export-submission --data $DATA \\
        ${PARAMS:+--params /tmp/giscup-params.txt} --out submission.txt
    python3 tests/conformance.py submission.txt 9
    ./giscup verify --data $DATA --out submission.txt
EOF
else
    echo "  NO-GO. Resolve the problems above first."
fi
exit "$FAIL"
