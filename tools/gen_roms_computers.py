#!/usr/bin/env python3
"""Pack the ROMs of the computers migrated after the Spectrum into rom_data.c.

Usage:
  tools/gen_roms_computers.py <machine> <romdir> <out.c>
  tools/gen_roms_computers.py --stub <machine> <out.c>
  tools/gen_roms_computers.py --loaders [<repo root>]   (writes every rom_load.c)

<machine>: cpc6128 | c64 | lynx | msx1 | msx2 | amiga | atarist | apple2 | apple2p | apple2e | apple2ee |
apple2gs | macplus | macii | specnext | samcoupe | exl100 | exeltel, or a console of
main/drivers/consoles with a BIOS (coleco, ...). <romdir> is roms/<machine>/, unzipped or the
MAME set's .zip itself. Names (first found wins; a CRC mismatch is a warning):
  cpc6128  cpc6128.rom (32 KB: OS + BASIC), amsdos.rom | cpcados.rom (16 KB)
  c64      901227-03.u4 | kernal.rom (kernal 8 KB), 901226-01.u3 | basic.rom (8 KB),
           901225-01.u5 | chargen.rom (4 KB), and the 1541's DOS: 325302-01.uab4
           (C000-DFFF) + 901229-05 ae.uab5 | 901229-06 aa.uab5 | 901229-03.uab5
           (E000-FFFF), or one 16 KB dos1541 | 1541.rom (optional: no drive without it)
  lynx     lynxboot.img | lynx.bin | lynxa.bin (512 bytes)
  msx1     MSX.ROM | msx1.rom | mpc100bios.rom (32 KB); DISK.ROM (16 KB, optional)
  amiga    315093-02.u2 (Kickstart 1.3) | kick13.rom | 315093-01.u2 (1.2) | kick.rom | kickstart.rom
           (256 KB; the MAME a500 set's .zip as it is)
  atarist  tos104.bin | tos102.bin | tos100.bin | tos.img | etos192uk.img | etos192us.img ...
           (192 KB: TOS 1.04 / 1.02 / 1.00 or EmuTOS 192K; the MAME st set's .zip as it is)
  msx2     MSX2.ROM | nms8250_basic-bios2.rom (32 KB), MSX2EXT.ROM | nms8250_msx2sub.rom
           (16 KB), DISK.ROM | nms8250_disk.rom (16 KB, optional)
  apple2   (MAME apple2) 341-0001-00.e0 + 341-0002-00.e8 + 341-0003-00.f0 (Integer BASIC) +
           341-0020-00.f8 (Autostart) | 341-0004-00.f8 (8 KB, $E000), a2.chr (2 KB),
           341-0016-00.d0 (Programmer's Aid, optional), 341-0027-a.p5 (Disk II, optional)
  apple2p  (MAME apple2p) 341-0011.d0 .. 341-0015.f0 + 341-0020-00.f8 (12 KB, $D000),
           341-0036.chr (2 KB), 341-0027-a.p5
  apple2e  (MAME apple2e) 342-0135-b.64 + 342-0134-a.64 (16 KB, $C000), 342-0133-a.chr (4 KB), 341-0027-a.p5
  apple2ee (MAME apple2ee) 342-0304-a.e10 + 342-0303-a.e8 (16 KB), 342-0265-a.chr (4 KB), 341-0027-a.p5
  apple2gs (MAME apple2gs, ROM 03) 341-0728 + 341-0748 (256 KB), apple2gs.chr (4 KB)
  macplus  (MAME macplus) 342-0341-c.u6d + 342-0342-b.u8d (v3; or -b + -a, -a + -a: 64 KB each, even / odd)
  macii    (MAME macii) 9779d2c4.rom | 97851db6.rom (256 KB), 3410868.bin (Display Card 8*24, 32 KB)
  specnext (MAME tbblue) boot-30204.bin (the TBBlue boot ROM, 8 KB)
  samcoupe (MAME samcoupe) rom30.z5 (ROM 3.0) | rom31.z5 | rom25.z5 ... | atom.z5 (32 KB)
  exl100   (MAME exl100) exl100in.bin (TMS7020, 2 KB) + exl100_7041.bin (the I/O CPU, 4 KB)
  exeltel  (MAME exeltel) exeltel_7040.bin (4 KB) + exeltel_7042.bin (4 KB) + exeltel14.bin (64 KB)
"""
from __future__ import print_function
import os
import sys
import zipfile
import zlib

