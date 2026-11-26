// bloodbros.c - Blood Bros. (TAD, 1990) and Sky Smasher (Nihon System, 1990)
// for the common arcade frontends (arc.h): C port of dsp-cpp's
// drivers/arcade/bloodbros (MAME's bloodbro.cpp). A 68000 at 10 MHz
// (Musashi here), two 512x256 layers of 16x16 tiles, 16x16 sprites up to
// 8x8 of them, a layer of 8x8 text and 2048 colours of 4-bit RGB. The sound
// is the Seibu sound system with a YM3812 and an OKI M6295 (seibu_snd.c),
// which also reads the coins. ARC_VARIANT 1: Sky Smasher (its CRT registers'
// address bits 3 and 4 swapped, IRQ 2, a turned monitor).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "seibu_snd.h"

static m68kd cpu;

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define SKYSMASH (ARC_VARIANT == 1)

#define MAIN_CLOCK 10000000
#define SOUND_CLOCK (7159090 / 2)
#define OKI_CLOCK 1000000
#define FPS 59.39
#define SCANLINES 256
#define RATE SEIBU_RATE

#if SKYSMASH
#define W 224
#define H 256
static const char *const kSets[] = { "skysmash", NULL };
static const arc_rom kProgram[] = {
    {"rom5", 0x20000, 0x00000, 0x867f9897}, {"rom6", 0x20000, 0x00001, 0xe9c1d308},
    {"rom7", 0x20000, 0x40000, 0xd209db4d}, {"rom8", 0x20000, 0x40001, 0xd3646728}};
static const arc_rom kSound[] = {{"rom2", 0x10000, 0, 0x75b194cf}};
static const arc_rom kChars[] = {{"rom3", 0x10000, 0x00000, 0xfbb241be}, {"rom4", 0x10000, 0x10000, 0xad3cde81}};
static const arc_rom kTiles[] = {{"rom9", 0x100000, 0, 0xb0a5eecf}};
static const arc_rom kSprites[] = {{"rom10", 0x80000, 0, 0x1bbcda5d}};
static const arc_rom kOki[] = {{"rom1", 0x20000, 0, 0xe69986f6}};
const arc_info arc_game = { "skysmash", "Sky Smasher", "Nihon System", 1990, W, H, FPS, RATE, kSets,
                            { "FIRE", "BOMB", NULL, NULL }, ARC_JOY_8WAY };
#define IRQ_LEVEL 2
#else
#define W 256
#define H 224
static const char *const kSets[] = { "bloodbro", "bloodbros", NULL };
static const arc_rom kProgram[] = {
    {"2.u021.7n", 0x20000, 0x00001, 0x204dca6e}, {"1.u022.8n", 0x20000, 0x00000, 0xac6719e7},
    {"4.u023.7l", 0x20000, 0x40001, 0xfd951c2c}, {"3.u024.8l", 0x20000, 0x40000, 0x18d3c460}};
static const arc_rom kSound[] = {{"bb_07.u1016.6a", 0x10000, 0, 0x411b94e8}};
static const arc_rom kChars[] = {{"bb_05.u061.6f", 0x10000, 0x00000, 0x04ba6d19}, {"bb_06.u063.6d", 0x10000, 0x10000, 0x7092e35b}};
static const arc_rom kTiles[] = {{"blood_bros_bk__=c=1990_tad_corp.u064.4d", 0x100000, 0, 0x1aa87ee6}};
static const arc_rom kSprites[] = {{"blood_bros_obj__=c=1990_tad_corp.u078.2n", 0x100000, 0, 0xd27c3952}};
static const arc_rom kOki[] = {{"bb_08.u095.5a", 0x20000, 0, 0xdeb1b975}};
const arc_info arc_game = { "bloodbro", "Blood Bros.", "TAD", 1990, W, H, FPS, RATE, kSets,
                            { "FIRE", "DODGE", "GRENADE", NULL }, ARC_JOY_8WAY };
#define IRQ_LEVEL 4
#endif

