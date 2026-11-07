// msx_cart.c - see msx_cart.h
#include "msx_cart.h"

#include <ctype.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Konami SCC: five channels of 32-byte waveforms (4 and 5 share one)
// ---------------------------------------------------------------------------
static uint8_t scc_read(const konami_scc *s, uint8_t reg)
{
    if (reg < 0x80) return (uint8_t)s->wave[reg >> 5][reg & 31];
    if (reg >= 0xa0 && reg < 0xc0) return (uint8_t)s->wave[3][reg & 31];
    return 0xff;
}

static void scc_write(konami_scc *s, uint8_t reg, uint8_t v)
{
    if (reg < 0x80) {
        s->wave[reg >> 5][reg & 31] = (int8_t)v;
        return;
    }
    if (reg >= 0xa0) return;
    reg &= 0x8f;
    if (reg < 0x8a) {
        const int ch = (reg - 0x80) >> 1;
        if (reg & 1) s->period[ch] = (uint16_t)((s->period[ch] & 0x0ff) | ((v & 0x0f) << 8));
        else s->period[ch] = (uint16_t)((s->period[ch] & 0xf00) | v);
    } else if (reg < 0x8f) {
        s->volume[reg - 0x8a] = v & 0x0f;
    } else {
        s->enable = v & 0x1f;
    }
}

static int32_t scc_update(konami_scc *s, uint32_t clock, int rate)
{
    int32_t out = 0;
    for (int ch = 0; ch < 5; ch++) {
        const uint32_t period = s->period[ch];
        if (period < 9) continue;
        const uint64_t step = ((uint64_t)clock << 16) / ((uint64_t)(period + 1) * (uint64_t)rate);
        s->phase[ch] = (uint32_t)((s->phase[ch] + step) & ((32u << 16) - 1));
        if (!((s->enable >> ch) & 1)) continue;
        out += (int32_t)s->wave[ch < 4 ? ch : 3][s->phase[ch] >> 16] * s->volume[ch];
    }
    return out;
}

// ---------------------------------------------------------------------------
// the mapper
// ---------------------------------------------------------------------------
void msxc_init(msx_cart *c)
{
    memset(c, 0, sizeof *c);
    zx_pb_init(&c->rom);
}

void msxc_free(msx_cart *c)
{
    zx_pb_free(&c->rom);
    msxc_init(c);
}

void msxc_reset(msx_cart *c)
{
    c->bank[0] = 0;
    c->bank[1] = 1;
    c->bank[2] = 2;
    c->bank[3] = 3;
    c->write_enable = false;
    memset(&c->scc, 0, sizeof c->scc);
}

static bool has(const char *lower, const char *s) { return strstr(lower, s) != NULL; }

