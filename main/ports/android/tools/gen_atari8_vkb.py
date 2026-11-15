#!/usr/bin/env python3
"""Draws the Atari 800XL's on-screen keyboard for the Android port, the same
way as dsp-cpp's keyboards (vkb_common.py: drawn from scratch with FreeSans,
sculpted caps, no logos or model badges); dsp-cpp has no Atari 8-bit
keyboard of its own.

The XL's layout: the console keys (START, SELECT, OPTION) and BREAK in a
row above; ESC 1..0 < > DELETE; TAB Q..P - = RETURN; CONTROL A..L ; + *
CAPS; SHIFT Z../ SHIFT and the inverse key; the space bar. Key codes are
POKEY's scan codes (atari8_core.h, 0-63) and jni/m_atari8.c's 64 SHIFT,
65 CONTROL, 66 BREAK, 67 START, 68 SELECT, 69 OPTION. Writes
machines/atari800xl/assets/keyboard.png + keyboard.txt.

usage: gen_atari8_vkb.py [--preview out.png]
"""
import os
import sys

from vkb_common import Canvas, lerp

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "machines", "atari800xl", "assets")

W, H = 1536, 560
U = 76.0
GAP = 0.07
MAIN_W = 15.2
PAD_X = MAIN_W + 0.6
LEFT = (W - (PAD_X + 2.4) * U) / 2
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

# Case: the XL's charcoal top with a lighter back edge.
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
well(PAD_X, R + 2, PAD_X + 2.4, R + 5)


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


small = 0.17 * U
SH, CT, BRK, START, SELECT, OPTION = 64, 65, 66, 67, 68, 69

# Console row: START SELECT OPTION (the XL has them on the right) and BREAK
cap(0, RF, 2.0, 0.8, ["START"], "grey", 0.16 * U, START)
cap(2.0, RF, 2.0, 0.8, ["SELECT"], "grey", 0.16 * U, SELECT)
cap(4.0, RF, 2.0, 0.8, ["OPTION"], "grey", 0.16 * U, OPTION)
cap(MAIN_W - 2.0, RF, 2.0, 0.8, ["BREAK"], "red", 0.16 * U, BRK)

# Row 1: ESC 1..0 < > DELETE
cap(0, R, 1, 1, ["ESC"], "dark", small, 0x1c)
row = [(("!", "1"), 0x1f), (('"', "2"), 0x1e), (("#", "3"), 0x1a), (("$", "4"), 0x18), (("%", "5"), 0x1d),
       (("&", "6"), 0x1b), (("'", "7"), 0x33), (("@", "8"), 0x35), (("(", "9"), 0x30), ((")", "0"), 0x32),
       (("CLEAR", "<"), 0x36), (("INSERT", ">"), 0x37)]
x = 1.0
for leg, code in row:
    cap(x, R, 1, 1, list(leg), code=code, size=(0.14 * U if len(leg[0]) > 1 else None))
    x += 1
cap(x, R, MAIN_W - x, 1, ["DELETE", "BACK S"], "dark", 0.14 * U, 0x34, name="DELETE")

# Row 2: TAB Q..P - = RETURN
cap(0, R + 1, 1.5, 1, ["TAB"], "dark", small, 0x2c)
x = 1.5
for ch, code in zip("QWERTYUIOP", (0x2f, 0x2e, 0x2a, 0x28, 0x2d, 0x2b, 0x0b, 0x0d, 0x08, 0x0a)):
    cap(x, R + 1, 1, 1, [ch], code=code)
    x += 1
cap(x, R + 1, 1, 1, ["_", "-"], code=0x0e)
cap(x + 1, R + 1, 1, 1, ["|", "="], code=0x0f)
cap(x + 2, R + 1, MAIN_W - x - 2, 1, ["RETURN"], "dark", small, 0x0c)

# Row 3: CONTROL A..L ; + * CAPS
cap(0, R + 2, 1.75, 1, ["CONTROL"], "dark", 0.15 * U, CT, 1)
x = 1.75
for ch, code in zip("ASDFGHJKL", (0x3f, 0x3e, 0x3a, 0x38, 0x3d, 0x39, 0x01, 0x05, 0x00)):
    cap(x, R + 2, 1, 1, [ch], code=code)
    x += 1
cap(x, R + 2, 1, 1, [":", ";"], code=0x02)
cap(x + 1, R + 2, 1, 1, ["\\", "+"], code=0x06)
cap(x + 2, R + 2, 1, 1, ["^", "*"], code=0x07)
cap(x + 3, R + 2, MAIN_W - x - 3, 1, ["CAPS"], "dark", small, 0x3c)

# Row 4: SHIFT Z../ SHIFT, the inverse key
cap(0, R + 3, 2.25, 1, ["SHIFT"], "dark", small, SH, 1, "SHIFT (left)")
x = 2.25
for ch, code in zip("ZXCVBNM", (0x17, 0x16, 0x12, 0x10, 0x15, 0x23, 0x25)):
    cap(x, R + 3, 1, 1, [ch], code=code)
    x += 1
for leg, code in [(("[", ","), 0x20), (("]", "."), 0x22), (("?", "/"), 0x26)]:
    cap(x, R + 3, 1, 1, list(leg), code=code)
    x += 1
cap(x, R + 3, MAIN_W - x - 1.2, 1, ["SHIFT"], "dark", small, SH, 1, "SHIFT (right)")
cap(MAIN_W - 1.2, R + 3, 1.2, 1, ["INV"], "dark", small, 0x27, name="INVERSE")

# Row 5: the space bar
cap(2.5, R + 4, 9.5, 1, [""], code=0x21, name="SPACE")

# Beside the main block: the console keys again, larger (games use them)
cap(PAD_X, R + 2, 2.4, 1, ["START"], "grey", 0.18 * U, START, name="START (big)")
cap(PAD_X, R + 3, 2.4, 1, ["SELECT"], "grey", 0.18 * U, SELECT, name="SELECT (big)")
cap(PAD_X, R + 4, 2.4, 1, ["OPTION"], "grey", 0.18 * U, OPTION, name="OPTION (big)")

img = c.final()

# The app's MENÚ / ▼ buttons: the free case above the side keys
g = GAP * U / 2
menu = (ux(PAD_X) + g, uy(RF), ux(PAD_X + 2.4) - g, uy(RF + 0.9))
hide = (ux(PAD_X) + g, uy(R + 0.2), ux(PAD_X + 2.4) - g, uy(R + 1.1))

args = sys.argv[1:]
os.makedirs(OUT, exist_ok=True)
img.save(os.path.join(OUT, "keyboard.png"), optimize=True)
with open(os.path.join(OUT, "keyboard.txt"), "w") as f:
    f.write("# tools/gen_atari8_vkb.py, do not edit\n")
    f.write("picture keyboard.png %d %d\n" % (W, H))
    f.write("button menu %d %d %d %d\n" % tuple(round(v) for v in menu))
    f.write("button hide %d %d %d %d\n" % tuple(round(v) for v in hide))
    for code, flags, (x1, y1, x2, y2), name in keys:
        f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, round(x1), round(y1), round(x2), round(y2), name))
if "--preview" in args:
    from PIL import Image
    bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(args[args.index("--preview") + 1])
print("atari800xl: %d keys" % len(keys))
