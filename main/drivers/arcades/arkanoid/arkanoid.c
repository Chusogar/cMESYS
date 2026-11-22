// arkanoid.c - Arkanoid (Taito, 1986) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/arkanoid. A Z80 at 6 MHz, the
// 68705 MCU (dsp-cpp's hookup: taito68705dsp, which also reads the paddle),
// a YM2149 (its port B reads DIP switch A), a 32x32 tile layer and 16 sprites
// of two tiles each, on a vertical monitor. The paddle: left / right move it
// 8 steps a frame.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"
#include "../../../machine/taito68705dsp.h"

#define CPU_CLOCK 6000000
#define MCU_CLOCK 3000000
#define AY_CLOCK 3000000
#define SCANLINES 264
#define FPS 59.185608
#define RATE 44100
#define W 224
#define H 256

static const char *const kSets[] = { "arkanoid", NULL };
static const arc_rom kMain[] = {
    {"a75-01-1.ic17|a75__01-1.ic17|a75-01.ic17", 0x8000, 0x0000, 0x5bcda3b0},
    {"a75-11.ic16|a75__11.ic16", 0x8000, 0x8000, 0xeafd7191}};
static const arc_rom kMcu[] = {
    {"a75__06.ic14|a75-06.ic14|a75-06__bootleg_68705.ic14|arkanoid_mcu.ic14", 0x800, 0, 0x0be83647}};
static const arc_rom kTiles[] = {
    {"a75-03.ic64|a75__03.ic64", 0x8000, 0x00000, 0x038b74ba}, {"a75-04.ic63|a75__04.ic63", 0x8000, 0x08000, 0x71fae199},
    {"a75-05.ic62|a75__05.ic62", 0x8000, 0x10000, 0xc76374e2}};
static const arc_rom kProms[] = {
    {"a75-07.ic24", 0x200, 0x000, 0x0af8b289}, {"a75-08.ic23", 0x200, 0x200, 0xabb002fb}, {"a75-09.ic22", 0x200, 0x400, 0xa7c6c277}};

const arc_info arc_game = { "arkanoid", "Arkanoid", "Taito", 1986, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_2WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};

static struct {
    zd_z80 cpu;
    t68705d mcu;
    ay8910 ay;
    uint8_t *mem;                   // (the big buffers on the heap: PSRAM where there is one)
    uint8_t *tiles;
    uint16_t palette[0x200];
    uint16_t *fb;
    uint8_t in0, in1, dswa, palette_bank, gfx_bank, paddle_select, paddle_x[2];
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} A;

static uint8_t paddle(void *u) { (void)u; return A.paddle_x[A.paddle_select & 1]; }

static uint8_t ay_read(void)
{
    if (A.ay.latch == 14) A.ay.regs[14] = 0xff;     // port A: nothing
    if (A.ay.latch == 15) A.ay.regs[15] = A.dswa;   // port B: DIP switch A
    return ay8910_data_r(&A.ay);
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xbfff || (a >= 0xe000 && a <= 0xefff)) return A.mem[a];
    if (a >= 0xc000 && a <= 0xcfff) return A.mem[0xc000 + (a & 0x7ff)];
    if (a >= 0xd000 && a <= 0xdfff) {
        const uint8_t off = (uint8_t)(a & 0x1f);
        if (off == 1 || off == 3 || off == 5 || off == 7) return ay_read();
        if (off >= 8 && off <= 0x0b) return 0xff;
        if (off >= 0x0c && off <= 0x0f) return (uint8_t)((!A.mcu.mcu_sent ? 0x80 : 0) | (!A.mcu.main_sent ? 0x40 : 0) | A.in0);
        if (off >= 0x10 && off <= 0x17) return A.in1;
        if (off >= 0x18) return t68705d_read(&A.mcu);
    }
    return 0;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a >= 0xc000 && a <= 0xcfff) {
        A.mem[0xc000 + (a & 0x7ff)] = v;
    } else if (a >= 0xd000 && a <= 0xdfff) {
        const uint8_t off = (uint8_t)(a & 0x1f);
        if (off == 0 || off == 2 || off == 4 || off == 6) ay8910_address_w(&A.ay, v);
        else if (off == 1 || off == 3 || off == 5 || off == 7) ay8910_data_w(&A.ay, v);
        else if (off >= 8 && off <= 0x0f) {
            A.paddle_select = (uint8_t)((v >> 2) & 1);
            A.gfx_bank = (uint8_t)((v >> 5) & 1);
            A.palette_bank = (uint8_t)((v & 0x40) >> 1);
            t68705d_set_reset(&A.mcu, (v & 0x80) == 0);     // bit 7: the MCU runs
        } else if (off >= 0x18) {
            t68705d_write(&A.mcu, v);
        }
    } else if (a >= 0xe000) {
        A.mem[a] = v;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void update_video(void)
{
    // the tiles, opaque (the picture skips the layer's first 16 columns)
    for (int f = 0; f < 0x400; f++) {
        const int x = 31 - (f >> 5), y = f & 0x1f;
        const uint8_t at = A.mem[0xe000 + f * 2];
        const int color = (((at & 0xf8) >> 3) + A.palette_bank) << 3;
        const int nchar = A.mem[0xe001 + f * 2] + ((at & 7) << 8) + 2048 * A.gfx_bank;
        const uint8_t *g = A.tiles + (nchar & 0xfff) * 64;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                const int px = x * 8 + c - 16;
                if (px < 0 || px >= W) continue;
                A.fb[(y * 8 + r) * W + px] = A.palette[(color + g[r * 8 + c]) & 0x1ff];
            }
    }
    for (int f = 0; f < 16; f++) {
        const uint8_t at = A.mem[0xe802 + f * 4];
        const int nchar = A.mem[0xe803 + f * 4] + ((at & 3) << 8) + 1024 * A.gfx_bank;
        const int color = (((at & 0xf8) >> 3) + A.palette_bank) << 3;
        const int x = A.mem[0xe801 + f * 4], y = A.mem[0xe800 + f * 4];
        for (int half = 0; half < 2; half++) {
            const uint8_t *g = A.tiles + ((2 * nchar + half) & 0xfff) * 64;
            const int dx = half ? x : x + 8;
            for (int r = 0; r < 8; r++) {
                const int py = y + r;
                if (py >= 256) continue;
                for (int c = 0; c < 8; c++) {
                    const int px = dx + c;
                    if (px >= 256 || px - 16 < 0 || px - 16 >= W) continue;
                    const uint8_t pix = g[r * 8 + c];
                    if (pix) A.fb[py * W + px - 16] = A.palette[(color + pix) & 0x1ff];
                }
            }
        }
    }
}

