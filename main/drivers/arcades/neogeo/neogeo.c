// neogeo.c - SNK Neo Geo MVS for the common arcade frontends (arc.h): C port
// of dsp-cpp's drivers/arcade/neogeo and video/neogeo_video. A 68000 at
// 12 MHz, a Z80 at 4 MHz with the YM2610 (sound/ym2610d), the LSPC2-A2
// sprites (381 chained, shrunk through the L0 ROM) and the fix layer, the
// uPD4990A calendar as the BIOS self-test needs it. The cart is any MAME
// split set (no encryption): its ROMs are told apart by their names
// (p1 / s1 / m1 / c1..c16 / v1..v4) as dsp-cpp does, the BIOS, SFIX, SM1 and
// L0 come from neogeo.zip.
// ARC_VARIANT: 0 mslug, 1 mslug2, 2 kof94.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2610d.h"
#include "../../../rom/romfs.h"

#define W 320
#define H 224
#define MAIN_CLOCK 12000000
#define Z80_CLOCK 4000000
#define YM_CLOCK 8000000
#define FPS 59.185606
#define SCANLINES 264
#define VBLANK_LINE 240
#define VISIBLE_TOP 16
#define SPRITE_COUNT 381
#define SPRITES_PER_LINE 96
#define RATE 44100

