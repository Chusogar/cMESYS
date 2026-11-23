// appoooh.c - Appoooh (Sanritsu / Sega, 1984) and Robo Wres 2001 (Sega,
// 1986) for the common arcade frontends (arc.h): C port of dsp-cpp's drivers/arcade/appoooh (from
// dsp-emulator's appoooh_hw.pas). A Z80 at 3 MHz, three SN76496, an MSM5205
// , two layers of 8x8
// tiles and two banks of 16x16 sprites with a priority switch. The ADPCM is
// MAME's hookup (dsp-cpp's played little of it): a byte a sample pair from
// the address the main CPU gives, until a 0x70.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/sn76496.h"
#include "../../../sound/msm5205dsp.h"
#include "../../../machine/sega_decrypt.h"

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif
#define ROBO (ARC_VARIANT == 1)     // Robo Wres: the 315-5179 cipher, twice the tiles

#define CLOCK (18432000 / 6)
#define MSM_CLOCK 384000
#define FPS 60.0
#define SCANLINES 256
#define RATE 44100
#define W 256
#define H 224

#if ROBO
static const char *const kSets[] = { "robowres", NULL };
static const arc_rom kMain[] = {
    {"epr-7540.13d", 0x8000, 0x00000, 0xa2a54237}, {"epr-7541.14d", 0x8000, 0x08000, 0xcbf7d1a8},
    {"epr-7542.15d", 0x8000, 0x10000, 0x3475fbd4}};
static const arc_rom kChar1[] = {
    {"epr-7544.7h", 0x8000, 0x00000, 0x07b846ce}, {"epr-7545.6h", 0x8000, 0x08000, 0xe99897be},
    {"epr-7546.5h", 0x8000, 0x10000, 0x1559235a}};
static const arc_rom kChar2[] = {
    {"epr-7547.7d", 0x8000, 0x00000, 0xb87ad4a4}, {"epr-7548.6d", 0x8000, 0x08000, 0x8b9c75b3},
    {"epr-7549.5d", 0x8000, 0x10000, 0xf640afbb}};
static const arc_rom kProm[] = {
    {"pr7571.10a", 0x20, 0x000, 0xe82c6d5c}, {"pr7572.7f", 0x100, 0x020, 0x2b083d0c},
    {"pr7573.7g", 0x100, 0x120, 0x2b083d0c}};
static const arc_rom kAdpcm[] = {{"epr-7543.12b", 0x8000, 0x0000, 0x4d108c49}};
const arc_info arc_game = { "robowres", "Robo Wres 2001", "Sega", 1986, W, H, FPS, RATE, kSets,
                            { "B1", "B2", NULL, NULL }, ARC_JOY_8WAY };
#define TILES 0x1000
#define SPRITES 0x400
#define SPRITE_BASE 0x200
#define GFX_SIZE 0x18000
#define DSW 0xe0
#else
static const char *const kSets[] = { "appoooh", NULL };
static const arc_rom kMain[] = {
    {"epr-5906.bin", 0x2000, 0x00000, 0xfffae7fe}, {"epr-5907.bin", 0x2000, 0x02000, 0x57696cd6},
    {"epr-5908.bin", 0x2000, 0x04000, 0x4537cddc}, {"epr-5909.bin", 0x2000, 0x06000, 0xcf82718d},
    {"epr-5910.bin", 0x2000, 0x08000, 0x312636da}, {"epr-5911.bin", 0x2000, 0x0a000, 0x0bc2acaa},
    {"epr-5913.bin", 0x2000, 0x0c000, 0xf5a0e6a7}, {"epr-5912.bin", 0x2000, 0x0e000, 0x3c3915ab},
    {"epr-5914.bin", 0x2000, 0x10000, 0x58792d4a}};
static const arc_rom kChar1[] = {
    {"epr-5895.bin", 0x4000, 0x0000, 0x4b0d4294}, {"epr-5896.bin", 0x4000, 0x4000, 0x7bc84d75},
    {"epr-5897.bin", 0x4000, 0x8000, 0x745f3ffa}};
static const arc_rom kChar2[] = {
    {"epr-5898.bin", 0x4000, 0x0000, 0xcf01644d}, {"epr-5899.bin", 0x4000, 0x4000, 0x885ad636},
    {"epr-5900.bin", 0x4000, 0x8000, 0xa8ed13f3}};
static const arc_rom kProm[] = {
    {"pr5921.prm", 0x20, 0x000, 0xf2437229}, {"pr5922.prm", 0x100, 0x020, 0x85c542bf},
    {"pr5923.prm", 0x100, 0x120, 0x16acbd53}};
