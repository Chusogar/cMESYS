// snk.c - SNK's three-Z80 boards for the common arcade frontends (arc.h): C
// port of dsp-cpp's drivers/arcade/snk (after dsp-emulator's snk_hw.pas). A
// main and a sub Z80 sharing the video RAM (each wakes the other with an
// NMI), a sound Z80 with one or two YM3526, 8x8 text, a scrolling background
// and 16x16 (Ikari Warriors also 32x32) sprites with a shadow pen; Ikari
// Warriors and T.N.K. III have the rotary joystick (here on B2 / B3).
// ARC_VARIANT: 0 Ikari Warriors, 1 Athena, 2 T.N.K. III, 3 ASO.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym3812dsp.h"

#define IKARI (ARC_VARIANT == 0)
#define ATHENA (ARC_VARIANT == 1)
#define TNK3 (ARC_VARIANT == 2)
#define ASO (ARC_VARIANT == 3)
#define TWO_YM (IKARI || ATHENA)
#define ROTATE_FINAL (TNK3 || ASO)

#define FPS 60.0
#define SCANLINES 224
#define CPU_SYNC 5
#define MAIN_CLOCK 3350000
#define SUB_CLOCK 3350000
#define SOUND_CLOCK 4000000
#define TIMER_CYCLES 120
#define RATE 44100
#define CW 288                                  // the part of the 512x512 work picture shown
#define BW 512

#if ATHENA
#define W 288
#define H 216
#else
#define W 216
#define H 288
#endif

#if ARC_VARIANT == 0
static const arc_rom kMain[] = { {"1.rom|1.4p", 0x10000, 0, 0x52a8b2dd} };
static const arc_rom kSub[] = { {"2.rom|2.8p", 0x10000, 0, 0x45364d55} };
static const arc_rom kSnd[] = { {"3.rom|3.7k", 0x10000, 0, 0x56a26699} };
static const arc_rom kChars[] = { {"7.rom|p7.3b", 0x4000, 0, 0xa7eb4917} };
static const arc_rom kTiles[] = { {"17.rom|p17.4d", 0x8000, 0x00000, 0xe0dba976}, {"18.rom|p18.2d", 0x8000, 0x08000, 0x24947d5f}, {"19.rom|p19.4b", 0x8000, 0x10000, 0x9ee59e91}, {"20.rom|p20.2b", 0x8000, 0x18000, 0x5da7ec1a} };
static const arc_rom kSp16[] = { {"8.rom|p8.3d", 0x8000, 0x00000, 0x9827c14a}, {"9.rom|p9.3f", 0x8000, 0x08000, 0x545c790c}, {"10.rom|p10.3h", 0x8000, 0x10000, 0xec9ba07e} };
static const arc_rom kSp32[] = { {"11.rom|p11.4m", 0x8000, 0x00000, 0x5c75ea8f}, {"14.rom|p14.2m", 0x8000, 0x08000, 0x3293fde4}, {"12.rom|p12.4p", 0x8000, 0x10000, 0x95138498}, {"15.rom|p15.2p", 0x8000, 0x18000, 0x65a61c99}, {"13.rom|p13.4r", 0x8000, 0x20000, 0x315383d7}, {"16.rom|p16.2r", 0x8000, 0x28000, 0xe9b03e07} };
static const arc_rom kProms[] = { {"7122er.prm|a6002-1.1k", 0x400, 0x000, 0xb9bf2c2c}, {"7122eg.prm|a6002-2.2l", 0x400, 0x400, 0x0703a770}, {"7122eb.prm|a6002-3.1l", 0x400, 0x800, 0x0a11cdde} };
#elif ARC_VARIANT == 1
static const arc_rom kMain[] = { {"athena_p1.4p", 0x4000, 0x0000, 0x900a113c}, {"athena_p2.4m", 0x8000, 0x4000, 0x61c69474} };
static const arc_rom kSub[] = { {"athena_p3.8p", 0x4000, 0x0000, 0xdf50af7e}, {"athena_p4.8m", 0x8000, 0x4000, 0xf3c933df} };
static const arc_rom kSnd[] = { {"p5.6g", 0x4000, 0x0000, 0x42dbe029}, {"p6.6k", 0x8000, 0x4000, 0x596f1c8a} };
static const arc_rom kChars[] = { {"p11.2d", 0x4000, 0, 0x18b4bcca} };
static const arc_rom kTiles[] = { {"p10.2b", 0x8000, 0, 0xf269c0eb} };
static const arc_rom kSp16[] = { {"p7.2p", 0x8000, 0x00000, 0xc63a871f}, {"p8.2s", 0x8000, 0x08000, 0x760568d8}, {"p9.2t", 0x8000, 0x10000, 0x57b35c73} };
static const arc_rom kProms[] = { {"3.2c", 0x400, 0x000, 0x294279ae}, {"2.1b", 0x400, 0x400, 0xd25c9099}, {"1.1c", 0x400, 0x800, 0xa4a4e7dc} };
#elif ARC_VARIANT == 2
static const arc_rom kMain[] = { {"tnk3-p1.bin", 0x4000, 0x0000, 0x0d2a8ca9}, {"tnk3-p2.bin", 0x4000, 0x4000, 0x0ae0a483}, {"tnk3-p3.bin", 0x4000, 0x8000, 0xd16dd4db} };
static const arc_rom kSub[] = { {"tnk3-p4.bin", 0x4000, 0x0000, 0x01b45a90}, {"tnk3-p5.bin", 0x4000, 0x4000, 0x60db6667}, {"tnk3-p6.bin", 0x4000, 0x8000, 0x4761fde7} };
static const arc_rom kSnd[] = { {"tnk3-p10.bin", 0x4000, 0x0000, 0x7bf0a517}, {"tnk3-p11.bin", 0x4000, 0x4000, 0x0569ce27} };
static const arc_rom kChars[] = { {"tnk3-p14.bin", 0x2000, 0, 0x1fd18c43} };
static const arc_rom kTiles[] = { {"tnk3-p12.bin", 0x4000, 0x0000, 0xff495a16}, {"tnk3-p13.bin", 0x4000, 0x4000, 0xf8344843} };
static const arc_rom kSp16[] = { {"tnk3-p7.bin", 0x4000, 0x0000, 0x06b92c88}, {"tnk3-p8.bin", 0x4000, 0x4000, 0x63d0e2eb}, {"tnk3-p9.bin", 0x4000, 0x8000, 0x872e3fac} };
static const arc_rom kProms[] = { {"7122.2", 0x400, 0x000, 0x34c06bc6}, {"7122.1", 0x400, 0x400, 0x6d0ac66a}, {"7122.0", 0x400, 0x800, 0x4662b4c8} };
#elif ARC_VARIANT == 3
static const arc_rom kMain[] = { {"p1.8d", 0x4000, 0x0000, 0x84981f3c}, {"p2.7d", 0x4000, 0x4000, 0xcfe912a6}, {"p3.5d", 0x4000, 0x8000, 0x39a666d2} };
static const arc_rom kSub[] = { {"p4.3d", 0x4000, 0x0000, 0xa4122355}, {"p5.2d", 0x4000, 0x4000, 0x9879e506}, {"p6.1d", 0x4000, 0x8000, 0xc0bfdf1f} };
static const arc_rom kSnd[] = { {"p7.4f", 0x4000, 0x0000, 0xdbc19736}, {"p8.3f", 0x4000, 0x4000, 0x537726a9}, {"p9.2f", 0x4000, 0x8000, 0xaef5a4f4} };
static const arc_rom kChars[] = { {"p14.1h", 0x2000, 0, 0x8baa2253} };
static const arc_rom kTiles[] = { {"p10.14h", 0x8000, 0, 0x00dff996} };
static const arc_rom kSp16[] = { {"p11.11h", 0x8000, 0x04000, 0x7feac86c}, {"p12.9h", 0x8000, 0x0c000, 0x6895990b}, {"p13.8h", 0x8000, 0x14000, 0x87a81ce1} };
static const arc_rom kProms[] = { {"mb7122h.12f", 0x400, 0x000, 0x5b0a0059}, {"mb7122h.13f", 0x400, 0x400, 0x37e28dd8}, {"mb7122h.14f", 0x400, 0x800, 0xc3fd1dd3} };
#endif

