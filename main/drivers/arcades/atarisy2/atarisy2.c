// atarisy2.c - Atari System 2 (1984-1986) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/atari_system2 (after MAME's
// atarisy2.cpp). A DEC T-11 (cpu/t11dsp) whose 020000-037777 window is banked
// by the SLAPSTIC between the alphanumerics / motion object RAM and the two
// halves of the playfield, two banked ROM windows; a 6502 with a YM2151, two
// POKEYs and a TMS5220C. 512x384.
// ARC_VARIANT: 0 Paperboy, 1 Super Sprint, 2 APB (rotated), 3 720 Degrees.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/t11dsp/t11dsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../machine/slapstic.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/pokey.h"
#include "../../../sound/tms5220.h"
#include "../../../video/atari_mo.h"

#define PAPERBOY (ARC_VARIANT == 0)
#define SSPRINT (ARC_VARIANT == 1)
#define APB (ARC_VARIANT == 2)
#define DEG720 (ARC_VARIANT == 3)

#define SW 512                                  // the screen as drawn
#define SH 384
#define PF_W 1024
#define PF_H 512
#define SCANLINES 416
#define MASTER_CLOCK 20000000
#define VIDEO_CLOCK 32000000
#define SOUND_XTAL 14318181
#define MAIN_CLOCK (MASTER_CLOCK / 2)
#define SOUND_CLOCK (SOUND_XTAL / 8)
#define YM_CLOCK (SOUND_XTAL / 4)
#define FPS ((double)VIDEO_CLOCK / 2.0 / 640.0 / (double)SCANLINES)
#define RATE 44100
#if APB
#define W SH
#define H SW
#else
#define W SW
#define H SH
#endif
#define HAS_TMS (!SSPRINT)

// A slice of a ROM file: `source` the offset inside the file (MAME's
// ROM_CONTINUE), `dest` the offset in the region (odd: the high byte lane of
// the T-11 program).
typedef struct { const char *name; uint32_t crc, source, length, dest; } chunk;
#define N(k) ((int)(sizeof k / sizeof k[0]))

#if PAPERBOY
static const char *const kSets[] = { "paperboy", NULL };
const arc_info arc_game = { "paperboy", "Paperboy", "Atari Games", 1984, W, H, FPS, RATE, kSets,
                            { "LANZAR", "FRENAR", NULL }, ARC_JOY_8WAY };
#define SLAPSTIC 105
#define PF_SIZE 0x20000
#define MO_SIZE 0x40000
#define ALPHA_SIZE 0x2000
static const chunk kMain[] = {
    {"cpu_l07.rv3", 0x4024bb9b, 0, 0x4000, 0x008000}, {"cpu_n07.rv3", 0x0260901a, 0, 0x4000, 0x008001},
    {"cpu_f06.rv2", 0x3fea86ac, 0, 0x4000, 0x010000}, {"cpu_n06.rv2", 0x711b17ba, 0, 0x4000, 0x010001},
    {"cpu_j06.rv1", 0xa754b12d, 0, 0x4000, 0x030000}, {"cpu_p06.rv1", 0x89a1ff9c, 0, 0x4000, 0x030001},
    {"cpu_k06.rv1", 0x290bb034, 0, 0x4000, 0x050000}, {"cpu_r06.rv1", 0x826993de, 0, 0x4000, 0x050001},
    {"cpu_l06.rv2", 0x8a754466, 0, 0x4000, 0x070000}, {"cpu_s06.rv2", 0x224209f9, 0, 0x4000, 0x070001}};
static const chunk kSound[] = {
    {"cpu_a02.rv3", 0xba251bc4, 0, 0x4000, 0x4000}, {"cpu_b02.rv2", 0xe4e7a8b9, 0, 0x4000, 0x8000},
    {"cpu_c02.rv2", 0xd44c2aa2, 0, 0x4000, 0xc000}};
static const chunk kPlayfield[] = {
    {"vid_a06.rv1", 0xb32ffddf, 0, 0x8000, 0x00000}, {"vid_b06.rv1", 0x301b849d, 0, 0x4000, 0x0c000},
    {"vid_c06.rv1", 0x7bb59d68, 0, 0x8000, 0x10000}, {"vid_d06.rv1", 0x1a1d4ba8, 0, 0x4000, 0x1c000}};
static const chunk kMotion[] = {
    {"vid_l06.rv1", 0x067ef202, 0, 0x8000, 0x00000}, {"vid_k06.rv1", 0x76b977c4, 0, 0x8000, 0x08000},
    {"vid_j06.rv1", 0x2a3cc8d0, 0, 0x8000, 0x10000}, {"vid_h06.rv1", 0x6763a321, 0, 0x8000, 0x18000},
    {"vid_s06.rv1", 0x0a321b7b, 0, 0x8000, 0x20000}, {"vid_p06.rv1", 0x5bd089ee, 0, 0x8000, 0x28000},
    {"vid_n06.rv1", 0xc34a517d, 0, 0x8000, 0x30000}, {"vid_m06.rv1", 0xdf723956, 0, 0x8000, 0x38000}};
static const chunk kAlpha[] = {{"vid_t06.rv1", 0x60d7aebb, 0, 0x2000, 0x0000}};
static const chunk kEeprom[] = {{"paperboy-eeprom.bin", 0x756b90cc, 0, 0x200, 0x0000}};
#elif SSPRINT
static const char *const kSets[] = { "ssprint", NULL };
const arc_info arc_game = { "ssprint", "Super Sprint", "Atari Games", 1986, W, H, FPS, RATE, kSets,
                            { "ACELERAR", "PEDAL 2", "START 3", "PEDAL 3" }, ARC_JOY_2WAY };
#define SLAPSTIC 108
#define PF_SIZE 0x80000
#define MO_SIZE 0x40000
#define ALPHA_SIZE 0x4000
static const chunk kMain[] = {
    {"136042-330.7l", 0xee312027, 0, 0x4000, 0x008000}, {"136042-331.7n", 0x2ef15354, 0, 0x4000, 0x008001},
    {"136042-329.6f", 0xed1d6205, 0, 0x8000, 0x010000}, {"136042-325.6n", 0xaecaa2bf, 0, 0x8000, 0x010001},
    {"136042-127.6k", 0xde6c4db9, 0, 0x8000, 0x050000}, {"136042-123.6r", 0xaff23b5a, 0, 0x8000, 0x050001},
    {"136042-126.6l", 0x92f5392c, 0, 0x8000, 0x070000}, {"136042-122.6s", 0x0381f362, 0, 0x8000, 0x070001}};
static const chunk kSound[] = {
    {"136042-419.2bc", 0xb277915a, 0, 0x4000, 0x8000}, {"136042-420.2d", 0x170b2c53, 0, 0x4000, 0xc000}};
