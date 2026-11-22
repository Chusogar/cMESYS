// centipede.c - Centipede (Atari, 1980) and Millipede (Atari, 1982) for the
// common arcade frontends (arc.h): C port of dsp-cpp's drivers/arcade/
// centipede. ARC_VARIANT 0: Centipede, 1: Millipede.
//
// A 6502 at 1.512 MHz, one POKEY (Millipede: two, whose pots read the DIP
// switches), a 32x30 character layer and 16 8x16 sprites on a vertical
// monitor (the native 256x240 picture turned 270 degrees, drawn straight into
// the cabinet's 240x256). The trackball is not wired: the joystick moves.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/pokey.h"

#define MILLI (ARC_VARIANT == 1)
#define CLOCK (12096000 / 8)
#define SCANLINES 256
#define FPS 60.0
#define RATE 44100
#define W 240
#define H 256

#if MILLI
static const char *const kSets[] = { "milliped", NULL };
static const arc_rom kRom[] = {
    {"136013-104.mn1", 0x1000, 0x4000, 0x40711675}, {"136013-103.l1", 0x1000, 0x5000, 0xfb01baf2},
    {"136013-102.jk1", 0x1000, 0x6000, 0x62e137e0}, {"136013-101.h1", 0x1000, 0x7000, 0x46752c7d}};
static const arc_rom kChars[] = {{"136013-107.r5", 0x800, 0x0000, 0x68c3437a}, {"136013-106.p5", 0x800, 0x0800, 0xf4468045}};
const arc_info arc_game = { "milliped", "Millipede", "Atari", 1982, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "centiped", NULL };
static const arc_rom kRom[] = {
    {"136001-407.d1", 0x800, 0x2000, 0xc4d995eb}, {"136001-408.e1", 0x800, 0x2800, 0xbcdebe1b},
    {"136001-409.fh1", 0x800, 0x3000, 0x66d7b04a}, {"136001-410.j1", 0x800, 0x3800, 0x33ce4640}};
static const arc_rom kChars[] = {{"136001-211.f7", 0x800, 0x0000, 0x880acfb9}, {"136001-212.hj7", 0x800, 0x0800, 0xb1397029}};
const arc_info arc_game = { "centiped", "Centipede", "Atari", 1980, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_8WAY };
#endif

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

static struct {
    m65xx cpu;
    pokey pk0, pk1;
    uint8_t *mem, nvram[0x40], sprite_pen[0x400];   // (mem, fb on the heap)
    uint16_t palette[0x20];
    uint8_t *chars, *sprites;
    uint16_t *fb;
    uint8_t in0, in1, in2, in3, dsw_a, dsw_b, dsw_c;
    bool flip;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} C;

static uint8_t pot_a(void *u, int pot) { (void)u, (void)pot; return C.dsw_a; }
static uint8_t pot_b(void *u, int pot) { (void)u, (void)pot; return C.dsw_b; }

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | b); }

static void palette_centipede(uint8_t pos, uint8_t v)
{
    if ((pos & 4) == 0) return;         // bit 2 of the address must be set
    uint8_t r = (uint8_t)(0xff * ((~v >> 0) & 1)), g = (uint8_t)(0xff * ((~v >> 1) & 1)), b = (uint8_t)(0xff * ((~v >> 2) & 1));
    if (~v & 0x08) {
        if (b) b = 0xc0;
        else if (g) g = 0xc0;
    }
    if (pos & 0x08) {
        C.palette[pos] = rgb(r, g, b);
        for (int f = 0; f < 0x40; ++f) {
            const int base = f * 4;
            if ((pos & 3) == ((base >> 2) & 3)) C.sprite_pen[base + 1] = pos;
            if ((pos & 3) == ((base >> 4) & 3)) C.sprite_pen[base + 2] = pos;
            if ((pos & 3) == ((base >> 6) & 3)) C.sprite_pen[base + 3] = pos;
        }
    } else {
        C.palette[pos & 3] = rgb(r, g, b);
    }
}

