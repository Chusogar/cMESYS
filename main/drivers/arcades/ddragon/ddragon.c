// ddragon.c - Double Dragon (Technos Japan, 1987) and Double Dragon II (1988;
// ARC_VARIANT 1) for the common arcade frontends (arc.h): C port of
// dsp-cpp's drivers/arcade/doubledragon (from dsp-emulator's
// doubledragon_hw.pas). An HD6309 (as a 6809) with 6 banks of 16 KB, a sub
// CPU behind 512 bytes of shared RAM (Double Dragon: an HD63701 with its own
// ROM; II: a Z80), a sound CPU with a YM2151 (Double Dragon: a 6809 and two
// MSM5205; II: a Z80 and an OKI M6295), a scrolling 512x512 background of
// 16x16 tiles, 16x16 sprites in up to 2x2 blocks and 8x8 text. The graphics
// are read straight from their ROMs and the picture built in place. The OKI
// runs at its own rate (dsp-cpp clocked it at the output rate).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/hd63701dsp/hd63701dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/msm5205dsp.h"
#include "../../../sound/okim6295dsp.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define DD2 (ARC_VARIANT == 1)
#define MAIN_CLOCK (12000000 / 4)
#define SUB_CLOCK (DD2 ? 4000000 : 6000000 / 4)
#define SOUND_CLOCK (DD2 ? 3579545 : 1500000)
#define YM_CLOCK 3579545
#define MSM_CLOCK 375000
#define OKI_CLOCK 1056000
#define FPS (6000000.0 / 384.0 / 272.0)
#define SCANLINES 272
#define CPU_SYNC 4
#define RATE 44100
#define W 256
#define H 240

#if !DD2
static const char *const kSets[] = { "ddragon", NULL };
static const arc_rom kMain[] = {
    {"21j-1-5.26", 0x8000, 0x00000, 0x42045dfd}, {"21j-2-3.25", 0x8000, 0x08000, 0x5779705e},
    {"21j-3.24", 0x8000, 0x10000, 0x3bdea613}, {"21j-4-1.23", 0x8000, 0x18000, 0x728f87b9}};
static const arc_rom kSub[] = {{"21jm-0.ic55", 0x4000, 0, 0xf5232d03}};
static const arc_rom kSound[] = {{"21j-0-1", 0x8000, 0x8000, 0x9efa95bb}};
static const arc_rom kChars[] = {{"21j-5", 0x8000, 0, 0x7a8b8db4}};
static const arc_rom kTiles[] = {
    {"21j-8", 0x10000, 0x00000, 0x7c435887}, {"21j-9", 0x10000, 0x10000, 0xc6640aed},
    {"21j-i", 0x10000, 0x20000, 0x5effb0a0}, {"21j-j", 0x10000, 0x30000, 0x5fb42e7c}};
static const arc_rom kSprites[] = {
    {"21j-a", 0x10000, 0x00000, 0x574face3}, {"21j-b", 0x10000, 0x10000, 0x40507a76},
    {"21j-c", 0x10000, 0x20000, 0xbb0bc76f}, {"21j-d", 0x10000, 0x30000, 0xcb4f231b},
    {"21j-e", 0x10000, 0x40000, 0xa0a0c261}, {"21j-f", 0x10000, 0x50000, 0x6ba152f6},
    {"21j-g", 0x10000, 0x60000, 0x3220a0b6}, {"21j-h", 0x10000, 0x70000, 0x65c7517d}};
