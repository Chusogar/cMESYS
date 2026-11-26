// skullxbo.c - Atari Games' Skull & Crossbones (1989) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/skullxbo (after
// MAME's atari/skullxbo.cpp). A 68000 at 7.16 MHz (here Musashi: dsp-cpp has
// its own core, so the timing drifts apart from it), a 672x240 picture: the
// 64x64 playfield of 16x8 tiles, 5bpp motion objects in SLIP bands of 8
// lines merged with the board's priority equations, the alphanumerics on top
// (whose last columns hold the scroll commands); the JSA II sound board: a
// 6502, a YM2151 and an OKI M6295.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/okim6295dsp.h"
#include "../../../video/atari_mo.h"

static m68kd cpu;

#define W 672
#define H 240
#define SCANLINES 262
#define CPU_SYNC 4
#define FPS 59.922743
#define MAIN_CLOCK (14318181 / 2)
#define JSA_CLOCK 3579545
#define SOUND_CLOCK (JSA_CLOCK / 2)
#define OKI_CLOCK (JSA_CLOCK / 3)
#define RATE 44100
#define SPR_REGION 0x190000
#define PF_REGION 0x0a0000
#define MO_W 1024
#define MO_H 512
#define MO_NONE 0xffff

static const char *const kSets[] = { "skullxbo", NULL };
const arc_info arc_game = { "skullxbo", "Skull & Crossbones", "Atari Games", 1989, W, H, FPS, RATE, kSets,
                            { "ESPADA", "SALTO", NULL }, ARC_JOY_8WAY };

static const arc_rom kMain[] = {
    {"136072-5150.228a", 0x10000, 0x000000, 0x9546d88b}, {"136072-5151.228c", 0x10000, 0x000001, 0xb9ed8bd4},
    {"136072-5152.213a", 0x10000, 0x020000, 0xc07e44fc}, {"136072-5153.213c", 0x10000, 0x020001, 0xfef8297f},
    {"136072-1154.200a", 0x10000, 0x040000, 0xde4101a3}, {"136072-1155.200c", 0x10000, 0x040001, 0x78c0f6ad},
    {"136072-1156.185a", 0x08000, 0x070000, 0xcde16b55}, {"136072-1157.185c", 0x08000, 0x070001, 0x31c77376}};
static const arc_rom kSound[] = {{"136072-1149.1b", 0x10000, 0x00000, 0x8d730e7a}};
static const arc_rom kSpr[] = {
    {"136072-1102.13r", 0x10000, 0x000000, 0x90becdfa}, {"136072-1104.28r", 0x10000, 0x010000, 0x33609071},
    {"136072-1106.41r", 0x10000, 0x020000, 0x71962e9f}, {"136072-1101.13p", 0x10000, 0x030000, 0x4d41701e},
    {"136072-1103.28p", 0x10000, 0x040000, 0x3011da3b}, {"136072-1108.53r", 0x10000, 0x050000, 0x386c7edc},
    {"136072-1110.67r", 0x10000, 0x060000, 0xa54d16e6}, {"136072-1112.81r", 0x10000, 0x070000, 0x669411f6},
    {"136072-1107.53p", 0x10000, 0x080000, 0xcaaeb57a}, {"136072-1109.67p", 0x10000, 0x090000, 0x61cb4e28},
    {"136072-1114.95r", 0x10000, 0x0a0000, 0xe340d5a1}, {"136072-1116.109r", 0x10000, 0x0b0000, 0xf25b8aca},
    {"136072-1118.123r", 0x10000, 0x0c0000, 0x8cf73585}, {"136072-1113.95p", 0x10000, 0x0d0000, 0x899b59af},
    {"136072-1115.109p", 0x10000, 0x0e0000, 0xcf4fd19a}, {"136072-1120.137r", 0x10000, 0x0f0000, 0xfde7c03d},
    {"136072-1122.151r", 0x10000, 0x100000, 0x6ff6a9f2}, {"136072-1124.165r", 0x10000, 0x110000, 0xf11909f1},
    {"136072-1119.137p", 0x10000, 0x120000, 0x6f8003a1}, {"136072-1121.151p", 0x10000, 0x130000, 0x8ff0a1ec},
    {"136072-1125.123n", 0x10000, 0x140000, 0x3aa7c756}, {"136072-1126.137n", 0x10000, 0x150000, 0xcb82c9aa},
    {"136072-1128.151n", 0x10000, 0x160000, 0xdce32863}};
