// m72.c - Irem M72 (1987-89) for the common arcade frontends (arc.h): C port
// of dsp-cpp's drivers/arcade/m72 (from dsp-emulator's m72_hw.pas).
// ARC_VARIANT: 0 R-Type, 1 Hammerin' Harry, 2 R-Type II. A NEC V30 at 8 MHz
// with a raster IRQ that splits the frame, a Z80 with a YM2151 (and, but on
// R-Type, an 8-bit DAC fed from a sample ROM), two 512x512 layers of 8x8
// tiles each in two priorities around the sprites (made of 16x16 blocks).
// The layers show what their RAM held at the previous frame's end, as
// dsp-cpp's cached ones. The graphics are read straight from their ROMs and
// the picture built in place.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/necv30/nec_v30.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define RTYPE (ARC_VARIANT == 0)
#define RTYPE2 (ARC_VARIANT == 2)
#define HAS_DAC (!RTYPE)
#define MAIN_CLOCK 8000000
#define SOUND_CLOCK 3579545
#define FPS 55.017606
#define SCANLINES 284
#define RATE 44100
#define W 384
#define H 256

typedef struct { const char *name; uint32_t size, offset, crc; } rom16;   // offset & 1: the odd bytes

#if ARC_VARIANT == 0
static const char *const kSets[] = { "rtype", NULL };
static const rom16 kProg[] = {
    {"rt_r-h0-b.1b", 0x10000, 1, 0x591c7754}, {"rt_r-l0-b.3b", 0x10000, 0, 0xa1928df0},
    {"rt_r-h1-b.1c", 0x10000, 0x20001, 0xa9d71eca}, {"rt_r-l1-b.3c", 0x10000, 0x20000, 0x0df3573d}};
static const arc_rom kChars0[] = {
    {"rt_b-a0.3c", 0x8000, 0, 0x4e212fb0}, {"rt_b-a1.3d", 0x8000, 0x8000, 0x8a65bdff},
    {"rt_b-a2.3a", 0x8000, 0x10000, 0x5a4ae5b9}, {"rt_b-a3.3e", 0x8000, 0x18000, 0x73327606}};
static const arc_rom kChars1[] = {
    {"rt_b-b0.3j", 0x8000, 0, 0xa7b17491}, {"rt_b-b1.3k", 0x8000, 0x8000, 0xb9709686},
    {"rt_b-b2.3h", 0x8000, 0x10000, 0x433b229a}, {"rt_b-b3.3f", 0x8000, 0x18000, 0xad89b072}};
static const arc_rom kSprites[] = {
    {"rt_r-00.1h", 0x10000, 0, 0xdad53bc0}, {"rt_r-01.1j", 0x8000, 0x10000, 0x5e441e7f}, {"rt_r-01.1j", 0x8000, 0x18000, 0x5e441e7f},
    {"rt_r-10.1k", 0x10000, 0x20000, 0xd6a66298}, {"rt_r-11.1l", 0x8000, 0x30000, 0x791df4f8}, {"rt_r-11.1l", 0x8000, 0x38000, 0x791df4f8},
    {"rt_r-20.3h", 0x10000, 0x40000, 0xfc247c8a}, {"rt_r-21.3j", 0x8000, 0x50000, 0xed793841}, {"rt_r-21.3j", 0x8000, 0x58000, 0xed793841},
    {"rt_r-30.3k", 0x10000, 0x60000, 0xeb02a1cb}, {"rt_r-31.3l", 0x8000, 0x70000, 0x8558355d}, {"rt_r-31.3l", 0x8000, 0x78000, 0x8558355d}};
#define ID "rtype"
#define TITLE "R-Type"
#define YEAR 1987
#define PROG_SIZE 0x40000
#define CHARS 0x1000
#define CHAR_ROM 0x20000
#define CODE_MASK 0xfff
#define DSW 0xfdfb
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "hharry", NULL };
static const rom16 kProg[] = {
    {"a-h0-v.rom", 0x20000, 1, 0xc52802a5}, {"a-l0-v.rom", 0x20000, 0, 0xf463074c},
    {"a-h1-0.rom", 0x10000, 0x60001, 0x3ae21335}, {"a-l1-0.rom", 0x10000, 0x60000, 0xbc6ac5f9}};
