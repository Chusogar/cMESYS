// mrdo.c - Mr. Do! (Universal, 1982) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/mrdo (from dsp-emulator's mrdo_hw.pas).
// A Z80 at 4.1 MHz, two SN76496, two 32x32 tile layers (the background one
// scrolled) with a priority bit each, 64 16x16 sprites; a vertical monitor.
//
// dsp-cpp keeps the layers in five 256x256 surfaces redrawn where the tiles
// change; here every pixel of the picture is worked out from the tiles when
// it is drawn, in the same order (the same result: those surfaces only ever
// hold what the tiles say), so the board fits in little memory.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/sn76496.h"

#define CLOCK 4100000
#define FPS 59.94323742
#define SCANLINES 262
#define RATE 44100
#define W 192
#define H 240

static const char *const kSets[] = { "mrdo", NULL };
static const arc_rom kMain[] = {
    {"a4-01.bin", 0x2000, 0x0000, 0x03dcfba2}, {"c4-02.bin", 0x2000, 0x2000, 0x0ecdd39c},
    {"e4-03.bin", 0x2000, 0x4000, 0x358f5dc2}, {"f4-04.bin", 0x2000, 0x6000, 0xf4190cfc}};
static const arc_rom kProms[] = {
    {"u02--2.bin", 0x20, 0x00, 0x238a65d7}, {"t02--3.bin", 0x20, 0x20, 0xae263dc0}, {"f10--1.bin", 0x20, 0x40, 0x16ee4ca2}};
static const arc_rom kChar1[] = {{"s8-09.bin", 0x1000, 0x0000, 0xaa80c5b6}, {"u8-10.bin", 0x1000, 0x1000, 0xd20ec85b}};
static const arc_rom kChar2[] = {{"r8-08.bin", 0x1000, 0x0000, 0xdbdc9ffa}, {"n8-07.bin", 0x1000, 0x1000, 0x4b9973db}};
static const arc_rom kSprites[] = {{"h5-05.bin", 0x1000, 0x0000, 0xe1218cc5}, {"k5-06.bin", 0x1000, 0x1000, 0xb1f68b04}};

const arc_info arc_game = { "mrdo", "Mr. Do!", "Universal", 1982, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_4WAY };

static const int kCharX[8] = {7, 6, 5, 4, 3, 2, 1, 0};
static const int kCharY[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kSprX[16] = {3, 2, 1, 0, 8 + 3, 8 + 2, 8 + 1, 8 + 0, 16 + 3, 16 + 2, 16 + 1, 16 + 0, 24 + 3, 24 + 2, 24 + 1, 24 + 0};
static const int kSprY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};

static struct {
    zd_z80 cpu;
    sn76496 sn0, sn1;
    uint8_t mem[0x10000];
    uint16_t palette[256];
    uint8_t sprite_lut[0x40];
    uint8_t *chars_fg, *chars_bg, *sprites;
    uint16_t fb[W * H];
    uint8_t scroll_x, scroll_y, in0, in1, dsw_a, dsw_b;
    bool flip;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} D;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x8fff || (a >= 0xe000 && a <= 0xefff)) return D.mem[a];
    if (a >= 0x9000 && a <= 0x90ff) return D.mem[a];
    if (a == 0x9803) return D.mem[(uint16_t)((D.cpu.h << 8) | D.cpu.l)];   // protection (mrdo_hw.pas)
    if (a == 0xa000) return D.in0;
    if (a == 0xa001) return D.in1;
    if (a == 0xa002) return D.dsw_a;
    if (a == 0xa003) return D.dsw_b;
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x8fff || (a >= 0x9000 && a <= 0x90ff) || (a >= 0xe000 && a <= 0xefff)) D.mem[a] = v;
    else if (a == 0x9800) D.flip = (v & 1) != 0;
    else if (a == 0x9801) sn76496_write(&D.sn0, v);
    else if (a == 0x9802) sn76496_write(&D.sn1, v);
    else if (a >= 0xf000 && a <= 0xf7ff) D.scroll_y = v;
    else if (a >= 0xf800) D.scroll_x = v;
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void cycles(void *u, int c)
{
    (void)u;
    D.audio_acc += (int64_t)c * RATE;
    while (D.audio_acc >= CLOCK) {
        D.audio_acc -= CLOCK;
        const int32_t s = (sn76496_update(&D.sn0) + sn76496_update(&D.sn1)) / 2;
        if (D.naudio < 1024) D.audio[D.naudio++] = arc_clamp16(s);
    }
}

