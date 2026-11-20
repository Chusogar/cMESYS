#!/usr/bin/env python3
"""The SAM Coupe app's on-screen keyboard (machines/computers/samcoupe/assets/
keyboard.jpg and keyboard.txt) from a photo of the SAM's keyboard (1920x1280,
the machine seen from above, the one given for it): the keyboard and the case
round it cropped out, down to the case under the keys (where MENÚ and ▼ go),
and the rectangle and key code (samcoupe_core.h's SK_*: column * 8 + bit) of
each key, measured on the photo. SHIFT, SYMBOL and CNTRL stay down for the
next key (flag 1). The keypad's "." is the main block's.

  python3 main/ports/android/tools/gen_samcoupe_vkb.py <photo.jpg>
"""
import os
import sys
from PIL import Image
from android_dirs import machine_dir

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(machine_dir("samcoupe"), "assets")
CROP = (205, 290, 1730, 900)        # the keyboard and its case in the 1920x1280 photo
W = 1400                            # the picture's width in the app

MOD = 1


def k(col, bit):
    return col * 8 + bit


# samcoupe_core.h
SK = dict(SHIFT=k(0, 0), Z=k(0, 1), X=k(0, 2), C=k(0, 3), V=k(0, 4), F1=k(0, 5), F2=k(0, 6), F3=k(0, 7),
          A=k(1, 0), S=k(1, 1), D=k(1, 2), F=k(1, 3), G=k(1, 4), F4=k(1, 5), F5=k(1, 6), F6=k(1, 7),
          Q=k(2, 0), W=k(2, 1), E=k(2, 2), R=k(2, 3), T=k(2, 4), F7=k(2, 5), F8=k(2, 6), F9=k(2, 7),
          N1=k(3, 0), N2=k(3, 1), N3=k(3, 2), N4=k(3, 3), N5=k(3, 4), ESC=k(3, 5), TAB=k(3, 6), CAPS=k(3, 7),
          N0=k(4, 0), N9=k(4, 1), N8=k(4, 2), N7=k(4, 3), N6=k(4, 4), MINUS=k(4, 5), PLUS=k(4, 6), DELETE=k(4, 7),
          P=k(5, 0), O=k(5, 1), I=k(5, 2), U=k(5, 3), Y=k(5, 4), EQUALS=k(5, 5), QUOTE=k(5, 6), F0=k(5, 7),
          RETURN=k(6, 0), L=k(6, 1), K=k(6, 2), J=k(6, 3), H=k(6, 4), SEMICOLON=k(6, 5), COLON=k(6, 6), EDIT=k(6, 7),
          SPACE=k(7, 0), SYMBOL=k(7, 1), M=k(7, 2), N=k(7, 3), B=k(7, 4), COMMA=k(7, 5), PERIOD=k(7, 6), INV=k(7, 7),
          CNTRL=k(8, 0), UP=k(8, 1), DOWN=k(8, 2), LEFT=k(8, 3), RIGHT=k(8, 4))

R1, R2, R3, R4, R5 = (318, 398), (403, 485), (488, 570), (573, 655), (658, 742)


def row(y, items):
    out = []
    for it in items:
        label, code, x1, x2 = it[:4]
        out.append((SK[code], it[4] if len(it) > 4 else 0, x1, y[0], x2, y[1], label))
    return out


