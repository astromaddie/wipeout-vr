#!/usr/bin/env python3
"""Check that src/vr/vr_cockpit_geometry.inc is what tools/mockup/cockpit_geometry.json generates.

    python3 tools/mockup/check_cockpit_geometry.py            # exits non-zero on a mismatch

Runs the exporter's --from-json step into a temporary file and compares it with the committed table, then
checks the JSON's own counts (parts, triangles, slots, hull shapes, faces). Needs no game data and no browser.
It also checks that neither file holds anything but authored geometry: no key outside the documented set.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
INC = ROOT / "src" / "vr" / "vr_cockpit_geometry.inc"
JSON = HERE / "cockpit_geometry.json"
KEYS = {"format", "generated_by", "design", "space", "world_scale", "eye", "eye_note", "fit", "regenerate", "draw", "cabin_cut",
        "hull_shapes", "hull_shape_match", "slots", "trim_rule", "instruments", "faces", "parts"}


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    data = json.loads(JSON.read_text())
    if set(data) != KEYS:
        fail(f"unexpected keys in the JSON: {sorted(set(data) ^ KEYS)}")
    if data["format"] != "wipeout-vr-cockpit-geometry/1":
        fail("format")
    slots = set(data["slots"])
    for p in data["parts"]:
        if p["slot"] not in slots:
            fail(f"part {p['name']} names an unknown slot {p['slot']}")
        if len(p["tris"]) % 9:
            fail(f"part {p['name']} has a partial triangle")
        if p["hulls"] != "all" and not set(p["hulls"]) <= {s["id"] for s in data["hull_shapes"]}:
            fail(f"part {p['name']} names an unknown hull shape")
    if len(data["hull_shapes"]) != 4 or len(data["faces"]) != 3:
        fail("expected 4 hull shapes and 3 faces")
    for s in data["slots"].values():
        if s["kind"] == "trim" and "rgb" in s:
            fail("a trim slot carries a colour: trim is sampled at load, never stored")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "vr_cockpit_geometry.inc"
        subprocess.run([sys.executable, str(HERE / "export_cockpit_geometry.py"), "--from-json", "--c", str(out)], check=True, stdout=subprocess.DEVNULL)
        if out.read_text() != INC.read_text():
            fail("src/vr/vr_cockpit_geometry.inc is not what cockpit_geometry.json generates: run export_cockpit_geometry.py --from-json --c src/vr/vr_cockpit_geometry.inc")
    tris = sum(len(p["tris"]) // 9 for p in data["parts"])
    print(f"ok: {len(data['parts'])} parts, {tris} triangles, world_scale {data['world_scale']}, eye {data['eye']}")


if __name__ == "__main__":
    main()