static const chunk kPlayfield[] = {
    {"136042-105.6a", 0x911499fe, 0x0000, 0x8000, 0x20000}, {"136042-105.6a", 0x911499fe, 0x8000, 0x8000, 0x00000},
    {"136042-106.6b", 0xa39b25ed, 0x0000, 0x8000, 0x08000}, {"136042-101.7a", 0x6d015c72, 0x0000, 0x8000, 0x30000},
    {"136042-101.7a", 0x6d015c72, 0x8000, 0x8000, 0x10000}, {"136042-102.7b", 0x54e21f0a, 0x0000, 0x8000, 0x18000},
    {"136042-107.6c", 0xb7ded658, 0x0000, 0x8000, 0x60000}, {"136042-107.6c", 0xb7ded658, 0x8000, 0x8000, 0x40000},
    {"136042-108.6de", 0x4a804a4c, 0x0000, 0x8000, 0x48000}, {"136042-104.7de", 0x339644ed, 0x0000, 0x8000, 0x70000},
    {"136042-104.7de", 0x339644ed, 0x8000, 0x8000, 0x50000}, {"136042-103.7c", 0x64d473a8, 0x0000, 0x8000, 0x58000}};
static const chunk kMotion[] = {
    {"136042-113.6l", 0xf869b0fc, 0, 0x8000, 0x00000}, {"136042-112.6k", 0xabcbc114, 0, 0x8000, 0x08000},
    {"136042-110.6jh", 0x9e91e734, 0, 0x8000, 0x10000}, {"136042-109.6fh", 0x3a051f36, 0, 0x8000, 0x18000},
    {"136042-117.6rs", 0xb15c1b90, 0, 0x8000, 0x20000}, {"136042-116.6pr", 0x1dcdd5aa, 0, 0x8000, 0x28000},
    {"136042-115.6n", 0xfb5677d9, 0, 0x8000, 0x30000}, {"136042-114.6m", 0x35e70a8d, 0, 0x8000, 0x38000}};
static const chunk kAlpha[] = {{"136042-118.6t", 0x8489d113, 0, 0x4000, 0x0000}};
static const chunk kEeprom[] = {{"ssprint-eeprom.bin", 0x9301ed27, 0, 0x200, 0x0000}};
#elif APB
static const char *const kSets[] = { "apb", NULL };
const arc_info arc_game = { "apb", "APB - All Points Bulletin", "Atari Games", 1987, W, H, FPS, RATE, kSets,
                            { "ACELERAR", "SIRENA", "DISPARO", NULL }, ARC_JOY_2WAY };
#define SLAPSTIC 110
#define PF_SIZE 0x80000
#define MO_SIZE 0x100000
#define ALPHA_SIZE 0x4000
static const chunk kMain[] = {
    {"136051-2126.7l", 0x8edf4726, 0, 0x4000, 0x008000}, {"136051-2127.7n", 0xe2b2aff2, 0, 0x4000, 0x008001},
    {"136051-7128.6f", 0xc08504d2, 0, 0x10000, 0x010000}, {"136051-7129.6n", 0x79adb57f, 0, 0x10000, 0x010001},
    {"136051-1130.6j", 0xf64c752e, 0, 0x10000, 0x030000}, {"136051-1131.6p", 0x0a506e04, 0, 0x10000, 0x030001},
    {"136051-1132.6l", 0x6d0e7a4e, 0, 0x10000, 0x070000}, {"136051-1133.6s", 0xaf88d429, 0, 0x10000, 0x070001}};
static const chunk kSound[] = {
    {"136051-5134.2a", 0x1c8bdeed, 0, 0x4000, 0x4000}, {"136051-5135.2bc", 0xed6adb91, 0, 0x4000, 0x8000},
    {"136051-5136.2d", 0x341f8486, 0, 0x4000, 0xc000}};
static const chunk kPlayfield[] = {
    {"136051-1118.6a", 0x93752c49, 0x0000, 0x8000, 0x00000}, {"136051-1120.6bc", 0x043086f8, 0x0000, 0x8000, 0x28000},
    {"136051-1120.6bc", 0x043086f8, 0x8000, 0x8000, 0x08000}, {"136051-1122.7a", 0x5ee79481, 0x0000, 0x8000, 0x30000},
    {"136051-1122.7a", 0x5ee79481, 0x8000, 0x8000, 0x10000}, {"136051-1124.7bc", 0x27760395, 0x0000, 0x8000, 0x38000},
    {"136051-1124.7bc", 0x27760395, 0x8000, 0x8000, 0x18000}, {"136051-1117.6cd", 0xcfc3f8a3, 0x0000, 0x8000, 0x40000},
    {"136051-1119.6de", 0x68850612, 0x0000, 0x8000, 0x68000}, {"136051-1119.6de", 0x68850612, 0x8000, 0x8000, 0x48000},
    {"136051-1121.7de", 0xc7977062, 0x0000, 0x8000, 0x70000}, {"136051-1121.7de", 0xc7977062, 0x8000, 0x8000, 0x50000},
    {"136051-1123.7cd", 0x3c96c848, 0x0000, 0x8000, 0x78000}, {"136051-1123.7cd", 0x3c96c848, 0x8000, 0x8000, 0x58000}};
static const chunk kMotion[] = {
    {"136051-1105.6t", 0x9b78a88e, 0x0000, 0x8000, 0x020000}, {"136051-1105.6t", 0x9b78a88e, 0x8000, 0x8000, 0x000000},
    {"136051-1106.6rs", 0x4787ff58, 0x0000, 0x8000, 0x028000}, {"136051-1106.6rs", 0x4787ff58, 0x8000, 0x8000, 0x008000},
    {"136051-1107.6pr", 0x0e85f2ac, 0x0000, 0x8000, 0x030000}, {"136051-1107.6pr", 0x0e85f2ac, 0x8000, 0x8000, 0x010000},
    {"136051-1108.6n", 0x70ff9308, 0x0000, 0x8000, 0x038000}, {"136051-1108.6n", 0x70ff9308, 0x8000, 0x8000, 0x018000},
    {"136051-1113.6m", 0x4a445356, 0x0000, 0x8000, 0x060000}, {"136051-1113.6m", 0x4a445356, 0x8000, 0x8000, 0x040000},
    {"136051-1114.6kl", 0xb9b27f3c, 0x0000, 0x8000, 0x068000}, {"136051-1114.6kl", 0xb9b27f3c, 0x8000, 0x8000, 0x048000},
    {"136051-1115.6jk", 0xa7671dd8, 0x0000, 0x8000, 0x070000}, {"136051-1115.6jk", 0xa7671dd8, 0x8000, 0x8000, 0x050000},
    {"136051-1116.6h", 0x879fc7de, 0x0000, 0x8000, 0x078000}, {"136051-1116.6h", 0x879fc7de, 0x8000, 0x8000, 0x058000},
    {"136051-1101.5t", 0x0ef13513, 0x0000, 0x8000, 0x0a0000}, {"136051-1101.5t", 0x0ef13513, 0x8000, 0x8000, 0x080000},
    {"136051-1102.5rs", 0x401e06fd, 0x0000, 0x8000, 0x0a8000}, {"136051-1102.5rs", 0x401e06fd, 0x8000, 0x8000, 0x088000},
    {"136051-1103.5pr", 0x50d820e8, 0x0000, 0x8000, 0x0b0000}, {"136051-1103.5pr", 0x50d820e8, 0x8000, 0x8000, 0x090000},
    {"136051-1104.5n", 0x912d878f, 0x0000, 0x8000, 0x0b8000}, {"136051-1104.5n", 0x912d878f, 0x8000, 0x8000, 0x098000},
    {"136051-1109.5m", 0x6716a408, 0x0000, 0x8000, 0x0e0000}, {"136051-1109.5m", 0x6716a408, 0x8000, 0x8000, 0x0c0000},
    {"136051-1110.5kl", 0x7e184981, 0x0000, 0x8000, 0x0e8000}, {"136051-1110.5kl", 0x7e184981, 0x8000, 0x8000, 0x0c8000},
    {"136051-1111.5jk", 0x353a14fd, 0x0000, 0x8000, 0x0f0000}, {"136051-1111.5jk", 0x353a14fd, 0x8000, 0x8000, 0x0d0000},
    {"136051-1112.5h", 0x3af7c50f, 0x0000, 0x8000, 0x0f8000}, {"136051-1112.5h", 0x3af7c50f, 0x8000, 0x8000, 0x0d8000}};
