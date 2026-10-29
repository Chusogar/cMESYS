#!/usr/bin/env python3
"""Pack the Pole Position / Pole Position II ROMs (MAME sets "polepos" and
"polepos2", with the Namco 51xx-54xx MCU ROMs) into rom_data.c (const arrays
-> flash). Run by tools/regen.sh polepos | polepos2.

  gen_roms_polepos.py polepos|polepos2 roms/<game> main/drivers/arcades/<game>/rom_data.c

Program ROMs as the CPUs see them (the Z8002 pairs interleaved even/odd),
graphics decoded to one byte a pixel (MAME's gfx layouts, as dsp-cpp's
polepos.cpp), the rest raw.
"""
import os, sys, zlib

PP = {
    "z80": [("pp3_9.6h", 0x2000, 0xc0511173), ("pp1_10b.5h", 0x1000, 0x7174bcb7)],
    "sub1": ([("pp3_2.8l", 0x2000, 0xfafb9049)], [("pp3_1.8m", 0x2000, 0x65c1c2c2)]),
    "sub2": ([("pp3_6.4l", 0x2000, 0xacc1ebc3)], [("pp3_5.4m", 0x2000, 0x46e5c99a)]),
    "chars": [("pp3_28.1f", 0x1000, 0x2e77187e)],
    "tiles": [("pp1_29.1e", 0x1000, 0x706e888a)],
    "sprites": [("pp3_25.1n", 0x2000, 0xb52c086b), ("pp3_26.1m", 0x2000, 0xd24a5707)],
    "big": [(0x0000, "pp1_17.5n", 0x2e134b46), (0x2000, "pp1_19.4n", 0x43ff83e1),
            (0x4000, "pp1_21.3n", 0x5f958eb4), (0x8000, "pp1_18.5m", 0x6f9997d2),
            (0xa000, "pp1_20.4m", 0xec18075b), (0xc000, "pp1_22.3m", 0x1d2f30b1)],
    "proms": [("pp1-7.8l", 0x100, 0xf07ff2ad), ("pp1-8.9l", 0x100, 0xadbde7d7), ("pp1-9.10l", 0x100, 0xddac786a),
              ("pp2-10.2h", 0x100, 0x1e8d0491), ("pp1-11.4d", 0x100, 0x0e4fe8a0), ("pp1-15.9a", 0x100, 0x2d502464),
              ("pp1-16.10a", 0x100, 0x027aa62c), ("pp1-17.11a", 0x100, 0x1f8d0df3), ("pp1-12.3c", 0x400, 0x7afc7cfc),
              ("pp3-6.6m", 0x400, 0x63fb6057)],
    "engine": [("pp1_15.6a", 0x2000, 0xb5ad4d5f), ("pp1_16.5a", 0x2000, 0x8fdd2f6f)],
    "voice": [("pp2_11.2e", 0x2000, 0x5b4cf05e), ("pp2_12.2f", 0x2000, 0x32b694c2), ("pp2_13.1e", 0x2000, 0x8842138a)],
}
PP2 = {
    "z80": [("pp4_9.6h", 0x2000, 0xbcf87004), ("pp4_10.5h", 0x1000, 0xa9d4c380)],
    "sub1": ([("pp4_2.8l", 0x2000, 0x51b9a669)], [("pp4_1.8m", 0x2000, 0x3f6ac294)]),
    # MAME loads pp4_7 / pp4_8 into sub2 at $4000 (sub1 $4000-$5fff is IC25, not ROM)
    "sub2": ([("pp4_6.4l", 0x2000, 0x38d04e0f), ("pp4_8.3l", 0x1000, 0xef25a2ee)],
             [("pp4_5.4m", 0x2000, 0xc3053cae), ("pp4_7.3m", 0x1000, 0xad1c8994)]),
    "chars": [("pp4_28.1f", 0x2000, 0x280dde7d)],
    "tiles": [("pp4_29.1e", 0x2000, 0xec3ec6e6)],
    "sprites": [("pp4_25.1n", 0x2000, 0xfd098e65), ("pp4_26.1m", 0x2000, 0x35ac62b3)],
    "big": [(0x0000, "pp1_17.5n", 0x2e134b46), (0x2000, "pp1_19.4n", 0x43ff83e1), (0x4000, "pp1_21.3n", 0x5f958eb4),
            (0x6000, "pp4_23.2n", 0x9e056fcd), (0x8000, "pp1_18.5m", 0x6f9997d2), (0xa000, "pp1_20.4m", 0xec18075b),
            (0xc000, "pp1_22.3m", 0x1d2f30b1), (0xe000, "pp4_24.2m", 0x795268cf)],
    "proms": [("pp4-7.8l", 0x100, 0x16d69c31), ("pp4-8.9l", 0x100, 0x07340311), ("pp4-9.10l", 0x100, 0x1efc84d7),
              ("pp4-10.2h", 0x100, 0x064d51a0), ("pp4-11.4d", 0x100, 0x7880c5af), ("pp1-15.9a", 0x100, 0x2d502464),
              ("pp1-16.10a", 0x100, 0x027aa62c), ("pp1-17.11a", 0x100, 0x1f8d0df3), ("pp4-12.3c", 0x400, 0x8b270902),
              ("pp4-6.6m", 0x400, 0x647212b5)],
    "engine": [("pp4_15.6a", 0x2000, 0x7d93bc1c), ("pp4_16.5a", 0x2000, 0x7d93bc1c)],
    "voice": [("pp1_11.2e", 0x2000, 0x45b9bfeb), ("pp1_13.1e", 0x2000, 0xa4237466), ("pp1_12.2f", 0x2000, 0xa31b4be5),
              ("pp1_14.1f", 0x2000, 0x944580f9)],
}
COMMON = {
    "road": [("pp1_30.3a", 0x2000, 0xee6b3315), ("pp1_31.2a", 0x2000, 0x6d1e7042), ("pp1_32.1a", 0x1000, 0x4e97f101)],
    "scale": [("pp1_27.1l", 0x1000, 0xa61bff15)],
    "wave": [("pp1-5.3b", 0x100, 0x8568decc)],
    "n51": [("51xx.bin", 0x400, 0xc2f57ef8)],
    "n52": [("52xx.bin", 0x400, 0x3257d11e)],
    "n53": [("53xx.bin", 0x400, 0xb326fecb)],
    "n54": [("54xx.bin", 0x400, 0xee7357e0)],
}


