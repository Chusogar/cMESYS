// ambush.c - Ambush (Nippon Amuse, 1983) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/ambush (from dsp-emulator's
// ambush.pas). A Z80 at 3 MHz, two AY-3-8910 (the inputs on their port A), a
// map of 8x8 characters with a scroll per column and a priority bit, and
// 8x8 / 16x16 sprites. dsp-cpp draws layers into canvases; here each pixel
// is taken from the tiles as the picture is put together (the same result).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"

#define CLOCK (18432000 / 6)
#define AY_CLOCK (CLOCK / 2)
#define SCANLINES 264
#define FPS 60.60606060
#define RATE 44100
#define W 256
#define H 224

static const char *const kSets[] = { "ambush", NULL };
static const arc_rom kMain[] = {
    {"a1.i7", 0x2000, 0x0000, 0x31b85d9d}, {"a2.g7", 0x2000, 0x2000, 0x8328d88a},
    {"a3.f7", 0x2000, 0x4000, 0x8db57ab5}, {"a4.e7", 0x2000, 0x6000, 0x4a34d2a4}};
static const arc_rom kGfx[] = {{"fa1.m4", 0x2000, 0, 0xad10969e}, {"fa2.n4", 0x2000, 0x2000, 0xe7f134ba}};
static const arc_rom kProms[] = {{"a.bpr", 0x100, 0, 0x5f27f511}, {"b.bpr", 0x100, 0x100, 0x1b03fd3b}};

const arc_info arc_game = { "ambush", "Ambush", "Nippon Amuse", 1983, W, H, FPS, RATE, kSets,
                            { "FIRE", "FIRE 2", NULL, NULL }, ARC_JOY_8WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
    ay8910 ay0, ay1;
    uint8_t mem[0x10000];
    uint8_t *chars, *sprites;
    uint16_t palette[256];
    uint16_t compose[256 * 256];
    uint16_t *fb;
    uint16_t scroll_y[32];
    uint8_t color_bank, in0, in1, dsw;
    bool flip;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *A_;
#define A (*A_)

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x87ff || (a >= 0xc000 && a <= 0xc7ff)) return A.mem[a];
    if (a == 0xc800) return A.dsw;
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x87ff) A.mem[a] = v;
    else if (a >= 0xc080 && a <= 0xc09f) A.scroll_y[a & 0x1f] = (uint16_t)(v + 1);
    else if (a >= 0xc000 && a <= 0xc7ff) A.mem[a] = v;
    else if (a >= 0xcc00 && a <= 0xcc07) {
        if ((a & 7) == 4) A.flip = (v & 1) != 0;
        else if ((a & 7) == 5) A.color_bank = v & 3;
    }
}

static uint8_t ay_rd(ay8910 *ay, uint8_t port_a)
{
    if (ay->latch == 14) ay->regs[14] = port_a;
    return ay8910_data_r(ay);
}

static uint8_t in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0x00: return ay_rd(&A.ay0, A.in0);
    case 0x80: return ay_rd(&A.ay1, A.in1);
    default: return 0xff;
    }
}

static void out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0x00: ay8910_address_w(&A.ay0, v); break;
    case 0x01: ay8910_data_w(&A.ay0, v); break;
    case 0x80: ay8910_address_w(&A.ay1, v); break;
    case 0x81: ay8910_data_w(&A.ay1, v); break;
    default: break;
    }
}

static void draw_sprite(const uint8_t *g, int size, int dx, int dy, int color, bool fx, bool fy)
{
    for (int y = 0; y < size; y++) {
        const int py = dy + y;
        if (py < 0 || py >= 256) continue;
        const int sy = fy ? size - 1 - y : y;
        for (int x = 0; x < size; x++) {
            const int px = dx + x;
            if (px < 0 || px >= 256) continue;
            const uint8_t pen = g[sy * size + (fx ? size - 1 - x : x)];
            if (pen) A.compose[py * 256 + px] = A.palette[(color + pen) & 0xff];
        }
    }
}

