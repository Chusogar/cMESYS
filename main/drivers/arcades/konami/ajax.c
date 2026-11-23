// ajax.c - Konami's Ajax (1987) for the common arcade frontends (arc.h): C
// port of dsp-cpp's drivers/arcade/ajax (after dsp-emulator's ajax_hw.pas
// and MAME 0.260's video). The 052001 main CPU, a 6809 sub CPU (the
// HD6309E's 6809 subset) with the 051316 ROZ layer, the 052109 tilemaps and
// the 051960 sprites merged with MAME's priority masks, a Z80 with a YM2151
// and two 007232. The picture is turned to the monitor (224x304).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/konamidsp/konamidsp.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/k007232d.h"
#include "../../../video/k052109d.h"
#include "../../../video/k051960d.h"
#include "../../../video/k051316d.h"

#define NW 304
#define NH 224
#define W NH
#define H NW
#define FPS 59.185606
#define SCANLINES 256
#define MAIN_CLOCK (12000000 / 4)
#define SUB_CLOCK 3000000
#define SOUND_CLOCK 3579545
#define RATE 44100

static const char *const kSets[] = { "ajax", "typhoon", "ajaxj", NULL };
const arc_info arc_game = { "ajax", "Ajax", "Konami", 1987, W, H, FPS, RATE, kSets, { "DISPARO", "MISIL", "SUPER", NULL }, ARC_JOY_8WAY };

static const arc_rom kMain[] = {{"770_m01.n11", 0x10000, 0x00000, 0x4a64e53a}, {"770_l02.n12", 0x10000, 0x10000, 0xad7d592b}};
static const arc_rom kSub[] = {{"770_l05.i16", 0x08000, 0x00000, 0xed64fbb2}, {"770_f04.g16", 0x10000, 0x08000, 0xe0e4ec9c}};
static const arc_rom kSound[] = {{"770_h03.f16", 0x8000, 0, 0x2ffd2afc}};
static const arc_rom kTiles[] = {
    {"770c13-a.f3", 0x10000, 0x00000, 0x4ef6fff2}, {"770c13-c.f4", 0x10000, 0x00001, 0x97ffbab6}, {"770c12-a.f5", 0x10000, 0x00002, 0x6c0ade68},
    {"770c12-c.f6", 0x10000, 0x00003, 0x61fc39cc}, {"770c13-b.e3", 0x10000, 0x40000, 0x86fdd706}, {"770c13-d.e4", 0x10000, 0x40001, 0x7d7acb2d},
    {"770c12-b.e5", 0x10000, 0x40002, 0x5f221cc6}, {"770c12-d.e6", 0x10000, 0x40003, 0xf1edb2f4}};
static const arc_rom kSprites[] = {
    {"770c09-a.f8", 0x10000, 0x00000, 0x76690fb8}, {"770c09-e.f9", 0x10000, 0x00001, 0x17b482c9}, {"770c08-a.f10", 0x10000, 0x00002, 0xefd29a56},
    {"770c08-e.f11", 0x10000, 0x00003, 0x6d43afde}, {"770c09-b.e8", 0x10000, 0x40000, 0xcd1709d1}, {"770c09-f.e9", 0x10000, 0x40001, 0xcba4b47e},
    {"770c08-b.e10", 0x10000, 0x40002, 0xf3374014}, {"770c08-f.e11", 0x10000, 0x40003, 0xf5ba59aa}, {"770c09-c.d8", 0x10000, 0x80000, 0xbfd080b8},
    {"770c09-g.d9", 0x10000, 0x80001, 0x77d58ea0}, {"770c08-c.d10", 0x10000, 0x80002, 0x28e7088f}, {"770c08-g.d11", 0x10000, 0x80003, 0x17da8f6d},
    {"770c09-d.c8", 0x10000, 0xc0000, 0x6f955600}, {"770c09-h.c9", 0x10000, 0xc0001, 0x494a9090}, {"770c08-d.c10", 0x10000, 0xc0002, 0x91591777},
    {"770c08-h.c11", 0x10000, 0xc0003, 0xd97d4b15}};
