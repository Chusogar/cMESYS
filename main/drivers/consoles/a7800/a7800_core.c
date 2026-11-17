// a7800_core.c - Atari 7800 ProSystem (1986): a C11 migration of dsp-cpp's
// drivers/consoles/a7800. SALLY (cpu/m6502/m65xx), MARIA (video/maria:
// display lists, holding the CPU while it fetches), the 2600's TIA for the
// sound (video/tia) and the RIOT's timer and joysticks. No BIOS: the
// cartridge starts at once. Plain carts against the top of the address
// space; SuperGame ones (16 KB banks at $8000, the last fixed at $C000).
// .a78 files (the 128-byte header) or raw dumps.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/cons.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../video/maria.h"
#include "../../../video/tia.h"

#define CPU_CLOCK 1789773
#define LINE_CYCLES 114
#define FB_H 240
#define RATE 44100

static const char *const kExts[] = { ".a78", ".bin", NULL };
const cons_info cons_machine = {
    "a7800", "Atari 7800", "Atari, 1986", kExts, "A78 BIN", MARIA_W, FB_H, 59.92, RATE,
    { "FIRE", NULL, NULL, NULL }, "RESET", "SELECT", CONS_NO_KEYS, 2, NULL,
};

static struct {
    m65xx cpu;
    maria maria;
    tia tia;
    uint8_t *cart;
    size_t cart_size;
    int cart_banks;
    bool supergame, bank6_at_4000, pal;
    uint8_t bank;
    uint8_t ram[0x1000];
    uint8_t riot_ram[0x80];
    uint32_t riot_timer;
    int riot_shift;
    bool riot_irq;
    uint8_t swcha, swchb, inpt[6];
    uint16_t pal565[256];
    uint16_t *fb;
    int debt;
    int16_t audio[1100];
    int naudio;
    bool loaded;
} S;

static uint8_t cart_read(uint16_t a)
{
    if (!S.cart_size) return 0xff;
    if (S.supergame) {
        if (a >= 0xc000) return S.cart[(size_t)(S.cart_banks - 1) * 0x4000 + (a - 0xc000)];
        if (a >= 0x8000) return S.cart[(size_t)(S.bank % (S.cart_banks > 1 ? S.cart_banks : 1)) * 0x4000 + (a - 0x8000)];
        if (S.bank6_at_4000 && S.cart_banks > 6) return S.cart[6 * 0x4000 + (a - 0x4000)];
        return 0xff;
    }
    const uint32_t start = (uint32_t)(0x10000 - S.cart_size);
    if (a < start) return 0xff;
    return S.cart[a - start];
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x0020) return (a >= 0x08 && a <= 0x0d) ? S.inpt[a - 0x08] : 0;
    if (a < 0x0040) return maria_read(&S.maria, a);
    if (a < 0x0100) return S.ram[a + 0x800];
    if (a < 0x0120) return (a >= 0x108 && a <= 0x10d) ? S.inpt[a - 0x108] : 0;
    if (a < 0x0140) return maria_read(&S.maria, a);
    if (a < 0x0200) return S.ram[a + 0x800];
    if (a < 0x0220) return 0;
    if (a < 0x0240) return maria_read(&S.maria, a);
    if (a < 0x0280) return 0;
    if (a < 0x0300) {
        switch (a & 7) {
        case 0: return S.swcha;
        case 2: return S.swchb;
        case 4: case 6:
            S.riot_irq = false;
            return (uint8_t)(S.riot_timer >> S.riot_shift);
        case 5: case 7: return S.riot_irq ? 0x80 : 0x00;
        default: return 0;
        }
    }
    if (a < 0x0480) return 0;
    if (a < 0x0500) return S.riot_ram[a & 0x7f];
    if (a < 0x1800) return 0;
    if (a < 0x2800) return S.ram[a - 0x1800];
    if (a < 0x4000) return S.ram[(a - 0x2800) & 0xfff];
    return cart_read(a);
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x0020) { tia_write(&S.tia, (uint8_t)a, v); return; }
    if (a < 0x0040) { maria_write(&S.maria, a, v); return; }
    if (a < 0x0100) { S.ram[a + 0x800] = v; return; }
    if (a < 0x0120) return;
    if (a < 0x0140) { maria_write(&S.maria, a, v); return; }
    if (a < 0x0200) { S.ram[a + 0x800] = v; return; }
    if (a < 0x0220) return;
    if (a < 0x0240) { maria_write(&S.maria, a, v); return; }
    if (a < 0x0280) return;
    if (a < 0x0300) {
        if (a & 0x04) {
            static const int shifts[4] = { 0, 3, 6, 10 };
            S.riot_shift = shifts[a & 3];
            S.riot_timer = (uint32_t)v << S.riot_shift;
            S.riot_irq = false;
        }
        return;
    }
    if (a < 0x0480) return;
    if (a < 0x0500) { S.riot_ram[a & 0x7f] = v; return; }
    if (a < 0x1800) return;
    if (a < 0x2800) { S.ram[a - 0x1800] = v; return; }
    if (a < 0x4000) { S.ram[(a - 0x2800) & 0xfff] = v; return; }
    if (S.supergame && a >= 0x8000) S.bank = v & 0x0f;
}

static uint8_t maria_rd(void *u, uint16_t a) { return rd(u, a); }
static void dli(void *u) { (void)u; m65xx_set_nmi(&S.cpu, M65_PULSE); }

static int clamp8(double v)
{
    const int i = (int)(v * 255.0 + 0.5);
    return i < 0 ? 0 : i > 255 ? 255 : i;
}

