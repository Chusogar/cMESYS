// arabian.c - Arabian (Sun Electronics, 1983) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/arabian (dsp-emulator's
// arabian_hw.pas, MAME's arabian.cpp). A Z80 at 3 MHz drawing in a 256x256
// bitmap of two 4-bit planes, directly and with a blitter from the graphics
// ROMs, an AY-3-8910 (its ports: the palette bank and the MCU's IRQ and
// reset) and an MB8841 MCU reading the inputs and sharing the work RAM.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mb88xx/mb88xx.h"
#include "../../../sound/ay8910.h"

#define CLOCK 3000000
#define AY_CLOCK 1500000
#define MCU_CLOCK 2000000
#define FPS 60.0
#define SCANLINES 256
#define RATE 44100
#define W 234
#define H 256

static const char *const kSets[] = { "arabian", NULL };
static const arc_rom kMain[] = {
    {"ic1rev2.87", 0x2000, 0x0000, 0x5e1c98b8}, {"ic2rev2.88", 0x2000, 0x2000, 0x092f587e},
    {"ic3rev2.89", 0x2000, 0x4000, 0x15145f23}, {"ic4rev2.90", 0x2000, 0x6000, 0x32b77b44}};
static const arc_rom kMcu[] = {{"sun-8212.ic3", 0x800, 0, 0x8869611e}};
static const arc_rom kGfx[] = {
    {"tvg-91.ic84", 0x2000, 0x0000, 0xc4637822}, {"tvg-92.ic85", 0x2000, 0x2000, 0xf7c6866d},
    {"tvg-93.ic86", 0x2000, 0x4000, 0x71acd48d}, {"tvg-94.ic87", 0x2000, 0x6000, 0x82160b9a}};

const arc_info arc_game = { "arabian", "Arabian", "Sun Electronics", 1983, W, H, FPS, RATE, kSets,
                            { "KICK", NULL, NULL, NULL }, ARC_JOY_4WAY };

