// rom_data.h - Sinclair QL ROMs and the microdrive cartridge in flash
// (rom_data.c, made by tools/gen_roms_ql.py; run tools/regen.sh ql)
#ifndef ROM_DATA_H
#define ROM_DATA_H
#include <stdint.h>
extern const uint8_t ql_rom[0xc000];        // JS system ROM (0000-BFFF)
extern const uint8_t ql_ipc_rom[0x800];     // 8749 IPC
extern const uint8_t *const ql_mdv_image;   // QLAY cartridge (174930 bytes), NULL: none
extern const char ql_mdv_name[];            // its file name
#endif
