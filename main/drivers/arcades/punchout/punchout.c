// punchout.c - Nintendo's Punch-Out!! (1984) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/punchout (after MAME's
// nintendo/punchout.cpp). A Z80 at 4 MHz, the sound board's 2A03 (its APU)
// and a VLM5030 for the speech, and the cabinet's two monitors one above the
// other (256x448): 2bpp tiles on each, the zooming 3bpp big sprite and, on
// the lower one, the 2bpp second big sprite and per-row scrolling.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/nes_apu.h"
#include "../../../sound/vlm5030dsp.h"

#define W 256
#define MH 224
#define H (MH * 2)
#define MAIN_CLOCK 4000000
#define SOUND_CLOCK 1789773
#define LINES 256
#define FPS 60.0
#define RATE 44100

static const char *const kSets[] = { "punchout", NULL };
const arc_info arc_game = { "punchout", "Punch-Out!!", "Nintendo", 1984, W, H, FPS, RATE, kSets,
                            { "GOLPE IZQ", "GOLPE DER", "K.O.", NULL }, ARC_JOY_4WAY };

static const arc_rom kMain[] = {
    {"chp1-c.8l", 0x2000, 0x0000, 0xa4003adc}, {"chp1-c.8k", 0x2000, 0x2000, 0x745ecf40}, {"chp1-c.8j", 0x2000, 0x4000, 0x7a7f870e},
    {"chp1-c.8h", 0x2000, 0x6000, 0x5d8123d7}, {"chp1-c.8f", 0x4000, 0x8000, 0xc8a55ddb}};
static const arc_rom kSound[] = {{"chp1-c.4k", 0x2000, 0x0000, 0xcb6ef376}};
static const arc_rom kVlm[] = {{"chp1-c.6p", 0x4000, 0x0000, 0xea0bbb31}};
// character ROMs whose 2 KB quarters are stored in 0, 2, 1, 3 order
static const arc_rom kGfx1[] = {{"chp1-b.4c", 0x2000, 0x0000, 0x49b763bc}, {"chp1-b.4d", 0x2000, 0x2000, 0x08bc6d67}};
static const arc_rom kGfx2[] = {{"chp1-b.4a", 0x2000, 0x0000, 0xc075f831}, {"chp1-b.4b", 0x2000, 0x2000, 0xc4cc2b5a}};
static const arc_rom kGfx4[] = {{"chp1-v.6p", 0x2000, 0x0000, 0x75be7aae}, {"chp1-v.6n", 0x2000, 0x2000, 0xdaf74de0},
                                {"chp1-v.8p", 0x2000, 0x8000, 0x4cb7ea82}, {"chp1-v.8n", 0x2000, 0xa000, 0x1c0d09aa}};
static const arc_rom kGfx3[] = {
    {"chp1-v.2r", 0x4000, 0x00000, 0xbd1d4b2e}, {"chp1-v.2t", 0x4000, 0x04000, 0xdd9a688a}, {"chp1-v.2u", 0x2000, 0x08000, 0xda6a3c4b},
    {"chp1-v.2v", 0x2000, 0x0c000, 0x8c734a67}, {"chp1-v.3r", 0x4000, 0x10000, 0x2e74ad1d}, {"chp1-v.3t", 0x4000, 0x14000, 0x630ba9fb},
    {"chp1-v.3u", 0x2000, 0x18000, 0x6440321d}, {"chp1-v.3v", 0x2000, 0x1c000, 0xbb7b7198}, {"chp1-v.4r", 0x4000, 0x20000, 0x4e5b0fe9},
    {"chp1-v.4t", 0x4000, 0x24000, 0x37ffc940}, {"chp1-v.4u", 0x2000, 0x28000, 0x1a7521d4}};
static const arc_rom kProms[] = {
    {"chp1-b-6e_pink.6e", 0x200, 0x000, 0xe9ca3ac6}, {"chp1-b-6f_pink.6f", 0x200, 0x200, 0x02be56ab},
    {"chp1-b-7f_pink.7f", 0x200, 0x400, 0x11de55f1}, {"chp1-b-7e_pink.7e", 0x200, 0x600, 0xfddaa777},
    {"chp1-b-8e_pink.8e", 0x200, 0x800, 0xc3d5d71f}, {"chp1-b-8f_pink.8f", 0x200, 0xa00, 0xa3037155}};

