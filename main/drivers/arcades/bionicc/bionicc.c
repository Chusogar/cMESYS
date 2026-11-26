// bionicc.c - Bionic Commando (Capcom, 1987) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/bionicc (from dsp-emulator's
// bioniccommando_hw.pas, with MAME's priorities and palette). A 68000 at 12
// MHz (Musashi here, dsp-cpp has its own core), a Z80 with a YM2151 and an
// i8751 MCU that copies data between the 68000's RAM and the sound CPU while
// it holds the 68000 ("DMA"). Text, an 8x8 background, a 16x16 foreground
// split around the sprites, and 160 buffered sprites.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../cpu/mcs51dsp/mcs51dsp.h"
#include "../../../sound/ym2151.h"

static m68kd cpu;

#define MAIN_CLOCK 12000000
#define SOUND_CLOCK 3579545
#define MCU_CLOCK (6000000 / 12)
#define SCANLINES 260
#define FPS (6000000.0 / 384.0 / 260.0)
#define RATE 44100
#define W 256
#define H 224
#define SLICES 4

static const char *const kSets[] = { "bionicc", NULL };
static const arc_rom kSound[] = {{"ts_01b.4e", 0x8000, 0, 0xa9a6cafa}};
static const arc_rom kMcu[] = {{"ts.2f", 0x1000, 0, 0x3ed7f0be}};
static const arc_rom kChars[] = {{"tsu_08.8l", 0x8000, 0, 0x9bf0b7a2}};
static const arc_rom kBg[] = {{"tsu_07.5l", 0x8000, 0, 0x9469efa4}, {"tsu_06.4l", 0x8000, 0x8000, 0x40bf0eb4}};
static const arc_rom kFg[] = {
    {"ts_12.17f", 0x8000, 0x00000, 0xe4b4619e}, {"ts_11.15f", 0x8000, 0x08000, 0xab30237a},
    {"ts_17.17g", 0x8000, 0x10000, 0xdeb657e4}, {"ts_16.15g", 0x8000, 0x18000, 0xd363b5f9},
    {"ts_13.18f", 0x8000, 0x20000, 0xa8f5a004}, {"ts_18.18g", 0x8000, 0x28000, 0x3b36948c},
    {"ts_23.18j", 0x8000, 0x30000, 0xbbfbe58a}, {"ts_24.18k", 0x8000, 0x38000, 0xf156e564}};
static const arc_rom kSprites[] = {
    {"tse_10.13f", 0x8000, 0x00000, 0xd28eeacc}, {"tsu_09.11f", 0x8000, 0x08000, 0x6a049292},
    {"tse_15.13g", 0x8000, 0x10000, 0x9b5593c0}, {"tsu_14.11g", 0x8000, 0x18000, 0x46b2ad83},
    {"tse_20.13j", 0x8000, 0x20000, 0xb03db778}, {"tsu_19.11j", 0x8000, 0x28000, 0xb5c82722},
    {"tse_22.17j", 0x8000, 0x30000, 0xd4dedeb3}, {"tsu_21.15j", 0x8000, 0x38000, 0x98777006}};
// the 68000 program: even bytes in tse_02 / tse_03, odd bytes in tse_04 / tse_05
static const arc_rom kEven[] = {{"tse_02.1a", 0x10000, 0, 0xe4aeefaa}, {"tse_03.2a", 0x10000, 0x10000, 0xb2ac0a45}};
static const arc_rom kOdd[] = {{"tse_04.1b", 0x10000, 0, 0xd0c8ec75}, {"tse_05.2b", 0x10000, 0x10000, 0xa79cb406}};

const arc_info arc_game = { "bionicc", "Bionic Commando", "Capcom", 1987, W, H, FPS, RATE, kSets,
                            { "FIRE", "ARM", NULL, NULL }, ARC_JOY_8WAY };

