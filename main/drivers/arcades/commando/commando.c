// commando.c - Commando (Capcom, 1985) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/commando. A Z80 at 3 MHz (its
// opcodes bit-swapped), a sound Z80 with two YM2203, a 512x512 scrolling
// background of 16x16 tiles, 8x8 text and 16x16 sprites on a vertical
// monitor. dsp-cpp keeps the background and the text in canvases; here each
// pixel is taken from the tiles when the picture is put together (the same
// result).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2203dsp.h"

#define CLOCK 3000000
#define SCANLINES 262
#define FPS (12000000.0 / 2.0 / 384.0 / 262.0)
#define RATE 44100
#define W 224
#define H 256

static const char *const kSets[] = { "commando", NULL };
static const arc_rom kMain[] = {{"cm04.9m", 0x8000, 0, 0x8438b694}, {"cm03.8m", 0x4000, 0x8000, 0x35486542}};
static const arc_rom kSound[] = {{"cm02.9f", 0x4000, 0, 0xf9cc4a74}};
static const arc_rom kPal[] = {
    {"vtb1.1d", 0x100, 0, 0x3aba15a1}, {"vtb2.2d", 0x100, 0x100, 0x88865754}, {"vtb3.3d", 0x100, 0x200, 0x4c14c3f6}};
static const arc_rom kChars[] = {{"vt01.5d", 0x4000, 0, 0x505726e0}};
static const arc_rom kSprites[] = {
    {"vt05.7e", 0x4000, 0, 0x79f16e3d}, {"vt06.8e", 0x4000, 0x4000, 0x26fee521}, {"vt07.9e", 0x4000, 0x8000, 0xca88bdfd},
    {"vt08.7h", 0x4000, 0xc000, 0x2019c883}, {"vt09.8h", 0x4000, 0x10000, 0x98703982}, {"vt10.9h", 0x4000, 0x14000, 0xf069d2f8}};
static const arc_rom kTiles[] = {
    {"vt11.5a", 0x4000, 0, 0x7b2e1b48}, {"vt12.6a", 0x4000, 0x4000, 0x81b417d3}, {"vt13.7a", 0x4000, 0x8000, 0x5612dbd2},
    {"vt14.8a", 0x4000, 0xc000, 0x2b2dee36}, {"vt15.9a", 0x4000, 0x10000, 0xde70babf}, {"vt16.10a", 0x4000, 0x14000, 0x14178237}};

const arc_info arc_game = { "commando", "Commando", "Capcom", 1985, W, H, FPS, RATE, kSets,
                            { "FIRE", "GRENADE", NULL, NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static const int kCharY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kSprX[16] = {0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267};
static const int kY16x16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
static const int kTileX[16] = {0, 1, 2, 3, 4, 5, 6, 7, 128, 129, 130, 131, 132, 133, 134, 135};
static const int kTileY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sound;
    ym2203d ym0, ym1;
    uint8_t *rom_data, *rom_opcode, *chars, *sprites, *tiles;
    uint8_t ram[0x2000], char_ram[0x800], tile_ram[0x800], sprite_ram[0x200], sound_ram[0x4800];
    uint16_t palette[256];
    uint16_t *fb;
    int scroll_x, scroll_y;
    bool flip, sound_reset_held;
    uint8_t sound_command, in0, in1, in2, dsw_a, dsw_b;
    int64_t audio_acc, irq_acc;
    int per_line;
    int16_t audio[1024];
    int naudio;
} *C_;
#define C (*C_)

static uint8_t bitswap8(uint8_t v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0)
{
#define BIT(n) ((v >> (n)) & 1)
    return (uint8_t)((BIT(b7) << 7) | (BIT(b6) << 6) | (BIT(b5) << 5) | (BIT(b4) << 4) | (BIT(b3) << 3) |
                     (BIT(b2) << 2) | (BIT(b1) << 1) | BIT(b0));
#undef BIT
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xbfff) return C.rom_data[a];
    if (a == 0xc000) return C.in0;
    if (a == 0xc001) return C.in1;
    if (a == 0xc002) return C.in2;
    if (a == 0xc003) return C.dsw_a;
    if (a == 0xc004) return C.dsw_b;
    if (a >= 0xd000 && a <= 0xd7ff) return C.char_ram[a & 0x7ff];
    if (a >= 0xd800 && a <= 0xdfff) return C.tile_ram[a & 0x7ff];
    if (a >= 0xe000) return C.ram[a & 0x1fff];
    return 0xff;
}

