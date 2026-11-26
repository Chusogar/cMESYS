// hangon.c - Sega's Hang-On board (1985-1986) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/hangon (after dsp-emulator's
// hangon_hw.pas). Two 68000s (cpu/m68kdsp) sharing the road RAM, the System
// 16 era tilemaps, text layer, sprites and road (video/sega16d), two 8255s
// (one with the port A handshake that raises the sound Z80's NMI) and an
// ADC0804 for the analog controls; a Z80 with a YM2203 (or a YM2151) and the
// Sega PCM. Enduro Racer's program is encrypted (FD1089B); Space Harrier has
// an i8751 that raises the main CPU's interrupts.
// ARC_VARIANT: 0 Hang-On, 1 Enduro Racer, 2 Space Harrier.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mcs51dsp/mcs51dsp.h"
#include "../../../machine/i8255dsp.h"
#include "../../../machine/fd1089d.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/segapcmd.h"
#include "../../../video/sega16d.h"

#define HANGON (ARC_VARIANT == 0)
#define ENDURO (ARC_VARIANT == 1)
#define SHARRIER (ARC_VARIANT == 2)

#define W 320
#define H 224
#define FPS 60.0
#define SCANLINES 262
#define SOUND_CLOCK 4000000
#define RATE 44100
#define MIX_GAIN 1.8
#if HANGON
#define MAIN_CLOCK (25174800 / 4)
#else
#define MAIN_CLOCK 10000000
#endif
#define CPU_SYNC (ENDURO ? 12 : 2)

#define N(k) ((int)(sizeof k / sizeof k[0]))
static const arc_rom kZoom[] = {{"epr-6844.ic123", 0x2000, 0, 0xe3ec7bd6}};

#if HANGON
static const char *const kSets[] = { "hangon", NULL };
const arc_info arc_game = { "hangon", "Hang-On", "Sega", 1985, W, H, FPS, RATE, kSets,
                            { "ACELERAR", "FRENAR", NULL }, ARC_JOY_2WAY };
static const arc_rom kMain[] = {{"epr-6918a.ic22", 0x8000, 0x00000, 0x20b1c2b0}, {"epr-6916a.ic8", 0x8000, 0x00001, 0x7d9db1bf},
                                {"epr-6917a.ic20", 0x8000, 0x10000, 0xfea12367}, {"epr-6915a.ic6", 0x8000, 0x10001, 0xac883240}};
static const arc_rom kSub[] = {{"epr-6920.ic63", 0x8000, 0, 0x1c95013e}, {"epr-6919.ic51", 0x8000, 1, 0x6ca30d69}};
static const arc_rom kSound[] = {{"epr-6833.ic73", 0x4000, 0, 0x3b942f5f}};
static const arc_rom kTiles[] = {{"epr-6841.ic38", 0x8000, 0x0000, 0x54d295dc}, {"epr-6842.ic23", 0x8000, 0x8000, 0xf677b568},
                                 {"epr-6843.ic7", 0x8000, 0x10000, 0xa257f0da}};
static const arc_rom kSprites[] = {
    {"epr-6819.ic27", 0x8000, 0x00000, 0x469dad07}, {"epr-6820.ic34", 0x8000, 0x00001, 0x87cbc6de},
    {"epr-6821.ic28", 0x8000, 0x10000, 0x15792969}, {"epr-6822.ic35", 0x8000, 0x10001, 0xe9718de5},
    {"epr-6823.ic29", 0x8000, 0x20000, 0x49422691}, {"epr-6824.ic36", 0x8000, 0x20001, 0x701deaa4},
    {"epr-6825.ic30", 0x8000, 0x30000, 0x6e23c8b4}, {"epr-6826.ic37", 0x8000, 0x30001, 0x77d0de2c},
    {"epr-6827.ic31", 0x8000, 0x40000, 0x7fa1bfb6}, {"epr-6828.ic38", 0x8000, 0x40001, 0x8e880c93},
    {"epr-6829.ic32", 0x8000, 0x50000, 0x7ca0952d}, {"epr-6830.ic39", 0x8000, 0x50001, 0xb1a63aef},
    {"epr-6845.ic18", 0x8000, 0x60000, 0xba08c9b8}, {"epr-6846.ic25", 0x8000, 0x60001, 0xf21e57a3}};
static const arc_rom kRoad[] = {{"epr-6840.ic108", 0x8000, 0, 0x581230e3}};
static const arc_rom kPcm[] = {{"epr-6831.ic5", 0x8000, 0x0000, 0xcfef5481}, {"epr-6832.ic6", 0x8000, 0x8000, 0x4165aea5}};
#define SPRITE_BANKS 7
#define DSW_B 0xfffe
#elif ENDURO
static const char *const kSets[] = { "enduror", NULL };
const arc_info arc_game = { "enduror", "Enduro Racer", "Sega", 1986, W, H, FPS, RATE, kSets,
                            { "ACELERAR", "FRENAR", "CABALLITO", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"epr-7640a.ic97", 0x8000, 0x00000, 0x1d1dc5d4}, {"epr-7636a.ic84", 0x8000, 0x00001, 0x84131639},
                                {"epr-7641.ic98", 0x8000, 0x10000, 0x2503ae7c}, {"epr-7637.ic85", 0x8000, 0x10001, 0x82a27a8c},
                                {"epr-7642.ic99", 0x8000, 0x20000, 0x1c453bea}, {"epr-7638.ic86", 0x8000, 0x20001, 0x70544779}};
