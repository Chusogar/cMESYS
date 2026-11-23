// wboy.c - Wonder Boy (Sega / Westone, 1986) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/sega_system1 (from
// dsp-emulator's system1_hw.pas), the Wonder Boy machine of Sega System 1. A
// Z80 at 4 MHz with the 315-5177 opcode encryption, a sound Z80 with two
// SN76496 fed through a Z80 PIO, two 8x8 tile layers and 32 sprites drawn
// from nibble-packed ROMs, mixed by the priority PROM.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../machine/sega_decrypt.h"
#include "../../../machine/z80piodsp.h"
#include "../../../sound/sn76496.h"

#define MAIN_CLOCK 4000000
#define SOUND_CLOCK 4000000
#define FPS 60.096154
#define SCANLINES 260
#define IRQ_LINE 224
#define RATE 44100
#define W 256
#define H 224

static const char *const kSets[] = { "wboy", NULL };
static const arc_rom kMain[] = {
    {"epr-7489.116", 0x4000, 0x0000, 0x130f4b70}, {"epr-7490.109", 0x4000, 0x4000, 0x9e656733},
    {"epr-7491.96", 0x4000, 0x8000, 0x1f7d0efe}};
static const arc_rom kChars[] = {
    {"epr-7497.62", 0x2000, 0x0000, 0x08d609ca}, {"epr-7496.61", 0x2000, 0x2000, 0x6f61fdf1},
    {"epr-7495.64", 0x2000, 0x4000, 0x6a0d2c2d}, {"epr-7494.63", 0x2000, 0x6000, 0xa8e281c7},
    {"epr-7493.66", 0x2000, 0x8000, 0x89305df4}, {"epr-7492.65", 0x2000, 0xa000, 0x60f806b1}};
static const arc_rom kSprites[] = {
    {"epr-7485.117", 0x4000, 0x0000, 0xc2891722}, {"epr-7487.04", 0x4000, 0x4000, 0x2d3a421b},
    {"epr-7486.110", 0x4000, 0x8000, 0x8d622c50}, {"epr-7488.05", 0x4000, 0xc000, 0x007c2f1b}};
static const arc_rom kSound[] = {{"epr-7498.120", 0x2000, 0, 0x78ae1e7b}};
static const arc_rom kVideoProm[] = {{"pr-5317.76", 0x100, 0, 0x648350b8}};

const arc_info arc_game = { "wboy", "Wonder Boy", "Sega / Westone", 1986, W, H, FPS, RATE, kSets,
                            { "JUMP", "ATTACK", NULL, NULL }, ARC_JOY_8WAY };

static const int kX8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
#define SPRITE_BANKS 2

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sound;
    sn76496 psg0, psg1;
    z80piod pio;
    const uint8_t *rom, *opcodes, *sound_rom;   // 0000-bfff decrypted, 0000-7fff (blobs: the CYD's in the flash)
    uint8_t ram[0x2000], bg_ram[0x1000], pal_ram[0x800], mix_collide[0x40], sprite_collide[0x400];   // ram: c000-dfff
    bool mix_collide_summary, sprite_collide_summary;
    uint8_t sound_ram[0x800];
    const uint8_t *chars, *sprite_rom;
    uint8_t mix_lookup[0x100];
    double rw[8], gw[8], bw[8];
    uint16_t palette[0x600];
    // the 256x256 sprite layer: colour | sprite * 16, in two halves of rows
    // (bits 0-7) and a bit array (bit 8)
    uint8_t *spr_lo[2], spr_hi[256 * 256 / 8];
    arc_fb fb;
    int bg_xscroll;
    uint8_t bg_yscroll, sound_latch, videomode, in0, in1, in2, dsw_a, dsw_b;
    int sound_irq_accum;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *S_;
#define S (*S_)