#if IKARI
static const char *const kSets[] = { "ikari", NULL };
const arc_info arc_game = { "ikari", "Ikari Warriors", "SNK", 1986, W, H, FPS, RATE, kSets, { "DISPARO", "GIRAR <", "GIRAR >", "GRANADA" }, ARC_JOY_8WAY };
#elif ATHENA
static const char *const kSets[] = { "athena", NULL };
const arc_info arc_game = { "athena", "Athena", "SNK", 1986, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif TNK3
static const char *const kSets[] = { "tnk3", NULL };
const arc_info arc_game = { "tnk3", "T.N.K. III", "SNK", 1985, W, H, FPS, RATE, kSets, { "CAÑON", "GIRAR <", "GIRAR >", "AMETRALLADORA" }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "aso", NULL };
const arc_info arc_game = { "aso", "ASO - Armored Scrum Object", "SNK", 1985, W, H, FPS, RATE, kSets, { "DISPARO", "MISIL", "ARMADURA", NULL }, ARC_JOY_8WAY };
#endif

static const int kPbX[16] = {4, 0, 12, 8, 20, 16, 28, 24, 36, 32, 44, 40, 52, 48, 60, 56};
static const int kPbY[16] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
static const int kPcY[32] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480,
                             512, 544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992};
static const int kPsX[32] = {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8,
                             23, 22, 21, 20, 19, 18, 17, 16, 31, 30, 29, 28, 27, 26, 25, 24};
static const int kPsY[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};

