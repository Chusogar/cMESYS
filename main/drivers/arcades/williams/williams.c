// williams.c - Defender (1980), Mayday (Hoei, 1980), Colony 7 (Taito, 1981),
// Stargate (1981), Robotron: 2084 (1982) and Joust (1982), the Williams
// 6809 machines, for the common arcade frontends (arc.h): C port of dsp-cpp's
// drivers/arcade/williams (dsp-emulator's williams_hw.pas). A 6809 at 1 MHz
// drawing in a 304x256 bitmap of 4-bit pixels (16 colours), banked ROM, two
// 6821 PIAs for the inputs and the IRQs, and the Joust-era blitter; the sound
// board is a 6808 with a PIA and an 8-bit DAC.
//
// The picture is dsp-cpp's. The sound is not: dsp-cpp's 6800 runs only a few
// of the opcodes, four times too fast, and its DAC is sampled once a frame;
// here the 6808 is a full 6800 (cpu/hd63701dsp in its 6800 mode) at its own
// 894.886 kHz, and the DAC is heard as it is written. The CMOS is not kept:
// each start is a first boot, the operator's Advance pressed for the player
// as dsp-cpp does then. ARC_VARIANT: 0 defender, 1 mayday, 2 colony7,
// 3 joust, 4 robotron, 5 stargate.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/hd63701dsp/hd63701dsp.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define DEFENDER (ARC_VARIANT == 0)
#define MAYDAY (ARC_VARIANT == 1)
#define COLONY7 (ARC_VARIANT == 2)
#define JOUST (ARC_VARIANT == 3)
#define ROBOTRON (ARC_VARIANT == 4)
#define STARGATE (ARC_VARIANT == 5)
#define BLITTER (ARC_VARIANT >= 3)

#define MAIN_CLOCK 1000000
#define SOUND_CLOCK 894886
#define FPS 60.096154
#define SCANLINES 260
#define CPU_SYNC 8
#define RATE 44100
#define FB_W 304
#define VIS_W 292
#define VIS_H 240
#if COLONY7                             // a turned monitor (ROT270)
#define W VIS_H
#define H VIS_W
#else
#define W VIS_W
#define H VIS_H
#endif
#define XOFF (BLITTER ? 6 : 12)

typedef struct { const char *name; uint32_t size, at; } piece;

#if DEFENDER
static const char *const kSets[] = { "defender", NULL };
static const piece kPieces[] = {
    {"defend.1", 0x800, 0}, {"defend.4", 0x800, 0x800}, {"defend.2", 0x1000, 0x1000}, {"defend.3", 0x1000, 0x2000},
    {"defend.9", 0x800, 0x3000}, {"defend.12", 0x800, 0x3800}, {"defend.8", 0x800, 0x4000}, {"defend.11", 0x800, 0x4800},
    {"defend.7", 0x800, 0x5000}, {"defend.10", 0x800, 0x5800}, {"defend.6", 0x800, 0x9000}};
static const char *const kSound = "defend.snd|video_sound_rom_1.ic12";
#define SND_BASE 0xf800
const arc_info arc_game = { "defender", "Defender", "Williams", 1980, W, H, FPS, RATE, kSets,
                            { "FIRE", "THRUST", "SMART BOMB", "HYPERSPACE" }, ARC_JOY_8WAY };
#elif MAYDAY
static const char *const kSets[] = { "mayday", NULL };
static const piece kPieces[] = {
    {"ic03-3.bin|mayday.c", 0x1000, 0}, {"ic02-2.bin|mayday.b", 0x1000, 0x1000}, {"ic01-1.bin|mayday.a", 0x1000, 0x2000},
    {"ic04-4.bin|mayday.d", 0x1000, 0x3000}, {"ic05-5.bin|mayday.e", 0x1000, 0x4000}, {"ic06-6.bin|mayday.f", 0x1000, 0x5000},
    {"ic07-7d.bin|mayday.g", 0x1000, 0x9000}};
static const char *const kSound = "ic28-8.bin";
#define SND_BASE 0xf800
const arc_info arc_game = { "mayday", "Mayday", "Hoei", 1980, W, H, FPS, RATE, kSets,
                            { "FIRE", "THRUST", "SMART BOMB", "HYPERSPACE" }, ARC_JOY_8WAY };
