// shaolins.c - Shao-lin's Road (Konami, 1985) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/shaolinsroad (from
// dsp-emulator's shaolins_hw.pas). A 6809 at 1.5 MHz, two SN76496, a
// scrolling map of 8x8 characters and 24 sprites through colour PROMs, on a
// vertical monitor. dsp-cpp caches the tilemap; here each pixel is taken
// from the tiles as the picture is put together (the same result).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../sound/sn76496.h"

#define CLOCK (18432000 / 12)
#define SCANLINES 256
#define FPS 60.0
#define RATE 44100
#define W 224
#define H 256

static const char *const kSets[] = { "shaolins", NULL };
static const arc_rom kMain[] = {
    {"477l03.d9|477-l03.d9|shaolins.d9", 0x2000, 0x6000, 0x2598dfdd},
    {"477l04.d10|477-l04.d10|shaolins.d10", 0x4000, 0x8000, 0x0cf0351a},
    {"477l05.d11|477-l05.d11|shaolins.d11", 0x4000, 0xc000, 0x654037f8}};
static const arc_rom kChars[] = {
    {"477j06.a10|shaolins.a10|kicker.a10", 0x2000, 0, 0xff18a7ed}, {"477j07.a11|shaolins.a11|kicker.a11", 0x2000, 0x2000, 0x5f53ae61}};
static const arc_rom kSprites[] = {
    {"477j02.h15|477-k02.h15|shaolins.h15", 0x4000, 0, 0xb94e645b}, {"477j01.h14|477-k01.h14|shaolins.h14", 0x4000, 0x4000, 0x61bbf797}};
static const arc_rom kProms[] = {
    {"477j10.a12|shaolins.a12", 0x100, 0x000, 0xb09db4b4}, {"477j11.a13|shaolins.a13", 0x100, 0x100, 0x270a2bf3},
    {"477j12.a14|shaolins.a14", 0x100, 0x200, 0x83e95ea8}, {"477j09.b8|shaolins.b8", 0x100, 0x300, 0xaa900724},
    {"477j08.f16|shaolins.f16", 0x100, 0x400, 0x80009cf5}};

