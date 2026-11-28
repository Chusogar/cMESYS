// pirates.c - Pirates and Genix Family (NIX, 1994) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/pirates (from dsp-emulator's
// pirates_hw.pas). A 68000 at 16 MHz (Musashi here) with its program, tiles,
// sprites and samples scrambled, an OKI M6295 with two 256 KB banks, a 93C46
// EEPROM for the settings (no DIP switches), two 512-wide scrolling layers of
// 8x8 tiles, a 288-wide text layer and 16x16 sprites.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../machine/eeprom93c46.h"
#include "../../../sound/okim6295dsp.h"

static m68kd cpu;

#define CLOCK 16000000
#define OKI_CLOCK 1333333
#define SCANLINES 256
#define VBLANK 240
#define FPS 60.0
#define RATE 44100
#define W 288
#define H 224
#define PLANE 0x80000

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define GENIX (ARC_VARIANT == 1)   // ARC_VARIANT: 0 pirates, 1 genix

#if !GENIX
static const char *const kSets[] = { "pirates", NULL };
static const arc_rom kProgLow[] = {{"r_449b.bin", 0x80000, 0, 0x224aeeda}};
static const arc_rom kProgHigh[] = {{"l_5c1e.bin", 0x80000, 0, 0x46740204}};
static const arc_rom kGfx[] = {
    {"p4_4d48.bin", 0x80000, 0 * PLANE, 0x89fda216}, {"p2_5d74.bin", 0x80000, 1 * PLANE, 0x40e069b4},
    {"p1_7b30.bin", 0x80000, 2 * PLANE, 0x26d78518}, {"p8_9f4f.bin", 0x80000, 3 * PLANE, 0xf31696ea}};
static const arc_rom kSprites[] = {
    {"s1_6e89.bin", 0x80000, 0 * PLANE, 0xc78a276f}, {"s2_6df3.bin", 0x80000, 1 * PLANE, 0x9f0bad96},
    {"s4_fdcc.bin", 0x80000, 2 * PLANE, 0x8916ddb5}, {"s8_4b7c.bin", 0x80000, 3 * PLANE, 0x1c41bd2c}};
static const arc_rom kOki[] = {{"s89_49d4.bin", 0x80000, 0, 0x63a739ec}};

const arc_info arc_game = { "pirates", "Pirates", "NIX", 1994, W, H, FPS, RATE, kSets,
                            { "FIRE", "JUMP", "B3", NULL }, ARC_JOY_8WAY };
#else
static const char *const kSets[] = { "genix", NULL };
static const arc_rom kProgLow[] = {{"11.u15.15c", 0x80000, 0, 0xd26abfb0}};
static const arc_rom kProgHigh[] = {{"12.u16.16c", 0x80000, 0, 0xa14a25b4}};
static const arc_rom kGfx[] = {   // (256 KB per plane, still at $80000 spacing)
    {"17.u34.12g", 0x40000, 0 * PLANE, 0x58da8aac}, {"19.u35.12h", 0x40000, 1 * PLANE, 0x96bad9a8},
    {"18.u48.13g", 0x40000, 2 * PLANE, 0x0ddc58b6}, {"20.u49.13h", 0x40000, 3 * PLANE, 0x2be308c5}};
static const arc_rom kSprites[] = {
    {"16.u69.6g", 0x40000, 0 * PLANE, 0xb8422af7}, {"15.u70.4g", 0x40000, 1 * PLANE, 0xe46125c5},
    {"14.u71.3g", 0x40000, 2 * PLANE, 0x7a8ed21b}, {"13.u72.1g", 0x40000, 3 * PLANE, 0xf78bd6ca}};
static const arc_rom kOki[] = {{"10.u31.1b", 0x80000, 0, 0x80d087bc}};

const arc_info arc_game = { "genix", "Genix Family", "NIX", 1994, W, H, FPS, RATE, kSets,
                            { "B1", "B2", "B3", NULL }, ARC_JOY_8WAY };
#endif

// bit permutations: the source bit of each destination bit, MSB first
static uint32_t bitswap(uint32_t v, const int *bits, int n)
{
    uint32_t out = 0;
    for (int i = 0; i < n; i++) out |= ((v >> bits[i]) & 1u) << (n - 1 - i);
    return out;
}
#define SW24(v, ...) bitswap((v), (const int[24]){__VA_ARGS__}, 24)
#define SW8(v, ...) (uint8_t)bitswap((v), (const int[8]){__VA_ARGS__}, 8)

