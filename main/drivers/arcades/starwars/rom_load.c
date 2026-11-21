// rom_load.c - the Star Wars / ESB ROMs from storage when rom_data.c has
// none (a stub, or ROMS=external): MAME's sets "starwars" and "esb", laid
// out as tools/gen_roms_starwars.py does.
#include "rom_data.h"
#include "sw_core.h"
#include "../../../rom/romfs_arcade.h"

// (the set read from storage: freed when the other game's is read, so that a
// board holds only one of them)
static bool sw_owned, esb_owned;

static void release_starwars(void)
{
    if (!sw_owned) return;
    free((void *)sw_main_rom), free((void *)sw_vec_rom), free((void *)sw_snd_rom);
    free((void *)sw_avg_prom), free((void *)sw_math_prom);
    sw_main_rom = sw_vec_rom = sw_snd_rom = sw_avg_prom = sw_math_prom = NULL;
    sw_owned = false;
}

static void release_esb(void)
{
    if (!esb_owned) return;
    free((void *)esb_main_rom), free((void *)esb_slap_rom), free((void *)esb_vec_rom);
    free((void *)esb_snd_rom), free((void *)esb_avg_prom), free((void *)esb_math_prom);
    esb_main_rom = esb_slap_rom = esb_vec_rom = esb_snd_rom = esb_avg_prom = esb_math_prom = NULL;
    esb_owned = false;
}

static const char *const sw_sets[] = {"starwars", NULL};
static const char *const esb_sets[] = {"esb", "starwars", NULL};

static bool get_starwars(void)
{
    if (sw_main_rom) return true;
    uint8_t *m = (uint8_t *)malloc(SW_MAIN_ROM_LEN), *sn = (uint8_t *)malloc(SW_SND_ROM_LEN);
    uint8_t *mp = (uint8_t *)malloc(SW_MATH_PROM_LEN), *vec = NULL, *avg = NULL;
    const char *const *s = sw_sets;
    bool ok = m && sn && mp &&
              ra_into(s, "136021.214.1f", 0x4000, 0x04f1876e, m) &&
              ra_into(s, "136021.102.1hj", 0x2000, 0xf725e344, m + 0x4000) &&
              ra_into(s, "136021.203.1jk", 0x2000, 0xf6da0a00, m + 0x6000) &&
              ra_into(s, "136021.104.1kl", 0x2000, 0x7e406703, m + 0x8000) &&
              ra_into(s, "136021.206.1m", 0x2000, 0xc7e51237, m + 0xa000) &&
              (vec = ra_get(s, "136021-105.1l", 0x1000, 0x538e7d2f)) != NULL &&
              ra_into(s, "136021-107.1jk", 0x2000, 0xdbf3aea2, sn) &&
              ra_into(s, "136021-208.1h", 0x2000, 0xe38070a8, sn + 0x2000) &&
              (avg = ra_get(s, "136021-109.4b", 0x100, 0x82fc3eb2)) != NULL &&
              ra_into(s, "136021-110.7h", 0x400, 0x810e040e, mp) &&
              ra_into(s, "136021-111.7j", 0x400, 0xae69881c, mp + 0x400) &&
              ra_into(s, "136021-112.7k", 0x400, 0xecf22628, mp + 0x800) &&
              ra_into(s, "136021-113.7l", 0x400, 0x83febfde, mp + 0xc00);
    romfs_release();
    if (ok) {
        sw_main_rom = m;
        sw_vec_rom = vec;
        sw_snd_rom = sn;
        sw_avg_prom = avg;
        sw_math_prom = mp;
        sw_owned = true;
        return true;
    }
    free(m), free(sn), free(mp), free(vec), free(avg);
    return false;
}

static bool get_esb(void)
{
    if (esb_main_rom) return true;
    uint8_t *m = (uint8_t *)malloc(ESB_MAIN_ROM_LEN), *sl = (uint8_t *)malloc(ESB_SLAP_ROM_LEN);
    uint8_t *sn = (uint8_t *)malloc(ESB_SND_ROM_LEN), *mp = (uint8_t *)malloc(ESB_MATH_PROM_LEN);
    uint8_t *vec = NULL, *avg = NULL;
    const char *const *s = esb_sets;
    bool ok = m && sl && sn && mp &&
              ra_into(s, "136031-101.1f", 0x4000, 0xef1e3ae5, m) &&
              ra_into(s, "136031-102.1jk", 0x4000, 0x62ce5c12, m + 0x4000) &&
              ra_into(s, "136031-203.1kl", 0x4000, 0x27b0889b, m + 0x8000) &&
              ra_into(s, "136031-104.1m", 0x4000, 0xfd5c725e, m + 0xc000) &&
              ra_into(s, "136031-105.3u", 0x4000, 0xea9e4dce, sl) &&
              ra_into(s, "136031-106.2u", 0x4000, 0x76d07f59, sl + 0x4000) &&
              (vec = ra_get(s, "136031-111.1l", 0x1000, 0xb1f9bd12)) != NULL &&
              ra_into(s, "136031-113.1jk", 0x4000, 0x24ae3815, sn) &&
              ra_into(s, "136031-112.1h", 0x4000, 0xca72d341, sn + 0x4000) &&
              (avg = ra_get(s, "136021-109.4b", 0x100, 0x82fc3eb2)) != NULL &&
              ra_into(s, "136031-110.7h", 0x400, 0xb8d0f69d, mp) &&
              ra_into(s, "136031-109.7j", 0x400, 0x6a2a4d98, mp + 0x400) &&
              ra_into(s, "136031-108.7k", 0x400, 0x6a76138f, mp + 0x800) &&
              ra_into(s, "136031-107.7l", 0x400, 0xafbf6e01, mp + 0xc00);
    romfs_release();
    if (ok) {
        esb_main_rom = m;
        esb_slap_rom = sl;
        esb_vec_rom = vec;
        esb_snd_rom = sn;
        esb_avg_prom = avg;
        esb_math_prom = mp;
        esb_owned = true;
        return true;
    }
    free(m), free(sl), free(sn), free(mp), free(vec), free(avg);
    return false;
}

bool sw_roms_get(int game)
{
    if (game == SW_GAME_ESB) {
        if (esb_main_rom) return true;
        release_starwars();
        return get_esb();
    }
    if (sw_main_rom) return true;
    release_esb();
    return get_starwars();
}