static const int kCX[8] = {3, 2, 1, 0, 11, 10, 9, 8};
static const int kCY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kTX[16] = {3, 2, 1, 0, 19, 18, 17, 16, 515, 514, 513, 512, 531, 530, 529, 528};
static const int kTY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};
static const arc_layout kCharL = { 8, 8, 0x1000, 4, 128, false, false, false, {0, 4, 0x1000 * 128, 0x1000 * 128 + 4}, kCX, kCY };
static const arc_layout kTileL = { 16, 16, 0x2000, 4, 1024, false, false, false, {8, 12, 0, 4}, kTX, kTY };

static struct state {
    seibu_snd snd;
    const uint8_t *rom, *chars, *sprites, *tiles, *sound, *oki;
    uint16_t ram[0x8000], pal_ram[0x800], crt[0x28], palette[0x801];
    arc_fb fb;
    uint16_t in0, in1, dsw;
    double main_debt, sound_debt;
    int16_t audio[1024];
} *B_;
#define C (*B_)

// ---------------------------------------------------------------------------
// The 68000




static uint16_t pal4(uint16_t v)
{
    const uint32_t r = v & 15, g = (v >> 4) & 15, b = (v >> 8) & 15;
    return arc_rgb565((r * 17) << 16 | (g * 17) << 8 | (b * 17));
}

static int crt_index(uint32_t a)
{
    int i = (int)((a & 0x7f) >> 1);
    if (SKYSMASH) i = (i & 0xffe7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1);   // bits 3 and 4 swapped
    return i < 0x28 ? i : -1;
}

static uint16_t read16(uint32_t a)
{
    a &= 0xfffff;
    if (a < 0x80000) return (uint16_t)(C.rom[a & ~1u] << 8 | C.rom[a | 1]);
    if (a < 0x90000) {
        if (a >= 0x8e800 && a < 0x8f800) return C.pal_ram[(a - 0x8e800) >> 1];
        return C.ram[(a & 0xffff) >> 1];
    }
    if (a >= 0xa0000 && a < 0xa000e) return seibu_main_read(&C.snd, (int)((a & 0xf) >> 1));
    if (a >= 0xc0000 && a < 0xc0050) {
        const int i = crt_index(a);
        return i >= 0 ? C.crt[i] : 0xffff;
    }
    switch (a & ~1u) {
    case 0xe0000: return C.dsw;
    case 0xe0002: return C.in0;
    case 0xe0004: return C.in1;
    default: return 0xffff;
    }
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xfffff;
    if (a < 0x80000) return;
    if (a < 0x90000) {
        if (a >= 0x8e800 && a < 0x8f800) {
            const int i = (int)((a - 0x8e800) >> 1);
            C.pal_ram[i] = v;
            C.palette[i] = pal4(v);
        } else C.ram[(a & 0xffff) >> 1] = v;
    } else if (a >= 0xa0000 && a < 0xa000e) seibu_main_write(&C.snd, (int)((a & 0xf) >> 1), (uint8_t)v);
    else if (a >= 0xc0000 && a < 0xc0050) {
        const int i = crt_index(a);
        if (i >= 0) C.crt[i] = v;
    }
}

static uint16_t cpu_rd(void *u_, uint32_t a) { (void)u_; return read16(a); }
static void cpu_wr(void *u_, uint32_t a, uint16_t v) { (void)u_; write16(a, (uint16_t)v); }


// ---------------------------------------------------------------------------
// Video: the 256x256 screen's rows 16-239 (Sky Smasher: turned, ROT270)

static inline void put(int x, int y, uint16_t c)
{
#if SKYSMASH
    arc_fb_row(&C.fb, 255 - x)[y - 16] = c;
#else
    arc_fb_row(&C.fb, y - 16)[x] = c;
#endif
}

