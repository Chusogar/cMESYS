// galaxian.c - Galaxian (Namco / Midway, 1979) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/galaxian (from
// dsp-emulator's galaxian_hw.pas), the Galaxian machine. A Z80 at 3 MHz, a
// map of 8x8 characters scrolled per row, 8 sprites, 8 bullets and a star
// field. Like dsp-cpp it has no sound (the board's is discrete).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"

#define CLOCK 3072000
#define SCANLINES 256
#define FPS 60.60606060
#define RATE 44100
#define W 224
#define H 256

static const char *const kSets[] = { "galaxian", "galmidw", NULL };
static const arc_rom kCpu[] = {
    {"galmidw.u", 0x800, 0x0000, 0x745e2d61}, {"galmidw.v", 0x800, 0x0800, 0x9c999a40},
    {"galmidw.w", 0x800, 0x1000, 0xb5894925}, {"galmidw.y", 0x800, 0x1800, 0x6b3ca10b},
    {"7l", 0x800, 0x2000, 0x1b933207}};
static const arc_rom kGfx[] = {{"1h.bin", 0x800, 0, 0x39fb43a4}, {"1k.bin", 0x800, 0x800, 0x7e3f56a2}};
static const arc_rom kPal[] = {{"6l.bpr", 0x20, 0, 0xc3ac9467}};

const arc_info arc_game = { "galaxian", "Galaxian", "Namco", 1979, W, H, FPS, RATE, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_2WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
    uint8_t rom[0x4000], ram[0x400], videoram[0x400], attributes[0x40], sprites[0x20], bullets[0x20], misc[0x100];
    uint8_t *chars, *sprite_gfx;
    uint16_t palette[64];
    uint16_t compose[256 * 256];
    uint16_t *fb;
    bool nmi_enable, stars_enable;
    uint32_t stars_scroll;
    uint8_t in0, in1, dsw_a, dsw_b, dsw_c;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *G_;
#define G (*G_)

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x3fff) return G.rom[a];
    if (a <= 0x47ff) return G.ram[a & 0x3ff];
    if (a >= 0x5000 && a <= 0x57ff) return G.videoram[a & 0x3ff];
    if (a >= 0x5800 && a <= 0x5fff) {
        const uint8_t off = a & 0xff;
        if (off <= 0x3f) return G.attributes[off];
        if (off <= 0x5f) return G.sprites[off & 0x1f];
        if (off <= 0x7f) return G.bullets[off & 0x1f];
        return G.misc[off];
    }
    if (a >= 0x6000 && a <= 0x67ff) return (uint8_t)(G.in0 | G.dsw_a);
    if (a >= 0x6800 && a <= 0x6fff) return (uint8_t)(G.in1 | G.dsw_b);
    if (a >= 0x7000 && a <= 0x77ff) return G.dsw_c;
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
    if (a <= 0x47ff) G.ram[a & 0x3ff] = v;
    else if (a >= 0x5000 && a <= 0x57ff) G.videoram[a & 0x3ff] = v;
    else if (a >= 0x5800 && a <= 0x5fff) {
        const uint8_t off = a & 0xff;
        if (off <= 0x3f) G.attributes[off] = v;
        else if (off <= 0x5f) G.sprites[off & 0x1f] = v;
        else if (off <= 0x7f) G.bullets[off & 0x1f] = v;
        else G.misc[off] = v;
    } else if (a >= 0x7000 && a <= 0x77ff) {
        if ((a & 7) == 1) {
            G.nmi_enable = (v & 1) != 0;
            if (!G.nmi_enable) zd_set_nmi(&G.cpu, ZD_CLEAR);
        } else if ((a & 7) == 4) G.stars_enable = (v & 1) != 0;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void cycles(void *u, int c)
{
    (void)u;
    G.audio_acc += (int64_t)c * RATE;
    while (G.audio_acc >= CLOCK) {
        G.audio_acc -= CLOCK;
        if (G.naudio < 1024) G.audio[G.naudio++] = 0;
    }
}

static void update_video(void)
{
    // the characters (pen 0 black), each row scrolled by its attribute
    for (int offset = 0; offset < 0x400; offset++) {
        const int tile_x = 31 - (offset >> 5), tile_y = offset & 31;
        const int color = (G.attributes[1 + (tile_y << 1)] & 7) << 2;
        const int scroll = tile_x * 8 + G.attributes[tile_y << 1];
        const uint8_t *px = G.chars + G.videoram[offset] * 64;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t p = px[y * 8 + x];
                G.compose[(tile_y * 8 + y) * 256 + ((scroll + x) & 0xff)] = p ? G.palette[p + color] : 0;
            }
    }
    if (G.stars_enable) {
        uint32_t rng = 0x12345678u ^ G.stars_scroll;
        for (int n = 0; n < 64; n++) {
            rng = rng * 1664525u + 1013904223u;
            const int x = (int)((rng >> 8) + G.stars_scroll) & 0xff, y = (int)(rng >> 16) & 0xff;
            if (G.compose[y * 256 + x] != 0) continue;
            const uint32_t c = 0x40 + ((rng >> 24) & 3) * 0x3f;
            G.compose[y * 256 + x] = arc_rgb565(c << 16 | c << 8 | c);
        }
    }
    for (int f = 0; f < 8; f++) {
        int y = 250 - G.bullets[3 + f * 4];
        if (f > 2) y += 1;
        const uint16_t color = G.palette[f == 7 ? 33 : 32];
        const int x = G.bullets[1 + f * 4];
        for (int dy = 0; dy < 4; dy++)
            if (y + dy >= 0 && y + dy < 256) G.compose[(y + dy) * 256 + x] = color;
    }
    for (int i = 7; i >= 0; i--) {
        const uint8_t *e = &G.sprites[i * 4];
        const int y = e[3] + 1;
        if (y < 16) continue;
        const uint8_t attr = e[1];
        const bool fx = (attr & 0x80) != 0, fy = (attr & 0x40) != 0;
        const int color = (e[2] & 7) << 2, x = e[0];
        const uint8_t *px = G.sprite_gfx + (attr & 0x3f) * 256;
        for (int py = 0; py < 16; py++) {
            const int sy = y + py;
            if (sy >= 256) continue;
            const int src_y = fy ? 15 - py : py;
            for (int qx = 0; qx < 16; qx++) {
                const uint8_t p = px[src_y * 16 + (fx ? 15 - qx : qx)];
                if (p) G.compose[sy * 256 + ((x + qx) & 0xff)] = G.palette[p + color];
            }
        }
    }
    for (int y = 0; y < H; y++) memcpy(G.fb + y * W, G.compose + y * 256 + 16, W * 2);
    G.stars_scroll++;
}