static const arc_rom kSound[] = {{"a-sp-0.rom", 0x10000, 0, 0x80e210e7}};
static const arc_rom kDac[] = {{"a-v0-0.rom", 0x20000, 0, 0xfaaacaff}};
static const arc_rom kChars0[] = {
    {"hh_a0.rom", 0x20000, 0, 0xc577ba5f}, {"hh_a1.rom", 0x20000, 0x20000, 0x429d12ab},
    {"hh_a2.rom", 0x20000, 0x40000, 0xb5b163b0}, {"hh_a3.rom", 0x20000, 0x60000, 0x8ef566a1}};
static const arc_rom kSprites[] = {
    {"hh_00.rom", 0x20000, 0, 0xec5127ef}, {"hh_10.rom", 0x20000, 0x20000, 0xdef65294},
    {"hh_20.rom", 0x20000, 0x40000, 0xbb0d6ad4}, {"hh_30.rom", 0x20000, 0x60000, 0x4351044e}};
#define ID "hharry"
#define TITLE "Hammerin' Harry"
#define YEAR 1990
#define PROG_SIZE 0x80000
#define CHARS 0x4000
#define CHAR_ROM 0x80000
#define CODE_MASK 0x3fff
#define DSW 0xfdbf
#else
static const char *const kSets[] = { "rtype2", NULL };
static const rom16 kProg[] = {
    {"rt2-a-h0-d.54", 0x20000, 1, 0xd8ece6f4}, {"rt2-a-l0-d.60", 0x20000, 0, 0x32cfb2e4},
    {"rt2-a-h1-d.53", 0x20000, 0x40001, 0x4f6e9b15}, {"rt2-a-l1-d.59", 0x20000, 0x40000, 0x0fd123bf}};
static const arc_rom kSound[] = {{"ic17.4f", 0x10000, 0, 0x73ffecb4}};
static const arc_rom kDac[] = {{"ic14.4c", 0x20000, 0, 0x637172d5}};
static const arc_rom kChars0[] = {
    {"ic50.7s", 0x20000, 0, 0xf3f8736e}, {"ic51.7u", 0x20000, 0x20000, 0xb4c543af}, {"ic56.8s", 0x20000, 0x40000, 0x4cb80d66},
    {"ic57.8u", 0x20000, 0x60000, 0xbee128e0}, {"ic65.9r", 0x20000, 0x80000, 0x2dc9c71a}, {"ic66.9u", 0x20000, 0xa0000, 0x7533c428},
    {"ic63.9m", 0x20000, 0xc0000, 0xa6ad67f2}, {"ic64.9p", 0x20000, 0xe0000, 0x3686d555}};
static const arc_rom kSprites[] = {
    {"ic31.6l", 0x20000, 0, 0x2cd8f913}, {"ic21.4l", 0x20000, 0x20000, 0x5033066d},
    {"ic32.6m", 0x20000, 0x40000, 0xec3a0450}, {"ic22.4m", 0x20000, 0x60000, 0xdb6176fc}};
#define ID "rtype2"
#define TITLE "R-Type II"
#define YEAR 1989
#define PROG_SIZE 0x80000
#define CHARS 0x8000
#define CHAR_ROM 0x100000
#define CODE_MASK 0x7fff
#define DSW 0xf7ff
#endif

const arc_info arc_game = { ID, TITLE, "Irem", YEAR, W, H, FPS, RATE, kSets,
                            { "FIRE", "FORCE", NULL, NULL }, ARC_JOY_8WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 128, 129, 130, 131, 132, 133, 134, 135};
#define STRIDE (CHAR_ROM / 4 * 8)
static const arc_layout kCharL = { 8, 8, CHARS, 4, 64, false, false, false, {STRIDE * 3, STRIDE * 2, STRIDE, 0}, kX8, kY16 };
static const arc_layout kSprL = { 16, 16, 0x1000, 4, 256, false, false, false, {0x60000 * 8, 0x40000 * 8, 0x20000 * 8, 0}, kX16, kY16 };

