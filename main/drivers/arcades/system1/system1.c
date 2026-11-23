// system1.c - Sega System 1 / 2 (1983-86) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/sega_system1 (from
// dsp-emulator's system1_hw.pas). A Z80 at 4 MHz with its opcodes
// scrambled (a per-game table, or the 315-5177 / 317-000x chips), a sound
// Z80 with two SN76496 fed through a Z80 PIO or an 8255, two 8x8 tile layers
// and 32 sprites drawn from nibble-packed ROMs, mixed by the priority PROM.
// ARC_VARIANT: 0 Wonder Boy, 1 Pitfall II, 2 TeddyBoy Blues, 3 Mr. Viking,
// 4 Sega Ninja, 5 Up'n Down, 6 Flicky, 7 Gardia (System 2: banked ROM, the
// colours through PROMs). The vertical ones turn the picture round unless
// built with SYSTEM1_NO_ROTATE (as dsp-cpp draws it).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../machine/sega_decrypt.h"
#include "../../../machine/z80piodsp.h"
#include "../../../machine/i8255dsp.h"
#include "../../../sound/sn76496.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define MAIN_CLOCK 4000000
#define SOUND_CLOCK 4000000
#define FPS 60.096154
#define SCANLINES 260
#define IRQ_LINE 224
#define RATE 44100
#define H_SRC 224

#if ARC_VARIANT == 0
static const char *const kSets[] = { "wboy", NULL };
static const arc_rom kMain[] = {
    {"epr-7489.116", 0x4000, 0x0000, 0x130f4b70}, {"epr-7490.109", 0x4000, 0x4000, 0x9e656733},
    {"epr-7491.96", 0x4000, 0x8000, 0x1f7d0efe}};
static const arc_rom kChars[] = {
    {"epr-7497.62", 0x2000, 0x0000, 0x08d609ca}, {"epr-7496.61", 0x2000, 0x2000, 0x6f61fdf1},
    {"epr-7495.64", 0x2000, 0x4000, 0x6a0d2c2d}, {"epr-7494.63", 0x2000, 0x6000, 0xa8e281c7},
    {"epr-7493.66", 0x2000, 0x8000, 0x89305df4}, {"epr-7492.65", 0x2000, 0xa000, 0x60f806b1}};
static const arc_rom kSprites[] = {
    {"epr-7485.117", 0x4000, 0x0000, 0xc2891722}, {"epr-7487.04", 0x4000, 0x4000, 0x2d3a421b},
    {"epr-7486.110", 0x4000, 0x8000, 0x8d622c50}, {"epr-7488.05", 0x4000, 0xc000, 0x007c2f1b}};
static const arc_rom kSound[] = {{"epr-7498.120", 0x2000, 0, 0x78ae1e7b}};
static const arc_rom kVideoProm[] = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
#define ID "wboy"
#define TITLE "Wonder Boy"
#define MAKER "Sega / Westone"
#define YEAR 1986
#define BUTTONS { "JUMP", "ATTACK", NULL, NULL }
#define SPRITE_SIZE 0x10000
#define DSW_B 0xec
#define CRYPT_TYPE2 SEGA2_315_5177
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "pitfall2", NULL };
static const arc_rom kMain[] = {
    {"epr-6456a.116", 0x4000, 0x0000, 0xbcc8406b}, {"epr-6457a.109", 0x4000, 0x4000, 0xa016fd2a},
    {"epr-6458a.96", 0x4000, 0x8000, 0x5c30b3e8}};
static const arc_rom kChars[] = {
    {"epr-6474a.62", 0x2000, 0x0000, 0x9f1711b9}, {"epr-6473a.61", 0x2000, 0x2000, 0x8e53b8dd},
    {"epr-6472a.64", 0x2000, 0x4000, 0xe0f34a11}, {"epr-6471a.63", 0x2000, 0x6000, 0xd5bc805c},
    {"epr-6470a.66", 0x2000, 0x8000, 0x1439729f}, {"epr-6469a.65", 0x2000, 0xa000, 0xe4ac6921}};
