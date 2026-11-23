// galaga.c - Galaga (Namco, 1981), Dig Dug (Namco, 1982), Xevious (Namco,
// 1982), Super Xevious (Namco, 1984) and Bosconian (Namco / Midway, 1981) for
// the common arcade frontends (arc.h): C port of dsp-cpp's
// drivers/arcade/galaga_hw (from dsp-emulator's galaga_hw.pas). Three Z80 at
// 3 MHz sharing the video and work RAM, a Namco 06xx bus with the 51xx (the
// software model of dsp-emulator), Dig Dug's 53xx and the 50xx of Xevious and
// Bosconian (MB88 MCUs running their ROMs), and the 3-voice Namco WSG. Like
// dsp-cpp, the 54xx's explosions are not played. ARC_VARIANT: 0 galaga,
// 1 digdug, 2 xevious, 3 sxevious, 4 bosco.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mb88xx/mb88xx.h"
#include "../../../rom/romfs.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define GALAGA (ARC_VARIANT == 0)
#define DIGDUG (ARC_VARIANT == 1)
#define XEVIOUS (ARC_VARIANT == 2 || ARC_VARIANT == 3)
#define SUPER (ARC_VARIANT == 3)
#define BOSCO (ARC_VARIANT == 4)
#define HAS50 (!GALAGA)                 // (Dig Dug's slot 2 is wired, its MCU without a ROM)

#define CLOCK 3072000
#define SCANLINES 264
#define FPS 60.606060606
#define RATE 96000                      // the WSG: the CPU clock / 32
#if BOSCO
#define W 285
#define H 224
#else
#define W 224
#define H 288
#endif
#define NMI_PERIOD 768                  // the 06xx's NMI while a chip is selected

#if DIGDUG
static const char *const kSets[] = { "digdug", "namco53", NULL };
static const arc_rom kMain[] = {
    {"dd1a.1", 0x1000, 0x0000, 0xa80ec984}, {"dd1a.2", 0x1000, 0x1000, 0x559f00bd},
    {"dd1a.3", 0x1000, 0x2000, 0x8cbc6fe1}, {"dd1a.4", 0x1000, 0x3000, 0xd066f830}};
static const arc_rom kSub[] = {{"dd1a.5", 0x1000, 0, 0x6687933b}, {"dd1a.6", 0x1000, 0x1000, 0x843d857f}};
static const arc_rom kSub2[] = {{"dd1.7", 0x1000, 0, 0xa41bce72}};
static const arc_rom kWave[] = {{"136007.110", 0x100, 0, 0x7a2815b4}};
static const arc_rom kChars[] = {{"dd1.9", 0x800, 0, 0xf14a6fe1}};
static const arc_rom kSprites[] = {
    {"dd1.15", 0x1000, 0x0000, 0xe22957c8}, {"dd1.14", 0x1000, 0x1000, 0x2829ec99},
    {"dd1.13", 0x1000, 0x2000, 0x458499e9}, {"dd1.12", 0x1000, 0x3000, 0xc58252a0}};
static const arc_rom kBgMap[] = {{"dd1.10b", 0x1000, 0, 0x2cf399c2}};
static const arc_rom kBgChars[] = {{"dd1.11", 0x1000, 0, 0x7b383983}};
static const arc_rom kProms[] = {
    {"136007.113", 0x20, 0, 0x4cb9da99}, {"136007.111", 0x100, 0x20, 0x00c7c419}, {"136007.112", 0x100, 0x120, 0xe9b3e08e}};
const arc_info arc_game = { "digdug", "Dig Dug", "Namco", 1982, W, H, FPS, RATE, kSets,
                            { "PUMP", NULL, NULL, NULL }, ARC_JOY_4WAY };
#elif XEVIOUS
#if SUPER
static const char *const kSets[] = { "sxevious", "xevious", "namco50", NULL };
static const arc_rom kMain[] = {
    {"cpu_3p.rom", 0x1000, 0x0000, 0x1c8d27d5}, {"cpu_3m.rom", 0x1000, 0x1000, 0xfd04e615},
    {"xv3_3.2m", 0x1000, 0x2000, 0x294d5404}, {"xv3_4.2l", 0x1000, 0x3000, 0x6a44bf92}};
