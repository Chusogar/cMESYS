// zx81_core.c - Sinclair ZX81 / Timex TS1000 (PAL) and ZX80: C port of
// dsp-cpp's drivers/computers/zx81 (dspc.h). A Z80 at 3.25 MHz (z80dsp),
// 207 T-states a line and 311 lines; the ZX81's 8 KB ROM and NMI generator
// (the ZX80's 4 KB ROM, none), 16 KB of RAM at $4000 (mirrored at $C000), the
// ULA taking the display file's characters from M1 fetches above $8000. The
// picture is drawn as dsp-cpp does: the ULA's characters as they come, then
// the whole display file at the end of the frame. Media: .p programs
// (injected 100 frames after the start, as dsp-cpp). No sound.
// DSPC_MODEL: 0 ZX81, 1 ZX80.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/dspc.h"
#include "../common/dspc_rom.h"
#include "../../../cpu/z80dsp/z80dsp.h"

#ifndef DSPC_MODEL
#define DSPC_MODEL 0
#endif
#define ZX80 (DSPC_MODEL == 1)

#define CLOCK 3250000
#define T_LINE 207
#define LINES 311
#define T_FRAME (T_LINE * LINES)
#define SW 384
#define SH 311
#define PAPER_X 64
#define PAPER_Y 56
#define RATE 44100
#define P_INJECT_FRAMES 100
#define BLACK 0xff000000u
#define WHITE 0xffffffffu

static const char *const kExts[] = { ".p", NULL };
static const char *const kNoExts[] = { NULL };
#if ZX80
const dspc_info dspc_machine = { "zx80", "Sinclair ZX80", SW, SH, 1, (double)CLOCK / T_FRAME, RATE, "", kNoExts, "", "" };
#else
const dspc_info dspc_machine = { "zx81", "Sinclair ZX81", SW, SH, 1, (double)CLOCK / T_FRAME, RATE, "P", kExts,
                                 "*.p *.P", ".p" };
#endif

static struct {
    zd_z80 cpu;
    uint8_t rom[0x2000], ram[0x4000];
    uint32_t fb[SW * SH];
    uint8_t keys[8];
    uint8_t prev_refresh;
    uint16_t ula_char;
    bool nmi_generator, nmi_on, vsync_active;
    int line_t, scanline, frame_t;
    uint64_t total_cycles;
    uint8_t *pending_p;
    size_t pending_n;
    int boot_frames;
    int16_t audio[1024];
} S;

static inline uint16_t rom_mask(void) { return ZX80 ? 0x0fff : 0x1fff; }
static inline uint16_t charset_base(void) { return ZX80 ? 0x0e00 : 0x1e00; }

static uint8_t mem_read(uint16_t a)
{
    if (a < 0x4000) return S.rom[a & rom_mask()];
    if (a < 0x8000) return S.ram[a & 0x3fff];
    if (a >= 0xc000) return S.ram[a & 0x3fff];
    return 0xff;
}

static uint8_t z_read(void *u, uint16_t a) { (void)u; return mem_read(a); }

static void z_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x4000) return;
    if (a < 0x8000) {
        S.ram[a & 0x3fff] = v;
        return;
    }
    if (a >= 0xc000) S.ram[a & 0x3fff] = v;
}

static uint8_t z_opcode(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x8000) return mem_read(a);
    const uint8_t cdata = mem_read((uint16_t)(a & 0x7fff));
    if (S.cpu.halted) return cdata;
    if (cdata & 0x40) return cdata;
    S.ula_char = cdata;
    return 0x00;
}

static uint8_t z_in(void *u, uint16_t port)
{
    (void)u;
    uint8_t data = 0xff;
    if (port & 1) return data;
    for (int i = 0; i < 8; ++i)
        if ((port & (0x0100u << i)) == 0) data &= S.keys[i];
    data |= 0xc0;
    if (!S.vsync_active && !S.nmi_generator) S.vsync_active = true;
    return data;
}

static void z_out(void *u, uint16_t port, uint8_t v)
{
    (void)u;
    (void)v;
    if (!ZX80) {
        if ((port & 1) == 0 && !S.nmi_generator) {
            S.nmi_generator = true;
            S.nmi_on = S.line_t >= 192;
            zd_set_nmi(&S.cpu, S.nmi_on ? ZD_ASSERT : ZD_CLEAR);
        }
        if ((port & 2) == 0 && S.nmi_generator) {
            S.nmi_generator = false;
            if (S.nmi_on) {
                zd_set_nmi(&S.cpu, ZD_CLEAR);
                S.nmi_on = false;
            }
        }
    }
    if (S.vsync_active) {
        S.vsync_active = false;
        if (S.frame_t > 1000) {
            S.scanline = 0;
            S.line_t = 0;
        }
    }
}