static const arc_rom kZoom[] = {{"770c06.f4", 0x40000, 0x00000, 0xd0c592ee}, {"770c07.h4", 0x40000, 0x40000, 0x0b399fb1}};
static const arc_rom kPcm1[] = {{"770c10-a.a7", 0x10000, 0x00000, 0xe45ec094}, {"770c10-b.a6", 0x10000, 0x10000, 0x349db7d3},
                                {"770c10-c.a5", 0x10000, 0x20000, 0x71cb1f05}, {"770c10-d.a4", 0x10000, 0x30000, 0xe8ab1844}};
static const arc_rom kPcm2[] = {{"770c11-a.c6", 0x10000, 0x00000, 0x8cccd9e0}, {"770c11-b.c5", 0x10000, 0x10000, 0x0af2fedd},
                                {"770c11-c.c4", 0x10000, 0x20000, 0x7471f24a}, {"770c11-d.c3", 0x10000, 0x30000, 0xa58be323},
                                {"770c11-e.b7", 0x10000, 0x40000, 0xdd553541}, {"770c11-f.b6", 0x10000, 0x50000, 0x3f78bd0f},
                                {"770c11-g.b5", 0x10000, 0x60000, 0x078c51b2}, {"770c11-h.b4", 0x10000, 0x70000, 0x7300c2e1}};

static struct state {
    konamid cpu;
    m6809d sub;
    zd_z80 snd;
    ym2151 ym;
    k007232d pcm0, pcm1;
    k052109d tiles;
    k051960d sprites;
    k051316d zoom;
    uint8_t *main_rom, *sub_rom, *pcm0_rom, *pcm1_rom, *tile_rom, *sprite_rom, *zoom_rom;
    uint8_t mem[0x10000], misc[0x10000], snd_mem[0x10000], pal_ram[0x1000];
    uint16_t pal[0x800];
    uint16_t pens[NW * NH], layer[NW * NH];
    uint8_t pri[NW * NH];
    uint8_t sound_latch, rom_bank1, rom_bank2, gun_rand;
    bool sub_firq_enable, priority;
    int watchdog;
    uint8_t in0, in1, in2, dsw_a, dsw_b, dsw_c;
    double frame_main, frame_sub, frame_snd;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *J_;
#define J (*J_)

static const uint8_t *main_bank(int b) { return J.main_rom + (b < 4 ? b * 0x2000 : 0x10000 + (b - 4) * 0x2000); }
static const uint8_t *sub_bank(int b) { return b == 8 ? J.sub_rom : J.sub_rom + 0x8000 + b * 0x2000; }

// ---------------------------------------------------------------------------
// The 052001

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x01c0) {
        switch ((a & 0x1c0) >> 6) {
        case 0: return (uint8_t)(J.gun_rand++ * 17 + 0x5a);
        case 4: return J.in1;
        case 6:
            switch (a & 3) {
            case 0: return J.in2;
            case 1: return J.in0;
            case 2: return J.dsw_a;
            default: return J.dsw_b;
            }
        case 7: return J.dsw_c;
        default: return 0xff;
        }
    }
    if (a >= 0x0800 && a <= 0x0807) return k051937d_read(&J.sprites, a & 7);
    if (a >= 0x0c00 && a <= 0x0fff) return k051960d_read(&J.sprites, a - 0x0c00);
    if (a >= 0x1000 && a <= 0x1fff) return J.pal_ram[a & 0xfff];
    if (a >= 0x6000 && a <= 0x7fff) return main_bank(J.rom_bank1 & 0x0f)[a & 0x1fff];
    return J.mem[a];
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x01c0) {
        switch ((a & 0x1c0) >> 6) {
        case 0:
            if (a == 0) {
                if (J.sub_firq_enable) m6809d_set_firq(&J.sub, M6809D_HOLD);
            } else J.watchdog = 0;
            break;
        case 1: zd_set_irq(&J.snd, ZD_HOLD, 0xff); break;
        case 2: J.sound_latch = v; break;
        case 3:
            J.rom_bank1 = (uint8_t)((((~v) & 0x80) >> 5) + (v & 7));
            J.priority = v & 0x08;
            break;
        default: break;
        }
        return;
    }
    if (a >= 0x0800 && a <= 0x0807) k051937d_write(&J.sprites, a & 7, v);
    else if (a >= 0x0c00 && a <= 0x0fff) k051960d_write(&J.sprites, a - 0x0c00, v);
    else if (a >= 0x1000 && a <= 0x1fff) {
        if (J.pal_ram[a & 0xfff] != v) {
            J.pal_ram[a & 0xfff] = v;
            const int i = (a & 0xfff) >> 1;
            const uint16_t w = (uint16_t)((J.pal_ram[i * 2] << 8) | J.pal_ram[i * 2 + 1]);
            const unsigned b = (w >> 10) & 0x1f, g = (w >> 5) & 0x1f, r = w & 0x1f;
            const unsigned r8 = (r << 3) | (r >> 2), g8 = (g << 3) | (g >> 2), b8 = (b << 3) | (b >> 2);
            J.pal[i] = (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
        }
    } else if (a >= 0x2000 && a <= 0x5fff) J.mem[a] = v;
}