static const arc_rom kSub[] = {{"xv3_5.3f", 0x1000, 0, 0xd4bd3d81}, {"xv3_6.3j", 0x1000, 0x1000, 0xaf06be5f}};
const arc_info arc_game = { "sxevious", "Super Xevious", "Namco", 1984, W, H, FPS, RATE, kSets,
                            { "ZAPPER", "BLASTER", NULL, NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "xevious", "namco50", NULL };
static const arc_rom kMain[] = {
    {"xvi_1.3p", 0x1000, 0x0000, 0x09964dda}, {"xvi_2.3m", 0x1000, 0x1000, 0x60ecce84},
    {"xvi_3.2m", 0x1000, 0x2000, 0x79754b7d}, {"xvi_4.2l", 0x1000, 0x3000, 0xc7d4bbf0}};
static const arc_rom kSub[] = {{"xvi_5.3f", 0x1000, 0, 0xc85b703f}, {"xvi_6.3j", 0x1000, 0x1000, 0xe18cdaad}};
const arc_info arc_game = { "xevious", "Xevious", "Namco", 1982, W, H, FPS, RATE, kSets,
                            { "ZAPPER", "BLASTER", NULL, NULL }, ARC_JOY_8WAY };
#endif
static const arc_rom kSub2[] = {{"xvi_7.2c", 0x1000, 0, 0xdd35cf1c}};
static const arc_rom kWave[] = {{"xvi-2.7n", 0x100, 0, 0x550f06bc}};
static const arc_rom kChars[] = {{"xvi_12.3b", 0x1000, 0, 0x088c8b26}};
static const arc_rom kSprites[] = {
    {"xvi_15.4m", 0x2000, 0x0000, 0xdc2c0ecb}, {"xvi_17.4p", 0x2000, 0x2000, 0xdfb587ce},
    {"xvi_16.4n", 0x1000, 0x4000, 0x605ca889}, {"xvi_18.4r", 0x2000, 0x5000, 0x02417d19}};
static const arc_rom kBgChars[] = {{"xvi_13.3c", 0x1000, 0, 0xde60ba25}, {"xvi_14.3d", 0x1000, 0x1000, 0x535cdbbc}};
static const arc_rom kBgMap[] = {
    {"xvi_9.2a", 0x1000, 0x0000, 0x57ed9879}, {"xvi_10.2b", 0x2000, 0x1000, 0xae3ba9e5}, {"xvi_11.2c", 0x1000, 0x3000, 0x31e244dd}};
static const arc_rom kProms[] = {
    {"xvi-8.6a", 0x100, 0x000, 0x5cc2727f}, {"xvi-9.6d", 0x100, 0x100, 0x5c8796cc}, {"xvi-10.6e", 0x100, 0x200, 0x3cb60975},
    {"xvi-7.4h", 0x200, 0x300, 0x22d98032}, {"xvi-6.4f", 0x200, 0x500, 0x3a7599f0}, {"xvi-4.3l", 0x200, 0x700, 0xfd8b9d91},
    {"xvi-5.3m", 0x200, 0x900, 0xbf906d82}};
#elif BOSCO
static const char *const kSets[] = { "bosco", "boscomd", "namco50", NULL };
static const arc_rom kMain[] = {
    {"bos5_1.3p", 0x1000, 0x0000, 0}, {"bos5_2.3m", 0x1000, 0x1000, 0}, {"bos5_3.2m", 0x1000, 0x2000, 0}, {"bos5_4.2l", 0x1000, 0x3000, 0}};
static const arc_rom kSub[] = {{"bos5_5.3f", 0x1000, 0, 0}, {"bos5_6.3j", 0x1000, 0x1000, 0}};
static const arc_rom kSub2[] = {{"bos1_7.2c", 0x1000, 0, 0}};
static const arc_rom kWave[] = {{"bos1-1.1d", 0x100, 0, 0xde2316c6}};
static const arc_rom kChars[] = {{"bos1_14.5d", 0x1000, 0, 0xa956d3c5}};
static const arc_rom kSprites[] = {{"bos1_13.5e", 0x1000, 0, 0xe869219c}};
static const arc_rom kProms[] = {
    {"bos1-6.6b", 0x20, 0, 0}, {"bos1-5.4m", 0x100, 0x20, 0}, {"bos1-4.2r", 0x100, 0x120, 0},
    {"bos1-3.2d", 0x20, 0x220, 0}, {"bos1-2.5c", 0x100, 0x240, 0}};
const arc_info arc_game = { "bosco", "Bosconian", "Namco / Midway", 1981, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "galaga", NULL };
static const arc_rom kMain[] = {
    {"gg1_1b.3p", 0x1000, 0x0000, 0xab036c9f}, {"gg1_2b.3m", 0x1000, 0x1000, 0xd9232240},
    {"gg1_3.2m", 0x1000, 0x2000, 0x753ce503}, {"gg1_4b.2l", 0x1000, 0x3000, 0x499fcc76}};
static const arc_rom kSub[] = {{"gg1_5b.3f", 0x1000, 0, 0xbb5caae3}};
static const arc_rom kSub2[] = {{"gg1_7b.2c", 0x1000, 0, 0xd016686b}};
static const arc_rom kWave[] = {{"prom-1.1d", 0x100, 0, 0x7a2815b4}};
static const arc_rom kChars[] = {{"gg1_9.4l", 0x1000, 0, 0x58b2f47c}};
static const arc_rom kSprites[] = {{"gg1_11.4d", 0x1000, 0, 0xad447c80}, {"gg1_10.4f", 0x1000, 0x1000, 0xdd6f1afc}};
static const arc_rom kProms[] = {
    {"prom-5.5n", 0x20, 0, 0x54603c6b}, {"prom-4.2n", 0x100, 0x20, 0x59b6edab}, {"prom-3.1c", 0x100, 0x120, 0x4a04bb6b}};

const arc_info arc_game = { "galaga", "Galaga", "Namco", 1981, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_2WAY };
#endif

// the star field: (x, y, colour, set) of the hardware's four sets of 63
typedef struct { uint16_t x, y; uint8_t col, set; } star_seed;
static const star_seed kStars[252] = {
{0x0085,0x0006,0x35,0},{0x008F,0x0008,0x30,0},{0x00E5,0x001B,0x07,0},{0x0022,0x001C,0x31,0},
{0x00E5,0x0025,0x1D,0},{0x0015,0x0026,0x29,0},{0x0080,0x002D,0x3B,0},{0x0097,0x002E,0x1C,0},
{0x00BA,0x003B,0x05,0},{0x0036,0x003D,0x36,0},{0x0057,0x0044,0x09,0},{0x00CF,0x0044,0x3D,0},
{0x0061,0x004E,0x27,0},{0x0087,0x0064,0x1A,0},{0x00D6,0x0064,0x17,0},{0x000B,0x006C,0x3C,0},
{0x0006,0x006D,0x24,0},{0x0018,0x006E,0x3A,0},{0x00A9,0x0079,0x23,0},{0x008A,0x007B,0x11,0},
{0x00D6,0x0080,0x0C,0},{0x0067,0x0082,0x3F,0},{0x0039,0x0083,0x38,0},{0x0072,0x0083,0x14,0},
{0x00EC,0x0084,0x16,0},{0x008E,0x0085,0x10,0},{0x0020,0x0088,0x25,0},{0x0095,0x008A,0x0F,0},
{0x000E,0x008D,0x00,0},{0x0006,0x0091,0x2E,0},{0x0007,0x0094,0x0D,0},{0x00AE,0x0097,0x0B,0},
{0x0000,0x0098,0x2D,0},{0x0086,0x009B,0x01,0},{0x0058,0x00A1,0x34,0},{0x00FE,0x00A1,0x3E,0},
{0x00A2,0x00A8,0x1F,0},{0x0041,0x00AA,0x0A,0},{0x003F,0x00AC,0x32,0},{0x00DE,0x00AC,0x03,0},
{0x00D4,0x00B9,0x26,0},{0x006D,0x00BB,0x1B,0},{0x0062,0x00BD,0x39,0},{0x00C9,0x00BE,0x18,0},
{0x006C,0x00C1,0x04,0},{0x0059,0x00C3,0x21,0},{0x0060,0x00CC,0x0E,0},{0x0091,0x00CC,0x12,0},
{0x003F,0x00CF,0x06,0},{0x00F7,0x00CF,0x22,0},{0x0044,0x00D0,0x33,0},{0x0034,0x00D2,0x08,0},
{0x00D3,0x00D9,0x20,0},{0x0071,0x00DD,0x37,0},{0x0073,0x00E1,0x2C,0},{0x00B9,0x00E3,0x2F,0},
{0x00A9,0x00E4,0x13,0},{0x00D3,0x00E7,0x19,0},{0x0037,0x00ED,0x02,0},{0x00BD,0x00F4,0x15,0},
{0x000F,0x00F6,0x28,0},{0x004F,0x00F7,0x2B,0},{0x00FB,0x00FF,0x2A,0},
{0x00FE,0x0004,0x3D,1},{0x00C4,0x0006,0x10,1},{0x001E,0x0007,0x2D,1},{0x0083,0x000B,0x1F,1},
{0x002E,0x000D,0x3C,1},{0x001F,0x000E,0x00,1},{0x00D8,0x000E,0x2C,1},{0x0003,0x000F,0x17,1},
{0x0095,0x0011,0x3F,1},{0x006A,0x0017,0x35,1},{0x00CC,0x0017,0x02,1},{0x0000,0x0018,0x32,1},
{0x0092,0x001D,0x36,1},{0x00E3,0x0021,0x04,1},{0x002F,0x002D,0x37,1},{0x00F0,0x002F,0x0C,1},
{0x009B,0x003E,0x06,1},{0x00A4,0x004C,0x07,1},{0x00EA,0x004D,0x13,1},{0x0084,0x004E,0x21,1},
{0x0033,0x0052,0x0F,1},{0x0070,0x0053,0x0E,1},{0x0006,0x0059,0x08,1},{0x0081,0x0060,0x28,1},
{0x0037,0x0061,0x29,1},{0x008F,0x0067,0x2F,1},{0x001B,0x006A,0x1D,1},{0x00BF,0x007C,0x12,1},
{0x0051,0x007F,0x31,1},{0x0061,0x0086,0x25,1},{0x006A,0x008F,0x0D,1},{0x006A,0x0091,0x19,1},
{0x0090,0x0092,0x05,1},{0x003B,0x0096,0x24,1},{0x008C,0x0097,0x0A,1},{0x0006,0x0099,0x03,1},
{0x0038,0x0099,0x38,1},{0x00A8,0x0099,0x18,1},{0x0076,0x00A6,0x20,1},{0x00AD,0x00A6,0x1C,1},
{0x00EC,0x00A6,0x1E,1},{0x0086,0x00AC,0x15,1},{0x0078,0x00AF,0x3E,1},{0x007B,0x00B3,0x09,1},
{0x0027,0x00B8,0x39,1},{0x0088,0x00C2,0x23,1},{0x0044,0x00C3,0x3A,1},{0x00CF,0x00C5,0x34,1},
{0x0035,0x00C9,0x30,1},{0x006E,0x00D1,0x3B,1},{0x00D6,0x00D7,0x16,1},{0x003A,0x00D9,0x2B,1},
{0x00AB,0x00E0,0x11,1},{0x00E0,0x00E2,0x1B,1},{0x006F,0x00E6,0x0B,1},{0x00B8,0x00E8,0x14,1},
{0x00D9,0x00E8,0x1A,1},{0x00F9,0x00E8,0x22,1},{0x0004,0x00F1,0x2E,1},{0x0049,0x00F8,0x26,1},
{0x0010,0x00F9,0x01,1},{0x0039,0x00FB,0x33,1},{0x0028,0x00FC,0x27,1},
{0x00FA,0x0006,0x19,2},{0x00E4,0x0007,0x2D,2},{0x0072,0x000A,0x03,2},{0x0084,0x001B,0x00,2},
{0x00BA,0x001D,0x29,2},{0x00E3,0x0022,0x04,2},{0x00D1,0x0026,0x2A,2},{0x0089,0x0032,0x30,2},
{0x005B,0x0036,0x27,2},{0x0084,0x003A,0x36,2},{0x0053,0x003F,0x0D,2},{0x0008,0x0040,0x1D,2},
{0x0055,0x0040,0x1A,2},{0x00AA,0x0041,0x31,2},{0x00FB,0x0041,0x2B,2},{0x00BC,0x0046,0x16,2},
{0x0093,0x0052,0x39,2},{0x00B9,0x0057,0x10,2},{0x0054,0x0059,0x28,2},{0x00E6,0x005A,0x01,2},
{0x00A7,0x005D,0x1B,2},{0x002D,0x005E,0x35,2},{0x0014,0x0062,0x21,2},{0x0069,0x006D,0x1F,2},
{0x00CE,0x006F,0x0B,2},{0x00DF,0x0075,0x2F,2},{0x00CB,0x0077,0x12,2},{0x004E,0x007C,0x23,2},
{0x004A,0x0084,0x0F,2},{0x0012,0x0086,0x25,2},{0x0068,0x008C,0x32,2},{0x0003,0x0095,0x20,2},
{0x000A,0x009C,0x17,2},{0x005B,0x00A3,0x08,2},{0x005F,0x00A4,0x3E,2},{0x0072,0x00A4,0x2E,2},
{0x00CC,0x00A6,0x06,2},{0x008A,0x00AB,0x0C,2},{0x00E0,0x00AD,0x26,2},{0x00F3,0x00AF,0x0A,2},
{0x0075,0x00B4,0x13,2},{0x0068,0x00B7,0x11,2},{0x006D,0x00C2,0x2C,2},{0x0076,0x00C3,0x14,2},
{0x00CF,0x00C4,0x1E,2},{0x0004,0x00C5,0x1C,2},{0x0013,0x00C6,0x3F,2},{0x00B9,0x00C7,0x3C,2},
{0x0005,0x00D7,0x34,2},{0x0095,0x00D7,0x3A,2},{0x00FC,0x00D8,0x02,2},{0x00E7,0x00DC,0x09,2},
{0x001D,0x00E1,0x05,2},{0x0005,0x00E6,0x33,2},{0x001C,0x00E9,0x3B,2},{0x00A2,0x00ED,0x37,2},
{0x0028,0x00EE,0x07,2},{0x00DD,0x00EF,0x18,2},{0x006D,0x00F0,0x38,2},{0x00A1,0x00F2,0x0E,2},
{0x0074,0x00F7,0x3D,2},{0x0069,0x00F9,0x22,2},{0x003F,0x00FF,0x24,2},
{0x0071,0x0010,0x34,3},{0x00AF,0x0011,0x23,3},{0x00A0,0x0014,0x26,3},{0x0002,0x0017,0x02,3},
{0x004B,0x0019,0x31,3},{0x0093,0x001C,0x0E,3},{0x001B,0x001E,0x25,3},{0x0032,0x0020,0x2E,3},
{0x00EE,0x0020,0x3A,3},{0x0079,0x0022,0x2F,3},{0x006C,0x0023,0x17,3},{0x00BC,0x0025,0x11,3},
{0x0041,0x0029,0x30,3},{0x001C,0x002E,0x32,3},{0x00B9,0x0031,0x01,3},{0x0083,0x0032,0x05,3},
{0x0095,0x003A,0x12,3},{0x000D,0x003F,0x07,3},{0x0020,0x0041,0x33,3},{0x0092,0x0045,0x2C,3},
{0x00D4,0x0047,0x08,3},{0x00A1,0x004B,0x2D,3},{0x00D2,0x004B,0x3B,3},{0x00D6,0x0052,0x24,3},
{0x009A,0x005F,0x1C,3},{0x0016,0x0060,0x3D,3},{0x001A,0x0063,0x1F,3},{0x00CD,0x0066,0x28,3},
{0x00FF,0x0067,0x10,3},{0x0035,0x0069,0x20,3},{0x008F,0x006C,0x04,3},{0x00CA,0x006C,0x2A,3},
{0x005A,0x0074,0x09,3},{0x0060,0x0078,0x38,3},{0x0072,0x0079,0x1E,3},{0x0037,0x007F,0x29,3},
{0x0012,0x0080,0x14,3},{0x0029,0x0082,0x2B,3},{0x0084,0x0098,0x36,3},{0x0032,0x0099,0x37,3},
{0x00BB,0x00A0,0x19,3},{0x003E,0x00A3,0x3E,3},{0x004A,0x00A6,0x1A,3},{0x0029,0x00A7,0x21,3},
{0x009D,0x00B7,0x22,3},{0x006C,0x00B9,0x15,3},{0x000C,0x00C0,0x0A,3},{0x00C2,0x00C3,0x0F,3},
{0x002F,0x00C9,0x0D,3},{0x00D2,0x00CE,0x16,3},{0x00F3,0x00CE,0x0B,3},{0x0075,0x00CF,0x27,3},
{0x001A,0x00D5,0x35,3},{0x0026,0x00D6,0x39,3},{0x0080,0x00DA,0x3C,3},{0x00A9,0x00DD,0x00,3},
{0x00BC,0x00EB,0x03,3},{0x0032,0x00EF,0x1B,3},{0x0067,0x00F0,0x3F,3},{0x00EF,0x00F1,0x18,3},
{0x00A8,0x00F3,0x0C,3},{0x00DE,0x00F9,0x1D,3},{0x002C,0x00FA,0x13,3},
};


static const int kCharX[8] = {64, 65, 66, 67, 0, 1, 2, 3};
static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kX8r[8] = {7, 6, 5, 4, 3, 2, 1, 0};
static const int kCharY[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kSprX[16] = {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195};
static const int kBoscoSprX[16] = {64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195, 0, 1, 2, 3};
static const int kSprY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312};

// an MB88 MCU on the 06xx: the 50xx (protection / scores) or the 53xx (I/O)
typedef struct {
    mb88 cpu;
    uint8_t cmd, port_o;
    bool rw;
    int prot_step, prot_idx;
} n50_t;

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sub, sub2;
    uint8_t mem[0x10000], sub_rom[0x4000], sub2_rom[0x4000], wave[0x100];
    uint8_t *chars, *sprites, *tiles;   // (Galaga and Dig Dug: turned a quarter clockwise)
    uint16_t palette[256];
    uint8_t char_lut[0x100], tile_lut[0x200], sprite_lut[0x200];
    uint8_t star_control[6];
    uint16_t *fb;
    bool main_irq, sub_irq, sub2_nmi, sub_held, sub2_held, flip;
    // 06xx (and Bosconian's second one, whose NMI goes nowhere)
    uint8_t ctrl, ctrl2;
    bool sel[4], sel2;
    bool nmi_enabled;
    int nmi_acc;
#if HAS50
    n50_t n50, n50b;
    uint8_t n50_rom[0x800];
#endif
#if DIGDUG
    mb88 n53;
    uint8_t n53_rom[0x400], n53_o, bg_map[0x1000], bg_select, bg_color_bank, tx_color_mode;
    bool bg_disable;
    uint8_t dd_char_lut[0x100], dd_bg_lut[0x100];
#endif
#if XEVIOUS
    uint8_t xtiles[0x4000], bs[2];
#endif
    int scrollx_bg, scrolly_bg, scrollx_fg, scrolly_fg;
    // 51xx (software model)
    uint8_t mode, coincred_mode, credits, in_count, lastcoins, lastbuttons, coins[2], coins_per_cred[2], creds_per_coin[2];
    bool remap_joy, kludge;
    // WSG
    uint8_t wsg[0x20];
    uint8_t in0, in1, in2, dsw_a, dsw_b;
    int64_t wsg_acc;
    int16_t audio[2048];
    int naudio;
} *G_;
#define G (*G_)

