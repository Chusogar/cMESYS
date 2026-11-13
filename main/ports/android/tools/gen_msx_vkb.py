#!/usr/bin/env python3
"""Draws the MSX's on-screen keyboard for the Android port, the same way as
dsp-cpp's keyboards (tools/gen_cpc464_vkb.py, with its vkb_common.py: drawn
from scratch with FreeSans, sculpted caps, no logos or model badges); dsp-cpp
has no MSX keyboard of its own.

The international MSX layout: the five wide function keys (F1/F6..F5/F10),
SELECT, STOP, HOME/CLS, INS, DEL; ESC .. BS; TAB .. the L-shaped RETURN;
CTRL .. ; ' `; SHIFT .. the dead key; CAPS, GRAPH, SPACE, CODE; the
cursor keys as an inverted T. Key codes are the matrix row * 8 + bit of
msx_core.h. Writes machines/msx/assets/keyboard.png + keyboard.txt (and,
with --inc, an include in dsp-cpp's format).

usage: gen_msx_vkb.py [--inc out.inc] [--preview out.png]
"""
import os
import sys

from vkb_common import Canvas, lerp, write_inc

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "machines", "msx", "assets")

W, H = 1536, 560
U = 76.0
GAP = 0.07
MAIN_W = 15.2
PAD_X = MAIN_W + 0.6
LEFT = (W - (PAD_X + 3.0) * U) / 2
TOP = 34.0

CASE_TOP = (52, 53, 57)
CASE_BOT = (34, 35, 38)

# Cap palettes: top face, top face lower edge, side skirt, front skirt, legend.
PAL = {
    "black": ((52, 53, 56), (40, 41, 44), (30, 31, 34), (20, 21, 23), (238, 238, 234)),
    "grey": ((176, 178, 180), (156, 158, 160), (124, 126, 128), (98, 100, 102), (24, 24, 26)),
    "dark": ((86, 88, 92), (72, 74, 78), (56, 58, 62), (42, 44, 48), (240, 240, 236)),
    "red": ((196, 44, 42), (170, 32, 30), (136, 24, 22), (104, 16, 14), (250, 244, 240)),
}

c = Canvas(W, H)
keys = []

# Case: a plain charcoal top with a lighter back edge.
c.shadow([(14, 10, W - 14, H - 4, 18)], 7, alpha=170)
c.vgrad_rrect(8, 4, W - 8, H - 8, 16, CASE_TOP, CASE_BOT)
c.vgrad_rrect(8, 4, W - 8, 12, 6, (90, 91, 95), CASE_TOP)


def ux(v):
    return LEFT + v * U


def uy(v):
    return TOP + v * U


def well(x1, y1, x2, y2):
    c.rrect(ux(x1) - 7, uy(y1) - 7, ux(x2) + 7, uy(y2) + 8, 8, (20, 20, 22, 255))
    c.rrect(ux(x1) - 5, uy(y1) - 5, ux(x2) + 5, uy(y2) + 5, 7, (14, 14, 16, 255))


RF = 0.0       # function row
R = 1.1        # first main row
well(0, RF, MAIN_W, RF + 0.8)
well(0, R, MAIN_W, R + 5)
well(PAD_X, R + 3, PAD_X + 3, R + 5)


