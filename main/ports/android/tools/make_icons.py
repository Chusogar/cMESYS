#!/usr/bin/env python3
"""Draws the Android port's icons (main/ports/android/app/res): 32x32 pixel art in
the style of Hataroid's GEM-desktop icons (white bodies, black outlines, a
grey shade), scaled without smoothing to each screen density, and the
launcher icon. Original drawings for cMESYS (not Hataroid's files).

  python3 main/ports/android/tools/make_icons.py
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "app", "res")
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3}

K = (0, 0, 0, 255)          # outline
W = (255, 255, 255, 255)    # body
G = (170, 170, 170, 255)    # shade
D = (96, 96, 96, 255)       # dark shade
R = (220, 30, 30, 255)      # QL red
B = (40, 90, 200, 255)      # accent blue
Y = (250, 200, 40, 255)     # accent yellow
T = (0, 0, 0, 0)

# 5x7 digits / letters for labels on the icons
FONT = {
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
}


def text(d, x, y, s, c, scale=1):
    for i, ch in enumerate(s):
        for r, line in enumerate(FONT[ch]):
            for col, p in enumerate(line):
                if p == "#":
                    d.rectangle([x + (i * 6 + col) * scale, y + r * scale,
                                 x + (i * 6 + col) * scale + scale - 1, y + r * scale + scale - 1], fill=c)


def new():
    im = Image.new("RGBA", (32, 32), T)
    return im, ImageDraw.Draw(im)


def cartridge(d, x, y, label=None):
    """A microdrive cartridge seen from above: body, tape window, label."""
    d.rectangle([x, y, x + 21, y + 15], fill=W, outline=K)          # body
    d.rectangle([x + 1, y + 13, x + 20, y + 14], fill=G)             # lower shade
    d.rectangle([x + 3, y + 2, x + 12, y + 7], fill=D, outline=K)    # tape window
    d.line([x + 4, y + 5, x + 11, y + 5], fill=G)                   # the tape
    d.rectangle([x + 15, y + 2, x + 19, y + 11], fill=B, outline=K)  # label
    d.line([x + 3, y + 10, x + 12, y + 10], fill=K)                 # grip ridges
    d.line([x + 3, y + 12, x + 12, y + 12], fill=K)
    if label:
        text(d, x + 15, y + 3, label, W) if False else None


def icon_mdv():
    im, d = new()
    cartridge(d, 2, 4)
    cartridge(d, 8, 12)
    return im


def icon_drive(n):
    im, d = new()
    cartridge(d, 1, 8)
    d.rectangle([22, 2, 30, 12], fill=W, outline=K)
    text(d, 24, 4, n, K)
    return im


def icon_eject():
    im, d = new()
    cartridge(d, 5, 14)
    d.polygon([(16, 1), (25, 9), (19, 9), (19, 12), (13, 12), (13, 9), (7, 9)], fill=Y, outline=K)
    return im


def icon_firmware():
    im, d = new()
    cartridge(d, 5, 3)
    d.rectangle([4, 21, 27, 29], fill=G, outline=K)                # chip
    for x in range(6, 27, 3):
        d.line([x, 19, x, 20], fill=K)
        d.line([x, 30, x, 31], fill=K)
    return im


def icon_reset():
    im, d = new()
    d.arc([4, 4, 27, 27], 40, 330, fill=K, width=5)
    d.arc([5, 5, 26, 26], 40, 330, fill=W, width=3)
    d.polygon([(19, 2), (29, 8), (20, 14)], fill=W, outline=K)
    return im


def icon_pause():
    im, d = new()
    d.ellipse([2, 2, 29, 29], fill=W, outline=K)
    d.ellipse([3, 25, 28, 29], fill=G)
    d.ellipse([2, 2, 29, 29], outline=K)
    d.rectangle([10, 9, 13, 22], fill=K)
    d.rectangle([18, 9, 21, 22], fill=K)
    return im


def ql_computer(d, x, y):
    """The QL: a flat black wedge with the red QL badge and microdrive slots."""
    d.polygon([(x, y + 11), (x + 4, y), (x + 27, y), (x + 29, y + 11)], fill=(30, 30, 30, 255), outline=K)
    d.rectangle([x, y + 11, x + 29, y + 15], fill=D, outline=K)
    for r in range(3):                                             # key rows
        yy = y + 2 + r * 3
        for k in range(5 + r, 24 - r, 3):
            d.rectangle([x + k, yy, x + k + 1, yy + 1], fill=G)
    d.rectangle([x + 21, y + 12, x + 27, y + 13], fill=K)           # mdv slots
    d.rectangle([x + 2, y + 12, x + 7, y + 14], fill=R)              # badge


def icon_settings():
    im, d = new()
    ql_computer(d, 1, 6)
    d.ellipse([20, 18, 30, 28], fill=G, outline=K)                  # cog
    d.ellipse([23, 21, 27, 25], fill=W, outline=K)
    for (a, b) in [(25, 17), (25, 29), (19, 23), (31, 23)]:
        d.rectangle([a - 1, b - 1, a, b], fill=K)
    return im


def icon_quit():
    im, d = new()
    d.rectangle([7, 7, 24, 29], fill=W, outline=K)                   # bin
    d.rectangle([5, 4, 26, 7], fill=W, outline=K)                    # lid
    d.rectangle([13, 1, 18, 4], fill=W, outline=K)
    for x in (11, 15, 19):
        d.line([x, 10, x, 26], fill=G)
        d.line([x + 1, 10, x + 1, 26], fill=K)
    return im


def icon_help():
    im, d = new()
    d.polygon([(6, 1), (20, 1), (26, 7), (26, 30), (6, 30)], fill=W, outline=K)
    d.polygon([(20, 1), (20, 7), (26, 7)], fill=G, outline=K)
    for y in range(11, 27, 3):
        d.line([9, y, 23, y], fill=D)
    return im


def icon_keyboard():
    im, d = new()
    d.rectangle([1, 8, 30, 24], fill=W, outline=K)
    d.rectangle([2, 22, 29, 23], fill=G)
    for r in range(3):
        y = 10 + r * 4
        off = 3 + r
        for k in range(off, 28 - r, 4):
            d.rectangle([k, y, k + 2, y + 2], fill=G, outline=K)
    d.rectangle([9, 21, 22, 22], fill=K)                             # space
    return im


def icon_speed(turbo):
    im, d = new()
    d.pieslice([2, 6, 29, 33], 180, 360, fill=W, outline=K)
    d.arc([2, 6, 29, 33], 180, 360, fill=K)
    for a in range(0, 5):
        import math
        t = math.pi + a * math.pi / 4
        d.point([(15.5 + 12 * math.cos(t), 19.5 + 12 * math.sin(t))], fill=K)
    import math
    t = math.pi * (1.85 if turbo else 1.4)
    d.line([16, 19, 16 + 11 * math.cos(t), 19 + 11 * math.sin(t)], fill=R if turbo else K, width=2)
    d.ellipse([13, 16, 18, 21], fill=K)
    return im


def icon_screen():
    im, d = new()
    d.rectangle([2, 3, 29, 23], fill=G, outline=K)
    d.rectangle([5, 6, 26, 20], fill=(20, 30, 60, 255), outline=K)
    d.rectangle([11, 24, 20, 26], fill=G, outline=K)
    d.rectangle([7, 27, 24, 29], fill=W, outline=K)
    for (a, b, c, e) in [(7, 8, 10, 8), (7, 8, 7, 11), (24, 18, 21, 18), (24, 18, 24, 15)]:
        d.line([a, b, c, e], fill=Y)
    d.line([7, 8, 12, 12], fill=Y)
    d.line([24, 18, 19, 14], fill=Y)
    return im


def icon_keys():
    im, d = new()
    d.rectangle([2, 6, 29, 25], fill=W, outline=K)
    d.rectangle([3, 22, 28, 24], fill=G)
    text(d, 5, 9, "F", K, 2)
    text(d, 18, 9, "1", K, 2)
    return im


def icon_folder():
    im, d = new()
    d.polygon([(2, 7), (11, 7), (14, 10), (29, 10), (29, 27), (2, 27)], fill=W, outline=K)
    d.rectangle([3, 13, 28, 13], fill=K)
    d.rectangle([3, 24, 28, 26], fill=G)
    return im


def icon_file():
    im, d = icon_mdv(), None
    return im


def icon_launcher(size):
    """The app icon: a QL on a dark rounded square, with QL letters."""
    s = 64
    im = Image.new("RGBA", (s, s), T)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, s - 1, s - 1], 12, fill=(28, 34, 52, 255))
    # the QL wedge, larger
    d.polygon([(6, 34), (12, 22), (52, 22), (58, 34)], fill=(20, 20, 20, 255), outline=(120, 120, 120, 255))
    d.rectangle([6, 34, 58, 42], fill=(60, 60, 60, 255), outline=(120, 120, 120, 255))
    for r in range(3):
        y = 25 + r * 3
        for k in range(15 + 2 * r, 50 - 2 * r, 4):
            d.rectangle([k, y, k + 2, y + 1], fill=(170, 170, 170, 255))
    d.rectangle([44, 37, 54, 38], fill=K)
    d.rectangle([10, 36, 20, 40], fill=R)
    text(d, 12, 6, "QL", W, 2)
    d.rectangle([40, 8, 55, 13], fill=R)
    return im.resize((size, size), Image.NEAREST)


def launcher_base():
    s = 64
    im = Image.new("RGBA", (s, s), T)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, s - 1, s - 1], 12, fill=(28, 34, 52, 255))
    return im, d


def keys_rows(d, x1, x2, y, rows, color, step=4):
    for r in range(rows):
        for k in range(x1, x2, step):
            d.rectangle([k, y + r * 3, k + 2, y + r * 3 + 1], fill=color)


def launcher_zxspec(size):
    """A 48K Spectrum: black, the rainbow stripe, rubber keys."""
    im, d = launcher_base()
    d.rectangle([6, 22, 58, 44], fill=(24, 24, 26, 255), outline=(120, 120, 120, 255))
    keys_rows(d, 10, 44, 27, 4, (150, 150, 156, 255))
    for i, c in enumerate([(220, 30, 30), (240, 200, 20), (40, 180, 60), (40, 160, 220)]):
        d.polygon([(44 + i * 3, 44), (50 + i * 3, 34), (52 + i * 3, 34), (46 + i * 3, 44)], fill=c + (255,))
    text(d, 20, 6, "ZX", W, 2)
    return im.resize((size, size), Image.NEAREST)


def launcher_cpc(size):
    """A CPC: charcoal case, the coloured keys."""
    im, d = launcher_base()
    d.rectangle([4, 24, 60, 44], fill=(60, 61, 64, 255), outline=(130, 130, 134, 255))
    keys_rows(d, 8, 44, 29, 4, (24, 25, 27, 255))
    d.rectangle([8, 29, 10, 30], fill=(214, 40, 46, 255))
    d.rectangle([40, 32, 44, 37], fill=(86, 116, 178, 255))
    d.rectangle([8, 35, 12, 36], fill=(70, 182, 104, 255))
    keys_rows(d, 48, 58, 29, 4, (24, 25, 27, 255))
    text(d, 15, 6, "CPC", W, 2)
    return im.resize((size, size), Image.NEAREST)


def launcher_c64(size):
    """The breadbin: beige, brown keys, the four function keys."""
    im, d = launcher_base()
    d.polygon([(4, 44), (8, 24), (56, 24), (60, 44)], fill=(196, 184, 156, 255), outline=(240, 230, 210, 255))
    d.rectangle([10, 27, 47, 41], fill=(38, 30, 26, 255))
    keys_rows(d, 11, 46, 28, 4, (98, 82, 70, 255))
    for i in range(4):
        d.rectangle([50, 27 + i * 3, 54, 28 + i * 3], fill=(230, 222, 200, 255))
    text(d, 15, 6, "C64", W, 2)
    return im.resize((size, size), Image.NEAREST)


def launcher_msx(size):
    """An MSX: black, grey function keys and cursors."""
    im, d = launcher_base()
    d.rectangle([4, 22, 60, 44], fill=(40, 41, 44, 255), outline=(120, 120, 124, 255))
    for i in range(5):
        d.rectangle([8 + i * 7, 25, 13 + i * 7, 26], fill=(176, 178, 180, 255))
    keys_rows(d, 8, 46, 29, 4, (14, 14, 16, 255))
    for (x, y) in [(52, 34), (48, 38), (52, 38), (56, 38)]:
        d.rectangle([x, y, x + 2, y + 2], fill=(176, 178, 180, 255))
    text(d, 15, 6, "MSX", W, 2)
    return im.resize((size, size), Image.NEAREST)


def launcher_lynx(size):
    """The Lynx: a wide grey handheld, its screen, the cross and A / B."""
    im, d = launcher_base()
    d.rounded_rectangle([3, 24, 60, 46], 8, fill=(78, 80, 86, 255), outline=(140, 142, 148, 255))
    d.rectangle([20, 27, 43, 42], fill=(20, 30, 60, 255), outline=K)
    d.rectangle([24, 31, 30, 36], fill=(220, 120, 40, 255))
    d.rectangle([33, 34, 39, 39], fill=(60, 180, 220, 255))
    d.rectangle([9, 33, 15, 35], fill=(26, 27, 30, 255))
    d.rectangle([11, 31, 13, 37], fill=(26, 27, 30, 255))
    d.ellipse([47, 30, 51, 34], fill=(26, 27, 30, 255))
    d.ellipse([52, 35, 56, 39], fill=(26, 27, 30, 255))
    text(d, 9, 6, "LYNX", W, 2)
    return im.resize((size, size), Image.NEAREST)


def launcher_arcade(size):
    """An arcade cabinet: marquee, screen, the control panel with a red-ball joystick and buttons."""
    im, d = launcher_base()
    d.polygon([(16, 6), (48, 6), (50, 58), (14, 58)], fill=(40, 44, 70, 255), outline=(150, 160, 200, 255))
    d.rectangle([17, 8, 47, 14], fill=(240, 190, 40, 255))
    d.rectangle([19, 17, 45, 35], fill=(10, 12, 20, 255), outline=K)
    d.rectangle([24, 22, 28, 26], fill=(60, 200, 90, 255))
    d.rectangle([34, 27, 39, 31], fill=(230, 80, 60, 255))
    d.polygon([(14, 40), (50, 40), (54, 46), (10, 46)], fill=(70, 74, 100, 255), outline=(150, 160, 200, 255))
    d.line([(22, 43), (21, 38)], fill=(150, 150, 160, 255), width=2)
    d.ellipse([18, 34, 24, 40], fill=(230, 40, 34, 255))
    d.ellipse([33, 41, 37, 44], fill=(46, 125, 232, 255))
    d.ellipse([40, 41, 44, 44], fill=(242, 194, 48, 255))
    d.rectangle([28, 50, 36, 54], fill=(20, 20, 24, 255))
    d.rectangle([31, 51, 33, 53], fill=(255, 150, 40, 255))
    return im.resize((size, size), Image.NEAREST)


LAUNCHERS = {"ql": icon_launcher, "zxspec": launcher_zxspec, "cpc": launcher_cpc, "c64": launcher_c64,
             "msx": launcher_msx, "lynx": launcher_lynx, "arcade": launcher_arcade}


ICONS = {
    "ic_mdv": icon_mdv, "ic_mdv1": lambda: icon_drive("1"), "ic_mdv2": lambda: icon_drive("2"),
    "ic_eject": icon_eject, "ic_firmware": icon_firmware, "ic_reset": icon_reset, "ic_pause": icon_pause,
    "ic_settings": icon_settings, "ic_quit": icon_quit, "ic_help": icon_help, "ic_keyboard": icon_keyboard,
    "ic_speed": lambda: icon_speed(False), "ic_turbo": lambda: icon_speed(True), "ic_screen": icon_screen,
    "ic_keys": icon_keys, "ic_folder": icon_folder, "ic_file": icon_file,
}


def main():
    for name, fn in ICONS.items():
        im = fn()
        for dens, f in DENSITIES.items():
            out = os.path.join(RES, "drawable-" + dens)
            os.makedirs(out, exist_ok=True)
            im.resize((int(32 * f), int(32 * f)), Image.NEAREST).save(os.path.join(out, name + ".png"))
    # each machine's launcher icon: machines/<machine>/res/mipmap-*
    for machine, fn in LAUNCHERS.items():
        for dens, f in DENSITIES.items():
            out = os.path.join(HERE, "..", "arcade" if machine == "arcade" else os.path.join("machines", machine),
                               "res", "mipmap-" + dens)
            os.makedirs(out, exist_ok=True)
            fn(int(48 * f)).save(os.path.join(out, "ic_launcher.png"))
    # a contact sheet for the docs
    names = list(ICONS)
    sheet = Image.new("RGBA", (len(names) * 72, 72), (60, 60, 60, 255))
    for i, n in enumerate(names):
        im = ICONS[n]().resize((64, 64), Image.NEAREST)
        sheet.paste(im, (i * 72 + 4, 4), im)
    sheet.save(os.path.join(HERE, "..", "icons.png"))
    launch = Image.new("RGBA", (len(LAUNCHERS) * 104, 104), (60, 60, 60, 255))
    for i, fn in enumerate(LAUNCHERS.values()):
        im = fn(96)
        launch.paste(im, (i * 104 + 4, 4), im)
    launch.save(os.path.join(HERE, "..", "launchers.png"))


main()
