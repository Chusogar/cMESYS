// bombjack.c - Bomb Jack (Tehkan, 1984) and Calorie Kun vs Moguranian (Sega,
// 1986) for the common arcade frontends (arc.h): C port of dsp-cpp's
// drivers/arcade/bombjack (itself from dsp-emulator's bombjack_hw.pas, MAME
// bombjack.cpp / calorie.cpp). ARC_VARIANT 0: Bomb Jack, 1: Calorie Kun.
//
// A Z80 with a 32x32 text layer, a background picked among 16 pictures stored
// as tile maps in ROM, 16x16 and 32x32 sprites and 128 colours of 4-bit RGB.
// A second Z80 plays three AY-3-8910s and takes commands through a latch that
// clears when read. Bomb Jack: 4 MHz main CPU with a VBLANK NMI, big sprites
// chosen by a range of sprite numbers, vertical monitor. Calorie Kun: Sega
// 317-000x encrypted opcodes, IRQs instead of NMIs, big sprites flagged per
// sprite, horizontal monitor.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"
#include "../../../machine/sega_decrypt.h"

#define CALORIE (ARC_VARIANT == 1)
#define MAIN_CLOCK 4000000
#define SOUND_CLOCK 3000000
#define SCANLINES 264
#define FPS 60.0
#define RATE 44100
#define W (CALORIE ? 256 : 224)
#define H (CALORIE ? 224 : 256)

#if CALORIE
static const char *const kSets[] = { "calorie", NULL };
static const arc_rom kMain[] = {
    {"epr10072.1j", 0x4000, 0x0000, 0xade792c1}, {"epr10073.1k", 0x4000, 0x4000, 0xb53e109f},
    {"epr10074.1m", 0x4000, 0x8000, 0xa08da685}};
static const arc_rom kSound[] = {{"epr10075.4d", 0x4000, 0, 0xca547036}};
static const arc_rom kChars[] = {
    {"epr10082.5r", 0x2000, 0x0000, 0x5984ea44}, {"epr10081.4r", 0x2000, 0x2000, 0xe2d45dd8},
    {"epr10080.3r", 0x2000, 0x4000, 0x42edfcfe}};
static const arc_rom kTiles[] = {
    {"epr10078.7d", 0x4000, 0x0000, 0x5b8eecce}, {"epr10077.6d", 0x4000, 0x4000, 0x01bcb609},
    {"epr10076.5d", 0x4000, 0x8000, 0xb1529782}};
static const arc_rom kMap[] = {{"epr10079.8d", 0x2000, 0, 0x3c61a42c}};
static const arc_rom kSprites[] = {
    {"epr10071.7m", 0x4000, 0x0000, 0x5f55527a}, {"epr10070.7k", 0x4000, 0x4000, 0x97f35a23},
    {"epr10069.7j", 0x4000, 0x8000, 0xc0c3deaf}};
enum { CHARS = 0x400, TILES = 0x200, SPRITES = 0x200, DSW_A = 0x30, DSW_B = 0x00, BIG_MASK = 0x3f };
const arc_info arc_game = { "calorie", "Calorie Kun vs Moguranian", "Sega", 1986, 256, 224, FPS, RATE, kSets,
                            { "PUNCH", "JUMP", NULL, NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "bombjack", NULL };
static const arc_rom kMain[] = {
    {"09_j01b.bin", 0x2000, 0x0000, 0xc668dc30}, {"10_l01b.bin", 0x2000, 0x2000, 0x52a1e5fb},
    {"11_m01b.bin", 0x2000, 0x4000, 0xb68a062a}, {"12_n01b.bin", 0x2000, 0x6000, 0x1d3ecee5},
    {"13.1r", 0x2000, 0xc000, 0x70e0244d}};
static const arc_rom kSound[] = {{"01_h03t.bin", 0x2000, 0, 0x8407917d}};
static const arc_rom kChars[] = {
    {"03_e08t.bin", 0x1000, 0x0000, 0x9f0470d5}, {"04_h08t.bin", 0x1000, 0x1000, 0x81ec12e6},
    {"05_k08t.bin", 0x1000, 0x2000, 0xe87ec8b1}};
static const arc_rom kTiles[] = {
    {"06_l08t.bin", 0x2000, 0x0000, 0x51eebd89}, {"07_n08t.bin", 0x2000, 0x2000, 0x9dd98e9d},
    {"08_r08t.bin", 0x2000, 0x4000, 0x3155ee7d}};
static const arc_rom kMap[] = {{"02_p04t.bin", 0x1000, 0, 0x398d4a02}};
static const arc_rom kSprites[] = {
    {"16_m07b.bin", 0x2000, 0x0000, 0x94694097}, {"15_l07b.bin", 0x2000, 0x2000, 0x013f58f2},
    {"14_j07b.bin", 0x2000, 0x4000, 0x101c858d}};
enum { CHARS = 0x200, TILES = 0x100, SPRITES = 0x100, DSW_A = 0xc0, DSW_B = 0x50, BIG_MASK = 0x1f };
const arc_info arc_game = { "bombjack", "Bomb Jack", "Tehkan", 1984, 224, 256, FPS, RATE, kSets,
                            { "JUMP", NULL, NULL, NULL }, ARC_JOY_8WAY };
#endif

// 8x8 quarters top-left, top-right (+8 bytes)... (MAME bombjack.cpp layouts)
static const int kX[32] = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71,
                           256, 257, 258, 259, 260, 261, 262, 263, 320, 321, 322, 323, 324, 325, 326, 327};