// ---------------------------------------------------------------------------
// The 6809

static uint8_t sub_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x07ff) return k051316d_read(&J.zoom, a);
    if (a >= 0x1000 && a <= 0x17ff) return k051316d_rom_read(&J.zoom, a & 0x7ff);
    if (a >= 0x2000 && a <= 0x3fff) return J.mem[a];
    if (a >= 0x4000 && a <= 0x7fff) return k052109d_read(&J.tiles, a & 0x3fff);
    if (a >= 0x8000 && a <= 0x9fff) return sub_bank(J.rom_bank2 % 9)[a & 0x1fff];
    return J.misc[a];
}

static void sub_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x07ff) k051316d_write(&J.zoom, a, v);
    else if (a >= 0x0800 && a <= 0x080f) k051316d_control_w(&J.zoom, a & 0x0f, v);
    else if (a == 0x1800) {                         // RMRD, the ROZ wraparound, FIRQ enable, the bank
        J.tiles.rmrd = v & 0x40;
        J.zoom.wrap = v & 0x20;
        J.sub_firq_enable = v & 0x10;
        J.rom_bank2 = v & 0x0f;
    } else if (a >= 0x2000 && a <= 0x3fff) J.mem[a] = v;
    else if (a >= 0x4000 && a <= 0x7fff) k052109d_write(&J.tiles, a & 0x3fff, v);
}

// ---------------------------------------------------------------------------
// The sound board

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x87ff) return J.snd_mem[a];
    if (a == 0xc001) return ym2151_status(&J.ym);
    if (a == 0xe000) return J.sound_latch;
    return 0;
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x8000 && a <= 0x87ff) J.snd_mem[a] = v;
    else if (a == 0x9000) {
        k007232d_set_bank(&J.pcm0, (v >> 1) & 1, v & 1);
        k007232d_set_bank(&J.pcm1, (v >> 4) & 3, (v >> 2) & 3);
    } else if (a >= 0xa000 && a <= 0xa00d) k007232d_write(&J.pcm0, a & 0x0f, v);
    else if (a >= 0xb000 && a <= 0xb00d) k007232d_write(&J.pcm1, a & 0x0f, v);
    else if (a == 0xb80c) {
        const uint8_t vv = (uint8_t)((v & 0x0f) * (0x11 >> 1));
        k007232d_set_volume(&J.pcm1, 0, vv, vv);
    } else if (a == 0xc000) ym2151_select(&J.ym, v);
    else if (a == 0xc001) ym2151_write(&J.ym, v);
}