static const arc_rom kAdpcm[] = {{"21j-6", 0x10000, 0x00000, 0x34755de3}, {"21j-7", 0x10000, 0x10000, 0x904de6f8}};
#define CHARS_SIZE 0x8000
#define SPRITES_SIZE 0x80000
#define ADPCM_SIZE 0x20000
#define SOUND_SIZE 0x10000
const arc_info arc_game = { "ddragon", "Double Dragon", "Technos Japan", 1987, W, H, FPS, RATE, kSets,
                            { "PUNCH", "KICK", "JUMP", NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "ddragon2", NULL };
static const arc_rom kMain[] = {
    {"26a9-04.bin", 0x8000, 0x00000, 0xf2cfc649}, {"26aa-03.bin", 0x8000, 0x08000, 0x44dd5d4b},
    {"26ab-0.bin", 0x8000, 0x10000, 0x49ddddcd}, {"26ac-0e.63", 0x8000, 0x18000, 0x57acad2c}};
static const arc_rom kSub[] = {{"26ae-0.bin", 0x10000, 0, 0xea437867}};
static const arc_rom kSound[] = {{"26ad-0.bin", 0x8000, 0, 0x75e36cd6}};
static const arc_rom kChars[] = {{"26a8-0e.19", 0x10000, 0, 0x4e80cd36}};
static const arc_rom kTiles[] = {{"26j4-0.bin", 0x20000, 0x00000, 0xa8c93e76}, {"26j5-0.bin", 0x20000, 0x20000, 0xee555237}};
static const arc_rom kSprites[] = {
    {"26j0-0.bin", 0x20000, 0x00000, 0xdb309c84}, {"26j1-0.bin", 0x20000, 0x20000, 0xc3081e0c},
    {"26af-0.bin", 0x20000, 0x40000, 0x3a615aad}, {"26j2-0.bin", 0x20000, 0x60000, 0x589564ae},
    {"26j3-0.bin", 0x20000, 0x80000, 0xdaf040d6}, {"26a10-0.bin", 0x20000, 0xa0000, 0x6d16d889}};
static const arc_rom kAdpcm[] = {{"26j6-0.bin", 0x20000, 0x00000, 0xa84b2a29}, {"26j7-0.bin", 0x20000, 0x20000, 0xbc6a48d5}};
#define CHARS_SIZE 0x10000
#define SPRITES_SIZE 0xc0000
#define ADPCM_SIZE 0x40000
#define SOUND_SIZE 0x8000
const arc_info arc_game = { "ddragon2", "Double Dragon II: The Revenge", "Technos Japan", 1988, W, H, FPS, RATE, kSets,
                            { "PUNCH", "KICK", "JUMP", NULL }, ARC_JOY_8WAY };
#endif

static const int kCharX[8] = {1, 0, 65, 64, 129, 128, 193, 192};
static const int kTX[16] = {3, 2, 1, 0, 131, 130, 129, 128, 259, 258, 257, 256, 387, 386, 385, 384};
static const int kTY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
static const arc_layout kCharL = { 8, 8, CHARS_SIZE / 32, 4, 256, false, false, false, {0, 2, 4, 6}, kCharX, kTY };
static const arc_layout kTileL = { 16, 16, 0x800, 4, 512, false, false, false, {0x20000 * 8, 0x20000 * 8 + 4, 0, 4}, kTX, kTY };
static const arc_layout kSprL = { 16, 16, SPRITES_SIZE / 128, 4, 512, false, false, false,
                                  {SPRITES_SIZE / 2 * 8, SPRITES_SIZE / 2 * 8 + 4, 0, 4}, kTX, kTY };

// the machine on the heap
static struct state {
    m6809d main;
#if DD2
    zd_z80 sub, sound;
    okim6295d oki;
    const uint8_t *sub_rom;
    uint8_t sub_ram[0x400];             // c000-c3ff (shared at its start)
#else
    hd63701 sub;
    m6809d sound;
    msm5205d msm0, msm1;
    uint8_t shared_ram[0x200];
#endif
    ym2151 ym;
    const uint8_t *rom, *chars, *tiles, *sprites, *adpcm;   // blobs (the CYD: in the flash)
    uint8_t ram[0x4000], pal_ram[0x400];
    uint16_t palette[0x200];
    uint8_t sound_rom[SOUND_SIZE], sound_ram[0x1000];
    arc_fb fb;
    uint8_t in0, in1, in2, dsw_a, dsw_b, soundlatch, rom_bank, sub_port;
    uint16_t scroll_x, scroll_y;
    bool flip, sub_halt, sub_reset;
    int64_t audio_acc, msm_acc, oki_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
} *D_;
#define D (*D_)

#define SUB_STOPPED (D.sub_halt || D.sub_reset)
#define PAL_BASE (DD2 ? 0x3c00 : 0x1000)

static void set_color(int i)
{
    const uint8_t lo = D.pal_ram[i], hi = D.pal_ram[i + 0x200];
    D.palette[i] = arc_rgb565((uint32_t)((lo & 15) * 0x11) << 16 | (uint32_t)((lo >> 4) * 0x11) << 8 | (uint32_t)((hi & 15) * 0x11));
}

static void sound_irq(void)
{
#if DD2
    zd_set_nmi(&D.sound, ZD_ASSERT);
#else
    m6809d_set_irq(&D.sound, M6809D_ASSERT);
#endif
}

static void sub_nmi(void)
{
#if DD2
    zd_set_nmi(&D.sub, ZD_ASSERT);
#else
    hd63701_set_nmi(&D.sub, HD_ASSERT);
#endif
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a >= PAL_BASE && a < PAL_BASE + 0x400) return D.pal_ram[a & 0x3ff];
    if (a >= 0x2000 && a <= 0x27ff) {
        if (!SUB_STOPPED) return 0xff;
#if DD2
        return D.sub_ram[a & 0x1ff];
#else
        return D.shared_ram[a & 0x1ff];
#endif
    }
    if (a >= 0x3800 && a <= 0x380f) switch (a) {
        case 0x3800: return D.in0;
        case 0x3801: return D.in1;
        case 0x3802: return (uint8_t)(D.in2 | (SUB_STOPPED ? 0 : 0x10));
        case 0x3803: return D.dsw_a;
        case 0x3804: return D.dsw_b;
        case 0x380b: m6809d_set_nmi(&D.main, M6809D_CLEAR); return 0xff;
        case 0x380c: m6809d_set_firq(&D.main, M6809D_CLEAR); return 0xff;
        case 0x380d: m6809d_set_irq(&D.main, M6809D_CLEAR); return 0xff;
        case 0x380e: sound_irq(); return D.soundlatch;
        case 0x380f: sub_nmi(); return 0xff;
        default: return 0xff;
        }
    if (a >= 0x8000) return D.rom[a - 0x8000];
    if (a >= 0x4000) return D.rom[0x8000 + (D.rom_bank % 6) * 0x4000 + (a & 0x3fff)];
    return D.ram[a];
}

