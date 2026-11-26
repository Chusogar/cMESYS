// badlands.c - Bad Lands (Atari Games, 1989) for the common arcade frontends
// (arc.h): C port of dsp-cpp's drivers/arcade/badlands (from dsp-emulator's
// badlands_hw.pas). A 68000 at 7.16 MHz, a 6502 with a YM2151, an 8 x 8
// playfield and the split Atari motion objects (video/atari_mo).
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/ym2151.h"
#include "../../../video/atari_mo.h"

#define W 336
#define H 240
#define SCANLINES 262
#define CPU_SYNC 4
#define FPS 59.922743
#define ATARI_CLOCK 14318180
#define MAIN_CLOCK (ATARI_CLOCK / 2)
#define SOUND_CLOCK (ATARI_CLOCK / 8)
#define YM_CLOCK (ATARI_CLOCK / 4)
#define RATE 44100
#define N(k) ((int)(sizeof k / sizeof k[0]))

static const char *const kSets[] = { "badlands", NULL };
const arc_info arc_game = { "badlands", "Bad Lands", "Atari Games", 1989, W, H, FPS, RATE, kSets, { "DISPARO", "ACELERAR", NULL }, ARC_JOY_2WAY };

static const arc_rom kMainRoms[] = {
    {"136074-1008.20f", 0x10000, 0x00000, 0xa3da5774}, {"136074-1006.27f", 0x10000, 0x00001, 0xaa03b4f3},
    {"136074-1009.17f", 0x10000, 0x20000, 0x0e2e807f}, {"136074-1007.24f", 0x10000, 0x20001, 0x99a20c2c},
};
static const arc_rom kSoundRoms[] = {{"136074-1018.9c", 0x10000, 0, 0xa05fd146}};
static const arc_rom kPlayfieldRoms[] = {
    {"136074-1012.4n", 0x10000, 0x00000, 0x5d124c6c}, {"136074-1013.2n", 0x10000, 0x10000, 0xb1ec90d6},
    {"136074-1014.4s", 0x10000, 0x20000, 0x248a6845}, {"136074-1015.2s", 0x10000, 0x30000, 0x792296d8},
    {"136074-1016.4u", 0x10000, 0x40000, 0x878f7c66}, {"136074-1017.2u", 0x10000, 0x50000, 0xad0071a3},
};
static const arc_rom kSpriteRoms[] = {
    {"136074-1010.14r", 0x10000, 0x00000, 0xc15f629e}, {"136074-1011.10r", 0x10000, 0x10000, 0xfb0b6717},
    {"136074-1019.14t", 0x10000, 0x20000, 0x0e26bff6},
};

static struct state {
    m68kd cpu;
    m65xx snd;
    ym2151 ym;
    atari_mo mo;
    uint16_t main_rom[0x20000];
    uint8_t sound_memory[0x10000], sound_banks[4][0x1000];
    uint16_t ram[0x1000], palette_ram[0x200];
    uint8_t eeprom[0x1000];
    uint32_t palette[0x100];
    uint8_t *pf_gfx, *spr_gfx;
    uint16_t pf_pixels[512 * 256];
    uint32_t fbuf[W * H];
    uint16_t fb[W * H];
    uint8_t sound_bank, playfield_bank, sound_latch, main_latch;
    bool eeprom_unlocked, main_pending, sound_pending, sound_halted, ym_irq, timed_irq;
    uint16_t main_inputs;
    uint8_t sound_inputs, pedal1, pedal2, steering1, steering2;
    bool pedal1_pressed, pedal2_pressed, service;
    int64_t audio_acc;
    int16_t audio[4096];
    int naudio;
} *S_;
#define S (*S_)

static void update_sound_irq(void) { m65xx_set_irq(&S.snd, (S.ym_irq || S.timed_irq) ? M65_ASSERT : M65_CLEAR); }

static uint8_t pal6(uint8_t v) { return (uint8_t)((v << 2) | (v >> 4)); }