static void set_color(int index, uint8_t v)
{
    const int r = arc_combine(S.rw, 3, v & 7), g = arc_combine(S.gw, 3, (v >> 3) & 7), b = arc_combine(S.bw, 2, v >> 6);
    S.palette[index] = arc_rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
}

static uint8_t read_data(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xbfff) return S.rom[a];
    if (a <= 0xd7ff) return S.ram[a - 0xc000];
    if (a <= 0xddff) return S.pal_ram[a & 0x7ff];
    if (a <= 0xdfff) return S.ram[a - 0xc000];
    if (a <= 0xefff) return S.bg_ram[a & 0xfff];
    if (a <= 0xf3ff) return (uint8_t)(S.mix_collide[a & 0x3f] | 0x7e | (S.mix_collide_summary ? 0x80 : 0));
    if (a <= 0xf7ff) return 0xff;
    if (a <= 0xfbff) return (uint8_t)(S.sprite_collide[a & 0x3ff] | 0x7e | (S.sprite_collide_summary ? 0x80 : 0));
    return 0xff;
}

static uint8_t read_opcode(void *u, uint16_t a) { return a <= 0x7fff ? S.opcodes[a] : read_data(u, a); }

static void write_byte(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a <= 0xd7ff || (a >= 0xde00 && a <= 0xdfff)) S.ram[a - 0xc000] = v;
    else if (a <= 0xddff) {
        const int pos = a & 0x7ff;
        if (S.pal_ram[pos] != v) {
            S.pal_ram[pos] = v;
            set_color(pos, v);
        }
    } else if (a <= 0xefff) S.bg_ram[a & 0xfff] = v;
    else if (a <= 0xf3ff) S.mix_collide[a & 0x3f] = 0;
    else if (a <= 0xf7ff) S.mix_collide_summary = false;
    else if (a <= 0xfbff) S.sprite_collide[a & 0x3ff] = 0;
    else S.sprite_collide_summary = false;
}

static uint8_t read_port(void *u, uint16_t port)
{
    (void)u;
    const int p = port & 0x1f;
    if (p <= 0x3) return S.in1;
    if (p <= 0x7) return S.in2;
    if (p <= 0xb) return S.in0;
    if (p == 0xc || p == 0xe) return S.dsw_a;
    if (p == 0xd || p == 0xf || (p >= 0x10 && p <= 0x13)) return S.dsw_b;
    if (p >= 0x18 && p <= 0x1b) return z80piod_read(&S.pio, (uint16_t)p);
    return 0xff;
}

static void write_port(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    const int p = port & 0x1f;
    if (p >= 0x18 && p <= 0x1b) z80piod_write(&S.pio, (uint16_t)p, v);
}

static uint8_t snd_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x7fff) return S.sound_rom[a];
    if (a <= 0x9fff) return S.sound_ram[a & 0x7ff];
    if (a < 0xe000) return 0xff;
    const uint8_t v = z80piod_port_output(&S.pio, Z80PIO_A);
    z80piod_strobe(&S.pio, Z80PIO_A, false);
    z80piod_strobe(&S.pio, Z80PIO_A, true);
    return v;
}

static void snd_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0x9fff) S.sound_ram[a & 0x7ff] = v;
    else if (a <= 0xbfff) sn76496_write(&S.psg0, v);
    else if (a <= 0xdfff) sn76496_write(&S.psg1, v);
}

