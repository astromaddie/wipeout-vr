#!/usr/bin/env python3
"""Write the cockpit mockup as one self-contained HTML file.

    python3 tools/mockup/build_cockpit_mockup.py

Reads build/mockup-data/ (written by export_mockup, see build.sh), inlines a
pinned three.js (fetched once, SHA-256 checked, cached in build/mockup-cache/),
the data as base64 and cockpit_mockup.js, and writes
build/mockups/cockpit-mockup.html. It opens from file:// with no server.

The output embeds game data: it lives under build/ (git-ignored) and must never
be committed or shared.
"""
import base64
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DATA = ROOT / "build" / "mockup-data"
CACHE = ROOT / "build" / "mockup-cache"
OUT = ROOT / "build" / "mockups" / "cockpit-mockup.html"

THREE_VERSION = "0.158.0"
THREE_URL = f"https://cdn.jsdelivr.net/npm/three@{THREE_VERSION}/build/three.min.js"
THREE_SHA256 = "3b3a1c97de3ce65ec5b28c28d9f2805a738e9651622691ba771aa95c79d41d51"


def three_js():
    path = CACHE / f"three-{THREE_VERSION}.min.js"
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        print(f"fetching {THREE_URL}")
        with urllib.request.urlopen(THREE_URL) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if digest != THREE_SHA256:
            sys.exit(f"three.js SHA-256 mismatch: got {digest}, want {THREE_SHA256}")
        path.write_bytes(data)
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != THREE_SHA256:
        sys.exit(f"cached {path} has SHA-256 {digest}, want {THREE_SHA256}; delete it to refetch")
    # keep a literal "</script" from ending the inline script
    return data.decode("utf-8").replace("</script", "<\\/script")


def speedo_table():
    """The speedo bar table from src/wipeout/hud.c, so the bars match the game."""
    src = (ROOT / "src" / "wipeout" / "hud.c").read_text()
    width = int(re.search(r"\.width\s*=\s*(\d+)", src).group(1))
    skew = int(re.search(r"\.skew\s*=\s*(\d+)", src).group(1))
    bars = []
    pat = re.compile(r"\{\{\.x\s*=\s*(\d+),\s*\.y\s*=\s*(\d+)\},\s*\.height\s*=\s*(\d+),\s*\.color\s*=\s*rgba\(\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)\}")
    for m in pat.finditer(src):
        x, y, h, r, g, b, a = map(int, m.groups())
        bars.append({"x": x, "y": y, "height": h, "color": [r, g, b, a]})
    if len(bars) != 13:
        sys.exit(f"expected 13 speedo bars in hud.c, found {len(bars)}")
    return {"width": width, "skew": skew, "bars": bars}


CSS = """
html,body{margin:0;height:100%;background:#000;color:#ddd;font:12px/1.35 -apple-system,Helvetica,Arial,sans-serif;overflow:hidden}
#wrap{display:flex;height:100%}
#stage{position:relative;flex:1;min-width:0}
#view{width:100%;height:100%;display:block;cursor:grab}
#caption{position:absolute;left:10px;top:8px;right:10px;color:#f0c840;font-size:13px;text-shadow:0 1px 2px #000;pointer-events:none}
#blurb{position:absolute;left:10px;bottom:8px;right:10px;color:#cfd6df;font-size:12px;text-shadow:0 1px 2px #000;pointer-events:none}
#panel{width:330px;flex:none;overflow:auto;background:#121418;border-left:1px solid #2a2e35;padding:10px 12px;box-sizing:border-box}
#panel h3{margin:12px 0 4px;font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:#f0c840}
#panel button{background:#23272e;color:#ddd;border:1px solid #3a404a;border-radius:3px;padding:3px 6px;margin:2px;cursor:pointer;font-size:11px}
#panel button.on{background:#f0c840;color:#111;border-color:#f0c840}
#panel select{width:100%;background:#23272e;color:#ddd;border:1px solid #3a404a}
#panel label{display:inline-block;width:48%;font-size:11px}
#panel input[type=range]{width:62%;vertical-align:middle}
.row{margin:3px 0}.row span{display:inline-block;width:70px}
.small{font-size:10.5px;color:#9aa3ad;margin-top:4px}
table{border-collapse:collapse;width:100%;font-size:10.5px}td,th{padding:1px 3px;text-align:left;border-bottom:1px solid #22262c}
td.bad{color:#ff7060}
body.shot #panel{display:none}
#checks{display:none}
#trim .trow{font-size:10.5px;color:#9aa3ad;margin:1px 0}#trim .trow.on{color:#f0c840}
#trim .sw{display:inline-block;width:14px;height:10px;margin-right:2px;vertical-align:middle;border:1px solid #000}
#panel a{color:#7fb8ff}
"""

