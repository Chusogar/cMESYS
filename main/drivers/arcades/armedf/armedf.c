// armedf.c - Nichibutsu's Armed F hardware for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/armedf_hw (from
// dsp-emulator's armedf_hw.pas). ARC_VARIANT: 0 Armed Formation (1988),
// 1 Terra Force (1987), 2 Crazy Climber 2 (1988), 3 Legion (1987). A 68000
// at 8 MHz (Musashi here), a Z80 with a YM3812 / YM3526 and two DACs, 8x8
// text, two scrolling layers of 16x16 tiles and 16x16 sprites through a
// colour look-up RAM, in three priorities. All but Armed F carry the NB1414M4
// that stamps prepared messages into the text RAM. The graphics are read
// straight from their ROMs and the picture built in place (turned round for
// the vertical ones).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym3812dsp.h"

static m68kd cpu;

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define MAIN_CLOCK 8000000
#define SOUND_CLOCK 4000000
#define SCANLINES 256
#define FPS 59.082012
#define RATE 44100

typedef struct { const char *name; uint32_t size, p, crc; } word_rom;   // p >> 1: the word, p & 1: the high byte

#if ARC_VARIANT == 0
static const char *const kSets[] = { "armedf", NULL };
static const word_rom kProg[] = {
    {"06.3d", 0x10000, 1, 0x0f9015e2}, {"01.3f", 0x10000, 0, 0x816ff7c5},
    {"07.5d", 0x10000, 0x20001, 0x5b3144a5}, {"02.4f", 0x10000, 0x20000, 0xfa10c29d},
    {"af_08.rom", 0x10000, 0x40001, 0xd1d43600}, {"af_03.rom", 0x10000, 0x40000, 0xbbe1fe2d}};
static const arc_rom kSound[] = {{"af_10.rom", 0x10000, 0, 0xc5eacb87}};
static const arc_rom kChars[] = {{"09.11c", 0x8000, 0, 0x5c6993d5}};
static const arc_rom kBg[] = {{"af_14.rom", 0x10000, 0, 0x8c5dc5a7}, {"af_13.rom", 0x10000, 0x10000, 0x136a58a3}};
static const arc_rom kFg[] = {{"af_04.rom", 0x10000, 0, 0x44d3af4f}, {"af_05.rom", 0x10000, 0x10000, 0x92076cab}};
static const arc_rom kSprites[] = {{"af_11.rom", 0x20000, 0, 0xb46c473c}, {"af_12.rom", 0x20000, 0x20000, 0x23cb6bfe}};
#define ID "armedf"
#define TITLE "Armed Formation"
#define YEAR 1988
#define SPRITE_SIZE 0x40000
#define WIDE_SPRITES 1
#define ROTATED 1
#define IRQ_LEVEL 1
#define SPRITE_OFFSET 0x80
#define SPRITE_COUNT 0x200
#define BUTTONS { "FIRE", "BOMB", NULL, NULL }
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "terraf", NULL };
static const word_rom kProg[] = {
    {"8.6e", 0x10000, 1, 0xfd58fa06}, {"3.6h", 0x10000, 0, 0x54823a7d},
    {"7.4e", 0x10000, 0x20001, 0xfde8de7e}, {"2.4h", 0x10000, 0x20000, 0xdb987414},
    {"6.3e", 0x10000, 0x40001, 0xa5bb8c3b}, {"1.3h", 0x10000, 0x40000, 0xd2de6d28}};
static const arc_rom kSound[] = {{"11.17k", 0x10000, 0, 0x4407d475}};
static const arc_rom kNb[] = {{"10.11c", 0x4000, 0, 0xac705812}};
static const arc_rom kChars[] = {{"9.11e", 0x8000, 0, 0xbc6f7cbc}};
static const arc_rom kBg[] = {{"15.8a", 0x10000, 0, 0x2144d8e0}, {"14.6a", 0x10000, 0x10000, 0x744f5c9e}};
static const arc_rom kFg[] = {{"5.15h", 0x10000, 0, 0x25d23dfd}, {"4.13h", 0x10000, 0x10000, 0xb9b0fe27}};
static const arc_rom kSprites[] = {{"12.7d", 0x10000, 0, 0x2d1f2ceb}, {"13.9d", 0x10000, 0x10000, 0x1d2f92d6}};
#define ID "terraf"
#define TITLE "Terra Force"
#define YEAR 1987
#define SPRITE_SIZE 0x20000
#define IRQ_LEVEL 1
#define SPRITE_OFFSET 0x80
#define SPRITE_COUNT 0x80
#define BUTTONS { "FIRE", "BOMB", NULL, NULL }
#elif ARC_VARIANT == 2
static const char *const kSets[] = { "cclimbr2", NULL };
static const word_rom kProg[] = {
    {"4.bin", 0x10000, 1, 0x7922ea14}, {"1.bin", 0x10000, 0, 0x2ac7ed67},
    {"6.bin", 0x10000, 0x20001, 0x7905c992}, {"5.bin", 0x10000, 0x20000, 0x47be6c1e},
    {"3.bin", 0x10000, 0x40001, 0x1fb110d6}, {"2.bin", 0x10000, 0x40000, 0x0024c15b}};
