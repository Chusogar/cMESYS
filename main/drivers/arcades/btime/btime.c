// btime.c - BurgerTime (Data East, 1982), Lock'n'Chase (1981) and Minky
// Monkey (Technos, 1982) for the common arcade frontends (arc.h): C port of
// dsp-cpp's drivers/arcade/btime (MAME's btime.cpp). A 6502 at 1.5 MHz (its
// opcodes scrambled), a sound 6502 with two AY-3-8910, a background of 16x16
// tiles from map ROMs (BurgerTime), 8x8 characters and 8 sprites in the
// character RAM. ARC_VARIANT: 0 btime, 1 lnc, 2 mmonkey (a protection
// device doing the BCD scores and a table lookup).
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

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define BTIME (ARC_VARIANT == 0)
#define MMONKEY (ARC_VARIANT == 2)

#if ARC_VARIANT == 0
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
#define DSW_B 0xeb
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "lnc", NULL };
static const arc_rom kMain[] = {
    {"s3-3d", 0x1000, 0xc000, 0x1ab4f2c2}, {"s2-3c", 0x1000, 0xd000, 0x5e46b789},
    {"s1-3b", 0x1000, 0xe000, 0x1308a32e}, {"s0-3a", 0x1000, 0xf000, 0xbeb4b1fc}};
static const arc_rom kSound[] = {{"sa-1h", 0x1000, 0, 0x379387ec}};
static const arc_rom kChars[] = {
    {"s4-11l", 0x1000, 0x0000, 0xa2162a9e}, {"s5-11m", 0x1000, 0x1000, 0x12f1c2db},
    {"s6-13l", 0x1000, 0x2000, 0xd21e2a57}, {"s7-13m", 0x1000, 0x3000, 0xc4f247cd},
    {"s8-15l", 0x1000, 0x4000, 0x672a92d0}, {"s9-15m", 0x1000, 0x5000, 0x87c8ee9a}};
static const arc_rom kProm[] = {{"sc-5m", 0x20, 0, 0x2a976ebe}};
const arc_info arc_game = { "lnc", "Lock'n'Chase", "Data East", 1981, W, H, FPS, RATE, kSets,
                            { "PUERTA", NULL, NULL, NULL }, ARC_JOY_4WAY };
#define DSW_B 0xff
#else
static const char *const kSets[] = { "mmonkey", NULL };
static const arc_rom kMain[] = {
    {"mmonkey.e4", 0x1000, 0xc000, 0x8d31bf6a}, {"mmonkey.d4", 0x1000, 0xd000, 0xe54f584a},
    {"mmonkey.b4", 0x1000, 0xe000, 0x399a161e}, {"mmonkey.a4", 0x1000, 0xf000, 0xf7d3d1e3}};
static const arc_rom kSound[] = {{"mmonkey.h1", 0x1000, 0, 0x5bcb2e81}};
static const arc_rom kChars[] = {
    {"mmonkey.l11", 0x1000, 0x0000, 0xb6aa8566}, {"mmonkey.m11", 0x1000, 0x1000, 0x6cc4d0c4},
    {"mmonkey.l13", 0x1000, 0x2000, 0x2a343b7e}, {"mmonkey.m13", 0x1000, 0x3000, 0x0230b50d},
    {"mmonkey.l14", 0x1000, 0x4000, 0x922bb3e1}, {"mmonkey.m14", 0x1000, 0x5000, 0xf943e28c}};
static const arc_rom kProm[] = {{"mmi6331.m5", 0x20, 0, 0x55e28b32}};
const arc_info arc_game = { "mmonkey", "Minky Monkey", "Technos", 1982, W, H, FPS, RATE, kSets,
                            { "SALTO", NULL, NULL, NULL }, ARC_JOY_4WAY };
#define DSW_B 0xe9
#endif

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
    uint8_t in0, in1, in2, dsw_a, dsw_b, sound_latch, bg_control, charbank;
    uint8_t prot_status, prot_ret, prot_command, prot_value;
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

#if BTIME
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

#endif

#if BTIME
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
#endif

