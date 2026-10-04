#!/usr/bin/env python3
"""Screenshots, checks and sheets of build/mockups/cockpit-mockup.html with headless Chrome.

    python3 tools/mockup/shoot_cockpit_mockup.py               # hybrid shots, hybrid sheet, asset sheet
    python3 tools/mockup/shoot_cockpit_mockup.py --variations  # also the C1-C3 comparison set
    python3 tools/mockup/shoot_cockpit_mockup.py --one 'var=C1&ship=3&look=fwd' out.png

Writes build/mockups/shots/*.png, build/mockups/checks.json,
build/mockups/cockpit-hybrid-sheet.png, build/mockups/cockpit-assets.png and,
with --variations, build/mockups/cockpit-contact-sheet.png. WebGL runs on
SwiftShader. Like the mockup itself, the output shows game data: keep it under
build/.
"""
import argparse
import concurrent.futures as cf
import html
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MOCK = ROOT / "build" / "mockups" / "cockpit-mockup.html"
SHOTS = ROOT / "build" / "mockups" / "shots"
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
FLAGS = ["--headless=new", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--hide-scrollbars",
         "--disable-gpu-sandbox", "--no-first-run", "--no-default-browser-check", "--virtual-time-budget=60000"]

# One ship per hull shape: Feisar (0), Qirex (1), AG Systems (3), Auricom (5).
# Feisar and Qirex are the two most different shapes.
SHAPES = [(0, "Feisar"), (1, "Qirex"), (3, "AG Systems"), (5, "Auricom")]
VARIATIONS = ["C1", "C2", "C3"]
SHIPS = [0, 1]


def chrome(args, done, timeout=300):
    """Run headless Chrome until done(stdout_path) is true. With this page Chrome
    writes its output and then does not always exit, so it is stopped here."""
    with tempfile.TemporaryDirectory(prefix="wo-mock-chrome-") as prof:
        out = Path(prof) / "stdout.txt"
        with open(out, "w") as fo:
            p = subprocess.Popen([CHROME, *FLAGS, f"--user-data-dir={prof}/profile", *args],
                                 stdout=fo, stderr=subprocess.DEVNULL)
            t0 = time.time()
            try:
                while time.time() - t0 < timeout:
                    if p.poll() is not None or done(out):
                        time.sleep(1.0)
                        break
                    time.sleep(0.5)
            finally:
                p.terminate()
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
        return out.read_text(errors="replace")


def url(query):
    return f"{MOCK.as_uri()}?shot=1&{query}"


def shot(query, out, size=(1000, 1000)):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    chrome([f"--window-size={size[0]},{size[1]}", f"--screenshot={out}", url(query)], lambda _: out.exists() and out.stat().st_size > 0)
    if not out.exists():
        raise RuntimeError(f"no screenshot for {query}")
    return out


def checks(query):
    dom = chrome(["--dump-dom", url(query)], lambda p: "</html>" in p.read_text(errors="replace"))
    m = re.search(r'<pre id="checks"[^>]*>(.*?)</pre>', dom, re.S)
    if not m or not m.group(1).strip():
        raise RuntimeError(f"no checks for {query}: {dom[-400:]}")
    return json.loads(html.unescape(m.group(1)))


def variations_set():
    # the round-1 designs, at their own setup: 128 with the game's eye
    jobs = []
    for v in VARIATIONS:
        for s in SHIPS:
            jobs.append((f"var={v}&ws=128&eye=game&ship={s}&look=fwd", SHOTS / f"{v}-ship{s}-fwd.png", f"{v} ship {s}: forward"))
        jobs.append((f"var={v}&ws=128&eye=game&ship=0&look=left", SHOTS / f"{v}-ship0-left.png", f"{v}: look left"))
        jobs.append((f"var={v}&ws=128&eye=game&ship=0&look=back", SHOTS / f"{v}-ship0-back.png", f"{v}: look back"))
        jobs.append((f"var={v}&ws=128&eye=game&ship=0&view=outside", SHOTS / f"{v}-ship0-outside.png", f"{v}: outside, cabin in ship"))
    return jobs


def hybrid_set():
    jobs = []
    for s, name in SHAPES:
        for look, cap in (("look=fwd", "forward"), ("look=left", "look left"), ("look=back", "look back"), ("view=outside", "outside")):
            jobs.append((f"var=H&ship={s}&{look}", SHOTS / f"H-ship{s}-{look.split('=')[1]}.png", f"{name}: {cap}"))
    jobs.append(("var=H&ship=0&look=fwd&bank=follow", SHOTS / "H-ship0-bank-follow.png", "bank-follow (opt-in)"))
    jobs.append(("var=H&ship=0&look=fwd&bank=cockpit", SHOTS / "H-ship0-bank-cockpit.png", "cockpit banks, head level (opt-in)"))
    return jobs