bool arc_init(void)
{
    if (!(G_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    G.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t gfx[0x1000], prom[0x20];
    if (!G.fb || !ARC_LOAD(kCpu, G.rom, 0x2800) || !ARC_LOAD(kGfx, gfx, sizeof gfx) || !ARC_LOAD(kPal, prom, sizeof prom))
        return false;
    arc_layout cl = { 8, 8, 0x100, 2, 64, false, true, false, {0, 0x100 * 64}, kX8, kY8 };
    arc_layout sl = { 16, 16, 0x40, 2, 256, false, true, false, {0, 0x40 * 256}, kX16, kY16 };
    if (!(G.chars = arc_gfx_decode(&cl, gfx, sizeof gfx)) || !(G.sprite_gfx = arc_gfx_decode(&sl, gfx, sizeof gfx)))
        return false;
    static const arc_resnet nets[3] = {{3, {1000, 470, 220}, 470, 0}, {3, {1000, 470, 220}, 470, 0}, {2, {470, 220}, 470, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    for (int i = 0; i < 32; i++) {
        const uint8_t d = prom[i];
        const int r = arc_combine(w[0], 3, d & 7), g = arc_combine(w[1], 3, (d >> 3) & 7), b = arc_combine(w[2], 2, d >> 6);
        G.palette[i] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    G.palette[32] = arc_rgb565(0xffffff);
    G.palette[33] = arc_rgb565(0xffff00);
    G.cpu.read = rd;
    G.cpu.write = wr;
    G.cpu.in = in_ff;
    G.cpu.out = out_nop;
    G.cpu.cycles = cycles;
    zd_init(&G.cpu);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&G.cpu);
    G.nmi_enable = G.stars_enable = false;
    G.stars_scroll = 0;
    G.in0 = G.in1 = 0;
    memset(G.videoram, 0, sizeof G.videoram);
    memset(G.attributes, 0, sizeof G.attributes);
    memset(G.sprites, 0, sizeof G.sprites);
    memset(G.bullets, 0, sizeof G.bullets);
    G.audio_acc = 0;
    zd_set_nmi(&G.cpu, ZD_CLEAR);
}

int arc_run_frame(const int16_t **audio)
{
    const int line_cycles = (int)((double)CLOCK / (FPS * SCANLINES) + 0.5);
    G.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 248 && G.nmi_enable) zd_set_nmi(&G.cpu, ZD_ASSERT);
        zd_run(&G.cpu, line_cycles);
        if (line == 248) zd_set_nmi(&G.cpu, ZD_CLEAR);
    }
    update_video();
    if (audio) *audio = G.audio;
    return G.naudio;
}

const uint16_t *arc_frame(void) { return G.fb; }

void arc_input(const arc_in *in)
{
    uint8_t a = 0, b = 0;
    if (in->coin1) a |= 0x01;
    if (in->coin2) a |= 0x02;
    if (in->p[0] & AP_LEFT) a |= 0x04;
    if (in->p[0] & AP_RIGHT) a |= 0x08;
    if (in->p[0] & AP_B1) a |= 0x10;
    if (in->p[0] & AP_START) b |= 0x01;
    if (in->p[1] & AP_START) b |= 0x02;
    G.in0 = a;
    G.in1 = b;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) G.dsw_a = v;
    else if (bank == 1) G.dsw_b = v;
    else if (bank == 2) G.dsw_c = v;
}
