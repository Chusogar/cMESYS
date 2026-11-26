// opwolf.c - Operation Wolf (Taito, 1987) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/opwolf (from dsp-emulator's
// operationwolf_hw.pas). A 68000 at 8 MHz (Musashi here) with the C-Chip
// simulated (dsp-cpp's opwolf_cchip: coins, difficulty and the level
// tables), a Z80 behind the TC0140SYT with a YM2151 (its CT pins the sound
// bank) and two MSM5205, two 512x512 layers of 8x8 tiles with row scroll and
// 16x16 sprites. The gun's sight is moved with the joystick (4 pixels a
// frame). The graphics are read straight from their ROMs and the picture
// built in place.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/msm5205dsp.h"

static m68kd cpu;

#define MAIN_CLOCK 8000000
#define SOUND_CLOCK 4000000
#define MSM_CLOCK 384000
#define FPS 60.0
#define SCANLINES 256
#define RATE 44100
#define W 320
#define H 240

static const char *const kSets[] = { "opwolf", NULL };
static const arc_rom kHigh[] = {{"b20-05-02.40", 0x10000, 0x00000, 0x3ffbfe3a}, {"b20-04.39", 0x10000, 0x10000, 0x216b4838}};
static const arc_rom kLow[] = {{"b20-03-02.30", 0x10000, 0x00000, 0xfdabd8a5}, {"b20-20.29", 0x10000, 0x10000, 0xd244431a}};
static const arc_rom kSound[] = {{"b20-07.10", 0x10000, 0, 0x45c7ace3}};
static const arc_rom kCchipEprom[] = {{"b20-18.73", 0x2000, 0, 0x5987b4e9}};
static const arc_rom kCchipMcu[] = {{"cchip_upd78c11.bin", 0x1000, 0, 0x43021521}};
static const arc_rom kTiles[] = {{"b20-13.13", 0x80000, 0, 0xf6acdab1}};
static const arc_rom kSprites[] = {{"b20-14.72", 0x80000, 0, 0x89f889e5}};
static const arc_rom kAdpcm[] = {{"b20-08.21", 0x80000, 0, 0xf3e19c64}};

const arc_info arc_game = { "opwolf", "Operation Wolf", "Taito", 1987, W, H, FPS, RATE, kSets,
                            { "FIRE", "GRENADE", NULL, NULL }, ARC_JOY_8WAY };

static const int kPX[16] = {8, 12, 0, 4, 24, 28, 16, 20, 40, 44, 32, 36, 56, 60, 48, 52};
static const int kTY[8] = {0, 32, 64, 96, 128, 160, 192, 224};
static const int kSY[16] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
static const arc_layout kTileL = { 8, 8, 0x4000, 4, 256, false, false, false, {0, 1, 2, 3}, kPX, kTY };
static const arc_layout kSprL = { 16, 16, 0x1000, 4, 1024, false, false, false, {0, 1, 2, 3}, kPX, kSY };

// the TC0140SYT (dsp-cpp's: the NMI on a filled port pair)
typedef struct {
    uint8_t slave[4], master[4], main_mode, sub_mode, status;
    bool nmi_enabled, nmi_req;
} syt_t;

// the simulated C-Chip
typedef struct {
    uint8_t ram[0x400 * 8];
    uint8_t cmd, bank, last_7a, last_04, last_05, c588, c589, c58a;
    uint8_t coins[2], coins_for_credit[2], credits_for_coin[2];
    int command_cycles;
} cchip_t;

// the machine on the heap
static struct state {
    zd_z80 sound;
    ym2151 ym;
    msm5205d msm0, msm1;
    syt_t syt;
    cchip_t cc;
    const uint8_t *rom, *tiles, *sprites, *adpcm, *sound_rom;   // blobs, the graphics raw
    uint16_t ram1[0x4000], ram2[0x8000], ram3[0x2000], pal_ram[0x800], palette[0x800];
    uint8_t sound_ram[0x1000], sound_bank, adpcm_b[8], adpcm_c[8];
    arc_fb fb;
    uint16_t scroll_x1, scroll_y1, scroll_x2, scroll_y2, gun_x, gun_y;
    uint8_t sprite_bank, in0, in1, dsw_a, dsw_b;
    int64_t audio_acc, msm_acc;
    int16_t audio[1024];
    int naudio;
} *O_;
#define O (*O_)

// ---------------------------------------------------------------------------
// The TC0140SYT

static void reset_sound(void);
static void syt_irq(void)
{
    if (O.syt.nmi_req && O.syt.nmi_enabled) {
        O.syt.nmi_req = false;
        zd_set_nmi(&O.sound, ZD_PULSE);
    }
}

static void syt_comm_w(uint8_t v)
{
    syt_t *s = &O.syt;
    v &= 0x0f;
    switch (s->main_mode) {
    case 0: s->slave[0] = v; s->main_mode = 1; break;
    case 1: s->slave[1] = v; s->main_mode = 2; s->status |= 1; s->nmi_req = true; syt_irq(); break;
    case 2: s->slave[2] = v; s->main_mode = 3; break;
    case 3: s->slave[3] = v; s->main_mode = 4; s->status |= 2; s->nmi_req = true; syt_irq(); break;
    case 4:
        if (v) {
            memset(s, 0, sizeof *s);
            reset_sound();
        }
        break;
    default: break;
    }
}

static uint8_t syt_comm_r(void)
{
    syt_t *s = &O.syt;
    switch (s->main_mode) {
    case 0: s->main_mode = 1; return s->master[0];
    case 1: s->status &= ~4; s->main_mode = 2; return s->master[1];
    case 2: s->main_mode = 3; return s->master[2];
    case 3: s->status &= ~8; s->main_mode = 4; return s->master[3];
    case 4: return s->status;
    default: return 0;
    }
}

static void syt_slave_comm_w(uint8_t v)
{
    syt_t *s = &O.syt;
    v &= 0x0f;
    switch (s->sub_mode) {
    case 0: s->master[0] = v; s->sub_mode = 1; break;
    case 1: s->master[1] = v; s->sub_mode = 2; s->status |= 4; break;
    case 2: s->master[2] = v; s->sub_mode = 3; break;
    case 3: s->master[3] = v; s->sub_mode = 4; s->status |= 8; break;
    case 5: s->nmi_enabled = false; break;
    case 6: s->nmi_enabled = true; break;
    default: break;
    }
    syt_irq();
}

