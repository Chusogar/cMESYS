// appoooh.c - Appoooh (Sanritsu / Sega, 1984) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/appoooh (from
// dsp-emulator's appoooh_hw.pas). A Z80 at 3 MHz, three SN76496, an MSM5205
// (dsp-cpp's hookup: it plays little of the ADPCM ROM), two layers of 8x8
// tiles and two banks of 16x16 sprites with a priority switch.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/sn76496.h"
#include "../../../sound/msm5205dsp.h"

#define CLOCK (18432000 / 6)
#define MSM_CLOCK 384000
#define FPS 60.0
#define SCANLINES 256
#define RATE 44100
#define W 256
#define H 224

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

static const int kX8[8] = {7, 6, 5, 4, 3, 2, 1, 0};
static const int kX16[16] = {7, 6, 5, 4, 3, 2, 1, 0, 71, 70, 69, 68, 67, 66, 65, 64};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 cpu;
    sn76496 sn[3];
    msm5205d msm;
    uint8_t mem[0x10000], bank_rom[2][0x4000], adpcm[0x10000];
    uint8_t *chars[2], *sprites[2];
    uint16_t palette[512];
    uint16_t bg[256 * 256], fg[256 * 256], compose[256 * 256];   // palette index + 1
    uint16_t *fb;
    uint8_t in0, in1, in2, dsw, priority, bank;
    bool nmi_vblank, flip, adpcm_playing;
    uint32_t adpcm_pos;
    int64_t audio_acc, msm_acc;
    int16_t audio[1024];
    int naudio;
} *A_;
#define A (*A_)

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x9fff || a >= 0xe000) return A.mem[a];
    return A.bank_rom[A.bank & 1][a - 0xa000];
}
static void wr(void *u, uint16_t a, uint8_t v) { (void)u; if (a >= 0xe000) A.mem[a] = v; }

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
        A.adpcm_pos = ((uint32_t)v << 8) * 2;
        msm5205d_set_reset(&A.msm, false);
        A.adpcm_playing = true;
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

