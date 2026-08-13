#!/usr/bin/env bash
# Acceptance test: the whole path on tau/k values the pipeline has never seen.
#
# The contest publishes its own three tau and three k values. The sample's
# 0.25/0.5/0.75 x 50/500/1000 are examples, and anything that quietly assumes
# them is aimed at all nine blocks at once. This drives the full path --
# parameters file -> solve -> polish -> archive -> export -> conformance ->
# verify -- from a parameters *file*, with no flags carrying tau or k and no
# manual edits anywhere.
#
#   bash tests/alien_params.sh [dataset.geojson]
set -uo pipefail

DATA=${1:-data/GIS-cup-sample-dataset.geojson}
BIN=${BIN:-./giscup}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAIL=0
say() { printf '\n\033[1m--- %s\033[0m\n' "$1"; }

# Deliberately unlike the sample in every component, including a k larger than
# any tested and a tau above 0.75.
cat > "$WORK/competition-parameters.txt" <<'EOF'
# competition parameters
tau: 0.4, 0.7, 0.9
k: 100, 750, 2000
EOF

say "1/6  parse the parameters file"
python3 tools/parse_params.py "$WORK/competition-parameters.txt" > "$WORK/params.txt" || FAIL=1
nblocks=$(grep -c . "$WORK/params.txt")
echo "  $nblocks blocks:"; sed 's/^/    /' "$WORK/params.txt"
[ "$nblocks" -eq 9 ] || { echo "  expected 9 blocks"; FAIL=1; }

say "2/6  solve every block (short polish; this is a plumbing test)"
$BIN solve --data "$DATA" --params "$WORK/params.txt" \
    --radius 600 --verify-radius 3000 --lns-sec 5 --swap 200 \
    --archive "$WORK/archive" --archive-add --method alien \
    --out "$WORK/direct.txt" 2>/dev/null | tail -12 || FAIL=1

say "3/6  archive holds all nine alien blocks"
$BIN archive best --data "$DATA" --params "$WORK/params.txt" \
    --archive "$WORK/archive" 2>/dev/null | tail -11
$BIN archive best --data "$DATA" --params "$WORK/params.txt" \
    --archive "$WORK/archive" >/dev/null 2>&1 || { echo "  archive incomplete"; FAIL=1; }

say "4/6  export from the archive, driven by the parameters file"
$BIN archive export-submission --data "$DATA" --params "$WORK/params.txt" \
    --archive "$WORK/archive" --out "$WORK/submission.txt" 2>/dev/null | tail -3 || FAIL=1

say "5/6  official submission-format conformance"
python3 tests/conformance.py "$WORK/submission.txt" 9 | tail -3 || FAIL=1

say "6/6  internal verification"
$BIN verify --data "$DATA" --out "$WORK/submission.txt" 2>/dev/null \
    | grep -E 'false=|SUBMISSION|PROBLEM' | tail -10
$BIN verify --data "$DATA" --out "$WORK/submission.txt" 2>/dev/null \
    | grep -q 'SUBMISSION OK' || { echo "  verification failed"; FAIL=1; }

say "budget allocation on the alien blocks"
python3 tools/allocate.py 6 --params "$WORK/params.txt" 2>&1 | head -6

echo
[ "$FAIL" -eq 0 ] && echo "ALIEN-PARAMETERS TEST PASSED" || echo "ALIEN-PARAMETERS TEST FAILED"
exit "$FAIL"
