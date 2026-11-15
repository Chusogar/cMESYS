#!/usr/bin/env python3
"""Takes dsp-cpp's Atari ST on-screen keyboard (Hataroid's, GPLv2: the
picture and key table of src/drivers/computers/atari_st_vkb_data.inc, made
by dsp-cpp's tools/gen_st_vkb.py) into the Android port, as
machines/atarist/assets/keyboard.png + keyboard.txt (see
import_dsp_keyboards.py for the format). The picture is doubled to 1536x512
and its badge (Hataroid's name and the model's) painted over with the
case's grey; the app's MENÚ / ▼ buttons go there. The slanted function keys
are hit as their bounding boxes; SHIFT, CONTROL and ALTERNATE are
modifiers.

  python3 main/ports/android/tools/import_st_keyboard.py <dsp-cpp>
"""
import os
import re
import sys
import zlib

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "machines", "atarist", "assets")
S = 2                                   # 768x256 -> 1536x512
BADGE = (556, 6, 764, 50)               # the badge (picture pixels)


def main():
    dsp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "..", "..", "..", "dsp-cpp")
    text = open(os.path.join(dsp, "src/drivers/computers/atari_st_vkb_data.inc")).read()
    m = re.search(r"kVkbPictureUk\[\] = \{(.*?)\};", text, re.S)
    raw = zlib.decompress(bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))))
    img = Image.frombytes("RGB", (768, 256), raw)
    case = img.getpixel((548, 20))
    d = ImageDraw.Draw(img)
    d.rectangle(BADGE, fill=case)
    img = img.resize((768 * S, 256 * S), Image.LANCZOS)
    keys = []
    for name, code, poly, mod, v in re.findall(
            r'\{"((?:[^"\\]|\\.)*)", (0x[0-9a-fA-F]+), (true|false), (true|false), \{([^}]*)\}\}', text):
        v = [int(x) for x in v.split(",")]
        if poly == "true":
            xs, ys = v[0::2], v[1::2]
            box = (min(xs), min(ys), max(xs), max(ys))
        else:
            box = tuple(v[:4])
        name = name.replace('\\"', '"').replace("\\\\", "\\")
        keys.append((int(code, 16), 1 if mod == "true" else 0, tuple(c * S for c in box), name))
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, "keyboard.png"), optimize=True)
    menu = (566 * S, 10 * S, 650 * S, 42 * S)
    hide = (664 * S, 10 * S, 752 * S, 42 * S)
    with open(os.path.join(OUT, "keyboard.txt"), "w") as f:
        f.write("# tools/import_st_keyboard.py (dsp-cpp's atari_st_vkb_data.inc, Hataroid's), do not edit\n")
        f.write("picture keyboard.png %d %d\n" % img.size)
        f.write("travel 3\n")
        f.write("button menu %d %d %d %d\n" % menu)
        f.write("button hide %d %d %d %d\n" % hide)
        for code, flags, (x1, y1, x2, y2), name in keys:
            f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, x1, y1, x2 + 1, y2 + 1, name))
    print("atarist: %d keys" % len(keys))


if __name__ == "__main__":
    main()
