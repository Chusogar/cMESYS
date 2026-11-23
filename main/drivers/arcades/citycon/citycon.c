// citycon.c - City Connection (Jaleco, 1985) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/citycon (from dsp-emulator's
// cityconnection_hw.pas). A 6809 at 8 MHz, a sound 6809 with an AY-3-8910
// and a YM2203, a 1024-wide background picked from the scenery ROMs, a
// 1024-wide text layer coloured per line (the top 48 lines fixed) and 8x16
// sprites. Its quirks (the extra palette writes, the coin bit added to DSW
// A) are dsp-cpp's, kept for the same picture.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../sound/ay8910.h"
#include "../../../sound/ym2203dsp.h"

#define MAIN_CLOCK 8000000
#define SOUND_CLOCK (20000000 / 28)
#define AY_CLOCK (20000000 / 16)
#define FPS 59.637405
#define SCANLINES 262
#define RATE 44100
#define W 240
#define H 224

static const char *const kSets[] = { "citycon", NULL };
static const arc_rom kMain[] = {{"c10", 0x4000, 0x4000, 0xae88b53c}, {"c11", 0x8000, 0x8000, 0x139eb1aa}};
static const arc_rom kSound[] = {{"c1", 0x8000, 0x8000, 0x1fad7589}};
static const arc_rom kChars[] = {{"c4", 0x2000, 0, 0xa6b32fc6}};
static const arc_rom kSprites[] = {{"c12", 0x2000, 0, 0x08eaaccd}, {"c13", 0x2000, 0x2000, 0x1819aafb}};
static const arc_rom kTiles[] = {
    {"c9", 0x8000, 0, 0x8aeb47e6}, {"c8", 0x4000, 0x8000, 0x0d7a1eeb},
    {"c6", 0x8000, 0xc000, 0x2246fe9d}, {"c7", 0x4000, 0x14000, 0xe8b97de9}};
static const arc_rom kScenery[] = {
    {"c2", 0x8000, 0, 0xf2da4f23}, {"c3", 0x4000, 0x8000, 0x7ef3ac1b}, {"c5", 0x2000, 0xc000, 0xc03d8b1b}};

const arc_info arc_game = { "citycon", "City Connection", "Jaleco", 1985, W, H, FPS, RATE, kSets,
                            { "JUMP", NULL, NULL, NULL }, ARC_JOY_8WAY };

static const int kX8[8] = {0, 1, 2, 3, 16384, 16385, 16386, 16387};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m6809d main, sound;
    ym2203d ym;
    ay8910 ay;
    uint8_t mem[0x10000], snd[0x10000], scenery[0xe000], lines_color[0x100], pal_ram[0x800];
    uint16_t palette[0x400];
    uint8_t *chars, *tiles, *sprites;
    uint16_t *bg;                       // 1024x256, cached while its palette and scenery stay
    uint16_t *fg;                       // 1024x256, palette index + 1 (0 transparent)
    uint16_t composite[256 * 256];
    uint16_t *fb;
    uint8_t fondo, soundlatch, soundlatch2, in0, in1, in2, dsw_a, dsw_b;
    uint16_t scroll_x;
    bool bg_dirty, flip;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *C_;
#define C (*C_)

static void set_color(int index)
{
    const int dir = index * 2;
    if (dir + 1 >= 0x800) return;
    const uint8_t rg = C.pal_ram[dir], bx = C.pal_ram[dir + 1];
    C.palette[index & 0x3ff] = arc_rgb565((uint32_t)((rg >> 4) * 0x11) << 16 | (uint32_t)((rg & 15) * 0x11) << 8 | (uint32_t)((bx >> 4) * 0x11));
    if (index >= 256 && index <= 511) C.bg_dirty = true;
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff || (a >= 0x2800 && a <= 0x28ff) || a >= 0x4000) return C.mem[a];
    if (a >= 0x2000 && a <= 0x20ff) return C.lines_color[a & 0xff];
    if (a == 0x3000) return C.flip ? C.in2 : C.in0;
    if (a == 0x3001) return (uint8_t)(C.dsw_a + C.in1);
    if (a == 0x3002) return C.dsw_b;
    if (a == 0x3007) {
        m6809d_set_irq(&C.main, M6809D_CLEAR);
        return 0;
    }
    if (a >= 0x3800 && a <= 0x3cff) return C.pal_ram[a & 0x7ff];
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff || (a >= 0x2800 && a <= 0x28ff)) { C.mem[a] = v; return; }
    if (a >= 0x2000 && a <= 0x20ff) { C.lines_color[a & 0xff] = v; return; }
    switch (a) {
    case 0x3000:
        if (C.fondo != v >> 4) {
            C.fondo = v >> 4;
            C.bg_dirty = true;
        }
        C.flip = (v & 1) != 0;
        return;
    case 0x3001: C.soundlatch = v; return;
    case 0x3002: C.soundlatch2 = v; return;
    case 0x3004: C.scroll_x = (uint16_t)((C.scroll_x & 0xff) | ((v & 3) << 8)); return;
    case 0x3005: C.scroll_x = (uint16_t)((C.scroll_x & 0x300) | v); return;
    default: break;
    }
    if (a >= 0x3800 && a <= 0x3cff) {
        const int off = a & 0x7ff;
        if (C.pal_ram[off] != v) {
            C.pal_ram[off] = v;
            set_color(off & ~1);        // (as dsp-cpp: three updates)
            set_color(off >> 1);
            set_color((off & ~1) >> 1);
        }
    }
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x0fff || a >= 0x8000) return C.snd[a];
    if (a == 0x4000) return ay8910_data_r(&C.ay);
    if (a == 0x6000) return ym2203d_status(&C.ym);
    if (a == 0x6001) return ym2203d_read(&C.ym);
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x0fff) { C.snd[a] = v; return; }
    switch (a) {
    case 0x4000: ay8910_address_w(&C.ay, v); break;
    case 0x4001: ay8910_data_w(&C.ay, v); break;
    case 0x6000: ym2203d_control(&C.ym, v); break;
    case 0x6001: ym2203d_write(&C.ym, v); break;
    default: break;
    }
}

