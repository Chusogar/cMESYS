// cabal.c - Cabal (TAD, 1988) for the common arcade frontends (arc.h): C port
// of dsp-cpp's drivers/arcade/cabal (dsp-emulator's cabal_hw.pas, MAME's
// cabal.cpp). A 68000 at 10 MHz (Musashi here), a fixed 256x256 background
// of 16x16 tiles, 512 16x16 sprites and a layer of 2bpp text, 1024 colours of
// 4-bit RGB. The sound is the Seibu sound system with an encrypted Z80, a
// YM2151 and two ADPCM channels (seibu_snd.c, built with SEIBU_ADPCM); it
// also reads the coins. The joystick version's inputs, as dsp-cpp.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "seibu_snd.h"

static m68kd cpu;

#define MAIN_CLOCK 10000000
#define SOUND_CLOCK 3579545
#define FPS 59.60
#define SCANLINES 256
#define RATE SEIBU_RATE
#define W 256
#define H 224

static const char *const kSets[] = { "cabal", NULL };
static const arc_rom kProgram[] = {
    {"13.7h", 0x10000, 0x00000, 0x00abbe0c}, {"11.6h", 0x10000, 0x00001, 0x44736281},
    {"12.7j", 0x10000, 0x20000, 0xd763a47c}, {"10.6j", 0x10000, 0x20001, 0x96d5e8af}};
static const arc_rom kChars[] = {{"5-6s", 0x4000, 0, 0x6a76955a}};
static const arc_rom kSprites[] = {
    {"sp_rom1.bin", 0x10000, 0x00000, 0x34d3cac8}, {"sp_rom2.bin", 0x10000, 0x00001, 0x4e49c28e},
    {"sp_rom3.bin", 0x10000, 0x20000, 0x7065e840}, {"sp_rom4.bin", 0x10000, 0x20001, 0x6a0e739d},
    {"sp_rom5.bin", 0x10000, 0x40000, 0x0e1ec30e}, {"sp_rom6.bin", 0x10000, 0x40001, 0x581a50c1},
    {"sp_rom7.bin", 0x10000, 0x60000, 0x55c44764}, {"sp_rom8.bin", 0x10000, 0x60001, 0x702735c9}};
static const arc_rom kTiles[] = {
    {"bg_rom1.bin", 0x10000, 0x00000, 0x1023319b}, {"bg_rom2.bin", 0x10000, 0x00001, 0x3b6d2b09},
    {"bg_rom3.bin", 0x10000, 0x20000, 0x420b0801}, {"bg_rom4.bin", 0x10000, 0x20001, 0x77bc7a60},
    {"bg_rom5.bin", 0x10000, 0x40000, 0x543fcb37}, {"bg_rom6.bin", 0x10000, 0x40001, 0x0bc50075},
    {"bg_rom7.bin", 0x10000, 0x60000, 0xd28d921e}, {"bg_rom8.bin", 0x10000, 0x60001, 0x67e4fe47}};
static const arc_rom kSound[] = {{"4-3n", 0x2000, 0x0000, 0x4038eff2}, {"3-3p", 0x8000, 0x8000, 0xd9defcbf}};
static const arc_rom kAdpcm[] = {{"2-1s", 0x10000, 0, 0x850406b4}, {"1-1u", 0x10000, 0x10000, 0x8b3e0789}};

const arc_info arc_game = { "cabal", "Cabal", "TAD", 1988, W, H, FPS, RATE, kSets,
                            { "FIRE", "GRENADE", "ROLL", NULL }, ARC_JOY_8WAY };

static const int kCX[8] = {3, 2, 1, 0, 11, 10, 9, 8};
static const int kCY[8] = {0, 16, 32, 48, 64, 80, 96, 112};
static const int kSX[16] = {3, 2, 1, 0, 19, 18, 17, 16, 35, 34, 33, 32, 51, 50, 49, 48};
static const int kSY[16] = {960, 896, 832, 768, 704, 640, 576, 512, 448, 384, 320, 256, 192, 128, 64, 0};
static const int kTX[16] = {3, 2, 1, 0, 19, 18, 17, 16, 515, 514, 513, 512, 531, 530, 529, 528};
static const int kTY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480};
static const arc_layout kCharL = { 8, 8, 0x400, 2, 128, false, false, false, {0, 4}, kCX, kCY };
static const arc_layout kSprL = { 16, 16, 0x1000, 4, 1024, false, false, false, {8, 12, 0, 4}, kSX, kSY };
static const arc_layout kTileL = { 16, 16, 0x1000, 4, 1024, false, false, false, {8, 12, 0, 4}, kTX, kTY };

static struct state {
    seibu_snd snd;
    const uint8_t *rom, *chars, *sprites, *tiles, *sound, *adpcm;
    uint16_t ram[0x8000], fg_ram[0x400], bg_ram[0x200], pal_ram[0x400], palette[0x400];
    arc_fb fb;
    uint16_t in0, in1, dsw;
    double main_debt, sound_debt;
    int16_t audio[1024];
} *C_;
#define C (*C_)

