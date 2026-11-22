// renegade.c - Renegade (Technos Japan / Taito America, 1986) for the common
// arcade frontends (arc.h): C port of dsp-cpp's drivers/arcade/renegade (from
// dsp-emulator's renegade_hw.pas). A 6502 at 1.5 MHz, a 6809 with a YM3526
// and an MSM5205 playing the ADPCM samples, and a 68705 protection MCU. A
// 1024-wide scrolling background of 16x16 tiles, 8x8 text and 16x16 / 16x32
// sprites, all 3 bits per pixel.
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../machine/taito68705dsp.h"
#include "../../../sound/ym3812dsp.h"

#define MAIN_CLOCK 1500000
#define SOUND_CLOCK 1500000
#define MCU_CLOCK 3000000
#define YM_CLOCK 3000000
#define MSM_RATE (12000000 / 32 / 48)   // the MSM5205's VCLK, prescaler 48
#define SCANLINES 272
#define FPS 57.444853
#define RATE 44100
#define W 256
#define H 238

static const char *const kSets[] = { "renegade", NULL };
static const arc_rom kMain[] = {{"na-5.ic52", 0x8000, 0x0000, 0xde7e7df4}, {"nb-5.ic51", 0x8000, 0x8000, 0xba683ddf}};
static const arc_rom kChars[] = {{"nc-5.bin", 0x8000, 0, 0x9adfaa5d}};
static const arc_rom kSound[] = {{"n0-5.ic13", 0x8000, 0x8000, 0x3587de3b}};
static const arc_rom kMcu[] = {{"nz-5.ic97", 0x800, 0, 0x32e47560}};
static const arc_rom kTiles[] = {
    {"n1-5.ic1", 0x8000, 0x00000, 0x4a9f47f3}, {"n6-5.ic28", 0x8000, 0x08000, 0xd62a0aa8},
    {"n7-5.ic27", 0x8000, 0x10000, 0x7ca5a532}, {"n2-5.ic14", 0x8000, 0x18000, 0x8d2e7982},
    {"n8-5.ic26", 0x8000, 0x20000, 0x0dba31d3}, {"n9-5.ic25", 0x8000, 0x28000, 0x5b621b6a}};
static const arc_rom kSprites[] = {
    {"nh-5.bin", 0x8000, 0x00000, 0xdcd7857c}, {"nd-5.bin", 0x8000, 0x08000, 0x2de1717c},
    {"nj-5.bin", 0x8000, 0x10000, 0x0f96a18e}, {"nn-5.bin", 0x8000, 0x18000, 0x1bf15787},
    {"ne-5.bin", 0x8000, 0x20000, 0x924c7388}, {"nk-5.bin", 0x8000, 0x28000, 0x69499a94},
    {"ni-5.bin", 0x8000, 0x30000, 0x6f597ed2}, {"nf-5.bin", 0x8000, 0x38000, 0x0efc8d45},
    {"nl-5.bin", 0x8000, 0x40000, 0x14778336}, {"no-5.bin", 0x8000, 0x48000, 0x147dd23b},
    {"ng-5.bin", 0x8000, 0x50000, 0xa8ee3720}, {"nm-5.bin", 0x8000, 0x58000, 0xc100258e}};
static const arc_rom kAdpcm[] = {
    {"n3-5.ic33", 0x8000, 0x0000, 0x78fd6190}, {"n4-5.ic32", 0x8000, 0x8000, 0x6557564c},
    {"n5-5.ic31", 0x8000, 0x10000, 0x7ee43a3c}};

const arc_info arc_game = { "renegade", "Renegade", "Technos Japan", 1986, W, H, FPS, RATE, kSets,
                            { "ATTACK L", "JUMP", "ATTACK R", NULL }, ARC_JOY_8WAY };

