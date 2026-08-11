#!/usr/bin/env python3
"""Tile the sample dataset into a larger synthetic one, for scale testing.

The real evaluation dataset lands with a 24-hour clock running, so the time to
find out the pipeline does not fit is now, not then. Tiling preserves local
density (which is what drives the visibility sweep's cost) while multiplying the
building count.

    python3 tools/scale_dataset.py data/GIS-cup-sample-dataset.geojson 4 out.geojson
"""
import json
import sys


def main():
    src, factor, dst = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    d = json.load(open(src))
    feats = d["features"]

    xs = [p[0] for f in feats for r in f["geometry"]["coordinates"] for p in r]
    ys = [p[1] for f in feats for r in f["geometry"]["coordinates"] for p in r]
    w, h = max(xs) - min(xs), max(ys) - min(ys)

    # Smallest grid with at least `factor` cells, kept close to square so tiles
    # stay adjacent and cross-tile visibility behaves like more of the same city.
    n = 1
    while n * n < factor:
        n += 1
    gap = 30.0  # a plausible street width between tiles

    out, nid = [], 0
    for i in range(n):
        for j in range(n):
            if len(out) >= factor * len(feats):
                break
            dx, dy = i * (w + gap), j * (h + gap)
            for f in feats:
                nid += 1
                out.append({
                    "type": "Feature",
                    "properties": {"id": nid},
                    "geometry": {
                        "type": "Polygon",
                        "coordinates": [[[p[0] + dx, p[1] + dy] for p in r]
                                        for r in f["geometry"]["coordinates"]],
                    },
                })
    json.dump({"type": "FeatureCollection", "crs": d.get("crs"), "features": out},
              open(dst, "w"))
    print(f"{dst}: {len(out)} buildings ({len(out)/len(feats):.1f}x)")


main()