static void palette_millipede(uint8_t pos, uint8_t v)
{
    const uint8_t r = (uint8_t)(0x21 * ((~v >> 5) & 1) + 0x47 * ((~v >> 6) & 1) + 0x97 * ((~v >> 7) & 1));
    const uint8_t g = (uint8_t)(0x47 * ((~v >> 3) & 1) + 0x97 * ((~v >> 4) & 1));
    const uint8_t b = (uint8_t)(0x21 * ((~v >> 0) & 1) + 0x47 * ((~v >> 1) & 1) + 0x97 * ((~v >> 2) & 1));
    C.palette[pos] = rgb(r, g, b);
    if (pos >= 0x10) {
        const int base = (pos & 0xc) << 6;
        for (int f = 0; f < 0x40; ++f) {
            const int res = base + f * 4;
            if ((pos & 3) == ((res >> 2) & 3)) C.sprite_pen[res + 1] = pos;
            if ((pos & 3) == ((res >> 4) & 3)) C.sprite_pen[res + 2] = pos;
            if ((pos & 3) == ((res >> 6) & 3)) C.sprite_pen[res + 3] = pos;
        }
    }
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
#if MILLI
    a &= 0x7fff;
    if (a <= 0x3ff || (a >= 0x1000 && a <= 0x13ff) || a >= 0x4000) return C.mem[a];
    if (a >= 0x400 && a <= 0x40f) return pokey_read(&C.pk0, a & 0xf);
    if (a >= 0x800 && a <= 0x80f) return pokey_read(&C.pk1, a & 0xf);
    if (a == 0x2000) return (uint8_t)(C.dsw_c | C.in0);
    if (a == 0x2001) return C.in1;
    if (a == 0x2010) return C.in2;
    if (a == 0x2011) return C.in3;
    if (a >= 0x2030 && a <= 0x206f) return C.nvram[a & 0x3f];
    return 0xff;
#else
    a &= 0x3fff;
    if (a <= 0x7ff || a >= 0x2000) return C.mem[a];
    if (a == 0x800) return C.dsw_a;
    if (a == 0x801) return C.dsw_b;
    if (a == 0xc00) return C.dsw_c;     // vsync + trackball / cabinet
    if (a == 0xc01) return C.in0;
    if (a == 0xc02) return 0;           // trackball 2
    if (a == 0xc03) return C.in1;
    if (a >= 0x1000 && a <= 0x100f) return pokey_read(&C.pk0, a & 0xf);
    if (a >= 0x1700 && a <= 0x173f) return C.nvram[a & 0x3f];
    return 0xff;
#endif
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if MILLI
    a &= 0x7fff;
    if (a <= 0x3ff || (a >= 0x1000 && a <= 0x13ff)) C.mem[a] = v;
    else if (a >= 0x400 && a <= 0x40f) pokey_write(&C.pk0, a & 0xf, v);
    else if (a >= 0x800 && a <= 0x80f) pokey_write(&C.pk1, a & 0xf, v);
    else if (a >= 0x2480 && a <= 0x249f) {
        if (C.mem[a] != v) {
            C.mem[a] = v;
            palette_millipede((uint8_t)(a & 0x1f), v);
        }
    } else if (a == 0x2506) C.flip = (v & 0x80) != 0;
    else if (a == 0x2600) m65xx_set_irq(&C.cpu, M65_CLEAR);
    else if (a >= 0x2780 && a <= 0x27bf) C.nvram[a & 0x3f] = v;
#else
    a &= 0x3fff;
    if (a <= 0x7ff) C.mem[a] = v;
    else if (a >= 0x1000 && a <= 0x100f) pokey_write(&C.pk0, a & 0xf, v);
    else if (a >= 0x1400 && a <= 0x140f) {
        if (C.mem[a] != v) {
            C.mem[a] = v;
            palette_centipede((uint8_t)(a & 0xf), v);
        }
    } else if (a >= 0x1600 && a <= 0x163f) C.nvram[a & 0x3f] = v;
    else if (a == 0x1800) m65xx_set_irq(&C.cpu, M65_CLEAR);
    else if (a == 0x1c07) C.flip = (v & 0x80) != 0;
#endif
}

static void cycles(void *u, int c)
{
    (void)u;
    pokey_run(&C.pk0, c);
    if (MILLI) pokey_run(&C.pk1, c);
    C.audio_acc += (int64_t)c * RATE;
    while (C.audio_acc >= CLOCK) {
        C.audio_acc -= CLOCK;
        int32_t s = pokey_update(&C.pk0);
        if (MILLI) s += pokey_update(&C.pk1);
        if (C.naudio < 1024) C.audio[C.naudio++] = arc_clamp16(s);
    }
}