static void snd_cycles(void *u, int c)
{
    (void)u;
    if (c > 0) ym2151_run_timers(&J.ym, c);
    J.audio_acc += (int64_t)c * RATE;
    while (J.audio_acc >= SOUND_CLOCK) {
        J.audio_acc -= SOUND_CLOCK;
        int32_t s = ym2151_update(&J.ym) + k007232d_update(&J.pcm0) + k007232d_update(&J.pcm1);
        s *= 24;
        if (J.naudio < 1024) J.audio[J.naudio++] = arc_clamp16(s);
    }
}

static uint8_t no_in(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void no_out(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&J.snd, on ? ZD_HOLD : ZD_CLEAR, 0xff); }
static void pcm0_vol(void *u, uint8_t v)
{
    (void)u;
    k007232d_set_volume(&J.pcm0, 0, (uint8_t)((v >> 4) * 0x11), 0);
    k007232d_set_volume(&J.pcm0, 1, 0, (uint8_t)((v & 0x0f) * 0x11));
}
static void pcm1_vol(void *u, uint8_t v) { (void)u; k007232d_set_volume(&J.pcm1, 1, (uint8_t)((v & 0x0f) * (0x11 >> 1)), (uint8_t)((v >> 4) * (0x11 >> 1))); }

static void tile_cb(void *u, int layer, int bank, uint32_t *code, uint16_t *color, uint16_t *flags, uint16_t *pri)
{
    static const int base[3] = {1024 / 16, 0, 512 / 16};
    (void)u, (void)flags, (void)pri;
    *code = *code | ((uint32_t)((*color & 0x0f) << 8) | ((uint32_t)bank << 12));
    *color = (uint16_t)(base[layer] + ((*color & 0xf0) >> 4));
}

static void sprite_cb(void *u, uint16_t *code, uint16_t *color, uint16_t *pri, uint16_t *shadow)
{
    (void)u, (void)code, (void)shadow;
    *pri = 0;                                       // MAME's GFX_PMASK bits
    if (*color & 0x10) *pri |= 0x04;
    if (!(*color & 0x40)) *pri |= 0x02;
    if (*color & 0x20) *pri |= 0x01;
    *color = (uint16_t)(16 + (*color & 0x0f));
}

static void zoom_cb(void *u, uint16_t *code, uint16_t *color, uint16_t *pri)
{
    (void)u, (void)pri;
    *code = (uint16_t)(*code | ((*color & 7) << 8));
    *color = (uint16_t)(6 + ((*color & 8) >> 3));
}

static void sprite_irq(void *u, bool on) { (void)u; konamid_set_irq(&J.cpu, on ? KC_HOLD : KC_CLEAR); }
static void sprite_nmi(void *u, bool on) { (void)u; konamid_set_nmi(&J.cpu, on ? KC_HOLD : KC_CLEAR); }

// ---------------------------------------------------------------------------
// Video

static void composite(uint8_t p)
{
    for (int i = 0; i < NW * NH; i++)
        if (J.layer[i]) {
            J.pens[i] = J.layer[i];
            J.pri[i] = p;
        }
}

static void update_video(void)
{
    k052109d_draw_tiles(&J.tiles);
    k051960d_update_sprites(&J.sprites);
    const int cx = 108, cy = 16;                    // (MAME 0.260's visible area)
    memset(J.pens, 0, sizeof J.pens);
    memset(J.pri, 0, sizeof J.pri);
    memset(J.layer, 0, sizeof J.layer);
    k052109d_draw_layer(&J.tiles, 2, J.layer, NW, NH, cx, cy);
    composite(0x01);
    for (int k = 0; k < 2; k++) {
        memset(J.layer, 0, sizeof J.layer);
        if ((k == 0) == J.priority) {
            k051316d_draw(&J.zoom, J.layer, NW, NH, cx, cy);
            composite(0x04);
        } else {
            k052109d_draw_layer(&J.tiles, 1, J.layer, NW, NH, cx, cy);
            composite(0x02);
        }
    }
    k051960d_draw_masked(&J.sprites, J.pens, J.pri, NW, NH, cx, cy);
    memset(J.layer, 0, sizeof J.layer);
    k052109d_draw_layer(&J.tiles, 0, J.layer, NW, NH, cx, cy);
    for (int i = 0; i < NW * NH; i++)
        if (J.layer[i]) J.pens[i] = J.layer[i];
    for (int y = 0; y < NH; y++)                    // turned 90 degrees clockwise
        for (int x = 0; x < NW; x++) J.fb[x * W + (NH - 1 - y)] = J.pal[J.pens[y * NW + x] & 0x7ff];
}