static const arc_rom kSprites[] = {{"epr-6454a.117", 0x4000, 0x0000, 0xa5d96780}, {"epr-6455.05", 0x4000, 0x4000, 0x32ee64a1}};
static const arc_rom kSound[] = {{"epr-6462.120", 0x2000, 0, 0x86bb9185}};
static const arc_rom kVideoProm[] = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
#define ID "pitfall2"
#define TITLE "Pitfall II"
#define MAKER "Sega"
#define YEAR 1985
#define BUTTONS { "JUMP", NULL, NULL, NULL }
#define SPRITE_SIZE 0x8000
#define DSW_B 0xdc
#define CRYPT SEGA_PITFALL2
#elif ARC_VARIANT == 2
static const char *const kSets[] = { "teddybb", NULL };
static const arc_rom kMain[] = {
    {"epr-6768.116", 0x4000, 0x0000, 0x5939817e}, {"epr-6769.109", 0x4000, 0x4000, 0x14a98ddd},
    {"epr-6770.96", 0x4000, 0x8000, 0x67b0c7c2}};
static const arc_rom kChars[] = {
    {"epr-6747.62", 0x2000, 0x0000, 0xa0e5aca7}, {"epr-6746.61", 0x2000, 0x2000, 0xcdb77e51},
    {"epr-6745.64", 0x2000, 0x4000, 0x0cab75c3}, {"epr-6744.63", 0x2000, 0x6000, 0x0ef8d2cd},
    {"epr-6743.66", 0x2000, 0x8000, 0xc33062b5}, {"epr-6742.65", 0x2000, 0xa000, 0xc457e8c5}};
static const arc_rom kSprites[] = {
    {"epr-6735.117", 0x4000, 0x0000, 0x1be35a97}, {"epr-6737.04", 0x4000, 0x4000, 0x6b53aa7a},
    {"epr-6736.110", 0x4000, 0x8000, 0x565c25d0}, {"epr-6738.05", 0x4000, 0xc000, 0xe116285f}};
static const arc_rom kSound[] = {{"epr6748x.120", 0x2000, 0, 0xc2a1b89d}};
static const arc_rom kVideoProm[] = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
#define ID "teddybb"
#define TITLE "TeddyBoy Blues"
#define MAKER "Sega"
#define YEAR 1985
#define BUTTONS { "FIRE", "JUMP", NULL, NULL }
#define SPRITE_SIZE 0x10000
#define DSW_B 0xfe
#define CRYPT SEGA_TEDDYBOY
#elif ARC_VARIANT == 3
static const char *const kSets[] = { "mrviking", NULL };
static const arc_rom kMain[] = {
    {"epr-5873.129", 0x2000, 0x0000, 0x14d21624}, {"epr-5874.130", 0x2000, 0x2000, 0x6df7de87},
    {"epr-5875.131", 0x2000, 0x4000, 0xac226100}, {"epr-5876.132", 0x2000, 0x6000, 0xe77db1dc},
    {"epr-5755.133", 0x2000, 0x8000, 0xedd62ae1}, {"epr-5756.134", 0x2000, 0xa000, 0x11974040}};
static const arc_rom kChars[] = {
    {"epr-5762.82", 0x2000, 0x0000, 0x4a91d08a}, {"epr-5761.65", 0x2000, 0x2000, 0xf7d61b65},
    {"epr-5760.81", 0x2000, 0x4000, 0x95045820}, {"epr-5759.64", 0x2000, 0x6000, 0x5f9bae4e},
    {"epr-5758.80", 0x2000, 0x8000, 0x808ee706}, {"epr-5757.63", 0x2000, 0xa000, 0x480f7074}};
static const arc_rom kSprites[] = {{"epr-5749.86", 0x4000, 0x0000, 0xe24682cd}, {"epr-5750.93", 0x4000, 0x4000, 0x6564d1ad}};
static const arc_rom kSound[] = {{"epr-5763.3", 0x2000, 0, 0xd712280d}};
static const arc_rom kVideoProm[] = {{"pr-5317.106", 0x100, 0, 0x648350b8}};
#define ID "mrviking"
#define TITLE "Mr. Viking"
#define MAKER "Sega"
#define YEAR 1984
#define BUTTONS { "FIRE", NULL, NULL, NULL }
#define SPRITE_SIZE 0x8000
#define DSW_B 0xfc
#define CRYPT SEGA_MRVIKING
#define PPI 1
#define NARROW 1
#define ROT270 1
#elif ARC_VARIANT == 4
static const char *const kSets[] = { "seganinj", NULL };
static const arc_rom kMain[] = {
    {"epr-6594a.116", 0x4000, 0x0000, 0xa5d0c9d0}, {"epr-6595a.109", 0x4000, 0x4000, 0xb9e6775c},
    {"epr-6596a.96", 0x4000, 0x8000, 0xf2eeb0d8}};