static uint8_t sw_a(uint8_t v) { return SW8(v, 2, 3, 4, 0, 7, 5, 1, 6); }
static uint8_t sw_b(uint8_t v) { return SW8(v, 4, 2, 7, 1, 6, 5, 0, 3); }
static uint8_t sw_c(uint8_t v) { return SW8(v, 1, 4, 7, 0, 3, 5, 6, 2); }
static uint32_t tile_addr(uint32_t f) { return SW24(f, 23, 22, 21, 20, 19, 18, 10, 2, 5, 9, 7, 13, 16, 14, 11, 4, 1, 6, 12, 17, 3, 0, 15, 8); }
static uint32_t sprite_addr(uint32_t f) { return SW24(f, 23, 22, 21, 20, 19, 18, 17, 5, 12, 14, 8, 3, 0, 7, 9, 16, 4, 2, 6, 11, 13, 1, 10, 15); }

static const int kX8[8] = {7, 6, 5, 4, 3, 2, 1, 0};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8};
static const int kY16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};

// the machine on the heap
static struct state {
    okim6295d oki;
    eeprom93c46_t eeprom;
    uint16_t *rom;                      // the decrypted program, 512K words
    uint8_t *tiles, *sprites, *sound;   // sound: the two 256 KB banks
    uint16_t work_ram[0x8000], sprite_ram[0x800], palette_ram[0x2000], tile_ram[0x4000];
    uint16_t palette[0x2000];
    uint16_t canvas[512 * 256];
    uint16_t *fb;
    uint8_t in0;
    uint16_t in1, scroll_x;
    bool irq;
    int64_t oki_acc, audio_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
} *P_;
#define P (*P_)


static uint16_t rd16(uint32_t a)
{
    a &= 0xfffffe;
    if (a <= 0xfffff) return P.rom[a >> 1];
    if (a >= 0x100000 && a <= 0x10ffff) {
#if GENIX
        if ((a & 0xffff) == 0x9e98) return 4;   // (the protection, as dsp-cpp)
        if ((a & 0xffff) == 0x9e9a) return 0;
#endif
        return P.work_ram[(a & 0xffff) >> 1];
    }
    if (a == 0x300000) return P.in1;
#if GENIX
    if (a == 0x400000) return P.in0;
#else
    if (a == 0x400000) return (uint16_t)(P.in0 | (eeprom93c46_do_read(&P.eeprom) << 4));
#endif
    if (a >= 0x500000 && a <= 0x500fff) return P.sprite_ram[(a & 0xfff) >> 1];
    if (a >= 0x800000 && a <= 0x803fff) return P.palette_ram[(a & 0x3fff) >> 1];
    if (a >= 0x900000 && a <= 0x907fff) return P.tile_ram[(a & 0x7fff) >> 1];
    if (a == 0xa00000) return okid_read(&P.oki);
    return 0xffff;
}

static void wr16(uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a <= 0xfffff) return;
    if (a >= 0x100000 && a <= 0x10ffff) P.work_ram[(a & 0xffff) >> 1] = v;
    else if (a >= 0x500000 && a <= 0x500fff) P.sprite_ram[(a & 0xfff) >> 1] = v;
    else if (a == 0x600000) {
        eeprom93c46_di_write(&P.eeprom, (v >> 2) & 1);
        eeprom93c46_cs_write(&P.eeprom, v & 1);
        eeprom93c46_clk_write(&P.eeprom, (v >> 1) & 1);
        P.oki.bank_offset = (v & 0x40) ? 0x40000 : 0;
    } else if (a == 0x700000) P.scroll_x = v & 0x1ff;
    else if (a >= 0x800000 && a <= 0x803fff) {
        const int i = (a & 0x3fff) >> 1;
        P.palette_ram[i] = v;
        P.palette[i] = arc_rgb565((uint32_t)((v >> 10) & 0x1f) * 255 / 31 << 16 |
                                  (uint32_t)((v >> 5) & 0x1f) * 255 / 31 << 8 | (uint32_t)(v & 0x1f) * 255 / 31);
    } else if (a >= 0x900000 && a <= 0x907fff) P.tile_ram[(a & 0x7fff) >> 1] = v;
    else if (a == 0xa00000) okid_write(&P.oki, (uint8_t)v);
}

static void sound_cycles(int c);
static void cpu_cyc(void *u, int c) { (void)u; sound_cycles(c); }
static uint16_t cpu_rd(void *u_, uint32_t a) { (void)u_; return rd16(a); }
static void cpu_wr(void *u_, uint32_t a, uint16_t v) { (void)u_; wr16(a, (uint16_t)v); }