static const arc_rom kSub[] = {{"epr-7634a.ic54", 0x8000, 0, 0xaec83731}, {"epr-7635a.ic67", 0x8000, 1, 0xb2fce96f}};
static const arc_rom kSound[] = {{"epr-7682.ic58", 0x8000, 0, 0xc4efbf48}};
static const arc_rom kTiles[] = {{"epr-7644.ic31", 0x8000, 0x0000, 0xe7a4ff90}, {"epr-7645.ic46", 0x8000, 0x8000, 0x4caa0095},
                                 {"epr-7646.ic60", 0x8000, 0x10000, 0x7e432683}};
static const arc_rom kSprites[] = {
    {"epr-7678.ic36", 0x8000, 0x00000, 0x9fb5e656}, {"epr-7670.ic28", 0x8000, 0x00001, 0xdbbe2f6e},
    {"epr-7662.ic18", 0x8000, 0x00002, 0xcb0c13c5}, {"epr-7654.ic8", 0x8000, 0x00003, 0x2db6520d},
    {"epr-7677.ic35", 0x8000, 0x20000, 0x7764765b}, {"epr-7669.ic27", 0x8000, 0x20001, 0xf9525faa},
    {"epr-7661.ic17", 0x8000, 0x20002, 0xfe93a79b}, {"epr-7653.ic7", 0x8000, 0x20003, 0x46a52114},
    {"epr-7676.ic34", 0x8000, 0x40000, 0x2e42e0d4}, {"epr-7668.ic26", 0x8000, 0x40001, 0xe115ce33},
    {"epr-7660.ic16", 0x8000, 0x40002, 0x86dfbb68}, {"epr-7652.ic6", 0x8000, 0x40003, 0x2880cfdb},
    {"epr-7675.ic33", 0x8000, 0x60000, 0x05cd2d61}, {"epr-7667.ic25", 0x8000, 0x60001, 0x923bde9d},
    {"epr-7659.ic15", 0x8000, 0x60002, 0x629dc8ce}, {"epr-7651.ic5", 0x8000, 0x60003, 0xd7902bad},
    {"epr-7674.ic32", 0x8000, 0x80000, 0x1a129acf}, {"epr-7666.ic24", 0x8000, 0x80001, 0x23697257},
    {"epr-7658.ic14", 0x8000, 0x80002, 0x1677f24f}, {"epr-7650.ic4", 0x8000, 0x80003, 0x642635ec},
    {"epr-7673.ic31", 0x8000, 0xa0000, 0x82602394}, {"epr-7665.ic23", 0x8000, 0xa0001, 0x12d77607},
    {"epr-7657.ic13", 0x8000, 0xa0002, 0x8158839c}, {"epr-7649.ic3", 0x8000, 0xa0003, 0x4edba14c},
    {"epr-7672.ic30", 0x8000, 0xc0000, 0xd11452f7}, {"epr-7664.ic22", 0x8000, 0xc0001, 0x0df2cfad},
    {"epr-7656.ic12", 0x8000, 0xc0002, 0x6c741272}, {"epr-7648.ic2", 0x8000, 0xc0003, 0x983ea830},
    {"epr-7671.ic29", 0x8000, 0xe0000, 0xb0c7fdc6}, {"epr-7663.ic21", 0x8000, 0xe0001, 0x2b0b8f08},
    {"epr-7655.ic11", 0x8000, 0xe0002, 0x3433fe7b}, {"epr-7647.ic1", 0x8000, 0xe0003, 0x2e7fbec0}};
static const arc_rom kRoad[] = {{"epr-7633.ic1", 0x8000, 0, 0x6f146210}};
static const arc_rom kKey[] = {{"317-0013a.key", 0x2000, 0, 0xa965b2da}};
static const arc_rom kPcm[] = {{"epr-7681.ic8", 0x8000, 0x00000, 0xbc0c4d12}, {"epr-7680.ic7", 0x8000, 0x10000, 0x627b3c8c}};
#define SPRITE_BANKS 8
#define DSW_B 0xff7e
#else
static const char *const kSets[] = { "sharrier", NULL };
const arc_info arc_game = { "sharrier", "Space Harrier", "Sega", 1985, W, H, FPS, RATE, kSets,
                            { "DISPARO", "DISPARO 2", "DISPARO 3", NULL }, ARC_JOY_8WAY };
static const arc_rom kMain[] = {{"epr-7188a.ic97", 0x8000, 0x00000, 0x45e173c3}, {"epr-7184a.ic84", 0x8000, 0x00001, 0xe1934a51},
                                {"epr-7189.ic98", 0x8000, 0x10000, 0x40b1309f}, {"epr-7185.ic85", 0x8000, 0x10001, 0xce78045c},
                                {"epr-7190.ic99", 0x8000, 0x20000, 0xf6391091}, {"epr-7186.ic86", 0x8000, 0x20001, 0x79b367d7},
                                {"epr-7191.ic100", 0x8000, 0x30000, 0x6171e9d3}, {"epr-7187.ic87", 0x8000, 0x30001, 0x70cb72ef}};
static const arc_rom kSub[] = {{"epr-7182.ic54", 0x8000, 0, 0xd7c535b6}, {"epr-7183.ic67", 0x8000, 1, 0xa6153af8}};
static const arc_rom kSound[] = {{"epr-7234.ic73", 0x4000, 0x0000, 0xd6397933}, {"epr-7233.ic72", 0x4000, 0x4000, 0x504e76d9}};
static const arc_rom kTiles[] = {{"epr-7196.ic31", 0x8000, 0x0000, 0x347fa325}, {"epr-7197.ic46", 0x8000, 0x8000, 0x39d98bd1},
                                 {"epr-7198.ic60", 0x8000, 0x10000, 0x3da3ea6b}};
