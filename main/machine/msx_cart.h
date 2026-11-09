// msx_cart.h - MSX ROM cartridges, C11 port of dsp-cpp's machine/msx_mapper
// (MsxCartridgeMapper) with its Konami SCC (sound/konami_scc): plain 8-64 KB
// ROMs and the megaROM mappers (ASCII 8/16, Konami, Konami SCC, R-Type,
// Cross Blaim, Harry Fox, Game Master 2, Holy Quran, Super Lode Runner,
// Super Pierrot, Zemina, Koei, Halnote, ESE-RAM, MegaRAM DDX, Manbow 2),
// told apart as openMSX does: tags in the file name, then the LD (nn),A
// writes to the mapper's addresses counted in the code. The ROM is a
// zx_pbuf (pages, or on the card on the boards).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "zx_pbuf.h"

enum {
    MSXC_PLAIN, MSXC_ASCII8, MSXC_ASCII16, MSXC_KONAMI, MSXC_KONAMI_SCC, MSXC_GENERIC8, MSXC_GENERIC16,
    MSXC_RTYPE, MSXC_CROSS_BLAIM, MSXC_HARRY_FOX, MSXC_GAME_MASTER2, MSXC_HOLY_QURAN, MSXC_SUPER_LODE_RUNNER,
    MSXC_SUPER_PIERROT, MSXC_ZEMINA8, MSXC_ZEMINA16, MSXC_SGW126, MSXC_KOEI8, MSXC_HALNOTE, MSXC_ESERAM8,
    MSXC_ESERAM16, MSXC_MEGARAM_DDX, MSXC_MANBOW2,
};

typedef struct {
    int8_t wave[4][32];
    uint16_t period[5];
    uint8_t volume[5], enable;
    uint32_t phase[5];
} konami_scc;

typedef struct {
    zx_pbuf rom;
    int type;
    uint8_t bank[4];
    uint16_t plain_base;        // a plain ROM's first address ($0000, $4000 or $8000)
    bool write_enable;
    konami_scc scc;
} msx_cart;

void msxc_init(msx_cart *c);
void msxc_free(msx_cart *c);
// takes the pages over; the mapper from `name` and the code
bool msxc_load(msx_cart *c, zx_pbuf *rom, const char *name);
void msxc_reset(msx_cart *c);
static inline bool msxc_present(const msx_cart *c) { return c->rom.size != 0; }
uint8_t msxc_read(msx_cart *c, uint16_t a);
void msxc_write(msx_cart *c, uint16_t a, uint8_t v);
int32_t msxc_sample(msx_cart *c, uint32_t clock, int rate);    // the SCC (0: none)
const char *msxc_name(const msx_cart *c);
