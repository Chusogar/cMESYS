// slapfght.c - Tiger Heli (Toaplan / Taito, 1985) and Slap Fight (1986) for
// the common arcade frontends (arc.h; ARC_VARIANT 1: Slap Fight): C port of
// dsp-cpp's drivers/arcade/slapfight (from dsp-emulator's slapfight_hw.pas).
// dsp-cpp's Tiger Heli variant loads the Slap Fight ROMs, so the ROM map of
// Tiger Heli here is MAME's tigerh (slapfght.cpp): no bank switching, 2048
// background tiles and 512 sprites. A Z80 at 6 MHz, a sound Z80 with two
// AY-3-8910 (the inputs and DIP switches on their ports), the 68705 MCU, a
// scrolling background, 16x16 sprites and 8x8 text.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../machine/taito68705dsp.h"
#include "../../../sound/ay8910.h"

#define MAIN_CLOCK 6000000
#define SOUND_CLOCK 3000000
#define MCU_CLOCK 3000000
#define AY_CLOCK 1500000
#define SCANLINES 270
#define FPS (36000000.0 / 6.0 / 388.0 / 270.0)
#define RATE 44100
#define W 239
#define H 280
#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#if ARC_VARIANT == 0
#define CHARS 0x400
#define TILES 0x800
#define SPRITES 0x200
#define MAIN_SIZE 0xc000
#define MCU_TYPE T68705_TIGERHELI
static const char *const kSets[] = { "tigerh", NULL };
static const arc_rom kMain[] = {
    {"a47_00-1.8p", 0x4000, 0x0000, 0x4be73246}, {"a47_01-1.8n", 0x4000, 0x4000, 0xaad04867},
    {"a47_02-1.8k", 0x4000, 0x8000, 0x4843f15c}};
static const arc_rom kSound[] = {{"a47_03.12d", 0x2000, 0, 0xd105260f}};
static const arc_rom kMcu[] = {{"a47_14.6a", 0x800, 0, 0x4042489f}};
static const arc_rom kProm[] = {
    {"82s129.12q", 0x100, 0x000, 0x2c69350d}, {"82s129.12m", 0x100, 0x100, 0x7142e972},
    {"82s129.12n", 0x100, 0x200, 0x25f273f2}};
static const arc_rom kChars[] = {{"a47_05.6f", 0x2000, 0, 0xc5325b49}, {"a47_04.6g", 0x2000, 0x2000, 0xcd59628e}};
static const arc_rom kTiles[] = {
    {"a47_09.4m", 0x4000, 0x0000, 0x31fae8a8}, {"a47_08.6m", 0x4000, 0x4000, 0xe539af2b},
    {"a47_07.6n", 0x4000, 0x8000, 0x02fdd429}, {"a47_06.6p", 0x4000, 0xc000, 0x11fbcc8c}};
static const arc_rom kSprites[] = {
    {"a47_13.8j", 0x4000, 0x0000, 0x739a7e7e}, {"a47_12.6j", 0x4000, 0x4000, 0xc064ecdb},
    {"a47_11.8h", 0x4000, 0x8000, 0x744fae9b}, {"a47_10.6h", 0x4000, 0xc000, 0xe1cf844e}};

const arc_info arc_game = { "tigerh", "Tiger Heli", "Toaplan / Taito", 1985, W, H, FPS, RATE, kSets,
                            { "FIRE", "BOMB", NULL, NULL }, ARC_JOY_8WAY };
#else
// Slap Fight (dsp-cpp's own variant): 0000-7fff, then two banks of 8000-bfff
// picked by ports 8 / 9; 4096 background tiles and 1024 sprites
#define CHARS 0x400
#define TILES 0x1000
#define SPRITES 0x400
#define MAIN_SIZE 0x10000
#define MCU_TYPE T68705_STANDARD
#define BANKED 1
static const char *const kSets[] = { "slapfght", NULL };
static const arc_rom kMain[] = {{"a77_00.8p", 0x8000, 0x0000, 0x674c0e0f}, {"a77_01.8n", 0x8000, 0x8000, 0x3c42e4a7}};
static const arc_rom kSound[] = {{"a77_02.12d", 0x2000, 0, 0x87f4705a}};
static const arc_rom kMcu[] = {{"a77_13.6a", 0x800, 0, 0xa70c81d9}};
static const arc_rom kProm[] = {
    {"21_82s129.12q", 0x100, 0x000, 0xa0efaf99}, {"20_82s129.12m", 0x100, 0x100, 0xa56d57e5},
    {"19_82s129.12n", 0x100, 0x200, 0x5cbf9fbf}};