static const arc_rom kSprites[] = {
    {"epr-7230.ic36", 0x8000, 0x00000, 0x93e2d264}, {"epr-7222.ic28", 0x8000, 0x00001, 0xedbf5fc3},
    {"epr-7214.ic18", 0x8000, 0x00002, 0xe8c537d8}, {"epr-7206.ic8", 0x8000, 0x00003, 0x22844fa4},
    {"epr-7229.ic35", 0x8000, 0x20000, 0xcd6e7500}, {"epr-7221.ic27", 0x8000, 0x20001, 0x41f25a9c},
    {"epr-7213.ic17", 0x8000, 0x20002, 0x5bb09a67}, {"epr-7205.ic7", 0x8000, 0x20003, 0xdcaa2ebf},
    {"epr-7228.ic34", 0x8000, 0x40000, 0xd5e15e66}, {"epr-7220.ic26", 0x8000, 0x40001, 0xac62ae2e},
    {"epr-7212.ic16", 0x8000, 0x40002, 0x9c782295}, {"epr-7204.ic6", 0x8000, 0x40003, 0x3711105c},
    {"epr-7227.ic33", 0x8000, 0x60000, 0x60d7c1bb}, {"epr-7219.ic25", 0x8000, 0x60001, 0xf6330038},
    {"epr-7211.ic15", 0x8000, 0x60002, 0x60737b98}, {"epr-7203.ic5", 0x8000, 0x60003, 0x70fb5ebb},
    {"epr-7226.ic32", 0x8000, 0x80000, 0x6d7b5c97}, {"epr-7218.ic24", 0x8000, 0x80001, 0xcebf797c},
    {"epr-7210.ic14", 0x8000, 0x80002, 0x24596a8b}, {"epr-7202.ic4", 0x8000, 0x80003, 0xb537d082},
    {"epr-7225.ic31", 0x8000, 0xa0000, 0x5e784271}, {"epr-7217.ic23", 0x8000, 0xa0001, 0x510e5e10},
    {"epr-7209.ic13", 0x8000, 0xa0002, 0x7a2dad15}, {"epr-7201.ic3", 0x8000, 0xa0003, 0xf5ba4e08},
    {"epr-7224.ic30", 0x8000, 0xc0000, 0xec42c9ef}, {"epr-7216.ic22", 0x8000, 0xc0001, 0x6d4a7d7a},
    {"epr-7208.ic12", 0x8000, 0xc0002, 0x0f732717}, {"epr-7200.ic2", 0x8000, 0xc0003, 0xfc3bf8f3},
    {"epr-7223.ic29", 0x8000, 0xe0000, 0xed51fdc4}, {"epr-7215.ic21", 0x8000, 0xe0001, 0xdfe75f3d},
    {"epr-7207.ic11", 0x8000, 0xe0002, 0xa2c07741}, {"epr-7199.ic1", 0x8000, 0xe0003, 0xb191e22f}};
static const arc_rom kRoad[] = {{"epr-7181.ic2", 0x8000, 0, 0xb4740419}};
static const arc_rom kMcu[] = {{"315-5163a.ic32", 0x1000, 0, 0x203dffeb}};
static const arc_rom kPcm[] = {{"epr-7231.ic5", 0x8000, 0x0000, 0x871c6b14}, {"epr-7232.ic6", 0x8000, 0x8000, 0x4b59340c}};
#define SPRITE_BANKS 8
#define DSW_B 0xfffc
#endif

