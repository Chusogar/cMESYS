#!/usr/bin/env python3
"""Pack one ZX Spectrum / clone model's ROMs into a dedicated rom_data_*.c.

Usage:
  tools/gen_roms_zxspec.py <romdir> <out.c> <model>
  tools/gen_roms_zxspec.py --stub <out.c> <model>

Each model uses its own romdir (roms/<model>/) and its own out.c
(main/drivers/computers/zxspec/rom_data_<model>.c). No shared fallbacks.
<romdir> may hold the files unzipped or the MAME set's .zip itself.

MAME sets (the first choice; CRCs checked, a mismatch is only a warning):
  spectrum48    spectrum.zip  spectrum.rom                       -> 48K BASIC
  spectrum128   spec128.zip   128-0.rom + 128-1.rom              -> ROM 0 128K editor, ROM 1 48K BASIC
  plus3         plus3.zip     plus3-0.rom ... plus3-3.rom (v4.0) -> ROMs 0-3, ROM 3 = 48K BASIC
  pentagon      pentagon.zip  128p-0.rom + 128p-1.rom (+ trd503.rom) -> ROM 0 128K, ROM 1 48K BASIC,
                              ROM 3 TR-DOS 5.03 (paged by the core from 3D00, no disk drive)
  pentagon1024  pent1024.zip  128p-0.rom + 128p-1.rom + gluk63r.rom (Gluk 6.3r, MAME's
                              default BIOS) + trd503.rom (TR-DOS 5.03)  -> 4 x 16 KB
  scorpion      scorpio.zip   scorp294.rom (V2.94, MAME's default BIOS; or scorpion.rom
                              V2.92, or scorp0..3.rom)           -> ROM 0 128K, 1 48K, 2 service, 3 TR-DOS
Older names still accepted: 48.rom, 128.rom, pentagon.rom (32 KB), ...
"""
from __future__ import print_function
import io
import os
import sys
import zipfile
import zlib

MODELS = (
    "spectrum48",
    "spectrum128",
    "plus3",
    "pentagon",
    "pentagon1024",
    "scorpion",
)

ROM48_NAMES = ("spectrum.rom", "48.rom", "48k.rom", "zx48.rom", "48e.rom")


def read(path, size=None):
    with open(path, "rb") as f:
        b = f.read()
    if size is not None and len(b) < size:
        raise SystemExit("%s: got %d bytes, need %d" % (path, len(b), size))
    return b[:size] if size else b


# MAME CRC32s of the files the sets above load
CRCS = {
    "spectrum.rom": 0xddee531f, "48e.rom": 0xf051746e,
    "128-0.rom": 0xe76799d2, "128-1.rom": 0xb96a36be, "zx128_0.rom": 0xe76799d2, "zx128_1.rom": 0xb96a36be,
    "plus3-0.rom": 0x30c9f490, "plus3-1.rom": 0xa7916b3f, "plus3-2.rom": 0xc9a0b748, "plus3-3.rom": 0xb88fd6e3,
    "128p-0.rom": 0x124ad9e0, "128p-1.rom": 0xb96a36be, "pentagon.rom": 0xaa1ce4bd,
    "gluk63r.rom": 0xca321d79, "trd503.rom": 0x10751aba,
    "scorp294.rom": 0x99f57ce1, "scorpion.rom": 0xfef73c28,
    "scorp0.rom": 0x0eb40a09, "scorp1.rom": 0x9d513013, "scorp2.rom": 0xfd0d3ce1, "scorp3.rom": 0x1fe1d003,
}


def entries(dirpath):
    """name (lower case) -> bytes loader, from the directory and any .zip in it."""
    out = {}
    if not os.path.isdir(dirpath):
        return out
    for f in sorted(os.listdir(dirpath)):
        p = os.path.join(dirpath, f)
        if f.lower().endswith(".zip"):
            with zipfile.ZipFile(p) as z:
                for info in z.infolist():
                    name = os.path.basename(info.filename).lower()
                    if name and name not in out:
                        out[name] = (lambda z=p, n=info.filename: zipfile.ZipFile(z).read(n))
    for f in sorted(os.listdir(dirpath)):                  # loose files win over the zips
        p = os.path.join(dirpath, f)
        if os.path.isfile(p) and not f.lower().endswith(".zip"):
            out[f.lower()] = (lambda p=p: read(p))
    return out


_cache = {}


