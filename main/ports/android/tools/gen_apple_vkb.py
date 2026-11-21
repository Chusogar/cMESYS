#!/usr/bin/env python3
"""Draws the Apple machines' on-screen keyboards for the Android port, the
same way as the MSX's (gen_msx_vkb.py, vkb_common.py: drawn from scratch with
FreeSans, sculpted caps, no logos or badges: the Apple keys are spelt out).

  apple2/assets/keyboard_ii   the Apple ][ / ][+: the 52-key keyboard, upper
                              case, RESET, REPT, the two arrows (A2K_* codes)
  apple2/assets/keyboard_iie  the Apple //e: US layout, DELETE, the Open and
                              Solid Apple keys, four arrows, RESET (A2K_*)
  apple2gs/assets/keyboard    the Apple Desktop Bus keyboard of the IIGS
                              (M0116: control, option, command, keypad, the
                              RESET key; ADB codes)
  macplus/assets/keyboard     the Macintosh Plus keyboard (M0110A: keypad,
                              arrows, option, command; the Mac's key codes)
  macii/assets/keyboard       the Apple Extended Keyboard (F1-F15, the
                              navigation keys, keypad, power; ADB codes)

Each as keyboard*.png + keyboard*.txt (the tiles KeyboardView reads; flags:
1 a modifier that stays down for the next key, 2 a locking key).

usage: gen_apple_vkb.py [--preview <dir>]
"""
import os
import sys

from vkb_common import Canvas, lerp

HERE = os.path.dirname(os.path.abspath(__file__))
from android_dirs import MACHINES, machine_dir
SYMBOLS = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

GAP = 0.07
MOD, LOCK = 1, 2


