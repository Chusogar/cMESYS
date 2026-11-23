// taitosj.c - Elevator Action (Taito, 1983) and Jungle King (Taito, 1982),
// the Taito SJ machine, for the common arcade frontends (arc.h): C port of
// dsp-cpp's drivers/arcade/taitosj (taitosj_hw.pas). A Z80 at 4 MHz with three
// scrolled layers of 8x8 characters and 16x16 sprites from character RAM
// (the graphics ROMs are read through a port), a priority PROM, a sound Z80
// at 3 MHz with four AY-3-8910 and a DAC, and on Elevator Action a 68705
// protection MCU. ARC_VARIANT 1: Jungle King (no MCU, the monitor turned
// round).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/m6805dsp/m6805dsp.h"
#include "../../../sound/ay8910.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define ELEVATOR (ARC_VARIANT == 0)

#define MAIN_CLOCK 4000000
#define SOUND_CLOCK 3000000
#define MCU_CLOCK (3000000 / 4)            // (dsp-cpp's M6805 counts a quarter of its clock)
#define AY_CLOCK 1500000
#define FPS 60.0
#define SCANLINES 256
#define RATE 44100
#define W 256
#define H 224
#define SOUND_IRQ_PERIOD 81920          // 4 * 16 * 16 * 10 * 16 / 2
#define RAM_BASE 0x8000
#define RAM_SIZE (0xd200 - RAM_BASE)
#define SCREEN_TOP 16

#if ELEVATOR
static const char *const kSets[] = { "elevator", NULL };
static const arc_rom kMain[] = {
    {"ba3__01.2764.ic1", 0x2000, 0x0000, 0xda775a24}, {"ba3__02.2764.ic2", 0x2000, 0x2000, 0xfbfd8b3a},
    {"ba3__03-1.2764.ic3", 0x2000, 0x4000, 0xa2e69833}, {"ba3__04-1.2764.ic6", 0x2000, 0x6000, 0x2b78c462}};
static const arc_rom kSound[] = {{"ba3__09.2732.ic70", 0x1000, 0, 0x6d5f57cb}, {"ba3__10.2732.ic71", 0x1000, 0x1000, 0xf0a769a1}};
static const arc_rom kMcu[] = {{"ba3__11.mc68705p3.ic24", 0x800, 0, 0x9ce75afc}};
static const arc_rom kChars[] = {
    {"ba3__05.2764.ic4", 0x2000, 0x0000, 0x6c4ee58f}, {"ba3__06.2764.ic5", 0x2000, 0x2000, 0x41ab0afc},
    {"ba3__07.2764.ic9", 0x2000, 0x4000, 0xefe43731}, {"ba3__08.2764.ic10", 0x2000, 0x6000, 0x3ca20696}};
const arc_info arc_game = { "elevator", "Elevator Action", "Taito", 1983, W, H, FPS, RATE, kSets,
                            { "FIRE", "JUMP", NULL, NULL }, ARC_JOY_8WAY };
static const int8_t kPosX[5] = {-8, -23, -21, -2, 0};
#define DSW_A 0x7f
#else
static const char *const kSets[] = { "junglek", NULL };
static const arc_rom kMain[] = {
    {"kn21-1.bin", 0x1000, 0x0000, 0x45f55d30}, {"kn22-1.bin", 0x1000, 0x1000, 0x07cc9a21}, {"kn43.bin", 0x1000, 0x2000, 0xa20e5a48},
    {"kn24.bin", 0x1000, 0x3000, 0x19ea7f83}, {"kn25.bin", 0x1000, 0x4000, 0x844365ea}, {"kn46.bin", 0x1000, 0x5000, 0x27a95fd5},
    {"kn47.bin", 0x1000, 0x6000, 0x5c3199e0}, {"kn28.bin", 0x1000, 0x7000, 0x194a2d09}, {"kn60.bin", 0x1000, 0x8000, 0x1a9c0a26}};