// the machine on the heap
static struct state {
    nec_v30 main;
    zd_z80 sound;
    ym2151 ym;
    const uint8_t *rom, *chars0, *chars1, *sprites, *dac;   // blobs, the graphics raw
    uint8_t ram[0x4000], spriteram[0x400], sprite_buffer[0x400], pal1[0xc00], pal2[0xc00];
    uint8_t vram1[0x4000], vram2[0x4000], snd[0x10000];
    // the layers as dsp-cpp redraws them: the tile RAM and colours of the
    // last frame's end
    uint8_t vram1_s[0x4000], vram2_s[0x4000];
    uint16_t palette[512], pal_s[256];
    bool have_layers;
    arc_fb fb;
    uint8_t soundlatch, snd_irq_vector, irq_base[6], irq_pos;
    bool sound_irq_timer, sound_reset_held, video_off;
    uint16_t raster_pos, scroll_x1, scroll_y1, scroll_x2, scroll_y2;
    uint32_t sample_addr;
    int16_t dac_sample;
    uint16_t in0, in1, dsw;
    int nmi_counter;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *M_;
#define M (*M_)

static void color1(int num)
{
    M.palette[num >> 1] = arc_rgb565((uint32_t)((M.pal1[num] & 31) * 255 / 31) << 16 |
                                     (uint32_t)((M.pal1[num + 0x400] & 31) * 255 / 31) << 8 | (uint32_t)((M.pal1[num + 0x800] & 31) * 255 / 31));
}

static void color2(int num)
{
    M.palette[(num >> 1) + 0x100] = arc_rgb565((uint32_t)((M.pal2[num] & 31) * 255 / 31) << 16 |
                                               (uint32_t)((M.pal2[num + 0x400] & 31) * 255 / 31) << 8 |
                                               (uint32_t)((M.pal2[num + 0x800] & 31) * 255 / 31));
}

// ---------------------------------------------------------------------------
// The V30

static uint8_t main_read(void *u, uint32_t a)
{
    (void)u;
    a &= 0xfffff;
    if (a < PROG_SIZE) return M.rom[a];
    if (a >= 0xffff0) return M.rom[a & (PROG_SIZE - 1)];
    if (a >= 0xc0000 && a <= 0xc03ff) return M.spriteram[a & 0x3ff];
    if (a >= 0xc8000 && a <= 0xc8bff) return (a & 1) ? 0xff : (uint8_t)(M.pal1[(a & 0x1ff) + (a & 0xc00)] + 0xe0);
    if (a >= 0xd0000 && a <= 0xd3fff) return M.vram1[a & 0x3fff];
#if RTYPE2
    if (a >= 0xd4000 && a <= 0xd7fff) return M.vram2[a & 0x3fff];
    if (a >= 0xd8000 && a <= 0xd8bff) return (a & 1) ? 0xff : (uint8_t)(M.pal2[(a & 0x1ff) + (a & 0xc00)] + 0xe0);
    if (a >= 0xe0000 && a <= 0xe3fff) return M.ram[a & 0x3fff];
#else
    if (a >= (RTYPE ? 0x40000u : 0xa0000u) && a <= (RTYPE ? 0x43fffu : 0xa3fffu)) return M.ram[a & 0x3fff];
    if (a >= 0xcc000 && a <= 0xccbff) return (a & 1) ? 0xff : (uint8_t)(M.pal2[(a & 0x1ff) + (a & 0xc00)] + 0xe0);
    if (a >= 0xd8000 && a <= 0xdbfff) return M.vram2[a & 0x3fff];
#if RTYPE
    if (a >= 0xe0000 && a <= 0xeffff) return M.snd[a & 0xffff];
#endif
#endif
    return 0xff;
}

static void main_write(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    a &= 0xfffff;
#if RTYPE2
    if (a >= 0xb0000 && a <= 0xb0001) memcpy(M.sprite_buffer, M.spriteram, sizeof M.sprite_buffer);
    else if (a >= 0xbc000 && a <= 0xbc001) M.raster_pos = (uint16_t)(v + 64);
    else if (a >= 0xc0000 && a <= 0xc03ff) M.spriteram[a & 0x3ff] = v;
    else if (a >= 0xc8000 && a <= 0xc8bff) {
        M.pal1[(a & 0x1ff) + (a & 0xc00)] = v;
        color1(a & 0x1fe);
    } else if (a >= 0xd0000 && a <= 0xd3fff) M.vram1[a & 0x3fff] = v;
    else if (a >= 0xd4000 && a <= 0xd7fff) M.vram2[a & 0x3fff] = v;
    else if (a >= 0xd8000 && a <= 0xd8bff) {
        M.pal2[(a & 0x1ff) + (a & 0xc00)] = v;
        color2(a & 0x1fe);
    } else if (a >= 0xe0000 && a <= 0xe3fff) M.ram[a & 0x3fff] = v;
#else
    if (a < PROG_SIZE || a >= 0xffff0) return;
    const uint32_t ram_base = RTYPE ? 0x40000 : 0xa0000;
    if (a >= ram_base && a <= ram_base + 0x3fff) M.ram[a & 0x3fff] = v;
    else if (a >= 0xc0000 && a <= 0xc03ff) M.spriteram[a & 0x3ff] = v;
    else if (a >= 0xc8000 && a <= 0xc8bff) {
        M.pal1[(a & 0x1ff) + (a & 0xc00)] = v;
        color1(a & 0x1fe);
    } else if (a >= 0xcc000 && a <= 0xccbff) {
        M.pal2[(a & 0x1ff) + (a & 0xc00)] = v;
        color2(a & 0x1fe);
    } else if (a >= 0xd0000 && a <= 0xd3fff) M.vram1[a & 0x3fff] = v;
    else if (a >= 0xd8000 && a <= 0xdbfff) M.vram2[a & 0x3fff] = v;
#if RTYPE
    else if (a >= 0xe0000 && a <= 0xeffff) M.snd[a & 0xffff] = v;
#endif
#endif
}

#if RTYPE
static void set_sound_reset(bool held)
{
    if (held != M.sound_reset_held) zd_reset(&M.sound);
    M.sound_reset_held = held;
}
#endif

static void main_out16(void *u, uint32_t p, uint16_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0:
        M.soundlatch = (uint8_t)v;
        M.snd_irq_vector &= 0xdf;
        M.sound_irq_timer = true;
        break;
    case 2:
#if RTYPE
        set_sound_reset((v & 0x10) == 0);
#endif
        M.video_off = (v & 8) != 0;
        break;
#if !RTYPE2
    case 4:
        memcpy(M.sprite_buffer, M.spriteram, sizeof M.sprite_buffer);
        memset(M.spriteram, 0, sizeof M.spriteram);
        break;
    case 6: M.raster_pos = (uint16_t)(v - 128); break;
#endif
    case 0x40:
        M.irq_base[0] = (uint8_t)v;
        M.irq_pos = 1;
        break;
    case 0x42:
        if (M.irq_pos < 6) M.irq_base[M.irq_pos] = (uint8_t)v;
        M.irq_pos++;
        break;
    case 0x80: M.scroll_y1 = v; break;
    case 0x82: M.scroll_x1 = v; break;
    case 0x84: M.scroll_y2 = v; break;
    case 0x86: M.scroll_x2 = v; break;
    default: break;
    }
}

