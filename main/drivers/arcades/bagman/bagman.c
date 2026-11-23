// bagman.c - Bagman (Valadon Automation, 1982) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/bagman (from
// dsp-emulator's bagman_hw.pas), with its PAL16R6 protection
// (machine/bagman_pal). A Z80 at 3 MHz, an AY-3-8910 (the inputs on its
// ports), 8x8 characters in two banks and 8 sprites, on a vertical monitor.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"

#define CLOCK 3072000
#define AY_CLOCK 1536000
#define SCANLINES 264
#define FPS 60.60606060
#define RATE 44100
#define W 224
#define H 256

static const char *const kSets[] = { "bagman", NULL };
static const arc_rom kMain[] = {
    {"e9_b05.bin", 0x1000, 0x0000, 0xe0156191}, {"f9_b06.bin", 0x1000, 0x1000, 0x7b758982},
    {"f9_b07.bin", 0x1000, 0x2000, 0x302a077b}, {"k9_b08.bin", 0x1000, 0x3000, 0xf04293cb},
    {"m9_b09s.bin", 0x1000, 0x4000, 0x68e83e4f}, {"n9_b10.bin", 0x1000, 0x5000, 0x1d6579f7}};
static const arc_rom kProms[] = {{"p3.bin", 0x20, 0, 0x2a855523}, {"r3.bin", 0x20, 0x20, 0xae6f1019}};
static const arc_rom kChars[] = {{"e1_b02.bin", 0x1000, 0, 0x4a0a6b55}, {"j1_b04.bin", 0x1000, 0x1000, 0xc680ef04}};
static const arc_rom kSprites[] = {{"c1_b01.bin", 0x1000, 0, 0x705193b2}, {"f1_b03s.bin", 0x1000, 0x1000, 0xdba1eda7}};

const arc_info arc_game = { "bagman", "Bagman", "Valadon Automation", 1982, W, H, FPS, RATE, kSets,
                            { "ACTION", NULL, NULL, NULL }, ARC_JOY_4WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};

