// blueprnt.c - Blue Print (Bally Midway, 1982), Saturn (Jaleco, 1983) and
// Grasspin (Jaleco, 1983) for the common arcade frontends (arc.h): C port of dsp-cpp's drivers/arcade/blueprint (MAME's
// blueprnt.cpp). A Z80 at 3.5 MHz, a sound Z80 with two AY-3-8910 (one
// passes the DIP switches to the main CPU), 8x8 characters with a scroll
// per column and a priority bit, and 8x16 3bpp sprites, on a vertical
// monitor. dsp-cpp draws the tilemaps into canvases; here each pixel is
// taken from the tiles as the picture is put together (the same result).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"

#define MAIN_CLOCK (7000000 / 2)
#define SOUND_CLOCK (10000000 / 8)
#define SCANLINES 256
#define FPS 60.0
#define RATE 44100
#define W 224
#define H 256

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif

#if ARC_VARIANT == 1
static const char *const kSets[] = { "saturnzi", NULL };
static const arc_rom kMain[] = {
    {"r1", 0x1000, 0x0000, 0x18a6d68e}, {"r2", 0x1000, 0x1000, 0xa7dd2665}, {"r3", 0x1000, 0x2000, 0xb9cfa791},
    {"r4", 0x1000, 0x3000, 0xc5a997e7}, {"r5", 0x1000, 0x4000, 0x43444d00}, {"r6", 0x1000, 0x5000, 0x4d4821f6}};
static const arc_rom kSound[] = {{"r7", 0x1000, 0, 0xdd43e02f}, {"r8", 0x1000, 0x2000, 0x7f9d0877}};
static const arc_rom kChars[] = {{"r10", 0x1000, 0, 0x35987d61}, {"r9", 0x1000, 0x1000, 0xca6a7fda}};
static const arc_rom kSprites[] = {
    {"r11", 0x1000, 0, 0x6e4e6e5d}, {"r12", 0x1000, 0x1000, 0x46fc049e}, {"r13", 0x1000, 0x2000, 0x8b3e8c32}};
const arc_info arc_game = { "saturnzi", "Saturn", "Jaleco", 1983, W, H, FPS, RATE, kSets,
                            { "FIRE", "BOMB", NULL, NULL }, ARC_JOY_4WAY };
#define DSW_A 0x3d
#define DSW_B 0xfd
#elif ARC_VARIANT == 2
static const char *const kSets[] = { "grasspin", NULL };
static const arc_rom kMain[] = {
    {"prom_1.4b", 0x1000, 0x0000, 0x6fd50509}, {"jaleco-2.4c", 0x1000, 0x1000, 0xcd319007},
    {"jaleco-3.4d", 0x1000, 0x2000, 0xac73ccc2}, {"jaleco-4.4f", 0x1000, 0x3000, 0x41f6279d},
    {"jaleco-5.4h", 0x1000, 0x4000, 0xd20aead9}};
static const arc_rom kSound[] = {{"jaleco-6.4j", 0x1000, 0, 0xf58bf3b0}, {"jaleco-7.4l", 0x1000, 0x2000, 0x2d587653}};
static const arc_rom kChars[] = {{"jaleco-9.4p", 0x1000, 0, 0xbccca24c}, {"jaleco-8.3p", 0x1000, 0x1000, 0x9d6185ca}};
static const arc_rom kSprites[] = {
    {"jaleco-10.5p", 0x1000, 0, 0x3a0765c6}, {"jaleco-11.6p", 0x1000, 0x1000, 0xcccfbeb4},
    {"jaleco-12.7p", 0x1000, 0x2000, 0x615b3299}};
const arc_info arc_game = { "grasspin", "Grasspin", "Jaleco", 1983, W, H, FPS, RATE, kSets,
                            { "B1", "B2", NULL, NULL }, ARC_JOY_4WAY };
#define DSW_A 0x7f
#define DSW_B 0x9f
#else
static const char *const kSets[] = { "blueprnt", NULL };
static const arc_rom kMain[] = {
    {"bp-1.1m", 0x1000, 0x0000, 0xb20069a6}, {"bp-2.1n", 0x1000, 0x1000, 0x4a30302e},
    {"bp-3.1p", 0x1000, 0x2000, 0x6866ca07}, {"bp-4.1r", 0x1000, 0x3000, 0x5d3cfac3},
    {"bp-5.1s", 0x1000, 0x4000, 0xa556cac4}};
