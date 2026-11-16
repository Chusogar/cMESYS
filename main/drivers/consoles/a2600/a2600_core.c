// a2600_core.c - Atari 2600 / VCS (1977): a C11 migration of dsp-cpp's
// drivers/consoles/a2600. The 6507 (cpu/m6502/m65xx; 13 address lines, no
// interrupts), the TIA (video/tia: the picture and the two sound channels)
// and the 6532 RIOT (machine/mos6532: RAM, switches, joysticks, timer). No
// BIOS. Cartridges: 2K / 4K, Atari F8 / F6 / F4 (+ Superchip), F0, Parker
// Bros E0, M Network E7, Activision FE, Tigervision 3F (told apart as
// Stella's CartDetector does, as dsp-cpp). START is the console's RESET,
// SELECT its SELECT; buttons 3 / 4 flip the left / right difficulty.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../video/tia.h"
#include "../../../machine/mos6532.h"

#define CPU_CLOCK (3579545 / 3)
#define FB_H 192
#define MAX_CART (64 * 1024 + 256)

enum { MP_FLAT, MP_F8, MP_F6, MP_F4, MP_F0, MP_E0, MP_E7, MP_FE, MP_3F };

static const char *const kExts[] = { ".a26", ".bin", ".rom", NULL };
const cons_info cons_machine = {
    "a2600", "Atari 2600", "Atari, 1977", kExts, "A26 BIN", TIA_W, FB_H,
    (double)CPU_CLOCK / TIA_CPU_LINE / TIA_LINES, TIA_RATE, { "FIRE", NULL, "LEFT DIFF.", "RIGHT DIFF." },
    "RESET", "SELECT", CONS_NO_KEYS, 2, NULL,
};

static struct {
    m65xx cpu;
    tia tia;
    mos6532 riot;
    uint8_t *rom;
    size_t rom_size, rom_mask;
    int mapper, bank, bank_count;
    bool superchip;
    uint8_t superchip_ram[128];
    int e0_slice[4];
    int e7_ram_page;
    uint8_t e7_ram[2048];
    bool fe_pending, fe_jsr;
    struct { uint8_t reg, value; } pending[4];
    int npending;
    unsigned pads[2];
    uint8_t dips;
    int line_in_frame, first_visible, last_row;
    bool prev_vsync, frame_done;
    uint16_t fb[TIA_W * FB_H];
    int16_t audio[1600];
    int naudio;
    bool loaded;
} S;

// ---- cartridges ----
static bool contains(const uint8_t *d, size_t size, const uint8_t *pat, size_t len, int needed)
{
    int found = 0;
    for (size_t i = 0; i + len <= size; i++)
        if (!memcmp(d + i, pat, len) && ++found >= needed) return true;
    return false;
}

static bool any_of(const uint8_t *d, size_t size, const uint8_t *pats, size_t n, size_t len)
{
    for (size_t i = 0; i < n; i++)
        if (contains(d, size, pats + i * len, len, 1)) return true;
    return false;
}

static bool probably_superchip(const uint8_t *d, size_t size)
{
    if (size < 8192 || size % 4096) return false;
    for (size_t b = 0; b < size; b += 4096)
        if (memcmp(d + b, d + b + 128, 128)) return false;
    return true;
}

static bool probably_e0(const uint8_t *d, size_t size)
{
    static const uint8_t k[][3] = {
        {0x8D, 0xE0, 0x1F}, {0x8D, 0xE0, 0x5F}, {0x8D, 0xE9, 0xFF}, {0x0C, 0xE0, 0x1F},
        {0xAD, 0xE0, 0x1F}, {0xAD, 0xE9, 0xFF}, {0xAD, 0xED, 0xFF}, {0xAD, 0xF3, 0xBF},
    };
    return any_of(d, size, k[0], 8, 3);
}

static bool probably_e7(const uint8_t *d, size_t size)
{
    static const uint8_t k[][3] = {
        {0xAD, 0xE2, 0xFF}, {0xAD, 0xE5, 0xFF}, {0xAD, 0xE5, 0x1F}, {0xAD, 0xE7, 0x1F},
        {0x0C, 0xE7, 0x1F}, {0x8D, 0xE7, 0xFF}, {0x8D, 0xE7, 0x1F},
    };
    return any_of(d, size, k[0], 7, 3);
}

static bool probably_fe(const uint8_t *d, size_t size)
{
    static const uint8_t k[][5] = {
        {0x20, 0x00, 0xD0, 0xC6, 0xC5}, {0x20, 0xC3, 0xF8, 0xA5, 0x82},
        {0xD0, 0xFB, 0x20, 0x73, 0xFE}, {0x20, 0x00, 0xF0, 0x84, 0xD6},
    };
    return any_of(d, size, k[0], 4, 5);
}