static const int kCharX[8] = {1, 0, 65, 64, 129, 128, 193, 192};
static const int kY8[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int kX16[16] = {3, 2, 1, 0, 131, 130, 129, 128, 259, 258, 257, 256, 387, 386, 385, 384};
static const int kY16[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};

// the MSM5205 (dsp-cpp's model, the samples read from its ROM)
typedef struct {
    uint32_t position, end;
    int data_value, signal, step;
    bool reset, idle;
} msm5205;

// the machine on the heap
static struct state {
    m65xx main;
    m6809d sound;
    t68705d mcu;
    ym3812d ym;
    msm5205 msm;
    uint8_t mem[0x10000], bank_rom[2][0x4000], sound_mem[0x10000], pal_ram[0x200];
    uint8_t *chars, *tiles, *sprites, *adpcm;
    uint16_t palette[256];
    uint16_t *bg, *fg, *spr, *fb;       // the layers: 0 transparent
    uint8_t in0, in1, dswa, dswb, bank, sound_latch;
    uint16_t scroll_x;
    int scroll_comp;
    int64_t audio_acc, msm_acc;
    int16_t audio[1024];
    int naudio;
} *R_;
#define R (*R_)

static int msm_diff[49 * 16];
static const int kIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static void msm_build(void)
{
    static const int nib[16][4] = {
        {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1}, {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
    for (int step = 0; step <= 48; step++) {
        const int sv = (int)floor(16.0 * pow(11.0 / 10.0, step));
        for (int n = 0; n < 16; n++)
            msm_diff[step * 16 + n] = nib[n][0] * (sv * nib[n][1] + (sv >> 1) * nib[n][2] + (sv >> 2) * nib[n][3] + (sv >> 3));
    }
}

static void msm_set_reset(bool state)
{
    R.msm.reset = R.msm.idle = state;
    if (state) {
        R.msm.signal = R.msm.step = 0;
        R.msm.data_value = -1;
    }
}

static void msm_decode(uint8_t nibble)
{
    if (R.msm.reset) {
        R.msm.signal = R.msm.step = 0;
        R.msm.data_value = -1;
        return;
    }
    R.msm.signal += msm_diff[R.msm.step * 16 + (nibble & 0x0f)];
    if (R.msm.signal > 2047) R.msm.signal = 2047;
    else if (R.msm.signal < -2048) R.msm.signal = -2048;
    R.msm.step += kIndexShift[nibble & 7];
    if (R.msm.step > 48) R.msm.step = 48;
    else if (R.msm.step < 0) R.msm.step = 0;
}

static void msm_vclk(void)
{
    if (R.msm.idle) return;
    if (R.msm.data_value != -1) {
        msm_decode((uint8_t)(R.msm.data_value & 0x0f));
        R.msm.data_value = -1;
        R.msm.position++;
        if (R.msm.position >= R.msm.end || R.msm.position >= 0x18000) msm_set_reset(true);
    } else {
        R.msm.data_value = R.msm.position < 0x18000 ? R.adpcm[R.msm.position] : 0;
        msm_decode((uint8_t)(R.msm.data_value >> 4));
    }
}

static void update_palette(int i)
{
    const uint8_t lo = R.pal_ram[i], hi = R.pal_ram[i + 0x100];
    R.palette[i] = arc_rgb565((uint32_t)((lo & 15) * 0x11) << 16 | (uint32_t)((lo >> 4) * 0x11) << 8 | (uint32_t)((hi & 15) * 0x11));
}

static uint8_t main_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff || (a >= 0x2800 && a <= 0x2fff) || a >= 0x8000) return R.mem[a];
    if (a <= 0x27ff) return R.mem[0x2000 + (a & 0x1ff)];
    if (a >= 0x3000 && a <= 0x31ff) return R.pal_ram[a & 0x1ff];
    switch (a) {
    case 0x3800: return R.in0;
    case 0x3801: return R.in1;
    case 0x3802: return (uint8_t)(R.dswb | ((!R.mcu.main_sent) << 4) | ((!R.mcu.mcu_sent) << 5));
    case 0x3803: return R.dswa;
    case 0x3804: return t68705d_read(&R.mcu);
    case 0x3805:                        // pulses the MCU's reset
        t68705d_set_reset(&R.mcu, true);
        t68705d_set_reset(&R.mcu, false);
        return 0;
    default: break;
    }
    if (a >= 0x4000 && a <= 0x7fff) return R.bank_rom[R.bank & 1][a & 0x3fff];
    return 0xff;
}

static void main_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff || (a >= 0x2800 && a <= 0x2fff)) R.mem[a] = v;
    else if (a <= 0x27ff) R.mem[0x2000 + (a & 0x1ff)] = v;
    else if (a >= 0x3000 && a <= 0x31ff) {
        R.pal_ram[a & 0x1ff] = v;
        update_palette(a & 0xff);
    } else switch (a) {
        case 0x3800: R.scroll_x = (uint16_t)((R.scroll_x & 0xff00) | v); break;
        case 0x3801: R.scroll_x = (uint16_t)((R.scroll_x & 0x00ff) | (v << 8)); break;
        case 0x3802:
            R.sound_latch = v;
            m6809d_set_irq(&R.sound, M6809D_ASSERT);
            break;
        case 0x3803: R.scroll_comp = (v & 1) ? 256 : 0; break;
        case 0x3804: t68705d_write(&R.mcu, v); break;
        case 0x3805: R.bank = v & 1; break;
        case 0x3806: m65xx_set_nmi(&R.main, M65_CLEAR); break;
        case 0x3807: m65xx_set_irq(&R.main, M65_CLEAR); break;
        default: break;
        }
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x0fff || a >= 0x8000) return R.sound_mem[a];
    if (a == 0x1000) {
        m6809d_set_irq(&R.sound, M6809D_CLEAR);
        return R.sound_latch;
    }
    if (a == 0x2800) return ym3812d_status(&R.ym);
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x0fff) R.sound_mem[a] = v;
    else if (a == 0x1800) msm_set_reset(false);          // start
    else if (a == 0x2000) {
        uint32_t pos;
        switch (v & 0x1c) {
        case 0x0c: pos = 2 * 0x8000; break;              // ic31
        case 0x14: pos = 1 * 0x8000; break;              // ic32
        case 0x18: pos = 0; break;                       // ic33
        default: R.msm.position = R.msm.end = 0; return;
        }
        pos |= (uint32_t)(v & 3) * 0x2000;
        R.msm.position = pos;
        R.msm.end = pos + 0x2000;
    } else if (a == 0x2800) ym3812d_control(&R.ym, v);
    else if (a == 0x2801) ym3812d_write(&R.ym, v);
    else if (a == 0x3000) msm_set_reset(true);           // stop
}

