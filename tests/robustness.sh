#!/usr/bin/env bash
# Run-day robustness suite.
#
# The competition is a 24-hour window with one submission and no second attempt.
# A wrong id-property name or an unhandled geometry variant invalidates every
# block -- a failure mode that costs far more than any algorithmic shortfall.
# The loader claims to handle these cases; this checks that it does, by mutating
# the sample dataset into each shape the real one might plausibly arrive in and
# confirming the whole pipeline still produces a submission that verifies.
#
#   bash tests/robustness.sh
set -u
BIN=${BIN:-./giscup}
SRC=${SRC:-data/GIS-cup-sample-dataset.geojson}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0

variant() {  # name, python-transform
    local name="$1" code="$2"
    python3 - "$SRC" "$TMP/$name.geojson" <<PY 2>/dev/null
import json, sys
d = json.load(open(sys.argv[1]))
F = d["features"][:1500]
$code
json.dump(d, open(sys.argv[2], "w"))
PY
    if [ ! -s "$TMP/$name.geojson" ]; then
        echo "  FAIL $name (could not build variant)"; FAIL=$((FAIL+1)); return
    fi
    if ! $BIN solve --data "$TMP/$name.geojson" --tau 0.5 --k 20 --lns-sec 0 \
            --powers 2 --radius 400 --out "$TMP/$name.sub" >/dev/null 2>&1; then
        echo "  FAIL $name (solve crashed)"; FAIL=$((FAIL+1)); return
    fi
    local out
    out=$($BIN verify --data "$TMP/$name.geojson" --out "$TMP/$name.sub" 2>/dev/null)
    if echo "$out" | grep -q "false=0"; then
        local n
        n=$(echo "$out" | grep -oE 'verified=[0-9]+' | head -1 | cut -d= -f2)
        echo "  ok   $name (serviced $n, no false claims)"; PASS=$((PASS+1))
    else
        echo "  FAIL $name"; echo "$out" | head -3; FAIL=$((FAIL+1))
    fi
}

echo "run-day input robustness:"

variant baseline          'd["features"]=F'
variant id_named_ID       'd["features"]=[{**f,"properties":{"ID":f["properties"]["id"]}} for f in F]'
variant id_string         'd["features"]=[{**f,"properties":{"id":"b%d"%f["properties"]["id"]}} for f in F]'
variant id_building_id    'd["features"]=[{**f,"properties":{"building_id":f["properties"]["id"]}} for f in F]'
variant id_missing        'd["features"]=[{**f,"properties":{}} for f in F]'
variant extra_props       'd["features"]=[{**f,"properties":{**f["properties"],"height":3.5,"name":"x,y"}} for f in F]'
variant multipolygon      'd["features"]=[{**f,"geometry":{"type":"MultiPolygon","coordinates":[f["geometry"]["coordinates"]]}} for f in F]'
variant ccw_rings         'd["features"]=[{**f,"geometry":{**f["geometry"],"coordinates":[list(reversed(r)) for r in f["geometry"]["coordinates"]]}} for f in F]'
variant unclosed_rings    'd["features"]=[{**f,"geometry":{**f["geometry"],"coordinates":[r[:-1] for r in f["geometry"]["coordinates"]]}} for f in F]'
variant xyz_coords        'd["features"]=[{**f,"geometry":{**f["geometry"],"coordinates":[[[p[0],p[1],0.0] for p in r] for r in f["geometry"]["coordinates"]]}} for f in F]'
variant no_crs            'd["features"]=F; d.pop("crs",None)'
variant shifted_origin    'd["features"]=[{**f,"geometry":{**f["geometry"],"coordinates":[[[p[0]-480000,p[1]-3765000] for p in r] for r in f["geometry"]["coordinates"]]}} for f in F]'
variant duplicate_vertex  'd["features"]=[{**f,"geometry":{**f["geometry"],"coordinates":[[r[0]]+r for r in f["geometry"]["coordinates"]]}} for f in F]'
variant k_exceeds_cands   'd["features"]=F[:3]'

echo
echo "passed $PASS, failed $FAIL"
[ "$FAIL" -eq 0 ]