// the PAL16R6's fuse map (the Bagman schematics; MAME's bagmanpal): row r,
// bit c set = that fuse intact
static const uint32_t kFuses[64] = {
    0xffffffffu, 0xfbb7b7bfu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0xffffffbbu, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xfffffbbbu, 0xfffff77bu,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0xffffbbbbu, 0xffff7f7bu, 0xffff77fbu, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0xfffbbbbbu, 0xfff7ff7bu, 0xfff7f7fbu, 0xfff77ffbu,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xffbbbbbbu, 0xff7fff7bu,
    0xff7ff7fbu, 0xff7f7ffbu, 0xff77fffbu, 0x00000000u, 0x00000000u, 0x00000000u,
    0xfbbbbbbbu, 0xf7ffff7bu, 0xf7fff7fbu, 0xf7ff7ffbu, 0xf7f7fffbu, 0xf77ffffbu,
    0x00000000u, 0x00000000u, 0xffffffffu, 0xefffffffu, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
    ay8910 ay;
    uint8_t mem[0x10000];
    uint8_t *chars0, *chars1, *sprites;
    uint16_t palette[256];
    uint16_t compose[256 * 256];
    uint16_t *fb;
    uint8_t pal_and[64], pal_col[32], pal_out[8];
    bool irq_enable, video_enable, flip;
    uint8_t in0, in1, dsw;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *B_;
#define B (*B_)

static void pal_write(int pos, uint8_t v)
{
    B.pal_col[pos * 4] = v & 1;
    B.pal_col[pos * 4 + 1] = (uint8_t)(1 - (v & 1));
}

static uint8_t pal_or(int first, int last)
{
    uint8_t v = 0;
    for (int r = first; r <= last; r++) v |= B.pal_and[r];
    return v;
}

static void pal_update(void)
{
    for (int r = 0; r < 64; r++) {
        uint8_t v = 1;
        for (int c = 0; c < 32; c++)
            if (!(kFuses[r] >> c & 1)) v &= B.pal_col[c];
        B.pal_and[r] = v;
    }
    uint8_t v = pal_or(1, 7);                       // I/O pin 19
    if (B.pal_and[0] == 1) {
        B.pal_col[2] = (uint8_t)(1 - v);
        B.pal_col[3] = v;
        B.pal_out[0] = (uint8_t)(1 - v);
    } else {
        B.pal_col[2] = 0;
        B.pal_col[3] = 1;
    }
    for (int o = 1; o <= 6; o++) {                  // registered pins 18-13
        v = pal_or(o * 8, o * 8 + 7);
        B.pal_col[o * 4 + 2] = (uint8_t)(1 - v);
        B.pal_col[o * 4 + 3] = v;
        B.pal_out[o] = (uint8_t)(1 - v);
    }
    v = pal_or(57, 63);                             // I/O pin 12
    if (B.pal_and[56] == 1) {
        B.pal_col[30] = (uint8_t)(1 - v);
        B.pal_col[31] = v;
        B.pal_out[7] = (uint8_t)(1 - v);
    } else {
        B.pal_col[30] = 0;
        B.pal_col[31] = 1;
    }
}

static void pal_reset(void)
{
    for (int p = 0; p < 8; p++) pal_write(p, 1);
    pal_update();
}

static uint8_t pal_read(void)
{
    pal_update();
    return (uint8_t)(B.pal_out[6] + (B.pal_out[5] << 1) + (B.pal_out[4] << 2) + (B.pal_out[3] << 3) +
                     (B.pal_out[2] << 4) + (B.pal_out[1] << 5));
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x67ff || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff) || a >= 0xc000) return B.mem[a];
    if (a == 0xa000) return pal_read();
    if (a == 0xb000) return B.dsw;
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x5fff || a >= 0xc000) return;
    if (a <= 0x67ff || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff)) { B.mem[a] = v; return; }
    switch (a) {
    case 0xa000: B.irq_enable = (v & 1) != 0; break;
    case 0xa001:
    case 0xa002: B.flip = (v & 1) != 1; break;
    case 0xa003: B.video_enable = (v & 1) != 0; break;
    default:
        if (a >= 0xa800 && a <= 0xa805) pal_write(a & 7, v);
        break;
    }
}

static uint8_t in(void *u, uint16_t p)
{
    (void)u;
    if ((p & 0xff) != 0x0c) return 0xff;
    if (B.ay.latch == 14) B.ay.regs[14] = B.in0;
    if (B.ay.latch == 15) B.ay.regs[15] = B.in1;
    return ay8910_data_r(&B.ay);
}

static void out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    if ((p & 0xff) == 0x08) ay8910_address_w(&B.ay, v);
    else if ((p & 0xff) == 0x09) ay8910_data_w(&B.ay, v);
}

static void cycles(void *u, int c)
{
    (void)u;
    B.audio_acc += (int64_t)c * RATE;
    while (B.audio_acc >= CLOCK) {
        B.audio_acc -= CLOCK;
        if (B.naudio < 1024) B.audio[B.naudio++] = arc_clamp16(ay8910_render(&B.ay));
    }
}

