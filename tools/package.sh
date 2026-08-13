#!/usr/bin/env bash
# Build the EasyChair submission archive, and refuse to build a bad one.
#
# EasyChair (conf=giscup2026) takes ONE archive containing BOTH the nine-block
# results file AND the source with build/run instructions. Getting that wrong is
# the same class of failure as the parameter-line format (FINDINGS §5.20): it
# costs everything and no internal check notices, so the gates run first and the
# archive is only written if they pass.
#
#   bash tools/package.sh <submission.txt> [out.zip] [expected_blocks]
set -uo pipefail

SUB=${1:?usage: package.sh <submission.txt> [out.zip] [blocks]}
OUT=${2:-giscup2026-submission.zip}
NB=${3:-9}
BIN=${BIN:-./giscup}
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

[ -f "$SUB" ] || { echo "no such submission: $SUB"; exit 2; }

echo "=== gate 1: official submission-format conformance ==="
python3 tests/conformance.py "$SUB" "$NB" | tail -3 || {
    echo "REFUSING TO PACKAGE: the official parser would reject this file."; exit 1; }

echo
echo "=== gate 2: repository self-tests ==="
make check 2>&1 | grep -E 'PASSED|FAILED' || true
make check >/dev/null 2>&1 || { echo "REFUSING TO PACKAGE: make check fails."; exit 1; }

echo
echo "=== staging ==="
PKG="$STAGE/giscup2026"
mkdir -p "$PKG/source"
cp "$SUB" "$PKG/results.txt"
for p in src tests tools Makefile README.md plan.md FINDINGS.md; do
    [ -e "$p" ] && cp -r "$p" "$PKG/source/"
done
# Data and build artefacts are not part of the submission.
rm -rf "$PKG/source/tools/__pycache__" "$PKG/source/archive" 2>/dev/null

cat > "$PKG/README.txt" <<'EOF'
SIGSPATIAL 2026 GIS Cup -- antenna placement
============================================

CONTENTS
  results.txt   the nine-block results file (one block = 3 lines:
                "(tau, k)"; the k antenna coordinates; the claimed building ids)
  source/       complete source, tests and tooling

BUILD
  cd source && make portable

  Requires a C++17 compiler with OpenMP. No third-party libraries: the GeoJSON
  reader, geometry kernel and spatial index are all in-tree. `make portable`
  omits -march=native so the binary is not tuned to our machine.

RUN
  ./giscup solve --data <dataset>.geojson \
      --params <competition-parameters> \
      --radius <R> --verify-radius <V> \
      --lns-sec <seconds> --swap 400 \
      --archive-add --out results.txt

  Radii are re-derived per dataset rather than assumed:
      ./giscup tune --data <dataset>.geojson --tau <t> --k <k>

  One-command readiness check on an unseen dataset:
      bash tools/runday.sh <dataset>.geojson <competition-parameters.txt>

VERIFY
  ./giscup verify --data <dataset>.geojson --out results.txt
  python3 tests/conformance.py results.txt 9

  The first re-derives every claim with an exact, uncapped visibility sweep and
  reports false claims, unclaimed-but-serviceable buildings and unknown ids. The
  second checks the file against the official evaluator's parser rules.

METHOD
  Visible boundary arcs are computed exactly by rotational plane sweep -- no
  boundary sampling, so coverage ratios carry no discretisation error. Antennas
  are placed at polygon vertices, where a single antenna covers both incident
  edges outright. Selection optimises a per-building potential (tuned per
  sub-problem) under a target-set restriction, followed by 2-exchange and
  large-neighbourhood polish. See source/FINDINGS.md for the measured
  justification of each choice, including the approaches that were tried and
  rejected.

AI DISCLOSURE
  This entry was developed with AI assistance (Claude). Every reported number is
  reproducible from the commands recorded in source/FINDINGS.md.
EOF

echo "  results.txt        $(wc -c < "$PKG/results.txt") bytes, $(wc -l < "$PKG/results.txt") lines"
echo "  source/            $(find "$PKG/source" -type f | wc -l) files"

echo
echo "=== writing archive ==="
rm -f "$OUT"
( cd "$STAGE" && zip -qr "$OUT" giscup2026 ) && mv "$STAGE/$OUT" . 2>/dev/null \
    || ( cd "$STAGE" && zip -qr "$(pwd)/$OUT" giscup2026 && mv "$(pwd)/$OUT" "$OLDPWD/" )
[ -f "$OUT" ] || { echo "zip failed"; exit 1; }

echo "  $OUT  $(du -h "$OUT" | cut -f1)"
echo
echo "=== gate 3: the packaged results file still conforms ==="
TMPD=$(mktemp -d); unzip -q "$OUT" -d "$TMPD"
python3 tests/conformance.py "$TMPD/giscup2026/results.txt" "$NB" | tail -2
ls "$TMPD/giscup2026" "$TMPD/giscup2026/source" | head -20
rm -rf "$TMPD"
echo
echo "PACKAGE READY: $OUT  -> upload to EasyChair (conf=giscup2026)"