static const arc_rom kSound[] = {{"11.bin", 0x4000, 0, 0xfe0175be}, {"12.bin", 0x8000, 0x4000, 0x5ddf18f2}};
static const arc_rom kNb[] = {{"9.bin", 0x4000, 0, 0x740d260f}};
static const arc_rom kChars[] = {{"10.bin", 0x8000, 0, 0x7f475266}};
static const arc_rom kBg[] = {{"17.bin", 0x10000, 0, 0xe24bb2d7}, {"18.bin", 0x10000, 0x10000, 0x56834554}};
static const arc_rom kFg[] = {{"7.bin", 0x10000, 0, 0xcbdd3906}, {"8.bin", 0x10000, 0x10000, 0xb2a613c0}};
static const arc_rom kSprites[] = {
    {"15.bin", 0x10000, 0, 0x4bf838be}, {"13.bin", 0x10000, 0x20000, 0x6b6ec999},
    {"16.bin", 0x10000, 0x10000, 0x21a265c5}, {"14.bin", 0x10000, 0x30000, 0xf426a4ad}};
#define ID "cclimbr2"
#define TITLE "Crazy Climber 2"
#define YEAR 1988
#define SPRITE_SIZE 0x40000
#define WIDE_SPRITES 1
#define NARROW 1
#define IRQ_LEVEL 2
#define SPRITE_OFFSET 0
#define SPRITE_COUNT 0x200
#define BUTTONS { NULL, NULL, NULL, NULL }
#define JOY ARC_JOY_4WAY
#else
static const char *const kSets[] = { "legion", NULL };
static const word_rom kProg[] = {
    {"lg1.bin", 0x10000, 0, 0xc4aeb724}, {"lg3.bin", 0x10000, 1, 0x777e4935},
    {"legion.1b", 0x10000, 0x20000, 0xc306660a}, {"legion.1d", 0x10000, 0x20001, 0xc2e45e1e}};
static const arc_rom kSound[] = {{"legion.1h", 0x4000, 0, 0x2ca4f7f0}, {"legion.1i", 0x8000, 0x4000, 0x79f4a827}};
static const arc_rom kNb[] = {{"lg7.bin", 0x4000, 0, 0x533e2b58}};
static const arc_rom kChars[] = {{"lg8.bin", 0x8000, 0, 0xe0596570}};
static const arc_rom kBg[] = {{"legion.1l", 0x10000, 0, 0x29b8adaa}};
static const arc_rom kFg[] = {{"legion.1e", 0x10000, 0, 0xa9d70faf}, {"legion.1f", 0x8000, 0x18000, 0xf018313b}};
static const arc_rom kSprites[] = {{"legion.1k", 0x10000, 0, 0xff5a0db9}, {"legion.1j", 0x10000, 0x10000, 0xbae220c8}};
#define ID "legion"
#define TITLE "Legion"
#define YEAR 1987
#define SPRITE_SIZE 0x20000
#define NARROW 1
#define ROTATED 1
#define IRQ_LEVEL 2
#define SPRITE_OFFSET 0
#define SPRITE_COUNT 0x80
#define BUTTONS { "FIRE", "BOMB", NULL, NULL }
#endif
#define NB1414 (ARC_VARIANT != 0)
#ifndef JOY
#define JOY ARC_JOY_8WAY
#endif
#if defined(NARROW)                     // 288x224 from 16, 8 of the region
#define RW 288
#define RH 224
#define OX (96 + 16)
#define OY (8 + 8)
#else
#define RW 320
#define RH 240
#define OX 96
#define OY 8
#endif
#if defined(ROTATED)
#define W RH
#define H RW
#else
#define W RW
#define H RH
#endif

const arc_info arc_game = { ID, TITLE, "Nichibutsu", YEAR, W, H, FPS, RATE, kSets, BUTTONS, JOY };

