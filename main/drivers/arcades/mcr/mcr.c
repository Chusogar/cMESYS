// mcr.c - the Bally Midway MCR-II / MCR-III machines: Tapper (1983), Tron
// (1982), Satan's Hollow (1982), Domino Man (1982), Wacko (1982), Discs of
// Tron (1983) and Timber (1984), for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/mcr (mcr_hw.pas). A Z80 at 5 MHz with
// a Z80 CTC (daisy-chained IRQs, channel 0 cascaded into 1), a 32x30 map of
// 16x16 tiles and 32x32 sprites on a 512x480 picture, and the SSIO sound
// board: a Z80 at 2 MHz with two AY-3-8910 and the 14024 counter's IRQ.
// ARC_VARIANT: 0 tapper, 1 tron, 2 shollow, 3 domino, 4 wacko, 5 dotron,
// 6 timber.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define TRON_MAP (ARC_VARIANT >= 1 && ARC_VARIANT <= 4)

#define MAIN_CLOCK 5000000
#define SOUND_CLOCK 2000000
#define FPS 30.0
#define SCANLINES 480
#define RATE 44100
#define W 512
#define H 480
#define SSIO_PERIOD (160 * 2 * 16 * 10)

#if ARC_VARIANT == 0
static const char *const kSets[] = { "tapper", NULL };
static const arc_rom kMain[] = { {"tapper_c.p.u._pg_0_1c_1-27-84.1c", 0x4000, 0x0000, 0xbb060bb0}, {"tapper_c.p.u._pg_1_2c_1-27-84.2c", 0x4000, 0x4000, 0xfd9acc22}, {"tapper_c.p.u._pg_2_3c_1-27-84.3c", 0x4000, 0x8000, 0xb3755d41}, {"tapper_c.p.u._pg_3_4c_1-27-84.4c", 0x2000, 0xc000, 0x77273096}, };
static const arc_rom kSnd[] = { {"tapper_sound_snd_0_a7_12-7-83.a7", 0x1000, 0x0000, 0x0e8bb9d5}, {"tapper_sound_snd_1_a8_12-7-83.a8", 0x1000, 0x1000, 0x0cf0e29b}, {"tapper_sound_snd_2_a9_12-7-83.a9", 0x1000, 0x2000, 0x31eb6dc6}, {"tapper_sound_snd_3_a10_12-7-83.a10", 0x1000, 0x3000, 0x01a9be6a}, };
static const arc_rom kChars[] = { {"tapper_c.p.u._bg_1_6f_12-7-83.6f", 0x4000, 0x0000, 0x2a30238c}, {"tapper_c.p.u._bg_0_5f_12-7-83.5f", 0x4000, 0x4000, 0x394ab576}, };
static const arc_rom kSprites[] = { {"tapper_video_fg_1_a7_12-7-83.a7", 0x4000, 0x00000, 0x32509011}, {"tapper_video_fg_0_a8_12-7-83.a8", 0x4000, 0x04000, 0x8412c808}, {"tapper_video_fg_3_a5_12-7-83.a5", 0x4000, 0x08000, 0x818fffd4}, {"tapper_video_fg_2_a6_12-7-83.a6", 0x4000, 0x0c000, 0x67e37690}, {"tapper_video_fg_5_a3_12-7-83.a3", 0x4000, 0x10000, 0x800f7c8a}, {"tapper_video_fg_4_a4_12-7-83.a4", 0x4000, 0x14000, 0x32674ee6}, {"tapper_video_fg_7_a1_12-7-83.a1", 0x4000, 0x18000, 0x070b4c81}, {"tapper_video_fg_6_a2_12-7-83.a2", 0x4000, 0x1c000, 0xa37aef36}, };
const arc_info arc_game = { "tapper", "Tapper", "Bally Midway", 1983, W, H, FPS, RATE, kSets,
                            { "SERVE", "TAP", NULL, NULL }, ARC_JOY_8WAY };