static int detect(const msx_cart *c, const char *filename)
{
    char f[256];
    size_t n = 0;
    for (; filename && filename[n] && n < sizeof f - 1; n++) f[n] = (char)tolower((unsigned char)filename[n]);
    f[n] = 0;
    if (has(f, "asc-08") || has(f, "ascii8")) return MSXC_ASCII8;
    if (has(f, "asc-16") || has(f, "ascii16")) return MSXC_ASCII16;
    if (has(f, "konscc") || has(f, "konami-scc") || has(f, "konamiscc") || has(f, "konami5")) return MSXC_KONAMI_SCC;
    if (has(f, "konami4") || has(f, "konami-4")) return MSXC_KONAMI;
    if (has(f, "generic8")) return MSXC_GENERIC8;
    if (has(f, "generic16")) return MSXC_GENERIC16;
    if (has(f, "rtype")) return MSXC_RTYPE;
    if (has(f, "cross")) return MSXC_CROSS_BLAIM;
    if (has(f, "harry")) return MSXC_HARRY_FOX;
    if (has(f, "gamemaster")) return MSXC_GAME_MASTER2;
    if (has(f, "quran")) return MSXC_HOLY_QURAN;
    if (has(f, "loderunner")) return MSXC_SUPER_LODE_RUNNER;
    if (has(f, "pierrot")) return MSXC_SUPER_PIERROT;
    if (has(f, "zemina8")) return MSXC_ZEMINA8;
    if (has(f, "zemina16") || has(f, "zemina126")) return MSXC_ZEMINA16;
    if (has(f, "halnote")) return MSXC_HALNOTE;
    if (has(f, "eseram8")) return MSXC_ESERAM8;
    if (has(f, "eseram16")) return MSXC_ESERAM16;
    if (has(f, "megaram") || has(f, "ddx")) return MSXC_MEGARAM_DDX;
    if (has(f, "manbow")) return MSXC_MANBOW2;

    const zx_pbuf *d = &c->rom;
    if (d->size <= 0x8000 || d->size == 0xc000) return MSXC_PLAIN;
    if (d->size == 0x10000 && zx_pb_eq(d, 0x4000, "AB", 2) && !zx_pb_eq(d, 0, "AB", 2)) return MSXC_PLAIN;
    if (d->size == 0x60000 && zx_pb_eq(d, 0x28000, "MANBOW2", 7)) return MSXC_MANBOW2;
    unsigned ascii8 = 0, ascii16 = 0, konami = 0, scc = 0, generic16 = 0;
    for (uint32_t i = 0; i + 2 < d->size; i++) {
        if (zx_pb_byte(d, i) != 0x32) continue;
        switch (zx_pb_rd16(d, i + 1)) {
        case 0x5000: case 0x9000: case 0xb000: scc++; break;
        case 0x4000: case 0x8000: case 0xa000: konami++; generic16++; break;
        case 0x6800: case 0x7800: ascii8++; break;
        case 0x6000: ascii8++; ascii16++; konami++; break;
        case 0x7000: ascii8++; ascii16++; scc++; break;
        case 0x77ff: ascii16++; break;
        default: break;
        }
    }
    if (scc > ascii8 && scc >= ascii16 && scc >= konami) return MSXC_KONAMI_SCC;
    if (ascii8 > ascii16 && ascii8 >= konami) return MSXC_ASCII8;
    if (ascii16 > konami && ascii16 >= ascii8) return MSXC_ASCII16;
    if (konami) return MSXC_KONAMI;
    if (generic16) return MSXC_GENERIC16;
    return d->size > 0x10000 ? MSXC_GENERIC8 : MSXC_PLAIN;
}

static void plain_layout(msx_cart *c);

bool msxc_load(msx_cart *c, zx_pbuf *rom, const char *name)
{
    zx_pb_free(&c->rom);
    c->rom = *rom;
    zx_pb_init(rom);
    c->type = detect(c, name);
    if (c->type == MSXC_PLAIN) plain_layout(c);
    msxc_reset(c);
    return c->rom.size != 0;
}

static inline uint8_t rom8(const msx_cart *c, uint32_t bank, uint16_t off)
{
    uint32_t count = c->rom.size / 0x2000;
    if (!count) count = 1;
    return zx_pb_byte(&c->rom, (bank % count) * 0x2000 + (off & 0x1fff));
}

static inline uint8_t rom16(const msx_cart *c, uint32_t bank, uint16_t off)
{
    uint32_t count = c->rom.size / 0x4000;
    if (!count) count = 1;
    return zx_pb_byte(&c->rom, (bank % count) * 0x4000 + (off & 0x3fff));
}

// A plain ROM: from $4000 (the "AB" header first), from $8000 (a 16 KB one
// whose INIT is there: BASIC ROMs), or from $0000 (48 / 64 KB ones with the
// header at $4000). 8 and 16 KB ones are mirrored over $4000-$BFFF.
static void plain_layout(msx_cart *c)
{
    const zx_pbuf *d = &c->rom;
    c->plain_base = 0x4000;
    if (d->size > 0x8000 && zx_pb_eq(d, 0x4000, "AB", 2) && !zx_pb_eq(d, 0, "AB", 2)) c->plain_base = 0;
    else if (d->size <= 0x4000) {
        const uint16_t init = zx_pb_rd16(d, 2), text = zx_pb_rd16(d, 8);
        if ((init >= 0x8000 && init < 0xc000) || (!init && text >= 0x8000 && text < 0xc000)) c->plain_base = 0x8000;
    }
}

