// zx48.c - ZX Spectrum 48K: C11 port of dsp-cpp drivers/computers/spectrum
// (Spectrum48k, from spectrum_48k.pas + spectrum_misc.pas). Z80 at 3.5 MHz,
// 224 T-states x 312 lines; Fuse contention (memory, ports and the internal
// T-states that keep an address on the bus), Fuse floating bus, ULA+,
// beeper at +-8192 with the speaker mirrored on bit 6 of port $FE.
#include "zx_internal.h"

#define T_LINE 224
#define FIRST_CONTENDED 14335       // first contended T-state of the first paper line

// ULA wait states for a contended access at frame T-state t
static inline int contention_at(int t)
{
    t -= FIRST_CONTENDED;
    if (t < 0) return 0;
    const unsigned line = (unsigned)t / T_LINE;
    if (line >= 192) return 0;
    const int phase = t - (int)line * T_LINE;
    if (phase >= 128) return 0;
    const int p = phase & 7;
    return p < 6 ? 6 - p : 0;
}

// Wait states only: the Z80 already counts the base T-states.
static inline void contend(zxspec *m, int extra)
{
    if (extra > 0) zx_on_cycles(m, extra);
}

static inline uint8_t *bank_of(zxspec *m, uint16_t addr)
{
    switch (addr >> 14) {
    case 1: return m->ram[5];
    case 2: return m->ram[2];
    default: return m->ram[0];
    }
}

static uint8_t ZX_HOT mem_read(void *user, uint16_t addr)
{
    zxspec *m = (zxspec *)user;
    // contended $4000-$7FFF: the waits advance the beam (and the border)
    if ((addr & 0xc000) == 0x4000) contend(m, contention_at(zx_ula_time(m)));
    else if (zx_must_sync(m)) zx_ula_time(m);
    if (addr < 0x4000) return m->rom[0][addr];
    return bank_of(m, addr)[addr & 0x3fff];
}

static void ZX_HOT mem_write(void *user, uint16_t addr, uint8_t value)
{
    zxspec *m = (zxspec *)user;
    if (addr < 0x4000) return;
    if ((addr & 0xc000) == 0x4000) contend(m, contention_at(zx_ula_time(m)));
    else if (zx_must_sync(m)) zx_ula_time(m);
    bank_of(m, addr)[addr & 0x3fff] = value;
}

// Internal T-states that keep an address on the bus (JR offset, INC rr...)
// are contended too: loops synced to the beam through the floating bus (Top
// Gun, Arkanoid, Cobra) depend on them.
static void ZX_HOT internal(void *user, uint16_t addr)
{
    zxspec *m = (zxspec *)user;
    if ((addr & 0xc000) == 0x4000) contend(m, contention_at(zx_ula_time(m)));
    else if (zx_must_sync(m)) zx_ula_time(m);
}

static void port_contention(zxspec *m, uint16_t port)
{
    const int pos = zx_ula_time(m);
    int extra = 0;
    if ((port & 0xc000) == 0x4000) {
        if (port & 1) {
            int t = pos;
            for (int i = 0; i < 4; ++i) {
                const int d = contention_at(t);
                extra += d;
                t += d + 1;
            }
        } else {
            const int d0 = contention_at(pos);
            extra += d0 + contention_at(pos + d0 + 1);
        }
    } else if ((port & 1) == 0) {
        extra = contention_at(pos + 1);
    }
    contend(m, extra);
}

// Fuse spectrum_unattached_port for the 48K at T-state t (the start of the
// I/O cycle + waits + 3): during each 8 T-state cell of a paper line the ULA
// fetches bitmap, attribute, bitmap, attribute on phases 2-5, $FF otherwise.
static uint8_t floating_bus(const zxspec *m, int t)
{
    const int line0 = 14312;
    if (t < line0) return 0xff;
    const int rel = t - line0;
    const int line = rel / T_LINE;
    const int through = rel % T_LINE;
    if (line >= 192) return 0xff;
    if (through < 24 || through >= 24 + 128) return 0xff;
    int column = ((through - 24) / 8) * 2;
    const uint8_t *scr = m->ram[5];
    switch (through % 8) {
    case 4: ++column; /* fall through */
    case 2: return scr[zx_scr_table[line] + column];
    case 5: ++column; /* fall through */
    case 3: return scr[0x1800 + ((line >> 3) << 5) + column];
    default: return 0xff;
    }
}

static uint8_t io_in(void *user, uint16_t port)
{
    zxspec *m = (zxspec *)user;
    port_contention(m, port);
    const int read_t = zx_ula_time(m) + 3;
    if (m->rzx.playing) return zx_rzx_next_in(&m->rzx);
    uint8_t result;
    if ((port & 1) == 0) {
        // keys, bits 5 / 7 high, EAR and the speaker mirror on bit 6
        result = (uint8_t)((zx_keys_read(m, port) & 0x1f) | 0xa0);
        result = (uint8_t)((result & 0xbf) | m->ear | m->speaker);
    } else {
        result = floating_bus(m, read_t);
    }
    // Kempston on the classic $1F decode (floating-bus polls on $xxFF stay)
    if ((port & 0xff) == 0x1f) result = m->joy;
    zx_ulaplus_in(m, port, &result);
    return result;
}

static void io_out(void *user, uint16_t port, uint8_t value)
{
    zxspec *m = (zxspec *)user;
    port_contention(m, port);
    if ((port & 1) == 0) {
        m->border = value & 7;
        m->speaker = (value & 0x10) ? 0x40 : 0x00;
        m->beeper_level = (value & 0x10) ? 8192 : -8192;
    }
    zx_ulaplus_out(m, port, value);
}

static void m1(void *user)
{
    zxspec *m = (zxspec *)user;
    if (m->rzx.playing) zx_rzx_on_m1(&m->rzx);
}

void zx48_setup(zxspec *m)
{
    m->clock = 3500000;
    m->t_line = T_LINE;
    m->lines = 312;
    m->t_frame = T_LINE * 312;
    m->paper_first = 64;
    m->pic_first = 16;
    m->left_t = 200;
    m->fps = 3500000.0 / (T_LINE * 312);
    m->pantalla = 5;
    m->nay = 0;
    m->ulaplus_enabled = true;
    m->cpu.read = mem_read;
    m->cpu.write = mem_write;
    m->cpu.in = io_in;
    m->cpu.out = io_out;
    m->cpu.cycles = zx_on_insn_cycles;
    m->cpu.m1 = m1;
    m->cpu.internal = internal;
}

void zx48_reset(zxspec *m)
{
    for (int b = 0; b < 8; b++)
        if (m->ram[b]) memset(m->ram[b], 0, ZX_BANK);
    m->pantalla = 5;
}