#if ARC_VARIANT == 0
static const char *const kSets[] = { "mslug", "neogeo", NULL };
const arc_info arc_game = { "mslug", "Metal Slug", "Nazca / SNK", 1996, W, H, FPS, RATE, kSets, { "DISPARO", "SALTO", "GRANADA", NULL }, ARC_JOY_8WAY };
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "mslug2", "neogeo", NULL };
const arc_info arc_game = { "mslug2", "Metal Slug 2", "SNK", 1998, W, H, FPS, RATE, kSets, { "DISPARO", "SALTO", "GRANADA", NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "kof94", "neogeo", NULL };
const arc_info arc_game = { "kof94", "The King of Fighters '94", "SNK", 1994, W, H, FPS, RATE, kSets, { "PUNO D.", "PATADA D.", "PUNO F.", "PATADA F." }, ARC_JOY_8WAY };
#endif

typedef struct { uint8_t *p; size_t n; } buf;

static struct state {
    m68kd cpu;
    zd_z80 z80;
    ym2610d ym;
    buf bios, sfix, sm1, lo, p_rom, s_rom, m_rom, c_rom, v_rom;
    uint8_t ram[0x10000], sram[0x10000], z80_ram[0x2000], memcard[0x800];
    uint32_t cart_bank;
    bool bios_present, bios_vectors, sram_unlocked, sound_nmi_enabled;
    uint8_t sound_latch, sound_reply, z80_bank, dsw;
    int watchdog;
    unsigned p1, p2;
    bool coin1, coin2, service;
    uint32_t rtc_shiftlo;
    uint8_t rtc_command;
    int rtc_bitno;
    uint8_t rtc_ctrl;
    bool rtc_pulse, rtc_clock, rtc_strobe, rtc_reading, rtc_data_bit;
    uint8_t rtc_cmd_shift;
    int rtc_tp_interval, rtc_tp_counter;
    int64_t audio_acc;
    int16_t audio[4096];
    int naudio;
    // video (LSPC2-A2)
    uint16_t vram[0x10000], palette[2][0x1000];
    uint32_t palette_rgb[2][0x1000];
    uint16_t vram_addr, vram_mod, vram_read_buffer, lspc_mode;
    uint32_t timer_reload, timer_counter;
    bool timer_enable, irq_vblank, irq_timer, irq_reset, use_bios_fix;
    int timer_mode, palette_bank, auto_anim, auto_anim_speed, auto_anim_frame, scanline;
    uint8_t *fix_pixels, *bios_fix_pixels, *sprite_pixels;
    int cart_fix_tiles, bios_fix_tiles, sprite_tiles;
    uint32_t frame[W * H];
    uint16_t fb[W * H];
} *S_;
#define S (*S_)

// ---------------------------------------------------------------------------
// Video

static const uint16_t kZoomX[16] = {0x0080, 0x0880, 0x0888, 0x2888, 0x288a, 0x2a8a, 0x2aaa, 0xaaaa,
                                    0xaaea, 0xbaea, 0xbaeb, 0xbbeb, 0xbbef, 0xfbef, 0xfbff, 0xffff};

static uint8_t *decode_fix(const uint8_t *src, size_t size, int *tiles_out)
{
    static const int kColBase[4] = {0x10, 0x18, 0x00, 0x08};
    const int tiles = (int)(size / 32);
    uint8_t *dest = (uint8_t *)calloc((size_t)(tiles ? tiles : 1), 64);
    for (int t = 0; dest && t < tiles; t++) {
        const uint8_t *tile = src + (size_t)t * 32;
        uint8_t *out = dest + (size_t)t * 64;
        for (int y = 0; y < 8; y++)
            for (int c = 0; c < 4; c++) {
                const uint8_t d = tile[kColBase[c] + y];
                out[y * 8 + c * 2] = d & 0x0f;
                out[y * 8 + c * 2 + 1] = (d >> 4) & 0x0f;
            }
    }
    *tiles_out = tiles;
    return dest;
}

static uint8_t *decode_sprites(const uint8_t *src, size_t size, int *tiles_out)
{
    const int tiles = (int)(size / 128);
    uint8_t *dest = (uint8_t *)calloc((size_t)(tiles ? tiles : 1), 256);
    for (int t = 0; dest && t < tiles; t++) {
        const uint8_t *tile = src + (size_t)t * 128;
        uint8_t *out = dest + (size_t)t * 256;
        for (int y = 0; y < 16; y++) {
            const int row = y << 2;
            for (int x = 0; x < 8; x++) {
                out[y * 16 + x] = (uint8_t)(((tile[0x40 + row] >> x) & 1) | (((tile[0x40 + row + 2] >> x) & 1) << 1) |
                                            (((tile[0x40 + row + 1] >> x) & 1) << 2) | (((tile[0x40 + row + 3] >> x) & 1) << 3));
                out[y * 16 + 8 + x] = (uint8_t)(((tile[row] >> x) & 1) | (((tile[row + 2] >> x) & 1) << 1) |
                                                (((tile[row + 1] >> x) & 1) << 2) | (((tile[row + 3] >> x) & 1) << 3));
            }
        }
    }
    *tiles_out = tiles;
    return dest;
}

static uint32_t colour(uint16_t packed)
{
    int r = ((packed >> 7) & 0x1e) | ((packed >> 14) & 1);
    int g = ((packed >> 3) & 0x1e) | ((packed >> 13) & 1);
    int b = ((packed << 1) & 0x1e) | ((packed >> 12) & 1);
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    if (packed & 0x8000) {
        r = (r * 192) / 256;
        g = (g * 192) / 256;
        b = (b * 192) / 256;
    }
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void video_reset(void)
{
    memset(S.vram, 0, sizeof S.vram);
    memset(S.palette, 0, sizeof S.palette);
    for (int b = 0; b < 2; b++)
        for (int i = 0; i < 0x1000; i++) S.palette_rgb[b][i] = 0xff000000u;
    S.vram_addr = 0;
    S.vram_mod = 1;
    S.vram_read_buffer = 0;
    S.lspc_mode = 0;
    S.timer_reload = S.timer_counter = 0;
    S.timer_enable = false;
    S.timer_mode = 0;
    S.irq_vblank = S.irq_timer = false;
    S.irq_reset = true;
    S.palette_bank = 0;
    S.use_bios_fix = true;
    S.auto_anim = S.auto_anim_speed = S.auto_anim_frame = 0;
    S.scanline = 0;
}

static inline uint16_t vram_offset(void)
{
    return (S.vram_addr & 0x8000) ? (uint16_t)(S.vram_addr & 0x87ff) : (uint16_t)(S.vram_addr & 0x7fff);
}

static void step_vram_address(void)
{
    S.vram_addr = (uint16_t)((S.vram_addr & 0x8000) | ((S.vram_addr + S.vram_mod) & 0x7fff));
    S.vram_read_buffer = S.vram[vram_offset()];
}

static uint16_t video_read_register(uint32_t a)
{
    switch (a & 0x0e) {
    case 0x00: case 0x08: case 0x02: case 0x0a: return S.vram_read_buffer;
    case 0x04: case 0x0c: return S.vram_mod;
    default: return (uint16_t)((((S.scanline + 0xf8) & 0x1ff) << 7) | (S.auto_anim & 7));
    }
}

static void video_write_register(uint32_t a, uint16_t v)
{
    switch (a & 0x0e) {
    case 0x00:
        S.vram_addr = v;
        S.vram_read_buffer = S.vram[vram_offset()];
        break;
    case 0x02:
        S.vram[vram_offset()] = v;
        step_vram_address();
        break;
    case 0x04: S.vram_mod = v; break;
    case 0x06:
        S.lspc_mode = v;
        S.auto_anim_speed = (v >> 8) & 0xff;
        S.timer_mode = (v >> 4) & 7;
        S.timer_enable = (v & 0x10) != 0;
        break;
    case 0x08: S.timer_reload = (S.timer_reload & 0xffff) | ((uint32_t)v << 16); break;
    case 0x0a:
        S.timer_reload = (S.timer_reload & 0xffff0000u) | v;
        if ((S.timer_mode & 0x01) == 0) S.timer_counter = S.timer_reload;
        break;
    case 0x0c:
        if (v & 0x04) S.irq_vblank = false;
        if (v & 0x02) S.irq_timer = false;
        if (v & 0x01) S.irq_reset = false;
        break;
    default: break;
    }
}

static void begin_frame(void)
{
    S.scanline = 0;
    if ((S.lspc_mode & 0x08) == 0) {
        S.auto_anim_frame++;
        const int period = S.auto_anim_speed + 1 > 1 ? S.auto_anim_speed + 1 : 1;
        if (S.auto_anim_frame >= period) {
            S.auto_anim_frame = 0;
            S.auto_anim = (S.auto_anim + 1) & 7;
        }
    }
    if (S.timer_enable && (S.timer_mode & 0x02) != 0) S.timer_counter = S.timer_reload;
}

static void end_scanline(int line)
{
    S.scanline = line;
    if (S.timer_enable && (S.timer_mode & 0x01) != 0) S.timer_counter = S.timer_reload;
    if (S.timer_enable) {
        if (S.timer_counter <= 384) {
            S.irq_timer = true;
            S.timer_counter = S.timer_reload;
        } else S.timer_counter -= 384;
    }
    if (line == VBLANK_LINE) S.irq_vblank = true;
}

static void draw_fix(uint32_t *fbuf)
{
    const uint32_t *pal = S.palette_rgb[S.palette_bank];
    for (int col = 0; col < 40; col++)
        for (int row = 0; row < 32; row++) {
            const uint16_t word = S.vram[0x7000 + col * 32 + row];
            const int code = word & 0xfff, pal_index = (word >> 12) & 0x0f;
            const uint8_t *tile;
            if (S.use_bios_fix && S.bios_fix_tiles > 0) tile = S.bios_fix_pixels + (size_t)(code % S.bios_fix_tiles) * 64;
            else if (S.cart_fix_tiles > 0) tile = S.fix_pixels + (size_t)(code % S.cart_fix_tiles) * 64;
            else continue;
            const int dest_x = col * 8, dest_y = row * 8 - VISIBLE_TOP;
            for (int y = 0; y < 8; y++) {
                const int py = dest_y + y;
                if (py < 0 || py >= H) continue;
                uint32_t *line = fbuf + py * W + dest_x;
                const uint8_t *src = tile + y * 8;
                for (int x = 0; x < 8; x++)
                    if (src[x]) line[x] = pal[(pal_index << 4) | src[x]];
            }
        }
}

static void draw_sprite_pixels(uint32_t *fbuf, int dest_y, int x, int src_y, int xzoom, bool flip_x, int palette, int code)
{
    if (S.sprite_tiles <= 0) return;
    const uint8_t *tile = S.sprite_pixels + (size_t)(code % S.sprite_tiles) * 256;
    if (src_y < 0 || src_y > 15 || dest_y < 0 || dest_y >= H) return;
    const uint32_t *pal = S.palette_rgb[S.palette_bank];
    uint32_t *line = fbuf + dest_y * W;
    uint16_t zoom_bits = kZoomX[xzoom & 0x0f];
    int src_x = flip_x ? 15 : 0;
    const int src_inc = flip_x ? -1 : 1;
    if (x <= 0x1f0) {
        int dest_x = x;
        for (int i = 0; i < 16; i++) {
            if (zoom_bits & 0x8000) {
                if (dest_x >= 0 && dest_x < W) {
                    const uint8_t pen = tile[src_y * 16 + src_x];
                    if (pen) line[dest_x] = pal[((palette & 0xff) << 4) | pen];
                }
                dest_x++;
            }
            zoom_bits = (uint16_t)(zoom_bits << 1);
            if (zoom_bits == 0) break;
            src_x += src_inc;
        }
        return;
    }
    int wrapped = x, dest_x = 0;                // (0x1f1..0x1ff wraps onto the left edge)
    for (int i = 0; i < 16; i++) {
        if (zoom_bits & 0x8000) {
            if (wrapped >= 0x200 && dest_x < W) {
                const uint8_t pen = tile[src_y * 16 + src_x];
                if (pen) line[dest_x] = pal[((palette & 0xff) << 4) | pen];
                dest_x++;
            }
            wrapped++;
        }
        zoom_bits = (uint16_t)(zoom_bits << 1);
        if (zoom_bits == 0) break;
        src_x += src_inc;
    }
}

static void draw_sprites(uint32_t *fbuf)
{
    for (int fy = 0; fy < H; fy++) {
        const int scanline = fy + VISIBLE_TOP;
        int y = 0, x = 0, rows = 0, zoom_y = 0, zoom_x = 0, drawn = 0;
        for (int sprite = 0; sprite < SPRITE_COUNT; sprite++) {
            const uint16_t y_control = S.vram[0x8200 + sprite], zoom_control = S.vram[0x8000 + sprite];
            if (y_control & 0x40) {
                x = (x + zoom_x + 1) & 0x1ff;
                zoom_x = (zoom_control >> 8) & 0x0f;
            } else {
                y = 0x200 - (y_control >> 7);
                x = S.vram[0x8400 + sprite] >> 7;
                zoom_y = zoom_control & 0xff;
                zoom_x = (zoom_control >> 8) & 0x0f;
                rows = y_control & 0x3f;
            }
            if (rows == 0) continue;
            if (!(rows >= 0x20 || ((scanline - y) & 0x1ff) < (rows * 0x10))) continue;
            if (x >= 0x140 && x <= 0x1f0) {
                if (++drawn >= SPRITES_PER_LINE) break;
                continue;
            }
            const int sprite_line = (scanline - y) & 0x1ff;
            int zoom_line = sprite_line & 0xff;
            bool invert = (sprite_line & 0x100) != 0;
            if (invert) zoom_line ^= 0xff;
            if (rows > 0x20) {
                const int period = (zoom_y + 1) << 1;
                if (period > 0) zoom_line %= period;
                if (zoom_line > zoom_y) {
                    zoom_line = ((zoom_y + 1) << 1) - 1 - zoom_line;
                    invert = !invert;
                }
            }
            int src_y, tile;
            if (S.lo.n >= 0x10000) {
                const uint8_t entry = S.lo.p[(zoom_y << 8) | zoom_line];
                src_y = entry & 0x0f;
                tile = entry >> 4;
            } else {
                src_y = zoom_line & 0x0f;
                tile = (zoom_line >> 4) & 0x1f;
            }
            if (invert) {
                src_y ^= 0x0f;
                tile ^= 0x1f;
            }
            const uint16_t tile_lo = S.vram[sprite * 64 + tile * 2], attr = S.vram[sprite * 64 + tile * 2 + 1];
            int code = (int)tile_lo | ((int)(attr & 0x00f0) << 12);
            if ((S.lspc_mode & 0x08) == 0) {
                if (attr & 0x0008) code = (code & ~0x07) | (S.auto_anim & 7);
                else if (attr & 0x0004) code = (code & ~0x03) | (S.auto_anim & 3);
            }
            if (attr & 0x0002) src_y ^= 0x0f;
            draw_sprite_pixels(fbuf, fy, x, src_y, zoom_x, (attr & 0x0001) != 0, (attr >> 8) & 0xff, code);
            if (++drawn >= SPRITES_PER_LINE) break;
        }
    }
}

static void render_frame(void)
{
    const uint32_t backdrop = S.palette_rgb[S.palette_bank][0xfff];
    for (int i = 0; i < W * H; i++) S.frame[i] = backdrop;
    draw_sprites(S.frame);
    draw_fix(S.frame);
    for (int i = 0; i < W * H; i++) S.fb[i] = arc_rgb565(S.frame[i]);
}

// ---------------------------------------------------------------------------
// The uPD4990A

static uint8_t rtc_time_bit(int bit)
{
    static const uint8_t kSec = 0x00, kMin = 0x00, kHour = 0x12, kDay = 0x09, kWeek = 0x01, kMonth = 0x09, kYear = 0x73;
    if (bit < 8) return (kSec >> bit) & 1;
    if (bit < 16) return (kMin >> (bit - 8)) & 1;
    if (bit < 24) return (kHour >> (bit - 16)) & 1;
    if (bit < 32) return (kDay >> (bit - 24)) & 1;
    if (bit < 36) return (kWeek >> (bit - 32)) & 1;
    if (bit < 40) return (kMonth >> (bit - 36)) & 1;
    if (bit < 48) return (kYear >> (bit - 40)) & 1;
    return 0;
}

static void rtc_write(uint8_t value)
{
    const uint8_t data = value & 0x07;
    const bool clock = (data & 0x02) != 0, strobe = (data & 0x04) != 0;
    if (S.rtc_strobe && !strobe) {
        const uint8_t command = S.rtc_cmd_shift;
        S.rtc_command = command;
        S.rtc_shiftlo = 0;
        S.rtc_bitno = 0;
        if (command == 0x03) S.rtc_reading = true;
        static const int kTpInterval[] = {1, 1, 1, 1, 60, 600, 1800, 3600};
        if (command >= 0x04 && command <= 0x0b) {
            S.rtc_tp_interval = kTpInterval[command - 0x04];
            S.rtc_tp_counter = 0;
            S.rtc_pulse = false;
        }
        S.rtc_data_bit = rtc_time_bit(0) != 0;
    }
    S.rtc_strobe = strobe;
    if (S.rtc_clock && !clock) {
        S.rtc_cmd_shift = (uint8_t)(((S.rtc_cmd_shift >> 1) | (uint8_t)((data & 1) << 3)) & 0x0f);
        if (S.rtc_bitno < 32) S.rtc_shiftlo |= (uint32_t)(data & 1) << S.rtc_bitno;
        S.rtc_bitno++;
        if (S.rtc_reading) {
            S.rtc_data_bit = rtc_time_bit(S.rtc_bitno) != 0;
            if (S.rtc_bitno >= 0x34) {
                S.rtc_reading = false;
                S.rtc_bitno = 0;
                S.rtc_shiftlo = 0;
            }
        }
    }
    S.rtc_clock = clock;
    S.rtc_ctrl = data;
}

// ---------------------------------------------------------------------------
// Inputs

static uint8_t pad_bits(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_UP) v &= (uint8_t)~0x01;
    if (p & AP_DOWN) v &= (uint8_t)~0x02;
    if (p & AP_LEFT) v &= (uint8_t)~0x04;
    if (p & AP_RIGHT) v &= (uint8_t)~0x08;
    if (p & AP_B1) v &= (uint8_t)~0x10;
    if (p & AP_B2) v &= (uint8_t)~0x20;
    if (p & AP_B3) v &= (uint8_t)~0x40;
    if (p & AP_B4) v &= (uint8_t)~0x80;
    return v;
}

static uint8_t status_a(void)
{
    uint8_t v = 0xff;
    if (S.coin1) v &= (uint8_t)~0x01;
    if (S.coin2) v &= (uint8_t)~0x02;
    if (S.service) v &= (uint8_t)~0x04;
    if (!S.rtc_pulse) v &= (uint8_t)~0x40;
    if (!S.rtc_data_bit) v &= (uint8_t)~0x80;
    return v;
}

static uint8_t status_b(void)
{
    uint8_t v = 0xf0;
    if (!(S.p1 & AP_START)) v |= 0x01;
    v |= 0x02;                                  // (no select)
    if (!(S.p2 & AP_START)) v |= 0x04;
    v |= 0x08;
    return v;
}

// ---------------------------------------------------------------------------
// The 68000

static void update_irqs(void)
{
    m68kd_set_irq(&S.cpu, 3, S.irq_reset ? M68_ASSERT : M68_CLEAR);
    m68kd_set_irq(&S.cpu, 2, S.irq_timer ? M68_ASSERT : M68_CLEAR);
    m68kd_set_irq(&S.cpu, 1, S.irq_vblank ? M68_ASSERT : M68_CLEAR);
}

static uint16_t read_word(uint32_t a)
{
    a &= 0xfffffe;
    if (a < 0x80 && S.bios_vectors && S.bios.n >= 8 && a + 1 < S.bios.n) return (uint16_t)((S.bios.p[a] << 8) | S.bios.p[a + 1]);
    if (a < 0x100000) {
        if (!S.p_rom.n) return 0xffff;
        const uint32_t o = a % (uint32_t)S.p_rom.n;
        return (uint16_t)((S.p_rom.p[o] << 8) | S.p_rom.p[o + 1]);
    }
    if (a < 0x200000) {
        const uint32_t o = a & 0xffff;
        return (uint16_t)((S.ram[o] << 8) | S.ram[o + 1]);
    }
    if (a < 0x300000) {
        if (a == 0x220000) return (uint16_t)((S.sound_reply << 8) | status_a());
        if (a >= 0x2ffff0 || !S.p_rom.n) return 0xffff;
        uint32_t o = S.cart_bank + (a - 0x200000);
        if (o + 1 >= S.p_rom.n) o %= (uint32_t)S.p_rom.n;
        return (uint16_t)((S.p_rom.p[o] << 8) | S.p_rom.p[o + 1]);
    }
    if (a < 0x320000) return (uint16_t)((pad_bits(S.p1) << 8) | S.dsw);
    if (a < 0x340000) return (uint16_t)((S.sound_reply << 8) | status_a());
    if (a < 0x360000) return (uint16_t)(pad_bits(S.p2) << 8);
    if (a < 0x3a0000) return (uint16_t)(status_b() << 8);
    if (a >= 0x3c0000 && a < 0x3e0000) return video_read_register(a);
    if (a >= 0x400000 && a < 0x402000) return S.palette[S.palette_bank][(a >> 1) & 0xfff];
    if (a >= 0x800000 && a < 0x801000) return (uint16_t)(0xff00 | S.memcard[(a >> 1) & 0x7ff]);
    if (a >= 0xc00000 && a < 0xc20000) {
        if (!S.bios.n) return 0xffff;
        const uint32_t o = (a - 0xc00000) % (uint32_t)S.bios.n;
        return (uint16_t)((S.bios.p[o] << 8) | S.bios.p[o + 1]);
    }
    if (a >= 0xd00000 && a < 0xd10000) {
        const uint32_t o = a & 0xffff;
        return (uint16_t)((S.sram[o] << 8) | S.sram[o + 1]);
    }
    return 0xffff;
}

static void write_byte(uint32_t a, uint8_t v);

static void write_word(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a >= 0x100000 && a < 0x200000) {
        const uint32_t o = a & 0xffff;
        S.ram[o] = (uint8_t)(v >> 8);
        S.ram[o + 1] = (uint8_t)v;
        return;
    }
    if (a >= 0x200000 && a < 0x300000) {
        if (a >= 0x2ffff0) {                    // (bank n: ROM offset n + 1 MiB at $200000)
            if (S.p_rom.n > 0x100000) {
                S.cart_bank = ((uint32_t)(v & 0x07) + 1) * 0x100000;
                if (S.cart_bank >= S.p_rom.n) S.cart_bank = 0x100000;
            } else S.cart_bank = 0;
        }
        return;
    }
    if ((a & 0xfe0000) == 0x300000) {
        S.watchdog = 0;
        return;
    }
    if ((a & 0xfe0000) == 0x320000) {
        S.sound_latch = (uint8_t)(v >> 8);
        if (S.sound_nmi_enabled) zd_set_nmi(&S.z80, ZD_PULSE);
        return;
    }
    if ((a & 0xfe0000) == 0x3a0000) {
        write_byte(a | 1, (uint8_t)v);
        return;
    }
    if (a >= 0x3c0000 && a < 0x3e0000) {
        video_write_register(a, v);
        update_irqs();
        return;
    }
    if (a >= 0x400000 && a < 0x402000) {
        const int i = (int)((a >> 1) & 0xfff);
        S.palette[S.palette_bank][i] = v;
        S.palette_rgb[S.palette_bank][i] = colour(v);
        return;
    }
    if (a >= 0x800000 && a < 0x801000) {
        S.memcard[(a >> 1) & 0x7ff] = (uint8_t)v;
        return;
    }
    if (a >= 0xd00000 && a < 0xd10000 && S.sram_unlocked) {
        const uint32_t o = a & 0xffff;
        S.sram[o] = (uint8_t)(v >> 8);
        S.sram[o + 1] = (uint8_t)v;
    }
}

