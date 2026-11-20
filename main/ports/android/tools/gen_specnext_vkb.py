#!/usr/bin/env python3
"""The Spectrum Next app's on-screen keyboard (machines/computers/specnext/assets/
keyboard.jpg and keyboard.txt) from a photo of the Next's keyboard (KS1,
1536x719 on a red backdrop, the one given for it): the keyboard and the case
around it cropped out (the red backdrop round the case painted the app's
dark keyboard colour here, so the case's red stripe stays), and the
rectangle and key code (specnext_core.h's NK_*) of each key, measured on the
photo. CAPS SHIFT and SYMBOL SHIFT stay down for the next key (flag 1); the
Next's extra keys (BREAK, EDIT, TRUE / INV VIDEO, CAPS LOCK, GRAPH, EXTEND
MODE, DELETE, the cursors, ; " , .) are keys of their own, typed by the core
as the Spectrum combinations, like the real one. MENÚ and ▼ go on the case
right of the keys.

  python3 main/ports/android/tools/gen_specnext_vkb.py <photo.jpg>
"""
import os
import sys
from PIL import Image
from android_dirs import machine_dir

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(machine_dir("specnext"), "assets")
CROP = (22, 214, 1498, 696)         # the keyboard and its case in the 1536x719 photo
W = 1400                            # the picture's width in the app

MOD = 1

# specnext_core.h
NK = dict(CAPS_SHIFT=0x00, Z=0x01, X=0x02, C=0x03, V=0x04, A=0x08, S=0x09, D=0x0a, F=0x0b, G=0x0c,
          Q=0x10, W=0x11, E=0x12, R=0x13, T=0x14, N1=0x18, N2=0x19, N3=0x1a, N4=0x1b, N5=0x1c,
          N0=0x20, N9=0x21, N8=0x22, N7=0x23, N6=0x24, P=0x28, O=0x29, I=0x2a, U=0x2b, Y=0x2c,
          ENTER=0x30, L=0x31, K=0x32, J=0x33, H=0x34, SPACE=0x38, SYM_SHIFT=0x39, M=0x3a, N=0x3b, B=0x3c,
          SEMICOLON=0x40, QUOTE=0x41, COMMA=0x42, PERIOD=0x43, UP=0x44, DOWN=0x45, LEFT=0x46, RIGHT=0x47,
          DELETE=0x48, EDIT=0x49, BREAK=0x4a, INV_VIDEO=0x4b, TRUE_VIDEO=0x4c, GRAPH=0x4d, CAPS_LOCK=0x4e,
          EXTEND=0x4f)

R1, R2, R3, R4, R5 = (226, 306), (313, 394), (400, 481), (487, 568), (574, 657)


def row(y, items):
    out = []
    for it in items:
        label, code, x1, x2 = it[:4]
        out.append((NK[code], it[4] if len(it) > 4 else 0, x1, y[0], x2, y[1], label))
    return out