static struct state {
    m68kd main, sub;
    zd_z80 snd;
    ym2203d ym2203;
    ym2151 ym2151;
    segapcmd pcm;
    i8255d ppi0, ppi1;
#if SHARRIER
    mcs51 mcu;
    bool has_mcu;
    uint8_t i8751_addr;
#endif
    s16video video;
    uint16_t *rom, *rom_data, *rom2, *sprite16;
    uint32_t *sprite32;
    uint32_t rom_words, rom2_words, sprite_count;
    uint8_t *zoom, *pcm_rom, *road_gfx;
    uint32_t zoom_size, pcm_size;
    uint16_t ram[0x2000], ram2[0x2000], road_ram[0x800];
    uint8_t sound_mem[0x10000];
    uint32_t *frame, *bg, *fg, *text_low, *text_high;
    uint16_t fb[W * H];
    uint8_t adc_select, sound_latch, control_res;
    uint16_t in0, dsw_a, dsw_b;
    bool z80_reset, sound_mute;
    int64_t audio_acc, pcm_acc, pcm_sum;
    int pcm_count;
    int32_t pcm_last;
    double main_debt, sub_debt, sound_debt, mcu_debt;
    int steer, stick_y, gas, brake, lean;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

// ---------------------------------------------------------------------------
// ROM loading, as dsp-cpp's sega16 helpers

static uint32_t span(const arc_rom *l, int n, int mul, int lane_mask)
{
    uint32_t bytes = 0;
    for (int i = 0; i < n; i++) {
        const uint32_t e = (l[i].offset & ~(uint32_t)lane_mask) + l[i].size * (uint32_t)mul;
        if (e > bytes) bytes = e;
    }
    return bytes;
}

static bool load_part(const arc_rom *r, uint8_t *buf)
{
    arc_rom one = *r;
    one.offset = 0;
    return arc_load(&one, 1, buf, one.size);
}

static uint8_t *load_bytes(const arc_rom *l, int n, uint32_t *size)
{
    const uint32_t bytes = span(l, n, 1, 0);
    uint8_t *d = (uint8_t *)calloc(1, bytes);
    if (d && !arc_load(l, n, d, bytes)) {
        free(d);
        d = NULL;
    }
    if (size) *size = bytes;
    return d;
}

static uint16_t *load16w(const arc_rom *l, int n, uint32_t *words)   // even offset: the high byte
{
    *words = span(l, n, 2, 1) / 2;
    uint16_t *d = (uint16_t *)calloc(*words, 2);
    uint8_t *part = (uint8_t *)malloc(0x10000);
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

static uint16_t *load16b(const arc_rom *l, int n, uint32_t *words)   // interleaved, little-endian words
{
    const uint32_t bytes = span(l, n, 2, 0);
    uint8_t *raw = (uint8_t *)calloc(1, bytes), *part = (uint8_t *)malloc(0x10000);
    uint16_t *d = (uint16_t *)malloc(bytes);
    bool ok = raw && part && d;
    for (int i = 0; ok && i < n; i++) {
        if (!(ok = load_part(&l[i], part))) break;
        for (uint32_t j = 0; j < l[i].size; j++) raw[l[i].offset + j * 2] = part[j];
    }
    *words = bytes / 2;
    if (ok)
        for (uint32_t i = 0; i < *words; i++) d[i] = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
    free(raw);
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
    uint8_t *part = (uint8_t *)malloc(0x10000);
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

// ---------------------------------------------------------------------------

static uint8_t adc(int channel)
{
#if HANGON
    switch (channel & 3) {
    case 0: return (uint8_t)S.steer;
    case 1: return (uint8_t)S.gas;
    case 2: return (uint8_t)S.brake;
    default: return 0;
    }
#elif SHARRIER
    switch (channel & 3) {
    case 0: return (uint8_t)S.steer;
    case 1: return (uint8_t)S.stick_y;
    default: return 0;
    }
#else
    switch (channel & 3) {
    case 0: return (uint8_t)S.gas;
    case 1: return (uint8_t)S.brake;
    case 2: return (uint8_t)S.lean;
    default: return (uint8_t)S.steer;
    }
#endif
}

static void update_controls(void) { S.control_res = adc(S.adc_select); }   // (the ADC0804's conversion)

static void tile_write(uint16_t offset, uint16_t v)
{
    if (S.video.tile_ram[offset] != v) {
        S.video.tile_ram[offset] = v;
        s16_mark_tile(&S.video, offset);
    }
}

static void char_write(uint16_t offset, uint16_t v)
{
    if (S.video.char_ram[offset] != v) {
        S.video.char_ram[offset] = v;
        S.video.text_dirty[offset] = true;
    }
    s16_screen_select_hangon(&S.video, offset);
}

#if HANGON
static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x3ffff) return S.rom[(a >> 1) % S.rom_words];
    if (a >= 0x20c000 && a <= 0x20ffff) return S.ram[(a & 0x3fff) >> 1];
    if (a >= 0x400000 && a <= 0x403fff) return S.video.tile_ram[(a & 0x3fff) >> 1];
    if (a >= 0x410000 && a <= 0x410fff) return S.video.char_ram[(a & 0xfff) >> 1];
    if (a >= 0x600000 && a <= 0x6007ff) return S.video.sprite_ram[(a & 0x7ff) >> 1];
    if (a >= 0xa00000 && a <= 0xa00fff) return S.video.pal_ram[(a & 0xfff) >> 1];
    if (a >= 0xc00000 && a <= 0xc3ffff) return S.rom2[(a >> 1) % S.rom2_words];
    if (a >= 0xc68000 && a <= 0xc68fff) return S.road_ram[(a & 0xfff) >> 1];
    if (a >= 0xc7c000 && a <= 0xc7ffff) return S.ram2[(a & 0x3fff) >> 1];
    if (a >= 0xe00000 && a <= 0xe00fff) return i8255d_read(&S.ppi0, (int)((a & 7) >> 1));
    if (a >= 0xe01000 && a <= 0xe01fff) {
        switch ((a & 7) >> 1) {
        case 0: return S.in0;
        case 1: return S.dsw_a;
        case 2: return S.dsw_b;
        default: return 0xffff;
        }
    }
    if (a >= 0xe03000 && a <= 0xe03fff) {
        if ((a & 0x3f) <= 0x1f) return (uint16_t)(0xff00 | i8255d_read(&S.ppi1, (int)((a & 7) >> 1)));
        return (uint16_t)(0xff00 | S.control_res);
    }
    return 0xffff;
}

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a >= 0x20c000 && a <= 0x20ffff) S.ram[(a & 0x3fff) >> 1] = v;
    else if (a >= 0x400000 && a <= 0x403fff) tile_write((uint16_t)((a & 0x3fff) >> 1), v);
    else if (a >= 0x410000 && a <= 0x410fff) char_write((uint16_t)((a & 0xfff) >> 1), v);
    else if (a >= 0x600000 && a <= 0x6007ff) S.video.sprite_ram[(a & 0x7ff) >> 1] = v;
    else if (a >= 0xa00000 && a <= 0xa00fff) s16_set_palette_entry(&S.video, (int)((a & 0xfff) >> 1), v, false);
    else if (a >= 0xc68000 && a <= 0xc68fff) S.road_ram[(a & 0xfff) >> 1] = v;
    else if (a >= 0xc7c000 && a <= 0xc7ffff) S.ram2[(a & 0x3fff) >> 1] = v;
    else if (a >= 0xe00000 && a <= 0xe00fff) i8255d_write(&S.ppi0, (int)((a & 7) >> 1), (uint8_t)v);
    else if (a >= 0xe03000 && a <= 0xe03fff) {
        if ((a & 0x3f) <= 0x1f) i8255d_write(&S.ppi1, (int)((a & 7) >> 1), (uint8_t)v);
        else update_controls();
    }
}
#else
static uint16_t main_read(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x3ffff) {
        const size_t i = (a >> 1) % S.rom_words;
#if ENDURO
        return S.main.opcode ? S.rom[i] : S.rom_data[i];
#else
        return S.rom[i];
#endif
    }
    if (a >= 0x40000 && a <= 0x43fff) return S.ram[(a & 0x3fff) >> 1];
    if (a >= 0x100000 && a <= 0x107fff) return S.video.tile_ram[(a & 0x7fff) >> 1];
    if (a >= 0x108000 && a <= 0x108fff) return S.video.char_ram[(a & 0xfff) >> 1];
    if (a >= 0x110000 && a <= 0x110fff) return S.video.pal_ram[(a & 0xfff) >> 1];
    if (a >= 0x124000 && a <= 0x127fff) return S.ram2[(a & 0x3fff) >> 1];
    if (a >= 0x130000 && a <= 0x130fff) return S.video.sprite_ram[(a & 0xfff) >> 1];
    if (a >= 0x140000 && a <= 0x14ffff) {
        const uint32_t r = a & 0x3f;
        if (r <= 0x0f) return i8255d_read(&S.ppi0, (int)((a & 7) >> 1));
        if (r <= 0x1f) {
            switch ((a & 7) >> 1) {
            case 0: return S.in0;
            case 1: return 0xffff;
            case 2: return S.dsw_a;
            default: return S.dsw_b;
            }
        }
        if (r <= 0x2f) return i8255d_read(&S.ppi1, (int)((a & 7) >> 1));
        return (uint16_t)(0xff00 | S.control_res);
    }
    if (a >= 0xc68000 && a <= 0xc68fff) return S.road_ram[(a & 0xfff) >> 1];
    return 0xffff;
}