static void ym_irq(void *u, bool on) { (void)u; m6809d_set_firq(&R.sound, on ? M6809D_ASSERT : M6809D_CLEAR); }

static void sound_cycles(int c)
{
    if (!R.msm.idle) {
        R.msm_acc += (int64_t)c * MSM_RATE;
        while (R.msm_acc >= SOUND_CLOCK) {
            R.msm_acc -= SOUND_CLOCK;
            msm_vclk();
            if (R.msm.idle) m6809d_set_nmi(&R.sound, M6809D_PULSE);
        }
    }
    R.audio_acc += (int64_t)c * RATE;
    while (R.audio_acc >= SOUND_CLOCK) {
        R.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym3812d_update(&R.ym) + R.msm.signal * 16;
        if (R.naudio < 1024) R.audio[R.naudio++] = arc_clamp16(s);
    }
}

static void blit(uint16_t *dst, int dw, int dh, int dx, int dy, const uint8_t *src, int size, int base, bool fx)
{
    for (int y = 0; y < size; y++) {
        const int py = dy + y;
        if (py < 0 || py >= dh) continue;
        for (int x = 0; x < size; x++) {
            const uint8_t p = src[y * size + (fx ? size - 1 - x : x)];
            const int px = dx + x;
            if (p && px >= 0 && px < dw) dst[py * dw + px] = (uint16_t)(0x100 | ((base + p) & 0xff));
        }
    }
}

