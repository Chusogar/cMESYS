// m62.c - Irem M62 (1984-86) for the common arcade frontends (arc.h): C
// port of dsp-cpp's drivers/arcade/m62 (from dsp-emulator's m62_hw.pas).
// ARC_VARIANT: 0 Kung-Fu Master, 1 Spelunker, 2 Spelunker II, 3 Lode Runner,
// 4 Lode Runner II. A Z80, an M6803 driving two AY-3-8910 and two MSM5205
// (the first one's VCLK its NMI), a 512x512 tile layer (split in two
// priorities, or under Spelunker's 12-wide text) and 16x16 sprites up to 4
// high. The graphics are read straight from their ROMs and the picture built
// in place.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/hd63701dsp/hd63701dsp.h"
#include "../../../sound/ay8910.h"
#include "../../../sound/msm5205dsp.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define KUNGFU (ARC_VARIANT == 0)
#define SPELUNKER (ARC_VARIANT == 1 || ARC_VARIANT == 2)
#define LDRUN (ARC_VARIANT == 3 || ARC_VARIANT == 4)
#define FPS 56.338028
#define SCANLINES 284
#define MAIN_CLOCK (KUNGFU ? 3072000 : 4000000)
#define SOUND_CLOCK (3579545 / 4)
#define MSM_CLOCK 384000
#define RATE 44100
#define H 256

#if ARC_VARIANT == 0
static const char *const kSets[] = { "kungfum", NULL };
static const arc_rom kMain[] = {{"a-4e-c.bin", 0x4000, 0x0000, 0xb6e2d083}, {"a-4d-c.bin", 0x4000, 0x4000, 0x7532918e}};
static const arc_rom kPal[] = {
    {"g-1j-.bin", 0x100, 0x000, 0x668e6bca}, {"g-1f-.bin", 0x100, 0x100, 0x964b6495}, {"g-1h-.bin", 0x100, 0x200, 0x550563e1},
    {"b-1m-.bin", 0x100, 0x300, 0x76c05a9c}, {"b-1n-.bin", 0x100, 0x400, 0x23f06b99}, {"b-1l-.bin", 0x100, 0x500, 0x35e45021},
    {"b-5f-.bin", 0x020, 0x600, 0x7a601c3d}};
static const arc_rom kChars[] = {
    {"g-4c-a.bin", 0x2000, 0x0000, 0x6b2cc9c8}, {"g-4d-a.bin", 0x2000, 0x2000, 0xc648f558}, {"g-4e-a.bin", 0x2000, 0x4000, 0xfbe9276e}};
static const arc_rom kSound[] = {
    {"a-3e-.bin", 0x2000, 0xa000, 0x58e87ab0}, {"a-3f-.bin", 0x2000, 0xc000, 0xc81e31ea}, {"a-3h-.bin", 0x2000, 0xe000, 0xd99fb995}};
static const arc_rom kSprites[] = {
    {"b-4k-.bin", 0x2000, 0x00000, 0x16fb5150}, {"b-4f-.bin", 0x2000, 0x02000, 0x67745a33}, {"b-4l-.bin", 0x2000, 0x04000, 0xbd1c2261},
    {"b-4h-.bin", 0x2000, 0x06000, 0x8ac5ed3a}, {"b-3n-.bin", 0x2000, 0x08000, 0x28a213aa}, {"b-4n-.bin", 0x2000, 0x0a000, 0xd5228df3},
    {"b-4m-.bin", 0x2000, 0x0c000, 0xb16de4f2}, {"b-3m-.bin", 0x2000, 0x0e000, 0xeba0d66b}, {"b-4c-.bin", 0x2000, 0x10000, 0x01298885},
    {"b-4e-.bin", 0x2000, 0x12000, 0xc77b87d4}, {"b-4d-.bin", 0x2000, 0x14000, 0x6a70615f}, {"b-4a-.bin", 0x2000, 0x16000, 0x6189d626}};
#define ID "kungfum"
#define TITLE "Kung-Fu Master"
#define YEAR 1984
#define MAIN_SIZE 0x8000
#define CHARS 0x400
#define SPRITES 0x400
#define BUTTONS { "KICK", "PUNCH", NULL, NULL }
#define JOY ARC_JOY_4WAY
#elif ARC_VARIANT == 1
static const char *const kSets[] = { "spelunkr", NULL };
static const arc_rom kMain[] = {
    {"spra.4e", 0x4000, 0x0000, 0xcf811201}, {"spra.4d", 0x4000, 0x4000, 0xbb4faa4f},
    {"sprm.7c", 0x4000, 0x8000, 0xfb6197e2}, {"sprm.7b", 0x4000, 0xc000, 0x26bb25a4}};
