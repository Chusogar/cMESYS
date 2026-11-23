// bankp.c - Bank Panic (Sanritsu / Sega, 1984) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/bankpanic_hw (from
// dsp-emulator's bankpanic_hw.pas). A Z80 at 2.6 MHz, three SN76496, and two
// 32x32 layers of 8x8 tiles (the front one scrolled), either in front.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/sn76496.h"

#define CLOCK (15468480 / 6)
#define FPS 61.034091
#define SCANLINES 256
#define RATE 44100
#define W 224
#define H 224

static const char *const kSets[] = { "bankp", NULL };
static const arc_rom kRom[] = {
    {"epr-6175.7e", 0x4000, 0x0000, 0x044552b8}, {"epr-6174.7f", 0x4000, 0x4000, 0xd29b1598},
    {"epr-6173.7h", 0x4000, 0x8000, 0xb8405d38}, {"epr-6176.7d", 0x2000, 0xc000, 0xc98ac200}};
static const arc_rom kChars[] = {{"epr-6165.5l", 0x2000, 0, 0xaef34a93}, {"epr-6166.5k", 0x2000, 0x2000, 0xca13cb11}};
static const arc_rom kBg[] = {
    {"epr-6172.5b", 0x2000, 0x0000, 0xc4c4878b}, {"epr-6171.5d", 0x2000, 0x2000, 0xa18165a1},
    {"epr-6170.5e", 0x2000, 0x4000, 0xb58aa8fa}, {"epr-6169.5f", 0x2000, 0x6000, 0x1aa37fce},
    {"epr-6168.5h", 0x2000, 0x8000, 0x05f3a867}, {"epr-6167.5i", 0x2000, 0xa000, 0x3fa337e1}};
static const arc_rom kProm[] = {
    {"pr-6177.8a", 0x20, 0x000, 0xeb70c5ae}, {"pr-6178.6f", 0x100, 0x020, 0x0acca001},
    {"pr-6179.5a", 0x100, 0x120, 0xe53bafdb}};

const arc_info arc_game = { "bankp", "Bank Panic", "Sanritsu / Sega", 1984, W, H, FPS, RATE, kSets,
                            { "LEFT", "CENTRE", "RIGHT", NULL }, ARC_JOY_2WAY };

static const int kCharX[8] = {67, 66, 65, 64, 3, 2, 1, 0};
static const int kX8[8] = {7, 6, 5, 4, 3, 2, 1, 0};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
    sn76496 sn[3];
    const uint8_t *rom, *chars, *tiles;  // blobs (the CYD: in the flash)
    uint8_t ram[0x2000];                // e000-ffff
    uint16_t palette[32];
    uint8_t lut[512];
    arc_fb fb;
    uint8_t in0, in1, in2, dsw, color_hi, scroll_x;
    bool priority, display_on, nmi_vblank;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *K_;
#define K (*K_)

static uint8_t rd(void *u, uint16_t a) { (void)u; return a < 0xe000 ? K.rom[a] : K.ram[a & 0x1fff]; }
static void wr(void *u, uint16_t a, uint8_t v) { (void)u; if (a >= 0xe000) K.ram[a & 0x1fff] = v; }
#define VRAM(a) K.ram[(a) & 0x1fff]

static uint8_t port_in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0: return K.in0;
    case 1: return K.in1;
    case 2: return K.in2;
    case 4: return K.dsw;
    default: return 0xff;
    }
}

static void port_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0: case 1: case 2: sn76496_write(&K.sn[p & 3], v); break;
    case 5: K.scroll_x = v; break;
    case 7:
        K.priority = (v & 0x02) != 0;
        K.display_on = (v & 0x04) != 0;
        K.color_hi = (uint8_t)((v & 0x08) >> 3);
        K.nmi_vblank = (v & 0x10) != 0;
        break;
    default: break;
    }
}

static void cycles(void *u, int c)
{
    (void)u;
    K.audio_acc += (int64_t)c * RATE;
    while (K.audio_acc >= CLOCK) {
        K.audio_acc -= CLOCK;
        const int32_t s = sn76496_update(&K.sn[0]) + sn76496_update(&K.sn[1]) + sn76496_update(&K.sn[2]);
        if (K.naudio < 1024) K.audio[K.naudio++] = arc_clamp16(s);
    }
}

// a pixel of a layer (pen index + 1, 0 transparent), straight from the tile RAM
static inline uint8_t fg_pixel(int x, int y, bool masked)
{
    const int f = (y >> 3) * 32 + (x >> 3);
    const uint8_t attr = VRAM(0xf400 + f);
    const int base = ((attr >> 3) | (K.color_hi << 5)) << 2;
    const uint8_t *px = K.chars + (VRAM(0xf000 + f) + ((attr & 3) << 8)) * 64;
    const uint8_t punto = K.lut[(base + px[(y & 7) * 8 + ((attr & 4) ? 7 - (x & 7) : x & 7)]) & 0x1ff];
    return (masked && !(punto & 0x1f)) ? 0 : (uint8_t)((punto & 0x1f) + 1);
}

