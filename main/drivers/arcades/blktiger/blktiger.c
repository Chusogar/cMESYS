// blktiger.c - Black Tiger (Capcom, 1987) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/blktiger (from dsp-emulator's
// blacktiger_hw.pas, with MAME's tilemap layouts and priorities). A banked Z80
// at 6 MHz, a sound Z80 with two YM2203, an i8751 protection MCU on a latch
// pair, a 16x16 scrolling background split in front of / behind the sprites by
// colour, 8x8 text and 128 buffered sprites.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mcs51dsp/mcs51dsp.h"
#include "../../../sound/ym2203dsp.h"

#define MAIN_CLOCK 6000000
#define SOUND_CLOCK 3579545
#define MCU_CLOCK (8000000 / 12)
#define SCANLINES 262
#define FPS (24000000.0 / 4.0 / 384.0 / 262.0)
#define RATE 44100
#define W 256
#define H 224
#define SLICES 2

static const char *const kSets[] = { "blktiger", NULL };
static const arc_rom kMain[] = {
    {"bdu-01a.5e", 0x8000, 0x00000, 0xa8f98f22}, {"bdu-02a.6e", 0x10000, 0x08000, 0x7bef96e8},
    {"bdu-03a.8e", 0x10000, 0x18000, 0x4089e157}, {"bd-04.9e", 0x10000, 0x28000, 0xed6af6ec},
    {"bd-05.10e", 0x10000, 0x38000, 0xae59b72e}};
static const arc_rom kSound[] = {{"bd-06.1l", 0x8000, 0, 0x2cf54274}};
static const arc_rom kMcu[] = {{"bd.6k", 0x1000, 0, 0xac7d14f1}};
static const arc_rom kChars[] = {{"bd-15.2n", 0x8000, 0, 0x70175d78}};
static const arc_rom kTiles[] = {
    {"bd-12.5b", 0x10000, 0x00000, 0xc4524993}, {"bd-11.4b", 0x10000, 0x10000, 0x7932c86f},
    {"bd-14.9b", 0x10000, 0x20000, 0xdc49593a}, {"bd-13.8b", 0x10000, 0x30000, 0x7ed7a122}};
static const arc_rom kSprites[] = {
    {"bd-08.5a", 0x10000, 0x00000, 0xe2f17438}, {"bd-07.4a", 0x10000, 0x10000, 0x5fccbd27},
    {"bd-10.9a", 0x10000, 0x20000, 0xfc33ccc6}, {"bd-09.8a", 0x10000, 0x30000, 0xf449de01}};

const arc_info arc_game = { "blktiger", "Black Tiger", "Capcom", 1987, W, H, FPS, RATE, kSets,
                            { "ATTACK", "JUMP", NULL, NULL }, ARC_JOY_8WAY };

static const uint8_t kSplitTable[16] = {3, 3, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint16_t kFrontTransMask[4] = {0xffff, 0xfff0, 0xff00, 0xf000};
static const uint16_t kBackTransMask[4] = {0x8000, 0x800f, 0x80ff, 0x8fff};
static const int kCharX[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static const int kCharY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kX16[16] = {0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267};
static const int kY16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sound;
    mcs51 mcu;
    ym2203d ym0, ym1;
    uint8_t *rom;                       // 0000-7fff, then 16 banks of 16 KB
    uint8_t *chars, *tiles, *sprites;
    uint8_t scroll_ram[0x4000], tx_ram[0x800], pal_ram[0x800], wram[0x2000], sprite_buf[0x200];
    uint8_t sound_rom[0x8000], sound_ram[0x800];
    uint16_t palette[0x400];
    uint16_t *screen, *fb;              // 256x256 composition, visible rows 16..239
    uint8_t bank, soundlatch, i8751_latch, z80_latch;
    uint16_t scroll_x, scroll_y, scroll_bank;
    bool screen_layout, ch_on, bg_on, obj_on, flip, sound_reset;
    uint8_t in0, in1, in2, dsw_a, dsw_b;
    double main_debt, sound_debt, mcu_debt;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *B_;
#define B (*B_)

static uint16_t pal4(int r, int g, int b)
{
    r &= 15, g &= 15, b &= 15;
    return arc_rgb565((uint32_t)(r * 17) << 16 | (uint32_t)(g * 17) << 8 | (uint32_t)(b * 17));
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return B.rom[a];
    if (a < 0xc000) return B.rom[0x8000 + B.bank * 0x4000 + (a & 0x3fff)];
    if (a < 0xd000) return B.scroll_ram[B.scroll_bank + (a & 0xfff)];
    if (a < 0xd800) return B.tx_ram[a & 0x7ff];
    if (a < 0xe000) return B.pal_ram[a & 0x7ff];
    return B.wram[a & 0x1fff];
}

static void set_color(int i) { B.palette[i] = pal4(B.pal_ram[i] >> 4, B.pal_ram[i], B.pal_ram[i + 0x400]); }

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0xc000) return;
    if (a < 0xd000) B.scroll_ram[B.scroll_bank + (a & 0xfff)] = v;
    else if (a < 0xd800) B.tx_ram[a & 0x7ff] = v;
    else if (a < 0xe000) {
        B.pal_ram[a & 0x7ff] = v;
        set_color(a & 0x3ff);
    } else B.wram[a & 0x1fff] = v;
}