static void ns51_reset(bool kludge)
{
    G.kludge = kludge;
    G.credits = 0;
    G.coins[0] = G.coins[1] = 0;
    G.coins_per_cred[0] = G.coins_per_cred[1] = 1;
    G.creds_per_coin[0] = G.creds_per_coin[1] = 1;
    G.in_count = G.mode = G.coincred_mode = G.lastcoins = 0;
    G.lastbuttons = 0xff;
    G.remap_joy = false;
}

static void ns51_write(uint8_t d)
{
    d &= 7;
    if (G.coincred_mode) {
        switch (G.coincred_mode) {
        case 4: G.coins_per_cred[0] = d; break;
        case 3: G.creds_per_coin[0] = d; break;
        case 2: G.coins_per_cred[1] = d; break;
        case 1: G.creds_per_coin[1] = d; break;
        }
        G.coincred_mode--;
        return;
    }
    switch (d) {
    case 1:
        if (G.kludge) {
            G.coincred_mode = 6;
            G.remap_joy = true;
        } else {
            G.coincred_mode = 4;
            G.credits = 0;
        }
        break;
    case 2: G.mode = 1; G.in_count = 0; break;
    case 3: G.remap_joy = false; break;
    case 4: G.remap_joy = true; break;
    case 5: G.mode = 0; G.in_count = 0; break;
    default: break;
    }
}

static void add_credits(int i)
{
    if (++G.coins[i] >= G.coins_per_cred[i]) {
        G.credits = (uint8_t)(G.credits + G.creds_per_coin[i]);
        G.coins[i] = 0;
        if (G.credits > 99) G.credits = 99;
    }
}