// a pixel of the native 256x240 picture, turned 270 degrees (and 180 more
// with the cocktail flip) into the cabinet's
static inline void put(int nx, int ny, uint16_t c)
{
    if (ny >= W) return;
    int x = ny, y = 255 - nx;
    if (C.flip) {
        x = W - 1 - x;
        y = H - 1 - y;
    }
    C.fb[y * W + x] = c;
}

static void update_video(void)
{
    const int tile_base = MILLI ? 0x1000 : 0x400;
    for (int f = 0; f < 32 * 30; ++f) {
        const int tx = f % 32, ty = f / 32;
        const uint8_t at = C.mem[tile_base + f];
        int nchar, color_base = 0;
        bool fx = false, fy = false;
#if MILLI
        nchar = (at & 0x3f) | 0x40 | ((at & 0x40) << 1);
        color_base = ((at >> 6) & 3) << 2;
#else
        nchar = (at & 0x3f) | 0x40;
        fx = (at & 0x40) != 0;
        fy = (at & 0x80) != 0;
#endif
        const uint8_t *g = C.chars + nchar * 64;
        for (int r = 0; r < 8; ++r) {
            const uint8_t *row = g + (fy ? 7 - r : r) * 8;
            for (int c = 0; c < 8; ++c) put(tx * 8 + c, ty * 8 + r, C.palette[(color_base + row[fx ? 7 - c : c]) & 0x1f]);
        }
    }
    const int spr = MILLI ? 0x13c0 : 0x7c0;
    for (int f = 0; f < 0x10; ++f) {
        const uint8_t at = C.mem[spr + f];
        const int nchar = ((at & 0x3e) >> 1) | ((at & 1) << 6);
        const int color = C.mem[spr + 0x30 + f] << 2;
        const int x = C.mem[spr + 0x20 + f], y = 240 - C.mem[spr + 0x10 + f];
        const bool fx = !MILLI && (at & 0x40) != 0, fy = (at & 0x80) != 0;
        const uint8_t *g = C.sprites + nchar * 128;
        for (int r = 0; r < 16; ++r) {
            const int py = y + r;
            if (py < 0 || py >= 240) continue;
            const uint8_t *row = g + (fy ? 15 - r : r) * 8;
            for (int c = 0; c < 8; ++c) {
                const int px = x + c;
                if (px < 0 || px >= 256) continue;
                const int pen = row[fx ? 7 - c : c];
                if (pen) put(px, py, C.palette[C.sprite_pen[(color + pen) & 0x3ff] & 0x1f]);
            }
        }
    }
}

bool arc_init(void)
{
    uint8_t *gfx = (uint8_t *)calloc(1, 0x1000);
    C.mem = (uint8_t *)malloc(0x8000);
    C.fb = (uint16_t *)calloc(W * H, 2);
    if (!gfx || !C.mem || !C.fb) return false;
    memset(C.mem, 0xff, 0x8000);
    if (!ARC_LOAD(kRom, C.mem, 0x8000) || !ARC_LOAD(kChars, gfx, 0x1000)) return false;
    // (dsp-cpp: the ROM image over a zeroed memory: what no ROM covers is 0xff
    // in its loader's buffer and copied as such)
    arc_layout cl = { 8, 8, 0x100, 2, 64, false, false, false, {0x100 * 64, 0}, kX8, kY16 };
    arc_layout sl = { 8, 16, 0x80, 2, 128, false, false, false, {0x80 * 128, 0}, kX8, kY16 };
    C.chars = arc_gfx_decode(&cl, gfx, 0x1000);
    C.sprites = arc_gfx_decode(&sl, gfx, 0x1000);
    free(gfx);
    if (!C.chars || !C.sprites) return false;
#if MILLI
    C.dsw_a = 0x14;
    C.dsw_b = 0x02;
    C.dsw_c = 0x04;
#else
    C.dsw_a = 0x54;
    C.dsw_b = 0x02;
    C.dsw_c = 0x20;
#endif
    C.cpu.read = rd;
    C.cpu.write = wr;
    C.cpu.cycles = cycles;
    pokey_init(&C.pk0);
    pokey_init(&C.pk1);
#if MILLI
    C.pk0.pot_read = pot_a;
    C.pk1.pot_read = pot_b;
#else
    (void)pot_a, (void)pot_b;
#endif
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m65xx_reset(&C.cpu);
    pokey_reset(&C.pk0);
    if (MILLI) pokey_reset(&C.pk1);
    memset(C.nvram, 0, sizeof C.nvram);
    for (int i = 0; i < 0x400; ++i) C.sprite_pen[i] = (uint8_t)i;
    memset(C.palette, 0, sizeof C.palette);
    C.flip = false;
    C.audio_acc = 0;
#if MILLI
    memset(C.mem + 0x2480, 0xff, 0x20);
    C.in0 = C.in1 = 0x30;
    C.in2 = C.in3 = 0xff;
#else
    memset(C.mem + 0x1400, 0xff, 0x10);
    C.in0 = C.in1 = 0xff;
#endif
}