def load(d, name, size, crc):
    p = os.path.join(d, name)
    if not os.path.exists(p):
        sys.exit("missing ROM: " + p)
    data = open(p, "rb").read()
    if len(data) != size:
        sys.exit("%s: size %d, expected %d" % (name, len(data), size))
    if zlib.crc32(data) & 0xffffffff != crc:
        print("warning: %s has CRC %08x, expected %08x" % (name, zlib.crc32(data) & 0xffffffff, crc))
    return data


def cat(d, entries):
    return b"".join(load(d, *e) for e in entries)


def interleave(even, odd):
    out = bytearray(len(even) * 2)
    out[0::2] = even
    out[1::2] = odd
    return bytes(out) + bytes(0x8000 - len(out))


def bit(rom, b):
    i = b >> 3
    return (rom[i] >> (7 - (b & 7))) & 1 if i < len(rom) else 0


def decode(rom, total, w, h, planes, xoffs, yoffs, inc):
    out = bytearray()
    for n in range(total):
        base = n * inc
        for y in range(h):
            for x in range(w):
                v = 0
                for p in planes:          # first plane = most significant bit
                    v = (v << 1) | bit(rom, base + p + yoffs[y] + xoffs[x])
                out.append(v)
    return bytes(out)


def carray(out, decl, data):
    out.append("%s = {" % decl)
    for i in range(0, len(data), 32):
        out.append("    " + ",".join("%d" % b for b in data[i:i + 32]) + ",")
    out.append("};")


