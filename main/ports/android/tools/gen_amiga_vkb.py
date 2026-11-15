#!/usr/bin/env python3
"""The Amiga app's on-screen keyboard (machines/amiga/assets/keyboard.jpg and
keyboard.txt) from a photo of a Spanish Amiga 500 (2048x1294, the one given
for it): the keyboard cropped out of the photo (as the QL's), and the
rectangle and raw key code (amiga_core.h) of each key on it, measured on
the photo. Modifiers stay down for the next key (flag 1); Caps Lock locks (2).

  python3 main/ports/android/tools/gen_amiga_vkb.py <photo.jpg>
"""
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "machines", "amiga", "assets")
CROP = (40, 600, 2010, 1180)        # the keyboard in the 2048x1294 photo
W = 1400                            # the picture's width in the app

MOD, LOCK = 1, 2


def keys():
    pitch, kw = 57.9, 53
    out = []

    def row(y1, y2, x0, codes, labels):
        for i, (c, l) in enumerate(zip(codes, labels)):
            out.append((c, 0, round(x0 + i * pitch), y1, round(x0 + i * pitch + kw), y2, l))

    def key(c, x1, y1, x2, y2, l, f=0):
        out.append((c, f, x1, y1, x2, y2, l))

    fy = (12, 76)
    key(0x45, 20, fy[0], 80, fy[1], "ESC")
    for i, x in enumerate([105, 177, 249, 321, 393, 492, 564, 636, 708, 780]):
        key(0x50 + i, x, fy[0], x + 66, fy[1], "F%d" % (i + 1))
    r1, r2, r3, r4, r5 = (104, 162), (165, 222), (225, 282), (285, 342), (345, 402)
    key(0x00, 18, r1[0], 98, r1[1], "` ~")
    row(*r1, 105, list(range(1, 11)) + [0x0B, 0x0C, 0x0D], list("1234567890") + ["-", "=", "\\ |"])
    key(0x41, 858, r1[0], 908, r1[1], "BACKSPACE")
    key(0x42, 18, r2[0], 128, r2[1], "TAB")
    row(*r2, 133, list(range(0x10, 0x1A)) + [0x1A, 0x1B], list("QWERTYUIOP") + ["¨ ´", "^ `"])
    key(0x44, 830, r2[0], 908, r2[1], "RETURN")
    key(0x63, 18, r3[0], 86, r3[1], "CTRL", MOD)
    key(0x62, 90, r3[0], 146, r3[1], "CAPS LOCK", LOCK)
    row(*r3, 148, list(range(0x20, 0x29)) + [0x29, 0x2A, 0x2B], list("ASDFGHJKL") + ["Ñ", ": ;", "Ç"])
    key(0x60, 18, r4[0], 110, r4[1], "LEFT SHIFT", MOD)
    key(0x30, 115, r4[0], 168, r4[1], "< >")
    row(*r4, 172, list(range(0x31, 0x38)) + [0x38, 0x39, 0x3A], list("ZXCVBNM") + ["? ,", "! .", "\" '"])
    key(0x61, 758, r4[0], 908, r4[1], "RIGHT SHIFT", MOD)
    key(0x64, 55, r5[0], 128, r5[1], "LEFT ALT", MOD)
    key(0x66, 130, r5[0], 202, r5[1], "LEFT AMIGA", MOD)
    key(0x40, 205, r5[0], 728, r5[1], "SPACE")
    key(0x67, 733, r5[0], 796, r5[1], "RIGHT AMIGA", MOD)
    key(0x65, 800, r5[0], 870, r5[1], "RIGHT ALT", MOD)
    key(0x46, 935, r1[0], 1020, r1[1], "DEL")
    key(0x5F, 1025, r1[0], 1110, r1[1], "HELP")
    key(0x4C, 1000, 215, 1052, 270, "UP")
    key(0x4F, 945, 275, 997, 330, "LEFT")
    key(0x4D, 1000, 275, 1052, 330, "DOWN")
    key(0x4E, 1058, 275, 1110, 330, "RIGHT")
    kx = [1140, 1200, 1260, 1320]
    ky = [(100, 155), (160, 215), (220, 275), (280, 335), (340, 395)]
    pad = [[0x5A, 0x5B, 0x5C, 0x5D], [0x3D, 0x3E, 0x3F, 0x4A], [0x2D, 0x2E, 0x2F, 0x5E], [0x1D, 0x1E, 0x1F, None]]
    names = [["KP ( [", "KP ) ]", "KP /", "KP *"], ["KP 7", "KP 8", "KP 9", "KP -"],
             ["KP 4", "KP 5", "KP 6", "KP +"], ["KP 1", "KP 2", "KP 3", None]]
    for r in range(4):
        for c in range(4):
            if pad[r][c] is not None:
                key(pad[r][c], kx[c], ky[r][0], kx[c] + 55, ky[r][1], names[r][c])
    key(0x0F, 1140, ky[4][0], 1255, ky[4][1], "KP 0")
    key(0x3C, 1260, ky[4][0], 1315, ky[4][1], "KP .")
    key(0x43, 1320, ky[3][0], 1378, ky[4][1], "KP ENTER")
    # the lower part of the L-shaped RETURN (last: the first tile wins a tie)
    key(0x44, 850, r3[0] - 6, 908, r3[1], "RETURN")
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    photo = Image.open(sys.argv[1]).convert("RGB")
    if photo.size != (2048, 1294):
        sys.exit("expected the 2048x1294 photo of the A500 (the rectangles are measured on it)")
    c = photo.crop(CROP)
    h = round(c.height * W / c.width)
    c = c.resize((W, h), Image.LANCZOS)
    os.makedirs(OUT, exist_ok=True)
    c.save(os.path.join(OUT, "keyboard.jpg"), quality=88, optimize=True)
    with open(os.path.join(OUT, "keyboard.txt"), "w", encoding="utf-8") as f:
        f.write("# tools/gen_amiga_vkb.py (a Spanish Amiga 500's photo), do not edit\n")
        f.write("picture keyboard.jpg %d %d\n" % (W, h))
        f.write("crop 0 0 0\n")
        f.write("travel 4\n")
        f.write("button menu 880 16 1010 72\n")
        f.write("button hide 1030 16 1130 72\n")
        for code, flags, x1, y1, x2, y2, label in keys():
            f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, x1, y1, x2, y2, label))
    print("keyboard: %dx%d, %d keys" % (W, h, len(keys())))


if __name__ == "__main__":
    main()