static const arc_rom kPal[] = {
    {"sprm.2k", 0x100, 0x000, 0xfd8fa991}, {"sprm.2j", 0x100, 0x100, 0x0e3890b4}, {"sprm.2h", 0x100, 0x200, 0x0478082b},
    {"sprb.1m", 0x100, 0x300, 0x8d8cccad}, {"sprb.1n", 0x100, 0x400, 0xc40e1cb2}, {"sprb.1l", 0x100, 0x500, 0x3ec46248},
    {"sprb.5p", 0x020, 0x600, 0x746c6238}};
static const arc_rom kChars[] = {{"sprm.4p", 0x4000, 0x0000, 0x4dfe2e63}, {"sprm.4l", 0x4000, 0x4000, 0x239f2cd4}, {"sprm.4m", 0x4000, 0x8000, 0xd6d07d70}};
static const arc_rom kSound[] = {{"spra.3d", 0x4000, 0x8000, 0x4110363c}, {"spra.3f", 0x4000, 0xc000, 0x67a9d2e6}};
static const arc_rom kSprites[] = {
    {"sprb.4k", 0x4000, 0x00000, 0xe7f0e861}, {"sprb.4f", 0x4000, 0x04000, 0x32663097}, {"sprb.3p", 0x4000, 0x08000, 0x8fbaf373},
    {"sprb.4p", 0x4000, 0x0c000, 0x37069b76}, {"sprb.4c", 0x4000, 0x10000, 0xcfe46a88}, {"sprb.4e", 0x4000, 0x14000, 0x11c48979}};
static const arc_rom kTiles[] = {
    {"sprm.1d", 0x4000, 0x00000, 0x4ef7ae89}, {"sprm.1e", 0x4000, 0x04000, 0xa3755180}, {"sprm.3c", 0x4000, 0x08000, 0xb4008e6a},
    {"sprm.3b", 0x4000, 0x0c000, 0xf61cf012}, {"sprm.1c", 0x4000, 0x10000, 0x58b21c76}, {"sprm.1b", 0x4000, 0x14000, 0xa95cb3e5}};
#define ID "spelunkr"
#define TITLE "Spelunker"
#define YEAR 1985
#define MAIN_SIZE 0x10000
#define SPRITES 0x400
#define SPRITES_SP 1
#define BUTTONS { "JUMP", "FLARE", NULL, NULL }
#elif ARC_VARIANT == 2
static const char *const kSets[] = { "spelunk2", NULL };
static const arc_rom kMain[] = {
    {"sp2-a.4e", 0x4000, 0x00000, 0x96c04bbb}, {"sp2-a.4d", 0x4000, 0x04000, 0xcb38c2ff}, {"sp2-r.7d", 0x8000, 0x08000, 0x558837ea},
    {"sp2-r.7c", 0x8000, 0x10000, 0x4b380162}, {"sp2-r.7b", 0x4000, 0x18000, 0x7709a1fe}};
static const arc_rom kPal[] = {
    {"sp2-r.1k", 0x200, 0x000, 0x31c1bcdc}, {"sp2-r.2k", 0x100, 0x200, 0x1cf5987e}, {"sp2-r.2j", 0x100, 0x300, 0x1acbe2a5},
    {"sp2-b.1m", 0x100, 0x400, 0x906104c7}, {"sp2-b.1n", 0x100, 0x500, 0x5a564c06}, {"sp2-b.1l", 0x100, 0x600, 0x8f4a2e3c},
    {"sp2-b.5p", 0x020, 0x700, 0xcd126f6a}};
static const arc_rom kChars[] = {{"sp2-r.4l", 0x4000, 0x0000, 0x6a4b2d8b}, {"sp2-r.4m", 0x4000, 0x4000, 0xe1368b61}, {"sp2-r.4p", 0x4000, 0x8000, 0xfc138e13}};
static const arc_rom kSound[] = {{"sp2-a.3d", 0x4000, 0x8000, 0x839ec7e2}, {"sp2-a.3f", 0x4000, 0xc000, 0xad3ce898}};
static const arc_rom kSprites[] = {
    {"sp2-b.4k", 0x4000, 0x00000, 0x6cb67a17}, {"sp2-b.4f", 0x4000, 0x04000, 0xe4a1166f}, {"sp2-b.3n", 0x4000, 0x08000, 0xf59e8b76},
    {"sp2-b.4n", 0x4000, 0x0c000, 0xfa65bac9}, {"sp2-b.4c", 0x4000, 0x10000, 0x1caf7013}, {"sp2-b.4e", 0x4000, 0x14000, 0x780a463b}};
