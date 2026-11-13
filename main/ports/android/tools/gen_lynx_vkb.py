#!/usr/bin/env python3
"""Draws the Atari Lynx's on-screen controls for the Android port, the same
way as dsp-cpp's on-screen keyboards (vkb_common.py: drawn from scratch,
no logos): the console's dark grey body with the cross on the left, A and B
on the right and OPTION 1, PAUSE and OPTION 2 between them. The Lynx has no
keyboard, so this is its keyboard: codes are the bit numbers of lynx_core.h
(A 0, B 1, OPTION 2 2, OPTION 1 3, right 4, left 5, down 6, up 7, PAUSE 8)
and 9..12 for the cross's diagonals (up-left, up-right, down-left,
down-right). Writes machines/lynx/assets/keyboard.png + keyboard.txt.

usage: gen_lynx_vkb.py [--preview out.png]
"""
import os
import sys

from vkb_common import Canvas, lerp

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "machines", "lynx", "assets")

W, H = 1536, 520
BODY_TOP = (78, 80, 86)
BODY_BOT = (52, 54, 58)

c = Canvas(W, H)
keys = []

# The body: a wide rounded slab with grips at both ends.
c.shadow([(20, 16, W - 20, H - 6, 120)], 9, alpha=170)
c.vgrad_rrect(14, 8, W - 14, H - 14, 118, BODY_TOP, BODY_BOT)
c.vgrad_rrect(140, 9, W - 140, 22, 6, (104, 106, 112), BODY_TOP)
# grip ridges
for gx in (70, W - 82):
    for i in range(7):
        y = 150 + i * 32
        c.rrect(gx, y, gx + 12, y + 18, 5, (40, 41, 45, 255))

# --- the cross ---------------------------------------------------------------
CX, CY, ARM, LEN = 330, 280, 104, 150
c.draw.ellipse(c.box(CX - 205, CY - 205, CX + 205, CY + 205), fill=(40, 41, 45, 255))
c.draw.ellipse(c.box(CX - 196, CY - 196, CX + 196, CY + 196), fill=(58, 60, 65, 255))
a, l = ARM / 2, LEN
c.shadow([(CX - a, CY - l, CX + a, CY + l, 14), (CX - l, CY - a, CX + l, CY + a, 14)], 6, alpha=230)
for (x1, y1, x2, y2) in ((CX - a, CY - l, CX + a, CY + l), (CX - l, CY - a, CX + l, CY + a)):
    c.vgrad_rrect(x1, y1, x2, y2, 14, (46, 47, 51), (26, 27, 30))
c.draw.ellipse(c.box(CX - 26, CY - 26, CX + 26, CY + 26), fill=(32, 33, 36, 255))
for dx, dy, s in ((0, -1, "▲"), (0, 1, "▼"), (-1, 0, "◀"), (1, 0, "▶")):
    c.text(CX + dx * (l - 46), CY + dy * (l - 46), s, 34, (150, 152, 158), "mm", font="/usr/share/fonts/truetype/freefont/FreeSans.ttf")
# hit areas: the arms, and the corners between them for the diagonals
keys += [
    (7, 0, (CX - a, CY - l, CX + a, CY - a), "UP"),
    (6, 0, (CX - a, CY + a, CX + a, CY + l), "DOWN"),
    (5, 0, (CX - l, CY - a, CX - a, CY + a), "LEFT"),
    (4, 0, (CX + a, CY - a, CX + l, CY + a), "RIGHT"),
    (9, 0, (CX - l, CY - l, CX - a, CY - a), "UP+LEFT"),
    (10, 0, (CX + a, CY - l, CX + l, CY - a), "UP+RIGHT"),
    (11, 0, (CX - l, CY + a, CX - a, CY + l), "DOWN+LEFT"),
    (12, 0, (CX + a, CY + a, CX + l, CY + l), "DOWN+RIGHT"),
]


# --- A and B -------------------------------------------------------------------
def round_button(cx, cy, r, label, code):
    c.draw.ellipse(c.box(cx - r - 14, cy - r - 14, cx + r + 14, cy + r + 14), fill=(40, 41, 45, 255))
    c.draw.ellipse(c.box(cx - r - 9, cy - r - 9, cx + r + 9, cy + r + 9), fill=(60, 62, 67, 255))
    c.shadow([(cx - r, cy - r + 5, cx + r, cy + r + 6, r)], 5, alpha=230)
    c.vgrad_rrect(cx - r, cy - r, cx + r, cy + r, r, (62, 63, 68), (24, 25, 28))
    c.vgrad_rrect(cx - r + 8, cy - r + 6, cx + r - 8, cy + 4, r - 8, (88, 90, 96), (52, 53, 58))
    c.text(cx, cy + r + 34, label, 30, (220, 220, 214), "mm")
    keys.append((code, 0, (cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6), label))


round_button(W - 340, 318, 78, "B", 1)
round_button(W - 170, 210, 78, "A", 0)


# --- OPTION 1, PAUSE, OPTION 2 -------------------------------------------------
def pill(cx, cy, label, code, name):
    w, h = 124, 50
    c.rrect(cx - w / 2 - 7, cy - h / 2 - 7, cx + w / 2 + 7, cy + h / 2 + 8, h / 2 + 6, (36, 37, 41, 255))
    c.shadow([(cx - w / 2, cy - h / 2 + 3, cx + w / 2, cy + h / 2 + 4, h / 2)], 3, alpha=220)
    c.vgrad_rrect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, h / 2, (150, 152, 158), (104, 106, 112))
    c.text(cx, cy + h / 2 + 30, label, 22, (214, 214, 208), "mm")
    keys.append((code, 0, (cx - w / 2 - 4, cy - h / 2 - 4, cx + w / 2 + 4, cy + h / 2 + 4), name))


MID = 800
pill(MID - 170, 380, "OPTION 1", 3, "OPTION 1")
pill(MID, 380, "PAUSE", 8, "PAUSE")
pill(MID + 170, 380, "OPTION 2", 2, "OPTION 2")

# The app's MENÚ / ▼ buttons: the free middle of the body, above the pills
menu = (MID - 220, 110, MID + 40, 190)
hide = (MID + 70, 110, MID + 210, 190)

img = c.final()
os.makedirs(OUT, exist_ok=True)
img.save(os.path.join(OUT, "keyboard.png"), optimize=True)
with open(os.path.join(OUT, "keyboard.txt"), "w") as f:
    f.write("# tools/gen_lynx_vkb.py, do not edit\n")
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
print("lynx: %d controls" % len(keys))
