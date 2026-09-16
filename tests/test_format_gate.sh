#!/usr/bin/env bash
# Fast submission-format gate regression; no C++ build or competition solve.
# Run from repository root: bash tests/test_format_gate.sh
set -euo pipefail
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The old Makefile silently skipped this situation and could reuse a stale
# /tmp/giscup-conformance.txt written by an unrelated run.
if make --no-print-directory -s check-format SUBMISSION="$TMP/absent.txt" BLOCKS=1 >"$TMP/missing.log" 2>&1; then
    echo "FAIL: missing submission passed conformance" >&2
    exit 1
fi

printf '(0.5, 1)\n(0,0)\nbuilding-1\n' >"$TMP/valid.txt"
make --no-print-directory -s check-format SUBMISSION="$TMP/valid.txt" BLOCKS=1 >"$TMP/valid.log"

# Bare parameters are rejected by the official-format parser.
printf '0.5,1\n(0,0)\nbuilding-1\n' >"$TMP/invalid.txt"
if make --no-print-directory -s check-format SUBMISSION="$TMP/invalid.txt" BLOCKS=1 >"$TMP/invalid.log" 2>&1; then
    echo "FAIL: malformed parameters passed conformance" >&2
    exit 1
fi

echo "PASS: missing input fails, well-formed input passes, invalid parameters fail"