#if !BTIME
// Minky Monkey's protection: BCD score additions and a table lookup
static uint8_t protection_read(uint16_t a)
{
    a &= 0xfff;
    if (a == 0) return T.prot_status;
    if (a >= 0xd00 && a <= 0xd02) return T.mem[0xb000 + a];
    if (a == 0xe00) return T.prot_ret;
    return 0;
}

static int bcd3(const uint8_t *p)
{
    int r = 0;
    for (int i = 2; i >= 0; i--) r = r * 100 + (p[i] >> 4) * 10 + (p[i] & 0xf);
    return r;
}

static void protection_write(uint16_t a, uint8_t v)
{
    a &= 0xfff;
    if (a == 0) {
        if (v) return;
        uint8_t *m = &T.mem[0xbd00];
        if (T.prot_command == 0) {
            int r = bcd3(m) + bcd3(m + 3);
            for (int i = 0; i < 3; i++) {
                const int lo = r % 10;
                r /= 10;
                const int hi = r % 10;
                r /= 10;
                m[i] = (uint8_t)(lo | hi << 4);
            }
        } else if (T.prot_command == 1) {
            for (int f = 0; f < 0x100; f++)
                if (T.mem[0xbf00 + f] == T.prot_value) {
                    T.prot_ret = (uint8_t)f;
                    break;
                }
        }
        T.prot_status = 0;
    } else if (a == 0xc00) T.prot_command = v;
    else if ((a >= 0xd00 && a <= 0xd05) || a >= 0xf00) T.mem[0xb000 + a] = v;
    else if (a == 0xe00) T.prot_value = v;
}

static uint8_t lnc_read(void *u, uint16_t a)
{
    (void)u;
    if (MMONKEY && a >= 0xb000 && a < 0xc000) return protection_read(a);
    if (a < 0x3c00 || (a >= 0xb000 && a < 0xb200) || a >= 0xc000) return T.mem[a];
    if (a < 0x4000) return T.video_ram[a & 0x3ff];
    if (a >= 0x7c00 && a < 0x8000) return T.video_ram[mirror_index(a)];
    switch (a) {
    case 0x8000: return T.dsw_a;
    case 0x8001: return T.dsw_b;
    case 0x9000: return T.in0;
    case 0x9001: return T.in1;
    case 0x9002: return T.in2;
    default: return 0;
    }
}

static void lnc_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (MMONKEY && a >= 0xb000 && a < 0xc000) protection_write(a, v);
    else if (a < 0x3c00 || (a >= 0xb000 && a < 0xb200)) T.mem[a] = v;
    else if (a < 0x4000) {              // the character bank latch goes with each character
        T.video_ram[a & 0x3ff] = v;
        T.color_ram[a & 0x3ff] = T.charbank;
    } else if (a >= 0x7c00 && a < 0x8000) T.video_ram[mirror_index(a)] = v;
    else if (a == 0x8003) T.charbank = v;
    else if (a == 0x9002) {
        T.sound_latch = v;
        m65xx_set_irq(&T.sound, M65_ASSERT);
    }
}
#endif

#if BTIME
#define MAIN_WRITE main_write
#else
#define main_read lnc_read
#define MAIN_WRITE lnc_write
#endif

static uint8_t swap8(uint8_t v, const int *k)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++)
        if (v & (1 << k[i])) r |= (uint8_t)(1 << (7 - i));
    return r;
}