#define CHARS 0x400
#define SPRITES 0x100
#define CHAR_BYTES 0x8000
#define SPRITE_BYTES 0x20000
#define DSW 0xc0
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "tron", NULL };
static const arc_rom kMain[] = { {"scpu-pga_lctn-c2_tron_aug_9.c2", 0x2000, 0x0000, 0x0de0471a}, {"scpu-pgb_lctn-c3_tron_aug_9.c3", 0x2000, 0x2000, 0x8ddf8717}, {"scpu-pgc_lctn-c4_tron_aug_9.c4", 0x2000, 0x4000, 0x4241e3a0}, {"scpu-pgd_lctn-c5_tron_aug_9.c5", 0x2000, 0x6000, 0x035d2fe7}, {"scpu-pge_lctn-c6_tron_aug_9.c6", 0x2000, 0x8000, 0x24c185d8}, {"scpu-pgf_lctn-c7_tron_aug_9.c7", 0x2000, 0xa000, 0x38c4bbaf}, };
static const arc_rom kSnd[] = { {"ssi-0a_lctn-a7_tron.a7", 0x1000, 0x0000, 0x765e6eba}, {"ssi-0b_lctn-a8_tron.a8", 0x1000, 0x1000, 0x1b90ccdd}, {"ssi-0c_lctn-a9_tron.a9", 0x1000, 0x2000, 0x3a4bc629}, };
static const arc_rom kChars[] = { {"scpu-bgg_lctn-g3_tron.g3", 0x2000, 0x0000, 0x1a9ed2f5}, {"lscpu-bgh_lctn-g4_tron.g4", 0x2000, 0x2000, 0x3220f974}, };
static const arc_rom kSprites[] = { {"vga_lctn-e1_tron.e1", 0x2000, 0x0000, 0xbc036d1d}, {"vgb_lctn-dc1_tron.dc1", 0x2000, 0x2000, 0x58ee14d3}, {"vgc_lctn-cb1_tron.cb1", 0x2000, 0x4000, 0x3329f9d4}, {"vgd_lctn-a1_tron.a1", 0x2000, 0x6000, 0x9743f873}, };
const arc_info arc_game = { "tron", "Tron", "Bally Midway", 1982, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_8WAY };
#define CHARS 0x200
#define SPRITES 0x40
#define CHAR_BYTES 0x4000
#define SPRITE_BYTES 0x8000
#define DSW 0x00
#elif ARC_VARIANT == 2
static const char *const kSets[] = { "shollow", NULL };
static const arc_rom kMain[] = { {"sh-pro.00", 0x2000, 0x0000, 0x95e2b800}, {"sh-pro.01", 0x2000, 0x2000, 0xb99f6ff8}, {"sh-pro.02", 0x2000, 0x4000, 0x1202c7b2}, {"sh-pro.03", 0x2000, 0x6000, 0x0a64afb9}, {"sh-pro.04", 0x2000, 0x8000, 0x22fa9175}, {"sh-pro.05", 0x2000, 0xa000, 0x1716e2bb}, };
static const arc_rom kSnd[] = { {"sh-snd.01", 0x1000, 0x0000, 0x55a297cc}, {"sh-snd.02", 0x1000, 0x1000, 0x46fc31f6}, {"sh-snd.03", 0x1000, 0x2000, 0xb1f4a6a8}, };
static const arc_rom kChars[] = { {"sh-bg.00", 0x2000, 0x0000, 0x3e2b333c}, {"sh-bg.01", 0x2000, 0x2000, 0xd1d70cc4}, };
static const arc_rom kSprites[] = { {"sh-fg.00", 0x2000, 0x0000, 0x33f4554e}, {"sh-fg.01", 0x2000, 0x2000, 0xba1a38b4}, {"sh-fg.02", 0x2000, 0x4000, 0x6b57f6da}, {"sh-fg.03", 0x2000, 0x6000, 0x37ea9d07}, };
const arc_info arc_game = { "shollow", "Satan's Hollow", "Bally Midway", 1982, W, H, FPS, RATE, kSets,
                            { "FIRE", "SHIELD", NULL, NULL }, ARC_JOY_8WAY };
#define CHARS 0x200
#define SPRITES 0x40
#define CHAR_BYTES 0x4000
#define SPRITE_BYTES 0x8000
#define DSW 0xfd
#elif ARC_VARIANT == 3
static const char *const kSets[] = { "domino", NULL };
static const arc_rom kMain[] = { {"dmanpg0.bin", 0x2000, 0x0000, 0x3bf3bb1c}, {"dmanpg1.bin", 0x2000, 0x2000, 0x85cf1d69}, {"dmanpg2.bin", 0x2000, 0x4000, 0x7dd2177a}, {"dmanpg3.bin", 0x2000, 0x6000, 0xf2e0aa44}, };
static const arc_rom kSnd[] = { {"dm-a7.snd", 0x1000, 0x0000, 0xfa982dcc}, {"dm-a8.snd", 0x1000, 0x1000, 0x72839019}, {"dm-a9.snd", 0x1000, 0x2000, 0xad760da7}, {"dm-a10.snd", 0x1000, 0x3000, 0x958c7287}, };
static const arc_rom kChars[] = { {"dmanbg0.bin", 0x2000, 0x0000, 0x9163007f}, {"dmanbg1.bin", 0x2000, 0x2000, 0x28615c56}, };
static const arc_rom kSprites[] = { {"dmanfg0.bin", 0x2000, 0x0000, 0x0b1f9f9e}, {"dmanfg1.bin", 0x2000, 0x2000, 0x16aa4b9b}, {"dmanfg2.bin", 0x2000, 0x4000, 0x4a8e76b8}, {"dmanfg3.bin", 0x2000, 0x6000, 0x1f39257e}, };
const arc_info arc_game = { "domino", "Domino Man", "Bally Midway", 1982, W, H, FPS, RATE, kSets,
                            { "ACTION", NULL, NULL, NULL }, ARC_JOY_8WAY };