static void main_write(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a >= 0x40000 && a <= 0x43fff) S.ram[(a & 0x3fff) >> 1] = v;
    else if (a >= 0x100000 && a <= 0x107fff) tile_write((uint16_t)((a & 0x7fff) >> 1), v);
    else if (a >= 0x108000 && a <= 0x108fff) char_write((uint16_t)((a & 0xfff) >> 1), v);
    else if (a >= 0x110000 && a <= 0x110fff) s16_set_palette_entry(&S.video, (int)((a & 0xfff) >> 1), v, false);
    else if (a >= 0x124000 && a <= 0x127fff) S.ram2[(a & 0x3fff) >> 1] = v;
    else if (a >= 0x130000 && a <= 0x130fff) S.video.sprite_ram[(a & 0xfff) >> 1] = v;
    else if (a >= 0x140000 && a <= 0x14ffff) {
        const uint32_t r = a & 0x3f;
        if (r <= 0x0f) i8255d_write(&S.ppi0, (int)((a & 7) >> 1), (uint8_t)v);
        else if (r >= 0x20 && r <= 0x2f) i8255d_write(&S.ppi1, (int)((a & 7) >> 1), (uint8_t)v);
        else if (r >= 0x30) update_controls();
    } else if (a >= 0xc68000 && a <= 0xc68fff) S.road_ram[(a & 0xfff) >> 1] = v;
}
#endif

static uint16_t sub_read(uint32_t a)            // (the Hang-On sub CPU map on the three)
{
    a &= 0x7ffff;
    if (a <= 0xffff) return S.rom2[(a >> 1) % S.rom2_words];
    if (a >= 0x68000 && a <= 0x68fff) return S.road_ram[(a & 0xfff) >> 1];
    if (a >= 0x7c000 && a <= 0x7ffff) return S.ram2[(a & 0x3fff) >> 1];
    return 0xffff;
}

static void sub_write(uint32_t a, uint16_t v)
{
    a &= 0x7ffff;
    if (a >= 0x68000 && a <= 0x68fff) S.road_ram[(a & 0xfff) >> 1] = v;
    else if (a >= 0x7c000 && a <= 0x7ffff) S.ram2[(a & 0x3fff) >> 1] = v;
}

static uint16_t main_rd(void *u, uint32_t a) { (void)u; return main_read(a); }
static void main_wr(void *u, uint32_t a, uint16_t v) { (void)u; main_write(a, v); }
static uint16_t sub_rd(void *u, uint32_t a) { (void)u; return sub_read(a); }
static void sub_wr(void *u, uint32_t a, uint16_t v) { (void)u; sub_write(a, v); }

// ---------------------------------------------------------------------------
// The sound Z80

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
#if ENDURO
    if (a <= 0x7fff || a >= 0xf800) return S.sound_mem[a];
    if (a >= 0xf000 && a <= 0xf7ff) return segapcmd_read(&S.pcm, a);
    return 0xff;
#else
    if (a <= 0x7fff) return S.sound_mem[a];
    if (a >= 0xc000 && a <= 0xcfff) return S.sound_mem[0xc000 + (a & 0x7ff)];
    if (a >= 0xd000 && a <= 0xdfff) return (a & 1) ? ym2203d_read(&S.ym2203) : ym2203d_status(&S.ym2203);
    if (a >= 0xe000 && a <= 0xefff) return segapcmd_read(&S.pcm, a);
    return 0xff;
#endif
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if ENDURO
    if (a >= 0xf000 && a <= 0xf7ff) segapcmd_write(&S.pcm, a, v);
    else if (a >= 0xf800) S.sound_mem[a] = v;
#else
    if (a >= 0xc000 && a <= 0xcfff) S.sound_mem[0xc000 + (a & 0x7ff)] = v;
    else if (a >= 0xd000 && a <= 0xdfff) {
        if ((a & 1) == 0) ym2203d_control(&S.ym2203, v);
        else ym2203d_write(&S.ym2203, v);
    } else if (a >= 0xe000 && a <= 0xefff) segapcmd_write(&S.pcm, a, v);