static uint8_t ns51_read(void)
{
    static const uint8_t kJoyMap[16] = {0xf, 0xe, 0xd, 0x5, 0xc, 0x9, 0x7, 0x6, 0xb, 0x3, 0xa, 0x4, 0x1, 0x2, 0x0, 0x8};
    const uint8_t in0 = G.in0, in1 = G.in1;
    uint8_t res = 0;
    if (G.mode == 0) {                  // switch mode
        res = (G.in_count & 3) == 0 ? in0 : (G.in_count & 3) == 1 ? in1 : 0;
        G.in_count = (G.in_count + 1) & 3;
        return res;
    }
    switch (G.in_count % 3) {           // credits mode
    case 0: {
        const uint8_t inv = (uint8_t)~in0, toggle = inv ^ G.lastcoins;
        G.lastcoins = inv;
        if (G.coins_per_cred[0] > 0 && G.credits < 99) {
            if (toggle & inv & 0x10) add_credits(0);
            if (toggle & inv & 0x20) add_credits(1);
            if ((toggle & inv & 0x04) && G.credits >= 1) { G.credits--; G.mode = 2; }
            if ((toggle & inv & 0x08) && G.credits >= 2) { G.credits -= 2; G.mode = 2; }
        }
        if ((~in0) & 0x80) return 0xbb; // the test switch
        res = (uint8_t)(((G.credits / 10) << 4) | (G.credits % 10));
        break;
    }
    case 1: {
        uint8_t joy = in1 & 0x0f;
        const uint8_t inv = (uint8_t)~(in0 & 0x0f), toggle = inv ^ G.lastbuttons;
        G.lastbuttons = (G.lastbuttons & 2) | (inv & 1);
        if (G.remap_joy) joy = kJoyMap[joy];
        joy |= (uint8_t)((((toggle & inv & 0x01) ^ 1) & 1) << 4);
        joy |= (uint8_t)((((inv & 0x01) ^ 1) & 1) << 5);
        res = joy;
        break;
    }
    case 2: {
        uint8_t joy = in1 >> 4;
        const uint8_t inv = (uint8_t)~(in0 & 0x0f), toggle = inv ^ G.lastbuttons;
        G.lastbuttons = (G.lastbuttons & 1) | (inv & 2);
        if (G.remap_joy) joy = kJoyMap[joy & 0x0f];
        joy |= (uint8_t)((((toggle & inv & 0x02) ^ 2) & 2) << 3);
        joy |= (uint8_t)((((inv & 0x02) ^ 2) & 2) << 4);
        res = joy;
        break;
    }
    }
    G.in_count = (G.in_count + 1) % 3;
    return res;
}


#if HAS50
// ---------------------------------------------------------------------------
// The 50xx: K = the command's high nibble, R0 its low one, R2 = R/W; O the
// answer, a nibble at a time. dsp-cpp also feeds Xevious the answers of its
// start-up protection check.

static uint8_t n50_k(void *u) { return (uint8_t)(((n50_t *)u)->cmd >> 4); }
static uint8_t n50_r(void *u, int p)
{
    n50_t *n = u;
    return p == 0 ? (uint8_t)(n->cmd & 15) : p == 2 ? (n->rw ? 1 : 0) : 0;
}
static void n50_o(void *u, uint8_t d)
{
    n50_t *n = u;
    const uint8_t nib = d & 0x0f;
    n->port_o = (d & 0x10) ? (uint8_t)((n->port_o & 0x0f) | (nib << 4)) : (uint8_t)((n->port_o & 0xf0) | nib);
}

static void n50_init(n50_t *n, const uint8_t *rom)
{
    mb88_init(&n->cpu, MB8842, rom);
    n->cpu.read_k = n50_k;
    n->cpu.read_r = n50_r;
    n->cpu.write_o = n50_o;
    n->cpu.user = n;
    mb88_set_reset_line(&n->cpu, true);
}

static void n50_reset(n50_t *n)
{
    n->cmd = n->port_o = 0;
    n->rw = true;
    n->prot_step = n->prot_idx = 0;
    mb88_reset(&n->cpu);
}

static void n50_pulse(n50_t *n)
{
    mb88_set_irq(&n->cpu, true);
    mb88_run(&n->cpu, 48);
    mb88_set_irq(&n->cpu, false);
    mb88_run(&n->cpu, 64);
}

static void n50_write(n50_t *n, uint8_t d)
{
    n->cmd = d;
    n->rw = false;
    if (d == 0x10) {
        n->prot_step = 1;
        n->prot_idx = 0;
    } else if (d == 0x80 && n->prot_step == 1) {
        n->prot_step = 2;
        n->prot_idx = 0;
    } else if (d == 0xe5) {
        n->prot_step = 3;
        n->prot_idx = 0;
    }
    n50_pulse(n);
}

static uint8_t n50_read(n50_t *n)
{
    uint8_t r = n->port_o;
    if (n->prot_step == 2 || n->prot_step == 3) {
        r = (n->prot_idx & 3) == 3 ? (n->prot_step == 2 ? 0x05 : 0x95) : 0x00;
        if (++n->prot_idx >= 4) n->prot_step = 0;
    }
    n->rw = true;
    n50_pulse(n);
    return r;
}
#endif

#if DIGDUG
static uint8_t n53_zero(void *u) { (void)u; return 0; }
static uint8_t n53_r(void *u, int p) { (void)u, (void)p; return 0; }
static void n53_o(void *u, uint8_t d) { (void)u; G.n53_o = d; }
#endif

// ---------------------------------------------------------------------------
// The 06xx: slot 0 the 51xx, 1 Dig Dug's 53xx, 2 the 50xx (3, the 54xx, has
// no effect here)

static void ctrl_write(uint8_t v)
{
    G.ctrl = v;
    const uint8_t mask = v & 0x0f;
    const bool rd = (v & 0x10) != 0;
    for (int i = 0; i < 4; i++) {
        const bool s = (mask >> i) & 1;
        if (s != G.sel[i]) {
            G.sel[i] = s;
#if DIGDUG
            if (i == 1) mb88_set_irq(&G.n53, s);
#endif
        }
#if HAS50
        if (s && i == 2) G.n50.rw = rd;
#endif
    }
    G.nmi_enabled = mask != 0;
    G.nmi_acc = 0;
    (void)rd;
}

static uint8_t data_read(void)
{
    if (!(G.ctrl & 0x10)) return 0;
    uint8_t r = 0xff;
    if (G.ctrl & 1) r &= ns51_read();
#if DIGDUG
    if (G.ctrl & 2) r &= G.n53_o;
#endif
#if HAS50
    if (G.ctrl & 4) r &= n50_read(&G.n50);
#endif
    return r;
}

static void data_write(uint8_t v)
{
    if (G.ctrl & 0x10) return;
    if (G.ctrl & 1) ns51_write(v);
#if HAS50
    if (G.ctrl & 4) n50_write(&G.n50, v);
#endif
}

#if BOSCO
// the second 06xx: slot 0 the second 50xx
static void ctrl2_write(uint8_t v)
{
    G.ctrl2 = v;
    if (v & 1) G.n50b.rw = (v & 0x10) != 0;
}
static uint8_t data2_read(void) { return !(G.ctrl2 & 0x10) ? 0 : (G.ctrl2 & 1) ? n50_read(&G.n50b) : 0xff; }
static void data2_write(uint8_t v)
{
    if (!(G.ctrl2 & 0x10) && (G.ctrl2 & 1)) n50_write(&G.n50b, v);
}
#endif

static int16_t wsg_update(void)
{
    int32_t mix = 0;
    for (int v = 0; v < 3; v++) {
        const int cb = v == 0 ? 0x00 : v == 1 ? 0x06 : 0x0b, n = v == 0 ? 5 : 4;
        const int fb = v == 0 ? 0x10 : v == 1 ? 0x16 : 0x1b;
        const int wr = v == 0 ? 0x05 : v == 1 ? 0x0a : 0x0f, vr = v == 0 ? 0x15 : v == 1 ? 0x1a : 0x1f;
        const int bias = v == 0 ? 0 : 4;  // voices 1 and 2: an implicit zero low nibble
        uint32_t counter = 0, freq = 0;
        for (int i = 0; i < n; i++) counter |= (uint32_t)G.wsg[cb + i] << (4 * i + bias);
        for (int i = 0; i < n; i++) freq |= (uint32_t)G.wsg[fb + i] << (4 * i + bias);
        counter = (counter + freq) & 0xfffffu;
        for (int i = 0; i < n; i++) G.wsg[cb + i] = (counter >> (4 * i + bias)) & 0xf;
        const uint8_t nib = G.wave[(G.wsg[wr] & 7) * 32 + ((counter >> 15) & 0x1f)] & 0xf;
        mix += ((int32_t)nib - 8) * (G.wsg[vr] & 0xf) * 64;
    }
    return arc_clamp16(mix);
}


static void latch_write(int bit, uint8_t v)
{
    const bool set = v & 1;
    switch (bit) {
    case 0:
        G.main_irq = set;
        if (!set) zd_set_irq(&G.main, ZD_CLEAR, 0xff);
        break;
    case 1:
        G.sub_irq = set;
        if (!set) zd_set_irq(&G.sub, ZD_CLEAR, 0xff);
        break;
    case 2: G.sub2_nmi = !set; break;
    case 3:
        if (set && G.sub_held) zd_reset(&G.sub);
        if (set && G.sub2_held) zd_reset(&G.sub2);
        G.sub_held = G.sub2_held = !set;
        break;
    default: break;
    }
}

