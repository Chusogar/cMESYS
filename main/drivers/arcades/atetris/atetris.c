// atetris.c - Tetris (Atari, 1988) for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/tetris_atari_hw (from dsp-emulator's
// tetris_atari_hw.pas). A 6502 at 1.79 MHz with a SLAPSTIC (101) switching
// the two ROM banks at 4000, two POKEYs (the inputs on their ALLPOT) and a
// 336x240 map of 8x8 4bpp characters.
#include <stdlib.h>
#include <string.h>
#include "../common/arc.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../machine/slapstic.h"
#include "../../../sound/pokey.h"

#define CLOCK 1789772
#define FPS 59.922743
#define SCANLINES 262
#define RATE 44100
#define W 336
#define H 240

static const char *const kSets[] = { "atetris", NULL };
static const arc_rom kMain[] = {{"136066-1100.45f", 0x10000, 0, 0x2acbdb09}};
static const arc_rom kChars[] = {{"136066-1101.35a", 0x10000, 0, 0x84a1939f}};

const arc_info arc_game = { "atetris", "Tetris", "Atari Games", 1988, W, H, FPS, RATE, kSets,
                            { "ROTATE", NULL, NULL, NULL }, ARC_JOY_4WAY };

// the machine on the heap (calloc in arc_init: the ESP32's DRAM is short)
static struct state {
    m65xx cpu;
    slapstic slap;
    pokey pk0, pk1;
    uint8_t *rom, *chars;               // 64 KB each, the characters a nibble per pixel
    uint8_t ram[0x2000], nv_ram[0x200], pal_ram[0x100];
    uint16_t palette[0x100];
    uint16_t *fb;
    uint8_t rom_bank, in0, in1, dsw;
    bool nvram_we;
    int pokey_pending;
    int64_t audio_acc;
    int16_t audio[1024];
    int naudio;
} *T_;
#define T (*T_)

static uint8_t pal3(uint8_t b) { b &= 7; return (uint8_t)((b << 5) | (b << 2) | (b >> 1)); }
static uint8_t pal2(uint8_t b) { b &= 3; return (uint8_t)((b << 6) | (b << 4) | (b << 2) | b); }

static void set_palette(int i, uint8_t v)
{
    T.palette[i] = arc_rgb565((uint32_t)pal3(v >> 5) << 16 | (uint32_t)pal3((v >> 2) & 7) << 8 | pal2(v & 3));
}

// the POKEYs run in batches: up to the CPU's next access to them, or to the
// end of the instruction on which a sample is due
static void sync_pokeys(void)
{
    if (T.pokey_pending > 0) {
        pokey_run(&T.pk0, T.pokey_pending);
        pokey_run(&T.pk1, T.pokey_pending);
        T.pokey_pending = 0;
    }
}

static uint8_t rd(void *u, uint16_t a)
{
    (void)u;
    if (a <= 0x1fff) return T.ram[a];
    if (a <= 0x23ff) return T.pal_ram[a & 0xff];
    if (a <= 0x27ff) return T.nv_ram[a & 0x1ff];
    if (a <= 0x2bff) {
        sync_pokeys();
        return (a & 0x1f) < 0x10 ? pokey_read(&T.pk0, a & 0x0f) : pokey_read(&T.pk1, a & 0x0f);
    }
    if (a >= 0x4000 && a <= 0x5fff) return T.rom[(T.rom_bank & 1) * 0x4000 + (a & 0x3fff)];
    if (a >= 0x6000 && a <= 0x7fff) {
        const uint8_t r = T.rom[(T.rom_bank & 1) * 0x4000 + (a & 0x3fff)];
        T.rom_bank = slapstic_tweak(&T.slap, a & 0x1fff) & 1;
        return r;
    }
    if (a >= 0x8000) return T.rom[a];
    return 0;
}

static void wr(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a <= 0x1fff) T.ram[a] = v;
    else if (a <= 0x23ff) {
        const int i = a & 0xff;
        if (T.pal_ram[i] != v) {
            T.pal_ram[i] = v;
            set_palette(i, v);
        }
    } else if (a <= 0x27ff) {
        if (T.nvram_we) T.nv_ram[a & 0x1ff] = v;
        T.nvram_we = false;
    } else if (a <= 0x2bff) {
        sync_pokeys();
        if ((a & 0x1f) < 0x10) pokey_write(&T.pk0, a & 0x0f, v);
        else pokey_write(&T.pk1, a & 0x0f, v);
    } else if (a >= 0x3400 && a <= 0x37ff) T.nvram_we = true;
    else if (a >= 0x3800 && a <= 0x3bff) m65xx_set_irq(&T.cpu, M65_CLEAR);
}