static uint8_t main_in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0: return B.in0;
    case 1: return B.in1;
    case 2: return B.in2;
    case 3: return B.dsw_a;
    case 4: return B.dsw_b;
    case 5: return 0x01;                // the "freeze" switch off
    case 7: return B.i8751_latch;
    default: return 0xff;
    }
}

static void main_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0x00: B.soundlatch = v; break;
    case 0x01: B.bank = v & 0x0f; break;
    case 0x04: {                        // 5 sound CPU reset, 6 flip, 7 text off
        B.ch_on = (v & 0x80) == 0;
        B.flip = (v & 0x40) != 0;
        const bool held = (v & 0x20) != 0;
        if (held && !B.sound_reset) zd_reset(&B.sound);
        B.sound_reset = held;
        break;
    }
    case 0x07:
        B.z80_latch = v;
        mcs51_set_irq1_line(&B.mcu, MCS51_ASSERT);
        break;
    case 0x08: B.scroll_x = (uint16_t)((B.scroll_x & 0xff00) | v); break;
    case 0x09: B.scroll_x = (uint16_t)((B.scroll_x & 0x00ff) | (v << 8)); break;
    case 0x0a: B.scroll_y = (uint16_t)((B.scroll_y & 0xff00) | v); break;
    case 0x0b: B.scroll_y = (uint16_t)((B.scroll_y & 0x00ff) | (v << 8)); break;
    case 0x0c:
        B.bg_on = (v & 0x02) == 0;
        B.obj_on = (v & 0x04) == 0;
        break;
    case 0x0d: B.scroll_bank = (uint16_t)((v & 3) << 12); break;
    case 0x0e: B.screen_layout = (v & 1) != 0; break;
    default: break;
    }
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return B.sound_rom[a];
    if (a >= 0xc000 && a < 0xc800) return B.sound_ram[a & 0x7ff];
    if (a == 0xc800) return B.soundlatch;
    if (a == 0xe000) return ym2203d_status(&B.ym0);
    if (a == 0xe001) return ym2203d_read(&B.ym0);
    if (a == 0xe002) return ym2203d_status(&B.ym1);
    if (a == 0xe003) return ym2203d_read(&B.ym1);
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xc000 && a < 0xc800) { B.sound_ram[a & 0x7ff] = v; return; }
    switch (a) {
    case 0xe000: ym2203d_control(&B.ym0, v); break;
    case 0xe001: ym2203d_write(&B.ym0, v); break;
    case 0xe002: ym2203d_control(&B.ym1, v); break;
    case 0xe003: ym2203d_write(&B.ym1, v); break;
    default: break;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// the YM2203 timers advance per sample, so the chips are clocked with the CPU
static void sound_cycles(void *u, int cycles)
{
    (void)u;
    B.audio_acc += (int64_t)cycles * RATE;
    while (B.audio_acc >= SOUND_CLOCK) {
        B.audio_acc -= SOUND_CLOCK;
        const int32_t s = (ym2203d_update(&B.ym0) + ym2203d_update(&B.ym1)) * 3 / 4;
        if (B.naudio < 1024) B.audio[B.naudio++] = arc_clamp16(s);
    }
}

static void ym0_irq(void *u, bool on) { (void)u; zd_set_irq(&B.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

static uint8_t mcu_p0_read(void *u)
{
    (void)u;
    mcs51_set_irq1_line(&B.mcu, MCS51_CLEAR);
    return B.z80_latch;
}
static void mcu_p0_write(void *u, uint8_t v) { (void)u; B.i8751_latch = v; }

static inline void bg_pixel(int x, int y, int *pen, int *group, int *color)
{
    const int bg_w = B.screen_layout ? 2048 : 1024, bg_h = B.screen_layout ? 1024 : 2048;
    const int px = (x + B.scroll_x) & (bg_w - 1), py = (y + B.scroll_y) & (bg_h - 1);
    const int col = px >> 4, row = py >> 4;
    const int index = B.screen_layout
        ? (col & 0x0f) + ((row & 0x0f) << 4) + ((col & 0x70) << 4) + ((row & 0x30) << 7)
        : (col & 0x0f) + ((row & 0x0f) << 4) + ((col & 0x30) << 4) + ((row & 0x70) << 6);
    const uint8_t attr = B.scroll_ram[index * 2 + 1];
    const int code = B.scroll_ram[index * 2] | ((attr & 0x07) << 8);
    *color = (attr & 0x78) >> 3;
    *group = kSplitTable[*color];
    int tx = px & 15;
    if (attr & 0x80) tx = 15 - tx;
    *pen = B.tiles[code * 256 + (py & 15) * 16 + tx];
}

static void bg_half(const uint16_t *mask)
{
    for (int y = 16; y < 240; y++)
        for (int x = 0; x < 256; x++) {
            int pen, group, color;
            bg_pixel(x, y, &pen, &group, &color);
            if (!(mask[group] >> pen & 1)) B.screen[y * 256 + x] = B.palette[color * 16 + pen];
        }
}

static void update_video(void)
{
    for (int i = 0; i < 256 * 256; i++) B.screen[i] = B.palette[0x3ff];
    if (B.bg_on) bg_half(kBackTransMask);
    if (B.obj_on) {
        for (int offs = 0x200 - 4; offs >= 0; offs -= 4) {
            const uint8_t attr = B.sprite_buf[offs + 1];
            const int sx = B.sprite_buf[offs + 3] - ((attr & 0x10) << 4);
            const int sy = B.sprite_buf[offs + 2];
            const int code = B.sprite_buf[offs] | ((attr & 0xe0) << 3);
            const int base = 0x200 + (attr & 0x07) * 16;
            const bool flip_x = (attr & 0x08) != 0;
            const uint8_t *g = B.sprites + code * 256;
            for (int r = 0; r < 16; r++) {
                const int y = sy + r;
                if (y < 16 || y >= 240) continue;
                for (int c = 0; c < 16; c++) {
                    const int x = sx + c;
                    if (x < 0 || x >= 256) continue;
                    const int pen = g[r * 16 + (flip_x ? 15 - c : c)];
                    if (pen != 15) B.screen[y * 256 + x] = B.palette[base + pen];
                }
            }
        }
    }
    if (B.bg_on) bg_half(kFrontTransMask);
    if (B.ch_on) {
        for (int i = 0; i < 0x400; i++) {
            const int ty = i >> 5, tx = i & 31;
            if (ty < 2 || ty >= 30) continue;
            const uint8_t attr = B.tx_ram[i + 0x400];
            const int code = B.tx_ram[i] | ((attr & 0xe0) << 3);
            const int base = 0x300 + (attr & 0x1f) * 4;
            const uint8_t *g = B.chars + code * 64;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    const int pen = g[r * 8 + c];
                    if (pen != 3) B.screen[(ty * 8 + r) * 256 + tx * 8 + c] = B.palette[base + pen];
                }
        }
    }
    for (int y = 0; y < H; y++) {
        uint16_t *dst = &B.fb[y * W];
        if (!B.flip) memcpy(dst, &B.screen[(y + 16) * 256], 256 * 2);
        else {
            const uint16_t *src = &B.screen[(239 - y) * 256];
            for (int x = 0; x < 256; x++) dst[x] = src[255 - x];
        }
    }
}

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    B.rom = (uint8_t *)calloc(1, 0x48000);
    B.screen = (uint16_t *)calloc(256 * 256, 2);
    B.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *gfx = (uint8_t *)calloc(1, 0x40000);
    if (!B.rom || !B.screen || !B.fb || !gfx) return false;
    if (!ARC_LOAD(kMain, B.rom, 0x48000) || !ARC_LOAD(kSound, B.sound_rom, 0x8000)) return false;
    mcs51_init(&B.mcu);
    B.mcu.clock = MCU_CLOCK;
    if (!ARC_LOAD(kMcu, B.mcu.rom, 0x1000)) return false;
    arc_layout cl = { 8, 8, 2048, 2, 128, false, false, false, {4, 0}, kCharX, kCharY };
    if (!ARC_LOAD(kChars, gfx, 0x8000) || !(B.chars = arc_gfx_decode(&cl, gfx, 0x8000))) return false;
    arc_layout l16 = { 16, 16, 2048, 4, 512, false, false, false, {0x20000 * 8 + 4, 0x20000 * 8, 4, 0}, kX16, kY16 };
    if (!ARC_LOAD(kTiles, gfx, 0x40000) || !(B.tiles = arc_gfx_decode(&l16, gfx, 0x40000))) return false;
    memset(gfx, 0, 0x40000);
    if (!ARC_LOAD(kSprites, gfx, 0x40000) || !(B.sprites = arc_gfx_decode(&l16, gfx, 0x40000))) return false;
    free(gfx);
    B.main.read = main_read;
    B.main.write = main_write;
    B.main.in = main_in;
    B.main.out = main_out;
    B.sound.read = sound_read;
    B.sound.write = sound_write;
    B.sound.in = in_ff;
    B.sound.out = out_nop;
    B.sound.cycles = sound_cycles;
    zd_init(&B.main);
    zd_init(&B.sound);
    B.mcu.port_read[0] = mcu_p0_read;
    B.mcu.port_write[0] = mcu_p0_write;
    ym2203d_init(&B.ym0, SOUND_CLOCK, 1.0f, 1.0f);
    ym2203d_init(&B.ym1, SOUND_CLOCK, 1.0f, 1.0f);
    B.ym0.irq = ym0_irq;
    B.dsw_a = 0xff;
    B.dsw_b = 0x6f;
    B.in0 = B.in1 = B.in2 = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(B.scroll_ram, 0, sizeof B.scroll_ram);
    memset(B.tx_ram, 0, sizeof B.tx_ram);
    memset(B.pal_ram, 0, sizeof B.pal_ram);
    memset(B.wram, 0, sizeof B.wram);
    memset(B.sprite_buf, 0, sizeof B.sprite_buf);
    memset(B.sound_ram, 0, sizeof B.sound_ram);
    for (int i = 0; i < 0x400; i++) B.palette[i] = 0;
    B.bank = B.soundlatch = B.i8751_latch = B.z80_latch = 0;
    B.scroll_x = B.scroll_y = B.scroll_bank = 0;
    B.screen_layout = false;
    B.ch_on = B.bg_on = B.obj_on = true;
    B.flip = B.sound_reset = false;
    B.main_debt = B.sound_debt = B.mcu_debt = 0;
    B.audio_acc = 0;
    zd_reset(&B.main);
    zd_reset(&B.sound);
    mcs51_reset(&B.mcu);
    mcs51_set_irq1_line(&B.mcu, MCS51_CLEAR);
    ym2203d_reset(&B.ym0);
    ym2203d_reset(&B.ym1);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = MAIN_CLOCK / FPS / SCANLINES / SLICES;
    const double sound_cycles_ = SOUND_CLOCK / FPS / SCANLINES / SLICES;
    const double mcu_cycles = MCU_CLOCK / FPS / SCANLINES / SLICES;
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 246) {
            zd_set_irq(&B.main, ZD_HOLD, 0xff);
            memcpy(B.sprite_buf, B.wram + 0x1e00, 0x200);
            update_video();
        }
        for (int s = 0; s < SLICES; s++) {
            B.main_debt += main_cycles;
            B.main_debt -= zd_run(&B.main, (int)B.main_debt);
            B.sound_debt += sound_cycles_;
            if (B.sound_reset) {
                const int c = (int)B.sound_debt;
                sound_cycles(NULL, c);
                B.sound_debt -= c;
            } else B.sound_debt -= zd_run(&B.sound, (int)B.sound_debt);
            B.mcu_debt += mcu_cycles;
            B.mcu_debt -= mcs51_run(&B.mcu, (int)B.mcu_debt);
        }
    }
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.fb; }

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
    if (in->service) v &= ~0x20;
    if (in->coin1) v &= ~0x40;
    if (in->coin2) v &= ~0x80;
    B.in0 = v;
    B.in1 = joy(in->p[0]);
    B.in2 = joy(in->p[1]);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw_a = v;
    if (bank == 1) B.dsw_b = v;
}
