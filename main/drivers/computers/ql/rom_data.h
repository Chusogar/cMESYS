// rom_data.h - Sinclair QL ROMs and the microdrive cartridge in flash
// (rom_data.c, made by tools/gen_roms_ql.py; run tools/regen.sh ql)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdbool.h>
#include <stdint.h>
extern const uint8_t *ql_rom;               // JS system ROM (0000-BFFF), 48 KB; NULL: not built in
extern const uint8_t *ql_ipc_rom;           // 8749 IPC, 2 KB
extern const uint8_t *const ql_mdv_image;   // QLAY cartridge (174930 bytes), NULL: none
extern const char ql_mdv_name[];            // its file name
// The ROMs not compiled in, from storage (rom_load.c, main/rom/romfs.h):
// roms/ql/ or MAME's ql.zip. False if they are not there (romfs_error()).
bool ql_rom_load(void);
#endif