static const arc_rom kAdpcm[] = {
    {"epr-5901.bin", 0x2000, 0x0000, 0x170a10a4}, {"epr-5902.bin", 0x2000, 0x2000, 0xf6981640},
    {"epr-5903.bin", 0x2000, 0x4000, 0x0439df50}, {"epr-5904.bin", 0x2000, 0x6000, 0x9988f2ae},
    {"epr-5905.bin", 0x2000, 0x8000, 0xfb5cd70e}};

const arc_info arc_game = { "appoooh", "Appoooh", "Sanritsu / Sega", 1984, W, H, FPS, RATE, kSets,
                            { "B1", "B2", NULL, NULL }, ARC_JOY_8WAY };
#define TILES 0x800
#define SPRITES 0x200
#define SPRITE_BASE 0
#define GFX_SIZE 0xc000
#define DSW 0x60
#endif

static const int kX8[8] = {7, 6, 5, 4, 3, 2, 1, 0};
static const int kX16[16] = {7, 6, 5, 4, 3, 2, 1, 0, 71, 70, 69, 68, 67, 66, 65, 64};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
    sn76496 sn[3];
    msm5205d msm;
    const uint8_t *rom, *adpcm;         // blobs (the CYD: in the flash)
    const uint8_t *chars[2], *sprites[2];
    uint8_t ram[0x2000];                // e000-ffff
    uint16_t palette[512];
    arc_fb fb;
    uint8_t in0, in1, in2, dsw, priority, bank;
    bool nmi_vblank, flip;
    uint32_t adpcm_address;             // 0xffffffff: stopped
    int adpcm_data;                     // -1: the next byte to fetch
    int64_t audio_acc, msm_acc;
    int16_t audio[1024];
    int naudio;
} *A_;
#define A (*A_)

#define MEM(a) A.ram[(a) - 0xe000]

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x9fff) return A.rom[a];
    if (a >= 0xe000) return MEM(a);
    return A.rom[0xa000 + (A.bank & 1) * 0x4000 + (a - 0xa000)];
}
#if ROBO
static uint8_t op_rd(void *u, uint16_t a) { return a <= 0x7fff ? A.rom[0x12000 + a] : rd(u, a); }
#endif
static void wr(void *u, uint16_t a, uint8_t v) { (void)u; if (a >= 0xe000) MEM(a) = v; }

static uint8_t port_in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0: return A.in0;
    case 1: return A.in1;
    case 3: return A.dsw;
    case 4: return A.in2;
    default: return 0xff;
    }
}

static void port_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0: case 1: case 2: sn76496_write(&A.sn[p & 3], v); break;
    case 3:
        A.adpcm_address = (uint32_t)v << 8;
        msm5205d_set_reset(&A.msm, false);
        A.adpcm_data = -1;
        break;
    case 4:
        A.nmi_vblank = (v & 1) != 0;
        A.flip = (v & 2) != 0;
        A.priority = (uint8_t)((v & 0x30) >> 4);
        A.bank = (uint8_t)((v & 0x40) >> 6);
        break;
    default: break;
    }
}

// each VCLK: the high nibble of a new byte, then its low one (MAME's appoooh)
static void adpcm_vclk(void *u)
{
    (void)u;
    if (A.adpcm_address == 0xffffffff) return;
    if (A.adpcm_data == -1) {
        A.adpcm_data = A.adpcm[A.adpcm_address++ & 0xffff];
        msm5205d_data_w(&A.msm, (uint8_t)(A.adpcm_data >> 4));
        if (A.adpcm_data == 0x70) {
            A.adpcm_address = 0xffffffff;
            msm5205d_set_reset(&A.msm, true);
        }
    } else {
        msm5205d_data_w(&A.msm, (uint8_t)(A.adpcm_data & 0x0f));
        A.adpcm_data = -1;
    }
}

static void cycles(int c)
{
    A.msm_acc += (int64_t)c * msm5205d_sample_frequency(&A.msm);
    while (A.msm_acc >= CLOCK) {
        A.msm_acc -= CLOCK;
        msm5205d_vclk(&A.msm);
    }
    A.audio_acc += (int64_t)c * RATE;
    while (A.audio_acc >= CLOCK) {
        A.audio_acc -= CLOCK;
        const int32_t s = sn76496_update(&A.sn[0]) + sn76496_update(&A.sn[1]) + sn76496_update(&A.sn[2]) + msm5205d_output(&A.msm);
        if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(s);
    }
}