static void sound_cycles(int c)
{
    P.oki_acc += (int64_t)c * okid_sample_frequency(&P.oki);
    while (P.oki_acc >= CLOCK) {
        P.oki_acc -= CLOCK;
        P.last_oki = okid_update(&P.oki);
    }
    P.audio_acc += (int64_t)c * RATE;
    while (P.audio_acc >= CLOCK) {
        P.audio_acc -= CLOCK;
        if (P.naudio < 1024) P.audio[P.naudio++] = arc_clamp16(P.last_oki);
    }
}

static void draw_layer(int base, int wt, int ht, int color_add, bool transparent, int scroll_x)
{
    const int lw = wt * 8;
    for (int f = 0; f < wt * ht; f++) {
        const int tx = f / ht, ty = f % ht;
        const int e = base + f * 2;
        if (e + 1 >= 0x4000) continue;
        const uint8_t *px = P.tiles + (size_t)P.tile_ram[e] * 64;
        const uint16_t *pal = P.palette + (((P.tile_ram[e + 1] & 0x1ff) + color_add) << 4);
        const int bx = ((tx * 8 - scroll_x) % lw + lw) % lw;
        for (int y = 0; y < 8; y++) {
            const int fy = ty * 8 + y;
            if (fy >= 256) continue;
            for (int x = 0; x < 8; x++) {
                const uint8_t p = px[y * 8 + x];
                if (transparent && !p) continue;
                int fx = bx + x;
                if (lw != 512) { if (fx >= W) continue; }
                else fx &= 511;
                P.canvas[fy * 512 + fx] = pal[p];
            }
        }
    }
}

static void draw_sprites(void)
{
    for (int f = 0; f <= 0x1fd; f++) {
        const int b = f * 4;
        if (b + 6 >= 0x800) break;
        const uint16_t sy_raw = P.sprite_ram[b + 3];
        if (sy_raw & 0x8000) break;     // the end of the list
        const int sx = P.sprite_ram[b + 5] - 32;
        const uint16_t attr = P.sprite_ram[b + 6];
        const bool fx = attr & 2, fy = attr & 1;
        const uint16_t *pal = P.palette + ((P.sprite_ram[b + 4] & 0xff) << 4) + 0x1800;
        const int sy = (uint16_t)(0x00f2 - sy_raw);
        const uint8_t *px = P.sprites + (size_t)(attr >> 2) * 256;
        for (int y = 0; y < 16; y++) {
            const int py = sy + y;
            if (py >= 256) continue;
            const uint8_t *row = px + (fy ? 15 - y : y) * 16;
            for (int x = 0; x < 16; x++) {
                const uint8_t p = row[fx ? 15 - x : x];
                const int qx = sx + x;
                if (p && qx >= 0 && qx < 512) P.canvas[py * 512 + qx] = pal[p];
            }
        }
    }
}

static void render(void)
{
    draw_layer(0x1540, 64, 32, 0x100, false, P.scroll_x);
    draw_layer(0x9c0, 64, 32, 0x080, true, P.scroll_x);
    draw_sprites();
    draw_layer(0xc0, 36, 32, 0x000, true, 0);
    for (int y = 0; y < H; y++) memcpy(P.fb + y * W, P.canvas + (y + 16) * 512, W * 2);
}

// the four planes descrambled (address and data) into one buffer
static uint8_t *decrypt_planes(const uint8_t *raw, uint32_t (*addr)(uint32_t), uint8_t (*const fn[4])(uint8_t))
{
    uint8_t *d = (uint8_t *)calloc(4, PLANE);
    if (!d) return NULL;
    for (uint32_t f = 0; f < PLANE; f++) {
        const uint32_t to = addr(f);
        for (int p = 0; p < 4; p++) d[p * PLANE + to] = fn[p](raw[p * PLANE + f]);
    }
    return d;
}