def find(dirpath, names, size):
    if dirpath not in _cache:
        _cache[dirpath] = entries(dirpath)
    ent = _cache[dirpath]
    for n in names:
        get = ent.get(n.lower())
        if get is None:
            continue
        b = get()
        if size is not None and len(b) < size:
            raise SystemExit("%s/%s: got %d bytes, need %d" % (dirpath, n, len(b), size))
        crc = CRCS.get(n.lower())
        if crc is not None and zlib.crc32(b) & 0xffffffff != crc:
            print("warning: %s: CRC %08x, MAME's is %08x" % (n, zlib.crc32(b) & 0xffffffff, crc), file=sys.stderr)
        return b[:size] if size else b
    return None


def find_concat(dirpath, name_pairs, part_size):
    for a_name, b_name in name_pairs:
        a = find(dirpath, [a_name], part_size)
        b = find(dirpath, [b_name], part_size)
        if a and b:
            return a + b
    return None


def find_parts(dirpath, names, part_size):
    parts = []
    for n in names:
        p = find(dirpath, [n], part_size)
        if not p:
            return None
        parts.append(p)
    return b"".join(parts)


def c_array(f, name, data, width=16):
    f.write("const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        f.write("  " + ", ".join("0x%02x" % b for b in chunk) + ",\n")
    f.write("};\n")


def write_rom_data(out, model, rom48, rom128):
    os.makedirs(os.path.dirname(os.path.abspath(out)) or ".", exist_ok=True)
    with open(out, "w") as f:
        f.write("// Generated by tools/gen_roms_zxspec.py for model %s — do not edit.\n" % model)
        f.write("// Source directory must be roms/%s/ (exclusive to this model).\n" % model)
        f.write('#include "rom_data.h"\n')
        f.write("#include <stddef.h>\n")
        if rom48 is None:
            f.write("// Empty stub: run tools/regen.sh %s after placing ROMs in roms/%s/\n" % (model, model))
            f.write("const uint8_t zx_rom48[0x4000];\n")
        else:
            c_array(f, "zx_rom48", rom48)
        if rom128:
            c_array(f, "zx_rom128_data", rom128)
            f.write("const uint8_t *const zx_rom128 = zx_rom128_data;\n")
            f.write("const size_t zx_rom128_bytes = %d;\n" % len(rom128))
        else:
            f.write("const uint8_t *const zx_rom128 = 0;\n")
            f.write("const size_t zx_rom128_bytes = 0;\n")
        f.write('const char zx_rom_name[] = "%s";\n' % model)


def load_model_roms(dirpath, model):
    """(rom48, banked ROMs or None). The core pages the banked ROMs in 16 KB
    steps (port 7FFD bit 4, and 1FFD on the +3 / Scorpion); rom48 is what
    a 48K view (and the 48K model) shows."""
    if not os.path.isdir(dirpath):
        raise SystemExit("ROM directory not found: %s (expected roms/%s/)" % (dirpath, model))
    K16 = 0x4000
    rom48 = find(dirpath, ROM48_NAMES, K16)

    if model == "spectrum48":                      # MAME spectrum.zip
        if rom48 is None:
            raise SystemExit("%s: need spectrum.rom (16 KB, MAME spectrum.zip) in %s" % (model, dirpath))
        return rom48, None

    if model == "spectrum128":                     # MAME spec128.zip
        rom128 = find_concat(dirpath, [("128-0.rom", "128-1.rom"), ("zx128_0.rom", "zx128_1.rom")], K16)
        if rom128 is None:
            rom128 = find(dirpath, ["128.rom", "zx128.rom"], 0x8000)
        if rom128 is None:
            raise SystemExit("%s: need 128-0.rom + 128-1.rom (MAME spec128.zip) or 128.rom in %s" % (model, dirpath))
        return rom48 or rom128[K16:2 * K16], rom128

    if model == "plus3":                           # MAME plus3.zip (v4.0 in four 16 KB parts)
        rom128 = find_parts(dirpath, ("plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom"), K16)
        if rom128 is None:
            rom128 = find(dirpath, ["plus3.rom", "plus2a.rom"], 0x10000)
        if rom128 is None:
            raise SystemExit("%s: need plus3-0..3.rom (MAME plus3.zip) or plus3.rom (64 KB) in %s" % (model, dirpath))
        return rom48 or rom128[3 * K16:4 * K16], rom128

    if model == "pentagon":                        # MAME pentagon.zip, BIOS v1
        rom128 = find_concat(dirpath, [("128p-0.rom", "128p-1.rom"), ("128-0.rom", "128-1.rom")], K16)
        if rom128 is None:
            rom128 = find(dirpath, ["pentagon.rom", "128.rom"], 0x8000)
        if rom128 is None:
            raise SystemExit("%s: need 128p-0.rom + 128p-1.rom (MAME pentagon.zip) in %s" % (model, dirpath))
        trdos = find(dirpath, ["trd503.rom", "trd504.rom"], K16)
        if trdos is not None:                      # TR-DOS (Beta Disk) as ROM 3, as on the Pentagon 1024
            rom128 = rom128 + b"\xff" * K16 + trdos
        return rom48 or rom128[K16:2 * K16], rom128

    if model == "pentagon1024":                    # MAME pent1024.zip
        base = find_concat(dirpath, [("128p-0.rom", "128p-1.rom")], K16)
        if base is not None:
            gluk = find(dirpath, ["gluk63r.rom", "gluk601r.rom", "gluk60r.rom"], K16) or b"\xff" * K16
            trdos = find(dirpath, ["trd503.rom", "trd504.rom"], K16) or b"\xff" * K16
            rom128 = base + gluk + trdos
        else:
            rom128 = find(dirpath, ["pentagon1024.rom", "glukrom.rom", "gluk.rom"], 0x10000)
            if rom128 is None:
                rom128 = find_parts(dirpath, ("pentagon-0.rom", "pentagon-1.rom", "pentagon-2.rom",
                                              "pentagon-3.rom"), K16)
        if rom128 is None:
            raise SystemExit("%s: need 128p-0.rom + 128p-1.rom (+ gluk63r.rom, trd503.rom; MAME pent1024.zip) in %s"
                             % (model, dirpath))
        return rom48 or rom128[K16:2 * K16], rom128

    if model == "scorpion":                        # MAME scorpio.zip
        rom128 = find(dirpath, ["scorp294.rom", "scorpion.rom", "scorpio.rom", "scorp.rom"], 0x10000)
        if rom128 is None:
            rom128 = find_parts(dirpath, ("scorp0.rom", "scorp1.rom", "scorp2.rom", "scorp3.rom"), K16)
        if rom128 is None:
            rom128 = find_parts(dirpath, ("scorpion-0.rom", "scorpion-1.rom", "scorpion-2.rom",
                                          "scorpion-3.rom"), K16)
        if rom128 is None:
            raise SystemExit("%s: need scorp294.rom (MAME scorpio.zip), scorpion.rom or scorp0..3.rom in %s"
                             % (model, dirpath))
        return rom48 or rom128[K16:2 * K16], rom128

    raise SystemExit("unknown model %r (want: %s)" % (model, ", ".join(MODELS)))


def write_stub(out, model):
    write_rom_data(out, model, None, None)
    print("wrote stub %s (%s)" % (out, model))


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(
            "usage: %s <romdir> <out.c> <model>\n"
            "       %s --stub <out.c> <model>\n"
            "       %s --stub-all <zxspec_dir>\n"
            "models: %s" % (sys.argv[0], sys.argv[0], sys.argv[0], ", ".join(MODELS))
        )

    if args[0] == "--stub-all":
        if len(args) != 2:
            sys.exit("usage: %s --stub-all <zxspec_dir>" % sys.argv[0])
        base = args[1]
        for model in MODELS:
            write_stub(os.path.join(base, "rom_data_%s.c" % model), model)
        return

    if args[0] == "--stub":
        if len(args) != 3:
            sys.exit("usage: %s --stub <out.c> <model>" % sys.argv[0])
        out, model = args[1], args[2]
        if model not in MODELS:
            sys.exit("unknown model %r (want: %s)" % (model, ", ".join(MODELS)))
        write_stub(out, model)
        return

    if len(args) != 3:
        sys.exit("usage: %s <romdir> <out.c> <model>" % sys.argv[0])
    romdir, out, model = args
    if model not in MODELS:
        sys.exit("unknown model %r (want: %s)" % (model, ", ".join(MODELS)))

    # Refuse accidental shared directories: each model owns roms/<model>/.
    base = os.path.basename(os.path.normpath(romdir))
    if base != model:
        print(
            "warning: romdir basename %r != model %r; expected roms/%s/" % (base, model, model),
            file=sys.stderr,
        )

    rom48, rom128 = load_model_roms(romdir, model)
    write_rom_data(out, model, rom48, rom128)
    extra = (" + %dK banked" % (len(rom128) // 1024)) if rom128 else ""
    print("wrote %s from %s (%s, 48K%s)" % (out, romdir, model, extra))


if __name__ == "__main__":
    main()