static const int kX8[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static const int kY8[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kFgX[16] = {0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267};
static const int kFgY[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
static const int kSprX[16] = {0, 1, 2, 3, 4, 5, 6, 7, 128, 129, 130, 131, 132, 133, 134, 135};
static const int kSprY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 sound;
    mcs51 mcu;
    ym2151 ym;
    uint8_t *rom, *chars, *bg_tiles, *fg_tiles, *sprites;
    uint8_t ram[0x1000], wram[0x4000], tx_ram[0x1000], fg_ram[0x4000], bg_ram[0x4000], pal_ram[0x800];
    uint16_t sprite_buf[0x280];
    uint8_t sound_rom[0x8000], sound_ram[0x800];
    uint16_t palette[0x400];
    uint16_t scroll[4];
    uint16_t *screen, *fb;
    bool flip;
    uint8_t audiocpu_to_mcu, mcu_to_audiocpu, mcu_p1, mcu_p3;
    uint16_t inputs, dsw;
    double main_debt, sound_debt, mcu_debt;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *G_;
#define G (*G_)



static uint8_t read8(uint32_t a)
{
    a &= 0xfffff;
    if (a < 0x40000) return G.rom[a];
    if (a >= 0xe0000 && a < 0xe4000) return G.ram[a & 0xfff];
    if (a >= 0xe4000 && a < 0xe8000) {
        const uint16_t w = (a & 2) ? G.dsw : G.inputs;
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (a >= 0xec000 && a < 0xf0000) return G.tx_ram[a & 0xfff];
    if (a >= 0xf0000 && a < 0xf4000) return G.fg_ram[a & 0x3fff];
    if (a >= 0xf4000 && a < 0xf8000) return G.bg_ram[a & 0x3fff];
    if (a >= 0xf8000 && a < 0xf8800) return G.pal_ram[a & 0x7ff];
    if (a >= 0xfc000) return G.wram[a & 0x3fff];
    return 0xff;
}

static void set_palette(int i)
{
    // RRRRGGGGBBBBIIII: when I bit 3 is clear the colour is dimmed
    const uint16_t raw = (uint16_t)(G.pal_ram[i * 2] << 8 | G.pal_ram[i * 2 + 1]);
    const int bright = raw & 0x0f;
    int r = ((raw >> 12) & 0x0f) * 0x11, g = ((raw >> 8) & 0x0f) * 0x11, b = ((raw >> 4) & 0x0f) * 0x11;
    if ((bright & 0x08) == 0) {
        r = (int)(r / 2 + bright * 18.15);
        g = (int)(g / 2 + bright * 18.15);
        b = (int)(b / 2 + bright * 18.15);
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    G.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
}

static void write8(uint32_t a, uint8_t v)
{
    a &= 0xfffff;
    if (a < 0x40000) return;
    if (a >= 0xe0000 && a < 0xe4000) G.ram[a & 0xfff] = v;
    else if (a >= 0xe4000 && a < 0xe8000) {
        if ((a & 3) == 0) G.flip = (v & 1) != 0;
        else if ((a & 3) == 2) zd_set_nmi(&G.sound, ZD_PULSE);
    } else if (a >= 0xe8010 && a < 0xe8018) {
        uint16_t *s = &G.scroll[(a - 0xe8010) / 2];
        *s = (a & 1) ? (uint16_t)((*s & 0xff00) | v) : (uint16_t)((*s & 0x00ff) | (v << 8));
    } else if (a == 0xe8018 || a == 0xe8019) {
        for (int i = 0; i < 0x280; i++) G.sprite_buf[i] = (uint16_t)(G.ram[0x800 + i * 2] << 8 | G.ram[0x800 + i * 2 + 1]);
    } else if (a == 0xe801a || a == 0xe801b) {
        // DMA on: the MCU's INT0 copies data while the 68000 is held
        mcs51_set_irq0_line(&G.mcu, MCS51_ASSERT);
        m68kd_set_halt_line(&cpu, M68_ASSERT);
    } else if (a >= 0xec000 && a < 0xf0000) G.tx_ram[a & 0xfff] = v;
    else if (a >= 0xf0000 && a < 0xf4000) G.fg_ram[a & 0x3fff] = v;
    else if (a >= 0xf4000 && a < 0xf8000) G.bg_ram[a & 0x3fff] = v;
    else if (a >= 0xf8000 && a < 0xf8800) {
        G.pal_ram[a & 0x7ff] = v;
        set_palette((int)((a & 0x7ff) >> 1));
    } else if (a >= 0xfc000) G.wram[a & 0x3fff] = v;
}

static uint8_t cpu_rd8(void *u_, uint32_t a) { (void)u_; return read8(a); }
static uint16_t cpu_rd(void *u_, uint32_t a)
{
    (void)u_;
    a &= 0xffffe;
    if (a < 0x40000) return (unsigned)(G.rom[a] << 8 | G.rom[a + 1]);
    return (unsigned)(read8(a) << 8 | read8(a + 1));
}
static void cpu_wr8(void *u_, uint32_t a, uint8_t v) { (void)u_; write8(a, (uint8_t)v); }
static void cpu_wr(void *u_, uint32_t a, uint16_t v)
{
    (void)u_;
    a &= 0xffffe;
    write8(a, (uint8_t)(v >> 8));
    if (a == 0xe8018 || a == 0xe801a) return;  // one strobe per word access
    write8(a + 1, (uint8_t)v);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return G.sound_rom[a];
    if (a == 0x8000 || a == 0x8001) return ym2151_status(&G.ym);
    if (a == 0xa000) return G.mcu_to_audiocpu;
    if (a >= 0xc000 && a < 0xc800) return G.sound_ram[a & 0x7ff];
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a == 0x8000) ym2151_select(&G.ym, v);
    else if (a == 0x8001) ym2151_write(&G.ym, v);
    else if (a == 0xa000) G.audiocpu_to_mcu = v;
    else if (a >= 0xc000 && a < 0xc800) G.sound_ram[a & 0x7ff] = v;
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&G.ym, cycles);
    G.audio_acc += (int64_t)cycles * RATE;
    while (G.audio_acc >= SOUND_CLOCK) {
        G.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&G.ym) * 6 / 10;
        if (G.naudio < 1024) G.audio[G.naudio++] = arc_clamp16(s);
    }
}

static uint8_t mcu_p1_read(void *u) { (void)u; return G.audiocpu_to_mcu; }
static void mcu_p1_write(void *u, uint8_t v) { (void)u; G.mcu_p1 = v; }
static void mcu_p3_write(void *u, uint8_t v)
{
    (void)u;
    // 7 read strobe, 6 write strobe, 5 dma, 4 int1 ack, 0 int0 ack
    if ((G.mcu_p3 & 0x01) && !(v & 0x01)) {
        mcs51_set_irq0_line(&G.mcu, MCS51_CLEAR);
        m68kd_set_halt_line(&cpu, M68_CLEAR);
    }
    if ((G.mcu_p3 & 0x10) && !(v & 0x10)) mcs51_set_irq1_line(&G.mcu, MCS51_CLEAR);
    if ((G.mcu_p3 & 0x40) && !(v & 0x40)) G.mcu_to_audiocpu = G.mcu_p1;
    G.mcu_p3 = v;
}
// MOVX: the MCU reaches the 68000 bus (odd bytes) while it holds the CPU
static uint8_t mcu_ext_read(void *u, uint16_t off)
{
    (void)u;
    if (G.mcu_p3 & 0x20) return 0xff;
    return read8(0xe3e01u | ((off & 0x700u) << 6) | ((off & 0xffu) << 1));
}
static void mcu_ext_write(void *u, uint16_t off, uint8_t v)
{
    (void)u;
    if (G.mcu_p3 & 0x20) return;
    write8(0xe3e01u | ((off & 0x700u) << 6) | ((off & 0xffu) << 1), v);
}

// one tile into the 256x256 screen; mask bit n set = pen n transparent
static void draw_tile(const uint8_t *src, int size, int x, int y, bool flipx, bool flipy, const uint16_t *pal, uint32_t mask)
{
    for (int ty = 0; ty < size; ty++) {
        const int py = y + ty;
        if (py < 0 || py >= 256) continue;
        const uint8_t *row = src + (flipy ? size - 1 - ty : ty) * size;
        for (int tx = 0; tx < size; tx++) {
            const int px = x + tx;
            if (px < 0 || px >= 256) continue;
            const uint8_t pen = row[flipx ? size - 1 - tx : tx];
            if (mask & (1u << pen)) continue;
            G.screen[py * 256 + px] = pal[pen];
        }
    }
}

static inline uint16_t word(const uint8_t *m, int i) { return (uint16_t)(m[i * 2] << 8 | m[i * 2 + 1]); }

// the 16x16 foreground (64x64): pass 0 the tiles behind the background (both
// flip bits set), 1 its back half, 2 its front half
static void draw_fg(int pass)
{
    const int fg_sx = G.scroll[0] & 0x3ff, fg_sy = G.scroll[1] & 0x3ff;
    for (int ty = 0; ty < 17; ty++)
        for (int tx = 0; tx < 17; tx++) {
            const int col = ((fg_sx >> 4) + tx) & 63, row = ((fg_sy >> 4) + ty) & 63;
            const int idx = row * 64 + col;
            const int attr = word(G.fg_ram, idx * 2 + 1) & 0xff;
            const int code = ((attr & 7) << 8) | (word(G.fg_ram, idx * 2) & 0xff);
            const bool behind = (attr & 0xc0) == 0xc0;
            if ((pass == 0) != behind) continue;
            const int group = behind ? 0 : (attr & 0x20) >> 5;
            const uint32_t mask = pass == 2 ? (group ? 0xffc1u : 0xffffu) : (group ? 0x803eu : 0x8000u);
            if (mask == 0xffff) continue;
            const bool fx = !behind && (attr & 0x80), fy = !behind && (attr & 0x40);
            draw_tile(G.fg_tiles + code * 256, 16, tx * 16 - (fg_sx & 15), ty * 16 - (fg_sy & 15), fx, fy,
                      G.palette + 256 + ((attr & 0x18) >> 3) * 16, mask);
        }
}

static void update_video(void)
{
    const uint16_t black = arc_rgb565(0);
    for (int i = 0; i < 256 * 256; i++) G.screen[i] = black;
    const int bg_sx = G.scroll[2] & 0x1ff, bg_sy = G.scroll[3] & 0x1ff;
    draw_fg(0);
    for (int ty = 0; ty < 33; ty++)
        for (int tx = 0; tx < 33; tx++) {
            const int col = ((bg_sx >> 3) + tx) & 63, row = ((bg_sy >> 3) + ty) & 63;
            const int idx = row * 64 + col;
            const int attr = word(G.bg_ram, idx * 2 + 1) & 0xff;
            const int code = ((attr & 7) << 8) | (word(G.bg_ram, idx * 2) & 0xff);
            draw_tile(G.bg_tiles + code * 64, 8, tx * 8 - (bg_sx & 7), ty * 8 - (bg_sy & 7), (attr & 0x80) != 0,
                      (attr & 0x40) != 0, G.palette + ((attr & 0x38) >> 3) * 16, 0x8000);
        }
    draw_fg(1);
    for (int i = 0x9f; i >= 0; i--) {   // the last sprite first, pen 15 transparent
        const uint16_t *s = &G.sprite_buf[i * 4];
        const int code = s[0] & 0x7ff;
        if (code == 0x7ff) continue;
        const int attr = s[1];
        int sy = s[2] & 0x1ff, sx = s[3] & 0x1ff;
        if (sy & 0x100) sy -= 0x200;
        if (sx & 0x100) sx -= 0x200;
        draw_tile(G.sprites + code * 256, 16, sx, sy, (attr & 2) != 0, (attr & 1) != 0,
                  G.palette + 512 + ((attr >> 2) & 0x0f) * 16, 0x8000);
    }
    draw_fg(2);
    for (int idx = 0; idx < 0x400; idx++) {
        const int attr = word(G.tx_ram, idx + 0x400) & 0xff;
        const int code = ((attr & 0xc0) << 2) | (word(G.tx_ram, idx) & 0xff);
        draw_tile(G.chars + code * 64, 8, (idx & 31) * 8, (idx >> 5) * 8, (attr & 0x10) != 0, (attr & 0x20) != 0,
                  G.palette + 768 + (attr & 0x0f) * 4, 0x8);
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int sx = G.flip ? 255 - x : x, sy = G.flip ? 255 - (y + 16) : y + 16;
            G.fb[y * W + x] = G.screen[sy * 256 + sx];
        }
}

bool arc_init(void)
{
    if (!(G_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    G.rom = (uint8_t *)malloc(0x40000);
    G.screen = (uint16_t *)calloc(256 * 256, 2);
    G.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *gfx = (uint8_t *)calloc(1, 0x40000);
    if (!G.rom || !G.screen || !G.fb || !gfx) return false;
    memset(G.rom, 0xff, 0x40000);
    if (!ARC_LOAD(kEven, gfx, 0x20000) || !ARC_LOAD(kOdd, gfx + 0x20000, 0x20000)) return false;
    for (int i = 0; i < 0x20000; i++) {
        G.rom[i * 2] = gfx[i];
        G.rom[i * 2 + 1] = gfx[0x20000 + i];
    }
    if (!ARC_LOAD(kSound, G.sound_rom, 0x8000)) return false;
    mcs51_init(&G.mcu);
    G.mcu.clock = MCU_CLOCK;
    if (!ARC_LOAD(kMcu, G.mcu.rom, 0x1000)) return false;
    memset(gfx, 0, 0x40000);
    arc_layout cl = { 8, 8, 1024, 2, 128, false, false, false, {4, 0}, kX8, kY8 };
    if (!ARC_LOAD(kChars, gfx, 0x8000) || !(G.chars = arc_gfx_decode(&cl, gfx, 0x8000))) return false;
    arc_layout bl = { 8, 8, 2048, 4, 128, false, false, false, {0x8000 * 8 + 4, 0x8000 * 8, 4, 0}, kX8, kY8 };
    if (!ARC_LOAD(kBg, gfx, 0x10000) || !(G.bg_tiles = arc_gfx_decode(&bl, gfx, 0x10000))) return false;
    arc_layout fl = { 16, 16, 2048, 4, 512, false, false, false, {0x20000 * 8 + 4, 0x20000 * 8, 4, 0}, kFgX, kFgY };
    if (!ARC_LOAD(kFg, gfx, 0x40000) || !(G.fg_tiles = arc_gfx_decode(&fl, gfx, 0x40000))) return false;
    arc_layout sl = { 16, 16, 2048, 4, 256, false, false, false, {0x30000 * 8, 0x20000 * 8, 0x10000 * 8, 0}, kSprX, kSprY };
    if (!ARC_LOAD(kSprites, gfx, 0x40000) || !(G.sprites = arc_gfx_decode(&sl, gfx, 0x40000))) return false;
    free(gfx);
    G.sound.read = sound_read;
    G.sound.write = sound_write;
    G.sound.in = in_ff;
    G.sound.out = out_nop;
    G.sound.cycles = sound_cycles;
    zd_init(&G.sound);
    G.mcu.port_read[1] = mcu_p1_read;
    G.mcu.port_write[1] = mcu_p1_write;
    G.mcu.port_write[3] = mcu_p3_write;
    G.mcu.read_external = mcu_ext_read;
    G.mcu.write_external = mcu_ext_write;
    ym2151_init(&G.ym, SOUND_CLOCK, RATE);
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    cpu.read_byte = cpu_rd8;
    cpu.write_byte = cpu_wr8;
    m68kd_set_address_mask(&cpu, 0xfffff);
    G.dsw = 0xdfff;
    G.inputs = 0xffff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(G.ram, 0, sizeof G.ram);
    memset(G.wram, 0, sizeof G.wram);
    memset(G.tx_ram, 0, sizeof G.tx_ram);
    memset(G.fg_ram, 0, sizeof G.fg_ram);
    memset(G.bg_ram, 0, sizeof G.bg_ram);
    memset(G.pal_ram, 0, sizeof G.pal_ram);
    memset(G.sound_ram, 0, sizeof G.sound_ram);
    memset(G.sprite_buf, 0, sizeof G.sprite_buf);
    memset(G.scroll, 0, sizeof G.scroll);
    for (int i = 0; i < 0x400; i++) G.palette[i] = 0;
    G.flip = false;
    G.audiocpu_to_mcu = G.mcu_to_audiocpu = G.mcu_p1 = G.mcu_p3 = 0;
    G.main_debt = G.sound_debt = G.mcu_debt = 0;
    G.audio_acc = 0;
    m68kd_reset(&cpu);
    zd_reset(&G.sound);
    mcs51_reset(&G.mcu);
    ym2151_reset(&G.ym);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = MAIN_CLOCK / FPS / SCANLINES / SLICES;
    const double sound_cycles_ = SOUND_CLOCK / FPS / SCANLINES / SLICES;
    const double mcu_cycles = MCU_CLOCK / FPS / SCANLINES / SLICES;
    G.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 128) m68kd_set_irq(&cpu, 4, M68_HOLD);   // inputs
        if (line == 240) {                                                  // vblank
            m68kd_set_irq(&cpu, 2, M68_HOLD);
            update_video();
        }
        for (int s = 0; s < SLICES; s++) {
            G.main_debt += main_cycles;
            G.main_debt -= m68kd_run(&cpu, (int)G.main_debt);
            G.sound_debt += sound_cycles_;
            G.sound_debt -= zd_run(&G.sound, (int)G.sound_debt);
            G.mcu_debt += mcu_cycles;
            G.mcu_debt -= mcs51_run(&G.mcu, (int)G.mcu_debt);
        }
    }
    if (audio) *audio = G.audio;
    return G.naudio;
}

const uint16_t *arc_frame(void) { return G.fb; }

void arc_input(const arc_in *in)
{
    uint16_t v = 0xffff;
    const unsigned p1 = in->p[0], p2 = in->p[1];
    if (p2 & AP_B2) v &= ~0x0001;
    if (p2 & AP_B1) v &= ~0x0002;
    if (p2 & AP_RIGHT) v &= ~0x0004;
    if (p2 & AP_LEFT) v &= ~0x0008;
    if (p2 & AP_DOWN) v &= ~0x0010;
    if (p2 & AP_UP) v &= ~0x0020;
    if (p1 & AP_B2) v &= ~0x0040;
    if (p1 & AP_B1) v &= ~0x0080;
    if (p1 & AP_RIGHT) v &= ~0x0100;
    if (p1 & AP_LEFT) v &= ~0x0200;
    if (p1 & AP_DOWN) v &= ~0x0400;
    if (p1 & AP_UP) v &= ~0x0800;
    if (p2 & AP_START) v &= ~0x1000;
    if (p1 & AP_START) v &= ~0x2000;
    if (in->coin2) v &= ~0x4000;
    if (in->coin1) v &= ~0x8000;
    G.inputs = v;
}

// bank 0: the low byte (switch B: coinage, service, flip); bank 1: the high
// byte (switch A: lives, cabinet, bonus, difficulty, freeze)
void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) G.dsw = (uint16_t)((G.dsw & 0xff00) | v);
    if (bank == 1) G.dsw = (uint16_t)((G.dsw & 0x00ff) | (v << 8));
}