#if XEVIOUS
// the Xevious BB custom: the background map's lookup
static uint8_t xevious_bb(uint16_t a)
{
    const uint16_t adr_2b = (uint16_t)(((G.bs[1] & 0x7e) << 6) | ((G.bs[0] & 0xfe) >> 1));
    uint16_t dat1;
    if (adr_2b & 1) dat1 = (uint16_t)(((G.xtiles[adr_2b >> 1] & 0xf0) << 4) | G.xtiles[0x1000 + adr_2b]);
    else dat1 = (uint16_t)(((G.xtiles[adr_2b >> 1] & 0x0f) << 8) | G.xtiles[0x1000 + adr_2b]);
    uint16_t adr_2c = (uint16_t)(((dat1 & 0x1ff) << 2) | ((G.bs[1] & 1) << 1) | (G.bs[0] & 1));
    if (dat1 & 0x400) adr_2c ^= 1;
    if (dat1 & 0x200) adr_2c ^= 2;
    if (a & 1) return G.xtiles[0x3000 + (adr_2c | 0x800)];
    uint8_t dat2 = G.xtiles[0x3000 + adr_2c];
    dat2 = (uint8_t)(((dat2 & 0x40) << 1) | ((dat2 & 0x80) >> 1) | (dat2 & 0x3f));
    if (dat1 & 0x400) dat2 ^= 0x40;
    if (dat1 & 0x200) dat2 ^= 0x80;
    return dat2;
}
#endif

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff) return G.mem[a];
    if (a >= 0x6800 && a <= 0x6807) {
        const int b = a & 7;
#if XEVIOUS
        return (uint8_t)((((G.dsw_b | G.in2) >> b) & 1) | (((G.dsw_a >> b) & 1) << 1));
#else
        return (uint8_t)(((G.dsw_b >> b) & 1) | (((G.dsw_a >> b) & 1) << 1));
#endif
    }
    if (a >= 0x7000 && a <= 0x70ff) return data_read();
    if (a == 0x7100) return G.ctrl;
#if DIGDUG
    if ((a >= 0x8000 && a <= 0x8bff) || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff) || (a >= 0xb800 && a <= 0xb83f))
        return G.mem[a];
#elif XEVIOUS
    if ((a >= 0x7800 && a <= 0x87ff) || (a >= 0x9000 && a <= 0x97ff) || (a >= 0xa000 && a <= 0xa7ff) || (a >= 0xb000 && a <= 0xcfff))
        return G.mem[a];
    if (a >= 0xf000) return xevious_bb(a);
#elif BOSCO
    if (a >= 0x7800 && a <= 0x8fff) return G.mem[a];
    if (a >= 0x9000 && a <= 0x90ff) return data2_read();
    if (a == 0x9100) return G.ctrl2;
#else
    if ((a >= 0x8000 && a <= 0x8bff) || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff)) return G.mem[a];
#endif
    return 0xff;
}

static uint8_t sub_read(void *u, uint16_t a) { return a <= 0x3fff ? G.sub_rom[a] : main_read(u, a); }
static uint8_t sub2_read(void *u, uint16_t a) { return a <= 0x3fff ? G.sub2_rom[a] : main_read(u, a); }

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
    if (a >= 0x6800 && a <= 0x681f) { G.wsg[a & 0x1f] = v & 0x0f; return; }
    if (a >= 0x6820 && a <= 0x6827) { latch_write(a & 7, v); return; }
    if (a >= 0x7000 && a <= 0x70ff) { data_write(v); return; }
    if (a == 0x7100) { ctrl_write(v); return; }
#if DIGDUG
    if ((a >= 0x8000 && a <= 0x8bff) || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff) || (a >= 0xb800 && a <= 0xb840))
        G.mem[a] = v;
    else if (a >= 0xa000 && a <= 0xa007) {
        const int b = a & 7;
        switch (b) {
        case 0: case 1: G.bg_select = (uint8_t)((G.bg_select & ~(1 << b)) | ((v & 1) << b)); break;
        case 2: G.tx_color_mode = v & 1; break;
        case 3: G.bg_disable = (v & 1) != 0; break;
        case 4: case 5: G.bg_color_bank = (uint8_t)((G.bg_color_bank & ~(1 << b)) | ((v & 1) << b)); break;
        case 7: G.flip = v & 1; break;
        default: break;
        }
    }
#elif XEVIOUS
    if ((a >= 0x7800 && a <= 0x87ff) || (a >= 0x9000 && a <= 0x97ff) || (a >= 0xa000 && a <= 0xa7ff) || (a >= 0xb000 && a <= 0xcfff))
        G.mem[a] = v;
    else if (a >= 0xd000 && a <= 0xd07f) {
        const int s = v + ((a & 1) << 8);
        switch ((a & 0xf0) >> 4) {
        case 0: G.scrollx_bg = s; break;
        case 1: G.scrollx_fg = s; break;
        case 2: G.scrolly_bg = s; break;
        case 3: G.scrolly_fg = s; break;
        case 7: G.flip = s & 1; break;
        default: break;
        }
    } else if (a >= 0xf000) G.bs[a & 1] = v;
#elif BOSCO
    if (a >= 0x7800 && a <= 0x8fff) G.mem[a] = v;
    else if (a >= 0x9000 && a <= 0x90ff) data2_write(v);
    else if (a == 0x9100) ctrl2_write(v);
    else if (a >= 0xa000 && a <= 0xa005) G.star_control[a & 7] = v;
    else if (a == 0xa006 || a == 0xffc0) G.scrollx_bg = v;
    else if (a == 0xffc1) G.scrolly_bg = v;
#else
    if ((a >= 0x8000 && a <= 0x8bff) || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff)) G.mem[a] = v;
    else if (a >= 0xa000 && a <= 0xa005) G.star_control[a & 7] = v;
    else if (a == 0xa007) G.flip = v & 1;
#endif
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void main_cycles(void *u, int c)
{
    (void)u;
    if (G.nmi_enabled) {
        G.nmi_acc += c;
        while (G.nmi_acc >= NMI_PERIOD) {
            G.nmi_acc -= NMI_PERIOD;
            zd_set_nmi(&G.main, ZD_PULSE);
        }
    }
    G.wsg_acc += (int64_t)c * RATE;
    while (G.wsg_acc >= CLOCK) {
        G.wsg_acc -= CLOCK;
        const int16_t s = wsg_update();
        if (G.naudio < 2048) G.audio[G.naudio++] = s;
    }
}

// ---------------------------------------------------------------------------
// Video

static inline void plot(int x, int y, uint16_t c)
{
    if ((unsigned)x < W && (unsigned)y < H) G.fb[y * W + x] = c;
}

#if GALAGA
static void render(void)
{
    const uint16_t black = arc_rgb565(0);
    for (int i = 0; i < W * H; i++) G.fb[i] = black;
    if (G.star_control[5] & 1) {
        const int set_a = G.star_control[3] & 1, set_b = (G.star_control[4] & 1) | 2;
        for (int i = 0; i < 252; i++) {
            const star_seed *s = &kStars[i];
            if (s->set != set_a && s->set != set_b) continue;
            const int y = s->y % 256 + 16, x = (112 + s->x) % 256;
            if (x < W && y < H) G.fb[y * W + x] = G.palette[32 + s->col];
        }
    }
    for (int f = 0; f < 0x40; f++) {
        const uint8_t code = G.mem[0x8b80 + f * 2] & 0x7f;
        const int color = (G.mem[0x8b81 + f * 2] & 0x3f) << 2;
        const int y = G.mem[0x9381 + f * 2] - 40 + 0x100 * (G.mem[0x9b81 + f * 2] & 3);
        const int x = G.mem[0x9380 + f * 2] - 16 - 1;
        const uint8_t attr = G.mem[0x9b80 + f * 2];
        const bool fx = attr & 2, fy = attr & 1;
        const uint8_t *px = G.sprites + code * 256;
        for (int sy = 0; sy < 16; sy++) {
            const int py = y + sy;
            if (py < 0 || py >= H) continue;
            const uint8_t *row = px + (fy ? 15 - sy : sy) * 16;
            for (int sx = 0; sx < 16; sx++) {
                const uint8_t p = row[fx ? 15 - sx : sx];
                const int qx = x + sx;
                if (p && qx >= 0 && qx < W) G.fb[py * W + qx] = G.palette[G.tile_lut[(color + p) & 0xff] & 0x5f];
            }
        }
    }
    for (int x = 0; x <= 27; x++)
        for (int y = 0; y <= 35; y++) {
            const int sx = 29 - x, sy = y - 2;
            const int pos = (sy & 0x20) ? sx + ((sy & 0x1f) << 5) : sy + (sx << 5);
            if (pos < 0 || pos >= 0x400) continue;
            const int color = (G.mem[0x8400 + pos] & 0x3f) << 2;
            const uint8_t *px = G.chars + G.mem[0x8000 + pos] * 64;
            for (int py = 0; py < 8; py++)
                for (int qx = 0; qx < 8; qx++) {
                    const uint8_t p = px[py * 8 + qx];
                    if (p) G.fb[(y * 8 + py) * W + x * 8 + qx] = G.palette[G.char_lut[(color + p) & 0xff] & 0x5f];
                }
        }
}


