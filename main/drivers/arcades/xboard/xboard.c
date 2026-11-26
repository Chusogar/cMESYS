// xboard.c - Sega X-Board: After Burner II (1987) for the common arcade
// frontends (arc.h). C port of dsp-cpp's drivers/arcade/xboard (after MAME's
// segaxbd): two 68000s at 12.5 MHz sharing the road, the 315-5248 multiplier,
// 315-5249 divider and 315-5250 compare / timer chips, two CXD1095 I/O chips
// with an ADC0804 for the stick and the throttle, the System 16B tilemaps and
// text, the Out Run sprites and road, and a Z80 with a YM2151 and the Sega
// PCM 315-5218.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/segapcmd.h"
#include "../../../video/sega16d.h"

#define W 320
#define H 224
#define SCANLINES 262
#define MAIN_CLOCK (50000000 / 4)
#define SOUND_CLOCK (16000000 / 4)
#define FPS (50000000.0 / 8.0 / (400.0 * 262.0))
#define RATE 44100
#define NPAL 0x2000
#define N(k) ((int)(sizeof k / sizeof k[0]))

#if ARC_VARIANT == 1                            // (After Burner, MAME's aburner: not in dsp-cpp, the same board)
#define ABURNER1 1
static const char *const kSets[] = { "aburner", NULL };
const arc_info arc_game = { "aburner", "After Burner", "Sega", 1987, W, H, FPS, RATE, kSets,
                            { "VULCAN", "MISIL", "ACELERAR", "FRENAR" }, ARC_JOY_8WAY };
#else
#define ABURNER1 0
static const char *const kSets[] = { "aburner2", NULL };
const arc_info arc_game = { "aburner2", "After Burner II", "Sega", 1987, W, H, FPS, RATE, kSets,
                            { "VULCAN", "MISIL", "ACELERAR", "FRENAR" }, ARC_JOY_8WAY };
#endif

#if ABURNER1
static const arc_rom kMainRom[] = {{"epr-10940.58", 0x20000, 0x00000, 0}, {"epr-10941.63", 0x20000, 0x00001, 0}};
static const arc_rom kSubRom[] = {{"epr-10927.20", 0x20000, 0x00000, 0}, {"epr-10928.29", 0x20000, 0x00001, 0}};
static const arc_rom kTileRom[] = {
    {"epr-10926.154", 0x10000, 0x00000, 0}, {"epr-10925.153", 0x10000, 0x10000, 0}, {"epr-10924.152", 0x10000, 0x20000, 0},
};
static const arc_rom kSpriteRom[] = {
    {"mpr-10932.90", 0x20000, 0x000000, 0}, {"mpr-10934.94", 0x20000, 0x000001, 0},
    {"mpr-10936.98", 0x20000, 0x000002, 0}, {"mpr-10938.102", 0x20000, 0x000003, 0},
    {"mpr-10933.91", 0x20000, 0x080000, 0}, {"mpr-10935.95", 0x20000, 0x080001, 0},
    {"mpr-10937.99", 0x20000, 0x080002, 0}, {"mpr-10939.103", 0x20000, 0x080003, 0},
    {"epr-10942.92", 0x20000, 0x100000, 0}, {"epr-10943.96", 0x20000, 0x100001, 0},
    {"epr-10944.100", 0x20000, 0x100002, 0}, {"epr-10945.104", 0x20000, 0x100003, 0},
    {"epr-10946.93", 0x20000, 0x180000, 0}, {"epr-10947.97", 0x20000, 0x180001, 0},
    {"epr-10948.101", 0x20000, 0x180002, 0}, {"epr-10949.105", 0x20000, 0x180003, 0},
};
static const arc_rom kRoadRom[] = {{"epr-10922.40", 0x10000, 0, 0}};
static const arc_rom kSoundRom[] = {{"epr-10923.17", 0x10000, 0, 0}};
static const arc_rom kPcmRom[] = {
    {"mpr-10931.11", 0x20000, 0x00000, 0}, {"mpr-10930.12", 0x20000, 0x20000, 0}, {"epr-10929.13", 0x20000, 0x40000, 0},
};
#else
// After Burner II (VER 2.00), MAME's aburner2.
static const arc_rom kMainRom[] = {
    {"epr-11107.58", 0x20000, 0x00000, 0x6d87bab7},
    {"epr-11108.63", 0x20000, 0x00001, 0x202a3e1d},
};
static const arc_rom kSubRom[] = {
    {"epr-11109.20", 0x20000, 0x00000, 0x85a0fe07},
    {"epr-11110.29", 0x20000, 0x00001, 0xf3d6797c},
};
static const arc_rom kTileRom[] = {
    {"epr-11115.154", 0x10000, 0x00000, 0xe8e32921},
    {"epr-11114.153", 0x10000, 0x10000, 0x2e97f633},
    {"epr-11113.152", 0x10000, 0x20000, 0x36058c8c},
};
static const arc_rom kSpriteRom[] = {
    {"mpr-10932.90", 0x20000, 0x000000, 0xcc0821d6},
    {"mpr-10934.94", 0x20000, 0x000001, 0x4a51b1fa},
    {"mpr-10936.98", 0x20000, 0x000002, 0xada70d64},
    {"mpr-10938.102", 0x20000, 0x000003, 0xe7675baf},
    {"mpr-10933.91", 0x20000, 0x080000, 0xc8efb2c3},
    {"mpr-10935.95", 0x20000, 0x080001, 0xc1e23521},
    {"mpr-10937.99", 0x20000, 0x080002, 0xf0199658},
    {"mpr-10939.103", 0x20000, 0x080003, 0xa0d49480},
    {"epr-11103.92", 0x20000, 0x100000, 0xbdd60da2},
    {"epr-11104.96", 0x20000, 0x100001, 0x06a35fce},
    {"epr-11105.100", 0x20000, 0x100002, 0x027b0689},
    {"epr-11106.104", 0x20000, 0x100003, 0x9e1fec09},
    {"epr-11116.93", 0x20000, 0x180000, 0x49b4c1ba},
    {"epr-11117.97", 0x20000, 0x180001, 0x821fbb71},
    {"epr-11118.101", 0x20000, 0x180002, 0x8f38540b},
    {"epr-11119.105", 0x20000, 0x180003, 0xd0343a8e},
};
static const arc_rom kRoadRom[] = {{"epr-10922.40", 0x10000, 0, 0xb49183d4}};
static const arc_rom kSoundRom[] = {{"epr-11112.17", 0x10000, 0, 0xd777fc6d}};
static const arc_rom kPcmRom[] = {
    {"mpr-10931.11", 0x20000, 0x00000, 0x9209068f},
    {"mpr-10930.12", 0x20000, 0x20000, 0x6493368b},
    {"epr-11102.13", 0x20000, 0x40000, 0x6c07c78d},
};