static void write_byte(uint32_t a, uint8_t v)
{
    a &= 0xffffff;
    if (a >= 0x200000 && a < 0x3c0000) {
        if ((a & 0xfe0000) == 0x220000 && (a & 1) == 0) {
            S.sound_latch = v;
            if (S.sound_nmi_enabled) zd_set_nmi(&S.z80, ZD_PULSE);
            return;
        }
        if ((a & 0xfe0000) == 0x280000 && (a & 1) && (a & 0x70) == 0x50) {
            rtc_write(v);
            return;
        }
        if ((a & 0xfe0000) == 0x300000 && (a & 1)) {
            S.watchdog = 0;
            return;
        }
        if ((a & 0xfe0000) == 0x320000 && (a & 1) == 0) {
            S.sound_latch = v;
            if (S.sound_nmi_enabled) zd_set_nmi(&S.z80, ZD_PULSE);
            return;
        }
        if ((a & 0xfe0000) == 0x380000 && (a & 1)) {
            if ((a & 0x70) == 0x50) rtc_write(v);
            return;
        }
        if ((a & 0xfe0000) == 0x3a0000) {       // (the system latch)
            const bool bit = (a & 0x10) != 0;
            switch ((a >> 1) & 7) {
            case 1: S.bios_vectors = !bit; break;
            case 5: S.use_bios_fix = !bit; break;
            case 6: S.sram_unlocked = bit; break;
            case 7: S.palette_bank = bit ? 0 : 1; break;
            default: break;
            }
            return;
        }
    }
    const uint32_t al = a & ~1u;
    uint16_t w = read_word(al);
    w = (a & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0x00ff) | (v << 8));
    write_word(al, w);
}