static const arc_rom kChars[] = {
    {"epr-6558.62", 0x2000, 0x0000, 0x2af9eaeb}, {"epr-6592.61", 0x2000, 0x2000, 0x7804db86},
    {"epr-6556.64", 0x2000, 0x4000, 0x79fd26f7}, {"epr-6590.63", 0x2000, 0x6000, 0xbf858cad},
    {"epr-6554.66", 0x2000, 0x8000, 0x5ac9d205}, {"epr-6588.65", 0x2000, 0xa000, 0xdc931dbb}};
static const arc_rom kSprites[] = {
    {"epr-6546.117", 0x4000, 0x0000, 0xa4785692}, {"epr-6548.04", 0x4000, 0x4000, 0xbdf278c1},
    {"epr-6547.110", 0x4000, 0x8000, 0x34451b08}, {"epr-6549a.05", 0x4000, 0xc000, 0x7c51488c}};
static const arc_rom kSound[] = {{"epr-6559.120", 0x2000, 0, 0x5a1570ee}};
static const arc_rom kVideoProm[] = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
#define ID "seganinj"
#define TITLE "Sega Ninja"
#define MAKER "Sega"
#define YEAR 1985
#define BUTTONS { "SHOOT", "JUMP", NULL, NULL }
#define SPRITE_SIZE 0x10000
#define DSW_B 0xdc
#define CRYPT SEGA_SEGANINJA
#elif ARC_VARIANT == 5
static const char *const kSets[] = { "upndown", NULL };
static const arc_rom kMain[] = {
    {"epr5516a.129", 0x2000, 0x0000, 0x038c82da}, {"epr5517a.130", 0x2000, 0x2000, 0x6930e1de},
    {"epr-5518.131", 0x2000, 0x4000, 0x2a370c99}, {"epr-5519.132", 0x2000, 0x6000, 0x9d664a58},
    {"epr-5520.133", 0x2000, 0x8000, 0x208dfbdf}, {"epr-5521.134", 0x2000, 0xa000, 0xe7b8d87a}};
static const arc_rom kChars[] = {
    {"epr-5527.82", 0x2000, 0x0000, 0xb2d616f1}, {"epr-5526.65", 0x2000, 0x2000, 0x8a8b33c2},
    {"epr-5525.81", 0x2000, 0x4000, 0xe749c5ef}, {"epr-5524.64", 0x2000, 0x6000, 0x8b886952},
    {"epr-5523.80", 0x2000, 0x8000, 0xdede35d9}, {"epr-5522.63", 0x2000, 0xa000, 0x5e6d9dff}};
static const arc_rom kSprites[] = {{"epr-5514.86", 0x4000, 0x0000, 0xfcc0a88b}, {"epr-5515.93", 0x4000, 0x4000, 0x60908838}};
static const arc_rom kSound[] = {{"epr-5535.3", 0x2000, 0, 0xcf4e4c45}};
static const arc_rom kVideoProm[] = {{"pr-5317.106", 0x100, 0, 0x648350b8}};
#define ID "upndown"
#define TITLE "Up'n Down"
#define MAKER "Sega"
#define YEAR 1983
#define BUTTONS { "JUMP", NULL, NULL, NULL }
#define SPRITE_SIZE 0x8000
#define DSW_B 0xfe
#define CRYPT SEGA_UPNDOWN
#define PPI 1
#define NARROW 1
#define ROT270 1
#elif ARC_VARIANT == 6
static const char *const kSets[] = { "flicky", NULL };
static const arc_rom kMain[] = {{"epr-5978a.116", 0x4000, 0x0000, 0x296f1492}, {"epr-5979a.109", 0x4000, 0x4000, 0x64b03ef9}};
static const arc_rom kChars[] = {
    {"epr-5868.62", 0x2000, 0x0000, 0x7402256b}, {"epr-5867.61", 0x2000, 0x2000, 0x2f5ce930},
    {"epr-5866.64", 0x2000, 0x4000, 0x967f1d9a}, {"epr-5865.63", 0x2000, 0x6000, 0x03d9a34c},
    {"epr-5864.66", 0x2000, 0x8000, 0xe659f358}, {"epr-5863.65", 0x2000, 0xa000, 0xa496ca15}};