static void update_video(void)
{
    if (B.video_enable) {
        for (int off = 0; off < 0x400; off++) {
            const uint8_t attr = B.mem[0x9800 + off];
            const int tx = 31 - off / 32, ty = off % 32;
            const int code = B.mem[0x9000 + off] + ((attr & 0x20) << 3);
            const uint8_t *g = ((attr & 0x10) ? B.chars1 : B.chars0) + code * 64;
            const int color = (attr & 0x0f) << 2;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) B.compose[(ty * 8 + y) * 256 + tx * 8 + x] = B.palette[g[y * 8 + x] + color];
        }
        for (int i = 7; i >= 0; i--) {
            const uint8_t *e = &B.mem[0x9800 + i * 4];
            const int color = (e[1] & 0x1f) << 2, code = (e[0] & 0x3f) + ((e[1] & 0x20) << 1);
            const int px = e[2], py = e[3];
            if (px == 0 || py == 0) continue;
            const bool fx = (e[0] & 0x80) != 0, fy = (e[0] & 0x40) != 0;
            const uint8_t *g = B.sprites + code * 256;
            for (int y = 0; y < 16; y++) {
                const int sy = py - 1 + y;
                if (sy < 0 || sy >= 256) continue;
                for (int x = 0; x < 16; x++) {
                    const uint8_t p = g[(fy ? 15 - y : y) * 16 + (fx ? 15 - x : x)];
                    if (p) B.compose[sy * 256 + ((px + 1 + x) & 0xff)] = B.palette[p + color];
                }
            }
        }
    } else {
        const uint16_t black = arc_rgb565(0);
        for (int i = 0; i < 256 * 256; i++) B.compose[i] = black;
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int t = B.flip ? (H - 1 - y) * W + (W - 1 - x) : y * W + x;
            B.fb[t] = B.compose[y * 256 + x + 16];
        }
}

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    B.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t crom[0x2000], srom[0x2000], prom[0x40];
    if (!B.fb || !ARC_LOAD(kMain, B.mem, 0x6000) || !ARC_LOAD(kChars, crom, sizeof crom) ||
        !ARC_LOAD(kSprites, srom, sizeof srom) || !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    // the character ROMs hold the first character bank and the sprites, the
    // sprite ROMs the second character bank
    arc_layout cl = { 8, 8, 512, 2, 64, false, true, false, {0, 512 * 64}, kX8, kY8 };
    arc_layout sl = { 16, 16, 128, 2, 256, false, true, false, {0, 128 * 256}, kX16, kY16 };
    if (!(B.chars0 = arc_gfx_decode(&cl, crom, sizeof crom)) || !(B.sprites = arc_gfx_decode(&sl, crom, sizeof crom)) ||
        !(B.chars1 = arc_gfx_decode(&cl, srom, sizeof srom)))
        return false;
    static const arc_resnet nets[3] = {{3, {1000, 470, 220}, 470, 0}, {3, {1000, 470, 220}, 470, 0}, {2, {470, 220}, 470, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    for (int i = 0; i < 256; i++) B.palette[i] = arc_rgb565(0);
    for (int i = 0; i < 0x40; i++) {
        const uint8_t d = prom[i];
        const int r = arc_combine(w[0], 3, d & 7), g = arc_combine(w[1], 3, (d >> 3) & 7), b = arc_combine(w[2], 2, d >> 6);
        B.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    B.cpu.read = rd;
    B.cpu.write = wr;
    B.cpu.in = in;
    B.cpu.out = out;
    B.cpu.cycles = cycles;
    zd_init(&B.cpu);
    ay8910_init(&B.ay, AY_CLOCK, RATE);
    B.dsw = 0xfe;
    pal_reset();                    // (dsp-cpp's BagmanPal resets when it is made, and again in reset)
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&B.cpu);
    ay8910_reset(&B.ay);
    pal_reset();
    B.irq_enable = B.video_enable = true;
    B.flip = false;
    B.in0 = B.in1 = 0xff;
    B.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int per_line = (int)(CLOCK / FPS / SCANLINES);
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (B.irq_enable) zd_set_irq(&B.cpu, ZD_HOLD, 0xff);
            update_video();
        }
        zd_run(&B.cpu, per_line);
    }
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.fb; }

static uint8_t joy(unsigned b)
{
    uint8_t v = 0xff;
    if (b & AP_START) v &= 0xfb;
    if (b & AP_LEFT) v &= 0xf7;
    if (b & AP_RIGHT) v &= 0xef;
    if (b & AP_UP) v &= 0xdf;
    if (b & AP_DOWN) v &= 0xbf;
    if (b & AP_B1) v &= 0x7f;
    return v;
}

void arc_input(const arc_in *in)
{
    uint8_t a = joy(in->p[0]);
    if (in->coin1) a &= 0xfe;
    if (in->coin2) a &= 0xfd;
    B.in0 = a;
    B.in1 = joy(in->p[1]);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw = v;
}