static uint16_t cpu_rw(void *u, uint32_t a) { (void)u; return read_word(a); }
static void cpu_ww(void *u, uint32_t a, uint16_t v) { (void)u; write_word(a, v); }
static uint8_t cpu_rb(void *u, uint32_t a)
{
    (void)u;
    const uint16_t w = read_word(a & 0xfffffe);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
static void cpu_wb(void *u, uint32_t a, uint8_t v) { (void)u; write_byte(a, v); }

static void cpu_cycles(void *u, int cycles)
{
    (void)u;
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= MAIN_CLOCK) {
        S.audio_acc -= MAIN_CLOCK;
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(ym2610d_update(&S.ym));
    }
}

// ---------------------------------------------------------------------------
// The Z80

static const buf *z80_rom(void)
{
    if (S.use_bios_fix && S.sm1.n) return &S.sm1;
    if (S.m_rom.n) return &S.m_rom;
    if (S.sm1.n) return &S.sm1;
    return NULL;
}

static uint8_t z80_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0xc000) {
        const buf *r = z80_rom();
        if (!r || !r->n) return 0xff;
        if (a < 0x8000) return r->p[a % r->n];
        return r->p[((uint32_t)(S.z80_bank & 0x0f) * 0x4000 + (a - 0x8000)) % r->n];
    }
    return S.z80_ram[a & 0x1fff];
}

