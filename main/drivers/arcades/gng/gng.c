// gng.c - Ghosts'n Goblins (Capcom, 1985) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/gng (from gng_hw.pas). A 6809
// at 6 MHz with banked ROM, a sound Z80 with two YM2203 (their samples made
// at the end of each frame, as dsp-cpp does), a 512x512 scrolling background
// whose tiles can also be drawn over the sprites, 8x8 text and 16x16 sprites.
// dsp-cpp keeps the layers in surfaces redrawn on changes (all of them when a
// colour changes); here each pixel comes from the tiles when the picture is
// put together, the same result.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2203dsp.h"

#define FPS (12000000.0 / 2.0 / 384.0 / 262.0)
#define SCANLINES 262
#define MAIN_CLOCK 6000000
#define SOUND_CLOCK 3000000
#define YM_CLOCK 1500000
#define RATE 44100
#define W 256
#define H 224

static const char *const kSets[] = { "gng", NULL };
static const arc_rom kMain[] = {
    {"gg3.bin", 0x8000, 0x8000, 0x9e01c65e}, {"gg4.bin", 0x4000, 0x4000, 0x66606beb}, {"gg5.bin", 0x8000, 0x10000, 0xd6397b2b}};
static const arc_rom kSound[] = {{"gg2.bin", 0x8000, 0, 0x615f5b6f}};
static const arc_rom kChar[] = {{"gg1.bin", 0x4000, 0, 0xecfccf07}};
static const arc_rom kTiles[] = {
    {"gg11.bin", 0x4000, 0, 0xddd56fa9}, {"gg10.bin", 0x4000, 0x4000, 0x7302529d}, {"gg9.bin", 0x4000, 0x8000, 0x20035bda},
    {"gg8.bin", 0x4000, 0xc000, 0xf12ba271}, {"gg7.bin", 0x4000, 0x10000, 0xe525207d}, {"gg6.bin", 0x4000, 0x14000, 0x2d77e9b2}};
static const arc_rom kSprites[] = {
    {"gg17.bin", 0x4000, 0, 0x93e50a8f}, {"gg16.bin", 0x4000, 0x4000, 0x06d7e5ca}, {"gg15.bin", 0x4000, 0x8000, 0xbc1fe02d},
    {"gg14.bin", 0x4000, 0xc000, 0x6aaf12f9}, {"gg13.bin", 0x4000, 0x10000, 0xe80c3fca}, {"gg12.bin", 0x4000, 0x14000, 0x7780a925}};

const arc_info arc_game = { "gng", "Ghosts'n Goblins", "Capcom", 1985, W, H, FPS, RATE, kSets,
                            { "FIRE", "JUMP", NULL, NULL }, ARC_JOY_8WAY };

static const int kPsX[16] = {0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267};
static const int kPsY[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
static const int kPtX[16] = {0, 1, 2, 3, 4, 5, 6, 7, 128, 129, 130, 131, 132, 133, 134, 135};
static const int kPtY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m6809d main;
    zd_z80 sound;
    ym2203d ym0, ym1;
    uint8_t *mem, *mem_snd, *bank_rom;      // (on the heap) bank_rom: 5 x 0x2000
    uint8_t *chars, *sprites, *tiles;
    uint8_t palette_ram[0x200], sprite_buffer[0x200];
    uint16_t palette[0x100];
    uint16_t *fb;
    uint8_t bank, soundlatch, in0, in1, in2, dsw_a, dsw_b;
    uint16_t scroll_x, scroll_y;
    bool flip;
    double frame_main, frame_snd;
    int sound_irq_accum;
    int16_t audio[1024];
    int naudio;
} *G_;
#define G (*G_)