static uint8_t syt_slave_comm_r(void)
{
    syt_t *s = &O.syt;
    uint8_t r = 0;
    switch (s->sub_mode) {
    case 0: r = s->slave[0]; s->sub_mode = 1; break;
    case 1: s->status &= ~1; r = s->slave[1]; s->sub_mode = 2; break;
    case 2: r = s->slave[2]; s->sub_mode = 3; break;
    case 3: s->status &= ~2; r = s->slave[3]; s->sub_mode = 4; break;
    case 4: r = s->status; break;
    default: break;
    }
    syt_irq();
    return r;
}

// ---------------------------------------------------------------------------
// The C-Chip

#define COMMAND_CYCLES 80000
static const uint16_t kLevelData[7][0xcc] = {
    {
        0x0480, 0x1008, 0x0300, 0x5701, 0x0001, 0x0010, 0x0480, 0x1008,
        0x0300, 0x5701, 0x0001, 0x002b, 0x0780, 0x0009, 0x0300, 0x4a01,
        0x0004, 0x0020, 0x0780, 0x1208, 0x0300, 0x5d01, 0x0004, 0x0030,
        0x0780, 0x0209, 0x0300, 0x4c01, 0x0004, 0x0038, 0x0780, 0x0309,
        0x0300, 0x4d01, 0x0004, 0x0048, 0x0980, 0x1108, 0x0300, 0x5a01,
        0xc005, 0x0018, 0x0980, 0x0109, 0x0300, 0x4b01, 0xc005, 0x0028,
        0x0b80, 0x020a, 0x0000, 0x6401, 0x8006, 0x0004, 0x0c80, 0x010b,
        0x0000, 0xf201, 0x8006, 0x8002, 0x0b80, 0x020a, 0x0000, 0x6401,
        0x8006, 0x0017, 0x0c80, 0x010b, 0x0000, 0xf201, 0x8006, 0x8015,
        0x0b80, 0x020a, 0x0000, 0x6401, 0x0007, 0x0034, 0x0c80, 0x010b,
        0x0000, 0xf201, 0x0007, 0x8032, 0x0b80, 0x020a, 0x0000, 0x6401,
        0x8006, 0x803e, 0x0c80, 0x010b, 0x0000, 0xf201, 0x8006, 0x803d,
        0x0b80, 0x100a, 0x0000, 0x6001, 0x0007, 0x0008, 0x0b80, 0x100a,
        0x0000, 0x6001, 0x0007, 0x000b, 0x0b80, 0x100a, 0x0000, 0x6001,
        0x0007, 0x001b, 0x0b80, 0x100a, 0x0000, 0x6001, 0x0007, 0x001e,
        0x0b80, 0x100a, 0x0000, 0x6001, 0x8007, 0x0038, 0x0b80, 0x100a,
        0x0000, 0x6001, 0x8007, 0x003b, 0x0b80, 0x100a, 0x0000, 0x6001,
        0x0007, 0x8042, 0x0b80, 0x100a, 0x0000, 0x6001, 0x0007, 0x8045,
        0x0c80, 0x000b, 0x0000, 0xf101, 0x800b, 0x8007, 0x0c80, 0x000b,
        0x0000, 0xf101, 0x800b, 0x801a, 0x0c80, 0x000b, 0x0000, 0xf101,
        0x000c, 0x8037, 0x0c80, 0x000b, 0x0000, 0xf101, 0x800b, 0x0042,
        0x0c80, 0xd04b, 0x0000, 0xf301, 0x8006, 0x8009, 0x0c80, 0xd04b,
        0x0000, 0xf301, 0x8006, 0x801c, 0x0c80, 0xd04b, 0x0000, 0xf301,
        0x8006, 0x0044, 0x0c80, 0x030b, 0x0000, 0xf401, 0x0008, 0x0028,
        0x0c80, 0x030b, 0x0000, 0xf401, 0x0008, 0x804b, 0x0c00, 0x040b,
        0x0000, 0xf501, 0x0008, 0x8026
    },
    {
        0x0780, 0x0209, 0x0300, 0x4c01, 0x0004, 0x0010, 0x0780, 0x0209,
        0x0300, 0x4c01, 0x4004, 0x0020, 0x0780, 0x0309, 0x0300, 0x4d01,
        0xe003, 0x0030, 0x0780, 0x0309, 0x0300, 0x4d01, 0x8003, 0x0040,
        0x0780, 0x0209, 0x0300, 0x4c01, 0x8004, 0x0018, 0x0780, 0x0309,
        0x0300, 0x4d01, 0xc003, 0x0028, 0x0b80, 0x000b, 0x0000, 0x0b02,
        0x8009, 0x0029, 0x0b80, 0x0409, 0x0000, 0x0f02, 0x8008, 0x8028,
        0x0b80, 0x040a, 0x0000, 0x3502, 0x000a, 0x8028, 0x0b80, 0x050a,
        0x0000, 0x1002, 0x8006, 0x8028, 0x0b80, 0x120a, 0x0000, 0x3602,
        0x0008, 0x004d, 0x0b80, 0x120a, 0x0000, 0x3602, 0x0008, 0x004f,
        0x0b80, 0x120a, 0x0000, 0x3602, 0x0008, 0x0001, 0x0b80, 0x120a,
        0x0000, 0x3602, 0x0008, 0x0003, 0x0b80, 0x130a, 0x0000, 0x3a02,
        0x0007, 0x0023, 0x0b80, 0x130a, 0x0000, 0x3a02, 0x0007, 0x8025,
        0x0b80, 0x130a, 0x0000, 0x3a02, 0x8009, 0x0023, 0x0b80, 0x130a,
        0x0000, 0x3a02, 0x8009, 0x8025, 0x0b80, 0x140a, 0x0000, 0x3e02,
        0x0007, 0x000d, 0x0b80, 0x140a, 0x0000, 0x3e02, 0x0007, 0x800f,
        0x0b80, 0x000b, 0x0000, 0x0102, 0x0007, 0x804e, 0x0b80, 0xd24b,
        0x0000, 0x0302, 0x0007, 0x000e, 0x0b80, 0x000b, 0x0000, 0x0402,
        0x8006, 0x0020, 0x0b80, 0xd34b, 0x0000, 0x0502, 0x8006, 0x0024,
        0x0b80, 0x000b, 0x0000, 0x0602, 0x8009, 0x0001, 0x0b80, 0xd44b,
        0x0000, 0x0702, 0x800b, 0x800b, 0x0b80, 0xd54b, 0x0000, 0x0802,
        0x800b, 0x000e, 0x0b80, 0x000b, 0x0000, 0x0902, 0x800b, 0x0010,
        0x0b80, 0x000b, 0x0000, 0x0a02, 0x0009, 0x0024, 0x0b80, 0xd64b,
        0x0000, 0x0c02, 0x000c, 0x8021, 0x0b80, 0x000b, 0x0000, 0x0d02,
        0x000c, 0x0025, 0x0b80, 0x000b, 0x0000, 0x0e02, 0x8009, 0x004e,
        0x0b80, 0x0609, 0x0300, 0x4e01, 0x8006, 0x8012, 0x0b80, 0x0609,
        0x0300, 0x4e01, 0x0007, 0x8007
    },
    {
        0x0480, 0x000b, 0x0300, 0x4501, 0x0001, 0x0018, 0x0480, 0x000b,
        0x0300, 0x4501, 0x2001, 0x0030, 0x0780, 0x1208, 0x0300, 0x5d01,
        0x0004, 0x0010, 0x0780, 0x1208, 0x0300, 0x5d01, 0x2004, 0x001c,
        0x0780, 0x1208, 0x0300, 0x5d01, 0xe003, 0x0026, 0x0780, 0x1208,
        0x0300, 0x5d01, 0x8003, 0x0034, 0x0780, 0x1208, 0x0300, 0x5d01,
        0x3004, 0x0040, 0x0780, 0x010c, 0x0300, 0x4601, 0x4004, 0x0022,
        0x0780, 0x010c, 0x0300, 0x4601, 0x6004, 0x0042, 0x0780, 0x000c,
        0x0500, 0x7b01, 0x800b, 0x0008, 0x0780, 0x010c, 0x0300, 0x4601,
        0x2004, 0x0008, 0x0000, 0x0000, 0x0000, 0xf001, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0xf001, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0xf001, 0x0000, 0x0000, 0x0b80, 0x000b, 0x0000, 0x1902,
        0x000b, 0x0004, 0x0b80, 0x000b, 0x0000, 0x1a02, 0x0009, 0x8003,
        0x0b80, 0x000b, 0x0000, 0x1902, 0x000b, 0x000c, 0x0b80, 0x000b,
        0x0000, 0x1a02, 0x0009, 0x800b, 0x0b80, 0x000b, 0x0000, 0x1902,
        0x000b, 0x001c, 0x0b80, 0x000b, 0x0000, 0x1a02, 0x0009, 0x801b,
        0x0b80, 0x000b, 0x0000, 0x1902, 0x000b, 0x002c, 0x0b80, 0x000b,
        0x0000, 0x1a02, 0x0009, 0x802b, 0x0b80, 0x000b, 0x0000, 0x1902,
        0x000b, 0x0044, 0x0b80, 0x000b, 0x0000, 0x1a02, 0x0009, 0x8043,
        0x0b80, 0x000b, 0x0000, 0x1902, 0x000b, 0x004c, 0x0b80, 0x000b,
        0x0000, 0x1a02, 0x0009, 0x804b, 0x0b80, 0x020c, 0x0300, 0x4801,
        0xa009, 0x0010, 0x0b80, 0x020c, 0x0300, 0x4801, 0xa009, 0x0028,
        0x0b80, 0x020c, 0x0300, 0x4801, 0xa009, 0x0036, 0x0000, 0x0000,
        0x0000, 0xf001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xf001,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xf001, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0xf001, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0xf001, 0x0000, 0x0000
    },
    {
        0x0480, 0x000b, 0x0300, 0x4501, 0x0001, 0x0018, 0x0480, 0x000b,
        0x0300, 0x4501, 0x2001, 0x002b, 0x0780, 0x010c, 0x0300, 0x4601,
        0x0004, 0x000d, 0x0780, 0x000c, 0x0500, 0x7b01, 0x800b, 0x0020,
        0x0780, 0x010c, 0x0300, 0x4601, 0x2004, 0x0020, 0x0780, 0x010c,
        0x0300, 0x4601, 0x8003, 0x0033, 0x0780, 0x010c, 0x0300, 0x4601,
        0x0004, 0x003c, 0x0780, 0x010c, 0x0300, 0x4601, 0xd003, 0x0045,
        0x0780, 0x000c, 0x0500, 0x7b01, 0x900b, 0x0041, 0x0780, 0x010c,
        0x0300, 0x4601, 0x3004, 0x0041, 0x0b80, 0x020c, 0x0300, 0x4801,
        0x0007, 0x0000, 0x0b80, 0x410a, 0x0000, 0x2b02, 0xe006, 0x4049,
        0x0b80, 0x020c, 0x0300, 0x4801, 0x8007, 0x000b, 0x0b80, 0x000b,
        0x0000, 0x2702, 0x800a, 0x8005, 0x0b80, 0x000b, 0x0000, 0x1e02,
        0x0008, 0x800e, 0x0b80, 0x000b, 0x0000, 0x1f02, 0x8007, 0x0011,
        0x0b80, 0x000b, 0x0000, 0x2802, 0x000b, 0x0012, 0x0b80, 0x000b,
        0x0000, 0x2002, 0x0007, 0x8015, 0x0b80, 0x000b, 0x0000, 0x2102,
        0x0007, 0x801b, 0x0b80, 0x000b, 0x0000, 0x2902, 0x800a, 0x001a,
        0x0b80, 0x000b, 0x0000, 0x2202, 0x8007, 0x001e, 0x0b80, 0x000b,
        0x0000, 0x1e02, 0x0008, 0x0025, 0x0b80, 0x000b, 0x0000, 0x2302,
        0x8007, 0x802c, 0x0b80, 0x000b, 0x0000, 0x2802, 0x000b, 0x8028,
        0x0b80, 0x020c, 0x0300, 0x4801, 0x0007, 0x0030, 0x0b80, 0x400a,
        0x0000, 0x2e02, 0x4007, 0x002d, 0x0b80, 0x000b, 0x0000, 0x2702,
        0x800a, 0x8035, 0x0b80, 0x020c, 0x0300, 0x4801, 0x8007, 0x0022,
        0x0b80, 0x000b, 0x0000, 0x2402, 0x8007, 0x0047, 0x0b80, 0x000b,
        0x0000, 0x2a02, 0x800a, 0x004b, 0x0b80, 0x000b, 0x0000, 0x2502,
        0x0007, 0x804b, 0x0b80, 0x000b, 0x0000, 0x2602, 0x0007, 0x004e,
        0x0b80, 0x020c, 0x0300, 0x4801, 0x0007, 0x8043, 0x0b80, 0x020c,
        0x0300, 0x4801, 0x8007, 0x803d
    },
    {
        0x0780, 0x0209, 0x0300, 0x4c01, 0x0004, 0x0010, 0x0780, 0x0209,
        0x0300, 0x4c01, 0x4004, 0x0020, 0x0780, 0x0309, 0x0300, 0x4d01,
        0xe003, 0x0030, 0x0780, 0x0309, 0x0300, 0x4d01, 0x8003, 0x0040,
        0x0780, 0x0209, 0x0300, 0x4c01, 0x8004, 0x0018, 0x0780, 0x0309,
        0x0300, 0x4d01, 0xc003, 0x0028, 0x0780, 0x000b, 0x0300, 0x5601,
        0x8004, 0x0008, 0x0780, 0x000b, 0x0300, 0x5601, 0x8004, 0x0038,
        0x0780, 0x000b, 0x0300, 0x5501, 0x8004, 0x0048, 0x0980, 0x0509,
        0x0f00, 0x0f01, 0x4005, 0x4007, 0x0980, 0x0509, 0x0f00, 0x0f01,
        0x4005, 0x4037, 0x0b80, 0x030a, 0x0000, 0x1302, 0x8006, 0x0040,
        0x0b80, 0x110a, 0x0000, 0x1502, 0x8008, 0x8048, 0x0b80, 0x110a,
        0x0000, 0x1502, 0x8008, 0x8049, 0x0b80, 0x000b, 0x0000, 0xf601,
        0x0007, 0x8003, 0x0b80, 0x000b, 0x0000, 0xf701, 0x0007, 0x0005,
        0x0b80, 0x000b, 0x0000, 0xf901, 0x0007, 0x8008, 0x0b80, 0x000b,
        0x0000, 0xf901, 0x0007, 0x0010, 0x0b80, 0x000b, 0x0000, 0xfa01,
        0x0007, 0x8013, 0x0b80, 0x000b, 0x0000, 0xf801, 0x800b, 0x800b,
        0x0b80, 0x000b, 0x0000, 0x0002, 0x800b, 0x801a, 0x0b80, 0x000b,
        0x0000, 0xf901, 0x0007, 0x8017, 0x0b80, 0x000b, 0x0000, 0xfa01,
        0x0007, 0x001b, 0x0b80, 0x000b, 0x0000, 0xf801, 0x800b, 0x0013,
        0x0b80, 0x000b, 0x0000, 0x4202, 0x800b, 0x0016, 0x0b80, 0x000b,
        0x0000, 0xfb01, 0x8007, 0x8020, 0x0b80, 0x000b, 0x0000, 0xf601,
        0x0007, 0x8023, 0x0b80, 0x000b, 0x0000, 0x4202, 0x800b, 0x800e,
        0x0b80, 0x000b, 0x0000, 0x4302, 0x800b, 0x801d, 0x0b80, 0x000b,
        0x0000, 0xf701, 0x0007, 0x0025, 0x0b80, 0x000b, 0x0000, 0xfd01,
        0x8006, 0x003f, 0x0b80, 0x000b, 0x0000, 0xfe01, 0x0007, 0x0046,
        0x0b80, 0x000b, 0x0000, 0xff01, 0x8007, 0x8049, 0x0b80, 0x000b,
        0x0000, 0xfc01, 0x8009, 0x0042
    },
    {
        0x0480, 0x1008, 0x0300, 0x5701, 0x0001, 0x0010, 0x0480, 0x1008,
        0x0300, 0x5701, 0x0001, 0x002b, 0x0780, 0x0009, 0x0300, 0x4a01,
        0x0004, 0x0020, 0x0780, 0x1208, 0x0300, 0x5d01, 0x0004, 0x0030,
        0x0780, 0x0209, 0x0300, 0x4c01, 0x0004, 0x0038, 0x0780, 0x0309,
        0x0300, 0x4d01, 0x0004, 0x0048, 0x0980, 0x1108, 0x0300, 0x5a01,
        0xc005, 0x0018, 0x0980, 0x0109, 0x0300, 0x4b01, 0xc005, 0x0028,
        0x0b80, 0x020a, 0x0000, 0x6401, 0x8006, 0x0004, 0x0c80, 0x010b,
        0x0000, 0xf201, 0x8006, 0x8002, 0x0b80, 0x020a, 0x0000, 0x6401,
        0x8006, 0x0017, 0x0c80, 0x010b, 0x0000, 0xf201, 0x8006, 0x8015,
        0x0b80, 0x020a, 0x0000, 0x6401, 0x0007, 0x0034, 0x0c80, 0x010b,
        0x0000, 0xf201, 0x0007, 0x8032, 0x0b80, 0x020a, 0x0000, 0x6401,
        0x8006, 0x803e, 0x0c80, 0x010b, 0x0000, 0xf201, 0x8006, 0x803d,
        0x0b80, 0x100a, 0x0000, 0x6001, 0x0007, 0x0008, 0x0b80, 0x100a,
        0x0000, 0x6001, 0x0007, 0x000b, 0x0b80, 0x100a, 0x0000, 0x6001,
        0x0007, 0x001b, 0x0b80, 0x100a, 0x0000, 0x6001, 0x0007, 0x001e,
        0x0b80, 0x100a, 0x0000, 0x6001, 0x8007, 0x0038, 0x0b80, 0x100a,
        0x0000, 0x6001, 0x8007, 0x003b, 0x0b80, 0x100a, 0x0000, 0x6001,
        0x0007, 0x8042, 0x0b80, 0x100a, 0x0000, 0x6001, 0x0007, 0x8045,
        0x0c80, 0x000b, 0x0000, 0xf101, 0x800b, 0x8007, 0x0c80, 0x000b,
        0x0000, 0xf101, 0x800b, 0x801a, 0x0c80, 0x000b, 0x0000, 0xf101,
        0x000c, 0x8037, 0x0c80, 0x000b, 0x0000, 0xf101, 0x800b, 0x0042,
        0x0c80, 0xd04b, 0x0000, 0xf301, 0x8006, 0x8009, 0x0c80, 0xd04b,
        0x0000, 0xf301, 0x8006, 0x801c, 0x0c80, 0xd04b, 0x0000, 0xf301,
        0x8006, 0x0044, 0x0c80, 0x030b, 0x0000, 0xf401, 0x0008, 0x0028,
        0x0c80, 0x030b, 0x0000, 0xf401, 0x0008, 0x804b, 0x0c00, 0x040b,
        0x0000, 0xf501, 0x0008, 0x8026
    },
    {
        0x0000, 0x1008, 0x0300, 0x5701, 0x0001, 0x0010, 0x0000, 0x1008,
        0x0300, 0x5701, 0x0001, 0x002b, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0700, 0x0009, 0x0300, 0x4a01, 0x0004, 0x0020,
        0x0700, 0x1208, 0x0300, 0x5d01, 0x0004, 0x0030, 0x0700, 0x0209,
        0x0300, 0x4c01, 0x0004, 0x0038, 0x0700, 0x0309, 0x0300, 0x4d01,
        0x0004, 0x0048, 0x0900, 0x1108, 0x0300, 0x5a01, 0xc005, 0x0018,
        0x0900, 0x0109, 0x0300, 0x4b01, 0xc005, 0x0028, 0x0000, 0x000b,
        0x0000, 0x0000, 0x0018, 0x0000, 0x0000, 0x000b, 0x0000, 0x0000,
        0x0018, 0x0000, 0x0000, 0x000b, 0x0000, 0x0000, 0x0018, 0x0000,
        0x0000, 0x000b, 0x0000, 0x0000, 0x0018, 0x0000, 0x0000, 0x000b,
        0x0000, 0x0000, 0x0018, 0x0000, 0x0000, 0x000b, 0x0000, 0x0000,
        0x0018, 0x0000, 0x0000, 0x000b, 0x0000, 0x0000, 0x0018, 0x0000,
        0x0000, 0x000b, 0x0000, 0x0000, 0x0018, 0x0000, 0x0000, 0x000b,
        0x0000, 0x0000, 0x0018, 0x0000, 0x0980, 0xdb4c, 0x0000, 0x3202,
        0x0006, 0x0004, 0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x8002,
        0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x8003, 0x0b80, 0x0609,
        0x0300, 0x4e01, 0x5006, 0x8004, 0x0b80, 0x0609, 0x0300, 0x4e01,
        0x5006, 0x0008, 0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x0010,
        0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x0012, 0x0b80, 0x0609,
        0x0300, 0x4e01, 0x5006, 0x0014, 0x0b80, 0x0609, 0x0300, 0x4e01,
        0x5006, 0x0016, 0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x0018,
        0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x0020, 0x0b80, 0x0609,
        0x0300, 0x4e01, 0x5006, 0x0023, 0x0b80, 0x0609, 0x0300, 0x4e01,
        0x5006, 0x0030, 0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x0038,
        0x0b80, 0x0609, 0x0300, 0x4e01, 0x5006, 0x0040, 0x0b80, 0x0609,
        0x0300, 0x4e01, 0x5006, 0x0042
    }
};