static const arc_rom kTiles[] = {{"sp2-r.1d", 0x8000, 0x00000, 0xc19fa4c9}, {"sp2-r.3b", 0x8000, 0x08000, 0x366604af}, {"sp2-r.1b", 0x8000, 0x10000, 0x3a0c4d47}};
#define ID "spelunk2"
#define TITLE "Spelunker II: 23 no Kagi"
#define YEAR 1986
#define MAIN_SIZE 0x1c000
#define SPRITES 0x400
#define SPRITES_SP 2
#define BUTTONS { "JUMP", "FLARE", NULL, NULL }
#elif ARC_VARIANT == 3
static const char *const kSets[] = { "ldrun", NULL };
static const arc_rom kMain[] = {
    {"lr-a-4e", 0x2000, 0x0000, 0x5d7e2a4d}, {"lr-a-4d", 0x2000, 0x2000, 0x96f20473},
    {"lr-a-4b", 0x2000, 0x4000, 0xb041c4a9}, {"lr-a-4a", 0x2000, 0x6000, 0x645e42aa}};
static const arc_rom kPal[] = {
    {"lr-e-3m", 0x100, 0x000, 0x53040416}, {"lr-e-3l", 0x100, 0x100, 0x67786037}, {"lr-e-3n", 0x100, 0x200, 0x5b716837},
    {"lr-b-1m", 0x100, 0x300, 0x4bae1c25}, {"lr-b-1n", 0x100, 0x400, 0x9cd3db94}, {"lr-b-1l", 0x100, 0x500, 0x08d8cf9a},
    {"lr-b-5p", 0x020, 0x600, 0xe01f69e2}};
static const arc_rom kChars[] = {{"lr-e-2d", 0x2000, 0x0000, 0x24f9b58d}, {"lr-e-2j", 0x2000, 0x2000, 0x43175e08}, {"lr-e-2f", 0x2000, 0x4000, 0xe0317124}};
static const arc_rom kSound[] = {{"lr-a-3f", 0x2000, 0xc000, 0x7a96accd}, {"lr-a-3h", 0x2000, 0xe000, 0x3f7f3939}};
static const arc_rom kSprites[] = {{"lr-b-4k", 0x2000, 0x0000, 0x8141403e}, {"lr-b-3n", 0x2000, 0x2000, 0x55154154}, {"lr-b-4c", 0x2000, 0x4000, 0x924e34d0}};
#define ID "ldrun"
#define TITLE "Lode Runner"
#define YEAR 1984
#define MAIN_SIZE 0x8000
#define CHARS 0x400
#define SPRITES 0x100
#define LDRUN_COLOR 0x0c
#define BUTTONS { "DIG LEFT", "DIG RIGHT", NULL, NULL }
#define JOY ARC_JOY_4WAY
#else
static const char *const kSets[] = { "ldrun2", NULL };
static const arc_rom kMain[] = {
    {"lr2-a-4e.a", 0x2000, 0x0000, 0x22313327}, {"lr2-a-4d", 0x2000, 0x2000, 0xef645179}, {"lr2-a-4a.a", 0x2000, 0x4000, 0xb11ddf59},
    {"lr2-a-4a", 0x2000, 0x6000, 0x470cc8a1}, {"lr2-h-1c.a", 0x2000, 0x8000, 0x7ebcadbc}, {"lr2-h-1d.a", 0x2000, 0xa000, 0x64cbb7f9}};
static const arc_rom kPal[] = {
    {"lr2-h-3m", 0x100, 0x000, 0x2c5d834b}, {"lr2-h-3l", 0x100, 0x100, 0x3ae69aca}, {"lr2-h-3n", 0x100, 0x200, 0x2b28aec5},
    {"lr2-b-1m", 0x100, 0x300, 0x4ec9bb3d}, {"lr2-b-1n", 0x100, 0x400, 0x1daf1fa4}, {"lr2-b-1l", 0x100, 0x500, 0xc8fb708a},
    {"lr2-b-5p", 0x020, 0x600, 0xe01f69e2}};
static const arc_rom kChars[] = {{"lr2-h-1e", 0x2000, 0x0000, 0x9d63a8ff}, {"lr2-h-1j", 0x2000, 0x2000, 0x40332bbd}, {"lr2-h-1h", 0x2000, 0x4000, 0x9404727d}};
static const arc_rom kSound[] = {
    {"lr2-a-3e", 0x2000, 0xa000, 0x853f3898}, {"lr2-a-3f", 0x2000, 0xc000, 0x7a96accd}, {"lr2-a-3h", 0x2000, 0xe000, 0x2a0e83ca}};
static const arc_rom kSprites[] = {
    {"lr2-b-4k", 0x2000, 0x0000, 0x79909871}, {"lr2-b-4f", 0x2000, 0x2000, 0x06ba1ef4}, {"lr2-b-3n", 0x2000, 0x4000, 0x3cc5893f},
    {"lr2-b-4n", 0x2000, 0x6000, 0x49c12f42}, {"lr2-b-4c", 0x2000, 0x8000, 0xfbe6d24c}, {"lr2-b-4e", 0x2000, 0xa000, 0x75172d1f}};