static const arc_rom kSound[] = {{"snd-1.3u", 0x1000, 0, 0xfd38777a}, {"snd-2.3v", 0x1000, 0x2000, 0x33d5bf5b}};
static const arc_rom kChars[] = {{"bg-1.3c", 0x1000, 0, 0xac2a61bc}, {"bg-2.3d", 0x1000, 0x1000, 0x81fe85d7}};
static const arc_rom kSprites[] = {
    {"red.17d", 0x1000, 0, 0xa73b6483}, {"blue.18d", 0x1000, 0x1000, 0x7d622550}, {"green.20d", 0x1000, 0x2000, 0x2fcb4f26}};

const arc_info arc_game = { "blueprnt", "Blue Print", "Bally Midway", 1982, W, H, FPS, RATE, kSets,
                            { "BOMB", NULL, NULL, NULL }, ARC_JOY_4WAY };
#define DSW_A 0xc3
#define DSW_B 0xd5
#endif

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sound;
    ay8910 ay0, ay1;
    uint8_t rom[0x8000], ram[0x800], video_ram[0x400], color_ram[0x400], scroll_ram[0x100], sprite_ram[0x100];
    uint8_t sound_rom[0x4000], sound_ram[0x400];
    uint8_t *chars, *sprites;
    uint16_t palette[0x208];
    uint16_t screen[256 * 256];
    uint16_t *fb;
    uint8_t in0, in1, dsw_a, dsw_b, sound_latch, dip_latch, gfx_bank;
    bool flip;
    double main_debt, sound_debt;
    int64_t audio_acc;
    int sound_irq_counter;
    int16_t audio[1024];
    int naudio;
} *P_;
#define P (*P_)

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return P.rom[a];
    if (a < 0x8800) return P.ram[a & 0x7ff];
    if (a >= 0x9000 && a < 0xa000) return P.video_ram[a & 0x3ff];
    if (a >= 0xa000 && a < 0xa100) return P.scroll_ram[a & 0xff];
    if (a >= 0xb000 && a < 0xb100) return P.sprite_ram[a & 0xff];
    if (a >= 0xf000) return P.color_ram[a & 0x3ff];
    if (a == 0xc000) return P.in0;
    if (a == 0xc001) return P.in1;
    if (a == 0xc003)                    // from the sound CPU, through AY 0's port A
        return ARC_VARIANT == 2 ? (uint8_t)((P.dip_latch & 0x7f) | 0x80) : P.dip_latch;
    return 0;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x8000) return;
    if (a < 0x8800) P.ram[a & 0x7ff] = v;
    else if (a >= 0x9000 && a < 0xa000) P.video_ram[a & 0x3ff] = v;
    else if (a >= 0xa000 && a < 0xa100) P.scroll_ram[a & 0xff] = v;
    else if (a >= 0xb000 && a < 0xb100) P.sprite_ram[a & 0xff] = v;
    else if (a >= 0xf000) P.color_ram[a & 0x3ff] = v;
    else if (a == 0xd000) {
        P.sound_latch = v;
        zd_set_nmi(&P.sound, ZD_PULSE);
    } else if (a == 0xe000) {
        P.flip = (v & 2) == 0;
        P.gfx_bank = (uint8_t)((v & 4) >> 2);
    }
}

static uint8_t ay_rd(ay8910 *ay, int has_a, uint8_t pa, int has_b, uint8_t pb)
{
    if (ay->latch == 14 && has_a) ay->regs[14] = pa;
    if (ay->latch == 15 && has_b) ay->regs[15] = pb;
    return ay8910_data_r(ay);
}