#endif
}

static uint8_t sound_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
#if ENDURO
    if (p <= 0x3f && (p & 1)) return ym2151_status(&S.ym2151);
#endif
    if (p >= 0x40 && p <= 0x7f) {
        i8255d_ack_a(&S.ppi0);                  // (the latch read strobes /ACK: the NMI goes)
        return S.sound_latch;
    }
    return 0xff;
}

static void sound_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
#if ENDURO
    const uint8_t p = (uint8_t)port;
    if (p <= 0x3f) {
        if ((p & 1) == 0) ym2151_select(&S.ym2151, v);
        else ym2151_write(&S.ym2151, v);
    }
#else
    (void)port;
    (void)v;
#endif
}

static void sound_cycles(int cycles)
{
#if ENDURO
    ym2151_run_timers(&S.ym2151, cycles);
#endif
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
        if (S.pcm_count > 0) {                  // (the PCM ticks within this sample, averaged)
            S.pcm_last = (int32_t)(S.pcm_sum / S.pcm_count);
            S.pcm_sum = 0;
            S.pcm_count = 0;
        }
#if ENDURO
        double mix = (double)ym2151_update(&S.ym2151) * 0.30 + (double)S.pcm_last * 0.70;
#else
        double mix = (double)ym2203d_update(&S.ym2203) + (double)S.pcm_last * 0.40;
#endif
        if (S.sound_mute) mix = 0;
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16((int32_t)lround(mix * MIX_GAIN));
    }
}
static void snd_cycles(void *u, int c) { (void)u; sound_cycles(c); }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&S.snd, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

// the 8255s
static void ppi0_a(void *u, uint8_t v) { (void)u; S.sound_latch = v; }
static void ppi0_b(void *u, uint8_t v)
{
    (void)u;
    const bool reset = (v & 0x20) == 0;
    if (reset && !S.z80_reset) zd_reset(&S.snd);
    S.z80_reset = reset;
    S.video.screen_enabled = (v & 0x10) != 0;
}
static void ppi0_c(void *u, uint8_t v)
{
    (void)u;
    zd_set_nmi(&S.snd, (v & 0x80) ? ZD_CLEAR : ZD_ASSERT);
    S.sound_mute = (v & 0x01) == 0;
}
static uint8_t ppi1_c_in(void *u) { (void)u; return 0x00; }   // (the ADC's /INTR: always done)
static void ppi1_a(void *u, uint8_t v)
{
    (void)u;
    m68kd_set_irq(&S.sub, 4, (v & 0x40) ? M68_CLEAR : M68_ASSERT);
    m68kd_set_reset_line(&S.sub, (v & 0x20) ? M68_ASSERT : M68_CLEAR);
    S.adc_select = (v >> 2) & 3;
}

#if SHARRIER
static void mcu_p1(void *u, uint8_t v)
{
    (void)u;
    S.i8751_addr = (uint8_t)(((v & 0x40) >> 2) | ((v & 0x38) >> 3));
    const uint8_t irq = (uint8_t)((~v) & 7);
    if (irq != 0) m68kd_set_irq(&S.main, irq, M68_HOLD);
}
static uint8_t mcu_ext_r(void *u, uint16_t address)
{
    (void)u;
    const uint32_t a = ((uint32_t)S.i8751_addr << 16) | (uint32_t)(address ^ 1);
    const uint16_t w = main_read(a);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
static void mcu_ext_w(void *u, uint16_t address, uint8_t v)
{
    (void)u;
    const uint32_t a = ((uint32_t)S.i8751_addr << 16) | (uint32_t)(address ^ 1);
    if (a == 0x40385) return;
    uint16_t w = main_read(a);
    w = (a & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0x00ff) | (v << 8));
    main_write(a, w);
}
#endif

// ---------------------------------------------------------------------------
// Video

static void update_video(void)
{
    s16video *v = &S.video;
    if (!v->screen_enabled) {
        for (int i = 0; i < W * H; i++) S.frame[i] = v->palette[0x2000];
    } else {
        s16_render_tile_pages(v, S.bg, S.fg, 0, true, 5, 0xfff, 0, false, false);
        s16_render_tile_pages(v, S.fg, NULL, 4, true, 5, 0xfff, 0, false, false);
        s16_render_text(v, S.text_low, S.text_high, 8, 0xff, 0x800, false);
        const int sx1 = (0xc8 - (v->char_ram[0x7fd] & 0x1ff)) & 0x3ff, sy1 = v->char_ram[0x793] & 0xff;
        const int sx2 = (0xc8 - (v->char_ram[0x7fc] & 0x1ff)) & 0x3ff, sy2 = v->char_ram[0x792] & 0xff;
        s16_draw_hangon_road(S.frame, v->palette, S.road_ram, S.road_gfx, 0x38, 0x7c0, 0, !HANGON);
        s16_blit_scrolled(v, S.frame, S.bg, sx1, sy1, 1024, 512);
        s16_blit_scrolled(v, S.frame, S.fg, sx2, sy2, 1024, 512);
        s16_draw_hangon_road(S.frame, v->palette, S.road_ram, S.road_gfx, 0x38, 0x7c0, 1, !HANGON);
#if HANGON
        s16_blit_text(v, S.frame, S.text_low);
        for (int pri = 3; pri >= 0; pri--)
            s16_draw_sprites_hangon(v, S.frame, S.sprite16, S.sprite_count, S.zoom, S.zoom_size, SPRITE_BANKS, pri);
        s16_blit_text(v, S.frame, S.text_high);
#else
        s16_draw_sprites_sharrier(v, S.frame, S.sprite32, S.sprite_count, S.zoom, S.zoom_size, SPRITE_BANKS, 0);
        s16_draw_sprites_sharrier(v, S.frame, S.sprite32, S.sprite_count, S.zoom, S.zoom_size, SPRITE_BANKS, 1);
        s16_blit_text(v, S.frame, S.text_low);
#endif
    }
    for (int i = 0; i < W * H; i++) S.fb[i] = s16_to565(S.frame[i]);
}