class Board:
    """One keyboard: a case, the keys in key units (U pixels) from (left, top)."""

    def __init__(self, units_w, units_h, case, keys, legend_dark, U=76.0, margin=0.45, buttons_units=3.2):
        self.U = U
        self.units_w = units_w + buttons_units if buttons_units else units_w
        self.W = int(round((self.units_w + 2 * margin) * U))
        self.H = int(round((units_h + 2 * margin) * U))
        self.left = margin * U
        self.top = margin * U
        self.pal = keys
        self.c = Canvas(self.W, self.H)
        self.keys = []
        self.buttons = []
        top, bot = case
        c = self.c
        c.shadow([(14, 10, self.W - 14, self.H - 4, 18)], 7, alpha=170)
        c.vgrad_rrect(8, 4, self.W - 8, self.H - 8, 16, top, bot)
        c.vgrad_rrect(8, 4, self.W - 8, 12, 6, lerp(top, (255, 255, 255), 0.25), top)

    def ux(self, v):
        return self.left + v * self.U

    def uy(self, v):
        return self.top + v * self.U

    def well(self, x1, y1, x2, y2, color=(20, 20, 22)):
        c = self.c
        c.rrect(self.ux(x1) - 7, self.uy(y1) - 7, self.ux(x2) + 7, self.uy(y2) + 8, 8, color + (255,))
        c.rrect(self.ux(x1) - 5, self.uy(y1) - 5, self.ux(x2) + 5, self.uy(y2) + 5, 7,
                lerp(color, (0, 0, 0), 0.3) + (255,))

    def cap(self, x, y, w, h, legends, code, flags=0, name="", size=None, pal=None, sym=False, align="mm"):
        """A key at (x, y), w x h units; legends: one (centred) or two (shifted
        over unshifted); align "lb": one legend at the bottom left (the Mac's)."""
        U, c = self.U, self.c
        top, top2, side, front, ink = self.pal[pal or "key"]
        g = GAP * U
        x1, y1 = self.ux(x) + g / 2, self.uy(y) + g / 2
        x2, y2 = self.ux(x + w) - g / 2, self.uy(y + h) - g / 2
        c.shadow([(x1, y1 + 3, x2, y2 + 4, 8)], 3, alpha=200)
        c.rrect(x1, y1, x2, y2, 7, side + (255,))
        ix, it, ib = 0.10 * U, 0.04 * U, (0.19 if h >= 1 else 0.13) * U
        tx1, ty1, tx2, ty2 = x1 + ix, y1 + it, x2 - ix, y2 - ib
        steps = c.s(y2 - 2) - c.s(ty2)
        for i in range(steps):
            t = i / max(1, steps)
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
        font = SYMBOLS if sym else None
        sz = size or 0.30 * U
        mw = tx2 - tx1 - 8
        if align == "lb" and len(legends) == 1:
            c.text(tx1 + 0.08 * U, ty2 - 0.09 * U, legends[0], sz, ink, "ls", max_w=mw, font=font)
        elif len(legends) == 1:
            c.text((tx1 + tx2) / 2, (ty1 + ty2) / 2, legends[0], sz, ink, "mm", max_w=mw, font=font)
        elif len(legends) == 2:
            s1 = size or 0.24 * U
            s2 = size or 0.27 * U
            c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.30, legends[0], s1, ink, "mm", max_w=mw, font=font)
            c.text((tx1 + tx2) / 2, ty1 + (ty2 - ty1) * 0.73, legends[1], s2, ink, "mm", max_w=mw, font=font)
        if code is not None:
            self.keys.append((code, flags, (x1, y1, x2, y2), name or " ".join(l for l in legends if l) or "?"))

    def menu_buttons(self, x, y, w, h):
        """The app's MENÚ and ▼ buttons: two tiles over this free area (units)."""
        g = GAP * self.U / 2
        mid = y + h / 2
        self.buttons = [("menu", (self.ux(x) + g, self.uy(y) + g, self.ux(x + w) - g, self.uy(mid - 0.1) - g)),
                        ("hide", (self.ux(x) + g, self.uy(mid + 0.1) + g, self.ux(x + w) - g, self.uy(y + h) - g))]

    def save(self, machine, name, title, preview=None):
        img = self.c.final()
        out = os.path.join(machine_dir(machine), "assets")
        os.makedirs(out, exist_ok=True)
        img.save(os.path.join(out, name + ".png"), optimize=True)
        with open(os.path.join(out, name + ".txt"), "w", encoding="utf-8") as f:
            f.write("# tools/gen_apple_vkb.py (%s), do not edit\n" % title)
            f.write("picture %s.png %d %d\n" % (name, self.W, self.H))
            for b, r in self.buttons:
                f.write("button %s %d %d %d %d\n" % ((b,) + tuple(round(v) for v in r)))
            for code, flags, (x1, y1, x2, y2), label in self.keys:
                f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, round(x1), round(y1), round(x2), round(y2), label))
        if preview:
            from PIL import Image
            bg = Image.new("RGBA", img.size, (0, 0, 120, 255))
            bg.alpha_composite(img)
            os.makedirs(preview, exist_ok=True)
            bg.convert("RGB").save(os.path.join(preview, "%s_%s.png" % (machine, name)))
        print("%s/%s: %dx%d, %d keys" % (machine, name, self.W, self.H, len(self.keys)))


# --- palettes: top face, its lower edge, side skirt, front skirt, legend ---------------
def pal(top, ink):
    return (top, lerp(top, (0, 0, 0), 0.10), lerp(top, (0, 0, 0), 0.25), lerp(top, (0, 0, 0), 0.42), ink)


BROWN = {"key": pal((84, 70, 58), (238, 230, 214)), "mod": pal((70, 58, 48), (238, 230, 214))}
PLATINUM = {"key": pal((226, 222, 212), (40, 40, 44)), "mod": pal((196, 192, 182), (40, 40, 44))}
BEIGE = {"key": pal((222, 214, 194), (44, 42, 40)), "mod": pal((196, 188, 168), (44, 42, 40))}