static void set_palette(int index, uint16_t v)  // (one IRRRRRGG GGGBBBBB colour across the high bytes of two words)
{
    index &= 0x1ff;
    S.palette_ram[index] = v;
    const int ci = index >> 1;
    const uint16_t packed = (uint16_t)((S.palette_ram[ci * 2] & 0xff00) | (S.palette_ram[ci * 2 + 1] >> 8));
    const uint8_t in = (packed >> 15) & 1;
    const uint8_t r = pal6((uint8_t)(((packed >> 9) & 0x3e) | in)), g = pal6((uint8_t)(((packed >> 4) & 0x3e) | in));
    const uint8_t b = pal6((uint8_t)(((packed << 1) & 0x3e) | in));
    S.palette[ci] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint16_t main_read(void *u, uint32_t a)
{
    (void)u;
    if (a <= 0x3ffff) return S.main_rom[a >> 1];
    if (a >= 0xfc0000 && a <= 0xfc1fff) return (uint16_t)(0xfeff | (S.sound_pending ? 0x0100 : 0));
    if (a >= 0xfd0000 && a <= 0xfd1fff) return (uint16_t)(0xff00 | S.eeprom[(a & 0x1fff) >> 1]);
    if (a == 0xfe4000) return S.main_inputs;
    if (a == 0xfe6000) return (uint16_t)(0xff00 | S.steering1);
    if (a == 0xfe6002) return (uint16_t)(0xff00 | S.steering2);
    if (a == 0xfe6004) return S.pedal1;
    if (a == 0xfe6006) return S.pedal2;
    if (a >= 0xfea000 && a <= 0xfebfff) {
        S.main_pending = false;
        m68kd_set_irq(&S.cpu, 2, M68_CLEAR);
        return (uint16_t)(S.main_latch << 8);
    }
    if (a >= 0xffc000 && a <= 0xffc3ff) return S.palette_ram[(a & 0x3ff) >> 1];
    if (a >= 0xffe000) return S.ram[(a & 0x1fff) >> 1];
    return 0xffff;
}

static void main_write(void *u, uint32_t a, uint16_t v)
{
    (void)u;
    if (a <= 0x3ffff) return;
    if (a >= 0xfc0000 && a <= 0xfc1fff) {
        m65xx_reset(&S.snd);
        S.sound_bank = 0;
        ym2151_reset(&S.ym);
        S.sound_halted = false;
        return;
    }
    if (a >= 0xfd0000 && a <= 0xfd1fff) {
        if (S.eeprom_unlocked) {
            S.eeprom[(a & 0x1fff) >> 1] = (uint8_t)v;
            S.eeprom_unlocked = false;
        }
        return;
    }
    if (a >= 0xfe0000 && a <= 0xfe1fff) return;
    if (a >= 0xfe2000 && a <= 0xfe3fff) { m68kd_set_irq(&S.cpu, 1, M68_CLEAR); return; }
    if (a >= 0xfe8000 && a <= 0xfe9fff) {
        S.sound_latch = (uint8_t)(v >> 8);
        S.sound_pending = true;
        m65xx_set_nmi(&S.snd, M65_ASSERT);
        return;
    }
    if (a >= 0xfec000 && a <= 0xfedfff) { S.playfield_bank = v & 1; return; }
    if (a >= 0xfee000 && a <= 0xfeffff) { S.eeprom_unlocked = true; return; }
    if (a >= 0xffc000 && a <= 0xffc3ff) { set_palette((int)((a & 0x3ff) >> 1), v); return; }
    if (a >= 0xffe000) S.ram[(a & 0x1fff) >> 1] = v;
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff || a >= 0x4000) return S.sound_memory[a];
    if (a <= 0x27ff && (a & 1)) return ym2151_status(&S.ym);
    if (a >= 0x2800 && a <= 0x29ff) {
        switch (a & 6) {
        case 2:
            S.sound_pending = false;
            m65xx_set_nmi(&S.snd, M65_CLEAR);
            return S.sound_latch;
        case 4: return (uint8_t)(S.sound_inputs | 0x10 | (S.main_pending ? 0x20 : 0) | (!S.sound_pending ? 0x40 : 0));
        case 6:
            S.timed_irq = false;
            update_sound_irq();
            return 0xff;
        default: break;
        }
    }
    if (a >= 0x3000 && a <= 0x3fff) return S.sound_banks[S.sound_bank][a & 0x0fff];
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) { S.sound_memory[a] = v; return; }
    if (a >= 0x2000 && a <= 0x27ff) {
        if (a & 1) ym2151_write(&S.ym, v);
        else ym2151_select(&S.ym, v);
        return;
    }
    if (a >= 0x2800 && a <= 0x29ff && (a & 6) == 6) {
        S.timed_irq = false;
        update_sound_irq();
        return;
    }
    if (a >= 0x2a00 && a <= 0x2bff) {
        if ((a & 6) == 2) {
            S.main_latch = v;
            S.main_pending = true;
            m68kd_set_irq(&S.cpu, 2, M68_ASSERT);
        }
        if ((a & 6) == 4) {
            S.sound_bank = (uint8_t)((v >> 6) & 3);
            if ((v & 1) == 0) ym2151_reset(&S.ym);
        }
    }
}