static void z80_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xc000) S.z80_ram[a & 0x1fff] = v;
}

static uint8_t z80_in(void *u, uint16_t port)
{
    (void)u;
    switch (port & 0xff) {
    case 0x00: return S.sound_latch;
    case 0x04: case 0x05: case 0x06: case 0x07: return ym2610d_read(&S.ym, port & 3);
    default: return 0;
    }
}

static void z80_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t low = (uint8_t)port;
    switch (low) {
    case 0x04: case 0x05: case 0x06: case 0x07: ym2610d_write(&S.ym, low & 3, v); return;
    case 0x08: case 0x09: case 0x0a: case 0x0b:
        S.sound_nmi_enabled = true;
        S.z80_bank = (uint8_t)((port >> 8) & 0x0f);
        return;
    case 0x0c: S.sound_reply = v; return;
    case 0x18: S.sound_nmi_enabled = false; return;
    default:
        if ((low & 0xf0) != 0 && (low & 0x0f) == 0x08) S.z80_bank = (uint8_t)((low >> 4) & 0x0f);
        return;
    }
}

// ---------------------------------------------------------------------------
// ROMs (dsp-cpp's loader: the cart's files told apart by their names)

static bool contains(const char *t, const char *n) { return strstr(t, n) != NULL; }

static bool has_part(const char *name, const char *part)
{
    const size_t ln = strlen(name), lp = strlen(part);
    if (ln < lp) return false;
    char tmp[64];
    if (strcmp(name, part) == 0) return true;
    snprintf(tmp, sizeof tmp, "%s.bin", part);
    if (strcmp(name, tmp) == 0) return true;
    snprintf(tmp, sizeof tmp, "%s.rom", part);
    if (strcmp(name, tmp) == 0) return true;
    if (ln >= lp + 1 && strcmp(name + ln - lp, part) == 0) {
        const char prev = name[ln - lp - 1];
        if (prev == '.' || prev == '-' || prev == '_') return true;
    }
    snprintf(tmp, sizeof tmp, "-%s.", part);
    if (contains(name, tmp)) return true;
    snprintf(tmp, sizeof tmp, ".%s.", part);
    return contains(name, tmp);
}

