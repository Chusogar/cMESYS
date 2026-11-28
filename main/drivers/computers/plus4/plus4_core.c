// plus4_core.c - Commodore Plus/4 (64 KB) and C16 (16 KB), PAL: C port of
// dsp-cpp's drivers/computers/plus4 (dspc.h). A 7501 (m65xx) at half the TED
// clock, the TED (sound/mos7360d: video, timers, sound and the keyboard
// latch), the PLA's decoding, the 6529 keyboard port, BASIC + KERNAL and the
// Plus/4's function ROMs (3-plus-1, optional). Media: .prg (injected once
// BASIC has started, then RUN typed). DSPC_MODEL: 0 Plus/4, 1 C16.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/dspc.h"
#include "../common/dspc_rom.h"
#include "../../../cpu/m6502/m65xx.h"
#include "../../../sound/mos7360d.h"

#ifndef DSPC_MODEL
#define DSPC_MODEL 0
#endif
#define C16 (DSPC_MODEL == 1)
#define PRG_INJECT_FRAMES 150

static const char *const kExts[] = { ".prg", NULL };
const dspc_info dspc_machine = {
    C16 ? "c16" : "plus4", C16 ? "Commodore 16 (PAL)" : "Commodore Plus/4 (PAL)", MOS7360_VIS_W, MOS7360_VIS_H, 1,
    (double)(MOS7360_PAL_CLOCK / 2u) / (312.0 * (double)((MOS7360_PAL_CLOCK / 2u) / (312u * 50u))), MOS7360_RATE,
    "PRG", kExts, "*.prg *.PRG", ".prg" };

enum { VIEW_LATCH, VIEW_RAM, VIEW_ROM };

static struct {
    m65xx cpu;
    mos7360d ted;
    uint8_t *ram;
    uint32_t ram_mask;
    uint8_t kernal_basic[0x8000], function[0x8000];
    bool have_function;
    uint8_t port_ddr, port_out, kb, addr_latch;
    uint8_t keyboard[8], joy1, joy2;
    bool ted_irq;
    uint8_t *pending_prg;
    size_t pending_n;
    int boot_frames, debt;
    int64_t audio_acc;
    int16_t audio[2048];
    int naudio;
    uint32_t *fb;
} S;

static void bankswitch(uint16_t offset, int ras, int *scs, int *user, int *acia, int *addr_clk, int *keyport, int *kernal)
{
    const int a15 = (offset >> 15) & 1, a14 = (offset >> 14) & 1, a13 = (offset >> 13) & 1, a12 = (offset >> 12) & 1;
    const int a11 = (offset >> 11) & 1, a10 = (offset >> 10) & 1, a9 = (offset >> 9) & 1, a8 = (offset >> 8) & 1;
    const int a7 = (offset >> 7) & 1, a6 = (offset >> 6) & 1, a5 = (offset >> 5) & 1, a4 = (offset >> 4) & 1;
    const int phi0 = 1, i0_f7 = 1;
    *scs = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || !a5 || a4 || !a15 || !phi0;
    *user = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || a5 || !a4 || !a15 || !phi0 || !i0_f7;
    *acia = !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || a5 || a4 || !a15 || !phi0;
    *addr_clk = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || !a7 || !a6 || a5 || !a4 || !a15 || !phi0 || !i0_f7;
    *keyport = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || !a5 || !a4 || !a15 || !phi0 || !i0_f7;
    *kernal = a10 && a11 && a13 && !a9 && !a8 && a14 && a12 && a15;
}

