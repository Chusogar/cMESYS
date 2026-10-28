#!/usr/bin/env python3
"""Pack the RoboCop (Data East, 1988, MAME set "robocop", World revision 4)
ROMs into main/drivers/arcades/robocop/rom_data.c (const arrays -> ESP32 flash).
Run by tools/regen.sh robocop.

  ep05-4.11c, ep01-4.11b, ep04-3, ep00-3   68000 program (16-bit interleaved)
  ep03-3                                   6502 sound program (8000-FFFF)
  en_24_mb7124e.a2                         HuC6280 protection program (1E00-1FFF)
  ep02                                     OKI M6295 samples
  ep23, ep22                               8x8 characters (text layer)
  ep20, ep21, ep18, ep19                   16x16 tiles, playfield 1
  ep14 ... ep17                            16x16 tiles, playfield 2
  ep07 ... ep12                            16x16 sprites

Graphics are stored decoded with MAME's gfx layouts (dec0.cpp charlayout /
tilelayout) as rows of 4-bit pens: pixel x in bits 4x..4x+3 of a 32-bit word
(8x8: one word a row; 16x16: two words a row, left half first).

Pure Python 3 (no numpy).
"""
import os, sys, zlib

ROMS = {  # name: (size, crc32)
    "ep05-4.11c": (0x10000, 0x29c35379), "ep01-4.11b": (0x10000, 0x77507c69),
    "ep04-3": (0x10000, 0x39181778), "ep00-3": (0x10000, 0xe128541f),
    "ep03-3": (0x8000, 0x5b164b24), "en_24_mb7124e.a2": (0x200, 0xb8e2ca98),
    "ep02": (0x10000, 0x711ce46f),
    "ep23": (0x10000, 0xa77e4ab1), "ep22": (0x10000, 0x9fbd6903),
    "ep20": (0x10000, 0x1d8d38b8), "ep21": (0x10000, 0x187929b2),
    "ep18": (0x10000, 0xb6580b5e), "ep19": (0x10000, 0x9bad01c7),
    "ep14": (0x8000, 0xca56ceda), "ep15": (0x8000, 0xa945269c),
    "ep16": (0x8000, 0xe7fa4d58), "ep17": (0x8000, 0x84aae89d),
    "ep07": (0x10000, 0x495d75cf), "ep06": (0x8000, 0xa2ae32e2),
    "ep11": (0x10000, 0x62fa425a), "ep10": (0x8000, 0xcce3bd95),
    "ep09": (0x10000, 0x11bed656), "ep08": (0x8000, 0xc45c7b4c),
    "ep13": (0x10000, 0x8fca9f28), "ep12": (0x8000, 0x3cd1d0c3),
}


def load(d, name):
    p = os.path.join(d, name)
    if not os.path.exists(p):
        sys.exit("missing ROM: " + p)
    data = open(p, "rb").read()
    size, crc = ROMS[name]
    if len(data) != size:
        sys.exit("%s: size %d, expected %d" % (name, len(data), size))
    if zlib.crc32(data) & 0xffffffff != crc:
        print("warning: %s has CRC %08x, expected %08x" % (name, zlib.crc32(data) & 0xffffffff, crc))
    return data


def region(size, parts):
    r = bytearray(size)
    for off, data in parts:
        r[off:off + len(data)] = data
    return bytes(r)


def bit_at(rom, bitoff):
    """MSB-first bit, like MAME / numpy.unpackbits."""
    return (rom[bitoff >> 3] >> (7 - (bitoff & 7))) & 1


