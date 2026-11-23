// blockout.c - Block Out (Technos Japan, 1989) for the common arcade
// frontends (arc.h): C port of dsp-cpp's drivers/arcade/blockout (from
// MAME's blockout.cpp). A 68000 at 10 MHz (Musashi here) drawing into two
// 512x256 8-bit bitmaps (front over back) and a 1-bit overlay, a Z80 with a
// YM2151 and an OKI M6295.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m68k/m68k.h"
#include "../../../cpu/z80dsp/z80dsp.h"
#include "../../../sound/ym2151.h"
#include "../../../sound/okim6295dsp.h"

#define MAIN_CLOCK 10000000
#define SOUND_CLOCK 3579545
#define OKI_CLOCK 1056000
#define SCANLINES 272
#define FIRST_LINE 10
#define FPS (28000000.0 / 4.0 / 448.0 / 272.0)
#define RATE 44100
#define W 320
#define H 240

static const char *const kSets[] = { "blockout", NULL };
static const arc_rom kEven[] = {{"bo29a0-2.bin", 0x20000, 0, 0xb0103427}};
static const arc_rom kOdd[] = {{"bo29a1-2.bin", 0x20000, 0, 0x5984d5a2}};
static const arc_rom kSound[] = {{"bo29e3-0.bin", 0x8000, 0, 0x3ea01f78}};
static const arc_rom kOki[] = {{"bo29e2-0.bin", 0x20000, 0, 0x15c5a99d}};

const arc_info arc_game = { "blockout", "Block Out", "Technos Japan", 1989, W, H, FPS, RATE, kSets,
                            { "A", "B", "C", "DROP" }, ARC_JOY_8WAY };

// the machine on the heap
static struct state {
    zd_z80 sound;
    ym2151 ym;
    okim6295d oki;
    uint8_t *rom, *videoram, *oki_rom;   // 256 KB each (the OKI's half used)
    uint16_t *bitmap;                    // 512x256 pens: front, or 0x100 | back
    uint8_t ram1[0xc000], ram2[0xc000], front_ram[0x8000], ram3[0x18000], pal_ram[0x400];
    uint16_t front_color, palette[513];
    uint8_t sound_rom[0x8000], sound_ram[0x800];
    uint16_t *fb;
    uint8_t soundlatch, p1, p2, system, dsw1, dsw2, buttons_a;
    uint8_t irq_pending;                 // bit n: 68000 IRQ level n asserted
    double main_debt, sound_debt;
    int64_t audio_acc, oki_acc;
    int32_t last_oki;
    int16_t audio[1024];
    int naudio;
} *B_;
#define B (*B_)

static void update_irq(void)
{
    m68k_set_irq((B.irq_pending & 0x40) ? 6 : (B.irq_pending & 0x20) ? 5 : 0);
}

// Musashi callbacks (m68kconf.h)
void cpu_pulse_reset(void) {}
void cpu_instr_callback(unsigned int pc) { (void)pc; }
void cpu_set_fc(int fc) { (void)fc; }
int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }   // asserted lines

static uint8_t gun(unsigned n) { return (uint8_t)(0x0e * (n & 1) + 0x1f * (n >> 1 & 1) + 0x43 * (n >> 2 & 1) + 0x8f * (n >> 3 & 1)); }
static void set_color(int i, uint16_t raw)     // xBGR 444
{
    B.palette[i] = arc_rgb565((uint32_t)gun(raw & 15) << 16 | (uint32_t)gun(raw >> 4 & 15) << 8 | gun(raw >> 8 & 15));
}

static uint8_t read8(uint32_t a)
{
    a &= 0xffffff;
    if (a < 0x40000) return B.rom[a];
    if (a >= 0x100000 && a < 0x10000a) {
        if (a & 1) switch (a & 0xe) {
            case 0x0: return B.p1;
            case 0x2: return B.p2;
            case 0x4: return B.system;
            case 0x6: return B.dsw1;
            case 0x8: return (uint8_t)(B.dsw2 & (0x3f | B.buttons_a));
            }
        return 0xff;
    }
    if (a >= 0x180000 && a < 0x1c0000) return B.videoram[a & 0x3ffff];
    if (a >= 0x1d4000 && a < 0x1e0000) return B.ram1[a - 0x1d4000];
    if (a >= 0x1f4000 && a < 0x200000) return B.ram2[a - 0x1f4000];
    if (a >= 0x200000 && a < 0x208000) return B.front_ram[a & 0x7fff];
    if (a >= 0x208000 && a < 0x220000) return B.ram3[a - 0x208000];
    if (a >= 0x280200 && a < 0x280600) return B.pal_ram[a - 0x280200];
    return 0xff;
}