static uint8_t plain(const msx_cart *c, uint16_t a)
{
    const uint32_t size = c->rom.size;
    if (size <= 0x4000) {
        if (a < 0x4000 || a >= 0xc000) return 0xff;
        const uint32_t p = (uint32_t)(a & 0x3fff) % (size ? size : 1);
        return zx_pb_byte(&c->rom, p);
    }
    if (a < c->plain_base) return 0xff;
    const uint32_t p = (uint32_t)(a - c->plain_base);
    return p < size ? zx_pb_byte(&c->rom, p) : 0xff;
}

static uint8_t bank8(const msx_cart *c, uint16_t a)
{
    if (a < 0x4000 || a >= 0xc000) return 0xff;
    if (c->type == MSXC_HOLY_QURAN) {
        const int s = a < 0x6000 ? (a < 0x5400 ? 0 : 1) : (a < 0x8000 ? 1 : (a < 0xa000 ? 2 : 3));
        return rom8(c, c->bank[s], a);
    }
    return rom8(c, c->bank[(a - 0x4000) >> 13], a);
}

static uint8_t bank16(const msx_cart *c, uint16_t a)
{
    if (a >= 0x4000 && a < 0x8000) return rom16(c, c->bank[0], a);
    if (a >= 0x8000 && a < 0xc000) return rom16(c, c->bank[1], a);
    return 0xff;
}

static bool scc_on(const msx_cart *c) { return (c->bank[2] & 0x3f) == 0x3f; }

uint8_t msxc_read(msx_cart *c, uint16_t a)
{
    if (!c->rom.size) return 0xff;
    switch (c->type) {
    case MSXC_PLAIN: return plain(c, a);
    case MSXC_ASCII8: case MSXC_ESERAM8: case MSXC_KOEI8:
        if (a < 0x4000 || a >= 0xc000) return 0xff;
        return rom8(c, c->bank[(a - 0x4000) >> 13], a);
    case MSXC_ASCII16: case MSXC_ESERAM16: case MSXC_RTYPE: case MSXC_HARRY_FOX: case MSXC_SUPER_PIERROT:
    case MSXC_ZEMINA16: case MSXC_SGW126: case MSXC_GENERIC16:
        return bank16(c, a);
    case MSXC_KONAMI_SCC:
        if (scc_on(c) && a >= 0x9800 && a < 0xa000) return scc_read(&c->scc, (uint8_t)a);
        return bank8(c, a);
    case MSXC_CROSS_BLAIM:
        if (a >= 0x4000 && a < 0x8000) return rom16(c, 0, a);
        if (a >= 0x8000 && a < 0xc000) return rom16(c, c->bank[1], a);
        return 0xff;
    case MSXC_SUPER_LODE_RUNNER:
        return a >= 0x8000 && a < 0xc000 ? rom16(c, c->bank[0], a) : 0xff;
    default:        // Konami, Generic8, Zemina8, Holy Quran, Game Master 2, Halnote, MegaRAM, Manbow 2
        return bank8(c, a);
    }
}

