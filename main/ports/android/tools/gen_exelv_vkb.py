#!/usr/bin/env python3
"""Draws the Exelvision EXL-100 / EXELTEL on-screen keyboard for the Android
port (gen_apple_vkb.py's Board, drawn from scratch, no logos): the EXL-100's
French AZERTY keyboard, its keys as MAME lists them (the digits with & é " '
( $ è ! ç à, LOCK, CTL, FCT, the ↖ key, ⌫ and the four arrows), black keys in
a black case with the red of the machine for the modifiers, and a top strip
with the app's MENÚ and ▼ buttons. Codes: the infrared keyboard's channels
(row * 8 + column, EK_* of exelv_core.h). SHIFT, CTL and FCT stay down for
the next key (the machine wants them pressed first, as they are sent).

  exelv/assets/keyboard.png + keyboard.txt

usage: gen_exelv_vkb.py [--preview <dir>]
"""
import os
import sys

from gen_apple_vkb import Board, pal, MOD, MACHINES

EXL = {"key": pal((44, 44, 48), (238, 238, 238)), "mod": pal((150, 30, 34), (250, 238, 238)),
       "grey": pal((96, 98, 104), (245, 245, 245))}


def ch(row, col):
    return row * 8 + col


def exelv(preview):
    T = 0.8                                     # the top strip
    b = Board(15, 5 + T, ((34, 34, 38), (16, 16, 18)), EXL, None, buttons_units=0)
    s = 0.17 * b.U
    # the strip: the name, MENÚ and ▼
    b.c.text(b.ux(0.1), b.uy(T / 2), "exelvision", 0.42 * b.U, (225, 50, 50, 255), anchor="lm")
    b.c.text(b.ux(3.0), b.uy(T / 2), "EXL 100", 0.28 * b.U, (200, 200, 205, 255), anchor="lm")
    g = 0.035 * b.U
    b.buttons = [("menu", (b.ux(10.3) + g, b.uy(0.05) + g, b.ux(12.6) - g, b.uy(T - 0.05) - g)),
                 ("hide", (b.ux(12.7) + g, b.uy(0.05) + g, b.ux(15) - g, b.uy(T - 0.05) - g))]
    b.well(0, T, 15, T + 5, (10, 10, 12))
    y = T
    # row 0: ESC, 1-0, "# )", "_ -", ⌫
    b.cap(0, y, 1.5, 1, ["ESC"], ch(1, 4), size=s, pal="grey")
    digits = [("&", "1", ch(2, 4)), ("é", "2", ch(3, 4)), ('"', "3", ch(3, 5)), ("'", "4", ch(6, 4)),
              ("(", "5", ch(7, 4)), ("$", "6", ch(2, 5)), ("è", "7", ch(6, 5)), ("!", "8", ch(2, 6)),
              ("ç", "9", ch(3, 6)), ("à", "0", ch(2, 7))]
    x = 1.5
    for top, low, code in digits:
        b.cap(x, y, 1, 1, [top, low], code)
        x += 1
    b.cap(x, y, 1, 1, [")", "#"], ch(3, 7))
    b.cap(x + 1, y, 1, 1, ["-", "_"], ch(0, 7))
    b.cap(x + 2, y, 1.5, 1, ["⌫"], ch(3, 1), size=0.36 * b.U, pal="grey", sym=True, name="BACKSPACE")
    # row 1: TAB, A Z E R T Y U I O P, "~ ^", "* \", ENTER (two rows)
    y = T + 1
    b.cap(0, y, 1.5, 1, ["TAB"], ch(2, 0), size=s, pal="grey")
    x = 1.5
    for c, code in zip("AZERTYUIOP", [ch(3, 0), ch(0, 0), ch(0, 5), ch(1, 5), ch(4, 4), ch(5, 4), ch(5, 1),
                                      ch(7, 1), ch(4, 1), ch(6, 1)]):
        b.cap(x, y, 1, 1, [c], code)
        x += 1
    b.cap(x, y, 1, 1, ["^", "~"], ch(6, 3))
    b.cap(x + 1, y, 1, 1, ["\\", "*"], ch(6, 6))
    b.cap(x + 2, y, 1.5, 2, ["ENTER"], ch(7, 3), size=s, pal="grey")
    # row 2: LOCK, Q S D F G H J K L M, "% @", "< ["
    y = T + 2
    b.cap(0, y, 1.5, 1, ["LOCK"], ch(1, 1), size=s, pal="grey")
    x = 1.5
    for c, code in zip("QSDFGHJKLM", [ch(7, 0), ch(5, 0), ch(5, 7), ch(6, 7), ch(4, 7), ch(4, 2), ch(6, 2),
                                      ch(5, 2), ch(7, 2), ch(4, 5)]):
        b.cap(x, y, 1, 1, [c], code)
        x += 1
    b.cap(x, y, 1, 1, ["@", "%"], ch(7, 5))
    b.cap(x + 1, y, 1, 1, ["[", "<"], ch(4, 3))
    # row 3: SHIFT, W X C V B N, "? ,", ". ;", "/ :", "+ =", "> ]", ↑, ↖
    y = T + 3
    b.cap(0, y, 2, 1, ["SHIFT"], ch(4, 0), MOD, size=s, pal="mod")
    x = 2
    for c, code in zip("WXCVBN", [ch(6, 0), ch(1, 2), ch(3, 2), ch(2, 2), ch(5, 6), ch(4, 6)]):
        b.cap(x, y, 1, 1, [c], code)
        x += 1
    for top, low, code in [(",", "?", ch(1, 6)), (";", ".", ch(3, 3)), (":", "/", ch(1, 3)),
                           ("=", "+", ch(2, 3)), ("]", ">", ch(5, 3))]:
        b.cap(x, y, 1, 1, [top, low], code)
        x += 1
    b.cap(13, y, 1, 1, ["↑"], ch(0, 1), size=0.36 * b.U, pal="grey", sym=True, name="UP")
    b.cap(14, y, 1, 1, ["↖"], ch(2, 1), size=0.36 * b.U, pal="grey", sym=True, name="HOME")
    # row 4: CTL, FCT, SPACE, ← ↓ →
    y = T + 4
    b.cap(0, y, 1.5, 1, ["CTL"], ch(1, 0), MOD, size=s, pal="mod")
    b.cap(1.5, y, 1.5, 1, ["FCT"], ch(1, 7), MOD, size=s, pal="mod")
    b.cap(3, y, 9, 1, [""], ch(0, 6), name="SPACE")
    for i, (sym, code, name) in enumerate([("←", ch(0, 4), "LEFT"), ("↓", ch(0, 3), "DOWN"), ("→", ch(0, 2), "RIGHT")]):
        b.cap(12 + i, y, 1, 1, [sym], code, size=0.36 * b.U, pal="grey", sym=True, name=name)
    b.save("exelv", "keyboard", "the Exelvision EXL-100", preview)
    txt = os.path.join(MACHINES, "exelv", "assets", "keyboard.txt")
    lines = open(txt, encoding="utf-8").read().split("\n")
    lines[0] = "# tools/gen_exelv_vkb.py (the Exelvision EXL-100), do not edit"
    open(txt, "w", encoding="utf-8").write("\n".join(lines))


def main():
    args = sys.argv[1:]
    exelv(args[args.index("--preview") + 1] if "--preview" in args else None)


if __name__ == "__main__":
    main()