#elif DIGDUG
static void render(void)
{
    for (int i = 0; i < W * H; i++) G.fb[i] = 0;
    if (!G.bg_disable)                  // the playfield from the map ROM
        for (int x = 0; x <= 27; x++)
            for (int y = 0; y <= 35; y++) {
                const int sx = 29 - x, sy = y - 2;
                const int pos = (sy & 0x20) ? sx + ((sy & 0x1f) << 5) : sy + (sx << 5);
                const uint8_t code = G.bg_map[(pos | (G.bg_select << 10)) & 0xfff];
                const int color = ((code >> 4) | (G.bg_color_bank << 4)) << 2;
                const uint8_t *px = G.tiles + code * 64;
                for (int py = 0; py < 8; py++)
                    for (int qx = 0; qx < 8; qx++) {
                        const uint8_t pen = px[py * 8 + qx];
                        if (pen) plot(x * 8 + qx, y * 8 + py, G.palette[G.dd_bg_lut[(color + pen) & 0xff] & 0x1f]);
                    }
            }
    for (int x = 0; x <= 27; x++)       // the characters, 1bpp
        for (int y = 0; y <= 35; y++) {
            const int sx = 29 - x, sy = y - 2;
            const int pos = (sy & 0x20) ? sx + ((sy & 0x1f) << 5) : sy + (sx << 5);
            if (pos < 0 || pos >= 0x400) continue;
            const uint8_t code = G.mem[0x8000 + pos];
            const int color = (((code >> 4) & 0x0e) | ((code >> 3) & 2)) << 1;
            const uint8_t *px = G.chars + (code & 0x7f) * 64;
            for (int py = 0; py < 8; py++)
                for (int qx = 0; qx < 8; qx++) {
                    const uint8_t pen = px[py * 8 + qx];
                    if (!pen) continue;
                    const uint8_t idx = G.dd_char_lut[(color + pen) & 0xff];
                    if (idx) plot(x * 8 + qx, y * 8 + py, G.palette[idx & 0x1f]);
                }
        }
    for (int f = 0; f < 0x40; f++) {    // the sprites, 16x16 or 32x32
        const uint8_t code = G.mem[0x8b80 + f * 2];
        const int color = (G.mem[0x8b81 + f * 2] & 0x3f) << 2;
        int y = G.mem[0x9381 + f * 2] - 40 + 1;
        if (y <= 0) y += 256;
        const int x = G.mem[0x9380 + f * 2] - 16 - 1;
        const uint8_t attr = G.mem[0x9b80 + f * 2];
        const bool fx = attr & 2, fy = attr & 1;
        int codes[4], xs[4], ys[4], n = 1;
        if (!(code & 0x80)) {
            codes[0] = code, xs[0] = x, ys[0] = y;
        } else {
            const int k = (int)fy ^ ((int)fx << 1), base = (code & 0xc0) | ((code & 0x3f) << 2);
            codes[0] = base + (0 ^ k), xs[0] = x + 16, ys[0] = y;
            codes[1] = base + (1 ^ k), xs[1] = x + 16, ys[1] = y + 16;
            codes[2] = base + (2 ^ k), xs[2] = x, ys[2] = y;
            codes[3] = base + (3 ^ k), xs[3] = x, ys[3] = y + 16;
            n = 4;
        }
        for (int t = 0; t < n; t++) {
            const uint8_t *px = G.sprites + (codes[t] & 0xff) * 256;
            for (int sy = 0; sy < 16; sy++) {
                const int srcy = fy ? 15 - sy : sy;
                for (int sx = 0; sx < 16; sx++) {
                    const uint8_t pen = px[srcy * 16 + (fx ? 15 - sx : sx)];
                    if (pen == 0 || pen >= 0x1f) continue;
                    plot(xs[t] + sx, ys[t] + sy, G.palette[G.tile_lut[(color + pen) & 0xff] & 0x1f]);
                }
            }
        }
    }
}
#elif XEVIOUS
// the 512x512 layers: tile f at (x, y) = (31 - f / 64, f % 64), so only
// x < 256 holds anything; -1 where nothing is drawn
static int bg_px(int lx, int ly)
{
    if (lx >= 256) return -1;
    const int f = (31 - (lx >> 3)) * 64 + (ly >> 3);
    const uint8_t lo = G.mem[0xc800 + f], attr = G.mem[0xb800 + f];
    const int code = lo + ((attr & 1) << 8);
    const int color = (((attr & 0x3c) >> 2) | ((lo & 0x80) >> 3) | ((attr & 3) << 5)) << 2;
    const int py = (attr & 0x80) ? 7 - (ly & 7) : ly & 7, qx = (attr & 0x40) ? 7 - (lx & 7) : lx & 7;
    const uint8_t pen = G.tiles[(code % 0x200) * 64 + py * 8 + qx];
    return pen ? G.palette[G.tile_lut[(color + pen) & 0x1ff]] : -1;
}

static int fg_px(int lx, int ly)
{
    if (lx >= 256) return -1;
    const int f = (31 - (lx >> 3)) * 64 + (ly >> 3);
    const uint8_t attr = G.mem[0xb000 + f], code = G.mem[0xc000 + f];
    const int color = (((attr & 0x03) << 4) | ((attr & 0x3c) >> 2)) << 1;
    const int py = (attr & 0x80) ? 7 - (ly & 7) : ly & 7, qx = (attr & 0x40) ? 7 - (lx & 7) : lx & 7;
    const uint8_t pen = G.chars[(code % 0x200) * 64 + py * 8 + qx];
    if (!pen) return -1;
    const uint8_t lut = G.char_lut[(color + pen) & 0xff];
    return lut == 0x80 ? -1 : G.palette[lut];
}

static void blit_xsprite(int code, int ox, int oy, int color, bool fx, bool fy)
{
    const uint8_t *px = G.sprites + (code % 0x140) * 256;
    for (int sy = 0; sy < 16; sy++)
        for (int sx = 0; sx < 16; sx++) {
            const uint8_t pen = px[(fy ? 15 - sy : sy) * 16 + (fx ? 15 - sx : sx)];
            if (!pen) continue;
            const uint8_t idx = G.sprite_lut[(color + pen) & 0x1ff];
            if (idx != 0x80) plot(ox + sx, oy + sy, G.palette[idx]);
        }
}

static void render(void)
{
    const int bsx = G.scrolly_bg + 20, bsy = G.scrollx_bg + 20;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int c = bg_px((x + bsx) & 511, (y + bsy) & 511);
            G.fb[y * W + x] = c < 0 ? 0 : (uint16_t)c;
        }
    for (int f = 0; f < 0x40; f++) {
        if (G.mem[0xa781 + f * 2] & 0x40) continue;
        int code = G.mem[0xa780 + f * 2];
        const uint8_t a2 = G.mem[0x9780 + f * 2];
        if (a2 & 0x80) code = (code & 0x3f) + 0x100;
        const int color = (G.mem[0xa781 + f * 2] & 0x7f) << 3;
        const bool fy = (a2 & 4) != 0, fx = (a2 & 8) != 0;
        const int y = G.mem[0x8781 + f * 2] - 39 + 0x100 * (G.mem[0x9781 + f * 2] & 1);
        const int x = G.mem[0x8780 + f * 2] - 19;
        switch (a2 & 3) {
        case 0: blit_xsprite(code, x, y, color, fx, fy); break;
        case 1: {
            const int c = code & 0x1fe;
            blit_xsprite(c, x + (fx ? 16 : 0), y, color, fx, fy);
            blit_xsprite(c + 1, x + (fx ? 0 : 16), y, color, fx, fy);
            break;
        }
        case 2: {
            const int c = code & 0x1fd;
            blit_xsprite(c + 2, x, y + (fy ? 16 : 0), color, fx, fy);
            blit_xsprite(c, x, y + (fy ? 0 : 16), color, fx, fy);
            break;
        }
        default: {
            const int c = code & 0x1fc, sx1 = fx ? 16 : 0, sx2 = fx ? 0 : 16, sy1 = fy ? 16 : 0, sy2 = fy ? 0 : 16;
            blit_xsprite(c + 3, x + sx1, y + sy2, color, fx, fy);
            blit_xsprite(c + 1, x + sx2, y + sy2, color, fx, fy);
            blit_xsprite(c + 2, x + sx1, y + sy1, color, fx, fy);
            blit_xsprite(c, x + sx2, y + sy1, color, fx, fy);
            break;
        }
        }
    }
    const int fsx = G.scrolly_fg + 18, fsy = G.scrollx_fg + 32;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int c = fg_px((x + fsx) & 511, (y + fsy) & 511);
            if (c >= 0) G.fb[y * W + x] = (uint16_t)c;
        }
}
#elif BOSCO
// the layers' (x, y) is the picture's (x, y - 16)
static inline void lplot(int x, int y, uint16_t c) { plot(x, y - 16, c); }