static const arc_rom kSprites[] = {{"epr-5855.117", 0x4000, 0x0000, 0xb5f894a1}, {"epr-5856.110", 0x4000, 0x4000, 0x266af78f}};
static const arc_rom kSound[] = {{"epr-5869.120", 0x2000, 0, 0x6d220d4e}};
static const arc_rom kVideoProm[] = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
#define ID "flicky"
#define TITLE "Flicky"
#define MAKER "Sega"
#define YEAR 1984
#define BUTTONS { "JUMP", NULL, NULL, NULL }
#define SPRITE_SIZE 0x8000
#define DSW_B 0xfe
#define CRYPT SEGA_FLICKY
#define JOY ARC_JOY_2WAY
#else
static const char *const kSets[] = { "gardia", NULL };
static const arc_rom kMain[] = {
    {"epr-10255.1", 0x8000, 0x00000, 0x89282a6b}, {"epr-10254.2", 0x8000, 0x08000, 0x2826b6d8},
    {"epr-10253.3", 0x8000, 0x10000, 0x7911260f}};
static const arc_rom kChars[] = {
    {"epr-10249.61", 0x4000, 0x0000, 0x4e0ad0f2}, {"epr-10248.64", 0x4000, 0x4000, 0x3515d124},
    {"epr-10247.66", 0x4000, 0x8000, 0x541e1555}};
static const arc_rom kSprites[] = {
    {"epr-10234.117", 0x8000, 0x00000, 0x8a6aed33}, {"epr-10233.110", 0x8000, 0x08000, 0xc52784d3},
    {"epr-10236.04", 0x8000, 0x10000, 0xb35ab227}, {"epr-10235.5", 0x8000, 0x18000, 0x006a3151}};
static const arc_rom kSound[] = {{"epr-10243.120", 0x4000, 0, 0x87220660}};
static const arc_rom kVideoProm[] = {{"pr5317.4", 0x100, 0, 0x648350b8}};
static const arc_rom kProms[] = {
    {"pr-7345.3", 0x100, 0x000, 0x8eee0f72}, {"pr-7344.2", 0x100, 0x100, 0x3e7babd7}, {"pr-7343.1", 0x100, 0x200, 0x371c44a6}};
#define ID "gardia"
#define TITLE "Gardia"
#define MAKER "Sega / Coreland"
#define YEAR 1986
#define BUTTONS { "FIRE", "BOMB", NULL, NULL }
#define SPRITE_SIZE 0x20000
#define DSW_B 0x7c
#define CRYPT_TYPE2 SEGA2_317_000X
#define CRYPT_SHIFT 2
#define BANKED 1
#define NARROW 1
#define ROT270 1
#endif
#ifndef CRYPT_SHIFT
#define CRYPT_SHIFT 0
#endif
#ifndef JOY
#define JOY ARC_JOY_8WAY
#endif
#if defined(NARROW)                     // 240 of the 256 columns, from 8
#define W_SRC 240
#define X_OFF 8
#else
#define W_SRC 256
#define X_OFF 0
#endif
#define SPRITE_BANKS (SPRITE_SIZE / 0x8000)
#if defined(ROT270) && !defined(SYSTEM1_NO_ROTATE)
#define W H_SRC
#define H W_SRC
#else
#undef ROT270
#define W W_SRC
#define H H_SRC
#endif

const arc_info arc_game = { ID, TITLE, MAKER, YEAR, W, H, FPS, RATE, kSets, BUTTONS, JOY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sound;
    sn76496 psg0, psg1;
    z80piod pio;
    i8255d ppi;
    // 0000-bfff decrypted (Gardia: 0000-7fff, then its four banks of
    // 8000-bfff), the opcodes of 0000-7fff (blobs: the CYD's in the flash)
    const uint8_t *rom, *opcodes, *sound_rom;
    uint8_t ram[0x2000], bg_ram[0x1000], pal_ram[0x800], mix_collide[0x40], sprite_collide[0x400];   // ram: c000-dfff
    bool mix_collide_summary, sprite_collide_summary;
    uint8_t sound_ram[0x800];
    const uint8_t *chars, *sprite_rom;
    uint8_t mix_lookup[0x100];
    double rw[8], gw[8], bw[8];
    uint16_t palette[0x600];
    // the 256x256 sprite layer: colour | sprite * 16, in two halves of rows
    // (bits 0-7) and a bit array (bit 8)
    uint8_t *spr_lo[2], spr_hi[256 * 256 / 8];
    arc_fb fb;
    int bg_xscroll;
    uint8_t bg_yscroll, sound_latch, videomode, rom_bank, in0, in1, in2, dsw_a, dsw_b;
#if BANKED
    uint8_t proms[0x300];
#endif
    int sound_irq_accum;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

#if BANKED
// System 2: the colour through three PROMs
static int weight4(uint8_t v) { return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1); }
static void set_color(int index, uint8_t v)
{
    S.palette[index] = arc_rgb565((uint32_t)weight4(S.proms[v]) << 16 | (uint32_t)weight4(S.proms[0x100 + v]) << 8 |
                                  (uint32_t)weight4(S.proms[0x200 + v]));
}
#else
static void set_color(int index, uint8_t v)
{
    const int r = arc_combine(S.rw, 3, v & 7), g = arc_combine(S.gw, 3, (v >> 3) & 7), b = arc_combine(S.bw, 2, v >> 6);
    S.palette[index] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
}
#endif