# world_scale candidates (round 3), with the round-2 reference first
SCALES = [(128, "game"), (112, "fit"), (100, "fit"), (88, "fit")]
SCALE_SHIPS = [(0, "Feisar"), (3, "AG Systems")]   # AG Systems: the least room above the deck


def scale_query(ws, eye):
    return f"var=H&ws={ws}&eye={eye}"


def scale_set():
    jobs = []
    for ws, eye in SCALES:
        for s, name in SCALE_SHIPS:
            q = scale_query(ws, eye)
            jobs.append((f"{q}&ship={s}&view=outside&oyaw=-90&opitch=6&odist=4.6", SHOTS / f"S{ws}-ship{s}-side.png", f"{name}: side, 1.8 m pilot"))
            jobs.append((f"{q}&ship={s}&look=fwd", SHOTS / f"S{ws}-ship{s}-fwd.png", f"{name}: forward"))
            jobs.append((f"{q}&ship={s}&look=back", SHOTS / f"S{ws}-ship{s}-back.png", f"{name}: look back"))
    return jobs


def scale_table(res):
    rows = []
    for ws, eye in SCALES:
        for s, name in SCALE_SHIPS + [(1, "Qirex"), (5, "Auricom")]:
            r = res[f"S{ws}-{s}"]
            texts = [v["text_deg"] for v in r["instruments"].values() if v["text_deg"]]
            blocked = sum(int(v["blocked"].split("/")[0]) for v in r["instruments"].values())
            pokes = sum(v["pokes"] for v in r["instruments"].values())
            clear = min(r["clearance"].items(), key=lambda kv: kv[1])
            rows.append(f"<tr><td>{ws}{' (game eye)' if eye == 'game' else ''}</td><td>{850 / ws:.2f}</td><td>{name}</td>"
                        f"<td>(0, {r['eye']['game'][1]:.1f}, 0)</td><td>{r['eye']['above_ridge_m']:.2f}</td><td>{min(texts):.2f}&deg;</td>"
                        f"<td>{blocked}</td><td>{pokes}</td><td>{r['nose_visible'] * 100:.0f}%</td><td>{clear[1]:.2f} ({clear[0]})</td><td>{r['clearance']['hull']:.2f}</td></tr>")
    return ("<table><tr><th>world_scale</th><th>ship m</th><th>hull</th><th>eye (game units)</th><th>eye above deck m</th><th>smallest text</th>"
            "<th>blocked rays</th><th>poke-through</th><th>nose visible</th><th>nearest part m</th><th>nearest hull m</th></tr>" + "".join(rows) + "</table>")


def contact_sheet(rows, out, title, subtitle, cols=5, extra=""):
    # rows: [(label, [(png, caption)])]
    cells = []
    for label, items in rows:
        imgs = "".join(f'<figure><img src="{p.as_uri()}"><figcaption>{html.escape(c)}</figcaption></figure>' for p, c in items)
        cells.append(f'<section><h2>{html.escape(label)}</h2><div class="row">{imgs}</div></section>')
    page = f"""<!doctype html><html><head><meta charset="utf-8"><style>
body{{margin:0;background:#0d0f12;color:#ddd;font:15px Helvetica,Arial,sans-serif;padding:14px}}
h1{{margin:0 0 6px;font-size:22px;color:#f0c840}} h2{{margin:14px 0 6px;font-size:17px;color:#f0c840}}
.row{{display:flex;gap:8px}} figure{{margin:0;width:390px}} img{{width:390px;height:390px;display:block;object-fit:cover}}
figcaption{{font-size:13px;color:#b8c0ca;padding:3px 0}}
table{{border-collapse:collapse;margin:10px 0 4px;font-size:14px}} td,th{{padding:2px 10px;border-bottom:1px solid #262a31;text-align:left}} th{{color:#f0c840}}
</style></head><body><h1>{html.escape(title)}</h1>
<div style="color:#9aa3ad">{html.escape(subtitle)}</div>
{extra}{''.join(cells)}</body></html>"""
    tmp = SHOTS / "sheet.html"
    tmp.write_text(page)
    height = 90 + len(rows) * 460 + (40 + 22 * extra.count("<tr>") if extra else 0)
    width = 14 * 2 + cols * 398
    if out.exists():
        out.unlink()
    chrome([f"--window-size={width},{height}", f"--screenshot={out}", tmp.as_uri()], lambda _: out.exists() and out.stat().st_size > 0)
    tmp.unlink()
    return out