static struct state {
    zd_z80 cpu;
    m65xx snd;
    nes_apu apu;
    vlm5030d vlm;
    uint8_t rom[0xc000], sound_rom[0x2000], vlm_rom[0x4000], nvram[0x400], work[0x800], top[0x800], spr1[0x800], spr2[0x800], bot[0x1000], sram[0x800];
    uint8_t *gfx_top, *gfx_bot, *gfx_spr1, *gfx_spr2;
    uint16_t top_pal[2][0x100], bot_pal[2][0x100];
    uint16_t pens[256 * 256];
    uint8_t in0, in1, dsw1, dsw2, latch, sound_latch[2];
    bool nmi_mask, sound_reset;
    int vlm_acc, apu_cycles;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *P_;
#define P (*P_)

// planar 8x8 characters: the highest part of the region is the most significant plane
static uint8_t *decode_planar(const uint8_t *region, size_t size, int planes)
{
    const size_t psize = size / (size_t)planes, count = psize / 8;
    uint8_t *out = (uint8_t *)malloc(count * 64);
    if (!out) return NULL;
    for (size_t c = 0; c < count; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int v = 0;
                for (int p = 0; p < planes; p++) v |= ((region[(size_t)p * psize + c * 8 + (size_t)y] >> (7 - x)) & 1) << p;
                out[c * 64 + (size_t)(y * 8 + x)] = (uint8_t)v;
            }
    return out;
}

static bool load_swizzled(const arc_rom *list, int n, uint8_t *region, size_t size)
{
    static const uint32_t dest[4] = {0x0000, 0x1000, 0x0800, 0x1800};
    uint8_t d[0x2000];
    for (int i = 0; i < n; i++) {
        arc_rom r = list[i];
        const uint32_t at = r.offset;
        r.offset = 0;
        memset(d, 0, sizeof d);
        if (!arc_load(&r, 1, d, sizeof d) || at + 0x2000 > size) return false;
        for (int q = 0; q < 4; q++) memcpy(region + at + dest[q], d + q * 0x800, 0x800);
    }
    return true;
}

// ---------------------------------------------------------------------------
// The sound board (the 2A03)

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x0800) return P.sram[a];
    if (a == 0x4015) return nes_apu_read(&P.apu, a);
    if (a == 0x4016) return P.sound_latch[0];
    if (a == 0x4017) return P.sound_latch[1];
    if (a >= 0xe000) return P.sound_rom[a - 0xe000];
    return 0;
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x0800) P.sram[a] = v;
    else if (a >= 0x4000 && a <= 0x4017 && a != 0x4014 && a != 0x4016) nes_apu_write(&P.apu, a, v);
}

static void snd_cycles(void *u, int c)
{
    (void)u;
    P.apu_cycles += c;
    while (P.apu_cycles >= 4) {
        P.apu_cycles -= 4;
        nes_apu_advance(&P.apu);
    }
    const int period = vlm5030d_cycles_per_sample(&P.vlm, SOUND_CLOCK);
    P.vlm_acc += c;
    while (P.vlm_acc >= period) {
        P.vlm_acc -= period;
        vlm5030d_update_stream(&P.vlm);
    }
    P.audio_acc += (int64_t)c * RATE;
    while (P.audio_acc >= SOUND_CLOCK) {
        P.audio_acc -= SOUND_CLOCK;
        const int32_t s = (int32_t)nes_apu_update(&P.apu) / 2 + vlm5030d_out(&P.vlm) / 2;
        if (P.naudio < 1024) P.audio[P.naudio++] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

static uint8_t dpcm_rd(void *u, uint16_t a) { return snd_rd(u, a); }
static void apu_irq(void *u) { (void)u; m65xx_set_irq(&P.snd, M65_HOLD); }

// ---------------------------------------------------------------------------
// The Z80

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0xc000) return P.rom[a];
    if (a < 0xc400) return P.nvram[a & 0x3ff];
    if (a < 0xd000) return 0xff;
    if (a < 0xd800) return P.work[a & 0x7ff];
    if (a < 0xe000) return P.top[a & 0x7ff];
    if (a < 0xe800) return P.spr1[a & 0x7ff];
    if (a < 0xf000) return P.spr2[a & 0x7ff];
    return P.bot[a & 0xfff];
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0xc000) return;
    if (a < 0xc400) P.nvram[a & 0x3ff] = v;
    else if (a < 0xd000) return;
    else if (a < 0xd800) P.work[a & 0x7ff] = v;
    else if (a < 0xe000) P.top[a & 0x7ff] = v;     // (with the controls at $dff0)
    else if (a < 0xe800) P.spr1[a & 0x7ff] = v;
    else if (a < 0xf000) P.spr2[a & 0x7ff] = v;
    else P.bot[a & 0xfff] = v;
}

