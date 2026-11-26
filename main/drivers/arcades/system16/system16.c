// system16.c - Sega System 16A / 16B (1986-1990) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/system16 (after
// dsp-emulator's system16a_hw.pas / system16b_hw.pas). A 68000 at 10 MHz
// (cpu/m68kdsp; FD1089 or FD1094 encrypted on some), the tilemaps, text and
// sprites of video/sega16d, a Z80 with a YM2151 and, on 16A, an 8255 whose
// port A handshake raises the Z80's NMI (and an N7751 for the speech of
// Shinobi, Alex Kidd and Alien Syndrome); on 16B the 315-5195 memory mapper,
// a uPD7759 fed by the Z80, the 315-5248 / 5250 maths chips and the i8751 of
// Altered Beast and Golden Axe.
// ARC_VARIANT, in dsp-cpp's order: 0 fantzone, 1 shinobi, 2 tetris,
// 3 altbeast, 4 alexkidd, 5 aliensyn, 6 wb3, 7 goldnaxe, 8 ddux, 9 eswat,
// 10 passsht, 11 aurail, 12 riotcity, 13 sdi, 14 cotton, 15 bayroute,
// 16 sonicbom, 17 timescan.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mcs51dsp/mcs51dsp.h"
#include "../../../cpu/mcs48/mcs48.h"
#include "../../../machine/i8255dsp.h"
#include "../../../machine/fd1089d.h"
#include "../../../machine/fd1094d.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/upd7759d.h"
#include "../../../video/sega16d.h"
#include "s16_roms.h"

#define G_FANTZONE 0
#define G_SHINOBI 1
#define G_TETRIS 2
#define G_ALTBEAST 3
#define G_ALEXKIDD 4
#define G_ALIENSYN 5
#define G_WB3 6
#define G_GOLDNAXE 7
#define G_DDUX 8
#define G_ESWAT 9
#define G_PASSSHT 10
#define G_AURAIL 11
#define G_RIOTCITY 12
#define G_SDI 13
#define G_COTTON 14
#define G_BAYROUTE 15
#define G_SONICBOM 16
#define G_TIMESCAN 17
#define GAME ARC_VARIANT
#define IS_16B_C (GAME == G_ALTBEAST || GAME >= G_GOLDNAXE)
#define IS_16B (S.b16)                          // (Alien Syndrome and Wonder Boy III: also as 16B sets)
#define USES_N7751 (S.n7751_on)
#define SOUND_CLOCK (S.sound_clock)
#define ROTATED (GAME == G_PASSSHT || GAME == G_SONICBOM || GAME == G_TIMESCAN)
#define USES_N7751_C (GAME == G_SHINOBI || GAME == G_ALEXKIDD || GAME == G_ALIENSYN)

#define NW 320                                  // the native screen
#define NH 224
#if ROTATED
#define W NH
#define H NW
#else
#define W NW
#define H NH
#endif
#define SCANLINES 262
#define CPU_SYNC 4
#define MAIN_CLOCK 10000000
#define RATE 44100
#define MIX_GAIN 1.8
#if IS_16B_C
#define FPS 60.05439
#else
#define FPS 60.0
#endif
#define N(k) ((int)(sizeof k / sizeof k[0]))

