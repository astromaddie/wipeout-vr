#!/usr/bin/env python3
"""Export the approved cockpit (variation H) as data the build can consume.

    python3 tools/mockup/export_cockpit_geometry.py            # writes tools/mockup/cockpit_geometry.json
    python3 tools/mockup/export_cockpit_geometry.py --c OUT    # also a C table (vr_cockpit_geometry.inc)
    python3 tools/mockup/export_cockpit_geometry.py --from-json --c OUT   # C table from the committed JSON only

The geometry comes from the mockup's own builders (cockpit_mockup.js), run in
headless Chrome against build/mockups/cockpit-mockup.html, so the mockup, the
asset sheet and this data cannot drift apart. The page needs the exported game
data to fit parts to each hull's deck, but the output holds none of it: only
the authored parts (vertices in game units, per hull shape where they differ),
material slot names, face layouts and the trim sampling rule. No hull mesh,
texture, pixel or sampled colour is written.
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import shoot_cockpit_mockup as shoot  # noqa: E402

OUT_JSON = HERE / "cockpit_geometry.json"
FORMAT = "wipeout-vr-cockpit-geometry/1"

SLOTS = {
    # fixed slots carry an authored default colour (sRGB, as the game's
    # gamma-encoded colours); trim slots are sampled per ship at runtime
    "frame": {"kind": "fixed", "rgb": [0xa9, 0xb0, 0xb9], "note": "canopy struts, sill rails, roll hoop (silver)"},
    "frame_dark": {"kind": "fixed", "rgb": [0x4a, 0x4f, 0x58], "note": "coaming rim, bulkhead, pod lips, headrest tower"},
    "cabin": {"kind": "fixed", "rgb": [0x30, 0x34, 0x3b], "note": "tub walls, dash shell"},
    "cabin_dark": {"kind": "fixed", "rgb": [0x1c, 0x1e, 0x22], "note": "tub floor, head pad"},
    "seat": {"kind": "fixed", "rgb": [0x3b, 0x33, 0x37], "note": "seat"},
    "bezel": {"kind": "fixed", "rgb": [0x14, 0x16, 0x19], "note": "pod and keystone bezels behind the faces"},
    "glass": {"kind": "glass", "rgb": [0x9e, 0xd9, 0xff], "alpha": 0.045, "note": "canopy glass: blended, no depth write, drawn last"},
    "trim_primary": {"kind": "trim", "note": "sampled: primary trim colour (coaming stripe, bulkhead cap, keystone surround)"},
    "trim_secondary": {"kind": "trim", "note": "sampled: secondary trim colour (pod lip edges)"},
}


def from_page():
    dom = shoot.chrome(["--dump-dom", shoot.url("export=geometry")], lambda p: "</html>" in p.read_text(errors="replace"))
    m = re.search(r'<pre id="export"[^>]*>(.*?)</pre>', dom, re.S)
    if not m or not m.group(1).strip():
        sys.exit("the page did not export geometry (is build/mockups/cockpit-mockup.html current?)")
    import html
    return json.loads(html.unescape(m.group(1)))


def build_json(page):
    shapes = page["shapes"]
    ids = [s["id"] for s in shapes]
    # a part is shared when its vertices match on every hull shape
    by_name = {}
    for s in shapes:
        for p in s["parts"]:
            by_name.setdefault(p["name"], {})[s["id"]] = p
    parts = []
    for name, per in by_name.items():
        first = per[ids[0]]
        same = len(per) == len(ids) and all(per[i]["tris"] == first["tris"] and per[i]["slot"] == first["slot"] for i in ids)
        if same:
            parts.append({"name": name, "group": first["group"], "slot": first["slot"], "hulls": "all", "tris": first["tris"]})
        else:
            for i in ids:
                if i in per:
                    parts.append({"name": name, "group": per[i]["group"], "slot": per[i]["slot"], "hulls": [i], "tris": per[i]["tris"]})
    faces0 = shapes[0]["faces"]
    for s in shapes[1:]:
        if s["faces"] != faces0:
            sys.exit("instrument faces differ between hull shapes; the format assumes they are shared")
    rule = page["trim_rule"]
    cut = page["cabin_cut"]
    return {
        "format": FORMAT,
        "generated_by": "tools/mockup/export_cockpit_geometry.py from tools/mockup/cockpit_mockup.js (variation H)",
        "design": "Cockpit H: C1's slim frame with C2's split dash; approved by the user 2026-10-04",
        "space": "ship space in game units: +x right, +y down, +z forward (the game's own axes, as object_draw uses them)",
        "eye": [0, -128, 0],
        "reference_world_scale": 128,
        "draw": "every part double-sided (no culling), flat-shaded; glass blended after the opaque parts; faces textured",
        "cabin_cut": {
            "note": "remove the part of the player's hull inside this box (clip polygons, keep the outside parts); y has no upper limit (open top)",
            "x": [round(v, 2) for v in cut["x"]], "y_max": round(cut["y_max"], 2), "z": [round(v, 2) for v in cut["z"]],
        },
        "hull_shapes": [
            {"id": s["id"], "name": s["name"], "models": [{"index": m["index"], "name": m["name"], "tris": m["tris"]} for m in s["models"]]}
            for s in shapes
        ],
        "hull_shape_match": "pick the shape whose model name and captured triangle count match the player's ship; no match: draw no cockpit (keep the flat internal view)",
        "slots": SLOTS,
        "trim_rule": {
            **rule,
            "steps": [
                "for every triangle of the ship model: 6 barycentric samples, the permutations of (2/3,1/6,1/6) and (1/6,5/12,5/12), each weighted by area/6 (game units squared)",
                "sample colour = nearest texel * vertex colour * 2 (as the game's shader shows it), clamped to 255; skip texels with alpha 0",
                "keep samples with HSV saturation >= minSat and value >= minVal",
                "histogram the kept weight by hue in `bins` bins, smoothed circularly with `smooth`",
                "primary = weighted mean RGB of the samples in the peak bin and its two neighbours",
                "secondary = the same around the strongest smoothed bin >= secondaryMinBins bins from the peak with >= secondaryMinWeight of its weight",
                "scale both so the max channel / 255 lies in valueRange, keeping hue and saturation; no secondary: primary * fallbackValue",
                "if the saturated share of the sampled area is under neutralBelowShare: primary = slot frame, secondary = slot frame_dark",
            ],
        },
        "instruments": page["instruments"],
        "faces": faces0,
        "parts": parts,
    }


SLOT_ORDER = list(SLOTS)


def cf(v):
    """A C float literal."""
    t = repr(float(v))
    return t + "f"


def write_c(data, out):
    ids = [s["id"] for s in data["hull_shapes"]]
    verts, rows = [], []
    for p in data["parts"]:
        mask = (1 << len(ids)) - 1 if p["hulls"] == "all" else sum(1 << ids.index(h) for h in p["hulls"])
        first = len(verts) // 3
        verts.extend(p["tris"])
        rows.append((p["name"], p["group"], SLOT_ORDER.index(p["slot"]), mask, first // 3, len(p["tris"]) // 9))
    lines = [
        "// Generated by tools/mockup/export_cockpit_geometry.py from tools/mockup/cockpit_geometry.json.",
        "// Cockpit H geometry in ship space, game units (+y down, +z forward). Do not edit by hand.",
        "// No game data: authored parts only. The player's hull is clipped at runtime (VR_COCKPIT_CUT_*).",
        "",
        f"#define VR_COCKPIT_EYE_Y ({cf(data['eye'][1])})",
        f"#define VR_COCKPIT_CUT_X0 ({cf(data['cabin_cut']['x'][0])})",
        f"#define VR_COCKPIT_CUT_X1 ({cf(data['cabin_cut']['x'][1])})",
        f"#define VR_COCKPIT_CUT_Y_MAX ({cf(data['cabin_cut']['y_max'])})",
        f"#define VR_COCKPIT_CUT_Z0 ({cf(data['cabin_cut']['z'][0])})",
        f"#define VR_COCKPIT_CUT_Z1 ({cf(data['cabin_cut']['z'][1])})",
        "",
        "typedef enum {",
        *[f"\tVR_COCKPIT_SLOT_{s.upper()}," for s in SLOT_ORDER],
        "\tVR_COCKPIT_SLOT_COUNT",
        "} vr_cockpit_slot_t;",
        "",
        "static const uint8_t vr_cockpit_slot_rgb[VR_COCKPIT_SLOT_COUNT][3] = {",
        *[f"\t{{{', '.join(str(c) for c in SLOTS[s].get('rgb', [0, 0, 0]))}}}, // {s}" for s in SLOT_ORDER],
        "};",
        "",
        "// hull shapes, bit order of vr_cockpit_part_t.hull_mask",
        "static const struct { const char *model; uint16_t tris; uint8_t shape; } vr_cockpit_hull_models[] = {",
        *[f'\t{{"{m["name"]}", {m["tris"]}, {k}}}, // {s["name"]}' for k, s in enumerate(data["hull_shapes"]) for m in s["models"]],
        "};",
        "",
        "typedef struct {",
        "\tconst char *name;",
        "\tconst char *group; // component toggle: struts, sills, tub, seat, bulkhead, headrest, hoop, dash, podL, podR, weaponPod, glass",
        "\tuint8_t slot; // vr_cockpit_slot_t",
        "\tuint8_t hull_mask; // which hull shapes use this part",
        "\tuint32_t first_tri, tri_count; // into vr_cockpit_verts (3 vertices per triangle)",
        "} vr_cockpit_part_t;",
        "",
        "static const vr_cockpit_part_t vr_cockpit_parts[] = {",
        *[f'\t{{"{n}", "{g}", {sl}, 0x{mk:x}, {f}, {c}}},' for n, g, sl, mk, f, c in rows],
        "};",
        "",
        "// instrument faces: corners top-left, top-right, bottom-right, bottom-left; texture u right, v down",
        "typedef struct { const char *id; uint16_t tex_w, tex_h; float corners[12]; } vr_cockpit_face_t;",
        "static const vr_cockpit_face_t vr_cockpit_faces[] = {",
        *[f'\t{{"{f["id"]}", {f["texture"][0]}, {f["texture"][1]}, {{{", ".join(cf(v) for v in f["corners"])}}}}},' for f in data["faces"]],
        "};",
        "",
        "typedef struct { const char *face, *element; int16_t x, y; uint8_t ui_scale; } vr_cockpit_element_t;",
        "static const vr_cockpit_element_t vr_cockpit_elements[] = {",
        *[f'\t{{"{f["id"]}", "{e["id"]}", {e["x"]}, {e["y"]}, {e["ui_scale"]}}},' for f in data["faces"] for e in f["elements"]],
        "};",
        "",
        f"static const float vr_cockpit_verts[{len(verts)}] = {{",
    ]
    for i in range(0, len(verts), 9):
        lines.append("\t" + ", ".join(cf(v) for v in verts[i:i + 9]) + ",")
    lines.append("};")
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(rows)} parts, {len(verts) // 9} triangles)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--c", metavar="OUT", help="also write a C table")
    ap.add_argument("--from-json", action="store_true", help="read the committed JSON instead of running the page")
    a = ap.parse_args()
    if a.from_json:
        data = json.loads(OUT_JSON.read_text())
    else:
        data = build_json(from_page())
        # one part per line keeps diffs reviewable
        text = json.dumps({k: v for k, v in data.items() if k != "parts"}, indent=1)[:-2]
        text += ',\n "parts": [\n' + ",\n".join("  " + json.dumps(p, separators=(",", ":")) for p in data["parts"]) + "\n ]\n}\n"
        OUT_JSON.write_text(text)
        tris = sum(len(p["tris"]) // 9 for p in data["parts"])
        print(f"wrote {OUT_JSON.relative_to(HERE.parent.parent)} ({len(data['parts'])} parts, {tris} triangles, {OUT_JSON.stat().st_size / 1e3:.0f} kB)")
    if a.c:
        write_c(data, a.c)


if __name__ == "__main__":
    main()