#elif COLONY7
static const char *const kSets[] = { "colony7", NULL };
static const piece kPieces[] = {
    {"cs03.bin", 0x1000, 0}, {"cs02.bin", 0x1000, 0x1000}, {"cs01.bin", 0x1000, 0x2000}, {"cs06.bin", 0x800, 0x3000},
    {"cs04.bin", 0x800, 0x3800}, {"cs07.bin", 0x800, 0x4000}, {"cs05.bin", 0x800, 0x4800}, {"cs08.bin", 0x800, 0x5000},
    {"cs08.bin", 0x800, 0x5800}};
static const char *const kSound = "cs11.bin";
#define SND_BASE 0xf800
#define NO_SND_MIRROR 1                 // (as dsp-cpp: only f800-ffff)
const arc_info arc_game = { "colony7", "Colony 7", "Taito", 1981, W, H, FPS, RATE, kSets,
                            { "FIRE", "SMART BOMB", "WARP", NULL }, ARC_JOY_8WAY };
#else
#if JOUST
static const char *const kSets[] = { "joust", NULL };
static const piece kPieces[] = {
    {"joust_rom_10b_3006-22.a7", 0x1000, 0x0000}, {"joust_rom_11b_3006-23.c7", 0x1000, 0x1000},
    {"joust_rom_12b_3006-24.e7", 0x1000, 0x2000}, {"joust_rom_1b_3006-13.e4", 0x1000, 0x3000},
    {"joust_rom_2b_3006-14.c4", 0x1000, 0x4000}, {"joust_rom_3b_3006-15.a4", 0x1000, 0x5000},
    {"joust_rom_4b_3006-16.e5", 0x1000, 0x6000}, {"joust_rom_5b_3006-17.c5", 0x1000, 0x7000},
    {"joust_rom_6b_3006-18.a5", 0x1000, 0x8000}, {"joust_rom_7b_3006-19.e6", 0x1000, 0x9000},
    {"joust_rom_8b_3006-20.c6", 0x1000, 0xa000}, {"joust_rom_9b_3006-21.a6", 0x1000, 0xb000}};
static const char *const kSound = "video_sound_rom_4_std_780.ic12";
#define SND_BASE 0xf000
const arc_info arc_game = { "joust", "Joust", "Williams", 1982, W, H, FPS, RATE, kSets,
                            { "FLAP", NULL, NULL, NULL }, ARC_JOY_2WAY };
#elif ROBOTRON
static const char *const kSets[] = { "robotron", NULL };
static const piece kPieces[] = {
    {"2084_rom_10b_3005-22.a7", 0x1000, 0x0000}, {"2084_rom_11b_3005-23.c7", 0x1000, 0x1000},
    {"2084_rom_12b_3005-24.e7", 0x1000, 0x2000}, {"2084_rom_1b_3005-13.e4", 0x1000, 0x3000},
    {"2084_rom_2b_3005-14.c4", 0x1000, 0x4000}, {"2084_rom_3b_3005-15.a4", 0x1000, 0x5000},
    {"2084_rom_4b_3005-16.e5", 0x1000, 0x6000}, {"2084_rom_5b_3005-17.c5", 0x1000, 0x7000},
    {"2084_rom_6b_3005-18.a5", 0x1000, 0x8000}, {"2084_rom_7b_3005-19.e6", 0x1000, 0x9000},
    {"2084_rom_8b_3005-20.c6", 0x1000, 0xa000}, {"2084_rom_9b_3005-21.a6", 0x1000, 0xb000}};
static const char *const kSound = "video_sound_rom_3_std_767.ic12";
#define SND_BASE 0xf000
const arc_info arc_game = { "robotron", "Robotron: 2084", "Williams", 1982, W, H, FPS, RATE, kSets,
                            { "FIRE UP", "FIRE DOWN", "FIRE LEFT", "FIRE RIGHT" }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "stargate", NULL };