static struct state {
    zd_z80 main, sub, snd;
    ym3812d ym0, ym1;
    uint8_t mem[0x10000], sub_mem[0x10000], snd_mem[0x10000];
    uint8_t txt[0x800], spr[0x800], bg[0x2000];
    bool dirty_txt[0x800], dirty_bg[0x1000];
    uint8_t *chars, *tiles, *sp16, *sp32;
    int chars_n, tiles_n, sp16_n, sp32_n;
    uint16_t pal[2048];
    uint16_t text[288 * 288];                   // palette index + 1 (0: transparent)
    uint16_t background[BW * BW];
    uint16_t comp[CW * CW];
    uint16_t scroll_x, scroll_y, sp16_x, sp16_y, sp32_x, sp32_y, hf_x, hf_y, txt_offset, bg_offset, bg_pal_offset;
    bool flip;
    uint8_t sound_status, sound_latch;
    bool timer_on;
    int timer_cycles;
    uint8_t in0, in1, in2, in3, dsw_a, dsw_b, dsw_c, rot_cont, rot_nibble;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *N_;
#define N (*N_)

static uint8_t no_in(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void no_out(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// ---------------------------------------------------------------------------
// Sound

static void sound_latch_w(uint8_t v)
{
    N.sound_latch = v;
    N.sound_status |= 0x0c;
    N.timer_on = true;
    N.timer_cycles = 0;
}

static void snd_cycles(void *u, int c)
{
    (void)u;
    if (N.timer_on) {
        N.timer_cycles += c;
        if (N.timer_cycles >= TIMER_CYCLES) {
            N.timer_cycles = 0;
            N.timer_on = false;
            zd_set_irq(&N.snd, (N.sound_status & 0x0b) ? ZD_ASSERT : ZD_CLEAR, 0xff);
        }
    }
    N.audio_acc += (int64_t)c * RATE;
    while (N.audio_acc >= SOUND_CLOCK) {
        N.audio_acc -= SOUND_CLOCK;
        int32_t s = ym3812d_update(&N.ym0);
        if (TWO_YM) s += ym3812d_update(&N.ym1);
        if (N.naudio < 1024) N.audio[N.naudio++] = arc_clamp16(s);
    }
}

static void ym0_irq(void *u, bool on) { (void)u; if (on) { N.sound_status |= 1; N.timer_on = true; } }
static void ym1_irq(void *u, bool on) { (void)u; if (on) { N.sound_status |= 2; N.timer_on = true; } }

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
#if IKARI || ATHENA
    if (a <= 0xcfff) return N.snd_mem[a];
    switch (a) {
    case 0xe000: return N.sound_latch;
    case 0xe800: return ym3812d_status(&N.ym0);
    case 0xf000: return ym3812d_status(&N.ym1);
    case 0xf800: return N.sound_status;
    default: return 0xff;
    }
#else
    const uint16_t ram_end = TNK3 ? 0x87ff : 0xc7ff, latch = TNK3 ? 0xa000 : 0xd000, clr = TNK3 ? 0xc000 : 0xe000, ym = TNK3 ? 0xe000 : 0xf000;
    if (a <= ram_end) return N.snd_mem[a];
    if (a == latch) return N.sound_latch;
    if (a == clr) {
        N.sound_latch = 0;
        N.sound_status &= 0xfb;
        N.timer_on = true;
        return 0xff;
    }
    if (a == ym) return ym3812d_status(&N.ym0);
    if (a == ym + 4) {
        N.sound_status &= 0xf7;
        N.timer_on = true;
        return 0xff;
    }
    if (a == ym + 6) {
        N.sound_status &= 0xfe;
        N.timer_on = true;
        return 0xff;
    }
    return 0xff;
#endif
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if IKARI || ATHENA
    if (a <= 0xbfff) return;
    if (a <= 0xcfff) N.snd_mem[a] = v;
    else if (a == 0xe800) ym3812d_control(&N.ym0, v);
    else if (a == 0xec00) ym3812d_write(&N.ym0, v);
    else if (a == 0xf000) ym3812d_control(&N.ym1, v);
    else if (a == 0xf400) ym3812d_write(&N.ym1, v);
    else if (a == 0xf800) {
        if (!(v & 0x10)) N.sound_status &= 0xfe;
        if (!(v & 0x20)) N.sound_status &= 0xfd;
        if (!(v & 0x40)) N.sound_status &= 0xfb;
        if (!(v & 0x80)) N.sound_status &= 0xf7;
        N.timer_on = true;
    }
#else
    const uint16_t rom_end = TNK3 ? 0x7fff : 0xbfff, ram_end = TNK3 ? 0x87ff : 0xc7ff, ym = TNK3 ? 0xe000 : 0xf000;
    if (a <= rom_end) return;
    if (a <= ram_end) {
        N.snd_mem[a] = v;
        return;
    }
    if (a == ym) ym3812d_control(&N.ym0, v);
    if (a == ym + 1) ym3812d_write(&N.ym0, v);
#endif
}

// ---------------------------------------------------------------------------
// The main and sub CPUs

static void bg_w(int o, uint8_t v)
{
    o &= 0x1fff;
    if (N.bg[o] == v) return;
    N.bg[o] = v;
    N.dirty_bg[ASO ? (o & 0xfff) : ((o >> 1) & 0xfff)] = true;
}

static void txt_w(int o, uint8_t v)
{
    o &= 0x7ff;
    if (N.txt[o] == v) return;
    N.txt[o] = v;
    N.dirty_txt[o] = true;
}

static void txt_offset_w(uint16_t off, bool always)
{
    if (always || N.txt_offset != off) {
        N.txt_offset = off;
        for (int i = 0; i < 0x400; i++) N.dirty_txt[i] = true;
    }
}

#if IKARI
static uint8_t hf_check(int i)
{
    const uint8_t *sr = &N.mem[0xe800 + 4 * i];
    const uint16_t x = (uint16_t)(sr[2] + ((sr[3] & 0x80) << 1)), y = (uint16_t)(sr[0] + ((sr[3] & 0x10) << 4));
    const uint16_t dx = (uint16_t)((x - N.hf_x) & 0x1ff), dy = (uint16_t)((y - N.hf_y) & 0x1ff);
    return (dx > 0x20 && dx <= 0x1e0 && dy > 0x20 && dy <= 0x1e0) ? 0 : 1;
}

static int hf_read(uint16_t a)
{
    if (a >= 0xce00 && a <= 0xcea0 && !(a & 0x1f)) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) v |= (uint8_t)(hf_check((a - 0xce00) / 0x20 * 8 + b) << b);
        return v;
    }
    if (a == 0xcee0) return (hf_check(48) << 0) | (hf_check(49) << 1) | (hf_check(48) << 4) | (hf_check(49) << 5);
    return -1;
}
#endif

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
#if IKARI
    if (a <= 0xbfff || (a >= 0xe000 && a <= 0xf7ff)) return N.mem[a];
    switch (a) {
    case 0xc000: return (uint8_t)(N.in0 | ((N.sound_status & 4) >> 2));
    case 0xc100: return N.in1;
    case 0xc200: return N.in2;
    case 0xc300: return N.in3;
    case 0xc500: return (uint8_t)(N.dsw_a + (N.dsw_c & 0x04));
    case 0xc600: return (uint8_t)(N.dsw_b + (N.dsw_c & 0x30));
    case 0xc700: zd_set_nmi(&N.sub, ZD_ASSERT); return 0xff;
    default: break;
    }
    const int hf = hf_read(a);
    if (hf >= 0) return (uint8_t)hf;
    if (a >= 0xd000 && a <= 0xdfff) return N.bg[a & 0x7ff];
    if (a >= 0xf800) return N.txt[a & 0x7ff];
    return 0xff;