static const arc_rom kSound[] = {{"kn37.bin", 0x1000, 0, 0xdee7f5d4}, {"kn38.bin", 0x1000, 0x1000, 0xbffd3d21}, {"kn59-1.bin", 0x1000, 0x2000, 0xcee485fc}};
static const arc_rom kChars[] = {
    {"kn29.bin", 0x1000, 0x0000, 0x8f83c290}, {"kn30.bin", 0x1000, 0x1000, 0x89fd19f1}, {"kn51.bin", 0x1000, 0x2000, 0x70e8fc12},
    {"kn52.bin", 0x1000, 0x3000, 0xbcbac1a3}, {"kn53.bin", 0x1000, 0x4000, 0xb946c87d}, {"kn34.bin", 0x1000, 0x5000, 0x320db2e1},
    {"kn55.bin", 0x1000, 0x6000, 0x70aef58f}, {"kn56.bin", 0x1000, 0x7000, 0x932eb667}};
const arc_info arc_game = { "junglek", "Jungle King", "Taito", 1982, W, H, FPS, RATE, kSets,
                            { "JUMP", NULL, NULL, NULL }, ARC_JOY_8WAY };
static const int8_t kPosX[5] = {8, 10, 12, 1, -2};
#define DSW_A 0x3f
#endif
static const arc_rom kProm[] = {{"eb16.22|eb16.ic22", 0x100, 0, 0xb833b5ea}};

static const uint8_t kVolume[256] = {
    0xff, 0xfe, 0xfc, 0xfb, 0xf9, 0xf7, 0xf6, 0xf4, 0xf3, 0xf2, 0xf1, 0xef, 0xee, 0xec, 0xeb, 0xea,
    0xe8, 0xe7, 0xe5, 0xe4, 0xe2, 0xe1, 0xe0, 0xdf, 0xde, 0xdd, 0xdc, 0xdb, 0xd9, 0xd8, 0xd7, 0xd6,
    0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0, 0xcf, 0xce, 0xcd, 0xcc, 0xcb, 0xca, 0xc9, 0xc8, 0xc7, 0xc6,
    0xc5, 0xc4, 0xc3, 0xc2, 0xc1, 0xc0, 0xbf, 0xbf, 0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xba, 0xb9, 0xb8,
    0xb7, 0xb7, 0xb6, 0xb5, 0xb4, 0xb3, 0xb3, 0xb2, 0xb1, 0xb1, 0xb0, 0xaf, 0xae, 0xae, 0xad, 0xac,
    0xab, 0xaa, 0xaa, 0xa9, 0xa8, 0xa8, 0xa7, 0xa6, 0xa6, 0xa5, 0xa5, 0xa4, 0xa3, 0xa2, 0xa2, 0xa1,
    0xa1, 0xa0, 0xa0, 0x9f, 0x9e, 0x9e, 0x9d, 0x9d, 0x9c, 0x9c, 0x9b, 0x9b, 0x9a, 0x99, 0x99, 0x98,
    0x97, 0x97, 0x96, 0x96, 0x95, 0x95, 0x94, 0x94, 0x93, 0x93, 0x92, 0x92, 0x91, 0x91, 0x90, 0x90,
    0x8b, 0x8b, 0x8a, 0x8a, 0x89, 0x89, 0x89, 0x88, 0x88, 0x87, 0x87, 0x87, 0x86, 0x86, 0x85, 0x85,
    0x84, 0x84, 0x83, 0x83, 0x82, 0x82, 0x82, 0x81, 0x81, 0x81, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7e,
    0x7e, 0x7e, 0x7d, 0x7d, 0x7c, 0x7c, 0x7c, 0x7b, 0x7b, 0x7b, 0x7a, 0x7a, 0x7a, 0x79, 0x79, 0x79,
    0x78, 0x78, 0x77, 0x77, 0x77, 0x76, 0x76, 0x76, 0x75, 0x75, 0x75, 0x74, 0x74, 0x74, 0x73, 0x73,
    0x73, 0x73, 0x72, 0x72, 0x72, 0x71, 0x71, 0x71, 0x70, 0x70, 0x70, 0x70, 0x6f, 0x6f, 0x6f, 0x6e,
    0x6e, 0x6e, 0x6d, 0x6d, 0x6d, 0x6c, 0x6c, 0x6c, 0x6c, 0x6b, 0x6b, 0x6b, 0x6b, 0x6a, 0x6a, 0x6a,
    0x6a, 0x69, 0x69, 0x69, 0x68, 0x68, 0x68, 0x68, 0x68, 0x67, 0x67, 0x67, 0x66, 0x66, 0x66, 0x66,
    0x65, 0x65, 0x65, 0x65, 0x64, 0x64, 0x64, 0x64, 0x64, 0x63, 0x63, 0x63, 0x63, 0x62, 0x62, 0x62};

