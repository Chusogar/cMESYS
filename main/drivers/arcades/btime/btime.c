// btime.c - BurgerTime (Data East, 1982) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/btime (MAME's btime.cpp). A
// 6502 at 1.5 MHz (its opcodes scrambled after a write), a sound 6502 with
// two AY-3-8910, a background of 16x16 tiles from map ROMs, 8x8 characters
// and 8 sprites in the character RAM.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/ay8910.h"

#define MAIN_CLOCK 1500000
#define SOUND_CLOCK 500000
#define SCANLINES 272
#define FPS 57.444855
#define RATE 44100
#define W 240
#define H 240

static const char *const kSets[] = { "btime", NULL };
static const arc_rom kMain[] = {
    {"aa04.9b", 0x1000, 0xc000, 0x368a25b5}, {"aa06.13b", 0x1000, 0xd000, 0xb4ba400d},
    {"aa05.10b", 0x1000, 0xe000, 0x8005bffa}, {"aa07.15b", 0x1000, 0xf000, 0x086440ad}};
static const arc_rom kSound[] = {{"ab14.12h", 0x1000, 0, 0xf55e5211}};
static const arc_rom kChars[] = {
    {"aa12.7k", 0x1000, 0x0000, 0xc4617243}, {"ab13.9k", 0x1000, 0x1000, 0xac01042f},
    {"ab10.10k", 0x1000, 0x2000, 0x854a872a}, {"ab11.12k", 0x1000, 0x3000, 0xd4848014},
    {"aa8.13k", 0x1000, 0x4000, 0x8650c788}, {"ab9.15k", 0x1000, 0x5000, 0x8dec15e6}};
static const arc_rom kTiles[] = {
    {"ab00.1b", 0x800, 0x0000, 0xc7a14485}, {"ab01.3b", 0x800, 0x0800, 0x25b49078}, {"ab02.4b", 0x800, 0x1000, 0xb8ef56c3}};
static const arc_rom kMap[] = {{"ab03.6b", 0x800, 0, 0xd26bc1f3}};

const arc_info arc_game = { "btime", "BurgerTime", "Data East", 1982, W, H, FPS, RATE, kSets,
                            { "PEPPER", NULL, NULL, NULL }, ARC_JOY_4WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
// 16x16 elements: the right half first, rows of 8 bits (for the turned monitor)
static const int kX16[16] = {128, 129, 130, 131, 132, 133, 134, 135, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m65xx main, sound;
    ay8910 ay0, ay1;
    uint8_t mem[0x10000], sound_rom[0x1000], sound_ram[0x400], video_ram[0x400], color_ram[0x400], bg_map[0x800];
    uint8_t *chars, *sprites, *tiles;
    uint16_t palette[0x20];
    uint16_t screen[256 * 256];
    uint16_t *fb;
    uint8_t in0, in1, in2, dsw_a, dsw_b, sound_latch, bg_control;
    bool sound_nmi_enabled, had_written;
    double main_debt, sound_debt;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *T_;
#define T (*T_)

static uint16_t mirror_index(uint16_t a) { return (uint16_t)(((a & 0x1f) << 5) + ((a & 0x3ff) >> 5)); }

static void set_color(int i, uint8_t value)
{
    const uint8_t v = (uint8_t)~value;  // stored inverted
    const int r = 0x21 * (v & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
    const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
    const int b = 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
    T.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x800 || a >= 0xb000) return T.mem[a];
    if (a >= 0x1000 && a < 0x1400) return T.video_ram[a & 0x3ff];
    if (a >= 0x1400 && a < 0x1800) return T.color_ram[a & 0x3ff];
    if (a >= 0x1800 && a < 0x1c00) return T.video_ram[mirror_index(a)];
    if (a >= 0x1c00 && a < 0x2000) return T.color_ram[mirror_index(a)];
    switch (a) {
    case 0x4000: return T.in0;
    case 0x4001: return T.in1;
    case 0x4002: return T.in2;
    case 0x4003: return T.dsw_a;
    case 0x4004: return T.dsw_b;
    default: return 0;
    }
}

// the opcode after a write has bits swapped when the address has 0x104 set
static uint8_t opcode_read(void *u, uint16_t a)
{
    uint8_t v = main_read(u, a);
    if (T.had_written) {
        T.had_written = false;
        if ((a & 0x104) == 0x104) {
            static const int kSwap[8] = {6, 5, 3, 4, 2, 7, 1, 0};
            uint8_t r = 0;
            for (int i = 0; i < 8; i++)
                if (v & (1 << kSwap[i])) r |= (uint8_t)(1 << (7 - i));
            v = r;
        }
    }
    return v;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    T.had_written = true;
    if (a < 0x800) T.mem[a] = v;
    else if (a >= 0xc00 && a < 0xc10) set_color(a & 0xf, v);
    else if (a >= 0x1000 && a < 0x1400) T.video_ram[a & 0x3ff] = v;
    else if (a >= 0x1400 && a < 0x1800) T.color_ram[a & 0x3ff] = v;
    else if (a >= 0x1800 && a < 0x1c00) T.video_ram[mirror_index(a)] = v;
    else if (a >= 0x1c00 && a < 0x2000) T.color_ram[mirror_index(a)] = v;
    else if (a == 0x4003) {
        T.sound_latch = v;
        m65xx_set_irq(&T.sound, M65_ASSERT);
    } else if (a == 0x4004) T.bg_control = v;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x2000) return T.sound_ram[a & 0x3ff];
    if (a >= 0xa000 && a < 0xc000) {
        m65xx_set_irq(&T.sound, M65_CLEAR);
        return T.sound_latch;
    }
    if (a >= 0xe000) return T.sound_rom[a & 0xfff];
    return 0;
}

// AY 0's port A bit 0 low lets the 16-line NMI through (dsp-cpp's AY8910
// calls the port's handler on a write to it, and when its direction changes)
static void ay0_data_w(uint8_t v)
{
    const int16_t last = T.ay0.last_enable;
    const uint8_t reg = T.ay0.latch;
    ay8910_data_w(&T.ay0, v);
    if (reg == 14) T.sound_nmi_enabled = !(v & 1);
    else if (reg == 7 && (last == -1 || ((last & 0x40) != (T.ay0.regs[7] & 0x40))))
        T.sound_nmi_enabled = !(((T.ay0.regs[7] & 0x40) ? T.ay0.regs[14] : 0xff) & 1);
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x2000) T.sound_ram[a & 0x3ff] = v;
    else if (a < 0x4000) ay0_data_w(v);
    else if (a < 0x6000) ay8910_address_w(&T.ay0, v);
    else if (a < 0x8000) ay8910_data_w(&T.ay1, v);
    else if (a < 0xa000) ay8910_address_w(&T.ay1, v);
    else if (a >= 0xc000 && a < 0xe000) T.sound_nmi_enabled = (v & 1) != 0;
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    T.audio_acc += (int64_t)c * RATE;
    while (T.audio_acc >= SOUND_CLOCK) {
        T.audio_acc -= SOUND_CLOCK;
        const int32_t s = (ay8910_render(&T.ay0) + ay8910_render(&T.ay1)) / 3;
        if (T.naudio < 1024) T.audio[T.naudio++] = arc_clamp16(s);
    }
}