static const chunk kAlpha[] = {{"136051-1125.4t", 0x05a0341c, 0, 0x4000, 0x0000}};
#else
static const char *const kSets[] = { "720", NULL };
const arc_info arc_game = { "720", "720 Degrees", "Atari Games", 1986, W, H, FPS, RATE, kSets,
                            { "PATADA", "SALTO", NULL }, ARC_JOY_8WAY };
#define SLAPSTIC 107
#define PF_SIZE 0x40000
#define MO_SIZE 0x100000
#define ALPHA_SIZE 0x4000
static const chunk kMain[] = {
    {"136047-3126.7lm", 0x43abd367, 0, 0x4000, 0x008000}, {"136047-3127.7mn", 0x772e1e5b, 0, 0x4000, 0x008001},
    {"136047-3128.6fh", 0xbf6f425b, 0, 0x10000, 0x010000}, {"136047-4131.6mn", 0x2ea8a20f, 0, 0x10000, 0x010001},
    {"136047-1129.6hj", 0xeabf0b01, 0, 0x10000, 0x030000}, {"136047-1132.6p", 0xa24f333e, 0, 0x10000, 0x030001},
    {"136047-1130.6k", 0x93fba845, 0, 0x10000, 0x050000}, {"136047-1133.6r", 0x53c177be, 0, 0x10000, 0x050001}};
static const chunk kSound[] = {
    {"136047-2134.2a", 0x0db4ca28, 0, 0x4000, 0x4000}, {"136047-1135.2b", 0xb1f157d0, 0, 0x4000, 0x8000},
    {"136047-2136.2cd", 0x00b06bec, 0, 0x4000, 0xc000}};
static const chunk kPlayfield[] = {
    {"136047-1121.6a", 0x7adb5f9a, 0, 0x8000, 0x00000}, {"136047-1122.6b", 0x41b60141, 0, 0x8000, 0x08000},
    {"136047-1123.7a", 0x501881d5, 0, 0x8000, 0x10000}, {"136047-1124.7b", 0x096f2574, 0, 0x8000, 0x18000},
    {"136047-1117.6c", 0x5a55f149, 0, 0x8000, 0x20000}, {"136047-1118.6d", 0x9bb2429e, 0, 0x8000, 0x28000},
    {"136047-1119.7d", 0x8f7b20e5, 0, 0x8000, 0x30000}, {"136047-1120.7c", 0x46af6d35, 0, 0x8000, 0x38000}};
static const chunk kMotion[] = {
    {"136047-1109.6t", 0x0a46b693, 0x0000, 0x8000, 0x020000}, {"136047-1109.6t", 0x0a46b693, 0x8000, 0x8000, 0x000000},
    {"136047-1110.6sr", 0x457d7e38, 0x0000, 0x8000, 0x028000}, {"136047-1110.6sr", 0x457d7e38, 0x8000, 0x8000, 0x008000},
    {"136047-1111.6p", 0xffad0a5b, 0x0000, 0x8000, 0x030000}, {"136047-1111.6p", 0xffad0a5b, 0x8000, 0x8000, 0x010000},
    {"136047-1112.6n", 0x06664580, 0x0000, 0x8000, 0x038000}, {"136047-1112.6n", 0x06664580, 0x8000, 0x8000, 0x018000},
    {"136047-1113.6m", 0x7445dc0f, 0x0000, 0x8000, 0x060000}, {"136047-1113.6m", 0x7445dc0f, 0x8000, 0x8000, 0x040000},
    {"136047-1114.6l", 0x23eaceb0, 0x0000, 0x8000, 0x068000}, {"136047-1114.6l", 0x23eaceb0, 0x8000, 0x8000, 0x048000},
    {"136047-1115.6kj", 0x0cc8de53, 0x0000, 0x8000, 0x070000}, {"136047-1115.6kj", 0x0cc8de53, 0x8000, 0x8000, 0x050000},
    {"136047-1116.6jh", 0x2d8f1369, 0x0000, 0x8000, 0x078000}, {"136047-1116.6jh", 0x2d8f1369, 0x8000, 0x8000, 0x058000},
    {"136047-1101.5t", 0x2ac77b80, 0x0000, 0x8000, 0x0a0000}, {"136047-1101.5t", 0x2ac77b80, 0x8000, 0x8000, 0x080000},
    {"136047-1102.5sr", 0xf19c3b06, 0x0000, 0x8000, 0x0a8000}, {"136047-1102.5sr", 0xf19c3b06, 0x8000, 0x8000, 0x088000},
    {"136047-1103.5p", 0x78f9ab90, 0x0000, 0x8000, 0x0b0000}, {"136047-1103.5p", 0x78f9ab90, 0x8000, 0x8000, 0x090000},
    {"136047-1104.5n", 0x77ce4a7f, 0x0000, 0x8000, 0x0b8000}, {"136047-1104.5n", 0x77ce4a7f, 0x8000, 0x8000, 0x098000},
    {"136047-1105.5m", 0xbef5a025, 0x0000, 0x8000, 0x0e0000}, {"136047-1105.5m", 0xbef5a025, 0x8000, 0x8000, 0x0c0000},
    {"136047-1106.5l", 0x92a159c8, 0x0000, 0x8000, 0x0e8000}, {"136047-1106.5l", 0x92a159c8, 0x8000, 0x8000, 0x0c8000},
    {"136047-1107.5kj", 0x0a94a3ef, 0x0000, 0x8000, 0x0f0000}, {"136047-1107.5kj", 0x0a94a3ef, 0x8000, 0x8000, 0x0d0000},
    {"136047-1108.5jh", 0x9815eda6, 0x0000, 0x8000, 0x0f8000}, {"136047-1108.5jh", 0x9815eda6, 0x8000, 0x8000, 0x0d8000}};
