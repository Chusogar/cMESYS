// mikie.c - Mikie (Konami, 1984) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/mikie (from dsp-emulator's
// mikie_hw.pas). A 6809 at 1.5 MHz, a sound Z80 with two SN76496, a map of
// 8x8 characters (some in front of the sprites) and 36 sprites through
// colour PROMs, on a vertical monitor. dsp-cpp caches the tilemap; here each
// pixel is taken from the tiles as the picture is put together.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/sn76496.h"

#define MAIN_CLOCK (18432000 / 12)
#define SOUND_CLOCK (14318180 / 4)
#define SCANLINES 256
#define FPS 60.59
#define RATE 44100
#define W 224
#define H 256

static const char *const kSets[] = { "mikie", NULL };
static const arc_rom kMain[] = {
    {"n14.11c", 0x2000, 0x6000, 0xf698e6dd}, {"o13.12a", 0x4000, 0x8000, 0x826e7035}, {"o17.12d", 0x4000, 0xc000, 0x161c25c8}};
static const arc_rom kSound[] = {{"n10.6e", 0x2000, 0, 0x2cf9d670}};
static const arc_rom kChars[] = {{"o11.8i", 0x4000, 0, 0x3c82aaf3}};
static const arc_rom kSprites[] = {
    {"001.f1", 0x4000, 0x0000, 0xa2ba0df5}, {"003.f3", 0x4000, 0x4000, 0x9775ab32},
    {"005.h1", 0x4000, 0x8000, 0xba44aeef}, {"007.h3", 0x4000, 0xc000, 0x31afc153}};
static const arc_rom kProms[] = {
    {"d19.1i", 0x100, 0x000, 0x8b83e7cf}, {"d21.3i", 0x100, 0x100, 0x3556304a}, {"d20.2i", 0x100, 0x200, 0x676a0669},
    {"d22.12h", 0x100, 0x300, 0x872be05c}, {"d18.f9", 0x100, 0x400, 0x7396b374}};

