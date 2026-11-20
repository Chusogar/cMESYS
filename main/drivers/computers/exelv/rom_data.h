// rom_data.h - the Exelvision ROMs (tools/gen_roms_computers.py exl100 /
// exeltel: rom_data_<model>.c; NULL in the committed stubs: put MAME's set in
// roms/<model>/ and run tools/regen.sh <model>)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *exelv_rom_main;   // the TMS7020 (2 KB, EXL-100) or TMS7040 (4 KB, EXELTEL)
extern const uint8_t *exelv_rom_sub;    // the TMS7041 / 7042 I/O CPU (4 KB), or NULL
extern const uint8_t *exelv_rom_sys;    // the EXELTEL's system ROM (64 KB), or NULL
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if the main CPU's is missing (romfs_error() says which).
bool exelv_rom_load(void);              // (the build's EXELV_MODEL)
bool exl100_rom_load(void);
bool exeltel_rom_load(void);