// The 256x256 screen, shown from row 16 (turned round when flipped), is
// built straight in the picture: (sx, sy) of it is fb (x, y) or nothing
static inline uint16_t *screen_px(int sx, int sy)
{
    const int x = A.flip ? 255 - sx : sx, y = A.flip ? 255 - sy - 16 : sy - 16;
    return y >= 0 && y < H && x >= 0 && x < W ? arc_fb_row(&A.fb, y) + x : NULL;
}

// a pixel of a tile layer: palette index + 1, 0 transparent
static inline int layer_px(int layer, int sx, int sy)
{
    const int f = (sy >> 3) * 32 + (sx >> 3);
    const uint8_t attr = MEM((layer ? 0xfc00 : 0xf400) + f);
    const int code = MEM((layer ? 0xf800 : 0xf000) + f) + ((attr >> 5) & 7) * 256;
    const int c = (attr & 0x10) ? 7 - (sx & 7) : sx & 7;
    const uint8_t pen = A.chars[layer][(size_t)code * 64 + (sy & 7) * 8 + c];
    return pen ? (((((attr & 0x0f) << 3) + (layer ? 256 : 0) + pen) & 0x1ff) + 1) : 0;
}

static void blit_sprite(int dx, int dy, const uint8_t *src, int base, bool fx)
{
    for (int y = 0; y < 16; y++) {
        const int py = dy + y;
        if (py < 0 || py >= 256) continue;
        for (int x = 0; x < 16; x++) {
            const int px = dx + x;
            if (px < 0 || px >= 256) continue;
            const uint8_t pen = src[y * 16 + (fx ? 15 - x : x)];
            uint16_t *d;
            if (pen && (d = screen_px(px, py))) *d = A.palette[(base + pen) & 0x1ff];
        }
    }
}

static void draw_sprites(int bank)
{
    const int base = 0xf000 + 0x800 * bank;
    for (int f = 7; f >= 0; f--) {
        const uint8_t attr = MEM(base + 1 + f * 4), attr2 = MEM(base + 2 + f * 4);
        const int code = SPRITE_BASE + (attr >> 2) + ((attr2 & 0xe0) << 1);
        blit_sprite(MEM(base + 3 + f * 4), 240 - MEM(base + f * 4), A.sprites[bank] + (size_t)code * 256,
                    ((attr2 & 0x0f) << 3) + 0x100 * bank, (attr & 1) != 0);
    }
}

static void update_video(void)
{
    // BG, FG on top of it (priority 0), the sprites, FG over them (otherwise)
    for (int y = 0; y < H; y++) {
        uint16_t *out = arc_fb_row(&A.fb, y);
        for (int x = 0; x < W; x++) {
            const int sx = A.flip ? 255 - x : x, sy = A.flip ? 255 - (y + 16) : y + 16;
            int p = layer_px(1, sx, sy);
            if (A.priority == 0) {
                const int f = layer_px(0, sx, sy);
                if (f) p = f;
            }
            out[x] = p ? A.palette[p - 1] : 0;
        }
    }
    if (A.priority == 1) { draw_sprites(0); draw_sprites(1); }
    else { draw_sprites(1); draw_sprites(0); }
    if (A.priority != 0)
        for (int y = 0; y < H; y++) {
            uint16_t *out = arc_fb_row(&A.fb, y);
            for (int x = 0; x < W; x++) {
                const int f = layer_px(0, A.flip ? 255 - x : x, A.flip ? 255 - (y + 16) : y + 16);
                if (f) out[x] = A.palette[f - 1];
            }
        }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&A.fb, y); }

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t prom[0x220];
    if (!arc_fb_init(&A.fb, W, H)) return false;
    arc_frame_row_hook = frame_row;
    // 0000-9fff, then the two banks of a000-dfff
#if ROBO
    {   // (Robo Wres: then the decrypted opcodes of 0000-7fff)
        arc_blob b;
        if (arc_blob_begin(&b, "program", 0x1a000, ARC_ROM_KEY(kMain, 0))) {
            uint8_t *raw = (uint8_t *)malloc(0x18000), data[0x400], ops[0x400];
            if (!raw || !ARC_LOAD(kMain, raw, 0x18000)) return false;
            for (uint32_t a = 0; a < 0x8000; a += 0x400) {
                sega_decrypt_type2_part(raw + a, 0x400, a, SEGA2_315_5179, 0, data, ops);
                arc_blob_write(&b, a, data, 0x400);
                arc_blob_write(&b, 0x12000 + a, ops, 0x400);
            }
            arc_blob_write(&b, 0x8000, raw + 0x8000, 0x2000);
            arc_blob_write(&b, 0xa000, raw + 0xa000, 0x4000);
            arc_blob_write(&b, 0xe000, raw + 0x12000, 0x4000);
            free(raw);
        }
        if (!(A.rom = arc_blob_end(&b))) return false;
    }