static bool probably_3f(const uint8_t *d, size_t size)
{
    static const uint8_t k[] = { 0x85, 0x3F };
    return contains(d, size, k, 2, 2);
}

static void detect_mapper(const uint8_t *d, size_t size)
{
    S.mapper = MP_FLAT;
    if (size <= 4096) return;
    if (size <= 8192) {
        if (probably_superchip(d, size)) S.superchip = true, S.mapper = MP_F8;
        else if (probably_e0(d, size)) S.mapper = MP_E0;
        else if (probably_3f(d, size)) S.mapper = MP_3F;
        else if (probably_fe(d, size)) S.mapper = MP_FE;
        else S.mapper = MP_F8;
    } else if (size <= 16384) {
        if (probably_superchip(d, size)) S.superchip = true, S.mapper = MP_F6;
        else if (probably_e7(d, size)) S.mapper = MP_E7;
        else if (probably_3f(d, size)) S.mapper = MP_3F;
        else S.mapper = MP_F6;
    } else if (size <= 32768) {
        if (probably_superchip(d, size)) S.superchip = true, S.mapper = MP_F4;
        else if (probably_3f(d, size)) S.mapper = MP_3F;
        else S.mapper = MP_F4;
    } else {
        S.mapper = probably_3f(d, size) ? MP_3F : MP_F0;
    }
}

static void touch_hotspot(uint16_t o)
{
    int next = S.bank;
    switch (S.mapper) {
    case MP_F8:
        if (o == 0xff8) next = 0;
        else if (o == 0xff9) next = 1;
        break;
    case MP_F6: if (o >= 0xff6 && o <= 0xff9) next = o - 0xff6; break;
    case MP_F4: if (o >= 0xff4 && o <= 0xffb) next = o - 0xff4; break;
    case MP_F0: if (o == 0xff0) next = (S.bank + 1) % S.bank_count; break;
    case MP_E0: if (o >= 0xfe0 && o <= 0xff7) S.e0_slice[(o - 0xfe0) >> 3] = o & 7; break;
    case MP_E7:
        if (o >= 0xfe0 && o <= 0xfe7) next = o & 7;
        else if (o >= 0xfe8 && o <= 0xfeb) S.e7_ram_page = o & 3;
        break;
    default: break;
    }
    if (next >= 0 && next < S.bank_count) S.bank = next;
}

static uint8_t read_cart(uint16_t o)
{
    o &= 0x0fff;
    touch_hotspot(o);
    switch (S.mapper) {
    case MP_FLAT: return S.rom[o & S.rom_mask];
    case MP_E0: return S.rom[(((size_t)S.e0_slice[o >> 10] << 10) | (o & 0x3ff)) & S.rom_mask];
    case MP_E7:
        if (o < 0x800) {
            if (S.bank == 7) return o >= 0x400 ? S.e7_ram[o & 0x3ff] : 0xff;
            return S.rom[(((size_t)S.bank << 11) | o) & S.rom_mask];
        }
        if (o < 0xa00) return o >= 0x900 ? S.e7_ram[0x400 + S.e7_ram_page * 256 + (o & 0xff)] : 0xff;
        return S.rom[((7u << 11) | (o & 0x7ff)) & S.rom_mask];
    case MP_3F:
        if (o < 0x800) return S.rom[(((size_t)S.bank << 11) | o) & S.rom_mask];
        return S.rom[(S.rom_size - 0x800 + (o & 0x7ff)) & S.rom_mask];
    default: break;
    }
    if (S.superchip) {
        if (o < 0x80) return 0xff;
        if (o < 0x100) return S.superchip_ram[o - 0x80];
    }
    return S.rom[((size_t)S.bank * 0x1000 + o) & S.rom_mask];
}

static void write_cart(uint16_t o, uint8_t v)
{
    o &= 0x0fff;
    touch_hotspot(o);
    if (S.mapper == MP_E7) {
        if (o < 0x400 && S.bank == 7) S.e7_ram[o] = v;
        else if (o >= 0x800 && o < 0x900) S.e7_ram[0x400 + S.e7_ram_page * 256 + (o & 0xff)] = v;
        return;
    }
    if (S.superchip && o < 0x80) S.superchip_ram[o] = v;
}

