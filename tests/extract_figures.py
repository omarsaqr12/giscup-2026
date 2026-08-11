"""Extract Figure 4 / Figure 5 ground truth from the official spec SVGs into JSON.

The SVGs encode, exactly: the 15 building polygons, the 3 antenna positions, the
visible sub-arcs (as polylines), and the per-building coverage percentages that
the organizers themselves published. That makes them a regression oracle for the
whole visibility engine.
"""
import json, re, sys, pathlib

FIGDIR = pathlib.Path(__file__).resolve().parents[1] / "data" / "figures"


def nums(s):
    return [tuple(map(float, p.split(","))) for p in s.split()]


def parse(path, label_color_bold):
    s = path.read_text()
    polys = [nums(m) for m in re.findall(r'<polygon points="([^"]+)"', s)]
    ants = [(float(a), float(b)) for a, b in
            re.findall(r'<circle cx="([-\d.]+)" cy="([-\d.]+)"', s)]
    labels = [(float(x), float(y), fill, float(v))
              for x, y, fill, v in
              re.findall(r'<text x="([-\d.]+)" y="([-\d.]+)" fill="(#[0-9a-fA-F]+)"[^>]*>([\d.]+)%</text>', s)]
    return polys, ants, labels


def main():
    out = {}
    for name, fig, tau, expect_score in [
        ("los-scene-composite.svg", "figure4", 0.5, 8),
        ("los-scene-composite-2.svg", "figure5", 0.6, 4),
    ]:
        polys, ants, labels = parse(FIGDIR / name, None)
        # SVG y grows downward; flip to a right-handed frame. Lengths/visibility
        # are invariant under reflection, so published percentages still apply.
        polys = [[(x, -y) for x, y in p] for p in polys]
        ants = [(x, -y) for x, y in ants]
        assert len(polys) == 15, len(polys)
        assert len(ants) == 3, len(ants)
        assert len(labels) == 15, len(labels)
        # Labels appear in the same order as the polygons (both authored top-down).
        cov = [v / 100.0 for _, _, _, v in labels]
        bold = [f.lower() in ("#111111", "#020304") for _, _, f, _ in labels]
        score = sum(bold)
        assert score == expect_score, (fig, score, expect_score)
        # cross-check: bold <=> coverage >= tau
        for c, b in zip(cov, bold):
            assert (c >= tau - 1e-9) == b, (fig, c, b)
        out[fig] = {"tau": tau, "k": 3, "expected_service_score": expect_score,
                    "buildings": polys, "antennas": ants, "expected_coverage": cov}
        print(f"{fig}: tau={tau} score={score} coverages={[round(c,3) for c in cov]}")
    p = pathlib.Path(__file__).parent / "figures_groundtruth.json"
    p.write_text(json.dumps(out, indent=1))
    print("wrote", p)


main()

# Also emit a flat fixture the C++ test can read without a JSON parser.
def emit_txt():
    gt = json.loads((pathlib.Path(__file__).parent / "figures_groundtruth.json").read_text())
    lines = []
    for fig in ("figure4", "figure5"):
        g = gt[fig]
        lines.append(f"FIGURE {fig} {g['tau']} {g['expected_service_score']}")
        lines.append(f"NB {len(g['buildings'])}")
        for p in g["buildings"]:
            lines.append("B " + str(len(p)) + " " + " ".join(f"{x!r} {y!r}" for x, y in p))
        lines.append(f"NA {len(g['antennas'])}")
        for x, y in g["antennas"]:
            lines.append(f"A {x!r} {y!r}")
        lines.append("COV " + " ".join(repr(c) for c in g["expected_coverage"]))
    p = pathlib.Path(__file__).parent / "figures_groundtruth.txt"
    p.write_text("\n".join(lines) + "\n")
    print("wrote", p)

emit_txt()