#else
    if (!(A.rom = ARC_ROM_BLOB("program", kMain, 0x12000))) return false;
#endif
    for (int k = 0; k < 2; k++) {
        const uint8_t *raw = k ? ARC_ROM_BLOB("gfx2.raw", kChar2, GFX_SIZE) : ARC_ROM_BLOB("gfx1.raw", kChar1, GFX_SIZE);
        if (!raw) return false;
        const int t = TILES;
        arc_layout cl = { 8, 8, t, 3, 64, false, false, false, {t * 128, t * 64, 0}, kX8, kY8 };
        // sprites: the codes reached
        arc_layout sl = { 16, 16, SPRITES, 3, 256, false, false, false, {t * 128, t * 64, 0}, kX16, kY16 };
        const uint32_t key = k ? ARC_ROM_KEY(kChar2, 1) : ARC_ROM_KEY(kChar1, 1);
        arc_blob b;
        if (arc_blob_begin(&b, k ? "chars2" : "chars1", t * 64, key)) arc_blob_gfx(&b, &cl, raw, GFX_SIZE, 0);
        if (!(A.chars[k] = arc_blob_end(&b))) return false;
        if (arc_blob_begin(&b, k ? "sprites2" : "sprites1", SPRITES * 256, key + 1)) arc_blob_gfx(&b, &sl, raw, GFX_SIZE, 0);
        arc_blob_free(raw);
        if (!(A.sprites[k] = arc_blob_end(&b))) return false;
    }
    if (!ARC_LOAD(kProm, prom, sizeof prom) || !(A.adpcm = ARC_ROM_BLOB("adpcm", kAdpcm, 0x10000))) return false;
    for (int f = 0; f < 0x200; f++) {
        int pen = prom[0x20 + f] & 0x0f;
        if (f > 0xff && !ROBO) pen |= 0x10;
        const uint8_t v = prom[pen];
        const int r = 0x21 * (v & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        A.palette[f] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    A.cpu.read = rd;
#if ROBO
    A.cpu.opcode_read = op_rd;
#endif
    A.cpu.write = wr;
    A.cpu.in = port_in;
    A.cpu.out = port_out;
    zd_init(&A.cpu);
    for (int i = 0; i < 3; i++) sn76496_init(&A.sn[i], CLOCK, RATE, 1.0f);
    msm5205d_init(&A.msm, MSM_CLOCK, 64, 4);
    A.msm.vclk_handler = adpcm_vclk;
    A.dsw = DSW;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&A.cpu);
    for (int i = 0; i < 3; i++) sn76496_reset(&A.sn[i]);
    msm5205d_reset(&A.msm);
    A.in0 = A.in1 = A.in2 = 0;
    A.nmi_vblank = A.flip = false;
    A.adpcm_address = 0xffffffff;
    A.adpcm_data = -1;
    A.priority = 0xff;
    A.bank = 0;
    A.audio_acc = A.msm_acc = 0;
    for (int y = 0; y < H; y++) memset(arc_fb_row(&A.fb, y), 0, W * 2);
}

int arc_run_frame(const int16_t **audio)
{
    int slice = (int)(CLOCK / FPS) / SCANLINES;
    if (slice < 1) slice = 1;
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (A.nmi_vblank) zd_set_nmi(&A.cpu, ZD_PULSE);
            update_video();
        }
        zd_run(&A.cpu, slice);
        cycles(slice);
    }
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&A.fb); }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0, b = 0;
    if (p1 & AP_UP) a |= 0x01;
    if (p1 & AP_RIGHT) a |= 0x02;
    if (p1 & AP_DOWN) a |= 0x04;
    if (p1 & AP_LEFT) a |= 0x08;
    if (p1 & AP_B1) a |= 0x10;
    if (in->coin1) a |= 0x20;
    if (in->coin2) a |= 0x40;
    if (p1 & AP_B2) a |= 0x80;
    if (p2 & AP_UP) b |= 0x01;
    if (p2 & AP_RIGHT) b |= 0x02;
    if (p2 & AP_DOWN) b |= 0x04;
    if (p2 & AP_LEFT) b |= 0x08;
    if (p2 & AP_B1) b |= 0x10;
    if (p1 & AP_START) b |= 0x20;
    if (p2 & AP_START) b |= 0x40;
    if (p2 & AP_B2) b |= 0x80;
    A.in0 = a;
    A.in1 = b;
    A.in2 = 0;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw = v;
}