# --- the Apple ][ and ][+ (A2K_* of apple2_core.h) --------------------------------------
def apple2_ii(preview):
    b = Board(15, 5, ((206, 192, 156), (178, 162, 124)), BROWN, None)
    s = 0.17 * b.U
    b.well(0, 0, 15, 5, (52, 44, 36))
    r = 0
    x = 1.0
    for top, low in [("!", "1"), ('"', "2"), ("#", "3"), ("$", "4"), ("%", "5"), ("&", "6"), ("'", "7"),
                     ("(", "8"), (")", "9"), ("", "0"), ("*", ":"), ("=", "-")]:
        b.cap(x, r, 1, 1, [top, low] if top else [low], ord(low))
        x += 1
    b.cap(13.5, r, 1.5, 1, ["RESET"], 0x07, size=s, pal="mod")
    r = 1
    b.cap(0.5, r, 1.5, 1, ["ESC"], 0x1B, size=s, pal="mod")
    x = 2.0
    for ch in "QWERTYUIO":
        b.cap(x, r, 1, 1, [ch], ord(ch))
        x += 1
    b.cap(x, r, 1, 1, ["@", "P"], ord("P"))
    b.cap(x + 1, r, 1, 1, ["REPT"], 0x0E, size=s, pal="mod")
    b.cap(x + 2, r, 15 - x - 2, 1, ["RETURN"], 0x0D, size=s, pal="mod")
    r = 2
    b.cap(0.25, r, 1.75, 1, ["CTRL"], 0x01, MOD, size=s, pal="mod")
    x = 2.0
    for ch in "ASDFGHJKL":
        b.cap(x, r, 1, 1, ["BELL" if ch == "G" else "", ch] if ch == "G" else [ch], ord(ch),
              size=None if ch != "G" else 0.2 * b.U)
        x += 1
    b.cap(x, r, 1, 1, ["+", ";"], 0x3B)
    b.cap(x + 1, r, 1, 1, ["←"], 0x08, size=0.34 * b.U, pal="mod", sym=True)
    b.cap(x + 2, r, 1, 1, ["→"], 0x15, size=0.34 * b.U, pal="mod", sym=True)
    r = 3
    b.cap(0, r, 2.25, 1, ["SHIFT"], 0x02, MOD, size=s, pal="mod", name="SHIFT (left)")
    x = 2.25
    for ch in "ZXCVB":
        b.cap(x, r, 1, 1, [ch], ord(ch))
        x += 1
    b.cap(x, r, 1, 1, ["^", "N"], ord("N"))
    b.cap(x + 1, r, 1, 1, ["M"], ord("M"))
    x += 2
    for top, low in [("<", ","), (">", "."), ("?", "/")]:
        b.cap(x, r, 1, 1, [top, low], ord(low))
        x += 1
    b.cap(x, r, 15 - x, 1, ["SHIFT"], 0x03, MOD, size=s, pal="mod", name="SHIFT (right)")
    b.cap(3.5, 4, 9.0, 1, [""], 0x20, name="SPACE")
    b.menu_buttons(15.55, 0.4, 3.0, 4.2)
    b.save("apple2", "keyboard_ii", "the Apple ][ / ][+", preview)


