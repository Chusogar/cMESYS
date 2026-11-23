// brkthru.c - Break Thru (Data East, 1986) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/brkthru (from MAME's
// brkthru.cpp). A 6809 at 1.5 MHz with 8 banks of 8 KB, a sound 6809 with a
// YM2203 and a YM3526, a 512-wide layer of 16x16 tiles split around the
// sprites, 16x16 / 16x32 sprites and 8x8 text.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../sound/ym2203dsp.h"
#include "../../../sound/ym3812dsp.h"

#define MAIN_CLOCK (12000000 / 8)
#define SOUND_CLOCK (12000000 / 8)
#define SCANLINES 272
#define FPS 57.444885
#define RATE 44100
#define W 240
#define H 240

static const char *const kSets[] = { "brkthru", NULL };
static const arc_rom kProgram[] = {
    {"brkthru.1", 0x4000, 0x00000, 0xcfb4265f}, {"brkthru.2", 0x8000, 0x04000, 0xfa8246d9},
    {"brkthru.4", 0x8000, 0x0c000, 0x8cabf252}, {"brkthru.3", 0x8000, 0x14000, 0x2f2c40c2}};
static const arc_rom kSound[] = {{"brkthru.5", 0x8000, 0, 0xc309435f}};
static const arc_rom kChars[] = {{"brkthru.12", 0x2000, 0, 0x58c0b29b}};
static const arc_rom kSprites[] = {
    {"brkthru.9", 0x8000, 0x00000, 0xf54e50a7}, {"brkthru.10", 0x8000, 0x08000, 0xfd156945},
    {"brkthru.11", 0x8000, 0x10000, 0xc152a99b}};
static const arc_rom kTiles[] = {
    {"brkthru.7", 0x8000, 0x00000, 0x920cc56a}, {"brkthru.6", 0x8000, 0x08000, 0xfd3cee40},
    {"brkthru.8", 0x8000, 0x10000, 0xf67ee64e}};
static const arc_rom kProms[] = {{"brkthru.13", 0x100, 0x000, 0xaae44269}, {"brkthru.14", 0x100, 0x100, 0xf2d4822a}};

const arc_info arc_game = { "brkthru", "Break Thru", "Data East", 1986, W, H, FPS, RATE, kSets,
                            { "FIRE", "JUMP", NULL, NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {16384, 16385, 16386, 16387, 0, 1, 2, 3};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kTileX[16] = {0, 1, 2, 3, 65536, 65537, 65538, 65539, 128, 129, 130, 131, 65664, 65665, 65666, 65667};
static const int kSprX[16] = {128, 129, 130, 131, 132, 133, 134, 135, 0, 1, 2, 3, 4, 5, 6, 7};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m6809d main, sound;
    ym2203d ym2203;
    ym3812d ym3526;
    const uint8_t *rom, *banks;         // 4000-ffff, 8 x 8 KB (blobs: on the CYD in the flash)
    const uint8_t *sound_rom;           // 8000-ffff
    const uint8_t *chars, *tiles, *sprites;
    uint8_t sound_ram[0x2000], ram[0x1800], bg_video[0x400], fg_video[0x400], sprite_ram[0x100];
    uint16_t palette[0x100];
    arc_fb fb;                          // the visible 240x240 of the 256x256 screen
    uint8_t in0, in1, in2, dsw_a, dsw_b, sound_latch, rom_bank, bg_color;
    uint16_t scroll_x;
    bool nmi_enabled, old_coin1, old_coin2;
    double main_debt, sound_debt;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *B_;
#define B (*B_)

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a >= 0x4000) return B.rom[a - 0x4000];
    if (a >= 0x2000) return B.banks[B.rom_bank * 0x2000 + (a & 0x1fff)];
    if (a < 0x400) return B.bg_video[a];
    if (a < 0xc00) return B.ram[a];
    if (a < 0x1000) return B.fg_video[a & 0x3ff];
    if (a < 0x1100) return B.sprite_ram[a & 0xff];
    if (a < 0x1800) return B.ram[a];
    switch (a) {
    case 0x1800: return B.in0;
    case 0x1801: return B.in1;
    case 0x1802: return B.dsw_a;
    case 0x1803: return (uint8_t)(B.dsw_b | B.in2);
    default: return 0xff;
    }
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x2000) return;
    if (a < 0x400) B.bg_video[a] = v;
    else if (a < 0xc00) B.ram[a] = v;
    else if (a < 0x1000) B.fg_video[a & 0x3ff] = v;
    else if (a < 0x1100) B.sprite_ram[a & 0xff] = v;
    else if (a < 0x1800) B.ram[a] = v;
    else if (a == 0x1800) B.scroll_x = (uint16_t)((B.scroll_x & 0xff00) | v);
    else if (a == 0x1801) {
        B.rom_bank = v & 7;
        B.bg_color = (uint8_t)((v & 0x38) >> 2);
        B.scroll_x = (uint16_t)((B.scroll_x & 0x00ff) | ((v >> 7) << 8));
    } else if (a == 0x1802) {
        B.sound_latch = v;
        m6809d_set_nmi(&B.sound, M6809D_PULSE);
    } else if (a == 0x1803) {
        B.nmi_enabled = (v & 1) == 0;   // the VBLANK NMI enabled by a 0
        if (!(v & 2)) m6809d_set_irq(&B.main, M6809D_CLEAR);   // the coin IRQ acknowledge
    }
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x2000) return B.sound_ram[a];
    if (a >= 0x8000) return B.sound_rom[a - 0x8000];
    switch (a) {
    case 0x2000: return ym3812d_status(&B.ym3526);
    case 0x4000: return B.sound_latch;
    case 0x6000: return ym2203d_status(&B.ym2203);
    default: return 0xff;
    }
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x2000) { B.sound_ram[a] = v; return; }
    switch (a) {
    case 0x2000: ym3812d_control(&B.ym3526, v); break;
    case 0x2001: ym3812d_write(&B.ym3526, v); break;
    case 0x6000: ym2203d_control(&B.ym2203, v); break;
    case 0x6001: ym2203d_write(&B.ym2203, v); break;
    default: break;
    }
}