def decode(rom, w, h, planes, xoffs, yoffs, inc):
    """MAME gfx_layout decode (planes MSB first, RGN_FRAC(1,4) elements)
    -> list pens[tile][y][x]."""
    total = (len(rom) * 8 // 4) // inc
    pens = [[[0] * w for _ in range(h)] for _ in range(total)]
    for code in range(total):
        base = code * inc
        for y in range(h):
            for x in range(w):
                pen = 0
                off = yoffs[y] + xoffs[x]
                for p in planes:
                    pen = (pen << 1) | bit_at(rom, base + p + off)
                pens[code][y][x] = pen
    return pens


def words(pens):
    """[tile][y][x] pens -> 32-bit words, 8 pixels each (pixel x in bits 4x)."""
    out = []
    for tile in pens:
        for row in tile:
            for group in range(0, len(row), 8):
                v = 0
                for x in range(8):
                    v |= (row[group + x] & 15) << (4 * x)
                out.append(v)
    return out


def carray(f, ctype, name, data, per_line, fmt):
    f.write("const %s %s[%d] = {\n" % (ctype, name, len(data)))
    for i in range(0, len(data), per_line):
        f.write("    " + ",".join(fmt % v for v in data[i:i + per_line]) + ",\n")
    f.write("};\n\n")


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: %s <romdir> <out.c>" % sys.argv[0])
    d, out = sys.argv[1], sys.argv[2]
    prog = bytearray(0x40000)
    for name, off in (("ep05-4.11c", 0), ("ep01-4.11b", 1), ("ep04-3", 0x20000), ("ep00-3", 0x20001)):
        data = load(d, name)
        prog[off:off + 2 * len(data):2] = data
    snd = load(d, "ep03-3")
    mcu = load(d, "en_24_mb7124e.a2")
    oki = load(d, "ep02")
    chars = region(0x20000, [(0, load(d, "ep23")), (0x10000, load(d, "ep22"))])
    tiles1 = region(0x40000, [(0, load(d, "ep20")), (0x10000, load(d, "ep21")),
                              (0x20000, load(d, "ep18")), (0x30000, load(d, "ep19"))])
    tiles2 = region(0x20000, [(0, load(d, "ep14")), (0x8000, load(d, "ep15")),
                              (0x10000, load(d, "ep16")), (0x18000, load(d, "ep17"))])
    sprites = region(0x80000, [(0, load(d, "ep07")), (0x10000, load(d, "ep06")),
                               (0x20000, load(d, "ep11")), (0x30000, load(d, "ep10")),
                               (0x40000, load(d, "ep09")), (0x50000, load(d, "ep08")),
                               (0x60000, load(d, "ep13")), (0x70000, load(d, "ep12"))])

    # charlayout: planes {0, 2/4, 1/4, 3/4}, x 0-7, y step 8, 8 bytes a char
    F = len(chars) * 8 // 4
    ch = decode(chars, 8, 8, [0, 2 * F, F, 3 * F], list(range(8)), [y * 8 for y in range(8)], 64)
    # tilelayout: planes {1/4, 3/4, 0, 2/4}, x {128-135, 0-7}, y step 8, 32 bytes a tile
    xo = [128 + i for i in range(8)] + list(range(8))
    yo = [y * 8 for y in range(16)]

    def tiles(rom):
        Ff = len(rom) * 8 // 4
        return decode(rom, 16, 16, [Ff, 3 * Ff, 0, 2 * Ff], xo, yo, 256)

    chw = words(ch)
    t1w = words(tiles(tiles1))
    t2w = words(tiles(tiles2))
    spw = words(tiles(sprites))

    with open(out, "w") as f:
        f.write("// Generated by tools/gen_roms_robocop.py - RoboCop ROMs\n")
        f.write('#include "rom_data.h"\n\n')
        carray(f, "uint8_t", "rc_main_rom", prog, 32, "0x%02x")
        carray(f, "uint8_t", "rc_sound_rom", snd, 32, "0x%02x")
        carray(f, "uint8_t", "rc_mcu_rom", mcu, 32, "0x%02x")
        carray(f, "uint8_t", "rc_oki_rom", oki, 32, "0x%02x")
        carray(f, "uint32_t", "rc_chars", chw, 8, "0x%08x")
        carray(f, "uint32_t", "rc_tiles1", t1w, 8, "0x%08x")
        carray(f, "uint32_t", "rc_tiles2", t2w, 8, "0x%08x")
        carray(f, "uint32_t", "rc_sprites", spw, 8, "0x%08x")
    print("wrote", out)


if __name__ == "__main__":
    main()