# --- the Apple //e (A2K_*) -------------------------------------------------------------
def apple2_iie(preview):
    b = Board(15, 5.75, ((204, 194, 164), (176, 166, 134)), BROWN, None)
    s = 0.16 * b.U
    b.cap(13.5, 0, 1.5, 0.65, ["RESET"], 0x07, size=0.15 * b.U, pal="mod")
    R = 0.75
    b.well(0, R, 15, R + 5, (52, 44, 36))
    b.cap(0, R, 1, 1, ["esc"], 0x1B, size=s, pal="mod")
    x = 1
    for top, low in [("!", "1"), ("@", "2"), ("#", "3"), ("$", "4"), ("%", "5"), ("^", "6"), ("&", "7"),
                     ("*", "8"), ("(", "9"), (")", "0"), ("_", "-"), ("+", "=")]:
        b.cap(x, R, 1, 1, [top, low], ord(low))
        x += 1
    b.cap(x, R, 15 - x, 1, ["delete"], 0x7F, size=s, pal="mod")
    r = R + 1
    b.cap(0, r, 1.5, 1, ["tab"], 0x09, size=s, pal="mod")
    x = 1.5
    for ch in "QWERTYUIOP":
        b.cap(x, r, 1, 1, [ch], ord(ch))
        x += 1
    for top, low in [("{", "["), ("}", "]")]:
        b.cap(x, r, 1, 1, [top, low], ord(low))
        x += 1
    b.cap(x, r, 15 - x, 1, ["|", "\\"], 0x5C)
    r = R + 2
    b.cap(0, r, 1.75, 1, ["control"], 0x01, MOD, size=s, pal="mod")
    x = 1.75
    for ch in "ASDFGHJKL":
        b.cap(x, r, 1, 1, [ch], ord(ch))
        x += 1
    b.cap(x, r, 1, 1, [":", ";"], 0x3B)
    b.cap(x + 1, r, 1, 1, ['"', "'"], 0x27)
    b.cap(x + 2, r, 15 - x - 2, 1, ["return"], 0x0D, size=s, pal="mod")
    r = R + 3
    b.cap(0, r, 2.25, 1, ["shift"], 0x02, MOD, size=s, pal="mod", name="shift (left)")
    x = 2.25
    for ch in "ZXCVBNM":
        b.cap(x, r, 1, 1, [ch], ord(ch))
        x += 1
    for top, low in [("<", ","), (">", "."), ("?", "/")]:
        b.cap(x, r, 1, 1, [top, low], ord(low))
        x += 1
    b.cap(x, r, 15 - x, 1, ["shift"], 0x03, MOD, size=s, pal="mod", name="shift (right)")
    r = R + 4
    b.cap(0, r, 1.5, 1, ["caps", "lock"], 0x04, size=0.15 * b.U, pal="mod", name="caps lock")
    b.cap(1.5, r, 1.25, 1, ["open", "apple"], 0x05, MOD, size=0.14 * b.U, pal="mod", name="open apple")
    b.cap(2.75, r, 1, 1, ["~", "`"], 0x60)
    b.cap(3.75, r, 6.25, 1, [""], 0x20, name="space")
    b.cap(10, r, 1, 1, ["solid", "apple"], 0x06, MOD, size=0.13 * b.U, pal="mod", name="solid apple")
    for i, (sym, code) in enumerate([("←", 0x08), ("→", 0x15), ("↓", 0x0A), ("↑", 0x0B)]):
        b.cap(11 + i, r, 1, 1, [sym], code, size=0.34 * b.U, pal="mod", sym=True)
    b.menu_buttons(15.55, R + 0.4, 3.0, 4.2)
    b.save("apple2", "keyboard_iie", "the Apple //e", preview)