const arc_info arc_game = { "shaolins", "Shao-lin's Road", "Konami", 1985, W, H, FPS, RATE, kSets,
                            { "KICK", "JUMP", NULL, NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {0, 1, 2, 3, 64, 65, 66, 67};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kSprX[16] = {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195};
static const int kSprY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m6809d cpu;
    sn76496 sn0, sn1;
    uint8_t mem[0x10000];
    uint8_t *chars, *sprites;
    uint16_t palette[256];
    uint8_t char_lut[0x800], sprite_lut[0x800];
    uint16_t compose[256 * 256];
    uint16_t *fb;
    uint8_t palette_bank, scroll, in0, in1, in2, dsw_a, dsw_b, dsw_c;
    bool nmi_enable, flip;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    switch (a) {
    case 0x0500: return S.dsw_a;
    case 0x0600: return S.dsw_b;
    case 0x0700: return S.in0;
    case 0x0701: return S.in1;
    case 0x0702: return S.in2;
    case 0x0703: return S.dsw_c;
    default: break;
    }
    if ((a >= 0x2800 && a <= 0x2bff) || (a >= 0x3000 && a <= 0x33ff) || a >= 0x3800) return S.mem[a];
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if ((a >= 0x2800 && a <= 0x2bff) || (a >= 0x3000 && a <= 0x33ff) || (a >= 0x3800 && a <= 0x3fff)) {
        S.mem[a] = v;
        return;
    }
    if (a >= 0x4000) return;
    switch (a) {
    case 0x0000:
        S.flip = (v & 1) != 0;
        S.nmi_enable = (v & 2) != 0;
        break;
    case 0x0300: sn76496_write(&S.sn0, v); break;
    case 0x0400: sn76496_write(&S.sn1, v); break;
    case 0x1800: S.palette_bank = v & 7; break;
    case 0x2000: S.scroll = (uint8_t)~v; break;
    default: break;
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= CLOCK) {
        S.audio_acc -= CLOCK;
        const int32_t s = (sn76496_update(&S.sn0) + sn76496_update(&S.sn1)) / 2;
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

// the tilemap's pixel (256x256; the characters stored by columns, right to left)
static inline uint16_t tile_px(int x, int y)
{
    const int off = (31 - (x >> 3)) * 32 + (y >> 3);
    const uint8_t attr = S.mem[0x3800 + off];
    const int code = S.mem[0x3c00 + off] + ((attr & 0x40) << 2);
    const int color = ((attr & 0x0f) + 0x10 * S.palette_bank) << 4;
    const int py = (attr & 0x10) ? 7 - (y & 7) : (y & 7), px = (attr & 0x20) ? 7 - (x & 7) : (x & 7);
    return S.palette[S.char_lut[S.chars[code * 64 + py * 8 + px] + color]];
}

static void update_video(void)
{
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) S.compose[y * 256 + x] = tile_px((x + S.scroll) & 0xff, y);
    for (int i = 0x17; i >= 0; i--) {
        const uint8_t attr = S.mem[0x2800 + i * 2], px = S.mem[0x2801 + i * 2];
        const uint8_t py = S.mem[0x3000 + i * 2], code = S.mem[0x3001 + i * 2];
        if (attr == 0 && code == 0) continue;
        const int color = ((attr & 0x0f) + 0x10 * S.palette_bank) << 4;
        const bool fx = (attr & 0x80) != 0, fy = (attr & 0x40) == 0;
        const uint8_t *g = S.sprites + code * 256;
        for (int y = 0; y < 16; y++) {
            const int row = ((py + y) & 0xff) * 256;
            for (int x = 0; x < 16; x++) {
                const uint8_t pen = g[(fy ? 15 - y : y) * 16 + (fx ? 15 - x : x)];
                if (pen) S.compose[row + ((px + x) & 0xff)] = S.palette[S.sprite_lut[pen + color]];
            }
        }
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int t = S.flip ? (H - 1 - y) * W + (W - 1 - x) : y * W + x;
            S.fb[t] = S.compose[y * 256 + x + 16];
        }
}

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    S.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *gfx = (uint8_t *)calloc(1, 0x8000), prom[0x500];
    if (!S.fb || !gfx) return false;
    if (!ARC_LOAD(kMain, S.mem, 0x10000) || !ARC_LOAD(kProms, prom, sizeof prom)) return false;
    arc_layout cl = { 8, 8, 512, 4, 128, false, true, false, {512 * 128 + 4, 512 * 128, 4, 0}, kCharX, kY8 };
    if (!ARC_LOAD(kChars, gfx, 0x4000) || !(S.chars = arc_gfx_decode(&cl, gfx, 0x4000))) return false;
    arc_layout sl = { 16, 16, 256, 4, 512, false, true, false, {256 * 512 + 4, 256 * 512, 4, 0}, kSprX, kSprY };
    if (!ARC_LOAD(kSprites, gfx, 0x8000) || !(S.sprites = arc_gfx_decode(&sl, gfx, 0x8000))) return false;
    free(gfx);
    static const arc_resnet nets[3] = {{4, {2200, 1000, 470, 220}, 470, 0}, {4, {2200, 1000, 470, 220}, 470, 0},
                                       {4, {2200, 1000, 470, 220}, 470, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    for (int i = 0; i < 0x100; i++) {
        const int r = arc_combine(w[0], 4, prom[i] & 15), g = arc_combine(w[1], 4, prom[i + 0x100] & 15),
                  b = arc_combine(w[2], 4, prom[i + 0x200] & 15);
        S.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    for (int low = 0; low < 0x100; low++)
        for (int bank = 0; bank < 8; bank++) {
            S.char_lut[low + (bank << 8)] = (uint8_t)((prom[low + 0x300] & 0x0f) + (bank << 5) + 16);
            S.sprite_lut[low + (bank << 8)] = (uint8_t)((prom[low + 0x400] & 0x0f) + (bank << 5));
        }
    S.cpu.read = rd;
    S.cpu.write = wr;
    S.cpu.cycles = cycles;
    sn76496_init(&S.sn0, 18432000 / 12, RATE, 1.0f);
    sn76496_init(&S.sn1, 18432000 / 6, RATE, 1.0f);
    S.dsw_a = 0x5a;
    S.dsw_b = 0x0f;
    S.dsw_c = 0xff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&S.cpu);
    sn76496_reset(&S.sn0);
    sn76496_reset(&S.sn1);
    S.palette_bank = S.scroll = 0;
    S.nmi_enable = S.flip = false;
    S.in0 = S.in1 = S.in2 = 0xff;
    S.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int per_line = (int)(CLOCK / FPS / SCANLINES);
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            m6809d_set_irq(&S.cpu, M6809D_HOLD);
            update_video();
        } else if (S.nmi_enable && (line & 0x1f) == 0) m6809d_set_nmi(&S.cpu, M6809D_PULSE);
        m6809d_run(&S.cpu, per_line);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

static uint8_t joy(unsigned b)
{
    uint8_t v = 0xff;
    if (b & AP_LEFT) v &= 0xfe;
    if (b & AP_RIGHT) v &= 0xfd;
    if (b & AP_UP) v &= 0xfb;
    if (b & AP_DOWN) v &= 0xf7;
    if (b & AP_B1) v &= 0xef;
    if (b & AP_B2) v &= 0xdf;
    return v;
}

void arc_input(const arc_in *in)
{
    uint8_t v = 0xff;
    if (in->coin1) v &= 0xfe;
    if (in->coin2) v &= 0xfd;
    if (in->p[0] & AP_START) v &= 0xf7;
    if (in->p[1] & AP_START) v &= 0xef;
    S.in0 = v;
    S.in1 = joy(in->p[0]);
    S.in2 = joy(in->p[1]);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    if (bank == 1) S.dsw_b = v;
    if (bank == 2) S.dsw_c = v;
}