#define CHARS 0x200
#define SPRITES 0x40
#define CHAR_BYTES 0x4000
#define SPRITE_BYTES 0x8000
#define DSW 0x3e
#elif ARC_VARIANT == 4
static const char *const kSets[] = { "wacko", NULL };
static const arc_rom kMain[] = { {"wackocpu.2d", 0x2000, 0x0000, 0xc98e29b6}, {"wackocpu.3d", 0x2000, 0x2000, 0x90b89774}, {"wackocpu.4d", 0x2000, 0x4000, 0x515edff7}, {"wackocpu.5d", 0x2000, 0x6000, 0x9b01bf32}, };
static const arc_rom kSnd[] = { {"wackosnd.7a", 0x1000, 0x0000, 0x1a58763f}, {"wackosnd.8a", 0x1000, 0x1000, 0xa4e3c771}, {"wackosnd.9a", 0x1000, 0x2000, 0x155ba3dd}, };
static const arc_rom kChars[] = { {"wackocpu.3g", 0x2000, 0x0000, 0x33160eb1}, {"wackocpu.4g", 0x2000, 0x2000, 0xdaf37d7c}, };
static const arc_rom kSprites[] = { {"wackovid.1e", 0x2000, 0x0000, 0xdca59be7}, {"wackovid.1d", 0x2000, 0x2000, 0xa02f1672}, {"wackovid.1b", 0x2000, 0x4000, 0x7d899790}, {"wackovid.1a", 0x2000, 0x6000, 0x080be3ad}, };
const arc_info arc_game = { "wacko", "Wacko", "Bally Midway", 1982, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_8WAY };
#define CHARS 0x200
#define SPRITES 0x40
#define CHAR_BYTES 0x4000
#define SPRITE_BYTES 0x8000
#define DSW 0x3e
#elif ARC_VARIANT == 5
static const char *const kSets[] = { "dotron", NULL };
static const arc_rom kMain[] = { {"disc_tron_uprt_pg0_10-4-83.1c", 0x4000, 0x0000, 0x40d00195}, {"disc_tron_uprt_pg1_10-4-83.2c", 0x4000, 0x4000, 0x5a7d1300}, {"disc_tron_uprt_pg2_10-4-83.3c", 0x4000, 0x8000, 0xcb89c9be}, {"disc_tron_uprt_pg3_10-4-83.4c", 0x2000, 0xc000, 0x5098faf4}, };
static const arc_rom kSnd[] = { {"disc_tron_uprt_snd0_10-4-83.a7", 0x1000, 0x0000, 0x7fb54293}, {"disc_tron_uprt_snd1_10-4-83.a8", 0x1000, 0x1000, 0xedef7326}, {"disc_tron_uprt_snd2_9-22-83.a9", 0x1000, 0x2000, 0xe8ef6519}, {"disc_tron_uprt_snd3_9-22-83.a10", 0x1000, 0x3000, 0x6b5aeb02}, };
static const arc_rom kChars[] = { {"loc-bg2.6f", 0x2000, 0x0000, 0x40167124}, {"loc-bg1.5f", 0x2000, 0x2000, 0xbb2d7a5d}, };
static const arc_rom kSprites[] = { {"loc-g.cp4", 0x2000, 0x0000, 0x57a2b1ff}, {"loc-h.cp3", 0x2000, 0x2000, 0x3bb4d475}, {"loc-e.cp6", 0x2000, 0x4000, 0xce957f1a}, {"loc-f.cp5", 0x2000, 0x6000, 0xd26053ce}, {"loc-c.cp8", 0x2000, 0x8000, 0xef45d146}, {"loc-d.cp7", 0x2000, 0xa000, 0x5e8a3ef3}, {"loc-a.cp0", 0x2000, 0xc000, 0xb35f5374}, {"loc-b.cp9", 0x2000, 0xe000, 0x565a5c48}, };
const arc_info arc_game = { "dotron", "Discs of Tron", "Bally Midway", 1983, W, H, FPS, RATE, kSets,
                            { "FIRE", "AIM", "DEFLECT", NULL }, ARC_JOY_8WAY };