static const int kCharX[8] = {4, 0, 12, 8, 20, 16, 28, 24};
static const int kCharY[8] = {0, 32, 64, 96, 128, 160, 192, 224};
static const int kTileX[16] = {4, 0, 12, 8, 20, 16, 28, 24, 36, 32, 44, 40, 52, 48, 60, 56};
static const int kTileY[16] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
#if defined(WIDE_SPRITES)
#define SPRITES 0x800
#else
#define SPRITES 0x400
#endif
#define SB (SPRITES * 64 * 8)
static const int kSprX[16] = {4, 0, SB + 4, SB + 0, 12, 8, SB + 12, SB + 8, 20, 16, SB + 20, SB + 16, 28, 24, SB + 28, SB + 24};
static const int kSprY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};
static const arc_layout kCharL = { 8, 8, 0x400, 4, 256, false, false, false, {0, 1, 2, 3}, kCharX, kCharY };
static const arc_layout kTileL = { 16, 16, 0x400, 4, 1024, false, false, false, {0, 1, 2, 3}, kTileX, kTileY };
static const arc_layout kSprL = { 16, 16, SPRITES, 4, 512, false, false, false, {0, 1, 2, 3}, kSprX, kSprY };

// the machine on the heap
static struct state {
    zd_z80 sound;
    ym3812d ym;
    const uint8_t *rom, *chars, *bg, *fg, *sprites;   // blobs, the graphics raw
    uint16_t ram[0x6400], ram_bg[0x800], ram_fg[0x800], ram_clut[0x800], ram_sprites[0x800], sprite_buffer[0x800];
    uint16_t pal_raw[0x800], palette[0x801];
    uint8_t ram_txt[0x1000], nb_rom[0x4000], sound_mem[0x10000];
    arc_fb fb;
    uint16_t video_reg, scroll_fg_x, scroll_fg_y, scroll_bg_x, scroll_bg_y, in0, in1;
    uint8_t dsw_a, dsw_b, soundlatch, frame_counter;
    int32_t dac0, dac1;
    int64_t audio_acc, sound_irq_acc;
    int16_t audio[1024];
    int naudio;
} *A_;
#define A (*A_)


static void set_color(int i, uint16_t v)
{
    A.pal_raw[i] = v;
    A.palette[i] = arc_rgb565((uint32_t)((v >> 8 & 15) * 255 / 15) << 16 | (uint32_t)((v >> 4 & 15) * 255 / 15) << 8 |
                              (uint32_t)((v & 15) * 255 / 15));
}

#if NB1414
static void nb_exec(void);
#endif