#endif

#define ROAD_BASE1 0x1700                       // (segaic16_road_init, ROAD_XBOARD)
#define ROAD_BASE2 0x1720
#define ROAD_BASE3 0x1780
#define ROAD_XOFF (-166)
#define TILE_BASE 0x1c00
#define SPRITE_XORIGIN 190

typedef struct { uint16_t regs[16], counter; int bit; bool exck, irq, zint; } ctimer;   // 315-5250
typedef struct { uint16_t regs[2]; } mult;      // 315-5248
typedef struct { uint16_t regs[8]; } divider;   // 315-5249
typedef struct { uint8_t latch[5], dir[5]; } iochip;   // CXD1095

static struct state {
    m68kd main, sub;
    zd_z80 snd;
    ym2151 ym;
    segapcmd pcm;
    uint16_t *main_rom, *sub_rom;
    uint32_t main_words, sub_words;
    uint8_t sound_rom[0x10000], *pcm_rom, *road_gfx, *tiles;
    uint32_t *sprite_rom, sprite_dwords;
    uint16_t backup1[0x2000], backup2[0x2000], subram0[0x2000], subram1[0x2000];
    uint16_t tile_ram[0x8000], text_ram[0x800], sprite_ram[0x800], sprite_buffer[0x800];
    uint16_t palette_ram[0x2000], road_ram[0x800], road_buffer[0x800];
    uint8_t sound_ram[0x800];
    mult mult_main, mult_sub;
    divider div_main, div_sub;
    ctimer timer_main, timer_sub;
    iochip io[2];
    uint32_t palette[NPAL * 2];
    uint8_t pal_normal[32], pal_shadow[32], pal_hilight[32];
    uint16_t latched_pages[4], latched_xscroll[4], latched_yscroll[4];
    uint16_t bitmap[W * H], sprite_bitmap[W * H], fb[W * H];
    uint8_t priority[W * H];
    uint8_t road_control, pc0, adc_latch, io1_porta, dsw_a, dsw_b;
    bool display_enable, sound_reset, sound_mute, vblank_irq;
    int stick_x, stick_y, throttle;
    double main_debt, sub_debt, sound_debt;
    int64_t pcm_acc, audio_acc, pcm_sum;
    int pcm_count;
    int32_t pcm_last;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

static inline uint16_t combine(uint16_t old, uint16_t data, uint16_t mask) { return (uint16_t)((old & ~mask) | (data & mask)); }
static int ramp(int value, int target, int delta)
{
    if (value < target) return value + delta < target ? value + delta : target;
    return value - delta > target ? value - delta : target;
}

// ---------------------------------------------------------------------------
// 315-5248 / 5249 / 5250 (MAME segaic16_m)

static uint16_t mult_read(const mult *m, int o)
{
    switch (o & 3) {
    case 0: return m->regs[0];
    case 1: return m->regs[1];
    case 2: return (uint16_t)(((int32_t)(int16_t)m->regs[0] * (int16_t)m->regs[1]) >> 16);
    default: return (uint16_t)(((int32_t)(int16_t)m->regs[0] * (int16_t)m->regs[1]) & 0xffff);
    }
}
static void mult_write(mult *m, int o, uint16_t d, uint16_t mask) { m->regs[o & 1] = combine(m->regs[o & 1], d, mask); }

static uint16_t div_read(const divider *dv, int o)
{
    switch (o & 7) {
    case 0: case 1: case 2: case 4: case 5: case 6: return dv->regs[o & 7];
    default: return 0xffff;
    }
}

static void div_execute(divider *dv, int mode)
{
    dv->regs[6] = 0;
    if (mode == 0) {
        const int32_t dividend = (int32_t)(((uint32_t)dv->regs[0] << 16) | dv->regs[1]);
        const int32_t divisor = (int16_t)dv->regs[2];
        int64_t q;
        if (divisor == 0) {
            q = dividend;
            dv->regs[6] |= 0x4000;
        } else q = (int64_t)dividend / divisor;
        if (q < -32768) {
            q = -32768;
            dv->regs[6] |= 0x8000;
        } else if (q > 32767) {
            q = 32767;
            dv->regs[6] |= 0x8000;
        }
        dv->regs[4] = (uint16_t)(int16_t)q;
        dv->regs[5] = (uint16_t)(int16_t)((int64_t)dividend - q * divisor);
    } else {
        const uint32_t dividend = ((uint32_t)dv->regs[0] << 16) | dv->regs[1], divisor = dv->regs[2];
        uint32_t q;
        if (divisor == 0) {
            q = dividend;
            dv->regs[6] |= 0x4000;
        } else q = dividend / divisor;
        dv->regs[4] = (uint16_t)(q >> 16);
        dv->regs[5] = (uint16_t)q;
    }
}

static void div_write(divider *dv, int o, uint16_t d, uint16_t mask)
{
    switch (o & 3) {
    case 0: case 1: case 2: dv->regs[o & 3] = combine(dv->regs[o & 3], d, mask); break;
    default: break;
    }
    if (o & 8) div_execute(dv, o & 4);
}

static void ct_reset(ctimer *t)
{
    memset(t->regs, 0, sizeof t->regs);
    t->counter = 0;
    t->bit = 0;
    t->irq = t->zint = false;
}

static void ct_execute(ctimer *t, bool history)
{
    const int16_t b1 = (int16_t)t->regs[0], b2 = (int16_t)t->regs[1], value = (int16_t)t->regs[2];
    const int16_t lo = b1 < b2 ? b1 : b2, hi = b1 > b2 ? b1 : b2;
    if (value < lo) {
        t->regs[7] = (uint16_t)lo;
        t->regs[3] = 0x8000;
    } else if (value > hi) {
        t->regs[7] = (uint16_t)hi;
        t->regs[3] = 0x4000;
    } else {
        t->regs[7] = (uint16_t)value;
        t->regs[3] = 0;
    }
    if (history) {
        if (t->bit < 16) t->regs[4] = (uint16_t)(t->regs[4] | ((t->regs[3] == 0 ? 1u : 0u) << t->bit));
        t->bit++;
    }
}

static uint16_t ct_read(ctimer *t, int o)
{
    switch (o & 15) {
    case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: return t->regs[o & 15];
    case 0x5: return t->regs[1];
    case 0x6: return t->regs[2];
    case 0x7: return t->regs[7];
    case 0x9: case 0xd: t->irq = false; break;
    default: break;
    }
    return 0xffff;
}

static void ct_write(ctimer *t, int o, uint16_t d, uint16_t mask)
{
    switch (o & 15) {
    case 0x0: t->regs[0] = combine(t->regs[0], d, mask); ct_execute(t, false); break;
    case 0x1: t->regs[1] = combine(t->regs[1], d, mask); ct_execute(t, false); break;
    case 0x2: t->regs[2] = combine(t->regs[2], d, mask); ct_execute(t, true); break;
    case 0x4: t->regs[4] = 0; t->bit = 0; break;
    case 0x6: t->regs[2] = combine(t->regs[2], d, mask); ct_execute(t, false); break;
    case 0x8: case 0xc: t->regs[8] = combine(t->regs[8], d, mask); break;
    case 0x9: case 0xd: t->irq = false; break;
    case 0xa: case 0xe: t->regs[10] = combine(t->regs[10], d, mask); break;
    case 0xb: case 0xf:                         // (the sound latch: an NMI until the Z80 reads port 0x40)
        t->regs[11] = d & 0xff;
        t->zint = true;
        break;
    default: break;
    }
}

static void ct_clock(ctimer *t, bool state)
{
    if (t->exck == state) return;
    t->exck = state;
    if (!t->exck) return;
    const uint16_t old = t->counter;
    if (t->regs[10] & 1) t->counter = (uint16_t)((t->counter + 1) & 0xffff);
    if (old == 0xfff) {
        t->irq = true;
        t->counter = t->regs[8] & 0xfff;
    }
}

// ---------------------------------------------------------------------------

static void palette_write(int index, uint16_t value)
{
    S.palette_ram[index] = value;
    const int r = ((value >> 12) & 1) | ((value << 1) & 0x1e);
    const int g = ((value >> 13) & 1) | ((value >> 3) & 0x1e);
    const int b = ((value >> 14) & 1) | ((value >> 7) & 0x1e);
    S.palette[index] = s16_argb(S.pal_normal[r], S.pal_normal[g], S.pal_normal[b]);
    const uint8_t *fx = (value & 0x8000) ? S.pal_hilight : S.pal_shadow;
    S.palette[index + NPAL] = s16_argb(fx[r], fx[g], fx[b]);
}

static uint8_t adc_value(int channel)
{
    switch (channel & 7) {
    case 0: return (uint8_t)S.stick_x;
    case 1: return (uint8_t)S.stick_y;
    case 2: return (uint8_t)S.throttle;
    case 3: case 4: return (uint8_t)((0xb0 + 0x50) / 2);   // (the motor: upright, centred)
    default: return 0x10;
    }
}

static uint8_t iochip_input(int chip, int port)
{
    if (chip == 0) return port == 0 ? 0xbf : 0xff;   // (aburner2_motor_r: ADC done, the limit switches open)
    switch (port) {
    case 0: return S.io1_porta;
    case 2: return S.dsw_a;
    case 3: return S.dsw_b;
    default: return 0xff;
    }
}

static void iochip_output(int chip, int port, uint8_t v)
{
    if (chip != 0) return;
    if (port == 2) {                            // (D6 watchdog, D5 display, D4-D2 ADC select, D0 sound reset)
        S.pc0 = v;
        S.display_enable = (v & 0x20) != 0;
        const bool reset = (v & 0x01) == 0;
        if (reset && !S.sound_reset) zd_reset(&S.snd);
        S.sound_reset = reset;
    } else if (port == 3) S.sound_mute = (v & 0x80) == 0;
}

static uint8_t iochip_read(int chip, int o)
{
    iochip *io = &S.io[chip];
    if (o < 5) {
        uint8_t mask = io->dir[o];
        if (o == 4) mask &= 0x0f;
        const uint8_t in = mask ? (uint8_t)(iochip_input(chip, o) & mask) : 0;
        return (uint8_t)(in | (io->latch[o] & ~io->dir[o]));
    }
    return 0;
}

static void iochip_write(int chip, int o, uint8_t v)
{
    iochip *io = &S.io[chip];
    if (o < 5) {
        if (o == 4) v &= 0x0f;
        io->latch[o] = v;
        iochip_output(chip, o, (uint8_t)(v & ~io->dir[o]));
    } else if (o == 6) {
        uint8_t d = v;
        for (int port = 0; port < 4; port++) {
            io->dir[port] = (uint8_t)(((d & 1) ? 0x0f : 0) | ((d & 2) ? 0xf0 : 0));
            d >>= 2;
        }
    } else if (o == 7) io->dir[4] = (uint8_t)((v & 0x0f) | 0xf0);
}

static void update_main_irqs(void)              // (the timer and VBLANK lines share the IPL pins: both give 6)
{
    int irq = 0;
    if (S.timer_main.irq) irq |= 2;
    else m68kd_set_irq(&S.main, 2, M68_CLEAR);
    if (S.vblank_irq) irq |= 4;
    else m68kd_set_irq(&S.main, 4, M68_CLEAR);
    if (irq != 6) m68kd_set_irq(&S.main, 6, M68_CLEAR);
    if (irq) m68kd_set_irq(&S.main, irq, M68_ASSERT);
}

static void road_swap(void)
{
    uint16_t t[0x800];
    memcpy(t, S.road_ram, sizeof t);
    memcpy(S.road_ram, S.road_buffer, sizeof t);
    memcpy(S.road_buffer, t, sizeof t);
}

static void sprite_swap(void)
{
    uint16_t t[0x800];
    memcpy(t, S.sprite_ram, sizeof t);
    memcpy(S.sprite_ram, S.sprite_buffer, sizeof t);
    memcpy(S.sprite_buffer, t, sizeof t);
    S.sprite_ram[0] = 0xffff;                   // (MAME: the hack for Thunder Blade)
}

// ---------------------------------------------------------------------------
// The main 68000 (segaxbd main_map)

static uint16_t main_read(uint32_t a)
{
    a &= 0x3fffff;
    if (a < 0x080000) return S.main_rom[(a >> 1) % S.main_words];
    if (a < 0x0a0000) return S.backup1[(a & 0x3fff) >> 1];
    if (a < 0x0c0000) return S.backup2[(a & 0x3fff) >> 1];
    if (a < 0x0d0000) return S.tile_ram[(a & 0xffff) >> 1];
    if (a < 0x0e0000) return S.text_ram[(a & 0xfff) >> 1];
    if (a < 0x0e4000) return mult_read(&S.mult_main, (int)((a >> 1) & 3));
    if (a < 0x0e8000) return div_read(&S.div_main, (int)((a >> 1) & 0xf));
    if (a < 0x0ec000) {
        const uint16_t v = ct_read(&S.timer_main, (int)((a >> 1) & 0xf));
        update_main_irqs();
        return v;
    }
    if (a < 0x100000) return 0xffff;
    if (a < 0x110000) return S.sprite_ram[(a & 0xfff) >> 1];
    if (a < 0x120000) return 0xffff;
    if (a < 0x130000) return S.palette_ram[(a & 0x3fff) >> 1];
    if (a < 0x140000) return (uint16_t)(0xff00 | S.adc_latch);
    if (a < 0x150000) return (uint16_t)(0xff00 | iochip_read(0, (int)((a >> 1) & 7)));
    if (a < 0x160000) return (uint16_t)(0xff00 | iochip_read(1, (int)((a >> 1) & 7)));
    if (a < 0x200000) return 0xffff;
    if (a < 0x280000) return S.sub_rom[((a - 0x200000) >> 1) % S.sub_words];
    if (a < 0x2a0000) return S.subram0[(a & 0x3fff) >> 1];
    if (a < 0x2c0000) return S.subram1[(a & 0x3fff) >> 1];
    if (a < 0x2e0000) return 0xffff;
    if (a < 0x2e4000) return mult_read(&S.mult_sub, (int)((a >> 1) & 3));
    if (a < 0x2e8000) return div_read(&S.div_sub, (int)((a >> 1) & 0xf));
    if (a < 0x2ec000) return ct_read(&S.timer_sub, (int)((a >> 1) & 7));
    if (a < 0x2ee000) return S.road_ram[(a & 0xfff) >> 1];
    if (a < 0x2f0000) {
        road_swap();
        return 0xffff;
    }
    if (a >= 0x3f8000 && a < 0x3fc000) return S.backup1[(a & 0x3fff) >> 1];
    if (a >= 0x3fc000) return S.backup2[(a & 0x3fff) >> 1];
    return 0xffff;
}

#define STORE(cell) do { cell = combine(cell, v, mask); return; } while (0)

static void main_write(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0x3fffff;
    if (a < 0x080000) return;
    if (a < 0x0a0000) STORE(S.backup1[(a & 0x3fff) >> 1]);
    if (a < 0x0c0000) STORE(S.backup2[(a & 0x3fff) >> 1]);
    if (a < 0x0d0000) STORE(S.tile_ram[(a & 0xffff) >> 1]);
    if (a < 0x0e0000) STORE(S.text_ram[(a & 0xfff) >> 1]);
    if (a < 0x0e4000) { mult_write(&S.mult_main, (int)((a >> 1) & 3), v, mask); return; }
    if (a < 0x0e8000) { div_write(&S.div_main, (int)((a >> 1) & 0xf), v, mask); return; }
    if (a < 0x0ec000) {
        const int reg = (int)((a >> 1) & 0xf);
        ct_write(&S.timer_main, reg, v, mask);
        if ((reg & 0xb) == 0xb) zd_set_nmi(&S.snd, ZD_ASSERT);
        update_main_irqs();
        return;
    }
    if (a < 0x100000) return;
    if (a < 0x110000) STORE(S.sprite_ram[(a & 0xfff) >> 1]);
    if (a < 0x120000) { sprite_swap(); return; }
    if (a < 0x130000) {
        const int i = (int)((a & 0x3fff) >> 1);
        palette_write(i, combine(S.palette_ram[i], v, mask));
        return;
    }
    if (a < 0x140000) {                         // (the ADC0804: converts the channel port C D4-D2 selects)
        S.adc_latch = adc_value((S.pc0 >> 2) & 7);
        return;
    }
    if (a < 0x150000) { if (mask & 0x00ff) iochip_write(0, (int)((a >> 1) & 7), (uint8_t)v); return; }
    if (a < 0x160000) { if (mask & 0x00ff) iochip_write(1, (int)((a >> 1) & 7), (uint8_t)v); return; }
    if (a < 0x280000) return;
    if (a < 0x2a0000) STORE(S.subram0[(a & 0x3fff) >> 1]);
    if (a < 0x2c0000) STORE(S.subram1[(a & 0x3fff) >> 1]);
    if (a < 0x2e0000) return;
    if (a < 0x2e4000) { mult_write(&S.mult_sub, (int)((a >> 1) & 3), v, mask); return; }
    if (a < 0x2e8000) { div_write(&S.div_sub, (int)((a >> 1) & 0xf), v, mask); return; }
    if (a < 0x2ec000) { ct_write(&S.timer_sub, (int)((a >> 1) & 7), v, mask); return; }
    if (a < 0x2ee000) STORE(S.road_ram[(a & 0xfff) >> 1]);
    if (a < 0x2f0000) {
        if (mask & 0x00ff) S.road_control = v & 7;
        return;
    }
    if (a >= 0x3f8000 && a < 0x3fc000) STORE(S.backup1[(a & 0x3fff) >> 1]);
    if (a >= 0x3fc000) STORE(S.backup2[(a & 0x3fff) >> 1]);
}

// the sub 68000 (sub_map)
static uint16_t sub_read(uint32_t a)
{
    a &= 0xfffff;
    if (a < 0x080000) return S.sub_rom[(a >> 1) % S.sub_words];
    if (a < 0x0a0000) return S.subram0[(a & 0x3fff) >> 1];
    if (a < 0x0c0000) return S.subram1[(a & 0x3fff) >> 1];
    if (a < 0x0e0000) return 0xffff;
    if (a < 0x0e4000) return mult_read(&S.mult_sub, (int)((a >> 1) & 3));
    if (a < 0x0e8000) return div_read(&S.div_sub, (int)((a >> 1) & 0xf));
    if (a < 0x0ec000) return ct_read(&S.timer_sub, (int)((a >> 1) & 7));
    if (a < 0x0ee000) return S.road_ram[(a & 0xfff) >> 1];
    if (a < 0x0f0000) road_swap();
    return 0xffff;
}

static void sub_write(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xfffff;
    if (a < 0x080000) return;
    if (a < 0x0a0000) STORE(S.subram0[(a & 0x3fff) >> 1]);
    if (a < 0x0c0000) STORE(S.subram1[(a & 0x3fff) >> 1]);
    if (a < 0x0e0000) return;
    if (a < 0x0e4000) { mult_write(&S.mult_sub, (int)((a >> 1) & 3), v, mask); return; }
    if (a < 0x0e8000) { div_write(&S.div_sub, (int)((a >> 1) & 0xf), v, mask); return; }
    if (a < 0x0ec000) { ct_write(&S.timer_sub, (int)((a >> 1) & 7), v, mask); return; }
    if (a < 0x0ee000) STORE(S.road_ram[(a & 0xfff) >> 1]);
    if (a < 0x0f0000 && (mask & 0x00ff)) S.road_control = v & 7;
}
#undef STORE

static uint16_t m_rw(void *u, uint32_t a) { (void)u; return main_read(a); }
static void m_ww(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v, 0xffff); }
static uint8_t m_rb(void *u, uint32_t a) { (void)u; const uint16_t w = main_read(a & ~1u); return (uint8_t)((a & 1) ? w : (w >> 8)); }
static void m_wb(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    if (a & 1) main_write(a & ~1u, v, 0x00ff);
    else main_write(a & ~1u, (uint16_t)(v << 8), 0xff00);
}
static void m_reset_instr(void *u) { (void)u; m68kd_set_reset_line(&S.sub, M68_PULSE); }   // (RESET resets the sub CPU)
static uint16_t s_rw(void *u, uint32_t a) { (void)u; return sub_read(a); }
static void s_ww(void *u, uint32_t a, uint16_t v) { (void)u; sub_write(a, v, 0xffff); }
static uint8_t s_rb(void *u, uint32_t a) { (void)u; const uint16_t w = sub_read(a & ~1u); return (uint8_t)((a & 1) ? w : (w >> 8)); }
static void s_wb(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    if (a & 1) sub_write(a & ~1u, v, 0x00ff);
    else sub_write(a & ~1u, (uint16_t)(v << 8), 0xff00);
}

