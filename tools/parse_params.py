#!/usr/bin/env python3
"""Parse the run-day parameters file into the nine (tau, k) pairs.

The competition publishes the three tau values and three k values alongside the
evaluation dataset. **0.25/0.5/0.75 x 50/500/1000 are the sample's values, not
the contest's** -- the problem statement explicitly encourages testing others.
Anything in the pipeline that assumes them is aimed at all nine blocks at once.

The exact file format is not published: no `competition-parameters.txt` exists in
the organizers' evaluator repository as of the pin in FINDINGS 5.20. So this
accepts every layout that seemed plausible and **fails loudly rather than
guessing** when a file does not clearly match one of them. A silent misparse here
has the same blast radius as the header-format defect of 5.20 -- nine blocks at
once -- so ambiguity must stop the run, not be resolved by assumption.

Accepted:

    tau: 0.25, 0.5, 0.75              labelled lists, any order, ':' or '='
    k: 50, 500, 1000

    0.25 0.5 0.75                     two bare lines, taus first
    50 500 1000

    (0.25, 50)                        explicit pairs, one per line,
    (0.25, 500)                       matching the submission header form
    ...

    {"tau": [...], "k": [...]}        JSON, either key spelling

Comments (# or //) and blank lines are ignored throughout.

    python3 tools/parse_params.py competition-parameters.txt [--format pairs|shell|tau|k]
"""
import json
import re
import sys

NUM = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'


def fail(msg):
    print(f"PARAMETER PARSE FAILED: {msg}", file=sys.stderr)
    print("Refusing to guess. Inspect the file and pass --tau/--k explicitly.",
          file=sys.stderr)
    sys.exit(2)


def clean(text):
    out = []
    for line in text.replace('\r\n', '\n').replace('\r', '\n').split('\n'):
        line = re.sub(r'(#|//).*$', '', line).strip()
        if line:
            out.append(line)
    return out


def as_k(v):
    if abs(v - round(v)) > 1e-9 or round(v) < 0:
        fail(f"k value {v} is not a non-negative integer")
    return int(round(v))


def as_tau(v):
    if not (0 < v <= 1):
        fail(f"tau value {v} is outside (0, 1]")
    return v


def parse(text):
    # --- JSON ------------------------------------------------------------
    stripped = text.strip()
    if stripped.startswith('{') or stripped.startswith('['):
        try:
            d = json.loads(stripped)
        except Exception as e:
            fail(f"looks like JSON but does not parse: {e}")
        if isinstance(d, dict):
            tk = next((d[x] for x in ('tau', 'taus', 'thresholds', 'tauValues') if x in d), None)
            kk = next((d[x] for x in ('k', 'ks', 'antennas', 'kValues') if x in d), None)
            if tk and kk:
                return [as_tau(float(t)) for t in tk], [as_k(float(v)) for v in kk]
        fail("JSON present but no recognisable tau/k arrays")

    lines = clean(text)
    if not lines:
        fail("file is empty")

    # --- explicit pairs, one per line -------------------------------------
    pair_re = re.compile(rf'^\(?\s*({NUM})\s*,\s*({NUM})\s*\)?$')
    pair_matches = [pair_re.match(l) for l in lines]
    if all(pair_matches):
        got = [(as_tau(float(m.group(1))), as_k(float(m.group(2)))) for m in pair_matches]
        taus = sorted({t for t, _ in got})
        ks = sorted({k for _, k in got})
        if len(got) != len(taus) * len(ks):
            fail(f"{len(got)} explicit pairs do not form a complete "
                 f"{len(taus)}x{len(ks)} grid")
        return taus, ks

    # --- explicit pairs embedded in a labelled/prose file -----------------
    # The organizers' own competition-parameters.txt lists the nine "(tau, k)"
    # sub-problems, but *after* a labelled summary and a prose header, so not
    # every line is a pair. Extract the pair lines and, if they form a complete
    # grid of at least 2x2, use them. A stray "(x, y)" in prose cannot trigger
    # this -- it would not complete a >=2x2 grid -- so this stays conservative.
    got = [(as_tau(float(m.group(1))), as_k(float(m.group(2))))
           for m in pair_matches if m]
    if got:
        taus = sorted({t for t, _ in got})
        ks = sorted({k for _, k in got})
        if len(got) == len(taus) * len(ks) and len(taus) >= 2 and len(ks) >= 2:
            return taus, ks

    # --- labelled lists ---------------------------------------------------
    taus = ks = None
    for line in lines:
        m = re.match(r'^\s*([A-Za-z_]+)\s*[:=]\s*(.+)$', line)
        if not m:
            continue
        key, rest = m.group(1).lower(), m.group(2)
        vals = [float(x) for x in re.findall(NUM, rest)]
        if not vals:
            continue
        if key in ('tau', 'taus', 'threshold', 'thresholds'):
            taus = [as_tau(v) for v in vals]
        elif key in ('k', 'ks', 'antenna', 'antennas', 'numantennas'):
            ks = [as_k(v) for v in vals]
    if taus and ks:
        return taus, ks

    # --- two bare numeric lines, taus first --------------------------------
    numeric = [[float(x) for x in re.findall(NUM, l)] for l in lines]
    numeric = [v for v in numeric if v]
    if len(numeric) == 2:
        a, b = numeric
        # Disambiguate by range rather than order: taus lie in (0,1], k are
        # integers >= 1. Refuse if both readings are plausible.
        a_tau = all(0 < v <= 1 for v in a)
        b_tau = all(0 < v <= 1 for v in b)
        if a_tau and not b_tau:
            return [as_tau(v) for v in a], [as_k(v) for v in b]
        if b_tau and not a_tau:
            return [as_tau(v) for v in b], [as_k(v) for v in a]
        fail("two numeric lines found but which is tau and which is k is ambiguous")

    fail(f"no recognised layout ({len(lines)} non-comment lines). "
         "Supported: labelled lists, explicit (tau, k) pairs, two bare lines, JSON")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    fmt = 'pairs'
    if '--format' in sys.argv:
        fmt = sys.argv[sys.argv.index('--format') + 1]
    taus, ks = parse(open(sys.argv[1], encoding='utf-8').read())

    if len(taus) != 3 or len(ks) != 3:
        print(f"note: {len(taus)} tau x {len(ks)} k = {len(taus)*len(ks)} blocks "
              f"(the contest states nine)", file=sys.stderr)

    if fmt == 'tau':
        print(','.join(f'{t:g}' for t in taus))
    elif fmt == 'k':
        print(','.join(str(k) for k in ks))
    elif fmt == 'shell':
        print(f"TAUS='{','.join(f'{t:g}' for t in taus)}'")
        print(f"KS='{','.join(str(k) for k in ks)}'")
    else:
        for t in taus:
            for k in ks:
                print(f"{t:g} {k}")


main()