static uint8_t read_memory(uint16_t offset, int view)
{
    int scs, user, acia, addr_clk, keyport, kernal;
    bankswitch(offset, view == VIEW_LATCH ? 0 : 1, &scs, &user, &acia, &addr_clk, &keyport, &kernal);
    bool rom_on = S.ted.rom != 0;
    if (view == VIEW_RAM) rom_on = false;
    if (view == VIEW_ROM) rom_on = true;
    int cs0 = 1, cs1 = 1;
    if (rom_on) {
        if (offset >= 0x8000 && offset < 0xc000) cs0 = 0;
        if ((offset >= 0xc000 && offset < 0xfd00) || offset >= 0xff20) cs1 = 0;
    }
    const uint8_t data = S.ted.last_data;
    if (view == VIEW_LATCH && offset >= 0xff00 && offset <= 0xff1f) return mos7360d_read(&S.ted, offset, &cs0, &cs1);
    if (view == VIEW_LATCH) {
        int tcs0 = 1, tcs1 = 1;
        mos7360d_read(&S.ted, offset, &tcs0, &tcs1);
        cs0 = tcs0;
        cs1 = tcs1;
    }
    if (!scs) return data;
    if (!user) return 0xfb;
    if (!acia) return 0xff;
    if (!keyport) return S.kb;
    if (!cs0) {
        switch (S.addr_latch & 3) {
        case 0: return S.kernal_basic[offset & 0x7fff];
        case 1: return S.have_function ? S.function[offset & 0x7fff] : 0xff;
        default: return 0xff;
        }
    }
    if (!cs1) {
        if (kernal) return S.kernal_basic[offset & 0x7fff];
        switch ((S.addr_latch >> 2) & 3) {
        case 0: return S.kernal_basic[offset & 0x7fff];
        case 1: return S.have_function ? S.function[offset & 0x7fff] : 0xff;
        default: return 0xff;
        }
    }
    if (offset < 0xfd00 || offset >= 0xff20) return S.ram[offset & S.ram_mask];
    return data;
}

static uint8_t ted_mem(void *u, uint16_t a, bool rom) { (void)u; return read_memory(a, rom ? VIEW_ROM : VIEW_RAM); }

static uint8_t ted_key(void *u, uint8_t columns)
{
    (void)u;
    uint8_t data = 0xff;
    if ((columns & 0x04) == 0) {
        data &= (uint8_t)(0xf0 | (S.joy1 & 0x0f));
        if ((S.joy1 & 0x20) == 0) data &= (uint8_t)~0x40;
    }
    if ((columns & 0x02) == 0) {
        data &= (uint8_t)(0xf0 | (S.joy2 & 0x0f));
        if ((S.joy2 & 0x20) == 0) data &= (uint8_t)~0x80;
    }
    for (int row = 0; row < 8; row++)
        if ((S.kb & (1 << row)) == 0) data &= S.keyboard[row];
    return data;
}

static void ted_irq(void *u, bool on) { (void)u; S.ted_irq = on; m65xx_set_irq(&S.cpu, on ? M65_ASSERT : M65_CLEAR); }

static uint8_t c_read(void *u, uint16_t addr)
{
    (void)u;
    if (addr == 0) return S.port_ddr;
    if (addr == 1) return (uint8_t)((S.port_out & S.port_ddr) | (0xd0 & (uint8_t)~S.port_ddr));
    return read_memory(addr, VIEW_LATCH);
}

static void c_write(void *u, uint16_t addr, uint8_t value)
{
    (void)u;
    if (addr == 0) { S.port_ddr = value; return; }
    if (addr == 1) { S.port_out = value; return; }
    int scs, user, acia, addr_clk, keyport, kernal;
    bankswitch(addr, 0, &scs, &user, &acia, &addr_clk, &keyport, &kernal);
    int cs0 = 1, cs1 = 1;
    mos7360d_write(&S.ted, addr, value, &cs0, &cs1);
    if (!scs || !user || !acia) return;
    if (!addr_clk) { S.addr_latch = (uint8_t)(addr & 0x0f); return; }
    if (!keyport) { S.kb = value; return; }
    if (addr < 0xfd00 || addr >= 0xff20) S.ram[addr & S.ram_mask] = value;
}

static void c_cycles(void *u, int cycles)
{
    (void)u;
    if (cycles <= 0) return;
    S.audio_acc += (int64_t)cycles * MOS7360_RATE;
    const int64_t den = (int64_t)mos7360d_cpu_clock(&S.ted);
    while (S.audio_acc >= den) {
        S.audio_acc -= den;
        const int16_t s = mos7360d_update(&S.ted);
        if (S.naudio < 2048) S.audio[S.naudio++] = s;
    }
}