static void update_video(void)
{
    if (T.bg_control & 0x10) {          // 16x16 tiles from one of eight maps, colours 8-15
        static const int kMapOrder[8] = {1, 2, 3, 0, 5, 6, 7, 4};
        const uint8_t *map = &T.bg_map[kMapOrder[T.bg_control & 7] * 0x100];
        for (int f = 0; f < 0x100; f++) {
            const uint8_t *g = T.tiles + map[f] * 256;
            const int tx = (f & 0xf) * 16, ty = (f >> 4) * 16;
            for (int r = 0; r < 16; r++)
                for (int c = 0; c < 16; c++) T.screen[(ty + r) * 256 + tx + c] = T.palette[8 + g[r * 16 + c]];
        }
    } else {
        for (int i = 0; i < 256 * 256; i++) T.screen[i] = T.palette[0];
    }
    for (int f = 0; f < 0x400; f++) {   // the characters, pen 0 clear
        const int code = T.video_ram[f] + ((T.color_ram[f] & 3) << 8);
        const uint8_t *g = T.chars + code * 64;
        const int tx = (f & 0x1f) * 8, ty = (f >> 5) * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                const int pen = g[r * 8 + c];
                if (pen) T.screen[(ty + r) * 256 + tx + c] = T.palette[pen];
            }
    }
    for (int f = 0; f < 8; f++) {       // 8 sprites in the character RAM, every 0x80
        const uint8_t attr = T.video_ram[f * 0x80];
        if (!(attr & 1)) continue;
        const int x = 240 - T.video_ram[f * 0x80 + 0x40], y = T.video_ram[f * 0x80 + 0x60] - 1;
        const uint8_t *g = T.sprites + T.video_ram[f * 0x80 + 0x20] * 256;
        const bool fx = (attr & 2) != 0, fy = (attr & 4) != 0;
        for (int r = 0; r < 16; r++) {
            const int py = y + r;
            if (py < 0 || py >= 256) continue;
            const uint8_t *row = g + (fy ? 15 - r : r) * 16;
            for (int c = 0; c < 16; c++) {
                const int px = x + c;
                if (px < 0 || px >= 256) continue;
                const int pen = row[fx ? 15 - c : c];
                if (pen) T.screen[py * 256 + px] = T.palette[pen];
            }
        }
    }
    for (int y = 0; y < H; y++) memcpy(T.fb + y * W, T.screen + (8 + y) * 256 + 8, W * 2);
}