static uint8_t main_opcode(void *u, uint16_t a) { return a <= 0xbfff ? C.rom_opcode[a] : main_read(u, a); }

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a == 0xc800) C.sound_command = v;
    else if (a == 0xc804) {
        const bool hold = (v & 0x10) != 0;
        if (C.sound_reset_held && !hold) zd_reset(&C.sound);
        C.sound_reset_held = hold;
        C.flip = (v & 0x80) != 0;
    } else if (a == 0xc808) C.scroll_y = (C.scroll_y & 0x100) | v;
    else if (a == 0xc809) C.scroll_y = (C.scroll_y & 0xff) | ((v & 1) << 8);
    else if (a == 0xc80a) C.scroll_x = (C.scroll_x & 0x100) | v;
    else if (a == 0xc80b) C.scroll_x = (C.scroll_x & 0xff) | ((v & 1) << 8);
    else if (a >= 0xd000 && a <= 0xd7ff) C.char_ram[a & 0x7ff] = v;
    else if (a >= 0xd800 && a <= 0xdfff) C.tile_ram[a & 0x7ff] = v;
    else if (a >= 0xe000) C.ram[a & 0x1fff] = v;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x47ff) return C.sound_ram[a];
    if (a == 0x6000) return C.sound_command;
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
    if (a <= 0x47ff) C.sound_ram[a] = v;
    else if (a == 0x8000) ym2203d_control(&C.ym0, v);
    else if (a == 0x8001) ym2203d_write(&C.ym0, v);
    else if (a == 0x8002) ym2203d_control(&C.ym1, v);
    else if (a == 0x8003) ym2203d_write(&C.ym1, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    C.irq_acc += cycles;                    // 4 x 60 Hz: the music's tempo
    const int64_t period = CLOCK / (4 * 60);
    while (C.irq_acc >= period) {
        C.irq_acc -= period;
        zd_set_irq(&C.sound, ZD_HOLD, 0xff);
    }
    C.audio_acc += (int64_t)cycles * RATE;
    while (C.audio_acc >= CLOCK) {
        C.audio_acc -= CLOCK;
        const int32_t s = ym2203d_update(&C.ym0) + ym2203d_update(&C.ym1);
        if (C.naudio < 1024) C.audio[C.naudio++] = arc_clamp16(s);
    }
}

// the background canvas's pixel (512x512, as dsp-cpp draws it)
static inline uint16_t bg_px(int cx, int cy)
{
    const int cell_x = cx >> 4, cell_y = cy >> 4;
    const int f = (31 - cell_y) * 32 + cell_x;
    const uint8_t attr = C.tile_ram[0x400 + f];
    const int nchar = C.tile_ram[f] + ((attr & 0xc0) << 2);
    const int color = (attr & 0xf) << 3;
    const int py = (attr & 0x10) ? 15 - (cy & 15) : (cy & 15);
    const int px = (attr & 0x20) ? 15 - (cx & 15) : (cx & 15);
    return C.palette[(color + C.tiles[(nchar & 0x3ff) * 256 + py * 16 + px]) & 0xff];
}

static void render_frame(void)
{
    for (int fy = 0; fy < H; fy++) {
        const int by = (fy + (256 - C.scroll_y) + 1024) & 511;
        for (int fx = 0; fx < W; fx++) C.fb[fy * W + fx] = bg_px((fx + 16 + C.scroll_x) & 511, by);
    }
    for (int f = 0; f < 0x80; f++) {
        const uint8_t *s = &C.sprite_ram[f * 4];
        const uint8_t attr = s[1];
        const int bank = (attr >> 6) & 3;
        if (bank >= 3) continue;
        const int nchar = s[0] + (bank << 8), color = attr & 0x30;
        const int x = s[2] - 16, y = 240 - (s[3] + ((attr & 1) << 8));
        const bool fx = (attr & 8) != 0, fy = (attr & 4) != 0;
        const uint8_t *g = C.sprites + (nchar & 0x2ff) * 256;
        for (int py = 0; py < 16; py++) {
            const int dy = y + (fy ? 15 - py : py);
            if (dy < 0 || dy >= H) continue;
            for (int px = 0; px < 16; px++) {
                const uint8_t p = g[py * 16 + px];
                if (p == 15) continue;
                const int dx = x + (fx ? 15 - px : px);
                if (dx < 0 || dx >= W) continue;
                C.fb[dy * W + dx] = C.palette[128 + ((color + p) & 0x3f)];
            }
        }
    }
    // the text over everything (pen 3 transparent); its 256x256 canvas from x 16
    for (int f = 0; f < 0x400; f++) {
        const int x = f / 32, y = 31 - (f % 32);
        const uint8_t attr = C.char_ram[0x400 + f];
        const int nchar = C.char_ram[f] + ((attr & 0xc0) << 2), color = (attr & 0xf) << 2;
        const bool fx = (attr & 0x20) != 0, fy = (attr & 0x10) != 0;
        const uint8_t *g = C.chars + (nchar & 0x3ff) * 64;
        for (int py = 0; py < 8; py++) {
            const int dy = y * 8 + (fy ? 7 - py : py);
            for (int px = 0; px < 8; px++) {
                const uint8_t p = g[py * 8 + px];
                if (p == 3) continue;
                const int dx = x * 8 + (fx ? 7 - px : px) - 16;
                if (dx < 0 || dx >= W) continue;
                C.fb[dy * W + dx] = C.palette[192 + ((color + p) & 0x3f)];
            }
        }
    }
    memcpy(C.sprite_ram, C.ram + 0x2000 - 0x200, 0x200);
}