// AY 0's port A latches what it is written: on a write to the port, and when
// the port's direction changes (0xff as an input), as dsp-cpp's AY8910
static void ay0_data_w(uint8_t v)
{
    const int16_t last = P.ay0.last_enable;
    const uint8_t reg = P.ay0.latch;
    ay8910_data_w(&P.ay0, v);
    if (reg == 14) P.dip_latch = v;
    else if (reg == 7 && (last == -1 || ((last & 0x40) != (P.ay0.regs[7] & 0x40))))
        P.dip_latch = (P.ay0.regs[7] & 0x40) ? P.ay0.regs[14] : 0xff;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x2000) return P.sound_rom[a & 0xfff];
    if (a < 0x4000) return P.sound_rom[0x2000 + (a & 0xfff)];
    if (a < 0x4400) return P.sound_ram[a & 0x3ff];
    if (a == 0x6002) return ay_rd(&P.ay0, 0, 0, 1, P.sound_latch);
    if (a == 0x8002) return ay_rd(&P.ay1, 1, P.dsw_a, 1, P.dsw_b);
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x4000 && a < 0x4400) { P.sound_ram[a & 0x3ff] = v; return; }
    switch (a) {
    case 0x6000: ay8910_address_w(&P.ay0, v); break;
    case 0x6001: ay0_data_w(v); break;
    case 0x8000: ay8910_address_w(&P.ay1, v); break;
    case 0x8001: ay8910_data_w(&P.ay1, v); break;
    default: break;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(void *u, int c)
{
    (void)u;
    const int period = SOUND_CLOCK / (4 * 60);   // four interrupts a frame (a timer)
    P.sound_irq_counter += c;
    while (P.sound_irq_counter >= period) {
        P.sound_irq_counter -= period;
        zd_set_irq(&P.sound, ZD_HOLD, 0xff);
    }
    P.audio_acc += (int64_t)c * RATE;
    while (P.audio_acc >= SOUND_CLOCK) {
        P.audio_acc -= SOUND_CLOCK;
        const int32_t s = (ay8910_render(&P.ay0) + ay8910_render(&P.ay1)) / 2;
        if (P.naudio < 1024) P.audio[P.naudio++] = arc_clamp16(s);
    }
}

// the tilemap's pixel (256x256): 32x32 characters stored by columns, right to
// left; a character's bank comes from bit 6 of the attribute 32 entries back,
// bit 7 puts it in front of the sprites. *front: in front and not pen 0
static inline uint16_t tile_px(int x, int y, bool *front)
{
    const int f = (31 - (x >> 3)) * 32 + (y >> 3);
    const uint8_t attr = P.color_ram[f];
    int code = P.video_ram[f];
    if (P.color_ram[(f - 32) & 0x3ff] & 0x40) code += P.gfx_bank * 0x100;
    const int pen = P.chars[code * 64 + (y & 7) * 8 + (x & 7)];
    *front = (attr & 0x80) && pen != 0;
    return P.palette[(attr & 0x7f) * 4 + pen];
}

static void update_video(void)
{
    int scroll[32];
    for (int col = 0; col < 32; col++) scroll[col] = P.scroll_ram[(30 - col) & 0xff];
    bool fr;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) P.screen[y * 256 + x] = tile_px(x, (y + scroll[x >> 3]) & 255, &fr);
    // 8x16 sprites; a sprite's vertical flip is in the previous one's attribute
    bool flip_y = false;
    for (int f = 0; f < 0x40; f++) {
        const uint8_t *s = &P.sprite_ram[f * 4];
        const int sx = 2 + s[3], sy = 240 - s[0] - 1;
        const uint8_t *g = P.sprites + s[1] * 128;
        const bool flip_x = (s[2] & 0x40) != 0;
        for (int r = 0; r < 16; r++) {
            const int y = sy + r;
            if (y < 0 || y >= 256) continue;
            const uint8_t *row = g + (flip_y ? 15 - r : r) * 8;
            for (int c = 0; c < 8; c++) {
                const int x = sx + c;
                if (x < 0 || x >= 256) continue;
                const int pen = row[flip_x ? 7 - c : c];
                if (pen) P.screen[y * 256 + x] = P.palette[0x200 + pen];
            }
        }
        flip_y = (s[2] & 0x80) != 0;
    }
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const uint16_t p = tile_px(x, (y + scroll[x >> 3]) & 255, &fr);
            if (fr) P.screen[y * 256 + x] = p;
        }
    // rows 16..239, flipped for the cocktail's second player, turned 270 degrees
    for (int oy = 0; oy < H; oy++)
        for (int ox = 0; ox < W; ox++) {
            int row = 16 + ox, col = 255 - oy;
            if (P.flip) {
                row = 255 - row;
                col = 255 - col;
            }
            P.fb[oy * W + ox] = P.screen[row * 256 + col];
        }
}