bool cons_init(void)
{
    S.fb = (uint16_t *)calloc(MARIA_W * FB_H, sizeof(uint16_t));
    if (!S.fb) return false;
    for (int i = 0; i < 256; i++) {               // (as dsp-cpp builds it)
        const int hue = (i >> 4) & 0x0f, lum = i & 0x0f;
        const double y = 0.0625 + (double)lum * 0.0625 * 0.9375;
        double r = y, g = y, b = y;
        if (hue) {
            const double angle = ((double)hue - 1.0) * (2.0 * 3.14159265358979 / 15.0) - 0.4;
            const double qi = 0.35 * cos(angle), qq = 0.35 * sin(angle);
            r = y + 0.956 * qi + 0.621 * qq;
            g = y - 0.272 * qi - 0.647 * qq;
            b = y - 1.106 * qi + 1.703 * qq;
        }
        S.pal565[i] = (uint16_t)(((clamp8(r) & 0xf8) << 8) | ((clamp8(g) & 0xfc) << 3) | (clamp8(b) >> 3));
    }
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.maria.read = maria_rd;
    S.maria.dli = dli;
    cons_reset();
    return true;
}

void cons_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    memset(S.riot_ram, 0, sizeof S.riot_ram);
    maria_reset(&S.maria);
    tia_reset(&S.tia);
    S.bank = 0;
    S.riot_timer = 0;
    S.riot_shift = 10;
    S.riot_irq = false;
    S.swcha = 0xff;
    S.swchb = 0x0b;
    S.inpt[4] = S.inpt[5] = 0x80;
    for (int i = 0; i < MARIA_W * FB_H; i++) S.fb[i] = S.pal565[0];
    S.debt = 0;
    if (S.loaded) m65xx_reset(&S.cpu);
}

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (!data || size < 0x1000) {
        if (why) *why = "cartridge too small";
        free(data);
        return false;
    }
    bool sg, b6 = false, pal = false;
    size_t off = 0;
    if (size > 128 && !memcmp(data + 1, "ATARI7800", 9)) {
        const uint16_t type = (uint16_t)((data[53] << 8) | data[54]);
        sg = (type & 0x0002) != 0;
        b6 = (type & 0x0010) != 0;
        pal = data[57] == 1;
        off = 128;
    } else {
        sg = size > 0xc000;
    }
    size -= off;
    if (off) memmove(data, data + off, size);
    free(S.cart);
    S.cart = data;
    S.cart_size = size;
    S.cart_banks = (int)(size / 0x4000);
    S.supergame = sg;
    S.bank6_at_4000 = b6;
    S.pal = pal;
    S.loaded = true;
    cons_reset();
    return true;
}

void cons_eject(void)
{
    free(S.cart);
    S.cart = NULL;
    S.cart_size = 0;
    S.loaded = false;
    cons_reset();
}

bool cons_loaded(void) { return S.loaded; }

int cons_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    *audio = S.audio;
    const int lines = S.pal ? 312 : 262;
    uint8_t idx[MARIA_W];
    for (int line = 0; line < lines; line++) {
        maria_scanline(&S.maria, line, lines);
        int budget = LINE_CYCLES;
        if (S.maria.dmaon && line > 15 && line < lines - 5) budget -= 24;
        if (S.maria.wsync) {
            S.maria.wsync = false;
            budget = 0;
            S.debt = 0;
        }
        if (S.loaded && budget > 0) {
            const int want = budget - S.debt;
            S.debt = want > 0 ? m65xx_run(&S.cpu, want) - want : -want;
        }
        tia_clock_audio(&S.tia);
        tia_emit_audio(&S.tia, LINE_CYCLES, CPU_CLOCK, S.audio, &S.naudio, 1100);
        if (S.riot_timer > LINE_CYCLES) {
            S.riot_timer -= LINE_CYCLES;
        } else {
            S.riot_timer = 0;
            S.riot_irq = true;
        }
        const int y = line - 16;
        if (y >= 0 && y < FB_H) {
            maria_emit(&S.maria, idx);
            uint16_t *d = S.fb + y * MARIA_W;
            for (int x = 0; x < MARIA_W; x++) d[x] = S.pal565[idx[x]];
        }
    }
    return S.naudio;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = MARIA_W;
    *h = FB_H;
    *stride = MARIA_W;
    return S.fb;
}

void cons_pad(int player, unsigned b)
{
    static unsigned pads[2];
    pads[player & 1] = b;
    uint8_t a = 0xff;
    for (int p = 0; p < 2; p++) {
        const int sh = p ? 0 : 4;
        if (pads[p] & CP_UP) a &= (uint8_t)~(1u << sh);
        if (pads[p] & CP_DOWN) a &= (uint8_t)~(1u << (sh + 1));
        if (pads[p] & CP_LEFT) a &= (uint8_t)~(1u << (sh + 2));
        if (pads[p] & CP_RIGHT) a &= (uint8_t)~(1u << (sh + 3));
    }
    S.swcha = a;
    S.inpt[4] = (pads[0] & CP_B1) ? 0x00 : 0x80;
    S.inpt[5] = (pads[1] & CP_B1) ? 0x00 : 0x80;
    uint8_t sb = 0x0b;
    if (pads[0] & CP_START) sb &= (uint8_t)~0x02;   // (as dsp-cpp: RESET on bit 1)
    if (pads[0] & (CP_SELECT | CP_COIN)) sb &= (uint8_t)~0x01;
    S.swchb = sb;
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return S.loaded && S.supergame ? "SuperGame" : NULL; }
