#!/usr/bin/env python3
"""The Atari ST app's on-screen keyboard (machines/computers/atarist/assets/keyboard.png
and keyboard.txt) from dsp-cpp's src/drivers/computers/atari_st_vkb_data.inc (made
there by tools/gen_st_vkb.py from Hataroid's 1040ST keyboard, GPLv2): the 768x256
picture, and the rectangle and IKBD scan code (st_core.h) of each key on it (the
slanted function keys: their bounding box). Shift, Control and Alternate stay down
for the next key (flag 1).

  python3 main/ports/android/tools/gen_st_vkb.py <atari_st_vkb_data.inc>
"""
import os
import re
import sys
import zlib
from PIL import Image
from android_dirs import machine_dir

OUT = os.path.join(machine_dir("atarist"), "assets")
TEX_W, TEX_H = 768, 256
MOD = 1


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = open(sys.argv[1], encoding="utf-8").read()
    pic = src[src.index("kVkbPictureUk[]"):src.index("};", src.index("kVkbPictureUk[]"))]
    data = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", pic))
    rgb = zlib.decompress(data)
    img = Image.frombytes("RGB", (TEX_W, TEX_H), rgb)
    keys = []
    for m in re.finditer(r'\{"((?:[^"\\]|\\.)*)", 0x([0-9a-f]+), (true|false), (true|false), \{([-0-9, ]+)\}\}', src):
        label, code, poly, mod = m.group(1), int(m.group(2), 16), m.group(3) == "true", m.group(4) == "true"
        v = [int(x) for x in m.group(5).split(",")]
        if poly:
            xs, ys = v[0::2], v[1::2]
            x1, y1, x2, y2 = min(xs), min(ys), max(xs), max(ys)
        else:
            x1, y1, x2, y2 = v[:4]
        keys.append((code, MOD if mod else 0, x1, y1, x2, y2, label))
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, "keyboard.png"), optimize=True)
    with open(os.path.join(OUT, "keyboard.txt"), "w", encoding="utf-8") as f:
        f.write("# tools/gen_st_vkb.py (Hataroid's 1040ST keyboard, GPLv2, via dsp-cpp), do not edit\n")
        f.write("picture keyboard.png %d %d\n" % (TEX_W, TEX_H))
        f.write("crop 0 0 0\n")
        f.write("travel 2\n")
        f.write("button menu %s\n" % MENU)
        f.write("button hide %s\n" % HIDE)
        for code, flags, x1, y1, x2, y2, label in keys:
            f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, x1, y1, x2, y2, label))
    print("keyboard: %dx%d, %d keys" % (TEX_W, TEX_H, len(keys)))


MENU = "522 182 614 208"           # (the free space under the cursor keys)
HIDE = "522 212 614 238"

if __name__ == "__main__":
    main()