static void update_video(void)
{
    // the layers hold 0x100 | the colour index, 0 where transparent
    memset(R.bg, 0, 1024 * 256 * 2);
    memset(R.fg, 0, 256 * 256 * 2);
    memset(R.spr, 0, 256 * 256 * 2);
    for (int f = 0; f < 0x400; f++) {
        const uint8_t attr = R.mem[0x2c00 + f];
        blit(R.bg, 1024, 256, (f % 64) * 16, (f / 64) * 16, R.tiles + (size_t)(R.mem[0x2800 + f] + ((attr & 7) << 8)) * 256, 16,
             ((attr >> 5) << 3) + 192, false);
    }
    for (int f = 0; f < 0x400; f++) {
        const uint8_t attr = R.mem[0x1c00 + f];
        blit(R.fg, 256, 256, (f % 32) * 8, (f / 32) * 8, R.chars + (size_t)(R.mem[0x1800 + f] + ((attr & 3) << 8)) * 64, 8,
             (attr >> 6) << 3, false);
    }
    for (int f = 0; f < 0x80; f++) {
        const int y = 224 - R.mem[0x2000 + f * 4];
        if (y < 16) continue;
        const uint8_t attr = R.mem[0x2001 + f * 4];
        const int x = R.mem[0x2003 + f * 4];
        int code = R.mem[0x2002 + f * 4] + ((attr & 0x0f) << 8);
        const int color = ((attr & 0x30) >> 1) + 128;
        const bool fx = (attr & 0x40) != 0;
        if (attr & 0x80) {
            code &= ~1;
            blit(R.spr, 256, 256, x, y + 16, R.sprites + (size_t)(code | 1) * 256, 16, color, fx);
            blit(R.spr, 256, 256, x, y, R.sprites + (size_t)code * 256, 16, color, fx);
        } else blit(R.spr, 256, 256, x, y + 16, R.sprites + (size_t)code * 256, 16, color, fx);
    }
    const int scroll = (R.scroll_x - R.scroll_comp) & 0x3ff;
    for (int y = 0; y < H; y++) {
        const int sy = y + 9;
        for (int x = 0; x < W; x++) {
            uint16_t p = R.bg[sy * 1024 + ((x + scroll) & 0x3ff)];
            if (R.fg[sy * 256 + x]) p = R.fg[sy * 256 + x];
            if (R.spr[sy * 256 + x]) p = R.spr[sy * 256 + x];
            R.fb[y * W + x] = p ? R.palette[p & 0xff] : 0;
        }
    }
}

// the tiles and sprites: per 0x18000 bytes, four batches of 0x100 with
// different plane layouts (renegade_hw.pas)
static bool decode16(uint8_t *out, const uint8_t *rom, int halves)
{
    static const int planes[4][3] = {{4, 0x8000 * 8, 0x8000 * 8 + 4}, {0, 0xc000 * 8, 0xc000 * 8 + 4},
                                     {0x4000 * 8 + 4, 0x10000 * 8, 0x10000 * 8 + 4}, {0x4000 * 8, 0x14000 * 8, 0x14000 * 8 + 4}};
    for (int f = 0; f < halves; f++)
        for (int k = 0; k < 4; k++) {
            arc_layout l = { 16, 16, 0x100, 3, 512, false, false, false, {planes[k][0], planes[k][1], planes[k][2]}, kX16, kY16 };
            uint8_t *g = arc_gfx_decode(&l, rom + f * 0x18000, 0x18000);
            if (!g) return false;
            memcpy(out + (size_t)(f * 0x400 + k * 0x100) * 256, g, 0x100 * 256);
            free(g);
        }
    return true;
}