static void z_cycles(void *u, int cycles)
{
    (void)u;
    const uint8_t refresh = S.cpu.r;
    if ((refresh ^ S.prev_refresh) & 0x40) zd_set_irq(&S.cpu, (refresh & 0x40) ? ZD_CLEAR : ZD_ASSERT, 0xff);
    S.prev_refresh = refresh;
    if (S.ula_char != 0xffff) {
        const int x = 2 * S.line_t, y = S.scanline;
        if (x >= 0 && x + 8 <= SW && y >= 0 && y < SH) {
            const uint16_t glyph = (uint16_t)((((uint16_t)S.cpu.i << 8) & 0xfe00) | ((S.ula_char & 0x3f) << 3) | (S.scanline & 7));
            uint8_t pixels = mem_read(glyph);
            if (S.ula_char & 0x80) pixels = (uint8_t)~pixels;
            uint32_t *dest = &S.fb[(size_t)y * SW + (size_t)x];
            for (int i = 0; i < 8; ++i) dest[i] = (pixels & (0x80 >> i)) ? BLACK : WHITE;
        }
        S.ula_char = 0xffff;
    }
    for (int n = 0; n < cycles; ++n) {
        ++S.line_t;
        ++S.frame_t;
        ++S.total_cycles;
        if (S.line_t == 192 && S.nmi_generator) {
            S.nmi_on = true;
            zd_set_nmi(&S.cpu, ZD_ASSERT);
        }
        if (S.line_t >= T_LINE) {
            S.line_t = 0;
            if (S.nmi_generator) {
                S.nmi_on = false;
                zd_set_nmi(&S.cpu, ZD_CLEAR);
            }
            if (++S.scanline >= LINES) S.scanline = 0;
        }
    }
}

bool dspc_init(void)
{
    static const char *const sets[] = { ZX80 ? "zx80" : "zx81", NULL };
    memset(&S, 0, sizeof S);
    uint8_t *r;
    if (ZX80) {
        static const uint32_t crc[] = { 0x4c7fc597 };
        r = dspc_rom_find(sets, "zx80.rom|zx80.rom.bin|zx80.bin", crc, 1, 0x1000);
        if (!r) return false;
        memcpy(S.rom, r, 0x1000);
    } else {
        static const uint32_t crc[] = { 0x522c37b8, 0xfcbbd617, 0x4b1dd6eb };
        r = dspc_rom_find(sets, "zx81b.rom|zx81.rom|zx81a.rom|zx81.rom.bin|zx81.bin|ts1000.rom", crc, 3, 0x2000);
        if (!r) return false;
        memcpy(S.rom, r, 0x2000);
    }
    free(r);
    romfs_release();
    S.cpu.read = z_read;
    S.cpu.write = z_write;
    S.cpu.in = z_in;
    S.cpu.out = z_out;
    S.cpu.opcode_read = z_opcode;
    S.cpu.cycles = z_cycles;
    zd_init(&S.cpu);
    dspc_reset();
    return true;
}

void dspc_reset(void)
{
    memset(S.ram, 0, sizeof S.ram);
    zd_reset(&S.cpu);
    memset(S.keys, 0xff, sizeof S.keys);
    S.prev_refresh = 0xff;
    S.ula_char = 0xffff;
    S.nmi_generator = S.nmi_on = S.vsync_active = false;
    S.line_t = S.scanline = S.frame_t = 0;
    S.total_cycles = 0;
    free(S.pending_p);
    S.pending_p = NULL;
    S.pending_n = 0;
    S.boot_frames = 0;
    zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
    zd_set_nmi(&S.cpu, ZD_CLEAR);
    for (int i = 0; i < SW * SH; i++) S.fb[i] = WHITE;
}

