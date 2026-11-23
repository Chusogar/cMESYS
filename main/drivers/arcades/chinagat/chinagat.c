// chinagat.c - China Gate (Technos Japan / Taito, 1988) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/chinagate (from
// dsp-emulator's chinagate_hw.pas). Two HD6309 (as 6809s) at 6 MHz sharing
// 8 KB of RAM, each with 6 banks of 16 KB, a Z80 with a YM2151 and an OKI
// M6295, a scrolling 512x512 background of 16x16 tiles, 16x16 sprites (in
// up to 2x2 blocks) and 8x8 text. The graphics are read straight from their
// ROMs and the picture built in place. The OKI runs at its own rate
// (dsp-cpp clocked it at the output rate).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/okim6295dsp.h"

#define CPU_CLOCK 6000000
#define SOUND_CLOCK 3579545
#define OKI_CLOCK 1056000
#define FPS (6000000.0 / 384.0 / 272.0)
#define SCANLINES 272
#define CPU_SYNC 4
#define RATE 44100
#define W 256
#define H 240

static const char *const kSets[] = { "chinagat", NULL };
static const arc_rom kMain[] = {{"cgate51.bin", 0x20000, 0, 0x439a3b19}};
static const arc_rom kSub[] = {{"23j4-0.48", 0x20000, 0, 0x2914af38}};
static const arc_rom kSound[] = {{"23j0-0.40", 0x8000, 0, 0x9ffcadb6}};
static const arc_rom kChars[] = {{"cgate18.bin", 0x20000, 0, 0x8d88d64d}};
static const arc_rom kTiles[] = {
    {"chinagat_a-13", 0x10000, 0x00000, 0xb745cac4}, {"chinagat_a-12", 0x10000, 0x10000, 0x3c864299},
    {"chinagat_a-15", 0x10000, 0x20000, 0x2f268f37}, {"chinagat_a-14", 0x10000, 0x30000, 0xaef814c8}};
static const arc_rom kSprites[] = {
    {"23j7-0.103", 0x20000, 0x00000, 0x2f445030}, {"23j8-0.102", 0x20000, 0x20000, 0x237f725a},
    {"23j9-0.101", 0x20000, 0x40000, 0x8caf6097}, {"23ja-0.100", 0x20000, 0x60000, 0xf678594f}};
static const arc_rom kAdpcm[] = {{"23j1-0.53", 0x20000, 0x00000, 0xf91f1001}, {"23j2-0.52", 0x20000, 0x20000, 0x8b6f26e9}};