static const arc_rom kChars[] = {{"a77_04.6f", 0x2000, 0x0000, 0x2ac7b943}, {"a77_03.6g", 0x2000, 0x2000, 0x33cadc93}};
static const arc_rom kTiles[] = {
    {"a77_08.6k", 0x8000, 0x00000, 0xb6358305}, {"a77_07.6m", 0x8000, 0x08000, 0xe92d9d60},
    {"a77_06.6n", 0x8000, 0x10000, 0x5faeeea3}, {"a77_05.6p", 0x8000, 0x18000, 0x974e2ea9}};
static const arc_rom kSprites[] = {
    {"a77_12.8j", 0x8000, 0x00000, 0x8545d397}, {"a77_11.7j", 0x8000, 0x08000, 0xb1b7b925},
    {"a77_10.8h", 0x8000, 0x10000, 0x422d946b}, {"a77_09.7h", 0x8000, 0x18000, 0x587113ae}};

const arc_info arc_game = { "slapfght", "Slap Fight", "Toaplan / Taito", 1986, W, H, FPS, RATE, kSets,
                            { "FIRE", "BOMB", NULL, NULL }, ARC_JOY_8WAY };
#endif
#define TILE_ROM (TILES * 32)                 // 4 bits a pixel
#define SPRITE_ROM (SPRITES * 128)

static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kY16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sound;
    t68705d mcu;
    ay8910 ay0, ay1;
    const uint8_t *rom, *sound_rom;     // blobs (the CYD: in the flash)
    uint8_t ram[0x2800], ram_f000[0x1000], sound_ram[0x3000];   // c000-e7ff, f000-ffff; d000-ffff
    const uint8_t *chars, *tiles, *sprites;
    uint16_t palette[256];
    arc_fb fb;
    uint8_t in0, in1, dswa, dswb, scroll_x, rom_bank;
    uint16_t scroll_y;
    bool ena_irq, sound_nmi, sound_reset;
    int status_state, sound_nmi_counter;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

static void mcu_misc(void *u, int pos, uint8_t v)
{
    (void)u;
    if (pos == 0) S.scroll_y = (uint16_t)((S.scroll_y & 0xff00) | v);
    if (pos == 1) S.scroll_y = (uint16_t)((S.scroll_y & 0x00ff) | (v << 8));
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a == 0xe803) return t68705d_read(&S.mcu);
#if BANKED
    if (a >= 0x8000 && a <= 0xbfff) return S.rom[0x8000 + (S.rom_bank & 1) * 0x4000 + (a & 0x3fff)];
#endif
    if (a <= 0xbfff) return S.rom[a];
    if (a <= 0xe7ff) return S.ram[a - 0xc000];
    if (a >= 0xf000) return S.ram_f000[a - 0xf000];
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    switch (a) {
    case 0xe800: S.scroll_y = (uint16_t)((S.scroll_y & 0xff00) | v); return;
    case 0xe801: S.scroll_y = (uint16_t)((S.scroll_y & 0x00ff) | (v << 8)); return;
    case 0xe802: S.scroll_x = v; return;
    case 0xe803: t68705d_write(&S.mcu, v); return;
    default: break;
    }
    if (a <= 0xe7ff) S.ram[a - 0xc000] = v;
    else if (a >= 0xf000) S.ram_f000[a - 0xf000] = v;
}

static uint8_t main_in(void *u, uint16_t p)
{
    (void)u;
    if ((p & 0xff) == 0) {
        static const uint8_t states[3] = {0xc7, 0x55, 0x00};
        const int idx = S.status_state % 3;
        S.status_state = (S.status_state + 1) & 3;
        return (uint8_t)((states[idx] & 0xf9) | ((!S.mcu.main_sent) << 1) | ((!S.mcu.mcu_sent) << 2));
    }
    return 0xff;
}

static void main_out(void *u, uint16_t p, uint8_t v)
{
    (void)u, (void)v;
    switch (p & 0xff) {
    case 0: S.sound_reset = true; break;
    case 1: S.sound_reset = false; S.sound_nmi = false; break;
    case 6: S.ena_irq = false; zd_set_irq(&S.main, ZD_CLEAR, 0xff); break;
    case 7: S.ena_irq = true; break;
#if BANKED
    case 8: S.rom_bank = 0; break;
    case 9: S.rom_bank = 1; break;
#endif
    default: break;
    }
}