static void write_control(uint8_t v)
{
    D.scroll_x = (uint16_t)((D.scroll_x & 0xff) | ((v & 1) << 8));
    D.scroll_y = (uint16_t)((D.scroll_y & 0xff) | ((v & 2) << 7));
    D.flip = (v & 4) == 0;
    const bool reset_line = (v & 8) == 0, halt_line = (v & 0x10) != 0;
#if DD2
    if (D.sub_reset && !reset_line) zd_reset(&D.sub);
#else
    hd63701_set_reset(&D.sub, reset_line ? HD_ASSERT : HD_CLEAR);
    hd63701_set_halt(&D.sub, halt_line ? HD_ASSERT : HD_CLEAR);
#endif
    D.sub_reset = reset_line;
    D.sub_halt = halt_line;
    D.rom_bank = (uint8_t)((v & 0xe0) >> 5);
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= PAL_BASE && a < PAL_BASE + 0x400) {
        D.pal_ram[a & 0x3ff] = v;
        set_color(a & 0x1ff);
        return;
    }
    if (a >= 0x2000 && a <= 0x27ff) {
        if (!SUB_STOPPED) return;
#if DD2
        D.sub_ram[a & 0x1ff] = v;
#else
        D.shared_ram[a & 0x1ff] = v;
#endif
        return;
    }
    if (a >= 0x3800 && a <= 0x380f) {
        switch (a) {
        case 0x3808: write_control(v); break;
        case 0x3809: D.scroll_x = (uint16_t)((D.scroll_x & 0x100) | v); break;
        case 0x380a: D.scroll_y = (uint16_t)((D.scroll_y & 0x100) | v); break;
        case 0x380b: m6809d_set_nmi(&D.main, M6809D_CLEAR); break;
        case 0x380c: m6809d_set_firq(&D.main, M6809D_CLEAR); break;
        case 0x380d: m6809d_set_irq(&D.main, M6809D_CLEAR); break;
        case 0x380e: D.soundlatch = v; sound_irq(); break;
        case 0x380f: sub_nmi(); break;
        default: break;
        }
        return;
    }
    if (a >= 0x4000) return;
    D.ram[a] = v;
}