// ---------------------------------------------------------------------------

static bool load_roms(void)
{
    uint32_t n;
#if ENDURO
    uint16_t *enc = load16w(kMain, N(kMain), &S.rom_words);
    uint8_t *key = load_bytes(kKey, N(kKey), &n);
    if (!enc || !key) return false;
    S.rom = (uint16_t *)calloc(S.rom_words, 2);
    S.rom_data = (uint16_t *)calloc(S.rom_words, 2);
    if (!S.rom || !S.rom_data) return false;
    fd1089_decrypt(enc, S.rom, S.rom_data, S.rom_words * 2, key, FD1089_B);
    free(enc);
    free(key);
#else
    if (!(S.rom = load16w(kMain, N(kMain), &S.rom_words))) return false;
#endif
    if (!(S.rom2 = load16w(kSub, N(kSub), &S.rom2_words))) return false;
    uint8_t *b = load_bytes(kSound, N(kSound), &n);
    if (!b) return false;
    memcpy(S.sound_mem, b, n);
    free(b);
    if (!(b = load_bytes(kTiles, N(kTiles), &n))) return false;
    S.video.tiles = s16_decode_tiles(b, 1);
    S.video.tiles_total = 0x1000;
    free(b);
    if (!S.video.tiles) return false;
#if HANGON
    if (!(S.sprite16 = load16b(kSprites, N(kSprites), &S.sprite_count))) return false;
    if (!(S.zoom = load_bytes(kZoom, N(kZoom), &S.zoom_size))) return false;
#else
    if (!(S.sprite32 = load32dw(kSprites, N(kSprites), &S.sprite_count))) return false;
    if (!(S.zoom = load_bytes(kZoom, N(kZoom), &S.zoom_size))) {   // (optional: zeros)
        S.zoom = (uint8_t *)calloc(1, 0x2000);
        S.zoom_size = 0x2000;
    }
#endif
    if (!(b = load_bytes(kRoad, N(kRoad), &n))) return false;
    S.road_gfx = (uint8_t *)malloc(256 * 512);
    if (!S.road_gfx) return false;
    s16_decode_hangon_road(S.road_gfx, b, n);
    free(b);
    if (!(S.pcm_rom = load_bytes(kPcm, N(kPcm), &S.pcm_size))) return false;
#if ENDURO
    uint8_t *p = (uint8_t *)realloc(S.pcm_rom, 0x20000);   // (32K ROMs in 64K banks: A15 not decoded)
    if (!p) return false;
    S.pcm_rom = p;
    if (S.pcm_size < 0x20000) memset(p + S.pcm_size, 0, 0x20000 - S.pcm_size);
    S.pcm_size = 0x20000;
    for (uint32_t bank = 0; bank < 0x20000; bank += 0x10000) memcpy(p + bank + 0x8000, p + bank, 0x8000);
#endif
#if SHARRIER
    mcs51_init(&S.mcu);
    S.has_mcu = (b = load_bytes(kMcu, N(kMcu), &n)) != NULL;
    if (S.has_mcu) {
        memcpy(S.mcu.rom, b, n);
        free(b);
    }
#endif
    return true;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    s16_init(&S.video);
    S.frame = (uint32_t *)calloc(W * H, 4);
    S.bg = (uint32_t *)malloc(4u * S16_MAP_W * S16_MAP_H);
    S.fg = (uint32_t *)malloc(4u * S16_MAP_W * S16_MAP_H);
    S.text_low = (uint32_t *)malloc(4u * S16_TEXT_W * 256);
    S.text_high = (uint32_t *)malloc(4u * S16_TEXT_W * 256);
    if (!S.frame || !S.bg || !S.fg || !S.text_low || !S.text_high || !load_roms()) return false;
    S.dsw_a = 0xffff;
    S.dsw_b = DSW_B;

    m68kd_init(&S.main, MAIN_CLOCK, M68K_T_68000);
    S.main.read_word = main_rd;
    S.main.write_word = main_wr;
    m68kd_init(&S.sub, MAIN_CLOCK, M68K_T_68000);
    S.sub.read_word = sub_rd;
    S.sub.write_word = sub_wr;
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.in = sound_in;
    S.snd.out = sound_out;
    S.snd.cycles = snd_cycles;
    zd_init(&S.snd);
    ym2203d_init(&S.ym2203, SOUND_CLOCK, 0.3f, 0.3f);
    ym2151_init(&S.ym2151, SOUND_CLOCK, RATE);
    S.ym2151.amplitude = 0.5f;
#if ENDURO
    S.ym2151.irq_handler = ym_irq;
#else
    S.ym2203.irq = ym_irq;
#endif
    segapcmd_init(&S.pcm, SOUND_CLOCK, 1.0f, ENDURO ? SEGAPCMD_315_5218 : SEGAPCMD_DISCRETE);
    S.pcm.rom = S.pcm_rom;
    S.pcm.rom_size = S.pcm_size;
    segapcmd_set_bank(&S.pcm, SEGAPCMD_BANK_512);
    S.ppi0.handshake = true;
    S.ppi0.out[0] = ppi0_a;
    S.ppi0.out[1] = ppi0_b;
    S.ppi0.out[2] = ppi0_c;
    S.ppi1.in[2] = ppi1_c_in;
    S.ppi1.out[0] = ppi1_a;
#if SHARRIER
    S.mcu.clock = 8000000 / 12;
    S.mcu.port_write[1] = mcu_p1;
    S.mcu.read_external = mcu_ext_r;
    S.mcu.write_external = mcu_ext_w;
#endif
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&S.main);
    m68kd_reset(&S.sub);
    zd_reset(&S.snd);
    ym2203d_reset(&S.ym2203);
    ym2151_reset(&S.ym2151);
    segapcmd_reset(&S.pcm);
    i8255d_reset(&S.ppi0);
    i8255d_reset(&S.ppi1);
