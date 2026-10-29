// zx128.c - ZX Spectrum 128K and +3: C11 port of dsp-cpp
// drivers/computers/spectrum_128k (Spectrum128k) and spectrum_3 (Spectrum3).
// Z80 at 3546895 Hz, 228 T-states x 311 lines; the 128K contention pattern
// from T-state 14361 (6,5,4,3,2,1,0,0) on bank 5 and the odd banks, port
// contention (128K only), Pascal floating bus, ULA+, two AY-3-8912 at
// 1773447 Hz (turbosound select: $FFFD writes with $9C set), beeper at
// +-4096, Interface 2 cartridges. The +3 adds the $1FFD paging (special
// all-RAM maps) and the uPD765 with a .DSK in drive A.
#include "zx_internal.h"

#define T_LINE 228
#define FIRST_CONTENDED 14361

// dsp-cpp builds a 72000 entry table; the same values, computed
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

// See Spectrum128k::contend: wait + 2 for the mid-instruction bus settle.
static inline void contend(zxspec *m, int extra)
{
    if (extra > 0) zx_on_cycles(m, extra + 2);
}

static inline bool contended(const zxspec *m, int slot, uint8_t bank)
{
    (void)m;
    return slot == 1 || (slot == 3 && (bank & 1));
}

static uint8_t ZX_HOT mem_read(void *user, uint16_t addr)
{
    zxspec *m = (zxspec *)user;
    const int slot = addr >> 14;
    // Interface 2: over $0000-$3FFF (the lower 16K, then the upper one)
    if (slot == 0 && m->if2_rom)
        return m->if2_rom[(m->if2_switched ? 0x4000 : 0) + (addr & 0x3fff)];
    const uint8_t bank = m->marco[slot];
    m->last_mem_t = zd_t_in_instruction(&m->cpu);
    if (contended(m, slot, bank)) contend(m, contention_at(zx_ula_time(m)));
    else if (zx_must_sync(m)) zx_ula_time(m);
    return bank >= 8 ? m->rom[bank - 8][addr & 0x3fff] : m->ram[bank][addr & 0x3fff];
}

static void ZX_HOT mem_write(void *user, uint16_t addr, uint8_t value)
{
    zxspec *m = (zxspec *)user;
    const int slot = addr >> 14;
    if (m->kind == ZK_PLUS3 && !m->special_paging && slot == 0) return;
    const uint8_t bank = m->marco[slot];
    if (bank >= 8) return;      // ROM
    m->last_mem_t = zd_t_in_instruction(&m->cpu);
    if (contended(m, slot, bank)) contend(m, contention_at(zx_ula_time(m)));
    else if (zx_must_sync(m)) zx_ula_time(m);
    m->ram[bank][addr & 0x3fff] = value;
}

// spectrum_128k.pas: cont = T mod 228, lin = line; paper lines 63..254,
// cont < 128; phases 1-4 bitmap / attribute / bitmap / attribute of the
// display bank, idle ($FF) otherwise.
static uint8_t floating_bus(const zxspec *m)
{
    const int cont = m->t_in_line, lin = m->line;
    if (lin < 63 || lin > 254 || cont >= 128) return 0xff;
    const int y = lin - 63;
    const int col = (cont & 0xf8) >> 2;
    const uint8_t *vram = m->ram[m->pantalla];
    const uint16_t attr_base = (uint16_t)(0x1800 + 32 * (y / 8));
    const uint16_t pix_base = zx_scr_table[y];
    switch (cont & 7) {
    case 1: return vram[(pix_base + col) & 0x3fff];
    case 2: return vram[(attr_base + col) & 0x3fff];
    case 3: return vram[(pix_base + col + 1) & 0x3fff];
    case 4: return vram[(attr_base + col + 1) & 0x3fff];
    default: return 0xff;
    }
}