static uint16_t main_in16(void *u, uint32_t p)
{
    (void)u;
    switch (p & 0xff) {
#if RTYPE2
    case 0: return M.in0;
    case 2: return M.in1;
#else
    case 0: return (uint16_t)(0xff00 | (M.in0 & 0xff));
    case 2: return (uint16_t)(0xff00 | (M.in1 & 0xff));
#endif
    case 4: return M.dsw;
    default: return 0xffff;
    }
}

static uint8_t main_in8(void *u, uint16_t p) { const uint16_t w = main_in16(u, p); return (p & 1) ? (uint8_t)(w >> 8) : (uint8_t)w; }
static void main_out8(void *u, uint16_t p, uint8_t v) { main_out16(u, p, v); }

// ---------------------------------------------------------------------------
// Sound

static void sound_irq_ack(void)
{
    if (M.snd_irq_vector == 0xff) zd_set_irq(&M.sound, ZD_CLEAR, 0xff);
    else zd_set_irq(&M.sound, ZD_ASSERT, M.snd_irq_vector);
    M.sound_irq_timer = false;
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    if (M.sound_irq_timer) sound_irq_ack();
    ym2151_run_timers(&M.ym, c);
#if HAS_DAC
    M.nmi_counter += c;
    const int period = SOUND_CLOCK / (128 * 55);
    while (M.nmi_counter >= period) {
        M.nmi_counter -= period;
        zd_set_nmi(&M.sound, ZD_PULSE);
    }
#endif
    M.audio_acc += (int64_t)c * RATE;
    while (M.audio_acc >= SOUND_CLOCK) {
        M.audio_acc -= SOUND_CLOCK;
        int32_t s = ym2151_update(&M.ym);
#if HAS_DAC
        s += M.dac_sample / 2;
#endif
        if (M.naudio < 1024) M.audio[M.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on)
{
    (void)u;
    if (on) M.snd_irq_vector &= 0xef;
    else M.snd_irq_vector |= 0x10;
    M.sound_irq_timer = true;
}

static uint8_t sound_read(void *u, uint16_t a) { (void)u; return M.snd[a]; }
static void sound_write(void *u, uint16_t a, uint8_t v) { (void)u; M.snd[a] = v; }

static uint8_t sound_in(void *u, uint16_t p)
{
    (void)u;
    p &= 0xff;
    if (p == 1) return ym2151_status(&M.ym);
#if RTYPE
    if (p == 2) return M.soundlatch;
#else
    if (p == 0x80) return M.soundlatch;
    if (p == 0x84) return M.dac[M.sample_addr & 0x1ffff];
#endif
    return 0xff;
}

static void sound_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    p &= 0xff;
    if (p == 0) ym2151_select(&M.ym, v);
    else if (p == 1) ym2151_write(&M.ym, v);
#if RTYPE
    else if (p == 6) {
        M.snd_irq_vector |= 0x20;
        M.sound_irq_timer = true;
    }
#else
    else if (p == 0x80) M.sample_addr = ((((M.sample_addr >> 5) & 0xff00) | v) << 5);
    else if (p == 0x81) M.sample_addr = ((((M.sample_addr >> 5) & 0xff) | ((uint32_t)v << 8)) << 5);
    else if (p == 0x82) {
        M.dac_sample = (int16_t)((int8_t)v * 128);
        M.sample_addr = (M.sample_addr + 1) & 0x1ffff;
    } else if (p == 0x83) {
        M.snd_irq_vector |= 0x20;
        M.sound_irq_timer = true;
    }
#endif
}

// ---------------------------------------------------------------------------
// Video

// a layer's pixel at (x, y) of its 512x512: palette index, -1 transparent
static int layer_px(const uint8_t *vram, bool fg, bool hi, int x, int y)
{
    if (!M.have_layers) return (!fg && !hi) ? -2 : -1;   // never drawn: 0
    const int f = (y >> 3) * 64 + (x >> 3);
    const uint8_t a2 = vram[f * 4 + 2];
    int code;
    bool fx, fy, pri;
#if RTYPE2
    const uint8_t a = vram[f * 4 + 3];
    code = (vram[f * 4] | (vram[f * 4 + 1] << 8)) & CODE_MASK;
    fx = a2 & 0x20;
    fy = a2 & 0x40;
    pri = a & 1;
#else
    const uint8_t a = vram[f * 4 + 1];
    code = (vram[f * 4] | ((a & 0x3f) << 8)) & CODE_MASK;
    fx = a & 0x40;
    fy = a & 0x80;
    pri = a2 & 0x80;
#endif
    if (pri != hi) return (!fg && !hi) ? -2 : -1;   // cleared (black in the opaque one)
    const unsigned pen = arc_gfx_px(&kCharL, (fg || !RTYPE) ? M.chars0 : M.chars1, CHAR_ROM, code, fx ? 7 - (x & 7) : x & 7,
                                    fy ? 7 - (y & 7) : y & 7);
    if (pen == 0 && (fg || hi)) return -1;
    return ((((a2 & 0xf) << 4) + 256 + (int)pen) & 0x1ff);
}

static inline uint16_t layer_color(int i) { return i == -2 ? 0 : (i >= 256 ? M.pal_s[i - 256] : M.palette[i]); }

static void draw_sprites(int from, int to)
{
    for (int f = 0; f < 0x80; f++) {
        const uint8_t *s = &M.sprite_buffer[f * 8];
        const int code = (s[2] | (s[3] << 8)) & 0xfff;
        const uint16_t attr = (uint16_t)(s[4] | (s[5] << 8));
        const int color = (attr & 0xf) << 4;
        const int x = (s[6] | (s[7] << 8)) - 256;
        const bool fx = attr & 0x800, fy = attr & 0x400;
        const int w = 1 << ((attr >> 14) & 3), h = 1 << ((attr >> 12) & 3);
        const int y = 384 - (s[0] | (s[1] << 8)) - 16 * h;
        for (int wx = 0; wx < w; wx++)
            for (int wy = 0; wy < h; wy++) {
                const int c = (code + (fx ? 8 * (w - 1 - wx) : 8 * wx) + (fy ? h - 1 - wy : wy)) & 0xfff;
                const int dx = (x + 16 * wx) & 0x3ff, dy = (y + 16 * wy) & 0x1ff;
                for (int r = 0; r < 16; r++) {
                    const int py = (dy + r) & 0x1ff;
                    if (py < from || py > to) continue;
                    uint16_t *out = arc_fb_row(&M.fb, py);
                    for (int col = 0; col < 16; col++) {
                        const int px = ((dx + col) & 0x3ff) - 64;
                        if (px < 0 || px >= W) continue;
                        const unsigned pen = arc_gfx_px(&kSprL, M.sprites, 0x80000, c, fx ? 15 - col : col, fy ? 15 - r : r);
                        if (pen) out[px] = M.palette[(color + pen) & 0x1ff];
                    }
                }
            }
    }
}

// rows from-to of the picture: the low layers, the sprites, the high ones
static void paint_video(int from, int to)
{
    if (to >= H) to = H - 1;
    if (from < 0 || to < from) return;
#if RTYPE
    const int bg_x = M.scroll_x2, bg_y = M.scroll_y2 + 128, fg_x = M.scroll_x1, fg_y = M.scroll_y1 + 129;
#else
    const int bg_x = M.scroll_x2 - 6, bg_y = M.scroll_y2 + 128, fg_x = M.scroll_x1 - 4, fg_y = M.scroll_y1 + 128;
#endif
    for (int y = from; y <= to; y++) {
        uint16_t *out = arc_fb_row(&M.fb, y);
        const int by = (y + bg_y) & 511, fy = (y + fg_y) & 511;
        for (int x = 0; x < W; x++) {
            const int X = x + 64;
            int c = layer_px(M.vram2_s, false, false, (X + bg_x) & 511, by);
            uint16_t p = layer_color(c);
            c = layer_px(M.vram1_s, true, false, (X + fg_x) & 511, fy);
            if (c >= 0) p = layer_color(c);
            out[x] = p;
        }
    }
    draw_sprites(from, to);
    for (int y = from; y <= to; y++) {
        uint16_t *out = arc_fb_row(&M.fb, y);
        const int by = (y + bg_y) & 511, fy = (y + fg_y) & 511;
        for (int x = 0; x < W; x++) {
            const int X = x + 64;
            int c = layer_px(M.vram2_s, false, true, (X + bg_x) & 511, by);
            if (c >= 0) out[x] = layer_color(c);
            c = layer_px(M.vram1_s, true, true, (X + fg_x) & 511, fy);
            if (c >= 0) out[x] = layer_color(c);
        }
    }
}

// the layers redrawn: their RAM and colours as they are now
static void update_layers(void)
{
    memcpy(M.vram1_s, M.vram1, sizeof M.vram1_s);
    memcpy(M.vram2_s, M.vram2, sizeof M.vram2_s);
    memcpy(M.pal_s, M.palette + 256, sizeof M.pal_s);
    M.have_layers = true;
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&M.fb, y); }