bool arc_init(void)
{
    uint8_t *gfx = (uint8_t *)calloc(1, 0x18000);
    uint8_t prom[0x600];
    A.mem = (uint8_t *)calloc(1, 0x10000);
    A.fb = (uint16_t *)calloc(W * H, 2);
    if (!gfx || !A.mem || !A.fb) return false;
    t68705d_init(&A.mcu, T68705_ARKANOID);
    if (!ARC_LOAD(kMain, A.mem, 0x10000) || !ARC_LOAD(kMcu, A.mcu.mem, 0x800) || !ARC_LOAD(kTiles, gfx, 0x18000) ||
        !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    arc_layout l = { 8, 8, 0x1000, 3, 64, false, true, false, {2 * 4096 * 64, 4096 * 64, 0}, kX8, kY8 };
    A.tiles = arc_gfx_decode(&l, gfx, 0x18000);
    free(gfx);
    if (!A.tiles) return false;
    for (int i = 0; i < 0x200; i++) {
        const uint8_t r = prom[i] & 15, g = prom[i + 0x200] & 15, b = prom[i + 0x400] & 15;
        A.palette[i] = arc_rgb565((uint32_t)(r | r << 4) << 16 | (uint32_t)(g | g << 4) << 8 | (uint32_t)(b | b << 4));
    }
    A.dswa = 0xfe;
    A.mcu.arkanoid_read = paddle;
    A.cpu.read = rd;
    A.cpu.write = wr;
    A.cpu.in = in_ff;
    A.cpu.out = out_nop;
    zd_init(&A.cpu);
    ay8910_init(&A.ay, AY_CLOCK, RATE);
    ay8910_set_amplitude(&A.ay, 1.5f);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&A.cpu);
    t68705d_reset(&A.mcu);
    ay8910_reset(&A.ay);
    A.in0 = 0x0f;
    A.in1 = 0xff;
    A.palette_bank = A.gfx_bank = A.paddle_select = 0;
    A.paddle_x[0] = A.paddle_x[1] = 0x7f;
    A.audio_acc = 0;
    memset(A.fb, 0, W * H * 2);
}

int arc_run_frame(const int16_t **audio)
{
    const int slice = (int)(CPU_CLOCK / FPS) / SCANLINES;
    const int mcu_slice = (int)(MCU_CLOCK / FPS) / SCANLINES;
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) zd_set_irq(&A.cpu, ZD_PULSE, 0xff);
        zd_run(&A.cpu, slice);
        A.audio_acc += (int64_t)slice * RATE;
        while (A.audio_acc >= CPU_CLOCK) {
            A.audio_acc -= CPU_CLOCK;
            if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(ay8910_render(&A.ay));
        }
        t68705d_run(&A.mcu, mcu_slice);
    }
    update_video();             // (dsp-cpp draws at line 240 and again here: this one shows)
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return A.fb; }

void arc_input(const arc_in *in)
{
    A.in0 = 0x0f;
    A.in1 = 0xff;
    if (in->p[0] & AP_START) A.in0 &= ~0x01;
    if (in->p[1] & AP_START) A.in0 &= ~0x02;
    if (in->coin1) A.in0 |= 0x10;
    if (in->coin2) A.in0 |= 0x20;
    if (in->p[0] & AP_B1) A.in1 &= ~0x01;
    if (in->p[1] & AP_B1) A.in1 &= ~0x02;
    for (int p = 0; p < 2; p++) {
        if (in->p[p] & AP_LEFT) A.paddle_x[p] = (uint8_t)(A.paddle_x[p] > 8 ? A.paddle_x[p] - 8 : 0);
        if (in->p[p] & AP_RIGHT) A.paddle_x[p] = (uint8_t)(A.paddle_x[p] < 247 ? A.paddle_x[p] + 8 : 255);
    }
}

void arc_dip(int bank, uint8_t v) { if (bank == 0) A.dswa = v; }