#if GAME == G_FANTZONE
static const char *const kSets[] = { "fantzone", NULL };
const arc_info arc_game = { "fantzone", "Fantasy Zone", "Sega", 1986, W, H, FPS, RATE, kSets, { "DISPARO", "BOMBA", NULL }, ARC_JOY_8WAY };
#elif GAME == G_SHINOBI
static const char *const kSets[] = { "shinobi", NULL };
const arc_info arc_game = { "shinobi", "Shinobi", "Sega", 1987, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", "MAGIA" }, ARC_JOY_8WAY };
#elif GAME == G_TETRIS
static const char *const kSets[] = { "tetris", NULL };
const arc_info arc_game = { "s16tetris", "Tetris (Sega)", "Sega", 1988, W, H, FPS, RATE, kSets, { "GIRAR", NULL }, ARC_JOY_4WAY };
#elif GAME == G_ALTBEAST
static const char *const kSets[] = { "altbeast", NULL };
const arc_info arc_game = { "altbeast", "Altered Beast", "Sega", 1988, W, H, FPS, RATE, kSets, { "PUNO", "PATADA", "SALTO" }, ARC_JOY_8WAY };
#elif GAME == G_ALEXKIDD
static const char *const kSets[] = { "alexkidd", NULL };
const arc_info arc_game = { "alexkidd", "Alex Kidd: The Lost Stars", "Sega", 1986, W, H, FPS, RATE, kSets, { "SALTO", "DISPARO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_ALIENSYN
static const char *const kSets[] = { "aliensyn", NULL };
const arc_info arc_game = { "aliensyn", "Alien Syndrome", "Sega", 1987, W, H, FPS, RATE, kSets, { "DISPARO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_WB3
static const char *const kSets[] = { "wb3", NULL };
const arc_info arc_game = { "wb3", "Wonder Boy III: Monster Lair", "Sega / Westone", 1988, W, H, FPS, RATE, kSets, { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_GOLDNAXE
static const char *const kSets[] = { "goldnaxe", NULL };
const arc_info arc_game = { "goldnaxe", "Golden Axe", "Sega", 1989, W, H, FPS, RATE, kSets, { "MAGIA", "ATAQUE", "SALTO" }, ARC_JOY_8WAY };
#elif GAME == G_DDUX
static const char *const kSets[] = { "ddux", NULL };
const arc_info arc_game = { "ddux", "Dynamite Dux", "Sega", 1989, W, H, FPS, RATE, kSets, { "ATAQUE", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_ESWAT
static const char *const kSets[] = { "eswat", NULL };
const arc_info arc_game = { "eswat", "E-Swat", "Sega", 1989, W, H, FPS, RATE, kSets, { "ESPECIAL", "DISPARO", "SALTO" }, ARC_JOY_8WAY };
#elif GAME == G_PASSSHT
static const char *const kSets[] = { "passsht", NULL };
const arc_info arc_game = { "passsht", "Passing Shot", "Sega", 1988, W, H, FPS, RATE, kSets, { "PLANO", "LIFTADO", "GLOBO" }, ARC_JOY_8WAY };
#elif GAME == G_AURAIL
static const char *const kSets[] = { "aurail", NULL };
const arc_info arc_game = { "aurail", "Aurail", "Sega / Westone", 1990, W, H, FPS, RATE, kSets, { "DISPARO", "GIRO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_RIOTCITY
static const char *const kSets[] = { "riotcity", NULL };
const arc_info arc_game = { "riotcity", "Riot City", "Sega / Westone", 1991, W, H, FPS, RATE, kSets, { "SALTO", "ATAQUE", NULL }, ARC_JOY_8WAY };
#elif GAME == G_SDI
static const char *const kSets[] = { "sdi", NULL };
const arc_info arc_game = { "sdi", "SDI - Strategic Defense Initiative", "Sega", 1987, W, H, FPS, RATE, kSets, { "DISPARO", "MIRA", NULL }, ARC_JOY_8WAY };
#elif GAME == G_COTTON
static const char *const kSets[] = { "cotton", NULL };
const arc_info arc_game = { "cotton", "Cotton", "Sega / Success", 1991, W, H, FPS, RATE, kSets, { "DISPARO", "BOMBA", "MAGIA" }, ARC_JOY_8WAY };
#elif GAME == G_BAYROUTE
static const char *const kSets[] = { "bayroute", NULL };
const arc_info arc_game = { "bayroute", "Bay Route", "Sega / Sunsoft", 1989, W, H, FPS, RATE, kSets, { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };
#elif GAME == G_SONICBOM
static const char *const kSets[] = { "sonicbom", NULL };
const arc_info arc_game = { "sonicbom", "Sonic Boom", "Sega", 1987, W, H, FPS, RATE, kSets, { "DISPARO", "BOMBA", NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "timescan", NULL };
const arc_info arc_game = { "timescan", "Time Scanner", "Sega", 1987, W, H, FPS, RATE, kSets, { "FLIPPER IZQ.", "FLIPPER DER.", NULL }, ARC_JOY_8WAY };
#endif

// The unprotected System 16B sets of Alien Syndrome (171-5358 small ROM board)
// and Wonder Boy III (171-5704), as MAME's current parents: not in dsp-cpp,
// used when its 16A sets are not there.
static const arc_rom kAliensyn16bMain[] = {
    {"epr-11083.a4", 0x8000, 0x00000, 0}, {"epr-11080.a1", 0x8000, 0x00001, 0},
    {"epr-11084.a5", 0x8000, 0x10000, 0}, {"epr-11081.a2", 0x8000, 0x10001, 0},
    {"epr-11085.a6", 0x8000, 0x20000, 0}, {"epr-11082.a3", 0x8000, 0x20001, 0},
};
static const arc_rom kAliensyn16bSound[] = {
    {"epr-10723.a7", 0x8000, 0x00000, 0}, {"epr-10724.a8", 0x8000, 0x08000, 0},
    {"epr-10725.a9", 0x8000, 0x10000, 0}, {"epr-10726.a10", 0x8000, 0x18000, 0},
};
static const arc_rom kAliensyn16bTiles[] = {
    {"epr-10702.b9", 0x10000, 0x00000, 0}, {"epr-10703.b10", 0x10000, 0x10000, 0}, {"epr-10704.b11", 0x10000, 0x20000, 0},
};
static const arc_rom kAliensyn16bSprites[] = {
    {"epr-10709.b1", 0x10000, 0x00001, 0}, {"epr-10713.b5", 0x10000, 0x00000, 0},
    {"epr-10710.b2", 0x10000, 0x20001, 0}, {"epr-10714.b6", 0x10000, 0x20000, 0},
    {"epr-10711.b3", 0x10000, 0x40001, 0}, {"epr-10715.b7", 0x10000, 0x40000, 0},
    {"epr-10712.b4", 0x10000, 0x60001, 0}, {"epr-10716.b8", 0x10000, 0x60000, 0},
};
static const arc_rom kWb316bMain[] = {
    {"epr-12259.a7", 0x20000, 0x00000, 0}, {"epr-12258.a5", 0x20000, 0x00001, 0},
};
static const arc_rom kWb316bSound[] = {{"epr-12127.a10", 0x8000, 0x00000, 0}};
static const arc_rom kWb316bTiles[] = {
    {"epr-12124.a14", 0x10000, 0x00000, 0}, {"epr-12125.a15", 0x10000, 0x10000, 0}, {"epr-12126.a16", 0x10000, 0x20000, 0},
};
static const arc_rom kWb316bSprites[] = {    // (one pair per even 128K bank: the odd banks empty)
    {"epr-12090.b1", 0x10000, 0x00001, 0}, {"epr-12094.b5", 0x10000, 0x00000, 0},
    {"epr-12091.b2", 0x10000, 0x40001, 0}, {"epr-12095.b6", 0x10000, 0x40000, 0},
    {"epr-12092.b3", 0x10000, 0x80001, 0}, {"epr-12096.b7", 0x10000, 0x80000, 0},
    {"epr-12093.b4", 0x10000, 0xc0001, 0}, {"epr-12097.b8", 0x10000, 0xc0000, 0},
};
static const arc_rom kSdi16aMain[] = {    // (SDI, MAME's System 16A parent: FD1089B 317-0027)
    {"epr-10970.43", 0x8000, 0x00000, 0}, {"epr-10968.26", 0x8000, 0x00001, 0},
    {"epr-10971.42", 0x8000, 0x10000, 0}, {"epr-10969.25", 0x8000, 0x10001, 0},
    {"epr-10755.41", 0x8000, 0x20000, 0}, {"epr-10752.24", 0x8000, 0x20001, 0},
};
static const arc_rom kSdi16aKey[] = {{"317-0027.key", 0x2000, 0, 0}};
static const arc_rom kSdi16aSound[] = {{"epr-10759.12", 0x8000, 0, 0}};
static const arc_rom kSdi16aTiles[] = {
    {"epr-10756.95", 0x10000, 0x00000, 0}, {"epr-10757.94", 0x10000, 0x10000, 0}, {"epr-10758.93", 0x10000, 0x20000, 0},
};
static const arc_rom kSdi16aSprites[] = {
    {"epr-10760.10", 0x10000, 0x00000, 0}, {"epr-10763.11", 0x10000, 0x00001, 0},
    {"epr-10761.17", 0x10000, 0x20000, 0}, {"epr-10764.18", 0x10000, 0x20001, 0},
    {"epr-10762.23", 0x10000, 0x40000, 0}, {"epr-10765.24", 0x10000, 0x40001, 0},
};

enum { SB_NONE, SB_5704, SB_5797, SB_5358 };
enum { R1_NONE, R1_ROM, R1_5797, R1_ROM_FD1089 };
enum { R2_NONE, R2_WRITE5704, R2_ROM, R2_ROM_FD1089 };

// the Sega 315-5195 memory mapper (dsp-cpp's sega_315_5195, sega_315_5195.pas)
typedef struct {
    uint8_t regs[0x20];
    uint32_t start[8], end[8];
    uint8_t from_sound, to_sound;
} mapper5195;

static struct state {
    m68kd cpu;
    zd_z80 snd;
    ym2151 ym;
    i8255d ppi;
    mapper5195 map;
    mcs51 mcu;
    mcs48 n7751;
    uint8_t n7751_rom[0x800];
    struct { uint8_t p2, p2out, prog, opcode, p[4]; } i8243;
    upd7759d upd;
    int32_t dac_sample;
    s16video video;
    fd1094d fd1094;
    uint16_t *rom, *rom_data, *sprite_rom;
    uint32_t rom_words, sprite_words;
    uint16_t ram[0x10000 / 2 * 2];
    uint8_t sound_mem[0x10000], sound_bank[16][0x4000];
    uint8_t *n7751_data;
    uint32_t n7751_data_size;
    uint32_t *frame, *bg_low, *bg_high, *fg_low, *fg_high, *text_low, *text_high;
    uint16_t fb[W * H];
    uint16_t in0, in1, in2;
    uint8_t dsw_a, dsw_b, dsw_c, sound_latch, sound_bank_num;
    int sprite_banks, tile_n, tile_color_shift, text_color_shift, text_code_mask;
    bool b16, n7751_on, alt16b, alt16a;
    int sound_clock;
    double fps;
    bool use_mcu, use_fd1089, use_fd1094, mb_type, sdi_io;
    int sound_bank_mode, region1_mode, region2_mode;
    uint32_t region1_rom_pos, region1_rom_mask, region2_rom_pos, region2_rom_mask;
    int sound_bank_count, sound_bank_base;
    uint8_t n7751_numroms, n7751_command;
    uint32_t n7751_rom_address;
    uint16_t s5248[2], s5250[16];
    uint8_t s5250_bit;
    uint8_t analog_x, analog_y, video_control, track[2][2];
    uint16_t sdi_joy;
    int64_t audio_acc;
    double main_debt, sound_debt, mcu_debt, n7751_debt, dc_in, dc_out, dac_lp1, dac_lp2;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

static uint16_t read_16b(uint32_t a);
static void write_16b(uint32_t a, uint16_t v, bool allow_mapper);

// ---------------------------------------------------------------------------
// The 315-5195

static void map_rebuild(void)
{
    static const uint32_t kSize[4] = {0x10000, 0x20000, 0x80000, 0x200000};
    for (int r = 0; r < 8; r++) {
        S.map.start[r] = (uint32_t)S.map.regs[0x11 + r * 2] << 16;
        S.map.end[r] = S.map.start[r] + kSize[S.map.regs[0x10 + r * 2] & 3];
    }
}

static void map_reset(void)
{
    memset(S.map.regs, 0, sizeof S.map.regs);
    S.map.from_sound = S.map.to_sound = 0;
    map_rebuild();
}

static inline bool map_contains(int r, uint32_t a) { return a >= S.map.start[r] && a < S.map.end[r]; }

static uint8_t map_read_reg(uint8_t a)
{
    a &= 0x1f;
    switch (a) {
    case 0: case 1: return S.map.regs[a];
    case 2: return ((S.map.regs[2] & 3) == 3) ? 0 : 0x0f;
    case 3: return S.map.from_sound;
    default: return (uint8_t)S.rom[(S.cpu.pc.l >> 1) % S.rom_words];   // (the open bus)
    }
}

static void map_write_reg(uint8_t a, uint8_t v)
{
    a &= 0x1f;
    const uint8_t old = S.map.regs[a];
    S.map.regs[a] = v;
    switch (a) {
    case 2:
        if ((old ^ v) & 3) m68kd_set_reset_line(&S.cpu, ((v & 3) == 3) ? M68_ASSERT : M68_CLEAR);
        break;
    case 3:
        S.map.to_sound = v;
        S.sound_latch = v;
        zd_set_irq(&S.snd, ZD_ASSERT, 0xff);
        break;
    case 4:
        if (v > 0 && v < 0x0f) m68kd_set_irq(&S.cpu, (~v) & 7, M68_HOLD);
        break;
    case 5:
        if (v == 1) {
            const uint32_t addr = ((uint32_t)S.map.regs[0x0a] << 17) | ((uint32_t)S.map.regs[0x0b] << 9) | ((uint32_t)S.map.regs[0x0c] << 1);
            write_16b(addr, (uint16_t)((S.map.regs[0] << 8) | S.map.regs[1]), false);
        } else if (v == 2) {
            const uint32_t addr = ((uint32_t)S.map.regs[7] << 17) | ((uint32_t)S.map.regs[8] << 9) | ((uint32_t)S.map.regs[9] << 1);
            const uint16_t w = read_16b(addr);
            S.map.regs[0] = (uint8_t)(w >> 8);
            S.map.regs[1] = (uint8_t)w;
        }
        break;
    default:
        if (a >= 0x10 && old != v) map_rebuild();
        break;
    }
}

// ---------------------------------------------------------------------------

static void configure_16b(void)
{
    S.use_mcu = S.use_fd1094 = S.use_fd1089 = S.mb_type = S.sdi_io = false;
    S.region1_mode = R1_NONE;
    S.region2_mode = R2_NONE;
    S.sound_bank_mode = SB_5704;
    S.sound_bank_count = 16;
    S.sound_bank_base = 0x8000;
    S.tile_color_shift = 6;
    S.text_color_shift = 9;
    S.text_code_mask = 0x1ff;
    S.tile_n = 4;
    S.sprite_banks = 8;
    S.dsw_b = 0xfd;
    S.video.tile_banks = 3;
    switch (GAME) {
    case G_ALTBEAST:
        S.use_mcu = true;
        S.region2_mode = R2_WRITE5704;
        break;
    case G_GOLDNAXE:
        S.use_mcu = true;
        S.region1_mode = R1_5797;
        S.sound_bank_mode = SB_5797;
        S.sound_bank_count = 8;
        S.sprite_banks = 16;
        break;
    case G_DDUX:
        S.use_fd1094 = true;
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x40000 >> 1;
        S.region1_rom_mask = 0x3ffff;
        S.region2_mode = R2_WRITE5704;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 4;
        S.dsw_b = 0xfe;
        break;
    case G_ESWAT:
        S.use_fd1094 = true;
        S.region1_mode = R1_5797;
        S.sound_bank_mode = SB_5797;
        S.sound_bank_count = 16;
        S.tile_n = 8;
        S.video.tile_banks = 7;
        S.sprite_banks = 16;
        break;
    case G_PASSSHT:
        S.use_fd1094 = true;
        S.sound_bank_mode = SB_5358;
        S.sound_bank_count = 16;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 3;
        S.dsw_b = 0xfe;
        S.mb_type = true;
        break;
    case G_AURAIL:
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x40000 >> 1;
        S.region1_rom_mask = 0x3ffff;
        S.region2_mode = R2_WRITE5704;
        S.sound_bank_count = 8;
        S.tile_n = 8;
        S.video.tile_banks = 7;
        S.sprite_banks = 16;
        break;
    case G_RIOTCITY:
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x40000 >> 1;
        S.region1_rom_mask = 0x3ffff;
        S.region2_mode = R2_WRITE5704;
        S.sound_bank_count = 8;
        S.sound_bank_base = 0x10000;
        S.tile_n = 8;
        S.video.tile_banks = 7;
        S.sprite_banks = 16;
        break;
    case G_SDI:
        S.use_fd1089 = true;
        S.region1_mode = R1_ROM_FD1089;
        S.region1_rom_pos = 0x10000 >> 1;
        S.region1_rom_mask = 0xffff;
        S.region2_mode = R2_ROM_FD1089;
        S.region2_rom_pos = 0x20000 >> 1;
        S.region2_rom_mask = 0x1ffff;
        S.sound_bank_mode = SB_NONE;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 3;
        S.mb_type = true;
        S.sdi_io = true;
        break;
    case G_COTTON:
        S.use_fd1094 = true;
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x40000 >> 1;
        S.region1_rom_mask = 0x3ffff;
        S.region2_mode = R2_WRITE5704;
        S.sound_bank_count = 8;
        S.tile_n = 8;
        S.video.tile_banks = 7;
        S.sprite_banks = 16;
        S.dsw_b = 0xfe;
        break;
    case G_BAYROUTE:
        S.use_fd1094 = true;
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x40000 >> 1;
        S.region1_rom_mask = 0x3ffff;
        S.region2_mode = R2_WRITE5704;
        S.sound_bank_count = 16;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 4;
        S.dsw_b = 0xbd;
        break;
    case G_SONICBOM:
        S.use_fd1094 = true;
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x20000 >> 1;
        S.region1_rom_mask = 0x1ffff;
        S.sound_bank_mode = SB_5358;
        S.sound_bank_count = 4;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 4;
        S.dsw_b = 0x7f;
        S.mb_type = true;
        break;
    case G_ALIENSYN:                            // (the 16B set: 171-5358 small)
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x10000 >> 1;
        S.region1_rom_mask = 0xffff;
        S.region2_mode = R2_ROM;
        S.region2_rom_pos = 0x20000 >> 1;
        S.region2_rom_mask = 0xffff;
        S.sound_bank_mode = SB_5358;
        S.sound_bank_count = 8;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 4;
        S.mb_type = true;
        break;
    case G_WB3:                                 // (the 16B set: 171-5704)
        S.region2_mode = R2_WRITE5704;
        S.dsw_b = 0x7c;
        S.sound_bank_mode = SB_NONE;
        S.tile_n = 2;
        S.video.tile_banks = 1;
        S.sprite_banks = 8;
        break;
    case G_TIMESCAN:
        S.region1_mode = R1_ROM;
        S.region1_rom_pos = 0x10000 >> 1;
        S.region1_rom_mask = 0xffff;
        S.region2_mode = R2_ROM;
        S.region2_rom_pos = 0x20000 >> 1;
        S.region2_rom_mask = 0xffff;
        S.sound_bank_mode = SB_5358;
        S.sound_bank_count = 2;
        S.tile_n = 1;
        S.video.tile_banks = 1;
        S.sprite_banks = 4;
        S.dsw_b = 0xff;
        S.dsw_c = 0xff;
        S.mb_type = true;
        S.tile_color_shift = 5;
        S.text_color_shift = 8;
        S.text_code_mask = 0xff;
        break;
    default: break;
    }
}

static uint8_t calc_sound_bank(uint8_t v)
{
    switch (S.sound_bank_mode) {
    case SB_5797: return (uint8_t)((v & 7) | ((v & 0x10) >> 1));
    case SB_5358: return (uint8_t)((v & 3) + ((~v & 0x38) >> 1));
    default: return (uint8_t)(v & 0xf);
    }
}

static void exec_5250(bool history)            // the 315-5250 compare / timer chip
{
    const int16_t b1 = (int16_t)S.s5250[0], b2 = (int16_t)S.s5250[1], value = (int16_t)S.s5250[2];
    const int16_t minv = b1 < b2 ? b1 : b2, maxv = b1 > b2 ? b1 : b2;
    if (value < minv) {
        S.s5250[7] = (uint16_t)minv;
        S.s5250[3] = 0x8000;
    } else if (value > maxv) {
        S.s5250[7] = (uint16_t)maxv;
        S.s5250[3] = 0x4000;
    } else {
        S.s5250[7] = (uint16_t)value;
        S.s5250[3] = 0;
    }
    if (history) {
        S.s5250[4] = (uint16_t)(S.s5250[4] | ((uint16_t)(S.s5250[3] == 0) << S.s5250_bit));
        S.s5250_bit++;
    }
}

// ---------------------------------------------------------------------------
// I/O

static uint16_t io_16a(uint16_t a)
{
    switch (a & 0x3000) {
    case 0x0000: return i8255d_read(&S.ppi, (a >> 1) & 3);
    case 0x1000:
        if (S.alt16a) {                         // (SDI on 16A: the trackballs, muxed by the 8255's port B bit 2)
            switch ((a >> 1) & 3) {
            case 0: return S.in0;
            case 1: return (S.video_control & 4) ? S.track[0][1] : S.track[0][0];
            case 2: return S.sdi_joy;
            default: return (S.video_control & 4) ? S.track[1][1] : S.track[1][0];
            }
        }
        switch (a & 7) {
        case 0: case 1: return S.in0;
        case 2: case 3: return S.in1;
        case 6: case 7: return S.in2;
        default: return 0xffff;
        }
    case 0x2000: return (a & 2) ? S.dsw_b : S.dsw_a;
    default: return 0xffff;
    }
}

static uint16_t io_sdi(uint16_t a)
{
    switch (a & 0x1800) {
    case 0x0800:
        switch (a & 3) {
        case 0: return S.in0;
        case 2: return (uint16_t)((S.in1 >> 4) | (S.in2 & 0xf0));
        default: return 0xffff;
        }
    case 0x1000: return (a & 1) ? S.dsw_a : S.dsw_b;
    case 0x1800:
        switch ((a >> 1) & 3) {
        case 0: return S.analog_x;
        case 1: return S.analog_y;
        default: return 0xffff;
        }
    default: return 0xffff;
    }
}

static uint16_t io_16b(uint16_t a)
{
    if (S.sdi_io) return io_sdi(a);
    switch (a & 0x1800) {
    case 0x0800:
        switch (a & 3) {
        case 0: return S.in0;
        case 1: return S.in1;
        case 2: return GAME == G_TIMESCAN ? S.dsw_c : 0xffff;
        default: return S.in2;
        }
    case 0x1000: return (a & 1) ? S.dsw_a : S.dsw_b;
    default: return 0xffff;
    }
}

// ---------------------------------------------------------------------------
// The 68000: 16B (the 315-5195's regions)

static uint16_t read_region0(uint32_t a)
{
    const size_t i = (a >> 1) % (S.rom_words ? S.rom_words : 1);
    if (S.use_fd1094 && S.cpu.opcode) return fd1094d_opcodes(&S.fd1094, S.rom, S.rom_words * 2)[i];
    if (S.use_fd1089 && S.rom_data) return S.cpu.opcode ? S.rom[i] : S.rom_data[i];
    return S.rom[i];
}

static uint16_t read_region1(uint32_t a)
{
    switch (S.region1_mode) {
    case R1_ROM: {
        const uint32_t w = ((a & S.region1_rom_mask) >> 1) + S.region1_rom_pos;
        return w >= S.rom_words ? 0xffff : S.rom[w];
    }
    case R1_ROM_FD1089: {
        const uint32_t w = ((a & S.region1_rom_mask) >> 1) + S.region1_rom_pos;
        if (w >= S.rom_words) return 0xffff;
        if (S.use_fd1089 && S.rom_data) return S.cpu.opcode ? S.rom[w] : S.rom_data[w];
        return S.rom[w];
    }
    case R1_5797: {
        const uint32_t dir = (a >> 1) & 0x1fff;
        switch (dir & 0x1800) {
        case 0x0000:
            switch (dir & 3) {
            case 0: return S.s5248[0];
            case 1: return S.s5248[1];
            case 2: return (uint16_t)(((int32_t)(int16_t)S.s5248[0] * (int16_t)S.s5248[1]) >> 16);
            default: return (uint16_t)((int32_t)(int16_t)S.s5248[0] * (int16_t)S.s5248[1]);
            }
        case 0x0800: return (dir & 0xf) <= 7 ? S.s5250[dir & 0xf] : 0xffff;
        default: return 0xffff;
        }
    }
    default: return 0xffff;
    }
}

static uint16_t read_region2(uint32_t a)
{
    if (S.region2_mode != R2_ROM && S.region2_mode != R2_ROM_FD1089) return 0xffff;
    const uint32_t w = ((a & S.region2_rom_mask) >> 1) + S.region2_rom_pos;
    if (w >= S.rom_words) return 0xffff;
    if (S.region2_mode == R2_ROM_FD1089 && S.use_fd1089 && S.rom_data) return S.cpu.opcode ? S.rom[w] : S.rom_data[w];
    return S.rom[w];
}

static void set_tile_bank(int index, uint16_t v)
{
    const uint8_t bank = (uint8_t)((v & 7) & S.video.tile_banks);
    if (S.video.tile_bank[index] != bank) {
        S.video.tile_bank[index] = bank;
        memset(S.video.tile_dirty, 1, sizeof S.video.tile_dirty);
    }
}

static void write_region1(uint32_t a, uint16_t v)
{
    if (S.region1_mode != R1_5797) return;
    const uint32_t dir = (a >> 1) & 0x1fff;
    switch (dir & 0x1800) {
    case 0x0000: S.s5248[dir & 1] = v; break;
    case 0x0800:
        switch (dir & 15) {
        case 0: case 1:
            S.s5250[dir & 0xf] = v;
            exec_5250(false);
            break;
        case 2:
            S.s5250[2] = v;
            exec_5250(true);
            break;
        case 4:
            S.s5250[4] = 0;
            S.s5250_bit = 0;
            break;
        case 6:
            S.s5250[2] = v;
            exec_5250(false);
            break;
        case 8: case 0xc: S.s5250[8] = v; break;
        case 0xa: case 0xe: S.s5250[10] = v; break;
        case 0xb: case 0xf: S.s5250[11] = v; break;
        default: break;
        }
        break;
    case 0x1000: set_tile_bank((int)(dir & 1), v); break;
    default: break;
    }
}

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
}

static uint16_t read_16b(uint32_t a)
{
    a &= 0xffffff;
    if (map_contains(0, a)) return read_region0(a);
    bool mapped = false;
    uint16_t r = 0xffff;
    if (map_contains(1, a)) { r = read_region1(a); mapped = true; }
    if (map_contains(2, a)) { r = read_region2(a); mapped = true; }
    if (map_contains(3, a)) { r = S.ram[(a & 0xffff) >> 1]; mapped = true; }
    if (map_contains(4, a)) { r = S.video.sprite_ram[(a & 0x7ff) >> 1]; mapped = true; }
    if (map_contains(5, a)) {
        r = (a & 0x1ffff) <= 0xffff ? S.video.tile_ram[(a & 0xffff) >> 1] : S.video.char_ram[(a & 0xfff) >> 1];
        mapped = true;
    }
    if (map_contains(6, a)) { r = S.video.pal_ram[(a & 0xfff) >> 1]; mapped = true; }
    if (map_contains(7, a)) { r = io_16b((uint16_t)((a >> 1) & 0x1fff)); mapped = true; }
    if (!mapped) r = map_read_reg((uint8_t)((a >> 1) & 0x1f));
    return r;
}

static void mapper_write_reg_watched(uint8_t reg, uint8_t v)
{
    const uint32_t old = S.map.start[5];
    map_write_reg(reg, v);
    if (old != S.map.start[5]) memset(S.video.tile_dirty, 0, sizeof S.video.tile_dirty);   // (as dsp-cpp: false)
}

static void write_16b(uint32_t a, uint16_t v, bool allow_mapper)
{
    a &= 0xffffff;
    bool mapped = false;
    if (map_contains(0, a)) mapped = true;
    if (map_contains(1, a)) { write_region1(a, v); mapped = true; }
    if (map_contains(2, a)) {
        if (S.region2_mode == R2_WRITE5704) set_tile_bank((int)((a & 3) >> 1), v);
        mapped = true;
    }
    if (map_contains(3, a)) { S.ram[(a & 0xffff) >> 1] = v; mapped = true; }
    if (map_contains(4, a)) { S.video.sprite_ram[(a & 0x7ff) >> 1] = v; mapped = true; }
    if (map_contains(5, a)) {
        if ((a & 0x1ffff) <= 0xffff) tile_write((uint16_t)((a & 0xffff) >> 1), v);
        else {
            const uint16_t o = (uint16_t)((a & 0xfff) >> 1);
            char_write(o, v);
            s16_screen_select_16b(&S.video, o);
        }
        mapped = true;
    }
    if (map_contains(6, a)) { s16_set_palette_entry(&S.video, (int)((a & 0xfff) >> 1), v, true); mapped = true; }
    if (map_contains(7, a)) {
        if (((a & 0x1fff) >> 1) == 0) S.video.screen_enabled = (v & 0x20) != 0;
        mapped = true;
    }
    if (!mapped && allow_mapper) mapper_write_reg_watched((uint8_t)((a >> 1) & 0x1f), (uint8_t)v);
}

// ---------------------------------------------------------------------------
// The 68000: 16A

static uint16_t read_16a(uint32_t a)
{
    a &= 0xffffff;
    if (a <= 0x3fffff) {
        const size_t i = (a & 0x3ffff) >> 1;
        if (S.use_fd1094 && S.cpu.opcode) return fd1094d_opcodes(&S.fd1094, S.rom, S.rom_words * 2)[i % S.rom_words];
        if (S.use_fd1089 && S.rom_data) return S.cpu.opcode ? S.rom[i % S.rom_words] : S.rom_data[i % S.rom_words];
        return S.rom[i % S.rom_words];
    }
    if (a <= 0x7fffff) {
        const uint32_t o = a & 0x7ffff;
        if (o <= 0x0ffff) return S.video.tile_ram[(a & 0x7fff) >> 1];
        if (o <= 0x1ffff) return S.video.char_ram[(a & 0xfff) >> 1];
        if (o >= 0x40000) return S.video.sprite_ram[(a & 0x7ff) >> 1];
        return 0xffff;
    }
    if (a <= 0xbfffff) {
        if ((a & 0xfffff) >= 0x40000 && (a & 0xfffff) <= 0x7ffff) return S.video.pal_ram[(a & 0xfff) >> 1];
        return 0xffff;
    }
    const uint32_t o = a & 0x7ffff;
    if (o <= 0x0ffff) return S.video.tile_ram[(a & 0x7fff) >> 1];
    if (o <= 0x1ffff) return S.video.char_ram[(a & 0xfff) >> 1];
    if (o >= 0x40000 && o <= 0x5ffff) return io_16a((uint16_t)(a & 0x3fff));
    if (o >= 0x70000) return S.ram[(a & 0x3fff) >> 1];
    return 0xffff;
}

static void write_16a(uint32_t a, uint16_t v)
{
    a &= 0xffffff;
    if (a >= 0x400000 && a <= 0x7fffff) {
        const uint32_t o = a & 0x7ffff;
        if (o <= 0x0ffff) tile_write((uint16_t)((a & 0x7fff) >> 1), v);
        else if (o <= 0x1ffff) {
            const uint16_t off = (uint16_t)((a & 0xfff) >> 1);
            char_write(off, v);
            s16_screen_select_16a(&S.video, off);
        } else if (o >= 0x40000) S.video.sprite_ram[(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x800000 && a <= 0xbfffff) {
        if ((a & 0xfffff) >= 0x40000 && (a & 0xfffff) <= 0x7ffff)
            s16_set_palette_entry(&S.video, (int)((a & 0xfff) >> 1), v, false);
        return;
    }
    if (a >= 0xc00000) {
        const uint32_t o = a & 0x7ffff;
        if (o <= 0x0ffff) tile_write((uint16_t)((a & 0x7fff) >> 1), v);
        else if (o <= 0x1ffff) {
            const uint16_t off = (uint16_t)((a & 0xfff) >> 1);
            char_write(off, v);
            s16_screen_select_16a(&S.video, off);
        } else if (o >= 0x40000 && o <= 0x5ffff) {
            if ((a & 0x3000) == 0) i8255d_write(&S.ppi, (int)((a >> 1) & 3), (uint8_t)v);
        } else if (o >= 0x70000) S.ram[(a & 0x3fff) >> 1] = v;
    }
}

static uint16_t cpu_rd(void *u, uint32_t a) { (void)u; return IS_16B ? read_16b(a) : read_16a(a); }
static void cpu_wr(void *u, uint32_t a, uint16_t v)
{
    (void)u;
    if (IS_16B) write_16b(a, v, !S.use_mcu);
    else write_16a(a, v);
}
static void cpu_cmpild(void *u, uint8_t reg, uint32_t imm) { (void)u; fd1094d_on_cmpild(&S.fd1094, reg, imm); }
static void cpu_rte(void *u) { (void)u; fd1094d_on_rte(&S.fd1094); }
static void cpu_irq_taken(void *u, int level) { (void)u; (void)level; fd1094d_on_irq(&S.fd1094); }

// ---------------------------------------------------------------------------
// The sound Z80

static uint8_t sound_data_r(void)
{
    i8255d_ack_a(&S.ppi);                       // (the latch read strobes /ACK: the NMI goes)
    return S.sound_latch;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (IS_16B) {
        if (a <= 0x7fff) return S.sound_mem[a];
        if (a <= 0xdfff) return S.sound_bank[S.sound_bank_num & 0xf][a & 0x3fff];
        if (a == 0xe800) {
            zd_set_irq(&S.snd, ZD_CLEAR, 0xff);
            return S.sound_latch;
        }
        if (a >= 0xf800) return S.sound_mem[a];
        return 0xff;
    }
    if (a <= 0x7fff || a >= 0xf800) return S.sound_mem[a];
    if (a == 0xe800) return sound_data_r();
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xf800) S.sound_mem[a] = v;
}

static uint8_t sound_in(void *u, uint16_t port)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p <= 0x3f && (p & 1)) return ym2151_status(&S.ym);
    if (IS_16B) {
        if (p >= 0x80 && p <= 0xbf) return (uint8_t)(upd7759d_busy_r(&S.upd) << 7);
        if (p >= 0xc0) {
            zd_set_irq(&S.snd, ZD_CLEAR, 0xff);
            return S.sound_latch;
        }
    } else if (p >= 0xc0) return sound_data_r();
    return 0xff;
}

static void sound_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const uint8_t p = (uint8_t)port;
    if (p <= 0x3f) {
        if ((p & 1) == 0) ym2151_select(&S.ym, v);
        else ym2151_write(&S.ym, v);
        return;
    }
    if (IS_16B && p >= 0x40 && p <= 0x7f) {
        upd7759d_start_w(&S.upd, (uint8_t)((v >> 7) & 1));
        upd7759d_reset_w(&S.upd, (uint8_t)((v >> 6) & 1));
        S.sound_bank_num = calc_sound_bank(v);
        return;
    }
    if (IS_16B && p >= 0x80 && p <= 0xbf) {
        upd7759d_port_w(&S.upd, v);
        return;
    }
    if (USES_N7751 && p >= 0x80 && p <= 0xbf) {
        S.n7751_rom_address = (S.n7751_rom_address & 0x3fff) | ((uint32_t)(v & 1) << 14);
        if ((v & 0x04) == 0 && S.n7751_numroms >= 2) S.n7751_rom_address |= 0x8000;
        if ((v & 0x08) == 0 && S.n7751_numroms >= 3) S.n7751_rom_address |= 0x10000;
        if ((v & 0x10) == 0 && S.n7751_numroms >= 4) S.n7751_rom_address |= 0x18000;
        S.n7751_command = (uint8_t)(v >> 5);
    }
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&S.ym, IS_16B ? (int)(((int64_t)cycles * 4000000) / SOUND_CLOCK) : cycles);
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        double mix = (double)ym2151_update(&S.ym) * 0.43;
        if (IS_16B) mix += (double)upd7759d_update(&S.upd) * 0.48;
        if (USES_N7751) {                       // (a two-pole ~4 kHz low-pass for the DAC)
            const double alpha = 0.43;
            S.dac_lp1 += alpha * ((double)S.dac_sample - S.dac_lp1);
            S.dac_lp2 += alpha * (S.dac_lp1 - S.dac_lp2);
            mix += S.dac_lp2 * 0.4;
        }
        const double hp = mix - S.dc_in + 0.9985 * S.dc_out;   // (the amplifier's AC coupling)
        S.dc_in = mix;
        S.dc_out = hp;
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16((int32_t)lround(hp * MIX_GAIN));
    }
}

static void ym_port(void *u, uint8_t v)
{
    (void)u;
    S.n7751.reset_line = (v & 0x01) == 0;
    mcs48_set_irq(&S.n7751, (v & 0x02) == 0);
}

static void upd_drq(void *u, uint8_t v) { (void)u; if (v & 1) zd_set_nmi(&S.snd, ZD_PULSE); }

// the N7751 and its 8243
static void i8243_write(int port, uint8_t v)
{
    const uint32_t mask = (0x0fu << (4 * port)) & 0x3fff;
    S.n7751_rom_address = (S.n7751_rom_address & ~mask) | (((uint32_t)v << (4 * port)) & mask);
}

static void i8243_prog_w(uint8_t v)
{
    v &= 1;
    if (S.i8243.prog != 0 && v == 0) {
        S.i8243.opcode = S.i8243.p2;
        if ((S.i8243.opcode >> 2) == 0) S.i8243.p2out = (uint8_t)(S.i8243.p[S.i8243.opcode & 3] & 0x0f);   // (no read handler)
    } else if (S.i8243.prog == 0 && v != 0) {
        const int port = S.i8243.opcode & 3;
        switch (S.i8243.opcode >> 2) {
        case 1: S.i8243.p[port] = (uint8_t)(S.i8243.p2 & 0x0f); i8243_write(port, S.i8243.p[port]); break;
        case 2: S.i8243.p[port] = (uint8_t)(S.i8243.p[port] | (S.i8243.p2 & 0x0f)); i8243_write(port, S.i8243.p[port]); break;
        case 3: S.i8243.p[port] = (uint8_t)(S.i8243.p[port] & (S.i8243.p2 & 0x0f)); i8243_write(port, S.i8243.p[port]); break;
        default: break;
        }
    }
    S.i8243.prog = v;
}

static uint8_t n7751_in(void *u, uint16_t port)
{
    (void)u;
    switch (port) {
    case MCS48_BUS: return S.n7751_data ? S.n7751_data[S.n7751_rom_address % S.n7751_data_size] : 0;
    case MCS48_T1: return 0;
    case MCS48_P2: return (uint8_t)(0x80 | ((S.n7751_command & 7) << 4) | (S.i8243.p2out & 0x0f));
    default: return 0xff;
    }
}

static void n7751_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    switch (port) {
    case MCS48_P1: S.dac_sample = (int32_t)(((int)v - 0x80) * 256); break;
    case MCS48_P2: S.i8243.p2 = (uint8_t)(v & 0x0f); break;
    case MCS48_PROG: i8243_prog_w(v); break;
    default: break;
    }
}

// the 8255 (16A)
static void ppi_a(void *u, uint8_t v) { (void)u; S.sound_latch = v; }
static void ppi_b(void *u, uint8_t v)
{
    (void)u;
    S.video_control = v;
    S.video.screen_enabled = (v & 0x10) != 0;
}
static void ppi_c(void *u, uint8_t v) { (void)u; zd_set_nmi(&S.snd, (v & 0x80) ? ZD_CLEAR : ZD_ASSERT); }

// the i8751 (16B)
static uint8_t mcu_ext_r(void *u, uint16_t a) { (void)u; return map_read_reg((uint8_t)(a & 0x1f)); }
static void mcu_ext_w(void *u, uint16_t a, uint8_t v) { (void)u; mapper_write_reg_watched((uint8_t)(a & 0x1f), v); }
static uint8_t mcu_p1_r(void *u) { (void)u; return (uint8_t)S.in0; }

// ---------------------------------------------------------------------------
// Video

static void update_video(void)
{
    s16video *v = &S.video;
    uint32_t *f = S.frame;
    if (!v->screen_enabled) {
        const uint32_t blank = IS_16B ? v->palette[0] : v->palette[0x1fff];
        for (int i = 0; i < NW * NH; i++) f[i] = blank;
    } else if (IS_16B) {
        for (int i = 0; i < NW * NH; i++) f[i] = v->palette[0];
        const int cs = S.tile_color_shift;
        s16_draw_tilemap_16b(v, f, NULL, 1, -1, 0, true, cs, 0x1fff, true);
        s16_draw_sprites_16b(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 0, 0x800);
        s16_draw_tilemap_16b(v, f, NULL, 1, 1, 0, false, cs, 0x1fff, true);
        s16_draw_sprites_16b(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 1, 0x800);
        s16_draw_tilemap_16b(v, f, NULL, 0, 0, 0, false, cs, 0x1fff, true);
        s16_draw_sprites_16b(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 2, 0x800);
        s16_draw_tilemap_16b(v, f, NULL, 0, 1, 0, false, cs, 0x1fff, true);
        s16_draw_text_16b(v, f, NULL, 0, 0, S.text_color_shift, S.text_code_mask, true);
        s16_draw_sprites_16b(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 3, 0x800);
        s16_draw_text_16b(v, f, NULL, 1, 0, S.text_color_shift, S.text_code_mask, true);
    } else {
        s16_render_tile_pages(v, S.bg_low, S.bg_high, 0, false, 5, 0x1fff, 0x1000, false, true);
        s16_render_tile_pages(v, S.fg_low, S.fg_high, 4, true, 5, 0x1fff, 0x1000, false, true);
        s16_render_text(v, S.text_low, S.text_high, 8, 0xff, 0x800, false);
        const int sx1 = (0xc8 - (v->char_ram[0x7fd] & 0x1ff)) & 0x3ff, sy1 = v->char_ram[0x793] & 0xff;
        const int sx2 = (0xc8 - (v->char_ram[0x7fc] & 0x1ff)) & 0x3ff, sy2 = v->char_ram[0x792] & 0xff;
        s16_blit_scrolled(v, f, S.bg_low, sx1, sy1, 1024, 512);
        s16_draw_sprites_16a(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 0, 0x400, 0x1000);
        s16_blit_scrolled(v, f, S.bg_high, sx1, sy1, 1024, 512);
        s16_draw_sprites_16a(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 1, 0x400, 0x1000);
        s16_blit_scrolled(v, f, S.fg_low, sx2, sy2, 1024, 512);
        s16_draw_sprites_16a(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 2, 0x400, 0x1000);
        s16_blit_scrolled(v, f, S.fg_high, sx2, sy2, 1024, 512);
        s16_blit_text(v, f, S.text_low);
        s16_draw_sprites_16a(v, f, S.sprite_rom, S.sprite_words, S.sprite_banks, 3, 0x400, 0x1000);
        s16_blit_text(v, f, S.text_high);
    }
#if ROTATED
    for (int y = 0; y < NH; y++)                // (ROT270)
        for (int x = 0; x < NW; x++) S.fb[(NW - 1 - x) * NH + y] = s16_to565(f[y * NW + x]);
#else
    for (int i = 0; i < NW * NH; i++) S.fb[i] = s16_to565(f[i]);
#endif
}

// ---------------------------------------------------------------------------
// ROMs (as dsp-cpp's sega16 helpers)

static uint32_t list_span(const arc_rom *l, int n, int mul, uint32_t lane_mask)
{
    uint32_t bytes = 0;
    for (int i = 0; i < n; i++) {
        const uint32_t e = (l[i].offset & ~lane_mask) + l[i].size * (uint32_t)mul;
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
    const uint32_t bytes = list_span(l, n, 1, 0);
    uint8_t *d = (uint8_t *)calloc(1, bytes ? bytes : 1);
    if (d && !arc_load(l, n, d, bytes)) {
        free(d);
        d = NULL;
    }
    if (size) *size = bytes;
    return d;
}

static uint16_t *load16w(const arc_rom *l, int n, uint32_t *words)   // even offset: the high byte
{
    *words = list_span(l, n, 2, 1) / 2;
    uint16_t *d = (uint16_t *)calloc(*words ? *words : 1, 2);
    uint8_t *part = (uint8_t *)malloc(0x40000);
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

static uint8_t *load_interleaved_bytes(const arc_rom *l, int n, uint32_t *bytes)   // byte i to offset + 2 * i
{
    *bytes = list_span(l, n, 2, 0);
    uint8_t *raw = (uint8_t *)calloc(1, *bytes), *part = (uint8_t *)malloc(0x40000);
    bool ok = raw && part;
    for (int i = 0; ok && i < n; i++) {
        if (!(ok = load_part(&l[i], part))) break;
        for (uint32_t j = 0; j < l[i].size; j++) raw[l[i].offset + j * 2] = part[j];
    }
    free(part);
    if (!ok) {
        free(raw);
        return NULL;
    }
    return raw;
}

static void remap_sprites_5797(void)            // each 0x40000-word pair split at +0 / +0x100000
{
    uint16_t *t = (uint16_t *)calloc(0x100000, 2);
    if (!t) return;
    const uint32_t half = 0x20000, src[3] = {0, 0x40000, 0x80000}, lo[3] = {0, 0x20000, 0x40000},
                   hi[3] = {0x80000, 0xa0000, 0xc0000};
    for (int p = 0; p < 3; p++) {
        if (src[p] + half > S.sprite_words) break;
        for (uint32_t i = 0; i < half; i++) {
            t[lo[p] + i] = S.sprite_rom[src[p] + i];
            if (src[p] + half + i < S.sprite_words) t[hi[p] + i] = S.sprite_rom[src[p] + half + i];
        }
    }
    free(S.sprite_rom);
    S.sprite_rom = t;
    S.sprite_words = 0x100000;
}

static bool load_roms(void)
{
    const arc_rom *main = NULL, *sound = NULL, *tiles = NULL, *sprites = NULL, *key = NULL, *mcu = NULL, *n7751 = NULL;
    int nmain = 0, nsound = 0, ntiles = 0, nsprites = 0, nkey = 0, nmcu = 0, nn7751 = 0;
    int fd_type = FD1089_B;
    bool scramble = false, remap = false;
#define SET(v, k) (v = k, n##v = N(k))
    switch (GAME) {
    case G_FANTZONE:
        SET(main, kFantzoneMain); SET(sound, kFantzoneSound); SET(tiles, kFantzoneTiles); SET(sprites, kFantzoneSprites);
        S.tile_n = 1; S.sprite_banks = 4; S.dsw_b = 0xfc;
        break;
    case G_SHINOBI:
        SET(main, kShinobiMain); SET(sound, kShinobiSound); SET(tiles, kShinobiTiles); SET(sprites, kShinobiSprites);
        SET(n7751, kShinobiN7751); S.n7751_numroms = 1;
        S.tile_n = 2; S.sprite_banks = 8; S.dsw_b = 0xfc; scramble = true;
        break;
    case G_ALEXKIDD:
        SET(main, kAlexkiddMain); SET(sound, kAlexkiddSound); SET(tiles, kAlexkiddTiles); SET(sprites, kAlexkiddSprites);
        SET(n7751, kAlexkiddN7751); S.n7751_numroms = 2;
        S.tile_n = 1; S.sprite_banks = 4; S.dsw_b = 0xfc;
        break;
    case G_ALIENSYN:
        if (S.alt16b) {
            SET(main, kAliensyn16bMain); SET(sound, kAliensyn16bSound); SET(tiles, kAliensyn16bTiles);
            SET(sprites, kAliensyn16bSprites);
            break;
        }
        SET(main, kAliensynMain); SET(sound, kAliensynSound); SET(tiles, kAliensynTiles); SET(sprites, kAliensynSprites);
        SET(key, kAliensynKey); SET(n7751, kAliensynN7751); S.n7751_numroms = 3;
        fd_type = FD1089_B; S.tile_n = 2; S.sprite_banks = 8; S.dsw_b = 0xfd; scramble = true;
        break;
    case G_WB3:
        if (S.alt16b) {
            SET(main, kWb316bMain); SET(sound, kWb316bSound); SET(tiles, kWb316bTiles); SET(sprites, kWb316bSprites);
            break;
        }
        SET(main, kWb3Main); SET(sound, kWb3Sound); SET(tiles, kWb3Tiles); SET(sprites, kWb3Sprites); SET(key, kWb3Key);
        fd_type = FD1089_A; S.tile_n = 2; S.sprite_banks = 8; S.dsw_b = 0x7c; scramble = true;
        break;
    case G_TETRIS:
        SET(main, kTetrisMain); SET(sound, kTetrisSound); SET(tiles, kTetrisTiles); SET(sprites, kTetrisSprites);
        S.tile_n = 2; S.sprite_banks = 1; S.dsw_b = 0x30;
        break;
    case G_ALTBEAST:
        SET(main, kAltbeastMain); SET(sound, kAltbeastSound); SET(tiles, kAltbeastTiles); SET(sprites, kAltbeastSprites);
        SET(mcu, kAltbeastMcu);
        break;
    case G_GOLDNAXE:
        SET(main, kGoldnaxeMain); SET(sound, kGoldnaxeSound); SET(tiles, kGoldnaxeTiles); SET(sprites, kGoldnaxeSprites);
        SET(mcu, kGoldnaxeMcu); remap = true;
        break;
    case G_DDUX:
        SET(main, kDduxMain); SET(sound, kDduxSound); SET(tiles, kDduxTiles); SET(sprites, kDduxSprites); SET(key, kDduxKey);
        break;
    case G_ESWAT:
        SET(main, kEswatMain); SET(sound, kEswatSound); SET(tiles, kEswatTiles); SET(sprites, kEswatSprites); SET(key, kEswatKey);
        remap = true;
        break;
    case G_PASSSHT:
        SET(main, kPassshtMain); SET(sound, kPassshtSound); SET(tiles, kPassshtTiles); SET(sprites, kPassshtSprites);
        SET(key, kPassshtKey);
        break;
    case G_AURAIL:
        SET(main, kAurailMain); SET(sound, kAurailSound); SET(tiles, kAurailTiles); SET(sprites, kAurailSprites);
        break;
    case G_RIOTCITY:
        SET(main, kRiotcityMain); SET(sound, kRiotcitySound); SET(tiles, kRiotcityTiles); SET(sprites, kRiotcitySprites);
        remap = true;
        break;
    case G_SDI:
        if (S.alt16a) {
            SET(main, kSdi16aMain); SET(sound, kSdi16aSound); SET(tiles, kSdi16aTiles); SET(sprites, kSdi16aSprites);
            SET(key, kSdi16aKey);
            fd_type = FD1089_B; S.tile_n = 2; S.sprite_banks = 8; scramble = true;
            break;
        }
        SET(main, kSdiMain); SET(sound, kSdiSound); SET(tiles, kSdiTiles); SET(sprites, kSdiSprites); SET(key, kSdiKey);
        fd_type = FD1089_A;
        break;
    case G_COTTON:
        SET(main, kCottonMain); SET(sound, kCottonSound); SET(tiles, kCottonTiles); SET(sprites, kCottonSprites);
        SET(key, kCottonKey);
        break;
    case G_BAYROUTE:
        SET(main, kBayrouteMain); SET(sound, kBayrouteSound); SET(tiles, kBayrouteTiles); SET(sprites, kBayrouteSprites);
        SET(key, kBayrouteKey);
        break;
    case G_SONICBOM:
        SET(main, kSonicbomMain); SET(sound, kSonicbomSound); SET(tiles, kSonicbomTiles); SET(sprites, kSonicbomSprites);
        SET(key, kSonicbomKey);
        break;
    default:
        SET(main, kTimescanMain); SET(sound, kTimescanSound); SET(tiles, kTimescanTiles); SET(sprites, kTimescanSprites);
        break;
    }
#undef SET
    (void)nmcu;
    uint32_t n;
    if (GAME == G_TETRIS) {
        if ((S.rom = load16w(main, nmain, &S.rom_words))) S.use_fd1089 = false;   // (a decrypted set)
        else {
            // (MAME's tetris: an FD1094 317-0093; dsp-cpp's FD1089 path for it boots to a black screen)
            if (!(S.rom = load16w(kTetrisEncrypted, N(kTetrisEncrypted), &S.rom_words))) return false;
            uint8_t *k = load_bytes(kTetrisKey, N(kTetrisKey), &n);
            if (!k) return false;
            fd1094d_set_key(&S.fd1094, k, n);
            free(k);
            S.use_fd1089 = false;
            S.use_fd1094 = true;
        }
    } else if (S.use_fd1094 && key) {
        if (!(S.rom = load16w(main, nmain, &S.rom_words))) return false;   // (the data space: the encrypted image)
        uint8_t *k = load_bytes(key, nkey, &n);
        if (!k) return false;
        fd1094d_set_key(&S.fd1094, k, n);
        free(k);
    } else if (key) {
        uint16_t *enc = load16w(main, nmain, &S.rom_words);
        uint8_t *k = load_bytes(key, nkey, &n);
        if (!enc || !k) return false;
        uint8_t *k2 = (uint8_t *)calloc(1, 0x2000);
        if (!k2) return false;
        memcpy(k2, k, n < 0x2000 ? n : 0x2000);
        S.rom = (uint16_t *)calloc(S.rom_words, 2);
        S.rom_data = (uint16_t *)calloc(S.rom_words, 2);
        if (!S.rom || !S.rom_data) return false;
        fd1089_decrypt(enc, S.rom, S.rom_data, S.rom_words * 2, k2, fd_type);
        free(enc);
        free(k);
        free(k2);
        S.use_fd1089 = true;
    } else if (!(S.rom = load16w(main, nmain, &S.rom_words))) return false;

    uint8_t *snd = load_bytes(sound, nsound, &n);
    if (!snd) return false;
    memcpy(S.sound_mem, snd, n < 0x10000 ? n : 0x10000);
    if (IS_16B) {
        for (int b = 0; b < S.sound_bank_count; b++) {
            const uint32_t src = (uint32_t)S.sound_bank_base + (uint32_t)b * 0x4000;
            if (src < n) memcpy(S.sound_bank[b], snd + src, n - src < 0x4000 ? n - src : 0x4000);
        }
    }
    free(snd);
    if (IS_16B) {
        if (S.use_mcu && mcu) {
            uint8_t *m = load_bytes(mcu, nmcu, &n);
            if (!m) return false;
            memcpy(S.mcu.rom, m, n);
            free(m);
        }
        if (!(S.sprite_rom = load16w(sprites, nsprites, &S.sprite_words))) return false;
        if (GAME == G_WB3 && S.alt16b) {        // (the empty banks read as 0xff, as MAME's ERASEFF)
            uint16_t *p = (uint16_t *)realloc(S.sprite_rom, 0x80000 * 2);
            if (!p) return false;
            S.sprite_rom = p;
            S.sprite_words = 0x80000;
            for (uint32_t b = 1; b < 8; b += 2)
                for (uint32_t i = b * 0x10000; i < (b + 1) * 0x10000; i++) p[i] = 0xffff;
        }
        if (remap) remap_sprites_5797();
        if (GAME == G_TIMESCAN && S.sprite_words < 0x40000) {   // (the gaps of its sprite ROM)
            uint16_t *p = (uint16_t *)realloc(S.sprite_rom, 0x40000 * 2);
            if (!p) return false;
            memset(p + S.sprite_words, 0, (0x40000 - S.sprite_words) * 2);
            S.sprite_rom = p;
            S.sprite_words = 0x40000;
        }
    } else {
        uint32_t bytes;
        uint8_t *raw = load_interleaved_bytes(sprites, nsprites, &bytes);
        if (!raw) return false;
        if (scramble) {                         // (the 16A sprite ROMs scrambled in 64K pieces)
            uint8_t *out = (uint8_t *)calloc(1, 0x80000);
            if (!out) return false;
            static const uint32_t kMap[8][2] = {{0x00000, 0x00000}, {0x40000, 0x10000}, {0x10000, 0x20000}, {0x50000, 0x30000},
                                                {0x20000, 0x40000}, {0x60000, 0x50000}, {0x30000, 0x60000}, {0x70000, 0x70000}};
            for (int i = 0; i < 8; i++)
                if (kMap[i][1] + 0x10000 <= bytes) memcpy(out + kMap[i][0], raw + kMap[i][1], 0x10000);
            free(raw);
            raw = out;
            bytes = 0x80000;
        }
        S.sprite_words = bytes / 2;
        if (!(S.sprite_rom = (uint16_t *)malloc(bytes))) return false;
        for (uint32_t i = 0; i < S.sprite_words; i++) S.sprite_rom[i] = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
        free(raw);
    }
    uint8_t *t = load_bytes(tiles, ntiles, &n);
    if (!t) return false;
    uint8_t *tf = (uint8_t *)calloc(1, (size_t)S.tile_n * 0x18000);   // (the planes at n * 0x8000 / 0x10000)
    if (!tf) return false;
    memcpy(tf, t, n < (uint32_t)S.tile_n * 0x18000 ? n : (uint32_t)S.tile_n * 0x18000);
    free(t);
    S.video.tiles = s16_decode_tiles(tf, S.tile_n);
    S.video.tiles_total = S.tile_n * 0x1000;
    free(tf);
    if (!S.video.tiles) return false;
    if (USES_N7751) {
        static const arc_rom kN7751Mcu[] = {{"7751.bin", 0x400, 0, 0x6a9534fc}};
        uint8_t *m = load_bytes(kN7751Mcu, 1, &n);
        if (!m) return false;
        memcpy(S.n7751_rom, m, n);
        free(m);
        uint8_t *d = load_bytes(n7751, nn7751, &n);
        if (!d) return false;
        S.n7751_data = (uint8_t *)calloc(1, 0x18000);
        if (!S.n7751_data) return false;
        memcpy(S.n7751_data, d, n < 0x18000 ? n : 0x18000);
        S.n7751_data_size = 0x18000;
        free(d);
    }
    return true;
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    s16_init(&S.video);
    S.in0 = S.in1 = S.in2 = 0xffff;
    S.dsw_a = 0xff;
    S.dsw_b = 0xfc;
    S.dsw_c = 0xff;
    S.sprite_banks = 4;
    S.tile_n = 1;
    S.tile_color_shift = 6;
    S.text_color_shift = 9;
    S.text_code_mask = 0x1ff;
    S.sound_bank_count = 16;
    S.sound_bank_base = 0x8000;
    S.b16 = IS_16B_C;
    S.n7751_on = USES_N7751_C;
    if (GAME == G_ALIENSYN || GAME == G_WB3) {  // (dsp-cpp's 16A set, or else the 16B one)
        static uint8_t probe[0x10000];
        const arc_rom *first = GAME == G_WB3 ? &kWb3Main[0] : &kAliensynMain[0];
        if (!arc_load(first, 1, probe, sizeof probe)) S.b16 = S.alt16b = true, S.n7751_on = false;
    }
    if (GAME == G_SDI) {                        // (dsp-cpp's 16B set, or else MAME's 16A one)
        static uint8_t probe[0x8000];
        if (!arc_load(&kSdiMain[0], 1, probe, sizeof probe)) S.b16 = false, S.alt16a = true;
    }
    S.sound_clock = S.b16 ? 5000000 : 4000000;
    S.fps = S.b16 ? 60.05439 : 60.0;
    if (IS_16B) configure_16b();
    else S.use_fd1089 = GAME == G_ALIENSYN || GAME == G_WB3 || GAME == G_SDI;
    if (IS_16B && S.use_mcu) mcs51_init(&S.mcu);
    S.frame = (uint32_t *)calloc(NW * NH, 4);
    if (!S.frame) return false;
    if (!IS_16B) {
        uint32_t **bufs[4] = {&S.bg_low, &S.bg_high, &S.fg_low, &S.fg_high};
        for (int i = 0; i < 4; i++)
            if (!(*bufs[i] = (uint32_t *)malloc(4u * S16_MAP_W * S16_MAP_H))) return false;
        if (!(S.text_low = (uint32_t *)malloc(4u * S16_TEXT_W * 256)) || !(S.text_high = (uint32_t *)malloc(4u * S16_TEXT_W * 256)))
            return false;
    }
    if (!load_roms()) return false;

    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68000);
    S.cpu.read_word = cpu_rd;
    S.cpu.write_word = cpu_wr;
    if (S.use_fd1094) {
        S.cpu.cmpild = cpu_cmpild;
        S.cpu.rte_cb = cpu_rte;
        S.cpu.irq_taken_cb = cpu_irq_taken;
    }
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.in = sound_in;
    S.snd.out = sound_out;
    S.snd.cycles = sound_cycles;
    zd_init(&S.snd);
    ym2151_init(&S.ym, 4000000, RATE);
    S.ym.amplitude = 0.5f;
    if (USES_N7751) {
        mcs48_init(&S.n7751, MCS48_N7751, S.n7751_rom);
        S.n7751.port_in = n7751_in;
        S.n7751.port_out = n7751_out;
        S.ym.port_handler = ym_port;
    }
    if (!IS_16B) {
        S.ppi.handshake = true;
        S.ppi.out[0] = ppi_a;
        S.ppi.out[1] = ppi_b;
        S.ppi.out[2] = ppi_c;
    } else {
        if (S.use_mcu) {
            S.mcu.clock = 8000000 / 12;
            S.mcu.read_external = mcu_ext_r;
            S.mcu.write_external = mcu_ext_w;
            S.mcu.port_read[1] = mcu_p1_r;
        }
        upd7759d_init(&S.upd, 0.9f, RATE);
        S.upd.drq_cb = upd_drq;
    }
    arc_reset();
    return true;
}

void arc_reset(void)
{
    if (IS_16B) map_reset();
    if (S.use_fd1094) fd1094d_reset(&S.fd1094);
    m68kd_reset(&S.cpu);
    zd_reset(&S.snd);
    ym2151_reset(&S.ym);
    if (!IS_16B) i8255d_reset(&S.ppi);
    if (IS_16B && S.use_mcu) mcs51_reset(&S.mcu);
    if (USES_N7751) {
        mcs48_reset(&S.n7751);
        S.i8243.p2 = S.i8243.p2out = 0x0f;
        S.i8243.prog = 1;
        S.i8243.opcode = 0;
        memset(S.i8243.p, 0, sizeof S.i8243.p);
    }
    if (IS_16B) upd7759d_reset(&S.upd);
    S.dac_sample = 0;
    s16_reset(&S.video);
    S.video.screen_enabled = !IS_16B;
    S.video.bank_size = 0x1000;
    for (int i = 0; i < 8; i++) S.video.tile_bank[i] = (uint8_t)i;
    if (S.mb_type) {
        memset(S.video.sprite_bank, 0xff, sizeof S.video.sprite_bank);
        S.video.sprite_bank[0] = 0;
        S.video.sprite_bank[7] = 3;
        S.video.sprite_bank[11] = 2;
        S.video.sprite_bank[13] = 1;
        S.video.sprite_bank[14] = 0;
    }
    memset(S.ram, 0, sizeof S.ram);
    S.in0 = S.in1 = S.in2 = 0xffff;
    S.sound_latch = S.sound_bank_num = S.n7751_command = 0;
    S.n7751_rom_address = 0;
    memset(S.s5248, 0, sizeof S.s5248);
    memset(S.s5250, 0, sizeof S.s5250);
    S.s5250_bit = 0;
    S.analog_x = S.analog_y = 0x80;
    S.sdi_joy = 0xffff;
    S.video_control = 0;
    memset(S.track, 0, sizeof S.track);
    S.audio_acc = 0;
    S.main_debt = S.sound_debt = S.mcu_debt = S.n7751_debt = 0;
    S.dc_in = S.dc_out = S.dac_lp1 = S.dac_lp2 = 0;
    memset(S.frame, 0, 4u * NW * NH);
    memset(S.fb, 0, sizeof S.fb);
}

int arc_run_frame(const int16_t **audio)
{
    const double slices = (double)(SCANLINES * CPU_SYNC);
    const double main_cycles = (double)MAIN_CLOCK / S.fps / slices;
    const double snd = (double)SOUND_CLOCK / S.fps / slices;
    const double mcu_cycles = (IS_16B && S.use_mcu) ? (double)S.mcu.clock / S.fps / slices : 0.0;
    const double n7751_cycles = USES_N7751 ? (double)(6000000 / 15) / S.fps / slices : 0.0;
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 224) {
            if (IS_16B && S.use_mcu) mcs51_set_irq0_line(&S.mcu, MCS51_HOLD);
            else m68kd_set_irq(&S.cpu, 4, M68_HOLD);
            if (GAME == G_WB3 && S.alt16b) {    // (MAME's wb3_i8751_sim: the 317-0098 takes the sound byte from work RAM)
                const uint16_t cmd = S.ram[0xc008 >> 1];
                if (cmd & 0x00ff) {
                    map_write_reg(3, (uint8_t)(cmd >> 8));
                    S.ram[0xc008 >> 1] = cmd & 0xff00;
                }
            }
            update_video();
        }
        for (int slice = 0; slice < CPU_SYNC; slice++) {
            S.main_debt += main_cycles;
            S.main_debt -= m68kd_run(&S.cpu, (int)S.main_debt);
            S.sound_debt += snd;
            S.sound_debt -= zd_run(&S.snd, (int)S.sound_debt);
            if (IS_16B && S.use_mcu) {
                S.mcu_debt += mcu_cycles;
                S.mcu_debt -= mcs51_run(&S.mcu, (int)S.mcu_debt);
            }
            if (USES_N7751) {
                S.n7751_debt += n7751_cycles;
                S.n7751_debt -= mcs48_run(&S.n7751, (int)S.n7751_debt);
            }
        }
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

static uint16_t player(uint16_t port, unsigned p)
{
    if (IS_16B) {
        port = 0xffff;
        if (p & AP_B3) port &= (uint16_t)~0x0001;
        if (p & AP_B1) port &= (uint16_t)~0x0002;
        if (p & AP_B2) port &= (uint16_t)~0x0004;
        if (p & AP_DOWN) port &= (uint16_t)~0x0010;
        if (p & AP_UP) port &= (uint16_t)~0x0020;
        if (p & AP_RIGHT) port &= (uint16_t)~0x0040;
        if (p & AP_LEFT) port &= (uint16_t)~0x0080;
        return port;
    }
    static const struct { unsigned bit; uint16_t mask; } kMap[7] = {
        {AP_UP, 0x20}, {AP_DOWN, 0x10}, {AP_LEFT, 0x80}, {AP_RIGHT, 0x40}, {AP_B2, 0x04}, {AP_B1, 0x02}, {AP_B3, 0x01}};
    for (int i = 0; i < 7; i++) {
        if (p & kMap[i].bit) port &= (uint16_t)~kMap[i].mask;
        else port |= kMap[i].mask;
    }
    return port;
}

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    if (IS_16B) {
        S.in0 = 0xffff;
        if (in->service) S.in0 &= (uint16_t)~0x0004;
        S.in1 = player(S.in1, p1);
        S.in2 = player(S.in2, p2);
        if (in->coin1) S.in0 &= (uint16_t)~0x0001;
        if (in->coin2) S.in0 &= (uint16_t)~0x0002;
        if (p1 & AP_START) S.in0 &= (uint16_t)~0x0010;
        if (p2 & AP_START) S.in0 &= (uint16_t)~0x0020;
        if (S.sdi_io) {                         // (SDI: the fire buttons also on the service port)
            if (p1 & AP_B1) S.in0 &= (uint16_t)~0x0048;
            if (p2 & AP_B1) S.in0 &= (uint16_t)~0x0080;
            S.analog_x = S.analog_y = 0x80;
            if (p1 & AP_LEFT) S.analog_x = 0x00;
            if (p1 & AP_RIGHT) S.analog_x = 0xff;
            if (p1 & AP_UP) S.analog_y = 0x00;
            if (p1 & AP_DOWN) S.analog_y = 0xff;
        }
    } else {
        S.in1 = player(S.in1, p1);
        S.in2 = player(S.in2, p2);
        S.in0 = (p1 & AP_START) ? (uint16_t)(S.in0 & ~0x0010) : (uint16_t)(S.in0 | 0x0010);
        S.in0 = (p2 & AP_START) ? (uint16_t)(S.in0 & ~0x0020) : (uint16_t)(S.in0 | 0x0020);
        S.in0 = in->coin1 ? (uint16_t)(S.in0 & ~0x0001) : (uint16_t)(S.in0 | 0x0001);
        S.in0 = in->coin2 ? (uint16_t)(S.in0 & ~0x0002) : (uint16_t)(S.in0 | 0x0002);
        S.in0 = in->service ? (uint16_t)(S.in0 & ~0x0004) : (uint16_t)(S.in0 | 0x0004);
        if (S.alt16a) {                         // (SDI: the stick moves the ship; with B2 held, the sight)
            S.sdi_joy = 0xffff;
            for (int pl = 0; pl < 2; pl++) {
                const unsigned p = in->p[pl];
                S.in0 = (p & AP_B1) ? (uint16_t)(S.in0 & ~(0x40 << pl)) : (uint16_t)(S.in0 | (0x40 << pl));
                if (p & AP_B2) {
                    if (p & AP_LEFT) S.track[pl][0] = (uint8_t)(S.track[pl][0] - 6);
                    if (p & AP_RIGHT) S.track[pl][0] = (uint8_t)(S.track[pl][0] + 6);
                    if (p & AP_UP) S.track[pl][1] = (uint8_t)(S.track[pl][1] - 6);
                    if (p & AP_DOWN) S.track[pl][1] = (uint8_t)(S.track[pl][1] + 6);
                } else {
                    const int sh = pl ? 4 : 0;
                    if (p & AP_DOWN) S.sdi_joy &= (uint16_t)~(0x1 << sh);
                    if (p & AP_UP) S.sdi_joy &= (uint16_t)~(0x2 << sh);
                    if (p & AP_RIGHT) S.sdi_joy &= (uint16_t)~(0x4 << sh);
                    if (p & AP_LEFT) S.sdi_joy &= (uint16_t)~(0x8 << sh);
                }
            }
        }
    }
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    else if (bank == 1) S.dsw_b = v;
    else if (bank == 2) S.dsw_c = v;
}
