// hd63701dsp.c - Hitachi HD63701 / Motorola 6803: C port of dsp-cpp's
// cpu/hd63701 (from dsp-emulator's m680x.pas): the 6801 instruction set with
// the HD6301 additions, the on-chip ports, RAM, ROM and output compare timer.
#include "hd63701dsp.h"
#include <string.h>


// Addressing mode of every opcode (direc_680x in m680x.pas).
//   0 inherent, 1 immediate byte, 2 immediate word, 3 extended address,
//   4 indexed address, 5 direct page byte, 6 indexed byte, 7 extended word,
//   8 extended byte, 9 indexed word, 10 direct page word, 11 direct page address,
//   15 invalid.
static const uint8_t kMode[256] = {
    15, 0, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        // 00
    0, 0, 0, 0, 15, 15, 0, 0, 0, 0, 15, 0, 15, 15, 15, 15,    // 10
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,           // 20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,           // 30
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,      // 40
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,      // 50
    4, 1, 1, 6, 6, 15, 6, 6, 6, 6, 6, 1, 6, 6, 4, 4,          // 60
    8, 1, 1, 8, 8, 15, 8, 8, 8, 8, 8, 1, 8, 8, 3, 3,          // 70
    1, 1, 1, 2, 1, 1, 1, 15, 1, 1, 1, 1, 2, 1, 2, 15,         // 80
    5, 5, 5, 10, 5, 5, 5, 11, 5, 5, 5, 5, 10, 11, 10, 11,     // 90
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,           // a0
    8, 8, 8, 7, 8, 8, 8, 3, 8, 8, 8, 8, 7, 3, 7, 3,           // b0
    1, 1, 1, 2, 1, 1, 1, 15, 1, 1, 1, 1, 2, 15, 2, 15,        // c0
    5, 5, 5, 10, 5, 5, 5, 11, 5, 5, 5, 5, 10, 11, 10, 11,     // d0
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,           // e0
    8, 8, 8, 7, 8, 8, 8, 3, 8, 8, 8, 8, 7, 3, 7, 3,           // f0
};

// Base cycle count of every opcode (ciclos_6803 in m680x.pas).
static const uint8_t kCycles6803[256] = {
    99, 2, 99, 99, 3, 3, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2,        // 00
    2, 2, 99, 99, 99, 99, 2, 2, 99, 2, 99, 2, 99, 99, 99, 99, // 10
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,           // 20
    3, 3, 4, 4, 3, 3, 3, 3, 5, 5, 3, 10, 4, 10, 9, 12,        // 30
    2, 99, 99, 2, 2, 99, 2, 2, 2, 2, 2, 99, 2, 2, 99, 2,      // 40
    2, 99, 99, 2, 2, 99, 2, 2, 2, 2, 2, 99, 2, 2, 99, 2,      // 50
    6, 99, 99, 6, 6, 99, 6, 6, 6, 6, 6, 99, 6, 6, 3, 6,       // 60
    6, 99, 99, 6, 6, 99, 6, 6, 6, 6, 6, 99, 6, 6, 3, 6,       // 70
    2, 2, 2, 4, 2, 2, 2, 99, 2, 2, 2, 2, 4, 6, 3, 99,         // 80
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4,           // 90
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,           // a0
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 5, 5,           // b0
    2, 2, 2, 4, 2, 2, 2, 99, 2, 2, 2, 2, 3, 99, 3, 99,        // c0
    3, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,           // d0
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // e0
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // f0
};

// Base cycle count of every opcode (ciclos_63701 in m680x.pas).
// the 6800's own timings (MAME's m6800 table; its undefined opcodes as 2)
static const uint8_t kCycles6800[256] = {
    2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    4, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 2, 5, 2, 10, 2, 2, 9, 12,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    7, 2, 2, 7, 7, 2, 7, 7, 7, 7, 7, 2, 7, 7, 4, 7,
    6, 2, 2, 6, 6, 2, 6, 6, 6, 6, 6, 2, 6, 6, 3, 6,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 8, 3, 2,
    3, 3, 3, 2, 3, 3, 3, 4, 3, 3, 3, 3, 4, 2, 4, 5,
    5, 5, 5, 2, 5, 5, 5, 6, 5, 5, 5, 5, 6, 8, 6, 7,
    4, 4, 4, 2, 4, 4, 4, 5, 4, 4, 4, 4, 5, 9, 5, 6,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2,
    3, 3, 3, 2, 3, 3, 3, 4, 3, 3, 3, 3, 2, 2, 4, 5,
    5, 5, 5, 2, 5, 5, 5, 6, 5, 5, 5, 5, 2, 2, 6, 7,
    4, 4, 4, 2, 4, 4, 4, 5, 4, 4, 4, 4, 2, 2, 5, 6};

static const uint8_t kCycles[256] = {
    99, 1, 99, 99, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,        // 00
    1, 1, 99, 99, 99, 99, 1, 1, 2, 2, 4, 1, 99, 99, 99, 99,   // 10
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,           // 20
    1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1, 10, 5, 7, 9, 12,         // 30
    1, 99, 99, 1, 1, 99, 1, 1, 1, 1, 1, 99, 1, 1, 99, 1,      // 40
    1, 99, 99, 1, 1, 99, 1, 1, 1, 1, 1, 99, 1, 1, 99, 1,      // 50
    6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,           // 60
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,           // 70
    2, 2, 2, 3, 2, 2, 2, 99, 2, 2, 2, 2, 3, 5, 3, 99,         // 80
    3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,           // 90
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // a0
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,           // b0
    2, 2, 2, 3, 2, 2, 2, 99, 2, 2, 2, 2, 3, 99, 3, 99,        // c0
    3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,           // d0
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // e0
    4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,           // f0
};