#else
    if (a <= 0xbfff || (ASO && a >= 0xd800 && a <= 0xdfff)) return N.mem[a];
    switch (a) {
    case 0xc000: return (uint8_t)(N.in0 | (ATHENA ? (N.sound_status & 4) >> 2 : (N.sound_status & 4) << 3));
    case 0xc100: return N.in1;
    case 0xc200: return N.in2;
    case 0xc300: if (TNK3) return N.in3; break;
    case 0xc500: return (uint8_t)(N.dsw_a + (N.dsw_c & (ATHENA ? 0x04 : 0xc0)));
    case 0xc600: return (uint8_t)(N.dsw_b + (N.dsw_c & (ATHENA ? 0x30 : 0x01)));
    case 0xc700: zd_set_nmi(&N.sub, ZD_ASSERT); return 0xff;
    default: break;
    }
    const uint16_t spr = ASO ? 0xe000 : 0xd000, bg = ASO ? 0xe800 : 0xd800;
    if (a >= spr && a <= spr + 0x7ff) return N.spr[a & 0x7ff];
    if (a >= bg && a <= 0xf7ff) return N.bg[a - bg];
    if (a >= 0xf800) return N.txt[a & 0x7ff];
    return 0xff;
#endif
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a == 0xc400) {
        sound_latch_w(v);
        return;
    }
    if (a == 0xc700) {
        zd_set_nmi(&N.main, ZD_CLEAR);
        return;
    }
#if IKARI
    switch (a) {
    case 0xc800: N.scroll_y = (uint16_t)((N.scroll_y & 0x100) | v); return;
    case 0xc880: N.scroll_x = (uint16_t)((N.scroll_x & 0x100) | v); return;
    case 0xc900:
        N.scroll_x = (uint16_t)((N.scroll_x & 0xff) | ((v & 2) << 7));
        N.scroll_y = (uint16_t)((N.scroll_y & 0xff) | ((v & 1) << 8));
        return;
    case 0xc980: txt_offset_w((uint16_t)((v & 0x10) << 4), true); return;
    case 0xca00: N.sp16_y = (uint16_t)((N.sp16_y & 0x100) | v); return;
    case 0xca80: N.sp16_x = (uint16_t)((N.sp16_x & 0x100) | v); return;
    case 0xcb00: N.sp32_y = (uint16_t)((N.sp32_y & 0x100) | v); return;
    case 0xcb80: N.sp32_x = (uint16_t)((N.sp32_x & 0x100) | v); return;
    case 0xcc00: N.hf_y = (uint16_t)((N.hf_y & 0x100) | v); return;
    case 0xcc80: N.hf_x = (uint16_t)((N.hf_x & 0x100) | v); return;
    case 0xcd00:
        N.sp32_x = (uint16_t)((N.sp32_x & 0xff) | ((v & 0x20) << 3));
        N.sp16_x = (uint16_t)((N.sp16_x & 0xff) | ((v & 0x10) << 4));
        N.sp32_y = (uint16_t)((N.sp32_y & 0xff) | ((v & 0x08) << 5));
        N.sp16_y = (uint16_t)((N.sp16_y & 0xff) | ((v & 0x04) << 6));
        return;
    case 0xcd80:
        N.hf_x = (uint16_t)((N.hf_x & 0xff) | ((v & 0x80) << 1));
        N.hf_y = (uint16_t)((N.hf_y & 0xff) | ((v & 0x40) << 2));
        return;
    default: break;
    }
    if (a >= 0xd000 && a <= 0xdfff) bg_w(a & 0x7ff, v);
    else if (a >= 0xe000 && a <= 0xf7ff) N.mem[a] = v;
    else if (a >= 0xf800) txt_w(a & 0x7ff, v);
#else
    switch (a) {
    case 0xc800:
        N.flip = v & (ASO ? 0x20 : 0x80);
        if (!ASO) txt_offset_w((uint16_t)((v & 0x40) << 2), false);
        N.scroll_y = (uint16_t)((N.scroll_y & 0xff) | ((v & 0x10) << 4));
        N.sp16_y = (uint16_t)((N.sp16_y & 0xff) | ((v & 0x08) << 5));
        N.scroll_x = (uint16_t)((N.scroll_x & 0xff) | ((v & 0x02) << 7));
        N.sp16_x = (uint16_t)((N.sp16_x & 0xff) | ((v & 0x01) << 8));
        return;
    case 0xc900: N.sp16_y = (uint16_t)((N.sp16_y & 0x100) | v); return;
    case 0xca00: N.sp16_x = (uint16_t)((N.sp16_x & 0x100) | v); return;
    case 0xcb00: N.scroll_y = (uint16_t)((N.scroll_y & 0x100) | v); return;
    case 0xcc00: N.scroll_x = (uint16_t)((N.scroll_x & 0x100) | v); return;
    case 0xcf00:
        if (ASO) {
            const uint16_t pal = (uint16_t)(((v & 0x0f) ^ 8) << 4), bank = (uint16_t)((v & 0x30) << 4);
            if (N.bg_pal_offset != pal || N.bg_offset != bank) {
                N.bg_pal_offset = pal;
                N.bg_offset = bank;
                memset(N.dirty_bg, 1, sizeof N.dirty_bg);
            }
            return;
        }
        break;
    default: break;
    }
    const uint16_t spr = ASO ? 0xe000 : 0xd000, bg = ASO ? 0xe800 : 0xd800;
    if (ASO && a >= 0xd800 && a <= 0xdfff) N.mem[a] = v;
    else if (a >= spr && a <= spr + 0x7ff) N.spr[a & 0x7ff] = v;
    else if (a >= bg && a <= 0xf7ff) bg_w(a - bg, v);
    else if (a >= 0xf800) txt_w(a & 0x7ff, v);
#endif
}

