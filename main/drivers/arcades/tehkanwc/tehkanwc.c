// tehkanwc.c - Tehkan World Cup (Tehkan, 1985) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/tehkanwc (from
// dsp-emulator's tehkanworldcup_hw.pas). Three Z80 at 4.6 MHz (main and sub
// sharing the video and work RAM, sound), two AY-3-8910 and an MSM5205
// (dsp-cpp's hookup), a 512x256 background of 16x8 tiles, 8x8 text split
// around the sprites. The trackballs are driven by the joystick.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ay8910.h"
#include "../../../sound/msm5205dsp.h"

#define CLOCK 4608000
#define AY_CLOCK 1536000
#define MSM_CLOCK 384000
#define FPS 60.0
#define SCANLINES 256
#define RATE 44100
#define W 256
#define H 224

static const char *const kSets[] = { "tehkanwc", NULL };
static const arc_rom kMain[] = {
    {"twc-1.bin", 0x4000, 0x0000, 0x34d6d5ff}, {"twc-2.bin", 0x4000, 0x4000, 0x7017a221},
    {"twc-3.bin", 0x4000, 0x8000, 0x8b662902}};
static const arc_rom kSub[] = {{"twc-4.bin", 0x8000, 0, 0x70a9f883}};
static const arc_rom kSound[] = {{"twc-6.bin", 0x4000, 0, 0xe3112be2}};
static const arc_rom kAdpcm[] = {{"twc-5.bin", 0x4000, 0, 0x444b5544}};
static const arc_rom kChars[] = {{"twc-12.bin", 0x4000, 0, 0xa9e274f8}};
static const arc_rom kSprites[] = {{"twc-8.bin", 0x8000, 0, 0x055a5264}, {"twc-7.bin", 0x8000, 0x8000, 0x59faebe7}};
static const arc_rom kTiles[] = {{"twc-11.bin", 0x8000, 0, 0x669389fc}, {"twc-9.bin", 0x8000, 0x8000, 0x347ef108}};

const arc_info arc_game = { "tehkanwc", "Tehkan World Cup", "Tehkan", 1985, W, H, FPS, RATE, kSets,
                            { "KICK", NULL, NULL, NULL }, ARC_JOY_8WAY };

static const int kPsX[16] = {4, 0, 12, 8, 20, 16, 28, 24, 260, 256, 268, 264, 276, 272, 284, 280};
static const int kPsY[16] = {0, 32, 64, 96, 128, 160, 192, 224, 512, 544, 576, 608, 640, 672, 704, 736};

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    zd_z80 main, sub, sound;
    ay8910 ay0, ay1;
    msm5205d msm;
    uint8_t mem[0x10000], mem_sub[0x10000], mem_snd[0x10000], pal_ram[0x800], adpcm[0x8000];
    uint8_t *chars, *sprites, *tiles;
    uint16_t palette[0x400];
    uint16_t composite[256 * 256], lo[256 * 256], hi[256 * 256];   // lo / hi: palette index + 1
    uint16_t *bg;                       // 512x256
    uint16_t *fb;
    uint16_t scroll_x, msm_pos;
    uint8_t scroll_y, sound_latch, sound_latch2, track0[2], track1[2];
    int8_t stick0_x, stick0_y, stick1_x, stick1_y;
    bool sub_reset;
    uint8_t in0, in1, in2, dsw_a, dsw_b, dsw_c;
    int64_t audio_acc, msm_acc;
    int16_t audio[1024];
    int naudio;
} *T_;
#define T (*T_)

static void set_palette_color(int index)
{
    const int dir = index & 0x7fe;
    const uint8_t lo = T.pal_ram[dir], hi = T.pal_ram[dir + 1];
    T.palette[dir >> 1] = arc_rgb565((uint32_t)((hi & 15) * 0x11) << 16 | (uint32_t)((hi >> 4) * 0x11) << 8 | (uint32_t)((lo & 15) * 0x11));
}

