// rom_data.h - the Apple IIGS's ROMs (tools/gen_roms_computers.py apple2gs;
// NULL in the committed stub: put MAME's apple2gs set in roms/apple2gs/ and
// run tools/regen.sh apple2gs)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *a2gs_rom_fc;   // 341-0728: banks $FC-$FD (128 KB)
extern const uint8_t *a2gs_rom_fe;   // 341-0748: banks $FF, $FE (128 KB, as in the chip)
extern const uint8_t *a2gs_rom_chr;  // the video ROM (4 KB)
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool apple2gs_rom_load(void);
