// rom_data.h - the Apple II family's ROMs (tools/gen_roms_computers.py apple2 /
// apple2p / apple2e / apple2ee: rom_data_<model>.c; NULL in the committed
// stubs: put MAME's set in roms/<model>/ and run tools/regen.sh <model>)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *a2_rom_main;   // ][: 8 KB at $E000; ][+: 12 KB at $D000; //e: 16 KB at $C000
extern const uint8_t *a2_rom_chr;    // 2 KB (][, ][+) or 4 KB (//e), or NULL
extern const uint8_t *a2_rom_disk;   // the Disk II's P5 PROM (256 bytes) or NULL
extern const uint8_t *a2_rom_aid;    // ][: the Programmer's Aid #1 at $D000 (2 KB) or NULL
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool a2_rom_load(void);              // (the build's A2_MODEL)
bool apple2_rom_load(void);
bool apple2p_rom_load(void);
bool apple2e_rom_load(void);
bool apple2ee_rom_load(void);
// The ROM sizes of each model (a2_rom_main, a2_rom_chr)
static inline uint32_t a2_main_size(int model) { return model == 0 ? 0x2000 : model == 1 ? 0x3000 : 0x4000; }
static inline uint32_t a2_chr_size(int model) { return model < 2 ? 0x800 : 0x1000; }