# --- the ADB / Mac keyboards: the main block (Apple's key codes) -------------------------
def mac_main(b, R, s, first, control, caps_row3, bottom):
    """Rows R..R+4, 15 units wide. first: (legend, code) of the key left of 1;
    caps_row3: the caps lock key starts row 3 (the Mac Plus) instead of control;
    bottom: [(legends, code, width, flags)] of row 5."""
    leg, code = first
    b.cap(0, R, 1, 1, leg, code, size=s if len(leg) == 1 and len(leg[0]) > 1 else None, pal="mod" if code == 0x35 else None)
    digits = [0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D]
    x = 1
    for (top, low), code in zip([("!", "1"), ("@", "2"), ("#", "3"), ("$", "4"), ("%", "5"), ("^", "6"),
                                 ("&", "7"), ("*", "8"), ("(", "9"), (")", "0"), ("_", "-"), ("+", "=")],
                                digits + [0x1B, 0x18]):
        b.cap(x, R, 1, 1, [top, low], code)
        x += 1
    b.cap(x, R, 15 - x, 1, ["delete"], 0x33, size=s, pal="mod", align="lb")
    letters = {"A": 0x00, "S": 0x01, "D": 0x02, "F": 0x03, "H": 0x04, "G": 0x05, "Z": 0x06, "X": 0x07, "C": 0x08,
               "V": 0x09, "B": 0x0B, "Q": 0x0C, "W": 0x0D, "E": 0x0E, "R": 0x0F, "Y": 0x10, "T": 0x11, "O": 0x1F,
               "U": 0x20, "I": 0x22, "P": 0x23, "L": 0x25, "J": 0x26, "K": 0x28, "N": 0x2D, "M": 0x2E}
    r = R + 1
    b.cap(0, r, 1.5, 1, ["tab"], 0x30, size=s, pal="mod", align="lb")
    x = 1.5
    for ch in "QWERTYUIOP":
        b.cap(x, r, 1, 1, [ch], letters[ch])
        x += 1
    b.cap(x, r, 1, 1, ["{", "["], 0x21)
    b.cap(x + 1, r, 1, 1, ["}", "]"], 0x1E)
    b.cap(x + 2, r, 15 - x - 2, 1, ["|", "\\"], 0x2A)
    r = R + 2
    if caps_row3:
        b.cap(0, r, 1.75, 1, ["caps lock"], 0x39, LOCK, size=s, pal="mod", align="lb")
    else:
        b.cap(0, r, 1.75, 1, ["control"], control, MOD, size=s, pal="mod", align="lb")
    x = 1.75
    for ch in "ASDFGHJKL":
        b.cap(x, r, 1, 1, [ch], letters[ch])
        x += 1
    b.cap(x, r, 1, 1, [":", ";"], 0x29)
    b.cap(x + 1, r, 1, 1, ['"', "'"], 0x27)
    b.cap(x + 2, r, 15 - x - 2, 1, ["return"], 0x24, size=s, pal="mod", align="lb")
    r = R + 3
    b.cap(0, r, 2.25, 1, ["shift"], 0x38, MOD, size=s, pal="mod", align="lb", name="shift (left)")
    x = 2.25
    for ch in "ZXCVBNM":
        b.cap(x, r, 1, 1, [ch], letters[ch])
        x += 1
    for (top, low), code in zip([("<", ","), (">", "."), ("?", "/")], [0x2B, 0x2F, 0x2C]):
        b.cap(x, r, 1, 1, [top, low], code)
        x += 1
    return x


def keypad(b, X, R, s, top_row, enter=0x4C):
    """The numeric keypad, 4 x 5 units at (X, R): top_row [(legend, code)]."""
    for i, (leg, code) in enumerate(top_row):
        b.cap(X + i, R, 1, 1, [leg], code, size=s if len(leg) > 1 else 0.3 * b.U, pal="mod",
              name="KP " + leg)
    rows = [[("7", 0x59), ("8", 0x5B), ("9", 0x5C), ("-", 0x4E)],
            [("4", 0x56), ("5", 0x57), ("6", 0x58), ("+", 0x45)],
            [("1", 0x53), ("2", 0x54), ("3", 0x55)]]
    for j, row in enumerate(rows):
        for i, (leg, code) in enumerate(row):
            b.cap(X + i, R + 1 + j, 1, 1, [leg], code, name="KP " + leg)
    b.cap(X + 3, R + 3, 1, 2, ["enter"], enter, size=s, pal="mod", name="KP enter")
    b.cap(X, R + 4, 2, 1, ["0"], 0x52, name="KP 0")
    b.cap(X + 2, R + 4, 1, 1, ["."], 0x41, name="KP .")