static void ym3526_irq(void *u, bool on) { (void)u; m6809d_set_irq(&B.sound, on ? M6809D_ASSERT : M6809D_CLEAR); }

static void sound_cycles(void *u, int c)
{
    (void)u;
    B.audio_acc += (int64_t)c * RATE;
    while (B.audio_acc >= SOUND_CLOCK) {
        B.audio_acc -= SOUND_CLOCK;
        const int32_t s = (ym2203d_update(&B.ym2203) + ym3812d_update(&B.ym3526)) * 2 / 3;
        if (B.naudio < 1024) B.audio[B.naudio++] = arc_clamp16(s);
    }
}

// screen (x, y) of 256x256 is fb (x - 8, y - 8)
static inline uint16_t *screen_row(int y) { return y >= 8 && y < 8 + H ? arc_fb_row(&B.fb, y - 8) - 8 : NULL; }

static void sprite_tile(int c, int x, int ty, int color)
{
    const uint8_t *g = B.sprites + (size_t)(c & 0x3ff) * 256;
    for (int r = 0; r < 16; r++) {
        uint16_t *out = screen_row((ty + r) & 0xff);
        if (!out) continue;
        for (int col = 0; col < 16; col++) {
            const int px = x + col;
            if (px >= 8 + W) break;
            const int pen = g[r * 16 + col];
            if (pen && px >= 8) out[px] = B.palette[color + pen];
        }
    }
}

// sprite byte 0: ccc d p bb e (colour, double height, priority, bank, enable)
static void draw_sprites(int priority)
{
    for (int f = 0; f < 0x40; f++) {
        const uint8_t *s = &B.sprite_ram[f * 4];
        if ((s[0] & 0x09) != priority) continue;
        const int code = s[1] + ((s[0] & 0x06) << 7), color = 0x40 + ((s[0] & 0xe0) >> 2);
        const int x = (240 - s[3]) & 0xff, y = (224 - s[2]) & 0xff;
        if (s[0] & 0x10) {
            sprite_tile(code & 0x3fe, x, y, color);
            sprite_tile(code | 1, x, y + 16, color);
        } else sprite_tile(code, x, y + 16, color);
    }
}