#if SHARRIER
    if (S.has_mcu) mcs51_reset(&S.mcu);
#endif
    s16_reset(&S.video);
    S.video.screen_enabled = true;
    memset(S.ram, 0, sizeof S.ram);
    memset(S.ram2, 0, sizeof S.ram2);
    memset(S.road_ram, 0, sizeof S.road_ram);
    S.in0 = 0xffff;
    S.adc_select = S.sound_latch = S.control_res = 0;
    S.z80_reset = false;
#if SHARRIER
    S.i8751_addr = 0;
#endif
    S.steer = 0x80;
    S.stick_y = 0x80;
    S.gas = S.brake = 0;
    S.lean = 0x20;
    S.audio_acc = S.pcm_acc = S.pcm_sum = 0;
    S.pcm_count = 0;
    S.pcm_last = 0;
    S.main_debt = S.sub_debt = S.sound_debt = S.mcu_debt = 0;
    S.sound_mute = false;
    memset(S.frame, 0, 4u * W * H);
    memset(S.fb, 0, sizeof S.fb);
}

int arc_run_frame(const int16_t **audio)
{
    const double slices = (double)(SCANLINES * CPU_SYNC);
    const double main_cycles = (double)MAIN_CLOCK / FPS / slices;
    const double snd = (double)SOUND_CLOCK / FPS / slices;
#if SHARRIER
    const double mcu_cycles = S.has_mcu ? (double)S.mcu.clock / FPS / slices : 0.0;
#endif
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 224) {
#if SHARRIER
            if (S.has_mcu) mcs51_set_irq0_line(&S.mcu, MCS51_HOLD);
            else m68kd_set_irq(&S.main, 4, M68_HOLD);
#else
            m68kd_set_irq(&S.main, 4, M68_HOLD);
#endif
            update_video();
        }
        for (int slice = 0; slice < CPU_SYNC; slice++) {
            S.main_debt += main_cycles;
            S.main_debt -= m68kd_run(&S.main, (int)S.main_debt);
            S.sub_debt += main_cycles;
            S.sub_debt -= m68kd_run(&S.sub, (int)S.sub_debt);
            S.sound_debt += snd;
            if (!S.z80_reset) S.sound_debt -= zd_run(&S.snd, (int)S.sound_debt);
            else {                              // (held in reset: silent, but the time passes)
                const int idle = (int)S.sound_debt;
                sound_cycles(idle);
                S.sound_debt -= idle;
            }
#if SHARRIER
            if (S.has_mcu) {
                S.mcu_debt += mcu_cycles;
                S.mcu_debt -= mcs51_run(&S.mcu, (int)S.mcu_debt);
            }
#endif
        }
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

static int ramp(int value, int target, int delta)   // MAME's PORT_KEYDELTA from digital keys
{
    if (value < target) return value + delta < target ? value + delta : target;
    return value - delta > target ? value - delta : target;
}

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    S.in0 = 0xffff;
    if (in->coin1) S.in0 &= (uint16_t)~0x0001;
    if (in->coin2) S.in0 &= (uint16_t)~0x0002;
    if (in->service) S.in0 &= (uint16_t)~0x0004;
    if (p & AP_START) S.in0 &= (uint16_t)(ENDURO ? ~0x0040 : ~0x0010);
#if SHARRIER
    if (p & AP_B1) S.in0 &= (uint16_t)~0x0020;
    if (p & AP_B2) S.in0 &= (uint16_t)~0x0040;
    if (p & AP_B3) S.in0 &= (uint16_t)~0x0080;
    S.steer = ramp(S.steer, (p & AP_LEFT) ? 0xe0 : (p & AP_RIGHT) ? 0x20 : 0x80, 12);   // (reversed)
    S.stick_y = ramp(S.stick_y, (p & AP_UP) ? 0xe0 : (p & AP_DOWN) ? 0x20 : 0x80, 12);
#elif HANGON
    S.steer = ramp(S.steer, (p & AP_LEFT) ? 0xe0 : (p & AP_RIGHT) ? 0x20 : 0x80, 8);
    S.gas = ramp(S.gas, (p & (AP_B1 | AP_UP)) ? 0xff : 0x00, 20);
    S.brake = ramp(S.brake, (p & (AP_B2 | AP_DOWN)) ? 0xff : 0x00, 40);
#else
    S.gas = ramp(S.gas, (p & AP_B1) ? 0xff : 0x00, 20);
    S.brake = ramp(S.brake, (p & AP_B2) ? 0xff : 0x00, 40);
    S.lean = ramp(S.lean, (p & (AP_DOWN | AP_B3)) ? 0xff : (p & AP_UP) ? 0x00 : 0x20, 16);
    S.steer = ramp(S.steer, (p & AP_LEFT) ? 0xff : (p & AP_RIGHT) ? 0x01 : 0x80, 8);
#endif
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = (uint16_t)(0xff00 | v);
    else if (bank == 1) S.dsw_b = (uint16_t)(0xff00 | v);
}
