// rom_data.h - the Lynx boot ROM (tools/gen_roms_computers.py lynx; NULL in
// the committed stub: put lynxboot.img in roms/lynx/ and run tools/regen.sh lynx)
#pragma once
#include <stdint.h>
extern const uint8_t *const lynx_rom_boot;     // 512 bytes or NULL (built-in loader)