#define CHARS 0x200
#define SPRITES 0x80
#define CHAR_BYTES 0x4000
#define SPRITE_BYTES 0x10000
#define DSW 0xff
#elif ARC_VARIANT == 6
static const char *const kSets[] = { "timber", NULL };
static const arc_rom kMain[] = { {"timpg0.bin", 0x4000, 0x0000, 0x377032ab}, {"timpg1.bin", 0x4000, 0x4000, 0xfd772836}, {"timpg2.bin", 0x4000, 0x8000, 0x632989f9}, {"timpg3.bin", 0x2000, 0xc000, 0xdae8a0dc}, };
static const arc_rom kSnd[] = { {"tima7.bin", 0x1000, 0x0000, 0xc615dc3e}, {"tima8.bin", 0x1000, 0x1000, 0x83841c87}, {"tima9.bin", 0x1000, 0x2000, 0x22bcdcd3}, };
static const arc_rom kChars[] = { {"timbg1.bin", 0x4000, 0x0000, 0xb1cb2651}, {"timbg0.bin", 0x4000, 0x4000, 0x2ae352c4}, };
static const arc_rom kSprites[] = { {"timfg1.bin", 0x4000, 0x00000, 0x81de4a73}, {"timfg0.bin", 0x4000, 0x04000, 0x7f3a4f59}, {"timfg3.bin", 0x4000, 0x08000, 0x37c03272}, {"timfg2.bin", 0x4000, 0x0c000, 0xe2c2885c}, {"timfg5.bin", 0x4000, 0x10000, 0xeb636216}, {"timfg4.bin", 0x4000, 0x14000, 0xb7105eb7}, {"timfg7.bin", 0x4000, 0x18000, 0xd9c27475}, {"timfg6.bin", 0x4000, 0x1c000, 0x244778e8}, };
const arc_info arc_game = { "timber", "Timber", "Bally Midway", 1984, W, H, FPS, RATE, kSets,
                            { "CHOP", "CHOP", NULL, NULL }, ARC_JOY_8WAY };
#define CHARS 0x400
#define SPRITES 0x100
#define CHAR_BYTES 0x8000
#define SPRITE_BYTES 0x20000
#define DSW 0xc0
#endif

// ---------------------------------------------------------------------------
// The Z80 CTC (dsp-cpp's cpu/z80ctc, from z80ctc.pas)

enum { CTC_INT = 0x80, CTC_COUNTER = 0x40, CTC_PRE256 = 0x20, CTC_RISING = 0x10, CTC_TRIGGER = 0x08,
       CTC_CONST = 0x04, CTC_RESET = 0x02, CTC_CONTROL = 0x01 };

typedef struct {
    uint16_t mode, tconst, down;
    int acc;
    bool extclk, waiting, running;
} ctc_ch;

static struct state {
    zd_z80 main, sound;
    ay8910 ay0, ay1;
    ctc_ch ctc[4];
    uint8_t ctc_vector;
    uint8_t mem[0x10000], sound_mem[0x10000], nvram[0x800];
    const uint8_t *chars, *sprites;
    uint16_t palette[256];
    uint16_t *fb;
    uint8_t prio[512 * 512 / 8];       // Tapper's sprites: a pixel taken (one bit a pixel)
    uint8_t in0, in1, in2, in3, dsw, ssio_status, ssio_data[4], ssio_14024;
    int ssio_acc;
    int64_t audio_acc;
    int16_t audio[2048];
    int naudio;
} *M_;
#define M (*M_)

static void ctc_trigger(int ch, bool v);

static void ctc_timer(int ch)
{
    ctc_ch *c = &M.ctc[ch & 3];
    if (c->mode & CTC_INT) zd_set_irq(&M.main, ZD_HOLD, (uint8_t)(M.ctc_vector + (ch & 3) * 2));
    if ((ch & 3) == 0) {                // (zero count of 0 cascaded into 1)
        ctc_trigger(1, true);
        ctc_trigger(1, false);
    }
    c->down = c->tconst;
}

static void ctc_reset(void)
{
    for (int i = 0; i < 4; i++) {
        M.ctc[i] = (ctc_ch){CTC_RESET, 0x100, 0x100, 0, false, false, false};
    }
    M.ctc_vector = 0;
    zd_set_irq(&M.main, ZD_CLEAR, 0);
}

static void ctc_write(int ch, uint8_t v)
{
    ctc_ch *c = &M.ctc[ch & 3];
    if (c->mode & CTC_CONST) {
        c->tconst = v ? v : 0x100;
        c->mode &= (uint16_t)~(CTC_CONST | CTC_RESET);
        c->down = c->tconst;
        c->acc = 0;
        if (!(c->mode & CTC_COUNTER)) {
            c->running = !(c->mode & CTC_TRIGGER);
            c->waiting = (c->mode & CTC_TRIGGER) != 0;
        } else c->running = false;
        return;
    }
    if (!(v & CTC_CONTROL) && (ch & 3) == 0) {
        M.ctc_vector = v & 0xf8;
        return;
    }
    if (v & CTC_CONTROL) {
        c->mode = v;
        c->waiting = false;
        if (v & CTC_RESET) {
            c->running = false;
            c->acc = 0;
        }
    }
}