static bool is_bios_name(const char *name)
{
    static const char *const kNames[] = {"sp-s2.sp1", "sp-s.sp1", "sp-e.sp1", "sp-j2.sp1", "sp-j3.bin", "sp1-u4.bin",
                                         "usa_2slt.bin", "asia-s3.rom", "vs-bios.rom", "sp-s3.sp1", "uni-bios.rom",
                                         "uni-bios_4_0.rom", "uni-bios_3_3.rom", "uni-bios_3_2.rom", "uni-bios_3_1.rom",
                                         "uni-bios_3_0.rom", "uni-bios_2_3.rom", "uni-bios_2_2.rom", "uni-bios_2_1.rom",
                                         "uni-bios_2_0.rom", "uni-bios_1_3.rom", "uni-bios_1_2.rom", "uni-bios_1_1.rom",
                                         "uni-bios_1_0.rom", "neogeo.rom", "neo-geo.rom", "sp1.sp1", NULL};
    for (int i = 0; kNames[i]; i++)
        if (strcmp(name, kNames[i]) == 0) return true;
    return contains(name, "uni-bios") || contains(name, "vs-bios") || contains(name, "asia-s3") ||
           (contains(name, "sp-s") && contains(name, "sp1")) || contains(name, "sp-e.sp1");
}

static int p_rank(const char *n)
{
    if (has_part(n, "p1") || has_part(n, "pg1") || has_part(n, "ep1")) return 1;
    if (has_part(n, "p2") || has_part(n, "pg2") || has_part(n, "ep2")) return 2;
    if (has_part(n, "sp2")) return 3;
    if (has_part(n, "p3") || has_part(n, "ep3")) return 4;
    if (has_part(n, "p4") || has_part(n, "ep4")) return 5;
    return 9;
}

static int c_rank(const char *n)
{
    char part[8];
    for (int i = 16; i >= 1; i--) {
        snprintf(part, sizeof part, "c%d", i);
        if (has_part(n, part)) return i;
    }
    return 9;
}

static int v_rank(const char *n)
{
    char part[8];
    for (int i = 4; i >= 1; i--) {
        snprintf(part, sizeof part, "v%d", i);
        if (has_part(n, part)) return i;
    }
    return 9;
}