#define ID "ldrun2"
#define TITLE "Lode Runner II: The Bungeling Strikes Back"
#define YEAR 1984
#define MAIN_SIZE 0xc000
#define CHARS 0x400
#define SPRITES 0x200
#define LDRUN_COLOR 4
#define BUTTONS { "DIG LEFT", "DIG RIGHT", NULL, NULL }
#define JOY ARC_JOY_4WAY
#endif
#ifndef JOY
#define JOY ARC_JOY_8WAY
#endif
#if KUNGFU
#define W 256
#define CROP_X 128
#define CROP_Y 0
#else
#define W 384
#define CROP_X 64
#define CROP_Y (SPELUNKER ? 128 : 0)
#endif
#define SPRITE_ROM (SPRITES * 32 * 3)

const arc_info arc_game = { ID, TITLE, "Irem", YEAR, W, H, FPS, RATE, kSets, BUTTONS, JOY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 128, 129, 130, 131, 132, 133, 134, 135};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
static const arc_layout kSprL = { 16, 16, SPRITES, 3, 256, false, false, false, {2 * SPRITES * 256, SPRITES * 256, 0}, kX16, kY16 };
#if SPELUNKER
static const int kX12[12] = {0, 1, 2, 3, 0x10000, 0x10001, 0x10002, 0x10003, 0x10004, 0x10005, 0x10006, 0x10007};
static const arc_layout kCharL = { 12, 8, 0x200, 3, 64, false, false, false, {0, 0x4000 * 8, 2 * 0x4000 * 8}, kX12, kY8 };
static const arc_layout kTileL = { 8, 8, 4096, 3, 64, false, false, false, {2 * 4096 * 64, 4096 * 64, 0}, kX8, kY8 };
#else
static const arc_layout kCharL = { 8, 8, CHARS, 3, 64, false, false, false, {2 * CHARS * 64, CHARS * 64, 0}, kX8, kY8 };
#endif
#if ARC_VARIANT == 4
static const uint8_t kLdrun2Banks[30] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1};
#endif

// the machine on the heap
static struct state {
    zd_z80 main;
    hd63701 sound;
    ay8910 ay0, ay1;
    msm5205d msm0, msm1;
    const uint8_t *rom, *sound_rom, *chars, *sprites, *tiles;   // blobs, the graphics raw
    uint8_t ram[0x5000];                // a000-efff
    uint8_t sprite_height[0x20];
    uint16_t palette[768];
    arc_fb fb;
    uint8_t sound_command, port1, port2, rom_bank, rom_bank2, pal_bank, ldrun2_banksw, old_bank;
    uint16_t scroll_x, scroll_y;
    uint8_t in0, in1, in2, dsw_a, dsw_b;
    int64_t audio_acc, msm_acc;
    int16_t audio[1024];
    int naudio;
} *M_;
#define M (*M_)
#define MEM(a) M.ram[(a) - 0xa000]

// ---------------------------------------------------------------------------
// The main CPU

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff) return M.rom[a];
#if KUNGFU || ARC_VARIANT == 3
    if (a >= 0xd000 && a <= 0xefff) return MEM(a);
#elif ARC_VARIANT == 4
    if (a >= 0xd000 && a <= 0xefff) return MEM(a);
    if (a <= 0x9fff) return M.rom[0x8000 + (M.rom_bank & 1) * 0x2000 + (a & 0x1fff)];
#else
    if ((a >= 0xa000 && a <= 0xbfff) || (a >= 0xc800 && a <= 0xcfff) || (a >= 0xe000 && a <= 0xefff)) return MEM(a);
#if ARC_VARIANT == 1
    if (a <= 0x9fff) return M.rom[0x8000 + (M.rom_bank & 3) * 0x2000 + (a & 0x1fff)];
#else
    if (a <= 0x8fff) return M.rom[0x18000 + (M.rom_bank & 3) * 0x1000 + (a & 0x0fff)];
    if (a <= 0x9fff) return M.rom[0x8000 + (M.rom_bank2 & 15) * 0x1000 + (a & 0x0fff)];
#endif
#endif
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
#if KUNGFU
    if (a <= 0x7fff) return;
    if (a == 0xa000) M.scroll_x = (uint16_t)((M.scroll_x & 0x100) | v);
    else if (a == 0xb000) M.scroll_x = (uint16_t)((M.scroll_x & 0xff) | ((v & 1) << 8));
    else if ((a >= 0xc000 && a <= 0xc0ff) || (a >= 0xd000 && a <= 0xefff)) MEM(a) = v;
#elif LDRUN
    if (a <= 0x7fff) return;
    if ((a >= 0xc000 && a <= 0xc0ff) || (a >= 0xd000 && a <= 0xefff)) MEM(a) = v;
