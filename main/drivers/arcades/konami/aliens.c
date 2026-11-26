// aliens.c - Konami's Aliens (1990) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/aliens (after dsp-emulator's
// aliens_hw.pas). The 052001 CPU (cpu/konamidsp) with its banked ROM and
// palette RAM, the 052109 tilemaps and the 051960 sprites
// (video/k052109d, k051960d), a Z80 with a YM2151 and a 007232
// (sound/k007232d).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/konamidsp/konamidsp.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/k007232d.h"
#include "../../../video/k052109d.h"
#include "../../../video/k051960d.h"

#define W 288
#define H 224
#define FPS 59.185606
#define SCANLINES 256
#define MAIN_CLOCK (12000000 / 4)               // (the 052001 runs at a quarter of its input)
#define SOUND_CLOCK 3579545
#define RATE 44100

static const char *const kSets[] = { "aliens", NULL };
const arc_info arc_game = { "aliens", "Aliens", "Konami", 1990, W, H, FPS, RATE, kSets, { "DISPARO", "SALTO", NULL }, ARC_JOY_8WAY };

static const arc_rom kMain[] = {{"875_j01.c24", 0x20000, 0x00000, 0x6a529cd6}, {"875_j02.e24", 0x10000, 0x20000, 0x56c20971}};
static const arc_rom kSound[] = {{"875_b03.g04", 0x8000, 0, 0x1ac4d283}};
static const arc_rom kPcm[] = {{"875b04.e05", 0x40000, 0, 0x4e209ac8}};
static const arc_rom kTiles[] = {{"875b11.k13", 0x80000, 0x000000, 0x89c5c885}, {"875b12.k19", 0x80000, 0x000002, 0xea6bdc17},
                                 {"875b07.j13", 0x40000, 0x100000, 0xe9c56d66}, {"875b08.j19", 0x40000, 0x100002, 0xf9387966}};
static const arc_rom kSprites[] = {{"875b10.k08", 0x80000, 0x000000, 0x0b1035b1}, {"875b09.k02", 0x80000, 0x000002, 0xe76b3c19},
                                   {"875b06.j08", 0x40000, 0x100000, 0x081a0566}, {"875b05.j02", 0x40000, 0x100002, 0x19a261f2}};
static const uint8_t kLayerBase[3] = {0, 4, 8};

static struct state {
    konamid cpu;
    zd_z80 snd;
    ym2151 ym;
    k007232d pcm;
    k052109d tiles;
    k051960d sprites;
    uint8_t *main_rom, *pcm_rom, *tile_rom, *sprite_rom;
    uint8_t mem[0x10000], snd_mem[0x10000], ram_bank[2][0x400], pal_ram[0x400];
    uint16_t pal[0x200];
    uint16_t pens[W * H];
    uint8_t sound_latch, bank0, rom_bank1;
    bool rmrd;
    uint8_t in0, in1, dsw_a, dsw_b, dsw_c;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
    uint16_t fb[W * H];
} *A_;
#define A (*A_)

// ---------------------------------------------------------------------------
// The 052001

static uint8_t main_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x03ff) return A.ram_bank[A.bank0][a];
    if (a <= 0x1fff || a >= 0x8000) return A.mem[a];
    if (a <= 0x3fff) return A.main_rom[(A.rom_bank1 % 24) * 0x2000 + (a & 0x1fff)];
    switch (a) {
    case 0x5f80: return A.dsw_c;
    case 0x5f81: return A.in0;
    case 0x5f82: return A.in1;
    case 0x5f83: return A.dsw_b;
    case 0x5f84: return A.dsw_a;
    default: break;
    }
    const uint16_t o = a & 0x3fff;
    if (!A.rmrd) {
        if (o <= 0x37ff || (o >= 0x3808 && o <= 0x3bff)) return k052109d_read(&A.tiles, o);
        if (o >= 0x3800 && o <= 0x3807) return k051937d_read(&A.sprites, (uint8_t)(o - 0x3800));
        if (o >= 0x3c00) return k051960d_read(&A.sprites, o - 0x3c00);
        return 0;
    }
    return k052109d_read(&A.tiles, o);
}