bool arc_init(void)
{
    if (!(T_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    T.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *gfx = (uint8_t *)calloc(1, 0x6000);
    if (!T.fb || !gfx) return false;
    if (!ARC_LOAD(kMain, T.mem, 0x10000) || !ARC_LOAD(kSound, T.sound_rom, 0x1000) || !ARC_LOAD(kMap, T.bg_map, 0x800))
        return false;
    // the characters and the sprites share the same 3bpp ROMs
    arc_layout cl = { 8, 8, 1024, 3, 64, false, false, true, {2 * 1024 * 64, 1024 * 64, 0}, kX8, kY8 };
    arc_layout sl = { 16, 16, 256, 3, 256, false, false, true, {2 * 256 * 256, 256 * 256, 0}, kX16, kY16 };
    if (!ARC_LOAD(kChars, gfx, 0x6000) || !(T.chars = arc_gfx_decode(&cl, gfx, 0x6000)) ||
        !(T.sprites = arc_gfx_decode(&sl, gfx, 0x6000)))
        return false;
    arc_layout tl = { 16, 16, 64, 3, 256, false, false, true, {2 * 64 * 256, 64 * 256, 0}, kX16, kY16 };
    memset(gfx, 0, 0x1800);
    if (!ARC_LOAD(kTiles, gfx, 0x1800) || !(T.tiles = arc_gfx_decode(&tl, gfx, 0x1800))) return false;
    free(gfx);
    T.main.read = main_read;
    T.main.write = main_write;
    T.main.opcode_read = opcode_read;
    T.sound.read = sound_read;
    T.sound.write = sound_write;
    T.sound.cycles = sound_cycles;
    ay8910_init(&T.ay0, 1500000, RATE);
    ay8910_init(&T.ay1, 1500000, RATE);
    T.dsw_a = 0x3f;
    T.dsw_b = 0xeb;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(T.mem, 0, 0xc000);
    memset(T.sound_ram, 0, sizeof T.sound_ram);
    memset(T.video_ram, 0, sizeof T.video_ram);
    memset(T.color_ram, 0, sizeof T.color_ram);
    for (int i = 0; i < 16; i++) set_color(i, 0);
    T.in0 = T.in1 = 0xff;
    T.in2 = 0x3f;
    T.sound_latch = 0;
    T.sound_nmi_enabled = false;
    T.bg_control = 0;
    T.had_written = false;
    T.main_debt = T.sound_debt = 0;
    T.audio_acc = 0;
    m65xx_reset(&T.main);
    m65xx_reset(&T.sound);
    ay8910_reset(&T.ay0);
    ay8910_reset(&T.ay1);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = MAIN_CLOCK / FPS / SCANLINES, sound_cycles_ = SOUND_CLOCK / FPS / SCANLINES;
    T.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) T.dsw_a &= 0x7f;
        if (line == 248) {
            T.dsw_a |= 0x80;            // VBLANK
            update_video();
        }
        T.main_debt += main_cycles;
        T.main_debt -= m65xx_run(&T.main, (int)T.main_debt);
        T.sound_debt += sound_cycles_;
        T.sound_debt -= m65xx_run(&T.sound, (int)T.sound_debt);
        // a 16-line square wave on the sound CPU's NMI, gated by the AY's port
        m65xx_set_nmi(&T.sound, (line & 8) && T.sound_nmi_enabled ? M65_ASSERT : M65_CLEAR);
    }
    if (audio) *audio = T.audio;
    return T.naudio;
}

const uint16_t *arc_frame(void) { return T.fb; }

static uint8_t pad(unsigned b)
{
    uint8_t v = 0xff;
    if (b & AP_RIGHT) v &= ~0x01;
    if (b & AP_LEFT) v &= ~0x02;
    if (b & AP_UP) v &= ~0x04;
    if (b & AP_DOWN) v &= ~0x08;
    if (b & AP_B1) v &= ~0x10;
    return v;
}

void arc_input(const arc_in *in)
{
    T.in0 = pad(in->p[0]);
    T.in1 = pad(in->p[1]);
    T.in2 = 0x3f;                       // the coins active high; either holds the NMI
    if (in->coin1) T.in2 |= 0x40;
    else if (in->coin2) T.in2 |= 0x80;
    m65xx_set_nmi(&T.main, in->coin1 || in->coin2 ? M65_ASSERT : M65_CLEAR);
    if (in->p[0] & AP_START) T.in2 &= ~0x01;
    if (in->p[1] & AP_START) T.in2 &= ~0x02;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) T.dsw_a = (uint8_t)((T.dsw_a & 0x80) | (v & 0x7f));
    if (bank == 1) T.dsw_b = v;
}