// ---- the machine ----
static void end_line(void)
{
    tia *t = &S.tia;
    tia_clock_audio(t);
    tia_emit_audio(t, TIA_CPU_LINE / 2, CPU_CLOCK, S.audio, &S.naudio, 1600);
    tia_clock_audio(t);
    tia_emit_audio(t, TIA_CPU_LINE / 2, CPU_CLOCK, S.audio, &S.naudio, 1600);
    const bool vsync = t->vsync;
    if (!tia_blanked(t) && S.first_visible < 0) S.first_visible = S.line_in_frame;
    if (S.first_visible >= 0) {
        const int row = S.line_in_frame - S.first_visible;
        if (row >= 0 && row < FB_H) {
            memcpy(S.fb + row * TIA_W, t->line, sizeof t->line);
            S.last_row = row;
        }
    }
    S.line_in_frame++;
    tia_begin_line(t);
    if ((vsync && !S.prev_vsync && S.line_in_frame > 1) || S.line_in_frame >= 320) {
        for (int row = S.last_row + 1; row < FB_H; row++) memset(S.fb + row * TIA_W, 0, TIA_W * 2);
        S.line_in_frame = 0;
        S.first_visible = S.last_row = -1;
        S.frame_done = true;
    }
    S.prev_vsync = vsync;
}