static const chunk kAlpha[] = {{"136047-1125.4t", 0x6b7e2328, 0, 0x4000, 0x0000}};
static const chunk kEeprom[] = {{"720-eeprom.bin", 0xcfe1c24e, 0, 0x200, 0x0000}};
#endif

#define MO_TRANSPARENT 0xffff

static struct state {
    t11d cpu;
    m65xx snd;
    ym2151 ym;
    pokey pokey1, pokey2;
    tms5220 tms;
    slapstic slap;
    atari_mo mo;
    uint16_t rom[0x90000 / 2];                  // little-endian words
    uint16_t ram[0x800], palette_ram[0x100], pal[0x100];
    uint16_t alpha_ram[0xc00], mob_ram[0x400], pf_top[0x1000], pf_bottom[0x1000];
    uint8_t sound_memory[0x10000], eeprom[0x200];
    uint8_t *alpha_gfx, *pf_gfx, *mo_gfx;
    int alpha_total, pf_total, mo_total;
    int16_t alpha[SW * SH];
    uint16_t playfield[PF_W * PF_H];
    uint8_t pf_category[PF_W * PF_H];
    uint16_t mo_pen[SW * SH];
    uint8_t mo_priority[SW * SH];
    bool alpha_dirty[0xc00], pf_dirty[0x2000];
    uint32_t rom_bank[2];
    uint8_t vram_bank;
    uint16_t xscroll, yscroll_reg, yscroll, yscroll_pending;
    bool yscroll_reset;
    uint16_t pf_tile_bank[2], xscroll_line[SH], yscroll_line[SH];
    uint8_t interrupt_enable;
    bool video_int, scanline_int, p2portwr, p2portrd;
    bool sound_in_reset, sound_reset_state;
    uint8_t sound_latch, main_latch;
    bool sound_pending, main_pending;
    int sound_irq_counter;
    uint8_t adc_value, adc_input[4], leta_input[4];
    int dial, spin_position, spin_center_count;
    uint8_t spin_rotate_count, dsw[2], buttons;
    bool coin1, coin2, coin3, start3, service_coin, self_test;
    int line;
    int64_t audio_acc;
    int16_t audio[4096];
    int naudio;
    uint16_t fb[W * H];
} *S_;
#define S (*S_)

// ---------------------------------------------------------------------------

static void update_interrupts(void)
{
    t11d_set_irq(&S.cpu, 3, S.video_int ? T11_ASSERT : T11_CLEAR);
    t11d_set_irq(&S.cpu, 2, S.scanline_int ? T11_ASSERT : T11_CLEAR);
    t11d_set_irq(&S.cpu, 1, S.p2portwr ? T11_ASSERT : T11_CLEAR);
    t11d_set_irq(&S.cpu, 0, S.p2portrd ? T11_ASSERT : T11_CLEAR);
}

static void write_sound_chip_reset(uint8_t v)
{
    if ((v & 1) == (S.sound_reset_state ? 1 : 0)) return;
    S.sound_reset_state = (v & 1) != 0;
    ym2151_reset(&S.ym);
    if (!S.sound_reset_state || !HAS_TMS) return;   // (only the 0 -> 1 transition halts the speech chip)
    tms5220_reset(&S.tms);
    tms5220_set_rsq(&S.tms, true);
    tms5220_set_wsq(&S.tms, true);
}

static void set_sound_reset(bool in_reset)
{
    if (in_reset == S.sound_in_reset) return;
    S.sound_in_reset = in_reset;
    if (in_reset) {
        m65xx_set_irq(&S.snd, M65_CLEAR);
        m65xx_set_nmi(&S.snd, M65_CLEAR);
        S.sound_pending = S.main_pending = false;
    } else m65xx_reset(&S.snd);
}

static void bank_select(int index, uint16_t data)
{
    uint8_t bank = (uint8_t)(((data >> 10) & 077) ^ 3);
    bank = (uint8_t)((bank & 0x30) | (((bank >> 1) & 1) << 3) | ((bank & 1) << 2) | (((bank >> 3) & 1) << 1) | ((bank >> 2) & 1));
    S.rom_bank[index & 1] = 0x8000u + (uint32_t)bank * 0x1000u;
}

static uint16_t palette_entry(uint16_t raw)     // MAME's RRRRGGGGBBBBIIII
{
    enum { ZB = 115, Z3 = 78, Z2 = 37, Z1 = 17, Z0 = 9 };
    static const int kIntensity[16] = {0, ZB + Z0, ZB + Z1, ZB + Z1 + Z0, ZB + Z2, ZB + Z2 + Z0, ZB + Z2 + Z1, ZB + Z2 + Z1 + Z0,
                                       ZB + Z3, ZB + Z3 + Z0, ZB + Z3 + Z1, ZB + Z3 + Z1 + Z0, ZB + Z3 + Z2, ZB + Z3 + Z2 + Z0,
                                       ZB + Z3 + Z2 + Z1, ZB + Z3 + Z2 + Z1 + Z0};
    static const int kColor[16] = {0x0, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xe, 0xf, 0xf};
    const int i = kIntensity[raw & 15];
    const uint32_t r = (uint32_t)((kColor[(raw >> 12) & 15] * i) >> 4);
    const uint32_t g = (uint32_t)((kColor[(raw >> 8) & 15] * i) >> 4);
    const uint32_t b = (uint32_t)((kColor[(raw >> 4) & 15] * i) >> 4);
    return arc_rgb565(r << 16 | g << 8 | b);
}

static void write_xscroll(uint16_t v)
{
    S.xscroll = v;
    if (S.pf_tile_bank[0] != (v & 0x0f)) {
        S.pf_tile_bank[0] = (uint16_t)(v & 0x0f);
        memset(S.pf_dirty, 1, sizeof S.pf_dirty);
    }
}

static void write_yscroll(uint16_t v)
{
    S.yscroll_reg = v;
    if ((v & 0x10) == 0) {                      // clocked in at once, else at the next frame
        S.yscroll = (uint16_t)((v >> 6) - (uint16_t)S.line);
        S.yscroll_reset = false;
    } else {
        S.yscroll_pending = (uint16_t)(v >> 6);
        S.yscroll_reset = true;
    }
    if (S.pf_tile_bank[1] != (v & 0x0f)) {
        S.pf_tile_bank[1] = (uint16_t)(v & 0x0f);
        memset(S.pf_dirty, 1, sizeof S.pf_dirty);
    }
}

static uint16_t switch_r(void)
{
    uint16_t low = 0xcf;
    if (SSPRINT && S.start3) low = (uint16_t)(low & ~0x08);
    if (S.main_pending) low |= 0x10;
    if (S.sound_pending) low |= 0x20;
    low = (uint16_t)(low & ~(uint16_t)S.buttons);
    uint16_t high = 0xff;
    if (S.self_test) high = (uint16_t)(high & ~0x80);
    return (uint16_t)(low | (high << 8));
}

