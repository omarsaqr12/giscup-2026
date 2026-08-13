#!/usr/bin/env python3
"""Report the loader-relevant facts about a footprint dataset.

Run this first on any unseen file. Every check here corresponds to something
that was *not* what the spec implied on the 2026 sample (FINDINGS.md §2) and
would have caused a silent failure rather than an error:

  * rings arrived clockwise, opposite to the GeoJSON convention
  * one feature carried a second ring despite "no holes"
  * the id property could plausibly be named anything

    python3 tools/inspect_dataset.py data/file.geojson
"""
import collections
import json
import math
import sys


def ring_area2(r):
    return sum(r[i][0] * r[(i + 1) % len(r)][1] - r[(i + 1) % len(r)][0] * r[i][1]
               for i in range(len(r)))


def main():
    path = sys.argv[1]
    d = json.load(open(path))
    F = d.get("features", [])
    problems, notes = [], []

    print(f"file            {path}")
    print(f"type            {d.get('type')}")
    print(f"crs             {json.dumps(d.get('crs')) if d.get('crs') else '(absent)'}")
    print(f"features        {len(F):,}")
    if not F:
        print("\nNO FEATURES -- stop.")
        return 1

    # --- geometry types ----------------------------------------------------
    gt = collections.Counter(f["geometry"]["type"] for f in F if f.get("geometry"))
    print(f"geometry types  {dict(gt)}")
    if set(gt) - {"Polygon", "MultiPolygon"}:
        problems.append(f"unexpected geometry types: {set(gt) - {'Polygon', 'MultiPolygon'}}")

    # --- id property -------------------------------------------------------
    keys = collections.Counter(k for f in F for k in (f.get("properties") or {}))
    print(f"property keys   {dict(keys)}")
    known = ["id", "ID", "building_id", "fid", "OBJECTID", "osm_id"]
    id_key = next((k for k in known if keys.get(k) == len(F)), None)
    if id_key is None:
        partial = [k for k in known if k in keys]
        if partial:
            problems.append(f"id-like key {partial} present but not on every feature")
        else:
            notes.append("no recognised id property; loader will assign sequential ids "
                         f"(recognised names: {known})")
    else:
        vals = [f["properties"][id_key] for f in F]
        print(f"id property     '{id_key}'  type={type(vals[0]).__name__}  "
              f"unique={len(set(map(str, vals))) == len(vals)}")
        if len(set(map(str, vals))) != len(vals):
            problems.append(f"id property '{id_key}' is NOT unique -- claims may be ambiguous")

    # --- rings -------------------------------------------------------------
    nring = collections.Counter()
    orient = collections.Counter()
    nv, per, xs, ys = [], [], [], []
    degenerate = 0
    for f in F:
        g = f["geometry"]
        polys = g["coordinates"] if g["type"] == "Polygon" else [p for p in g["coordinates"]]
        if g["type"] == "MultiPolygon":
            polys = [r for poly in g["coordinates"] for r in poly]
        nring[len(g["coordinates"]) if g["type"] == "Polygon" else len(polys)] += 1
        outer = polys[0]
        r = outer[:-1] if len(outer) > 1 and outer[0] == outer[-1] else outer
        if len(r) < 3:
            degenerate += 1
            continue
        orient["ccw" if ring_area2(r) > 0 else "cw"] += 1
        nv.append(len(r))
        per.append(sum(math.dist(r[i], r[(i + 1) % len(r)]) for i in range(len(r))))
        for p in r:
            xs.append(p[0]); ys.append(p[1])

    print(f"rings/feature   {dict(nring)}")
    print(f"orientation     {dict(orient)}  (loader normalises to CCW)")
    if nring and max(nring) > 1:
        notes.append(f"{sum(v for k, v in nring.items() if k > 1)} feature(s) carry >1 ring "
                     "despite 'no holes'; loader keeps the largest-area ring")
    if degenerate:
        problems.append(f"{degenerate} feature(s) with <3 distinct vertices")

    coord_dims = {len(p) for f in F[:200] for r in
                  ([f["geometry"]["coordinates"][0]] if f["geometry"]["type"] == "Polygon"
                   else [f["geometry"]["coordinates"][0][0]]) for p in r}
    print(f"coordinate dims {sorted(coord_dims)}")

    per.sort()
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    print(f"vertices        total {sum(nv):,}  median {sorted(nv)[len(nv)//2]}  max {max(nv)}")
    print(f"perimeter (m)   median {per[len(per)//2]:.1f}  mean {sum(per)/len(per):.1f}  "
          f"max {per[-1]:.1f}")
    print(f"extent          {w:,.0f} m x {h:,.0f} m")
    print(f"density         {len(F)/(w*h/1e6):,.0f} buildings/km^2")

    # Planar check: lat/lon would give a tiny extent and a fractional perimeter.
    if abs(min(xs)) <= 180 and abs(max(xs)) <= 180 and abs(min(ys)) <= 90 and abs(max(ys)) <= 90:
        problems.append("coordinates look like lon/lat degrees, not a planar CRS -- "
                        "distances would be meaningless")

    scale = len(F) / 12860
    print(f"\nscale vs sample {scale:.1f}x  "
          f"(projected precompute ~{4.7*scale:.0f}s at radius 600, "
          f"~{0.6*scale:.1f} GB)")

    print()
    for p in problems:
        print(f"PROBLEM  {p}")
    for n in notes:
        print(f"note     {n}")
    if not problems:
        print("no blocking problems found")
    return 1 if problems else 0


sys.exit(main())