static inline uint8_t bg_pixel(int x, int y, bool masked)
{
    const int f = (y >> 3) * 32 + (x >> 3);
    const uint8_t attr = VRAM(0xfc00 + f);
    const int base = (((attr >> 4) | (K.color_hi << 4)) << 3) + 256;
    const uint8_t *px = K.tiles + (VRAM(0xf800 + f) + ((attr & 7) << 8)) * 64;
    const uint8_t punto = K.lut[(base + px[(y & 7) * 8 + ((attr & 8) ? 7 - (x & 7) : x & 7)]) & 0x1ff];
    return (masked && !(punto & 0x0f)) ? 0 : (uint8_t)((punto & 0x1f) + 1);
}

static void update_video(void)
{
    if (!K.display_on) {
        for (int y = 0; y < H; y++) memset(arc_fb_row(&K.fb, y), 0, W * 2);
        return;
    }
    // BG then the scrolled FG on top, or the other way round
    for (int y = 0; y < H; y++) {
        const int wy = y + 16;
        uint16_t *out = arc_fb_row(&K.fb, y);
        for (int x = 0; x < W; x++) {
            const int wx = x + 24;
            const uint8_t b = bg_pixel(wx, wy, K.priority), f = fg_pixel((wx + K.scroll_x) & 0xff, wy, !K.priority);
            uint8_t p = 0;
            if (!K.priority) { if (b) p = b; if (f) p = f; }
            else { if (f) p = f; if (b) p = b; }
            out[x] = p ? K.palette[p - 1] : 0;
        }
    }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&K.fb, y); }

bool arc_init(void)
{
    if (!(K_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t prom[0x220];
    if (!(K.rom = ARC_ROM_BLOB("rom", kRom, 0xe000)) || !ARC_LOAD(kProm, prom, sizeof prom)) return false;
    arc_blob b;
    const uint8_t *raw = ARC_ROM_BLOB("chars.raw", kChars, 0x4000);
    if (!raw) return false;
    arc_layout cl = { 8, 8, 0x400, 2, 128, false, false, false, {0, 4}, kCharX, kY8 };
    if (arc_blob_begin(&b, "chars", 0x400 * 64, ARC_ROM_KEY(kChars, 1))) arc_blob_gfx(&b, &cl, raw, 0x4000, 0);
    if (!(K.chars = arc_blob_end(&b)) || !(raw = ARC_ROM_BLOB("tiles.raw", kBg, 0xc000))) return false;
    arc_layout tl = { 8, 8, 0x800, 3, 64, false, false, false, {0, 2048 * 64, 2048 * 128}, kX8, kY8 };
    if (arc_blob_begin(&b, "tiles", 0x800 * 64, ARC_ROM_KEY(kBg, 1))) arc_blob_gfx(&b, &tl, raw, 0xc000, 0);
    if (!(K.tiles = arc_blob_end(&b)) || !arc_fb_init(&K.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    for (int f = 0; f < 0x20; f++) {
        const uint8_t v = prom[f];
        const int r = 0x21 * (v & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int gg = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        K.palette[f] = arc_rgb565((uint32_t)r << 16 | (uint32_t)gg << 8 | (uint32_t)b);
    }
    for (int f = 0; f < 256; f++) {
        const int index = ((f << 1) & 0x100) | (f & 0x7f);
        const uint8_t e = prom[0x20 + index] & 0x0f;
        K.lut[index] = e;
        K.lut[index | 0x80] = (uint8_t)(e | 0x10);
    }
    K.cpu.read = rd;
    K.cpu.write = wr;
    K.cpu.in = port_in;
    K.cpu.out = port_out;
    K.cpu.cycles = cycles;
    zd_init(&K.cpu);
    for (int i = 0; i < 3; i++) sn76496_init(&K.sn[i], CLOCK, RATE, 1.0f);
    K.dsw = 0xc0;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&K.cpu);
    for (int i = 0; i < 3; i++) sn76496_reset(&K.sn[i]);
    K.in0 = K.in1 = K.in2 = 0;
    K.nmi_vblank = false;
    K.scroll_x = K.color_hi = 0;
    K.display_on = true;
    K.priority = false;
    K.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int line_cycles = (int)((double)CLOCK / FPS / SCANLINES);
    K.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (K.nmi_vblank) zd_set_nmi(&K.cpu, ZD_PULSE);
            update_video();
        }
        zd_run(&K.cpu, line_cycles);
    }
    if (audio) *audio = K.audio;
    return K.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&K.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0, b = 0, c = 0;
    if (p1 & AP_UP) a |= 0x01;
    if (p1 & AP_RIGHT) a |= 0x02;
    if (p1 & AP_DOWN) a |= 0x04;
    if (p1 & AP_LEFT) a |= 0x08;
    if (p1 & AP_B1) a |= 0x10;
    if (in->coin1) a |= 0x20;
    if (p1 & AP_B2) a |= 0x80;
    if (p2 & AP_UP) b |= 0x01;
    if (p2 & AP_RIGHT) b |= 0x02;
    if (p2 & AP_DOWN) b |= 0x04;
    if (p2 & AP_LEFT) b |= 0x08;
    if (p2 & AP_B1) b |= 0x10;
    if (p1 & AP_START) b |= 0x20;
    if (p2 & AP_START) b |= 0x40;
    if (p2 & AP_B2) b |= 0x80;
    if (p1 & AP_B3) c |= 0x01;
    if (p2 & AP_B3) c |= 0x02;
    if (in->coin2) c |= 0x04;
    K.in0 = a;
    K.in1 = b;
    K.in2 = c;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) K.dsw = v;
}