static void adpcm_vclk(void *u)
{
    (void)u;
    if (!A.adpcm_playing) return;
    if (A.adpcm_pos / 2 >= 0x10000) {
        msm5205d_set_reset(&A.msm, true);
        A.adpcm_playing = false;
        return;
    }
    const uint8_t d = A.adpcm[A.adpcm_pos / 2];
    if (d == 0x70) {
        msm5205d_set_reset(&A.msm, true);
        A.adpcm_playing = false;
        return;
    }
    msm5205d_data_w(&A.msm, (A.adpcm_pos & 1) ? d & 0x0f : d >> 4);
    A.adpcm_pos = (A.adpcm_pos + 1) & 0x1ffff;
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

static void blit(uint16_t *dst, int dx, int dy, const uint8_t *src, int size, int base, bool fx)
{
    for (int y = 0; y < size; y++) {
        const int py = dy + y;
        if (py < 0 || py >= 256) continue;
        for (int x = 0; x < size; x++) {
            const int px = dx + x;
            if (px < 0 || px >= 256) continue;
            const uint8_t pen = src[y * size + (fx ? size - 1 - x : x)];
            if (pen) dst[py * 256 + px] = (uint16_t)(((base + pen) & 0x1ff) + 1);
        }
    }
}

static void draw_sprites(int bank)
{
    const int base = 0xf000 + 0x800 * bank;
    for (int f = 7; f >= 0; f--) {
        const uint8_t attr = A.mem[base + 1 + f * 4], attr2 = A.mem[base + 2 + f * 4];
        const int code = (attr >> 2) + ((attr2 & 0xe0) << 1);
        blit(A.compose, A.mem[base + 3 + f * 4], 240 - A.mem[base + f * 4], A.sprites[bank] + (size_t)code * 256, 16,
             ((attr2 & 0x0f) << 3) + 0x100 * bank, (attr & 1) != 0);
    }
}

static void update_video(void)
{
    memset(A.bg, 0, sizeof A.bg);
    memset(A.fg, 0, sizeof A.fg);
    for (int f = 0; f < 0x400; f++) {
        const int x = (f & 0x1f) * 8, y = (f >> 5) * 8;
        uint8_t attr = A.mem[0xfc00 + f];
        blit(A.bg, x, y, A.chars[1] + (size_t)(A.mem[0xf800 + f] + ((attr >> 5) & 7) * 256) * 64, 8, ((attr & 0x0f) << 3) + 256,
             (attr & 0x10) != 0);
        attr = A.mem[0xf400 + f];
        blit(A.fg, x, y, A.chars[0] + (size_t)(A.mem[0xf000 + f] + ((attr >> 5) & 7) * 256) * 64, 8, (attr & 0x0f) << 3,
             (attr & 0x10) != 0);
    }
    for (int i = 0; i < 256 * 256; i++) A.compose[i] = A.bg[i];
    if (A.priority == 0)
        for (int i = 0; i < 256 * 256; i++) if (A.fg[i]) A.compose[i] = A.fg[i];
    if (A.priority == 1) { draw_sprites(0); draw_sprites(1); }
    else { draw_sprites(1); draw_sprites(0); }
    if (A.priority != 0)
        for (int i = 0; i < 256 * 256; i++) if (A.fg[i]) A.compose[i] = A.fg[i];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int sx = A.flip ? 255 - x : x, sy = A.flip ? 255 - (y + 16) : y + 16;
            const uint16_t p = A.compose[sy * 256 + sx];
            A.fb[y * W + x] = p ? A.palette[p - 1] : 0;
        }
}

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    A.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *rom = (uint8_t *)calloc(1, 0x18000), prom[0x220];
    if (!A.fb || !rom || !ARC_LOAD(kMain, rom, 0x18000)) return false;
    memcpy(A.mem, rom, 0xa000);
    memcpy(A.bank_rom[0], rom + 0xa000, 0x4000);
    memcpy(A.bank_rom[1], rom + 0xe000, 0x4000);
    for (int k = 0; k < 2; k++) {
        memset(rom, 0, 0xc000);
        if (!(k ? ARC_LOAD(kChar2, rom, 0xc000) : ARC_LOAD(kChar1, rom, 0xc000))) return false;
        const int t = 0x800;
        arc_layout cl = { 8, 8, t, 3, 64, false, false, false, {t * 128, t * 64, 0}, kX8, kY8 };
        arc_layout sl = { 16, 16, t, 3, 256, false, false, false, {t * 128, t * 64, 0}, kX16, kY16 };
        if (!(A.chars[k] = arc_gfx_decode(&cl, rom, 0xc000)) || !(A.sprites[k] = arc_gfx_decode(&sl, rom, 0xc000))) return false;
    }
    free(rom);
    if (!ARC_LOAD(kProm, prom, sizeof prom) || !ARC_LOAD(kAdpcm, A.adpcm, 0x10000)) return false;
    for (int f = 0; f < 0x200; f++) {
        int pen = prom[0x20 + f] & 0x0f;
        if (f > 0xff) pen |= 0x10;
        const uint8_t v = prom[pen];
        const int r = 0x21 * (v & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        A.palette[f] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    A.cpu.read = rd;
    A.cpu.write = wr;
    A.cpu.in = port_in;
    A.cpu.out = port_out;
    zd_init(&A.cpu);
    for (int i = 0; i < 3; i++) sn76496_init(&A.sn[i], CLOCK, RATE, 1.0f);
    msm5205d_init(&A.msm, MSM_CLOCK, 64, 4);
    A.msm.rom = A.adpcm;                // (dsp-cpp also hands it the ROM)
    A.msm.rom_size = 0x10000;
    A.msm.vclk_handler = adpcm_vclk;
    A.dsw = 0x60;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&A.cpu);
    for (int i = 0; i < 3; i++) sn76496_reset(&A.sn[i]);
    msm5205d_reset(&A.msm);
    A.in0 = A.in1 = A.in2 = 0;
    A.nmi_vblank = A.adpcm_playing = A.flip = false;
    A.adpcm_pos = 0;
    A.priority = 0xff;
    A.bank = 0;
    A.audio_acc = A.msm_acc = 0;
    memset(A.fb, 0, W * H * 2);
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

const uint16_t *arc_frame(void) { return A.fb; }

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