static const arc_rom kPf[] = {
    {"136072-2129.180p", 0x10000, 0x000000, 0x36b1a578}, {"136072-2131.193p", 0x10000, 0x010000, 0x7b7c04a1},
    {"136072-2133.208p", 0x10000, 0x020000, 0xe03fe4d9}, {"136072-2135.221p", 0x10000, 0x030000, 0x7d497110},
    {"136072-2137.235p", 0x10000, 0x040000, 0xf91e7872}, {"136072-2130.180r", 0x10000, 0x050000, 0xb25368cc},
    {"136072-2132.193r", 0x10000, 0x060000, 0x112f2d20}, {"136072-2134.208r", 0x10000, 0x070000, 0x84884ed6},
    {"136072-2136.221r", 0x10000, 0x080000, 0xbc028690}, {"136072-2138.235r", 0x10000, 0x090000, 0x60cec955}};
static const arc_rom kChars[] = {{"136072-2141.250k", 0x8000, 0x0000, 0x60d6d6df}};
static const arc_rom kOki[] = {
    {"136072-1145.7k", 0x10000, 0x00000, 0xd9475d58}, {"136072-1146.7j", 0x10000, 0x10000, 0x133e6aef},
    {"136072-1147.7e", 0x10000, 0x20000, 0xba4d556e}, {"136072-1148.7d", 0x10000, 0x30000, 0xc48df49a}};