static uint8_t port_rd(void *u, uint16_t port)
{
    (void)u;
    switch (port & 0xff) {
    case 0: return P.in0;
    case 1: return P.in1;
    case 2: return P.dsw2;
    case 3: return (uint8_t)((P.dsw1 & ~0x10) | (vlm5030d_bsy(&P.vlm) ? 0 : 0x10));   // the VLM's BSY, active low
    default: return 0xff;
    }
}

static void latch_w(int bit, bool on)
{
    const uint8_t m = (uint8_t)(1 << bit);
    P.latch = on ? (P.latch | m) : (P.latch & ~m);
    switch (bit) {
    case 0:
        P.nmi_mask = on;
        if (!on) zd_set_nmi(&P.cpu, ZD_CLEAR);
        break;
    case 3:                                             // the 2A03's reset
        if (on && !P.sound_reset) {
            m65xx_reset(&P.snd);
            nes_apu_reset(&P.apu);
        }
        P.sound_reset = on;
        break;
    case 4: vlm5030d_set_rst(&P.vlm, on); break;
    case 5: vlm5030d_set_st(&P.vlm, on); break;
    case 6: vlm5030d_set_vcu(&P.vlm, on); break;
    default: break;
    }
}

static void port_wr(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p == 2) P.sound_latch[0] = v;
    else if (p == 3) P.sound_latch[1] = v;
    else if (p == 4) vlm5030d_data_w(&P.vlm, v);
    else if (p >= 8 && p <= 0x0f) latch_w(p - 8, v & 1);
}

// ---------------------------------------------------------------------------
// Video

// MAME's draw_roz: source = start + dest * inc (16.16, unsigned wrap), no wraparound
#define ROZ(startx, starty, incxx, incyy, PIXEL)                                         \
    for (int y = 16; y < 240; y++) {                                                   \
        uint32_t cx = (startx);                                                        \
        const uint32_t py = ((starty) + (uint32_t)y * (uint32_t)(incyy)) >> 16;        \
        if (py >= 256) continue;                                                       \
        for (int x = 0; x < 256; x++, cx += (uint32_t)(incxx)) {                       \
            const uint32_t px = cx >> 16;                                              \
            if (px >= 128) continue;                                                   \
            const int pen = PIXEL;                                                     \
            if (pen >= 0) P.pens[y * 256 + x] = (uint16_t)pen;                         \
        }                                                                              \
    }

static inline int spr1_pixel(int px, int py)
{
    const uint8_t *v = &P.spr1[((py >> 3) * 16 + (px >> 3)) * 4];
    const int code = v[0] + ((v[1] & 0x1f) << 8), color = v[3] & 0x1f;
    const int tx = (v[3] & 0x80) ? 7 - (px & 7) : (px & 7);
    const int pix = P.gfx_spr1[code * 64 + (py & 7) * 8 + tx];
    return pix == 7 ? -1 : color * 8 + pix;
}

static inline int spr2_pixel(int px, int py)
{
    const uint8_t *v = &P.spr2[((py >> 3) * 16 + (px >> 3)) * 4];
    const int code = v[0] + ((v[1] & 0x0f) << 8), color = v[3] & 0x3f;
    const int tx = (v[3] & 0x80) ? 7 - (px & 7) : (px & 7);
    const int pix = P.gfx_spr2[code * 64 + (py & 7) * 8 + tx];
    return pix == 3 ? -1 : color * 4 + pix;
}

static void big_sprite(void)
{
    const uint8_t *c = &P.top[0x7f0];
    const int zoom = c[0] + 256 * (c[1] & 0x0f);
    if (!zoom) return;
    int sx = 4096 - (c[2] + 256 * (c[3] & 0x0f));
    if (sx > 4096 - 4 * 127) sx -= 4096;
    int sy = -(c[4] + 256 * (c[5] & 1));
    if (sy <= -256 + zoom / 0x40) sy += 512;
    sy += 12;
    int32_t incxx = zoom << 6;
    const int32_t incyy = zoom << 6;
    uint32_t startx = (uint32_t)(-sx * 0x4000), starty = (uint32_t)(-sy * 0x10000);
    startx += (uint32_t)(3740 * zoom);                 // MAME's screenshot alignment
    starty -= (uint32_t)(178 * zoom);
    if (c[6] & 1) {
        startx = ((16 * 8) << 16) - startx - 1;
        incxx = -incxx;
    }
    const uint32_t sy0 = starty + (uint32_t)(0x200 * 2 * zoom);
    ROZ(startx, sy0, incxx, incyy, spr1_pixel((int)px, (int)py))
}