# (label, SK name, x1, x2[, flags]) in the photo's pixels
KEYS = (
    row(R1, [("ESC", "ESC", 230, 310), ("1", "N1", 315, 395), ("2", "N2", 400, 480), ("3", "N3", 485, 565),
             ("4", "N4", 570, 650), ("5", "N5", 655, 735), ("6", "N6", 740, 820), ("7", "N7", 825, 905),
             ("8", "N8", 910, 990), ("9", "N9", 995, 1075), ("0", "N0", 1078, 1157), ("-", "MINUS", 1160, 1240),
             ("+", "PLUS", 1243, 1322), ("DELETE", "DELETE", 1325, 1447), ("F7", "F7", 1450, 1530),
             ("F8", "F8", 1535, 1615), ("F9", "F9", 1620, 1700)]) +
    row(R2, [("TAB", "TAB", 230, 350), ("Q", "Q", 355, 435), ("W", "W", 440, 520), ("E", "E", 525, 605),
             ("R", "R", 610, 690), ("T", "T", 695, 775), ("Y", "Y", 780, 860), ("U", "U", 865, 945),
             ("I", "I", 950, 1030), ("O", "O", 1035, 1115), ("P", "P", 1118, 1198), ("=", "EQUALS", 1201, 1281),
             ('"', "QUOTE", 1285, 1365), ("RETURN (top)", "RETURN", 1370, 1447), ("F4", "F4", 1450, 1530),
             ("F5", "F5", 1535, 1615), ("F6", "F6", 1620, 1700)]) +
    row(R3, [("CAPS", "CAPS", 230, 370), ("A", "A", 375, 455), ("S", "S", 460, 540), ("D", "D", 545, 625),
             ("F", "F", 630, 710), ("G", "G", 715, 795), ("H", "H", 800, 880), ("J", "J", 885, 965),
             ("K", "K", 970, 1050), ("L", "L", 1055, 1135), (";", "SEMICOLON", 1140, 1220), (":", "COLON", 1223, 1301),
             ("RETURN", "RETURN", 1305, 1447), ("F1", "F1", 1450, 1530), ("F2", "F2", 1535, 1615),
             ("F3", "F3", 1620, 1700)]) +
    row(R4, [("SHIFT (left)", "SHIFT", 230, 415, MOD), ("Z", "Z", 420, 500), ("X", "X", 505, 585),
             ("C", "C", 590, 670), ("V", "V", 675, 755), ("B", "B", 760, 840), ("N", "N", 845, 925),
             ("M", "M", 930, 1010), (",", "COMMA", 1015, 1095), (".", "PERIOD", 1100, 1178),
             ("INV", "INV", 1181, 1261), ("SHIFT (right)", "SHIFT", 1265, 1450, MOD), ("F0", "F0", 1455, 1535),
             ("UP", "UP", 1540, 1615), (". (keypad)", "PERIOD", 1620, 1700)]) +
    row(R5, [("SYMBOL (left)", "SYMBOL", 230, 370, MOD), ("CNTRL", "CNTRL", 375, 500, MOD),
             ("SPACE", "SPACE", 505, 1180), ("EDIT", "EDIT", 1185, 1305), ("SYMBOL (right)", "SYMBOL", 1310, 1450, MOD),
             ("LEFT", "LEFT", 1455, 1535), ("DOWN", "DOWN", 1540, 1615), ("RIGHT", "RIGHT", 1620, 1700)])
)
# MENÚ and ▼: on the case under the keys, at the right
BUTTONS = [("menu", 1290, 772, 1495, 888), ("hide", 1515, 772, 1720, 888)]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    photo = Image.open(sys.argv[1]).convert("RGB")
    if photo.size != (1920, 1280):
        print("expected the 1920x1280 photo, got %dx%d" % photo.size)
        return 1
    x0, y0, x1, y1 = CROP
    s = W / (x1 - x0)
    pic = photo.crop(CROP).resize((W, round((y1 - y0) * s)), Image.LANCZOS)
    os.makedirs(OUT, exist_ok=True)
    pic.save(os.path.join(OUT, "keyboard.jpg"), quality=90, optimize=True)

    def sc(x, y):
        return round((x - x0) * s), round((y - y0) * s)

    with open(os.path.join(OUT, "keyboard.txt"), "w", encoding="utf-8") as f:
        f.write("# tools/gen_samcoupe_vkb.py (a photo of the SAM Coupe's keyboard), do not edit\n")
        f.write("picture keyboard.jpg %d %d\n" % pic.size)
        f.write("crop 0 0 0\n")
        f.write("travel 4\n")
        for name, bx1, by1, bx2, by2 in BUTTONS:
            f.write("button %s %d %d %d %d\n" % ((name,) + sc(bx1, by1) + sc(bx2, by2)))
        for code, flags, kx1, ky1, kx2, ky2, label in KEYS:
            f.write("key %d %d %d %d %d %d  # %s\n" % ((code, flags) + sc(kx1, ky1) + sc(kx2, ky2) + (label,)))
    print("samcoupe keyboard: %dx%d, %d keys" % (pic.size + (len(KEYS),)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