static const piece kPieces[] = {
    {"stargate_rom_10-a_3002-10.a7", 0x1000, 0x0000}, {"stargate_rom_11-a_3002-11.c7", 0x1000, 0x1000},
    {"stargate_rom_12-a_3002-12.e7", 0x1000, 0x2000}, {"stargate_rom_1-a_3002-1.e4", 0x1000, 0x3000},
    {"stargate_rom_2-a_3002-2.c4", 0x1000, 0x4000}, {"stargate_rom_3-a_3002-3.a4", 0x1000, 0x5000},
    {"stargate_rom_4-a_3002-4.e5", 0x1000, 0x6000}, {"stargate_rom_5-a_3002-5.c5", 0x1000, 0x7000},
    {"stargate_rom_6-a_3002-6.a5", 0x1000, 0x8000}, {"stargate_rom_7-a_3002-7.e6", 0x1000, 0x9000},
    {"stargate_rom_8-a_3002-8.c6", 0x1000, 0xa000}, {"stargate_rom_9-a_3002-9.a6", 0x1000, 0xb000}};
static const char *const kSound = "video_sound_rom_2_std_744.ic12";
#define SND_BASE 0xf800
const arc_info arc_game = { "stargate", "Stargate", "Williams", 1981, W, H, FPS, RATE, kSets,
                            { "FIRE", "THRUST", "SMART BOMB", "REVERSE" }, ARC_JOY_8WAY };
#endif
#endif

// ---------------------------------------------------------------------------
// The 6821 PIA (dsp-cpp's machine/pia6821)

typedef struct {
    uint8_t ctl_a, ctl_b, ddr_a, ddr_b, out_a, out_b, in_a, in_b;
    bool in_ca1, in_cb1, out_ca2, out_cb2, irq_a1, irq_a2, irq_b1, irq_b2, irq_a, irq_b;
    uint8_t (*rd_a)(void), (*rd_b)(void);
    void (*wr_a)(uint8_t), (*wr_b)(uint8_t), (*irq)(void), (*cb2)(bool);
} pia_t;

static void pia_reset(pia_t *p)
{
    p->ctl_a = p->ctl_b = p->ddr_a = p->ddr_b = p->out_a = p->out_b = 0;
    p->in_ca1 = true;
    p->in_cb1 = p->out_ca2 = p->out_cb2 = false;
    p->in_a = 0xff;
    p->in_b = 0;
    p->irq_a1 = p->irq_a2 = p->irq_b1 = p->irq_b2 = p->irq_a = p->irq_b = false;
}

static void pia_irqs(pia_t *p)
{
    const bool na = (p->irq_a1 && (p->ctl_a & 1)) || (p->irq_a2 && (p->ctl_a & 8) && !(p->ctl_a & 0x20));
    const bool nb = (p->irq_b1 && (p->ctl_b & 1)) || (p->irq_b2 && (p->ctl_b & 8) && !(p->ctl_b & 0x20));
    bool changed = false;
    if (na != p->irq_a) p->irq_a = na, changed = true;
    if (nb != p->irq_b) p->irq_b = nb, changed = true;
    if (changed && p->irq) p->irq();
}

static void pia_cb2(pia_t *p, bool d)
{
    if (d == p->out_cb2) return;
    p->out_cb2 = d;
    if (p->cb2) p->cb2(d);
}

static uint8_t pia_read(pia_t *p, int o)
{
    switch (o & 3) {
    case 0: {
        if (!(p->ctl_a & 4)) return p->ddr_a;
        const uint8_t v = (uint8_t)((p->out_a & p->ddr_a) | ((p->rd_a ? p->rd_a() : p->in_a) & ~p->ddr_a));
        p->irq_a1 = p->irq_a2 = false;
        pia_irqs(p);
        if ((p->ctl_a & 0x30) == 0x20 && (p->ctl_a & 8)) p->out_ca2 = true;
        return v;
    }
    case 1: return (uint8_t)((p->ctl_a & ~0xc0) | (p->irq_a1 ? 0x80 : 0) | (p->irq_a2 ? 0x40 : 0));
    case 2: {
        if (!(p->ctl_b & 4)) return p->ddr_b;
        const uint8_t v = (uint8_t)((p->out_b & p->ddr_b) | ((p->rd_b ? p->rd_b() : p->in_b) & ~p->ddr_b));
        if (p->irq_b1 && (p->ctl_b & 0x30) == 0x20 && (p->ctl_b & 8)) pia_cb2(p, true);
        p->irq_b1 = p->irq_b2 = false;
        pia_irqs(p);
        return v;
    }
    default: return (uint8_t)((p->ctl_b & ~0xc0) | (p->irq_b1 ? 0x80 : 0) | (p->irq_b2 ? 0x40 : 0));
    }
}