#define kTrcsrTdre 0x20
#define kTcsrEoci 0x08
#define kTcsrOcf 0x40

static inline uint16_t low_word(uint32_t value) { return (uint16_t)(value & 0xffff); }
static inline uint16_t high_word(uint32_t value) { return (uint16_t)(value >> 16); }
static inline uint32_t make_dword(uint16_t high, uint16_t low) {
    return ((uint32_t)(high) << 16) | low;
}


static inline uint16_t d(hd63701 *m) { return (uint16_t)(((uint16_t)m->a << 8) | m->b); }
static inline void set_d(hd63701 *m, uint16_t v) { m->a = (uint8_t)(v >> 8); m->b = (uint8_t)v; }
static uint8_t hd_rd(hd63701 *m, uint16_t address);
static inline uint8_t port_value(hd63701 *m, int port)
{
    if (m->port_read[port]) m->port_in[port] = m->port_read[port](m->user);
    return (uint8_t)((m->port_out[port] & m->port_ddr[port]) | (m->port_in[port] & ~m->port_ddr[port]));
}
static inline uint8_t portx_value(hd63701 *m, int port)
{
    if (m->portx_read[port]) m->portx_in[port] = m->portx_read[port](m->user);
    return (uint8_t)((m->portx_out[port] & m->portx_ddr[port]) | (m->portx_in[port] & ~m->portx_ddr[port]));
}
static inline void update_port(hd63701 *m, int port)
{
    if (m->port_write[port])
        m->port_write[port](m->user, (uint8_t)((m->port_out[port] & m->port_ddr[port]) | (m->port_in[port] & ~m->port_ddr[port])));
}
static inline void update_portx(hd63701 *m, int port)
{
    if (m->portx_write[port])
        m->portx_write[port](m->user, (uint8_t)((m->portx_out[port] & m->portx_ddr[port]) | (m->portx_in[port] & ~m->portx_ddr[port])));
}
#define BRANCH(cond) do { if (cond) m->pc = (uint16_t)(m->pc + (int8_t)m->operand); } while (0)
// AIM/OIM/TIM read a second operand byte holding the address
static inline uint16_t immediate_address(hd63701 *m, bool indexed)
{
    const uint16_t target = hd_rd(m, m->pc++);
    return indexed ? (uint16_t)(m->x + target) : target;
}
static uint8_t read_io(hd63701 *m, uint8_t reg);
static void write_io(hd63701 *m, uint8_t reg, uint8_t value);
static uint8_t hd_rd(hd63701 *m, uint16_t address);
static void hd_wr(hd63701 *m, uint16_t address, uint8_t value);
static uint16_t read_word(hd63701 *m, uint16_t address);
static void write_word(hd63701 *m, uint16_t address, uint16_t value);
static void push(hd63701 *m, uint8_t value);
static uint8_t pop(hd63701 *m);
static void push_word(hd63701 *m, uint16_t value);
static uint16_t pop_word(hd63701 *m);
static uint8_t get_cc(hd63701 *m);
static void set_cc(hd63701 *m, uint8_t value);
static int call_int(hd63701 *m, uint16_t vector);
static void modified_counters(hd63701 *m);
static void check_timer_event(hd63701 *m);
static uint8_t op_neg(hd63701 *m, uint8_t value);
static uint8_t op_com(hd63701 *m, uint8_t value);
static uint8_t op_lsr(hd63701 *m, uint8_t value);
static uint8_t op_asl(hd63701 *m, uint8_t value);
static uint8_t op_ror(hd63701 *m, uint8_t value);
static uint8_t op_rol(hd63701 *m, uint8_t value);
static uint8_t op_asr(hd63701 *m, uint8_t value);
static uint8_t op_dec(hd63701 *m, uint8_t value);
static uint8_t op_inc(hd63701 *m, uint8_t value);
static void op_tst(hd63701 *m, uint8_t value);
static uint8_t op_sub(hd63701 *m, uint8_t left, uint8_t right);
static uint8_t op_sbc(hd63701 *m, uint8_t left, uint8_t right);
static uint8_t op_and(hd63701 *m, uint8_t left, uint8_t right);
static uint8_t op_eor(hd63701 *m, uint8_t left, uint8_t right);
static uint8_t op_adc(hd63701 *m, uint8_t left, uint8_t right);
static uint8_t op_or(hd63701 *m, uint8_t left, uint8_t right);
static uint8_t op_add(hd63701 *m, uint8_t left, uint8_t right);







static uint8_t read_io(hd63701 *m, uint8_t reg) {

    switch (reg) {
        case 0x00: return m->port_ddr[0];
        case 0x01: return m->port_ddr[1];
        case 0x02: return port_value(m, 0);
        case 0x03: return port_value(m, 1);
        case 0x04: return m->port_ddr[2];
        case 0x05: return m->port_ddr[3];
        case 0x06: return port_value(m, 2);
        case 0x07: return port_value(m, 3);
        case 0x08:
            m->pending_tcsr = 0;
            return m->tcsr;
        case 0x09: return (uint8_t)(low_word(m->counter) >> 8);
        case 0x0b:
            if ((m->pending_tcsr & kTcsrOcf) == 0) m->tcsr = (uint8_t)(m->tcsr & ~kTcsrOcf);
            return (uint8_t)(low_word(m->output_compare) >> 8);
        case 0x0c:
            if ((m->pending_tcsr & kTcsrOcf) == 0) m->tcsr = (uint8_t)(m->tcsr & ~kTcsrOcf);
            return (uint8_t)(m->output_compare & 0xff);
        case 0x11:
            m->trcsr_read = true;
            return m->trcsr;
        case 0x14: return m->ram_control;
        case 0x15: return portx_value(m, 0);
        case 0x17: return portx_value(m, 1);
        default: return 0xff;
    }
}