static void main_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x03ff) {
        A.ram_bank[A.bank0][a] = v;
        if (A.bank0 == 1 && A.pal_ram[a] != v) {
            A.pal_ram[a] = v;
            const int i = a >> 1;
            const uint16_t w = (uint16_t)((A.pal_ram[i * 2] << 8) | A.pal_ram[i * 2 + 1]);
            const unsigned b = ((w >> 10) & 0x1f), g = ((w >> 5) & 0x1f), r = (w & 0x1f);
            const unsigned r8 = (r << 3) | (r >> 2), g8 = (g << 3) | (g >> 2), b8 = (b << 3) | (b >> 2);
            A.pal[i] = (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
        }
        return;
    }
    if (a <= 0x1fff) {
        A.mem[a] = v;
        return;
    }
    if (a <= 0x3fff || a >= 0x8000) return;
    if (a == 0x5f88) {
        A.bank0 = (v & 0x20) >> 5;
        A.rmrd = v & 0x40;
        A.tiles.rmrd = A.rmrd;
        return;
    }
    if (a == 0x5f8c) {
        A.sound_latch = v;
        zd_set_irq(&A.snd, ZD_HOLD, 0xff);
        return;
    }
    const uint16_t o = a & 0x3fff;
    if (o <= 0x37ff || (o >= 0x3808 && o <= 0x3bff)) k052109d_write(&A.tiles, o, v);
    else if (o >= 0x3800 && o <= 0x3807) k051937d_write(&A.sprites, (uint8_t)(o - 0x3800), v);
    else if (o >= 0x3c00) k051960d_write(&A.sprites, o - 0x3c00, v);
}

static void set_lines(void *u, uint8_t v) { (void)u; A.rom_bank1 = v & 0x1f; }
static void sprite_irq(void *u, bool on) { (void)u; konamid_set_irq(&A.cpu, on ? KC_HOLD : KC_CLEAR); }

static void tile_cb(void *u, int layer, int bank, uint32_t *code, uint16_t *color, uint16_t *flags, uint16_t *pri)
{
    (void)u, (void)flags, (void)pri;
    *code = *code | ((uint32_t)((*color & 0x3f) << 8) | ((uint32_t)bank << 14));
    *color = (uint16_t)(kLayerBase[layer % 3] + ((*color & 0xc0) >> 6));
}

static void sprite_cb(void *u, uint16_t *code, uint16_t *color, uint16_t *pri, uint16_t *shadow)
{
    (void)u;
    switch (*color & 0x70) {
    case 0x20: case 0x60: *pri = 7; break;
    case 0x00: *pri = 4; break;
    case 0x40: *pri = 6; break;
    case 0x10: *pri = 0; break;
    case 0x50: *pri = 2; break;
    case 0x30: case 0x70: *pri = 3; break;
    default: *pri = 0; break;
    }
    *code = (uint16_t)(*code | ((*color & 0x80) << 6));
    *color = (uint16_t)(16 + (*color & 0x0f));
    *shadow = 0;
}

// ---------------------------------------------------------------------------
// The sound board

static uint8_t snd_rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x87ff) return A.snd_mem[a];
    if (a == 0xa000 || a == 0xa001) return ym2151_status(&A.ym);
    if (a == 0xc000) return A.sound_latch;
    return 0;                                       // (the 007232 reads back 0)
}

static void snd_wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x8000 && a <= 0x87ff) A.snd_mem[a] = v;
    else if (a == 0xa000) ym2151_select(&A.ym, v);
    else if (a == 0xa001) ym2151_write(&A.ym, v);
    else if (a >= 0xe000 && a <= 0xe00d) k007232d_write(&A.pcm, a & 0x0f, v);
}

static uint8_t no_in(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void no_out(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }
static void snd_cycles(void *u, int c) { (void)u; if (c > 0) ym2151_run_timers(&A.ym, c); }
static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&A.snd, on ? ZD_HOLD : ZD_CLEAR, 0xff); }
static void ym_port(void *u, uint8_t v) { (void)u; k007232d_set_bank(&A.pcm, (v >> 1) & 1, v & 1); }

static void pcm_volume(void *u, uint8_t v)
{
    (void)u;
    k007232d_set_volume(&A.pcm, 0, (uint8_t)((v & 0x0f) * 0x11), 0);
    k007232d_set_volume(&A.pcm, 1, 0, (uint8_t)((v >> 4) * 0x11));
}

// ---------------------------------------------------------------------------

// ROM_LOAD32_WORD: byte pairs every 4 bytes, from the entry's offset
static bool load32b(const arc_rom *roms, int n, uint8_t *dest, uint32_t size)
{
    for (int i = 0; i < n; i++) {
        arc_rom r = roms[i];
        r.offset = 0;
        uint8_t *part = (uint8_t *)calloc(1, r.size);
        if (!part || !arc_load(&r, 1, part, r.size)) {
            free(part);
            return false;
        }
        uint32_t d = roms[i].offset;
        for (uint32_t h = 0; h < r.size / 2; h++, d += 4) {
            if (d < size) dest[d] = part[h * 2];
            if (d + 1 < size) dest[d + 1] = part[h * 2 + 1];
        }
        free(part);
    }
    return true;
}