static struct state {
    zd_z80 main, sound;
#if ELEVATOR
    m6805d mcu;
    uint8_t mcu_mem[0x800], mcu_to_z80, mcu_from_z80, mcu_pa_in, mcu_pa_out;
    uint16_t mcu_address;
    bool zaccept, zready, busreq;
#endif
    ay8910 ay[4];
    uint8_t main_rom[0x9000], sound_rom[0x4000], gfx[0x8000], ram[RAM_SIZE], sound_ram[0x400];
    uint8_t order[32][4], palette_ram[0x80], collision[4], scroll[6], colorbank[2], in0, in1, in2, in4, dsw_a, dsw_b, dsw_c;
    uint16_t scroll_y[0x60], gfx_pos;
    uint16_t palette[64];
    double pw[3][8];
    uint8_t priority, sound_latch, rom_bank, video_mode, dac_out, dac_vol;
    int32_t dac_mixed;
    bool semaphore, nmi0, nmi1;
    uint16_t *fb;
    uint8_t screen[W * H];              // pens: coloured when the frame is taken (dsp-cpp's framebuffer())
    int sound_irq_counter;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *T_;
#define T (*T_)

static uint8_t main_rom(uint32_t o) { return o < sizeof T.main_rom ? T.main_rom[o] : 0; }

// the AYs' ports: read on a read of the register (AY 0: the DIP switches B
// and C), written on a write to it and when their direction changes (0xff
// as an input), as dsp-cpp's AY8910
static void dac_update(void)
{
    const int16_t s = (int16_t)(T.dac_out * T.dac_vol - 0x8000);
    T.dac_mixed = (int32_t)(s * 0.30f);
}
static void port_w(int chip, int port, uint8_t v)
{
    if (chip == 1) {
        if (port == 0) T.dac_out = (uint8_t)~v;
        else T.dac_vol = kVolume[v];
        dac_update();
    } else if (chip == 2 && port == 0) T.in4 = v & 0xf0;
    else if (chip == 3 && port == 1) {
        T.nmi0 = (~v & 1) != 0;
        if (T.nmi0 && T.nmi1) zd_set_nmi(&T.sound, ZD_PULSE);
    }
}

static void ay_data_w(int chip, uint8_t v)
{
    ay8910 *ay = &T.ay[chip];
    const int16_t last = ay->last_enable;
    const uint8_t reg = ay->latch;
    ay8910_data_w(ay, v);
    if (reg == 14) port_w(chip, 0, v);
    else if (reg == 15) port_w(chip, 1, v);
    else if (reg == 7) {
        const uint8_t e = ay->regs[7];
        if (last == -1 || ((last & 0x40) != (e & 0x40))) port_w(chip, 0, (e & 0x40) ? ay->regs[14] : 0xff);
        if (last == -1 || ((last & 0x80) != (e & 0x80))) port_w(chip, 1, (e & 0x80) ? ay->regs[15] : 0xff);
    }
}

static uint8_t ay_read(int chip)
{
    ay8910 *ay = &T.ay[chip];
    if (chip == 0) {
        if (ay->latch == 14) ay->regs[14] = T.dsw_b;
        if (ay->latch == 15) ay->regs[15] = T.dsw_c;
    }
    return ay8910_data_r(ay);
}

static void ay_reset_all(void)
{
    for (int i = 0; i < 4; i++) {           // (the reset writes registers 0-13: port 7 with both inputs)
        ay8910_reset(&T.ay[i]);
        port_w(i, 0, 0xff);
        port_w(i, 1, 0xff);
    }
}

static void set_palette_entry(int index)
{
    const uint8_t odd = (uint8_t)~T.palette_ram[index | 1], even = (uint8_t)~T.palette_ram[index];
    const int b = arc_combine(T.pw[2], 3, odd & 7), g = arc_combine(T.pw[1], 3, (odd >> 3) & 7);
    const int r = arc_combine(T.pw[0], 3, ((odd >> 6) & 3) | ((even & 1) << 2));
    T.palette[index >> 1] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
}

// ---------------------------------------------------------------------------
// The main CPU

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x6000) return main_rom(a);
    if (a < 0x8000) {
        const uint32_t o = a & 0x1fff;
        return T.rom_bank == 0 || o < 0x1000 ? main_rom(0x6000 + o) : main_rom(0x8000 + o - 0x1000);
    }
    if (a < 0x8800) return T.ram[a - RAM_BASE];
    if (a < 0x9000) {
#if ELEVATOR
        if (!(a & 1)) {
            T.zaccept = true;
            return T.mcu_to_z80;
        }
        return (uint8_t)~((T.zready ? 1 : 0) | (T.zaccept ? 2 : 0));
#else
        return 0;
#endif
    }
    if (a < 0xd000) return T.ram[a - RAM_BASE];
    if (a < 0xd060) return (uint8_t)T.scroll_y[a & 0x7f];
    if (a >= 0xd200 && a < 0xd300) return T.palette_ram[a & 0x7f];
    if (a >= 0xd400 && a < 0xd500) {
        switch (a & 0x0f) {
        case 0: case 1: case 2: case 3: return T.collision[a & 3];
        case 4: case 5: case 6: case 7: {
            const uint8_t v = T.gfx_pos < 0x8000 ? T.gfx[T.gfx_pos] : 0;
            T.gfx_pos++;
            return v;
        }
        case 8: return T.in0;
        case 9: return T.in1;
        case 0xa: return T.dsw_a;
        case 0xb: return T.in2;
        case 0xc: return 0xef;
        case 0xd: return (uint8_t)(T.in4 | 0x0f);
        case 0xf: return ay_read(0);
        default: return 0;
        }
    }
    return 0;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x8000) return;
    if (a < 0x8800) T.ram[a - RAM_BASE] = v;
    else if (a < 0x9000) {
#if ELEVATOR
        if (!(a & 1)) {
            T.zready = true;
            m6805d_set_irq(&T.mcu, M6805D_ASSERT);
            T.mcu_from_z80 = v;
        }
#endif
    } else if (a < 0xd000) T.ram[a - RAM_BASE] = v;
    else if (a < 0xd060) T.scroll_y[a & 0x7f] = v;
    else if (a >= 0xd100 && a < 0xd200) T.ram[a - RAM_BASE] = v;
    else if (a >= 0xd200 && a < 0xd300) {
        const int e = a & 0x7f;
        if (T.palette_ram[e] != v) {
            T.palette_ram[e] = v;
            set_palette_entry(e & 0xfe);
        }
    } else if (a >= 0xd300 && a < 0xd400) T.priority = v;
    else if (a >= 0xd400 && a < 0xd500) {
        if ((a & 0x0f) == 0x0e) ay8910_address_w(&T.ay[0], v);
        if ((a & 0x0f) == 0x0f) ay_data_w(0, v);
    } else if (a >= 0xd500 && a < 0xd600) {
        switch (a & 0x0f) {
        case 0: case 1: case 2: case 3: case 4: case 5: T.scroll[a & 0x0f] = v; break;
        case 6: case 7: T.colorbank[a & 1] = v; break;
        case 8: memset(T.collision, 0, sizeof T.collision); break;
        case 9: T.gfx_pos = (uint16_t)((T.gfx_pos & 0xff00) | v); break;
        case 0xa: T.gfx_pos = (uint16_t)((T.gfx_pos & 0x00ff) | (v << 8)); break;
        case 0xb:
            T.sound_latch = v;
            T.nmi1 = true;
            if (T.nmi0 && T.nmi1) zd_set_nmi(&T.sound, ZD_PULSE);
            break;
        case 0xc:
            T.semaphore = (v & 1) != 0;
            if (T.semaphore) zd_set_nmi(&T.sound, ZD_PULSE);
            break;
        case 0xe: T.rom_bank = v >> 7; break;
        default: break;
        }
    } else if (a >= 0xd600 && a < 0xd700) T.video_mode = v;
}