static uint8_t switch_6502_r(void)
{
    uint8_t r = 0xf4;
    if (S.sound_pending) r |= 0x01;
    if (S.main_pending) r |= 0x02;
#if HAS_TMS
    if (!tms5220_readyq(&S.tms)) r = (uint8_t)(r & ~0x04);
#endif
    if (S.service_coin) r = (uint8_t)(r & ~0x10);
#if SSPRINT
    if (S.coin1) r = (uint8_t)(r & ~0x20);
    if (S.coin2) r = (uint8_t)(r & ~0x40);
    if (S.coin3) r = (uint8_t)(r & ~0x80);
#else
    if (S.coin3) r = (uint8_t)(r & ~0x20);
    if (S.coin1) r = (uint8_t)(r & ~0x40);
    if (S.coin2) r = (uint8_t)(r & ~0x80);
#endif
    return r;
}

static uint8_t leta_r(int channel)
{
#if DEG720
    if (channel <= 1) return channel == 0 ? (uint8_t)S.spin_center_count : S.spin_rotate_count;
#endif
    return S.leta_input[channel & 3];
}

// ---------------------------------------------------------------------------
// The T-11

static uint16_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x1000) return S.ram[a >> 1];
    if (a < 0x1400) return S.palette_ram[(a >> 1) & 0xff];
    if (a < 0x1480) return S.adc_value;
    if (a < 0x1800) return 0xffff;
    if (a < 0x1c00) return switch_r();
    if (a < 0x2000) {
        S.p2portwr = false;
        update_interrupts();
        S.main_pending = false;
        return (uint16_t)(S.main_latch | 0xff00);
    }
    if (a < 0x4000) {
        const uint16_t o = (uint16_t)((a - 0x2000) >> 1);
        switch (S.vram_bank) {
        case 0: return a < 0x3800 ? S.alpha_ram[o] : S.mob_ram[o & 0x3ff];
        case 2: return S.pf_top[o];
        case 3: return S.pf_bottom[o];
        default: return 0xffff;
        }
    }
    if (a < 0x6000) return S.rom[S.rom_bank[0] + ((a & 0x1fff) >> 1)];
    if (a < 0x8000) return S.rom[S.rom_bank[1] + ((a & 0x1fff) >> 1)];
    if (a < 0x8200) {
        const uint16_t v = S.rom[a >> 1];
        S.vram_bank = slapstic_tweak(&S.slap, (uint16_t)((a & 0x1ff) >> 1));
        return v;
    }
    return S.rom[a >> 1];
}

static void main_write(void *u, uint16_t a, uint16_t v, uint16_t mask)
{
    (void)u;
#define COMBINE(old) ((uint16_t)(((old) & ~mask) | (v & mask)))
    const bool low_byte = (mask & 0x00ff) != 0;   // (the 8 bit registers: the low half only)
    if (a < 0x1000) S.ram[a >> 1] = COMBINE(S.ram[a >> 1]);
    else if (a < 0x1400) {
        const int i = (a >> 1) & 0xff;
        S.palette_ram[i] = COMBINE(S.palette_ram[i]);
        S.pal[i] = palette_entry(S.palette_ram[i]);
    } else if (a < 0x1480) bank_select((a >> 1) & 1, v);
    else if (a < 0x1500) {
        if (low_byte) S.adc_value = S.adc_input[((a >> 1) & 7) & 3];
    } else if (a < 0x1580) {
    } else if (a < 0x15a0) {
        if (low_byte) {
            S.p2portrd = false;
            update_interrupts();
        }
    } else if (a < 0x15c0) {
        if (low_byte) {
            set_sound_reset((v & 1) != 0);
            write_sound_chip_reset(0);
        }
    } else if (a < 0x15e0) {
        if (low_byte) {
            S.scanline_int = false;
            update_interrupts();
        }
    } else if (a < 0x1600) {
        if (low_byte) {
            S.video_int = false;
            update_interrupts();
        }
    } else if (a < 0x1680) {
        if (low_byte) S.interrupt_enable = (uint8_t)(v & 0x0f);
    } else if (a < 0x1700) {
        if (low_byte) {
            S.sound_latch = (uint8_t)v;
            S.sound_pending = true;
            m65xx_set_nmi(&S.snd, M65_ASSERT);
        }
    } else if (a < 0x1780) write_xscroll(COMBINE(S.xscroll));
    else if (a < 0x1800) write_yscroll(COMBINE(S.yscroll_reg));
    else if (a < 0x2000) {
    } else if (a < 0x4000) {
        const uint16_t o = (uint16_t)((a - 0x2000) >> 1);
        switch (S.vram_bank) {
        case 0:
            if (a < 0x3800) {
                const uint16_t d = COMBINE(S.alpha_ram[o]);
                if (S.alpha_ram[o] != d) {
                    S.alpha_ram[o] = d;
                    S.alpha_dirty[o] = true;
                }
            } else S.mob_ram[o & 0x3ff] = COMBINE(S.mob_ram[o & 0x3ff]);
            break;
        case 2: {
            const uint16_t d = COMBINE(S.pf_top[o]);
            if (S.pf_top[o] != d) {
                S.pf_top[o] = d;
                S.pf_dirty[o] = true;
            }
            break;
        }
        case 3: {
            const uint16_t d = COMBINE(S.pf_bottom[o]);
            if (S.pf_bottom[o] != d) {
                S.pf_bottom[o] = d;
                S.pf_dirty[o + 0x1000] = true;
            }
            break;
        }
        default: break;
        }
    } else if (a >= 0x8000 && a < 0x8200) S.vram_bank = slapstic_tweak(&S.slap, (uint16_t)((a & 0x1ff) >> 1));
#undef COMBINE
}