static void sound_cycles(void *u, int c)
{
    (void)u;
    S.audio_acc += (int64_t)c * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = sn76496_update(&S.psg0) + sn76496_update(&S.psg1);
        if (S.naudio < 1024) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

// the PIO: port A the sound latch (its ARDY rising edge the sound NMI),
// port B the video mode
static void pio_a_out(void *u, uint8_t v) { (void)u; S.sound_latch = v; }
static void pio_b_out(void *u, uint8_t v) { (void)u; S.videomode = v; }
static void pio_ready_a(void *u, bool state) { (void)u; if (state) zd_set_nmi(&S.sound, ZD_PULSE); }

#define MEM(a) S.ram[(a) - 0xc000]

// a pixel of a tile layer (0 at the back, 1 in front): pen | colour << 3
static inline uint16_t tile_px(int layer, int x, int y)
{
    const int offset = (y >> 3) * 32 + (x >> 3), source = (layer << 11) + offset * 2;
    const uint16_t attr = (uint16_t)(S.bg_ram[source] | (S.bg_ram[source + 1] << 8));
    return (uint16_t)(S.chars[(attr & 0x7ff) * 64 + (y & 7) * 8 + (x & 7)] | (((attr >> 5) & 0xff) << 3));
}

static inline uint16_t spr_get(int i)
{
    return (uint16_t)(S.spr_lo[i >> 15][i & 0x7fff] | (((S.spr_hi[i >> 3] >> (i & 7)) & 1) << 8));
}

static void plot_sprite(int f, int destbase, int x, int color)
{
    const int i = destbase + x;
    const uint16_t old = spr_get(i);
    if (old & 0xf) {
        S.sprite_collide[((old >> 4) & 0x1f) + 32 * f] = 1;
        S.sprite_collide_summary = true;
    }
    const uint16_t v = (uint16_t)(color | (f * 0x10));
    S.spr_lo[i >> 15][i & 0x7fff] = (uint8_t)v;
    if (v & 0x100) S.spr_hi[i >> 3] |= (uint8_t)(1 << (i & 7));
    else S.spr_hi[i >> 3] &= (uint8_t)~(1 << (i & 7));
}

static void draw_sprites(void)
{
    for (int f = 0; f < 32; f++) {
        const int base = 0xd000 + f * 0x10;
        int srcaddr = MEM(base + 6) | (MEM(base + 7) << 8);
        const int stride = MEM(base + 4) | (MEM(base + 5) << 8);
        int bank = ((MEM(base + 3) & 0x80) >> 7) | ((MEM(base + 3) & 0x40) >> 5) | ((MEM(base + 3) & 0x20) >> 3);
        const int xstart = ((MEM(base + 2) | (MEM(base + 3) << 8)) & 0x1ff) / 2;
        const int bottom = MEM(base + 1) + 1, top = MEM(base) + 1;
        bank %= SPRITE_BANKS;
        const size_t bank_base = (size_t)bank * 0x8000;
        for (int y = top; y < bottom; y++) {
            const int destbase = (y & 0xff) * 256;
            srcaddr = (srcaddr + stride) & 0xffff;
            if (y > 256) continue;
            const int delta = (srcaddr & 0x8000) ? -1 : 1;
            int cur = srcaddr, x = xstart;
            for (int guard = 0; guard < 512; guard++) {
                const size_t i = bank_base + (size_t)(cur & 0x7fff);
                const uint8_t d = i < 0x10000 ? S.sprite_rom[i] : 0;
                const int c1 = (cur & 0x8000) ? d & 0xf : d >> 4, c2 = (cur & 0x8000) ? d >> 4 : d & 0xf;
                if (c1 == 0xf) break;
                if (c1 && x >= 0 && x <= 255) plot_sprite(f, destbase, x, c1);
                if (c2 == 0xf) break;
                if (c2 && x + 1 >= 0 && x + 1 <= 255) plot_sprite(f, destbase, x + 1, c2);
                cur = (cur + delta) & 0xffff;
                x += 2;
            }
        }
    }
}

static void render_frame(void)
{
    if (S.videomode & 0x10) {
        for (int y = 0; y < H; y++) memset(arc_fb_row(&S.fb, y), 0, W * 2);
        return;
    }
    memset(S.spr_lo[0], 0, 0x8000);
    memset(S.spr_lo[1], 0, 0x8000);
    memset(S.spr_hi, 0, sizeof S.spr_hi);
    if (MEM(0xd000) != 0xff) draw_sprites();
    for (int y = 0; y < H; y++) {
        const int bgy = (y + S.bg_yscroll) & 0xff;
        uint16_t *out = arc_fb_row(&S.fb, y);
        for (int x = 0; x < W; x++) {
            const uint16_t fg = tile_px(1, x, y), bg = tile_px(0, (x - S.bg_xscroll) & 0xff, bgy);
            const uint16_t spr = spr_get(y * 256 + x);
            const int idx = ((spr & 0xf) == 0 ? 1 : 0) | ((fg & 7) == 0 ? 2 : 0) | (((fg >> 9) & 3) << 2) |
                            ((bg & 7) == 0 ? 16 : 0) | (((bg >> 9) & 3) << 5);
            uint8_t lv = S.mix_lookup[idx & 0xff];
            if (!(lv & 4)) {
                S.mix_collide[((lv & 8) << 2) | ((spr >> 4) & 0x1f)] = 1;
                S.mix_collide_summary = true;
            }
            lv &= 3;
            out[x] = lv == 0 ? S.palette[spr & 0x1ff] : lv == 1 ? S.palette[0x200 | (fg & 0x1ff)] : S.palette[0x400 | (bg & 0x1ff)];
        }
    }
}

static const uint16_t *frame_row(int y) { return arc_fb_row(&S.fb, y); }

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    if (!arc_fb_init(&S.fb, W, H) || !(S.spr_lo[0] = (uint8_t *)calloc(1, 0x8000)) || !(S.spr_lo[1] = (uint8_t *)calloc(1, 0x8000)))
        return false;
    arc_frame_row_hook = frame_row;
    // the 315-5177: the whole 0xc000 decrypted (the opcodes view up to
    // 7fff), a KB at a time
    const uint8_t *raw = ARC_ROM_BLOB("main.raw", kMain, 0xc000);
    uint8_t *data = (uint8_t *)malloc(0x400), *ops = (uint8_t *)malloc(0x400);
    if (!raw || !data || !ops) return false;
    arc_blob b;
    if (arc_blob_begin(&b, "main", 0xc000, ARC_ROM_KEY(kMain, 1)))
        for (uint32_t a = 0; a < 0xc000; a += 0x400) {
            sega_decrypt_type2_part(raw + a, 0x400, a, SEGA2_315_5177, 0, data, ops);
            arc_blob_write(&b, a, data, 0x400);
        }
    if (!(S.rom = arc_blob_end(&b))) return false;
    if (arc_blob_begin(&b, "opcodes", 0x8000, ARC_ROM_KEY(kMain, 2)))
        for (uint32_t a = 0; a < 0x8000; a += 0x400) {
            sega_decrypt_type2_part(raw + a, 0x400, a, SEGA2_315_5177, 0, data, ops);
            arc_blob_write(&b, a, ops, 0x400);
        }
    free(data);
    free(ops);
    arc_blob_free(raw);
    if (!(S.opcodes = arc_blob_end(&b)) || !(raw = ARC_ROM_BLOB("chars.raw", kChars, 0xc000))) return false;
    arc_layout cl = { 8, 8, 2048, 3, 64, false, false, false, {0, 0x4000 * 8, 0x8000 * 8}, kX8, kY8 };
    if (arc_blob_begin(&b, "chars", 2048 * 64, ARC_ROM_KEY(kChars, 1))) arc_blob_gfx(&b, &cl, raw, 0xc000, 0);
    arc_blob_free(raw);
    if (!(S.chars = arc_blob_end(&b)) || !(S.sprite_rom = ARC_ROM_BLOB("sprites", kSprites, 0x10000)) ||
        !(S.sound_rom = ARC_ROM_BLOB("sound", kSound, 0x8000)) || !ARC_LOAD(kVideoProm, S.mix_lookup, 0x100))
        return false;
    static const arc_resnet nets[3] = {{3, {995, 495, 250}, 0, 0}, {3, {995, 495, 250}, 0, 0}, {2, {495, 250}, 0, 0}};
    double w[3][8];
    arc_resistor_weights(0, 255, -1.0, nets, 3, w);
    memcpy(S.rw, w[0], sizeof S.rw);
    memcpy(S.gw, w[1], sizeof S.gw);
    memcpy(S.bw, w[2], sizeof S.bw);
    S.main.read = read_data;
    S.main.opcode_read = read_opcode;
    S.main.write = write_byte;
    S.main.in = read_port;
    S.main.out = write_port;
    S.sound.read = snd_read;
    S.sound.write = snd_write;
    S.sound.in = in_ff;
    S.sound.out = out_nop;
    S.sound.cycles = sound_cycles;
    zd_init(&S.main);
    zd_init(&S.sound);
    sn76496_init(&S.psg0, 2000000, RATE, 0.5f);
    sn76496_init(&S.psg1, 4000000, RATE, 1.0f);
    z80piod_init(&S.pio);
    S.pio.port[Z80PIO_A].out_func = pio_a_out;
    S.pio.port[Z80PIO_A].ready_func = pio_ready_a;
    S.pio.port[Z80PIO_B].out_func = pio_b_out;
    S.dsw_a = 0xff;
    S.dsw_b = 0xec;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&S.main);
    zd_reset(&S.sound);
    sn76496_reset(&S.psg0);
    sn76496_reset(&S.psg1);
    z80piod_reset(&S.pio);
    memset(S.bg_ram, 0, sizeof S.bg_ram);
    memset(S.pal_ram, 0, sizeof S.pal_ram);
    memset(S.mix_collide, 0, sizeof S.mix_collide);
    memset(S.sprite_collide, 0, sizeof S.sprite_collide);
    S.mix_collide_summary = S.sprite_collide_summary = false;
    S.in0 = S.in1 = S.in2 = 0xff;
    S.sound_latch = S.videomode = 0;
    S.bg_xscroll = 0;
    S.bg_yscroll = 0;
    S.audio_acc = 0;
    S.sound_irq_accum = 0;
    for (int i = 0; i < 0x600; i++) S.palette[i] = 0;
    for (int y = 0; y < H; y++) memset(arc_fb_row(&S.fb, y), 0, W * 2);
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / (SCANLINES * FPS)), sc = (int)((double)SOUND_CLOCK / (SCANLINES * FPS));
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == IRQ_LINE) {
            zd_set_irq(&S.main, ZD_HOLD, 0xff);
            S.bg_xscroll = (int)(uint16_t)(S.bg_ram[0xffc] | (S.bg_ram[0xffd] << 8)) / 2 + 14;
            S.bg_yscroll = S.bg_ram[0xfbd];
            render_frame();
        }
        if (++S.sound_irq_accum >= 64) {    // the sound IRQ every 64 lines
            S.sound_irq_accum = 0;
            zd_set_irq(&S.sound, ZD_HOLD, 0xff);
        }
        zd_run(&S.main, mc);
        zd_run(&S.sound, sc);
    }
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return arc_fb_whole(&S.fb); }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_B3) v &= ~0x01;
    if (p & AP_B1) v &= ~0x02;
    if (p & AP_B2) v &= ~0x04;
    if (p & AP_DOWN) v &= ~0x10;
    if (p & AP_UP) v &= ~0x20;
    if (p & AP_RIGHT) v &= ~0x40;
    if (p & AP_LEFT) v &= ~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    uint8_t v = 0xff;
    if (in->coin1) v &= ~0x01;
    if (in->coin2) v &= ~0x02;
    if (in->p[0] & AP_START) v &= ~0x10;
    if (in->p[1] & AP_START) v &= ~0x20;
    S.in0 = v;
    S.in1 = pad(in->p[0]);
    S.in2 = pad(in->p[1]);
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) S.dsw_a = v;
    else if (bank == 1) S.dsw_b = v;
}