static void set_color(int i)
{
    const uint8_t lo = G.palette_ram[i], hi = G.palette_ram[0x100 + i];
    const uint32_t r = (lo >> 4) & 15, g = lo & 15, b = (hi >> 4) & 15;
    G.palette[i] = arc_rgb565((r << 4 | r) << 16 | (g << 4 | g) << 8 | (b << 4 | b));
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x2fff || a >= 0x6000) return G.mem[a];
    switch (a) {
    case 0x3000: return G.in0;
    case 0x3001: return G.in1;
    case 0x3002: return G.in2;
    case 0x3003: return G.dsw_a;
    case 0x3004: return G.dsw_b;
    default: break;
    }
    if (a >= 0x3800 && a <= 0x39ff) return G.palette_ram[a & 0x1ff];
    if (a >= 0x4000 && a <= 0x5fff) return G.bank_rom[(G.bank % 5) * 0x2000 + (a & 0x1fff)];
    return 0;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x2fff) G.mem[a] = v;
    else if (a >= 0x3800 && a <= 0x39ff) {
        if (G.palette_ram[a & 0x1ff] != v) {
            G.palette_ram[a & 0x1ff] = v;
            set_color(a & 0xff);
        }
    } else if (a == 0x3a00) G.soundlatch = v;
    else if (a == 0x3b08) G.scroll_x = (uint16_t)((G.scroll_x & 0x100) | v);
    else if (a == 0x3b09) G.scroll_x = (uint16_t)((G.scroll_x & 0xff) | ((v & 1) << 8));
    else if (a == 0x3b0a) G.scroll_y = (uint16_t)((G.scroll_y & 0x100) | v);
    else if (a == 0x3b0b) G.scroll_y = (uint16_t)((G.scroll_y & 0xff) | ((v & 1) << 8));
    else if (a == 0x3d00) G.flip = (v & 1) == 0;
    else if (a == 0x3e00) G.bank = v % 5;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff || (a >= 0xc000 && a <= 0xc7ff)) return G.mem_snd[a];
    if (a == 0xc800) return G.soundlatch;
    return 0;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a >= 0xc000 && a <= 0xc7ff) G.mem_snd[a] = v;
    else if (a == 0xe000) ym2203d_control(&G.ym0, v);
    else if (a == 0xe001) ym2203d_write(&G.ym0, v);
    else if (a == 0xe002) ym2203d_control(&G.ym1, v);
    else if (a == 0xe003) ym2203d_write(&G.ym1, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(void *u, int c)
{
    (void)u;
    if (c <= 0) return;
    const int period = (int)((double)SOUND_CLOCK / (4.0 * FPS) + 0.5);
    G.sound_irq_accum += c;
    while (G.sound_irq_accum >= period) {
        G.sound_irq_accum -= period;
        zd_set_irq(&G.sound, ZD_HOLD, 0xff);
    }
}

// the background's 512x512 plane at (bx, by): 0 transparent (pens 0 and 6),
// else 1 + the colour; *over: a tile drawn over the sprites
static inline int bg_px(int bx, int by, bool *over)
{
    const int f = (bx >> 4) * 32 + (by >> 4);
    const uint8_t at = G.mem[0x2c00 + f];
    const int nchar = G.mem[0x2800 + f] + ((at & 0xc0) << 2);
    const int sx = (at & 0x10) ? 15 - (bx & 15) : (bx & 15), sy = (at & 0x20) ? 15 - (by & 15) : (by & 15);
    const uint8_t pen = G.tiles[nchar * 256 + sy * 16 + sx];
    *over = (at & 8) != 0;
    return (pen == 0 || pen == 6) ? 0 : 1 + ((at & 7) << 3) + pen;
}

static inline void put(int x, int y, uint16_t c)      // composite (256x256) -> picture
{
    if (y >= 16 && y < 16 + H) G.fb[(y - 16) * W + x] = c;
}

static void update_video(void)
{
    for (int y = 16; y < 16 + H; y++) {
        const int by = (y + G.scroll_y) & 0x1ff;
        for (int x = 0; x < W; x++) {
            bool over;
            const int p = bg_px((x + G.scroll_x) & 0x1ff, by, &over);
            G.fb[(y - 16) * W + x] = p ? G.palette[p - 1] : G.palette[0];
        }
    }
    for (int f = 0x7f; f >= 0; f--) {
        const uint8_t at = G.sprite_buffer[(f << 2) + 1];
        const int nchar = G.sprite_buffer[f << 2] + ((at << 2) & 0x300);
        const int color = (at & 0x30) + 64;
        int x = (int)G.sprite_buffer[3 + (f << 2)] - 0x100 * (at & 1);
        int y = G.sprite_buffer[2 + (f << 2)];
        bool fx = (at & 4) != 0, fy = (at & 8) != 0;
        if (G.flip) {
            x = 240 - x;
            y = 240 - y;
            fx = !fx;
            fy = !fy;
        }
        const uint8_t *src = G.sprites + nchar * 256;
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 16; col++) {
                const uint8_t pen = src[(fy ? 15 - row : row) * 16 + (fx ? 15 - col : col)];
                if (pen == 15) continue;
                const int dx = x + col, dy = y + row;
                if (dx < 0 || dx >= 256 || dy < 0 || dy >= 256) continue;
                put(dx, dy, G.palette[color + pen]);
            }
    }
    for (int y = 16; y < 16 + H; y++) {
        const int by = (y + G.scroll_y) & 0x1ff;
        for (int x = 0; x < W; x++) {
            bool over;
            const int p = bg_px((x + G.scroll_x) & 0x1ff, by, &over);
            if (over && p) G.fb[(y - 16) * W + x] = G.palette[p - 1];
            // the text, no scroll (pen 3 transparent)
            const int f = (y >> 3) * 32 + (x >> 3);
            const uint8_t at = G.mem[0x2400 + f];
            const int nchar = G.mem[0x2000 + f] + ((at & 0xc0) << 2);
            const uint8_t pen = G.chars[nchar * 64 + (y & 7) * 8 + (x & 7)];
            if (pen != 3) G.fb[(y - 16) * W + x] = G.palette[((at & 0x0f) << 2) + 0x80 + pen];
        }
    }
    memcpy(G.sprite_buffer, G.mem + 0x1e00, 0x200);
}