static struct state {
    zd_z80 cpu;
    mb88 mcu;
    ay8910 ay;
    uint8_t rom[0x8000], mcu_rom[0x800], ram[0x800], video_ram[0x10000], gfx[0x10000], blitter[8];
    uint16_t palette[0x2000];
    uint16_t *fb;
    uint8_t video_control, mcu_p, mcu_o, mcu_r[4], in0, in1, in2, in3, dsw_a, dsw_b;
    bool flip;
    double mcu_acc;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *A_;
#define A (*A_)

// a CPU write: four pixels in the planes blitter[0] enables
static void video_ram_write(uint16_t a, uint8_t v)
{
    uint8_t *p = &A.video_ram[(a & 0xff) * 256 + (uint8_t)((a >> 8) << 2)];
    const uint8_t m = A.blitter[0];
    if (m & 8) {
        p[0] = (uint8_t)((p[0] & 0xfc) | ((v & 0x10) >> 3) | (v & 0x01));
        p[1] = (uint8_t)((p[1] & 0xfc) | ((v & 0x20) >> 4) | ((v & 0x02) >> 1));
        p[2] = (uint8_t)((p[2] & 0xfc) | ((v & 0x40) >> 5) | ((v & 0x04) >> 2));
        p[3] = (uint8_t)((p[3] & 0xfc) | ((v & 0x80) >> 6) | ((v & 0x08) >> 3));
    }
    if (m & 4) {
        p[0] = (uint8_t)((p[0] & 0xf3) | ((v & 0x10) >> 1) | ((v & 0x01) << 2));
        p[1] = (uint8_t)((p[1] & 0xf3) | ((v & 0x20) >> 2) | ((v & 0x02) << 1));
        p[2] = (uint8_t)((p[2] & 0xf3) | ((v & 0x40) >> 3) | (v & 0x04));
        p[3] = (uint8_t)((p[3] & 0xf3) | ((v & 0x80) >> 4) | ((v & 0x08) >> 1));
    }
    if (m & 2) {
        p[0] = (uint8_t)((p[0] & 0xcf) | ((v & 0x10) << 1) | ((v & 0x01) << 4));
        p[1] = (uint8_t)((p[1] & 0xcf) | (v & 0x20) | ((v & 0x02) << 3));
        p[2] = (uint8_t)((p[2] & 0xcf) | ((v & 0x40) >> 1) | ((v & 0x04) << 2));
        p[3] = (uint8_t)((p[3] & 0xcf) | ((v & 0x80) >> 2) | ((v & 0x08) << 1));
    }
    if (m & 1) {
        p[0] = (uint8_t)((p[0] & 0x3f) | ((v & 0x10) << 3) | ((v & 0x01) << 6));
        p[1] = (uint8_t)((p[1] & 0x3f) | ((v & 0x20) << 2) | ((v & 0x02) << 5));
        p[2] = (uint8_t)((p[2] & 0x3f) | ((v & 0x40) << 1) | ((v & 0x04) << 4));
        p[3] = (uint8_t)((p[3] & 0x3f) | (v & 0x80) | ((v & 0x08) << 3));
    }
}

// the blitter: (sx + 1) x (sy + 1) groups of 4 pixels from the ROMs, pen 8 clear
static void blit_area(uint8_t plane, uint16_t src, uint8_t x, uint8_t y, uint8_t sx, uint8_t sy)
{
    uint16_t s = (uint16_t)(src * 4);
    for (int i = 0; i <= sx; i++) {
        for (int j = 0; j <= sy; j++) {
            const uint8_t p1 = A.gfx[s++], p2 = A.gfx[s++], p3 = A.gfx[s++], p4 = A.gfx[s++];
            const uint32_t base = (uint32_t)(uint8_t)(y + j) * 256 + x;
            const uint8_t px[4] = {p4, p3, p2, p1};
            for (int k = 0; k < 4; k++) {
                if (px[k] == 8) continue;
                uint8_t *d = &A.video_ram[(base + k) & 0xffff];
                if (plane & 1) *d = (uint8_t)((*d & 0x0f) | (px[k] << 4));
                if (plane & 4) *d = (uint8_t)((*d & 0xf0) | px[k]);
            }
        }
        x = (uint8_t)(x + 4);
    }
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return A.rom[a];
    if (a >= 0xc000 && a <= 0xc1ff) return A.in3;
    if (a >= 0xc200 && a <= 0xc3ff) return A.dsw_a;
    if (a >= 0xd000 && a <= 0xdfff) return A.ram[a & 0x7ff];
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x8000) return;
    if (a < 0xc000) video_ram_write(a, v);
    else if (a >= 0xd000 && a <= 0xdfff) A.ram[a & 0x7ff] = v;
    else if (a >= 0xe000 && a <= 0xefff) {
        A.blitter[a & 7] = v;
        if ((a & 7) == 6)
            blit_area(A.blitter[0], (uint16_t)(A.blitter[1] | (A.blitter[2] << 8)), (uint8_t)(A.blitter[4] << 2),
                      A.blitter[3], A.blitter[6], A.blitter[5]);
    }
}

// the AY's ports: A the palette bank, B the MCU's /IRQ (bit 5) and /RESET
// (bit 4); written on a write to them and when their direction changes
// (0xff as an input), as dsp-cpp's AY8910
static void port_a(uint8_t v) { A.video_control = (uint8_t)(v >> 3); }
static void port_b(uint8_t v)
{
    mb88_set_irq(&A.mcu, (v & 0x20) == 0);
    mb88_set_reset_line(&A.mcu, (v & 0x10) == 0);
}

