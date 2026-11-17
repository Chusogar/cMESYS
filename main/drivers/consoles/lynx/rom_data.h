// rom_data.h - the Lynx boot ROM (tools/gen_roms_computers.py lynx; NULL in
// the committed stub: put lynxboot.img in roms/lynx/ and run tools/regen.sh lynx)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *lynx_rom_boot;     // 512 bytes or NULL (built-in loader)
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool lynx_rom_load(void);