static void big_sprite2(void)
{
    const uint8_t *c = &P.top[0x7f8];
    int sx = 512 - (c[0] + 256 * (c[1] & 1));
    if (sx > 512 - 127) sx -= 512;
    sx -= 55;
    int sy = -c[2] + 256 * (c[3] & 1);
    sy += 3;
    uint32_t startx = (uint32_t)(-sx) << 16;
    const uint32_t starty = (uint32_t)(-sy) << 16;
    int32_t incxx = 1 << 16;
    if (c[4] & 1) {
        startx = ((16 * 8) << 16) - startx - 1;
        incxx = -incxx;
    }
    ROZ(startx, starty, incxx, 1 << 16, spr2_pixel((int)px, (int)py))
}

static void render(void)
{
    memset(P.pens, 0, sizeof P.pens);
    for (int y = 16; y < 240; y++)
        for (int x = 0; x < 256; x++) {
            const int t = (y >> 3) * 32 + (x >> 3);
            const uint8_t attr = P.top[t * 2 + 1];
            const int code = P.top[t * 2] + ((attr & 3) << 8), color = (attr & 0x7c) >> 2;
            const int tx = (attr & 0x80) ? 7 - (x & 7) : (x & 7);
            P.pens[y * 256 + x] = (uint16_t)(color * 4 + P.gfx_top[code * 64 + (y & 7) * 8 + tx]);
        }
    if (P.top[0x7f7] & 1) big_sprite();
    const uint16_t *pal = P.top_pal[(P.top[0x7fd] >> 1) & 1];
    for (int y = 16; y < 240; y++)
        for (int x = 0; x < 256; x++) P.fb[(y - 16) * W + x] = pal[P.pens[y * 256 + x] & 0xff];

    memset(P.pens, 0, sizeof P.pens);
    for (int y = 16; y < 240; y++) {
        const int row = y >> 3, scroll = 58 + P.bot[2 * row] + 256 * (P.bot[2 * row + 1] & 1);
        for (int x = 0; x < 256; x++) {
            const int tx0 = (x + scroll) & 511, t = row * 64 + (tx0 >> 3);
            const uint8_t attr = P.bot[t * 2 + 1];
            const int code = P.bot[t * 2] + ((attr & 3) << 8), color = (attr & 0x7c) >> 2;
            const int tx = (attr & 0x80) ? 7 - (tx0 & 7) : (tx0 & 7);
            P.pens[y * 256 + x] = (uint16_t)(color * 4 + P.gfx_bot[code * 64 + (y & 7) * 8 + tx]);
        }
    }
    if (P.top[0x7f7] & 2) big_sprite();
    big_sprite2();
    pal = P.bot_pal[P.top[0x7fd] & 1];
    for (int y = 16; y < 240; y++)
        for (int x = 0; x < 256; x++) P.fb[(MH + y - 16) * W + x] = pal[P.pens[y * 256 + x] & 0xff];
}

// ---------------------------------------------------------------------------

static int pal4(uint8_t v) { return 255 - (v & 0x0f) * 0x11; }