static void pia_write(pia_t *p, int o, uint8_t v)
{
    switch (o & 3) {
    case 0:
        if (!(p->ctl_a & 4)) p->ddr_a = v;
        else {
            p->out_a = v;
            if ((p->ctl_a & 0x30) == 0x20) p->out_ca2 = false;
        }
        if (p->wr_a) p->wr_a((uint8_t)(p->out_a & p->ddr_a));
        break;
    case 1: {
        const bool was = (p->ctl_a & 0x20) != 0;
        p->ctl_a = v & 0x3f;
        pia_irqs(p);
        if (p->ctl_a & 0x20) {
            if ((p->ctl_a & 0x38) == 0x30) p->out_ca2 = false;
            else if ((p->ctl_a & 0x38) == 0x38) p->out_ca2 = true;
        } else if (was) p->out_ca2 = false;
        break;
    }
    case 2:
        if (!(p->ctl_b & 4)) p->ddr_b = v;
        else {
            p->out_b = v;
            if ((p->ctl_b & 0x30) == 0x20) pia_cb2(p, false);
        }
        if (p->wr_b) p->wr_b(p->out_b);
        break;
    default: {
        const bool was = (p->ctl_b & 0x20) != 0;
        p->ctl_b = v & 0x3f;
        pia_irqs(p);
        if (p->ctl_b & 0x20) {
            if ((p->ctl_b & 0x38) == 0x30) pia_cb2(p, false);
            else if ((p->ctl_b & 0x38) == 0x38) pia_cb2(p, true);
        } else if (was) pia_cb2(p, false);
        break;
    }
    }
}

static void pia_ca1(pia_t *p, bool s)
{
    if (p->in_ca1 == s) return;
    if ((s && (p->ctl_a & 2)) || (!s && !(p->ctl_a & 2))) {
        p->irq_a1 = true;
        pia_irqs(p);
        if ((p->ctl_a & 0x30) == 0x20 && !(p->ctl_a & 8)) p->out_ca2 = true;
    }
    p->in_ca1 = s;
}

static void pia_cb1(pia_t *p, bool s)
{
    if (p->in_cb1 == s) return;
    if ((s && (p->ctl_b & 2)) || (!s && !(p->ctl_b & 2))) {
        p->irq_b1 = true;
        pia_irqs(p);
    }
    p->in_cb1 = s;
}

// ---------------------------------------------------------------------------