static const int kY[32] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184,
                           512, 520, 528, 536, 544, 552, 560, 568, 640, 648, 656, 664, 672, 680, 688, 696};

static struct {
    zd_z80 main, sound;
    ay8910 ay[3];
    uint8_t *rom, *opcodes, *sound_rom, *bg_map;
    uint8_t *chars, *tiles, *sprites, *big;
    uint8_t ram[0x1000], screen_ram[0x800], sprite_ram[0x80], pal_ram[0x100], sound_ram[0x800];
    uint16_t palette[0x80];
    uint16_t fb[256 * 224];
    uint8_t in0, in1, in2, dsw_a, dsw_b, sound_latch, background, sprite_control[2];
    bool background_on, nmi_enabled, flip;
    double main_debt, sound_debt;
    int64_t audio_accum;
    int16_t audio[1024];
    int naudio;
} B;

static uint16_t pal4(uint8_t r, uint8_t g, uint8_t b)
{
    return arc_rgb565((uint32_t)((r & 15) * 17) << 16 | (uint32_t)((g & 15) * 17) << 8 | (uint32_t)((b & 15) * 17));
}

// ---------------------------------------------------------------------------
// Main CPU: both games share one block of hardware, at 8000 on Bomb Jack and
// at c000 on Calorie Kun

static uint8_t map_read(uint16_t o)
{
    if (o < 0x1000) return B.ram[o];
    if (o < 0x1800) return B.screen_ram[o & 0x7ff];
    if (o >= 0x1c00 && o < 0x1e00) return B.pal_ram[o & 0xff];
    if (o >= 0x3000 && o < 0x3800) {
        switch (o & 7) {
        case 0: return B.in0;
        case 1: return B.in1;
        case 2: return B.in2;
        case 4: return B.dsw_a;
        case 5: return B.dsw_b;
        default: return 0;
        }
    }
    return 0;
}

static void map_write(uint16_t o, uint8_t v)
{
    if (o < 0x1000) B.ram[o] = v;
    else if (o < 0x1800) B.screen_ram[o & 0x7ff] = v;
    else if (o < 0x1a00) B.sprite_ram[o & 0x7f] = v;
    else if (o >= 0x1c00 && o < 0x1e00) {
        B.pal_ram[o & 0xff] = v;
        const int i = o & 0xfe;
        B.palette[i >> 1] = pal4(B.pal_ram[i], (uint8_t)(B.pal_ram[i] >> 4), B.pal_ram[i + 1]);
    } else if (o >= 0x1e00 && o < 0x2000) {
        B.background_on = (v & 0x10) != 0;
        B.background = v & 0x0f;
    } else if (o >= 0x3800 && o < 0x4000) {
        B.sound_latch = v;
    }
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
#if CALORIE
    if (a < 0xc000) return B.rom[a];
    return map_read((uint16_t)(a - 0xc000));
#else
    if (a < 0x8000 || (a >= 0xc000 && a < 0xe000)) return B.rom[a];
    if ((a >= 0x8000 && a < 0x9800) || (a >= 0x9c00 && a < 0x9e00) || (a >= 0xb000 && a < 0xb800))
        return map_read((uint16_t)(a - 0x8000));
    return 0xff;
#endif
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if CALORIE
    if (a >= 0xc000) map_write((uint16_t)(a - 0xc000), v);
#else
    if ((a >= 0x8000 && a < 0x9a00) || (a >= 0x9c00 && a < 0xa000) || (a >= 0xb800 && a < 0xc000)) {
        map_write((uint16_t)(a - 0x8000), v);
    } else if (a >= 0x9a00 && a < 0x9c00) {
        B.sprite_control[a & 1] = v & 0x0f;
    } else if (a >= 0xb000 && a < 0xb800) {
        switch (a & 7) {
        case 0:
            B.nmi_enabled = v != 0;
            if (!B.nmi_enabled) zd_set_nmi(&B.main, ZD_CLEAR);
            break;
        case 4: B.flip = (v & 1) != 0; break;
        default: break;                 // 3: watchdog
        }
    }
#endif
}