const arc_info arc_game = { "chinagat", "China Gate", "Technos Japan / Taito", 1988, W, H, FPS, RATE, kSets,
                            { "ATTACK", "JUMP", "ATTACK 2", NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {1, 0, 65, 64, 129, 128, 193, 192};
static const int kTX[16] = {3, 2, 1, 0, 131, 130, 129, 128, 259, 258, 257, 256, 387, 386, 385, 384};
static const int kTY[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
static const arc_layout kCharL = { 8, 8, 0x1000, 4, 256, false, false, false, {0, 2, 4, 6}, kCharX, kTY };
static const arc_layout kTileL = { 16, 16, 0x800, 4, 512, false, false, false, {0x20000 * 8, 0x20000 * 8 + 4, 0, 4}, kTX, kTY };
static const arc_layout kSprL = { 16, 16, 0x1000, 4, 512, false, false, false, {0x40000 * 8, 0x40000 * 8 + 4, 0, 4}, kTX, kTY };

// the machine on the heap
static struct state {
    m6809d main, sub;
    zd_z80 sound;
    ym2151 ym;
    okim6295d oki;
    const uint8_t *rom, *sub_rom, *chars, *tiles, *sprites, *adpcm;   // blobs (the CYD: in the flash)
    uint8_t ram[0x2000], vram[0x1000], sprite_ram[0x180], pal_ram[0x800];   // 0000, 2000, 3800
    uint16_t palette[0x400];
    uint8_t sound_rom[0x8000], sound_ram[0x800];
    arc_fb fb;
    uint8_t in0, in1, in2, dsw_a, dsw_b, soundlatch, rom_bank, sub_bank;
    uint16_t scroll_x, scroll_y;
    bool flip;
    int64_t audio_acc, oki_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
} *C_;
#define C (*C_)

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff) return C.ram[a];
    if (a <= 0x2fff) return C.vram[a & 0xfff];
    if (a >= 0x3800 && a <= 0x397f) return C.sprite_ram[a - 0x3800];
    if (a >= 0x8000) return C.rom[0x18000 + (a - 0x8000)];
    switch (a) {
    case 0x3f00: return C.in0;
    case 0x3f01: return C.dsw_a;
    case 0x3f02: return C.dsw_b;
    case 0x3f03: return C.in1;
    case 0x3f04: return C.in2;
    default: break;
    }
    if (a >= 0x4000) return C.rom[(C.rom_bank % 6) * 0x4000 + (a & 0x3fff)];
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) { C.ram[a] = v; return; }
    if (a <= 0x2fff) { C.vram[a & 0xfff] = v; return; }
    if (a >= 0x3800 && a <= 0x397f) { C.sprite_ram[a - 0x3800] = v; return; }
    if (a >= 0x3000 && a <= 0x37ff) {
        const int o = a & 0x7ff;
        if (C.pal_ram[o] != v) {
            C.pal_ram[o] = v;
            const int i = o & 0x3ff;
            const uint8_t lo = C.pal_ram[i], hi = C.pal_ram[i + 0x400];
            C.palette[i] = arc_rgb565((uint32_t)((lo & 15) * 0x11) << 16 | (uint32_t)((lo >> 4) * 0x11) << 8 |
                                      (uint32_t)((hi & 15) * 0x11));
        }
        return;
    }
    switch (a) {
    case 0x3e00:
        C.soundlatch = v;
        zd_set_nmi(&C.sound, ZD_ASSERT);
        break;
    case 0x3e01: m6809d_set_nmi(&C.main, M6809D_CLEAR); break;
    case 0x3e02: m6809d_set_firq(&C.main, M6809D_CLEAR); break;
    case 0x3e03: m6809d_set_irq(&C.main, M6809D_CLEAR); break;
    case 0x3e04: m6809d_set_irq(&C.sub, M6809D_ASSERT); break;
    case 0x3e06: C.scroll_y = (uint16_t)((C.scroll_y & 0x100) | v); break;
    case 0x3e07: C.scroll_x = (uint16_t)((C.scroll_x & 0x100) | v); break;
    case 0x3f00:
        C.scroll_x = (uint16_t)((C.scroll_x & 0xff) | ((v & 1) << 8));
        C.scroll_y = (uint16_t)((C.scroll_y & 0xff) | ((v & 2) << 7));
        C.flip = (v & 4) == 0;
        break;
    case 0x3f01: C.rom_bank = v & 7; break;
    default: break;
    }
}

static uint8_t sub_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff) return C.ram[a];
    if (a >= 0x4000 && a <= 0x7fff) return C.sub_rom[(C.sub_bank % 6) * 0x4000 + (a & 0x3fff)];
    if (a >= 0x8000) return C.sub_rom[0x18000 + (a - 0x8000)];
    return 0xff;
}

static void sub_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) C.ram[a] = v;
    else if (a == 0x2000) C.sub_bank = v & 7;
    else if (a == 0x2800) m6809d_set_irq(&C.sub, M6809D_CLEAR);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return C.sound_rom[a];
    if (a <= 0x87ff) return C.sound_ram[a & 0x7ff];
    switch (a) {
    case 0x8801: return ym2151_status(&C.ym);
    case 0x9800: return okid_read(&C.oki);
    case 0xa000:
        zd_set_nmi(&C.sound, ZD_CLEAR);
        return C.soundlatch;
    default: return 0xff;
    }
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x8000 && a <= 0x87ff) C.sound_ram[a & 0x7ff] = v;
    else if (a == 0x8800) ym2151_select(&C.ym, v);
    else if (a == 0x8801) ym2151_write(&C.ym, v);
    else if (a == 0x9800) okid_write(&C.oki, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&C.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

static void sound_cycles(void *u, int c)
{
    (void)u;
    ym2151_run_timers(&C.ym, c);
    C.oki_acc += (int64_t)c * okid_sample_frequency(&C.oki);
    while (C.oki_acc >= SOUND_CLOCK) {
        C.oki_acc -= SOUND_CLOCK;
        C.last_oki = okid_update(&C.oki);
    }
    C.audio_acc += (int64_t)c * RATE;
    while (C.audio_acc >= SOUND_CLOCK) {
        C.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&C.ym) + C.last_oki / 2;
        if (C.naudio < 1024) C.audio[C.naudio++] = arc_clamp16(s);
    }
}