static void sound_cycles(void *u, int cycles)
{
    (void)u;
    ym2151_run_timers(&S.ym, cycles * 2);
    S.audio_acc += (int64_t)cycles * RATE;
    while (S.audio_acc >= SOUND_CLOCK) {
        S.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&S.ym);
        if (S.naudio < 4096) S.audio[S.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on)
{
    (void)u;
    S.ym_irq = on;
    update_sound_irq();
}

// ---------------------------------------------------------------------------
// Video

static int identity(void *u, int v) { (void)u; return v; }
static const atari_mo_lookup kLookup = {identity, identity, NULL};

static void mo_tile(void *u, int code, int color, bool hf, bool vf, int x, int y, int gfx, int prio)
{
    (void)u;
    (void)gfx;
    (void)prio;
    const uint8_t *px = S.spr_gfx + (size_t)(code % 0xc00) * 128;
    for (int row = 0; row < 8; row++) {
        const int dy = y + row;
        if (dy < 0 || dy >= H) continue;
        const int sy = vf ? 7 - row : row;
        for (int col = 0; col < 16; col++) {
            const int dx = x + col;
            if (dx < 0 || dx >= W) continue;
            const uint8_t pen = px[sy * 16 + (hf ? 15 - col : col)];
            if (pen) S.fbuf[dy * W + dx] = S.palette[(color + pen) & 0xff];
        }
    }
}

static void update_video(void)
{
    for (int tile = 0; tile < 0x800; tile++) {
        const int tx = tile & 63, ty = tile >> 6;
        const uint16_t attr = S.ram[tile];
        int code = attr & 0x1fff;
        if (attr & 0x1000) code += S.playfield_bank << 12;
        const int base = ((attr >> 13) & 7) << 4;
        const uint8_t *px = S.pf_gfx + (size_t)(code % 0x3000) * 64;
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++) S.pf_pixels[(ty * 8 + row) * 512 + tx * 8 + col] = (uint16_t)(base + px[row * 8 + col]);
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint16_t p = S.pf_pixels[y * 512 + x];
            S.fbuf[y * W + x] = S.palette[(p & 8) ? 0 : p];
        }
    atari_mo_draw(&S.mo, &S.ram[0x800], 0, 0, 0, &kLookup, mo_tile, NULL);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint16_t p = S.pf_pixels[y * 512 + x];
            if (p & 8) S.fbuf[y * W + x] = S.palette[p];
        }
    atari_mo_draw(&S.mo, &S.ram[0x800], 0, 0, 1, &kLookup, mo_tile, NULL);
    for (int i = 0; i < W * H; i++) {
        const uint32_t c = S.fbuf[i];
        S.fb[i] = (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
    }
}