#if CALORIE
static uint8_t main_opcode(void *u, uint16_t a) { return a < 0x8000 ? B.opcodes[a] : main_read(u, a); }
#endif

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// ---------------------------------------------------------------------------
// Sound CPU

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
#if CALORIE
    if (a < 0x8000) return B.sound_rom[a & 0x3fff];
    if (a < 0xc000) return B.sound_ram[a & 0x7ff];
#else
    a &= 0x7fff;
    if (a < 0x2000) return B.sound_rom[a];
    if (a < 0x4000) return 0xff;
    if (a < 0x6000) return B.sound_ram[a & 0x7ff];
#endif
    const uint8_t v = B.sound_latch;    // the latch clears when read
    B.sound_latch = 0;
    return v;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if CALORIE
    if (a >= 0x8000 && a < 0xc000) B.sound_ram[a & 0x7ff] = v;
#else
    a &= 0x7fff;
    if (a >= 0x4000 && a < 0x6000) B.sound_ram[a & 0x7ff] = v;
#endif
}

static uint8_t sound_in(void *u, uint16_t port)
{
    (void)u;
    switch (port & 0x91) {
    case 0x01: return ay8910_data_r(&B.ay[0]);
    case 0x11: return ay8910_data_r(&B.ay[1]);
    case 0x81: return ay8910_data_r(&B.ay[2]);
    default: return 0xff;
    }
}

static void sound_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    switch (port & 0x91) {
    case 0x00: ay8910_address_w(&B.ay[0], v); break;
    case 0x01: ay8910_data_w(&B.ay[0], v); break;
    case 0x10: ay8910_address_w(&B.ay[1], v); break;
    case 0x11: ay8910_data_w(&B.ay[1], v); break;
    case 0x80: ay8910_address_w(&B.ay[2], v); break;
    case 0x81: ay8910_data_w(&B.ay[2], v); break;
    default: break;
    }
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    B.audio_accum += (int64_t)cycles * RATE;
    while (B.audio_accum >= SOUND_CLOCK) {
        B.audio_accum -= SOUND_CLOCK;
        // three PSGs at a quarter of full scale each: their sum stays in range
        const int32_t s = (ay8910_render(&B.ay[0]) + ay8910_render(&B.ay[1]) + ay8910_render(&B.ay[2])) / 4;
        if (B.naudio < 1024) B.audio[B.naudio++] = arc_clamp16(s);
    }
}

// ---------------------------------------------------------------------------
// Video: drawn straight into the cabinet's picture (rows 16..239 of the 256x256
// screen; Bomb Jack's monitor turned 90 degrees)

static inline void put(int sx, int sy, uint16_t c)
{
    if (B.flip) {
        sy = 255 - sy;
        sx = 255 - sx;
    }
    const int y = sy - 16;
    if (y < 0 || y >= 224) return;
#if CALORIE
    B.fb[y * 256 + sx] = c;
#else
    B.fb[sx * 224 + (223 - y)] = c;
#endif
}

static void draw(const uint8_t *set, int code, int size, int x, int y, int color, bool fx, bool fy)
{
    const uint8_t *g = set + (size_t)code * (size_t)(size * size);
    for (int r = 0; r < size; ++r) {
        const int py = y + r;
        if (py < 0 || py >= 256) continue;
        for (int c = 0; c < size; ++c) {
            const int px = x + c;
            if (px < 0 || px >= 256) continue;
            const int pen = g[(fy ? size - 1 - r : r) * size + (fx ? size - 1 - c : c)];
            if (pen) put(px, py, B.palette[color + pen]);
        }
    }
}