static uint8_t read_data(void *u, uint16_t a)
{
    (void)u;
#if BANKED
    if (a >= 0x8000 && a <= 0xbfff) return S.rom[0x8000 + (S.rom_bank & 3) * 0x4000 + (a & 0x3fff)];
#endif
    if (a <= 0xbfff) return S.rom[a];
    if (a <= 0xd7ff) return S.ram[a - 0xc000];
    if (a <= 0xddff) return S.pal_ram[a & 0x7ff];
    if (a <= 0xdfff) return S.ram[a - 0xc000];
    if (a <= 0xefff) return S.bg_ram[a & 0xfff];
    if (a <= 0xf3ff) return (uint8_t)(S.mix_collide[a & 0x3f] | 0x7e | (S.mix_collide_summary ? 0x80 : 0));
    if (a <= 0xf7ff) return 0xff;
    if (a <= 0xfbff) return (uint8_t)(S.sprite_collide[a & 0x3ff] | 0x7e | (S.sprite_collide_summary ? 0x80 : 0));
    return 0xff;
}

static uint8_t read_opcode(void *u, uint16_t a) { return a <= 0x7fff ? S.opcodes[a] : read_data(u, a); }

static void write_byte(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a <= 0xd7ff || (a >= 0xde00 && a <= 0xdfff)) S.ram[a - 0xc000] = v;
    else if (a <= 0xddff) {
        const int pos = a & 0x7ff;
        if (S.pal_ram[pos] != v) {
            S.pal_ram[pos] = v;
            set_color(pos, v);
        }
    } else if (a <= 0xefff) S.bg_ram[a & 0xfff] = v;
    else if (a <= 0xf3ff) S.mix_collide[a & 0x3f] = 0;
    else if (a <= 0xf7ff) S.mix_collide_summary = false;
    else if (a <= 0xfbff) S.sprite_collide[a & 0x3ff] = 0;
    else S.sprite_collide_summary = false;
}

static uint8_t read_port(void *u, uint16_t port)
{
    (void)u;
    const int p = port & 0x1f;
    if (p <= 0x3) return S.in1;
    if (p <= 0x7) return S.in2;
    if (p <= 0xb) return S.in0;
    if (p == 0xc || p == 0xe) return S.dsw_a;
    if (p == 0xd || p == 0xf || (p >= 0x10 && p <= 0x13)) return S.dsw_b;
#if PPI
    if (p >= 0x14 && p <= 0x17) return i8255d_read(&S.ppi, p & 3);
#else
    if (p >= 0x18 && p <= 0x1b) return z80piod_read(&S.pio, (uint16_t)p);
#endif
    return 0xff;
}

static void write_port(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const int p = port & 0x1f;
#if PPI
    if (p >= 0x14 && p <= 0x17) i8255d_write(&S.ppi, p & 3, v);
#else
    if (p >= 0x18 && p <= 0x1b) z80piod_write(&S.pio, (uint16_t)p, v);
#endif
}

static uint8_t snd_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff) return S.sound_rom[a];
    if (a <= 0x9fff) return S.sound_ram[a & 0x7ff];
    if (a < 0xe000) return 0xff;
#if PPI
    // the latch read strobes PC6 (the 8255's /ACK)
    const uint8_t c = S.ppi.latch[2];
    i8255d_write(&S.ppi, 2, (uint8_t)(c & 0xbf));
    i8255d_write(&S.ppi, 2, (uint8_t)(c | 0x40));
    return S.sound_latch;
#endif
    const uint8_t v = z80piod_port_output(&S.pio, Z80PIO_A);
    z80piod_strobe(&S.pio, Z80PIO_A, false);
    z80piod_strobe(&S.pio, Z80PIO_A, true);
    return v;
}

