// rom_load.c - the Gauntlet ROMs from storage when rom_data.c has none (a
// stub, or ROMS=external): the same loading and decoding as
// tools/gen_roms_gauntlet.py (MAME set "gauntlet", rev 14).
#include "rom_data.h"
#include "../../../rom/romfs_arcade.h"

static const char *const sets[] = {"gauntlet", NULL};

// 136037-xxxx: the 68010's two lanes, the halves swapped (ROM_LOAD at +$8000, ROM_CONTINUE at +0)
static bool prog_pair(uint8_t *prog, uint32_t base, const char *name, uint32_t crc, int odd)
{
    uint8_t *r = ra_get(sets, name, 0x8000, crc);
    if (!r) return false;
    for (uint32_t i = 0; i < 0x4000; i++) {
        prog[base + 0x8000 + 2 * i + odd] = r[i];
        prog[base + 2 * i + odd] = r[0x4000 + i];
    }
    free(r);
    return true;
}

bool gt_roms_get(gt_roms *r)
{
    if (gt_rom_data(r)) return true;
    static const struct { const char *name; uint32_t crc; } tiles[8] = {
        {"136037-111.1a", 0x91700f33}, {"136037-112.1b", 0x869330be}, {"136037-113.1l", 0xd497d0a8},
        {"136037-114.1mn", 0x29ef9882}, {"136037-115.2a", 0x9510b898}, {"136037-116.2b", 0x11e0ac5b},
        {"136037-117.2l", 0x29a5db41}, {"136037-118.2mn", 0x8bf3b263}};
    uint8_t *prog = (uint8_t *)calloc(1, 0x80000), *snd = (uint8_t *)malloc(0xc000);
    uint8_t *chars = NULL, *spr = (uint8_t *)malloc(0x40000);
    uint32_t *tile = (uint32_t *)malloc(0x2000 * 8 * sizeof(uint32_t));
    bool ok = prog && snd && spr && tile &&
              prog_pair(prog, 0x00000, "136037-1307.9a", 0x46fe8743, 0) &&
              prog_pair(prog, 0x00000, "136037-1308.9b", 0x276e15c4, 1) &&
              prog_pair(prog, 0x40000, "136037-1409.7a", 0x6fb8419c, 0) &&
              prog_pair(prog, 0x40000, "136037-1410.7b", 0x931bd2a0, 1) &&
              ra_lane(sets, "136037-205.10a", 0x4000, 0x6d99ed51, prog + 0x38000, 0) &&
              ra_lane(sets, "136037-206.10b", 0x4000, 0x545ead91, prog + 0x38000, 1) &&
              ra_into(sets, "136037-120.16r", 0x4000, 0x6ee7f3cc, snd) &&
              ra_into(sets, "136037-119.16s", 0x8000, 0xfa19861f, snd + 0x4000) &&
              (chars = ra_get(sets, "136037-104.6p", 0x4000, 0x6c276a1d)) != NULL;
    for (int i = 0; ok && i < 8; i++) ok = ra_into(sets, tiles[i].name, 0x8000, tiles[i].crc, spr + i * 0x8000);
    romfs_release();
    if (ok) {
        for (uint32_t i = 0; i < 0x40000; i++) spr[i] ^= 0xff;            // ROMREGION_INVERT
        for (uint32_t at = 0; at < 0x2000 * 8; at++) {
            uint32_t row = 0;
            for (int k = 0; k < 4; k++) {                             // plane (3 - k) -> pen bit 3 - k
                uint8_t b = spr[(3 - k) * 0x10000 + at];
                for (int x = 0; x < 8; x++) row |= (uint32_t)((b >> (7 - x)) & 1) << (4 * x + 3 - k);
            }
            tile[at] = row;
        }
        r->main = prog;
        r->sound = snd;
        r->chars = chars;
        r->tiles = tile;
    } else {
        free(prog); free(snd); free(chars); free(tile);
    }
    free(spr);
    return ok;
}