static uint16_t read16(uint32_t a)
{
    a &= 0xfffff;
    if (a <= 0x5ffff) return (uint16_t)(A.rom[a & ~1u] << 8 | A.rom[a | 1]);
#if !NB1414
    if (a >= 0x60000 && a <= 0x60fff) return A.ram_sprites[(a & 0xfff) >> 1];
    if ((a >= 0x61000 && a <= 0x65fff) || (a >= 0x6c008 && a <= 0x6c7ff)) return A.ram[(a - 0x60000) >> 1];
    if (a >= 0x66000 && a <= 0x66fff) return A.ram_bg[(a & 0xfff) >> 1];
    if (a >= 0x67000 && a <= 0x67fff) return A.ram_fg[(a & 0xfff) >> 1];
    if (a >= 0x68000 && a <= 0x69fff) return A.ram_txt[(a & 0x1fff) >> 1];
    if (a >= 0x6a000 && a <= 0x6afff) return A.pal_raw[(a & 0xfff) >> 1];
    if (a >= 0x6b000 && a <= 0x6bfff) return A.ram_clut[(a & 0xfff) >> 1];
    if (a == 0x6c000) return A.in0;
    if (a == 0x6c002) return A.in1;
    if (a == 0x6c004) return A.dsw_a;
    if (a == 0x6c006) return A.dsw_b;
#else
    if (a >= 0x60000 && a <= 0x603ff) return A.ram_sprites[(a & 0xfff) >> 1];
    if ((a >= 0x60400 && a <= 0x63fff) || (a >= 0x6a000 && a <= 0x6a9ff)) return A.ram[(a - 0x60000) >> 1];
    if (a >= 0x64000 && a <= 0x64fff) return A.pal_raw[(a & 0xfff) >> 1];
    if (a >= 0x68000 && a <= 0x69fff) return A.ram_txt[(a & 0x1fff) >> 1];
    if (a >= 0x6c000 && a <= 0x6cfff) return A.ram_clut[(a & 0xfff) >> 1];
    if (a >= 0x70000 && a <= 0x70fff) return A.ram_fg[(a & 0xfff) >> 1];
    if (a >= 0x74000 && a <= 0x74fff) return A.ram_bg[(a & 0xfff) >> 1];
    if (a == 0x78000) return A.in0;
    if (a == 0x78002) return A.in1;
    if (a == 0x78004) return A.dsw_a;
    if (a == 0x78006) return A.dsw_b;
#endif
    return 0xffff;
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xfffff;
    if (a <= 0x5ffff) return;
#if !NB1414
    if (a >= 0x60000 && a <= 0x60fff) A.ram_sprites[(a & 0xfff) >> 1] = v;
    else if ((a >= 0x61000 && a <= 0x65fff) || (a >= 0x6c000 && a <= 0x6c7ff)) A.ram[(a - 0x60000) >> 1] = v;
    else if (a >= 0x66000 && a <= 0x66fff) A.ram_bg[(a & 0xfff) >> 1] = v;
    else if (a >= 0x67000 && a <= 0x67fff) A.ram_fg[(a & 0xfff) >> 1] = v;
    else if (a >= 0x68000 && a <= 0x69fff) A.ram_txt[(a & 0x1fff) >> 1] = (uint8_t)v;
    else if (a >= 0x6a000 && a <= 0x6afff) set_color((a & 0xfff) >> 1, v);
    else if (a >= 0x6b000 && a <= 0x6bfff) A.ram_clut[(a & 0xfff) >> 1] = v;
    else if (a == 0x6d000) A.video_reg = v;
    else if (a == 0x6d002) A.scroll_bg_x = v;
    else if (a == 0x6d004) A.scroll_bg_y = v;
    else if (a == 0x6d006) A.scroll_fg_x = v;
    else if (a == 0x6d008) A.scroll_fg_y = v;
    else if (a == 0x6d00a) A.soundlatch = (uint8_t)(((v & 0x7f) << 1) | 1);
    else if (a == 0x6d00e) m68kd_set_irq(&cpu, IRQ_LEVEL, M68_CLEAR);
#else
    if (a >= 0x60000 && a <= 0x603ff) A.ram_sprites[(a & 0xfff) >> 1] = v;
    else if ((a >= 0x60400 && a <= 0x63fff) || (a >= 0x6a000 && a <= 0x6a9ff)) A.ram[(a - 0x60000) >> 1] = v;
    else if (a >= 0x64000 && a <= 0x64fff) set_color((a & 0xfff) >> 1, v);
    else if (a >= 0x68000 && a <= 0x69fff) A.ram_txt[(a & 0x1fff) >> 1] = (uint8_t)v;
    else if (a >= 0x6c000 && a <= 0x6cfff) A.ram_clut[(a & 0xfff) >> 1] = v;
    else if (a >= 0x70000 && a <= 0x70fff) A.ram_fg[(a & 0xfff) >> 1] = v;
    else if (a >= 0x74000 && a <= 0x74fff) A.ram_bg[(a & 0xfff) >> 1] = v;
    else if (a == 0x7c000) {
        if ((v & 0x4000) && !(A.video_reg & 0x4000)) nb_exec();
        A.video_reg = v;
    } else if (a == 0x7c002) A.scroll_bg_x = v;
    else if (a == 0x7c004) A.scroll_bg_y = v;
    else if (a == 0x7c00a) A.soundlatch = (uint8_t)(((v & 0x7f) << 1) | 1);
    else if (a == 0x7c00e) m68kd_set_irq(&cpu, IRQ_LEVEL, M68_CLEAR);
#endif
}

static uint16_t cpu_rd(void *u_, uint32_t a) { (void)u_; return read16(a); }
static void cpu_wr(void *u_, uint32_t a, uint16_t v) { (void)u_; write16(a, (uint16_t)v); }

#if NB1414
// ---------------------------------------------------------------------------
// The NB1414M4: messages from its ROM into the text RAM

#define TXT(i) A.ram_txt[(size_t)(i) & 0xfff]
#define NB(i) A.nb_rom[(size_t)(i) & 0x3fff]
#define NBW(i) (((NB(i) << 8) | NB((i) + 1)) & 0x3fff)

static void nb_dma(int src, int dst, int size, bool cond)
{
    for (int f = 0; f < size; f++) {
        if (f + dst < 18) continue;     // the command block stays
        TXT(f + dst) = cond ? NB(f + src) : 0x20;
        TXT(f + dst + 0x400) = cond ? NB(f + size + src) : NB(0x13);
    }
}