static void build_palette(const uint8_t *prom)
{
    const double r1 = 150.0, r2 = 120.0, r3 = 100.0, r4 = 75.0, pull = 220.0, adj = 0.7;
    double weight[16], pot[16];
    for (int f = 15; f >= 0; f--) {
        double par = 0.0;
        if (f & 1) par += 1.0 / r1;
        if (f & 2) par += 1.0 / r2;
        if (f & 4) par += 1.0 / r3;
        if (f & 8) par += 1.0 / r4;
        if (par != 0.0) {
            par = 1.0 / par;
            pot[f] = pull / (pull + par) - adj;
        } else {
            pot[f] = 0.0;
        }
        weight[f] = 255.0 * pot[f] / pot[15];
        if (weight[f] < 0.0) weight[f] = 0.0;
    }
    for (int f = 0; f < 0x100; f++) {
        const int a1 = ((f >> 3) & 0x1c) + (f & 3) + 0x20, a2 = (f & 0x1c) + (f & 3);
        const int red = (int)trunc(weight[(prom[a1] & 3) + ((prom[a2] & 3) << 2)]);
        const int green = (int)trunc(weight[((prom[a1] >> 2) & 3) + (((prom[a2] >> 2) & 3) << 2)]);
        const int blue = (int)trunc(weight[((prom[a1] >> 4) & 3) + (((prom[a2] >> 4) & 3) << 2)]);
        D.palette[f] = arc_rgb565((uint32_t)red << 16 | (uint32_t)green << 8 | (uint32_t)blue);
    }
    for (int f = 0; f < 0x40; f++) {
        uint8_t b = prom[0x40 + (f & 0x1f)];
        b = (f & 0x20) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0f);
        D.sprite_lut[f] = (uint8_t)(b + ((b & 0x0c) << 3));
    }
}

// a layer's pixel at (x, y) of its 256x256 plane: the tile's priority, pen, colour
static inline void tile_px(int layer, int x, int y, bool *pri, int *pen, int *color)
{
    const int tx = x >> 3, ty = 31 - (y >> 3);
    const int off = tx * 32 + ty;
    const uint16_t base = layer ? 0x8800 : 0x8000;
    const uint8_t attr = D.mem[base + off];
    const int code = D.mem[base + 0x400 + off] + ((attr & 0x80) << 1);
    *pri = (attr & 0x40) != 0;
    *color = (attr & 0x3f) << 2;
    *pen = (layer ? D.chars_fg : D.chars_bg)[code * 64 + (y & 7) * 8 + (x & 7)];
}

static inline void out_px(int cx, int cy, uint16_t c)       // composite (256x256) -> picture
{
    const int x = cx - 32, y = cy - 8;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    if (D.flip) D.fb[(H - 1 - y) * W + (W - 1 - x)] = c;
    else D.fb[y * W + x] = c;
}

static void update_video(void)
{
    // the layers in mrdo_hw.pas's order: scrolled BG opaque, FG opaque,
    // scrolled BG transparent, FG transparent
    for (int y = 8; y < 8 + H; y++)
        for (int x = 32; x < 32 + W; x++) {
            bool bp, fp;
            int bpen, bcol, fpen, fcol;
            tile_px(0, (x + D.scroll_x) & 0xff, (y + D.scroll_y) & 0xff, &bp, &bpen, &bcol);
            tile_px(1, x, y, &fp, &fpen, &fcol);
            uint16_t c = bp ? D.palette[bpen + bcol] : D.palette[0];
            if (fp) c = D.palette[fpen + fcol];
            if (!bp && bpen) c = D.palette[bpen + bcol];
            if (!fp && fpen) c = D.palette[fpen + fcol];
            out_px(x, y, c);
        }
    for (int i = 0x3f; i >= 0; i--) {
        const int base = 0x9000 + i * 4;
        const uint8_t xr = D.mem[base + 1];
        if (xr == 0) continue;
        const int code = D.mem[base] & 0x7f;
        const uint8_t attr = D.mem[base + 2];
        const int color = (attr & 0x0f) << 2;
        const int py = (240 - D.mem[base + 3]) & 0xff, px = (256 - xr) & 0xff;
        const bool fx = (attr & 0x20) != 0, fy = (attr & 0x10) != 0;
        const uint8_t *g = D.sprites + code * 256;
        for (int y = 0; y < 16; y++) {
            const int sy = (py + y) & 0xff, gy = fy ? 15 - y : y;
            for (int x = 0; x < 16; x++) {
                const uint8_t pen = g[gy * 16 + (fx ? 15 - x : x)];
                if (pen) out_px((px + x) & 0xff, sy, D.palette[D.sprite_lut[(pen + color) & 0x3f]]);
            }
        }
    }
}