bool arc_init(void)
{
    if (!(P_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *g = (uint8_t *)malloc(0x30000);
    if (!g) return false;
    bool ok = ARC_LOAD(kMain, P.rom, sizeof P.rom) && ARC_LOAD(kSound, P.sound_rom, sizeof P.sound_rom) && ARC_LOAD(kVlm, P.vlm_rom, sizeof P.vlm_rom);
    memset(g, 0xff, 0x4000);
    if (ok && (ok = load_swizzled(kGfx1, 2, g, 0x4000))) P.gfx_top = decode_planar(g, 0x4000, 2);
    memset(g, 0xff, 0x4000);
    if (ok && (ok = load_swizzled(kGfx2, 2, g, 0x4000))) P.gfx_bot = decode_planar(g, 0x4000, 2);
    memset(g, 0xff, 0x10000);
    if (ok && (ok = load_swizzled(kGfx4, 4, g, 0x10000))) P.gfx_spr2 = decode_planar(g, 0x10000, 2);
    memset(g, 0xff, 0x30000);
    if (ok && (ok = ARC_LOAD(kGfx3, g, 0x30000))) P.gfx_spr1 = decode_planar(g, 0x30000, 3);
    memset(g, 0, 0xc00);
    ok = ok && ARC_LOAD(kProms, g, 0xc00);
    for (int b = 0; b < 2 && ok; b++)
        for (int i = 0; i < 0x100; i++) {
            const int t = i + 0x100 * b;
            int r = pal4(g[t]), gr = pal4(g[0x200 + t]), bl = pal4(g[0x400 + t]);
            P.top_pal[b][i] = (uint16_t)(((r >> 3) << 11) | ((gr >> 2) << 5) | (bl >> 3));
            r = pal4(g[0x600 + t]), gr = pal4(g[0x800 + t]), bl = pal4(g[0xa00 + t]);
            P.bot_pal[b][i] = (uint16_t)(((r >> 3) << 11) | ((gr >> 2) << 5) | (bl >> 3));
        }
    free(g);
    if (!ok || !P.gfx_top || !P.gfx_bot || !P.gfx_spr1 || !P.gfx_spr2) return false;
    vlm5030d_init(&P.vlm, 3579545, P.vlm_rom, 0x4000, 4.0f);
    P.dsw1 = 0x00;
    P.dsw2 = 0x10;
    P.cpu.read = main_rd;
    P.cpu.write = main_wr;
    P.cpu.in = port_rd;
    P.cpu.out = port_wr;
    zd_init(&P.cpu);
    P.snd.read = snd_rd;
    P.snd.write = snd_wr;
    P.snd.cycles = snd_cycles;
    P.snd.nes = true;
    P.apu.dpcm_read = dpcm_rd;
    P.apu.irq = apu_irq;
    nes_apu_init(&P.apu);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&P.cpu);
    m65xx_reset(&P.snd);
    nes_apu_reset(&P.apu);
    vlm5030d_reset(&P.vlm);
    memset(P.work, 0, sizeof P.work);
    memset(P.top, 0, sizeof P.top);
    memset(P.spr1, 0, sizeof P.spr1);
    memset(P.spr2, 0, sizeof P.spr2);
    memset(P.bot, 0, sizeof P.bot);
    memset(P.sram, 0, sizeof P.sram);
    P.latch = 0;
    P.sound_latch[0] = P.sound_latch[1] = 0;
    P.nmi_mask = P.sound_reset = false;
    P.vlm_acc = P.apu_cycles = 0;
    P.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const double main_per_line = (double)MAIN_CLOCK / FPS / LINES, sound_per_line = (double)SOUND_CLOCK / FPS / LINES;
    double main_acc = 0, sound_acc = 0;
    P.naudio = 0;
    for (int line = 0; line < LINES; line++) {
        if (line == 240) {                              // the top monitor's VBLANK
            render();
            if (P.nmi_mask) zd_set_nmi(&P.cpu, ZD_ASSERT);
            if (!P.sound_reset) m65xx_set_nmi(&P.snd, M65_PULSE);
        }
        main_acc += main_per_line;
        const int m = (int)main_acc;
        main_acc -= m;
        zd_run(&P.cpu, m);
        sound_acc += sound_per_line;
        const int s = (int)sound_acc;
        sound_acc -= s;
        if (P.sound_reset) snd_cycles(NULL, s);         // (held in reset: the chips keep running)
        else m65xx_run(&P.snd, s);
    }
    if (audio) *audio = P.audio;
    return P.naudio;
}

const uint16_t *arc_frame(void) { return P.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    P.in0 = 0;
    if (p & AP_B1) P.in0 |= 0x01;
    if (p & AP_B2) P.in0 |= 0x04;
    if (p & (AP_B3 | AP_START)) P.in0 |= 0x08;
    P.in1 = 0;
    if (p & AP_RIGHT) P.in1 |= 0x01;
    if (p & AP_LEFT) P.in1 |= 0x02;
    if (p & AP_UP) P.in1 |= 0x04;
    if (p & AP_DOWN) P.in1 |= 0x08;
    if (in->service) P.in1 |= 0x40;
    if (in->coin1 || in->coin2) P.in1 |= 0x80;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) P.dsw2 = v;
    if (bank == 1) P.dsw1 = v;
}