static void sound_cycles_common(int c, int ym_cycles)
{
    ym2151_run_timers(&D.ym, ym_cycles);
#if DD2
    D.oki_acc += (int64_t)c * okid_sample_frequency(&D.oki);
    while (D.oki_acc >= SOUND_CLOCK) {
        D.oki_acc -= SOUND_CLOCK;
        D.last_oki = okid_update(&D.oki);
    }
#else
    D.msm_acc += (int64_t)c * msm5205d_sample_frequency(&D.msm0);
    while (D.msm_acc >= SOUND_CLOCK) {
        D.msm_acc -= SOUND_CLOCK;
        msm5205d_vclk(&D.msm0);
        msm5205d_vclk(&D.msm1);
    }
#endif
    D.audio_acc += (int64_t)c * RATE;
    while (D.audio_acc >= SOUND_CLOCK) {
        D.audio_acc -= SOUND_CLOCK;
#if DD2
        const int32_t s = ym2151_update(&D.ym) + D.last_oki / 2;
#else
        const int32_t s = ym2151_update(&D.ym) + (msm5205d_output(&D.msm0) + msm5205d_output(&D.msm1)) * 2 / 5;
#endif
        if (D.naudio < 1024) D.audio[D.naudio++] = arc_clamp16(s);
    }
}

#if DD2
static uint8_t sub_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0xc000) return D.sub_rom[a];
    if (a < 0xc400) return D.sub_ram[a & 0x3ff];
    return 0xff;
}

static void sub_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xc000 && a <= 0xc3ff) D.sub_ram[a & 0x3ff] = v;
    else if (a == 0xd000) zd_set_nmi(&D.sub, ZD_CLEAR);
    else if (a == 0xe000) m6809d_set_irq(&D.main, M6809D_ASSERT);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return D.sound_rom[a];
    if (a <= 0x87ff) return D.sound_ram[a & 0x7ff];
    switch (a) {
    case 0x8801: return ym2151_status(&D.ym);
    case 0x9800: return okid_read(&D.oki);
    case 0xa000:
        zd_set_nmi(&D.sound, ZD_CLEAR);
        return D.soundlatch;
    default: return 0xff;
    }
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x8000 && a <= 0x87ff) D.sound_ram[a & 0x7ff] = v;
    else if (a == 0x8800) ym2151_select(&D.ym, v);
    else if (a == 0x8801) ym2151_write(&D.ym, v);
    else if (a == 0x9800) okid_write(&D.oki, v);
}

