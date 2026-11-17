// rom_data.h - the Sega 32X's BIOS (rom_data.c: tools/regen.sh sega32x, from
// roms/sega32x/: 32x_g_bios.bin, the 68000's 256-byte vector ROM, and the
// SH-2s' boot ROMs 32x_m_bios.bin, 2 KB, and 32x_s_bios.bin, 1 KB). NULL:
// none built in (the 32X then refuses its cartridges).
#ifndef S32X_ROM_DATA_H
#define S32X_ROM_DATA_H
#include <stdint.h>
extern const uint8_t *const s32x_rom_bios_g;
extern const uint8_t *const s32x_rom_bios_m;
extern const uint8_t *const s32x_rom_bios_s;
#endif