static struct state {
    m6809d main;
    hd63701 snd;
    pia_t pia0, pia1, pia2;
    uint8_t mem[0x10000], snd_mem[0x10000], rom_bank[16][0x1000], nvram[0x400], palette[16];
    uint16_t pal_lookup[256];
    uint16_t *fb;                       // the visible 292x240 (Colony 7: turned)
    uint16_t late[8][VIS_W];            // lines 239-246: drawn after the frame is shown, so in the next
    uint8_t ram_bank, sound_latch, dsw_a, in0, in1, in2, in3, blit[8];
    int scanline, cmos_burst, auto_advance;
    bool ram_rom, player_select, advance, service;
    double frame_main, frame_snd, tf_main, tf_snd;
    int16_t dac;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

static void update_main_irq(void) { m6809d_set_irq(&S.main, S.pia1.irq_a || S.pia1.irq_b ? M6809D_ASSERT : M6809D_CLEAR); }
static void update_sound_irq(void) { hd63701_set_irq(&S.snd, S.pia2.irq_a || S.pia2.irq_b ? HD_ASSERT : HD_CLEAR); }

static void sound_latch_w(uint8_t v)
{
    S.sound_latch = v | 0xc0;
    S.pia2.in_b = S.sound_latch;
    pia_cb1(&S.pia2, S.sound_latch != 0xff);
}

static uint8_t in0_r(void) { return S.in0; }
static uint8_t in1_r(void) { return S.in1; }
static uint8_t joust_in0(void) { return (uint8_t)(S.in0 | (S.player_select ? S.in1 : S.in3)); }
static uint8_t zero_r(void) { return 0; }
static void joust_cb2(bool s) { S.player_select = s; }
static uint8_t in2_r(void)
{
#if BLITTER
    return (uint8_t)(0x01 | (S.advance ? 0x02 : 0) | (S.in2 & 0x30) | S.dsw_a);
#else
    return (uint8_t)(0x01 | (S.advance ? 0x02 : 0) | (S.in2 & 0x70));
#endif
}
static void dac_w(uint8_t v) { S.dac = (int16_t)((v - 0x80) * 256); }

static void cmos_w(int i, uint8_t v)
{
    const uint8_t n = (uint8_t)(0xf0 | v);
    if (S.nvram[i] != n) {
        S.nvram[i] = n;
        S.cmos_burst++;
    }
}

#if !BLITTER
static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xbfff || a >= 0xd000) {
        if (MAYDAY && (a == 0xa190 || a == 0xa191)) return S.mem[a + 3];   // the protection
        return S.mem[a];
    }
    if (S.ram_bank == 0) {
        const uint16_t o = a & 0xfff;
        if (o >= 0x400 && o <= 0x7ff) return S.nvram[o & 0xff];
        if (o >= 0x800 && o <= 0xbff) return S.scanline < 0x100 ? (uint8_t)(S.scanline & 0xfc) : 0xfc;
        if (o >= 0xc00) {
            const uint8_t r = o & 0x1f;
            if (r <= 3) return pia_read(&S.pia1, r);
            if (r <= 7) return pia_read(&S.pia0, r);
        }
        return 0xff;
    }
    return S.rom_bank[S.ram_bank - 1][a & 0xfff];
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) S.mem[a] = v;
    else if (a >= 0xd000 && a <= 0xdfff) S.ram_bank = v & 0x0f;
    else if (a >= 0xc000 && a <= 0xcfff && S.ram_bank == 0) {
        const uint16_t o = a & 0xfff;
        if (o <= 0x3fe) {
            if ((o & 0x1f) <= 0x0f) S.palette[o & 0x0f] = v;
        } else if (o >= 0x400 && o <= 0x7ff) cmos_w(o & 0xff, v);
        else if (o >= 0xc00) {
            const uint8_t r = o & 0x1f;
            if (r <= 3) pia_write(&S.pia1, r, v);
            else if (r <= 7) pia_write(&S.pia0, r, v);
        }
    }
}
#else
static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x8fff) return S.ram_rom ? S.rom_bank[a >> 12][a & 0xfff] : S.mem[a];
    if (a <= 0xbfff || a >= 0xd000) return S.mem[a];
    if (a >= 0xc800 && a <= 0xc8ff) {
        const uint8_t r = a & 0x0f;
        if (r >= 4 && r <= 7) return pia_read(&S.pia0, r);
        if (r >= 0x0c) return pia_read(&S.pia1, r);
        return 0xff;
    }
    if (a >= 0xcb00 && a <= 0xcbff) return S.scanline < 0x100 ? (uint8_t)(S.scanline & 0xfc) : 0xfc;
    if (a >= 0xcc00 && a <= 0xcfff) return S.nvram[a & 0x3ff];
    return 0xff;
}

static void blitter_w(int reg, uint8_t v);
static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) S.mem[a] = v;
    else if (a <= 0xc3ff) S.palette[a & 0x0f] = v;
    else if (a >= 0xc800 && a <= 0xc8ff) {
        const uint8_t r = a & 0x0f;
        if (r >= 4 && r <= 7) pia_write(&S.pia0, r, v);
        else if (r >= 0x0c) pia_write(&S.pia1, r, v);
    } else if (a >= 0xc900 && a <= 0xc9ff) S.ram_rom = (v & 1) != 0;
    else if (a >= 0xca00 && a <= 0xcaff) blitter_w(a & 7, v);
    else if (a >= 0xcc00 && a <= 0xcfff) cmos_w(a & 0x3ff, v);
}

// the blitter (dsp-cpp's: no PROM remap)
static void blit_pixel(uint16_t dst, uint8_t src)
{
    uint8_t cur = dst < 0xc000 ? S.mem[dst] : main_read(NULL, dst);
    const uint8_t ctrl = S.blit[0];
    uint8_t keep = 0xff;
    if ((ctrl & 0x08) && !(src & 0xf0)) {
        if (ctrl & 0x80) keep &= 0x0f;
    } else if (!(ctrl & 0x80)) keep &= 0x0f;
    if ((ctrl & 0x08) && !(src & 0x0f)) {
        if (ctrl & 0x40) keep &= 0xf0;
    } else if (!(ctrl & 0x40)) keep &= 0xf0;
    const uint8_t out = (ctrl & 0x10) ? S.blit[1] : src;
    cur = (uint8_t)((cur & keep) | (out & ~keep));
    if (dst < 0xc000) S.mem[dst] = cur;
    else main_write(NULL, dst, cur);
}