static uint8_t sub_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xbfff) return N.sub_mem[a];
    if (a == 0xc000 || ((ATHENA || TNK3) && a == 0xc700)) {
        zd_set_nmi(&N.main, ZD_ASSERT);
        return 0xff;
    }
#if IKARI
    const int hf = hf_read(a);
    if (hf >= 0) return (uint8_t)hf;
    if (a >= 0xd000 && a <= 0xdfff) return N.bg[a & 0x7ff];
    if (a >= 0xe000 && a <= 0xf7ff) return N.mem[a];
#elif ASO
    if (a >= 0xc800 && a <= 0xcfff) return N.mem[a + 0x1000];
    if (a >= 0xd000 && a <= 0xd7ff) return N.spr[a & 0x7ff];
    if (a >= 0xd800 && a <= 0xe7ff) return N.bg[a - 0xd800];
#else
    if (a >= 0xc800 && a <= 0xcfff) return N.spr[a & 0x7ff];
    if (a >= 0xd000 && a <= 0xefff) return N.bg[a - 0xd000];
    if (a >= 0xf000 && a <= 0xf7ff) return N.sub_mem[a];
#endif
    if (a >= 0xf800) return N.txt[a & 0x7ff];
    return 0xff;
}

static void sub_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a == 0xc000 || ((ATHENA || TNK3) && a == 0xc700)) {
        zd_set_nmi(&N.sub, ZD_CLEAR);
        return;
    }
#if IKARI
    if (a == 0xc980) txt_offset_w((uint16_t)((v & 0x10) << 4), true);
    else if (a == 0xcc00) N.hf_y = (uint16_t)((N.hf_y & 0x100) | v);
    else if (a == 0xcc80) N.hf_x = (uint16_t)((N.hf_x & 0x100) | v);
    else if (a == 0xcd80) {
        N.hf_x = (uint16_t)((N.hf_x & 0xff) | ((v & 0x80) << 1));
        N.hf_y = (uint16_t)((N.hf_y & 0xff) | ((v & 0x40) << 2));
    } else if (a >= 0xd000 && a <= 0xdfff) bg_w(a & 0x7ff, v);
    else if (a >= 0xe000 && a <= 0xf7ff) N.mem[a] = v;
    else if (a >= 0xf800) txt_w(a & 0x7ff, v);
#elif ASO
    if (a >= 0xc800 && a <= 0xcfff) N.mem[a + 0x1000] = v;
    else if (a >= 0xd000 && a <= 0xd7ff) N.spr[a & 0x7ff] = v;
    else if (a >= 0xd800 && a <= 0xe7ff) bg_w(a - 0xd800, v);
    else if (a >= 0xf800) txt_w(a & 0x7ff, v);
#else
    if (a >= 0xc800 && a <= 0xcfff) N.spr[a & 0x7ff] = v;
    else if (a >= 0xd000 && a <= 0xefff) bg_w(a - 0xd000, v);
    else if (a >= 0xf000 && a <= 0xf7ff) N.sub_mem[a] = v;
    else if (a >= 0xf800) txt_w(a & 0x7ff, v);
#endif
}

// ---------------------------------------------------------------------------
// Video

static void put_tile(const uint8_t *set, int sz, int n, uint16_t *dest, int dw, int dh, int x, int y, int code, int color,
                     bool transparent, bool as_index)
{
    const uint8_t *px = set + (size_t)(code % n) * sz * sz;
    for (int r = 0; r < sz; r++) {
        const int dy = y + r;
        if (dy < 0 || dy >= dh) continue;
        for (int c = 0; c < sz; c++) {
            const int dx = x + c;
            if (dx < 0 || dx >= dw) continue;
            const uint8_t pen = px[r * sz + c];
            uint16_t *d = &dest[dy * dw + dx];
            if (transparent && pen == 15) *d = 0;
            else *d = as_index ? (uint16_t)(color + pen + 1) : N.pal[color + pen];
        }
    }
}

static void sprite(const uint8_t *set, int sz, int n, int code, int color, bool flip_y, int dx0, int dy0)
{
    const uint8_t *px = set + (size_t)(code % n) * sz * sz;
    for (int r = 0; r < sz; r++) {
        const int y = (dy0 + r) & 0x1ff;
        if (y >= CW) continue;
        const int sy = flip_y ? sz - 1 - r : r;
        for (int c = 0; c < sz; c++) {
            const uint8_t pen = px[sy * sz + c];
            if (pen == 7) continue;
            const int x = (dx0 + c) & 0x1ff;
            if (x >= CW) continue;
            N.comp[y * CW + x] = pen == 6 ? N.pal[0x401] : N.pal[color + pen];   // pen 6: the shadow
        }
    }
}

static void blit_bg(int sx0, int sy0)
{
    for (int y = 0; y < CW; y++) {
        const uint16_t *src = N.background + ((y + sy0) & 0x1ff) * BW;
        uint16_t *d = N.comp + y * CW;
        for (int x = 0; x < CW; x++) d[x] = src[(x + sx0) & 0x1ff];
    }
}

static void blit_text(int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint16_t p = N.text[y * 288 + x];
            if (p) N.comp[y * CW + x] = N.pal[p - 1];
        }
}

static void draw_text(void)
{
    for (int f = 0; f < 28; f++)
        for (int g = 0; g < 36; g++) {
            const int col = g - 2;
            const int pos = (col & 0x20) ? 0x400 + f + ((col & 0x1f) << 5) : f + (col << 5);
            if (!N.dirty_txt[pos & 0x7ff]) continue;
            N.dirty_txt[pos & 0x7ff] = false;
            const int nchar = N.txt[pos & 0x7ff];
            const bool opaque = pos & 0x400;
#if IKARI
            put_tile(N.chars, 8, N.chars_n, N.text, 288, 288, f * 8, (35 - g) * 8, N.txt_offset + nchar, 0x180, !opaque, true);
#else
            const int code = ASO ? nchar : N.txt_offset + nchar;
            put_tile(N.chars, 8, N.chars_n, N.text, 288, 288, g * 8, f * 8, code, opaque ? 0x180 : 0x180 + ((nchar & 0xe0) >> 1), !opaque, true);
#endif
        }
}