// ---------------------------------------------------------------------------
// Sound (sound_map / sound_portmap)

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0xf000) return S.sound_rom[a];
    if (a < 0xf800) return segapcmd_read(&S.pcm, a);
    return S.sound_ram[a & 0x7ff];
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0xf000) return;
    if (a < 0xf800) segapcmd_write(&S.pcm, a, v);
    else S.sound_ram[a & 0x7ff] = v;
}

static uint8_t sound_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p < 0x40) return (p & 1) ? ym2151_status(&S.ym) : 0xff;
    if (p < 0x80) {
        S.timer_main.zint = false;
        zd_set_nmi(&S.snd, ZD_CLEAR);
        return (uint8_t)S.timer_main.regs[11];
    }
    return 0xff;
}

static void sound_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p < 0x40) {
        if ((p & 1) == 0) ym2151_select(&S.ym, v);
        else ym2151_write(&S.ym, v);
    }
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&S.ym, cycles);
    S.pcm_acc += (int64_t)cycles * segapcmd_tick_rate(&S.pcm);
    while (S.pcm_acc >= SOUND_CLOCK) {
        S.pcm_acc -= SOUND_CLOCK;
        segapcmd_clock(&S.pcm);
        S.pcm_sum += segapcmd_last_sample(&S.pcm);
        S.pcm_count++;
    }
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        if (S.pcm_count > 0) {
            S.pcm_last = (int32_t)(S.pcm_sum / S.pcm_count);
            S.pcm_sum = 0;
            S.pcm_count = 0;
        }
        double mix = (double)ym2151_update(&S.ym) * 0.15 + (double)S.pcm_last * 0.35;   // (MAME: 0.15 / 0.35)
        if (S.sound_mute) mix = 0;
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16((int32_t)lround(mix * 3.5));
    }
}