static void draw_sprites(int priority)
{
    for (int f = 0x1ff; f >= 0; f--) {
        const uint16_t *s = &C.ram[0x5800 + f * 4];
        const uint16_t attr = s[0];
        if (attr & 0x8000) continue;
        if (((attr >> 11) & 1) != priority) continue;
        const int width = (attr >> 7) & 7, height = (attr >> 4) & 7;
        const bool fx = (attr & 0x2000) != 0, fy = (attr & 0x4000) != 0;
        const int base = (attr & 0x0f) << 4;
        int code = s[1] & 0x1fff;
        int x = s[2] & 0x1ff, y = s[3] & 0x1ff;
        if (x >= 256) x -= 512;
        if (y >= 256) y -= 512;
        for (int w = 0; w <= width; w++) {
            const int tx = x + 16 * (fx ? width - w : w);
            for (int h = 0; h <= height; h++, code++) {
                const int ty = y + 16 * (fy ? height - h : h);
                for (int r = 0; r < 16; r++) {
                    const int py = ty + r;
                    if (py < 16 || py >= 240) continue;
                    for (int c = 0; c < 16; c++) {
                        const int px = tx + c;
                        if (px < 0 || px >= 256) continue;
                        const unsigned pen = arc_gfx_px(&kTileL, C.sprites, 0x100000, code & 0x1fff, fx ? 15 - c : c, fy ? 15 - r : r);
                        if (pen != 15) put(px, py, C.palette[base + pen]);
                    }
                }
            }
        }
    }
}

static void tile_layer(int ram_base, int code_add, int color_base, uint16_t sx, uint16_t sy, bool opaque)
{
    for (int y = 16; y < 240; y++) {
        const int py = (y + sy) & 255;
        for (int x = 0; x < 256; x++) {
            const int px = (x + sx) & 511;
            const uint16_t attr = C.ram[ram_base + (py >> 4) * 32 + (px >> 4)];
            const unsigned pen = arc_gfx_px(&kTileL, C.tiles, 0x100000, (attr & 0xfff) + code_add, px & 15, py & 15);
            if (opaque || pen != 15) put(x, y, C.palette[color_base + (attr >> 12) * 16 + pen]);
        }
    }
}

static void update_video(void)
{
    const uint16_t control = C.crt[0x0e];
    if (!(control & 0x01)) tile_layer(0x6000, 0, 0x400, C.crt[0x10], C.crt[0x11], true);
    else
        for (int y = 16; y < 240; y++)
            for (int x = 0; x < 256; x++) put(x, y, C.palette[0x800]);
    if (!(control & 0x10)) draw_sprites(1);
    if (!(control & 0x02)) tile_layer(0x6800, 0x1000, 0x500, C.crt[0x12], C.crt[0x13], false);
    if (!(control & 0x10)) draw_sprites(0);
    if (!(control & 0x08))              // the text: 32x32 characters, no scroll
        for (int i = 0; i < 0x400; i++) {
            const int cy = i >> 5, cx = i & 31;
            if (cy < 2 || cy >= 30) continue;
            const uint16_t attr = C.ram[0x6c00 + i];
            const int base = 0x700 + (attr >> 12) * 16;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    const unsigned pen = arc_gfx_px(&kCharL, C.chars, 0x20000, attr & 0xfff, c, r);
                    if (pen != 15) put(cx * 8 + c, cy * 8 + r, C.palette[base + pen]);
                }
        }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&C.fb, y); }

// the files of the even and odd bytes (each entry's offset: its first byte)
// into a blob of `size` (0xff where none)
static const uint8_t *blob_interleaved(const char *name, const arc_rom *roms, int n, uint32_t size, uint32_t key)
{
    arc_blob b;
    if (arc_blob_begin(&b, name, size, key)) {
        uint8_t *img = (uint8_t *)malloc(size), *part = (uint8_t *)malloc(roms[0].size);
        if (!img || !part) return NULL;
        memset(img, 0xff, size);
        for (int i = 0; i < n; i++) {
            arc_rom one = roms[i];
            one.offset = 0;
            if (!arc_load(&one, 1, part, one.size)) return NULL;
            for (uint32_t k = 0; k < one.size; k++) img[roms[i].offset + 2 * k] = part[k];
        }
        arc_blob_write(&b, 0, img, size);
        free(part);
        free(img);
    }
    return arc_blob_end(&b);
}
#define BLOB_IL(name, list, size, seed) blob_interleaved(name, list, (int)(sizeof list / sizeof list[0]), size, ARC_ROM_KEY(list, seed))


bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&C.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    C.snd.audio = C.audio;
    C.snd.max_audio = 1024;
    if (!(C.rom = BLOB_IL("program", kProgram, 0x80000, 0)) || !(C.chars = ARC_ROM_BLOB("chars", kChars, 0x20000)) ||
        !(C.tiles = ARC_ROM_BLOB("tiles", kTiles, 0x100000)) || !(C.sprites = ARC_ROM_BLOB("sprites", kSprites, 0x100000)) ||
        !(C.oki = ARC_ROM_BLOB("oki", kOki, 0x40000)))
        return false;
    arc_blob b;
    if (arc_blob_begin(&b, "sound", 0x14000, ARC_ROM_KEY(kSound, 2))) {
        uint8_t *r = (uint8_t *)malloc(0x10000), *out = (uint8_t *)malloc(0x14000);
        if (!r || !out) return false;
        memset(r, 0xff, 0x10000);
        if (!ARC_LOAD(kSound, r, 0x10000)) return false;
        seibu_prepare(r, false, out);
        arc_blob_write(&b, 0, out, 0x14000);
        free(r);
        free(out);
    }
    if (!(C.sound = arc_blob_end(&b))) return false;
    seibu_init(&C.snd, SOUND_CLOCK, C.sound, C.sound + 0x2000, C.sound + 0x4000);
    okid_init(&C.snd.oki, OKI_CLOCK, true, C.oki, 0x40000);
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    C.dsw = 0xffff;
    C.palette[0x800] = 0;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(C.ram, 0, sizeof C.ram);
    memset(C.pal_ram, 0, sizeof C.pal_ram);
    memset(C.crt, 0, sizeof C.crt);
    memset(C.palette, 0, sizeof C.palette);
    C.in0 = C.in1 = 0xffff;
    C.main_debt = C.sound_debt = 0;
    m68kd_reset(&cpu);
    seibu_reset(&C.snd);
    C.snd.coins = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const double mc = (double)MAIN_CLOCK / FPS / SCANLINES, sc = (double)SOUND_CLOCK / FPS / SCANLINES;
    C.snd.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            m68kd_set_irq(&cpu, IRQ_LEVEL, M68_HOLD);
            update_video();
        }
        C.main_debt += mc;
        C.main_debt -= m68kd_run(&cpu, (int)C.main_debt);
        C.sound_debt += sc;
        C.sound_debt -= seibu_run(&C.snd, (int)C.sound_debt);
    }
    if (audio) *audio = C.audio;
    return C.snd.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&C.fb); }

static uint16_t pad(unsigned p)
{
    uint16_t v = 0xff;
    if (p & AP_UP) v &= ~0x01;
    if (p & AP_DOWN) v &= ~0x02;
    if (p & AP_LEFT) v &= ~0x04;
    if (p & AP_RIGHT) v &= ~0x08;
    if (p & AP_B1) v &= ~0x10;
    if (p & AP_B2) v &= ~0x20;
    if (p & AP_B3) v &= ~0x40;
    return v;
}

void arc_input(const arc_in *in)
{
    C.in0 = (uint16_t)(pad(in->p[0]) | pad(in->p[1]) << 8);
    uint16_t sys = 0xffff;
    if (in->p[0] & AP_START) sys &= ~0x0001;
    if (in->p[1] & AP_START) sys &= ~0x0010;
    C.in1 = sys;
    C.snd.coins = (uint8_t)((in->coin1 ? 1 : 0) | (in->coin2 ? 2 : 0));
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw = (uint16_t)((C.dsw & 0xff00) | v);
    else if (bank == 1) C.dsw = (uint16_t)((C.dsw & 0x00ff) | (v << 8));
}