void msxc_write(msx_cart *c, uint16_t a, uint8_t v)
{
    if (!c->rom.size) return;
    uint8_t *b = c->bank;
    switch (c->type) {
    case MSXC_ASCII8: case MSXC_ESERAM8: case MSXC_KOEI8:
        if (c->type == MSXC_ESERAM8 && (a == 0x7ffe || a == 0x7fff)) { c->write_enable = v & 0x10; return; }
        if (a >= 0x6000 && a < 0x8000) b[(a - 0x6000) >> 11] = v;
        break;
    case MSXC_ASCII16: case MSXC_ESERAM16:
        if (c->type == MSXC_ESERAM16 && (a == 0x7ffe || a == 0x7fff)) { c->write_enable = v & 0x10; return; }
        if (a >= 0x6000 && a < 0x6800) b[0] = v;
        else if (a >= 0x7000 && a < 0x7800) b[1] = v;
        break;
    case MSXC_KONAMI:       // $4000-$5FFF fixed to bank 0
        if (a >= 0x6000 && a < 0x8000) b[1] = v;
        else if (a >= 0x8000 && a < 0xa000) b[2] = v;
        else if (a >= 0xa000 && a < 0xc000) b[3] = v;
        break;
    case MSXC_KONAMI_SCC:
        if (a >= 0x5000 && a < 0x5800) b[0] = v;
        else if (a >= 0x7000 && a < 0x7800) b[1] = v;
        else if (a >= 0x9000 && a < 0x9800) b[2] = v;
        else if (a >= 0xb000 && a < 0xb800) b[3] = v;
        if (scc_on(c) && a >= 0x9800 && a < 0xa000) scc_write(&c->scc, (uint8_t)a, v);
        break;
    case MSXC_GENERIC8: case MSXC_ZEMINA8:
        if (a >= 0x4000 && a < 0xc000) b[(a - 0x4000) >> 13] = v;
        break;
    case MSXC_GENERIC16: case MSXC_ZEMINA16: case MSXC_SGW126:
        if (a >= 0x4000 && a < 0x8000) b[0] = v;
        else if (a >= 0x8000 && a < 0xc000) b[1] = v;
        break;
    case MSXC_RTYPE:
        if (a >= 0x6000 && a < 0x6800) b[0] = v;
        else if (a >= 0x7000 && a < 0x7800) b[1] = v;
        break;
    case MSXC_HARRY_FOX:
        if (a >= 0x6000 && a < 0x7000) b[0] = v;
        else if (a >= 0x7000 && a < 0x8000) b[1] = v;
        break;
    case MSXC_SUPER_PIERROT:
        if (!(a & 0x1000) && a >= 0x4000) b[0] = v;
        else if ((a & 0x1000) && a >= 0x5000 && a < 0xc000) b[1] = v;
        break;
    case MSXC_HOLY_QURAN:
        if (a >= 0x5000 && a < 0x6000) b[(a - 0x5000) >> 10] = v;
        break;
    case MSXC_GAME_MASTER2:
        if (a >= 0x6000 && a < 0x6800) b[1] = v & 0x0f;
        else if (a >= 0x8000 && a < 0x8800) b[2] = v & 0x0f;
        else if (a >= 0xa000 && a < 0xa800) b[3] = v & 0x0f;
        else if (a >= 0x7000 && a < 0x7800) c->write_enable = v & 0x10;
        break;
    case MSXC_CROSS_BLAIM:
        if (a == 0x4045) b[1] = v;
        break;
    case MSXC_SUPER_LODE_RUNNER:
        if (a < 0x4000) b[0] = v;
        break;
    case MSXC_HALNOTE:
        if (a == 0x4fff) { b[0] = v & 0x7f; c->write_enable = v & 0x80; }
        else if (a == 0x6fff) b[1] = v & 0x7f;
        else if (a == 0x8fff) b[2] = v & 0x7f;
        else if (a == 0xafff) b[3] = v & 0x7f;
        break;
    case MSXC_MEGARAM_DDX:
        if (a == 0x6000) { b[0] = v; c->write_enable = true; }
        else if (a == 0x7000) b[1] = v;
        break;
    case MSXC_MANBOW2:
        if (a >= 0x5000 && a < 0x7000) b[(a - 0x5000) >> 11] = v;
        break;
    default: break;
    }
}

int32_t msxc_sample(msx_cart *c, uint32_t clock, int rate)
{
    return c->type == MSXC_KONAMI_SCC ? scc_update(&c->scc, clock, rate) : 0;
}

const char *msxc_name(const msx_cart *c)
{
    static const char *const kNames[] = {
        "Plain", "ASCII8", "ASCII16", "Konami", "Konami SCC", "Generic8", "Generic16", "R-Type", "Cross Blaim",
        "Harry Fox", "Game Master 2", "Holy Quran", "Super Lode Runner", "Super Pierrot", "Zemina8", "Zemina16",
        "Super Game World 126", "Koei8", "Halnote", "ESE-RAM ASCII8", "ESE-RAM ASCII16", "MegaRAM DDX", "Manbow2",
    };
    return c->type >= 0 && c->type < (int)(sizeof kNames / sizeof kNames[0]) ? kNames[c->type] : "Unknown";
}