static uint8_t *first_of(const char *const *sets, const char *const *names, uint32_t size)
{
    uint32_t len = 0;
    for (int i = 0; names[i]; i++) {
        uint8_t *p = romfs_get(sets, names[i], 0, size, &len);
        if (p) return p;
    }
    return NULL;
}

static uint8_t *by_crc(const char *const *sets, const uint32_t *crcs, int n, uint32_t size)
{
    uint32_t len = 0;
    for (int i = 0; i < n; i++) {
        uint8_t *p = romfs_get(sets, "-", crcs[i], size, &len);
        if (p) return p;
    }
    return NULL;
}

static bool load_roms(void)
{
    static const char *const sets[] = { C16 ? "c16" : "plus4", "plus4", "c16", NULL };
    static const char *const kBasic1[] = { "318006-01.u23", NULL };
    static const char *const kBasic2[] = { "318006-01.u3", "318006-01", "basic", NULL };
    static const uint32_t kBasicCrc[] = { 0x74eaae87 };
    uint8_t *p = first_of(sets, kBasic1, 0x4000);
    if (!p) p = by_crc(sets, kBasicCrc, 1, 0x4000);
    if (!p) p = first_of(sets, kBasic2, 0x4000);
    if (!p) {
        romfs_missing(sets, "318006-01.u23 (BASIC)");
        return false;
    }
    memcpy(S.kernal_basic, p, 0x4000);
    free(p);
    static const char *const kPalK[] = { "318004-05.u24", "318004-05.u4", "318004-04.u24", "318004-03.u24", "318004-05", NULL };
    static const char *const kAnyK[] = { "318005-05.u24", "318005-04.u24", "318005-05", "318005-04", "318004-05.u24",
                                         "kernal", NULL };
    static const uint32_t kPalCrc[] = { 0x71c07bd4, 0xbe54ed79, 0x77bab934, 0x70295038 };
    p = first_of(sets, kPalK, 0x4000);
    if (!p) p = first_of(sets, kAnyK, 0x4000);
    if (!p) p = by_crc(sets, kPalCrc, 4, 0x4000);
    if (p) {
        memcpy(S.kernal_basic + 0x4000, p, 0x4000);
        free(p);
    } else {
        static const char *const kJiffy[] = { "jiffydos plus4.u24", NULL };
        p = first_of(sets, kJiffy, 0x8000);
        if (!p) {
            romfs_missing(sets, "318005-05.u24 / 318004-05.u24 (KERNAL)");
            return false;
        }
        memcpy(S.kernal_basic, p, 0x8000);
        free(p);
    }
    S.have_function = false;
    memset(S.function, 0xff, sizeof S.function);
    static const char *const kLo[] = { "317053-01.u25", "317053-01", NULL };
    static const char *const kHi[] = { "317054-01.u26", "317054-01", NULL };
    static const uint32_t kLoCrc[] = { 0x4fd1d8cb }, kHiCrc[] = { 0x109de2fc };
    p = first_of(sets, kLo, 0x4000);
    if (!p) p = by_crc(sets, kLoCrc, 1, 0x4000);
    if (p) {
        memcpy(S.function, p, 0x4000);
        free(p);
        p = first_of(sets, kHi, 0x4000);
        if (!p) p = by_crc(sets, kHiCrc, 1, 0x4000);
        if (p) {
            memcpy(S.function + 0x4000, p, 0x4000);
            free(p);
            S.have_function = true;
        }
    }
    romfs_release();
    return true;
}

bool dspc_init(void)
{
    memset(&S, 0, sizeof S);
    const size_t ram_size = C16 ? 0x4000 : 0x10000;
    S.ram = (uint8_t *)calloc(1, ram_size);
    S.ram_mask = (uint32_t)(ram_size - 1);
    S.fb = (uint32_t *)calloc((size_t)MOS7360_VIS_W * MOS7360_VIS_H, 4);
    if (!S.ram || !S.fb || !mos7360d_init(&S.ted, true)) return false;
    S.cpu.read = c_read;
    S.cpu.write = c_write;
    S.cpu.cycles = c_cycles;
    S.ted.mem = ted_mem;
    S.ted.key = ted_key;
    S.ted.irq = ted_irq;
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    if (!load_roms()) return false;
    dspc_reset();
    return true;
}