static uint16_t rom_word(uint32_t i) { return (uint16_t)(O.rom[i * 2] << 8 | O.rom[i * 2 + 1]); }

static void cc_reset(void)
{
    cchip_t *c = &O.cc;
    memset(c, 0, sizeof *c);
    c->last_04 = 0xfc;
    c->last_05 = 0xff;
    c->coins_for_credit[0] = c->coins_for_credit[1] = 1;
    c->credits_for_coin[0] = c->credits_for_coin[1] = 1;
    c->command_cycles = -1;
}

static void cc_difficulty(int mode)
{
    uint8_t *r = O.cc.ram;
    static const uint8_t k0[4][4] = {{0x20, 0x06, 0x07, 0x03}, {0x31, 0x05, 0x0f, 0x0b}, {0x3c, 0x04, 0x13, 0x0f}, {0x31, 0x05, 0x0f, 0x0b}};
    static const uint8_t k1[4][4] = {{0x30, 0x06, 0x0b, 0x03}, {0x3a, 0x05, 0x0f, 0x09}, {0x4c, 0x04, 0x19, 0x11}, {0x46, 0x05, 0x11, 0x0e}};
    const uint8_t *v = (mode ? k1 : k0)[r[0x15] & 3];
    r[0x2c] = v[0];
    r[0x77] = v[1];
    r[0x25] = v[2];
    r[0x26] = v[3];
}