// the AYs' ports: the inputs on the first, the DIP switches on the second
static uint8_t ay_read(ay8910 *ay, uint8_t a, uint8_t b)
{
    const uint8_t reg = ay->latch & 0x0f;
    if (reg == 14) ay->regs[14] = a;
    else if (reg == 15) ay->regs[15] = b;
    return ay8910_data_r(ay);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff) return S.sound_rom[a];
    if (a >= 0xd000) return S.sound_ram[a - 0xd000];
    if (a == 0xa081) return ay_read(&S.ay0, S.in0, S.in1);
    if (a == 0xa091) return ay_read(&S.ay1, S.dswa, S.dswb);
    if (a >= 0xc800 && a <= 0xcfff) return S.ram[a - 0xc000];
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) return;
    if (a == 0xa080) ay8910_address_w(&S.ay0, v);
    else if (a == 0xa082) ay8910_data_w(&S.ay0, v);
    else if (a == 0xa090) ay8910_address_w(&S.ay1, v);
    else if (a == 0xa092) ay8910_data_w(&S.ay1, v);
    else if (a >= 0xa0e0 && a <= 0xa0ef) S.sound_nmi = (a & 1) == 0;
    else if (a >= 0xc800 && a <= 0xcfff) S.ram[a - 0xc000] = v;
    else if (a >= 0xd000) S.sound_ram[a - 0xd000] = v;
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(int c)
{
    // the NMI at 180 Hz when enabled
    S.sound_nmi_counter += c;
    const int period = SOUND_CLOCK / 180;
    while (S.sound_nmi_counter >= period) {
        S.sound_nmi_counter -= period;
        if (S.sound_nmi) zd_set_nmi(&S.sound, ZD_PULSE);
    }
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(ay8910_render(&S.ay0) + ay8910_render(&S.ay1));
    }
}

#define MEM(a) ((a) >= 0xf000 ? S.ram_f000[(a) - 0xf000] : S.ram[(a) - 0xc000])

// The 256x512 layers, a pixel at a time: tile f is column x / 8, row 63 - y / 8
static inline int layer_tile(int x, int y) { return (x >> 3) * 64 + 63 - (y >> 3); }

static inline uint16_t bg_px(int x, int y)
{
    const int f = layer_tile(x, y);
    const uint8_t attr = S.ram[0xd800 - 0xc000 + f];
    const uint8_t *px = S.tiles + (size_t)((S.ram[0xd000 - 0xc000 + f] + ((attr & 0x0f) << 8)) & (TILES - 1)) * 64;
    return S.palette[((attr & 0xf0) + px[(y & 7) * 8 + (x & 7)]) & 0xff];
}

// the text: palette index + 1, 0 transparent
static inline int fg_px(int x, int y)
{
    const int f = layer_tile(x, y);
    const uint8_t attr = S.ram_f000[0x800 + f];
    const uint8_t p = S.chars[(size_t)((S.ram_f000[f] + ((attr & 3) << 8)) & (CHARS - 1)) * 64 + (y & 7) * 8 + (x & 7)];
    return p ? (((attr & 0xfc) + p) & 0xff) + 1 : 0;
}

// the scrolled background, the sprites, then the text (taken 16 right and 224 down)
static void update_video(void)
{
    const int sx = S.scroll_x + 17, sy = 736 - S.scroll_y;
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&S.fb, y);
        for (int x = 0; x < W; x++) out[x] = bg_px((x + sx) & 0xff, (y + sy) & 0x1ff);
    }
    for (int f = 0; f < 0x200; f++) {
        const uint8_t attr = MEM(0xe002 + f * 4);
        const int code = (MEM(0xe000 + f * 4) + ((attr & 0xc0) << 2)) & (SPRITES - 1);
        const int color = (attr & 0x1e) << 3, x = MEM(0xe003 + f * 4) - 16;
        const int y = (797 - (MEM(0xe001 + f * 4) + ((attr & 1) << 8))) & 0x1ff;
        const uint8_t *px = S.sprites + (size_t)code * 256;
        for (int r = 0; r < 16; r++) {
            const int py = y + r;
            if (py >= H) continue;
            uint16_t *out = arc_fb_row(&S.fb, py);
            for (int c = 0; c < 16; c++) {
                const int qx = x + c;
                if (qx < 0 || qx >= W || !px[r * 16 + c]) continue;
                out[qx] = S.palette[(color + px[r * 16 + c]) & 0xff];
            }
        }
    }
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&S.fb, y);
        for (int x = 0; x < W; x++) {
            const int f = fg_px(x + 16, y + 224);
            if (f) out[x] = S.palette[f - 1];
        }
    }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&S.fb, y); }