static void snd_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x9fff) S.sound_ram[a & 0x7ff] = v;
    else if (a <= 0xbfff) sn76496_write(&S.psg0, v);
    else if (a <= 0xdfff) sn76496_write(&S.psg1, v);
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = sn76496_update(&S.psg0) + sn76496_update(&S.psg1);
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// the PIO (or the 8255): port A the sound latch (the PIO's ARDY rising edge
// the sound NMI), port B the video mode and the ROM bank; the 8255's port C
// bit 7 the sound NMI line
static void pio_a_out(void *u, uint8_t v) { (void)u; S.sound_latch = v; }
#if BANKED
static void pio_b_out(void *u, uint8_t v) { (void)u; S.rom_bank = (uint8_t)(((v & 0x40) >> 5) | ((v & 0x04) >> 2)); S.videomode = v; }
#else
static void pio_b_out(void *u, uint8_t v) { (void)u; S.rom_bank = (v >> 2) & 3; S.videomode = v; }
#endif
static void pio_ready_a(void *u, bool state) { (void)u; if (state) zd_set_nmi(&S.sound, ZD_PULSE); }
static void ppi_c_out(void *u, uint8_t v) { (void)u; zd_set_nmi(&S.sound, (v & 0x80) ? ZD_CLEAR : ZD_ASSERT); }

#define MEM(a) S.ram[(a) - 0xc000]

// a pixel of a tile layer (0 at the back, 1 in front): pen | colour << 3
static inline uint16_t tile_px(int layer, int x, int y)
{
    const int offset = (y >> 3) * 32 + (x >> 3), source = (layer << 11) + offset * 2;
    const uint16_t attr = (uint16_t)(S.bg_ram[source] | (S.bg_ram[source + 1] << 8));
    return (uint16_t)(S.chars[(attr & 0x7ff) * 64 + (y & 7) * 8 + (x & 7)] | (((attr >> 5) & 0xff) << 3));
}

static inline uint16_t spr_get(int i)
{
    return (uint16_t)(S.spr_lo[i >> 15][i & 0x7fff] | (((S.spr_hi[i >> 3] >> (i & 7)) & 1) << 8));
}

static void plot_sprite(int f, int destbase, int x, int color)
{
    const int i = destbase + x;
    const uint16_t old = spr_get(i);
    if (old & 0xf) {
        S.sprite_collide[((old >> 4) & 0x1f) + 32 * f] = 1;
        S.sprite_collide_summary = true;
    }
    const uint16_t v = (uint16_t)(color | (f * 0x10));
    S.spr_lo[i >> 15][i & 0x7fff] = (uint8_t)v;
    if (v & 0x100) S.spr_hi[i >> 3] |= (uint8_t)(1 << (i & 7));
    else S.spr_hi[i >> 3] &= (uint8_t)~(1 << (i & 7));
}

static void draw_sprites(void)
{
    for (int f = 0; f < 32; f++) {
        const int base = 0xd000 + f * 0x10;
        int srcaddr = MEM(base + 6) | (MEM(base + 7) << 8);
        const int stride = MEM(base + 4) | (MEM(base + 5) << 8);
        int bank = ((MEM(base + 3) & 0x80) >> 7) | ((MEM(base + 3) & 0x40) >> 5) | ((MEM(base + 3) & 0x20) >> 3);
        const int xstart = ((MEM(base + 2) | (MEM(base + 3) << 8)) & 0x1ff) / 2;
        const int bottom = MEM(base + 1) + 1, top = MEM(base) + 1;
        bank %= SPRITE_BANKS;
        const size_t bank_base = (size_t)bank * 0x8000;
        for (int y = top; y < bottom; y++) {
            const int destbase = (y & 0xff) * 256;
            srcaddr = (srcaddr + stride) & 0xffff;
            if (y > 256) continue;
            const int delta = (srcaddr & 0x8000) ? -1 : 1;
            int cur = srcaddr, x = xstart;
            for (int guard = 0; guard < 512; guard++) {
                const size_t i = bank_base + (size_t)(cur & 0x7fff);
                const uint8_t d = i < SPRITE_SIZE ? S.sprite_rom[i] : 0;
                const int c1 = (cur & 0x8000) ? d & 0xf : d >> 4, c2 = (cur & 0x8000) ? d >> 4 : d & 0xf;
                if (c1 == 0xf) break;
                if (c1 && x >= 0 && x <= 255) plot_sprite(f, destbase, x, c1);
                if (c2 == 0xf) break;
                if (c2 && x + 1 >= 0 && x + 1 <= 255) plot_sprite(f, destbase, x + 1, c2);
                cur = (cur + delta) & 0xffff;
                x += 2;
            }
        }
    }
}

