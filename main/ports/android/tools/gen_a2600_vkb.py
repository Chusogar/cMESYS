#!/usr/bin/env python3
"""Draws the Atari 2600's on-screen controls for the Android port, the same
way as dsp-cpp's on-screen keyboards (vkb_common.py: drawn from scratch, no
logos): a black console panel with a wood-grain strip, the joystick's base
on the left (eight directions), its red FIRE button on the right and the
GAME SELECT / GAME RESET levers between them. Codes are jni/m_a2600.c's:
up 0, down 1, left 2, right 3, FIRE 4, RESET 5, SELECT 6, and 7..10 the
diagonals (up-left, up-right, down-left, down-right). Writes
machines/a2600/assets/keyboard.png + keyboard.txt.

usage: gen_a2600_vkb.py [--preview out.png]
"""
import os
import sys

from vkb_common import Canvas

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "machines", "a2600", "assets")

W, H = 1536, 520
c = Canvas(W, H)
keys = []

# The panel: black ribbed plastic with the wood-grain strip along the top
c.shadow([(20, 16, W - 20, H - 6, 40)], 9, alpha=170)
c.vgrad_rrect(14, 8, W - 14, H - 14, 36, (40, 38, 36), (18, 17, 16))
c.vgrad_rrect(30, 22, W - 30, 92, 10, (128, 82, 44), (92, 56, 28))
for i in range(9):
    y = 30 + i * 7
    c.rrect(40 + (i * 37) % 60, y, W - 40 - (i * 53) % 80, y + 2, 1, (78, 46, 22, 255))
for i in range(12):
    y = 110 + i * 30
    c.rrect(560, y, W - 560, y + 6, 3, (30, 29, 28, 255))

# --- the joystick: a square base, the stick in the middle -------------------
CX, CY, R = 330, 300, 190
c.shadow([(CX - R, CY - R + 8, CX + R, CY + R + 10, 40)], 8, alpha=230)
c.vgrad_rrect(CX - R, CY - R, CX + R, CY + R, 40, (52, 50, 48), (22, 21, 20))
c.draw.ellipse(c.box(CX - 120, CY - 120, CX + 120, CY + 120), fill=(14, 14, 14, 255))
c.draw.ellipse(c.box(CX - 44, CY - 44, CX + 44, CY + 44), fill=(36, 35, 34, 255))
c.draw.ellipse(c.box(CX - 30, CY - 34, CX + 30, CY + 26), fill=(58, 56, 54, 255))
for dx, dy, s in ((0, -1, "▲"), (0, 1, "▼"), (-1, 0, "◀"), (1, 0, "▶")):
    c.text(CX + dx * 150, CY + dy * 150, s, 34, (190, 120, 60), "mm",
           font="/usr/share/fonts/truetype/freefont/FreeSans.ttf")
a = 60
keys += [
    (0, 0, (CX - a, CY - R, CX + a, CY - a), "UP"),
    (1, 0, (CX - a, CY + a, CX + a, CY + R), "DOWN"),
    (2, 0, (CX - R, CY - a, CX - a, CY + a), "LEFT"),
    (3, 0, (CX + a, CY - a, CX + R, CY + a), "RIGHT"),
    (7, 0, (CX - R, CY - R, CX - a, CY - a), "UP+LEFT"),
    (8, 0, (CX + a, CY - R, CX + R, CY - a), "UP+RIGHT"),
    (9, 0, (CX - R, CY + a, CX - a, CY + R), "DOWN+LEFT"),
    (10, 0, (CX + a, CY + a, CX + R, CY + R), "DOWN+RIGHT"),
]

# --- FIRE ------------------------------------------------------------------------
FX, FY, FR = W - 300, 300, 110
c.draw.ellipse(c.box(FX - FR - 22, FY - FR - 22, FX + FR + 22, FY + FR + 22), fill=(14, 14, 14, 255))
c.shadow([(FX - FR, FY - FR + 6, FX + FR, FY + FR + 8, FR)], 6, alpha=230)
c.vgrad_rrect(FX - FR, FY - FR, FX + FR, FY + FR, FR, (230, 60, 40), (140, 20, 14))
c.vgrad_rrect(FX - FR + 18, FY - FR + 12, FX + FR - 18, FY + 6, FR - 18, (250, 120, 100), (200, 50, 36))
c.text(FX, FY + FR + 44, "FIRE", 32, (230, 220, 200), "mm")
keys.append((4, 0, (FX - FR - 20, FY - FR - 20, FX + FR + 20, FY + FR + 20), "FIRE"))


# --- GAME SELECT and GAME RESET: the console's levers ------------------------------
def lever(cx, label, code):
    x1, y1, x2, y2 = cx - 70, 250, cx + 70, 420
    c.rrect(x1 - 8, y1 - 8, x2 + 8, y2 + 8, 14, (10, 10, 10, 255))
    c.vgrad_rrect(x1, y1, x2, y2, 10, (34, 33, 32), (20, 19, 18))
    c.shadow([(cx - 40, 290, cx + 40, 360, 12)], 4, alpha=220)
    c.vgrad_rrect(cx - 40, 286, cx + 40, 356, 12, (200, 200, 196), (130, 130, 126))
    c.text(cx, y1 - 36, label, 26, (230, 220, 200), "mm")
    keys.append((code, 0, (x1 - 10, y1 - 60, x2 + 10, y2 + 10), label))


lever(W / 2 - 110, "SELECT", 6)
lever(W / 2 + 110, "RESET", 5)

menu = (W / 2 - 220, 110, W / 2 + 20, 180)
hide = (W / 2 + 50, 110, W / 2 + 200, 180)

img = c.final()
os.makedirs(OUT, exist_ok=True)
img.save(os.path.join(OUT, "keyboard.png"), optimize=True)
with open(os.path.join(OUT, "keyboard.txt"), "w") as f:
    f.write("# tools/gen_a2600_vkb.py, do not edit\n")
    f.write("picture keyboard.png %d %d\n" % (W, H))
    f.write("travel 4\n")
    f.write("button menu %d %d %d %d\n" % tuple(round(v) for v in menu))
    f.write("button hide %d %d %d %d\n" % tuple(round(v) for v in hide))
    for code, flags, (x1, y1, x2, y2), name in keys:
        f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, round(x1), round(y1), round(x2), round(y2), name))
args = sys.argv[1:]
if "--preview" in args:
    from PIL import Image
    bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(args[args.index("--preview") + 1])
print("a2600: %d controls" % len(keys))