bool arc_init(void)
{
    if (!(G_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    G.mem = (uint8_t *)calloc(1, 0x10000);
    G.mem_snd = (uint8_t *)calloc(1, 0x10000);
    G.bank_rom = (uint8_t *)calloc(5, 0x2000);
    G.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *tmp = (uint8_t *)calloc(1, 0x18000);
    if (!G.mem || !G.mem_snd || !G.bank_rom || !G.fb || !tmp) return false;
    if (!ARC_LOAD(kMain, tmp, 0x18000)) return false;
    memcpy(G.mem + 0x8000, tmp + 0x8000, 0x8000);
    for (int f = 0; f < 4; f++) memcpy(G.bank_rom + f * 0x2000, tmp + 0x10000 + f * 0x2000, 0x2000);
    memcpy(G.mem + 0x6000, tmp + 0x6000, 0x2000);
    memcpy(G.bank_rom + 4 * 0x2000, tmp + 0x4000, 0x2000);
    if (!ARC_LOAD(kSound, G.mem_snd, 0x8000)) return false;
    memset(tmp, 0, 0x18000);
    arc_layout cl = { 8, 8, 1024, 2, 128, false, false, false, {4, 0}, kPsX, kPsY };
    if (!ARC_LOAD(kChar, tmp, 0x4000) || !(G.chars = arc_gfx_decode(&cl, tmp, 0x4000))) return false;
    arc_layout tl = { 16, 16, 1024, 3, 256, false, false, false, {0x10000 * 8, 0x8000 * 8, 0}, kPtX, kPtY };
    if (!ARC_LOAD(kTiles, tmp, 0x18000) || !(G.tiles = arc_gfx_decode(&tl, tmp, 0x18000))) return false;
    arc_layout sl = { 16, 16, 1024, 4, 512, false, false, false, {0xc000 * 8 + 4, 0xc000 * 8, 4, 0}, kPsX, kPsY };
    if (!ARC_LOAD(kSprites, tmp, 0x18000) || !(G.sprites = arc_gfx_decode(&sl, tmp, 0x18000))) return false;
    free(tmp);
    G.main.read = main_read;
    G.main.write = main_write;
    G.sound.read = sound_read;
    G.sound.write = sound_write;
    G.sound.in = in_ff;
    G.sound.out = out_nop;
    G.sound.cycles = sound_cycles;
    zd_init(&G.sound);
    ym2203d_init(&G.ym0, YM_CLOCK, 1.0f, 1.0f);
    ym2203d_init(&G.ym1, YM_CLOCK, 1.0f, 1.0f);
    G.in0 = G.in1 = G.in2 = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(G.mem, 0, 0x6000);                // (work RAM and video RAM; the ROM stays)
    m6809d_reset(&G.main);
    zd_reset(&G.sound);
    ym2203d_reset(&G.ym0);
    ym2203d_reset(&G.ym1);
    G.bank = G.soundlatch = 0;
    G.scroll_x = G.scroll_y = 0;
    G.flip = false;
    G.frame_main = G.frame_snd = 0;
    G.sound_irq_accum = 0;
    G.dsw_a = 0xdf;
    G.dsw_b = 0x7b;
    memset(G.sprite_buffer, 0, sizeof G.sprite_buffer);
    memset(G.palette_ram, 0, sizeof G.palette_ram);
    memset(G.palette, 0, sizeof G.palette);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_c = (double)MAIN_CLOCK / FPS / SCANLINES, snd_c = (double)SOUND_CLOCK / FPS / SCANLINES;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 246) {
            update_video();
            m6809d_set_irq(&G.main, M6809D_HOLD);
        }
        m6809d_run(&G.main, (int)(main_c + G.frame_main));
        G.frame_main = (main_c + G.frame_main) - (int)(main_c + G.frame_main);
        zd_run(&G.sound, (int)(snd_c + G.frame_snd));
        G.frame_snd = (snd_c + G.frame_snd) - (int)(snd_c + G.frame_snd);
    }
    const int samples = (int)((double)RATE / FPS + 0.5);
    for (int i = 0; i < samples; i++) {
        int32_t s = ym2203d_update(&G.ym0) + ym2203d_update(&G.ym1);
        G.audio[i] = arc_clamp16(s * 8);
    }
    G.naudio = samples;
    if (audio) *audio = G.audio;
    return G.naudio;
}

const uint16_t *arc_frame(void) { return G.fb; }

static uint8_t joy(unsigned b)
{
    uint8_t v = 0xff;
    if (b & AP_RIGHT) v &= ~0x01;
    if (b & AP_LEFT) v &= ~0x02;
    if (b & AP_DOWN) v &= ~0x04;
    if (b & AP_UP) v &= ~0x08;
    if (b & AP_B1) v &= ~0x10;
    if (b & AP_B2) v &= ~0x20;
    return v;
}

void arc_input(const arc_in *in)
{
    G.in1 = joy(in->p[0]);
    G.in2 = joy(in->p[1]);
    G.in0 = 0xff;
    if (in->p[0] & AP_START) G.in0 &= ~0x01;
    if (in->p[1] & AP_START) G.in0 &= ~0x02;
    if (in->coin1) G.in0 &= ~0x40;
    if (in->coin2) G.in0 &= ~0x80;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) G.dsw_a = v;
    if (bank == 1) G.dsw_b = v;
}
