// atarisy1_roms.c - ROM loading of the Atari System 1 games (host builds,
// tools/gen_roms_atarisy1.c).
#include "atarisy1_roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *names; uint32_t length, offset; } rom_entry;

// From dsp-cpp's atari_system1.cpp: offsets are byte addresses of the
// image, the low bit choosing the high (even) or low (odd) byte of a word.
static const rom_entry bios[] = {
    {"136032.205.l13|136032.205", 0x4000, 0x0}, {"136032.206.l12|136032.206", 0x4000, 0x1}};
static const rom_entry chars[] = {{"136032.104.f5|136032.104|136032.107.b2", 0x2000, 0}};

// --- Indiana Jones (indytemp) ---
static const rom_entry prog[] = {
    {"136036.432", 0x8000, 0x00000}, {"136036.431", 0x8000, 0x00001},
    {"136036.434", 0x8000, 0x10000}, {"136036.433", 0x8000, 0x10001},
    {"136036.456", 0x4000, 0x20000}, {"136036.457", 0x4000, 0x20001},
    {"136036.358", 0x4000, 0x28000}, {"136036.359", 0x4000, 0x28001}};
static const rom_entry sound[] = {
    {"136036.153", 0x4000, 0x4000}, {"136036.154", 0x4000, 0x8000}, {"136036.155", 0x4000, 0xc000}};
static const rom_entry back[] = {
    {"136036.135", 0x8000, 0x000000}, {"136036.139", 0x8000, 0x010000},
    {"136036.143", 0x8000, 0x020000}, {"136036.147", 0x8000, 0x030000},
    {"136036.136", 0x8000, 0x080000}, {"136036.140", 0x8000, 0x090000},
    {"136036.144", 0x8000, 0x0a0000}, {"136036.148", 0x8000, 0x0b0000},
    {"136036.137", 0x8000, 0x100000}, {"136036.141", 0x8000, 0x110000},
    {"136036.145", 0x8000, 0x120000}, {"136036.149", 0x8000, 0x130000},
    {"136036.138", 0x8000, 0x180000}, {"136036.142", 0x8000, 0x190000},
    {"136036.146", 0x8000, 0x1a0000}, {"136036.150", 0x8000, 0x1b0000}};
static const rom_entry proms[] = {{"136036.152", 0x200, 0x000}, {"136036.151", 0x200, 0x200}};

// --- Road Runner (roadrunn): program at its absolute image offsets ---
static const rom_entry rr_prog[] = {
    {"136040-228.11c|136040.228", 0x8000, 0x10000}, {"136040-229.11a|136040.229", 0x8000, 0x10001},
    {"136040-230.13c|136040.230", 0x8000, 0x20000}, {"136040-231.13a|136040.231", 0x8000, 0x20001},
    {"136040-134.12c|136040.134", 0x8000, 0x50000}, {"136040-135.12a|136040.135", 0x8000, 0x50001},
    {"136040-136.14c|136040.136", 0x8000, 0x60000}, {"136040-137.14a|136040.137", 0x8000, 0x60001},
    {"136040-138.16c|136040.138", 0x8000, 0x70000}, {"136040-139.16a|136040.139", 0x8000, 0x70001},
    {"136040-140.17c|136040.140", 0x4000, 0x80000}, {"136040-141.17a|136040.141", 0x4000, 0x80001}};
static const rom_entry rr_sound[] = {
    {"136040-143.15e|136040.143", 0x4000, 0x8000}, {"136040-144.17e|136040.144", 0x4000, 0xc000}};
static const rom_entry rr_back[] = {
    {"136040-101.4b|136040.101", 0x8000, 0x000000}, {"136040-107.9b|136040.107", 0x8000, 0x010000},
    {"136040-113.4f|136040.113", 0x8000, 0x020000}, {"136040-119.9f|136040.119", 0x8000, 0x030000},
    {"136040-102.3b|136040.102", 0x8000, 0x080000}, {"136040-108.8b|136040.108", 0x8000, 0x090000},
    {"136040-114.3f|136040.114", 0x8000, 0x0a0000}, {"136040-120.8f|136040.120", 0x8000, 0x0b0000},
    {"136040-103.2b|136040.103", 0x8000, 0x100000}, {"136040-109.7b|136040.109", 0x8000, 0x110000},
    {"136040-115.2f|136040.115", 0x8000, 0x120000}, {"136040-121.7f|136040.121", 0x8000, 0x130000},
    {"136040-104.1b|136040.104", 0x8000, 0x180000}, {"136040-110.6b|136040.110", 0x8000, 0x190000},
    {"136040-116.1f|136040.116", 0x8000, 0x1a0000}, {"136040-122.6f|136040.122", 0x8000, 0x1b0000},
    {"136040-105.4d|136040.105", 0x8000, 0x200000}, {"136040-111.9d|136040.111", 0x8000, 0x210000},
    {"136040-117.2d|136040.117", 0x8000, 0x220000}, {"136040-123.7d|136040.123", 0x8000, 0x230000},
    {"136040-106.3d|136040.106", 0x8000, 0x280000}, {"136040-112.8d|136040.112", 0x8000, 0x290000},
    {"136040-118.1d|136040.118", 0x8000, 0x2a0000}, {"136040-124.6d|136040.124", 0x8000, 0x2b0000}};