// ---------------------------------------------------------------------------
// The sound CPU

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x4000) return T.sound_rom[a];
    if (a <= 0x43ff) return T.sound_ram[a & 0x3ff];
    switch (a) {
    case 0x4801: return ay_read(1);
    case 0x4803: return ay_read(2);
    case 0x4805: return ay_read(3);
    case 0x5000:
        T.nmi1 = false;
        return T.sound_latch;
    case 0x5001: return (uint8_t)((T.nmi1 ? 8 : 0) | (T.semaphore ? 4 : 0) | 3);
    default: return 0;
    }
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x4000 && a <= 0x43ff) T.sound_ram[a & 0x3ff] = v;
    else switch (a) {
    case 0x4800: ay8910_address_w(&T.ay[1], v); break;
    case 0x4801: ay_data_w(1, v); break;
    case 0x4802: ay8910_address_w(&T.ay[2], v); break;
    case 0x4803: ay_data_w(2, v); break;
    case 0x4804: ay8910_address_w(&T.ay[3], v); break;
    case 0x4805: ay_data_w(3, v); break;
    case 0x5000: T.sound_latch &= 0x7f; break;
    case 0x5001: T.semaphore = false; break;
    default: break;
    }
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    T.sound_irq_counter += c;
    while (T.sound_irq_counter >= SOUND_IRQ_PERIOD) {
        T.sound_irq_counter -= SOUND_IRQ_PERIOD;
        zd_set_irq(&T.sound, ZD_HOLD, 0xff);
    }
    T.audio_acc += (int64_t)c * RATE;
    while (T.audio_acc >= SOUND_CLOCK) {
        T.audio_acc -= SOUND_CLOCK;
        const int32_t s = ay8910_render(&T.ay[0]) + ay8910_render(&T.ay[1]) + ay8910_render(&T.ay[2]) + ay8910_render(&T.ay[3]) + T.dac_mixed;
        if (T.naudio < 1024) T.audio[T.naudio++] = arc_clamp16(s);
    }
}

