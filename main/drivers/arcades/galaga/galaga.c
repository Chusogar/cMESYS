// galaga.c - Galaga (Namco, 1981) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/galaga_hw (from dsp-emulator's
// galaga_hw.pas), the Galaga machine. Three Z80 at 3 MHz sharing the video
// and work RAM, a Namco 06xx bus with the 51xx (inputs and coins, the
// software model of dsp-emulator) and the 3-voice Namco WSG. Like dsp-cpp,
// the 54xx's explosions are not played. 8x8 characters, 16x16 sprites and
// the star field from a table of seeds.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"

#define CLOCK 3072000
#define SCANLINES 264
#define FPS 60.606060606
#define RATE 96000                      // the WSG: the CPU clock / 32
#define W 224
#define H 288
#define NMI_PERIOD 768                  // the 06xx's NMI while a chip is selected

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
static const int kCharY[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kSprX[16] = {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195};
static const int kSprY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sub, sub2;
    uint8_t mem[0x10000], sub_rom[0x1000], sub2_rom[0x1000], wave[0x100];
    uint8_t *chars, *sprites;           // rotated a quarter turn clockwise
    uint16_t palette[96];
    uint8_t char_lut[0x100], tile_lut[0x100], star_control[6];
    uint16_t *fb;
    bool main_irq, sub_irq, sub2_nmi, sub_held, sub2_held, flip;
    // 06xx
    uint8_t ctrl;
    bool nmi_enabled;
    int nmi_acc;
    // 51xx (software model)
    uint8_t mode, coincred_mode, credits, in_count, lastcoins, lastbuttons, coins[2], coins_per_cred[2], creds_per_coin[2];
    bool remap_joy;
    // WSG
    uint8_t wsg[0x20];
    uint8_t in0, in1, dsw_a, dsw_b;
    int64_t wsg_acc;
    int16_t audio[2048];
    int naudio;
} *G_;
#define G (*G_)

static void ns51_reset(void)
{
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
    case 1: G.coincred_mode = 4; G.credits = 0; break;
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

// the 06xx: slot 0 the 51xx (slot 3, the 54xx, has no effect here)
static void ctrl_write(uint8_t v)
{
    G.ctrl = v;
    if ((v & 0x0f) == 0) G.nmi_enabled = false;
    else G.nmi_enabled = true;
    G.nmi_acc = 0;
}

static uint8_t data_read(void)
{
    if (!(G.ctrl & 0x10)) return 0;
    uint8_t r = 0xff;
    if (G.ctrl & 1) r &= ns51_read();
    return r;
}

static void data_write(uint8_t v)
{
    if (G.ctrl & 0x10) return;
    if (G.ctrl & 1) ns51_write(v);
}

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

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff) return G.mem[a];
    if (a >= 0x6800 && a <= 0x6807) {
        const int b = a & 7;
        return (uint8_t)(((G.dsw_b >> b) & 1) | (((G.dsw_a >> b) & 1) << 1));
    }
    if (a >= 0x7000 && a <= 0x70ff) return data_read();
    if (a == 0x7100) return G.ctrl;
    if ((a >= 0x8000 && a <= 0x8bff) || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff)) return G.mem[a];
    return 0xff;
}

static uint8_t sub_read(void *u, uint16_t a) { return a <= 0x3fff ? (a < 0x1000 ? G.sub_rom[a] : 0) : main_read(u, a); }
static uint8_t sub2_read(void *u, uint16_t a) { return a <= 0x3fff ? (a < 0x1000 ? G.sub2_rom[a] : 0) : main_read(u, a); }

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
    if (a >= 0x6800 && a <= 0x681f) G.wsg[a & 0x1f] = v & 0x0f;
    else if (a >= 0x6820 && a <= 0x6827) latch_write(a & 7, v);
    else if (a >= 0x7000 && a <= 0x70ff) data_write(v);
    else if (a == 0x7100) ctrl_write(v);
    else if ((a >= 0x8000 && a <= 0x8bff) || (a >= 0x9000 && a <= 0x93ff) || (a >= 0x9800 && a <= 0x9bff)) G.mem[a] = v;
    else if (a >= 0xa000 && a <= 0xa005) G.star_control[a & 7] = v;
    else if (a == 0xa007) G.flip = v & 1;
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

bool arc_init(void)
{
    if (!(G_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    G.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t crom[0x1000], srom[0x2000], prom[0x220];
    if (!G.fb || !ARC_LOAD(kMain, G.mem, 0x4000) || !ARC_LOAD(kSub, G.sub_rom, 0x1000) ||
        !ARC_LOAD(kSub2, G.sub2_rom, 0x1000) || !ARC_LOAD(kWave, G.wave, 0x100) || !ARC_LOAD(kChars, crom, sizeof crom) ||
        !ARC_LOAD(kSprites, srom, sizeof srom) || !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    arc_layout cl = { 8, 8, 0x100, 2, 128, false, false, false, {4, 0}, kCharX, kCharY };
    arc_layout sl = { 16, 16, 0x80, 2, 512, false, false, false, {4, 0}, kSprX, kSprY };
    if (!(G.chars = rotated(&cl, crom, sizeof crom)) || !(G.sprites = rotated(&sl, srom, sizeof srom))) return false;
    for (int i = 0; i < 0x20; i++) {
        const uint8_t v = prom[i];
        G.palette[i] = arc_rgb565((uint32_t)rw3(v) << 16 | (uint32_t)rw3(v >> 3) << 8 | (uint32_t)(0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1)));
    }
    static const uint8_t kStarMap[4] = {0x00, 0x47, 0x97, 0xde};
    for (int i = 0; i < 64; i++)
        G.palette[32 + i] = arc_rgb565((uint32_t)kStarMap[i & 3] << 16 | (uint32_t)kStarMap[(i >> 2) & 3] << 8 | kStarMap[(i >> 4) & 3]);
    for (int i = 0; i < 0x100; i++) {
        G.char_lut[i] = (uint8_t)(prom[0x20 + i] + 0x10);
        G.tile_lut[i] = prom[0x120 + i];
    }
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
    G.dsw_a = G.dsw_b = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&G.main);
    zd_reset(&G.sub);
    zd_reset(&G.sub2);
    ns51_reset();
    G.ctrl = 0;
    G.nmi_enabled = false;
    G.nmi_acc = 0;
    memset(G.wsg, 0, sizeof G.wsg);
    memset(G.star_control, 0, sizeof G.star_control);
    G.main_irq = G.sub_irq = G.sub2_nmi = false;
    G.sub_held = G.sub2_held = true;
    G.flip = false;
    G.in0 = G.in1 = 0xff;
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