static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.snd, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

// ---------------------------------------------------------------------------
// Video (segaxbd screen_update)

static void latch_tilemaps(void)
{
    for (int i = 0; i < 4; i++) {
        S.latched_pages[i] = S.text_ram[0xe80 / 2 + i];
        S.latched_yscroll[i] = S.text_ram[0xe90 / 2 + i];
        S.latched_xscroll[i] = S.text_ram[0xe98 / 2 + i];
    }
}

static void draw_tile_layer(int which, int category, uint8_t mark)
{
    const uint16_t xscroll = S.latched_xscroll[which], yscroll = S.latched_yscroll[which], pages = S.latched_pages[which];
    const bool colscroll = (yscroll & 0x8000) != 0;
    for (int y = 0; y < H; y++) {
        const uint16_t rowscroll = S.text_ram[0xf80 / 2 + 0x20 * which + y / 8];
        for (int x0 = colscroll ? -8 : 0; x0 < W; x0 += colscroll ? 16 : W) {
            uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
            uint16_t effy = colscroll ? S.text_ram[0xf16 / 2 + 0x20 * which + (x0 + 8) / 16] : yscroll;
            uint16_t effpages = pages;
            if (rowscroll & 0x8000) {
                effx = S.latched_xscroll[which + 2];
                effy = S.latched_yscroll[which + 2];
                effpages = S.latched_pages[which + 2];
            }
            effx = (uint16_t)((0xc0 - effx) & 0x3ff);
            effy &= 0x1ff;
            const int vy = (y + effy) & 0x1ff;
            const int xs = x0 > 0 ? x0 : 0;
            const int xe0 = colscroll ? x0 + 16 : W, xe = xe0 < W ? xe0 : W;
            uint16_t *dest = &S.bitmap[y * W];
            uint8_t *pri = &S.priority[y * W];
            for (int x = xs; x < xe; x++) {
                const int vx = (x + effx) & 0x3ff;
                const int page = (effpages >> (((vy >> 8) << 3) | ((vx >> 9) << 2))) & 0xf;
                const uint16_t data = S.tile_ram[page * 0x800 + ((vy & 0xff) >> 3) * 64 + ((vx & 0x1ff) >> 3)];
                if ((int)(data >> 15) != category) continue;
                const uint8_t pen = S.tiles[(size_t)(data & 0x1fff) * 64 + (vy & 7) * 8 + (vx & 7)];
                if (pen == 0) continue;
                dest[x] = (uint16_t)(TILE_BASE + ((data >> 6) & 0x7f) * 8 + pen);
                pri[x] |= mark;
            }
        }
    }
}