static void write8(uint32_t a, uint8_t v)
{
    a &= 0xffffff;
    if (a < 0x40000) return;
    if (a >= 0x100010 && a < 0x100018) {
        switch (a) {
        case 0x100010: case 0x100011: B.irq_pending &= ~0x40; update_irq(); break;
        case 0x100012: case 0x100013: B.irq_pending &= ~0x20; update_irq(); break;
        case 0x100015:
            B.soundlatch = v;
            zd_set_nmi(&B.sound, ZD_PULSE);
            break;
        default: break;
        }
    } else if (a >= 0x180000 && a < 0x1c0000) {
        const uint32_t o = a & 0x3ffff;
        if (B.videoram[o] == v) return;
        B.videoram[o] = v;
        const uint32_t xy = o & 0x1ffff;
        const uint8_t front = B.videoram[xy], back = B.videoram[0x20000 | xy];
        B.bitmap[xy] = front ? front : (uint16_t)(0x100 | back);
    } else if (a >= 0x1d4000 && a < 0x1e0000) B.ram1[a - 0x1d4000] = v;
    else if (a >= 0x1f4000 && a < 0x200000) B.ram2[a - 0x1f4000] = v;
    else if (a >= 0x200000 && a < 0x208000) B.front_ram[a & 0x7fff] = v;
    else if (a >= 0x208000 && a < 0x220000) B.ram3[a - 0x208000] = v;
    else if (a == 0x280002 || a == 0x280003) {
        B.front_color = (a & 1) ? (uint16_t)((B.front_color & 0xff00) | v) : (uint16_t)((B.front_color & 0x00ff) | (v << 8));
        set_color(512, B.front_color);
    } else if (a >= 0x280200 && a < 0x280600) {
        const uint32_t o = a - 0x280200;
        B.pal_ram[o] = v;
        const uint32_t w = o & ~1u;
        set_color((int)(w >> 1), (uint16_t)(B.pal_ram[w] << 8 | B.pal_ram[w + 1]));
    }
}

unsigned int m68k_read_memory_8(unsigned int a) { return read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return (unsigned)(read8(a) << 8 | read8(a + 1)); }
unsigned int m68k_read_memory_32(unsigned int a) { return m68k_read_memory_16(a) << 16 | m68k_read_memory_16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { write8(a, (uint8_t)v); }
void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    write8(a, (uint8_t)(v >> 8));
    write8(a + 1, (uint8_t)v);
}
void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    m68k_write_memory_16(a, v >> 16);
    m68k_write_memory_16(a + 2, v & 0xffff);
}

static uint8_t sound_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return B.sound_rom[a];
    if (a < 0x8800) return B.sound_ram[a & 0x7ff];
    if (a == 0x8801) return ym2151_status(&B.ym);
    if (a == 0x9800) return okid_read(&B.oki);
    if (a == 0xa000) return B.soundlatch;
    return 0xff;
}

static void sound_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0x8000 && a < 0x8800) B.sound_ram[a & 0x7ff] = v;
    else if (a == 0x8800) ym2151_select(&B.ym, v);
    else if (a == 0x8801) ym2151_write(&B.ym, v);
    else if (a == 0x9800) okid_write(&B.oki, v);
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void sound_cycles(void *u, int c)
{
    (void)u;
    ym2151_run_timers(&B.ym, c);
    B.oki_acc += (int64_t)c * okid_sample_frequency(&B.oki);
    while (B.oki_acc >= SOUND_CLOCK) {
        B.oki_acc -= SOUND_CLOCK;
        B.last_oki = okid_update(&B.oki);
    }
    B.audio_acc += (int64_t)c * RATE;
    while (B.audio_acc >= SOUND_CLOCK) {
        B.audio_acc -= SOUND_CLOCK;
        const int32_t s = ym2151_update(&B.ym) * 9 / 10 + B.last_oki * 3 / 4;
        if (B.naudio < 1024) B.audio[B.naudio++] = arc_clamp16(s);
    }
}

static void ym_irq(void *u, bool on) { (void)u; zd_set_irq(&B.sound, on ? ZD_ASSERT : ZD_CLEAR, 0xff); }

static void update_video(void)
{
    const uint16_t overlay = B.palette[512];
    for (int y = 0; y < H; y++) {
        const int sy = y + FIRST_LINE;
        const uint16_t *src = B.bitmap + sy * 512;
        const uint8_t *front = B.front_ram + (sy & 0xff) * 128;
        uint16_t *dst = B.fb + y * W;
        for (int x = 0; x < W; x++) {
            const uint8_t bits = front[(x >> 3) * 2 + 1];   // 8 pixels, MSB first
            dst[x] = (bits >> (7 - (x & 7)) & 1) ? overlay : B.palette[src[x]];
        }
    }
}