static void cc_coinage(uint8_t dsw)
{
    static const uint32_t tables[2] = {0x03ffde, 0x03ffee};
    const int offs[2] = {12 - 4 * ((dsw & 0x30) >> 4), 12 - 4 * ((dsw & 0xc0) >> 6)};
    for (int s = 0; s < 2; s++) {
        O.cc.coins_for_credit[s] = (uint8_t)rom_word((tables[s] + (uint32_t)offs[s]) >> 1);
        O.cc.credits_for_coin[s] = (uint8_t)rom_word((tables[s] + (uint32_t)offs[s] + 2) >> 1);
    }
}

static uint16_t cc_data_r(uint16_t a) { return O.cc.ram[O.cc.bank * 0x400 + (a >> 1)]; }

static void cc_data_w(uint16_t a, uint16_t v)
{
    a >>= 1;
    O.cc.ram[O.cc.bank * 0x400 + a] = (uint8_t)v;
    if (O.cc.bank) return;
    if (a == 0x14) cc_coinage((uint8_t)v);
    if (a == 0x15) cc_difficulty(0);
}

static void cc_clear_flags(void)
{
    static const uint8_t k[] = {0x76, 0x75, 0x74, 0x72, 0x71, 0x70, 0x66, 0x2b, 0x30, 0x31, 0x32, 0x27};
    for (unsigned i = 0; i < sizeof k; i++) O.cc.ram[k[i]] = 0;
    O.cc.c588 = O.cc.c589 = O.cc.c58a = 0;
}