static void ctc_trigger(int ch, bool v)
{
    ctc_ch *c = &M.ctc[ch & 3];
    if (v == c->extclk) return;
    c->extclk = v;
    if (!((c->mode & CTC_RISING) ? v : !v)) return;
    if (c->waiting && !(c->mode & CTC_COUNTER)) {
        c->running = true;
        c->waiting = false;
        c->acc = 0;
        c->down = c->tconst;
    }
    c->waiting = false;
    if ((c->mode & CTC_COUNTER) && !(c->mode & CTC_RESET)) {
        if (c->down <= 1) ctc_timer(ch);
        else c->down--;
    }
}

static void ctc_pulse(int ch)
{
    ctc_trigger(ch, true);
    ctc_trigger(ch, false);
}

static void ctc_tick(int cycles)
{
    for (int i = 0; i < 4; i++) {
        ctc_ch *c = &M.ctc[i];
        if (!c->running || (c->mode & (CTC_COUNTER | CTC_RESET))) continue;
        c->acc += cycles;
        const int step = (c->mode & CTC_PRE256) ? 256 : 16;
        while (c->acc >= step) {
            c->acc -= step;
            if (c->down <= 1) ctc_timer(i);
            else c->down--;
        }
    }
}

// ---------------------------------------------------------------------------
// The main CPU

static void set_color(int i, uint16_t v)
{
#define P3(x) (uint32_t)(((x) & 7) * 36)
    M.palette[i & 0xff] = arc_rgb565(P3(v >> 6) << 16 | P3(v) << 8 | P3(v >> 3));
#undef P3
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
#if TRON_MAP
    if (a <= 0xbfff) return M.mem[a];
    if (a <= 0xdfff) return M.nvram[a & 0x7ff];
    if ((a & 0xfff) <= 0x7ff) return M.mem[0xe000 + (a & 0x1ff)];
    return M.mem[0xe800 + (a & 0x7ff)];
#else
    if (a <= 0xdfff || (a >= 0xf000 && a <= 0xf7ff)) return M.mem[a];
    if (a >= 0xe000 && a <= 0xe7ff) return M.nvram[a & 0x7ff];
    if (a >= 0xe800 && a <= 0xebff) return M.mem[0xe800 + (a & 0x1ff)];
    return 0xff;
#endif
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if TRON_MAP
    if (a <= 0xbfff) return;
    if (a <= 0xdfff) M.nvram[a & 0x7ff] = v;
    else if ((a & 0xfff) <= 0x7ff) M.mem[0xe000 + (a & 0x1ff)] = v;
    else {
        M.mem[0xe800 + (a & 0x7ff)] = v;
        if ((a & 0x780) == 0x780) set_color((a & 0x7f) >> 1, (uint16_t)(v | ((a & 1) << 8)));
    }
#else
    if (a <= 0xdfff) return;
    if (a <= 0xe7ff) M.nvram[a & 0x7ff] = v;
    else if (a >= 0xe800 && a <= 0xebff) M.mem[0xe800 + (a & 0x1ff)] = v;
    else if (a >= 0xf000 && a <= 0xf7ff) M.mem[a] = v;
    else if (a >= 0xf800) set_color((a & 0x7f) >> 1, (uint16_t)(v | ((a & 1) << 8)));
#endif
}

static uint8_t main_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = port & 0xff;
    if (p <= 0x1f) {
        switch (p & 7) {
        case 0: return M.in0;
        case 1: return M.in1;
        case 2: return M.in2;
        case 3: return M.dsw;
        case 4: return M.in3;
        case 7: return M.ssio_status;
        default: return 0xff;
        }
    }
    if (p >= 0xf0 && p <= 0xf3) return (uint8_t)M.ctc[p & 3].down;
    return 0xff;
}

static void main_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = port & 0xff;
    if (p >= 0x1c && p <= 0x1f) M.ssio_data[p & 3] = v;
    else if (p >= 0xf0 && p <= 0xf3) ctc_write(p & 3, v);
}

static void main_cycles(void *u, int c)
{
    (void)u;
    ctc_tick(c);
    M.audio_acc += (int64_t)c * RATE;
    while (M.audio_acc >= MAIN_CLOCK) {
        M.audio_acc -= MAIN_CLOCK;
        const int32_t s = ay8910_render(&M.ay0) + ay8910_render(&M.ay1);
        if (M.naudio < 2048) M.audio[M.naudio++] = arc_clamp16(s);
    }
}