// ---------------------------------------------------------------------------
// The 68000


static uint16_t pal4(uint16_t v)
{
    const uint32_t r = v & 15, g = (v >> 4) & 15, b = (v >> 8) & 15;
    return arc_rgb565((r * 17) << 16 | (g * 17) << 8 | (b * 17));
}

static uint16_t read16(uint32_t a)
{
    a &= 0xfffff;
    if (a < 0x40000) return (uint16_t)(C.rom[a & ~1u] << 8 | C.rom[a | 1]);
    if (a < 0x50000) return C.ram[(a & 0xffff) >> 1];
    if (a >= 0x60000 && a < 0x60800) return C.fg_ram[(a & 0x7ff) >> 1];
    if (a >= 0x80000 && a < 0x80400) return C.bg_ram[(a & 0x3ff) >> 1];
    if (a >= 0xe0000 && a < 0xe0800) return C.pal_ram[(a & 0x7ff) >> 1];
    if (a >= 0xe8000 && a < 0xe800e) return seibu_main_read(&C.snd, (int)((a & 0xe) >> 1));
    switch (a & ~1u) {
    case 0xa0000: return C.dsw;
    case 0xa0008: return C.in0;
    case 0xa000c: return 0xffff;        // the trackballs
    case 0xa000a: case 0xa000e: return 0;
    case 0xa0010: return C.in1;
    default: return 0xffff;
    }
}

static void write16(uint32_t a, uint16_t v)
{
    a &= 0xfffff;
    if (a < 0x40000) return;
    if (a < 0x50000) C.ram[(a & 0xffff) >> 1] = v;
    else if (a >= 0x60000 && a < 0x60800) C.fg_ram[(a & 0x7ff) >> 1] = v;
    else if (a >= 0x80000 && a < 0x80400) C.bg_ram[(a & 0x3ff) >> 1] = v;
    else if (a >= 0xe0000 && a < 0xe0800) {
        const int i = (a & 0x7ff) >> 1;
        C.pal_ram[i] = v;
        C.palette[i] = pal4(v);
    } else if (a >= 0xe8000 && a < 0xe800e) seibu_main_write(&C.snd, (int)((a & 0xe) >> 1), (uint8_t)v);
}

static uint16_t cpu_rd(void *u_, uint32_t a) { (void)u_; return read16(a); }
static void cpu_wr(void *u_, uint32_t a, uint16_t v) { (void)u_; write16(a, (uint16_t)v); }

// ---------------------------------------------------------------------------
// Video: the 256x256 screen's rows 16-239