SETS = {
    'cpc6128': [
        ('cpc_rom_os', 0x8000, [('cpc6128.rom', 0x9e827fe1)], True),
        ('cpc_rom_amsdos', 0x4000, [('amsdos.rom', 0x1fe22ecd), ('cpcados.rom', 0x1fe22ecd)], False),
    ],
    'c64': [
        ('c64_rom_kernal', 0x2000, [('901227-03.u4', 0xdbe3e7c7), ('kernal.rom', None), ('kernal.bin', None)], True),
        ('c64_rom_basic', 0x2000, [('901226-01.u3', 0xf833d117), ('basic.rom', None), ('basic.bin', None)], True),
        ('c64_rom_chargen', 0x1000, [('901225-01.u5', 0xec4272ee), ('chargen.rom', None), ('chargen.bin', None)], True),
        ('c64_rom_dos1541', 0x4000, [(('325302-01.uab4', '901229-05 ae.uab5'), None),
                                     (('325302-01.uab4', '901229-06 aa.uab5'), None),
                                     (('325302-01.uab4', '901229-03.uab5'), None),
                                     ('dos1541', None), ('1541.rom', None), ('dos1541.bin', None)], False),
    ],
    'msx1': [
        ('msx_rom_bios', 0x8000, [('MSX.ROM', None), ('msx1.rom', None), ('mpc100bios.rom', 0xa317e6b4),
                                  ('mpc100bios.ic122', None)], True),
        ('msx_rom_sub', 0x4000, [], False),
        ('msx_rom_disk', 0x4000, [('DISK.ROM', None), ('DISKROM.ROM', None), ('nms8250_disk.rom', None)], False),
    ],
    'msx2': [
        ('msx_rom_bios', 0x8000, [('MSX2.ROM', 0x6cdaf3a5), ('nms8250_basic-bios2.rom', None), ('msx2.rom', None),
                                  ('msx2_bios.rom', None)], True),
        ('msx_rom_sub', 0x4000, [('MSX2EXT.ROM', 0x66237ecf), ('nms8250_msx2sub.rom', None), ('msx2ext.rom', None),
                                 ('msx2_ext.rom', None)], True),
        ('msx_rom_disk', 0x4000, [('DISK.ROM', None), ('DISKROM.ROM', None), ('nms8250_disk.rom', None),
                                  ('v1.08.ic117', None), ('jq00014.ic117', None)], False),
    ],
    'amiga': [
        ('amiga_rom_kick', 0x40000, [('315093-02.u2', 0xc4f0f55f), ('kick13.rom', None), ('kick34005.a500', None),
                                     ('315093-01.u2', 0xa6ce1636), ('kick.rom', None), ('kickstart.rom', None)], True),
    ],
    'atarist': [
        ('st_rom_tos', 0x30000, [('tos104.bin', 0x90f4fbff), ('tos102.bin', 0xd3c32283), ('tos100.bin', 0xd331af30),
                                 ('tos104.img', None), ('tos102.img', None), ('tos100.img', None), ('tos.img', None),
                                 ('etos192uk.img', None), ('etos192us.img', None), ('etos192de.img', None),
                                 ('etos192fr.img', None), ('etos192es.img', None)], True),
    ],
    # Apple II family (drivers/computers/apple2): one rom_data_<model>.c each
    'apple2': [
        ('a2_rom_main', 0x2000, [(('341-0001-00.e0', '341-0002-00.e8', '341-0003-00.f0', '341-0020-00.f8'), None),
                                 (('341-0001-00.e0', '341-0002-00.e8', '341-0003-00.f0', '341-0004-00.f8'), None),
                                 ('apple2-int-auto.rom', None), ('apple2integer.rom', None)], True),
        ('a2_rom_chr', 0x800, [('a2.chr', 0x64f415c6), ('341-0036.chr', None), ('apple2.chr', None)], False),
        ('a2_rom_disk', 0x100, [('341-0027-a.p5', 0xce7144f6), ('disk2-16boot.rom', None), ('diskii.rom', None)], False),
        ('a2_rom_aid', 0x800, [('341-0016-00.d0', 0x4234e88a)], False),
    ],
    'apple2p': [
        ('a2_rom_main', 0x3000, [(('341-0011.d0', '341-0012.d8', '341-0013.e0', '341-0014.e8', '341-0015.f0',
                                   '341-0020-00.f8'), None),
                                 ('apple2plus.rom', None), ('apple2p.rom', None), ('apple2.rom', None)], True),
        ('a2_rom_chr', 0x800, [('341-0036.chr', 0x64f415c6), ('a2.chr', None), ('apple2.chr', None)], False),
        ('a2_rom_disk', 0x100, [('341-0027-a.p5', 0xce7144f6), ('disk2-16boot.rom', None), ('diskii.rom', None)], False),
        ('a2_rom_aid', 0x800, [], False),
    ],
    'apple2e': [
        ('a2_rom_main', 0x4000, [(('342-0135-b.64', '342-0134-a.64'), None), ('apple2e.rom', None)], True),
        ('a2_rom_chr', 0x1000, [('342-0133-a.chr', 0xb081df66), ('apple2e.chr', None)], False),
        ('a2_rom_disk', 0x100, [('341-0027-a.p5', 0xce7144f6), ('disk2-16boot.rom', None), ('diskii.rom', None)], False),
        ('a2_rom_aid', 0x800, [], False),
    ],
    'apple2ee': [
        ('a2_rom_main', 0x4000, [(('342-0304-a.e10', '342-0303-a.e8'), None), ('apple2ee.rom', None),
                                 ('AppleIIe.rom', None)], True),
        ('a2_rom_chr', 0x1000, [('342-0265-a.chr', 0x2651014d), ('342-0133-a.chr', None)], False),
        ('a2_rom_disk', 0x100, [('341-0027-a.p5', 0xce7144f6), ('disk2-16boot.rom', None), ('diskii.rom', None)], False),
        ('a2_rom_aid', 0x800, [], False),
    ],
    # Apple IIGS ROM 03: 341-0728 is banks $FC-$FD, 341-0748 is $FE-$FF with
    # its halves swapped (the core puts them in place)
    'apple2gs': [
        ('a2gs_rom_fc', 0x20000, [('341-0728', 0x8d410067), ('341-0728.bin', None)], True),
        ('a2gs_rom_fe', 0x20000, [('341-0748', 0x18190283), ('341-0748.bin', None)], True),
        ('a2gs_rom_chr', 0x1000, [('apple2gs.chr', 0x91e53cd8)], True),
    ],
    # Macintosh Plus: the even / odd byte chips (the core interleaves them)
    'macplus': [
        ('macplus_rom_even', 0x10000, [('342-0341-c.u6d', 0xf69697e6), ('342-0341-b.u6d', 0x65341487),
                                       ('342-0341-a.u6d', 0x5095fe39)], True),
        ('macplus_rom_odd', 0x10000, [('342-0342-b.u8d', 0x49f25913), ('342-0342-a.u8d', 0xfb766270)], True),
    ],
    'macii': [
        ('macii_rom', 0x40000, [('9779d2c4.rom', 0x4df6d054), ('97851db6.rom', 0x8c8b9d03)], True),
        ('macii_rom_card', 0x8000, [('3410868.bin', 0x57f925fa)], True),
    ],
    # ZX Spectrum Next: the TBBlue boot ROM (MAME's tbblue set; NextZXOS and
    # its ROMs come from the SD card image)
    'specnext': [
        ('specnext_rom_boot', 0x2000, [('boot-30204.bin', 0x95118eb6), ('boot-30200.bin', None),
                                       ('boot-30100.bin', 0xccbd55ba), ('tbblue_loader.rom', None)], True),
    ],
    # SAM Coupe: the 32 KB ROM (MAME's samcoupe set has every version: 3.0 first)
    'samcoupe': [
        ('samcoupe_rom', 0x8000, [('rom30.z5', 0xe535c25d), ('rom30.rom', 0xe535c25d), ('samcoupe.rom', None),
                                  ('rom31.z5', 0x0b7e3585), ('rom25.z5', 0xddadd358), ('rom24.z5', 0xbb23fee4),
                                  ('rom21.z5', 0xf6804b46), ('rom20.z5', 0xeaf32054), ('rom181.z5', 0xd25e1de1),
                                  ('rom18.z5', 0xf626063f), ('rom14.z5', 0x08799596), ('rom13.z5', 0x2093768c),
                                  ('rom12.z5', 0x7fe37dd8), ('rom10.z5', 0x3659d31f), ('rom04.z5', 0xf439e84e),
                                  ('rom01.z5', 0xc04acfdf), ('atom.z5', 0xdec75f58)], True),
    ],
    # Exelvision (drivers/computers/exelv): one rom_data_<model>.c each
    'exl100': [
        ('exelv_rom_main', 0x800, [('exl100in.bin', 0x049109a3), ('exl100.bin', None)], True),
        ('exelv_rom_sub', 0x1000, [('exl100_7041.bin', 0x38f6fc7a), ('exl100io.bin', None)], False),
        ('exelv_rom_sys', 0x10000, [], False),
    ],
    'exeltel': [
        ('exelv_rom_main', 0x1000, [('exeltel_7040.bin', 0x2792f02f), ('exeltel.bin', None)], True),
        ('exelv_rom_sub', 0x1000, [('exeltel_7042.bin', 0xa0163507)], False),
        ('exelv_rom_sys', 0x10000, [('exeltel14.bin', 0x52a80dd4), ('amper.bin', None), ('exeltel.rom', None)], False),
    ],
    # consoles (main/drivers/consoles): their BIOS, all optional
    'coleco': [
        ('coleco_rom_bios', 0x2000, [('coleco.rom', 0x3aa93ef3), ('313 10031-4005 73108a.u2', 0x3aa93ef3), ('313_10031-4005_73108a.u2', 0x3aa93ef3)], False),
    ],
    'scv': [
        ('scv_rom_bios', 0x1000, [('upd7801g.s01', 0x7ac06182)], False),
        ('scv_rom_chr', 0x400, [('epochtv.chr', 0xdb521533)], False),
    ],
    'vectrex': [
        ('vectrex_rom_bios', 0x2000, [('exec_rom.bin', 0xba13fb57)], False),
    ],
    'pv2000': [
        ('pv2000_rom_bios', 0x4000, [('hn613128pc64.bin', 0x8f31f297)], False),
    ],
    'sega32x': [
        ('s32x_rom_bios_g', 0x100, [('32x_g_bios.bin', 0x5c12eae8)], False),
        ('s32x_rom_bios_m', 0x800, [('32x_m_bios.bin', 0xdd9c46b8)], False),
        ('s32x_rom_bios_s', 0x400, [('32x_s_bios.bin', 0xbfda1fe5)], False),
    ],
    'gba': [
        ('gba_rom_bios', 0x4000, [('gba_bios.bin', 0x81977335), ('gba.bin', 0x81977335)], False),
    ],
    'psx': [
        ('psx_rom_bios', 0x80000, [('scph1001.bin', 0x37157331), ('scph5501.bin', 0x8d8cb7e4), ('scph7001.bin', 0x502224b6),
                                   ('scph1000.bin', 0x3b601fc8), ('scph5500.bin', 0xff3eeb8c), ('scph5502.bin', 0xd786f0b9),
                                   ('psx.bin', None), ('bios.bin', None)], False),
    ],
    'lynx': [
        ('lynx_rom_boot', 0x200, [('lynxboot.img', 0x0d973c9d), ('lynx.bin', None), ('lynxa.bin', None)], True),
    ],
}