static uint8_t latch1(void *u) { (void)u; return C.soundlatch; }
static uint8_t latch2(void *u) { (void)u; return C.soundlatch2; }

static void sound_cycles(void *u, int c)
{
    (void)u;
    C.audio_acc += (int64_t)c * RATE;
    while (C.audio_acc >= SOUND_CLOCK) {
        C.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2203d_update(&C.ym) + ay8910_render(&C.ay);
        if (C.naudio < 1024) C.audio[C.naudio++] = arc_clamp16(s);
    }
}

static void draw_background(void)
{
    if (!C.bg_dirty) return;
    for (int f = 0; f < 0x1000; f++) {
        const int yt = f >> 5, x = ((f & 0x1f) + (yt & 0x60)) << 3, y = (yt & 0x1f) << 3;
        const int code = C.scenery[0x1000 * C.fondo + f] + (C.fondo << 8);
        const int base = ((C.scenery[0xc000 + (code & 0x1fff)] & 0x0f) << 4) + 256;
        const uint8_t *px = C.tiles + (size_t)code * 64;
        for (int py = 0; py < 8; py++)
            for (int qx = 0; qx < 8; qx++) C.bg[(y + py) * 1024 + ((x + qx) & 0x3ff)] = C.palette[base + px[py * 8 + qx]];
    }
    C.bg_dirty = false;
}

static void draw_foreground(void)
{
    memset(C.fg, 0, 1024 * 256 * 2);
    for (int f = 0; f < 0x1000; f++) {
        const int yt = f >> 5, x = (f & 0x1f) + (yt & 0x60), y = yt & 0x1f;
        const uint8_t *px = C.chars + (size_t)C.mem[0x1000 + f] * 64;
        for (int y2 = 0; y2 < 8; y2++) {
            const int base = (C.lines_color[y2 + (y << 3)] << 2) + 512;
            for (int x2 = 0; x2 < 8; x2++) {
                const uint8_t p = px[y2 * 8 + x2];
                if (p) C.fg[((y << 3) + y2) * 1024 + (x << 3) + x2] = (uint16_t)(base + p + 1);
            }
        }
    }
}

static void update_video(void)
{
    draw_background();
    draw_foreground();
    for (int y = 0; y < 256; y++) {
        const int scroll = y < 48 ? 0 : C.scroll_x;
        for (int x = 0; x < 256; x++) {
            const int bx = (x + C.scroll_x) & 0x3ff, fx = (x + scroll) & 0x3ff;
            const uint16_t f = C.fg[y * 1024 + fx];
            C.composite[y * 256 + x] = f ? C.palette[f - 1] : C.bg[y * 1024 + bx];
        }
    }
    for (int f = 0x3f; f >= 0; f--) {
        const int b = 0x2800 + f * 4;
        const int y = 239 - C.mem[b], x = C.mem[b + 3];
        const uint8_t attr = C.mem[b + 2];
        const int base = (attr & 0x0f) << 4;
        const bool fx = (attr & 0x10) == 0;
        const uint8_t *px = C.sprites + (size_t)C.mem[b + 1] * 128;
        for (int py = 0; py < 16; py++) {
            const int sy = y + py;
            if (sy < 0 || sy >= 256) continue;
            for (int qx = 0; qx < 8; qx++) {
                const uint8_t p = px[py * 8 + (fx ? 7 - qx : qx)];
                if (p) C.composite[sy * 256 + ((x + qx) & 0xff)] = C.palette[base + p];
            }
        }
    }
    for (int y = 0; y < H; y++) memcpy(C.fb + y * W, C.composite + (y + 16) * 256 + 8, W * 2);
}