#else
    if (a <= 0x9fff) return;
    if (a <= 0xbfff || (a >= 0xc000 && a <= 0xc0ff) || (a >= 0xe000 && a <= 0xefff) || (a >= 0xc800 && a <= 0xcfff)) {
        MEM(a) = v;
        return;
    }
#if ARC_VARIANT == 1
    switch (a) {
    case 0xd000: M.scroll_y = (uint16_t)((M.scroll_y & 0x100) | v); break;
    case 0xd001: M.scroll_y = (uint16_t)((M.scroll_y & 0xff) | ((v & 1) << 8)); break;
    case 0xd002: M.scroll_x = (uint16_t)((M.scroll_x & 0x100) | v); break;
    case 0xd003: M.scroll_x = (uint16_t)((M.scroll_x & 0xff) | ((v & 1) << 8)); break;
    case 0xd004: M.rom_bank = v & 3; break;
    case 0xd005: M.pal_bank = (uint8_t)((v & 1) << 4); break;
    default: break;
    }
#else
    switch (a) {
    case 0xd000: M.scroll_y = (uint16_t)((M.scroll_y & 0x100) | v); break;
    case 0xd001: M.scroll_x = (uint16_t)((M.scroll_x & 0x100) | v); break;
    case 0xd002:
        M.scroll_x = (uint16_t)((M.scroll_x & 0xff) | ((v & 2) << 7));
        M.scroll_y = (uint16_t)((M.scroll_y & 0xff) | ((v & 1) << 8));
        M.pal_bank = (uint8_t)((v & 0x0c) << 2);
        break;
    case 0xd003:
        M.rom_bank = (uint8_t)((v & 0xc0) >> 6);
        M.rom_bank2 = (uint8_t)((v & 0x3c) >> 2);
        break;
    default: break;
    }
#endif
#endif
}

static uint8_t main_in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0: return M.in0;
    case 1: return M.in1;
    case 2: return M.in2;
    case 3: return M.dsw_a;
    case 4: return M.dsw_b;
    case 0x80:
#if ARC_VARIANT == 4
        if (M.ldrun2_banksw && --M.ldrun2_banksw == 0) M.rom_bank = 1;
#endif
        return 0;
    default: return 0xff;
    }
}

static void main_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0:
        if (!(v & 0x80)) M.sound_command = v & 0x7f;
        else hd63701_set_irq(&M.sound, HD_ASSERT);
        break;
#if ARC_VARIANT == 4
    case 0x80:
        M.rom_bank = (v >= 1 && v <= 30) ? kLdrun2Banks[v - 1] : 0;
        M.old_bank = v;
        break;
    case 0x81: M.ldrun2_banksw = (M.old_bank == 1 && v == 0x0d) ? 2 : 0; break;
#endif
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Sound: the M6803's port 2 strobes port 1 into (or out of) the AYs

static uint8_t sound_read(void *u, uint16_t a) { (void)u; return a >= 0x4000 ? M.sound_rom[a] : 0xff; }

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x0800 && a <= 0x08ff) switch (a & 3) {
        case 0: hd63701_set_irq(&M.sound, HD_CLEAR); break;
        case 1: msm5205d_data_w(&M.msm0, v); break;
        case 2: msm5205d_data_w(&M.msm1, v); break;
        default: break;
        }
}

static void ay0_port_b(uint8_t v)
{
    msm5205d_set_reset(&M.msm0, (v & 1) != 0);
    msm5205d_set_reset(&M.msm1, (v & 2) != 0);
}

// AY 0's port B the MSMs' resets (as dsp-cpp's AY: on a write to it, and
// when register 7 switches its direction)
static void ay0_write(uint8_t v)
{
    const uint8_t reg = M.ay0.latch & 0x0f;
    const int16_t prev = M.ay0.last_enable;
    ay8910_data_w(&M.ay0, v);
    if (reg == 15) ay0_port_b(v);
    else if (reg == 7) {
        const uint8_t en = M.ay0.regs[7];
        if (prev == -1 || ((prev & 0x80) != (en & 0x80))) ay0_port_b((en & 0x80) ? M.ay0.regs[15] : 0xff);
    }
}

static void port1_w(void *u, uint8_t v) { (void)u; M.port1 = v; }

static void port2_w(void *u, uint8_t v)
{
    (void)u;
    if ((M.port2 & 1) && !(v & 1)) {
        if (M.port2 & 4) {
            if (M.port2 & 8) ay8910_address_w(&M.ay0, M.port1);
            if (M.port2 & 0x10) ay8910_address_w(&M.ay1, M.port1);
        } else {
            if (M.port2 & 8) ay0_write(M.port1);
            if (M.port2 & 0x10) ay8910_data_w(&M.ay1, M.port1);
        }
    }
    M.port2 = v;
}