static void sound_cycles(void *u, int c) { (void)u; sound_cycles_common(c, c); }
static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&D.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }
#else
static uint8_t sub_read(void *u, uint16_t a) { (void)u; return a >= 0x8000 && a <= 0x81ff ? D.shared_ram[a & 0x1ff] : 0xff; }
static void sub_write(void *u, uint16_t a, uint8_t v) { (void)u; if (a >= 0x8000 && a <= 0x81ff) D.shared_ram[a & 0x1ff] = v; }
// port 6: the main CPU's IRQ, and the acknowledge of the sub's NMI
static void sub_port6(void *u, uint8_t v)
{
    (void)u;
    if (!(v & 1)) hd63701_set_nmi(&D.sub, HD_CLEAR);
    if ((v & 2) && (D.sub_port & 2)) m6809d_set_irq(&D.main, M6809D_ASSERT);
    D.sub_port = v;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x0fff) return D.sound_ram[a];
    if (a >= 0x8000) return D.sound_rom[a];
    switch (a) {
    case 0x1000:
        m6809d_set_irq(&D.sound, M6809D_CLEAR);
        return D.soundlatch;
    case 0x1800: return (uint8_t)(D.msm0.idle | (D.msm1.idle << 1));
    case 0x2801: return ym2151_status(&D.ym);
    default: return 0xff;
    }
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x0fff) { D.sound_ram[a] = v; return; }
    if (a == 0x2800) ym2151_select(&D.ym, v);
    else if (a == 0x2801) ym2151_write(&D.ym, v);
    else if (a >= 0x3800 && a <= 0x3807) switch (a & 7) {
        case 0: msm5205d_set_reset(&D.msm0, false); break;
        case 1: msm5205d_set_reset(&D.msm1, false); break;
        case 2: D.msm0.end = (uint32_t)(v & 0x7f) * 0x200; break;
        case 3: D.msm1.end = (uint32_t)(v & 0x7f) * 0x200; break;
        case 4: D.msm0.position = (uint32_t)(v & 0x7f) * 0x200; break;
        case 5: D.msm1.position = (uint32_t)(v & 0x7f) * 0x200; break;
        case 6: msm5205d_set_reset(&D.msm0, true); break;
        case 7: msm5205d_set_reset(&D.msm1, true); break;
        }
}

// (the YM2151 on its own oscillator, faster than the 6809)
static void sound_cycles(void *u, int c) { (void)u; sound_cycles_common(c, (int)((int64_t)c * YM_CLOCK / SOUND_CLOCK)); }
static void ym_irq(void *u, bool on) { (void)u; m6809d_set_firq(&D.sound, on ? M6809D_ASSERT : M6809D_CLEAR); }
#endif

// the screen is 256 lines from which the picture takes 8-247
static inline uint16_t *screen_row(int y) { return y >= 8 && y < 8 + H ? arc_fb_row(&D.fb, y - 8) : NULL; }

static void sprite_element(int code, int color, bool fx, bool fy, int px, int py)
{
    for (int r = 0; r < 16; r++) {
        uint16_t *out = screen_row(py + r);
        if (!out) continue;
        for (int c = 0; c < 16; c++) {
            const int x = px + c;
            if (x < 0 || x >= 256) continue;
            const unsigned pen = arc_gfx_px(&kSprL, D.sprites, SPRITES_SIZE, code, fx ? 15 - c : c, fy ? 15 - r : r);
            if (pen) out[x] = D.palette[color + pen];
        }
    }
}