static int pf_px(int lx, int ly)
{
    const int f = (ly >> 3) * 32 + (lx >> 3);
    const uint8_t code = G.mem[0x8400 + f], attr = G.mem[0x8c00 + f];
    const int color = (attr & 0x3f) << 2;
    const bool fx = (attr & 0x40) == 0, fy = (attr & 0x80) != 0;
    const uint8_t pen = G.chars[code * 64 + (fy ? 7 - (ly & 7) : ly & 7) * 8 + (fx ? 7 - (lx & 7) : lx & 7)];
    if (pen == 0 || pen >= 0x0f) return -1;
    return G.palette[G.char_lut[(color + pen) & 0xff] & 0x5f];
}

static void render(void)
{
    for (int i = 0; i < W * H; i++) G.fb[i] = 0;
    const int sx = G.scrollx_bg & 0xff, sy = G.scrolly_bg & 0xff;
    if (G.star_control[5] & 1) {
        const int set_a = G.star_control[0] & 1, set_b = (G.star_control[1] & 1) | 2;
        for (int i = 0; i < 252; i++) {
            const star_seed *s = &kStars[i];
            if (s->set != set_a && s->set != set_b) continue;
            lplot((s->x + sx) & 0xff, (s->y + sy) & 0xff, G.palette[32 + (s->col & 63)]);
        }
    }
    for (int dy = 16; dy < 240; dy++)
        for (int dx = 0; dx < 256; dx++) {
            const int c = pf_px((dx + sx) & 0xff, (dy + sy) & 0xff);
            if (c >= 0) lplot(dx, dy, (uint16_t)c);
        }
    for (int f = 0; f < 6; f++) {
        const int x = G.mem[0x83d5 + f * 2] - 1, y = 240 - G.mem[0x8bd4 + f * 2];
        const uint8_t a = G.mem[0x83d4 + f * 2];
        const bool fx = a & 1, fy = a & 2;
        const int color = (G.mem[0x8bd5 + f * 2] & 0x3f) << 2;
        const uint8_t *px = G.sprites + ((a & 0xfc) >> 2) * 256;
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 16; c++) {
                const uint8_t pen = px[(fy ? 15 - r : r) * 16 + (fx ? 15 - c : c)];
                if (pen == 0 || pen >= 0x0f) continue;
                if (x + c >= 0 && x + c < 512 && y + r >= 0 && y + r < 512)
                    lplot(x + c, y + r, G.palette[G.char_lut[(color + pen) & 0xff] & 0x5f]);
            }
    }
    // the radar: its 8 columns of characters, the right half first
    for (int y = 16; y < 240; y++)
        for (int x = 0; x < 64; x++) {
            const int pos = (x >> 3) + ((y >> 3) << 5);
            const uint8_t attr = G.mem[0x8800 + pos], code = G.mem[0x8000 + pos];
            const int color = (attr & 0x3f) << 2;
            const bool fx = (attr & 0x40) == 0, fy = (attr & 0x80) != 0;
            const uint8_t pen = G.chars[code * 64 + (fy ? 7 - (y & 7) : y & 7) * 8 + (fx ? 7 - (x & 7) : x & 7)];
            if (!pen) continue;
            lplot(x >= 32 ? 221 + (x - 32) : 253 + x, y, G.palette[G.char_lut[(color + pen) & 0xff] & 0x5f]);
        }
}
#endif

// decoded, then turned a quarter clockwise (galaga_hw.pas's rot90)
static uint8_t *rotated(const arc_layout *l, const uint8_t *rom, size_t len)
{
    uint8_t *g = arc_gfx_decode(l, rom, len);
    if (!g) return NULL;
    const int n = l->total, s = l->width;
    uint8_t *out = (uint8_t *)malloc((size_t)n * s * s);
    if (out)
        for (int k = 0; k < n; k++)
            for (int r = 0; r < s; r++)
                for (int c = 0; c < s; c++) out[(k * s + r) * s + c] = g[(k * s + (s - 1 - c)) * s + r];
    free(g);
    return out;
}

static uint8_t rw3(uint8_t b) { return (uint8_t)(0x21 * (b & 1) + 0x47 * ((b >> 1) & 1) + 0x97 * ((b >> 2) & 1)); }

static void base_palette(const uint8_t *prom, bool stars)
{
    for (int i = 0; i < 0x20; i++) {
        const uint8_t v = prom[i];
        G.palette[i] = arc_rgb565((uint32_t)rw3(v) << 16 | (uint32_t)rw3(v >> 3) << 8 | (uint32_t)(0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1)));
    }
    if (!stars) return;
    static const uint8_t kStarMap[4] = {0x00, 0x47, 0x97, 0xde};
    for (int i = 0; i < 64; i++)
        G.palette[32 + i] = arc_rgb565((uint32_t)kStarMap[i & 3] << 16 | (uint32_t)kStarMap[(i >> 2) & 3] << 8 | kStarMap[(i >> 4) & 3]);
}

// an MCU's ROM, if there (the MB88 runs zeros without one, as dsp-cpp's)
static bool optional_rom(const char *name, uint32_t crc, uint8_t *dst, uint32_t size)
{
    return romfs_get_into(arc_game.sets, name, crc, dst, size);
}

