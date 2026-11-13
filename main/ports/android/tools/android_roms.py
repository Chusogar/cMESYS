#!/usr/bin/env python3
"""The ROMs of one machine of the Android port, from roms/<set>/ (unzipped or
the MAME .zip itself), into an APK's assets (never into git): the same
files and checks as tools/gen_roms_computers.py, gen_roms_zxspec.py and
gen_roms_ql.py, written as plain binaries that the app gives its core.

  android_roms.py <machine> <roms dir> <assets dir>

  ql      roms/js.rom (48 KB), jm.rom, minerva.rom, ipc.rom; cartridge.mdv
          (+ cartridge.txt) from the first *.mdv of roms/ql
  zxspec  roms/<model>/rom48.rom (+ banked.rom) for each model there
          (spectrum48, spectrum128, plus3, pentagon, pentagon1024, scorpion)
  cpc     roms/os.rom, roms/amsdos.rom (roms/cpc6128)
  c64     roms/kernal.rom, basic.rom, chargen.rom, dos1541.rom
  msx     roms/msx1/bios.rom, disk.rom; roms/msx2/bios.rom, sub.rom, disk.rom
  lynx    roms/boot.rom (roms/lynx)
"""
import glob
import io
import os
import sys
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "tools"))


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def computers(machine_set, romdir, out, names):
    import gen_roms_computers as g
    files = g.open_dir(romdir)
    found = []
    for (sym, size, cands, required), name in zip(g.SETS[machine_set], names):
        with redirect_stdout(io.StringIO()):
            src, data = g.find(files, cands, size)
        if data is None:
            if required:
                sys.exit("%s: %s missing in %s" % (machine_set, sym, romdir))
            continue
        write(os.path.join(out, name), data)
        found.append(name)
    return found


def ql(roms, out):
    d = os.path.join(roms, "ql")

    def read(name, size):
        p = os.path.join(d, name)
        if not os.path.exists(p):
            return None
        b = open(p, "rb").read()
        return b if len(b) == size else None
    js = [read("ql.js 0000.ic33", 0x8000), read("ql.js 8000.ic34", 0x4000)]
    ipc = read("ipc8049.ic24", 0x800)
    if None in js or ipc is None:
        sys.exit("roms/ql needs 'ql.js 0000.ic33', 'ql.js 8000.ic34' and 'ipc8049.ic24' (MAME set ql)")
    write(os.path.join(out, "roms/js.rom"), js[0] + js[1])
    write(os.path.join(out, "roms/ipc.rom"), ipc)
    jm = [read("ql.jm 0000.ic33", 0x8000), read("ql.jm 8000.ic34", 0x4000)]
    if None not in jm:
        write(os.path.join(out, "roms/jm.rom"), jm[0] + jm[1])
    mi = read("minerva.rom", 0xc000)
    if mi:
        write(os.path.join(out, "roms/minerva.rom"), mi)
    mdvs = sorted(glob.glob(os.path.join(d, "*.mdv")))
    if mdvs and os.path.getsize(mdvs[0]) == 255 * 686:
        write(os.path.join(out, "cartridge.mdv"), open(mdvs[0], "rb").read())
        write(os.path.join(out, "cartridge.txt"), os.path.splitext(os.path.basename(mdvs[0]))[0].encode())
    return sorted(os.listdir(os.path.join(out, "roms"))) + (["cartridge.mdv"] if mdvs else [])


def zxspec(roms, out):
    import gen_roms_zxspec as g
    found = []
    for model in g.MODELS:
        d = os.path.join(roms, model)
        if not os.path.isdir(d):
            continue
        try:
            with redirect_stdout(io.StringIO()):
                rom48, banked = g.load_model_roms(d, model)
        except SystemExit as e:
            print("  %s: left out (%s)" % (model, e))
            continue
        write(os.path.join(out, "roms", model, "rom48.rom"), rom48)
        if banked:
            write(os.path.join(out, "roms", model, "banked.rom"), banked)
        found.append(model)
    if not found:
        sys.exit("no ZX Spectrum ROMs in %s/<model>/" % roms)
    return found


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    machine, roms, out = sys.argv[1:]
    if machine == "ql":
        found = ql(roms, out)
    elif machine == "zxspec":
        found = zxspec(roms, out)
    elif machine == "cpc":
        found = computers("cpc6128", os.path.join(roms, "cpc6128"), out, ["roms/os.rom", "roms/amsdos.rom"])
    elif machine == "c64":
        found = computers("c64", os.path.join(roms, "c64"), out,
                          ["roms/kernal.rom", "roms/basic.rom", "roms/chargen.rom", "roms/dos1541.rom"])
    elif machine == "msx":
        found = []
        for model, names in (("msx1", ["bios.rom", "sub.rom", "disk.rom"]), ("msx2", ["bios.rom", "sub.rom", "disk.rom"])):
            d = os.path.join(roms, model)
            if os.path.isdir(d):
                found += [model + "/" + n for n in computers(model, d, os.path.join(out, "roms", model), names)]
    elif machine == "lynx":
        found = computers("lynx", os.path.join(roms, "lynx"), out, ["roms/boot.rom"])
    else:
        sys.exit("unknown machine %s" % machine)
    print("ROMs (%s): %s" % (machine, ", ".join(found)))


if __name__ == "__main__":
    main()