static void shared_write(uint16_t a, uint8_t v)
{
    T.mem[a] = v;
    const uint16_t off = (uint16_t)(a - 0xc800);
    if (off >= 0x1000 && off <= 0x17ff) {
        T.pal_ram[a & 0x7ff] = v;
        set_palette_color(a & 0x7fe);
    } else if (off == 0x2400) T.scroll_x = (uint16_t)((T.scroll_x & 0x100) | v);
    else if (off == 0x2401) T.scroll_x = (uint16_t)((T.scroll_x & 0xff) | ((v & 1) << 8));
    else if (off == 0x2402) T.scroll_y = v;
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xec02) return T.mem[a];
    switch (a) {
    case 0xf800: return (uint8_t)(T.track0[0] - T.stick0_x);
    case 0xf801: return (uint8_t)(T.track0[1] - T.stick0_y);
    case 0xf802: case 0xf806: return T.in2;
    case 0xf803: return T.in0;
    case 0xf810: return (uint8_t)(T.track1[0] - T.stick1_x);
    case 0xf811: return (uint8_t)(T.track1[1] - T.stick1_y);
    case 0xf813: return T.in1;
    case 0xf820: return T.sound_latch2;
    case 0xf840: return T.dsw_a;
    case 0xf850: return T.dsw_b;
    case 0xf870: return T.dsw_c;
    default: return 0xff;
    }
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0xbfff) return;
    if (a <= 0xc7ff) { T.mem[a] = v; return; }
    if (a <= 0xec02) { shared_write(a, v); return; }
    switch (a) {
    case 0xf800: T.track0[0] = v; break;
    case 0xf801: T.track0[1] = v; break;
    case 0xf810: T.track1[0] = v; break;
    case 0xf811: T.track1[1] = v; break;
    case 0xf820:
        T.sound_latch = v;
        zd_set_nmi(&T.sound, ZD_ASSERT);
        break;
    case 0xf840:
        T.sub_reset = v == 0;
        if (T.sub_reset) zd_reset(&T.sub);
        break;
    default: break;
    }
}

static uint8_t sub_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0xc7ff) return T.mem_sub[a];
    if (a <= 0xec02) return T.mem[a];
    return 0xff;
}

static void sub_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x7fff) return;
    if (a <= 0xc7ff) T.mem_sub[a] = v;
    else if (a <= 0xec02) shared_write(a, v);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x47ff) return T.mem_snd[a];
    if (a == 0xc000) return T.sound_latch;
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x3fff) return;
    if (a <= 0x47ff) { T.mem_snd[a] = v; return; }
    switch (a) {
    case 0x8001: msm5205d_set_reset(&T.msm, (v & 1) == 0); break;
    case 0x8003: zd_set_nmi(&T.sound, ZD_CLEAR); break;
    case 0xc000: T.sound_latch2 = v; break;
    default: break;
    }
}

// AY 0's ports set the MSM position (writes to registers 14 / 15, and the
// port direction bits of register 7), AY 1's read it back
static void ay0_write(uint8_t v)
{
    const uint8_t reg = T.ay0.latch & 0x0f;
    const int16_t prev = T.ay0.last_enable;
    ay8910_data_w(&T.ay0, v);
    if (reg == 14) T.msm_pos = (uint16_t)((T.msm_pos & 0xff00) | v);
    else if (reg == 15) T.msm_pos = (uint16_t)((T.msm_pos & 0x00ff) | (v << 8));
    else if (reg == 7) {
        const uint8_t en = T.ay0.regs[7];
        if (prev == -1 || ((prev & 0x40) != (en & 0x40)))
            T.msm_pos = (uint16_t)((T.msm_pos & 0xff00) | ((en & 0x40) ? T.ay0.regs[14] : 0xff));
        if (prev == -1 || ((prev & 0x80) != (en & 0x80)))
            T.msm_pos = (uint16_t)((T.msm_pos & 0x00ff) | (((en & 0x80) ? T.ay0.regs[15] : 0xff) << 8));
    }
}

static uint8_t ay1_read(void)
{
    const uint8_t reg = T.ay1.latch & 0x0f;
    if (reg == 14) T.ay1.regs[14] = (uint8_t)(T.msm_pos & 0xff);
    else if (reg == 15) T.ay1.regs[15] = (uint8_t)(T.msm_pos >> 8);
    return ay8910_data_r(&T.ay1);
}

static uint8_t sound_in(void *u, uint16_t p)
{
    (void)u;
    switch (p & 0xff) {
    case 0: return ay8910_data_r(&T.ay0);
    case 1: return ay1_read();
    default: return 0xff;
    }
}