static void cycles(void *u, int c)
{
    (void)u;
    T.pokey_pending += c;
    T.audio_acc += (int64_t)c * RATE;
    if (T.audio_acc >= CLOCK) sync_pokeys();
    while (T.audio_acc >= CLOCK) {
        T.audio_acc -= CLOCK;
        const int32_t s = pokey_update(&T.pk0) + pokey_update(&T.pk1);
        if (T.naudio < 1024) T.audio[T.naudio++] = arc_clamp16(s);
    }
}

static uint8_t allpot0(void *u) { (void)u; return (uint8_t)(T.in0 | T.dsw); }
static uint8_t allpot1(void *u) { (void)u; return T.in1; }

static void update_video(void)
{
    for (int y = 0; y < H; y++) {
        const uint8_t *tiles = T.ram + 0x1000 + (y >> 3) * 128;
        uint16_t *dst = T.fb + y * W;
        for (int x = 0; x < W / 8; x++, dst += 8) {
            const uint8_t attr = tiles[x * 2 + 1];
            const unsigned code = tiles[x * 2] | ((unsigned)(attr & 7) << 8);
            const uint16_t *pal = T.palette + (attr & 0xf0);
            const uint8_t *bits = T.chars + code * 32 + (y & 7) * 4;
            for (int i = 0; i < 4; i++) {
                dst[i * 2] = pal[bits[i] >> 4];
                dst[i * 2 + 1] = pal[bits[i] & 0x0f];
            }
        }
    }
}

bool arc_init(void)
{
    if (!(T_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    T.rom = (uint8_t *)malloc(0x10000);
    T.chars = (uint8_t *)malloc(0x10000);
    T.fb = (uint16_t *)calloc(W * H, 2);
    if (!T.rom || !T.chars || !T.fb) return false;
    if (!ARC_LOAD(kMain, T.rom, 0x10000) || !ARC_LOAD(kChars, T.chars, 0x10000)) return false;
    memset(T.nv_ram, 0xff, sizeof T.nv_ram);
    slapstic_init(&T.slap, 101, NULL);
    T.cpu.read = rd;
    T.cpu.write = wr;
    T.cpu.cycles = cycles;
    pokey_init(&T.pk0);
    pokey_init(&T.pk1);
    T.pk0.allpot_read = allpot0;
    T.pk1.allpot_read = allpot1;
    T.dsw = 0;
    arc_reset();
    return true;
}

void arc_reset(void)
{
    slapstic_reset(&T.slap);
    T.rom_bank = T.slap.current_bank & 1;
    m65xx_reset(&T.cpu);
    pokey_reset(&T.pk0);
    pokey_reset(&T.pk1);
    T.pokey_pending = 0;
    T.in0 = 0x40;
    T.in1 = 0;
    T.nvram_we = false;
    T.audio_acc = 0;
}

int arc_run_frame(const int16_t **audio)
{
    const int line_cycles = (int)((double)CLOCK / FPS / SCANLINES + 0.5);
    T.naudio = 0;
    for (int line = 0; line < SCANLINES; line++) {
        switch (line) {
        case 0: T.in0 |= 0x40; break;
        case 48: case 112: case 176: m65xx_set_irq(&T.cpu, M65_ASSERT); break;
        case 240:
            update_video();
            T.in0 &= (uint8_t)~0x40;
            m65xx_set_irq(&T.cpu, M65_ASSERT);
            break;
        default: break;
        }
        m65xx_run(&T.cpu, line_cycles);
    }
    if (audio) *audio = T.audio;
    return T.naudio;
}

const uint16_t *arc_frame(void) { return T.fb; }

void arc_input(const arc_in *in)
{
    uint8_t a = 0x40, b = 0;           // (bit 6, the vblank, set again as dsp-cpp does)
    if (in->coin1) a |= 0x02;
    if (in->coin2) a |= 0x01;
    const unsigned p1 = in->p[0], p2 = in->p[1];
    if (p1 & AP_B1) b |= 0x01;
    if (p1 & AP_DOWN) b |= 0x02;
    if (p1 & AP_RIGHT) b |= 0x04;
    if (p1 & AP_LEFT) b |= 0x08;
    if (p2 & AP_B1) b |= 0x10;
    if (p2 & AP_DOWN) b |= 0x20;
    if (p2 & AP_RIGHT) b |= 0x40;
    if (p2 & AP_LEFT) b |= 0x80;
    T.in0 = a;
    T.in1 = b;
}

void arc_dip(int bank, uint8_t v)
{
    if (bank == 0) T.dsw = v;
}