int arc_run_frame(const int16_t **audio)
{
    const int cyc = (int)((double)CLOCK / FPS / SCANLINES + 0.5);
    C.naudio = 0;
    for (int line = 0; line < SCANLINES; ++line) {
        switch (line) {
        case 0: C.dsw_c &= (uint8_t)~0x40; break;
        case 16: case 80: case 144: case 208: m65xx_set_irq(&C.cpu, M65_CLEAR); break;
        case 48: case 112: case 176: m65xx_set_irq(&C.cpu, M65_ASSERT); break;
        case 240:
            update_video();
            m65xx_set_irq(&C.cpu, M65_ASSERT);
            C.dsw_c |= 0x40;
            break;
        default: break;
        }
        m65xx_run(&C.cpu, cyc);
    }
    if (audio) *audio = C.audio;
    return C.naudio;
}

const uint16_t *arc_frame(void) { return C.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
#if MILLI
    C.in0 = C.in1 = 0x30;
    C.in2 = C.in3 = 0xff;
    if (p1 & AP_B1) C.in0 &= ~0x10;
    if (p1 & AP_START) C.in0 &= ~0x20;
    if (p2 & AP_B1) C.in1 &= ~0x10;
    if (p2 & AP_START) C.in1 &= ~0x20;
    if (p1 & AP_RIGHT) C.in2 &= ~0x01;
    if (p1 & AP_LEFT) C.in2 &= ~0x02;
    if (p1 & AP_DOWN) C.in2 &= ~0x04;
    if (p1 & AP_UP) C.in2 &= ~0x08;
    if (in->coin1) C.in2 &= ~0x20;
    if (in->coin2) C.in2 &= ~0x40;
    if (p2 & AP_RIGHT) C.in3 &= ~0x01;
    if (p2 & AP_LEFT) C.in3 &= ~0x02;
    if (p2 & AP_DOWN) C.in3 &= ~0x04;
    if (p2 & AP_UP) C.in3 &= ~0x08;
#else
    C.in0 = C.in1 = 0xff;
    if (p1 & AP_START) C.in0 &= ~0x01;
    if (p2 & AP_START) C.in0 &= ~0x02;
    if (p1 & AP_B1) C.in0 &= ~0x04;
    if (p2 & AP_B1) C.in0 &= ~0x08;
    if (in->coin1) C.in0 &= ~0x20;
    if (in->coin2) C.in0 &= ~0x40;
    if (p2 & AP_UP) C.in1 &= ~0x01;
    if (p2 & AP_DOWN) C.in1 &= ~0x02;
    if (p2 & AP_LEFT) C.in1 &= ~0x04;
    if (p2 & AP_RIGHT) C.in1 &= ~0x08;
    if (p1 & AP_UP) C.in1 &= ~0x10;
    if (p1 & AP_DOWN) C.in1 &= ~0x20;
    if (p1 & AP_LEFT) C.in1 &= ~0x40;
    if (p1 & AP_RIGHT) C.in1 &= ~0x80;
#endif
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw_a = v;
    else if (bank == 1) C.dsw_b = v;
    else if (bank == 2) C.dsw_c = (uint8_t)((C.dsw_c & 0x40) | (v & ~0x40));
}