static void nb_fill(int dst, uint8_t tile, uint8_t pal)
{
    for (int f = 0; f <= 0x3ff; f++) {
        if (f + dst < 18) continue;
        TXT(f + dst) = tile;
        TXT(f + dst + 0x400) = pal;
    }
}

static void nb_score(int dst, int src_base)
{
    int first = 0;
    for (int f = 0; f < 6; f++) {
        const uint8_t res = (uint8_t)((A.ram_txt[(f >> 1) + 5 + src_base * 3] >> ((~(f & 1) & 1) * 4)) & 0xf);
        if (first || res) {
            TXT(f + dst) = (uint8_t)(res + 0x30);
            first = 1;
        } else TXT(f + dst) = 0x20;
        TXT(f + dst + 0x400) = NB(0x10f + src_base * 0x1c + f);
    }
    TXT(6 + dst) = 0x30;
    TXT(6 + dst + 0x400) = NB(0x10f + src_base * 0x1c + 6);
    TXT(7 + dst) = 0x30;
    TXT(7 + dst + 0x400) = NB(0x10f + src_base * 0x1c + 7);
}

static void nb_insert_coin(void)
{
    if (A.ram_txt[0xf] == 0) nb_dma(3, NBW(1), 0x10, (A.frame_counter & 0x10) != 0);
    else nb_dma(0x4b, NBW(0x49), 0x18, true);
}

static void nb_credit(void)
{
    const uint8_t credits = A.ram_txt[0xf];
    const bool flicker = (A.frame_counter & 0x10) != 0;
    nb_dma(0x25, NBW(0x23), 0x10, true);
    const int dst = NBW(0x45);
    TXT(dst) = (credits & 0xf0) ? (uint8_t)(((credits & 0xf0) >> 4) + 0x30) : 0x20;
    TXT(dst + 0x400) = NB(0x47);
    TXT(dst + 1) = (uint8_t)((credits & 0x0f) + 0x30);
    TXT(dst + 0x401) = NB(0x48);
    if (credits == 1) nb_dma(0x7d, NBW(0x7b), 0x18, flicker);
    else if (credits > 1) nb_dma(0xaf, NBW(0xad), 0x18, flicker);
}

static void nb_0200(uint8_t command)
{
    const int dst = NBW(0x330 + (command & 0xf) * 2);
    if (dst & 0x7ff) nb_fill(0, NB(dst), NB(dst + 1));
    else nb_dma(dst, 0, 0x400, true);
}

static void nb_0600(uint8_t is2p)
{
    TXT(NBW(0x1f5)) = (uint8_t)((A.ram_txt[7] & 7) + 0x30);
    nb_dma(0x1fa + ((A.ram_txt[7] & 0x30) >> 4) * 0x18, NBW(0x1f8), 12, true);
    nb_dma(0x264 + ((A.ram_txt[7] & 0x80) >> 7) * 0x18, NBW(0x262), 12, true);
    nb_dma(0x296 + ((A.ram_txt[7] & 0x40) >> 6) * 0x18, NBW(0x294), 12, true);
    TXT(NBW(0x2c6)) = (uint8_t)(((A.ram_txt[0xf] & 0xf0) >> 4) + 0x30);
    TXT(NBW(0x2c9)) = (uint8_t)((A.ram_txt[0xf] & 0x0f) + 0x30);
    TXT(NBW(0x2cc)) = (uint8_t)(((A.ram_txt[0x10] & 0xf0) >> 4) + 0x30);
    TXT(NBW(0x2cf)) = (uint8_t)((A.ram_txt[0x10] & 0x0f) + 0x30);
    int dst = NBW(0x2d2);
    TXT(dst) = (uint8_t)(((A.ram_txt[0x11] & 0xf0) >> 4) + 0x30);
    TXT(dst + 1) = (uint8_t)((A.ram_txt[0x11] & 0x0f) + 0x30);
    nb_dma(0x2d8 + is2p * 0x18, NBW(0x2d6), 12, true);
    dst = NBW(0x308);
    for (int f = 0; f <= 4; f++) nb_dma(0x310 + ((A.ram_txt[4] >> (4 - f)) & 1) * 6, dst + f * 0x20, 3, true);
    dst = NBW(0x30a);
    for (int f = 0; f <= 6; f++) nb_dma(0x310 + ((A.ram_txt[2 + is2p] >> (6 - f)) & 1) * 6, dst + f * 0x20, 3, true);
    dst = NBW(0x30c);
    for (int f = 0; f <= 7; f++) nb_dma(0x310 + ((A.ram_txt[5] >> (7 - f)) & 1) * 6, dst + f * 0x20, 3, true);
    dst = NBW(0x30e);
    for (int f = 0; f <= 7; f++) nb_dma(0x310 + ((A.ram_txt[6] >> (7 - f)) & 1) * 6, dst + f * 0x20, 3, true);
}