bool arc_init(void)
{
    if (!(P_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    P.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t gfx[0x3000];
    if (!P.fb) return false;
    memset(P.rom, 0xff, sizeof P.rom);
    memset(P.sound_rom, 0xff, sizeof P.sound_rom);
    if (!ARC_LOAD(kMain, P.rom, sizeof P.rom) || !ARC_LOAD(kSound, P.sound_rom, sizeof P.sound_rom)) return false;
    arc_layout cl = { 8, 8, 0x200, 2, 64, false, false, false, {0x1000 * 8, 0}, kX8, kY16 };
    if (!ARC_LOAD(kChars, gfx, 0x2000) || !(P.chars = arc_gfx_decode(&cl, gfx, 0x2000))) return false;
    arc_layout sl = { 8, 16, 0x100, 3, 128, false, false, false, {0x2000 * 8, 0x1000 * 8, 0}, kX8, kY16 };
    if (!ARC_LOAD(kSprites, gfx, 0x3000) || !(P.sprites = arc_gfx_decode(&sl, gfx, 0x3000))) return false;
    // MAME's blueprnt_palette: the characters combine two 3-bit fields of the
    // colour code selected by the pen bits, the sprites are the eight colours;
    // bit 3 dims
    for (int i = 0; i < 0x208; i++) {
        int pen;
        if (i < 0x200) {
            const int b0 = (i & 2) ? (i & 0xe0) >> 5 : 0, b1 = (i & 1) ? (i & 0x1c) >> 2 : 0;
            pen = ((i & 0x100) >> 5) | b0 | b1;
        } else pen = i - 0x200;
        const uint32_t level = (pen & 8) ? 0xbf : 0xff;
        const uint32_t r = (pen & 1) ? level : 0, g = (pen & 4) ? level : 0, b = (pen & 2) ? level : 0;
        P.palette[i] = arc_rgb565(r << 16 | g << 8 | b);
    }
    P.main.read = main_read;
    P.main.write = main_write;
    P.main.in = in_ff;
    P.main.out = out_nop;
    P.sound.read = sound_read;
    P.sound.write = sound_write;
    P.sound.in = in_ff;
    P.sound.out = out_nop;
    P.sound.cycles = sound_cycles;
    zd_init(&P.main);
    zd_init(&P.sound);
    ay8910_init(&P.ay0, 1250000, RATE);
    ay8910_init(&P.ay1, 625000, RATE);
    P.dsw_a = DSW_A;
    P.dsw_b = DSW_B;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(P.ram, 0, sizeof P.ram);
    memset(P.video_ram, 0, sizeof P.video_ram);
    memset(P.color_ram, 0, sizeof P.color_ram);
    memset(P.scroll_ram, 0, sizeof P.scroll_ram);
    memset(P.sprite_ram, 0, sizeof P.sprite_ram);
    memset(P.sound_ram, 0, sizeof P.sound_ram);
    P.in0 = P.in1 = 0;
    P.sound_latch = P.dip_latch = P.gfx_bank = 0;
    P.flip = false;
    P.main_debt = P.sound_debt = 0;
    P.audio_acc = 0;
    P.sound_irq_counter = 0;
    zd_reset(&P.main);
    zd_reset(&P.sound);
    ay8910_reset(&P.ay0);
    ay8910_reset(&P.ay1);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = MAIN_CLOCK / FPS / SCANLINES, sound_cycles_ = SOUND_CLOCK / FPS / SCANLINES;
    P.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 239) {
            zd_set_irq(&P.main, ZD_HOLD, 0xff);
            update_video();
        }
        P.main_debt += main_cycles;
        P.main_debt -= zd_run(&P.main, (int)P.main_debt);
        P.sound_debt += sound_cycles_;
        P.sound_debt -= zd_run(&P.sound, (int)P.sound_debt);
    }
    if (audio) *audio = P.audio;
    return P.naudio;
}

const uint16_t *arc_frame(void) { return P.fb; }

static uint8_t pad(unsigned b, bool coin)
{
    uint8_t v = 0;
    if (coin) v |= 0x01;
    if (b & AP_START) v |= 0x02;
    if (ARC_VARIANT && (b & AP_B2)) v |= 0x04;   // (Saturn and Grasspin)
    if (b & AP_B1) v |= 0x08;
    if (b & AP_LEFT) v |= 0x10;
    if (b & AP_RIGHT) v |= 0x20;
    if (b & AP_UP) v |= 0x40;
    if (b & AP_DOWN) v |= 0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    P.in0 = pad(in->p[0], in->coin1);
    P.in1 = pad(in->p[1], in->coin2);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) P.dsw_a = v;
    if (bank == 1) P.dsw_b = v;
}