bool arc_init(void)
{
    if (!(R_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    R.bg = (uint16_t *)malloc(1024 * 256 * 2);
    R.fg = (uint16_t *)malloc(256 * 256 * 2);
    R.spr = (uint16_t *)malloc(256 * 256 * 2);
    R.fb = (uint16_t *)calloc(W * H, 2);
    R.adpcm = (uint8_t *)calloc(1, 0x18000);
    R.tiles = (uint8_t *)malloc((size_t)0x800 * 256);
    R.sprites = (uint8_t *)malloc((size_t)0x1000 * 256);
    uint8_t *rom = (uint8_t *)calloc(1, 0x60000);
    if (!R.bg || !R.fg || !R.spr || !R.fb || !R.adpcm || !R.tiles || !R.sprites || !rom) return false;
    if (!ARC_LOAD(kMain, rom, 0x10000)) return false;
    memcpy(R.mem + 0x8000, rom + 0x8000, 0x8000);
    memcpy(R.bank_rom[0], rom, 0x4000);
    memcpy(R.bank_rom[1], rom + 0x4000, 0x4000);
    if (!ARC_LOAD(kSound, R.sound_mem, 0x10000)) return false;
    t68705d_init(&R.mcu, T68705_STANDARD);
    if (!ARC_LOAD(kMcu, R.mcu.mem, 0x800)) return false;
    memset(rom, 0, 0x60000);
    if (!ARC_LOAD(kChars, rom, 0x8000)) return false;
    arc_layout cl = { 8, 8, 0x400, 3, 256, false, false, false, {2, 4, 6}, kCharX, kY8 };
    if (!(R.chars = arc_gfx_decode(&cl, rom, 0x8000))) return false;
    memset(rom, 0, 0x60000);
    if (!ARC_LOAD(kTiles, rom, 0x30000) || !decode16(R.tiles, rom, 2)) return false;
    memset(rom, 0, 0x60000);
    if (!ARC_LOAD(kSprites, rom, 0x60000) || !decode16(R.sprites, rom, 4)) return false;
    free(rom);
    if (!ARC_LOAD(kAdpcm, R.adpcm, 0x18000)) return false;
    msm_build();
    R.main.read = main_read;
    R.main.write = main_write;
    R.sound.read = sound_read;
    R.sound.write = sound_write;
    ym3812d_init(&R.ym, YM_CLOCK, true, 1.0f);
    R.ym.irq = ym_irq;
    R.dswa = 0xbf;
    R.dswb = 0x8f;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m65xx_reset(&R.main);
    m6809d_reset(&R.sound);
    t68705d_reset(&R.mcu);
    ym3812d_reset(&R.ym);
    memset(&R.msm, 0, sizeof R.msm);
    R.msm.data_value = -1;
    msm_set_reset(true);
    R.in0 = R.in1 = 0xff;
    R.bank = R.sound_latch = 0;
    R.scroll_x = 0;
    R.scroll_comp = 256;
    R.audio_acc = 0;
    memset(R.pal_ram, 0, sizeof R.pal_ram);
    for (int i = 0; i < 256; i++) R.palette[i] = 0;
    memset(R.fb, 0, W * H * 2);
}

int arc_run_frame(const int16_t **audio)
{
    int slice = (int)(MAIN_CLOCK / FPS) / SCANLINES, sslice = (int)(SOUND_CLOCK / FPS) / SCANLINES;
    int mslice = (int)(MCU_CLOCK / FPS) / SCANLINES;
    if (slice < 1) slice = 1;
    if (sslice < 1) sslice = 1;
    if (mslice < 1) mslice = 1;
    R.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        switch (line) {
        case 16: case 40: case 56: case 72: case 88: case 104: case 120: case 136:
        case 152: case 168: case 184: case 200: case 216: case 232: case 248: case 264:
            m65xx_set_irq(&R.main, M65_ASSERT);
            break;
        case 19: R.dswb &= (uint8_t)~0x40; break;
        case 257:
            update_video();
            R.dswb |= 0x40;
            break;
        case 265: m65xx_set_nmi(&R.main, M65_ASSERT); break;
        default: break;
        }
        m65xx_run(&R.main, slice);
        m6809d_run(&R.sound, sslice);
        sound_cycles(sslice);
        t68705d_run(&R.mcu, mslice);
    }
    if (audio) *audio = R.audio;
    return R.naudio;
}

const uint16_t *arc_frame(void) { return R.fb; }

void arc_input(const arc_in *in)
{
    const unsigned p1 = in->p[0], p2 = in->p[1];
    uint8_t a = 0xff, b = 0xff;
    if (p1 & AP_RIGHT) a &= ~0x01;
    if (p1 & AP_LEFT) a &= ~0x02;
    if (p1 & AP_UP) a &= ~0x04;
    if (p1 & AP_DOWN) a &= ~0x08;
    if (p1 & AP_B1) a &= ~0x10;
    if (p1 & AP_B2) a &= ~0x20;
    if (p1 & AP_START) a &= ~0x40;
    if (p2 & AP_START) a &= ~0x80;
    if (p2 & AP_RIGHT) b &= ~0x01;
    if (p2 & AP_LEFT) b &= ~0x02;
    if (p2 & AP_UP) b &= ~0x04;
    if (p2 & AP_DOWN) b &= ~0x08;
    if (p2 & AP_B1) b &= ~0x10;
    if (p2 & AP_B2) b &= ~0x20;
    if (in->coin1) b &= ~0x40;
    if (in->coin2) b &= ~0x80;
    R.in0 = a;
    R.in1 = b;
    // the third buttons on DSW B bits 2 / 3 (active low)
    if (p1 & AP_B3) R.dswb &= ~0x04; else R.dswb |= 0x04;
    if (p2 & AP_B3) R.dswb &= ~0x08; else R.dswb |= 0x08;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) R.dswa = v;
    if (bank == 1) R.dswb = (uint8_t)((R.dswb & 0x40) | (v & ~0x40));   // keeps the vblank bit
}