static void render_frame(void)
{
    if (S.videomode & 0x10) {
        for (int y = 0; y < H; y++) memset(arc_fb_row(&S.fb, y), 0, W * 2);
        return;
    }
    memset(S.spr_lo[0], 0, 0x8000);
    memset(S.spr_lo[1], 0, 0x8000);
    memset(S.spr_hi, 0, sizeof S.spr_hi);
    if (MEM(0xd000) != 0xff) draw_sprites();
    for (int y = 0; y < H_SRC; y++) {
        const int bgy = (y + S.bg_yscroll) & 0xff;
#if ROT270
        // column y of the picture, from its bottom up
        const int col = y;
#else
        uint16_t *out = arc_fb_row(&S.fb, y);
#endif
        for (int i = 0; i < W_SRC; i++) {
            const int x = i + X_OFF;
            const uint16_t fg = tile_px(1, x, y), bg = tile_px(0, (x - S.bg_xscroll) & 0xff, bgy);
            const uint16_t spr = spr_get(y * 256 + x);
            const int idx = ((spr & 0xf) == 0 ? 1 : 0) | ((fg & 7) == 0 ? 2 : 0) | (((fg >> 9) & 3) << 2) |
                            ((bg & 7) == 0 ? 16 : 0) | (((bg >> 9) & 3) << 5);
            uint8_t lv = S.mix_lookup[idx & 0xff];
            if (!(lv & 4)) {
                S.mix_collide[((lv & 8) << 2) | ((spr >> 4) & 0x1f)] = 1;
                S.mix_collide_summary = true;
            }
            lv &= 3;
            const uint16_t px = lv == 0 ? S.palette[spr & 0x1ff] : lv == 1 ? S.palette[0x200 | (fg & 0x1ff)] : S.palette[0x400 | (bg & 0x1ff)];
#if ROT270
            arc_fb_row(&S.fb, W_SRC - 1 - i)[col] = px;
#else
            out[i] = px;
#endif
        }
    }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&S.fb, y); }

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    if (!arc_fb_init(&S.fb, W, H) || !(S.spr_lo[0] = (uint8_t *)calloc(1, 0x8000)) || !(S.spr_lo[1] = (uint8_t *)calloc(1, 0x8000)))
        return false;
    arc_frame_row_hook = frame_row;
    // the scrambled 0000-7fff decrypted a KB at a time into the data and
    // the opcodes views (the rest as it is: Gardia's banks)
#if BANKED
#define MAIN_SIZE 0x18000
#else
#define MAIN_SIZE 0xc000
#endif
    const uint8_t *raw = ARC_ROM_BLOB("main.raw", kMain, MAIN_SIZE);
    uint8_t *data = (uint8_t *)malloc(0x400), *ops = (uint8_t *)malloc(0x400);
    if (!raw || !data || !ops) return false;
    arc_blob b;
    for (int view = 0; view < 2; view++) {
        const uint32_t size = view ? 0x8000 : MAIN_SIZE;
        if (arc_blob_begin(&b, view ? "opcodes" : "main", size, ARC_ROM_KEY(kMain, 1 + view)))
            for (uint32_t a = 0; a < size; a += 0x400) {
#if defined(CRYPT_TYPE2)
                sega_decrypt_type2_part(raw + a, 0x400, a, CRYPT_TYPE2, CRYPT_SHIFT, data, ops);
#else
                sega_decrypt_part(raw + a, 0x400, a, CRYPT, data, ops);
#endif
                arc_blob_write(&b, a, view ? ops : data, 0x400);
            }
        if (view) S.opcodes = arc_blob_end(&b);
        else if (!(S.rom = arc_blob_end(&b))) return false;
    }
    free(data);
    free(ops);
    arc_blob_free(raw);
    if (!S.opcodes || !(raw = ARC_ROM_BLOB("chars.raw", kChars, 0xc000))) return false;
    arc_layout cl = { 8, 8, 2048, 3, 64, false, false, false, {0, 0x4000 * 8, 0x8000 * 8}, kX8, kY8 };
    if (arc_blob_begin(&b, "chars", 2048 * 64, ARC_ROM_KEY(kChars, 1))) arc_blob_gfx(&b, &cl, raw, 0xc000, 0);
    arc_blob_free(raw);
    if (!(S.chars = arc_blob_end(&b)) || !(S.sprite_rom = ARC_ROM_BLOB("sprites", kSprites, SPRITE_SIZE)) ||
        !(S.sound_rom = ARC_ROM_BLOB("sound", kSound, 0x8000)) || !ARC_LOAD(kVideoProm, S.mix_lookup, 0x100))
        return false;