void dspc_set_inputs(const dspc_inputs *in)
{
    static const uint8_t kMatrix[8][5] = {
        {DK_LSHIFT, DK_Z, DK_X, DK_C, DK_V}, {DK_A, DK_S, DK_D, DK_F, DK_G}, {DK_Q, DK_W, DK_E, DK_R, DK_T},
        {DK_1, DK_2, DK_3, DK_4, DK_5},      {DK_0, DK_9, DK_8, DK_7, DK_6}, {DK_P, DK_O, DK_I, DK_U, DK_Y},
        {DK_ENTER, DK_L, DK_K, DK_J, DK_H},  {DK_SPACE, DK_PERIOD, DK_M, DK_N, DK_B}};
    memset(S.keys, 0xff, sizeof S.keys);
    for (int row = 0; row < 8; ++row)
        for (int bit = 0; bit < 5; ++bit)
            if (in->keys[kMatrix[row][bit]]) S.keys[row] &= (uint8_t)~(1u << bit);
    if (in->keys[DK_RCTRL] || in->keys[DK_RSHIFT]) S.keys[7] &= 0xfd;
#define CAPS(row, bit) do { S.keys[0] &= 0xfe; S.keys[row] &= (uint8_t)~(1u << (bit)); } while (0)
    if (in->keys[DK_LEFT]) CAPS(3, 4);
    if (in->keys[DK_DOWN]) CAPS(4, 4);
    if (in->keys[DK_UP]) CAPS(4, 3);
    if (in->keys[DK_RIGHT]) CAPS(4, 2);
    if (in->keys[DK_BACKSPACE]) CAPS(4, 0);
#undef CAPS
}

static void plot_char_row(int x, int y, uint8_t ch, int row)
{
    if (y < 0 || y >= SH) return;
    const uint16_t glyph = (uint16_t)(charset_base() | ((ch & 0x3f) << 3) | (row & 7));
    uint8_t pixels = S.rom[glyph & rom_mask()];
    if (ch & 0x80) pixels = (uint8_t)~pixels;
    for (int i = 0; i < 8; ++i) {
        const int px = x + i;
        if (px < 0 || px >= SW) continue;
        S.fb[(size_t)y * SW + (size_t)px] = (pixels & (0x80 >> i)) ? BLACK : WHITE;
    }
}

static void render_dfile(void)
{
    for (int i = 0; i < SW * SH; i++) S.fb[i] = WHITE;
    const uint16_t dfile = (uint16_t)(mem_read(0x400c) | (mem_read(0x400d) << 8));
    if (dfile < 0x4000) return;
    uint16_t addr = dfile;
    if (mem_read(addr) == 0x76) ++addr;
    for (int row = 0; row < 24; ++row) {
        int col = 0;
        while (col < 32) {
            const uint8_t ch = mem_read(addr++);
            if (ch == 0x76) break;
            if ((ch & 0x40) == 0)
                for (int r = 0; r < 8; ++r) plot_char_row(PAPER_X + col * 8, PAPER_Y + row * 8 + r, ch, r);
            ++col;
            if (addr < 0x4000) return;
        }
        if (col == 32 && mem_read(addr) == 0x76) ++addr;
    }
}

static void inject_p(const uint8_t *data, size_t size)
{
    const size_t off = 0x0009;
    const size_t n = size < sizeof S.ram - off ? size : sizeof S.ram - off;
    memcpy(S.ram + off, data, n);
    S.cpu.halted = false;
    S.cpu.iff1 = S.cpu.iff2 = false;
    S.cpu.pc = 0x0207;
    if (S.cpu.sp < 0x4000 || S.cpu.sp > 0x7fff) S.cpu.sp = 0x7ffe;
    S.nmi_generator = S.nmi_on = false;
    zd_set_nmi(&S.cpu, ZD_CLEAR);
    zd_set_irq(&S.cpu, ZD_CLEAR, 0xff);
}

int dspc_run_frame(const int16_t **audio)
{
    if (S.pending_p && ++S.boot_frames >= P_INJECT_FRAMES) {
        uint8_t *p = S.pending_p;
        const size_t n = S.pending_n;
        S.pending_p = NULL;
        S.pending_n = 0;
        inject_p(p, n);
        free(p);
    }
    S.frame_t = 0;
    for (int ran = 0; ran < T_FRAME;) {
        const int slice = T_FRAME - ran < 64 ? T_FRAME - ran : 64;
        ran += zd_run(&S.cpu, slice);
    }
    render_dfile();
    const int n = (int)((double)RATE / dspc_machine.fps + 0.5);
    memset(S.audio, 0, sizeof S.audio);
    if (audio) *audio = S.audio;
    return n < 1024 ? n : 1024;
}

const uint32_t *dspc_frame(void) { return S.fb; }

const char *dspc_load(const char *name, const uint8_t *data, size_t size)
{
    (void)name;
    if (ZX80) return "ZX80 media: .o tape loading not implemented";
    if (!size) return ".p file empty";
    if (size > (size_t)(0x4000 - 0x0009)) return ".p file too large for 16K RAM";
    uint8_t *p = (uint8_t *)malloc(size);
    if (!p) return "out of memory";
    memcpy(p, data, size);
    free(S.pending_p);
    S.pending_p = p;
    S.pending_n = size;
    S.boot_frames = 0;
    return NULL;
}