static void draw_text_layer(int category, uint8_t mark)
{
    for (int y = 0; y < H; y++) {
        uint16_t *dest = &S.bitmap[y * W];
        uint8_t *pri = &S.priority[y * W];
        for (int x = 0; x < W; x++) {
            const int tx = x + 192;
            const uint16_t data = S.text_ram[(y >> 3) * 64 + (tx >> 3)];
            if ((int)(data >> 15) != category) continue;
            const uint8_t pen = S.tiles[(size_t)(data & 0x1ff) * 64 + (y & 7) * 8 + (tx & 7)];
            if (pen == 0) continue;
            dest[x] = (uint16_t)(TILE_BASE + ((data >> 9) & 7) * 8 + pen);
            pri[x] |= mark;
        }
    }
}

static void draw_road(bool background)          // (segaic16_road_outrun_draw, the X-Board parameters)
{
    static const uint8_t kPriorityMap[2][8] = {{0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0x00}, {0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80}};
    const uint16_t *roadram = S.road_buffer;
    const uint8_t *gfx = S.road_gfx;
    for (int y = 0; y < H; y++) {
        uint16_t *dest = &S.bitmap[y * W];
        const uint16_t data0 = roadram[0x000 + y], data1 = roadram[0x100 + y];
        if (background) {
            int color = -1;
            switch (S.road_control & 3) {
            case 0: if (data0 & 0x800) color = data0 & 0x7f; break;
            case 1:
                if (data0 & 0x800) color = data0 & 0x7f;
                else if (data1 & 0x800) color = data1 & 0x7f;
                break;
            case 2:
                if (data1 & 0x800) color = data1 & 0x7f;
                else if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 3: if (data1 & 0x800) color = data1 & 0x7f; break;
            }
            if (color != -1)
                for (int x = 0; x < W; x++) dest[x] = (uint16_t)(color | ROAD_BASE3);
            continue;
        }
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        const uint8_t control = S.road_control & 3;
        const bool per_line = (S.road_control & 4) != 0;
        const uint8_t *src0 = (data0 & 0x800) ? gfx + 256 * 2 * 512 : gfx + (0x000 + ((data0 >> 1) & 0xff)) * 512;
        int hpos0 = roadram[0x200 + (per_line ? y : (data0 & 0x1ff))] & 0xfff;
        const uint16_t color0 = roadram[0x600 + (per_line ? y : (data0 & 0x1ff))];
        const uint8_t *src1 = (data1 & 0x800) ? gfx + 256 * 2 * 512 : gfx + (0x100 + ((data1 >> 1) & 0xff)) * 512;
        int hpos1 = roadram[0x400 + (per_line ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        const uint16_t color1 = roadram[0x600 + (per_line ? (0x100 + y) : (data1 & 0x1ff))];
        uint16_t table[32] = {0};
        table[0x00] = (uint16_t)(ROAD_BASE1 ^ 0x00 ^ ((color0 >> 0) & 1));
        table[0x01] = (uint16_t)(ROAD_BASE1 ^ 0x02 ^ ((color0 >> 1) & 1));
        table[0x02] = (uint16_t)(ROAD_BASE1 ^ 0x04 ^ ((color0 >> 2) & 1));
        table[0x03] = (data0 & 0x200) ? table[0x00] : (uint16_t)(ROAD_BASE2 ^ 0x00 ^ ((color0 >> 8) & 0xf));
        table[0x07] = (uint16_t)(ROAD_BASE1 ^ 0x06 ^ ((color0 >> 3) & 1));
        table[0x10] = (uint16_t)(ROAD_BASE1 ^ 0x08 ^ ((color1 >> 4) & 1));
        table[0x11] = (uint16_t)(ROAD_BASE1 ^ 0x0a ^ ((color1 >> 5) & 1));
        table[0x12] = (uint16_t)(ROAD_BASE1 ^ 0x0c ^ ((color1 >> 6) & 1));
        table[0x13] = (data1 & 0x200) ? table[0x10] : (uint16_t)(ROAD_BASE2 ^ 0x10 ^ ((color1 >> 8) & 0xf));
        table[0x17] = (uint16_t)(ROAD_BASE1 ^ 0x0e ^ ((color1 >> 7) & 1));
        const int shift = 0x5f8 + ROAD_XOFF;
        hpos0 = (hpos0 - shift) & 0xfff;
        hpos1 = (hpos1 - shift) & 0xfff;
        switch (control) {
        case 0:
            if (data0 & 0x800) break;
            for (int x = 0; x < W; x++) {
                dest[x] = table[(hpos0 < 0x200) ? src0[hpos0] : 3];
                hpos0 = (hpos0 + 1) & 0xfff;
            }
            break;
        case 1: case 2:
            for (int x = 0; x < W; x++) {
                const int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3, pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                if ((kPriorityMap[control - 1][pix0] >> pix1) & 1) dest[x] = table[0x10 + pix1];
                else dest[x] = table[pix0];
                hpos0 = (hpos0 + 1) & 0xfff;
                hpos1 = (hpos1 + 1) & 0xfff;
            }
            break;
        case 3:
            if (data1 & 0x800) break;
            for (int x = 0; x < W; x++) {
                dest[x] = table[0x10 + ((hpos1 < 0x200) ? src1[hpos1] : 3)];
                hpos1 = (hpos1 + 1) & 0xfff;
            }
            break;
        }
    }
}

static void draw_sprites(void)                  // (sega_outrun_sprite_device::draw, the X-Board variant)
{
    for (int i = 0; i < W * H; i++) S.sprite_bitmap[i] = 0xffff;
    if (!S.sprite_dwords) return;
    const int numbanks = (int)(S.sprite_dwords / 0x10000);
    const int min_x = SPRITE_XORIGIN, max_x = SPRITE_XORIGIN + W - 1;
    for (int index = 0; index + 8 <= 0x800; index += 8) {
        uint16_t *data = &S.sprite_buffer[index];
        if (data[0] & 0x8000) break;
        const int hide = data[0] & 0x5000;
        int bank = (data[0] >> 9) & 7;
        const int top = (int)(data[0] & 0x1ff) - 0x100;
        uint16_t addr = data[1];
        const int pitch = (int16_t)((data[2] >> 1) | ((data[4] & 0x1000) << 3)) >> 8;
        int xpos = data[2] & 0x1ff;
        int vzoom = data[3] & 0x7ff;
        const int ydelta = (data[4] & 0x8000) ? 1 : -1;
        const bool flip = ((~data[4] >> 14) & 1) != 0;
        const int xdelta = (data[4] & 0x2000) ? 1 : -1;
        int hzoom = data[4] & 0x7ff;
        const int height = (data[5] & 0xfff) + 1;
        const uint16_t colpri = (uint16_t)(((data[6] & 0xff) << 4) | (((data[3] >> 12) & 7) << 12));
        if (xpos < 0x80 && xdelta < 0) xpos += 0x200;
        data[7] = addr;
        if (hide) continue;
        if (numbanks) bank %= numbanks;
        const uint32_t *spritedata = S.sprite_rom + (size_t)0x10000 * (size_t)bank;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        int yacc = 0;
        const int ytarget = top + ydelta * height;
        for (int y = top; y != ytarget; y += ydelta) {
            if (y >= 0 && y < H) {
                uint16_t *dest = &S.sprite_bitmap[y * W];
                int xacc = 0;
                data[7] = addr;
                for (int x = xpos; (xdelta > 0 && x <= max_x) || (xdelta < 0 && x >= min_x);) {
                    uint32_t pixels = spritedata[data[7]];
                    if (flip) data[7]--;
                    else {
                        data[7]++;
                        pixels = ((pixels << 28) & 0xf0000000u) | ((pixels << 20) & 0x0f000000u) | ((pixels << 12) & 0x00f00000u) |
                                 ((pixels << 4) & 0x000f0000u) | ((pixels >> 4) & 0x0000f000u) | ((pixels >> 12) & 0x00000f00u) |
                                 ((pixels >> 20) & 0x000000f0u) | ((pixels >> 28) & 0x0000000fu);
                    }
                    const bool last = (pixels & 0x0f000000u) == 0x0f000000u;
                    for (int k = 0; k < 8; k++) {
                        const int pix = (int)(pixels & 0xf);
                        while (xacc < 0x200) {
                            if (x >= min_x && x <= max_x && pix != 0 && pix != 15) dest[x - SPRITE_XORIGIN] = (uint16_t)(colpri | pix);
                            x += xdelta;
                            xacc += hzoom;
                        }
                        xacc -= 0x200;
                        pixels >>= 4;
                    }
                    if (last) break;
                }
            }
            yacc += vzoom;
            addr = (uint16_t)(addr + pitch * (yacc >> 9));
            yacc &= 0x1ff;
        }
    }
}

static void render(void)
{
    if (!S.display_enable) {
        memset(S.fb, 0, sizeof S.fb);
        return;
    }
    draw_sprites();
    memset(S.priority, 0, sizeof S.priority);
    memset(S.bitmap, 0, sizeof S.bitmap);
    draw_road(true);                            // (After Burner II: road_priority 0, both road layers first)
    draw_road(false);
    draw_tile_layer(1, 0, 0x01);
    draw_tile_layer(1, 1, 0x02);
    draw_tile_layer(0, 0, 0x02);
    draw_tile_layer(0, 1, 0x04);
    draw_text_layer(0, 0x04);
    draw_text_layer(1, 0x08);
    for (int i = 0; i < W * H; i++) {
        uint16_t index = S.bitmap[i];
        const uint16_t pix = S.sprite_bitmap[i];
        if (pix != 0xffff) {
            const int prio = (pix >> 12) & 3;
            if ((1 << prio) > S.priority[i]) {
                if ((pix & 0x400f) == 0x400a) index = (uint16_t)((index & 0x1fff) + NPAL);
                else index = pix & 0xfff;
            }
        }
        S.fb[i] = s16_to565(S.palette[index % (NPAL * 2)]);
    }
}

// ---------------------------------------------------------------------------
// ROMs

static bool load_part(const arc_rom *r, uint8_t *buf)
{
    arc_rom one = *r;
    one.offset = 0;
    return arc_load(&one, 1, buf, one.size);
}

static uint8_t *load_bytes(const arc_rom *l, int n, uint32_t alloc)
{
    uint8_t *d = (uint8_t *)malloc(alloc);
    if (!d) return NULL;
    memset(d, 0xff, alloc);
    for (int i = 0; i < n; i++)
        if (l[i].offset + l[i].size > alloc || !load_part(&l[i], d + l[i].offset)) {
            free(d);
            return NULL;
        }
    return d;
}

static uint16_t *load16w(const arc_rom *l, int n, uint32_t *words)
{
    uint32_t bytes = 0;
    for (int i = 0; i < n; i++)
        if ((l[i].offset & ~1u) + l[i].size * 2 > bytes) bytes = (l[i].offset & ~1u) + l[i].size * 2;
    *words = bytes / 2;
    uint16_t *d = (uint16_t *)calloc(*words, 2);
    uint8_t *part = (uint8_t *)malloc(0x40000);
    bool ok = d && part;
    for (int i = 0; ok && i < n; i++) {
        if (!(ok = load_part(&l[i], part))) break;
        const bool high = (l[i].offset & 1) == 0;
        for (uint32_t j = 0, k = l[i].offset >> 1; j < l[i].size; j++, k++)
            d[k] = high ? (uint16_t)((d[k] & 0x00ff) | (part[j] << 8)) : (uint16_t)((d[k] & 0xff00) | part[j]);
    }
    free(part);
    if (!ok) {
        free(d);
        return NULL;
    }
    return d;
}

static uint32_t *load32dw(const arc_rom *l, int n, uint32_t *dwords)   // byte lane = offset & 3
{
    *dwords = 0;
    for (int i = 0; i < n; i++)
        if ((l[i].offset >> 2) + l[i].size > *dwords) *dwords = (l[i].offset >> 2) + l[i].size;
    uint32_t *d = (uint32_t *)calloc(*dwords, 4);
    uint8_t *part = (uint8_t *)malloc(0x40000);
    bool ok = d && part;
    for (int i = 0; ok && i < n; i++) {
        if (!(ok = load_part(&l[i], part))) break;
        const int lane = (int)(l[i].offset & 3);
        for (uint32_t j = 0, k = l[i].offset >> 2; j < l[i].size; j++, k++)
            d[k] = (d[k] & ~(0xffu << (lane * 8))) | ((uint32_t)part[j] << (lane * 8));
    }
    free(part);
    if (!ok) {
        free(d);
        return NULL;
    }
    return d;
}

static bool load_roms(void)
{
    if (!(S.main_rom = load16w(kMainRom, N(kMainRom), &S.main_words))) return false;
    if (!(S.sub_rom = load16w(kSubRom, N(kSubRom), &S.sub_words))) return false;
    uint8_t *t = load_bytes(kTileRom, N(kTileRom), 0x30000);
    if (!t) return false;
    S.tiles = s16_decode_tiles(t, 2);
    free(t);
    if (!S.tiles) return false;
    if (!(S.sprite_rom = load32dw(kSpriteRom, N(kSpriteRom), &S.sprite_dwords))) return false;
    uint8_t *road = load_bytes(kRoadRom, N(kRoadRom), 0x10000);
    if (!road || !(S.road_gfx = (uint8_t *)malloc(256 * 2 * 512 + 512))) return false;
    s16_decode_outrun_road(S.road_gfx, road, 0x10000);
    free(road);
    uint8_t *snd = load_bytes(kSoundRom, N(kSoundRom), 0x10000);
    if (!snd) return false;
    memcpy(S.sound_rom, snd, 0x10000);
    free(snd);
    return (S.pcm_rom = load_bytes(kPcmRom, N(kPcmRom), 0x80000)) != NULL;   // (ROMREGION_ERASEFF)
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    S.dsw_a = 0xff;
    S.dsw_b = 0xdd;                             // (upright, the throttle lever, 3 lives, continue, normal)
    S.io1_porta = 0xff;
    if (!load_roms()) return false;
    s16_build_palette_luts(S.pal_normal, S.pal_shadow, S.pal_hilight);
    m68kd_init(&S.main, MAIN_CLOCK, M68K_T_68000);
    m68kd_init(&S.sub, MAIN_CLOCK, M68K_T_68000);
    m68kd_set_address_mask(&S.main, 0x3fffff);
    m68kd_set_address_mask(&S.sub, 0xfffff);
    S.main.read_word = m_rw;
    S.main.write_word = m_ww;
    S.main.read_byte = m_rb;
    S.main.write_byte = m_wb;
    S.main.reset_instr = m_reset_instr;
    S.sub.read_word = s_rw;
    S.sub.write_word = s_ww;
    S.sub.read_byte = s_rb;
    S.sub.write_byte = s_wb;
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.in = sound_in;
    S.snd.out = sound_out;
    S.snd.cycles = sound_cycles;
    zd_init(&S.snd);
    ym2151_init(&S.ym, SOUND_CLOCK, RATE);
    S.ym.amplitude = 0.5f;                      // (update() sums L + R: halved for a mono mix)
    S.ym.irq_handler = ym_irq;
    segapcmd_init(&S.pcm, SOUND_CLOCK, 1.0f, SEGAPCMD_315_5218);
    segapcmd_set_bank(&S.pcm, SEGAPCMD_BANK_512);
    S.pcm.rom = S.pcm_rom;
    S.pcm.rom_size = 0x80000;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&S.main);
    m68kd_reset(&S.sub);
    zd_reset(&S.snd);
    ym2151_reset(&S.ym);
    segapcmd_reset(&S.pcm);
    memset(S.backup1, 0, sizeof S.backup1);
    memset(S.backup2, 0, sizeof S.backup2);
    memset(S.subram0, 0, sizeof S.subram0);
    memset(S.subram1, 0, sizeof S.subram1);
    memset(S.tile_ram, 0, sizeof S.tile_ram);
    memset(S.text_ram, 0, sizeof S.text_ram);
    memset(S.sprite_ram, 0, sizeof S.sprite_ram);
    memset(S.sprite_buffer, 0, sizeof S.sprite_buffer);
    memset(S.road_ram, 0, sizeof S.road_ram);
    memset(S.road_buffer, 0, sizeof S.road_buffer);
    memset(S.sound_ram, 0, sizeof S.sound_ram);
    for (int i = 0; i < NPAL; i++) palette_write(i, 0);
    memset(&S.mult_main, 0, sizeof S.mult_main);
    memset(&S.mult_sub, 0, sizeof S.mult_sub);
    memset(&S.div_main, 0, sizeof S.div_main);
    memset(&S.div_sub, 0, sizeof S.div_sub);
    ct_reset(&S.timer_main);
    ct_reset(&S.timer_sub);
    for (int c = 0; c < 2; c++) {
        memset(S.io[c].latch, 0, sizeof S.io[c].latch);
        memset(S.io[c].dir, 0xff, sizeof S.io[c].dir);
    }
    memset(S.latched_pages, 0, sizeof S.latched_pages);
    memset(S.latched_xscroll, 0, sizeof S.latched_xscroll);
    memset(S.latched_yscroll, 0, sizeof S.latched_yscroll);
    S.road_control = 0;
    S.display_enable = false;
    S.pc0 = 0;
    S.sound_reset = true;
    S.sound_mute = false;
    S.vblank_irq = false;
    S.adc_latch = 0;
    S.stick_x = S.stick_y = S.throttle = 0x80;
    S.main_debt = S.sub_debt = S.sound_debt = 0;
    S.pcm_acc = S.audio_acc = S.pcm_sum = 0;
    S.pcm_count = 0;
    S.pcm_last = 0;
    update_main_irqs();
}

int arc_run_frame(const int16_t **audio)
{
    const double lines = (double)SCANLINES * 2;
    const double main_cycles = (double)MAIN_CLOCK / FPS / lines, snd = (double)SOUND_CLOCK / FPS / lines;
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        ct_clock(&S.timer_main, (line & 1) != 0);   // (the main 5250 counts the V0 edges)
        if (line == 223) {
            S.vblank_irq = true;
            m68kd_set_irq(&S.sub, 4, M68_ASSERT);
            render();
        } else if (line == 224) {
            S.vblank_irq = false;
            m68kd_set_irq(&S.sub, 4, M68_CLEAR);
        } else if (line == 261) latch_tilemaps();
        update_main_irqs();
        for (int slice = 0; slice < 2; slice++) {
            S.main_debt += main_cycles;
            S.main_debt -= m68kd_run(&S.main, (int)S.main_debt);
            S.sub_debt += main_cycles;
            S.sub_debt -= m68kd_run(&S.sub, (int)S.sub_debt);
            S.sound_debt += snd;
            if (S.sound_reset) {
                const int idle = (int)S.sound_debt;
                sound_cycles(NULL, idle);
                S.sound_debt -= idle;
            } else S.sound_debt -= zd_run(&S.snd, (int)S.sound_debt);
        }
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    S.io1_porta = 0xff;                         // (D1 test, D2 service, D3 start, D4 Vulcan, D5 missile, D6 / D7 coins)
    if (in->service) S.io1_porta &= (uint8_t)~0x02;
    if (p & AP_START) S.io1_porta &= (uint8_t)~0x08;
    if (p & AP_B1) S.io1_porta &= (uint8_t)~0x10;
    if (p & AP_B2) S.io1_porta &= (uint8_t)~0x20;
    if (in->coin1) S.io1_porta &= (uint8_t)~0x40;
    if (in->coin2) S.io1_porta &= (uint8_t)~0x80;
    // (the stick X 0x20-0xe0, Y 0x40-0xc0 reversed, the throttle centred at 0x80: ramped as MAME's keys)
    S.stick_x = ramp(S.stick_x, (p & AP_LEFT) ? 0x20 : (p & AP_RIGHT) ? 0xe0 : 0x80, 8);
    S.stick_y = ramp(S.stick_y, (p & AP_UP) ? 0xc0 : (p & AP_DOWN) ? 0x40 : 0x80, 6);
    S.throttle = ramp(S.throttle, (p & AP_B3) ? 0xff : (p & AP_B4) ? 0x00 : 0x80, 40);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    if (bank == 1) S.dsw_b = v;
}