// ---------------------------------------------------------------------------
// The sound 6502

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if ((a & ~0x2000) <= 0x0fff) return S.sound_memory[a & 0x0fff];
    if (a >= 0x4000) return S.sound_memory[a];
    const uint16_t base = a & ~0x2600;
    if (base >= 0x1000 && base <= 0x11ff) return S.eeprom[a & 0x1ff];
    if ((a & ~0x2780) >= 0x1800 && (a & ~0x2780) <= 0x180f) return pokey_read(&S.pokey1, a & 0x0f);
    if ((a & ~0x278c) >= 0x1810 && (a & ~0x278c) <= 0x1813) return leta_r(a & 3);
    if ((a & ~0x2780) >= 0x1830 && (a & ~0x2780) <= 0x183f) return pokey_read(&S.pokey2, a & 0x0f);
    if ((a & ~0x278f) == 0x1840) return switch_6502_r();
    if ((a & ~0x278e) == 0x1850 || (a & ~0x278e) == 0x1851) return ym2151_status(&S.ym);
    if ((a & ~0x278f) == 0x1860) {
        S.p2portrd = (S.interrupt_enable & 0x01) != 0;
        update_interrupts();
        S.sound_pending = false;
        m65xx_set_nmi(&S.snd, M65_CLEAR);
        return S.sound_latch;
    }
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if ((a & ~0x2000) <= 0x0fff) {
        S.sound_memory[a & 0x0fff] = v;
        return;
    }
    if (a >= 0x4000) return;
    const uint16_t base = a & ~0x2600;
    if (base >= 0x1000 && base <= 0x11ff) {
        S.eeprom[a & 0x1ff] = v;
        return;
    }
    if ((a & ~0x2780) >= 0x1800 && (a & ~0x2780) <= 0x180f) {
        pokey_write(&S.pokey1, a & 0x0f, v);
        return;
    }
    if ((a & ~0x2780) >= 0x1830 && (a & ~0x2780) <= 0x183f) {
        pokey_write(&S.pokey2, a & 0x0f, v);
        return;
    }
    if ((a & ~0x278e) == 0x1850) {
        ym2151_select(&S.ym, v);
        return;
    }
    if ((a & ~0x278e) == 0x1851) {
        ym2151_write(&S.ym, v);
        return;
    }
    switch (a & ~0x2781) {
    case 0x1870:                                // speech data
        if (HAS_TMS) tms5220_set_data_latch(&S.tms, v);
        return;
    case 0x1874:                                // the response to the main CPU
        S.p2portwr = (S.interrupt_enable & 0x02) != 0;
        update_interrupts();
        S.main_latch = v;
        S.main_pending = true;
        return;
    case 0x1876: return;                        // (coin counters)
    case 0x1878: m65xx_set_irq(&S.snd, M65_CLEAR); return;
    case 0x187a: return;                        // (mixer)
    case 0x187c:                                // the speech clock
        if (HAS_TMS) tms5220_set_clock(&S.tms, MASTER_CLOCK / 4 / (uint32_t)(16 - (12 | ((v >> 5) & 1))) / 2);
        return;
    case 0x187e: write_sound_chip_reset(v); return;
    default: break;
    }
    if ((a & ~0x2780) == 0x1872 || (a & ~0x2780) == 0x1873)
        if (HAS_TMS) tms5220_set_wsq(&S.tms, (a & 1) == 0);
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&S.ym, cycles * 2);
    pokey_run(&S.pokey1, cycles);
    pokey_run(&S.pokey2, cycles);
#if HAS_TMS
    tms5220_tick(&S.tms, (int)(((int64_t)cycles * (int64_t)S.tms.clock + (SOUND_CLOCK / 2)) / SOUND_CLOCK));
#endif
    S.sound_irq_counter += cycles;              // 244.14 Hz
    while (S.sound_irq_counter >= 7331) {
        S.sound_irq_counter -= 7331;
        m65xx_set_irq(&S.snd, M65_ASSERT);
    }
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        int32_t s = ym2151_update(&S.ym) + pokey_update(&S.pokey1) + pokey_update(&S.pokey2);
#if HAS_TMS
        s += tms5220_last_sample(&S.tms);
#endif
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; m65xx_set_irq(&S.snd, on ? M65_HOLD : M65_CLEAR); }
static uint8_t allpot1(void *u) { (void)u; return S.dsw[0]; }
static uint8_t allpot2(void *u) { (void)u; return S.dsw[1]; }

// ---------------------------------------------------------------------------
// Video

static void draw_alpha_tile(int offset)
{
    const int x = offset % 64, y = offset / 64;
    if (y >= 48) return;
    const uint16_t data = S.alpha_ram[offset];
    const int base = 64 + (((data >> 13) & 7) << 2);
    const uint8_t *px = S.alpha_gfx + (size_t)((data & 0x3ff) % S.alpha_total) * 64;
    for (int r = 0; r < 8; r++) {
        int16_t *t = &S.alpha[(y * 8 + r) * SW + x * 8];
        for (int c = 0; c < 8; c++) {
            const uint8_t pen = px[r * 8 + c];
            t[c] = pen == 0 ? -1 : (int16_t)(base + pen);
        }
    }
}

static void draw_playfield_tile(int offset)
{
    const int x = offset % 128, y = offset / 128;
    const uint16_t data = offset < 0x1000 ? S.pf_top[offset] : S.pf_bottom[offset & 0xfff];
    const int code = ((int)S.pf_tile_bank[(data >> 10) & 1] << 10) | (data & 0x3ff);
    const uint8_t category = (uint8_t)((~data >> 14) & 3);
    const int base = 128 + (((data >> 11) & 7) << 4);
    const uint8_t *px = S.pf_gfx + (size_t)(code % S.pf_total) * 64;
    for (int r = 0; r < 8; r++) {
        const size_t t = (size_t)(y * 8 + r) * PF_W + (size_t)x * 8;
        for (int c = 0; c < 8; c++) {
            S.playfield[t + c] = (uint16_t)(base + px[r * 8 + c]);
            S.pf_category[t + c] = category;
        }
    }
}

static void mo_tile(void *u, int code, int color, bool hflip, bool vflip, int x, int y, int gfx, int priority)
{
    (void)u;
    (void)gfx;
    const uint8_t *px = S.mo_gfx + (size_t)(code % S.mo_total) * 256;
    for (int r = 0; r < 16; r++) {
        const int ty = y + r;
        if (ty < 0 || ty >= SH) continue;
        const int sr = vflip ? 15 - r : r;
        for (int c = 0; c < 16; c++) {
            const int tx = x + c;
            if (tx < 0 || tx >= SW) continue;
            const uint8_t pen = px[sr * 16 + (hflip ? 15 - c : c)];
            if (pen == 15) continue;
            const size_t i = (size_t)(ty * SW + tx);
            S.mo_pen[i] = (uint16_t)((color + pen) & 0xff);
            S.mo_priority[i] = (uint8_t)priority;
        }
    }
}

static int identity(void *u, int v) { (void)u; return v; }
static const atari_mo_lookup kLookup = {identity, identity, NULL};

static void compose_frame(void)
{
    for (int y = 0; y < SH; y++) {
        const int sx = (int)S.xscroll_line[y] & (PF_W - 1);
        const int py = (y + ((int)S.yscroll_line[y] & (PF_H - 1))) & (PF_H - 1);
        for (int x = 0; x < SW; x++) {
            const size_t pf = (size_t)py * PF_W + (size_t)((x + sx) & (PF_W - 1));
            const size_t sc = (size_t)(y * SW + x);
            uint16_t pen = S.playfield[pf];
            const uint16_t mo = S.mo_pen[sc];
            if (mo != MO_TRANSPARENT) {
                if (((int)S.mo_priority[sc] + (int)S.pf_category[pf]) & 2) {
                    if ((pen & 0x08) == 0) pen = mo;   // (a high priority playfield: below pen 8 only)
                } else pen = mo;
            }
            if (S.alpha[sc] != -1) pen = (uint16_t)S.alpha[sc];
#if APB
            S.fb[(SW - 1 - x) * SH + y] = S.pal[pen & 0xff];   // (ROT270)
#else
            S.fb[sc] = S.pal[pen & 0xff];
#endif
        }
    }
}

static void update_video(void)
{
    for (int o = 0; o < 0xc00; o++)
        if (S.alpha_dirty[o]) {
            draw_alpha_tile(o);
            S.alpha_dirty[o] = false;
        }
    for (int o = 0; o < 0x2000; o++)
        if (S.pf_dirty[o]) {
            draw_playfield_tile(o);
            S.pf_dirty[o] = false;
        }
    for (int i = 0; i < SW * SH; i++) S.mo_pen[i] = MO_TRANSPARENT;
    memset(S.mo_priority, 0, sizeof S.mo_priority);
    atari_mo_draw(&S.mo, S.mob_ram, 0, 0, -1, &kLookup, mo_tile, NULL);
    compose_frame();
}