#if ELEVATOR
// ---------------------------------------------------------------------------
// The 68705: port A the data, port B the strobes of its Z80 interface

static uint8_t bus_peek(uint16_t a)
{
    if (a < 0x6000) return main_rom(a);
    if (a >= RAM_BASE && a < RAM_BASE + RAM_SIZE) return T.ram[a - RAM_BASE];
    return 0;
}

static uint8_t mcu_read(void *u, uint16_t a)
{
    (void)u;
    a &= 0x7ff;
    switch (a) {
    case 0: return T.mcu_pa_in;
    case 1: return 0xff;
    case 2: return (uint8_t)((T.zready ? 1 : 0) | (T.zaccept ? 2 : 0) | (T.busreq ? 0 : 4));
    default: return T.mcu_mem[a];
    }
}

static void mcu_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    a &= 0x7ff;
    if (a == 0) T.mcu_pa_out = v;
    else if (a == 1) {
        if (~v & 0x01) return;
        if (~v & 0x02) {
            T.zready = false;
            m6805d_set_irq(&T.mcu, M6805D_CLEAR);
            T.mcu_pa_in = T.mcu_from_z80;
        }
        T.busreq = (~v & 0x08) != 0;
        if (~v & 0x04) {
            T.mcu_to_z80 = T.mcu_pa_out;
            T.zaccept = false;
        }
        if (~v & 0x10) {
            if (T.mcu_address >= RAM_BASE && T.mcu_address < RAM_BASE + RAM_SIZE) T.ram[T.mcu_address - RAM_BASE] = T.mcu_pa_out;
            T.mcu_address = (uint16_t)((T.mcu_address & 0xff00) | ((T.mcu_address + 1) & 0xff));
        }
        if (~v & 0x20) T.mcu_pa_in = bus_peek(T.mcu_address);
        if (~v & 0x40) T.mcu_address = (uint16_t)((T.mcu_address & 0xff00) | T.mcu_pa_out);
        if (~v & 0x80) T.mcu_address = (uint16_t)((T.mcu_address & 0x00ff) | (T.mcu_pa_out << 8));
    } else if (a >= 3 && a <= 0x7f) T.mcu_mem[a] = v;
}
#endif