// ULA I/O wait states only (the Z80 counts the base I/O T-states)
static void port_contention(zxspec *m, uint16_t port)
{
    const int pos = zx_ula_time(m);
    int extra = 0;
    if ((port & 0xc000) == 0x4000) {
        if (port & 1) {         // contended non-ULA: before each of the 4 T-states
            int t = pos;
            for (int i = 0; i < 4; ++i) {
                const int d = contention_at(t);
                extra += d;
                t += d + 1;
            }
        } else {                // contended ULA
            const int d0 = contention_at(pos);
            extra += d0 + contention_at(pos + d0 + 1);
        }
    } else if ((port & 1) == 0) {
        extra = contention_at(pos + 1);
    }
    contend(m, extra);
}

// ---------------------------------------------------------------------------
// paging
// ---------------------------------------------------------------------------
static void plus3_memory_map(zxspec *m)
{
    // spectrum_3.pas memoria_spectrum3
    m->paging_enabled = (m->port_7ffd & 0x20) == 0;
    m->special_paging = (m->port_1ffd & 0x01) != 0;
    if (!m->special_paging) {
        m->marco[0] = (uint8_t)((((m->port_7ffd >> 4) & 1) | ((m->port_1ffd >> 1) & 2)) + 8);
        m->marco[1] = 5;
        m->marco[2] = 2;
        m->marco[3] = m->port_7ffd & 7;
    } else {
        static const uint8_t k_ram[4][4] = {
            { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 4, 5, 6, 3 }, { 4, 7, 6, 3 },
        };
        const int cfg = (m->port_1ffd >> 1) & 3;
        for (int i = 0; i < 4; ++i) m->marco[i] = k_ram[cfg][i];
    }
}

static void apply_7ffd(zxspec *m, uint8_t value)
{
    if (m->kind == ZK_PLUS3) {
        if (!m->paging_enabled) return;         // locked
        m->pantalla = (uint8_t)(((value & 8) >> 2) + 5);
        m->port_7ffd = value;
        plus3_memory_map(m);
        return;
    }
    // 128K: the screen bank follows even when the paging is locked (dsp-cpp)
    m->pantalla = (uint8_t)(((value & 8) >> 2) + 5);
    m->port_7ffd = value;
    if (m->paging_enabled) {
        m->paging_enabled = (value & 0x20) == 0;
        m->marco[0] = (uint8_t)(((value >> 4) & 1) + 8);
        m->marco[3] = value & 7;
    }
}

static void apply_1ffd(zxspec *m, uint8_t value)
{
    m->port_1ffd = value;
    nec_write_motor(&m->fdc, (value & 0x08) ? 1 : 0);      // bit 3: disk motor
    plus3_memory_map(m);
}

// ---------------------------------------------------------------------------
// ports
// ---------------------------------------------------------------------------
static uint8_t io_in(void *user, uint16_t port)
{
    zxspec *m = (zxspec *)user;
    if (m->kind == ZK_128) port_contention(m, port);       // (the +3 has none:
    else zx_sync_to(m, m->last_mem_t);                     //  the beam is where the last access left it)
    if (m->rzx.playing) return zx_rzx_next_in(&m->rzx);
    uint8_t result;
    if ((port & 1) == 0) {
        result = (uint8_t)((zx_keys_read(m, port) & 0x1f) | 0xa0);
        result = (uint8_t)((result & 0xbf) | m->ear | m->speaker);
    } else {
        result = floating_bus(m);
    }
    // Kempston: A5 = 0 and A0 = 1
    if (m->kempston_enabled && (port & 0x21) == 0x01) result = (uint8_t)(m->joy & 0x1f);
    if ((port & 0xc002) == 0xc000) result = ay8910_data_r(&m->ay[m->ay_select]);
    if (m->kind == ZK_PLUS3) {
        // uPD765 (always there on the +3; a disk may or may not be in)
        if ((port & 0xf002) == 0x2000) result = nec_read_status(&m->fdc);     // 2FFD
        if ((port & 0xf002) == 0x3000) result = nec_read_data(&m->fdc);       // 3FFD
    }
    zx_ulaplus_in(m, port, &result);
    return result;
}

static void ay_control(zxspec *m, uint8_t value)
{
    if ((value & 0x9c) == 0x9c) {               // turbosound: chip select
        m->ay_select = (uint8_t)((~value) & 1);
        if (m->ay_select) m->ay1_used = true;
    }
    ay8910_address_w(&m->ay[m->ay_select], value);
}