static void nb_0e00(uint8_t command)
{
    nb_dma(0xe1, NBW(0xdf), 8, true);
    if (command & 0x04) {
        nb_dma(0xfd, NBW(0xfb), 8, (command & 1) == 0);
        nb_score(NBW(0x10d), 0);
        if (command & 0x80) {
            nb_dma(0x119, NBW(0x117), 8, (command & 2) == 0);
            nb_score(NBW(0x129), 1);
        }
    } else {
        nb_dma(0x135, NBW(0x133), 0x10, (command & 1) == 0);
        nb_insert_coin();
        if ((command & 0x18) == 0) nb_credit();
    }
}

static void nb_exec(void)
{
    A.scroll_fg_x = (uint16_t)(A.ram_txt[0xd] | (A.ram_txt[0xe] << 8));
    A.scroll_fg_y = (uint16_t)(A.ram_txt[0xb] | (A.ram_txt[0xc] << 8));
    const uint16_t command = (uint16_t)((A.ram_txt[0] << 8) | A.ram_txt[1]);
    switch (command & 0xff00) {
    case 0x0000: nb_insert_coin(); nb_credit(); break;
    case 0x0200: nb_0200((uint8_t)(command & 0x87)); break;
    case 0x0600: nb_0600((uint8_t)(command & 1)); break;
    case 0x0e00: nb_0e00((uint8_t)command); break;
    default: break;
    }
}
#endif

// ---------------------------------------------------------------------------
// Sound

static uint8_t sound_read(void *u, uint16_t a) { (void)u; return A.sound_mem[a]; }
static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if ARC_VARIANT >= 2
    if (a <= 0xbfff) return;
#else
    if (a <= 0xf7ff) return;
#endif
    A.sound_mem[a] = v;
}

static uint8_t sound_in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 4: A.soundlatch = 0; return 0;
    case 6: return A.soundlatch;
    default: return 0xff;
    }
}

static void sound_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0: ym3812d_control(&A.ym, v); break;
    case 1: ym3812d_write(&A.ym, v); break;
    case 2: A.dac0 = ((int)v - 0x80) * 256; break;
    case 3: A.dac1 = ((int)v - 0x80) * 256; break;
    default: break;
    }
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    A.sound_irq_acc += c;
    const int64_t period = SOUND_CLOCK / (SOUND_CLOCK / 512);
    while (A.sound_irq_acc >= period) {
        A.sound_irq_acc -= period;
        zd_set_irq(&A.sound, ZD_HOLD, 0xff);
    }
    A.audio_acc += (int64_t)c * RATE;
    while (A.audio_acc >= SOUND_CLOCK) {
        A.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym3812d_update(&A.ym) + A.dac0 + A.dac1;
        if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(s);
    }
}

// ---------------------------------------------------------------------------
// Video: the 512-wide work area's region (OX, OY) to (OX + RW, OY + RH)
// built straight in the picture (turned round for the vertical ones)

static inline uint16_t *px_at(int x, int y)
{
    x -= OX;
    y -= OY;
    if (x < 0 || x >= RW || y < 0 || y >= RH) return NULL;
#if defined(ROTATED)
    return arc_fb_row(&A.fb, RW - 1 - x) + y;
#else
    return arc_fb_row(&A.fb, y) + x;
#endif
}

static inline int text_pos(int x, int y)
{
#if ARC_VARIANT == 0
    return x * 32 + y;
#elif ARC_VARIANT == 3
    return (x & 0x1f) * 32 + y + 0x800 * (x / 32);
#else
    return 32 * (31 - y) + (x & 0x1f) + 0x800 * (x / 32);
#endif
}

// the text at a work-area pixel: palette index, -1 transparent; *under:
// drawn below the layers
static int text_px(int X, int Y, bool *under)
{
#if ARC_VARIANT == 0
    const int attr_base = 0x800, hs = 0;
#else
    const int attr_base = 0x400, hs = 128;
#endif
    *under = false;
    if (Y >= 256) return -1;
    const int x = ((X - hs) & 511) >> 3, y = Y >> 3;
    const int pos = text_pos(x, y);
    if (pos < 0 || pos >= 0x1000) return -1;
    uint8_t attr = 0;
    int code = 0, color = 0;
    if (!(NB1414 && pos < 0x12)) {
        attr = A.ram_txt[(attr_base + pos) & 0xfff];
        code = A.ram_txt[pos & 0xfff] | ((attr & 3) << 8);
        color = (attr >> 4) & 0xf;
    }
    const unsigned p = arc_gfx_px(&kCharL, A.chars, 0x8000, code & 0x3ff, ((X - hs) & 511) & 7, Y & 7);
    if (p == 15) return -1;
    *under = (attr & 8) != 0;
    return ((color << 4) + (int)p) % 0x801;
}