bool arc_init(void)
{
    uint8_t *buf = (uint8_t *)calloc(1, 0x2000);
    uint8_t prom[0x60];
    if (!buf) return false;
    if (!ARC_LOAD(kMain, D.mem, 0x8000) || !ARC_LOAD(kProms, prom, sizeof prom)) return false;
    arc_layout cl = { 8, 8, 512, 2, 64, false, false, true, {0, 512 * 64}, kCharX, kCharY };
    arc_layout sl = { 16, 16, 128, 2, 512, false, false, true, {4, 0}, kSprX, kSprY };
    if (!ARC_LOAD(kChar1, buf, 0x2000) || !(D.chars_fg = arc_gfx_decode(&cl, buf, 0x2000))) return false;
    if (!ARC_LOAD(kChar2, buf, 0x2000) || !(D.chars_bg = arc_gfx_decode(&cl, buf, 0x2000))) return false;
    if (!ARC_LOAD(kSprites, buf, 0x2000) || !(D.sprites = arc_gfx_decode(&sl, buf, 0x2000))) return false;
    free(buf);
    build_palette(prom);
    D.dsw_a = 0xdf;
    D.dsw_b = 0xff;
    D.cpu.read = rd;
    D.cpu.write = wr;
    D.cpu.in = in_ff;
    D.cpu.out = out_nop;
    D.cpu.cycles = cycles;
    zd_init(&D.cpu);
    sn76496_init(&D.sn0, CLOCK, RATE, 1.0f);
    sn76496_init(&D.sn1, CLOCK, RATE, 1.0f);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&D.cpu);
    sn76496_reset(&D.sn0);
    sn76496_reset(&D.sn1);
    D.scroll_x = D.scroll_y = 0;
    D.flip = false;
    D.in0 = D.in1 = 0xff;
    D.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int per_line = (int)(CLOCK / FPS / SCANLINES);
    D.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 224) {
            zd_set_irq(&D.cpu, ZD_HOLD, 0xff);
            update_video();
        }
        zd_run(&D.cpu, per_line);
    }
    if (audio) *audio = D.audio;
    return D.naudio;
}

const uint16_t *arc_frame(void) { return D.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    D.in0 = D.in1 = 0xff;
    if (p1 & AP_LEFT) D.in0 &= 0xfe;
    if (p1 & AP_DOWN) D.in0 &= 0xfd;
    if (p1 & AP_RIGHT) D.in0 &= 0xfb;
    if (p1 & AP_UP) D.in0 &= 0xf7;
    if (p1 & AP_B1) D.in0 &= 0xef;
    if (p1 & AP_START) D.in0 &= 0xdf;
    if (p2 & AP_START) D.in0 &= 0xbf;
    if (p2 & AP_LEFT) D.in1 &= 0xfe;
    if (p2 & AP_DOWN) D.in1 &= 0xfd;
    if (p2 & AP_RIGHT) D.in1 &= 0xfb;
    if (p2 & AP_UP) D.in1 &= 0xf7;
    if (p2 & AP_B1) D.in1 &= 0xef;
    if (in->coin1) D.in1 &= 0xbf;
    if (in->coin2) D.in1 &= 0x7f;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) D.dsw_a = v;
    if (bank == 1) D.dsw_b = v;
}