// ---------------------------------------------------------------------------
// Video: composed a row at a time, 3bpp character RAM (planes $1000 apart
// ... $800, $000), the leftmost pixel in bit 0

#define CHAR_BANK(b) ((b) ? 0xa800 - RAM_BASE : 0x9000 - RAM_BASE)

typedef struct {
    const uint8_t *chars, *map;
    uint16_t scroll_x;
    uint8_t color;
    int offset_y[32];
} layer_t;

static void layer_setup(layer_t *f, int layer, uint16_t scroll_x, uint8_t scroll_y, int column_base)
{
    static const uint16_t kMaps[3] = {0xc400 - RAM_BASE, 0xc800 - RAM_BASE, 0xcc00 - RAM_BASE};
    f->color = layer == 2 ? (uint8_t)((T.colorbank[1] & 7) << 3) : layer == 1 ? (uint8_t)(((T.colorbank[0] >> 4) & 7) << 3)
                                                                                : (uint8_t)((T.colorbank[0] & 7) << 3);
    const int bank = layer == 2 ? (T.colorbank[1] & 8) >> 3 : layer == 1 ? (T.colorbank[0] & 0x80) >> 7 : (T.colorbank[0] & 8) >> 3;
    f->chars = T.ram + CHAR_BANK(bank);
    f->map = T.ram + kMaps[layer];
    f->scroll_x = scroll_x;
    for (int g = 0; g < 32; g++) f->offset_y[g] = T.scroll_y[column_base + ((g + scroll_x / 8) & 0x1f)] + scroll_y;
}

static void layer_row(const layer_t *f, int y, uint8_t *line)
{
    for (int x0 = 0; x0 < 256; x0 += 8) {
        const int first_x = (x0 + f->scroll_x) & 0xff;
        const int t0 = first_x >> 3, t1 = (t0 + 1) & 0x1f, shift = first_x & 7;
        const int sy = (y + SCREEN_TOP + f->offset_y[x0 >> 3]) & 0xff;
        const uint8_t *row = f->map + (sy >> 3) * 32;
        const size_t a = (size_t)row[t0] * 8 + (sy & 7), b = (size_t)row[t1] * 8 + (sy & 7);
        const unsigned p2 = ((f->chars[a + 0x1000] | (f->chars[b + 0x1000] << 8)) >> shift) & 0xff;
        const unsigned p1 = ((f->chars[a + 0x800] | (f->chars[b + 0x800] << 8)) >> shift) & 0xff;
        const unsigned p0 = ((f->chars[a] | (f->chars[b] << 8)) >> shift) & 0xff;
        for (int x = 0; x < 8; x++) {
            const uint8_t pen = (uint8_t)(((p2 >> x) & 1) << 2 | ((p1 >> x) & 1) << 1 | ((p0 >> x) & 1));
            if (pen) line[x0 + x] = (uint8_t)(f->color + pen);
        }
    }
}

typedef struct { const uint8_t *px; uint8_t sx, sy, color; bool fx, fy; } sprite_t;

static int sprites_setup(sprite_t *out)
{
    const int base = 0xd100 - RAM_BASE + ((T.video_mode & 4) << 5);
    int n = 0;
    for (int index = 0x1f; index >= 0; index--) {
        const int which = (index - 1) & 0x1f;
        if (which >= 0x10 && which <= 0x17) continue;
        const uint8_t *e = T.ram + base + which * 4;
        sprite_t *s = &out[n];
        s->sx = (uint8_t)(e[0] + kPosX[3]);
        s->sy = (uint8_t)(240 - e[1] + kPosX[4]);
        if (s->sy >= 240) continue;
        const uint8_t attr = e[2];
        s->color = (uint8_t)((2 * ((T.colorbank[1] >> 4) & 3) + ((attr >> 2) & 1)) << 3);
        s->fx = attr & 1;
        s->fy = (attr & 2) != 0;
        s->px = T.ram + CHAR_BANK((e[3] & 0x40) ? 1 : 0) + (e[3] & 0x3f) * 32;
        n++;
    }
    return n;
}