static void update_video(void)
{
    k052109d_draw_tiles(&A.tiles);
    k051960d_update_sprites(&A.sprites);
    for (int i = 0; i < W * H; i++) A.pens[i] = kLayerBase[1] * 16;
    const int cx = 112, cy = 16;
    k051960d_draw(&A.sprites, 7, A.pens, W, H, cx, cy);
    k052109d_draw_layer(&A.tiles, 1, A.pens, W, H, cx, cy);
    k051960d_draw(&A.sprites, 6, A.pens, W, H, cx, cy);
    k052109d_draw_layer(&A.tiles, 2, A.pens, W, H, cx, cy);
    k051960d_draw(&A.sprites, 4, A.pens, W, H, cx, cy);
    k052109d_draw_layer(&A.tiles, 0, A.pens, W, H, cx, cy);
    k051960d_draw(&A.sprites, 0, A.pens, W, H, cx, cy);
    for (int i = 0; i < W * H; i++) A.fb[i] = A.pal[A.pens[i] & 0x1ff];
}

bool arc_init(void)
{
    if (!(A_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    A.main_rom = (uint8_t *)calloc(1, 0x30000);
    A.pcm_rom = (uint8_t *)calloc(1, 0x40000);
    A.tile_rom = (uint8_t *)calloc(1, 0x200000);
    A.sprite_rom = (uint8_t *)calloc(1, 0x200000);
    if (!A.main_rom || !A.pcm_rom || !A.tile_rom || !A.sprite_rom) return false;
    if (!ARC_LOAD(kMain, A.main_rom, 0x30000) || !ARC_LOAD(kSound, A.snd_mem, 0x8000) || !ARC_LOAD(kPcm, A.pcm_rom, 0x40000) ||
        !load32b(kTiles, 4, A.tile_rom, 0x200000) || !load32b(kSprites, 4, A.sprite_rom, 0x200000))
        return false;
    memcpy(A.mem + 0x8000, A.main_rom + 0x28000, 0x8000);
    k007232d_init(&A.pcm, A.pcm_rom, 0x40000, 0.20f, false);
    A.pcm.volume_cb = pcm_volume;
    if (!k052109d_init(&A.tiles, tile_cb, NULL, A.tile_rom, 0x200000)) return false;
    k051960d_init(&A.sprites, sprite_cb, NULL, A.sprite_rom, 0x200000);
    A.sprites.irq = sprite_irq;
    A.dsw_a = 0xff, A.dsw_b = 0x5e, A.dsw_c = 0xff;
    A.cpu.rd = main_rd;
    A.cpu.wr = main_wr;
    A.cpu.set_lines = set_lines;
    A.snd.read = snd_rd;
    A.snd.write = snd_wr;
    A.snd.in = no_in;
    A.snd.out = no_out;
    A.snd.cycles = snd_cycles;
    zd_init(&A.snd);
    ym2151_init(&A.ym, SOUND_CLOCK, RATE);
    A.ym.irq_handler = ym_irq;
    A.ym.port_handler = ym_port;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    konamid_reset(&A.cpu);
    zd_reset(&A.snd);
    ym2151_reset(&A.ym);
    k007232d_reset(&A.pcm);
    k052109d_reset(&A.tiles);
    k051960d_reset(&A.sprites);
    A.sound_latch = A.bank0 = A.rom_bank1 = 0;
    A.rmrd = false;
    A.in0 = A.in1 = 0xff;
    A.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / FPS / SCANLINES + 0.5), sc = (int)((double)SOUND_CLOCK / FPS / SCANLINES + 0.5);
    A.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        k051960d_update_line(&A.sprites, line);
        if (line == 240) update_video();
        konamid_run(&A.cpu, mc);
        zd_run(&A.snd, sc);
        A.audio_acc += (int64_t)sc * RATE;
        while (A.audio_acc >= SOUND_CLOCK) {
            A.audio_acc -= SOUND_CLOCK;
            int32_t s = ym2151_update(&A.ym) + k007232d_update(&A.pcm);
            s *= 24;
            if (A.naudio < 1024) A.audio[A.naudio++] = arc_clamp16(s);
        }
    }
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *arc_frame(void) { return A.fb; }

void arc_input(const arc_in *in)
{
    static const unsigned bits[6] = {AP_LEFT, AP_RIGHT, AP_UP, AP_DOWN, AP_B1, AP_B2};
    A.in0 = A.in1 = 0xff;
    for (int b = 0; b < 6; b++) {
        if (in->p[0] & bits[b]) A.in0 &= (uint8_t)~(1 << b);
        if (in->p[1] & bits[b]) A.in1 &= (uint8_t)~(1 << b);
    }
    if (in->coin1) A.in0 &= 0xbf;
    if (in->p[0] & AP_START) A.in0 &= 0x7f;
    if (in->coin2) A.in1 &= 0xbf;
    if (in->p[1] & AP_START) A.in1 &= 0x7f;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) A.dsw_a = v;
    if (bank == 1) A.dsw_b = v;
    if (bank == 2) A.dsw_c = v;
}