static void write_io(hd63701 *m, uint8_t reg, uint8_t value) {

    switch (reg) {
        case 0x00:
            m->port_ddr[0] = value;
            update_port(m, 0);
            break;
        case 0x01:
            m->port_ddr[1] = value;
            update_port(m, 1);
            break;
        case 0x02:
            m->port_out[0] = value;
            update_port(m, 0);
            break;
        case 0x03:
            m->port_out[1] = value;
            update_port(m, 1);
            break;
        case 0x04:
            m->port_ddr[2] = value;
            update_port(m, 2);
            break;
        case 0x05:
            m->port_ddr[3] = value;
            update_port(m, 3);
            break;
        case 0x06:
            m->port_out[2] = value;
            update_port(m, 2);
            break;
        case 0x07:
            m->port_out[3] = value;
            update_port(m, 3);
            break;
        case 0x08:
            m->tcsr = (uint8_t)((value & 0x1f) | (m->tcsr & 0xe0));
            m->pending_tcsr &= m->tcsr;
            if (!m->cc.i && (m->tcsr & (kTcsrEoci | kTcsrOcf)) == (kTcsrEoci | kTcsrOcf)) {
                call_int(m, 0xfff4);
            }
            break;
        case 0x09:
            m->latch09 = value;
            m->counter = make_dword(high_word(m->counter), 0xfff8);
            modified_counters(m);
            break;
        case 0x0a:
            m->counter = make_dword(high_word(m->counter), (uint16_t)((m->latch09 << 8) | value));
            modified_counters(m);
            break;
        case 0x0b:
            if ((uint8_t)(low_word(m->output_compare) >> 8) != value) {
                m->output_compare = make_dword(
                    high_word(m->output_compare),
                    (uint16_t)((value << 8) | (m->output_compare & 0xff)));
                modified_counters(m);
            }
            break;
        case 0x0c:
            if ((uint8_t)(m->output_compare & 0xff) != value) {
                m->output_compare = make_dword(
                    high_word(m->output_compare),
                    (uint16_t)((low_word(m->output_compare) & 0xff00) | value));
                modified_counters(m);
            }
            break;
        case 0x11: m->trcsr = (uint8_t)((m->trcsr & 0xe0) | (value & 0x1f)); break;
        case 0x13:
            if (m->trcsr_read) {
                m->trcsr_read = false;
                m->trcsr = (uint8_t)(m->trcsr & ~kTrcsrTdre);
            }
            m->tdr = value;
            break;
        case 0x14: m->ram_control = value; break;
        case 0x15:
            m->portx_out[0] = value;
            update_portx(m, 0);
            break;
        case 0x16:
            if (m->portx_ddr[1] != value) {
                m->portx_ddr[1] = value;
                update_portx(m, 1);
            }
            break;
        case 0x17:
            m->portx_out[1] = value;
            update_portx(m, 1);
            break;
        case 0x20:
            if (m->portx_ddr[0] != value) {
                m->portx_ddr[0] = value;
                update_portx(m, 0);
            }
            break;
        default: break;
    }
}

static uint8_t hd_rd(hd63701 *m, uint16_t address) {
    if (m->type == HD_M6800) return m->read_mem(m->user, address);
    if (m->type == HD_M6803) {
        if (address <= 0x0007) return read_io(m, (uint8_t)(address));
        if (address >= 0x0040 && address <= 0x00ff) return m->internal_ram[address];
        if (address >= 0x0100) return m->read_mem(m->user, address);
        return 0xff;
    }
    if (address <= 0x001f) return read_io(m, (uint8_t)(address));
    if (address >= 0x0040 && address <= 0x01ff) return m->internal_ram[address];
    if (address >= 0x0200 && address <= 0xbfff) return m->read_mem(m->user, address);
    if (m->type == HD_HD63701V) {
        // 4KB internal ROM at $e000-$ffff; $c000-$dfff is external.
        if (address >= 0xc000 && address <= 0xdfff) return m->read_mem(m->user, address);
        if (address >= 0xe000) return m->rom[address & 0x1fff];
        return 0xff;
    }
    if (address >= 0xc000) return m->rom[address & 0x3fff];
    return 0xff;
}

static void hd_wr(hd63701 *m, uint16_t address, uint8_t value) {
    if (m->type == HD_M6800) {
        m->write_mem(m->user, address, value);
        return;
    }
    if (m->type == HD_M6803) {
        if (address <= 0x0007) {
            write_io(m, (uint8_t)(address), value);
            return;
        }
        if (address >= 0x0040 && address <= 0x00ff) {
            m->internal_ram[address] = value;
            return;
        }
        if (address >= 0x0100 && address <= 0xefff) m->write_mem(m->user, address, value);
        return;
    }
    if (address <= 0x001f) {
        write_io(m, (uint8_t)(address), value);
        return;
    }
    if (address >= 0x0040 && address <= 0x01ff) {
        m->internal_ram[address] = value;
        return;
    }
    if (address >= 0x0200 && address <= 0xbfff) {
        m->write_mem(m->user, address, value);
        return;
    }
    if (m->type == HD_HD63701V && address >= 0xc000 && address <= 0xdfff) {
        m->write_mem(m->user, address, value);
        return;
    }
    // $c000-$ffff (Y) / $e000-$ffff (V) is internal ROM.
}

static uint16_t read_word(hd63701 *m, uint16_t address) {
    uint16_t high = hd_rd(m, address);
    return (uint16_t)((high << 8) | hd_rd(m, (uint16_t)(address + 1)));
}

static void write_word(hd63701 *m, uint16_t address, uint16_t value) {
    hd_wr(m, address, (uint8_t)(value >> 8));
    hd_wr(m, (uint16_t)(address + 1), (uint8_t)(value));
}