#define U5 ((SPR_REGION / 5) * 8)
#define HALF ((PF_REGION / 2) * 8)
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, kY8[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kPfX[16] = {HALF, HALF, HALF + 4, HALF + 4, 0, 0, 4, 4, HALF + 8, HALF + 8, HALF + 12, HALF + 12, 8, 8, 12, 12};
static const int kAnX[16] = {0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14}, kAnY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const arc_layout kSprL = {16, 8, SPR_REGION / 5 / 16, 5, 128, false, false, false, {4 * U5, 3 * U5, 2 * U5, U5, 0}, kX16, kY8};
static const arc_layout kPfL = {16, 8, PF_REGION / 2 / 16, 4, 128, false, false, false, {0, 1, 2, 3}, kPfX, kY8};
static const arc_layout kAnL = {16, 8, 0x8000 / 16, 2, 128, false, false, false, {0, 1}, kAnX, kAnY};

static const atari_mo_config kMoCfg = {
    .tile_width = 16, .tile_height = 8, .bankcount = 2, .linked = true, .split = false,
    .maxperline = 0, .palettebase = 0, .link_entry = {0x00ff, 0, 0, 0},
    .code_entry = {{0, 0x7fff, 0, 0}, {0, 0, 0, 0}}, .color_entry = {{0, 0, 0x000f, 0}, {0, 0, 0, 0}},
    .xpos_entry = {0, 0, 0xffc0, 0}, .ypos_entry = {0, 0, 0, 0xff80}, .width_entry = {0, 0, 0, 0x0070},
    .height_entry = {0, 0, 0, 0x000f}, .hflip_entry = {0, 0x8000, 0, 0}, .priority_entry = {0, 0, 0x0030, 0},
    .slipheight = 8,
};

static struct state {
    uint16_t rom[0x40000];
    uint8_t sound_rom[0x10000], *spr, *pf, chars[0x8000], *oki_rom;
    m65xx snd;
    ym2151 ym;
    okim6295d oki;
    atari_mo mo;
    uint16_t playfield[0x1000], pf_ext[0x1000], alpha[0x800], mob[0x800], ram[0x1000], pal_ram[0x800];
    uint8_t eeprom[0x800], sound_ram[0x2000];
    uint16_t pal[0x800];
    uint16_t mo_index[W * H];
    uint8_t mo_prio[W * H];
    int scanline, pf_scrollx, pf_scrolly, mo_yscroll, playfield_latch, pending_irq, clip0, clip1;
    uint16_t xscroll_reg, yscroll_reg;
    bool eeprom_unlocked, halt_until_hblank;
    uint8_t sound_bank, to_main, to_sound;
    bool to_main_ready, to_sound_ready, timed_int, ym_int;
    float ym_gain, oki_gain, ym_ct1;
    int64_t sound_irq_acc, audio_acc, oki_acc;
    int32_t last_oki;
    uint16_t in0, in1;
    bool coin1, coin2, service;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *K_;
#define K (*K_)

// ---------------------------------------------------------------------------
// The 68000 (Musashi)

static void set_irq(int level, bool on) { m68kd_set_irq(&cpu, level, on ? M68_ASSERT : M68_CLEAR); }


static void update_sound_irq(void) { m65xx_set_irq(&K.snd, K.timed_int || K.ym_int ? M65_ASSERT : M65_CLEAR); }

static void sound_reset(void)
{
    m65xx_reset(&K.snd);
    ym2151_reset(&K.ym);
    okid_reset(&K.oki);
    K.sound_bank = 0;
    K.to_main_ready = false;
    set_irq(4, false);
    K.timed_int = K.ym_int = false;
    K.ym_gain = K.oki_gain = 1.0f;
    K.ym_ct1 = 0.0f;
    update_sound_irq();
}

static uint8_t pal6(uint8_t b) { b &= 0x3f; return (uint8_t)((b << 2) | (b >> 4)); }

static void set_yscroll(int v)
{
    K.pf_scrolly = v & 0x1ff;
    K.mo_yscroll = v & 0x1ff;
}

static uint16_t read16(uint32_t a)
{
    a &= 0xffffff;
    if (a < 0x80000) return K.rom[a >> 1];
    if (a >= 0xff2000 && a <= 0xff2fff) return K.pal_ram[(a & 0xfff) >> 1];
    if (a >= 0xff5000 && a <= 0xff5001) {
        K.to_main_ready = false;
        set_irq(4, false);
        return (uint16_t)(0xff00 | K.to_main);
    }
    if (a >= 0xff5800 && a <= 0xff5803) {
        if (!(a & 2)) return K.in0;
        uint16_t v = K.in1;
        if (K.scanline >= H) v |= 0x0020;
        if (K.to_sound_ready) v &= (uint16_t)~0x0040;
        return v;
    }
    if (a >= 0xff6000 && a <= 0xff6fff) return (uint16_t)(0xff00 | K.eeprom[(a & 0xfff) >> 1]);
    if (a >= 0xff8000 && a <= 0xff9fff) return K.playfield[(a & 0x1fff) >> 1];
    if (a >= 0xffa000 && a <= 0xffbfff) return K.pf_ext[(a & 0x1fff) >> 1];
    if (a >= 0xffc000 && a <= 0xffcfff) return K.alpha[(a & 0xfff) >> 1];
    if (a >= 0xffd000 && a <= 0xffdfff) return K.mob[(a & 0xfff) >> 1];
    if (a >= 0xffe000) return K.ram[(a & 0x1fff) >> 1];
    return 0xffff;
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a < 0x80000) return;
    if (a >= 0xff0000 && a <= 0xff07ff) K.mo.bank = (a >> 10) & 1;
    else if (a >= 0xff0800 && a <= 0xff0bff) K.halt_until_hblank = true;
    else if (a >= 0xff0c00 && a <= 0xff0fff) K.eeprom_unlocked = true;
    else if (a >= 0xff1000 && a <= 0xff13ff) set_irq(2, false);
    else if (a >= 0xff1400 && a <= 0xff17ff) {
        K.to_sound = (uint8_t)v;
        K.to_sound_ready = true;
        m65xx_set_nmi(&K.snd, M65_ASSERT);
    } else if (a >= 0xff1800 && a <= 0xff1bff) sound_reset();
    else if (a >= 0xff1c00 && a <= 0xff1fff) {
        switch ((a >> 7) & 3) {
        case 0: K.playfield_latch = v; break;
        case 1:
            K.xscroll_reg = v;
            K.pf_scrollx = 2 * (v >> 7);
            break;
        case 2: set_irq(1, false); break;
        default: break;
        }
    } else if (a >= 0xff2000 && a <= 0xff2fff) {
        const int i = (int)((a & 0xfff) >> 1);
        K.pal_ram[i] = v;
        const uint8_t in = (v >> 15) & 1;
        const unsigned r = pal6((uint8_t)(((v >> 9) & 0x3e) | in)), g = pal6((uint8_t)(((v >> 4) & 0x3e) | in)), b = pal6((uint8_t)(((v << 1) & 0x3e) | in));
        K.pal[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    } else if (a >= 0xff4000 && a <= 0xff47ff) {
        int line = K.scanline;
        K.yscroll_reg = v;
        if (line >= H) line = 0;
        set_yscroll((int)(v >> 7) - line);         // (a counter that counts with the lines)
    } else if (a >= 0xff6000 && a <= 0xff6fff) {
        if (K.eeprom_unlocked) {
            K.eeprom[(a & 0xfff) >> 1] = (uint8_t)v;
            K.eeprom_unlocked = false;
        }
    } else if (a >= 0xff8000 && a <= 0xff9fff) {
        const size_t o = (a & 0x1fff) >> 1;
        K.playfield[o] = v;
        if (K.playfield_latch != -1) K.pf_ext[o] = (uint16_t)((K.pf_ext[o] & 0xff00) | (K.playfield_latch & 0xff));
    } else if (a >= 0xffa000 && a <= 0xffbfff) K.pf_ext[(a & 0x1fff) >> 1] = v;
    else if (a >= 0xffc000 && a <= 0xffcfff) K.alpha[(a & 0xfff) >> 1] = v;
    else if (a >= 0xffd000 && a <= 0xffdfff) K.mob[(a & 0xfff) >> 1] = v;
    else if (a >= 0xffe000) K.ram[(a & 0x1fff) >> 1] = v;
}

static uint16_t cpu_rd(void *u_, uint32_t a) { (void)u_; return read16(a); }
static void cpu_wr(void *u_, uint32_t a, uint16_t v) { (void)u_; write16(a, (uint16_t)v); }

// ---------------------------------------------------------------------------
// The JSA II

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x2000) return K.sound_ram[a];
    if (a < 0x2800) return ym2151_status(&K.ym);
    if (a < 0x2a00) {
        switch (a & 6) {
        case 0: return okid_read(&K.oki);
        case 2:
            K.to_sound_ready = false;
            m65xx_set_nmi(&K.snd, M65_CLEAR);
            return K.to_sound;
        case 4: {
            uint8_t r = 0x1c;
            if (K.coin1) r |= 0x01;
            if (K.coin2) r |= 0x02;
            if (K.to_main_ready) r |= 0x20;
            if (!K.to_sound_ready) r |= 0x40;
            if (!K.service) r |= 0x80;
            return r;
        }
        default:
            K.timed_int = false;
            update_sound_irq();
            return 0;
        }
    }
    if (a < 0x3000) return 0xff;
    if (a < 0x4000) return K.sound_rom[K.sound_bank * 0x1000 + (a & 0x0fff)];
    return K.sound_rom[a];
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x2000) K.sound_ram[a] = v;
    else if (a < 0x2800) {
        if (!(a & 1)) ym2151_select(&K.ym, v);
        else ym2151_write(&K.ym, v);
    } else if (a < 0x2a00) return;
    else if (a < 0x2c00) {
        switch (a & 6) {
        case 0: okid_write(&K.oki, v); break;
        case 2:
            K.to_main = v;
            K.to_main_ready = true;
            set_irq(4, true);
            break;
        case 4:                                     // /WRIO: the ROM bank, the OKI's clock and the resets
            K.sound_bank = (v >> 6) & 3;
            K.oki.divisor = (v & 0x08) ? 132 : 165;
            if (!(v & 0x04)) okid_reset(&K.oki);
            if (!(v & 0x01)) ym2151_reset(&K.ym);
            break;
        default:                                    // /MIX
            K.ym_gain = (float)((v >> 1) & 7) / 7.0f;
            K.oki_gain = (v & 1) ? 1.0f : 0.5f;
            break;
        }
    }
}