const arc_info arc_game = { "mikie", "Mikie", "Konami", 1984, W, H, FPS, RATE, kSets,
                            { "BUTTON 1", "BUTTON 2", NULL, NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {0, 4, 8, 12, 16, 20, 24, 28};
static const int kCharY[8] = {0, 32, 64, 96, 128, 160, 192, 224};
static const int kSprX[16] = {256, 257, 258, 259, 128, 129, 130, 131, 0, 1, 2, 3, 384, 385, 386, 387};
static const int kSprY[16] = {0, 16, 32, 48, 64, 80, 96, 112, 512, 528, 544, 560, 576, 592, 608, 624};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m6809d main;
    zd_z80 sound;
    sn76496 sn0, sn1;
    uint8_t mem[0x10000], sound_mem[0x4400];
    uint8_t *chars, *sprites;
    uint16_t palette[256];
    uint8_t char_lut[0x800], sprite_lut[0x800];
    uint16_t compose[256 * 256];
    uint16_t *fb;
    uint8_t palette_bank, sound_latch, sound_irq_trigger, in0, in1, in2, dsw_a, dsw_b, dsw_c;
    bool irq_enable, flip;
    uint64_t sound_cycles;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *K_;
#define K (*K_)

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    switch (a) {
    case 0x2400: return K.in2;
    case 0x2401: return K.in0;
    case 0x2402: return K.in1;
    case 0x2403: return K.dsw_c;
    case 0x2500: return K.dsw_a;
    case 0x2501: return K.dsw_b;
    default: break;
    }
    if (a <= 0x00ff || a >= 0x2800) return K.mem[a];
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x00ff || (a >= 0x2800 && a <= 0x3fff)) {
        K.mem[a] = v;
        return;
    }
    switch (a) {
    case 0x2002:                        // the sound CPU's interrupt on a 0 -> 1 transition
        if (K.sound_irq_trigger == 0 && v == 1) zd_set_irq(&K.sound, ZD_HOLD, 0xff);
        K.sound_irq_trigger = v;
        break;
    case 0x2006: K.flip = (v & 1) != 0; break;
    case 0x2007:
        K.irq_enable = v != 0;
        if (!K.irq_enable) m6809d_set_irq(&K.main, M6809D_CLEAR);
        break;
    case 0x2200: K.palette_bank = v & 7; break;
    case 0x2400: K.sound_latch = v; break;
    default: break;
    }
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x43ff) return K.sound_mem[a];
    if (a == 0x8003) return K.sound_latch;
    if (a == 0x8005) return (uint8_t)(K.sound_cycles >> 9);   // a free-running timer
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x4000 && a <= 0x43ff) K.sound_mem[a] = v;
    else if (a == 0x8002) sn76496_write(&K.sn0, v);
    else if (a == 0x8004) sn76496_write(&K.sn1, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(void *u, int c)
{
    (void)u;
    K.sound_cycles += (uint64_t)c;
    K.audio_acc += (int64_t)c * RATE;
    while (K.audio_acc >= SOUND_CLOCK) {
        K.audio_acc -= SOUND_CLOCK;
        const int32_t s = (sn76496_update(&K.sn0) + sn76496_update(&K.sn1)) / 2;
        if (K.naudio < 1024) K.audio[K.naudio++] = arc_clamp16(s);
    }
}

// the tilemap's colour (an index of the palette) at (x, y), and whether it is
// in front of the sprites
static inline uint8_t tile_entry(int x, int y, bool *front)
{
    const int off = (x >> 3) * 32 + (31 - (y >> 3));
    const uint8_t attr = K.mem[0x3800 + off];
    const int color = ((attr & 0x0f) + 0x10 * K.palette_bank) << 4;
    const int code = K.mem[0x3c00 + off] + ((attr & 0x20) << 3);
    const int px = (attr & 0x80) ? (x & 7) : 7 - (x & 7), py = (attr & 0x40) ? (y & 7) : 7 - (y & 7);
    const uint8_t e = K.char_lut[K.chars[code * 64 + py * 8 + px] + color];
    *front = (attr & 0x10) && e != 0;
    return e;
}

static void update_video(void)
{
    bool fr;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) K.compose[y * 256 + x] = K.palette[tile_entry(x, y, &fr)];
    for (int i = 0; i < 0x24; i++) {
        const uint8_t *e = &K.mem[0x2800 + i * 4];
        const uint8_t attr = e[0], cb = e[2];
        const int code = ((attr & 0x40) << 1) + (cb & 0x3f) + ((cb & 0x80) >> 1) + ((cb & 0x40) << 2);
        const int color = ((attr & 0x0f) + 0x10 * K.palette_bank) << 4;
        int px = (244 - e[1]) & 0xff, py;
        const bool fx = (attr & 0x20) == 0;
        bool fy;
        if (!K.flip) {
            py = (240 - e[3]) & 0xff;
            fy = (attr & 0x10) != 0;
        } else {
            py = e[3];
            fy = (attr & 0x10) == 0;
            px = (px - 2) & 0xff;
        }
        const uint8_t *g = K.sprites + code * 256;
        for (int y = 0; y < 16; y++) {
            const int row = ((py + y) & 0xff) * 256;
            for (int x = 0; x < 16; x++) {
                const uint8_t pen = g[(fy ? 15 - y : y) * 16 + (fx ? 15 - x : x)];
                if (pen) K.compose[row + ((px + x) & 0xff)] = K.palette[K.sprite_lut[pen + color]];
            }
        }
    }
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const uint8_t e = tile_entry(x, y, &fr);
            if (fr) K.compose[y * 256 + x] = K.palette[e];
        }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int t = K.flip ? (H - 1 - y) * W + (W - 1 - x) : y * W + x;
            K.fb[t] = K.compose[y * 256 + x + 16];
        }
}