def cap(x, y, w, h, legends, pal="black", size=None, code=None, flags=0, name="", notch=None):
    """Key at (x, y) in units; notch=(dx, dh) makes an L (RETURN)."""
    top, top2, side, front, ink = PAL[pal]
    g = GAP * U
    x1, y1, x2, y2 = ux(x) + g / 2, uy(y) + g / 2, ux(x + w) - g / 2, uy(y + h) - g / 2
    c.shadow([(x1, y1 + 3, x2, y2 + 4, 8)], 3, alpha=220)
    c.rrect(x1, y1, x2, y2, 7, side + (255,))
    ix, it, ib = 0.10 * U, 0.04 * U, 0.19 * U
    if h < 1:
        ib = 0.15 * U
    tx1, ty1, tx2, ty2 = x1 + ix, y1 + it, x2 - ix, y2 - ib
    for i in range(c.s(y2 - 2) - c.s(ty2)):
        t = i / max(1, c.s(y2 - 2) - c.s(ty2))
        yy = c.s(ty2) + i
        xa = c.s(tx1) + (c.s(x1 + 3) - c.s(tx1)) * t
        xb = c.s(tx2) + (c.s(x2 - 3) - c.s(tx2)) * t
        c.draw.line([(xa, yy), (xb, yy)], fill=lerp(front, lerp(front, (0, 0, 0), 0.4), t) + (255,))
    for i in range(c.s(ix)):
        t = i / max(1, c.s(ix))
        c.draw.line([(c.s(x1) + i, c.s(y1 + 6) + i // 3), (c.s(x1) + i, c.s(y2 - 6))],
                    fill=lerp(lerp(side, (255, 255, 255), 0.12), side, t) + (255,))
        c.draw.line([(c.s(x2) - i, c.s(y1 + 6) + i // 3), (c.s(x2) - i, c.s(y2 - 6))],
                    fill=lerp(lerp(side, (0, 0, 0), 0.25), side, t) + (255,))
    c.vgrad_rrect(tx1, ty1, tx2, ty2, 6, None, None,
                  stops=[(0.0, lerp(top, (255, 255, 255), 0.20)), (0.07, top), (0.55, lerp(top, top2, 0.6)),
                         (1.0, top2)])
    lx1 = tx1
    if notch:
        dx, dh = notch
        nx2 = ux(x + dx) - g / 2
        ny1 = uy(y + dh) + g / 2
        c.rrect(x1 - 4, ny1 - 1, nx2 + 1, y2 + 6, 4, (14, 14, 16, 255))
        c.draw.line([(c.s(x1 + 2), c.s(ny1 - 1)), (c.s(nx2), c.s(ny1 - 1))], fill=front + (255,), width=c.s(3))
        c.draw.line([(c.s(nx2 + 1), c.s(ny1)), (c.s(nx2 + 1), c.s(y2 - 3))], fill=side + (255,), width=c.s(3))
        lx1 = nx2
    sz = size or 0.30 * U
    if len(legends) == 1:
        c.text((lx1 + tx2) / 2, (ty1 + ty2) / 2, legends[0], sz, ink, "mm", max_w=tx2 - lx1 - 6)
    elif len(legends) == 2 and size is None:
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.30, legends[0], 0.24 * U, ink, "mm")
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.74, legends[1], 0.27 * U, ink, "mm")
    elif len(legends) == 2:
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.32, legends[0], sz, ink, "mm", max_w=tx2 - tx1 - 6)
        c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.70, legends[1], sz, ink, "mm", max_w=tx2 - tx1 - 6)
    if code is not None:
        keys.append((code, flags, (x1, y1, x2, y2), name or " ".join(legends)))


def k(r, b):
    return r * 8 + b


small = 0.17 * U

# Function row: F1/F6 .. F5/F10, SELECT, STOP, HOME/CLS, INS, DEL.
fk = [k(6, 5), k(6, 6), k(6, 7), k(7, 0), k(7, 1)]
for i, code in enumerate(fk):
    cap(i * 1.7, RF, 1.7, 0.8, ["F%d" % (i + 1), "F%d" % (i + 6)], "grey", 0.16 * U, code, name="F%d" % (i + 1))
cap(8.8, RF, 1.3, 0.8, ["SELECT"], "grey", 0.15 * U, k(7, 6))
cap(10.1, RF, 1.3, 0.8, ["STOP"], "red", 0.15 * U, k(7, 4))
cap(11.8, RF, 1.15, 0.8, ["HOME", "CLS"], "grey", 0.14 * U, k(8, 1), name="HOME")
cap(12.95, RF, 1.1, 0.8, ["INS"], "grey", 0.15 * U, k(8, 2))
cap(14.05, RF, 1.15, 0.8, ["DEL"], "grey", 0.15 * U, k(8, 3))

# Row 1: ESC 1..0 - = \ BS
cap(0, R, 1, 1, ["ESC"], "dark", small, k(7, 2))
row = [(("!", "1"), k(0, 1)), (("@", "2"), k(0, 2)), (("#", "3"), k(0, 3)), (("$", "4"), k(0, 4)),
       (("%", "5"), k(0, 5)), (("^", "6"), k(0, 6)), (("&", "7"), k(0, 7)), (("*", "8"), k(1, 0)),
       (("(", "9"), k(1, 1)), ((")", "0"), k(0, 0)), (("_", "-"), k(1, 2)), (("+", "="), k(1, 3)),
       (("|", "\\"), k(1, 4))]
x = 1.0
for leg, code in row:
    cap(x, R, 1, 1, list(leg), code=code)
    x += 1
cap(x, R, MAIN_W - x, 1, ["BS"], "dark", small, k(7, 5))