static void snd_cycles(void *u, int c)
{
    (void)u;
    ym2151_run_timers(&K.ym, c * 2);
    K.sound_irq_acc += (int64_t)c * (JSA_CLOCK / 4 / 16 / 16);
    while (K.sound_irq_acc >= (int64_t)SOUND_CLOCK * 14) {
        K.sound_irq_acc -= (int64_t)SOUND_CLOCK * 14;
        K.timed_int = true;
        update_sound_irq();
    }
    K.oki_acc += (int64_t)c * okid_sample_frequency(&K.oki);
    while (K.oki_acc >= SOUND_CLOCK) {
        K.oki_acc -= SOUND_CLOCK;
        K.last_oki = okid_update(&K.oki);
    }
    K.audio_acc += (int64_t)c * RATE;
    while (K.audio_acc >= SOUND_CLOCK) {
        K.audio_acc -= SOUND_CLOCK;
        const int32_t s = (int32_t)((float)ym2151_update(&K.ym) * K.ym_gain * 0.6f) + (int32_t)((float)K.last_oki * K.oki_gain * K.ym_ct1 * 0.75f);
        if (K.naudio < 1024) K.audio[K.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on)
{
    (void)u;
    K.ym_int = on;
    update_sound_irq();
}

static void ym_port(void *u, uint8_t d) { (void)u; K.ym_ct1 = (d & 1) ? 1.0f : 0.0f; }

// ---------------------------------------------------------------------------
// Video

static int mo_code(void *u, int raw) { (void)u; return raw; }
static int mo_color(void *u, int i) { (void)u; return i * 2; }    // (5bpp: 32 entries a colour)

static void mo_tile(void *u, int code, int color, bool hflip, bool vflip, int x, int y, int gfx, int prio)
{
    (void)u, (void)gfx;
    const int sx = x >= W ? x - MO_W : x, sy = y >= H ? y - MO_H : y;
    for (int r = 0; r < 8; r++) {
        const int ty = sy + r;
        if (ty < K.clip0 || ty > K.clip1) continue;
        const int srow = vflip ? 7 - r : r;
        for (int c = 0; c < 16; c++) {
            const int tx = sx + c;
            if (tx < 0 || tx >= W) continue;
            const unsigned pen = arc_gfx_px(&kSprL, K.spr, SPR_REGION, code, hflip ? 15 - c : c, srow);
            if (!pen) continue;
            K.mo_index[ty * W + tx] = (uint16_t)(color + pen);
            K.mo_prio[ty * W + tx] = (uint8_t)prio;
        }
    }
}

static void mo_band(int line)
{
    static const atari_mo_lookup lk = {mo_code, mo_color, NULL};
    K.clip0 = line;
    K.clip1 = line + 7 < H - 1 ? line + 7 : H - 1;
    for (int y = line; y <= K.clip1; y++)
        for (int x = 0; x < W; x++) K.mo_index[y * W + x] = MO_NONE;
    const int band = atari_mo_band_for_line(&K.mo, line, K.mo_yscroll);
    atari_mo_draw_band(&K.mo, K.mob, &K.alpha[0x7c0], band, K.pf_scrollx, K.mo_yscroll, -1, &lk, mo_tile, NULL);
}

static void scanline_update(int line)
{
    int o = (line / 8) * 64 + 42;
    if (o >= 0x7c0) return;
    if (line == 0) set_yscroll((int)(K.yscroll_reg >> 7) & 0x1ff);
    for (int x = 42; x < 64; x++) {
        const uint16_t d = K.alpha[o++];
        if ((d & 0x0f) != 0x0d) continue;
        set_yscroll((int)(d >> 7) - line);
        K.yscroll_reg = d;
    }
}

static void render_line(int line)
{
    const int sy = (line + K.pf_scrolly) & 0x1ff, row = sy >> 3, prow = sy & 7;
    const int arow = (line >> 3) * 64, aprow = line & 7;
    uint16_t *d = K.fb + line * W;
    for (int x = 0; x < W; x++) {
        const int sx = (x + K.pf_scrollx) & 0x3ff;
        const int t = (sx >> 4) * 64 + row;
        const uint16_t data = K.playfield[t];
        int col = sx & 15;
        if (data & 0x8000) col = 15 - col;
        const unsigned pen = arc_gfx_px(&kPfL, K.pf, PF_REGION, data & 0x7fff, col, prow);
        int index = 0x200 + (K.pf_ext[t] & 0x0f) * 16 + (int)pen;
        const int o = line * W + x;
        const uint16_t obj = K.mo_index[o];
        if (obj != MO_NONE) {                       // the priority equations of the board's GALs
            const int mp = K.mo_prio[o], mopix = obj & 0x1f, pfcolor = (index >> 4) & 0x0f, pfpix = index & 0x0f;
            const bool o17 = (index & 0xc8) == 0xc8;
            if ((mp == 0 && !o17 && mopix >= 2) || (mp == 1 && mopix >= 2 && !(pfcolor & 8)) ||
                ((mp & 2) && mopix >= 2 && !(pfcolor & 0x0c)) || (!(pfpix & 8) && mopix >= 2))
                index = obj;
            else if ((mp == 0 && !o17 && mopix == 1) || (mp == 1 && mopix == 1 && !(pfcolor & 8)) ||
                     ((mp & 2) && mopix == 1 && !(pfcolor & 0x0c)) || (!(pfpix & 8) && mopix == 1))
                index |= 0x400;                     // shadow
        }
        const uint16_t al = K.alpha[arow + (x >> 4)];
        const unsigned ap = arc_gfx_px(&kAnL, K.chars, sizeof K.chars, (al ^ 0x400) & 0x7ff, x & 15, aprow);
        if (ap || (al & 0x8000)) index = 0x300 + ((al >> 11) & 0x0f) * 4 + (int)ap;
        d[x] = K.pal[index & 0x7ff];
    }
}

// ---------------------------------------------------------------------------

bool arc_init(void)
{
    if (!(K_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *prog = (uint8_t *)calloc(1, 0x80000), *t = (uint8_t *)malloc(0x10000);
    K.spr = (uint8_t *)calloc(1, SPR_REGION);
    K.pf = (uint8_t *)calloc(1, PF_REGION);
    K.oki_rom = (uint8_t *)calloc(1, 0x40000);
    if (!prog || !t || !K.spr || !K.pf || !K.oki_rom) return false;
    bool ok = true;
    for (int i = 0; i < 8 && ok; i++) {             // byte pairs: the even offsets the high bytes
        arc_rom r = kMain[i];
        r.offset = 0;
        memset(t, 0, 0x10000);
        ok = arc_load(&r, 1, t, 0x10000);
        for (uint32_t b = 0; b < kMain[i].size; b++) prog[(kMain[i].offset & ~1u) + b * 2 + (kMain[i].offset & 1u)] = t[b];
    }
    for (int i = 0; i < 0x80000; i += 2) K.rom[i >> 1] = (uint16_t)(prog[i] << 8 | prog[i + 1]);
    free(prog);
    free(t);
    ok = ok && ARC_LOAD(kSound, K.sound_rom, sizeof K.sound_rom) && ARC_LOAD(kSpr, K.spr, SPR_REGION) &&
         ARC_LOAD(kPf, K.pf, PF_REGION) && ARC_LOAD(kChars, K.chars, sizeof K.chars) && ARC_LOAD(kOki, K.oki_rom, 0x40000);
    if (!ok) return false;
    for (int i = 0; i < PF_REGION; i++) K.pf[i] = (uint8_t)~K.pf[i];    // ROMREGION_INVERT
    memset(K.eeprom, 0xff, sizeof K.eeprom);
    okid_init(&K.oki, OKI_CLOCK, true, K.oki_rom, 0x40000);
    ym2151_init(&K.ym, JSA_CLOCK, RATE);
    K.ym.irq_handler = ym_irq;
    K.ym.port_handler = ym_port;
    K.snd.read = snd_rd;
    K.snd.write = snd_wr;
    K.snd.cycles = snd_cycles;
    atari_mo_init(&K.mo, &kMoCfg, W + 16, H + 8);
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m65xx_reset(&K.snd);
    ym2151_reset(&K.ym);
    okid_reset(&K.oki);
    K.oki.divisor = 132;
    memset(K.playfield, 0, sizeof K.playfield);
    memset(K.pf_ext, 0, sizeof K.pf_ext);
    memset(K.alpha, 0, sizeof K.alpha);
    memset(K.mob, 0, sizeof K.mob);
    memset(K.ram, 0, sizeof K.ram);
    memset(K.sound_ram, 0, sizeof K.sound_ram);
    memset(K.pal_ram, 0, sizeof K.pal_ram);
    memset(K.pal, 0, sizeof K.pal);
    for (int i = 0; i < W * H; i++) K.mo_index[i] = MO_NONE;
    memset(K.mo_prio, 0, sizeof K.mo_prio);
    K.scanline = K.pf_scrollx = K.pf_scrolly = K.mo_yscroll = 0;
    K.xscroll_reg = K.yscroll_reg = 0;
    K.playfield_latch = -1;
    K.eeprom_unlocked = K.halt_until_hblank = false;
    K.pending_irq = -1;
    K.mo.bank = 0;
    K.sound_bank = 0;
    K.to_main_ready = K.to_sound_ready = false;
    K.to_main = K.to_sound = 0;
    K.timed_int = K.ym_int = false;
    K.ym_gain = K.oki_gain = 1.0f;
    K.ym_ct1 = 0.0f;
    K.sound_irq_acc = 0;
    K.in0 = 0xffff;
    K.in1 = (uint16_t)(0xffff & ~0x0030);           // HBLANK / VBLANK active high
    K.coin1 = K.coin2 = false;
    K.audio_acc = K.oki_acc = 0;
    K.last_oki = 0;
    m68kd_reset(&cpu);
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    const int sc = (int)((double)SOUND_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    K.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        K.scanline = line;
        if ((line & 7) == 0) {                      // a scanline IRQ asked for in the alpha RAM: 6 lines on
            const int o = (line / 8) * 64 + 42;
            if (o < 0x7c0 && (K.alpha[o] & 0x8000)) K.pending_irq = line + 6;
            scanline_update(line);
        }
        if (line == K.pending_irq) {
            set_irq(1, true);
            K.pending_irq = -1;
        }
        if (line == H) set_irq(2, true);
        K.halt_until_hblank = false;
        for (int step = 0; step < CPU_SYNC; step++) {
            if (step == CPU_SYNC - 1) K.in1 |= 0x0010;
            else K.in1 &= (uint16_t)~0x0010;
            if (!K.halt_until_hblank || step == CPU_SYNC - 1) m68kd_run(&cpu, mc);
            m65xx_run(&K.snd, sc);
        }
        K.in1 &= (uint16_t)~0x0010;
        if (line < H) {
            if ((line & 7) == 0) mo_band(line);
            render_line(line);
        }
    }
    if (audio) *audio = K.audio;
    return K.naudio;
}

const uint16_t *arc_frame(void) { return K.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    K.in0 = 0xffff;
    K.in1 = (uint16_t)(0xffff & ~0x0030);
    if (p1 & AP_B1) K.in0 &= (uint16_t)~0x0100;
    if (p1 & AP_B2) K.in0 &= (uint16_t)~0x0200;
    if (p1 & AP_RIGHT) K.in0 &= (uint16_t)~0x1000;
    if (p1 & AP_LEFT) K.in0 &= (uint16_t)~0x2000;
    if (p1 & AP_DOWN) K.in0 &= (uint16_t)~0x4000;
    if (p1 & AP_UP) K.in0 &= (uint16_t)~0x8000;
    if (p2 & AP_B1) K.in1 &= (uint16_t)~0x0100;
    if (p2 & AP_B2) K.in1 &= (uint16_t)~0x0200;
    if (p2 & AP_RIGHT) K.in1 &= (uint16_t)~0x1000;
    if (p2 & AP_LEFT) K.in1 &= (uint16_t)~0x2000;
    if (p2 & AP_DOWN) K.in1 &= (uint16_t)~0x4000;
    if (p2 & AP_UP) K.in1 &= (uint16_t)~0x8000;
    if (K.service) K.in1 &= (uint16_t)~0x0080;
    K.coin1 = in->coin1;
    K.coin2 = in->coin2;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) K.service = v & 1;
}