// ---------------------------------------------------------------------------
// The SSIO

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff) return M.sound_mem[a];
    if (a >= 0x8000 && a <= 0x8fff) return M.sound_mem[0x8000 | (a & 0x3ff)];
    if (a >= 0x9000 && a <= 0x9fff) return M.ssio_data[a & 3];
    if (a >= 0xa000 && a <= 0xafff && (a & 3) == 1) return ay8910_data_r(&M.ay0);
    if (a >= 0xb000 && a <= 0xbfff && (a & 3) == 1) return ay8910_data_r(&M.ay1);
    if (a >= 0xe000 && a <= 0xefff) {
        M.ssio_14024 = 0;
        zd_set_irq(&M.sound, ZD_CLEAR, 0xff);
    }
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
    if (a >= 0x8000 && a <= 0x8fff) M.sound_mem[0x8000 | (a & 0x3ff)] = v;
    else if (a >= 0xa000 && a <= 0xafff) {
        if ((a & 3) == 0) ay8910_address_w(&M.ay0, v);
        if ((a & 3) == 2) ay8910_data_w(&M.ay0, v);
    } else if (a >= 0xb000 && a <= 0xbfff) {
        if ((a & 3) == 0) ay8910_address_w(&M.ay1, v);
        if ((a & 3) == 2) ay8910_data_w(&M.ay1, v);
    } else if (a >= 0xc000 && a <= 0xcfff) M.ssio_status = v;
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// ---------------------------------------------------------------------------
// Video

static int kSprX[32];
static const int kCharX[16] = {0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14};
static const int kCharY[16] = {0, 0, 16, 16, 32, 32, 48, 48, 64, 64, 80, 80, 96, 96, 112, 112};
static int kSprY[32];
static const arc_layout kCharL = { 16, 16, CHARS, 4, 128, false, false, false, {CHARS * 128, CHARS * 128 + 1, 0, 1}, kCharX, kCharY };
static const arc_layout kSprL = { 32, 32, SPRITES, 4, 1024, false, false, false, {0, 1, 2, 3}, kSprX, kSprY };

static inline unsigned char_px(int code, int x, int y) { return arc_gfx_px(&kCharL, M.chars, CHAR_BYTES, code, x, y); }
static inline unsigned spr_px(int code, int x, int y) { return arc_gfx_px(&kSprL, M.sprites, SPRITE_BYTES, code, x, y); }

static void update_video(void)
{
#if TRON_MAP
    uint8_t tprio[32 * 32] = {0};
    const int tbase = 0xe800, flipx_bit = 0x200, flipy_bit = 0x400, color_shift = 11;
#else
    const int tbase = 0xf000, flipx_bit = 0x400, flipy_bit = 0x800, color_shift = 12;
#endif
    for (int f = 0; f < 0x3c0; f++) {   // 32 x 30 tiles of 16x16
        const uint16_t attr = (uint16_t)(M.mem[tbase + f * 2] | (M.mem[tbase + 1 + f * 2] << 8));
        const int tx = f & 0x1f, ty = f >> 5, pal = ((attr >> color_shift) & 3) << 4, code = attr & 0x3ff;
#if TRON_MAP
        tprio[ty * 32 + tx] = (uint8_t)(attr >> 14);
#endif
        const bool fx = (attr & flipx_bit) != 0, fy = (attr & flipy_bit) != 0;
        for (int y = 0; y < 16; y++) {
            uint16_t *out = M.fb + (ty * 16 + y) * W + tx * 16;
            for (int x = 0; x < 16; x++) out[x] = M.palette[(pal + char_px(code, fx ? 15 - x : x, fy ? 15 - y : y)) & 0xff];
        }
    }
#if TRON_MAP
    for (int f = 0; f < 0x80; f++) {
        const int base = 0xe000 + f * 4;
        const int sx = ((M.mem[base + 2] - 4) * 2) & 0x1ff, sy = ((240 - M.mem[base]) * 2) & 0x1ff;
        const uint8_t attr = M.mem[base + 1];
        const int code = attr % SPRITES;
        const bool fy = (attr & 0x80) != 0, fx = (attr & 0x40) != 0;
        for (int y = 0; y < 32; y++) {
            const int dy = (sy + y) & 0x1ff;
            for (int x = 0; x < 32; x++) {
                const int dx = (sx + x) & 0x1ff;
                const uint8_t pen = (uint8_t)((tprio[((dy / 16) & 0x1f) * 32 + ((dx / 16) & 0x1f)] << 4) | spr_px(code, fx ? 31 - x : x, fy ? 31 - y : y));
                if ((pen & 7) == 0) continue;
                if (dx < W && dy < H) M.fb[dy * W + dx] = M.palette[pen];
            }
        }
    }
#else
    memset(M.prio, 0, sizeof M.prio);
    for (int f = 0x7f; f >= 0; f--) {
        const int base = 0xe800 + f * 4;
        const int sx = ((M.mem[base + 3] - 3) * 2) & 0x1ff, sy = ((241 - M.mem[base]) * 2) & 0x1ff;
        const uint8_t attr = M.mem[base + 1];
        const int code = (M.mem[base + 2] + ((attr & 8) << 5)) % SPRITES, color = ((~attr) & 3) << 4;
        const bool fx = (attr & 0x10) != 0, fy = (attr & 0x20) != 0;
        for (int y = 0; y < 32; y++) {
            const int dy = (sy + y) & 0x1ff;
            for (int x = 0; x < 32; x++) {
                const int dx = (sx + x) & 0x1ff, k = dx * 512 + dy;
                if (M.prio[k >> 3] & (1 << (k & 7))) continue;
                const unsigned pen = spr_px(code, fx ? 31 - x : x, fy ? 31 - y : y);
                if ((pen & 0x0f) == 0) continue;
                M.prio[k >> 3] |= (uint8_t)(1 << (k & 7));
                if ((pen & 7) == 0) continue;
                if (dx < W && dy < H) M.fb[dy * W + dx] = M.palette[(color + pen) & 0xff];
            }
        }
    }
#endif
}

