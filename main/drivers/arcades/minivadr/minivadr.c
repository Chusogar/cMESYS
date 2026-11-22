// minivadr.c - Mini Vaders (Taito, 1990) for the common arcade frontends
// (arc.h). dsp-cpp's driver is a stub (no picture, no inputs), so this one
// follows MAME's minivadr.cpp: a Z80 at 4 MHz, its program at 0000-1fff, a
// 256x256 one-bit bitmap at a000-bfff (rows 16-239 shown), the inputs at
// e008, an IRQ at each VBLANK, no sound.
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/z80dsp/z80dsp.h"

#define CLOCK (24000000 / 6)
#define FPS 60.0
#define SCANLINES 256
#define W 256
#define H 224

static const char *const kSets[] = { "minivadr", NULL };
static const arc_rom kRom[] = {{"d26-01.ic7|d26-01.bin", 0x2000, 0x0000, 0xa96c823d}};
const arc_info arc_game = { "minivadr", "Mini Vaders", "Taito", 1990, W, H, FPS, 0, kSets,
                            { "FIRE", NULL, NULL, NULL }, ARC_JOY_2WAY };

static struct {
    zd_z80 cpu;
    uint8_t rom[0x2000], vram[0x2000];
    uint16_t *fb;                   // (on the heap)
    uint8_t in;
} V;

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x2000) return V.rom[a];
    if (a >= 0xa000 && a < 0xc000) return V.vram[a - 0xa000];
    if (a == 0xe008) return V.in;
    return 0xff;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a >= 0xa000 && a < 0xc000) V.vram[a - 0xa000] = v;
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

bool arc_init(void)
{
    V.fb = (uint16_t *)calloc(W * H, 2);
    if (!V.fb || !ARC_LOAD(kRom, V.rom, sizeof V.rom)) return false;
    V.cpu.read = rd;
    V.cpu.write = wr;
    V.cpu.in = in_ff;
    V.cpu.out = out_nop;
    zd_init(&V.cpu);
    arc_reset();
    return true;
}

void arc_reset(void)
{
    memset(V.vram, 0, sizeof V.vram);
    V.in = 0xff;
    zd_reset(&V.cpu);
}

int arc_run_frame(const int16_t **audio)
{
    const int per_line = (int)(CLOCK / FPS / SCANLINES);
    for (int line = 0; line < SCANLINES; line++) {
        if (line == 240) zd_set_irq(&V.cpu, ZD_HOLD, 0xff);
        zd_run(&V.cpu, per_line);
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x += 8) {
            const uint8_t b = V.vram[(y + 16) * 32 + x / 8];
            for (int i = 0; i < 8; i++) V.fb[y * W + x + i] = (b & (0x80 >> i)) ? 0xffff : 0;
        }
    if (audio) *audio = NULL;
    return 0;
}

const uint16_t *arc_frame(void) { return V.fb; }

void arc_input(const arc_in *in)
{
    V.in = 0xff;
    if (in->p[0] & AP_LEFT) V.in &= ~0x01;
    if (in->p[0] & AP_RIGHT) V.in &= ~0x02;
    if (in->p[0] & AP_B1) V.in &= ~0x04;
    if (in->p[0] & AP_START) V.in &= ~0x08;
    if (in->coin1) V.in &= ~0x10;
}

void arc_dip(int bank, uint8_t v) { (void)bank, (void)v; }