static void cc_complete_level(void)
{
    uint8_t *r = O.cc.ram;
    const int index = r[0x1b] < 6 ? r[0x1b] : 6;
    for (int i = 0; i < 0xcc; i++) {
        r[0x200 + i * 2] = (uint8_t)(kLevelData[index][i] >> 8);
        r[0x201 + i * 2] = (uint8_t)kLevelData[index][i];
    }
    r[0] = 0;
    cc_clear_flags();
    r[0x1a] = 0;
    r[0x7a] = 1;
    O.cc.cmd = 0;
}

static void cc_run(int cycles)
{
    cchip_t *c = &O.cc;
    if (c->command_cycles < 0) return;
    c->command_cycles -= cycles;
    if (c->command_cycles > 0) return;
    c->command_cycles = -1;
    if (c->cmd == 0xf5) cc_complete_level();
    c->cmd = 0;
}

static void cc_update(void)
{
    cchip_t *c = &O.cc;
    uint8_t *r = c->ram;
    if (r[0x04] != c->last_04) {
        int slot = -1;
        if (r[0x04] & 1) slot = 0;
        if (r[0x04] & 2) slot = 1;
        if (slot >= 0) {
            c->coins[slot]++;
            if (c->coins[slot] >= c->coins_for_credit[slot]) {
                r[0x53] = (uint8_t)(r[0x53] + c->credits_for_coin[slot]);
                r[0x51] = r[0x52] = 0x55;
                c->coins[slot] = (uint8_t)(c->coins[slot] - c->coins_for_credit[slot]);
            }
        }
        if (r[0x53] > 9) r[0x53] = 9;
    }
    c->last_04 = r[0x04];
    if (r[0x05] != c->last_05 && !(r[0x05] & 4)) {
        r[0x53]++;
        r[0x51] = r[0x52] = 0x55;
    }
    c->last_05 = r[0x05];
    if (r[0x1b] == 6) {
        if (!c->c58a && (r[0x72] & 0x7f) >= 8 && !r[0x74] && !r[0x1c] && !r[0x1d] && !r[0x1f]) {
            r[0x30] = 1;
            r[0x74] = 1;
            c->c58a = 1;
        }
        if (r[0x1a] == 0x90) r[0x74] = 0;
        if (c->c58a && !c->c589 && !r[0x27] && !r[0x75] && !r[0x1c] && !r[0x1d] && !r[0x1e] && !r[0x1f]) {
            r[0x31] = 1;
            r[0x75] = 1;
            c->c589 = 1;
        }
        if (r[0x2b] == 1) {
            r[0x2b] = 0;
            if (r[0x30] == 1 && r[0x1a] != 0x90) r[0x1a]--;
            if (r[0x72] == 9) {
                if (r[0x76] != 4) r[0x76] = 3;
            } else {
                c->c588 |= 0x80;
                r[0x72] = c->c588;
                c->c588++;
                r[0x1a] = (uint8_t)(r[0x1a] - 3);
            }
        }
        if (r[0x76] == 0) {
            r[0x76] = 1;
            cc_difficulty(1);
        }
    }
    if (r[0x34] < 2) {
        cc_difficulty(0);
        cc_clear_flags();
    }
    if (!r[0x1c] && !r[0x1d] && !r[0x1e] && !r[0x1f] && !r[0x20]) {
        if (r[0x1b] == 6) {
            if (r[0x27] == 1) r[0x32] = 1;
        } else r[0x32] = 1;
    }
    if (r[0x0e] == 1) {
        r[0x0e] = 0xfd;
        r[0x61] = 0x04;
    }
    if (r[0x7a] == 0 && c->last_7a != 0 && c->cmd != 0xf5) {
        c->cmd = 0xf5;
        c->command_cycles = COMMAND_CYCLES;
    }
    c->last_7a = r[0x7a];
    if (r[0x7f] == 0x0a) {
        r[0xfe] = 0xf7;
        r[0xff] = 0x6e;
    }
    r[0x64] = r[0x66] = 0;
}