bool arc_init(void)
{
    if (!(M_ = (struct state *)calloc(1, sizeof(struct state))) || !(M.fb = (uint16_t *)calloc(W * H, 2))) return false;
    memset(M.mem, 0xff, 0x10000);
    memset(M.sound_mem, 0xff, 0x4000);
    if (!ARC_LOAD(kMain, M.mem, 0x10000) || !ARC_LOAD(kSnd, M.sound_mem, 0x4000) ||
        !(M.chars = ARC_ROM_BLOB("chars", kChars, CHAR_BYTES)) || !(M.sprites = ARC_ROM_BLOB("sprites", kSprites, SPRITE_BYTES)))
        return false;
    // (dsp-cpp: the ROM image of 0x10000 bytes over the whole space, 0xff where none)
    const int bank = SPRITES * 32 * 32;
    for (int i = 0; i < 4; i++) {
        const int bit = i * 8;
        kSprX[i * 8 + 0] = bit, kSprX[i * 8 + 1] = bit + 4;
        kSprX[i * 8 + 2] = bank + bit, kSprX[i * 8 + 3] = bank + bit + 4;
        kSprX[i * 8 + 4] = 2 * bank + bit, kSprX[i * 8 + 5] = 2 * bank + bit + 4;
        kSprX[i * 8 + 6] = 3 * bank + bit, kSprX[i * 8 + 7] = 3 * bank + bit + 4;
    }
    for (int y = 0; y < 32; y++) kSprY[y] = y * 32;
    M.main.read = main_read;
    M.main.write = main_write;
    M.main.in = main_in;
    M.main.out = main_out;
    M.main.cycles = main_cycles;
    M.sound.read = sound_read;
    M.sound.write = sound_write;
    M.sound.in = in_ff;
    M.sound.out = out_nop;
    zd_init(&M.main);
    zd_init(&M.sound);
    ay8910_init(&M.ay0, SOUND_CLOCK, RATE);
    ay8910_init(&M.ay1, SOUND_CLOCK, RATE);
    M.dsw = DSW;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&M.main);
    zd_reset(&M.sound);
    // (the index registers power on as $ffff: Tapper's boot copy relies on it)
    M.main.ix = M.main.iy = M.sound.ix = M.sound.iy = 0xffff;
    ctc_reset();
    ay8910_reset(&M.ay0);
    ay8910_reset(&M.ay1);
    memset(M.nvram, 0, sizeof M.nvram);
    M.in0 = M.in1 = M.in2 = M.in3 = 0xff;
    M.ssio_status = 0;
    memset(M.ssio_data, 0, sizeof M.ssio_data);
    M.ssio_14024 = 0;
    M.ssio_acc = 0;
    M.audio_acc = 0;
    memset(M.palette, 0, sizeof M.palette);
    for (int i = 0; i < W * H; i++) M.fb[i] = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int ml = (int)(MAIN_CLOCK / FPS / SCANLINES), sl = (int)(SOUND_CLOCK / FPS / SCANLINES);
    M.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) {
            ctc_pulse(2);
            update_video();
            ctc_pulse(3);
        } else if (line == 240) ctc_pulse(2);
        for (int left = ml; left > 0;) {
            const int ran = zd_run(&M.main, left);
            if (ran <= 0) break;
            left -= ran;
        }
        for (int left = sl; left > 0;) {
            const int ran = zd_run(&M.sound, left);
            if (ran <= 0) break;
            left -= ran;
        }
        M.ssio_acc += sl;
        while (M.ssio_acc >= SSIO_PERIOD) {     // the 14024: the sound CPU's IRQ
            M.ssio_acc -= SSIO_PERIOD;
            M.ssio_14024 = (M.ssio_14024 + 1) & 0x7f;
            if ((M.ssio_14024 & 0x3f) == 0) zd_set_irq(&M.sound, (M.ssio_14024 & 0x40) ? ZD_ASSERT : ZD_CLEAR, 0xff);
        }
    }
    if (audio) *audio = M.audio;
    return M.naudio;
}