def asset_sheet(out):
    dom = chrome(["--dump-dom", url("sheet=assets")], lambda p: "</html>" in p.read_text(errors="replace"))
    m = re.search(r'data-sheet-height="(\d+)"', dom)
    if not m:
        raise RuntimeError("asset sheet did not render")
    return shot("sheet=assets", out, (2400, int(m.group(1))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--one", nargs=2, metavar=("QUERY", "OUT"))
    ap.add_argument("--checks", metavar="QUERY")
    ap.add_argument("--variations", action="store_true", help="also shoot the C1-C3 comparison set")
    ap.add_argument("--scales", action="store_true", help="also shoot the world_scale comparison")
    ap.add_argument("--size", default="1000x1000")
    ap.add_argument("-j", type=int, default=4)
    a = ap.parse_args()
    size = tuple(int(x) for x in a.size.split("x"))
    if a.one:
        print(shot(a.one[0], a.one[1], size))
        return
    if a.checks:
        print(json.dumps(checks(a.checks), indent=1))
        return
    if SHOTS.exists():
        shutil.rmtree(SHOTS)
    SHOTS.mkdir(parents=True)
    jobs = hybrid_set() + (variations_set() if a.variations else []) + (scale_set() if a.scales else [])
    check_queries = {f"H-{name}": f"var=H&ship={s}&look=fwd" for s, name in SHAPES}
    if a.scales:
        check_queries.update({f"S{ws}-{s}": f"{scale_query(ws, eye)}&ship={s}&look=fwd" for ws, eye in SCALES for s in (0, 1, 3, 5)})
    if a.variations:
        check_queries.update({f"{v}-ship{s}": f"var={v}&ws=128&eye=game&ship={s}&look=fwd" for v in VARIATIONS for s in SHIPS})
    with cf.ThreadPoolExecutor(a.j) as ex:
        list(ex.map(lambda j: shot(j[0], j[1], size), jobs))
        res = dict(zip(check_queries, ex.map(checks, check_queries.values())))
    (ROOT / "build" / "mockups" / "checks.json").write_text(json.dumps(res, indent=1))
    by = {j[1].name: (j[1], j[2]) for j in jobs}
    sub = "Mono approximation of the Frame per-eye view (about 110 x 110 degrees). Cockpit H at world_scale 88, eye fitted from a seated pilot."
    rows = [(name, [by[f"H-ship{s}-{v}.png"] for v in ("fwd", "left", "back", "outside")]) for s, name in SHAPES]
    rows.append(("Bank modes (Feisar, same turn)", [(by["H-ship0-fwd.png"][0], "level in banks (default)"), by["H-ship0-bank-follow.png"], by["H-ship0-bank-cockpit.png"]]))
    contact_sheet(rows, ROOT / "build" / "mockups" / "cockpit-hybrid-sheet.png", "WipEout VR cockpit H: slim frame, split dash, on all four hulls", sub, cols=4)
    if a.variations:
        vrows = []
        for v in VARIATIONS:
            vrows.append((v, [by[f"{v}-ship{s}-fwd.png"] for s in SHIPS] + [by[f"{v}-ship0-left.png"], by[f"{v}-ship0-back.png"], by[f"{v}-ship0-outside.png"]]))
        contact_sheet(vrows, ROOT / "build" / "mockups" / "cockpit-contact-sheet.png", "WipEout VR, Cockpit C (full canopy): the three earlier variations", "Round 1 variations at world_scale 128 with the game's eye. Mockup only; nothing is built.")
    if a.scales:
        srows = []
        for ws, eye in SCALES:
            label = f"world_scale {ws}" + (" with the game's eye (round 2 reference)" if eye == "game" else f": eye fitted from a seated pilot, ship {850 / ws:.1f} m long")
            srows.append((label, [by[f"S{ws}-ship{s}-{v}.png"] for s, _ in SCALE_SHIPS for v in ("side", "fwd", "back")]))
        contact_sheet(srows, ROOT / "build" / "mockups" / "cockpit-scale-options.png", "WipEout VR cockpit H: world_scale options",
                      "The cockpit is sized for a real pilot (1.8 m reference, orange) at each scale; the eye is fitted so the tub floor clears every hull. Outside views keep the ship the same size.",
                      cols=6, extra=scale_table(res))
    asset_sheet(ROOT / "build" / "mockups" / "cockpit-assets.png")
    print(f"wrote {len(jobs)} shots, the hybrid sheet and the asset sheet")


if __name__ == "__main__":
    main()