static void sprites_row(const sprite_t *sp, int n, int y, uint8_t *line)
{
    const int screen_y = y + SCREEN_TOP;
    for (int i = 0; i < n; i++) {
        const int r = screen_y - sp[i].sy;
        if (r < 0 || r >= 16) continue;
        const int src_y = sp[i].fy ? 15 - r : r;
        const uint8_t *row = sp[i].px + (src_y < 8 ? src_y : src_y + 8);
        for (int x = 0; x < 16; x++) {
            const int tx = sp[i].sx + x;
            if (tx >= 256) continue;
            const int sx = sp[i].fx ? 15 - x : x;
            const size_t o = sx < 8 ? 0 : 8;
            const int bit = sx & 7;
            const uint8_t pen = (uint8_t)((((row[o + 0x1000] >> bit) & 1) << 2) | (((row[o + 0x800] >> bit) & 1) << 1) | ((row[o] >> bit) & 1));
            if (pen) line[tx] = (uint8_t)(sp[i].color + pen);
        }
    }
}

static void update_video(void)
{
    layer_t layers[3];
    bool on[3] = {false, false, false};
    if (T.video_mode & 0x10) {
        const uint16_t x = T.scroll[0];
        layer_setup(&layers[0], 0, (uint16_t)((x & 0xf8) + ((x + 3) & 7) + kPosX[0]), T.scroll[1], 0);
        on[0] = true;
    }
    if (T.video_mode & 0x20) {
        const uint16_t x = T.scroll[2];
        layer_setup(&layers[1], 1, (uint16_t)((x & 0xf8) + ((x + 1) & 7) + kPosX[1]), T.scroll[3], 32);
        on[1] = true;
    }
    if (T.video_mode & 0x40) {
        const uint16_t x = T.scroll[4];
        layer_setup(&layers[2], 2, (uint16_t)((x & 0xf8) + ((x - 1) & 7) + kPosX[2]), T.scroll[5], 64);
        on[2] = true;
    }
    sprite_t sp[32];
    const int nsp = (T.video_mode & 0x80) ? sprites_setup(sp) : 0;
    const uint8_t *order = T.order[T.priority & 0x1f];
    const uint8_t bg = (uint8_t)((T.colorbank[1] & 7) << 3);
    const bool turned = !ELEVATOR;     // Jungle King's monitor is turned round
    const bool fx = ((T.video_mode & 1) != 0) != turned, fy = ((T.video_mode & 2) != 0) != turned;
    uint8_t line[256];
    for (int y = 0; y < H; y++) {
        memset(line, bg, sizeof line);
        for (int s = 0; s < 4; s++) {
            const int what = order[s];
            if (what == 0) sprites_row(sp, nsp, y, line);
            else if (what <= 3 && on[what - 1]) layer_row(&layers[what - 1], y, line);
        }
        uint8_t *out = T.screen + (fy ? H - 1 - y : y) * W;
        for (int x = 0; x < W; x++) out[x] = line[fx ? 255 - x : x];
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

bool arc_init(void)
{
    if (!(T_ = (struct state *)calloc(1, sizeof(struct state))) || !(T.fb = (uint16_t *)calloc(W * H, 2))) return false;
    uint8_t prom[0x100];
    if (!ARC_LOAD(kMain, T.main_rom, sizeof T.main_rom) || !ARC_LOAD(kSound, T.sound_rom, sizeof T.sound_rom) ||
        !ARC_LOAD(kChars, T.gfx, sizeof T.gfx) || !ARC_LOAD(kProm, prom, sizeof prom))
        return false;
#if ELEVATOR
    if (!ARC_LOAD(kMcu, T.mcu_mem, sizeof T.mcu_mem)) return false;
#endif
    for (int code = 0; code < 32; code++) {    // each priority code's drawing order, back to front
        uint8_t mask = 0;
        for (int slot = 3; slot >= 0; slot--) {
            uint8_t d = prom[0x10 * (code & 0x0f) + mask] & 0x0f;
            d = (code & 0x10) ? (uint8_t)(d >> 2) : (uint8_t)(d & 3);
            mask |= (uint8_t)(1 << d);
            T.order[code][slot] = d;
        }
    }
    static const arc_resnet nets[3] = {{3, {1000, 470, 270}, 0, 0}, {3, {1000, 470, 270}, 0, 0}, {3, {1000, 470, 270}, 0, 0}};
    arc_resistor_weights(0, 255, -1.0, nets, 3, T.pw);
    T.main.read = main_read;
    T.main.write = main_write;
    T.sound.read = sound_read;
    T.sound.write = sound_write;
    T.sound.cycles = sound_cycles;
    T.main.in = T.sound.in = in_ff;
    T.main.out = T.sound.out = out_nop;
    zd_init(&T.main);
    zd_init(&T.sound);
#if ELEVATOR
    T.mcu.read = mcu_read;
    T.mcu.write = mcu_write;
    T.mcu.m68705 = true;
#endif
    static const float kAmp[4] = {0.3f, 1.0f, 1.0f, 2.0f};
    for (int i = 0; i < 4; i++) {
        ay8910_init(&T.ay[i], AY_CLOCK, RATE);
        ay8910_set_amplitude(&T.ay[i], kAmp[i]);
    }
    T.dsw_a = DSW_A;
    T.dsw_b = 0x00;
    T.dsw_c = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&T.main);
    zd_reset(&T.sound);
#if ELEVATOR
    m6805d_reset(&T.mcu);
    T.zaccept = true;
    T.zready = T.busreq = false;
    T.mcu_to_z80 = T.mcu_from_z80 = T.mcu_pa_in = T.mcu_pa_out = 0;
    T.mcu_address = 0;
#endif
    ay_reset_all();
    T.in0 = T.in1 = T.in2 = 0xff;
    T.in4 = 0;
    memset(T.collision, 0, sizeof T.collision);
    memset(T.scroll, 0, sizeof T.scroll);
    memset(T.colorbank, 0, sizeof T.colorbank);
    T.gfx_pos = 0;
    T.priority = T.rom_bank = T.video_mode = 0;
    T.dac_out = T.dac_vol = 0;
    T.dac_mixed = 0;
    T.sound_latch = 0;
    T.semaphore = T.nmi0 = T.nmi1 = false;
    T.sound_irq_counter = 0;
    T.audio_acc = 0;
    memset(T.screen, 0, sizeof T.screen);
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)(MAIN_CLOCK / FPS / SCANLINES), sc = (int)(SOUND_CLOCK / FPS / SCANLINES);
    T.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            zd_set_irq(&T.main, ZD_HOLD, 0xff);
            update_video();
        }
        zd_run(&T.main, mc);
        zd_run(&T.sound, sc);
