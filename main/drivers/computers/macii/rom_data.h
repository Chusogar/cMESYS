// rom_data.h - the Macintosh II's ROMs (tools/gen_roms_computers.py macii;
// NULL in the committed stub: put MAME's macii set in roms/macii/ and run
// tools/regen.sh macii)
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *macii_rom;       // 9779d2c4.rom (rev. B) or 97851db6.rom (rev. A), 256 KB
extern const uint8_t *macii_rom_card;  // 3410868.bin: the Display Card 8*24's (32 KB; MAME's nb_mdc824)
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h);
// false if a required one is missing (romfs_error() says which).
bool macii_rom_load(void);