static uint8_t port1_r(void *u)
{
    (void)u;
    if (M.port2 & 8) {
        if ((M.ay0.latch & 0x0f) == 14) M.ay0.regs[14] = M.sound_command;   // port A: the command
        return ay8910_data_r(&M.ay0);
    }
    if (M.port2 & 0x10) return ay8910_data_r(&M.ay1);
    return 0xff;
}

static uint8_t port2_r(void *u) { (void)u; return 0; }
static void msm_vclk(void *u) { (void)u; hd63701_set_nmi(&M.sound, HD_PULSE); }

static void sound_cycles(void *u, int c)
{
    (void)u;
    M.msm_acc += (int64_t)c * msm5205d_sample_frequency(&M.msm0);
    while (M.msm_acc >= SOUND_CLOCK) {
        M.msm_acc -= SOUND_CLOCK;
        msm5205d_vclk(&M.msm0);
        msm5205d_vclk(&M.msm1);
    }
    M.audio_acc += (int64_t)c * RATE;
    while (M.audio_acc >= SOUND_CLOCK) {
        M.audio_acc -= SOUND_CLOCK;
        const int32_t s = ay8910_render(&M.ay0) + ay8910_render(&M.ay1) + msm5205d_output(&M.msm0) + msm5205d_output(&M.msm1);
        if (M.naudio < 1024) M.audio[M.naudio++] = arc_clamp16(s);
    }
}

// ---------------------------------------------------------------------------
// Video: the visible part of the 512x512 work area built in the picture

static inline uint16_t *px_at(int x, int y)
{
    x -= CROP_X;
    y -= CROP_Y;
    return x >= 0 && x < W && y >= 0 && y < H ? arc_fb_row(&M.fb, y) + x : NULL;
}

static void sprite_tile(int code, int color, bool fx, bool fy, int px, int py, int min_y)
{
    for (int y = 0; y < 16; y++) {
        const int dy = py + y;
        if (dy < min_y || dy >= 512) continue;
        for (int x = 0; x < 16; x++) {
            const int dx = px + x;
            if (dx < 0 || dx >= 512) continue;
            const unsigned pen = arc_gfx_px(&kSprL, M.sprites, SPRITE_ROM, code, fx ? 15 - x : x, fy ? 15 - y : y);
            uint16_t *d;
            if (pen && (d = px_at(dx, dy))) *d = M.palette[color + pen];
        }
    }
}

static void draw_sprites(int pos, int col, uint8_t col_mask, uint8_t pri_mask, uint8_t pri, int min_y)
{
    for (int i = 0; i < 0x20; i++) {
        const uint8_t *s = &MEM(0xc000 + i * 8);
        if ((s[0] & pri_mask) != pri) continue;
        const uint8_t attr = s[5];
        int code = s[4] + ((attr & 7) << 8);
        const int color = ((s[0] & col_mask) << 3) + 256 * col;
        const int x = ((s[7] & 1) << 8) + s[6];
        const int y = 256 + 128 * pos - 15 - (256 * (s[3] & 1) + s[2]);
        const bool fx = attr & 0x40, fy = attr & 0x80;
        switch (M.sprite_height[(code >> 5) & 0x1f] & 3) {
        case 1:
            code &= ~1;
            sprite_tile(fy ? code + 1 : code, color, fx, fy, x, y - 16, min_y);
            sprite_tile(fy ? code : code + 1, color, fx, fy, x, y, min_y);
            break;
        case 2:
            code &= ~3;
            sprite_tile(fy ? code + 3 : code, color, fx, fy, x, y - 48, min_y);
            sprite_tile(fy ? code + 2 : code + 1, color, fx, fy, x, y - 32, min_y);
            sprite_tile(fy ? code + 1 : code + 2, color, fx, fy, x, y - 16, min_y);
            sprite_tile(fy ? code : code + 3, color, fx, fy, x, y, min_y);
            break;
        default: sprite_tile(code, color, fx, fy, x, y, min_y); break;
        }
    }
}

#if !SPELUNKER
// a pixel of the tile layer (rows 256-511 black); *front: in the upper
// priority (Kung-Fu Master: opaque there; Lode Runner: its pens but 0)
static inline uint16_t layer_px(int lx, int ly, bool *front, bool *zero)
{
    *front = *zero = false;
    if (ly >= 256) return 0;
    const int off = (ly >> 3) * 64 + (lx >> 3);
#if KUNGFU
    const uint8_t attr = MEM(0xd800 + off);
    const int code = MEM(0xd000 + off) + ((attr & 0xc0) << 2);
    *front = (ly >> 3) < 6 || ((attr & 0x1f) >> 1) > 0x0c;
#else
    const uint8_t attr = MEM(0xd001 + off * 2);
    const int code = MEM(0xd000 + off * 2) + ((attr & 0xc0) << 2);
    *front = ((attr & 0x1f) >> 1) >= LDRUN_COLOR;
#endif
    const unsigned pen = arc_gfx_px(&kCharL, M.chars, CHARS * 24, code, (attr & 0x20) ? 7 - (lx & 7) : lx & 7, ly & 7);
    *zero = pen == 0;
    return M.palette[((attr & 0x1f) << 3) + pen];
}
#endif