static void update_video(void)
{
    if (B.background_on) {
        const size_t base = (size_t)B.background * 0x200;
        for (int f = 0; f < 0x100; ++f) {
            const uint8_t attr = B.bg_map[base + 0x100 + (size_t)f];
            const int code = (B.bg_map[base + (size_t)f] + ((attr & 0x30) << 4)) % TILES;
            const int color = (attr & 0x0f) * 8;
            const bool fx = (attr & 0x40) != 0;
            const uint8_t *g = B.tiles + (size_t)code * 256;
            const int tx = (f & 15) * 16, ty = (f >> 4) * 16;
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < 16; ++c) put(tx + c, ty + r, B.palette[color + g[r * 16 + (fx ? 15 - c : c)]]);
        }
    } else {
        for (int i = 0; i < 256 * 224; i++) B.fb[i] = B.palette[0];
    }
    for (int f = 0; f < 0x400; ++f) {
        const uint8_t attr = B.screen_ram[0x400 + f];
        const int code = (B.screen_ram[f] + ((attr & 0x30) << 4)) % CHARS;
        const int color = (attr & 0x0f) * 8;
        const bool fx = (attr & 0x40) != 0, fy = (attr & 0x80) != 0;
        const uint8_t *g = B.chars + (size_t)code * 64;
        const int tx = (f & 31) * 8, ty = (f >> 5) * 8;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int pen = g[(fy ? 7 - r : r) * 8 + (fx ? 7 - c : c)];
                if (pen) put(tx + c, ty + r, B.palette[color + pen]);
            }
    }
    for (int f = 31; f >= 7; --f) {             // (the first seven are not shown)
        const uint8_t *s = &B.sprite_ram[f * 4];
        const int code = s[0];
        const uint8_t attr = s[1];
        int y = 240 - s[2];
        const int x = s[3];
        const int color = (attr & 0x0f) * 8;
        const bool fx = (attr & 0x40) != 0, fy = (attr & 0x80) != 0;
        bool big;
#if CALORIE
        big = (attr & 0x10) != 0;
#else
        const int pair = f >> 1, lo = B.sprite_control[0], hi = B.sprite_control[1];
        big = lo > hi ? (pair > hi && pair <= lo) : (pair > lo && pair <= hi);
        y += 1;
#endif
        if (!big) draw(B.sprites, code % SPRITES, 16, x, y, color, fx, fy);
        else if (!(f & 1)) draw(B.big, code & BIG_MASK, 32, x, y - 16, color, fx, fy);
    }
}

// ---------------------------------------------------------------------------

static arc_layout layout(int size, int total, int plane_bytes, int element_bytes)
{
    arc_layout l;
    memset(&l, 0, sizeof l);
    l.width = l.height = size;
    l.total = total;
    l.planes = 3;
    l.char_increment = element_bytes * 8;
    l.plane_offsets[0] = 0;
    l.plane_offsets[1] = plane_bytes * 8;
    l.plane_offsets[2] = 2 * plane_bytes * 8;
    l.x_offsets = kX;
    l.y_offsets = kY;
    return l;
}

