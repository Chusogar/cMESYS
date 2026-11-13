#!/usr/bin/env python3
"""Takes the on-screen keyboards of dsp-cpp into the Android port: the
pictures and key tables of src/drivers/computers/*_vkb_data.inc (made by
dsp-cpp's tools/gen_*_vkb.py) and the QL's photo (tools/ql_keyboard.jpg),
written as main/ports/android/machines/<machine>/assets/keyboard.(png|jpg)
plus keyboard.txt, which KeyboardView reads:

  picture <file> <width> <height>
  crop <x> <y> <keyred 0|1>            (photo: the part shown, red backdrop out)
  travel <pixels>                      (how far a key that is down sinks)
  well <rrggbb>                        (C64: the colour the cap uncovers)
  button menu|hide <x1> <y1> <x2> <y2> (the app's MENÚ / ▼ buttons)
  key <code> <flags> <x1> <y1> <x2> <y2> [<fx1> <fy1> <fx2> <fy2> <cbm> <shift>]  # name

flags: 1 modifier (stays down for the next key), 2 lock, 4 RESTORE (C64),
8 SHIFT LOCK (C64). C64 keys also give the front face and the PETSCII codes
drawn on it from the character ROM at run time (as dsp-cpp does), so no ROM
data goes into the picture.

  python3 main/ports/android/tools/import_dsp_keyboards.py <dsp-cpp>
"""
import os
import re
import shutil
import sys
import zlib

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINES = os.path.join(HERE, "..", "machines")

# where the app's MENÚ / ▼ buttons go on each picture (free space on it)
BUTTONS = {
    "ql": ((980, 34, 1108, 98), (1120, 34, 1240, 98)),       # band above the keys
    "zxspec": ((1262, 24, 1372, 78), (1262, 88, 1372, 142)),  # right of 0, above P
    "cpc": ((820, 104, 980, 166), (1000, 104, 1100, 166)),    # the cassette area
    "c64": ((1060, 412, 1220, 468), (1240, 412, 1360, 468)),  # right of the space bar
}


def buttons(machine):
    (m, h) = BUTTONS[machine]
    return ["button menu %d %d %d %d" % m, "button hide %d %d %d %d" % h]


def c_bytes(text, name):
    m = re.search(r"%s\[\] = \{(.*?)\};" % re.escape(name), text, re.S)
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))


def c_int(text, name):
    return int(re.search(r"%s = (\d+);" % re.escape(name), text).group(1))


def picture(text, prefix, channels):
    w, h = c_int(text, "k%sVkbWidth" % prefix), c_int(text, "k%sVkbHeight" % prefix)
    raw = bytearray(zlib.decompress(c_bytes(text, "k%sVkbPicture" % prefix)))
    stride = w * channels
    for i in range(stride, len(raw)):
        raw[i] = (raw[i] + raw[i - stride]) & 0xff
    return Image.frombytes("RGBA" if channels == 4 else "RGB", (w, h), bytes(raw)), w, h


def keys(text, name):
    body = re.search(r"%s\[\] = \{(.*?)\n\};" % re.escape(name), text, re.S).group(1)
    out = []
    for line in body.splitlines():
        m = re.match(r"\s*\{([-0-9, ]+)\},\s*//\s*(.*)", line)
        if m:
            out.append(([int(v) for v in m.group(1).split(",")], m.group(2).strip()))
    return out


def write(machine, pic_line, keylines, extra=()):
    d = os.path.join(MACHINES, machine, "assets")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "keyboard.txt"), "w") as f:
        f.write("# from dsp-cpp (tools/import_dsp_keyboards.py), do not edit\n")
        f.write(pic_line + "\n")
        for e in extra:
            f.write(e + "\n")
        for k in keylines:
            f.write(k + "\n")
    return d


def generic(dsp, machine, inc, prefix, travel):
    text = open(os.path.join(dsp, "src/drivers/computers", inc)).read()
    img, w, h = picture(text, prefix, 4)
    lines = ["key %d %d %d %d %d %d  # %s" % (v[0], v[1], v[2], v[3], v[4], v[5], n)
             for v, n in keys(text, "k%sVkbKeys" % prefix)]
    d = write(machine, "picture keyboard.png %d %d" % (w, h), lines, ["travel %d" % travel] + buttons(machine))
    img.save(os.path.join(d, "keyboard.png"), optimize=True)
    print(machine, w, h, len(lines), "keys")


def c64(dsp):
    text = open(os.path.join(dsp, "src/drivers/computers/c64_vkb_data.inc")).read()
    img, w, h = picture(text, "C64", 3)
    lines = []
    for v, n in keys(text, "kC64VkbKeys"):
        col, row, fl = v[0], v[1], v[2]
        code = col * 8 + row if col >= 0 else -1
        flags = (1 if fl & 1 else 0) | (4 if fl & 2 else 0) | (8 if fl & 4 else 0)
        if fl & 2:
            code = 100                      # RESTORE (not in the matrix)
        if fl & 4:
            code = 1 * 8 + 7                # SHIFT LOCK holds the left SHIFT
        lines.append("key %d %d %s  # %s" % (code, flags, " ".join(str(x) for x in v[3:13]), n))
    # the colour a sunk cap uncovers: the well above the first key (as dsp-cpp)
    k0 = keys(text, "kC64VkbKeys")[0][0]
    well = img.getpixel((k0[3] + 20, k0[4] - 4))
    d = write("c64", "picture keyboard.png %d %d" % (w, h), lines,
              ["travel 5", "well %02x%02x%02x" % well[:3]] + buttons("c64"))
    img.save(os.path.join(d, "keyboard.png"), optimize=True)
    print("c64", w, h, len(lines), "keys")


def ql(dsp):
    text = open(os.path.join(dsp, "src/drivers/computers/ql_vkb_data.inc")).read()
    cx, cy = c_int(text, "kQlVkbCropX"), c_int(text, "kQlVkbCropY")
    lines = ["key %d %d %d %d %d %d  # %s" % (v[0], v[1], v[2], v[3], v[4], v[5], n)
             for v, n in keys(text, "kQlVkbKeys")]
    # only the keyboard part of the machine (up to the groove before the
    # microdrives): keys are bigger on a phone
    d = write("ql", "picture keyboard.jpg 1268 %d" % c_int(text, "kQlVkbHeight"), lines,
              ["crop %d %d 1" % (cx, cy), "travel 3"] + buttons("ql"))
    shutil.copy(os.path.join(dsp, "tools/ql_keyboard.jpg"), os.path.join(d, "keyboard.jpg"))
    print("ql", len(lines), "keys")


def main():
    dsp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "../../../../../chusogar/dsp-cpp")
    generic(dsp, "zxspec", "spectrum_vkb_data.inc", "Zx", 0)
    generic(dsp, "cpc", "amstrad_cpc_vkb_data.inc", "Cpc", 0)
    c64(dsp)
    ql(dsp)


if __name__ == "__main__":
    main()