def open_dir(romdir):
    files = {}
    if os.path.isdir(romdir):
        for name in os.listdir(romdir):
            path = os.path.join(romdir, name)
            if name.lower().endswith('.zip'):
                with zipfile.ZipFile(path) as z:
                    for info in z.infolist():
                        files.setdefault(os.path.basename(info.filename).lower(), z.read(info))
            elif os.path.isfile(path):
                with open(path, 'rb') as f:
                    files[name.lower()] = f.read()
    return files


def find(files, names, size):
    for name, crc in names:
        if isinstance(name, tuple):
            parts = [files.get(n.lower()) for n in name]
            if all(p is not None for p in parts):
                data = b''.join(parts)
                if len(data) == size:
                    return '+'.join(name), data
            continue
        data = files.get(name.lower())
        if data is None:
            continue
        if crc is not None and zlib.crc32(data) & 0xffffffff != crc:
            print('warning: %s CRC %08x (expected %08x)' % (name, zlib.crc32(data) & 0xffffffff, crc))
        if len(data) != size:
            print('warning: %s is %d bytes (expected %d)' % (name, len(data), size))
            data = (data + b'\xff' * size)[:size]
        return name, data
    return None, None


EMBED = '#include "../../../rom/rom_embed.h"\n'


def emit(out, machine, arrays):
    with open(out, 'w') as f:
        f.write('// Generated by tools/gen_roms_computers.py for %s - do not edit.\n' % machine)
        f.write('// (ROMS=external builds leave the ROMs out: they come from storage, rom_load.c)\n')
        f.write('#include "rom_data.h"\n' + EMBED)
        for sym, size, data in arrays:
            if data is None:
                f.write('const uint8_t *%s = NULL;\n' % sym)
                continue
            f.write('#ifndef CMESYS_ROMS_EXTERNAL\nstatic const uint8_t %s_data[%d] = {\n' % (sym, size))
            for i in range(0, size, 16):
                f.write('  ' + ', '.join('0x%02x' % b for b in bytearray(data[i:i + 16])) + ',\n')
            f.write('};\n#endif\nconst uint8_t *%s = ROM_EMBED(%s_data);\n' % (sym, sym))