// the screen is 256 lines from which the picture takes 8-247
static inline uint16_t *screen_row(int y) { return y >= 8 && y < 8 + H ? arc_fb_row(&C.fb, y - 8) : NULL; }

static void sprite_element(int code, int color, bool fx, bool fy, int px, int py)
{
    for (int r = 0; r < 16; r++) {
        uint16_t *out = screen_row(py + r);
        if (!out) continue;
        for (int c = 0; c < 16; c++) {
            const int x = px + c;
            if (x < 0 || x >= 256) continue;
            const unsigned pen = arc_gfx_px(&kSprL, C.sprites, 0x80000, code, fx ? 15 - c : c, fy ? 15 - r : r);
            if (pen) out[x] = C.palette[(color + pen) & 0x3ff];
        }
    }
}

static void update_video(void)
{
    for (int y = 8; y < 8 + H; y++) {   // the background
        uint16_t *out = screen_row(y);
        const int wy = (y + C.scroll_y) & 0x1ff, ty = wy >> 4;
        for (int x = 0; x < 256; x++) {
            const int wx = (x + C.scroll_x) & 0x1ff, tx = wx >> 4;
            const int pos = (tx & 0x0f) + ((ty & 0x0f) << 4) + ((tx & 0x10) << 4) + ((ty & 0x10) << 5);
            const uint8_t attr = C.vram[0x800 + pos * 2];
            const int code = C.vram[0x801 + pos * 2] + ((attr & 7) << 8);
            const int sx = (attr & 0x40) ? 15 - (wx & 15) : wx & 15, sy = (attr & 0x80) ? 15 - (wy & 15) : wy & 15;
            out[x] = C.palette[(0x100 + (((attr & 0x38) >> 3) << 4) + arc_gfx_px(&kTileL, C.tiles, 0x40000, code, sx, sy)) & 0x3ff];
        }
    }
    for (int i = 0; i < 0x40; i++) {
        const uint8_t *e = &C.sprite_ram[i * 5];
        const uint8_t attr = e[1];
        if (!(attr & 0x80)) continue;
        const int x = 240 - e[4] + ((attr & 2) << 7), y = 240 - e[0] + ((attr & 1) << 8);
        const int size = (attr & 0x30) >> 4;
        const bool fx = attr & 8, fy = attr & 4;
        const int color = (e[2] & 0x70) + 0x80;
        const int code = (e[3] + ((e[2] & 0x0f) << 8)) & ~size;
        switch (size) {
        case 1:
            sprite_element(code, color, fx, fy, x, y - 16);
            sprite_element(code + 1, color, fx, fy, x, y);
            break;
        case 2:
            sprite_element(code, color, fx, fy, x - 16, y);
            sprite_element(code + 1, color, fx, fy, x, y);
            break;
        case 3:
            sprite_element(code, color, fx, fy, x - 16, y - 16);
            sprite_element(code + 1, color, fx, fy, x, y - 16);
            sprite_element(code + 2, color, fx, fy, x - 16, y);
            sprite_element(code + 3, color, fx, fy, x, y);
            break;
        default: sprite_element(code, color, fx, fy, x, y); break;
        }
    }
    for (int y = 8; y < 8 + H; y++) {   // the text
        uint16_t *out = screen_row(y);
        for (int x = 0; x < 256; x++) {
            const int i = (y >> 3) * 32 + (x >> 3);
            const uint8_t attr = C.vram[i * 2];
            const unsigned pen = arc_gfx_px(&kCharL, C.chars, 0x20000, C.vram[i * 2 + 1] + ((attr & 0x0f) << 8), x & 7, y & 7);
            if (pen) out[x] = C.palette[((attr & 0xf0) + pen) & 0x3ff];
        }
    }
    if (C.flip)
        for (int y = 0; y < H / 2; y++) {
            uint16_t *a = arc_fb_row(&C.fb, y), *b = arc_fb_row(&C.fb, H - 1 - y);
            for (int x = 0; x < W; x++) {
                const uint16_t t = a[x];
                a[x] = b[W - 1 - x];
                b[W - 1 - x] = t;
            }
        }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&C.fb, y); }