// ---------------------------------------------------------------------------

static bool load_region(const chunk *list, int n, uint8_t *region, uint32_t size, bool interleaved)
{
    uint8_t *file = (uint8_t *)malloc(0x10000);
    bool ok = file != NULL;
    for (int i = 0; ok && i < n; i++) {
        uint32_t file_size = 0;                 // (the file's size: its furthest chunk)
        for (int j = 0; j < n; j++)
            if (!strcmp(list[j].name, list[i].name) && list[j].source + list[j].length > file_size)
                file_size = list[j].source + list[j].length;
        const arc_rom r = {list[i].name, file_size, 0, list[i].crc};
        if (!(ok = arc_load(&r, 1, file, 0x10000))) break;
        const uint32_t span = interleaved ? list[i].length * 2 : list[i].length;
        if ((list[i].dest & ~1u) + span > size) {
            ok = false;
            break;
        }
        if (interleaved) {
            const uint32_t b = list[i].dest & ~1u, lane = list[i].dest & 1u;
            for (uint32_t j = 0; j < list[i].length; j++) region[b + j * 2 + lane] = file[list[i].source + j];
        } else memcpy(region + list[i].dest, file + list[i].source, list[i].length);
    }
    free(file);
    return ok;
}

static const int kX8[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static const int kY8[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kX16[16] = {0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27};
static const int kY16[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};

static bool load_roms(void)
{
    uint8_t *buf = (uint8_t *)calloc(1, MO_SIZE > 0x90000 ? MO_SIZE : 0x90000);
    if (!buf) return false;
    bool ok = load_region(kMain, N(kMain), buf, 0x90000, true);
    if (ok) {
#if PAPERBOY
        for (uint32_t i = 0x10000; i < 0x90000; i += 0x20000) {   // (the 16K pairs mirrored over the 64K bank)
            memcpy(buf + i + 0x08000, buf + i, 0x8000);
            memcpy(buf + i + 0x10000, buf + i, 0x8000);
            memcpy(buf + i + 0x18000, buf + i, 0x8000);
        }
#elif SSPRINT
        for (uint32_t i = 0x10000; i < 0x90000; i += 0x20000) memcpy(buf + i + 0x10000, buf + i, 0x10000);
#endif
        for (int i = 0; i < 0x90000 / 2; i++) S.rom[i] = (uint16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8));
    }
    ok = ok && load_region(kSound, N(kSound), S.sound_memory, 0x10000, false);
    if (ok) {
        memset(buf, 0, PF_SIZE);
        if ((ok = load_region(kPlayfield, N(kPlayfield), buf, PF_SIZE, false))) {
            const int half = PF_SIZE / 2 * 8;
            S.pf_total = PF_SIZE / 2 / 16;
            const arc_layout l = {8, 8, S.pf_total, 4, 8 * 8 * 2, false, false, false, {0, 4, half, half + 4}, kX8, kY8};
            ok = (S.pf_gfx = arc_gfx_decode(&l, buf, PF_SIZE)) != NULL;
        }
    }
    if (ok) {
        memset(buf, 0, MO_SIZE);
        if ((ok = load_region(kMotion, N(kMotion), buf, MO_SIZE, false))) {
            for (uint32_t i = 0; i < MO_SIZE; i++) buf[i] = (uint8_t)~buf[i];   // (ROMREGION_INVERT: pen 15 transparent)
            const int half = MO_SIZE / 2 * 8;
            S.mo_total = MO_SIZE / 2 / 64;
            const arc_layout l = {16, 16, S.mo_total, 4, 16 * 16 * 2, false, false, false, {0, 4, half, half + 4}, kX16, kY16};
            ok = (S.mo_gfx = arc_gfx_decode(&l, buf, MO_SIZE)) != NULL;
        }
    }
    if (ok) {
        memset(buf, 0, ALPHA_SIZE);
        if ((ok = load_region(kAlpha, N(kAlpha), buf, ALPHA_SIZE, false))) {
            S.alpha_total = ALPHA_SIZE / 16;
            const arc_layout l = {8, 8, S.alpha_total, 2, 8 * 8 * 2, false, false, false, {0, 4}, kX8, kY8};
            ok = (S.alpha_gfx = arc_gfx_decode(&l, buf, ALPHA_SIZE)) != NULL;
        }
    }
    memset(S.eeprom, 0xff, sizeof S.eeprom);
#if !APB
    if (ok && !load_region(kEeprom, N(kEeprom), buf, 0x200, false)) memset(buf, 0xff, 0x200);   // (optional)
    if (ok) memcpy(S.eeprom, buf, 0x200);
#endif
    free(buf);
    return ok;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    if (!load_roms()) return false;
    t11d_init(&S.cpu, MAIN_CLOCK, 0x36ff);
    S.cpu.read = main_read;
    S.cpu.write = main_write;
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.cycles = sound_cycles;
    ym2151_init(&S.ym, YM_CLOCK, RATE);
    S.ym.irq_handler = ym_irq;
    pokey_init(&S.pokey1);
    pokey_init(&S.pokey2);
    S.pokey1.allpot_read = allpot1;
    S.pokey2.allpot_read = allpot2;
    tms5220_init(&S.tms, MASTER_CLOCK / 4 / 4 / 2, true);
    slapstic_init(&S.slap, SLAPSTIC, NULL);
    static const atari_mo_config cfg = {
        .tile_width = 16, .tile_height = 16, .bankcount = 1, .linked = true, .split = false,
        .maxperline = 0, .palettebase = 0,
        .link_entry = {0, 0, 0, 0x07f8},
        .code_entry = {{0, 0x07ff, 0, 0}, {0x0007, 0, 0, 0}},
        .color_entry = {{0, 0, 0, 0x3000}, {0, 0, 0, 0}},
        .xpos_entry = {0, 0, 0xffc0, 0}, .ypos_entry = {0x7fc0, 0, 0, 0},
        .height_entry = {0, 0x3800, 0, 0}, .hflip_entry = {0, 0x4000, 0, 0},
        .priority_entry = {0, 0, 0, 0xc000}, .neighbor_entry = {0, 0x8000, 0, 0},
        .slipheight = 0};
    atari_mo_init(&S.mo, &cfg, SW + 16, SH + 16);
    S.dsw[0] = S.dsw[1] = 0;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    slapstic_reset(&S.slap);
    S.vram_bank = S.slap.current_bank;
    t11d_reset(&S.cpu);
    ym2151_reset(&S.ym);
    pokey_reset(&S.pokey1);
    pokey_reset(&S.pokey2);
    tms5220_reset(&S.tms);
    tms5220_set_rsq(&S.tms, true);              // (/RS tied high)
    tms5220_set_wsq(&S.tms, true);
    memset(S.ram, 0, sizeof S.ram);
    memset(S.alpha_ram, 0, sizeof S.alpha_ram);
    memset(S.mob_ram, 0, sizeof S.mob_ram);
    memset(S.pf_top, 0, sizeof S.pf_top);
    memset(S.pf_bottom, 0, sizeof S.pf_bottom);
    memset(S.palette_ram, 0, sizeof S.palette_ram);
    memset(S.pal, 0, sizeof S.pal);
    S.rom_bank[0] = S.rom_bank[1] = 0;
    S.xscroll = S.yscroll_reg = S.yscroll = S.yscroll_pending = 0;
    S.yscroll_reset = false;
    S.pf_tile_bank[0] = S.pf_tile_bank[1] = 0;
    memset(S.xscroll_line, 0, sizeof S.xscroll_line);
    memset(S.yscroll_line, 0, sizeof S.yscroll_line);
    S.interrupt_enable = 0;
    S.video_int = S.scanline_int = S.p2portwr = S.p2portrd = false;
    update_interrupts();
    S.sound_reset_state = false;
    S.sound_in_reset = false;
    set_sound_reset(true);
    S.sound_latch = S.main_latch = 0;
    S.sound_pending = S.main_pending = false;
    S.sound_irq_counter = 0;
    m65xx_set_irq(&S.snd, M65_CLEAR);
    m65xx_set_nmi(&S.snd, M65_CLEAR);
    S.adc_value = 0;
    S.adc_input[0] = S.adc_input[1] = 0x80;
    S.adc_input[2] = S.adc_input[3] = 0;
    memset(S.leta_input, 0, sizeof S.leta_input);
    S.dial = S.spin_position = S.spin_center_count = 0;
    S.spin_rotate_count = 0;
    S.buttons = 0;
    S.coin1 = S.coin2 = S.coin3 = S.start3 = S.service_coin = S.self_test = false;
    S.line = 0;
    S.audio_acc = 0;
    memset(S.alpha_dirty, 1, sizeof S.alpha_dirty);
    memset(S.pf_dirty, 1, sizeof S.pf_dirty);
    for (int i = 0; i < SW * SH; i++) S.alpha[i] = -1;
    memset(S.playfield, 0, sizeof S.playfield);
    memset(S.pf_category, 0, sizeof S.pf_category);
    for (int i = 0; i < SW * SH; i++) S.mo_pen[i] = MO_TRANSPARENT;
    memset(S.mo_priority, 0, sizeof S.mo_priority);
    memset(S.fb, 0, sizeof S.fb);
}

int arc_run_frame(const int16_t **audio)
{
    const int main_cycles = (int)((double)MAIN_CLOCK / FPS / SCANLINES + 0.5);
    const int snd_cycles = (int)((double)SOUND_CLOCK / FPS / SCANLINES + 0.5);
    S.naudio = 0;
    if (S.yscroll_reset) {
        S.yscroll = S.yscroll_pending;
        S.yscroll_reset = false;
    }
    for (S.line = 0; S.line < SCANLINES; S.line++) {
        if ((S.line % 64) == 0) {               // the 32V interrupt
            S.scanline_int = (S.interrupt_enable & 0x04) != 0;
            update_interrupts();
        }
        t11d_run(&S.cpu, main_cycles);
        if (!S.sound_in_reset) m65xx_run(&S.snd, snd_cycles);
        if (S.line < SH) {
            S.xscroll_line[S.line] = (uint16_t)(S.xscroll >> 6);
            S.yscroll_line[S.line] = S.yscroll;
        }
        if (S.line == SH) {
            update_video();
            S.video_int = (S.interrupt_enable & 0x08) != 0;
            update_interrupts();
        }
    }
    S.line = 0;
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

static void update_spinner(int direction)       // MAME leta_r(): 144 counts a turn, the centre disc's teeth
{
    const int steps = direction < 0 ? -direction : direction, sign = direction < 0 ? -1 : 1;
    for (int i = 0; i < steps; i++) {
        if (sign < 0) {
            if (--S.spin_position < 0) S.spin_position = 143;
            else if (S.spin_position == 2 || S.spin_position == 3 || S.spin_position == 141 || S.spin_position == 142)
                S.spin_center_count--;
        } else {
            if (++S.spin_position > 143) S.spin_position = 0;
            else if (S.spin_position == 2 || S.spin_position == 3 || S.spin_position == 141 || S.spin_position == 142)
                S.spin_center_count++;
        }
        S.spin_rotate_count = (uint8_t)((int)S.spin_rotate_count + sign);
    }
}

static uint8_t turn(unsigned p)                 // a steering wheel: counts up while turning right
{
    if ((p & AP_RIGHT) && !(p & AP_LEFT)) S.dial += 2;
    if ((p & AP_LEFT) && !(p & AP_RIGHT)) S.dial -= 2;
    return (uint8_t)S.dial;
}

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    S.buttons = 0;
#if SSPRINT
    if (p1 & AP_START) S.buttons |= 0x80;
    if (p2 & AP_START) S.buttons |= 0x40;
    S.start3 = (p1 & AP_B3) != 0;
    S.adc_input[0] = (p1 & AP_B1) ? 0x00 : 0x3f;   // (the pedals: 6 bit pots read inverted)
    S.adc_input[1] = (p1 & AP_B2) ? 0x00 : 0x3f;
    S.adc_input[2] = (p1 & AP_B4) ? 0x00 : 0x3f;
    S.leta_input[0] = turn(p1);
#elif APB
    if (p1 & AP_B2) S.buttons |= 0x02;
    if (p1 & AP_B3) S.buttons |= 0x08;
    S.adc_input[1] = (p1 & AP_B1) ? 0x00 : 0x3f;
    S.leta_input[0] = turn(p1);
#else
    if ((p1 & AP_B1) || (p1 & AP_START)) S.buttons |= 0x80;
    if ((p1 & AP_B2) || (p2 & AP_START)) S.buttons |= 0x40;
#if PAPERBOY
    uint8_t x = 0x80, y = 0x80;                 // (a pair of pots limited to $10-$f0)
    if ((p1 & AP_LEFT) && !(p1 & AP_RIGHT)) x = 0x10;
    if ((p1 & AP_RIGHT) && !(p1 & AP_LEFT)) x = 0xf0;
    if ((p1 & AP_UP) && !(p1 & AP_DOWN)) y = 0x10;
    if ((p1 & AP_DOWN) && !(p1 & AP_UP)) y = 0xf0;
    S.adc_input[0] = x;
    S.adc_input[1] = y;
#else
    int dir = 0;                                // 720: a held arrow spins the disc
    if ((p1 & AP_RIGHT) && !(p1 & AP_LEFT)) dir = 3;
    if ((p1 & AP_LEFT) && !(p1 & AP_RIGHT)) dir = -3;
    update_spinner(dir);
#endif
#endif
    S.coin1 = in->coin1;
    S.coin2 = in->coin2;
    S.coin3 = (p2 & AP_B4) != 0;
    S.service_coin = in->service;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank >= 0 && bank <= 1) S.dsw[bank] = v;
}