// ---------------------------------------------------------------------------
// The 68000


static uint8_t pal5(unsigned v) { v &= 0x1f; return (uint8_t)((v << 3) | (v >> 2)); }
static void set_color(int i, uint16_t v)
{
    O.palette[i] = arc_rgb565((uint32_t)pal5(((v >> 7) & 0x1e) | ((v >> 14) & 1)) << 16 |
                              (uint32_t)pal5(((v >> 3) & 0x1e) | ((v >> 13) & 1)) << 8 | pal5(((v << 1) & 0x1e) | ((v >> 12) & 1)));
}

static uint16_t read16(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x3ffff) return (uint16_t)(O.rom[a & ~1u] << 8 | O.rom[a | 1]);
    if (a >= 0x0f0000 && a <= 0x0fffff) {
        const uint32_t sub = a & 0xfff;
        if (sub <= 0x7ff) return cc_data_r((uint16_t)sub);
        if ((sub & ~1u) == 0x802) return 1;
        return 0;
    }
    if (a >= 0x100000 && a <= 0x107fff) return O.ram1[(a & 0x7fff) >> 1];
    if (a >= 0x200000 && a <= 0x200fff) return O.pal_ram[(a & 0xfff) >> 1];
    switch (a & ~1u) {
    case 0x380000: return O.dsw_a;
    case 0x380002: return O.dsw_b;
    case 0x3a0000: return O.gun_x;
    case 0x3a0002: return O.gun_y;
    case 0x3e0002: return 0;            // (a word read leaves the mailbox)
    default: break;
    }
    if (a >= 0xc00000 && a <= 0xc0ffff) return O.ram2[(a & 0xffff) >> 1];
    if (a >= 0xd00000 && a <= 0xd03fff) return O.ram3[(a & 0x3fff) >> 1];
    return 0;
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a <= 0x3ffff) return;
    if (a >= 0x0ff000 && a <= 0x0ff7ff) { cc_data_w((uint16_t)(a & 0x7ff), v); return; }
    if ((a & ~1u) == 0x0ff802) {        // the status: the level loaded
        O.cc.ram[0x3d] = 1;
        O.cc.ram[0x7a] = 1;
        cc_difficulty(0);
        return;
    }
    if ((a & ~1u) == 0x0ffc00) { O.cc.bank = v & 7; return; }
    if (a >= 0x100000 && a <= 0x107fff) { O.ram1[(a & 0x7fff) >> 1] = v; return; }
    if (a >= 0x200000 && a <= 0x200fff) {
        const int i = (a & 0xfff) >> 1;
        if (O.pal_ram[i] != v) {
            O.pal_ram[i] = v;
            set_color(i, v);
        }
        return;
    }
    switch (a & ~1u) {
    case 0x380000: O.sprite_bank = (uint8_t)((v & 0xe0) >> 5); return;
    case 0x3e0000: O.syt.main_mode = (v >> 8) & 0x0f; return;
    case 0x3e0002: syt_comm_w((uint8_t)(v >> 8)); return;
    case 0xc20000: O.scroll_y1 = (512 - v) & 0x1ff; return;
    case 0xc20002: O.scroll_y2 = (512 - v) & 0x1ff; return;
    case 0xc40000: O.scroll_x1 = (512 - v) & 0x1ff; return;
    case 0xc40002: O.scroll_x2 = (512 - v) & 0x1ff; return;
    default: break;
    }
    if (a >= 0xc00000 && a <= 0xc0ffff) O.ram2[(a & 0xffff) >> 1] = v;
    else if (a >= 0xd00000 && a <= 0xd03fff) O.ram3[(a & 0x3fff) >> 1] = v;
}