static void io_out(void *user, uint16_t port, uint8_t value)
{
    zxspec *m = (zxspec *)user;
    if (m->kind == ZK_128) port_contention(m, port);       // (the +3 has none)
    else zx_sync_to(m, m->last_mem_t);
    if ((port & 1) == 0) {
        m->border = value & 7;
        m->speaker = (value & 0x10) ? 0x10 : 0x00;
        m->beeper_level = (value & 0x10) ? 4096 : -4096;
    }
    if (m->kind == ZK_128) {
        switch (port & 0xc002) {
        case 0x0000:
        case 0x4000: apply_7ffd(m, value); break;                       // 7FFD
        case 0xc000: ay_control(m, value); break;                       // FFFD
        case 0x8000: ay8910_data_w(&m->ay[m->ay_select], value); break; // BFFD
        default: break;
        }
    } else {
        // +3 decode (spectrum_3.pas): on port & $F002
        switch (port & 0xf002) {
        case 0x1000: apply_1ffd(m, value); break;
        case 0x3000: nec_write_data(&m->fdc, value); break;
        case 0x4000: case 0x5000: case 0x6000: case 0x7000: apply_7ffd(m, value); break;
        case 0x8000: case 0x9000: case 0xa000: case 0xb000: ay8910_data_w(&m->ay[m->ay_select], value); break;
        case 0xc000: case 0xd000: case 0xe000: case 0xf000: ay_control(m, value); break;
        default: break;
        }
    }
    zx_ulaplus_out(m, port, value);
}

static void m1(void *user)
{
    zxspec *m = (zxspec *)user;
    if (m->rzx.playing) zx_rzx_on_m1(&m->rzx);
}

// ---------------------------------------------------------------------------
void zx128_setup(zxspec *m)
{
    m->clock = 3546895;         // 17734475 / 5
    m->t_line = T_LINE;
    m->lines = 311;
    m->t_frame = T_LINE * 311;
    m->paper_first = 63;
    m->pic_first = 15;
    m->left_t = 203;
    m->fps = 3546895.0 / (T_LINE * 311);
    m->nay = 2;
    for (int i = 0; i < 2; i++) ay8910_init(&m->ay[i], 1773447, m->audio_rate);
    m->ulaplus_enabled = true;
    m->cpu.read = mem_read;
    m->cpu.write = mem_write;
    m->cpu.in = io_in;
    m->cpu.out = io_out;
    m->cpu.cycles = zx_on_insn_cycles;
    m->cpu.m1 = m1;
    m->cpu.internal = NULL;
}

void zx128_reset(zxspec *m)
{
    for (int b = 0; b < 8; b++) memset(m->ram[b], 0, ZX_BANK);
    m->marco[0] = 8;
    m->marco[1] = 5;
    m->marco[2] = 2;
    m->marco[3] = 0;
    m->pantalla = 5;
    m->port_7ffd = 0;
    m->port_1ffd = 0;
    m->paging_enabled = true;
    m->special_paging = false;
    m->if2_switched = false;
    m->if2_delay = 0;
    m->kempston_enabled = true;
    if (m->kind == ZK_PLUS3) {
        nec_reset(&m->fdc);
        nec_write_motor(&m->fdc, 0);
        plus3_memory_map(m);
    }
}

void zx128_apply_snap(zxspec *m, const zx_snap *s)
{
    m->paging_enabled = true;
    apply_7ffd(m, s->is_128 ? s->port_7ffd : 0x10);    // 48K snapshot: the 48K ROM
    if (m->kind == ZK_PLUS3 && s->is_128) apply_1ffd(m, s->port_1ffd);
    if (!s->is_128) {
        m->marco[0] = m->kind == ZK_PLUS3 ? 11 : 9;
        m->marco[1] = 5;
        m->marco[2] = 2;
        m->marco[3] = 0;
        m->pantalla = 5;
        m->special_paging = false;
    }
    if (s->ay_used) {
        for (int r = 0; r < 16; r++) ay8910_write_reg(&m->ay[0], (uint8_t)r, s->ay_regs[r]);
        ay8910_address_w(&m->ay[0], s->ay_latch);
    }
}