bool arc_init(void)
{
    if (!(P_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    P.fb = (uint16_t *)calloc(W * H, 2);
    P.rom = (uint16_t *)malloc(0x80000 * 2);
    P.sound = (uint8_t *)malloc(0x80000);
    uint8_t *raw = (uint8_t *)calloc(4, PLANE);
    if (!P.fb || !P.rom || !P.sound || !raw) return false;
    // the program: even ROM the high bytes, odd the low; then descrambled
    if (!ARC_LOAD(kProgLow, raw, 0x80000) || !ARC_LOAD(kProgHigh, raw + 0x80000, 0x80000)) return false;
    for (uint32_t f = 0; f < 0x80000; f++) {
        const uint32_t al = SW24(f, 23, 22, 21, 20, 19, 18, 4, 8, 3, 14, 2, 15, 17, 0, 9, 13, 10, 5, 16, 7, 12, 6, 1, 11);
        const uint32_t ah = SW24(f, 23, 22, 21, 20, 19, 18, 4, 10, 1, 11, 12, 5, 9, 17, 14, 0, 13, 6, 15, 8, 3, 16, 7, 2);
        const uint8_t vl = sw_b(raw[0x80000 + al]), vr = sw_c(raw[ah]);
        P.rom[f] = (uint16_t)(vr << 8 | vl);
    }
#if !GENIX
    P.rom[0x62c0 / 2] = 0x6006;         // the protection patch of pirates_hw.pas
#endif
    if (!ARC_LOAD(kOki, raw, 0x80000)) return false;
    for (uint32_t f = 0; f < 0x80000; f++)
        P.sound[SW24(f, 23, 22, 21, 20, 19, 10, 16, 13, 8, 4, 7, 11, 14, 17, 12, 6, 2, 0, 5, 18, 15, 3, 1, 9)] = sw_a(raw[f]);
    static uint8_t (*const tile_fn[4])(uint8_t) = {sw_a, sw_b, sw_c, sw_a};
    static uint8_t (*const spr_fn[4])(uint8_t) = {sw_b, sw_c, sw_a, sw_b};
    memset(raw, 0, 4 * PLANE);
    if (!ARC_LOAD(kGfx, raw, 4 * PLANE)) return false;
    uint8_t *dec = decrypt_planes(raw, tile_addr, tile_fn);
    if (!dec) return false;
    arc_layout tl = { 8, 8, 0x10000, 4, 64, false, false, false, {0x180000 * 8, 0x100000 * 8, 0x80000 * 8, 0}, kX8, kY8 };
    P.tiles = arc_gfx_decode(&tl, dec, 4 * PLANE);
    free(dec);
    memset(raw, 0, 4 * PLANE);
    if (!P.tiles || !ARC_LOAD(kSprites, raw, 4 * PLANE)) return false;
    dec = decrypt_planes(raw, sprite_addr, spr_fn);
    if (!dec) return false;
    arc_layout sl = { 16, 16, 0x4000, 4, 256, false, false, false, {0x180000 * 8, 0x100000 * 8, 0x80000 * 8, 0}, kX16, kY16 };
    P.sprites = arc_gfx_decode(&sl, dec, 4 * PLANE);
    free(dec);
    free(raw);
    if (!P.sprites) return false;
    okid_init(&P.oki, OKI_CLOCK, false, P.sound, 0x80000);
    eeprom93c46_init(&P.eeprom, 16);
    eeprom93c46_reset(&P.eeprom);
    m68kd_init(&cpu, CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    cpu.cycles = cpu_cyc;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&cpu);
    P.irq = false;
    okid_reset(&P.oki);
    P.in0 = 0x0f;
    P.in1 = 0xffff;
    P.scroll_x = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int line_cycles = (int)(CLOCK / (uint32_t)(SCANLINES * FPS));
    P.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == VBLANK) {
            P.irq = true;
            m68kd_set_irq(&cpu, 1, M68_HOLD);
            render();
        }
        m68kd_run(&cpu, line_cycles);
    }
    if (audio) *audio = P.audio;
    return P.naudio;
}

const uint16_t *arc_frame(void) { return P.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint16_t v = 0xffff;
    if (p1 & AP_UP) v &= ~0x0001;
    if (p1 & AP_DOWN) v &= ~0x0002;
    if (p1 & AP_LEFT) v &= ~0x0004;
    if (p1 & AP_RIGHT) v &= ~0x0008;
    if (p1 & AP_B1) v &= ~0x0010;
    if (p1 & AP_B2) v &= ~0x0020;
    if (p1 & AP_B3) v &= ~0x0040;
    if (p1 & AP_START) v &= ~0x0080;
    if (p2 & AP_RIGHT) v &= ~0x0100;
    if (p2 & AP_LEFT) v &= ~0x0200;
    if (p2 & AP_UP) v &= ~0x0400;
    if (p2 & AP_DOWN) v &= ~0x0800;
    if (p2 & AP_B1) v &= ~0x1000;
    if (p2 & AP_B2) v &= ~0x2000;
    if (p2 & AP_B3) v &= ~0x4000;
    if (p2 & AP_START) v &= ~0x8000;
    P.in1 = v;
    uint8_t c = 0x0f;
    if (in->coin1) c &= ~0x01;
    if (in->coin2) c &= ~0x02;
    P.in0 = c;
}

void arc_dip(int bank, uint8_t v) { (void)bank, (void)v; }   // the settings live in the EEPROM