// (the TC0140SYT's bytes alone: a read-modify-write would take a nibble)
static uint8_t cpu_rd8(void *u_, uint32_t a)
{
    (void)u_;
    if ((a & 0xfffffe) == 0x3e0002) return (a & 1) ? 0 : syt_comm_r();
    const uint16_t w = read16(a & ~1u);
    return (a & 1) ? w & 0xff : w >> 8;
}
static uint16_t cpu_rd(void *u_, uint32_t a) { (void)u_; return read16(a); }
static void cpu_wr8(void *u_, uint32_t a, uint8_t v)
{
    (void)u_;
    a &= 0xffffff;
    if ((a & ~1u) == 0x3e0000) { if (!(a & 1)) O.syt.main_mode = v & 0x0f; return; }
    if ((a & ~1u) == 0x3e0002) { if (!(a & 1)) syt_comm_w((uint8_t)v); return; }
    const uint16_t w = read16(a & ~1u);
    write16(a & ~1u, (a & 1) ? (uint16_t)((w & 0xff00) | (v & 0xff)) : (uint16_t)((w & 0xff) | (v & 0xff) << 8));
}
static void cpu_wr(void *u_, uint32_t a, uint16_t v) { (void)u_; write16(a, (uint16_t)v); }

// ---------------------------------------------------------------------------
// Sound

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff) return O.sound_rom[a];
    if (a <= 0x7fff) return O.sound_rom[O.sound_bank * 0x4000 + (a & 0x3fff)];
    if (a <= 0x8fff) return O.sound_ram[a & 0xfff];
    if (a == 0x9001) return ym2151_status(&O.ym);
    if (a == 0xa001) return syt_slave_comm_r();
    return 0xff;
}

static void start_adpcm(msm5205d *m, const uint8_t *r)
{
    m->position = (uint32_t)(r[0] + (r[1] << 8)) * 16;
    m->end = (uint32_t)(r[2] + (r[3] << 8)) * 16;
    msm5205d_set_reset(m, false);
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x8fff) O.sound_ram[a & 0xfff] = v;
    else if (a == 0x9000) ym2151_select(&O.ym, v);
    else if (a == 0x9001) ym2151_write(&O.ym, v);
    else if (a == 0xa000) O.syt.sub_mode = v & 0x0f;
    else if (a == 0xa001) syt_slave_comm_w(v);
    else if (a >= 0xb000 && a <= 0xb006) {
        O.adpcm_b[a & 7] = v;
        if ((a & 7) == 4) start_adpcm(&O.msm0, O.adpcm_b);
    } else if (a >= 0xc000 && a <= 0xc006) {
        O.adpcm_c[a & 7] = v;
        if ((a & 7) == 4) start_adpcm(&O.msm1, O.adpcm_c);
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&O.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }
static void ym_port(void *u, uint8_t ct) { (void)u; O.sound_bank = ct & 3; }

static void sound_cycles(void *u, int c)
{
    (void)u;
    ym2151_run_timers(&O.ym, c);
    O.msm_acc += (int64_t)c * msm5205d_sample_frequency(&O.msm0);
    while (O.msm_acc >= SOUND_CLOCK) {
        O.msm_acc -= SOUND_CLOCK;
        msm5205d_vclk(&O.msm0);
        msm5205d_vclk(&O.msm1);
    }
    O.audio_acc += (int64_t)c * RATE;
    while (O.audio_acc >= SOUND_CLOCK) {
        O.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&O.ym) + (msm5205d_output(&O.msm0) + msm5205d_output(&O.msm1)) / 4;
        if (O.naudio < 1024) O.audio[O.naudio++] = arc_clamp16(s);
    }
}

static void reset_sound(void)
{
    zd_reset(&O.sound);
    ym2151_reset(&O.ym);
    msm5205d_reset(&O.msm0);
    msm5205d_reset(&O.msm1);
    O.sound_bank = 0;
    memset(O.adpcm_b, 0, sizeof O.adpcm_b);
    memset(O.adpcm_c, 0, sizeof O.adpcm_c);
    O.audio_acc = O.msm_acc = 0;
}

// ---------------------------------------------------------------------------
// Video: the 512x512 work area's (16, 8) to (336, 248)

// a layer's pixel (row-scrolled), palette index or -1 (the foreground's pen 0)
static int layer_px(int base, int rowscroll, uint16_t sx, uint16_t sy, int X, int Y, bool fg)
{
    const int y = (Y + sy) & 0x1ff;
    const int rs = Y < 256 ? O.ram2[rowscroll + Y] : 0;
    const int x = (X + ((sx - rs) & 0x1ff)) & 0x1ff;
    const int t = (y >> 3) * 64 + (x >> 3);
    const uint16_t attr = O.ram2[base + t * 2];
    const int code = O.ram2[base + t * 2 + 1] & 0x3fff;
    const unsigned pen = arc_gfx_px(&kTileL, O.tiles, 0x80000, code, (attr & 0x4000) ? 7 - (x & 7) : x & 7,
                                    (attr & 0x8000) ? 7 - (y & 7) : y & 7);
    if (fg && !pen) return -1;
    return ((attr & 0x7f) << 4) + (int)pen;
}