static void update_video(void)
{
    for (int y = 0; y < H; y++) {       // the background, colours 512-767
        const int sy = y + 16;
        uint16_t *out = arc_fb_row(&C.fb, y);
        for (int x = 0; x < W; x++) {
            const uint16_t attr = C.bg_ram[(sy >> 4) * 16 + (x >> 4)];
            out[x] = C.palette[512 + ((attr >> 12) & 0xf) * 16 + arc_gfx_px(&kTileL, C.tiles, 0x80000, attr & 0xfff, x & 15, sy & 15)];
        }
    }
    for (int f = 0x1ff; f >= 0; f--) {  // the sprites, the last first, pen 15 clear
        const uint16_t *s = &C.ram[0x1c00 + f * 4];
        if (!(s[0] & 0x100)) continue;
        const int code = s[1] & 0xfff, base = 256 + ((s[2] & 0x7800) >> 11) * 16;
        const bool fx = (s[2] & 0x400) != 0;
        const int sy = s[0] & 0xff;
        int sx = s[2] & 0x1ff;
        if (sx > 256) sx -= 512;
        for (int r = 0; r < 16; r++) {
            const int py = sy + r;
            if (py >= 256) break;
            if (py < 16 || py >= 16 + H) continue;
            uint16_t *out = arc_fb_row(&C.fb, py - 16);
            for (int c = 0; c < 16; c++) {
                const int px = sx + c;
                if (px < 0 || px >= 256) continue;
                const unsigned pen = arc_gfx_px(&kSprL, C.sprites, 0x80000, code, fx ? 15 - c : c, r);
                if (pen != 15) out[px] = C.palette[base + pen];
            }
        }
    }
    for (int f = 0; f < 0x400; f++) {   // the text, pen 3 clear
        const uint16_t attr = C.fg_ram[f];
        const int base = ((attr >> 10) & 0x3f) * 4, tx = (f & 31) * 8, ty = (f >> 5) * 8;
        const uint8_t *g = C.chars + (attr & 0x3ff) * 64;
        for (int r = 0; r < 8; r++) {
            if (ty + r < 16 || ty + r >= 16 + H) continue;
            uint16_t *out = arc_fb_row(&C.fb, ty + r - 16);
            for (int c = 0; c < 8; c++)
                if (g[r * 8 + c] != 3) out[tx + c] = C.palette[base + g[r * 8 + c]];
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
    if (!(C_ = (struct state *)calloc(1, sizeof(struct state))) || !arc_fb_init(&C.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    C.snd.audio = C.audio;
    C.snd.max_audio = 1024;
    arc_blob b;
    if (!(C.rom = BLOB_IL("program", kProgram, 0x40000, 0)) || !(C.sprites = BLOB_IL("sprites", kSprites, 0x80000, 1)) ||
        !(C.tiles = BLOB_IL("tiles", kTiles, 0x80000, 2)))
        return false;
    const uint8_t *raw = ARC_ROM_BLOB("chars.raw", kChars, 0x4000);
    if (!raw) return false;
    if (arc_blob_begin(&b, "chars", 0x400 * 64, ARC_ROM_KEY(kChars, 1))) arc_blob_gfx(&b, &kCharL, raw, 0x4000, 0);
    arc_blob_free(raw);
    if (!(C.chars = arc_blob_end(&b))) return false;
    if (arc_blob_begin(&b, "sound", 0x14000, ARC_ROM_KEY(kSound, 2))) {
        uint8_t *r = (uint8_t *)malloc(0x10000), *out = (uint8_t *)malloc(0x14000);
        if (!r || !out) return false;
        memset(r, 0xff, 0x10000);
        if (!ARC_LOAD(kSound, r, 0x10000)) return false;
        seibu_prepare(r, true, out);
        arc_blob_write(&b, 0, out, 0x14000);
        free(r);
        free(out);
    }
    if (!(C.sound = arc_blob_end(&b))) return false;
    if (arc_blob_begin(&b, "adpcm", 0x20000, ARC_ROM_KEY(kAdpcm, 3))) {
        uint8_t *r = (uint8_t *)malloc(0x20000);
        if (!r || !ARC_LOAD(kAdpcm, r, 0x20000)) return false;
        for (int i = 0; i < 0x20000; i++) {   // the data lines scrambled on the board
            const uint8_t v = r[i];
            r[i] = (uint8_t)((v & 0x80) | ((v & 0x20) << 1) | ((v & 0x08) << 2) | ((v & 0x02) << 3) |
                             ((v & 0x40) >> 3) | ((v & 0x10) >> 2) | ((v & 0x04) >> 1) | (v & 0x01));
        }
        arc_blob_write(&b, 0, r, 0x20000);
        free(r);
    }
    if (!(C.adpcm = arc_blob_end(&b))) return false;
    seibu_init(&C.snd, SOUND_CLOCK, C.sound, C.sound + 0x2000, C.sound + 0x4000);
    for (int i = 0; i < 2; i++) {
        C.snd.adpcm[i].rom = C.adpcm + 0x10000 * i;
        C.snd.adpcm[i].rom_size = 0x10000;
    }
    m68kd_init(&cpu, MAIN_CLOCK, M68K_T_68000);
    cpu.read_word = cpu_rd;
    cpu.write_word = cpu_wr;
    C.dsw = 0xefff;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(C.ram, 0, sizeof C.ram);
    memset(C.fg_ram, 0, sizeof C.fg_ram);
    memset(C.bg_ram, 0, sizeof C.bg_ram);
    memset(C.pal_ram, 0, sizeof C.pal_ram);
    memset(C.palette, 0, sizeof C.palette);
    C.in0 = C.in1 = 0xffff;
    C.main_debt = C.sound_debt = 0;
    m68kd_reset(&cpu);
    seibu_reset(&C.snd);
    C.snd.coins = 0xfc;
}

int arc_run_frame(const int16_t **audio)
{
    const double mc = (double)MAIN_CLOCK / FPS / SCANLINES, sc = (double)SOUND_CLOCK / FPS / SCANLINES;
    C.snd.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            update_video();
            m68kd_set_irq(&cpu, 1, M68_HOLD);
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

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint16_t a = 0xffff, b = 0xffff;
    if (p1 & AP_UP) a &= ~0x0100;
    if (p1 & AP_DOWN) a &= ~0x0200;
    if (p1 & AP_LEFT) a &= ~0x0400;
    if (p1 & AP_RIGHT) a &= ~0x0800;
    if (p2 & AP_UP) a &= ~0x1000;
    if (p2 & AP_DOWN) a &= ~0x2000;
    if (p2 & AP_LEFT) a &= ~0x4000;
    if (p2 & AP_RIGHT) a &= ~0x8000;
    if (p1 & AP_B1) b &= ~0x0001;
    if (p1 & AP_B2) b &= ~0x0002;
    if (p2 & AP_B1) b &= ~0x0004;
    if (p2 & AP_B2) b &= ~0x0008;
    if (p2 & AP_B3) b &= ~0x1000;
    if (p1 & AP_B3) b &= ~0x2000;
    if (p2 & AP_START) b &= ~0x4000;
    if (p1 & AP_START) b &= ~0x8000;
    C.in0 = a;
    C.in1 = b;
    C.snd.coins = (uint8_t)(0xfc | (in->coin2 ? 1 : 0) | (in->coin1 ? 2 : 0));
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) C.dsw = (uint16_t)((C.dsw & 0xff00) | v);
    else if (bank == 1) C.dsw = (uint16_t)((C.dsw & 0x00ff) | (v << 8));
}