static void blitter_w(int reg, uint8_t v)
{
    S.blit[reg] = v;
    if (reg) return;
    uint16_t ss = (uint16_t)(S.blit[2] << 8 | S.blit[3]), ds = (uint16_t)(S.blit[4] << 8 | S.blit[5]);
    uint8_t w = S.blit[6] ^ 4, h = S.blit[7] ^ 4;
    if (!w) w = 1;
    if (!h) h = 1;
    const uint8_t ctrl = S.blit[0];
    const uint16_t sxa = (ctrl & 1) ? 0x100 : 1, sya = (ctrl & 1) ? 1 : w;
    const uint16_t dxa = (ctrl & 2) ? 0x100 : 1, dya = (ctrl & 2) ? 1 : w;
    uint16_t pix = 0;
    for (int y = 0; y < h; y++) {
        uint16_t s = ss, d = ds;
        for (int x = 0; x < w; x++) {
            const uint8_t b = main_read(NULL, s);
            if (!(ctrl & 0x20)) blit_pixel(d, b);
            else {
                pix = (uint16_t)(pix << 8 | b);
                blit_pixel(d, (uint8_t)(pix >> 4));
            }
            s = (uint16_t)(s + sxa);
            d = (uint16_t)(d + dxa);
        }
        ds = (ctrl & 2) ? (uint16_t)((ds & 0xff00) | ((ds + dya) & 0xff)) : (uint16_t)(ds + dya);
        ss = (ctrl & 1) ? (uint16_t)((ss & 0xff00) | ((ss + sya) & 0xff)) : (uint16_t)(ss + sya);
    }
}
#endif

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xff || a >= 0xb000) return S.snd_mem[a];
    if ((a >= 0x400 && a <= 0x403) || (a >= 0x8400 && a <= 0x8403)) return pia_read(&S.pia2, a & 3);
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xff) S.snd_mem[a] = v;
    else if ((a >= 0x400 && a <= 0x403) || (a >= 0x8400 && a <= 0x8403)) pia_write(&S.pia2, a & 3, v);
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        if (S.naudio < 1024) S.audio[S.naudio++] = S.dac;
    }
}

static inline void put(int x, int y, uint16_t c)
{
#if COLONY7
    S.fb[(VIS_W - 1 - x) * W + y] = c;
#else
    S.fb[y * W + x] = c;
#endif
}

// a line of the bitmap: two pixels a byte, columns of 256 bytes. The picture
// is shown at line 239 (dsp-cpp's present_frame), its rows 7-246.
static void video_line(int line)
{
#if defined(WILLIAMS_DSP_ROW0) && !COLONY7
    // (dsp-cpp draws line 247 past its 247-line bitmap, over the start of
    // the shown picture: built so only to compare with it)
    if (line == 247)
        for (int x = 0; x < FB_W; x++) {
            const uint8_t pix = S.mem[line + (x >> 1) * 256];
            S.fb[x] = S.pal_lookup[S.palette[(x & 1) ? pix & 0x0f : (pix >> 4) & 0x0f]];
        }
#endif
    if (line < 7 || line >= 7 + VIS_H) return;
    for (int x = 0; x < VIS_W; x++) {
        const int fx = x + XOFF;
        const uint8_t pix = S.mem[line + (fx >> 1) * 256];
        const uint16_t c = S.pal_lookup[S.palette[(fx & 1) ? pix & 0x0f : (pix >> 4) & 0x0f]];
        if (line >= 239) S.late[line - 239][x] = c;
        else put(x, line - 7, c);
    }
}

static void present(void)
{
    for (int r = 0; r < 8; r++)
        for (int x = 0; x < VIS_W; x++) put(x, 232 + r, S.late[r][x]);
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state))) || !(S.fb = (uint16_t *)calloc(W * H, 2))) return false;
    uint8_t *flat = (uint8_t *)calloc(1, 0x10000);
    if (!flat) return false;
    for (size_t i = 0; i < sizeof kPieces / sizeof kPieces[0]; i++) {
        const arc_rom r = {kPieces[i].name, kPieces[i].size, kPieces[i].at, 0};
        if (!arc_load(&r, 1, flat, 0x10000)) return false;
    }
    memcpy(S.mem + 0xd000, flat, 0x3000);
    for (int f = 0; f < (BLITTER ? 9 : 8); f++) memcpy(S.rom_bank[f], flat + 0x3000 + f * 0x1000, 0x1000);
    free(flat);
    const uint32_t snd_len = 0x10000 - SND_BASE;
    const arc_rom sr = {kSound, snd_len, SND_BASE, 0};
    if (!arc_load(&sr, 1, S.snd_mem, 0x10000)) return false;