// ---------------------------------------------------------------------------

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    uint8_t *buf = (uint8_t *)calloc(1, 0x60000);
    if (!buf) return false;
    for (int i = 0; i < N(kMainRoms); i++) {
        arc_rom one = kMainRoms[i];
        one.offset = 0;
        if (!arc_load(&one, 1, buf, one.size)) return false;
        const bool high = (kMainRoms[i].offset & 1) == 0;
        for (uint32_t j = 0, k = kMainRoms[i].offset >> 1; j < one.size; j++, k++)
            S.main_rom[k] = high ? (uint16_t)((buf[j] << 8) | (S.main_rom[k] & 0xff)) : (uint16_t)((S.main_rom[k] & 0xff00) | buf[j]);
    }
    if (!arc_load(kSoundRoms, 1, buf, 0x10000)) return false;
    memcpy(S.sound_memory + 0x4000, buf + 0x4000, 0xc000);
    for (int b = 0; b < 4; b++) memcpy(S.sound_banks[b], buf + b * 0x1000, 0x1000);
    static const int kX8[8] = {0, 4, 8, 12, 16, 20, 24, 28}, kY8[8] = {0, 32, 64, 96, 128, 160, 192, 224};
    static const int kX16[16] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};
    static const int kY16[8] = {0, 64, 128, 192, 256, 320, 384, 448};
    const arc_layout pl = {8, 8, 0x3000, 4, 32 * 8, false, false, false, {0, 1, 2, 3}, kX8, kY8};
    const arc_layout sl = {16, 8, 0xc00, 4, 64 * 8, false, false, false, {0, 1, 2, 3}, kX16, kY16};
    if (!arc_load(kPlayfieldRoms, N(kPlayfieldRoms), buf, 0x60000)) return false;
    for (int i = 0; i < 0x60000; i++) buf[i] = (uint8_t)~buf[i];
    if (!(S.pf_gfx = arc_gfx_decode(&pl, buf, 0x60000))) return false;
    if (!arc_load(kSpriteRoms, N(kSpriteRoms), buf, 0x30000)) return false;
    for (int i = 0; i < 0x30000; i++) buf[i] = (uint8_t)~buf[i];
    if (!(S.spr_gfx = arc_gfx_decode(&sl, buf, 0x30000))) return false;
    free(buf);
    memset(S.eeprom, 0xff, sizeof S.eeprom);

    m68kd_init(&S.cpu, MAIN_CLOCK, M68K_T_68000);
    S.cpu.read_word = main_read;
    S.cpu.write_word = main_write;
    S.snd.read = sound_read;
    S.snd.write = sound_write;
    S.snd.cycles = sound_cycles;
    ym2151_init(&S.ym, YM_CLOCK, RATE);
    S.ym.irq_handler = ym_irq;
    static const atari_mo_config cfg = {
        .tile_width = 16, .tile_height = 8, .bankcount = 1, .linked = false, .split = true, .reverse = false,
        .slipheight = 0, .maxperline = 0, .palettebase = 0x80,
        .link_entry = {0, 0, 0, 0x003f},
        .code_entry = {{0x0fff, 0, 0, 0}, {0, 0, 0, 0}},
        .color_entry = {{0, 0, 0, 0x0007}, {0, 0, 0, 0}},
        .xpos_entry = {0, 0, 0, 0xff80}, .ypos_entry = {0, 0xff80, 0, 0},
        .height_entry = {0, 0x000f, 0, 0}, .priority_entry = {0, 0, 0, 0x0008}};
    atari_mo_init(&S.mo, &cfg, W + 8, H + 8);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    m68kd_reset(&S.cpu);
    m65xx_reset(&S.snd);
    ym2151_reset(&S.ym);
    memset(S.ram, 0, sizeof S.ram);
    memset(S.palette_ram, 0, sizeof S.palette_ram);
    for (int i = 0; i < 0x100; i++) S.palette[i] = 0xff000000u;
    memset(S.pf_pixels, 0, sizeof S.pf_pixels);
    memset(S.fb, 0, sizeof S.fb);
    S.sound_bank = S.playfield_bank = S.sound_latch = S.main_latch = 0;
    S.eeprom_unlocked = S.main_pending = S.sound_pending = false;
    S.sound_halted = S.ym_irq = S.timed_irq = false;
    S.main_inputs = 0xffbf;
    S.sound_inputs = 0;
    S.pedal1 = S.pedal2 = S.steering1 = S.steering2 = 0x80;
    S.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int mc = (int)((double)MAIN_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    const int sc = (int)((double)SOUND_CLOCK / FPS / (SCANLINES * CPU_SYNC) + 0.5);
    S.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) {
            S.main_inputs &= (uint16_t)~0x40;
            S.timed_irq = true;
            update_sound_irq();
        }
        if (line == 64 || line == 128 || line == 192) {
            S.timed_irq = true;
            update_sound_irq();
        }
        if (line == 240) {
            update_video();
            m68kd_set_irq(&S.cpu, 1, M68_ASSERT);
            S.main_inputs |= 0x40;
        }
        for (int step = 0; step < CPU_SYNC; step++) {
            m68kd_run(&S.cpu, mc);
            if (!S.sound_halted) m65xx_run(&S.snd, sc);
        }
    }
    if (!S.pedal1_pressed) S.pedal1--;
    if (!S.pedal2_pressed) S.pedal2--;
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in)
{
    S.main_inputs = S.service ? 0xff3f : 0xffbf;
    if (in->p[0] & AP_B1) S.main_inputs &= (uint16_t)~0x10;
    if (in->p[1] & AP_B1) S.main_inputs &= (uint16_t)~0x20;
    S.sound_inputs = (uint8_t)((in->coin1 ? 1 : 0) | (in->coin2 ? 2 : 0));
    S.pedal1_pressed = (in->p[0] & AP_B2) != 0;
    S.pedal2_pressed = (in->p[1] & AP_B2) != 0;
    if (in->p[0] & AP_LEFT) S.steering1 = (uint8_t)(S.steering1 - 4);
    if (in->p[0] & AP_RIGHT) S.steering1 = (uint8_t)(S.steering1 + 4);
    if (in->p[1] & AP_LEFT) S.steering2 = (uint8_t)(S.steering2 - 4);
    if (in->p[1] & AP_RIGHT) S.steering2 = (uint8_t)(S.steering2 + 4);
}

void arc_dip(int bank, uint8_t v) { if (bank == 0) S.service = (v & 1) != 0; }