static uint8_t bitw(uint8_t v) { return (uint8_t)(0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1)); }

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t prom[0x300];
    if (!arc_fb_init(&S.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    if (!(S.rom = ARC_ROM_BLOB("main", kMain, MAIN_SIZE)) || !(S.sound_rom = ARC_ROM_BLOB("sound", kSound, 0x2000)) ||
        !ARC_LOAD(kProm, prom, sizeof prom))
        return false;
    t68705d_init(&S.mcu, MCU_TYPE);
    if (!ARC_LOAD(kMcu, S.mcu.mem, 0x800)) return false;
    S.mcu.misc = mcu_misc;
    arc_blob b;
    const uint8_t *raw = ARC_ROM_BLOB("chars.raw", kChars, 0x4000);
    if (!raw) return false;
    arc_layout cl = { 8, 8, CHARS, 2, 64, false, false, true, {0, CHARS * 64}, kX16, kY8 };
    if (arc_blob_begin(&b, "chars", CHARS * 64, ARC_ROM_KEY(kChars, 1))) arc_blob_gfx(&b, &cl, raw, 0x4000, 0);
    arc_blob_free(raw);
    if (!(S.chars = arc_blob_end(&b)) || !(raw = ARC_ROM_BLOB("tiles.raw", kTiles, TILE_ROM))) return false;
    arc_layout tl = { 8, 8, TILES, 4, 64, false, false, true, {0, TILES * 64, TILES * 128, TILES * 192}, kX16, kY8 };
    if (arc_blob_begin(&b, "tiles", TILES * 64, ARC_ROM_KEY(kTiles, 1))) arc_blob_gfx(&b, &tl, raw, TILE_ROM, 0);
    arc_blob_free(raw);
    if (!(S.tiles = arc_blob_end(&b)) || !(raw = ARC_ROM_BLOB("sprites.raw", kSprites, SPRITE_ROM))) return false;
    arc_layout sl = { 16, 16, SPRITES, 4, 256, false, false, true, {0, SPRITES * 256, SPRITES * 512, SPRITES * 768}, kX16, kY16 };
    if (arc_blob_begin(&b, "sprites", SPRITES * 256, ARC_ROM_KEY(kSprites, 1))) arc_blob_gfx(&b, &sl, raw, SPRITE_ROM, 0);
    arc_blob_free(raw);
    if (!(S.sprites = arc_blob_end(&b))) return false;
    for (int f = 0; f < 0x100; f++)
        S.palette[f] = arc_rgb565((uint32_t)bitw(prom[f]) << 16 | (uint32_t)bitw(prom[0x100 + f]) << 8 | bitw(prom[0x200 + f]));
    S.main.read = main_read;
    S.main.write = main_write;
    S.main.in = main_in;
    S.main.out = main_out;
    S.sound.read = sound_read;
    S.sound.write = sound_write;
    S.sound.in = in_ff;
    S.sound.out = out_nop;
    zd_init(&S.main);
    zd_init(&S.sound);
    ay8910_init(&S.ay0, AY_CLOCK, RATE);
    ay8910_init(&S.ay1, AY_CLOCK, RATE);
    ay8910_set_amplitude(&S.ay0, 1.2f);
    ay8910_set_amplitude(&S.ay1, 1.2f);
    S.dswa = 0x7f;
    S.dswb = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&S.main);
    zd_reset(&S.sound);
    t68705d_reset(&S.mcu);
    ay8910_reset(&S.ay0);
    ay8910_reset(&S.ay1);
    S.scroll_x = S.rom_bank = 0;
    S.scroll_y = 0;
    S.ena_irq = S.sound_nmi = false;
    S.sound_reset = true;
    S.status_state = S.sound_nmi_counter = 0;
    S.in0 = S.in1 = 0xff;
    S.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    int ms = (int)(MAIN_CLOCK / FPS) / SCANLINES, ss = (int)(SOUND_CLOCK / FPS) / SCANLINES, cs = (int)(MCU_CLOCK / FPS) / SCANLINES;
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (S.ena_irq) zd_set_irq(&S.main, ZD_ASSERT, 0xff);
            update_video();
        }
        zd_run(&S.main, ms);
        if (!S.sound_reset) {
            zd_run(&S.sound, ss);
            sound_cycles(ss);
        }
        t68705d_run(&S.mcu, cs);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&S.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0xff, b = 0xff;
    if (p1 & AP_UP) a &= ~0x01;
    if (p1 & AP_DOWN) a &= ~0x02;
    if (p1 & AP_RIGHT) a &= ~0x04;
    if (p1 & AP_LEFT) a &= ~0x08;
    if (p2 & AP_UP) a &= ~0x10;
    if (p2 & AP_DOWN) a &= ~0x20;
    if (p2 & AP_RIGHT) a &= ~0x40;
    if (p2 & AP_LEFT) a &= ~0x80;
    if (p1 & AP_B1) b &= ~0x01;
    if (p1 & AP_B2) b &= ~0x02;
    if (p2 & AP_B1) b &= ~0x04;
    if (p2 & AP_B2) b &= ~0x08;
    if (p1 & AP_START) b &= ~0x10;
    if (p2 & AP_START) b &= ~0x20;
    if (in->coin1) b &= ~0x40;
    if (in->coin2) b &= ~0x80;
    S.in0 = a;
    S.in1 = b;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dswa = v;
    if (bank == 1) S.dswb = v;
}