// a tile layer's pixel at (x, y) of its 1024x512: palette index, -1 transparent
static int tile_px(const uint16_t *ram, const uint8_t *gfx, size_t len, int base, int mask, int x, int y)
{
    const uint16_t attr = ram[(x >> 4) * 32 + (y >> 4)];
    const unsigned p = arc_gfx_px(&kTileL, gfx, len, attr & mask, x & 15, y & 15);
    return p == 15 ? -1 : ((((attr >> 11) & 0x1f) << 4) + base + (int)p) % 0x801;
}

static void draw_sprites(int priority)
{
    for (int f = SPRITE_COUNT - 1; f >= 0; f--) {
        const uint16_t *s = &A.sprite_buffer[f * 4];
        if (((s[0] >> 12) & 3) != priority) continue;
        const int code = s[1] & 0xfff;
        const bool fx = s[1] & 0x2000, fy = s[1] & 0x1000;
        const int color = (s[2] >> 8) & 0x1f, clut = s[2] & 0x7f;
        const int sx = s[3], sy = SPRITE_OFFSET + 240 - (s[0] & 0x1ff);
        for (int py = 0; py < 16; py++) {
            const int dy = sy + (fy ? 15 - py : py);
            if (dy < 0 || dy >= 512) continue;
            for (int px = 0; px < 16; px++) {
                const int dx = sx + (fx ? 15 - px : px);
                if (dx < 0 || dx >= 512) continue;
                const unsigned raw = arc_gfx_px(&kSprL, A.sprites, SPRITE_SIZE, code, px, py);
                const unsigned idx = A.ram_clut[clut * 16 + raw] & 0xf;
                if (idx == 15) continue;
                uint16_t *d = px_at(dx, dy);
                if (d) *d = A.palette[(0x200 + (color << 4) + idx) % 0x801];
            }
        }
    }
}

static void render_frame(void)
{
#if NB1414
    A.scroll_fg_x = (uint16_t)(A.ram_txt[0xd] | (A.ram_txt[0xe] << 8));
    A.scroll_fg_y = (uint16_t)(A.ram_txt[0xb] | (A.ram_txt[0xc] << 8));
#endif
    const bool text_on = (A.video_reg & 0x100) != 0;
    // black, the text below, the background, the foreground (with the
    // sprites between)
    for (int Y = OY; Y < OY + RH; Y++)
        for (int X = OX; X < OX + RW; X++) {
            uint16_t c = A.palette[0] & 0, *d = px_at(X, Y);
            bool under;
            int t;
            if (text_on && (t = text_px(X, Y, &under)) >= 0 && under) c = A.palette[t];
            if ((A.video_reg & 0x800) && Y < 256) {
                const int b = tile_px(A.ram_bg, A.bg, 0x20000, 0x600, 0x3ff, (X + A.scroll_bg_x) & 1023, (Y + A.scroll_bg_y) & 511);
                if (b >= 0) c = A.palette[b];
            }
            *d = c;
        }
    if (A.video_reg & 0x200) draw_sprites(2);
    if (A.video_reg & 0x400)
        for (int Y = OY; Y < OY + RH && Y < 256; Y++)
            for (int X = OX; X < OX + RW; X++) {
                const int f = tile_px(A.ram_fg, A.fg, 0x20000, 0x400, 0x7ff, (X + (A.scroll_fg_x & 0x3ff)) & 1023,
                                      (Y + (A.scroll_fg_y & 0x3ff)) & 511);
                if (f >= 0) *px_at(X, Y) = A.palette[f];
            }
    if (A.video_reg & 0x200) draw_sprites(1);
    if (text_on)
        for (int Y = OY; Y < OY + RH; Y++)
            for (int X = OX; X < OX + RW; X++) {
                bool under;
                const int t = text_px(X, Y, &under);
                if (t >= 0 && !under) *px_at(X, Y) = A.palette[t];
            }
    if (A.video_reg & 0x200) draw_sprites(0);
    A.frame_counter++;
    memcpy(A.sprite_buffer, A.ram_sprites, sizeof A.sprite_buffer);
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&A.fb, y); }

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&A.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    // the program: words from byte pairs
    const int n = (int)(sizeof kProg / sizeof kProg[0]);
    uint32_t key = 0x60000;
    for (int i = 0; i < n; i++) key = key * 31 + kProg[i].crc + kProg[i].p;
    arc_blob b;
    if (arc_blob_begin(&b, "program", 0x60000, key)) {
        uint8_t *img = (uint8_t *)calloc(1, 0x60000), *buf = (uint8_t *)malloc(0x10000);
        if (!img || !buf) return false;
        for (int i = 0; i < n; i++) {
            const arc_rom one = { kProg[i].name, kProg[i].size, 0, kProg[i].crc };
            if (!arc_load(&one, 1, buf, one.size)) return false;
            const uint32_t word = kProg[i].p >> 1, lane = kProg[i].p & 1;
            for (uint32_t k = 0; k < kProg[i].size && (word + k) * 2 + 1 < 0x60000; k++)
                img[(word + k) * 2 + (lane ? 0 : 1)] = buf[k];
        }
#if ARC_VARIANT == 3
        // the protection checks patched out (as dsp-cpp)
        img[0x1d6] = 0x00;
        img[0x1d7] = 0x01;
        img[0x488] = 0x4e;
        img[0x489] = 0x71;
#endif
        arc_blob_write(&b, 0, img, 0x60000);
        free(img);
        free(buf);
    }
    if (!(A.rom = arc_blob_end(&b)) || !ARC_LOAD(kSound, A.sound_mem, 0x10000) ||
        !(A.chars = ARC_ROM_BLOB("chars", kChars, 0x8000)) || !(A.bg = ARC_ROM_BLOB("bg", kBg, 0x20000)) ||
        !(A.fg = ARC_ROM_BLOB("fg", kFg, 0x20000)) || !(A.sprites = ARC_ROM_BLOB("sprites", kSprites, SPRITE_SIZE)))
        return false;
