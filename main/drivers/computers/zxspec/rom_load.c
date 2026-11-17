// rom_load.c - the ZX Spectrum / clone ROMs that rom_data_<model>.c does not
// have (a stub, or ROMS=external), from storage (main/rom/romfs.h): the same
// files and choices as tools/gen_roms_zxspec.py, in roms/<model>/ or the
// MAME set (spectrum, spec128, plus3, pentagon, pent1024, scorpio).
#include "rom_data.h"
#include "zxspec_core.h"
#include "../../../rom/romfs.h"
#include <stdlib.h>
#include <string.h>

#define K16 0x4000

static const char *const *sets;

// The first of `names` found with at least `size` bytes (only those used).
static uint8_t *find(const char *const *names, uint32_t size)
{
    for (; *names; names++) {
        uint32_t n = 0;
        uint8_t *b = romfs_get(sets, *names, 0, 0, &n);
        if (b && n >= size) return b;
        free(b);
    }
    return NULL;
}

// Each of `names` (16 KB each) one after the other; NULL if one is missing.
static uint8_t *parts(const char *const *names)
{
    int n = 0;
    while (names[n]) n++;
    uint8_t *all = (uint8_t *)malloc((size_t)n * K16);
    for (int i = 0; all && i < n; i++) {
        const char *one[] = {names[i], NULL};
        uint8_t *b = find(one, K16);
        if (!b) { free(all); return NULL; }
        memcpy(all + i * K16, b, K16);
        free(b);
    }
    return all;
}

// A banked image of `size` bytes with `got` bytes at its start, the rest
// 0xff (for the ROMs added after it).
static uint8_t *grow(uint8_t *b, uint32_t got, uint32_t size)
{
    uint8_t *r = (uint8_t *)realloc(b, size);
    if (!r) { free(b); return NULL; }
    memset(r + got, 0xff, size - got);
    return r;
}

bool zx_rom_load_model(int model)
{
    if (zx_rom48) return true;                        // built in
    static const char *const s48[] = {"spectrum48", "spectrum", NULL};
    static const char *const s128[] = {"spectrum128", "spec128", NULL};
    static const char *const sp3[] = {"plus3", NULL};
    static const char *const spent[] = {"pentagon", NULL};
    static const char *const s1024[] = {"pentagon1024", "pent1024", NULL};
    static const char *const sscorp[] = {"scorpion", "scorpio", NULL};
    static const char *const rom48_names[] = {"spectrum.rom", "48.rom", "48k.rom", "zx48.rom", "48e.rom", NULL};
    sets = model == ZX_SPEC48 ? s48 : model == ZX_SPEC128 ? s128 : model == ZX_PLUS3 ? sp3
         : model == ZX_PENTAGON ? spent : model == ZX_PENTAGON1024 ? s1024 : sscorp;

    uint8_t *rom48 = find(rom48_names, K16), *rom128 = NULL;
    uint32_t bytes = 0;
    int rom48_bank = 1;                               // the 48K BASIC in the banked ROMs
    if (model == ZX_SPEC128) {
        static const char *const a[] = {"128-0.rom", "128-1.rom", NULL}, *const b[] = {"zx128_0.rom", "zx128_1.rom", NULL};
        static const char *const one[] = {"128.rom", "zx128.rom", NULL};
        rom128 = parts(a);
        if (!rom128) rom128 = parts(b);
        if (!rom128) rom128 = find(one, 0x8000);
        bytes = 0x8000;
    } else if (model == ZX_PLUS3) {
        static const char *const a[] = {"plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom", NULL};
        static const char *const one[] = {"plus3.rom", "plus2a.rom", NULL};
        rom128 = parts(a);
        if (!rom128) rom128 = find(one, 0x10000);
        bytes = 0x10000;
        rom48_bank = 3;
    } else if (model == ZX_PENTAGON) {
        static const char *const a[] = {"128p-0.rom", "128p-1.rom", NULL}, *const b[] = {"128-0.rom", "128-1.rom", NULL};
        static const char *const one[] = {"pentagon.rom", "128.rom", NULL};
        static const char *const trd[] = {"trd503.rom", "trd504.rom", NULL};
        rom128 = parts(a);
        if (!rom128) rom128 = parts(b);
        if (!rom128) rom128 = find(one, 0x8000);
        bytes = 0x8000;
        uint8_t *trdos = rom128 ? find(trd, K16) : NULL;
        if (trdos) {                                  // TR-DOS as ROM 3
            rom128 = grow(rom128, 0x8000, 0x10000);
            if (rom128) memcpy(rom128 + 3 * K16, trdos, K16);
            bytes = 0x10000;
        }
        free(trdos);
    } else if (model == ZX_PENTAGON1024) {
        static const char *const a[] = {"128p-0.rom", "128p-1.rom", NULL};
        static const char *const gl[] = {"gluk63r.rom", "gluk601r.rom", "gluk60r.rom", NULL};
        static const char *const trd[] = {"trd503.rom", "trd504.rom", NULL};
        static const char *const one[] = {"pentagon1024.rom", "glukrom.rom", "gluk.rom", NULL};
        static const char *const p4[] = {"pentagon-0.rom", "pentagon-1.rom", "pentagon-2.rom", "pentagon-3.rom", NULL};
        rom128 = parts(a);
        if (rom128) {
            rom128 = grow(rom128, 0x8000, 0x10000);
            uint8_t *g = rom128 ? find(gl, K16) : NULL, *t = rom128 ? find(trd, K16) : NULL;
            if (g) memcpy(rom128 + 2 * K16, g, K16);
            if (t) memcpy(rom128 + 3 * K16, t, K16);
            free(g);
            free(t);
        } else {
            rom128 = find(one, 0x10000);
            if (!rom128) rom128 = parts(p4);
        }
        bytes = 0x10000;
    } else if (model == ZX_SCORPION) {
        static const char *const one[] = {"scorp294.rom", "scorpion.rom", "scorpio.rom", "scorp.rom", NULL};
        static const char *const p4[] = {"scorp0.rom", "scorp1.rom", "scorp2.rom", "scorp3.rom", NULL};
        static const char *const q4[] = {"scorpion-0.rom", "scorpion-1.rom", "scorpion-2.rom", "scorpion-3.rom", NULL};
        rom128 = find(one, 0x10000);
        if (!rom128) rom128 = parts(p4);
        if (!rom128) rom128 = parts(q4);
        bytes = 0x10000;
    }
    romfs_release();
    if (model != ZX_SPEC48 && !rom128) {
        free(rom48);
        romfs_missing(sets, model == ZX_SPEC128 ? "128-0.rom + 128-1.rom" : model == ZX_PLUS3 ? "plus3-0.rom ... plus3-3.rom"
                      : model == ZX_SCORPION ? "scorp294.rom" : "128p-0.rom + 128p-1.rom");
        return false;
    }
    if (!rom48 && rom128 && (rom48 = (uint8_t *)malloc(K16)) != NULL)   // (a buffer of its own)
        memcpy(rom48, rom128 + rom48_bank * K16, K16);
    if (!rom48) {
        romfs_missing(sets, "spectrum.rom (16 KB)");
        return false;
    }
    zx_rom48 = rom48;
    zx_rom128 = rom128;
    zx_rom128_bytes = rom128 ? bytes : 0;
    return true;
}

#ifdef ZX_MODEL
bool zx_rom_load(void) { return zx_rom_load_model(ZX_MODEL); }
#endif