# --- the Apple IIGS: the Apple Desktop Bus keyboard (M0116) ----------------------------------
def apple2gs(preview):
    b = Board(19.5, 5.85, ((206, 202, 192), (178, 174, 164)), PLATINUM, None, buttons_units=0)
    s = 0.16 * b.U
    R = 0.85
    b.cap(7, 0, 1.5, 0.7, ["RESET ◁"], 0x7F, size=0.15 * b.U, pal="mod", sym=True, name="RESET")
    b.well(0, R, 15, R + 5, (96, 94, 90))
    b.well(15.5, R, 19.5, R + 5, (96, 94, 90))
    x = mac_main(b, R, s, (["esc"], 0x35), 0x36, False, None)
    b.cap(x, R + 3, 15 - x, 1, ["shift"], 0x38, MOD, size=s, pal="mod", align="lb", name="shift (right)")
    r = R + 4
    x = 0
    for leg, code, w, fl, sym in [(["caps", "lock"], 0x39, 1.25, LOCK, False), (["option"], 0x3A, 1.25, MOD, False),
                                  (["⌘"], 0x37, 1.5, MOD, True), (["~", "`"], 0x32, 1, 0, False)]:
        b.cap(x, r, w, 1, leg, code, fl, size=(0.14 * b.U if len(leg) == 2 and code == 0x39 else
                                              0.34 * b.U if sym else (s if code != 0x32 else None)),
              pal="mod" if code != 0x32 else None, sym=sym, name={0x37: "command"}.get(code, ""))
        x += w
    b.cap(x, r, 11 - x, 1, [""], 0x31, name="space")
    for i, (sym, code) in enumerate([("←", 0x3B), ("→", 0x3C), ("↓", 0x3D), ("↑", 0x3E)]):
        b.cap(11 + i, r, 1, 1, [sym], code, size=0.34 * b.U, pal="mod", sym=True)
    keypad(b, 15.5, R, s, [("clear", 0x47), ("=", 0x51), ("/", 0x4B), ("*", 0x43)])
    b.menu_buttons(15.5, 0, 4.0, 0.75)
    b.buttons = [("menu", (b.ux(15.55), b.uy(0.02), b.ux(17.45), b.uy(0.72))),
                 ("hide", (b.ux(17.55), b.uy(0.02), b.ux(19.45), b.uy(0.72)))]
    b.save("apple2gs", "keyboard", "the Apple IIGS's ADB keyboard", preview)


# --- the Macintosh Plus: M0110A (macplus_core.h's MK_* codes) -----------------------------
def macplus(preview):
    b = Board(19.5, 5.85, ((214, 206, 186), (186, 178, 158)), BEIGE, None, buttons_units=0)
    s = 0.16 * b.U
    R = 0.85
    b.well(0, R, 15, R + 5, (100, 96, 88))
    b.well(15.5, R, 19.5, R + 5, (100, 96, 88))
    x = mac_main(b, R, s, (["~", "`"], 0x32), None, True, None)
    b.cap(x, R + 3, 14 - x, 1, ["shift"], 0x38, MOD, size=s, pal="mod", align="lb", name="shift (right)")
    b.cap(14, R + 3, 1, 1, ["↑"], 0x7E, size=0.34 * b.U, pal="mod", sym=True)
    r = R + 4
    b.cap(0, r, 1.5, 1, ["option"], 0x3A, MOD, size=s, pal="mod", align="lb")
    b.cap(1.5, r, 1.5, 1, ["⌘"], 0x37, MOD, size=0.34 * b.U, pal="mod", sym=True, name="command")
    b.cap(3, r, 9, 1, [""], 0x31, name="space")
    for i, (sym, code) in enumerate([("←", 0x7B), ("→", 0x7C), ("↓", 0x7D)]):
        b.cap(12 + i, r, 1, 1, [sym], code, size=0.34 * b.U, pal="mod", sym=True)
    keypad(b, 15.5, R, s, [("clear", 0x47), ("=", 0x51), ("/", 0x4B), ("*", 0x43)])
    b.buttons = [("menu", (b.ux(15.55), b.uy(0.02), b.ux(17.45), b.uy(0.72))),
                 ("hide", (b.ux(17.55), b.uy(0.02), b.ux(19.45), b.uy(0.72)))]
    b.save("macplus", "keyboard", "the Macintosh Plus keyboard, M0110A", preview)