bool arc_init(void)
{
    if (!(M_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&M.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    // the program: even and odd bytes from their ROMs
    const int n = (int)(sizeof kProg / sizeof kProg[0]);
    uint32_t key = PROG_SIZE;
    for (int i = 0; i < n; i++) key = key * 31 + kProg[i].crc + kProg[i].offset;
    arc_blob b;
    if (arc_blob_begin(&b, "program", PROG_SIZE, key)) {
        uint8_t *img = (uint8_t *)calloc(1, PROG_SIZE), *buf = (uint8_t *)malloc(0x20000);
        if (!img || !buf) return false;
        for (int i = 0; i < n; i++) {
            const arc_rom one = { kProg[i].name, kProg[i].size, 0, kProg[i].crc };
            if (!arc_load(&one, 1, buf, one.size)) return false;
            for (uint32_t k = 0; k < kProg[i].size && kProg[i].offset + k * 2 < PROG_SIZE; k++) img[kProg[i].offset + k * 2] = buf[k];
        }
        arc_blob_write(&b, 0, img, PROG_SIZE);
        free(img);
        free(buf);
    }
    if (!(M.rom = arc_blob_end(&b)) || !(M.chars0 = ARC_ROM_BLOB("chars0", kChars0, CHAR_ROM)) ||
        !(M.sprites = ARC_ROM_BLOB("sprites", kSprites, 0x80000)))
        return false;
#if RTYPE
    if (!(M.chars1 = ARC_ROM_BLOB("chars1", kChars1, CHAR_ROM))) return false;
#else
    if (!ARC_LOAD(kSound, M.snd, 0x10000) || !(M.dac = ARC_ROM_BLOB("dac", kDac, 0x20000))) return false;
#endif
    M.main.read = main_read;
    M.main.write = main_write;
    M.main.in8 = main_in8;
    M.main.out8 = main_out8;
    M.main.in16 = main_in16;
    M.main.out16 = main_out16;
    nec_v30_init(&M.main, NEC_V30);
    M.sound.read = sound_read;
    M.sound.write = sound_write;
    M.sound.in = sound_in;
    M.sound.out = sound_out;
    M.sound.cycles = sound_cycles;
    zd_init(&M.sound);
    ym2151_init(&M.ym, SOUND_CLOCK, RATE);
    M.ym.irq_handler = ym_irq;
    M.dsw = DSW;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    nec_v30_reset(&M.main);
    zd_reset(&M.sound);
    ym2151_reset(&M.ym);
    M.in0 = M.in1 = 0xffff;
    M.scroll_x1 = M.scroll_x2 = M.scroll_y1 = M.scroll_y2 = 0;
    M.snd_irq_vector = 0xff;
    M.soundlatch = 0;
    M.raster_pos = 0;
    M.video_off = true;
    M.sample_addr = 0;
    memset(M.irq_base, 0, sizeof M.irq_base);
    M.irq_pos = 0;
    M.sound_irq_timer = M.sound_reset_held = false;
    M.dac_sample = 0;
    M.nmi_counter = 0;
    M.audio_acc = 0;
    M.have_layers = false;
    for (int y = 0; y < H; y++) memset(arc_fb_row(&M.fb, y), 0, W * 2);
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)(MAIN_CLOCK / FPS / SCANLINES), sc = (int)(SOUND_CLOCK / FPS / SCANLINES);
    M.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        nec_v30_run(&M.main, mc);
        if (!M.sound_reset_held) zd_run(&M.sound, sc);
        else if (HAS_DAC) sound_cycles(NULL, sc);
        if (line < 255 && line == (int)M.raster_pos - 1) {
            nec_v30_set_irq(&M.main, 2, (uint8_t)(M.irq_base[1] + 2));
            if (!M.video_off) paint_video(0, line);
        }
        if (line == 255) {
            nec_v30_set_irq(&M.main, 2, M.irq_base[1]);
            if (!M.video_off) {
                paint_video(M.raster_pos & 0xff, line);
                update_layers();
            } else
                for (int y = 0; y < H; y++) memset(arc_fb_row(&M.fb, y), 0, W * 2);
        }
    }
    if (audio) *audio = M.audio;
    return M.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&M.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0];
    uint16_t a = 0xffff, b = 0xffff;
    if (p1 & AP_RIGHT) a &= ~0x0001;
    if (p1 & AP_LEFT) a &= ~0x0002;
    if (p1 & AP_DOWN) a &= ~0x0004;
    if (p1 & AP_UP) a &= ~0x0008;
    if (p1 & AP_B3) a &= ~0x0020;
    if (p1 & AP_B2) a &= ~0x0040;
    if (p1 & AP_B1) a &= ~0x0080;
    if (p1 & AP_START) b &= ~0x0001;
    if (in->p[1] & AP_START) b &= ~0x0002;
    if (in->coin1) b &= ~0x0004;
    if (in->coin2) b &= ~0x0008;
    M.in0 = a;
    M.in1 = b;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) M.dsw = (uint16_t)((M.dsw & 0xff00) | v);
    else if (bank == 1) M.dsw = (uint16_t)((M.dsw & 0x00ff) | (v << 8));
}