PANEL = """
<div id="panel">
  <h3>Ship</h3><select id="ship"></select>
  <div class="small">Trim, sampled from each ship's textures (primary, secondary):</div><div id="trim"></div>
  <div class="small"><a href="cockpit-assets.png" target="_blank">Asset sheet (cockpit-assets.png)</a> &middot; <a href="cockpit-hybrid-sheet.png" target="_blank">hybrid screenshots</a></div>
  <h3>Variation</h3><div id="variations"></div>
  <h3>View</h3>
  <select id="viewsel"><option value="cockpit">cockpit (Frame per-eye FOV, mono)</option><option value="outside">outside (cabin in ship)</option></select>
  <div id="looks"></div>
  <div class="small">Drag to look (head yaw/pitch). Outside: drag to orbit, wheel to zoom.</div>
  <h3>Eye</h3>
  <div class="row"><span>fore/aft</span><input id="eyef" type="range" min="-30" max="30" step="1"> <b id="eyefv"></b></div>
  <div class="row"><span>height</span><input id="eyeu" type="range" min="-30" max="30" step="1"> <b id="eyeuv"></b></div>
  <h3>World scale</h3>
  <div id="scales"></div>
  <div class="row"><span>world_scale</span><input id="ws" type="range" min="64" max="256" step="4"> <b id="wsv"></b></div>
  <div class="small">The cockpit is sized for a real pilot at the chosen scale, with the eye fitted so the tub floor clears every hull (except "game eye").</div>
  <div class="small" id="scaleinfo"></div>
  <h3>Bank</h3>
  <select id="bank">
    <option value="level">level in banks (default: yaw + pitch only)</option>
    <option value="follow">bank-follow (opt-in: view rolls with the ship)</option>
    <option value="cockpit">alt: cockpit banks around a level head</option>
  </select>
  <h3>Motion</h3>
  <div class="row"><button id="play"></button></div>
  <div class="row"><span>speed</span><input id="speed" type="range" min="0" max="20000" step="250"> <b id="speedv"></b></div>
  <div class="row"><span>lap pos</span><input id="at" type="range" min="0" max="1" step="0.001"> <b id="atv"></b></div>
  <h3>Components</h3><div id="components"></div>
  <h3>Checks</h3><div id="checkview"></div>
  <div class="small">text&deg;: smallest glyph height seen from the eye (floor 0.6&deg;, standard.md 3). blocked: eye rays to a 5x4 grid on the face that hit hull or frame. poke: hull/part edges crossing the face.</div>
</div>
"""


def main():
    manifest = json.loads((DATA / "manifest.json").read_text())
    bins = {name: base64.b64encode((DATA / f"{name}.bin").read_bytes()).decode()
            for name in ("ships", "track", "scene", "sky", "track_sections")}
    pngs = {p.stem: base64.b64encode(p.read_bytes()).decode() for p in sorted(DATA.glob("*.png"))}
    payload = {"manifest": manifest, "bin": bins, "png": pngs, "speedo": speedo_table()}
    js = (HERE / "cockpit_mockup.js").read_text()

    html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>WipEout VR cockpit mockup</title>
<!-- Generated by tools/mockup/build_cockpit_mockup.py. Contains game data: do not commit or share. -->
<style>{CSS}</style></head>
<body>
<div id="wrap"><div id="stage"><canvas id="view"></canvas><div id="caption"></div><div id="blurb"></div></div>{PANEL}</div>
<pre id="checks"></pre><pre id="export" style="display:none"></pre>
<script>if (new URLSearchParams(location.search).get('shot') === '1') document.body.classList.add('shot');</script>
<script>{three_js()}</script>
<script>window.MOCKUP = {json.dumps(payload, separators=(',', ':'))};</script>
<script>{js}</script>
</body></html>
"""
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(html)
    print(f"wrote {OUT.relative_to(ROOT)} ({OUT.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