static void sound_out(void *u, uint16_t p, uint8_t v)
{
    (void)u;
    switch (p & 0xff) {
    case 0: ay0_write(v); break;
    case 1: ay8910_address_w(&T.ay0, v); break;
    case 2: ay8910_data_w(&T.ay1, v); break;
    case 3: ay8910_address_w(&T.ay1, v); break;
    default: break;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void update_video(void)
{
    for (int f = 0; f < 0x400; f++) {   // the background, 512x256
        const uint8_t attr = T.mem[0xe001 + f * 2];
        const int code = T.mem[0xe000 + f * 2] + ((attr & 0x30) << 4);
        const bool fx = (attr & 0x40) != 0, fy = (attr & 0x80) != 0;
        const int x = (f % 32) * 16, y = (f / 32) * 8;
        const uint8_t *tile = T.tiles + (size_t)code * 128;
        for (int py = 0; py < 8; py++) {
            uint16_t *line = T.bg + (y + py) * 512;
            const uint8_t *src = tile + (fy ? 7 - py : py) * 16;
            for (int px = 0; px < 16; px++)
                line[x + px] = T.palette[(((attr & 0x0f) << 4) + src[fx ? 15 - px : px] + 512) & 0x3ff];
        }
    }
    memset(T.lo, 0, sizeof T.lo);
    memset(T.hi, 0, sizeof T.hi);
    for (int f = 0; f < 0x400; f++) {   // the text, low and high priority
        const uint8_t attr = T.mem[0xd400 + f];
        const int code = T.mem[0xd000 + f] + ((attr & 0x10) << 4);
        const bool fx = (attr & 0x40) != 0, fy = (attr & 0x80) != 0, high = (attr & 0x20) == 0;
        const int x = (f % 32) * 8, y = (f / 32) * 8;
        const uint8_t *tile = T.chars + (size_t)code * 64;
        for (int py = 0; py < 8; py++)
            for (int px = 0; px < 8; px++) {
                const uint8_t pen = tile[(fy ? 7 - py : py) * 8 + (fx ? 7 - px : px)];
                if (!pen) continue;
                const uint16_t idx = (uint16_t)((((attr & 0x0f) << 4) + pen) & 0x3ff) + 1;
                T.lo[(y + py) * 256 + x + px] = idx;
                if (high) T.hi[(y + py) * 256 + x + px] = idx;
            }
    }
    // (dsp-cpp skips the black text pixels: only colours with RGB != 0 show)
    for (int y = 0; y < 256; y++) {
        const uint16_t *bg = T.bg + ((y + T.scroll_y) & 0xff) * 512;
        uint16_t *dst = T.composite + y * 256;
        for (int x = 0; x < 256; x++) {
            dst[x] = bg[(x + T.scroll_x) & 0x1ff];
            const uint16_t l = T.lo[y * 256 + x];
            if (l && T.palette[l - 1]) dst[x] = T.palette[l - 1];
        }
    }
    for (int f = 0; f < 0x100; f++) {
        const uint8_t attr = T.mem[0xe801 + f * 4];
        const int x = T.mem[0xe802 + f * 4] + ((attr & 0x20) << 3) - 128, y = T.mem[0xe803 + f * 4];
        const int color = (attr & 7) << 4, code = T.mem[0xe800 + f * 4] + ((attr & 8) << 5);
        const bool fx = (attr & 0x40) != 0, fy = (attr & 0x80) != 0;
        const uint8_t *spr = T.sprites + (size_t)code * 256;
        for (int py = 0; py < 16; py++) {
            const int dy = y + py;
            if (dy >= 256) continue;
            for (int px = 0; px < 16; px++) {
                const int dx = x + px;
                if (dx < 0 || dx >= 256) continue;
                const uint8_t pen = spr[(fy ? 15 - py : py) * 16 + (fx ? 15 - px : px)];
                if (pen) T.composite[dy * 256 + dx] = T.palette[(color + pen + 256) & 0x3ff];
            }
        }
    }
    for (int i = 0; i < 256 * 256; i++) {
        const uint16_t h = T.hi[i];
        if (h && T.palette[h - 1]) T.composite[i] = T.palette[h - 1];
    }
    memcpy(T.fb, T.composite + 16 * 256, W * H * 2);
}

bool arc_init(void)
{
    if (!(T_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    T.fb = (uint16_t *)calloc(W * H, 2);
    T.bg = (uint16_t *)calloc(512 * 256, 2);
    uint8_t *g = (uint8_t *)calloc(1, 0x10000);
    if (!T.fb || !T.bg || !g) return false;
    if (!ARC_LOAD(kMain, T.mem, 0xc000) || !ARC_LOAD(kSub, T.mem_sub, 0x8000) || !ARC_LOAD(kSound, T.mem_snd, 0x4000) ||
        !ARC_LOAD(kAdpcm, T.adpcm, 0x4000))
        return false;
    memcpy(T.adpcm + 0x4000, T.adpcm, 0x4000);   // the 32 KB window, mirrored
    arc_layout cl = { 8, 8, 512, 4, 256, false, false, false, {0, 1, 2, 3}, kPsX, kPsY };
    if (!ARC_LOAD(kChars, g, 0x4000) || !(T.chars = arc_gfx_decode(&cl, g, 0x4000))) return false;
    arc_layout sl = { 16, 16, 512, 4, 1024, false, false, false, {0, 1, 2, 3}, kPsX, kPsY };
    if (!ARC_LOAD(kSprites, g, 0x10000) || !(T.sprites = arc_gfx_decode(&sl, g, 0x10000))) return false;
    arc_layout tl = { 16, 8, 1024, 4, 512, false, false, false, {0, 1, 2, 3}, kPsX, kPsY };
    if (!ARC_LOAD(kTiles, g, 0x10000) || !(T.tiles = arc_gfx_decode(&tl, g, 0x10000))) return false;
    free(g);
    T.main.read = main_read;
    T.main.write = main_write;
    T.sub.read = sub_read;
    T.sub.write = sub_write;
    T.sound.read = sound_read;
    T.sound.write = sound_write;
    T.sound.in = sound_in;
    T.sound.out = sound_out;
    T.main.in = T.sub.in = in_ff;
    T.main.out = T.sub.out = out_nop;
    zd_init(&T.main);
    zd_init(&T.sub);
    zd_init(&T.sound);
    ay8910_init(&T.ay0, AY_CLOCK, RATE);
    ay8910_init(&T.ay1, AY_CLOCK, RATE);
    msm5205d_init(&T.msm, MSM_CLOCK, 96, 4);
    T.msm.rom = T.adpcm;
    T.msm.rom_size = 0x8000;
    T.dsw_a = T.dsw_b = 0xff;
    T.dsw_c = 0x0f;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    zd_reset(&T.main);
    zd_reset(&T.sub);
    zd_reset(&T.sound);
    ay8910_reset(&T.ay0);
    ay8910_reset(&T.ay1);
    msm5205d_reset(&T.msm);
    T.scroll_x = 0;
    T.scroll_y = T.sound_latch = T.sound_latch2 = 0;
    T.sub_reset = true;
    T.track0[0] = T.track0[1] = T.track1[0] = T.track1[1] = 0;
    T.stick0_x = T.stick0_y = T.stick1_x = T.stick1_y = 0;
    T.in0 = T.in1 = 0x20;
    T.in2 = 0x0f;
    T.msm_pos = 0;
    T.audio_acc = T.msm_acc = 0;
    memset(T.pal_ram, 0, sizeof T.pal_ram);
    for (int i = 0; i < 0x400; i++) T.palette[i] = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int slice = (int)(CLOCK / FPS) / SCANLINES;
    const uint32_t msm_rate = msm5205d_sample_frequency(&T.msm);
    T.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        zd_run(&T.main, slice);
        if (!T.sub_reset) zd_run(&T.sub, slice);
        zd_run(&T.sound, slice);
        T.audio_acc += (int64_t)slice * RATE;
        while (T.audio_acc >= CLOCK) {
            T.audio_acc -= CLOCK;
            const int32_t s = (ay8910_render(&T.ay0) + ay8910_render(&T.ay1) + msm5205d_output(&T.msm)) / 3;
            if (T.naudio < 1024) T.audio[T.naudio++] = arc_clamp16(s);
        }
        T.msm_acc += (int64_t)slice * msm_rate;
        while (T.msm_acc >= CLOCK) {    // (dsp-cpp's: two VCLKs from msm_pos)
            T.msm_acc -= CLOCK;
            T.msm.position = T.msm_pos & 0x7fff;
            msm5205d_vclk(&T.msm);
            msm5205d_vclk(&T.msm);
        }
        if (line == 240) {
            zd_set_irq(&T.main, ZD_HOLD, 0xff);
            if (!T.sub_reset) zd_set_irq(&T.sub, ZD_HOLD, 0xff);
            zd_set_irq(&T.sound, ZD_HOLD, 0xff);
            update_video();
        }
    }
    if (audio) *audio = T.audio;
    return T.naudio;
}

const uint16_t *arc_frame(void) { return T.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    T.in0 = T.in1 = 0x20;
    T.in2 = 0x0f;
    if (p1 & AP_B1) T.in0 &= ~0x20;
    if (p2 & AP_B1) T.in1 &= ~0x20;
    if (in->coin1) T.in2 &= ~0x01;
    if (in->coin2) T.in2 &= ~0x02;
    if (p1 & AP_START) T.in2 &= ~0x04;
    if (p2 & AP_START) T.in2 &= ~0x08;
    // the joystick as trackball movement
    T.stick0_x = (int8_t)(((p1 & AP_RIGHT) ? 8 : 0) - ((p1 & AP_LEFT) ? 8 : 0));
    T.stick0_y = (int8_t)(((p1 & AP_DOWN) ? 8 : 0) - ((p1 & AP_UP) ? 8 : 0));
    T.stick1_x = (int8_t)(((p2 & AP_RIGHT) ? 8 : 0) - ((p2 & AP_LEFT) ? 8 : 0));
    T.stick1_y = (int8_t)(((p2 & AP_DOWN) ? 8 : 0) - ((p2 & AP_UP) ? 8 : 0));
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) T.dsw_a = v;
    else if (bank == 1) T.dsw_b = v;
    else if (bank == 2) T.dsw_c = v;
}
