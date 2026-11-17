// rom_load.c - the Sinclair QL ROMs that rom_data.c does not have (a stub,
// or ROMS=external), from storage (main/rom/romfs.h): roms/ql/ or MAME's
// ql.zip (as tools/gen_roms_ql.py), or a 48 KB js.rom / jm.rom / minerva.rom
// and ipc.rom (the names of the Android app's assets).
#include "rom_data.h"
#include "../../../rom/romfs.h"
#include <stdlib.h>
#include <string.h>

bool ql_rom_load(void)
{
    static const char *const sets[] = {"ql", NULL};
    if (!ql_rom) {
        uint8_t *rom = (uint8_t *)malloc(0xc000);
        if (rom && romfs_get_into(sets, "ql.js 0000.ic33", 0x1bbad3b8, rom, 0x8000) &&
            romfs_get_into(sets, "ql.js 8000.ic34", 0xc970800e, rom + 0x8000, 0x4000)) {
            ql_rom = rom;
        } else {
            free(rom);
            static const romfs_cand whole[] = {{"js.rom", 0}, {"ql_js.rom", 0}, {"jm.rom", 0}, {"minerva.rom", 0}, {NULL, 0}};
            uint32_t n = 0;
            rom = romfs_get_any(sets, whole, &n, NULL);
            if (rom && n >= 0xc000) ql_rom = rom;
            else free(rom);
        }
    }
    if (!ql_ipc_rom) {
        static const romfs_cand ipc[] = {{"ipc8049.ic24", 0x6a0d1f20}, {"ipc.rom", 0}, {NULL, 0}};
        uint32_t n = 0;
        uint8_t *b = romfs_get_any(sets, ipc, &n, NULL);
        if (b && n >= 0x800) ql_ipc_rom = b;
        else free(b);
    }
    romfs_release();
    if (!ql_rom) romfs_missing(sets, "ql.js 0000.ic33 + ql.js 8000.ic34 (or js.rom, 48 KB)");
    else if (!ql_ipc_rom) romfs_missing(sets, "ipc8049.ic24 (2 KB)");
    return ql_rom && ql_ipc_rom;
}
