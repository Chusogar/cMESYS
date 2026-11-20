#!/usr/bin/env python3
"""Draws the on-screen controls of the consoles of the Android port (the
same way as gen_lynx_vkb.py and dsp-cpp's keyboards: vkb_common.py, drawn
from scratch, no logos or brand names): a generic pad body in the console's
colours with what the console's cons_machine has - the cross (or a
joystick), its buttons with their names, START / SELECT (PAUSE, RUN,
RESET...), the ColecoVision's and the SCV's keypads, the WonderSwan's Y
cross and the PV-2000's keyboard. The consoles have no keyboard, so this is
their keyboard: codes are m_cons.c's (Console.java): 100 up, 101 down, 102
left, 103 right, 104-107 buttons 1-4, 108 START, 109 SELECT, 111-114 the
diagonals (up-left, up-right, down-left, down-right), and cons.h's CK_*
(0..) for the keys. Writes machines/consoles/<id>/assets/keyboard.png +
keyboard.txt for each console.

usage: gen_consoles_vkb.py [--preview DIR] [id ...]
"""
import os
import sys

from vkb_common import Canvas, FONT, lerp

HERE = os.path.dirname(os.path.abspath(__file__))
from android_dirs import machine_dir

UP, DOWN, LEFT, RIGHT, B1, B2, B3, B4, START, SELECT = 100, 101, 102, 103, 104, 105, 106, 107, 108, 109
UL, UR, DL, DR = 111, 112, 113, 114
# cons.h's keys
CK = {chr(ord("A") + i): i for i in range(26)}
CK.update({str(d): 26 + d for d in range(10)})
CK.update(ENTER=36, SPACE=37, LSHIFT=38, RSHIFT=39, LCTRL=40, BACKSPACE=42, COMMA=47, PERIOD=48,
          SEMICOLON=49, QUOTE=50, SLASH=51, MINUS=52, TAB=54, CAPSLOCK=55)
F_MODIFIER = 1

# id: (body top, body bottom, button colour, style, extras)
#   style: "cross" (a pad's cross) or "stick" (a joystick: the same eight directions)
CONSOLES = {
    "sg1000":     dict(body=((74, 78, 92), (48, 50, 60)), btn=(176, 40, 40), dir="cross",
                       buttons="pair", names=("1", "2"), pills=[("PAUSE", START)]),
    "coleco":     dict(body=((60, 60, 64), (30, 30, 33)), btn=(196, 120, 40), dir="stick",
                       buttons="sides", names=("FIRE L", "FIRE R"), keypad="coleco"),
    "pv2000":     dict(body=((236, 236, 232), (200, 200, 196)), btn=(200, 50, 50), dir="cross",
                       buttons="pair", names=("ATTACK 0", "ATTACK 1"), keyboard=True),
    "pv1000":     dict(body=((86, 90, 130), (56, 58, 88)), btn=(210, 210, 214), dir="cross",
                       buttons="pair", names=("1", "2"), pills=[("START", START)]),
    "sms":        dict(body=((52, 52, 56), (28, 28, 31)), btn=(190, 36, 36), dir="cross",
                       buttons="pair", names=("1", "2"), pills=[("PAUSE", START)]),
    "gamegear":   dict(body=((64, 64, 68), (36, 36, 40)), btn=(60, 120, 200), dir="cross",
                       buttons="pair", names=("1", "2"), pills=[("START", START)]),
    "nes":        dict(body=((204, 204, 206), (166, 166, 170)), btn=(200, 36, 36), dir="cross",
                       buttons="pair", names=("A", "B"), pills=[("SELECT", SELECT), ("START", START)]),
    "a2600":      dict(body=((96, 68, 40), (58, 38, 20)), btn=(200, 40, 30), dir="stick",
                       buttons="one", names=("FIRE",),
                       pills=[("RESET", START), ("SELECT", SELECT)],
                       switches=[("LEFT DIFF.", B3), ("RIGHT DIFF.", B4)]),
    "a7800":      dict(body=((60, 60, 64), (30, 30, 33)), btn=(200, 120, 40), dir="stick",
                       buttons="one", names=("FIRE",), pills=[("RESET", START), ("SELECT", SELECT)]),
    "vectrex":    dict(body=((52, 52, 56), (26, 26, 29)), btn=(200, 60, 50), dir="stick",
                       buttons="row4", names=("1", "2", "3", "4")),
    "gameboy":    dict(body=((206, 206, 196), (176, 176, 166)), btn=(160, 40, 90), dir="cross",
                       buttons="pair", names=("A", "B"), pills=[("SELECT", SELECT), ("START", START)]),
    "pcengine":   dict(body=((236, 236, 232), (196, 196, 192)), btn=(60, 60, 64), dir="cross",
                       buttons="pair", names=("I", "II"), pills=[("SELECT", SELECT), ("RUN", START)]),
    "scv":        dict(body=((206, 70, 64), (150, 40, 36)), btn=(236, 236, 230), dir="cross",
                       buttons="pair", names=("1", "2"), pills=[("PAUSE", START)], keypad="scv"),
    "wonderswan": dict(body=((168, 168, 206), (124, 124, 166)), btn=(64, 64, 80), dir="cross",
                       buttons="pair", names=("A", "B"), pills=[("START", START)], ycross=True),
    "genesis":    dict(body=((48, 48, 52), (22, 22, 24)), btn=(70, 70, 76), dir="cross",
                       buttons="row3", names=("A", "B", "C"), pills=[("START", START)]),
    "sega32x":    dict(body=((48, 48, 52), (22, 22, 24)), btn=(70, 70, 76), dir="cross",
                       buttons="row3", names=("A", "B", "C"), pills=[("START", START)]),
    "snes":       dict(body=((206, 206, 212), (170, 170, 178)), btn=(120, 110, 190), dir="cross",
                       buttons="diamond", names=("B", "Y", "A", "X"), places=("bottom", "left", "right", "top"), pills=[("SELECT", SELECT), ("START", START)]),
    "gba":        dict(body=((110, 90, 190), (72, 56, 140)), btn=(52, 52, 60), dir="cross",
                       buttons="pair", names=("A", "B"), pills=[("SELECT", SELECT), ("START", START)],
                       shoulders=(("L", B3), ("R", B4))),
    "psx":        dict(body=((186, 186, 192), (150, 150, 158)), btn=(70, 70, 76), dir="cross",
                       buttons="diamond", names=("×", "○", "□", "△"), places=("bottom", "right", "left", "top"),
                       pills=[("SELECT", SELECT), ("START", START)]),
}
SYMBOLS = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def light(c):
    return c[0] * 0.3 + c[1] * 0.59 + c[2] * 0.11 > 140