bool arc_init(void)
{
    B.rom = (uint8_t *)malloc(0x10000);
    B.sound_rom = (uint8_t *)malloc(0x4000);
    B.bg_map = (uint8_t *)calloc(1, 0x2000);
    const size_t gsz = (size_t)SPRITES * 32 * 3;
    uint8_t *gfx = (uint8_t *)calloc(1, gsz > (size_t)CHARS * 24 ? gsz : (size_t)CHARS * 24);
    if (!B.rom || !B.sound_rom || !B.bg_map || !gfx) return false;
    memset(B.rom, 0xff, 0x10000);
    memset(B.sound_rom, 0xff, 0x4000);
    if (!ARC_LOAD(kMain, B.rom, 0x10000) || !ARC_LOAD(kSound, B.sound_rom, 0x4000) || !ARC_LOAD(kMap, B.bg_map, 0x2000))
        return false;
#if CALORIE
    B.opcodes = (uint8_t *)malloc(0x8000);
    if (!B.opcodes) return false;
    sega_decrypt_type2(B.rom, 0x8000, SEGA2_317_000X, 0, B.rom, B.opcodes);
#endif
    arc_layout l = layout(8, CHARS, CHARS * 8, 8);
    memset(gfx, 0, (size_t)CHARS * 24);
    if (!ARC_LOAD(kChars, gfx, (size_t)CHARS * 24) || !(B.chars = arc_gfx_decode(&l, gfx, (size_t)CHARS * 24))) return false;
    l = layout(16, TILES, TILES * 32, 32);
    memset(gfx, 0, (size_t)TILES * 96);
    if (!ARC_LOAD(kTiles, gfx, (size_t)TILES * 96) || !(B.tiles = arc_gfx_decode(&l, gfx, (size_t)TILES * 96))) return false;
    l = layout(16, SPRITES, SPRITES * 32, 32);
    memset(gfx, 0, gsz);
    if (!ARC_LOAD(kSprites, gfx, gsz) || !(B.sprites = arc_gfx_decode(&l, gfx, gsz))) return false;
    // 32x32 sprites: the second half of each plane, (mask + 1) of them
    const int big = SPRITES / 4;
    memmove(gfx, gfx + 0x40 * big, gsz - 0x40 * (size_t)big);
    memset(gfx + gsz - 0x40 * (size_t)big, 0, 0x40 * (size_t)big);
    l = layout(32, BIG_MASK + 1, big * 128, 128);
    if (!(B.big = arc_gfx_decode(&l, gfx, gsz))) return false;
    free(gfx);
    B.dsw_a = DSW_A;
    B.dsw_b = DSW_B;

    B.main.read = main_read;
    B.main.write = main_write;
    B.main.in = in_ff;
    B.main.out = out_nop;
#if CALORIE
    B.main.opcode_read = main_opcode;
#endif
    B.sound.read = sound_read;
    B.sound.write = sound_write;
    B.sound.in = sound_in;
    B.sound.out = sound_out;
    B.sound.cycles = sound_cycles;
    zd_init(&B.main);
    zd_init(&B.sound);
    for (int i = 0; i < 3; i++) ay8910_init(&B.ay[i], 1500000, RATE);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(B.ram, 0, sizeof B.ram);
    memset(B.screen_ram, 0, sizeof B.screen_ram);
    memset(B.sprite_ram, 0, sizeof B.sprite_ram);
    memset(B.pal_ram, 0, sizeof B.pal_ram);
    memset(B.sound_ram, 0, sizeof B.sound_ram);
    memset(B.palette, 0, sizeof B.palette);
    B.in0 = B.in1 = 0;
    B.in2 = 0xf0;
    B.sound_latch = 0;
    B.background = 0xff;
    B.sprite_control[0] = B.sprite_control[1] = 0;
    B.background_on = B.nmi_enabled = B.flip = false;
    B.main_debt = B.sound_debt = 0;
    B.audio_accum = 0;
    zd_reset(&B.main);
    zd_reset(&B.sound);
    for (int i = 0; i < 3; i++) ay8910_reset(&B.ay[i]);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = (double)MAIN_CLOCK / FPS / SCANLINES;
    const double sound_cycles = (double)SOUND_CLOCK / FPS / SCANLINES;
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; ++line) {
        if (line == 240) {
            update_video();
#if CALORIE
            zd_set_irq(&B.main, ZD_HOLD, 0xff);
            zd_set_irq(&B.sound, ZD_HOLD, 0xff);
#else
            if (B.nmi_enabled) zd_set_nmi(&B.main, ZD_ASSERT);
            zd_set_nmi(&B.sound, ZD_PULSE);
#endif
        }
        B.main_debt += main_cycles;
        B.main_debt -= zd_run(&B.main, (int)B.main_debt);
        B.sound_debt += sound_cycles;
        B.sound_debt -= zd_run(&B.sound, (int)B.sound_debt);
    }
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.fb; }

void arc_input(const arc_in *in)
{
#if CALORIE
    for (int p = 0; p < 2; p++) {
        uint8_t v = 0;
        const unsigned b = in->p[p];
        if (b & AP_UP) v |= 0x01;
        if (b & AP_DOWN) v |= 0x02;
        if (b & AP_LEFT) v |= 0x04;
        if (b & AP_RIGHT) v |= 0x08;
        if (b & AP_B1) v |= 0x10;
        if (b & AP_B2) v |= 0x20;
        if (p == 0) B.in0 = v;
        else B.in1 = v;
    }
    B.in2 = (uint8_t)(0xf0 | ((in->p[0] & AP_START) ? 1 : 0) | ((in->p[1] & AP_START) ? 2 : 0) | (in->coin1 ? 4 : 0) |
                      (in->coin2 ? 8 : 0));
#else
    for (int p = 0; p < 2; p++) {
        uint8_t v = 0;
        const unsigned b = in->p[p];
        if (b & AP_RIGHT) v |= 0x01;
        if (b & AP_LEFT) v |= 0x02;
        if (b & AP_UP) v |= 0x04;
        if (b & AP_DOWN) v |= 0x08;
        if (b & AP_B1) v |= 0x10;
        if (p == 0) B.in0 = v;
        else B.in1 = v;
    }
    B.in2 = (uint8_t)(0xf0 | (in->coin1 ? 1 : 0) | (in->coin2 ? 2 : 0) | ((in->p[0] & AP_START) ? 4 : 0) |
                      ((in->p[1] & AP_START) ? 8 : 0));
#endif
}

void arc_dip(int bank, uint8_t value)
{
    if (bank == 0) B.dsw_a = value;
    else if (bank == 1) B.dsw_b = value;
}