static void update_video(void)
{
    for (int y = 8; y < 8 + H; y++) {   // the background
        uint16_t *out = screen_row(y);
        const int wy = (y + D.scroll_y) & 0x1ff, ty = wy >> 4;
        for (int x = 0; x < 256; x++) {
            const int wx = (x + D.scroll_x) & 0x1ff, tx = wx >> 4;
            const int pos = (tx & 0x0f) + ((ty & 0x0f) << 4) + ((tx & 0x10) << 4) + ((ty & 0x10) << 5);
            const uint8_t attr = D.ram[0x3000 + pos * 2];
            const int code = D.ram[0x3001 + pos * 2] + ((attr & 7) << 8);
            const int sx = (attr & 0x40) ? 15 - (wx & 15) : wx & 15, sy = (attr & 0x80) ? 15 - (wy & 15) : wy & 15;
            out[x] = D.palette[0x100 + (((attr & 0x38) >> 3) << 4) + arc_gfx_px(&kTileL, D.tiles, 0x40000, code, sx, sy)];
        }
    }
    for (int i = 0; i < 0x40; i++) {
        const uint8_t *e = &D.ram[0x2800 + i * 5];
        const uint8_t attr = e[1];
        if (!(attr & 0x80)) continue;
        const int x = 240 - e[4] + ((attr & 2) << 7), y = 240 - e[0] + ((attr & 1) << 8);
        const int size = (attr & 0x30) >> 4;
        const bool fx = attr & 8, fy = attr & 4;
#if DD2
        const int color = ((e[2] >> 5) << 4) + 0x80;
        const int code = (e[3] + ((e[2] & 0x1f) << 8)) & ~size;
#else
        const int color = (((e[2] >> 4) & 7) << 4) + 0x80;
        const int code = (e[3] + ((e[2] & 0x0f) << 8)) & ~size;
#endif
        switch (size) {
        case 1:
            sprite_element(code, color, fx, fy, x, y - 16);
            sprite_element(code + 1, color, fx, fy, x, y);
            break;
        case 2:
            sprite_element(code, color, fx, fy, x - 16, y);
            sprite_element(code + 1, color, fx, fy, x, y);
            break;
        case 3:
            sprite_element(code, color, fx, fy, x - 16, y - 16);
            sprite_element(code + 1, color, fx, fy, x, y - 16);
            sprite_element(code + 2, color, fx, fy, x - 16, y);
            sprite_element(code + 3, color, fx, fy, x, y);
            break;
        default: sprite_element(code, color, fx, fy, x, y); break;
        }
    }
    for (int y = 8; y < 8 + H; y++) {   // the text
        uint16_t *out = screen_row(y);
        for (int x = 0; x < 256; x++) {
            const int i = (y >> 3) * 32 + (x >> 3);
            const uint8_t attr = D.ram[0x1800 + i * 2];
            const unsigned pen = arc_gfx_px(&kCharL, D.chars, CHARS_SIZE, D.ram[0x1801 + i * 2] + ((attr & 7) << 8), x & 7, y & 7);
            if (pen) out[x] = D.palette[(((attr & 0xe0) >> 5) << 4) + pen];
        }
    }
    if (D.flip)
        for (int y = 0; y < H / 2; y++) {
            uint16_t *a = arc_fb_row(&D.fb, y), *b = arc_fb_row(&D.fb, H - 1 - y);
            for (int x = 0; x < W; x++) {
                const uint16_t t = a[x];
                a[x] = b[W - 1 - x];
                b[W - 1 - x] = t;
            }
        }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&D.fb, y); }