static void tiles(bool opaque)
{
    for (int y = 8; y < 8 + H; y++) {
        uint16_t *out = screen_row(y);
        for (int x = 8; x < 8 + W; x++) {
            const int px = (x + B.scroll_x) & 0x1ff, f = (px >> 4) * 16 + (y >> 4);
            const uint8_t attr = B.fg_video[f * 2 + 1];
            const int code = B.fg_video[f * 2] + ((attr & 3) << 8);
            const int color = 0x80 + ((B.bg_color + ((attr & 4) >> 2)) << 3);
            const int pen = B.tiles[(size_t)code * 256 + (y & 15) * 16 + (px & 15)];
            if (opaque || pen) out[x] = B.palette[(color + pen) & 0xff];
        }
    }
}

static void update_video(void)
{
    tiles(true);
    draw_sprites(0x01);
    tiles(false);
    draw_sprites(0x09);
    for (int i = 0; i < 0x400; i++) {   // the text, colours 0-7
        const uint8_t *g = B.chars + B.bg_video[i] * 64;
        const int cx = (i & 31) * 8, cy = (i >> 5) * 8;
        for (int r = 0; r < 8; r++) {
            uint16_t *out = screen_row(cy + r);
            if (!out) continue;
            for (int c = 0; c < 8; c++)
                if (g[r * 8 + c] && cx + c >= 8 && cx + c < 8 + W) out[cx + c] = B.palette[g[r * 8 + c]];
        }
    }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&B.fb, y); }