def write_stub(machine, out):
    with open(out, 'w') as f:
        f.write('// Generated by tools/gen_roms_computers.py for %s - do not edit.\n' % machine)
        f.write('// Empty stub: the ROMs come from storage (rom_load.c), or run\n'
                '// tools/regen.sh %s with them in roms/%s/ to build them in.\n' % (machine, machine))
        f.write('#include "rom_data.h"\n#include <stddef.h>\n')
        for sym, size, names, required in SETS[machine]:
            f.write('const uint8_t *%s = NULL;\n' % sym)


# The loaders (rom_load.c next to rom_data.c): the same sets in C, for the
# ROMs that are not built in (romfs: roms/<set>/, roms/<set>.zip, the SD...).
LOADERS = {   # machine: (function, set names searched, ROMs required)
    'cpc6128': ('cpc_rom_load', ['cpc6128'], True),
    'c64': ('c64_rom_load', ['c64'], True),
    'msx1': ('msx_rom_load', ['msx1'], True),
    'msx2': ('msx_rom_load', ['msx2'], True),
    'amiga': ('amiga_rom_load', ['amiga', 'a500'], True),
    'atarist': ('st_rom_load', ['atarist', 'st'], True),
    'coleco': ('coleco_rom_load', ['coleco'], False),
    'scv': ('scv_rom_load', ['scv'], False),
    'vectrex': ('vectrex_rom_load', ['vectrex'], False),
    'pv2000': ('pv2000_rom_load', ['pv2000'], False),
    'sega32x': ('s32x_rom_load', ['sega32x', '32x'], False),
    'gba': ('gba_rom_load', ['gba'], False),
    'psx': ('psx_rom_load', ['psx'], False),
    'lynx': ('lynx_rom_load', ['lynx'], False),   # (without it: the built-in loader)
    'apple2': ('apple2_rom_load', ['apple2'], True),
    'apple2p': ('apple2p_rom_load', ['apple2p'], True),
    'apple2e': ('apple2e_rom_load', ['apple2e'], True),
    'apple2ee': ('apple2ee_rom_load', ['apple2ee'], True),
    'apple2gs': ('apple2gs_rom_load', ['apple2gs'], True),
    'macplus': ('macplus_rom_load', ['macplus'], True),
    'macii': ('macii_rom_load', ['macii', 'nb_mdc824'], True),
    'specnext': ('specnext_rom_load', ['specnext', 'tbblue'], True),
    'samcoupe': ('samcoupe_rom_load', ['samcoupe'], True),
    'exl100': ('exl100_rom_load', ['exl100'], True),
    'exeltel': ('exeltel_rom_load', ['exeltel'], True),
}
LOADER_FILES = {   # rom_load.c -> the machines in it (#if MSX_MODEL for the two MSX)
    'main/drivers/computers/cpc/rom_load.c': ['cpc6128'],
    'main/drivers/computers/c64/rom_load.c': ['c64'],
    'main/drivers/computers/msx/rom_load.c': ['msx1', 'msx2'],
    'main/drivers/computers/amiga/rom_load.c': ['amiga'],
    'main/drivers/computers/atarist/rom_load.c': ['atarist'],
    'main/drivers/consoles/lynx/rom_load.c': ['lynx'],
    'main/drivers/computers/apple2/rom_load.c': ['apple2', 'apple2p', 'apple2e', 'apple2ee'],
    'main/drivers/computers/apple2gs/rom_load.c': ['apple2gs'],
    'main/drivers/computers/macplus/rom_load.c': ['macplus'],
    'main/drivers/computers/macii/rom_load.c': ['macii'],
    'main/drivers/computers/specnext/rom_load.c': ['specnext'],
    'main/drivers/computers/samcoupe/rom_load.c': ['samcoupe'],
    'main/drivers/computers/exelv/rom_load.c': ['exl100', 'exeltel'],
}
# rom_load.c with several models: (the build's model macro, its loader, the models' values)
MULTI = {
    ('msx1', 'msx2'): ('MSX_MODEL', 'msx_rom_load', ['1', '2']),
    ('apple2', 'apple2p', 'apple2e', 'apple2ee'): ('A2_MODEL', 'a2_rom_load', ['0', '1', '2', '3']),
    ('exl100', 'exeltel'): ('EXELV_MODEL', 'exelv_rom_load', ['0', '1']),
}
for _c in ('coleco', 'scv', 'vectrex', 'pv2000', 'sega32x', 'gba', 'psx'):
    LOADER_FILES['main/drivers/consoles/%s/rom_load.c' % _c] = [_c]