#if BANKED
    if (!ARC_LOAD(kProms, S.proms, sizeof S.proms)) return false;
#endif
    static const arc_resnet nets[3] = {{3, {995, 495, 250}, 0, 0}, {3, {995, 495, 250}, 0, 0}, {2, {495, 250}, 0, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    memcpy(S.rw, w[0], sizeof S.rw);
    memcpy(S.gw, w[1], sizeof S.gw);
    memcpy(S.bw, w[2], sizeof S.bw);
    S.main.read = read_data;
    S.main.opcode_read = read_opcode;
    S.main.write = write_byte;
    S.main.in = read_port;
    S.main.out = write_port;
    S.sound.read = snd_read;
    S.sound.write = snd_write;
    S.sound.in = in_ff;
    S.sound.out = out_nop;
    S.sound.cycles = sound_cycles;
    zd_init(&S.main);
    zd_init(&S.sound);
    sn76496_init(&S.psg0, 2000000, RATE, 0.5f);
    sn76496_init(&S.psg1, 4000000, RATE, 1.0f);
    z80piod_init(&S.pio);
    S.pio.port[Z80PIO_A].out_func = pio_a_out;
    S.pio.port[Z80PIO_A].ready_func = pio_ready_a;
    S.pio.port[Z80PIO_B].out_func = pio_b_out;
    S.ppi.out[0] = pio_a_out;
    S.ppi.out[1] = pio_b_out;
    S.ppi.out[2] = ppi_c_out;
    S.dsw_a = 0xff;
    S.dsw_b = DSW_B;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&S.main);
    zd_reset(&S.sound);
    sn76496_reset(&S.psg0);
    sn76496_reset(&S.psg1);
    z80piod_reset(&S.pio);
    i8255d_reset(&S.ppi);
    memset(S.bg_ram, 0, sizeof S.bg_ram);
    memset(S.pal_ram, 0, sizeof S.pal_ram);
    memset(S.mix_collide, 0, sizeof S.mix_collide);
    memset(S.sprite_collide, 0, sizeof S.sprite_collide);
    S.mix_collide_summary = S.sprite_collide_summary = false;
    S.in0 = S.in1 = S.in2 = 0xff;
    S.sound_latch = S.videomode = S.rom_bank = 0;
    S.bg_xscroll = 0;
    S.bg_yscroll = 0;
    S.audio_acc = 0;
    S.sound_irq_accum = 0;
    for (int i = 0; i < 0x600; i++) S.palette[i] = 0;
    for (int y = 0; y < H; y++) memset(arc_fb_row(&S.fb, y), 0, W * 2);
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / (SCANLINES * FPS)), sc = (int)((double)SOUND_CLOCK / (SCANLINES * FPS));
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == IRQ_LINE) {
            zd_set_irq(&S.main, ZD_HOLD, 0xff);
            S.bg_xscroll = (int)(uint16_t)(S.bg_ram[0xffc] | (S.bg_ram[0xffd] << 8)) / 2 + 14;
            S.bg_yscroll = S.bg_ram[0xfbd];
            render_frame();
        }
        if (++S.sound_irq_accum >= 64) {    // the sound IRQ every 64 lines
            S.sound_irq_accum = 0;
            zd_set_irq(&S.sound, ZD_HOLD, 0xff);
        }
        zd_run(&S.main, mc);
        zd_run(&S.sound, sc);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&S.fb); }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_B3) v &= ~0x01;
    if (p & AP_B1) v &= ~0x02;
    if (p & AP_B2) v &= ~0x04;
    if (p & AP_DOWN) v &= ~0x10;
    if (p & AP_UP) v &= ~0x20;
    if (p & AP_RIGHT) v &= ~0x40;
    if (p & AP_LEFT) v &= ~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    uint8_t v = 0xff;
    if (in->coin1) v &= ~0x01;
    if (in->coin2) v &= ~0x02;
    if (in->p[0] & AP_START) v &= ~0x10;
    if (in->p[1] & AP_START) v &= ~0x20;
    S.in0 = v;
    S.in1 = pad(in->p[0]);
    S.in2 = pad(in->p[1]);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    else if (bank == 1) S.dsw_b = v;
}