# --- the Macintosh II: the Apple Extended Keyboard (macii_core.h's ADBK_* codes) ----------
def macii(preview):
    b = Board(23.0, 6.6, ((206, 202, 192), (178, 174, 164)), PLATINUM, None, buttons_units=0)
    s = 0.16 * b.U
    fs = 0.15 * b.U
    # the function row
    F = 0
    b.cap(0, F, 1, 0.8, ["esc"], 0x35, size=fs, pal="mod")
    fkeys = [0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F, 0x69, 0x6B, 0x71]
    x = 2
    for i, code in enumerate(fkeys):
        if i in (4, 8):
            x += 0.5
        if i == 12:
            x = 15.5
        b.cap(x, F, 1, 0.8, ["F%d" % (i + 1)], code, size=fs, pal="mod")
        x += 1
    b.cap(22, F, 1, 0.8, ["◁"], 0x7F, size=0.3 * b.U, pal="mod", sym=True, name="power")
    R = 1.6
    b.well(0, R, 15, R + 5, (96, 94, 90))
    b.well(15.5, R, 18.5, R + 2, (96, 94, 90))
    b.well(15.5, R + 3, 18.5, R + 5, (96, 94, 90))
    b.well(19, R, 23, R + 5, (96, 94, 90))
    x = mac_main(b, R, s, (["~", "`"], 0x32), 0x36, True, None)
    b.cap(x, R + 3, 15 - x, 1, ["shift"], 0x38, MOD, size=s, pal="mod", align="lb", name="shift (right)")
    r = R + 4
    x = 0
    for leg, code, w, sym, name in [(["control"], 0x36, 1.5, False, ""), (["option"], 0x3A, 1.25, False, ""),
                                    (["⌘"], 0x37, 1.5, True, "command")]:
        b.cap(x, r, w, 1, leg, code, MOD, size=0.34 * b.U if sym else s, pal="mod", sym=sym, align="lb" if not sym else "mm",
              name=name)
        x += w
    b.cap(x, r, 6.5, 1, [""], 0x31, name="space")
    x += 6.5
    for leg, code, w, sym, name in [(["⌘"], 0x37, 1.5, True, "command (right)"), (["option"], 0x3A, 1.25, False, "option (right)"),
                                    (["control"], 0x36, 1.5, False, "control (right)")]:
        b.cap(x, r, w, 1, leg, code, MOD, size=0.34 * b.U if sym else s, pal="mod", sym=sym, align="lb" if not sym else "mm",
              name=name)
        x += w
    # the navigation keys and the arrows
    for i, (leg, code) in enumerate([("help", 0x72), ("home", 0x73), ("page up", 0x74)]):
        b.cap(15.5 + i, R, 1, 1, [leg], code, size=0.14 * b.U, pal="mod")
    for i, (leg, code) in enumerate([("del ⌦", 0x75), ("end", 0x77), ("page down", 0x79)]):
        b.cap(15.5 + i, R + 1, 1, 1, [leg], code, size=0.14 * b.U, pal="mod", sym=code == 0x75,
              name={0x75: "forward delete"}.get(code, ""))
    b.cap(16.5, R + 3, 1, 1, ["↑"], 0x3E, size=0.34 * b.U, pal="mod", sym=True)
    for i, (sym, code) in enumerate([("←", 0x3B), ("↓", 0x3D), ("→", 0x3C)]):
        b.cap(15.5 + i, R + 4, 1, 1, [sym], code, size=0.34 * b.U, pal="mod", sym=True)
    keypad(b, 19, R, s, [("clear", 0x47), ("=", 0x51), ("/", 0x4B), ("*", 0x43)])
    b.buttons = [("menu", (b.ux(15.55), b.uy(R + 2.05), b.ux(16.95), b.uy(R + 2.95))),
                 ("hide", (b.ux(17.05), b.uy(R + 2.05), b.ux(18.45), b.uy(R + 2.95)))]
    b.save("macii", "keyboard", "the Apple Extended Keyboard", preview)


def main():
    args = sys.argv[1:]
    preview = args[args.index("--preview") + 1] if "--preview" in args else None
    apple2_ii(preview)
    apple2_iie(preview)
    apple2gs(preview)
    macplus(preview)
    macii(preview)


if __name__ == "__main__":
    main()
