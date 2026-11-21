#!/usr/bin/env python3
"""Write a machine's rom_data.c without ROMs (a stub), the form the
repository keeps: the machine then reads its ROMs from storage at run time
(main/rom/romfs.h: roms/<set>/, roms/<set>.zip, the microSD...). Run by
tools/regen.sh --stub <machine>.

  tools/gen_rom_stub.py <arcade> <out.c>

The computers and the consoles have their own --stub (gen_roms_computers.py,
gen_roms_zxspec.py, gen_roms_ql.py); this is for the arcades.
"""
import sys

STRUCT = {   # game: (function, struct)
    'indytemp': ('as1_rom_data', 'as1_roms'), 'roadrunn': ('as1_rom_data', 'as1_roms'),
    'gauntlet': ('gt_rom_data', 'gt_roms'), 'outrun': ('or_rom_data', 'or_roms'),
    'polepos': ('pp_rom_data', 'pp_roms'), 'polepos2': ('pp_rom_data', 'pp_roms'),
    'robocop': ('rc_rom_data', 'rc_roms'),
}
POINTERS = {   # game: its ROM pointers (type, name)
    'retofinv': [('uint8_t', n) for n in ('rv_main_rom', 'rv_sub_rom', 'rv_sound_rom', 'rv_mcu_rom', 'rv_fg_rom')]
                + [('uint32_t', 'rv_bg_gfx'), ('uint32_t', 'rv_spr_gfx'), ('uint8_t', 'rv_palette_prom'), ('uint8_t', 'rv_clut')],
    'snowbros': [('uint8_t', n) for n in ('sb_prog_rom', 'sb_snd_rom', 'sb_gfx_rom')],
    'starwars': [('uint8_t', n) for n in ('sw_main_rom', 'sw_vec_rom', 'sw_snd_rom', 'sw_avg_prom', 'sw_math_prom',
                                          'esb_main_rom', 'esb_slap_rom', 'esb_vec_rom', 'esb_snd_rom',
                                          'esb_avg_prom', 'esb_math_prom')],
    'trackfld': [('uint8_t', n) for n in ('tf_main_rom', 'tf_audio_rom', 'tf_vlm_rom', 'tf_prom', 'tf_chr_pix', 'tf_spr_pix')],
    'pbobble': [('uint8_t', n) for n in ('pb_prog_rom', 'pb_snd_rom', 'pb_gfx_rom', 'pb_adpcma_rom')],
}


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in STRUCT and sys.argv[1] not in POINTERS:
        sys.exit(__doc__)
    game, out = sys.argv[1], sys.argv[2]
    lines = ['// rom_data.c - %s without ROMs (tools/gen_rom_stub.py): they come from' % game,
             '// storage (rom_load.c, main/rom/romfs.h), or run tools/regen.sh %s with' % game,
             '// them in roms/%s/ to build them in.' % game,
             '#include "rom_data.h"', '#include <stddef.h>']
    if game in STRUCT:
        fn, t = STRUCT[game]
        lines += ['bool %s(%s *r)' % (fn, t), '{', '    (void)r;', '    return false;', '}']
    else:
        lines += ['const %s *%s = NULL;' % (t, n) for t, n in POINTERS[game]]
    open(out, 'w').write('\n'.join(lines) + '\n')
    print('stub ->', out)


main()