LOADER_FN = '''
bool %s(void)
{
    static const char *const sets[] = {%s, NULL};
    const romfs_entry e[] = {
%s
    };
    return romfs_fill(sets, e, (int)(sizeof e / sizeof e[0]));
}'''


def loader_body(machine, fn=None):
    fn0, sets, need = LOADERS[machine]
    fn = fn or fn0
    out = []
    ents = []
    for sym, size, names, required in SETS[machine]:
        if not names:
            continue
        cands = []
        for name, crc in names:
            nm = '+'.join(name) if isinstance(name, tuple) else name
            cands.append('{"%s", 0x%08xu}' % (nm, crc or 0))
        out.append('static const romfs_cand c_%s_%s[] = {\n    %s,\n    {NULL, 0}};' %
                   (machine, sym, ',\n    '.join(cands)))
        ents.append('        {&%s, 0x%x, c_%s_%s, %s},' % (sym, size, machine, sym, 'true' if (required and need) else 'false'))
    out.append(LOADER_FN % (fn, ', '.join('"%s"' % x for x in sets), '\n'.join(ents)))
    return '\n'.join(out)


def write_loader(path, machines):
    with open(path, 'w') as f:
        f.write('// rom_load.c - generated by tools/gen_roms_computers.py --loaders - do not edit.\n'
                '// The ROMs of %s that rom_data.c does not have (a stub, or ROMS=external),\n'
                '// from storage: see main/rom/romfs.h.\n' % ' / '.join(machines))
        f.write('#include "rom_data.h"\n#include "../../../rom/romfs.h"\n\n')
        if len(machines) == 1:
            f.write(loader_body(machines[0]) + '\n')
        else:   # every model (the Android app has them all), and the build's
            macro, fn, values = MULTI[tuple(machines)]
            f.write('\n\n'.join(loader_body(mm, mm + '_rom_load') for mm in machines) + '\n')
            if macro == 'MSX_MODEL':
                f.write('\n#ifdef MSX_MODEL\nbool msx_rom_load(void) { return MSX_MODEL == 2 ? msx2_rom_load() : msx1_rom_load(); }\n#endif\n')
                return
            f.write('\n#ifdef %s\nbool %s(void)\n{\n' % (macro, fn))
            for mm, v in zip(machines[:-1], values[:-1]):
                f.write('    if (%s == %s) return %s_rom_load();\n' % (macro, v, mm))
            f.write('    return %s_rom_load();\n}\n#endif\n' % machines[-1])


def main():
    args = sys.argv[1:]
    if args and args[0] == '--stub':
        write_stub(args[1], args[2])
        return 0
    if args and args[0] == '--loaders':
        root = args[1] if len(args) > 1 else '.'
        for path, machines in LOADER_FILES.items():
            write_loader(os.path.join(root, path), machines)
            print('wrote', path)
        return 0
    if len(args) != 3 or args[0] not in SETS:
        print(__doc__)
        return 1
    machine, romdir, out = args
    files = open_dir(romdir)
    arrays = []
    for sym, size, names, required in SETS[machine]:
        name, data = find(files, names, size)
        if data is None:
            if required:
                print('error: %s missing in %s (one of %s)' % (sym, romdir, ', '.join(str(n) for n, _ in names)))
                return 1
            print('note: %s not found (optional)' % sym)
        else:
            print('%s <- %s' % (sym, name))
        arrays.append((sym, size, data))
    emit(out, machine, arrays)
    return 0


if __name__ == '__main__':
    sys.exit(main())