static void update_video(void)
{
    // the characters, scrolled per 8-pixel column; then the sprites
    for (int col = 0; col < 32; col++) {
        const int sy = A.scroll_y[col] & 0xff;
        for (int y = 0; y < 256; y++) {
            const int yy = (y + sy) & 0xff, f = (yy >> 3) * 32 + col;
            const uint8_t attr = A.mem[0xc100 + (((f >> 2) & 0xe0) | (f & 0x1f))];
            const int code = (((attr & 0x60) << 3) | A.mem[0xc400 + f]) & 0x3ff;
            const int color = ((A.color_bank << 4) | (attr & 0x0f)) << 2;
            const uint8_t *g = A.chars + code * 64 + (yy & 7) * 8;
            for (int x = 0; x < 8; x++) A.compose[y * 256 + col * 8 + x] = A.palette[(color + g[x]) & 0xff];
        }
    }
    for (int f = 0x7f; f >= 0; f--) {
        const uint8_t *s = &A.mem[0xc200 + f * 4];
        const int x = s[3];
        int y = s[0];
        if (x == 0 && y == 0xff) continue;
        const uint8_t attr = s[2];
        const bool wrap = (attr & 0x10) != 0;
        if ((x < 0x40 && wrap) || (x >= 0xc0 && !wrap)) continue;
        const int big = attr >> 7;
        int code = ((attr & 0x60) << 1) | (s[1] & 0x3f);
        const int color = ((A.color_bank << 4) | (attr & 0x0f)) << 2;
        const bool fx = (s[1] & 0x40) != 0, fy = (s[1] & 0x80) != 0;
        if (!big) {
            code <<= 2;
            y = 248 - y;
            draw_sprite(A.chars + (code & 0x3ff) * 64, 8, x, y, color, fx, fy);
        } else {
            y = 240 - y;
            draw_sprite(A.sprites + (code & 0xff) * 256, 16, x, y, color, fx, fy);
        }
    }
    // the characters with the priority bit over the sprites (pen 0 clear)
    for (int col = 0; col < 32; col++) {
        const int sy = A.scroll_y[col] & 0xff;
        for (int y = 0; y < 256; y++) {
            const int yy = (y + sy) & 0xff, f = (yy >> 3) * 32 + col;
            const uint8_t attr = A.mem[0xc100 + (((f >> 2) & 0xe0) | (f & 0x1f))];
            if (!(attr & 0x10)) continue;
            const int code = (((attr & 0x60) << 3) | A.mem[0xc400 + f]) & 0x3ff;
            const int color = ((A.color_bank << 4) | (attr & 0x0f)) << 2;
            const uint8_t *g = A.chars + code * 64 + (yy & 7) * 8;
            for (int x = 0; x < 8; x++)
                if (g[x]) A.compose[y * 256 + col * 8 + x] = A.palette[(color + g[x]) & 0xff];
        }
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            A.fb[y * W + x] = A.flip ? A.compose[(255 - (y + 16)) * 256 + 255 - x] : A.compose[(y + 16) * 256 + x];
}

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    A.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t gfx[0x4000], prom[0x200];
    if (!A.fb || !ARC_LOAD(kMain, A.mem, 0x8000) || !ARC_LOAD(kGfx, gfx, sizeof gfx) || !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    arc_layout cl = { 8, 8, 0x400, 2, 64, false, false, false, {0x400 * 64, 0}, kX8, kY8 };
    arc_layout sl = { 16, 16, 0x100, 2, 256, false, false, false, {0x100 * 256, 0}, kX16, kY16 };
    if (!(A.chars = arc_gfx_decode(&cl, gfx, sizeof gfx)) || !(A.sprites = arc_gfx_decode(&sl, gfx, sizeof gfx))) return false;
    for (int f = 0; f < 0x100; f++) {
        const uint8_t v = prom[f];
        const int r = 0x21 * (v & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        A.palette[f] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    A.cpu.read = rd;
    A.cpu.write = wr;
    A.cpu.in = in;
    A.cpu.out = out;
    zd_init(&A.cpu);
    ay8910_init(&A.ay0, AY_CLOCK, RATE);
    ay8910_init(&A.ay1, AY_CLOCK, RATE);
    A.dsw = 0xc4;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&A.cpu);
    ay8910_reset(&A.ay0);
    ay8910_reset(&A.ay1);
    A.in0 = A.in1 = 0xff;
    A.color_bank = 0;
    A.flip = false;
    memset(A.scroll_y, 0, sizeof A.scroll_y);
    A.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    int slice = (int)(CLOCK / FPS) / SCANLINES;
    if (slice < 1) slice = 1;
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            update_video();
            zd_set_irq(&A.cpu, ZD_HOLD, 0xff);
        }
        zd_run(&A.cpu, slice);
        A.audio_acc += (int64_t)slice * RATE;
        while (A.audio_acc >= CLOCK) {
            A.audio_acc -= CLOCK;
            const int32_t s = ay8910_render(&A.ay0) + ay8910_render(&A.ay1);
            if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(s);
        }
    }
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return A.fb; }

void arc_input(const arc_in *in)
{
    uint8_t a = 0xff, b = 0xff;
    const unsigned p1 = in->p[0], p2 = in->p[1];
    if (p1 & AP_B2) a &= ~0x01;
    if (p1 & AP_B1) a &= ~0x02;
    if (p2 & AP_B2) a &= ~0x04;
    if (p2 & AP_B1) a &= ~0x08;
    if (p2 & AP_START) a &= ~0x10;
    if (p1 & AP_START) a &= ~0x20;
    if (in->coin2) a &= ~0x40;
    if (in->coin1) a &= ~0x80;
    if (p1 & AP_UP) b &= ~0x01;
    if (p1 & AP_DOWN) b &= ~0x02;
    if (p1 & AP_LEFT) b &= ~0x04;
    if (p1 & AP_RIGHT) b &= ~0x08;
    if (p2 & AP_UP) b &= ~0x10;
    if (p2 & AP_DOWN) b &= ~0x20;
    if (p2 & AP_LEFT) b &= ~0x40;
    if (p2 & AP_RIGHT) b &= ~0x80;
    A.in0 = a;
    A.in1 = b;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw = v;
}