bool arc_init(void)
{
    if (!(C_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&C.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    if (!(C.rom = ARC_ROM_BLOB("main", kMain, 0x20000)) || !(C.sub_rom = ARC_ROM_BLOB("sub", kSub, 0x20000)) ||
        !ARC_LOAD(kSound, C.sound_rom, 0x8000) || !(C.chars = ARC_ROM_BLOB("chars", kChars, 0x20000)) ||
        !(C.tiles = ARC_ROM_BLOB("tiles", kTiles, 0x40000)) || !(C.sprites = ARC_ROM_BLOB("sprites", kSprites, 0x80000)) ||
        !(C.adpcm = ARC_ROM_BLOB("adpcm", kAdpcm, 0x40000)))
        return false;
    C.main.read = main_read;
    C.main.write = main_write;
    C.sub.read = sub_read;
    C.sub.write = sub_write;
    C.sound.read = sound_read;
    C.sound.write = sound_write;
    C.sound.in = in_ff;
    C.sound.out = out_nop;
    C.sound.cycles = sound_cycles;
    zd_init(&C.sound);
    ym2151_init(&C.ym, SOUND_CLOCK, RATE);
    C.ym.irq_handler = ym_irq;
    okid_init(&C.oki, OKI_CLOCK, true, C.adpcm, 0x40000);
    C.dsw_a = 0xbf;
    C.dsw_b = 0xe7;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m6809d_reset(&C.main);
    m6809d_reset(&C.sub);
    zd_reset(&C.sound);
    ym2151_reset(&C.ym);
    okid_reset(&C.oki);
    C.in0 = 0x0e;
    C.in1 = C.in2 = 0xff;
    C.soundlatch = C.rom_bank = C.sub_bank = 0;
    C.scroll_x = C.scroll_y = 0;
    C.flip = false;
    C.audio_acc = C.oki_acc = 0;
    C.last_oki = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int slice = (int)(CPU_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    const int sslice = (int)(SOUND_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    C.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 8) C.in0 &= 0xfe;
        if (line >= 16 && line <= 224 && line % 16 == 0) m6809d_set_firq(&C.main, M6809D_ASSERT);
        if (line == 248) {
            m6809d_set_nmi(&C.main, M6809D_ASSERT);
            update_video();
            C.in0 |= 1;
        }
        for (int s = 0; s < CPU_SYNC; s++) {
            m6809d_run(&C.main, slice);
            m6809d_run(&C.sub, slice);
            zd_run(&C.sound, sslice);
        }
    }
    if (audio) *audio = C.audio;
    return C.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&C.fb); }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_RIGHT) v &= ~0x01;
    if (p & AP_LEFT) v &= ~0x02;
    if (p & AP_UP) v &= ~0x04;
    if (p & AP_DOWN) v &= ~0x08;
    if (p & AP_B1) v &= ~0x10;
    if (p & AP_B2) v &= ~0x20;
    if (p & AP_B3) v &= ~0x40;
    if (p & AP_START) v &= ~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    C.in0 = (uint8_t)(0x0e | (C.in0 & 1));
    C.in1 = pad(in->p[0]);
    C.in2 = pad(in->p[1]);
    if (in->coin1) C.in0 &= ~0x02;
    if (in->coin2) C.in0 &= ~0x04;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw_a = v;
    else if (bank == 1) C.dsw_b = v;
}