void dspc_reset(void)
{
    uint8_t data = 0xff;
    for (uint32_t i = 0; i <= S.ram_mask; i++) {
        S.ram[i] = data;
        if ((i % 64) == 0) data ^= 0xff;
    }
    S.port_ddr = S.port_out = 0;
    S.kb = 0xff;
    S.addr_latch = 0;
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    S.joy1 = S.joy2 = 0xff;
    S.ted_irq = false;
    free(S.pending_prg);
    S.pending_prg = NULL;
    S.pending_n = 0;
    S.boot_frames = S.debt = 0;
    S.audio_acc = 0;
    S.naudio = 0;
    mos7360d_reset(&S.ted);
    m65xx_reset(&S.cpu);
    m65xx_set_irq(&S.cpu, S.ted_irq ? M65_ASSERT : M65_CLEAR);
}

void dspc_set_inputs(const dspc_inputs *in)
{
    memset(S.keyboard, 0xff, sizeof S.keyboard);
    const bool *k = in->keys;
#define P(row, m) (S.keyboard[row] &= (uint8_t)~(m))
    if (k[DK_BACKSPACE] || k[DK_DELETE]) P(0, 0x01);
    if (k[DK_ENTER]) P(0, 0x02);
    if (k[DK_BACKSLASH]) P(0, 0x04);
    if (k[DK_F7] || k[DK_F8]) P(0, 0x08);
    if (k[DK_F1] || k[DK_F4]) P(0, 0x10);
    if (k[DK_F2] || k[DK_F5]) P(0, 0x20);
    if (k[DK_F3] || k[DK_F6]) P(0, 0x40);
    if (k[DK_AT]) P(0, 0x80);
    if (k[DK_3]) P(1, 0x01);
    if (k[DK_W]) P(1, 0x02);
    if (k[DK_A]) P(1, 0x04);
    if (k[DK_4]) P(1, 0x08);
    if (k[DK_Z]) P(1, 0x10);
    if (k[DK_S]) P(1, 0x20);
    if (k[DK_E]) P(1, 0x40);
    if (k[DK_LSHIFT] || k[DK_RSHIFT]) P(1, 0x80);
    if (k[DK_5]) P(2, 0x01);
    if (k[DK_R]) P(2, 0x02);
    if (k[DK_D]) P(2, 0x04);
    if (k[DK_6]) P(2, 0x08);
    if (k[DK_C]) P(2, 0x10);
    if (k[DK_F]) P(2, 0x20);
    if (k[DK_T]) P(2, 0x40);
    if (k[DK_X]) P(2, 0x80);
    if (k[DK_7]) P(3, 0x01);
    if (k[DK_Y]) P(3, 0x02);
    if (k[DK_G]) P(3, 0x04);
    if (k[DK_8]) P(3, 0x08);
    if (k[DK_B]) P(3, 0x10);
    if (k[DK_H]) P(3, 0x20);
    if (k[DK_U]) P(3, 0x40);
    if (k[DK_V]) P(3, 0x80);
    if (k[DK_9]) P(4, 0x01);
    if (k[DK_I]) P(4, 0x02);
    if (k[DK_J]) P(4, 0x04);
    if (k[DK_0]) P(4, 0x08);
    if (k[DK_M]) P(4, 0x10);
    if (k[DK_K]) P(4, 0x20);
    if (k[DK_O]) P(4, 0x40);
    if (k[DK_N]) P(4, 0x80);
    if (k[DK_DOWN]) P(5, 0x01);
    if (k[DK_P]) P(5, 0x02);
    if (k[DK_L]) P(5, 0x04);
    if (k[DK_UP]) P(5, 0x08);
    if (k[DK_PERIOD]) P(5, 0x10);
    if (k[DK_SEMICOLON] || k[DK_QUOTE]) P(5, 0x20);
    if (k[DK_MINUS]) P(5, 0x40);
    if (k[DK_COMMA]) P(5, 0x80);
    if (k[DK_LEFT]) P(6, 0x01);
    if (k[DK_ASTERISK]) P(6, 0x02);
    if (k[DK_QUOTE] || k[DK_SEMICOLON]) P(6, 0x04);
    if (k[DK_RIGHT]) P(6, 0x08);
    if (k[DK_ESCAPE]) P(6, 0x10);
    if (k[DK_EQUALS]) P(6, 0x20);
    if (k[DK_PLUS]) P(6, 0x40);
    if (k[DK_SLASH]) P(6, 0x80);
    if (k[DK_1]) P(7, 0x01);
    if (k[DK_HOME] || k[DK_DELETE]) P(7, 0x02);
    if (k[DK_TAB] || k[DK_LCTRL]) P(7, 0x04);
    if (k[DK_2]) P(7, 0x08);
    if (k[DK_SPACE]) P(7, 0x10);
    if (k[DK_CBM] || k[DK_LGUI] || k[DK_RALT]) P(7, 0x20);
    if (k[DK_Q]) P(7, 0x40);
    if (k[DK_ESCAPE] || k[DK_HOME]) P(7, 0x80);
    if (k[DK_CAPSLOCK]) P(1, 0x80);
#undef P
    S.joy1 = S.joy2 = 0xff;
    if (in->p1.up || k[DK_UP]) S.joy1 &= (uint8_t)~0x01;
    if (in->p1.down || k[DK_DOWN]) S.joy1 &= (uint8_t)~0x02;
    if (in->p1.left || k[DK_LEFT]) S.joy1 &= (uint8_t)~0x04;
    if (in->p1.right || k[DK_RIGHT]) S.joy1 &= (uint8_t)~0x08;
    if (in->p1.b1) S.joy1 &= (uint8_t)~0x20;
}