typedef struct { char *v[64]; int n; } names;
static void collect(void *u, const char *name)
{
    names *l = (names *)u;
    if (l->n < 64) {
        l->v[l->n] = (char *)malloc(strlen(name) + 1);
        if (l->v[l->n]) {
            for (size_t i = 0; i <= strlen(name); i++) l->v[l->n][i] = (char)tolower((unsigned char)name[i]);
            l->n++;
        }
    }
}

static int (*g_rank)(const char *);
static int by_rank(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    const int rx = g_rank(x), ry = g_rank(y);
    if (rx != ry) return rx < ry ? -1 : 1;
    return strcmp(x, y);
}

static buf read_named(const char *const *sets, const char *name)
{
    buf b = {NULL, 0};
    uint32_t len = 0;
    b.p = romfs_get(sets, name, 0, 0, &len);
    if (b.p) b.n = len;
    return b;
}

static void append(buf *d, buf s)
{
    if (!s.n) return;
    uint8_t *p = (uint8_t *)realloc(d->p, d->n + s.n);
    if (!p) return;
    memcpy(p + d->n, s.p, s.n);
    d->p = p;
    d->n += s.n;
    free(s.p);
}

static void bios_piece(const char *const *names_, buf *dest)
{
    const char *const game_only[] = {kSets[0], NULL}, *const bios_only[] = {kSets[1], NULL};
    for (int pass = 0; pass < 2 && !dest->n; pass++)
        for (int i = 0; names_[i] && !dest->n; i++) {
            buf b = read_named(pass ? bios_only : game_only, names_[i]);
            if (b.n) *dest = b;
            else free(b.p);
        }
}

static void byteswap(buf *b)
{
    for (size_t i = 0; i + 1 < b->n; i += 2) {
        const uint8_t t = b->p[i];
        b->p[i] = b->p[i + 1];
        b->p[i + 1] = t;
    }
}

static bool header_at(const buf *b, size_t o) { return o + 7 <= b->n && memcmp(b->p + o, "NEO-GEO", 7) == 0; }