class Pad:
    def __init__(self, cid, spec):
        self.cid, self.spec = cid, spec
        self.W = 1536
        self.H = 640 if spec.get("keyboard") else 520
        self.c = Canvas(self.W, self.H)
        self.keys = []
        top, bot = spec["body"]
        self.top, self.bot = top, bot
        self.ink = (34, 34, 38) if light(top) else (226, 226, 220)
        self.well = lerp(bot, (0, 0, 0), 0.30)
        self.menu = self.hide = None

    # --- the body ------------------------------------------------------------------
    def body(self):
        c, W, H = self.c, self.W, self.H
        c.shadow([(20, 16, W - 20, H - 6, 120)], 9, alpha=170)
        c.vgrad_rrect(14, 8, W - 14, H - 14, 118, self.top, self.bot)
        c.vgrad_rrect(140, 9, W - 140, 22, 6, lerp(self.top, (255, 255, 255), 0.25), self.top)

    def key(self, code, rect, name, flags=0):
        self.keys.append((code, flags, rect, name))

    # --- directions --------------------------------------------------------------------
    def cross(self, cx, cy, arm=104, length=150, codes=(UP, DOWN, LEFT, RIGHT), diag=(UL, UR, DL, DR),
              labels=("▲", "▼", "◀", "▶"), names=("UP", "DOWN", "LEFT", "RIGHT")):
        c = self.c
        r = length + 55
        c.draw.ellipse(c.box(cx - r, cy - r, cx + r, cy + r), fill=self.well + (255,))
        c.draw.ellipse(c.box(cx - r + 9, cy - r + 9, cx + r - 9, cy + r - 9),
                       fill=lerp(self.bot, (0, 0, 0), 0.12) + (255,))
        a, l = arm / 2, length
        c.shadow([(cx - a, cy - l, cx + a, cy + l, 14), (cx - l, cy - a, cx + l, cy + a, 14)], 6, alpha=230)
        for (x1, y1, x2, y2) in ((cx - a, cy - l, cx + a, cy + l), (cx - l, cy - a, cx + l, cy + a)):
            c.vgrad_rrect(x1, y1, x2, y2, 14, (58, 59, 64), (26, 27, 30))
        c.draw.ellipse(c.box(cx - 26, cy - 26, cx + 26, cy + 26), fill=(36, 37, 40, 255))
        for (dx, dy), s in zip(((0, -1), (0, 1), (-1, 0), (1, 0)), labels):
            sz = 34 if len(s) == 1 else 28
            c.text(cx + dx * (l - 46), cy + dy * (l - 46), s, sz, (160, 162, 168), "mm", font=SYMBOLS)
        self.key(codes[0], (cx - a, cy - l, cx + a, cy - a), names[0])
        self.key(codes[1], (cx - a, cy + a, cx + a, cy + l), names[1])
        self.key(codes[2], (cx - l, cy - a, cx - a, cy + a), names[2])
        self.key(codes[3], (cx + a, cy - a, cx + l, cy + a), names[3])
        if diag:
            self.key(diag[0], (cx - l, cy - l, cx - a, cy - a), "UP+LEFT")
            self.key(diag[1], (cx + a, cy - l, cx + l, cy - a), "UP+RIGHT")
            self.key(diag[2], (cx - l, cy + a, cx - a, cy + l), "DOWN+LEFT")
            self.key(diag[3], (cx + a, cy + a, cx + l, cy + l), "DOWN+RIGHT")

    def stick(self, cx, cy, length=150):
        """A joystick seen from above: a round base with the stick; the eight
        directions are the same areas as a cross's."""
        c = self.c
        r = length + 55
        c.draw.ellipse(c.box(cx - r, cy - r, cx + r, cy + r), fill=self.well + (255,))
        c.shadow([(cx - r + 14, cy - r + 18, cx + r - 14, cy + r - 6, r - 14)], 8, alpha=200)
        c.vgrad_rrect(cx - r + 14, cy - r + 14, cx + r - 14, cy + r - 14, r - 14, (70, 71, 76), (30, 31, 34))
        # the eight directions printed on the base
        for i, s in enumerate(("▲", "◥", "▶", "◢", "▼", "◣", "◀", "◤")):
            import math
            ang = math.radians(i * 45 - 90)
            rr = length - 12
            c.text(cx + math.cos(ang) * rr, cy + math.sin(ang) * rr, s, 26 if i % 2 == 0 else 20,
                   (150, 152, 158), "mm", font=SYMBOLS)
        # the stick and its knob
        c.shadow([(cx - 40, cy - 34, cx + 40, cy + 46, 40)], 7, alpha=230)
        c.vgrad_rrect(cx - 40, cy - 40, cx + 40, cy + 40, 40, (44, 45, 49), (14, 14, 16))
        c.vgrad_rrect(cx - 28, cy - 32, cx + 28, cy + 10, 28, (90, 92, 98), (40, 41, 45))
        a, l = 52, length
        self.key(UP, (cx - a, cy - l, cx + a, cy - a), "UP")
        self.key(DOWN, (cx - a, cy + a, cx + a, cy + l), "DOWN")
        self.key(LEFT, (cx - l, cy - a, cx - a, cy + a), "LEFT")
        self.key(RIGHT, (cx + a, cy - a, cx + l, cy + a), "RIGHT")
        self.key(UL, (cx - l, cy - l, cx - a, cy - a), "UP+LEFT")
        self.key(UR, (cx + a, cy - l, cx + l, cy - a), "UP+RIGHT")
        self.key(DL, (cx - l, cy + a, cx - a, cy + l), "DOWN+LEFT")
        self.key(DR, (cx + a, cy + a, cx + l, cy + l), "DOWN+RIGHT")

    # --- buttons -------------------------------------------------------------------------
    def round_button(self, cx, cy, r, label, code, colour=None):
        c = self.c
        col = colour or self.spec["btn"]
        hi = lerp(col, (255, 255, 255), 0.30)
        lo = lerp(col, (0, 0, 0), 0.45)
        c.draw.ellipse(c.box(cx - r - 14, cy - r - 14, cx + r + 14, cy + r + 14), fill=self.well + (255,))
        c.draw.ellipse(c.box(cx - r - 9, cy - r - 9, cx + r + 9, cy + r + 9),
                       fill=lerp(self.bot, (0, 0, 0), 0.10) + (255,))
        c.shadow([(cx - r, cy - r + 5, cx + r, cy + r + 6, r)], 5, alpha=230)
        c.vgrad_rrect(cx - r, cy - r, cx + r, cy + r, r, hi, lo)
        c.vgrad_rrect(cx - r + 8, cy - r + 6, cx + r - 8, cy + 4, r - 8, lerp(hi, (255, 255, 255), 0.2), col)
        cap_ink = (30, 30, 34) if light(col) else (246, 246, 240)
        sym = any(ord(ch) > 127 for ch in label)
        if len(label) <= 2 or sym:
            c.text(cx, cy + 2, label, r * (0.8 if sym else 0.7), cap_ink, "mm", font=SYMBOLS if sym else None)
        else:
            c.text(cx, cy + r + 34, label, 26, self.ink, "mm")
        self.key(code, (cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6), label)

    def pill(self, cx, cy, label, code, w=124, h=50):
        c = self.c
        c.rrect(cx - w / 2 - 7, cy - h / 2 - 7, cx + w / 2 + 7, cy + h / 2 + 8, h / 2 + 6, self.well + (255,))
        c.shadow([(cx - w / 2, cy - h / 2 + 3, cx + w / 2, cy + h / 2 + 4, h / 2)], 3, alpha=220)
        c.vgrad_rrect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, h / 2, (150, 152, 158), (104, 106, 112))
        c.text(cx, cy + h / 2 + 30, label, 22, self.ink, "mm")
        self.key(code, (cx - w / 2 - 4, cy - h / 2 - 4, cx + w / 2 + 4, cy + h / 2 + 4), label)

    def switch(self, cx, cy, label, code):
        """The 2600's slide switches: a dark slot with a lever, the name under it."""
        c = self.c
        c.rrect(cx - 58, cy - 34, cx + 58, cy + 34, 12, self.well + (255,))
        c.vgrad_rrect(cx - 50, cy - 26, cx + 50, cy + 26, 10, (38, 38, 40), (16, 16, 18))
        c.shadow([(cx - 30, cy - 22, cx + 6, cy + 24, 6)], 3, alpha=220)
        c.vgrad_rrect(cx - 30, cy - 22, cx + 6, cy + 22, 6, (210, 210, 206), (140, 140, 136))
        c.text(cx, cy + 58, label, 20, self.ink, "mm")
        self.key(code, (cx - 58, cy - 34, cx + 58, cy + 34), label)

    def shoulder(self, x1, label, code, w=220):
        c = self.c
        y1, y2 = 18, 74
        c.shadow([(x1, y1 + 3, x1 + w, y2 + 4, 20)], 4, alpha=210)
        c.vgrad_rrect(x1, y1, x1 + w, y2, 20, (96, 98, 104), (52, 53, 58))
        c.text(x1 + w / 2, (y1 + y2) / 2 + 1, label, 30, (236, 236, 230), "mm")
        self.key(code, (x1 - 4, y1 - 4, x1 + w + 4, y2 + 4), label)

    def cap(self, x1, y1, x2, y2, label, code, flags=0, size=26, colour=None):
        """A square key (keypad, keyboard)."""
        c = self.c
        col = colour or (226, 226, 220)
        c.rrect(x1 - 3, y1 - 3, x2 + 3, y2 + 5, 9, self.well + (255,))
        c.shadow([(x1, y1 + 3, x2, y2 + 3, 8)], 3, alpha=220)
        c.vgrad_rrect(x1, y1, x2, y2, 8, lerp(col, (255, 255, 255), 0.25), lerp(col, (0, 0, 0), 0.25))
        c.vgrad_rrect(x1 + 6, y1 + 4, x2 - 6, y2 - 10, 6, lerp(col, (255, 255, 255), 0.15), col)
        ink = (30, 30, 34) if light(col) else (246, 246, 240)
        c.text((x1 + x2) / 2, (y1 + y2) / 2 - 3, label, size, ink, "mm", max_w=x2 - x1 - 14)
        self.key(code, (x1, y1, x2, y2), label, flags)

    # --- the layouts ---------------------------------------------------------------------------
    def face_buttons(self, cx, cy):
        s = self.spec
        kind, names = s["buttons"], s["names"]
        if kind == "pair":       # button 1 right / up, button 2 left / down
            self.round_button(cx + 85, cy - 55, 70, names[0], B1)
            self.round_button(cx - 85, cy + 55, 70, names[1], B2)
        elif kind == "one":
            self.round_button(cx, cy, 86, names[0], B1)
        elif kind == "sides":    # the ColecoVision's two side buttons
            self.round_button(cx - 80, cy, 64, "L", B1)
            self.round_button(cx + 80, cy, 64, "R", B2)
            self.c.text(cx, cy + 112, "FIRE", 26, self.ink, "mm")
        elif kind == "row3":
            for i, (n, code) in enumerate(zip(names, (B1, B2, B3))):
                self.round_button(cx - 150 + i * 150, cy + 30 - i * 30, 62, n, code)
        elif kind == "row4":
            for i, (n, code) in enumerate(zip(names, (B1, B2, B3, B4))):
                self.round_button(cx - 210 + i * 124, cy, 50, n, code)
        elif kind == "diamond":  # buttons 1-4 where s["places"] says
            d = 105
            at = {"bottom": (0, d), "left": (-d, 0), "right": (d, 0), "top": (0, -d)}
            for n, code, place in zip(names, (B1, B2, B3, B4), s["places"]):
                dx, dy = at[place]
                self.round_button(cx + dx, cy + dy, 58, n, code)

    def keypad(self, kind, x0, y0, kw=92, kh=78, gap=10):
        if kind == "coleco":
            rows = [[("1", CK["1"]), ("2", CK["2"]), ("3", CK["3"])],
                    [("4", CK["4"]), ("5", CK["5"]), ("6", CK["6"])],
                    [("7", CK["7"]), ("8", CK["8"]), ("9", CK["9"])],
                    [("*", CK["Q"]), ("0", CK["0"]), ("#", CK["W"])]]
        else:   # SCV
            rows = [[("1", CK["1"]), ("2", CK["2"]), ("3", CK["3"])],
                    [("4", CK["4"]), ("5", CK["5"]), ("6", CK["6"])],
                    [("7", CK["7"]), ("8", CK["8"]), ("9", CK["9"])],
                    [("CL", CK["Q"]), ("0", CK["0"]), ("EN", CK["W"])]]
        c = self.c
        w = 3 * kw + 2 * gap
        h = 4 * kh + 3 * gap
        c.rrect(x0 - 18, y0 - 18, x0 + w + 18, y0 + h + 18, 18, self.well + (255,))
        c.rrect(x0 - 12, y0 - 12, x0 + w + 12, y0 + h + 12, 14, lerp(self.bot, (0, 0, 0), 0.2) + (255,))
        for r, row in enumerate(rows):
            for col, (lab, code) in enumerate(row):
                x = x0 + col * (kw + gap)
                y = y0 + r * (kh + gap)
                self.cap(x, y, x + kw, y + kh, lab, code, size=34)

    def keyboard(self, x0, y0, u=76, gap=6):
        """The PV-2000's keys that the core reads (pv2000_core.c's matrix)."""
        rows = [
            [(k, CK[k], 1) for k in "1234567890"] + [("-", CK["MINUS"], 1)],
            [(k, CK[k], 1) for k in "QWERTYUIOP"] + [("DEL", CK["BACKSPACE"], 1)],
            [(k, CK[k], 1) for k in "ASDFGHJKL"] + [(";", CK["SEMICOLON"], 1), (":", CK["QUOTE"], 1)],
            [("SHIFT", CK["LSHIFT"], 1)] + [(k, CK[k], 1) for k in "ZXCVBNM"]
            + [(",", CK["COMMA"], 1), (".", CK["PERIOD"], 1), ("/", CK["SLASH"], 1)],
            [("KANA", CK["CAPSLOCK"], 1.5), ("HOME", CK["TAB"], 1.5), ("SPACE", CK["SPACE"], 5),
             ("RETURN", CK["ENTER"], 3)],
        ]
        c = self.c
        width = 11 * u
        c.rrect(x0 - 16, y0 - 16, x0 + width + 16, y0 + 5 * u + 16, 16, self.well + (255,))
        c.rrect(x0 - 10, y0 - 10, x0 + width + 10, y0 + 5 * u + 10, 12, (40, 40, 44, 255))
        for r, row in enumerate(rows):
            x = x0
            for lab, code, wu in row:
                w = wu * u
                special = lab in ("SHIFT", "KANA", "HOME", "RETURN", "DEL")
                self.cap(x + gap / 2, y0 + r * u + gap / 2, x + w - gap / 2, y0 + (r + 1) * u - gap / 2, lab, code,
                         flags=F_MODIFIER if lab == "SHIFT" else 0, size=22 if len(lab) > 1 else 30,
                         colour=(110, 112, 118) if special else (226, 226, 220))
                x += w

    def draw(self):
        s, W, H = self.spec, self.W, self.H
        self.body()
        if s.get("keyboard"):
            # PV-2000: cross on the left, the keyboard, the two buttons on the right
            self.cross(190, 400, arm=84, length=122)
            self.keyboard(380, 186)
            self.round_button(W - 255, 470, 58, "0", B1)
            self.round_button(W - 120, 380, 58, "1", B2)
            self.c.text(W - 188, 560, "ATTACK", 24, self.ink, "mm")
            self.menu = (480, 40, 740, 120)
            self.hide = (770, 40, 910, 120)
        else:
            cy = 280
            if s["dir"] == "stick":
                self.stick(300, cy)
            else:
                self.cross(300, cy)
            self.face_buttons(W - 300, cy + (20 if s["buttons"] == "diamond" else 0))
            mid = W / 2
            if s.get("keypad"):
                self.keypad(s["keypad"], mid - 151, 130)
                pills = s.get("pills", [])
                for i, (lab, code) in enumerate(pills):
                    self.pill(W - 150, 440, lab, code)
                self.menu = (mid - 210, 34, mid + 20, 104)
                self.hide = (mid + 40, 34, mid + 170, 104)
            else:
                if s.get("ycross"):
                    self.cross(660, 300, arm=66, length=90, codes=(CK["W"], CK["S"], CK["A"], CK["D"]),
                               diag=None, labels=("Y1", "Y3", "Y4", "Y2"), names=("Y1", "Y3", "Y4", "Y2"))
                pills = s.get("pills", [])
                n = len(pills)
                for i, (lab, code) in enumerate(pills):
                    self.pill(mid + (i - (n - 1) / 2) * 170 + (110 if s.get("ycross") else 0), 380, lab, code)
                for i, (lab, code) in enumerate(s.get("switches", [])):
                    self.switch(mid - 105 + i * 210, 200, lab, code)
                for lab, code in s.get("shoulders", ()):
                    self.shoulder(470 if lab == "L" else W - 690, lab, code)
                if s.get("switches"):
                    self.menu = (mid - 210, 34, mid + 20, 104)
                    self.hide = (mid + 40, 34, mid + 170, 104)
                elif s.get("ycross"):
                    self.menu = (mid + 60, 100, mid + 270, 180)
                    self.hide = (mid + 290, 100, mid + 410, 180)
                else:
                    self.menu = (mid - 200, 110, mid + 60, 190)
                    self.hide = (mid + 90, 110, mid + 230, 190)

    def write(self, preview_dir=None):
        img = self.c.final()
        out = os.path.join(machine_dir(self.cid), "assets")
        os.makedirs(out, exist_ok=True)
        img.save(os.path.join(out, "keyboard.png"), optimize=True)
        with open(os.path.join(out, "keyboard.txt"), "w") as f:
            f.write("# tools/gen_consoles_vkb.py, do not edit\n")
            f.write("picture keyboard.png %d %d\n" % (self.W, self.H))
            f.write("travel 4\n")
            f.write("button menu %d %d %d %d\n" % tuple(round(v) for v in self.menu))
            f.write("button hide %d %d %d %d\n" % tuple(round(v) for v in self.hide))
            for code, flags, (x1, y1, x2, y2), name in self.keys:
                f.write("key %d %d %d %d %d %d  # %s\n" % (code, flags, round(x1), round(y1), round(x2), round(y2), name))
        if preview_dir:
            from PIL import Image, ImageDraw
            bg = Image.new("RGBA", img.size, (20, 22, 28, 255))
            bg.alpha_composite(img)
            d = ImageDraw.Draw(bg)
            for box, txt in ((self.menu, "MENÚ"), (self.hide, "▼")):
                d.rounded_rectangle(box, radius=10, fill=(52, 52, 58, 170), outline=(120, 120, 128, 200), width=2)
            os.makedirs(preview_dir, exist_ok=True)
            bg.convert("RGB").save(os.path.join(preview_dir, self.cid + ".png"))
        return len(self.keys)


def main():
    args = sys.argv[1:]
    preview = None
    if "--preview" in args:
        i = args.index("--preview")
        preview = args[i + 1]
        del args[i:i + 2]
    for cid in args or list(CONSOLES):
        p = Pad(cid, CONSOLES[cid])
        p.draw()
        print("%s: %d controls" % (cid, p.write(preview)))


if __name__ == "__main__":
    main()