static void poke(uint16_t a, uint8_t v) { S.ram[a & S.ram_mask] = v; }

static void inject_prg(const uint8_t *data, size_t size)
{
    const uint16_t addr = (uint16_t)(data[0] | (data[1] << 8));
    const size_t n = size - 2;
    for (size_t i = 0; i < n; i++) poke((uint16_t)(addr + i), data[i + 2]);
    const uint16_t end = (uint16_t)(addr + n);
    if (addr != 0x1001) return;
    for (int o = 0x2d; o <= 0x31; o += 2) {
        poke((uint16_t)o, (uint8_t)end);
        poke((uint16_t)(o + 1), (uint8_t)(end >> 8));
    }
    poke(0xae, (uint8_t)end);
    poke(0xaf, (uint8_t)(end >> 8));
    static const uint8_t kRun[] = { 'R', 'U', 'N', 0x0d };
    for (size_t i = 0; i < sizeof kRun; i++) poke((uint16_t)(0x0527 + i), kRun[i]);
    poke(0x00ef, (uint8_t)sizeof kRun);
}

int dspc_run_frame(const int16_t **audio)
{
    if (S.pending_prg && ++S.boot_frames >= PRG_INJECT_FRAMES && S.ram[0x2b & S.ram_mask] == 0x01 &&
        S.ram[0x2c & S.ram_mask] == 0x10) {
        uint8_t *p = S.pending_prg;
        const size_t n = S.pending_n;
        S.pending_prg = NULL;
        S.pending_n = 0;
        inject_prg(p, n);
        free(p);
    }
    S.naudio = 0;
    const int lines = S.ted.total_lines, cpl = S.ted.cycles_per_line;
    for (int line = 0; line < lines; ++line) {
        mos7360d_update_line(&S.ted, line);
        S.debt += cpl;
        if (S.debt > 0) S.debt -= m65xx_run(&S.cpu, S.debt);
    }
    memcpy(S.fb, S.ted.bitmap, (size_t)MOS7360_VIS_W * MOS7360_VIS_H * 4);
    if (audio) *audio = S.audio;
    return S.naudio;
}

const uint32_t *dspc_frame(void) { return S.fb; }

const char *dspc_load(const char *name, const uint8_t *data, size_t size)
{
    (void)name;
    if (size < 3) return "PRG too small";
    uint8_t *p = (uint8_t *)malloc(size);
    if (!p) return "out of memory";
    memcpy(p, data, size);
    free(S.pending_prg);
    S.pending_prg = p;
    S.pending_n = size;
    S.boot_frames = 0;
    return NULL;
}
