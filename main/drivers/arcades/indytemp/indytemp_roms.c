// indytemp_roms.c - ROM loading for the Indiana Jones driver (host builds).
#include "indytemp_roms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *names; uint32_t length, offset; } rom_entry;

// From dsp-cpp's atari_system1.cpp: offsets are byte addresses of the
// image, the low bit choosing the high (even) or low (odd) byte of a word.
static const rom_entry bios[] = {
    {"136032.205.l13|136032.205", 0x4000, 0x0}, {"136032.206.l12|136032.206", 0x4000, 0x1}};
static const rom_entry chars[] = {{"136032.104.f5|136032.104|136032.107.b2", 0x2000, 0}};
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

static const uint8_t *gfx_chunks[0x200000 / 0x4000];
static const uint8_t *chunk_of(void *u, int i)
{
    (void)u;
    return (i >= 0 && i < (int)(sizeof gfx_chunks / sizeof gfx_chunks[0])) ? gfx_chunks[i] : NULL;
}

bool indy_load_roms(const char *dir, const char *bios_dir, indy_roms *roms, char *err, int errlen)
{
    memset(roms, 0, sizeof *roms);
    uint8_t *main = (uint8_t *)calloc(1, INDY_MAIN_IMAGE);
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
    for (size_t i = 0; i < sizeof prog / sizeof prog[0]; i++) {
        uint8_t *d = load_one(dir, bios_dir, prog[i].names, prog[i].length, err, errlen);
        if (!d) return false;
        for (uint32_t k = 0; k < prog[i].length; k++) game[(prog[i].offset & ~1u) + 2 * k + (prog[i].offset & 1)] = d[k];
        free(d);
    }
    // Program at $10000 (0x28000 bytes), the SLAPSTIC banks at $80000.
    memcpy(main + 0x10000, game, 0x28000);
    memcpy(main + 0x80000, game + 0x28000, 0x8000);
    free(game);

    for (size_t i = 0; i < sizeof sound / sizeof sound[0]; i++) {
        uint8_t *d = load_one(dir, bios_dir, sound[i].names, sound[i].length, err, errlen);
        if (!d) return false;
        memcpy(snd + sound[i].offset - 0x4000, d, sound[i].length);
        free(d);
    }
    uint8_t *ch = load_one(dir, bios_dir, chars[0].names, chars[0].length, err, errlen);
    if (!ch) return false;
    for (size_t i = 0; i < sizeof proms / sizeof proms[0]; i++) {
        uint8_t *d = load_one(dir, bios_dir, proms[i].names, proms[i].length, err, errlen);
        if (!d) return false;
        memcpy(pr + proms[i].offset, d, proms[i].length);
        free(d);
    }
    memset(gfx_chunks, 0, sizeof gfx_chunks);
    for (size_t i = 0; i < sizeof back / sizeof back[0]; i++) {
        uint8_t *d = load_one(dir, bios_dir, back[i].names, back[i].length, err, errlen);
        if (!d) return false;
        for (uint32_t o = 0; o < back[i].length; o += 0x4000) gfx_chunks[(back[i].offset + o) / 0x4000] = d + o;
    }
    for (int bank = 1; bank < INDY_GFX_BANKS; bank++) {
        uint32_t *t = (uint32_t *)malloc(INDY_TILES_PER_BANK * 8 * sizeof(uint32_t));
        if (t && indy_pack_bank(chunk_of, NULL, bank, t)) roms->tiles[bank] = t;
        else free(t);
    }
    roms->main = main;
    roms->sound = snd;
    roms->chars = ch;
    roms->proms = pr;
    return true;
}