// BurgerTime: the opcode after a write has bits swapped when the address has
// 0x104 set; the others: bits 5 and 6 swapped in the ROM and RAM
static uint8_t opcode_read(void *u, uint16_t a)
{
#if !BTIME
    const uint8_t w = lnc_read(u, a);
    if (a < 0x3c00 || (a >= 0xb000 && a < 0xb200) || a >= 0xc000) {
        static const int kSwapL[8] = {7, 5, 6, 4, 3, 2, 1, 0};
        return swap8(w, kSwapL);
    }
    return w;
#else
    uint8_t v = main_read(u, a);
    if (T.had_written) {
        T.had_written = false;
        if ((a & 0x104) == 0x104) {
            static const int kSwap[8] = {6, 5, 3, 4, 2, 7, 1, 0};
            v = swap8(v, kSwap);
        }
    }
    return v;
#endif
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
    if (!BTIME) {
    } else if (T.bg_control & 0x10) {          // 16x16 tiles from one of eight maps, colours 8-15
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
                if (pen || !BTIME) T.screen[(ty + r) * 256 + tx + c] = T.palette[pen];
            }
    }
    for (int f = 0; f < 8; f++) {       // 8 sprites in the character RAM, every 0x80
        const uint8_t attr = T.video_ram[f * 0x80];
        if (!(attr & 1)) continue;
        const int x = 240 - T.video_ram[f * 0x80 + 0x40], y = T.video_ram[f * 0x80 + 0x60] - (MMONKEY ? 0 : 1);
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
    if (!ARC_LOAD(kMain, T.mem, 0x10000) || !ARC_LOAD(kSound, T.sound_rom, 0x1000)) return false;
    // the characters and the sprites share the same 3bpp ROMs
    arc_layout cl = { 8, 8, 1024, 3, 64, false, false, true, {2 * 1024 * 64, 1024 * 64, 0}, kX8, kY8 };
    arc_layout sl = { 16, 16, 256, 3, 256, false, false, true, {2 * 256 * 256, 256 * 256, 0}, kX16, kY16 };
    if (!ARC_LOAD(kChars, gfx, 0x6000) || !(T.chars = arc_gfx_decode(&cl, gfx, 0x6000)) ||
        !(T.sprites = arc_gfx_decode(&sl, gfx, 0x6000)))
        return false;
#if BTIME
    arc_layout tl = { 16, 16, 64, 3, 256, false, false, true, {2 * 64 * 256, 64 * 256, 0}, kX16, kY16 };
    memset(gfx, 0, 0x1800);
    if (!ARC_LOAD(kTiles, gfx, 0x1800) || !(T.tiles = arc_gfx_decode(&tl, gfx, 0x1800)) || !ARC_LOAD(kMap, T.bg_map, 0x800))
        return false;
#else
    uint8_t prom[0x20];                 // a fixed palette
    if (!ARC_LOAD(kProm, prom, 0x20)) return false;
    for (int i = 0; i < 0x20; i++) {
        const uint8_t v = prom[i];
        const int r = 0x21 * ((v >> 7) & 1) + 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 5) & 1);
        const int g = 0x21 * ((v >> 4) & 1) + 0x47 * ((v >> 3) & 1) + 0x97 * ((v >> 2) & 1);
        const int b = 0x47 * ((v >> 1) & 1) + 0x97 * (v & 1);
        T.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
#endif
    free(gfx);
    T.main.read = main_read;
    T.main.write = MAIN_WRITE;
    T.main.opcode_read = opcode_read;
    T.sound.read = sound_read;
    T.sound.write = sound_write;
    T.sound.cycles = sound_cycles;
    ay8910_init(&T.ay0, 1500000, RATE);
    ay8910_init(&T.ay1, 1500000, RATE);
    T.dsw_a = 0x3f;
    T.dsw_b = DSW_B;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(T.mem, 0, 0xc000);
    memset(T.sound_ram, 0, sizeof T.sound_ram);
    memset(T.video_ram, 0, sizeof T.video_ram);
    memset(T.color_ram, 0, sizeof T.color_ram);
    if (BTIME)
        for (int i = 0; i < 16; i++) set_color(i, 0);
    T.in0 = T.in1 = 0xff;
    T.in2 = BTIME ? 0x3f : 0xff;
    T.charbank = 1;
    T.prot_status = T.prot_ret = T.prot_command = T.prot_value = 0;
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
#if BTIME
    T.in2 = 0x3f;                       // the coins active high; either holds the NMI
    if (in->coin1) T.in2 |= 0x40;
    else if (in->coin2) T.in2 |= 0x80;
#else
    T.in2 = 0xff;                       // (active low here)
    if (in->coin1) T.in2 &= ~0x40;
    else if (in->coin2) T.in2 &= ~0x80;
#endif
    m65xx_set_nmi(&T.main, in->coin1 || in->coin2 ? M65_ASSERT : M65_CLEAR);
    if (in->p[0] & AP_START) T.in2 &= ~0x01;
    if (in->p[1] & AP_START) T.in2 &= ~0x02;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) T.dsw_a = (uint8_t)((T.dsw_a & 0x80) | (v & 0x7f));
    if (bank == 1) T.dsw_b = v;
}