bool arc_init(void)
{
    if (!(C_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    C.rom_data = (uint8_t *)calloc(1, 0xc000);
    C.rom_opcode = (uint8_t *)calloc(1, 0xc000);
    C.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *gfx = (uint8_t *)calloc(1, 0x18000);
    uint8_t prom[0x300];
    if (!C.rom_data || !C.rom_opcode || !C.fb || !gfx) return false;
    if (!ARC_LOAD(kMain, C.rom_data, 0xc000) || !ARC_LOAD(kSound, C.sound_ram, 0x4000) || !ARC_LOAD(kPal, prom, sizeof prom))
        return false;
    C.rom_opcode[0] = C.rom_data[0];
    for (int f = 1; f < 0xc000; f++) C.rom_opcode[f] = bitswap8(C.rom_data[f], 3, 2, 1, 4, 7, 6, 5, 0);
    arc_layout cl = { 8, 8, 1024, 2, 128, false, false, true, {4, 0}, kCharX, kCharY };
    if (!ARC_LOAD(kChars, gfx, 0x4000) || !(C.chars = arc_gfx_decode(&cl, gfx, 0x4000))) return false;
    arc_layout sl = { 16, 16, 768, 4, 512, false, false, true, {0xc000 * 8 + 4, 0xc000 * 8, 4, 0}, kSprX, kY16x16 };
    if (!ARC_LOAD(kSprites, gfx, 0x18000) || !(C.sprites = arc_gfx_decode(&sl, gfx, 0x18000))) return false;
    arc_layout tl = { 16, 16, 1024, 3, 256, false, false, true, {0, 0x8000 * 8, 0x8000 * 16}, kTileX, kTileY };
    if (!ARC_LOAD(kTiles, gfx, 0x18000) || !(C.tiles = arc_gfx_decode(&tl, gfx, 0x18000))) return false;
    free(gfx);
    for (int i = 0; i < 256; i++) {
        const uint32_t r = (prom[i] & 15) * 255u / 15, g = (prom[i + 0x100] & 15) * 255u / 15, b = (prom[i + 0x200] & 15) * 255u / 15;
        C.palette[i] = arc_rgb565(r << 16 | g << 8 | b);
    }
    C.dsw_a = C.dsw_b = 0xff;
    C.main.read = main_read;
    C.main.write = main_write;
    C.main.opcode_read = main_opcode;
    C.main.in = in_ff;
    C.main.out = out_nop;
    C.sound.read = sound_read;
    C.sound.write = sound_write;
    C.sound.in = in_ff;
    C.sound.out = out_nop;
    C.sound.cycles = sound_cycles;
    zd_init(&C.main);
    zd_init(&C.sound);
    ym2203d_init(&C.ym0, 1500000, 1.0f, 1.0f);
    ym2203d_init(&C.ym1, 1500000, 1.0f, 1.0f);
    C.per_line = (int)(CLOCK / (uint32_t)(SCANLINES * FPS));
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&C.main);
    zd_reset(&C.sound);
    ym2203d_reset(&C.ym0);
    ym2203d_reset(&C.ym1);
    C.scroll_x = C.scroll_y = 0;
    C.sound_command = 0;
    C.flip = false;
    C.sound_reset_held = true;
    C.in0 = C.in1 = C.in2 = 0xff;
}

int arc_run_frame(const int16_t **audio)
{
    C.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 246) {
            zd_set_irq(&C.main, ZD_HOLD, 0xd7);
            render_frame();
        }
        zd_run(&C.main, C.per_line);
        if (!C.sound_reset_held) zd_run(&C.sound, C.per_line);
    }
    if (audio) *audio = C.audio;
    return C.naudio;
}

const uint16_t *arc_frame(void) { return C.fb; }

static uint8_t joy(unsigned b)
{
    uint8_t v = 0xff;
    if (b & AP_RIGHT) v &= ~0x01;
    if (b & AP_LEFT) v &= ~0x02;
    if (b & AP_DOWN) v &= ~0x04;
    if (b & AP_UP) v &= ~0x08;
    if (b & AP_B1) v &= ~0x10;
    if (b & AP_B2) v &= ~0x20;
    return v;
}

void arc_input(const arc_in *in)
{
    uint8_t v = 0xff;
    if (in->p[0] & AP_START) v &= ~0x01;
    if (in->p[1] & AP_START) v &= ~0x02;
    if (in->coin2) v &= ~0x40;
    if (in->coin1) v &= ~0x80;
    C.in0 = v;
    C.in1 = joy(in->p[0]);
    C.in2 = joy(in->p[1]);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw_a = v;
    if (bank == 1) C.dsw_b = v;
}