bool arc_init(void)
{
    if (!(D_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&D.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    // 8000-ffff, then the six banks of 4000-7fff
    if (!(D.rom = ARC_ROM_BLOB("main", kMain, 0x20000)) || !ARC_LOAD(kSound, D.sound_rom, SOUND_SIZE) ||
        !(D.chars = ARC_ROM_BLOB("chars", kChars, CHARS_SIZE)) || !(D.tiles = ARC_ROM_BLOB("tiles", kTiles, 0x40000)) ||
        !(D.sprites = ARC_ROM_BLOB("sprites", kSprites, SPRITES_SIZE)) || !(D.adpcm = ARC_ROM_BLOB("adpcm", kAdpcm, ADPCM_SIZE)))
        return false;
    D.main.read = main_read;
    D.main.write = main_write;
#if DD2
    if (!(D.sub_rom = ARC_ROM_BLOB("sub", kSub, 0x10000))) return false;
    D.sub.read = sub_read;
    D.sub.write = sub_write;
    D.sub.in = D.sound.in = in_ff;
    D.sub.out = D.sound.out = out_nop;
    D.sound.read = sound_read;
    D.sound.write = sound_write;
    D.sound.cycles = sound_cycles;
    zd_init(&D.sub);
    zd_init(&D.sound);
    okid_init(&D.oki, OKI_CLOCK, true, D.adpcm, ADPCM_SIZE);
    D.dsw_b = 0x96;
#else
    hd63701_init(&D.sub, SUB_CLOCK, HD_HD63701Y);
    if (!ARC_LOAD(kSub, D.sub.rom, 0x4000)) return false;
    D.sub.read_mem = sub_read;
    D.sub.write_mem = sub_write;
    D.sub.portx_write[1] = sub_port6;
    D.sound.read = sound_read;
    D.sound.write = sound_write;
    D.sound.cycles = sound_cycles;
    msm5205d_init(&D.msm0, MSM_CLOCK, 48, 4);
    msm5205d_init(&D.msm1, MSM_CLOCK, 48, 4);
    D.msm0.rom = D.adpcm;
    D.msm1.rom = D.adpcm + 0x10000;
    D.msm0.rom_size = D.msm1.rom_size = 0x10000;
    D.dsw_b = 0xff;
#endif
    ym2151_init(&D.ym, YM_CLOCK, RATE);
    D.ym.irq_handler = ym_irq;
    D.dsw_a = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&D.main);
    ym2151_reset(&D.ym);
#if DD2
    zd_reset(&D.sub);
    zd_reset(&D.sound);
    okid_reset(&D.oki);
#else
    hd63701_reset(&D.sub);
    m6809d_reset(&D.sound);
    msm5205d_reset(&D.msm0);
    msm5205d_reset(&D.msm1);
#endif
    D.in0 = D.in1 = 0xff;
    D.in2 = 0xe7;
    D.soundlatch = D.rom_bank = D.sub_port = 0;
    D.scroll_x = D.scroll_y = 0;
    D.flip = D.sub_halt = D.sub_reset = false;
    D.audio_acc = D.msm_acc = D.oki_acc = 0;
    D.last_oki = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int ms = (int)(MAIN_CLOCK / FPS / (SCANLINES * CPU_SYNC)), ss = (int)(SUB_CLOCK / FPS / (SCANLINES * CPU_SYNC));
    const int so = (int)(SOUND_CLOCK / FPS / (SCANLINES * CPU_SYNC));
    D.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 8) D.in2 &= 0xf7;
        if (line >= 16 && line <= 240 && line % 16 == 0) m6809d_set_firq(&D.main, M6809D_ASSERT);
        if (line == 248) {
            D.in2 |= 0x08;
            m6809d_set_nmi(&D.main, M6809D_ASSERT);
            update_video();
        }
        if (line == 264) m6809d_set_firq(&D.main, M6809D_ASSERT);
        for (int s = 0; s < CPU_SYNC; s++) {
            m6809d_run(&D.main, ms);
#if DD2
            if (!SUB_STOPPED) zd_run(&D.sub, ss);
            zd_run(&D.sound, so);
#else
            hd63701_run(&D.sub, ss);
            m6809d_run(&D.sound, so);
#endif
        }
    }
    if (audio) *audio = D.audio;
    return D.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&D.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0xff, b = 0xff, c = (uint8_t)(0xe7 | (D.in2 & 0x08));
    if (p1 & AP_RIGHT) a &= 0xfe;
    if (p1 & AP_LEFT) a &= 0xfd;
    if (p1 & AP_UP) a &= 0xfb;
    if (p1 & AP_DOWN) a &= 0xf7;
    if (p1 & AP_B1) a &= 0xef;
    if (p1 & AP_B2) a &= 0xdf;
    if (p1 & AP_START) a &= 0xbf;
    if (p2 & AP_START) a &= 0x7f;
    if (p1 & AP_B3) c &= 0xfd;
    if (p2 & AP_B3) c &= 0xfb;
    if (p2 & AP_RIGHT) b &= 0xfe;
    if (p2 & AP_LEFT) b &= 0xfd;
    if (p2 & AP_UP) b &= 0xfb;
    if (p2 & AP_DOWN) b &= 0xf7;
    if (p2 & AP_B1) b &= 0xef;
    if (p2 & AP_B2) b &= 0xdf;
    if (in->coin1) b &= 0xbf;
    if (in->coin2) b &= 0x7f;
    D.in0 = a;
    D.in1 = b;
    D.in2 = c;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) D.dsw_a = v;
    else if (bank == 1) D.dsw_b = v;
}