def main():
    game, d, outp = sys.argv[1], sys.argv[2], sys.argv[3]
    g = PP2 if game == "polepos2" else PP
    z80 = cat(d, g["z80"])
    sub1 = interleave(cat(d, g["sub1"][0]), cat(d, g["sub1"][1]))
    sub2 = interleave(cat(d, g["sub2"][0]), cat(d, g["sub2"][1]))
    chars_rom, tiles_rom = cat(d, g["chars"]), cat(d, g["tiles"])
    spr_rom = cat(d, g["sprites"])
    big_rom = bytearray(0x10000)
    for off, name, crc in g["big"]:
        big_rom[off:off + 0x2000] = load(d, name, 0x2000, crc)
    proms = cat(d, g["proms"])
    engine, voice = cat(d, g["engine"]), cat(d, g["voice"])
    road, scale, wave = cat(d, COMMON["road"]), cat(d, COMMON["scale"]), cat(d, COMMON["wave"])
    mcu = [cat(d, COMMON[k]) for k in ("n51", "n52", "n53", "n54")]

    # charlayout: 8x8x2, planes {0, 4}, x {0-3, 64-67}, 16 bytes a character
    cx = [0, 1, 2, 3, 64, 65, 66, 67]
    cy = [y * 8 for y in range(8)]
    chars = decode(chars_rom, len(chars_rom) // 16, 8, 8, [0, 4], cx, cy, 128)
    tiles = decode(tiles_rom, len(tiles_rom) // 16, 8, 8, [0, 4], cx, cy, 128)
    # small sprites: 16x16x4, planes {0, 4, 0x2000*8, 0x2000*8+4}
    sx = [0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27]
    sprites = decode(spr_rom, 128, 16, 16, [0, 4, 0x2000 * 8, 0x2000 * 8 + 4], sx, [y * 32 for y in range(16)], 16 * 32)
    # big sprites: 32x32x4, planes {0, 4, half*8, half*8+4}
    half = len(big_rom) // 2
    bx = [(i // 4) * 8 + (i % 4) for i in range(32)]
    bigs = decode(big_rom, half // 256, 32, 32, [0, 4, half * 8, half * 8 + 4], bx, [i * 64 for i in range(32)], 32 * 64)

    out = ["// Generated by tools/gen_roms_polepos.py (%s) - do not edit" % game, '#include "rom_data.h"', ""]
    carray(out, "static const uint8_t z80_rom[%d]" % len(z80), z80)
    carray(out, "static const uint8_t sub1_rom[0x8000]", sub1)
    carray(out, "static const uint8_t sub2_rom[0x8000]", sub2)
    carray(out, "static const uint8_t chars[%d]" % len(chars), chars)
    carray(out, "static const uint8_t tiles[%d]" % len(tiles), tiles)
    carray(out, "static const uint8_t sprites[%d]" % len(sprites), sprites)
    carray(out, "static const uint8_t bigsprites[%d]" % len(bigs), bigs)
    carray(out, "static const uint8_t road[%d]" % len(road), road)
    carray(out, "static const uint8_t scale[%d]" % len(scale), scale)
    carray(out, "static const uint8_t proms[%d]" % len(proms), proms)
    carray(out, "static const uint8_t wave[%d]" % len(wave), wave)
    carray(out, "static const uint8_t engine[%d]" % len(engine), engine)
    carray(out, "static const uint8_t voice[%d]" % len(voice), voice)
    for i, n in enumerate(("n51", "n52", "n53", "n54")):
        carray(out, "static const uint8_t %s[0x400]" % n, mcu[i])
    out.append("""
void pp_rom_data(pp_roms *r)
{
    r->pp2 = %d;
    r->z80 = z80_rom; r->z80_size = sizeof z80_rom;
    r->sub1 = sub1_rom; r->sub2 = sub2_rom;
    r->chars = chars; r->nchars = sizeof chars / 64;
    r->tiles = tiles; r->ntiles = sizeof tiles / 64;
    r->sprites = sprites; r->bigsprites = bigsprites; r->nbig = sizeof bigsprites / 1024;
    r->road = road; r->scale = scale; r->proms = proms; r->wave = wave;
    r->engine = engine; r->engine_size = sizeof engine;
    r->voice = voice; r->voice_size = sizeof voice;
    r->n51 = n51; r->n52 = n52; r->n53 = n53; r->n54 = n54;
}""" % (1 if game == "polepos2" else 0))
    open(outp, "w").write("\n".join(out) + "\n")
    print("%s: %d chars, %d tiles, %d big sprites" % (game, len(chars) // 64, len(tiles) // 64, len(bigs) // 1024))


main()
