#!/usr/bin/env python3
"""Submission-format conformance against the official GIS Cup 2026 evaluator.

Re-implements the acceptance rules of the organizers' `solution-parser.ts`
(github.com/alowe/gis-cup-2026-evaluator) so a format regression is caught in
`make check`, with no Node toolchain required.

This exists because of a real defect it would have caught. Our writer emitted the
parameter line as `0.25,50`. The official parser matches it with

    /^\\(\\s*([^,]*)\\s*,\\s*([^,]*)\\s*\\)$/

which is anchored and requires the parentheses. The bare form yields INVALID_TAU
and INVALID_K, whose documented action is "Score this subproblem as zero" -- so
all nine blocks would have scored zero while every internal check stayed green.
Nothing we own could have detected that; only the grader's own rules could.

    python3 tests/conformance.py submission.txt [expected_block_count]
"""
import re
import sys

# Transcribed from solution-parser.ts. Keep these byte-for-byte with upstream.
PARAM_RE = re.compile(r'^\(\s*([^,]*)\s*,\s*([^,]*)\s*\)$')
DECIMAL_RE = re.compile(r'^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$')
NONFINITE_RE = re.compile(r'^[+-]?(?:Infinity|NaN)$', re.I)


def scan_parenthesized(line):
    """Mirror scanParenthesizedEntries: top-level (...) groups."""
    out, depth, cur = [], 0, []
    for ch in line:
        if ch == '(':
            depth += 1
            cur.append(ch)
        elif ch == ')':
            depth -= 1
            cur.append(ch)
            if depth == 0:
                out.append(''.join(cur).strip())
                cur = []
        elif depth > 0:
            cur.append(ch)
    if cur:
        out.append(''.join(cur).strip())
    return out


def parse_decimal(tok):
    tok = tok.strip()
    if NONFINITE_RE.match(tok):
        return float('inf')
    if not DECIMAL_RE.match(tok):
        return None
    return float(tok)


def main():
    path = sys.argv[1]
    expect_blocks = int(sys.argv[2]) if len(sys.argv) > 2 else None
    text = open(path, encoding='utf-8').read().replace('﻿', '')
    text = re.sub(r'\r\n?', '\n', text)
    lines = text.split('\n')
    if lines and lines[-1] == '' and len(lines) % 3 == 1:
        lines.pop()

    warnings, blocks = [], 0
    for off in range(0, len(lines), 3):
        blocks += 1
        blk = lines[off:off + 3]
        if len(blk) != 3:
            warnings.append(f"block {blocks}: INCOMPLETE_SUBPROBLEM ({len(blk)} of 3 lines)")
            continue
        m = PARAM_RE.match(blk[0].strip())
        tau = k = None
        if m:
            tv, kv = parse_decimal(m.group(1)), parse_decimal(m.group(2))
            if tv is not None and 0 < tv <= 1:
                tau = tv
            if kv is not None and kv >= 0 and float(kv).is_integer():
                k = int(kv)
        if tau is None:
            warnings.append(f"block {blocks}: INVALID_TAU  -> subproblem scored ZERO")
        if k is None:
            warnings.append(f"block {blocks}: INVALID_K    -> subproblem scored ZERO")

        entries = scan_parenthesized(blk[1])
        retained = entries[:k] if k is not None else []
        for i, raw in enumerate(retained, 1):
            inner = raw[1:-1] if raw.startswith('(') and raw.endswith(')') else None
            toks = inner.split(',') if inner is not None else None
            ok = False
            if toks is not None and len(toks) == 2:
                x, y = parse_decimal(toks[0]), parse_decimal(toks[1])
                ok = x is not None and y is not None and abs(x) != float('inf') \
                    and abs(y) != float('inf')
            if not ok:
                warnings.append(f"block {blocks}: MALFORMED/NONFINITE antenna entry {i}")
        if k is not None and len(entries) < k:
            warnings.append(f"block {blocks}: only {len(entries)} antennas for k={k}")

        for i, tok in enumerate(blk[2].split(','), 1):
            if tok.strip() == '' and blk[2].strip() != '':
                warnings.append(f"block {blocks}: EMPTY_BUILDING_ID entry {i}")

        if tau is not None and k is not None:
            ids = [t for t in blk[2].split(',') if t.strip()]
            print(f"  block {blocks}: tau={tau:g} k={k}  antennas={len(entries)} "
                  f"retained={len(retained)}  claims={len(ids)}")

    print(f"\nblocks: {blocks}" + (f" (expected {expect_blocks})" if expect_blocks else ""))
    if expect_blocks is not None and blocks != expect_blocks:
        warnings.append(f"expected {expect_blocks} blocks, found {blocks}")
    for w in warnings:
        print(f"WARNING  {w}")
    print("CONFORMANCE PASSED" if not warnings else "CONFORMANCE FAILED")
    return 1 if warnings else 0


sys.exit(main())