static void push(hd63701 *m, uint8_t value) {
    hd_wr(m, m->sp, value);
    m->sp = (uint16_t)(m->sp - 1);
}

static uint8_t pop(hd63701 *m) {
    m->sp = (uint16_t)(m->sp + 1);
    return hd_rd(m, m->sp);
}

static void push_word(hd63701 *m, uint16_t value) {
    push(m, (uint8_t)(value));
    push(m, (uint8_t)(value >> 8));
}

static uint16_t pop_word(hd63701 *m) {
    uint16_t high = pop(m);
    return (uint16_t)((high << 8) | pop(m));
}

static uint8_t get_cc(hd63701 *m) {
    return (uint8_t)((m->cc.c ? 0x01 : 0) | (m->cc.v ? 0x02 : 0) | (m->cc.z ? 0x04 : 0) | (m->cc.n ? 0x08 : 0) |
                   (m->cc.i ? 0x10 : 0) | (m->cc.h ? 0x20 : 0));
}

static void set_cc(hd63701 *m, uint8_t value) {
    m->cc.c = (value & 0x01) != 0;
    m->cc.v = (value & 0x02) != 0;
    m->cc.z = (value & 0x04) != 0;
    m->cc.n = (value & 0x08) != 0;
    m->cc.i = (value & 0x10) != 0;
    m->cc.h = (value & 0x20) != 0;
}

void hd63701_reset(hd63701 *m) {
    m->pc = read_word(m, 0xfffe);
    m->x = 0;
    m->a = 0;
    m->b = 0;
    m->sp = 0;
    memset(&m->cc, 0, sizeof m->cc);
    m->cc.i = true;
    m->nmi_request = HD_CLEAR;
    m->nmi_state = HD_CLEAR;
    m->irq_state = HD_CLEAR;
    m->reset_state = HD_CLEAR;
    memset(m->port_ddr, 0, sizeof m->port_ddr);
    m->ram_control = 0x40;
    m->tcsr = kTrcsrTdre;
    m->counter = 0;
    m->output_compare = 0xffff;
    m->trcsr_read = false;
    m->pending_tcsr = 0;
    m->timer_next = m->output_compare;
}

void hd63701_set_nmi(hd63701 *m, int state) {
    m->nmi_request = state;
    if (state == HD_CLEAR) m->nmi_state = HD_CLEAR;
}

static int call_int(hd63701 *m, uint16_t vector) {
    push_word(m, m->pc);
    push_word(m, m->x);
    push(m, m->a);
    push(m, m->b);
    push(m, get_cc(m));
    m->cc.i = true;
    m->pc = read_word(m, vector);
    return 12;
}

static void modified_counters(hd63701 *m) {
    uint16_t high = low_word(m->output_compare) >= low_word(m->counter) ? high_word(m->counter)
                                                                   : (uint16_t)(high_word(m->counter) + 1);
    m->output_compare = make_dword(high, low_word(m->output_compare));
    m->timer_next = m->output_compare;
}

static void check_timer_event(hd63701 *m) {
    if (m->counter >= m->output_compare) {
        m->output_compare = make_dword((uint16_t)(high_word(m->output_compare) + 1),
                                     low_word(m->output_compare));
        m->pending_tcsr |= kTcsrOcf;
        m->tcsr |= kTcsrOcf;
        if (!m->cc.i && (m->tcsr & kTcsrEoci) != 0) call_int(m, 0xfff4);
    }
    m->timer_next = m->output_compare;
}