#ifndef NO_SND_MIRROR
    if (snd_len == 0x800) memcpy(S.snd_mem + 0xf000, S.snd_mem + 0xf800, 0x800);
#endif
    // BBGGGRRR through the resistors (1200, 560, 330 / 560, 330)
    static const float R[3] = {1200.f, 560.f, 330.f}, B[2] = {560.f, 330.f};
    for (int f = 0; f < 256; f++) {
        float c[3];
        for (int k = 0; k < 3; k++) {
            const float *res = k == 2 ? B : R;
            const int n = k == 2 ? 2 : 3, bits = k == 0 ? f & 7 : k == 1 ? (f >> 3) & 7 : (f >> 6) & 3;
            float s = 0, t = 0;
            for (int i = 0; i < n; i++) {
                const float w = 1.f / res[i];
                t += w;
                if (bits & (1 << i)) s += w;
            }
            c[k] = t > 0 ? s / t : 0;
        }
        S.pal_lookup[f] = arc_rgb565((uint32_t)(int)(c[0] * 255.f + 0.5f) << 16 | (uint32_t)(int)(c[1] * 255.f + 0.5f) << 8 |
                                     (uint32_t)(int)(c[2] * 255.f + 0.5f));
    }
    S.main.read = main_read;
    S.main.write = main_write;
    hd63701_init(&S.snd, SOUND_CLOCK, HD_M6800);
    S.snd.read_mem = sound_read;
    S.snd.write_mem = sound_write;
    S.snd.cycles = sound_cycles;
#if JOUST
    S.pia0.rd_a = joust_in0;
    S.pia0.rd_b = zero_r;
    S.pia0.cb2 = joust_cb2;
#else
    S.pia0.rd_a = in0_r;
    S.pia0.rd_b = in1_r;
    (void)joust_in0, (void)zero_r, (void)joust_cb2;
#endif
    S.pia1.rd_a = in2_r;
    S.pia1.wr_b = sound_latch_w;
    S.pia1.irq = update_main_irq;
    S.pia2.wr_a = dac_w;
    S.pia2.irq = update_sound_irq;
    S.dsw_a = COLONY7 ? 1 : 0;
    S.tf_main = (double)MAIN_CLOCK / (SCANLINES * CPU_SYNC) / FPS;
    S.tf_snd = (double)SOUND_CLOCK / (SCANLINES * CPU_SYNC) / FPS;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&S.main);
    hd63701_reset(&S.snd);
    pia_reset(&S.pia0);
    pia_reset(&S.pia1);
    pia_reset(&S.pia2);
    S.dac = 0;
    S.ram_bank = 0;
    S.sound_latch = 0;
    S.scanline = 0;
    S.in0 = S.in1 = S.in2 = S.in3 = 0;
    S.ram_rom = S.player_select = false;
    memset(S.nvram, 0, sizeof S.nvram);   // (a first boot each time)
    S.cmos_burst = 0;
    S.auto_advance = -1;
    S.frame_main = S.tf_main;
    S.frame_snd = S.tf_snd;
    S.audio_acc = 0;
    update_main_irq();
    update_sound_irq();
}