const uint16_t *arc_frame(void) { return M.fb; }

static uint8_t axis(bool neg, bool pos) { return neg && !pos ? 0x00 : pos && !neg ? 0xff : 0x80; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
#define L(p) (((p) & AP_LEFT) != 0)
#define R(p) (((p) & AP_RIGHT) != 0)
#define U(p) (((p) & AP_UP) != 0)
#define D(p) (((p) & AP_DOWN) != 0)
    uint8_t i0 = 0xff;
    if (in->coin1) i0 &= 0xfe;
    if (in->coin2) i0 &= 0xfd;
    if (p1 & AP_START) i0 &= 0xfb;
    if (p2 & AP_START) i0 &= 0xf7;
#if ARC_VARIANT == 0 || ARC_VARIANT == 6       // Tapper, Timber
    for (int k = 0; k < 2; k++) {
        const unsigned p = k ? p2 : p1;
        uint8_t v = 0xff;
        if (R(p)) v &= 0xfe;
        if (L(p)) v &= 0xfd;
        if (D(p)) v &= 0xfb;
        if (U(p)) v &= 0xf7;
        if (p & AP_B2) v &= 0xef;
        if (p & AP_B1) v &= 0xdf;
        if (k) M.in2 = v;
        else M.in1 = v;
    }
#elif ARC_VARIANT == 5                         // Discs of Tron
    M.in1 = axis(L(p1), R(p1)) | 0x80;
    if (p1 & AP_B1) i0 &= 0xef;
    uint8_t i2 = 0xff;
    if (L(p1)) i2 &= 0xfe;
    if (R(p1)) i2 &= 0xfd;
    if (U(p1)) i2 &= 0xfb;
    if (D(p1)) i2 &= 0xf7;
    if (p1 & AP_B3) i2 &= 0xef;
    if (p2 & AP_B1) i2 &= 0xdf;
    if (p1 & AP_B2) i2 &= 0xbf;
    M.in2 = i2;
#elif ARC_VARIANT == 1                         // Tron
    M.in1 = axis(L(p1), R(p1));
    if (p1 & AP_B1) i0 &= 0xef;
    uint8_t i2 = 0xff;
    if (L(p1)) i2 &= 0xfe;
    if (R(p1)) i2 &= 0xfd;
    if (U(p1)) i2 &= 0xfb;
    if (D(p1)) i2 &= 0xf7;
    if (L(p2)) i2 &= 0xef;
    if (R(p2)) i2 &= 0xdf;
    if (U(p2)) i2 &= 0xbf;
    if (D(p2)) i2 &= 0x7f;
    M.in2 = i2;
#elif ARC_VARIANT == 2                         // Satan's Hollow
    uint8_t i1 = 0xff;
    if (L(p1)) i1 &= 0xfe;
    if (R(p1)) i1 &= 0xfd;
    if (p1 & AP_B2) i1 &= 0xfb;
    if (p1 & AP_B1) i1 &= 0xf7;
    if (L(p2)) i1 &= 0xef;
    if (R(p2)) i1 &= 0xdf;
    if (p2 & AP_B2) i1 &= 0xbf;
    if (p2 & AP_B1) i1 &= 0x7f;
    M.in1 = i1;
#elif ARC_VARIANT == 3                         // Domino Man
    if (p1 & AP_B1) i0 &= 0xef;
    uint8_t i1 = 0xff, i2 = 0xff;
    if (L(p1)) i1 &= 0xfe;
    if (R(p1)) i1 &= 0xfd;
    if (U(p1)) i1 &= 0xfb;
    if (D(p1)) i1 &= 0xf7;
    if (L(p2)) i2 &= 0xfe;
    if (R(p2)) i2 &= 0xfd;
    if (U(p2)) i2 &= 0xfb;
    if (D(p2)) i2 &= 0xf7;
    if (p2 & AP_B1) i2 &= 0xef;
    M.in1 = i1, M.in2 = i2;
#else                                          // Wacko
    M.in1 = axis(L(p1), R(p1));
    M.in2 = axis(U(p1), D(p1));
    if (p1 & AP_B1) i0 &= 0xef;
    uint8_t i3 = 0xff;
    if (R(p1)) i3 &= 0xfe;
    if (L(p1)) i3 &= 0xfd;
    if (D(p1)) i3 &= 0xfb;
    if (U(p1)) i3 &= 0xf7;
    if (R(p2)) i3 &= 0xef;
    if (L(p2)) i3 &= 0xdf;
    if (D(p2)) i3 &= 0xbf;
    if (U(p2)) i3 &= 0x7f;
    M.in3 = i3;
#endif
    (void)axis;
    M.in0 = i0;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) M.dsw = v;
}