bool arc_init(void)
{
    if (!(K_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    K.fb = (uint16_t *)calloc(W * H, 2);
    K.sprites = (uint8_t *)calloc(512, 256);
    uint8_t *gfx = (uint8_t *)calloc(1, 0x10000), prom[0x500];
    if (!K.fb || !K.sprites || !gfx) return false;
    if (!ARC_LOAD(kMain, K.mem, 0x10000) || !ARC_LOAD(kSound, K.sound_mem, 0x2000) || !ARC_LOAD(kProms, prom, sizeof prom))
        return false;
    static const int kCharP[4] = {0, 1, 2, 3};
    arc_layout cl = { 8, 8, 512, 4, 256, false, true, false, {kCharP[0], kCharP[1], kCharP[2], kCharP[3]}, kCharX, kCharY };
    if (!ARC_LOAD(kChars, gfx, 0x4000) || !(K.chars = arc_gfx_decode(&cl, gfx, 0x4000))) return false;
    // the sprite ROMs hold two banks of 256 elements with other plane offsets
    if (!ARC_LOAD(kSprites, gfx, 0x10000)) return false;
    for (int bank = 0; bank < 2; bank++) {
        arc_layout sl = { 16, 16, 256, 4, 1024, false, true, false,
                          {bank * 8, 4 + bank * 8, bank * 8 + 0x8000 * 8, bank * 8 + 0x8000 * 8 + 4}, kSprX, kSprY };
        arc_gfx_decode_into(&sl, gfx, 0x10000, K.sprites, bank * 256);
    }
    free(gfx);
    static const arc_resnet nets[3] = {{4, {2200, 1000, 470, 220}, 470, 0}, {4, {2200, 1000, 470, 220}, 470, 0},
                                       {4, {2200, 1000, 470, 220}, 470, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    for (int i = 0; i < 0x100; i++) {
        const int r = arc_combine(w[0], 4, prom[i] & 15), g = arc_combine(w[1], 4, prom[i + 0x100] & 15),
                  b = arc_combine(w[2], 4, prom[i + 0x200] & 15);
        K.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    for (int low = 0; low < 0x100; low++)
        for (int bank = 0; bank < 8; bank++) {
            K.char_lut[low + (bank << 8)] = (uint8_t)((prom[low + 0x300] & 0x0f) + (bank << 5) + 16);
            K.sprite_lut[low + (bank << 8)] = (uint8_t)((prom[low + 0x400] & 0x0f) + (bank << 5));
        }
    K.main.read = main_read;
    K.main.write = main_write;
    K.sound.read = sound_read;
    K.sound.write = sound_write;
    K.sound.in = in_ff;
    K.sound.out = out_nop;
    K.sound.cycles = sound_cycles;
    zd_init(&K.sound);
    sn76496_init(&K.sn0, 14318180 / 8, RATE, 1.0f);
    sn76496_init(&K.sn1, 14318180 / 4, RATE, 1.0f);
    K.dsw_a = 0xff;
    K.dsw_b = 0x7b;
    K.dsw_c = 0xfe;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&K.main);
    zd_reset(&K.sound);
    sn76496_reset(&K.sn0);
    sn76496_reset(&K.sn1);
    K.palette_bank = K.sound_latch = K.sound_irq_trigger = 0;
    K.irq_enable = K.flip = false;
    K.in0 = K.in1 = K.in2 = 0xff;
    K.sound_cycles = 0;
    K.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int main_cycles = (int)(MAIN_CLOCK / FPS / SCANLINES), sound_cycles_ = (int)(SOUND_CLOCK / FPS / SCANLINES);
    K.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (K.irq_enable) m6809d_set_irq(&K.main, M6809D_ASSERT);
            update_video();
        }
        m6809d_run(&K.main, main_cycles);
        zd_run(&K.sound, sound_cycles_);
    }
    if (audio) *audio = K.audio;
    return K.naudio;
}

const uint16_t *arc_frame(void) { return K.fb; }

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
    K.in0 = joy(in->p[0]);
    K.in1 = joy(in->p[1]);
    uint8_t v = 0xff;
    if (in->coin1) v &= 0xfe;
    if (in->coin2) v &= 0xfd;
    if (in->p[0] & AP_START) v &= 0xf7;
    if (in->p[1] & AP_START) v &= 0xef;
    K.in2 = v;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) K.dsw_a = v;
    if (bank == 1) K.dsw_b = v;
    if (bank == 2) K.dsw_c = v;
}
