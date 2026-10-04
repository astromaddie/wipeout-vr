#!/usr/bin/env python3
"""Compare the build's cockpit render with the approved mockup shot.

    python3 tools/mockup/compare_cockpit_render.py HARNESS.png MOCKUP.png OUT.png [--mask-labels]

HARNESS.png is an eye image from tests/vr/vr_render_harness (build/vr-harness/cockpit-real/<ship>-eye.png:
the real cockpit on black, at the mockup's field of view, 836 x 858). MOCKUP.png is the same ship from
the page with the track and sky hidden: `shoot_cockpit_mockup.py --one 'var=H&ship=0&look=fwd&bg=black' MOCKUP.png`.

The page draws its 836 pixel wide view into 1000 rows, so the harness image is stretched to that before
the two are compared. The faces differ in what they show (the page's demo values against the harness's),
and the page's two caption lines sit in the top and bottom 40 rows: those are left out.

Prints the share of the cockpit's pixels that both draw (the overlap of the two masks), the mean colour
difference over pixels either draws, and the share of those within 32 levels. Writes a side-by-side:
build | mockup | difference. Pure Python, no dependencies. The images show game data: keep them under build/.
"""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w, h, ct = 8, b"", 0, 0, 2
    while pos < len(d):
        (n,) = struct.unpack(">I", d[pos:pos + 4])
        kind, body = d[pos + 4:pos + 8], d[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, _, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    bpp = {2: 3, 6: 4}[ct]
    rows, prev, i = [], bytearray(w * bpp), 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + w * bpp])
        i += w * bpp
        for x in range(len(line)):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line)
        prev = line
    # RGB rows as lists of (r, g, b)
    return w, h, [[(r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]) for x in range(w)] for r in rows]


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def stretch_rows(rows, new_h):
    h = len(rows)
    out = []
    for y in range(new_h):
        t = (y + 0.5) * h / new_h - 0.5
        y0 = max(0, min(h - 1, int(t // 1)))
        y1 = min(h - 1, y0 + 1)
        f = min(1.0, max(0.0, t - y0))
        a, b = rows[y0], rows[y1]
        out.append([(int(p[0] + (q[0] - p[0]) * f + 0.5), int(p[1] + (q[1] - p[1]) * f + 0.5), int(p[2] + (q[2] - p[2]) * f + 0.5)) for p, q in zip(a, b)])
    return out


def drawn(px, floor=16):
    return max(px) > floor


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 3:
        sys.exit(__doc__)
    hw, hh, harness = read_png(args[0])
    mw, mh, mock = read_png(args[1])

    # the page's view: the columns that are not the black bars at the sides
    left = min((x for y in range(100, mh - 100, 20) for x in range(mw) if drawn(mock[y][x], 4)), default=0)
    right = max((x for y in range(100, mh - 100, 20) for x in range(mw) if drawn(mock[y][x], 4)), default=mw - 1)
    # the harness image and the page cover the same field of view; the page puts it into 836 x mh
    width = right - left + 1
    crop = [row[left:right + 1] for row in mock]
    if hw != width:
        sys.exit(f"harness image is {hw} wide, the page's view {width}: render the harness at {width} x {round(width / 0.974)}")
    build = stretch_rows(harness, mh)

    top, bottom = 40, mh - 40
    both = either = near = 0
    diff_sum = 0
    lower_both = lower_either = 0
    diff_rows = []
    for y in range(mh):
        drow = []
        for x in range(width):
            a, b = build[y][x], crop[y][x]
            da, db = drawn(a), drawn(b)
            d = max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))
            drow.append((min(255, d * 3),) * 3)
            if top <= y < bottom:
                if da or db:
                    either += 1
                    diff_sum += (abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])) / 3
                    near += d <= 32
                    if y > mh // 2:
                        lower_either += 1
                        lower_both += da and db
                if da and db:
                    both += 1
        diff_rows.append(drow)
    print(f"pixels drawn by the build or the mockup: {either}")
    print(f"  drawn by both (mask overlap): {both / either * 100:.1f}%   lower half: {lower_both / lower_either * 100:.1f}%")
    print(f"  mean colour difference over them: {diff_sum / either:.1f} levels")
    print(f"  within 32 levels in every channel: {near / either * 100:.1f}%")

    montage = [b + [(40, 40, 40)] * 4 + c + [(40, 40, 40)] * 4 + d for b, c, d in zip(build, crop, diff_rows)]
    write_png(args[2], width * 3 + 8, mh, montage)
    print(f"wrote {args[2]} (build | mockup | difference x3)")


if __name__ == "__main__":
    main()