# Row 2: TAB Q..P [ ] and the RETURN (rows 2-3)
cap(0, R + 1, 1.5, 1, ["TAB"], "dark", small, k(7, 3))
x = 1.5
for ch in "QWERTYUIOP":
    o = ord(ch) - ord("A")          # A..Z: row 2 bit 6 onwards
    cap(x, R + 1, 1, 1, [ch], code=k(2 + (o + 6) // 8, (o + 6) % 8))
    x += 1
cap(x, R + 1, 1, 1, ["{", "["], code=k(1, 5))
cap(x + 1, R + 1, 1, 1, ["}", "]"], code=k(1, 6))
RET_X = x + 2
# (before row 3, whose ~` key the notch must not cover)
cap(RET_X, R + 1, MAIN_W - RET_X, 2, ["RETURN"], "dark", small, k(7, 7), notch=(1.75 + 9 + 3 - RET_X, 1.0))

# Row 3: CTRL A..L ; ' `
cap(0, R + 2, 1.75, 1, ["CTRL"], "dark", small, k(6, 1), 1)
x = 1.75
for ch in "ASDFGHJKL":
    o = ord(ch) - ord("A")
    cap(x, R + 2, 1, 1, [ch], code=k(2 + (o + 6) // 8, (o + 6) % 8))
    x += 1
cap(x, R + 2, 1, 1, [":", ";"], code=k(1, 7))
cap(x + 1, R + 2, 1, 1, ['"', "'"], code=k(2, 0))
cap(x + 2, R + 2, 1, 1, ["~", "`"], code=k(2, 1))

# Row 4: SHIFT Z..M , . / dead key SHIFT
cap(0, R + 3, 2.25, 1, ["SHIFT"], "dark", small, k(6, 0), 1, "SHIFT (left)")
x = 2.25
for ch in "ZXCVBNM":
    o = ord(ch) - ord("A")
    cap(x, R + 3, 1, 1, [ch], code=k(2 + (o + 6) // 8, (o + 6) % 8))
    x += 1
for leg, code in [(("<", ","), k(2, 2)), ((">", "."), k(2, 3)), (("?", "/"), k(2, 4)), (("^", "´"), k(2, 5))]:
    cap(x, R + 3, 1, 1, list(leg), code=code)
    x += 1
cap(x, R + 3, MAIN_W - x, 1, ["SHIFT"], "dark", small, k(6, 0), 1, "SHIFT (right)")

# Row 5: CAPS GRAPH SPACE CODE
cap(0.5, R + 4, 1.5, 1, ["CAPS"], "dark", small, k(6, 3))
cap(2.0, R + 4, 1.5, 1, ["GRAPH"], "dark", small, k(6, 2), 1)
cap(3.5, R + 4, 8.0, 1, [""], code=k(8, 0), name="SPACE")
cap(11.5, R + 4, 1.5, 1, ["CODE"], "dark", small, k(6, 4), 1)

# Cursor keys: an inverted T beside the main block.
cap(PAD_X + 1, R + 3, 1, 1, ["↑"], "grey", 0.34 * U, k(8, 5))
cap(PAD_X, R + 4, 1, 1, ["←"], "grey", 0.34 * U, k(8, 4))
cap(PAD_X + 1, R + 4, 1, 1, ["↓"], "grey", 0.34 * U, k(8, 6))
cap(PAD_X + 2, R + 4, 1, 1, ["→"], "grey", 0.34 * U, k(8, 7))

# The L-shaped RETURN's rectangle overlaps "~`": list it last (the smaller
# key wins the hit test).
keys.sort(key=lambda kk: kk[0] == k(7, 7))
img = c.final()

# The app's MENÚ / ▼ buttons: the free case above the cursor keys
g = GAP * U / 2
menu = (ux(PAD_X) + g, uy(R + 0.35), ux(PAD_X + 3) - g, uy(R + 1.25))
hide = (ux(PAD_X) + g, uy(R + 1.55), ux(PAD_X + 3) - g, uy(R + 2.45))

args = sys.argv[1:]
os.makedirs(OUT, exist_ok=True)
img.save(os.path.join(OUT, "keyboard.png"), optimize=True)
with open(os.path.join(OUT, "keyboard.txt"), "w") as f:
    f.write("# tools/gen_msx_vkb.py, do not edit\n")
    f.write("picture keyboard.png %d %d\n" % (W, H))
    f.write("button menu %d %d %d %d\n" % tuple(round(v) for v in menu))
    f.write("button hide %d %d %d %d\n" % tuple(round(v) for v in hide))
    for code, flags, (x1, y1, x2, y2), name in keys:
        f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, round(x1), round(y1), round(x2), round(y2), name))
if "--inc" in args:
    write_inc(args[args.index("--inc") + 1], img, keys, "MsxVkb",
              "// Generated by gen_msx_vkb.py (cMESYS Android port), do not edit.\n"
              "// MSX (international) keyboard; key codes are matrix row * 8 + bit.\n")
if "--preview" in args:
    from PIL import Image
    bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(args[args.index("--preview") + 1])
print("msx: %d keys" % len(keys))