#if ELEVATOR
        m6805d_run(&T.mcu, (int)(MCU_CLOCK / FPS / SCANLINES));
#endif
    }
    for (int i = 0; i < W * H; i++) T.fb[i] = T.palette[T.screen[i] & 0x3f];   // (the palette at the frame's end)
    if (audio) *audio = T.audio;
    return T.naudio;
}

const uint16_t *arc_frame(void) { return T.fb; }

void arc_input(const arc_in *in)
{
    uint8_t p[2] = {0xff, 0xff};
    for (int i = 0; i < 2; i++) {
        const unsigned b = in->p[i];
        if (b & AP_LEFT) p[i] &= 0xfe;
        if (b & AP_RIGHT) p[i] &= 0xfd;
        if (b & AP_DOWN) p[i] &= 0xfb;
        if (b & AP_UP) p[i] &= 0xf7;
        if (b & AP_B1) p[i] &= 0xef;
        if (b & AP_B2) p[i] &= 0xdf;
    }
    T.in0 = p[0];
    T.in1 = p[1];
    T.in2 = 0xff;
    if (in->coin2) T.in2 &= 0xef;
    if (in->coin1) T.in2 &= 0xdf;
    if (in->p[0] & AP_START) T.in2 &= 0xbf;
    if (in->p[1] & AP_START) T.in2 &= 0x7f;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) T.dsw_a = v;
    else if (bank == 1) T.dsw_b = v;
    else if (bank == 2) T.dsw_c = v;
}