bool arc_init(void)
{
    if (!(B_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    B.rom = (uint8_t *)calloc(1, 0x40000);
    B.videoram = (uint8_t *)calloc(1, 0x40000);
    B.oki_rom = (uint8_t *)calloc(1, 0x40000);
    B.bitmap = (uint16_t *)malloc(512 * 256 * 2);
    B.fb = (uint16_t *)calloc(W * H, 2);
    uint8_t *half = (uint8_t *)malloc(0x20000);
    if (!B.rom || !B.videoram || !B.oki_rom || !B.bitmap || !B.fb || !half) return false;
    if (!ARC_LOAD(kEven, half, 0x20000)) return false;
    for (int i = 0; i < 0x20000; i++) B.rom[i * 2] = half[i];
    if (!ARC_LOAD(kOdd, half, 0x20000)) return false;
    for (int i = 0; i < 0x20000; i++) B.rom[i * 2 + 1] = half[i];
    free(half);
    if (!ARC_LOAD(kSound, B.sound_rom, 0x8000) || !ARC_LOAD(kOki, B.oki_rom, 0x40000)) return false;
    B.sound.read = sound_read;
    B.sound.write = sound_write;
    B.sound.in = in_ff;
    B.sound.out = out_nop;
    B.sound.cycles = sound_cycles;
    zd_init(&B.sound);
    ym2151_init(&B.ym, SOUND_CLOCK, RATE);
    B.ym.irq_handler = ym_irq;
    okid_init(&B.oki, OKI_CLOCK, true, B.oki_rom, 0x40000);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    B.p1 = B.p2 = B.system = 0xff;
    B.dsw1 = B.dsw2 = 0xff;
    B.buttons_a = 0xc0;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(B.videoram, 0, 0x40000);
    for (int i = 0; i < 512 * 256; i++) B.bitmap[i] = 0x100;
    memset(B.ram1, 0, sizeof B.ram1);
    memset(B.ram2, 0, sizeof B.ram2);
    memset(B.front_ram, 0, sizeof B.front_ram);
    memset(B.ram3, 0, sizeof B.ram3);
    memset(B.pal_ram, 0, sizeof B.pal_ram);
    for (int i = 0; i < 513; i++) B.palette[i] = 0;
    B.front_color = 0;
    memset(B.sound_ram, 0, sizeof B.sound_ram);
    B.soundlatch = 0;
    B.irq_pending = 0;
    B.main_debt = B.sound_debt = 0;
    B.audio_acc = B.oki_acc = 0;
    B.last_oki = 0;
    m68k_pulse_reset();
    m68k_set_irq(0);
    zd_reset(&B.sound);
    ym2151_reset(&B.ym);
    okid_reset(&B.oki);
}

int arc_run_frame(const int16_t **audio)
{
    const double main_cycles = MAIN_CLOCK / FPS / SCANLINES, sound_cycles_ = SOUND_CLOCK / FPS / SCANLINES;
    B.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 0) { B.irq_pending |= 0x20; update_irq(); }
        if (line == 250) {              // the end of the visible area
            B.irq_pending |= 0x40;
            update_irq();
            update_video();
        }
        B.main_debt += main_cycles;
        if ((int)B.main_debt > 0) B.main_debt -= m68k_execute((int)B.main_debt);
        B.sound_debt += sound_cycles_;
        B.sound_debt -= zd_run(&B.sound, (int)B.sound_debt);
    }
    if (audio) *audio = B.audio;
    return B.naudio;
}

const uint16_t *arc_frame(void) { return B.fb; }

static uint8_t pad(unsigned p)
{
    uint8_t v = 0xff;
    if (p & AP_RIGHT) v &= ~0x01;
    if (p & AP_LEFT) v &= ~0x02;
    if (p & AP_UP) v &= ~0x04;
    if (p & AP_DOWN) v &= ~0x08;
    if (p & AP_B4) v &= ~0x10;          // drop (on top of the stick)
    if (p & AP_B2) v &= ~0x20;
    if (p & AP_B3) v &= ~0x40;
    if (p & AP_START) v &= ~0x80;
    return v;
}

void arc_input(const arc_in *in)
{
    B.p1 = pad(in->p[0]);
    B.p2 = pad(in->p[1]);
    // button A of each player is wired to the unused DSW2 switches 7 and 8
    B.buttons_a = 0xc0;
    if (in->p[0] & AP_B1) B.buttons_a &= ~0x40;
    if (in->p[1] & AP_B1) B.buttons_a &= ~0x80;
    uint8_t s = 0xff;
    if (in->coin1) s &= ~0x02;
    if (in->coin2) s &= ~0x04;
    B.system = s;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) B.dsw1 = v;
    else if (bank == 1) B.dsw2 = v;
}