static void ay_data_w(uint8_t v)
{
    const int16_t last = A.ay.last_enable;
    const uint8_t reg = A.ay.latch;
    ay8910_data_w(&A.ay, v);
    if (reg == 14) port_a(v);
    else if (reg == 15) port_b(v);
    else if (reg == 7) {
        const uint8_t e = A.ay.regs[7];
        if (last == -1 || ((last & 0x40) != (e & 0x40))) port_a((e & 0x40) ? A.ay.regs[14] : 0xff);
        if (last == -1 || ((last & 0x80) != (e & 0x80))) port_b((e & 0x80) ? A.ay.regs[15] : 0xff);
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void main_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    if (p >= 0xc800 && p <= 0xc9ff) ay8910_address_w(&A.ay, v);
    else if (p >= 0xca00 && p <= 0xcbff) ay_data_w(v);
}

// the MCU: the input multiplexer and the shared work RAM
static uint8_t mcu_r_read(void *u, int port)
{
    (void)u;
    uint8_t v = A.mcu_r[port];
    if (port == 0) v |= 4;              // the RAM mode
    return v;
}

static void mcu_r_write(void *u, int port, uint8_t v)
{
    (void)u;
    if (port == 0) {
        const uint16_t ra = (uint16_t)(((A.mcu_p & 7) << 8) | A.mcu_o);
        if (~v & 2) A.ram[ra & 0x7ff] = (uint8_t)(0xf0 | A.mcu_r[3]);
        A.flip = (v & 8) != 0;
    }
    A.mcu_r[port] = v & 0x0f;
}

static uint8_t mcu_k_read(void *u)
{
    (void)u;
    uint8_t v = 0x0f;
    if (~A.mcu_r[0] & 1) v = A.ram[(((A.mcu_p & 7) << 8) | A.mcu_o) & 0x7ff];
    else {
        const uint8_t sel = (uint8_t)(((A.mcu_r[2] << 4) | A.mcu_r[1]) & 0x3f);
        for (int i = 0; i < 6; i++)
            if (~sel & (1 << i)) {
                v = i == 0 ? A.in0 : i == 1 ? A.in1 : i == 2 ? A.in2 : i < 5 ? 0 : A.dsw_b;
                break;
            }
    }
    return v & 0x0f;
}

static void mcu_o_write(void *u, uint8_t v) { (void)u; A.mcu_o = v; }
static void mcu_p_write(void *u, uint8_t v) { (void)u; A.mcu_p = v & 0x0f; }

static void cycles(int c)
{
    A.audio_acc += (int64_t)c * RATE;
    while (A.audio_acc >= CLOCK) {
        A.audio_acc -= CLOCK;
        if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(ay8910_render(&A.ay));
    }
}

static void create_palette(void)
{
    for (int i = 0; i < 0x2000; i++) {
        const int ena = (i >> 12) & 1, enb = (i & 0x200) ? 1 : 0;
        const int abhf = ((i >> 10) & 1) ^ 1, aghf = ((i >> 9) & 1) ^ 1, arhf = ((i >> 8) & 1) ^ 1;
        const int az = (i >> 7) & 1, ar = (i >> 6) & 1, ag = (i >> 5) & 1, ab = (i >> 4) & 1;
        const int bz = (i >> 3) & 1, br = (i >> 2) & 1, bg = (i >> 1) & 1, bb = i & 1;
        const bool planea = ((az | ar | ag | ab) & ena) != 0;
        int rhi, rlo, ghi, glo;
        if (planea) {
            rhi = ar;
            rlo = ((arhf ^ 1) & az) ? 0 : ar;
            ghi = ag;
            glo = ((aghf ^ 1) & az) ? 0 : ag;
        } else {
            rhi = bz * enb;
            rlo = br * enb;
            ghi = bb * enb;
            glo = bg * enb;
        }
        const int bbase = ((abhf ^ 1) & az) ? 0 : ab;
        int r = (int)lround(rhi * 115.7 + rlo * 77.3);
        if (rhi | rlo) r = (int)lround(rhi * 115.7 + rlo * 77.3 + 62);
        int g = (int)lround(ghi * 117.9588 + glo * 75.0411);
        if (ghi | glo) g = (int)lround(ghi * 117.9588 + glo * 75.0411 + 62);
        const int b = ab * 192 + bbase * 63;
        A.palette[i] = arc_rgb565((uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b));
    }
}

static void update_video(void)
{
    // turned a quarter: the picture's column ox is the bitmap's row ox + 11
    const uint16_t *pal = A.palette + ((A.video_control & 0x1f) << 8);
    for (int oy = 0; oy < H; oy++)
        for (int ox = 0; ox < W; ox++) {
            const int sx = A.flip ? W - 1 - ox : ox, sy = A.flip ? H - 1 - oy : oy;
            A.fb[oy * W + ox] = pal[A.video_ram[(sx + 11) * 256 + 255 - sy]];
        }
}

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state))) || !(A.fb = (uint16_t *)calloc(W * H, 2))) return false;
    uint8_t *g = (uint8_t *)calloc(1, 0x8000);
    if (!g || !ARC_LOAD(kMain, A.rom, sizeof A.rom) || !ARC_LOAD(kMcu, A.mcu_rom, sizeof A.mcu_rom) || !ARC_LOAD(kGfx, g, 0x8000))
        return false;
    for (int f = 0; f < 0x4000; f++) {  // two ROM halves: 4 bits a pixel, 4 pixels a pair of bytes
        uint8_t v1 = g[f], v2 = g[f + 0x4000];
        for (int p = 3; p >= 0; p--) {
            A.gfx[f * 4 + p] = (uint8_t)((v1 & 1) | ((v1 & 0x10) >> 3) | ((v2 & 1) << 2) | ((v2 & 0x10) >> 1));
            v1 >>= 1;
            v2 >>= 1;
        }
    }
    free(g);
    create_palette();
    A.cpu.read = main_read;
    A.cpu.write = main_write;
    A.cpu.in = in_ff;
    A.cpu.out = main_out;
    zd_init(&A.cpu);
    mb88_init(&A.mcu, MB8841, A.mcu_rom);
    A.mcu.read_k = mcu_k_read;
    A.mcu.write_o = mcu_o_write;
    A.mcu.write_p = mcu_p_write;
    A.mcu.read_r = mcu_r_read;
    A.mcu.write_r = mcu_r_write;
    ay8910_init(&A.ay, AY_CLOCK, RATE);
    A.dsw_a = 0x06;
    A.dsw_b = 0x0f;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&A.cpu);
    mb88_reset(&A.mcu);
    mb88_set_reset_line(&A.mcu, false);
    ay8910_reset(&A.ay);
    port_a(0xff);                       // (the AY's reset: both ports inputs)
    port_b(0xff);
    memset(A.ram, 0, sizeof A.ram);
    memset(A.video_ram, 0, sizeof A.video_ram);
    memset(A.blitter, 0, sizeof A.blitter);
    A.video_control = 0;
    A.flip = false;
    A.mcu_p = A.mcu_o = 0;
    memset(A.mcu_r, 0, sizeof A.mcu_r);
    A.in0 = 1;
    A.in1 = A.in2 = A.in3 = 0;
    A.mcu_acc = 0;
    A.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int slice = (int)(CLOCK / FPS) / SCANLINES;
    const double mcu_slice = (double)MCU_CLOCK / 6.0 / FPS / SCANLINES;
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 244) {
            update_video();
            zd_set_irq(&A.cpu, ZD_HOLD, 0xff);
        }
        zd_run(&A.cpu, slice);
        cycles(slice);
        A.mcu_acc += mcu_slice;
        const int n = (int)A.mcu_acc;
        A.mcu_acc -= n;
        A.mcu.icount = 0;               // (dsp-cpp's MB88 drops a call's overshoot)
        mb88_run(&A.mcu, n);
    }
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return A.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0];
    A.in0 = 1;
    A.in1 = A.in2 = A.in3 = 0;
    if (p1 & AP_RIGHT) A.in1 |= 1;
    if (p1 & AP_LEFT) A.in1 |= 2;
    if (p1 & AP_UP) A.in1 |= 4;
    if (p1 & AP_DOWN) A.in1 |= 8;
    if (p1 & AP_B1) A.in2 |= 1;
    if (in->coin1) A.in3 |= 1;
    if (in->coin2) A.in3 |= 2;
    if (p1 & AP_START) A.in0 |= 2;
    if (in->p[1] & AP_START) A.in0 |= 4;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw_a = v;
    else if (bank == 1) A.dsw_b = v & 0x0f;
}