static const rom_entry rr_proms[] = {{"136040-126.7a|136040.126", 0x200, 0x000}, {"136040-125.5a|136040.125", 0x200, 0x200}};
#define N(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t *load_one(const char *dir, const char *bios_dir, const char *names, uint32_t length,
                         char *err, int errlen)
{
    char list[256];
    snprintf(list, sizeof list, "%s", names);
    for (char *name = strtok(list, "|"); name; name = strtok(NULL, "|")) {
        const char *dirs[2] = {dir, bios_dir};
        for (int d = 0; d < 2; d++) {
            if (!dirs[d]) continue;
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dirs[d], name);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            uint8_t *buf = (uint8_t *)malloc(length);
            size_t n = buf ? fread(buf, 1, length, f) : 0;
            fclose(f);
            if (n != length) {
                free(buf);
                snprintf(err, (size_t)errlen, "%s: tamano incorrecto", path);
                return NULL;
            }
            return buf;
        }
    }
    snprintf(err, (size_t)errlen, "No encuentro la ROM %s", names);
    return NULL;
}

static const uint8_t *gfx_chunks[0x300000 / 0x4000];
static const uint8_t *chunk_of(void *u, int i)
{
    (void)u;
    return (i >= 0 && i < (int)(sizeof gfx_chunks / sizeof gfx_chunks[0])) ? gfx_chunks[i] : NULL;
}

bool as1_load_roms(as1_game g, const char *dir, const char *bios_dir, as1_roms *roms, char *err, int errlen)
{
    memset(roms, 0, sizeof *roms);
    roms->game = g;
    const bool rr = g == AS1_ROADRUNN;
    const rom_entry *t_prog = rr ? rr_prog : prog, *t_sound = rr ? rr_sound : sound;
    const rom_entry *t_back = rr ? rr_back : back, *t_proms = rr ? rr_proms : proms;
    const size_t n_prog = rr ? N(rr_prog) : N(prog), n_sound = rr ? N(rr_sound) : N(sound);
    const size_t n_back = rr ? N(rr_back) : N(back), n_proms = rr ? N(rr_proms) : N(proms);
    uint8_t *main = (uint8_t *)calloc(1, AS1_MAIN_IMAGE);
    uint8_t *game = (uint8_t *)calloc(1, 0x30000);
    uint8_t *snd = (uint8_t *)calloc(1, 0xc000);
    uint8_t *pr = (uint8_t *)calloc(1, 0x400);
    if (!main || !game || !snd || !pr) { snprintf(err, (size_t)errlen, "sin memoria"); return false; }

    for (size_t i = 0; i < sizeof bios / sizeof bios[0]; i++) {
        uint8_t *d = load_one(dir, bios_dir, bios[i].names, bios[i].length, err, errlen);
        if (!d) return false;
        for (uint32_t k = 0; k < bios[i].length; k++) main[(bios[i].offset & ~1u) + 2 * k + (bios[i].offset & 1)] = d[k];
        free(d);
    }
    for (size_t i = 0; i < n_prog; i++) {
        const rom_entry *e = &t_prog[i];
        uint8_t *d = load_one(dir, bios_dir, e->names, e->length, err, errlen);
        if (!d) return false;
        uint8_t *to = rr ? main : game;       // Road Runner: absolute image offsets
        for (uint32_t k = 0; k < e->length; k++) to[(e->offset & ~1u) + 2 * k + (e->offset & 1)] = d[k];
        free(d);
    }
    if (!rr) {
        // Indy: program at $10000 (0x28000 bytes), the SLAPSTIC banks at $80000.
        memcpy(main + 0x10000, game, 0x28000);
        memcpy(main + 0x80000, game + 0x28000, 0x8000);
    }
    free(game);

    for (size_t i = 0; i < n_sound; i++) {
        uint8_t *d = load_one(dir, bios_dir, t_sound[i].names, t_sound[i].length, err, errlen);
        if (!d) return false;
        memcpy(snd + t_sound[i].offset - 0x4000, d, t_sound[i].length);
        free(d);
    }
    uint8_t *ch = load_one(dir, bios_dir, chars[0].names, chars[0].length, err, errlen);
    if (!ch) return false;
    for (size_t i = 0; i < n_proms; i++) {
        uint8_t *d = load_one(dir, bios_dir, t_proms[i].names, t_proms[i].length, err, errlen);
        if (!d) return false;
        memcpy(pr + t_proms[i].offset, d, t_proms[i].length);
        free(d);
    }
    memset(gfx_chunks, 0, sizeof gfx_chunks);
    for (size_t i = 0; i < n_back; i++) {
        uint8_t *d = load_one(dir, bios_dir, t_back[i].names, t_back[i].length, err, errlen);
        if (!d) return false;
        for (uint32_t o = 0; o < t_back[i].length; o += 0x4000) gfx_chunks[(t_back[i].offset + o) / 0x4000] = d + o;
    }
    for (int bank = 1; bank < AS1_GFX_BANKS && 0x80000u * (uint32_t)(bank - 1) < as1_gfx_bytes(g); bank++) {
        uint32_t *t = (uint32_t *)malloc(AS1_TILES_PER_BANK * 8 * sizeof(uint32_t));
        if (t && as1_pack_bank(chunk_of, NULL, bank, t)) roms->tiles[bank] = t;
        else free(t);
    }
    roms->main = main;
    roms->sound = snd;
    roms->chars = ch;
    roms->proms = pr;
    return true;
}