static void update_video(void)
{
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&O.fb, y);
        for (int x = 0; x < W; x++) out[x] = O.palette[layer_px(0, 0x2000, O.scroll_x1, O.scroll_y1, x + 16, y + 8, false)];
    }
    for (int i = 255; i >= 0; i--) {    // the first sprite on top
        const uint16_t *e = &O.ram3[i * 4];
        const int code = e[2] & 0xfff;
        if (!code) continue;
        const uint16_t attr = e[0];
        const int color = ((attr & 0x0f) | ((O.sprite_bank & 0x0f) << 4)) << 4;
        const bool fx = attr & 0x4000, fy = attr & 0x8000;
        int px = e[3] & 0x1ff, py = e[1] & 0x1ff;
        if (px > 0x140) px -= 0x200;
        if (py > 0x140) py -= 0x200;
        px += 16;
        for (int r = 0; r < 16; r++) {
            const int Y = py + r, y = Y - 8;
            if (Y < 0 || Y >= 512 || y < 0 || y >= H) continue;
            uint16_t *out = arc_fb_row(&O.fb, y);
            for (int c = 0; c < 16; c++) {
                const int X = px + c, x = X - 16;
                if (X < 0 || X >= 512 || x < 0 || x >= W) continue;
                const unsigned pen = arc_gfx_px(&kSprL, O.sprites, 0x80000, code, fx ? 15 - c : c, fy ? 15 - r : r);
                if (pen) out[x] = O.palette[color + pen];
            }
        }
    }
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&O.fb, y);
        for (int x = 0; x < W; x++) {
            const int c = layer_px(0x4000, 0x6000, O.scroll_x2, O.scroll_y2, x + 16, y + 8, true);
            if (c >= 0) out[x] = O.palette[c];
        }
    }
    // the sight
    const int cx = O.gun_x - 15, cy = O.gun_y;
#define PLOT(px, py, col) do { const int qx = (px), qy = (py); if (qx >= 0 && qy >= 0 && qx < W && qy < H) arc_fb_row(&O.fb, qy)[qx] = (col); } while (0)
    for (int d = -6; d <= 6; d++) {
        if (d >= -1 && d <= 1) continue;
        PLOT(cx + d, cy - 1, 0);
        PLOT(cx + d, cy, 0xffff);
        PLOT(cx + d, cy + 1, 0);
        PLOT(cx - 1, cy + d, 0);
        PLOT(cx, cy + d, 0xffff);
        PLOT(cx + 1, cy + d, 0);
    }
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) PLOT(cx + dx, cy + dy, (dx == 0 && dy == 0) ? 0xffff : 0);
#undef PLOT
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&O.fb, y); }

bool arc_init(void)
{
    if (!(O_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&O.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    arc_blob b;
    if (arc_blob_begin(&b, "program", 0x40000, ARC_ROM_KEY(kHigh, ARC_ROM_KEY(kLow, 0)))) {
        uint8_t *h = (uint8_t *)malloc(0x20000), *l = (uint8_t *)malloc(0x20000), *img = (uint8_t *)malloc(0x40000);
        if (!h || !l || !img || !ARC_LOAD(kHigh, h, 0x20000) || !ARC_LOAD(kLow, l, 0x20000)) return false;
        for (int i = 0; i < 0x20000; i++) {
            img[i * 2] = h[i];
            img[i * 2 + 1] = l[i];
        }
        arc_blob_write(&b, 0, img, 0x40000);
        free(h);
        free(l);
        free(img);
    }
    uint8_t probe[0x2000];
    // (the C-Chip's own ROMs are not run, but the set needs them, as dsp-cpp)
    if (!(O.rom = arc_blob_end(&b)) || !(O.sound_rom = ARC_ROM_BLOB("sound", kSound, 0x10000)) ||
        !ARC_LOAD(kCchipEprom, probe, 0x2000) || !ARC_LOAD(kCchipMcu, probe, 0x1000) ||
        !(O.tiles = ARC_ROM_BLOB("tiles", kTiles, 0x80000)) || !(O.sprites = ARC_ROM_BLOB("sprites", kSprites, 0x80000)) ||
        !(O.adpcm = ARC_ROM_BLOB("adpcm", kAdpcm, 0x80000)))
        return false;
    O.sound.read = sound_read;
    O.sound.write = sound_write;
    O.sound.in = in_ff;
    O.sound.out = out_nop;
    O.sound.cycles = sound_cycles;
    zd_init(&O.sound);
    ym2151_init(&O.ym, SOUND_CLOCK, RATE);
    O.ym.irq_handler = ym_irq;
    O.ym.port_handler = ym_port;
    msm5205d_init(&O.msm0, MSM_CLOCK, 48, 4);
    msm5205d_init(&O.msm1, MSM_CLOCK, 48, 4);
    O.msm0.rom = O.msm1.rom = O.adpcm;
    O.msm0.rom_size = O.msm1.rom_size = 0x80000;
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    cpu.read_byte = cpu_rd8;
    cpu.write_byte = cpu_wr8;
    O.dsw_a = 0xff;
    O.dsw_b = 0x7f;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(O.pal_ram, 0, sizeof O.pal_ram);
    for (int i = 0; i < 0x800; i++) set_color(i, 0);
    m68kd_reset(&cpu);
    memset(&O.syt, 0, sizeof O.syt);
    reset_sound();
    cc_reset();
    O.in0 = 0xfc;
    O.in1 = 0xff;
    O.scroll_x1 = O.scroll_y1 = O.scroll_x2 = O.scroll_y2 = 0;
    O.sprite_bank = 0;
    O.gun_x = 175;
    O.gun_y = 120;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)(MAIN_CLOCK / FPS / SCANLINES + 0.5), sc = (int)(SOUND_CLOCK / FPS / SCANLINES + 0.5);
    O.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 248) {
            update_video();
            m68kd_set_irq(&cpu, 5, M68_HOLD);
            O.cc.ram[0x04] = O.in0;
            O.cc.ram[0x05] = O.in1;
            cc_update();
        }
        m68kd_run(&cpu, mc);
        cc_run(mc);
        zd_run(&O.sound, sc);
    }
    if (audio) *audio = O.audio;
    return O.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&O.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p = in->p[0];
    O.in0 = 0xfc;
    O.in1 = 0xff;
    if (in->coin1) O.in0 |= 1;
    if (in->coin2) O.in0 |= 2;
    if (p & AP_B1) O.in1 &= 0xfe;
    if (p & AP_B2) O.in1 &= 0xfd;
    if (p & AP_START) O.in1 &= 0xef;
    int x = O.gun_x, y = O.gun_y;
    if (p & AP_LEFT) x -= 4;
    if (p & AP_RIGHT) x += 4;
    if (p & AP_UP) y -= 4;
    if (p & AP_DOWN) y += 4;
    O.gun_x = (uint16_t)(x < 15 ? 15 : x > 15 + W - 1 ? 15 + W - 1 : x);
    O.gun_y = (uint16_t)(y < 0 ? 0 : y > H - 1 ? H - 1 : y);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) O.dsw_a = v;
    else if (bank == 1) O.dsw_b = v;
}