static void advance(int clocks)
{
    while (clocks > 0) {
        int step = TIA_CLOCKS - S.tia.hclock;
        if (step > clocks) step = clocks;
        tia_run(&S.tia, step);
        clocks -= step;
        if (tia_line_done(&S.tia)) end_line();
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    mos6532_tick(&S.riot, c);
    if (S.npending == 0) {
        advance(c * 3);
        return;
    }
    advance((c - 1) * 3);
    bool wsync = false;
    for (int i = 0; i < S.npending; i++)
        if (S.pending[i].reg == 0x2a) tia_write(&S.tia, 0x2a, S.pending[i].value);
    advance(3);
    for (int i = 0; i < S.npending; i++) {
        const uint8_t reg = S.pending[i].reg;
        if (reg == 0x02) wsync = true;
        else if (reg == 0x03) end_line();
        else if (reg != 0x2a) tia_write(&S.tia, reg, S.pending[i].value);
    }
    S.npending = 0;
    if (wsync && S.tia.hclock != 0) {
        const int wait = TIA_CLOCKS - S.tia.hclock;
        advance(wait);
        mos6532_tick(&S.riot, wait / 3);
    }
}

static uint8_t rd(void *u, uint16_t full)
{
    (void)u;
    const uint16_t a = full & 0x1fff;
    uint8_t v;
    if (a & 0x1000) v = read_cart(a);
    else if (a & 0x80) v = (a & 0x200) ? mos6532_io_read(&S.riot, (uint8_t)(a & 0x1f)) : mos6532_ram_read(&S.riot, (uint8_t)(a & 0x7f));
    else v = tia_read(&S.tia, (uint8_t)(a & 0x0f));
    if (S.mapper == MP_FE && S.fe_jsr) {
        S.fe_jsr = false;
        S.bank = (full & 0x2000) ? 0 : 1;
        if (a & 0x1000) v = read_cart(a);
    } else if (S.mapper == MP_FE) {
        if (S.fe_pending) {
            S.bank = (v & 0x20) ? 0 : 1;
            S.fe_pending = false;
        } else if (full == 0x01fe) {
            S.fe_pending = true;
        }
    }
    return v;
}

static void wr(void *u, uint16_t full, uint8_t v)
{
    (void)u;
    const uint16_t a = full & 0x1fff;
    if (S.mapper == MP_FE) {
        S.fe_pending = false;
        S.fe_jsr = full == 0x01fe;
    }
    if (a & 0x1000) {
        write_cart(a, v);
        return;
    }
    if (a & 0x80) {
        if (a & 0x200) mos6532_io_write(&S.riot, (uint8_t)(a & 0x1f), v);
        else mos6532_ram_write(&S.riot, (uint8_t)(a & 0x7f), v);
        return;
    }
    if (S.mapper == MP_3F && (a & 0x1fc0) == 0) S.bank = v % (S.bank_count > 1 ? S.bank_count : 1);
    if (S.npending < 4) {
        S.pending[S.npending].reg = (uint8_t)(a & 0x3f);
        S.pending[S.npending].value = v;
        S.npending++;
    } else {
        tia_write(&S.tia, (uint8_t)(a & 0x3f), v);
    }
}

static uint8_t swcha(void *u)
{
    (void)u;
    uint8_t v = 0xff;
    for (int p = 0; p < 2; p++) {
        const int sh = p ? 0 : 4;
        const unsigned b = S.pads[p];
        if (b & CP_UP) v &= (uint8_t)~(1u << sh);
        if (b & CP_DOWN) v &= (uint8_t)~(1u << (sh + 1));
        if (b & CP_LEFT) v &= (uint8_t)~(1u << (sh + 2));
        if (b & CP_RIGHT) v &= (uint8_t)~(1u << (sh + 3));
    }
    return v;
}

static uint8_t swchb(void *u)
{
    (void)u;
    uint8_t v = 0x34;
    if (!(S.pads[0] & CP_START)) v |= 0x01;
    if (!(S.pads[0] & (CP_SELECT | CP_COIN))) v |= 0x02;
    v |= S.dips & 0xc8;
    return v;
}

bool cons_init(void)
{
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.cycles = cycles;
    S.riot.pa_in = swcha;
    S.riot.pb_in = swchb;
    S.dips = 0x08;
    S.rom = (uint8_t *)malloc(2048);
    if (!S.rom) return false;
    memset(S.rom, 0xff, 2048);
    S.rom_size = 2048;
    S.rom_mask = 2047;
    S.bank_count = 1;
    tia_reset(&S.tia);
    mos6532_reset(&S.riot);
    return true;
}

void cons_reset(void)
{
    tia_reset(&S.tia);
    mos6532_reset(&S.riot);
    S.npending = 0;
    S.line_in_frame = 0;
    S.first_visible = S.last_row = -1;
    S.prev_vsync = false;
    memset(S.fb, 0, sizeof S.fb);
    switch (S.mapper) {
    case MP_E0: for (int i = 0; i < 4; i++) S.e0_slice[i] = 4 + i; break;
    case MP_E7: S.bank = 0; S.e7_ram_page = 0; break;
    case MP_FE: S.bank = 0; S.fe_pending = false; break;
    case MP_3F: S.bank = 0; break;
    default: S.bank = S.bank_count - 1; break;
    }
    S.cpu.halted = false;
    if (S.loaded) m65xx_reset(&S.cpu);
}

static bool name_says_superchip(const char *name)
{
    char l[256];
    size_t i = 0;
    for (; name && name[i] && i < sizeof l - 1; i++) l[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    l[i] = 0;
    return strstr(l, "f8sc") || strstr(l, "f6sc") || strstr(l, "f4sc") || strstr(l, ".sc.");
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    if (!data || !size || size > MAX_CART) {
        if (why) *why = "cartridge is empty or larger than 64 KB";
        free(data);
        return false;
    }
    S.superchip = false;
    if (size > 128 && size % 1024 == 128) {
        S.superchip = true;
        size -= 128;
    }
    size_t padded = 1;
    while (padded < size) padded <<= 1;
    if (padded < 2048) padded = 2048;
    uint8_t *rom = (uint8_t *)malloc(padded);
    if (!rom) {
        if (why) *why = "out of memory";
        free(data);
        return false;
    }
    memcpy(rom, data, size);
    for (size_t i = size; i < padded; i++) rom[i] = rom[i % size];
    free(data);
    free(S.rom);
    S.rom = rom;
    S.rom_size = padded;
    S.rom_mask = padded - 1;
    detect_mapper(rom, padded);
    switch (S.mapper) {
    case MP_FLAT: S.bank_count = 1; break;
    case MP_E0: case MP_E7: S.bank_count = 8; break;
    case MP_3F: S.bank_count = (int)(padded / 2048); break;
    default: S.bank_count = padded / 4096 > 1 ? (int)(padded / 4096) : 1; break;
    }
    memset(S.superchip_ram, 0, sizeof S.superchip_ram);
    memset(S.e7_ram, 0, sizeof S.e7_ram);
    if (name_says_superchip(name)) S.superchip = true;
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    S.loaded = false;
    memset(S.rom, 0xff, S.rom_size);
    S.mapper = MP_FLAT;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    if (!S.loaded) {
        for (int i = 0; i < 735; i++) S.audio[i] = 0;
        return 735;
    }
    S.frame_done = false;
    const int limit = TIA_CPU_LINE * 330;
    int executed = 0;
    while (!S.frame_done && executed < limit) executed += m65xx_run(&S.cpu, 1);
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = TIA_W;
    *h = FB_H;
    *stride = TIA_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    const unsigned old = S.pads[player & 1];
    S.pads[player & 1] = b;
    if (player == 0) {
        if ((b & CP_B3) && !(old & CP_B3)) S.dips ^= 0x40;        // left difficulty
        if ((b & CP_B4) && !(old & CP_B4)) S.dips ^= 0x80;        // right difficulty
        tia_set_inpt4(&S.tia, b & CP_B1);
    } else {
        tia_set_inpt5(&S.tia, b & CP_B1);
    }
}

void cons_key(int key, bool down) { (void)key, (void)down; }

const char *cons_status(void)
{
    static const char *const names[] = { "4K", "F8", "F6", "F4", "F0", "E0", "E7", "FE", "3F" };
    static char s[32];
    if (!S.loaded) return NULL;
    snprintf(s, sizeof s, "%s%s  %c/%c", names[S.mapper], S.superchip ? "SC" : "",
             (S.dips & 0x40) ? 'A' : 'B', (S.dips & 0x80) ? 'A' : 'B');
    return s;
}