static void update_video(void)
{
#if KUNGFU
    bool front, zero;
    for (int Y = 0; Y < H; Y++) {
        uint16_t *out = arc_fb_row(&M.fb, Y);
        for (int x = 0; x < W; x++) {
            const int X = x + CROP_X;
            out[x] = layer_px(Y < 48 ? X : (X + M.scroll_x) & 511, Y, &front, &zero);
        }
    }
    draw_sprites(1, 1, 0x1f, 0, 0, 48);
    for (int Y = 48; Y < H; Y++) {
        uint16_t *out = arc_fb_row(&M.fb, Y);
        for (int x = 0; x < W; x++) {
            const uint16_t c = layer_px((x + CROP_X + M.scroll_x) & 511, Y, &front, &zero);
            if (front) out[x] = c;
        }
    }
#elif LDRUN
    bool front, zero;
    for (int Y = 0; Y < H; Y++) {
        uint16_t *out = arc_fb_row(&M.fb, Y);
        for (int x = 0; x < W; x++) out[x] = layer_px(x + CROP_X, Y, &front, &zero);
    }
    draw_sprites(1, 1, 0x0f, 0x10, 0, 0);
    for (int Y = 0; Y < H; Y++) {
        uint16_t *out = arc_fb_row(&M.fb, Y);
        for (int x = 0; x < W; x++) {
            const uint16_t c = layer_px(x + CROP_X, Y, &front, &zero);
            if (front && !zero) out[x] = c;
        }
    }
    draw_sprites(1, 1, 0x0f, 0x10, 0x10, 0);
#else
    // the scrolled background, the sprites, then the 12-wide text over it
    for (int Y = CROP_Y; Y < CROP_Y + H; Y++) {
        uint16_t *out = arc_fb_row(&M.fb, Y - CROP_Y);
        const int ly = (Y + M.scroll_y) & 511;
        for (int x = 0; x < W; x++) {
            const int lx = (x + CROP_X + M.scroll_x) & 511, off = (ly >> 3) * 64 + (lx >> 3);
            const uint8_t color = MEM(0xa001 + off * 2);
#if ARC_VARIANT == 2
            const int code = MEM(0xa000 + off * 2) + ((color & 0xf0) << 4);
#else
            const int code = MEM(0xa000 + off * 2) + ((color & 0x10) << 4) + ((color & 0x20) << 6) + ((color & 0xc0) << 3);
#endif
            out[x] = M.palette[((M.pal_bank | (color & 0x0f)) << 3) + arc_gfx_px(&kTileL, M.tiles, 0x18000, code, lx & 7, ly & 7)];
        }
    }
    draw_sprites(2, SPRITES_SP, 0x1f, 0, 0, 0);
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&M.fb, y);
        for (int x = 0; x < W; x++) {
            const int off = (y >> 3) * 32 + x / 12;
            const uint8_t color = MEM(0xc801 + off * 2);
            const int code = MEM(0xc800 + off * 2) + ((color & 0x10) << 4);
            const unsigned pen = arc_gfx_px(&kCharL, M.chars, 0xc000, code, x % 12, y & 7);
            if (pen) out[x] = M.palette[((M.pal_bank | (color & 0x0f)) << 3) + pen];
        }
    }
#endif
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&M.fb, y); }

// ---------------------------------------------------------------------------

static void build_palette(const uint8_t *prom)
{
    static const arc_resnet net = {4, {2200, 1000, 470, 220}, 470, 0};
    const arc_resnet nets[3] = {net, net, net};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
#define PROM_RGB(i) arc_rgb565((uint32_t)arc_combine(w[0], 4, prom[(i)] & 15) << 16 | \
                               (uint32_t)arc_combine(w[1], 4, prom[(i) + 0x100] & 15) << 8 | \
                               (uint32_t)arc_combine(w[2], 4, prom[(i) + 0x200] & 15))
#if ARC_VARIANT == 2
    for (int i = 0; i < 0x200; i++) {
        const uint8_t packed = prom[i], blue = prom[i + 0x200];
        const uint8_t g = (uint8_t)(((packed & 0xf0) >> 4) | (packed & 0xf0));
        M.palette[i] = arc_rgb565((uint32_t)((packed & 15) * 0x11) << 16 | (uint32_t)g << 8 | (uint32_t)((blue & 15) * 0x11));
    }
    for (int i = 0; i < 0x100; i++) M.palette[i + 0x200] = PROM_RGB(i + 0x400);
#else
    for (int i = 0; i < 0x100; i++) {
        M.palette[i] = PROM_RGB(i);
        M.palette[i + 0x100] = PROM_RGB(i + 0x300);
    }