static uint8_t weight4(uint8_t v) { return (uint8_t)(0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1)); }

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    if (!arc_fb_init(&B.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    arc_blob b;
    // the ROMs cover the whole of it
    if (arc_blob_begin(&b, "program", 0x1c000, ARC_ROM_KEY(kProgram, 0)) && !ARC_BLOB_LOAD(&b, kProgram)) b.err = true;
    if (!(B.rom = arc_blob_end(&b))) return false;
    B.banks = B.rom + 0xc000;
    if (!(B.sound_rom = ARC_ROM_BLOB("sound", kSound, 0x8000))) return false;
    const uint8_t *raw = ARC_ROM_BLOB("chars.raw", kChars, 0x2000);
    arc_layout cl = { 8, 8, 0x100, 3, 64, false, false, false, {512 * 64 + 4, 0, 4}, kCharX, kY8 };
    if (!raw) return false;
    if (arc_blob_begin(&b, "chars", 0x100 * 64, ARC_ROM_KEY(kChars, 1))) arc_blob_gfx(&b, &cl, raw, 0x2000, 0);
    arc_blob_free(raw);
    if (!(B.chars = arc_blob_end(&b))) return false;
    // the tiles: eight groups of 128 rearranged like MAME, then decoded
    const uint8_t *src = ARC_ROM_BLOB("tiles.raw", kTiles, 0x18000);
    if (!src) return false;
    if (arc_blob_begin(&b, "tiles.gfx", 0x20000, ARC_ROM_KEY(kTiles, 1)))
        for (int f = 0; f < 4; f++) {
            arc_blob_write(&b, 0x8000 * f, src + 0x4000 * f, 0x4000);
            arc_blob_write(&b, 0x8000 * f + 0x4000, src + 0x10000 + 0x2000 * f, 0x1000);
            arc_blob_write(&b, 0x8000 * f + 0x6000, src + 0x11000 + 0x2000 * f, 0x1000);
        }
    arc_blob_free(src);
    const uint8_t *gfx = arc_blob_end(&b);
    if (!gfx) return false;
    if (arc_blob_begin(&b, "tiles", 0x400 * 256, ARC_ROM_KEY(kTiles, 2)))
        for (int group = 0; group < 8; group++) {
            const int f = group / 2;
            const bool odd = group & 1;
            arc_layout tl = { 16, 16, 0x80, 3, 256, false, false, false, {odd ? 0x3000 * 8 : 0x4000 * 8 + 4, 0, 4}, kTileX, kY16 };
            arc_blob_gfx(&b, &tl, gfx + 0x8000 * f + (odd ? 0x1000 : 0), 0x7000, group * 0x80);
        }
    arc_blob_free(gfx);
    if (!(B.tiles = arc_blob_end(&b))) return false;
    arc_layout sl = { 16, 16, 0x400, 3, 256, false, false, false, {2 * 1024 * 256, 1024 * 256, 0}, kSprX, kY16 };
    if (!(raw = ARC_ROM_BLOB("sprites.raw", kSprites, 0x18000))) return false;
    if (arc_blob_begin(&b, "sprites", 0x400 * 256, ARC_ROM_KEY(kSprites, 1))) arc_blob_gfx(&b, &sl, raw, 0x18000, 0);
    arc_blob_free(raw);
    if (!(B.sprites = arc_blob_end(&b))) return false;
    uint8_t proms[0x200];
    if (!ARC_LOAD(kProms, proms, sizeof proms)) return false;
    for (int i = 0; i < 0x100; i++)
        B.palette[i] = arc_rgb565((uint32_t)weight4(proms[i] & 15) << 16 | (uint32_t)weight4(proms[i] >> 4) << 8 | weight4(proms[0x100 + i] & 15));
    B.main.read = main_read;
    B.main.write = main_write;
    B.sound.read = sound_read;
    B.sound.write = sound_write;
    B.sound.cycles = sound_cycles;
    ym2203d_init(&B.ym2203, 1500000, 1.0f, 0.5f);
    ym3812d_init(&B.ym3526, 3000000, true, 1.0f);
    B.ym3526.irq = ym3526_irq;
    B.dsw_a = 0x3f;
    B.dsw_b = 0x1f;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(B.ram, 0, sizeof B.ram);
    memset(B.bg_video, 0, sizeof B.bg_video);
    memset(B.fg_video, 0, sizeof B.fg_video);
    memset(B.sprite_ram, 0, sizeof B.sprite_ram);
    memset(B.sound_ram, 0, sizeof B.sound_ram);
    B.in0 = B.in1 = 0xff;
    B.in2 = 0xe0;
    B.rom_bank = 0;
    B.scroll_x = 0;
    B.old_coin1 = B.old_coin2 = false;
    B.sound_latch = B.bg_color = 0;
    B.nmi_enabled = true;
    B.main_debt = B.sound_debt = 0;
    B.audio_acc = 0;
    m6809d_reset(&B.main);
    m6809d_reset(&B.sound);
    ym2203d_reset(&B.ym2203);
    ym3812d_reset(&B.ym3526);
}

int arc_run_frame(const int16_t **audio)
{
    const double mc = (double)MAIN_CLOCK / FPS / SCANLINES, sc = (double)SOUND_CLOCK / FPS / SCANLINES;
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) B.in1 |= 0x80;
        if (line == 248) {
            if (B.nmi_enabled) m6809d_set_nmi(&B.main, M6809D_PULSE);
            update_video();
            B.in1 &= 0x7f;              // VBLANK
        }
        B.main_debt += mc;
        B.main_debt -= m6809d_run(&B.main, (int)B.main_debt);
        B.sound_debt += sc;
        B.sound_debt -= m6809d_run(&B.sound, (int)B.sound_debt);
    }
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&B.fb); }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_B1) v &= ~0x01;
    if (p & AP_B2) v &= ~0x02;
    if (p & AP_DOWN) v &= ~0x04;
    if (p & AP_UP) v &= ~0x08;
    if (p & AP_LEFT) v &= ~0x10;
    if (p & AP_RIGHT) v &= ~0x20;
    return v;
}

void arc_input(const arc_in *in)
{
    B.in0 = pad(in->p[0]);
    if (in->p[0] & AP_START) B.in0 &= ~0x40;
    if (in->p[1] & AP_START) B.in0 &= ~0x80;
    B.in1 = (uint8_t)((pad(in->p[1]) & 0x7f) | (B.in1 & 0x80));
    // a new coin clears its bit for a frame and interrupts the main CPU
    B.in2 = 0xe0;
    if (in->coin1 && !B.old_coin1) {
        B.in2 &= ~0x20;
        m6809d_set_irq(&B.main, M6809D_ASSERT);
    }
    if (in->coin2 && !B.old_coin2) {
        B.in2 &= ~0x40;
        m6809d_set_irq(&B.main, M6809D_ASSERT);
    }
    B.old_coin1 = in->coin1;
    B.old_coin2 = in->coin2;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw_a = v;
    else if (bank == 1) B.dsw_b = v & 0x1f;
}