static void draw_bg(void)
{
#if IKARI
    for (int f = 0; f < 0x400; f++) {
        if (!N.dirty_bg[f]) continue;
        N.dirty_bg[f] = false;
        const uint8_t atr = N.bg[1 + f * 2];
        put_tile(N.tiles, 16, N.tiles_n, N.background, BW, BW, (f % 32) * 16, (31 - f / 32) * 16, N.bg[f * 2] + ((atr & 3) << 8), (atr & 0x70) + 0x100, false, false);
    }
#else
    for (int f = 0; f < 0x1000; f++) {
        if (!N.dirty_bg[f]) continue;
        N.dirty_bg[f] = false;
#if ASO
        put_tile(N.tiles, 8, N.tiles_n, N.background, BW, BW, (f / 64) * 8, (f % 64) * 8, N.bg_offset + N.bg[f], 0x80 + N.bg_pal_offset, false, false);
#else
        const uint8_t atr = N.bg[1 + f * 2];
        put_tile(N.tiles, 8, N.tiles_n, N.background, BW, BW, (f / 64) * 8, (f % 64) * 8, N.bg[f * 2] + ((atr & 0x30) << 4), (((atr & 0x0f) ^ 8) << 4) + 0x80, false, false);
#endif
    }
#endif
}

#if IKARI
static void sprites16_ikari(int bank)
{
    const int base = 0xe800 + bank * 25 * 4;
    for (int f = 0; f < 25; f++) {
        const uint8_t *s = &N.mem[base + f * 4];
        const uint8_t atr = s[3];
        int sx = N.sp16_x + 300 - 16 - s[2], sy = -(int)N.sp16_y + 7 - 16 - 8 + s[0];
        sx += (atr & 0x80) << 1;
        sy += (atr & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        sprite(N.sp16, 16, N.sp16_n, s[1] | ((atr & 0x60) << 3), (atr & 0x0f) << 3, false, sy, (272 - sx) & 0x1ff);
    }
}

static void sprites32_ikari(void)
{
    for (int f = 0; f < 25; f++) {
        const uint8_t *s = &N.mem[0xe000 + f * 4];
        const uint8_t atr = s[3];
        int sx = N.sp32_x + 300 - 32 - s[2], sy = -(int)N.sp32_y + 7 - 32 - 8 + s[0];
        sx += (atr & 0x80) << 1;
        sy += (atr & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        sprite(N.sp32, 32, N.sp32_n, s[1] | ((atr & 0x40) << 2), ((atr & 0x0f) << 3) + 0x80, false, sy, (256 - sx) & 0x1ff);
    }
}
#else
static void sprites16(void)
{
    for (int f = 0; f < 50; f++) {
        const uint8_t *s = &N.spr[f * 4];
        const uint8_t atr = s[3];
        int code = s[1] | ((atr & 0x40) << 2);
        if (ATHENA) code |= (atr & 0x20) << 4;
        int sx = N.sp16_x + 301 - 16 - s[2], sy = -(int)N.sp16_y + 7 - 24 + s[0];
        sx += (atr & 0x80) << 1;
        sy += (atr & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        sprite(N.sp16, 16, N.sp16_n, code, (atr & 0x0f) << 3, !ATHENA && (atr & 0x20), sx, sy);
    }
}
#endif

static void copy_final(int sw, int sh)
{
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            const uint16_t p = N.comp[y * CW + x];
            int dx, dy;
            if (ROTATE_FINAL) {
                dx = y;
                dy = sw - 1 - x;
                if (N.flip) {
                    dx = W - 1 - dx;
                    dy = H - 1 - dy;
                }
            } else {
                dx = N.flip ? sw - 1 - x : x;
                dy = N.flip ? sh - 1 - y : y;
            }
            if (dx >= 0 && dx < W && dy >= 0 && dy < H) N.fb[dy * W + dx] = p;
        }
}

static void update_video(void)
{
    draw_text();
    draw_bg();
#if IKARI
    blit_bg(N.scroll_y, 239 - (int)N.scroll_x);
    sprites16_ikari(0);
    sprites32_ikari();
    sprites16_ikari(1);
    blit_text(224, 288);
    copy_final(216, 288);
#else
    blit_bg((int)N.scroll_x - 16 + (ASO ? 256 : 0), N.scroll_y);
    sprites16();
    blit_text(288, 224);
    copy_final(288, 216);
#endif
}

// ---------------------------------------------------------------------------

static uint8_t *decode(const arc_rom *roms, int nroms, size_t size, const arc_layout *l, bool *ok)
{
    uint8_t *raw = (uint8_t *)calloc(1, size), *set = NULL;
    if (raw && *ok && (*ok = arc_load(roms, nroms, raw, size))) set = arc_gfx_decode(l, raw, size);
    free(raw);
    if (!set) *ok = false;
    return set;
}
#define DECODE(list, size, l, ok) decode(list, (int)(sizeof list / sizeof list[0]), size, l, ok)

static uint8_t p4(uint8_t v) { return (uint8_t)((v & 0x0f) * 0x11); }

bool arc_init(void)
{
    if (!(N_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    bool ok = ARC_LOAD(kMain, N.mem, 0x10000) && ARC_LOAD(kSub, N.sub_mem, 0x10000) && ARC_LOAD(kSnd, N.snd_mem, 0x10000);
#if IKARI
    arc_layout cl = {8, 8, 0x200, 4, 256, false, false, true, {0, 1, 2, 3}, kPbX, kPcY};
    arc_layout tl = {16, 16, 0x400, 4, 1024, false, false, true, {0, 1, 2, 3}, kPbX, kPbY};
    arc_layout s16 = {16, 16, 0x400, 3, 256, false, false, true, {2 * 0x8000 * 8, 0x8000 * 8, 0}, kPsX, kPsY};
    arc_layout s32 = {32, 32, 0x200, 3, 1024, false, false, true, {2 * 0x10000 * 8, 0x10000 * 8, 0}, kPsX, kPcY};
    N.chars = DECODE(kChars, 0x4000, &cl, &ok);
    N.tiles = DECODE(kTiles, 0x20000, &tl, &ok);
    N.sp16 = DECODE(kSp16, 0x18000, &s16, &ok);
    N.sp32 = DECODE(kSp32, 0x30000, &s32, &ok);
    N.chars_n = 0x200, N.tiles_n = 0x400, N.sp16_n = 0x400, N.sp32_n = 0x200;
#else
    // chars (T.N.K. III and ASO: the 8 KB twice), 8x8 tiles, 16x16 sprites (ASO: its ROMs' halves packed)
    const int chars_n = ASO ? 0x100 : 0x200, spr_n = TNK3 ? 0x200 : 0x400;
    const size_t chars_sz = ATHENA ? 0x4000 : 0x2000, spr_sz = TNK3 ? 0xc000 : 0x18000;
    uint8_t *raw = (uint8_t *)calloc(1, 0x1c000);
    if (!raw) return false;
    ok = ok && ARC_LOAD(kChars, raw, chars_sz);
    if (!ATHENA) memcpy(raw + 0x2000, raw, 0x2000);
    arc_layout cl = {8, 8, chars_n, 4, 256, false, false, false, {0, 1, 2, 3}, kPbX, kPcY};
    if (ok) N.chars = arc_gfx_decode(&cl, raw, 0x4000);
    memset(raw, 0, 0x8000);
    ok = ok && ARC_LOAD(kTiles, raw, 0x8000);
    arc_layout tl = {8, 8, 0x400, 4, 256, false, false, false, {0, 1, 2, 3}, kPbX, kPcY};
    if (ok) N.tiles = arc_gfx_decode(&tl, raw, 0x8000);
    memset(raw, 0, 0x1c000);
    ok = ok && ARC_LOAD(kSp16, raw, ASO ? 0x1c000 : spr_sz);
    if (ASO) {
        memmove(raw, raw + 0x8000, 0x4000);
        memmove(raw + 0x8000, raw + 0x10000, 0x4000);
        memmove(raw + 0x10000, raw + 0x18000, 0x4000);
    }
    const int plane = (int)(spr_sz / 3);
    arc_layout sl = {16, 16, spr_n, 3, 256, false, false, false, {2 * plane * 8, plane * 8, 0}, kPsX, kPsY};
    if (ok) N.sp16 = arc_gfx_decode(&sl, raw, spr_sz);
    free(raw);
    N.chars_n = chars_n, N.tiles_n = 0x400, N.sp16_n = spr_n;
    if (!N.chars || !N.tiles || !N.sp16) ok = false;
#endif
    uint8_t prom[0xc00];
    memset(prom, 0, sizeof prom);
    ok = ok && ARC_LOAD(kProms, prom, sizeof prom);
    if (!ok) return false;
    for (int i = 0; i < 0x400; i++) {
#if IKARI
        const unsigned r = p4(prom[i]), g = p4(prom[i + 0x400]), b = p4(prom[i + 0x800]);
#else
        const uint8_t *p0 = &prom[i], *p1 = &prom[i + 0x400], *p2 = &prom[i + 0x800];
        const unsigned r = 0x0e * ((*p2 >> 3) & 1) + 0x1f * ((*p0 >> 1) & 1) + 0x43 * ((*p0 >> 2) & 1) + 0x8f * ((*p0 >> 3) & 1);
        const unsigned g = 0x0e * ((*p2 >> 2) & 1) + 0x1f * ((*p1 >> 2) & 1) + 0x43 * ((*p1 >> 3) & 1) + 0x8f * (*p0 & 1);
        const unsigned b = 0x0e * (*p2 & 1) + 0x1f * ((*p2 >> 1) & 1) + 0x43 * (*p1 & 1) + 0x8f * ((*p1 >> 1) & 1);
#endif
        N.pal[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    N.pal[0x401] = 0;                               // the shadow (black)
#if IKARI
    N.dsw_a = 0x3b, N.dsw_b = 0x4b, N.dsw_c = 0x34;
#elif ATHENA
    N.dsw_a = 0x39, N.dsw_b = 0xcb, N.dsw_c = 0x34;
#elif TNK3
    N.dsw_a = 0x3d, N.dsw_b = 0x76, N.dsw_c = 0xc1;
#else
    N.dsw_a = 0x3c, N.dsw_b = 0xf6, N.dsw_c = 0xc1;
#endif
    zd_z80 *cpus[3] = {&N.main, &N.sub, &N.snd};
    for (int i = 0; i < 3; i++) {
        cpus[i]->in = no_in;
        cpus[i]->out = no_out;
    }
    N.main.read = main_rd, N.main.write = main_wr;
    N.sub.read = sub_rd, N.sub.write = sub_wr;
    N.snd.read = snd_rd, N.snd.write = snd_wr, N.snd.cycles = snd_cycles;
    for (int i = 0; i < 3; i++) zd_init(cpus[i]);
    ym3812d_init(&N.ym0, 4000000, true, 2.0f);
    ym3812d_init(&N.ym1, 4000000, true, 2.0f);
    N.ym0.irq = ym0_irq;
    N.ym1.irq = ym1_irq;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&N.main);
    zd_reset(&N.sub);
    zd_reset(&N.snd);
    ym3812d_reset(&N.ym0);
    ym3812d_reset(&N.ym1);
    N.txt_offset = N.bg_offset = N.bg_pal_offset = 0;
    N.sound_latch = N.sound_status = 0;
    N.timer_on = false;
    N.timer_cycles = 0;
    N.hf_x = N.hf_y = 0;
    N.rot_cont = 0;
    N.rot_nibble = 0xb0;
    N.sp16_x = N.sp16_y = N.sp32_x = N.sp32_y = N.scroll_x = N.scroll_y = 0;
    N.flip = false;
    N.audio_acc = 0;
    memset(N.dirty_txt, 1, sizeof N.dirty_txt);
    memset(N.dirty_bg, 1, sizeof N.dirty_bg);
    memset(N.text, 0, sizeof N.text);
    if (IKARI || ATHENA) N.in0 = 0xfe, N.in1 = 0xbf, N.in2 = 0xbf;
    else if (TNK3) N.in0 = 0xdf, N.in1 = 0x0f, N.in2 = 0x0f;
    else N.in0 = 0xdf, N.in1 = 0xff, N.in2 = 0xff;
    N.in3 = 0xff;
}

int arc_run_frame(const int16_t **audio)
{
    const int ms = (int)(MAIN_CLOCK / FPS / (SCANLINES * CPU_SYNC)), ss = (int)(SUB_CLOCK / FPS / (SCANLINES * CPU_SYNC));
    const int so = (int)(SOUND_CLOCK / FPS / (SCANLINES * CPU_SYNC));
    N.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) {
            zd_set_irq(&N.main, ZD_HOLD, 0xff);
            zd_set_irq(&N.sub, ZD_HOLD, 0xff);
            update_video();
        }
        for (int s = 0; s < CPU_SYNC; s++) {
            zd_run(&N.main, ms);
            zd_run(&N.sub, ss);
            zd_run(&N.snd, so);
        }
    }
    if (audio) *audio = N.audio;
    return N.naudio;
}

const uint16_t *arc_frame(void) { return N.fb; }

static void step_rotary(bool inc)
{
    if (++N.rot_cont != 0x0f) return;
    N.rot_cont = 0;
    if (inc) {
        N.rot_nibble += 0x10;
        if ((N.rot_nibble & 0xf0) == 0xc0) N.rot_nibble = 0;
    } else {
        N.rot_nibble -= 0x10;
        if ((N.rot_nibble & 0xf0) == 0xf0) N.rot_nibble = 0xb0;
    }
}

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
#define DIRS(port, p) do { if ((p) & AP_UP) port &= 0xfe; if ((p) & AP_DOWN) port &= 0xfd; if ((p) & AP_LEFT) port &= 0xfb; if ((p) & AP_RIGHT) port &= 0xf7; } while (0)
#if IKARI || TNK3
    // the rotary joystick on B2 / B3; the second button on B4
    const unsigned f1 = AP_B1, f2 = AP_B4;
    N.in0 = IKARI ? 0xfe : 0xdf;
    N.in1 = N.in2 = 0x0f;
    N.in3 = IKARI ? 0x0f : 0xff;
    if (IKARI) {
        if (in->coin2) N.in0 &= 0xef;
        if (in->coin1) N.in0 &= 0xdf;
        if (p2 & AP_START) N.in0 &= 0xbf;
        if (p1 & AP_START) N.in0 &= 0x7f;
    } else {
        if (in->coin1) N.in0 &= 0xfe;
        if (p1 & AP_START) N.in0 &= 0xf7;
        if (p2 & AP_START) N.in0 &= 0xef;
    }
    DIRS(N.in1, p1);
    if (p1 & AP_B2) step_rotary(true);
    if (p1 & AP_B3) step_rotary(false);
    DIRS(N.in2, p2);
    if (p2 & AP_B2) step_rotary(true);
    if (p2 & AP_B3) step_rotary(false);
    N.in1 = (uint8_t)((N.in1 & 0x0f) | (N.rot_nibble & 0xf0));
    if (IKARI) {
        if (p1 & f1) N.in3 &= 0xfe;
        if (p1 & f2) N.in3 &= 0xfd;
        if (p2 & f1) N.in3 &= 0xf7;
        if (p2 & f2) N.in3 &= 0xef;
    } else {
        if (p1 & f2) N.in3 &= 0xfe;
        if (p1 & f1) N.in3 &= 0xfd;
        if (p2 & f2) N.in3 &= 0xf7;
        if (p2 & f1) N.in3 &= 0xef;
    }
#elif ATHENA
    N.in0 = 0xfe;
    N.in1 = N.in2 = 0xbf;
    if (in->coin2) N.in0 &= 0xef;
    if (in->coin1) N.in0 &= 0xdf;
    if (p2 & AP_START) N.in0 &= 0xbf;
    if (p1 & AP_START) N.in0 &= 0x7f;
    DIRS(N.in1, p1);
    if (p1 & AP_B2) N.in1 &= 0xef;
    if (p1 & AP_B1) N.in1 &= 0xdf;
    DIRS(N.in2, p2);
    if (p2 & AP_B2) N.in2 &= 0xef;
    if (p2 & AP_B1) N.in2 &= 0xdf;
#else
    N.in0 = 0xdf;
    N.in1 = N.in2 = 0xff;
    if (in->coin1) N.in0 &= 0xfe;
    if (p1 & AP_START) N.in0 &= 0xf7;
    if (p2 & AP_START) N.in0 &= 0xef;
    DIRS(N.in1, p1);
    if (p1 & AP_B3) N.in1 &= 0xef;
    if (p1 & AP_B1) N.in1 &= 0xdf;
    if (p1 & AP_B2) N.in1 &= 0xbf;
    DIRS(N.in2, p2);
    if (p2 & AP_B3) N.in2 &= 0xef;
    if (p2 & AP_B1) N.in2 &= 0xdf;
    if (p2 & AP_B2) N.in2 &= 0xbf;
#endif
#undef DIRS
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) N.dsw_a = v;
    if (bank == 1) N.dsw_b = v;
    if (bank == 2) N.dsw_c = v;
}