#if NB1414
    if (!ARC_LOAD(kNb, A.nb_rom, 0x4000)) return false;
#endif
    A.sound.read = sound_read;
    A.sound.write = sound_write;
    A.sound.in = sound_in;
    A.sound.out = sound_out;
    A.sound.cycles = sound_cycles;
    zd_init(&A.sound);
    ym3812d_init(&A.ym, SOUND_CLOCK, ARC_VARIANT != 0, 0.4f);
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    A.dsw_a = A.dsw_b = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&cpu);
    zd_reset(&A.sound);
    ym3812d_reset(&A.ym);
    A.dac0 = A.dac1 = 0;
    A.frame_counter = 0;
    A.scroll_fg_x = A.scroll_fg_y = A.scroll_bg_x = A.scroll_bg_y = 0;
    A.soundlatch = 0;
    A.video_reg = 0;
    A.in0 = A.in1 = 0xffff;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)(MAIN_CLOCK / (uint32_t)(SCANLINES * FPS));
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 248) {
            m68kd_set_irq(&cpu, IRQ_LEVEL, M68_ASSERT);
            render_frame();
        }
        m68kd_run(&cpu, mc);
        zd_run(&A.sound, mc / 2);
    }
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&A.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint16_t a = 0xffff, b = 0xffff;
    if (p1 & AP_UP) a &= ~0x0001;
    if (p1 & AP_DOWN) a &= ~0x0002;
    if (p1 & AP_LEFT) a &= ~0x0004;
    if (p1 & AP_RIGHT) a &= ~0x0008;
    if (p1 & AP_B1) a &= ~0x0010;
    if (p1 & AP_B2) a &= ~0x0020;
    if (p1 & AP_B3) a &= ~0x0040;
    if (p1 & AP_START) a &= ~0x0100;
    if (p2 & AP_START) a &= ~0x0200;
    if (in->coin1) a &= ~0x0400;
    if (in->coin2) a &= ~0x0800;
    if (p2 & AP_UP) b &= ~0x0001;
    if (p2 & AP_DOWN) b &= ~0x0002;
    if (p2 & AP_LEFT) b &= ~0x0004;
    if (p2 & AP_RIGHT) b &= ~0x0008;
    if (p2 & AP_B1) b &= ~0x0010;
    if (p2 & AP_B2) b &= ~0x0020;
    if (p2 & AP_B3) b &= ~0x0040;
    A.in0 = a;
    A.in1 = b;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw_a = v;
    else if (bank == 1) A.dsw_b = v;
}