# (label, NK name, x1, x2[, flags]) in the photo's pixels
KEYS = (
    row(R1, [("BREAK", "BREAK", 65, 147), ("EDIT", "EDIT", 152, 232), ("1", "N1", 240, 320), ("2", "N2", 327, 407),
             ("3", "N3", 415, 495), ("4", "N4", 502, 582), ("5", "N5", 589, 669), ("6", "N6", 676, 756),
             ("7", "N7", 763, 843), ("8", "N8", 850, 930), ("9", "N9", 937, 1017), ("0", "N0", 1024, 1104),
             ("DELETE", "DELETE", 1112, 1240)]) +
    row(R2, [("TRUE VIDEO", "TRUE_VIDEO", 65, 168), ("INV VIDEO", "INV_VIDEO", 173, 278), ("Q", "Q", 285, 365),
             ("W", "W", 372, 452), ("E", "E", 459, 539), ("R", "R", 546, 626), ("T", "T", 633, 713),
             ("Y", "Y", 720, 800), ("U", "U", 807, 887), ("I", "I", 894, 974), ("O", "O", 981, 1061),
             ("P", "P", 1068, 1148), ("ENTER (top)", "ENTER", 1156, 1240)]) +
    row(R3, [("CAPS LOCK", "CAPS_LOCK", 65, 188), ("GRAPH", "GRAPH", 195, 298), ("A", "A", 305, 385),
             ("S", "S", 392, 472), ("D", "D", 479, 559), ("F", "F", 566, 646), ("G", "G", 653, 733),
             ("H", "H", 740, 820), ("J", "J", 827, 907), ("K", "K", 914, 994), ("L", "L", 1001, 1081),
             ("ENTER", "ENTER", 1090, 1240)]) +
    row(R4, [("CAPS SHIFT (left)", "CAPS_SHIFT", 65, 255, MOD), ("EXTEND MODE", "EXTEND", 262, 365),
             ("Z", "Z", 372, 452), ("X", "X", 459, 539), ("C", "C", 546, 626), ("V", "V", 633, 713),
             ("B", "B", 720, 800), ("N", "N", 807, 887), ("M", "M", 894, 974), ("UP", "UP", 981, 1061),
             ("CAPS SHIFT (right)", "CAPS_SHIFT", 1068, 1240, MOD)]) +
    row(R5, [("SYMBOL SHIFT (left)", "SYM_SHIFT", 65, 147, MOD), (";", "SEMICOLON", 152, 232),
             ('"', "QUOTE", 240, 320), (",", "COMMA", 327, 407), (".", "PERIOD", 415, 495),
             ("SPACE", "SPACE", 502, 887), ("LEFT", "LEFT", 894, 974), ("DOWN", "DOWN", 981, 1061),
             ("RIGHT", "RIGHT", 1068, 1148), ("SYMBOL SHIFT (right)", "SYM_SHIFT", 1156, 1240, MOD)])
)
BUTTONS = [("menu", 1262, 236, 1430, 380), ("hide", 1262, 420, 1430, 564)]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    photo = Image.open(sys.argv[1]).convert("RGB")
    if photo.size != (1536, 719):
        print("expected the 1536x719 photo, got %dx%d" % photo.size)
        return 1
    x0, y0, x1, y1 = CROP
    s = W / (x1 - x0)
    pic = photo.crop(CROP).resize((W, round((y1 - y0) * s)), Image.LANCZOS)
    # the red backdrop (reached from the edges) -> the app's dark backdrop
    px = pic.load()
    pw, ph = pic.size

    def red(x, y):
        r, g, b = px[x, y]
        return r - max(g, b) > 40

    seen = bytearray(pw * ph)
    # (from the top, the bottom and the left: the case's red stripe reaches the right edge)
    todo = [(x, y) for x in range(pw) for y in (0, ph - 1)] + [(0, y) for y in range(ph)]
    while todo:
        x, y = todo.pop()
        if x < 0 or y < 0 or x >= pw or y >= ph or seen[y * pw + x] or not red(x, y):
            continue
        seen[y * pw + x] = 1
        px[x, y] = (8, 8, 12)
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    os.makedirs(OUT, exist_ok=True)
    pic.save(os.path.join(OUT, "keyboard.jpg"), quality=90, optimize=True)

    def sc(x, y):
        return round((x - x0) * s), round((y - y0) * s)

    with open(os.path.join(OUT, "keyboard.txt"), "w", encoding="utf-8") as f:
        f.write("# tools/gen_specnext_vkb.py (a photo of the ZX Spectrum Next's keyboard), do not edit\n")
        f.write("picture keyboard.jpg %d %d\n" % pic.size)
        f.write("crop 0 0 0\n")
        f.write("travel 4\n")
        for name, bx1, by1, bx2, by2 in BUTTONS:
            f.write("button %s %d %d %d %d\n" % ((name,) + sc(bx1, by1) + sc(bx2, by2)))
        for code, flags, kx1, ky1, kx2, ky2, label in KEYS:
            f.write("key %d %d %d %d %d %d  # %s\n" % ((code, flags) + sc(kx1, ky1) + sc(kx2, ky2) + (label,)))
    print("specnext keyboard: %dx%d, %d keys" % (pic.size + (len(KEYS),)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