static uint8_t op_neg(hd63701 *m, uint8_t value) {
    uint16_t result = (uint16_t)(-(int16_t)(value));
    m->cc.z = (result & 0xff) == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.v = ((0 ^ value ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}

static uint8_t op_com(hd63701 *m, uint8_t value) {
    uint8_t result = (uint8_t)(~value);
    m->cc.v = false;
    m->cc.c = true;
    m->cc.n = (result & 0x80) != 0;
    m->cc.z = result == 0;
    return result;
}

static uint8_t op_lsr(hd63701 *m, uint8_t value) {
    uint8_t result = (uint8_t)(value >> 1);
    m->cc.n = false;
    m->cc.c = (value & 1) != 0;
    m->cc.z = result == 0;
    m->cc.v = m->cc.n != m->cc.c;
    return result;
}

static uint8_t op_asl(hd63701 *m, uint8_t value) {
    uint16_t result = (uint16_t)(value << 1);
    m->cc.z = (result & 0xff) == 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.v = ((result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}

static uint8_t op_ror(hd63701 *m, uint8_t value) {
    uint8_t result = (uint8_t)((value >> 1) | (m->cc.c ? 0x80 : 0));
    m->cc.c = (value & 0x01) != 0;
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.v = m->cc.n != m->cc.c;
    return result;
}

static uint8_t op_rol(hd63701 *m, uint8_t value) {
    uint16_t result = (uint16_t)((value << 1) | (m->cc.c ? 1 : 0));
    m->cc.z = (result & 0xff) == 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.v = ((result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}

static uint8_t op_asr(hd63701 *m, uint8_t value) {
    m->cc.c = (value & 0x01) != 0;
    uint8_t result = (uint8_t)(value >> 1);
    result = (uint8_t)(result | ((result & 0x40) << 1));
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.v = m->cc.n != m->cc.c;
    return result;
}

static uint8_t op_dec(hd63701 *m, uint8_t value) {
    uint8_t result = (uint8_t)(value - 1);
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.v = result == 0x7f;
    return result;
}

static uint8_t op_inc(hd63701 *m, uint8_t value) {
    uint8_t result = (uint8_t)(value + 1);
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.v = result == 0x80;
    return result;
}

static void op_tst(hd63701 *m, uint8_t value) {
    m->cc.v = false;
    m->cc.c = false;
    m->cc.z = value == 0;
    m->cc.n = (value & 0x80) != 0;
}

static uint8_t op_sub(hd63701 *m, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left - right);
    m->cc.z = (result & 0xff) == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}

static uint8_t op_sbc(hd63701 *m, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left - right - (m->cc.c ? 1 : 0));
    m->cc.z = (result & 0xff) == 0;
    m->cc.n = (result & 0x80) != 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}

static uint8_t op_and(hd63701 *m, uint8_t left, uint8_t right) {
    uint8_t result = (uint8_t)(left & right);
    m->cc.v = false;
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    return result;
}

static uint8_t op_eor(hd63701 *m, uint8_t left, uint8_t right) {
    uint8_t result = (uint8_t)(left ^ right);
    m->cc.v = false;
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    return result;
}

static uint8_t op_adc(hd63701 *m, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left + right + (m->cc.c ? 1 : 0));
    m->cc.n = (result & 0x80) != 0;
    m->cc.z = (result & 0xff) == 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    m->cc.h = ((left ^ right ^ result) & 0x10) != 0;
    return (uint8_t)(result);
}

static uint8_t op_or(hd63701 *m, uint8_t left, uint8_t right) {
    uint8_t result = (uint8_t)(left | right);
    m->cc.v = false;
    m->cc.z = result == 0;
    m->cc.n = (result & 0x80) != 0;
    return result;
}

static uint8_t op_add(hd63701 *m, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left + right);
    m->cc.n = (result & 0x80) != 0;
    m->cc.z = (result & 0xff) == 0;
    m->cc.c = (result & 0x100) != 0;
    m->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    m->cc.h = ((left ^ right ^ result) & 0x10) != 0;
    return (uint8_t)(result);
}

int hd63701_run(hd63701 *m, int cycles) {
    int executed = 0;
    while (executed < cycles) {
        if (m->halt_state != HD_CLEAR) return cycles;
        if (m->reset_state != HD_CLEAR) {
            int requested = m->reset_state;
            hd63701_reset(m);
            if (requested == HD_ASSERT) {
                m->reset_state = HD_ASSERT;
                return cycles;
            }
        }

        m->extra_cycles = 0;
        if (m->nmi_request != HD_CLEAR) {
            if (m->nmi_state == HD_CLEAR) m->extra_cycles = call_int(m, 0xfffc);
            if (m->nmi_request == HD_PULSE) m->nmi_request = HD_CLEAR;
            if (m->nmi_request == HD_ASSERT) m->nmi_state = HD_ASSERT;
        } else if (m->irq_state != HD_CLEAR && !m->cc.i) {
            m->irq_state = HD_CLEAR;
            m->extra_cycles = call_int(m, 0xfff8);
        } else if (!m->cc.i && (m->tcsr & (kTcsrEoci | kTcsrOcf)) == (kTcsrEoci | kTcsrOcf)) {
            call_int(m, 0xfff4);
        }

        // CLEANUP_COUNTERS(): keep the counter inside its low word.
        m->output_compare = make_dword((uint16_t)(high_word(m->output_compare) - high_word(m->counter)),
                                     low_word(m->output_compare));
        m->counter = low_word(m->counter);
        m->timer_next = m->output_compare;

        uint8_t opcode = hd_rd(m, m->pc++);
        switch (kMode[opcode]) {
            case 0: break;  // inherent
            case 1: m->operand = hd_rd(m, m->pc++); break;
            case 2:
                m->operand_word = read_word(m, m->pc);
                m->pc = (uint16_t)(m->pc + 2);
                break;
            case 3:
                m->address = read_word(m, m->pc);
                m->pc = (uint16_t)(m->pc + 2);
                break;
            case 4: m->address = (uint16_t)(m->x + hd_rd(m, m->pc++)); break;
            case 5:
                m->address = hd_rd(m, m->pc++);
                m->operand = hd_rd(m, m->address);
                break;
            case 6:
                m->address = (uint16_t)(m->x + hd_rd(m, m->pc++));
                m->operand = hd_rd(m, m->address);
                break;
            case 7:
                m->address = read_word(m, m->pc);
                m->pc = (uint16_t)(m->pc + 2);
                m->operand_word = read_word(m, m->address);
                break;
            case 8:
                m->address = read_word(m, m->pc);
                m->pc = (uint16_t)(m->pc + 2);
                m->operand = hd_rd(m, m->address);
                break;
            case 9:
                m->address = (uint16_t)(m->x + hd_rd(m, m->pc++));
                m->operand_word = read_word(m, m->address);
                break;
            case 10:
                m->address = hd_rd(m, m->pc++);
                m->operand_word = read_word(m, m->address);
                break;
            case 11: m->address = hd_rd(m, m->pc++); break;
            default: break;  // invalid opcode
        }


        switch (opcode) {
            case 0x01: break;  // nop
            case 0x04: {       // lsrd
                m->cc.n = false;
                uint16_t value = d(m);
                m->cc.c = (value & 1) != 0;
                value = (uint16_t)(value >> 1);
                m->cc.z = value == 0;
                m->cc.v = m->cc.n != m->cc.c;
                set_d(m, value);
                break;
            }
            case 0x05: {  // asld
                uint32_t value = (uint32_t)(d(m)) << 1;
                m->cc.n = (value & 0x8000) != 0;
                m->cc.z = (value & 0xffff) == 0;
                m->cc.c = (value & 0x10000) != 0;
                m->cc.v = ((value ^ (value >> 1)) & 0x8000) != 0;
                set_d(m, (uint16_t)(value));
                break;
            }
            case 0x06: set_cc(m, m->a); break;  // tap
            case 0x07: m->a = get_cc(m); break;  // tpa
            case 0x08:                       // inx
                m->x = (uint16_t)(m->x + 1);
                m->cc.z = m->x == 0;
                break;
            case 0x09:  // dex
                m->x = (uint16_t)(m->x - 1);
                m->cc.z = m->x == 0;
                break;
            case 0x0a: m->cc.v = false; break;  // clv
            case 0x0b: m->cc.v = true; break;   // sev
            case 0x0c: m->cc.c = false; break;  // clc
            case 0x0d: m->cc.c = true; break;   // sec
            case 0x0e: m->cc.i = false; break;  // cli
            case 0x0f: m->cc.i = true; break;   // sei
            case 0x10: m->a = op_sub(m, m->a, m->b); break;  // sba
            case 0x11: op_sub(m, m->a, m->b); break;      // cba
            case 0x12:
            case 0x13: m->x = (uint16_t)(m->x + hd_rd(m, (uint16_t)(m->sp + 1))); break;  // undocumented asx1/asx2
            case 0x16:                                                  // tab
                m->b = m->a;
                m->cc.v = false;
                m->cc.z = m->b == 0;
                m->cc.n = (m->b & 0x80) != 0;
                break;
            case 0x17:  // tba
                m->a = m->b;
                m->cc.v = false;
                m->cc.z = m->a == 0;
                m->cc.n = (m->a & 0x80) != 0;
                break;
            case 0x18: {  // xgdx
                uint16_t value = m->x;
                m->x = d(m);
                set_d(m, value);
                break;
            }
            case 0x19: {  // daa
                uint8_t msn = (uint8_t)(m->a & 0xf0);
                uint8_t lsn = (uint8_t)(m->a & 0x0f);
                uint16_t correction = 0;
                if (lsn > 0x09 || m->cc.h) correction |= 0x06;
                if (msn > 0x80 && lsn > 0x09) correction |= 0x60;
                if (msn > 0x90 || m->cc.c) correction |= 0x60;
                uint16_t result = (uint16_t)(correction + m->a);
                m->cc.z = (result & 0xff) == 0;
                m->cc.n = (result & 0x80) != 0;
                m->cc.c = m->cc.c || (result & 0x100) != 0;
                m->cc.v = false;
                m->a = (uint8_t)(result);
                break;
            }
            case 0x1b: m->a = op_add(m, m->a, m->b); break;  // aba
            case 0x20: BRANCH(true); break;
            case 0x22: BRANCH(!(m->cc.c || m->cc.z)); break;  // bhi
            case 0x23: BRANCH(m->cc.c || m->cc.z); break;     // bls
            case 0x24: BRANCH(!m->cc.c); break;            // bcc
            case 0x25: BRANCH(m->cc.c); break;             // bcs
            case 0x26: BRANCH(!m->cc.z); break;            // bne
            case 0x27: BRANCH(m->cc.z); break;             // beq
            case 0x28: BRANCH(!m->cc.v); break;            // bvc
            case 0x29: BRANCH(m->cc.v); break;             // bvs
            case 0x2a: BRANCH(!m->cc.n); break;            // bpl
            case 0x2b: BRANCH(m->cc.n); break;             // bmi
            case 0x2c: BRANCH(m->cc.n == m->cc.v); break;     // bge
            case 0x2d: BRANCH(m->cc.n != m->cc.v); break;     // blt
            case 0x2e: BRANCH(m->cc.n == m->cc.v && !m->cc.z); break;    // bgt
            case 0x2f: BRANCH(m->cc.n != m->cc.v || m->cc.z); break;     // ble
            case 0x30:  // tsx
                m->x = (uint16_t)(m->sp + 1);
                break;
            case 0x31: m->sp = (uint16_t)(m->sp + 1); break;  // ins
            case 0x32: m->a = pop(m); break;              // pula
            case 0x33: m->b = pop(m); break;              // pulb
            case 0x34: m->sp = (uint16_t)(m->sp - 1); break;  // des
            case 0x35: m->sp = (uint16_t)(m->x - 1); break;   // txs
            case 0x36: push(m, m->a); break;                // psha
            case 0x37: push(m, m->b); break;                // pshb
            case 0x38: m->x = pop_word(m); break;         // pulx
            case 0x39: m->pc = pop_word(m); break;       // rts
            case 0x3a: m->x = (uint16_t)(m->x + m->b); break;    // abx
            case 0x3b:                                // rti
                set_cc(m, pop(m));
                m->b = pop(m);
                m->a = pop(m);
                m->x = pop_word(m);
                m->pc = pop_word(m);
                break;
            case 0x3c: push_word(m, m->x); break;  // pshx
            case 0x3d:                       // mul
                set_d(m, (uint16_t)(m->a * m->b));
                m->cc.c = (d(m) & 0x80) != 0;
                break;
            case 0x3e: break;                 // wai, unused by this board
            case 0x3f: call_int(m, 0xfffa); break;  // swi
            case 0x40: m->a = op_neg(m, m->a); break;
            case 0x43: m->a = op_com(m, m->a); break;
            case 0x44: m->a = op_lsr(m, m->a); break;
            case 0x46: m->a = op_ror(m, m->a); break;
            case 0x47: m->a = op_asr(m, m->a); break;
            case 0x48: m->a = op_asl(m, m->a); break;
            case 0x49: m->a = op_rol(m, m->a); break;
            case 0x4a: m->a = op_dec(m, m->a); break;
            case 0x4c: m->a = op_inc(m, m->a); break;
            case 0x4d: op_tst(m, m->a); break;
            case 0x4f:  // clra
                m->a = 0;
                m->cc.z = true;
                m->cc.n = false;
                m->cc.v = false;
                m->cc.c = false;
                break;
            case 0x50: m->b = op_neg(m, m->b); break;
            case 0x53: m->b = op_com(m, m->b); break;
            case 0x54: m->b = op_lsr(m, m->b); break;
            case 0x56: m->b = op_ror(m, m->b); break;
            case 0x57: m->b = op_asr(m, m->b); break;
            case 0x58: m->b = op_asl(m, m->b); break;
            case 0x59: m->b = op_rol(m, m->b); break;
            case 0x5a: m->b = op_dec(m, m->b); break;
            case 0x5c: m->b = op_inc(m, m->b); break;
            case 0x5d: op_tst(m, m->b); break;
            case 0x5f:  // clrb
                m->b = 0;
                m->cc.z = true;
                m->cc.n = false;
                m->cc.v = false;
                m->cc.c = false;
                break;
            case 0x60: hd_wr(m, m->address, op_neg(m, hd_rd(m, m->address))); break;
            case 0x70: hd_wr(m, m->address, op_neg(m, m->operand)); break;
            case 0x61:
            case 0x71: {  // aim
                uint16_t target = immediate_address(m, opcode == 0x61);
                uint8_t result = (uint8_t)(hd_rd(m, target) & m->operand);
                m->cc.v = false;
                m->cc.z = result == 0;
                m->cc.n = (result & 0x80) != 0;
                hd_wr(m, target, result);
                break;
            }
            case 0x62:
            case 0x72: {  // oim
                uint16_t target = immediate_address(m, opcode == 0x62);
                uint8_t result = (uint8_t)(hd_rd(m, target) | m->operand);
                m->cc.v = false;
                m->cc.z = result == 0;
                m->cc.n = (result & 0x80) != 0;
                hd_wr(m, target, result);
                break;
            }
            case 0x6b:
            case 0x7b: {  // tim
                uint16_t target = immediate_address(m, opcode == 0x6b);
                uint8_t result = (uint8_t)(hd_rd(m, target) & m->operand);
                m->cc.v = false;
                m->cc.z = result == 0;
                m->cc.n = (result & 0x80) != 0;
                break;
            }
            case 0x63:
            case 0x73: hd_wr(m, m->address, op_com(m, m->operand)); break;
            case 0x64:
            case 0x74: hd_wr(m, m->address, op_lsr(m, m->operand)); break;
            case 0x66:
            case 0x76: hd_wr(m, m->address, op_ror(m, m->operand)); break;
            case 0x67:
            case 0x77: hd_wr(m, m->address, op_asr(m, m->operand)); break;
            case 0x68:
            case 0x78: hd_wr(m, m->address, op_asl(m, m->operand)); break;
            case 0x69:
            case 0x79: hd_wr(m, m->address, op_rol(m, m->operand)); break;
            case 0x6a:
            case 0x7a: hd_wr(m, m->address, op_dec(m, m->operand)); break;
            case 0x6c:
            case 0x7c: hd_wr(m, m->address, op_inc(m, m->operand)); break;
            case 0x6d:
            case 0x7d: op_tst(m, m->operand); break;
            case 0x6e:
            case 0x7e: m->pc = m->address; break;  // jmp
            case 0x6f:
            case 0x7f:  // clr
                hd_wr(m, m->address, 0);
                m->cc.z = true;
                m->cc.n = false;
                m->cc.v = false;
                m->cc.c = false;
                break;
            case 0x80:
            case 0x90:
            case 0xa0:
            case 0xb0: m->a = op_sub(m, m->a, m->operand); break;  // suba
            case 0x81:
            case 0x91:
            case 0xa1:
            case 0xb1: op_sub(m, m->a, m->operand); break;  // cmpa
            case 0x82:
            case 0x92:
            case 0xa2:
            case 0xb2: m->a = op_sbc(m, m->a, m->operand); break;  // sbca
            case 0x83:
            case 0x93:
            case 0xa3:
            case 0xb3: {  // subd
                uint32_t result = (uint32_t)(d(m)) - m->operand_word;
                m->cc.z = (result & 0xffff) == 0;
                m->cc.n = (result & 0x8000) != 0;
                m->cc.c = (result & 0x10000) != 0;
                m->cc.v = ((d(m) ^ m->operand_word ^ result ^ (result >> 1)) & 0x8000) != 0;
                set_d(m, (uint16_t)(result));
                break;
            }
            case 0x84:
            case 0x94:
            case 0xa4:
            case 0xb4: m->a = op_and(m, m->a, m->operand); break;  // anda
            case 0x85:
            case 0x95:
            case 0xa5:
            case 0xb5: op_and(m, m->a, m->operand); break;  // bita
            case 0x86:
            case 0x96:
            case 0xa6:
            case 0xb6:  // lda
                m->a = m->operand;
                m->cc.v = false;
                m->cc.z = m->a == 0;
                m->cc.n = (m->a & 0x80) != 0;
                break;
            case 0x97:
            case 0xa7:
            case 0xb7:  // sta
                m->cc.v = false;
                m->cc.z = m->a == 0;
                m->cc.n = (m->a & 0x80) != 0;
                hd_wr(m, m->address, m->a);
                break;
            case 0x88:
            case 0x98:
            case 0xa8:
            case 0xb8: m->a = op_eor(m, m->a, m->operand); break;  // eora
            case 0x89:
            case 0x99:
            case 0xa9:
            case 0xb9: m->a = op_adc(m, m->a, m->operand); break;  // adca
            case 0x8a:
            case 0x9a:
            case 0xaa:
            case 0xba: m->a = op_or(m, m->a, m->operand); break;  // ora
            case 0x8b:
            case 0x9b:
            case 0xab:
            case 0xbb: m->a = op_add(m, m->a, m->operand); break;  // adda
            case 0x8c:
            case 0x9c:
            case 0xac:
            case 0xbc: {  // cpx
                uint32_t result = (uint32_t)(m->x) - m->operand_word;
                m->cc.z = (result & 0xffff) == 0;
                m->cc.n = (result & 0x8000) != 0;
                m->cc.v = ((m->x ^ m->operand_word ^ result ^ (result >> 1)) & 0x8000) != 0;
                m->cc.c = (result & 0x10000) != 0;
                break;
            }
            case 0x8d:  // bsr
                push_word(m, m->pc);
                m->pc = (uint16_t)(m->pc + (int8_t)(m->operand));
                break;
            case 0x9d:
            case 0xad:
            case 0xbd:  // jsr
                push_word(m, m->pc);
                m->pc = m->address;
                break;
            case 0x8e:
            case 0x9e:
            case 0xae:
            case 0xbe:  // lds
                m->sp = m->operand_word;
                m->cc.v = false;
                m->cc.z = m->sp == 0;
                m->cc.n = (m->sp & 0x8000) != 0;
                break;
            case 0x9f:
            case 0xaf:
            case 0xbf:  // sts
                m->cc.v = false;
                m->cc.z = m->sp == 0;
                m->cc.n = (m->sp & 0x8000) != 0;
                write_word(m, m->address, m->sp);
                break;
            case 0xc0:
            case 0xd0:
            case 0xe0:
            case 0xf0: m->b = op_sub(m, m->b, m->operand); break;  // subb
            case 0xc1:
            case 0xd1:
            case 0xe1:
            case 0xf1: op_sub(m, m->b, m->operand); break;  // cmpb
            case 0xc2:
            case 0xd2:
            case 0xe2:
            case 0xf2: m->b = op_sbc(m, m->b, m->operand); break;  // sbcb
            case 0xc3:
            case 0xd3:
            case 0xe3:
            case 0xf3: {  // addd
                uint32_t result = (uint32_t)(d(m)) + m->operand_word;
                m->cc.z = (result & 0xffff) == 0;
                m->cc.n = (result & 0x8000) != 0;
                m->cc.c = (result & 0x10000) != 0;
                m->cc.v = ((d(m) ^ m->operand_word ^ result ^ (result >> 1)) & 0x8000) != 0;
                set_d(m, (uint16_t)(result));
                break;
            }
            case 0xc4:
            case 0xd4:
            case 0xe4:
            case 0xf4: m->b = op_and(m, m->b, m->operand); break;  // andb
            case 0xc5:
            case 0xd5:
            case 0xe5:
            case 0xf5: op_and(m, m->b, m->operand); break;  // bitb
            case 0xc6:
            case 0xd6:
            case 0xe6:
            case 0xf6:  // ldb
                m->b = m->operand;
                m->cc.v = false;
                m->cc.z = m->b == 0;
                m->cc.n = (m->b & 0x80) != 0;
                break;
            case 0xd7:
            case 0xe7:
            case 0xf7:  // stb
                m->cc.v = false;
                m->cc.z = m->b == 0;
                m->cc.n = (m->b & 0x80) != 0;
                hd_wr(m, m->address, m->b);
                break;
            case 0xc8:
            case 0xd8:
            case 0xe8:
            case 0xf8: m->b = op_eor(m, m->b, m->operand); break;  // eorb
            case 0xc9:
            case 0xd9:
            case 0xe9:
            case 0xf9: m->b = op_adc(m, m->b, m->operand); break;  // adcb
            case 0xca:
            case 0xda:
            case 0xea:
            case 0xfa: m->b = op_or(m, m->b, m->operand); break;  // orb
            case 0xcb:
            case 0xdb:
            case 0xeb:
            case 0xfb: m->b = op_add(m, m->b, m->operand); break;  // addb
            case 0xcc:
            case 0xdc:
            case 0xec:
            case 0xfc:  // ldd
                set_d(m, m->operand_word);
                m->cc.v = false;
                m->cc.z = m->operand_word == 0;
                m->cc.n = (m->operand_word & 0x8000) != 0;
                break;
            case 0xdd:
            case 0xed:
            case 0xfd:  // std
                m->cc.v = false;
                m->cc.z = d(m) == 0;
                m->cc.n = (d(m) & 0x8000) != 0;
                write_word(m, m->address, d(m));
                break;
            case 0xce:
            case 0xde:
            case 0xee:
            case 0xfe:  // ldx
                m->x = m->operand_word;
                m->cc.v = false;
                m->cc.z = m->x == 0;
                m->cc.n = (m->x & 0x8000) != 0;
                break;
            case 0xdf:
            case 0xef:
            case 0xff:  // stx
                m->cc.v = false;
                m->cc.z = m->x == 0;
                m->cc.n = (m->x & 0x8000) != 0;
                write_word(m, m->address, m->x);
                break;
            default: break;
        }

        const uint8_t* cycles = (m->type == HD_M6800) ? kCycles6800 : (m->type == HD_M6803) ? kCycles6803 : kCycles;
        int elapsed = cycles[opcode] + m->extra_cycles;
        if (elapsed >= 99) elapsed = 1;  // invalid opcode
        executed += elapsed;
        if (m->type != HD_M6803 && m->type != HD_M6800) {
            m->counter += (uint32_t)(elapsed);
            if (m->counter >= m->timer_next) check_timer_event(m);
        }
        if (m->cycles) m->cycles(m->user, elapsed);
    }
    return executed;
}


static uint8_t no_read(void *u, uint16_t a) { (void)u, (void)a; return 0xff; }
static void no_write(void *u, uint16_t a, uint8_t v) { (void)u, (void)a, (void)v; }

void hd63701_init(hd63701 *m, uint32_t clock, int type)
{
    memset(m, 0, sizeof *m);
    m->read_mem = no_read;
    m->write_mem = no_write;
    m->clock = clock;
    m->type = type;
    m->cc.i = true;
    m->ram_control = 0x40;
    memset(m->port_in, 0xff, sizeof m->port_in);
    memset(m->portx_in, 0xff, sizeof m->portx_in);
}