static bool load_roms(void)
{
    static const char *const kBios[] = {"sp-s2.sp1", "sp-s.sp1", "sp-e.sp1", "usa_2slt.bin", "asia-s3.rom", "vs-bios.rom",
                                        "uni-bios.rom", "uni-bios_4_0.rom", "uni-bios_3_3.rom", "sp-j3.bin", "neo-geo.rom",
                                        "neogeo.rom", NULL};
    static const char *const kSfix[] = {"sfix.sfix", "sfix.rom", "sf.sfix", NULL};
    static const char *const kSm1[] = {"sm1.sm1", "sm1.rom", NULL};
    static const char *const kLo[] = {"000-lo.lo", "lo.lo", "000-lo.rom", NULL};
    bios_piece(kBios, &S.bios);
    bios_piece(kSfix, &S.sfix);
    bios_piece(kSm1, &S.sm1);
    bios_piece(kLo, &S.lo);

    names all = {{0}, 0}, p = {{0}, 0}, s = {{0}, 0}, m = {{0}, 0}, c = {{0}, 0}, v = {{0}, 0};
    romfs_list_set(kSets[0], collect, &all);
    for (int i = 0; i < all.n; i++) {
        const char *n = all.v[i];
        if (is_bios_name(n) || contains(n, "sfix") || contains(n, "sm1") || contains(n, "000-lo") || contains(n, "000-hi")) continue;
        names *to = NULL;
        if (has_part(n, "s1") || has_part(n, "s2")) to = &s;
        else if (has_part(n, "m1")) to = &m;
        else if (c_rank(n) < 9) to = &c;
        else if (v_rank(n) < 9) to = &v;
        else if (p_rank(n) < 9) to = &p;
        if (to) to->v[to->n++] = all.v[i];
    }
    g_rank = p_rank;
    qsort(p.v, (size_t)p.n, sizeof p.v[0], by_rank);
    g_rank = c_rank;
    qsort(c.v, (size_t)c.n, sizeof c.v[0], by_rank);
    g_rank = v_rank;
    qsort(v.v, (size_t)v.n, sizeof v.v[0], by_rank);
    const char *const game_only[] = {kSets[0], NULL};
    for (int i = 0; i < p.n; i++) append(&S.p_rom, read_named(game_only, p.v[i]));
    for (int i = 0; i < s.n; i++) append(&S.s_rom, read_named(game_only, s.v[i]));
    for (int i = 0; i < m.n; i++) append(&S.m_rom, read_named(game_only, m.v[i]));
    for (int i = 0; i < v.n; i++) append(&S.v_rom, read_named(game_only, v.v[i]));
    for (int i = 0; i < c.n; i += 2) {          // (ROM_LOAD16_BYTE pairs, one after another)
        buf even = read_named(game_only, c.v[i]);
        buf odd = i + 1 < c.n ? read_named(game_only, c.v[i + 1]) : (buf){NULL, 0};
        const size_t n = even.n > odd.n ? even.n : odd.n;
        if (n) {
            const size_t base = S.c_rom.n;
            uint8_t *q = (uint8_t *)realloc(S.c_rom.p, base + n * 2);
            if (q) {
                memset(q + base, 0, n * 2);
                for (size_t b = 0; b < n; b++) {
                    if (b < even.n) q[base + b * 2] = even.p[b];
                    if (b < odd.n) q[base + b * 2 + 1] = odd.p[b];
                }
                S.c_rom.p = q;
                S.c_rom.n = base + n * 2;
            }
        }
        free(even.p);
        free(odd.p);
    }
    for (int i = 0; i < all.n; i++) free(all.v[i]);
    if (S.c_rom.n) {
        const size_t tiles = (S.c_rom.n + 127) / 128;
        uint8_t *q = (uint8_t *)realloc(S.c_rom.p, tiles * 128);
        if (q) {
            memset(q + S.c_rom.n, 0, tiles * 128 - S.c_rom.n);
            S.c_rom.p = q;
            S.c_rom.n = tiles * 128;
        }
    }
    if (!S.bios.n && !S.p_rom.n) {
        romfs_missing(kSets, "sp-s2.sp1");
        return false;
    }
    S.bios_present = S.bios.n != 0;
    if (!S.bios.n) {
        S.bios.p = (uint8_t *)calloc(1, 0x20000);
        S.bios.n = 0x20000;
    }
    if (!S.p_rom.n) {
        S.p_rom.p = (uint8_t *)calloc(1, 0x100000);
        memcpy(S.p_rom.p, S.bios.p, S.bios.n < 0x100000 ? S.bios.n : 0x100000);
        S.p_rom.n = 0x100000;
    }
    if (!S.s_rom.n && S.sfix.n) {
        S.s_rom.p = (uint8_t *)malloc(S.sfix.n);
        memcpy(S.s_rom.p, S.sfix.p, S.sfix.n);
        S.s_rom.n = S.sfix.n;
    }
    if (!S.m_rom.n && S.sm1.n) {
        S.m_rom.p = (uint8_t *)malloc(S.sm1.n);
        memcpy(S.m_rom.p, S.sm1.p, S.sm1.n);
        S.m_rom.n = S.sm1.n;
    }
    // finish_load(true)
    byteswap(&S.bios);
    byteswap(&S.p_rom);
    if (S.p_rom.n >= 0x200000 && !header_at(&S.p_rom, 0x100) && header_at(&S.p_rom, 0x100100)) {
        uint8_t *rot = (uint8_t *)malloc(S.p_rom.n);   // (the fixed bank in the second MiB: ROM_CONTINUE)
        if (!rot) return false;
        memcpy(rot, S.p_rom.p + 0x100000, 0x100000);
        memcpy(rot + 0x100000, S.p_rom.p, 0x100000);
        if (S.p_rom.n > 0x200000) memcpy(rot + 0x200000, S.p_rom.p + 0x200000, S.p_rom.n - 0x200000);
        free(S.p_rom.p);
        S.p_rom.p = rot;
    }
    S.cart_bank = S.p_rom.n > 0x100000 ? 0x100000 : 0;
    S.fix_pixels = decode_fix(S.s_rom.p, S.s_rom.n, &S.cart_fix_tiles);
    S.bios_fix_pixels = decode_fix(S.sfix.p, S.sfix.n, &S.bios_fix_tiles);
    S.sprite_pixels = decode_sprites(S.c_rom.p, S.c_rom.n, &S.sprite_tiles);
    free(S.c_rom.p);                            // (the decoded tiles are what is kept)
    S.c_rom.p = NULL;
    romfs_release();
    return S.fix_pixels && S.bios_fix_pixels && S.sprite_pixels;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    S.dsw = 0xff;
    if (!load_roms()) return false;
    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68000);
    S.cpu.read_word = cpu_rw;
    S.cpu.write_word = cpu_ww;
    S.cpu.read_byte = cpu_rb;
    S.cpu.write_byte = cpu_wb;
    S.cpu.cycles = cpu_cycles;
    S.z80.read = z80_read;
    S.z80.write = z80_write;
    S.z80.in = z80_in;
    S.z80.out = z80_out;
    zd_init(&S.z80);
    ym2610d_init(&S.ym, YM_CLOCK, 1.2f);        // (dsp-cpp leaves its IRQ unconnected)
    S.ym.a_rom = S.ym.b_rom = S.v_rom.p;
    S.ym.a_size = S.ym.b_size = (uint32_t)S.v_rom.n;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    memset(S.z80_ram, 0, sizeof S.z80_ram);
    S.sound_latch = S.sound_reply = 0;
    S.sound_nmi_enabled = false;
    S.z80_bank = 0;
    S.watchdog = 0;
    S.sram_unlocked = false;
    S.bios_vectors = S.bios_present;
    S.rtc_bitno = 0;
    S.rtc_shiftlo = 0;
    S.rtc_ctrl = 0;
    S.rtc_clock = S.rtc_strobe = false;
    video_reset();
    S.use_bios_fix = S.sfix.n != 0;
    ym2610d_reset(&S.ym);
    zd_reset(&S.z80);
    m68kd_reset(&S.cpu);
    update_irqs();
}

int arc_run_frame(const int16_t **audio)
{
    S.naudio = 0;
    if (++S.watchdog > 180) {
        arc_reset();
        S.watchdog = 0;
    }
    if (S.rtc_tp_interval > 0) {
        if (++S.rtc_tp_counter >= S.rtc_tp_interval) S.rtc_tp_counter = 0;
        S.rtc_pulse = S.rtc_tp_counter == 0;
    } else S.rtc_pulse = false;
    int m68k_per_line = (int)((double)MAIN_CLOCK / FPS / SCANLINES), z80_per_line = (int)((double)Z80_CLOCK / FPS / SCANLINES);
    if (m68k_per_line < 1) m68k_per_line = 1;
    if (z80_per_line < 1) z80_per_line = 1;
    begin_frame();
    for (int line = 0; line < SCANLINES; line++) {
        end_scanline(line);
        update_irqs();
        m68kd_run(&S.cpu, m68k_per_line);
        zd_run(&S.z80, z80_per_line);
    }
    render_frame();
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    S.p1 = in->p[0];
    S.p2 = in->p[1];
    S.coin1 = in->coin1;
    S.coin2 = in->coin2;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw = v;
    if (bank == 1) S.service = (v & 1) != 0;
}