int arc_run_frame(const int16_t **audio)
{
    // the first boot restores the factory settings (a burst of CMOS writes)
    // and waits for the operator's Advance: pressed for the player, a second on
    if (S.auto_advance < 0 && S.cmos_burst >= 100) S.auto_advance = 60;
    S.cmos_burst = 0;
    if (S.auto_advance > 0) S.auto_advance--;
    S.advance = S.service || (S.auto_advance > 0 && S.auto_advance < 10);
    S.naudio = 0;
    for (S.scanline = 0; S.scanline < SCANLINES; S.scanline++) {
        if (S.scanline < 256 && (S.scanline & 0x1f) == 0) pia_cb1(&S.pia1, (S.scanline & 0x20) != 0);
        if (S.scanline == 0) pia_ca1(&S.pia1, false);
        if (S.scanline == 240) pia_ca1(&S.pia1, true);
        if (S.scanline == 239) present();
        for (int h = 0; h < CPU_SYNC; h++) {
            const int mb = S.frame_main + 0.5 < 1 ? 1 : (int)(S.frame_main + 0.5);
            const int sb = S.frame_snd + 0.5 < 1 ? 1 : (int)(S.frame_snd + 0.5);
            const int ran = m6809d_run(&S.main, mb);
            S.frame_main += S.tf_main - ran;
            if (S.frame_main < 0) S.frame_main = S.tf_main;
            update_sound_irq();         // (the 6808's IRQ is the PIA's level)
            S.frame_snd += S.tf_snd - hd63701_run(&S.snd, sb);
            if (S.frame_snd < 0) S.frame_snd = S.tf_snd;
        }
        video_line(S.scanline);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0, b = 0, c = 0, d = 0;
#if DEFENDER || MAYDAY || STARGATE
    if (p1 & AP_B1) a |= 0x01;
    if ((p1 & AP_B2) || (MAYDAY && (p1 & AP_RIGHT))) a |= 0x02;
    if (p1 & AP_B3) a |= 0x04;
    if (p1 & AP_B4) a |= STARGATE ? 0x40 : 0x08;   // hyperspace / (Stargate) reverse
    if (p2 & AP_START) a |= 0x10;
    if (p1 & AP_START) a |= 0x20;
#ifndef WILLIAMS_DSP_INPUTS                 // (dsp-cpp: on a Select button)
    if (!STARGATE && (p1 & (AP_LEFT | AP_RIGHT)) == AP_LEFT) a |= 0x40;   // reverse: the stick left
    if (STARGATE && (p1 & AP_LEFT)) a |= 0x08;                            // (Stargate) hyperspace
#endif
    if (p1 & AP_DOWN) a |= 0x80;
    if (p1 & AP_UP) b |= 0x01;
    if (STARGATE && (p2 & AP_B1)) b |= 0x02;        // inviso
#elif COLONY7
    if (p1 & AP_DOWN) a |= 0x01;
    if (p1 & AP_RIGHT) a |= 0x02;
    if (p1 & AP_LEFT) a |= 0x04;
    if (p1 & AP_UP) a |= 0x08;
    if (p2 & AP_START) a |= 0x10;
    if (p1 & AP_START) a |= 0x20;
    if (p1 & AP_B1) a |= 0x40;
    if (p1 & AP_B2) a |= 0x80;
    if (p1 & AP_B3) b |= 0x01;
#elif JOUST
    if (p2 & AP_START) a |= 0x10;
    if (p1 & AP_START) a |= 0x20;
    if (p1 & AP_LEFT) b |= 0x01;
    if (p1 & AP_RIGHT) b |= 0x02;
    if (p1 & AP_B1) b |= 0x04;
    if (p2 & AP_LEFT) d |= 0x01;
    if (p2 & AP_RIGHT) d |= 0x02;
    if (p2 & AP_B1) d |= 0x04;
#else
    // Robotron: the left stick moves, the right one fires; with one stick,
    // the buttons fire (B1 up, B2 down, B3 left, B4 right)
    if (p1 & AP_UP) a |= 0x01;
    if (p1 & AP_DOWN) a |= 0x02;
    if (p1 & AP_LEFT) a |= 0x04;
    if (p1 & AP_RIGHT) a |= 0x08;
    if (p1 & AP_START) a |= 0x10;
    if (p2 & AP_START) a |= 0x20;
#ifdef WILLIAMS_DSP_INPUTS
    const unsigned f = p2;
#else
    const unsigned f = p2 | (p1 & AP_B1 ? AP_UP : 0) | (p1 & AP_B2 ? AP_DOWN : 0) | (p1 & AP_B3 ? AP_LEFT : 0) | (p1 & AP_B4 ? AP_RIGHT : 0);
#endif
    if (f & AP_UP) a |= 0x40;
    if (f & AP_DOWN) a |= 0x80;
    if (f & AP_LEFT) b |= 0x01;
    if (f & AP_RIGHT) b |= 0x02;
#endif
    if (in->coin1) c |= 0x10;
    if (in->coin2) c |= 0x20;
    S.in0 = a, S.in1 = b, S.in2 = c, S.in3 = d;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
}