bool arc_init(void)
{
    if (!(C_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    C.bg = (uint16_t *)calloc(1024 * 256, 2);
    C.fg = (uint16_t *)calloc(1024 * 256, 2);
    C.fb = (uint16_t *)calloc(W * H, 2);
    C.tiles = (uint8_t *)malloc((size_t)3072 * 64);
    C.sprites = (uint8_t *)malloc((size_t)256 * 128);
    uint8_t *g = (uint8_t *)calloc(1, 0x18000);
    if (!C.bg || !C.fg || !C.fb || !C.tiles || !C.sprites || !g) return false;
    if (!ARC_LOAD(kMain, C.mem, 0x10000) || !ARC_LOAD(kSound, C.snd, 0x10000) || !ARC_LOAD(kScenery, C.scenery, 0xe000))
        return false;
    arc_layout cl = { 8, 8, 256, 2, 64, false, false, false, {4, 0}, kX8, kY8 };
    if (!ARC_LOAD(kChars, g, 0x2000) || !(C.chars = arc_gfx_decode(&cl, g, 0x2000))) return false;
    memset(g, 0, 0x18000);
    if (!ARC_LOAD(kSprites, g, 0x4000)) return false;
    for (int bank = 0; bank < 2; bank++) {
        const int base = 0x1000 * bank * 8, high = (0x2000 + 0x1000 * bank) * 8;
        arc_layout sl = { 8, 16, 128, 4, 128, false, false, false, {base + 4, base, high + 4, high}, kX8, kY16 };
        uint8_t *d = arc_gfx_decode(&sl, g, 0x4000);
        if (!d) return false;
        memcpy(C.sprites + (size_t)bank * 128 * 128, d, 128 * 128);
        free(d);
    }
    memset(g, 0, 0x18000);
    if (!ARC_LOAD(kTiles, g, 0x18000)) return false;
    for (int bank = 0; bank < 12; bank++) {
        arc_layout tl = { 8, 8, 256, 4, 64, false, false, false,
                          {4 + 0x1000 * bank * 8, 0x1000 * bank * 8, (0xc000 + 0x1000 * bank) * 8 + 4, (0xc000 + 0x1000 * bank) * 8},
                          kX8, kY8 };
        uint8_t *d = arc_gfx_decode(&tl, g, 0x18000);
        if (!d) return false;
        memcpy(C.tiles + (size_t)bank * 256 * 64, d, 256 * 64);
        free(d);
    }
    free(g);
    C.main.read = main_read;
    C.main.write = main_write;
    C.sound.read = sound_read;
    C.sound.write = sound_write;
    C.sound.cycles = sound_cycles;
    ym2203d_init(&C.ym, AY_CLOCK, 1.0f, 1.0f);
    C.ym.port_a_read = latch1;
    C.ym.port_b_read = latch2;
    ay8910_init(&C.ay, AY_CLOCK, RATE);
    C.dsw_a = 0x00;
    C.dsw_b = 0x80;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&C.main);
    m6809d_reset(&C.sound);
    ym2203d_reset(&C.ym);
    ay8910_reset(&C.ay);
    C.fondo = C.soundlatch = C.soundlatch2 = 0;
    C.scroll_x = 0;
    C.bg_dirty = true;
    C.flip = false;
    C.in0 = 0xff;
    C.in1 = 0x80;
    C.in2 = 0xff;
    memset(C.lines_color, 0, sizeof C.lines_color);
    memset(C.pal_ram, 0, sizeof C.pal_ram);
    for (int i = 0; i < 0x400; i++) C.palette[i] = 0;
    C.audio_acc = 0;
    m6809d_set_irq(&C.main, M6809D_CLEAR);
}

int arc_run_frame(const int16_t **audio)
{
    const int cm = (int)((double)MAIN_CLOCK / (FPS * SCANLINES) + 0.5), cs = (int)((double)SOUND_CLOCK / (FPS * SCANLINES) + 0.5);
    C.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 255) {
            m6809d_set_irq(&C.main, M6809D_ASSERT);
            update_video();
        }
        m6809d_run(&C.main, cm);
        m6809d_run(&C.sound, cs);
    }
    if (audio) *audio = C.audio;
    return C.naudio;
}

const uint16_t *arc_frame(void) { return C.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0xff, c = 0xff;
    if (p1 & AP_UP) a &= ~0x01;
    if (p1 & AP_DOWN) a &= ~0x02;
    if (p1 & AP_RIGHT) a &= ~0x04;
    if (p1 & AP_LEFT) a &= ~0x08;
    if (p1 & AP_B1) a &= ~0x10;
    if (p1 & AP_B2) a &= ~0x20;
    if (p1 & AP_START) a &= ~0x40;
    if (p2 & AP_START) a &= ~0x80;
    if (p2 & AP_UP) c &= ~0x01;
    if (p2 & AP_DOWN) c &= ~0x02;
    if (p2 & AP_RIGHT) c &= ~0x04;
    if (p2 & AP_LEFT) c &= ~0x08;
    if (p2 & AP_B1) c &= ~0x10;
    if (p2 & AP_B2) c &= ~0x20;
    C.in0 = a;
    C.in2 = c;
    C.in1 = in->coin1 ? 0x00 : 0x80;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw_a = v;
    else if (bank == 1) C.dsw_b = v;
}