// ROM_LOAD32_BYTE: each ROM a byte every 4, from its offset
static bool load32b_b(const arc_rom *roms, int n, uint8_t *dest, uint32_t size)
{
    uint8_t *part = (uint8_t *)malloc(0x10000);
    if (!part) return false;
    for (int i = 0; i < n; i++) {
        arc_rom r = roms[i];
        r.offset = 0;
        if (!arc_load(&r, 1, part, 0x10000)) {
            free(part);
            return false;
        }
        for (uint32_t b = 0; b < r.size; b++) {
            const uint32_t d = roms[i].offset + b * 4;
            if (d < size) dest[d] = part[b];
        }
    }
    free(part);
    return true;
}

bool arc_init(void)
{
    if (!(J_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    J.main_rom = (uint8_t *)calloc(1, 0x20000);
    J.sub_rom = (uint8_t *)calloc(1, 0x18000);
    J.pcm0_rom = (uint8_t *)calloc(1, 0x40000);
    J.pcm1_rom = (uint8_t *)calloc(1, 0x80000);
    J.tile_rom = (uint8_t *)calloc(1, 0x80000);
    J.sprite_rom = (uint8_t *)calloc(1, 0x100000);
    J.zoom_rom = (uint8_t *)calloc(1, 0x80000);
    if (!J.main_rom || !J.sub_rom || !J.pcm0_rom || !J.pcm1_rom || !J.tile_rom || !J.sprite_rom || !J.zoom_rom) return false;
    if (!ARC_LOAD(kMain, J.main_rom, 0x20000) || !ARC_LOAD(kSub, J.sub_rom, 0x18000) || !ARC_LOAD(kSound, J.snd_mem, 0x8000) ||
        !ARC_LOAD(kPcm1, J.pcm0_rom, 0x40000) || !ARC_LOAD(kPcm2, J.pcm1_rom, 0x80000) || !ARC_LOAD(kZoom, J.zoom_rom, 0x80000) ||
        !load32b_b(kTiles, 8, J.tile_rom, 0x80000) || !load32b_b(kSprites, 16, J.sprite_rom, 0x100000))
        return false;
    memcpy(J.mem + 0x8000, J.main_rom + 0x8000, 0x8000);
    memcpy(J.misc + 0xa000, J.sub_rom + 0x2000, 0x6000);
    k007232d_init(&J.pcm0, J.pcm0_rom, 0x40000, 0.20f, false);
    J.pcm0.volume_cb = pcm0_vol;
    k007232d_init(&J.pcm1, J.pcm1_rom, 0x80000, 0.50f, true);
    J.pcm1.volume_cb = pcm1_vol;
    if (!k052109d_init(&J.tiles, tile_cb, NULL, J.tile_rom, 0x80000)) return false;
    k051960d_init(&J.sprites, sprite_cb, NULL, J.sprite_rom, 0x100000);
    J.sprites.irq = sprite_irq;
    J.sprites.nmi = sprite_nmi;
    if (!k051316d_init(&J.zoom, zoom_cb, NULL, J.zoom_rom, 0x80000, true)) return false;
    J.dsw_a = 0xff, J.dsw_b = 0x5a, J.dsw_c = 0xff;
    J.cpu.rd = main_rd;
    J.cpu.wr = main_wr;
    J.sub.read = sub_rd;
    J.sub.write = sub_wr;
    J.snd.read = snd_rd;
    J.snd.write = snd_wr;
    J.snd.in = no_in;
    J.snd.out = no_out;
    J.snd.cycles = snd_cycles;
    zd_init(&J.snd);
    ym2151_init(&J.ym, SOUND_CLOCK, RATE);
    J.ym.irq_handler = ym_irq;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    konamid_reset(&J.cpu);
    m6809d_reset(&J.sub);
    zd_reset(&J.snd);
    ym2151_reset(&J.ym);
    k007232d_reset(&J.pcm0);
    k007232d_reset(&J.pcm1);
    k052109d_reset(&J.tiles);
    k051960d_reset(&J.sprites);
    k051316d_reset(&J.zoom);
    J.sound_latch = J.rom_bank1 = J.rom_bank2 = J.gun_rand = 0;
    J.sub_firq_enable = J.priority = false;
    J.watchdog = 0;
    J.in0 = J.in1 = J.in2 = 0xff;
    J.frame_main = (double)MAIN_CLOCK / FPS / SCANLINES;
    J.frame_sub = (double)SUB_CLOCK / FPS / SCANLINES;
    J.frame_snd = (double)SOUND_CLOCK / FPS / SCANLINES;
    J.audio_acc = 0;
}

static int budget(double residual, double t)
{
    const int b = (int)residual;
    return b > 0 ? b : ((int)t > 0 ? (int)t : 1);
}

int arc_run_frame(const int16_t **audio)
{
    const double mt = (double)MAIN_CLOCK / FPS / SCANLINES, st = (double)SUB_CLOCK / FPS / SCANLINES, at = (double)SOUND_CLOCK / FPS / SCANLINES;
    J.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) {
            if (J.tiles.irq_enabled) m6809d_set_irq(&J.sub, M6809D_HOLD);
            update_video();
        }
        J.frame_main = J.frame_main + mt - (double)konamid_run(&J.cpu, budget(J.frame_main, mt));
        J.frame_sub = J.frame_sub + st - (double)m6809d_run(&J.sub, budget(J.frame_sub, st));
        J.frame_snd = J.frame_snd + at - (double)zd_run(&J.snd, budget(J.frame_snd, at));
        k051960d_update_line(&J.sprites, line);
    }
    // the empty object-list walker at $6025 spins forever: dsp-cpp returns from it (an RTS) after 30 frames
    if (J.cpu.pc == 0x6025) {
        if (++J.watchdog > 30) {
            J.watchdog = 0;
            const uint16_t s = J.cpu.s;
            const uint16_t ret = (uint16_t)((main_rd(NULL, s) << 8) | main_rd(NULL, (uint16_t)(s + 1)));
            J.cpu.s = (uint16_t)(s + 2);
            J.cpu.pc = ret;
        }
    } else J.watchdog = 0;
    if (audio) *audio = J.audio;
    return J.naudio;
}

const uint16_t *arc_frame(void) { return J.fb; }

void arc_input(const arc_in *in)
{
    static const unsigned bits[7] = {AP_LEFT, AP_RIGHT, AP_UP, AP_DOWN, AP_B1, AP_B2, AP_B3};
    J.in0 = J.in1 = J.in2 = 0xff;
    for (int b = 0; b < 7; b++) {
        if (in->p[0] & bits[b]) J.in0 &= (uint8_t)~(1 << b);
        if (in->p[1] & bits[b]) J.in1 &= (uint8_t)~(1 << b);
    }
    if (in->coin1) J.in2 &= 0xfe;
    if (in->coin2) J.in2 &= 0xfd;
    if (in->p[0] & AP_START) J.in2 &= 0xf7;
    if (in->p[1] & AP_START) J.in2 &= 0xef;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) J.dsw_a = v;
    if (bank == 1) J.dsw_b = v;
    if (bank == 2) J.dsw_c = v;
}