static bool load_gfx(void)
{
#if GALAGA
    uint8_t crom[0x1000], srom[0x2000], prom[0x220];
    if (!ARC_LOAD(kChars, crom, sizeof crom) || !ARC_LOAD(kSprites, srom, sizeof srom) || !ARC_LOAD(kProms, prom, sizeof prom)) return false;
    arc_layout cl = { 8, 8, 0x100, 2, 128, false, false, false, {4, 0}, kCharX, kCharY };
    arc_layout sl = { 16, 16, 0x80, 2, 512, false, false, false, {4, 0}, kSprX, kSprY };
    if (!(G.chars = rotated(&cl, crom, sizeof crom)) || !(G.sprites = rotated(&sl, srom, sizeof srom))) return false;
    base_palette(prom, true);
    for (int i = 0; i < 0x100; i++) {
        G.char_lut[i] = (uint8_t)(prom[0x20 + i] + 0x10);
        G.tile_lut[i] = prom[0x120 + i];
    }
#elif DIGDUG
    uint8_t crom[0x800], srom[0x4000], brom[0x1000], prom[0x220];
    if (!ARC_LOAD(kChars, crom, sizeof crom) || !ARC_LOAD(kSprites, srom, sizeof srom) || !ARC_LOAD(kBgChars, brom, sizeof brom) ||
        !ARC_LOAD(kBgMap, G.bg_map, sizeof G.bg_map) || !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    arc_layout cl = { 8, 8, 0x100, 1, 64, false, false, false, {0}, kX8r, kCharY };
    arc_layout sl = { 16, 16, 0x100, 2, 512, false, false, false, {4, 0}, kSprX, kSprY };
    arc_layout bl = { 8, 8, 0x100, 2, 128, false, false, false, {4, 0}, kCharX, kCharY };
    if (!(G.chars = rotated(&cl, crom, sizeof crom)) || !(G.sprites = rotated(&sl, srom, sizeof srom)) ||
        !(G.tiles = rotated(&bl, brom, sizeof brom)))
        return false;
    base_palette(prom, false);
    for (int i = 0; i < 16; i++) {
        G.dd_char_lut[i * 2] = 0;
        G.dd_char_lut[i * 2 + 1] = (uint8_t)i;
    }
    for (int i = 0; i < 0x100; i++) {
        G.tile_lut[i] = (uint8_t)(prom[0x20 + i] + 0x10);
        G.dd_bg_lut[i] = prom[0x120 + i];
    }
#elif XEVIOUS
    uint8_t *srom = (uint8_t *)calloc(1, 0xa000), crom[0x1000], brom[0x2000], prom[0xb00];
    if (!srom || !ARC_LOAD(kChars, crom, sizeof crom) || !ARC_LOAD(kSprites, srom, 0x7000) || !ARC_LOAD(kBgChars, brom, sizeof brom) ||
        !ARC_LOAD(kBgMap, G.xtiles, sizeof G.xtiles) || !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    for (int f = 0x5000; f <= 0x6fff; f++) srom[f + 0x2000] = (uint8_t)(srom[f] >> 4);
    static const int kXSprX[16] = {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195};
    arc_layout cl = { 8, 8, 0x200, 1, 64, false, true, false, {0}, kX8, kCharY };
    arc_layout sl = { 16, 16, 0x140, 3, 512, false, true, false, {0x140 * 512 + 4, 0, 4}, kXSprX, kSprY };
    arc_layout bl = { 8, 8, 0x200, 2, 64, false, true, false, {0, 0x1000 * 8}, kX8, kCharY };
    if (!(G.chars = arc_gfx_decode(&cl, crom, sizeof crom)) || !(G.sprites = arc_gfx_decode(&sl, srom, 0xa000)) ||
        !(G.tiles = arc_gfx_decode(&bl, brom, sizeof brom)))
        return false;
    free(srom);
    for (int i = 0; i < 0x100; i++) {
#define W4(v) (uint32_t)(0x0e * ((v) & 1) + 0x1f * (((v) >> 1) & 1) + 0x43 * (((v) >> 2) & 1) + 0x8f * (((v) >> 3) & 1))
        G.palette[i] = arc_rgb565(W4(prom[i]) << 16 | W4(prom[0x100 + i]) << 8 | W4(prom[0x200 + i]));
#undef W4
    }
    for (int f = 0; f < 0x100; f++) G.char_lut[f] = (f & 1) ? (uint8_t)(f >> 1) : 0x80;
    for (int f = 0; f < 0x200; f++) {
        G.tile_lut[f] = (uint8_t)((prom[0x300 + f] & 0x0f) | ((prom[0x500 + f] & 0x0f) << 4));
        const uint8_t c = (uint8_t)((prom[0x700 + f] & 0x0f) | ((prom[0x900 + f] & 0x0f) << 4));
        G.sprite_lut[f] = (c & 0x80) ? (c & 0x7f) : 0x80;
    }
#elif BOSCO
    uint8_t crom[0x1000], srom[0x1000], prom[0x340];
    if (!ARC_LOAD(kChars, crom, sizeof crom) || !ARC_LOAD(kSprites, srom, sizeof srom) || !ARC_LOAD(kProms, prom, sizeof prom)) return false;
    arc_layout cl = { 8, 8, 0x100, 2, 128, false, false, false, {0, 4}, kCharX, kCharY };
    arc_layout sl = { 16, 16, 0x40, 2, 512, false, false, false, {0, 4}, kBoscoSprX, kSprY };
    if (!(G.chars = arc_gfx_decode(&cl, crom, sizeof crom)) || !(G.sprites = arc_gfx_decode(&sl, srom, sizeof srom))) return false;
    base_palette(prom, true);
    for (int i = 0; i < 0x100; i++) {
        G.char_lut[i] = (uint8_t)(prom[0x20 + i] + 0x10);
        G.tile_lut[i] = prom[0x120 + i];
    }
#endif
    return true;
}

bool arc_init(void)
{
    if (!(G_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    G.fb = (uint16_t *)calloc(W * H, 2);
    if (!G.fb || !ARC_LOAD(kMain, G.mem, 0x4000) || !ARC_LOAD(kSub, G.sub_rom, sizeof G.sub_rom) ||
        !ARC_LOAD(kSub2, G.sub2_rom, sizeof G.sub2_rom) || !ARC_LOAD(kWave, G.wave, 0x100) || !load_gfx())
        return false;
#if XEVIOUS
    // dsp-cpp skips the long ROM checksum
    G.mem[0x8500] = 0x10;
#if SUPER
    G.mem[0x015a] = 0xc3, G.mem[0x015b] = 0xe9, G.mem[0x015c] = 0x01;
    G.mem[0x0263] = 0x18, G.mem[0x0264] = 0x00;
    G.mem[0x0450] = 0x18, G.mem[0x0451] = 0x00;
#else
    G.mem[0x015b] = 0xc3, G.mem[0x015c] = 0xe7, G.mem[0x015d] = 0x01;
    G.mem[0x0261] = 0x18, G.mem[0x0262] = 0x00;
#endif
#endif
#if HAS50
    if (!DIGDUG && !ARC_LOAD(((const arc_rom[]){{"50xx.bin", 0x800, 0, 0xa0acbaf7}}), G.n50_rom, sizeof G.n50_rom)) return false;
    n50_init(&G.n50, DIGDUG ? NULL : G.n50_rom);
    n50_init(&G.n50b, G.n50_rom);
#endif
#if DIGDUG
    const bool have53 = optional_rom("53xx.bin", 0xb326fecb, G.n53_rom, sizeof G.n53_rom) ||
                        optional_rom("136007.105", 0, G.n53_rom, sizeof G.n53_rom);
    mb88_init(&G.n53, MB8843, have53 ? G.n53_rom : NULL);
    G.n53.read_k = n53_zero;
    G.n53.read_r = n53_r;
    G.n53.write_o = n53_o;
    mb88_set_reset_line(&G.n53, true);
#endif
    G.main.read = main_read;
    G.sub.read = sub_read;
    G.sub2.read = sub2_read;
    G.main.write = G.sub.write = G.sub2.write = main_write;
    G.main.in = G.sub.in = G.sub2.in = in_ff;
    G.main.out = G.sub.out = G.sub2.out = out_nop;
    G.main.cycles = main_cycles;
    zd_init(&G.main);
    zd_init(&G.sub);
    zd_init(&G.sub2);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&G.main);
    zd_reset(&G.sub);
    zd_reset(&G.sub2);
    ns51_reset(XEVIOUS);
#if HAS50
    n50_reset(&G.n50);
    n50_reset(&G.n50b);
    mb88_set_reset_line(&G.n50.cpu, false);
    mb88_set_reset_line(&G.n50b.cpu, false);
#endif
#if DIGDUG
    G.n53_o = 0;
    mb88_reset(&G.n53);
    mb88_set_reset_line(&G.n53, false);
    G.bg_select = G.bg_color_bank = G.tx_color_mode = 0;
    G.bg_disable = false;
#endif
    G.ctrl = G.ctrl2 = 0;
    memset(G.sel, 0, sizeof G.sel);
    G.nmi_enabled = false;
    G.nmi_acc = 0;
    memset(G.wsg, 0, sizeof G.wsg);
    memset(G.star_control, 0, sizeof G.star_control);
    G.scrollx_bg = G.scrolly_bg = G.scrollx_fg = G.scrolly_fg = 0;
    G.main_irq = G.sub_irq = G.sub2_nmi = false;
    G.sub_held = G.sub2_held = true;
    G.flip = false;
    G.in0 = G.in1 = 0xff;
    G.dsw_a = 0xff;
    G.dsw_b = SUPER ? 0x62 : XEVIOUS ? 0xee : 0xff;
    G.in2 = 0x11;
    for (int i = 0; i < 8; i++) latch_write(i, 0);
}

int arc_run_frame(const int16_t **audio)
{
    const int line_cycles = (int)(CLOCK / (uint32_t)(SCANLINES * FPS));
    G.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        zd_run(&G.main, line_cycles);
        if (!G.sub_held) zd_run(&G.sub, line_cycles);
        if (!G.sub2_held) zd_run(&G.sub2, line_cycles);
#if HAS50
        mb88_run(&G.n50.cpu, line_cycles);
#if BOSCO
        mb88_run(&G.n50b.cpu, line_cycles);
#endif
#endif
#if DIGDUG
        mb88_run(&G.n53, line_cycles);
#endif
        if (line == 63 || line == 191) {
            if (G.sub2_nmi) zd_set_nmi(&G.sub2, ZD_PULSE);
        } else if (line == 223) {
            if (G.main_irq) zd_set_irq(&G.main, ZD_ASSERT, 0xff);
            if (G.sub_irq) zd_set_irq(&G.sub, ZD_ASSERT, 0xff);
            render();
        }
    }
    if (audio) *audio = G.audio;
    return G.naudio;
}

const uint16_t *arc_frame(void) { return G.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t v1 = 0xff, v0 = 0xff;
    if (p1 & AP_UP) v1 &= ~0x01;
    if (p1 & AP_RIGHT) v1 &= ~0x02;
    if (p1 & AP_DOWN) v1 &= ~0x04;
    if (p1 & AP_LEFT) v1 &= ~0x08;
    if (p2 & AP_UP) v1 &= ~0x10;
    if (p2 & AP_RIGHT) v1 &= ~0x20;
    if (p2 & AP_DOWN) v1 &= ~0x40;
    if (p2 & AP_LEFT) v1 &= ~0x80;
    if (p1 & AP_B1) v0 &= ~0x01;
    if (p2 & AP_B1) v0 &= ~0x02;
    if (p1 & AP_START) v0 &= ~0x04;
    if (p2 & AP_START) v0 &= ~0x08;
    if (in->coin1) v0 &= ~0x10;
    if (in->coin2) v0 &= ~0x20;
    G.in0 = v0;
    G.in1 = v1;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) G.dsw_a = v;
    if (bank == 1) G.dsw_b = v;
}

