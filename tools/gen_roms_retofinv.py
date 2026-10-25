#!/usr/bin/env python3
"""Pack the Return of the Invaders (Taito, 1985, MAME set "retofinv") ROMs into
main/drivers/arcades/retofinv/rom_data.c (const arrays -> ESP32 flash).
Run by tools/regen.sh retofinv.

  a37__03.ic70, a37__02.ic71, a37__01.ic72   main Z80 (0000-5FFF)
  a37__04.ic62                               sub Z80 (0000-1FFF)
  a37__05.ic17                               sound Z80 (0000-1FFF)
  a37__09.ic37                               MC68705P5 (0000-07FF)
  a37__16.gfxboard.ic61                      fg chars, 1bpp (first half used)
  a37__10 ... a37__13.gfxboard               sprites, 4bpp
  a37__14, a37__15.gfxboard                  bg tiles, 4bpp
  a37-06.ic13, a37-07.ic4, a37-08.ic3        palette R / G / B (4 bits)
  a37-17 ... a37-20.gfxboard                 colour lookup (nibbles)

Tiles and sprites are stored decoded as rows of 4-bit pixels (pixel x in bits
4x..4x+3 of a 32-bit word; sprites: two words per row). The colour lookup
table is stored with MAME's address descramble already applied.
"""
import os, sys, zlib

ROMS = {  # name: (size, crc32)
    "a37__03.ic70": (0x2000, 0xeae7459d), "a37__02.ic71": (0x2000, 0x72895e37),
    "a37__01.ic72": (0x2000, 0x505dd20b), "a37__04.ic62": (0x2000, 0xd2899cc1),
    "a37__05.ic17": (0x2000, 0x9025abea), "a37__09.ic37": (0x800, 0x6a6d008d),
    "a37__16.gfxboard.ic61": (0x2000, 0x4e3f501c),
    "a37__10.gfxboard.ic8": (0x2000, 0x6afdeec8), "a37__11.gfxboard.ic9": (0x2000, 0xd3dc9da3),
    "a37__12.gfxboard.ic10": (0x2000, 0xd10b2eed), "a37__13.gfxboard.ic11": (0x2000, 0x00ca6b3d),
    "a37__14.gfxboard.ic55": (0x2000, 0xef7f8651), "a37__15.gfxboard.ic56": (0x2000, 0x03b40905),
    "a37-06.ic13": (0x100, 0xe9643b8b), "a37-07.ic4": (0x100, 0xe8f34e11), "a37-08.ic3": (0x100, 0x50030af0),
    "a37-17.gfxboard.ic36": (0x400, 0xc63cf10e), "a37-18.gfxboard.ic37": (0x400, 0x6db07bd1),
    "a37-19.gfxboard.ic83": (0x400, 0xa92aea27), "a37-20.gfxboard.ic84": (0x400, 0x77a7aaf6),
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


def bit(rom, b):
    return (rom[b >> 3] >> (7 - (b & 7))) & 1


def decode(rom, count, w, h, planes, xoffs, yoffs, inc):
    """MAME gfx_layout decode: list of tiles, each a list of h rows of w pens."""
    out = []
    for n in range(count):
        base = n * inc
        tile = []
        for y in range(h):
            row = []
            for x in range(w):
                v = 0
                for p in planes:
                    v = (v << 1) | bit(rom, base + p + yoffs[y] + xoffs[x])
                row.append(v)
            tile.append(row)
        out.append(tile)
    return out


def words(rows):
    """rows of 8 pens -> 32-bit words (pixel x in bits 4x..4x+3)."""
    res = []
    for r in rows:
        v = 0
        for x, p in enumerate(r):
            v |= p << (4 * x)
        res.append(v)
    return res


def carray(f, ctype, name, data, per_line, fmt):
    f.write("const %s %s[%d] = {\n" % (ctype, name, len(data)))
    for i in range(0, len(data), per_line):
        f.write("    " + ",".join(fmt % v for v in data[i:i + per_line]) + ",\n")
    f.write("};\n\n")


def main():
    d, out = sys.argv[1], sys.argv[2]
    main_rom = load(d, "a37__03.ic70") + load(d, "a37__02.ic71") + load(d, "a37__01.ic72")
    sub_rom = load(d, "a37__04.ic62")
    snd_rom = load(d, "a37__05.ic17")
    mcu_rom = load(d, "a37__09.ic37")
    fg = load(d, "a37__16.gfxboard.ic61")[:0x1000]          # bottom half of the ROM is empty
    spr = b"".join(load(d, n) for n in ("a37__10.gfxboard.ic8", "a37__11.gfxboard.ic9",
                                         "a37__12.gfxboard.ic10", "a37__13.gfxboard.ic11"))
    bg = load(d, "a37__14.gfxboard.ic55") + load(d, "a37__15.gfxboard.ic56")
    prom = load(d, "a37-06.ic13") + load(d, "a37-07.ic4") + load(d, "a37-08.ic3")
    c17, c18 = load(d, "a37-17.gfxboard.ic36"), load(d, "a37-18.gfxboard.ic37")
    c19, c20 = load(d, "a37-19.gfxboard.ic83"), load(d, "a37-20.gfxboard.ic84")
    clut = bytes(((c17[i] & 15) << 4) | (c18[i] & 15) for i in range(0x400)) + \
           bytes(((c19[i] & 15) << 4) | (c20[i] & 15) for i in range(0x400))

    # bglayout: 8x8x4, planes {0, H+4, H, 4}, x {0-3, 64-67}, 16 bytes per tile
    H = 0x2000 * 8
    bgt = decode(bg, 512, 8, 8, [0, H + 4, H, 4], [0, 1, 2, 3, 64, 65, 66, 67],
                 [y * 8 for y in range(8)], 128)
    # spritelayout: 16x16x4, planes {0, H+4, H, 4}, 64 bytes per sprite
    H = 0x4000 * 8
    sxo = [0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195]
    syo = [y * 8 for y in range(8)] + [256 + y * 8 for y in range(8)]
    spt = decode(spr, 256, 16, 16, [0, H + 4, H, 4], sxo, syo, 512)

    bg_words = []
    for t in bgt:
        bg_words += words(t)
    spr_words = []
    for t in spt:
        for r in t:
            spr_words += words([r[:8], r[8:]])
    # colour lookup with MAME's descramble: pen i -> clut[bitswap(i, ..., 0, 1, 2)]
    lut = bytes(clut[(i & ~7) | ((i & 1) << 2) | (i & 2) | ((i >> 2) & 1)] for i in range(0x800))

    with open(out, "w") as f:
        f.write("// Generated by tools/gen_roms_retofinv.py - Return of the Invaders ROMs\n")
        f.write('#include "rom_data.h"\n\n')
        carray(f, "uint8_t", "rv_main_rom", main_rom, 32, "0x%02x")
        carray(f, "uint8_t", "rv_sub_rom", sub_rom, 32, "0x%02x")
        carray(f, "uint8_t", "rv_sound_rom", snd_rom, 32, "0x%02x")
        carray(f, "uint8_t", "rv_mcu_rom", mcu_rom, 32, "0x%02x")
        carray(f, "uint8_t", "rv_fg_rom", fg, 32, "0x%02x")
        carray(f, "uint32_t", "rv_bg_gfx", bg_words, 8, "0x%08x")
        carray(f, "uint32_t", "rv_spr_gfx", spr_words, 8, "0x%08x")
        carray(f, "uint8_t", "rv_palette_prom", prom, 32, "0x%02x")
        carray(f, "uint8_t", "rv_clut", lut, 32, "0x%02x")
    print("wrote", out)


if __name__ == "__main__":
    main()