#endif
#undef PROM_RGB
}

bool arc_init(void)
{
    if (!(M_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&M.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    uint8_t *prom = (uint8_t *)calloc(1, 0x800);
    if (!prom || !ARC_LOAD(kPal, prom, 0x800)) return false;
    build_palette(prom);
    memcpy(M.sprite_height, prom + (ARC_VARIANT == 2 ? 0x700 : 0x600), 0x20);
    free(prom);
    if (!(M.rom = ARC_ROM_BLOB("main", kMain, MAIN_SIZE)) || !(M.sound_rom = ARC_ROM_BLOB("sound", kSound, 0x10000)) ||
        !(M.sprites = ARC_ROM_BLOB("sprites", kSprites, SPRITE_ROM)))
        return false;
#if SPELUNKER
    // the characters: each plane's 4 KB blocks split in two halves 8 KB apart
    const uint8_t *raw = ARC_ROM_BLOB("chars.raw", kChars, 0xc000);
    if (!raw) return false;
    arc_blob b;
    if (arc_blob_begin(&b, "chars", 0xc000, ARC_ROM_KEY(kChars, 1))) {
        uint8_t *re = (uint8_t *)calloc(1, 0xc000);
        if (!re) return false;
        for (int plane = 0; plane < 3; plane++)
            for (int block = 0; block < 4; block++) {
                const int src = plane * 0x4000 + block * 0x1000, dst = plane * 0x4000 + block * 0x800;
                memcpy(re + dst, raw + src, 0x800);
                memcpy(re + dst + 0x2000, raw + src + 0x800, 0x800);
            }
        arc_blob_write(&b, 0, re, 0xc000);
        free(re);
    }
    arc_blob_free(raw);
    if (!(M.chars = arc_blob_end(&b)) || !(M.tiles = ARC_ROM_BLOB("tiles", kTiles, 0x18000))) return false;
#else
    if (!(M.chars = ARC_ROM_BLOB("chars", kChars, CHARS * 24))) return false;
#endif
    M.main.read = main_read;
    M.main.write = main_write;
    M.main.in = main_in;
    M.main.out = main_out;
    zd_init(&M.main);
    hd63701_init(&M.sound, SOUND_CLOCK, HD_M6803);
    M.sound.read_mem = sound_read;
    M.sound.write_mem = sound_write;
    M.sound.port_read[0] = port1_r;
    M.sound.port_read[1] = port2_r;
    M.sound.port_write[0] = port1_w;
    M.sound.port_write[1] = port2_w;
    M.sound.cycles = sound_cycles;
    ay8910_init(&M.ay0, SOUND_CLOCK, RATE);
    ay8910_init(&M.ay1, SOUND_CLOCK, RATE);
    msm5205d_init(&M.msm0, MSM_CLOCK, 96, 4);
    msm5205d_init(&M.msm1, MSM_CLOCK, 0, 4);
    M.msm0.vclk_handler = msm_vclk;
    M.dsw_a = 0xff;
    M.dsw_b = 0xfd;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&M.main);
    hd63701_reset(&M.sound);
    ay8910_reset(&M.ay0);
    ay8910_reset(&M.ay1);
    msm5205d_reset(&M.msm0);
    msm5205d_reset(&M.msm1);
    M.sound_command = M.port1 = M.port2 = 0;
    M.rom_bank = M.rom_bank2 = M.pal_bank = M.ldrun2_banksw = M.old_bank = 0;
    M.scroll_x = M.scroll_y = 0;
    M.in0 = M.in1 = M.in2 = 0xff;
    M.audio_acc = M.msm_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)(MAIN_CLOCK / FPS / SCANLINES), sc = (int)(SOUND_CLOCK / FPS / SCANLINES);
    M.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 256) {
            zd_set_irq(&M.main, ZD_HOLD, 0xff);
            update_video();
        }
        zd_run(&M.main, mc);
        hd63701_run(&M.sound, sc);
    }
    if (audio) *audio = M.audio;
    return M.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&M.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0];
    uint8_t a = 0xff, b = 0xff, c = 0xff;
    if (p1 & AP_RIGHT) b &= 0xfe;
    if (p1 & AP_LEFT) b &= 0xfd;
    if (p1 & AP_DOWN) b &= 0xfb;
    if (p1 & AP_UP) b &= 0xf7;
    if (p1 & AP_B1) b &= 0xdf;
    if (p1 & AP_B2) b &= 0x7f;
    if (in->coin1) a &= 0xf7;
    if (in->coin2) c &= 0xef;
    if (p1 & AP_START) a &= 0xfe;
    if (in->p[1] & AP_START) a &= 0xfd;
    M.in0 = a;
    M.in1 = b;
    M.in2 = c;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) M.dsw_a = v;
    else if (bank == 1) M.dsw_b = v;
}
