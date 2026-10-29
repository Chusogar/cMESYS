// mb88xx.c - Fujitsu MB88xx. See mb88xx.h.
#include "mb88xx.h"
#include <string.h>

#define SERIAL_DISABLE_THRESH 1000
#define TIMER_PRESCALE 32
enum { INT_SERIAL = 0x01, INT_TIMER = 0x02, INT_EXTERNAL = 0x04 };

#define RK() (m->read_k ? m->read_k(m->user) : 0)
#define RR(p) (m->read_r ? m->read_r(m->user, (p)) : 0)
#define WR(p, v) do { if (m->write_r) m->write_r(m->user, (p), (v)); } while (0)
#define WP(v) do { if (m->write_p) m->write_p(m->user, (v)); } while (0)

void mb88_init(mb88 *m, int type, const uint8_t *rom)
{
    memset(m, 0, sizeof *m);
    int pw = (type == MB8841 || type == MB8842) ? 11 : 10;
    int dw = (type == MB8841 || type == MB8842) ? 7 : 6;
    m->program_mask = (uint16_t)((1u << pw) - 1);
    m->data_mask = (uint8_t)((1u << dw) - 1);
    m->rom = rom;
    mb88_reset(m);
    m->reset_asserted = true;
}

void mb88_reset(mb88 *m)
{
    m->pc = m->pa = 0;
    m->sp[0] = m->sp[1] = m->sp[2] = m->sp[3] = 0;
    m->si = m->a = m->x = m->y = 0;
    m->st = 1;
    m->zf = m->cf = m->vf = m->sf = m->nf = 0;
    m->pio = m->th = m->tl = m->tp = m->sb = 0;
    m->sb_count = 0;
    m->pending_interrupt = 0;
    m->o_output = 0;
    m->tc = false;
    m->serial_cycle_acc = 0;
}

void mb88_set_irq(mb88 *m, bool asserted)
{
    if ((m->pio & INT_EXTERNAL) && !m->nf && asserted) m->pending_interrupt |= INT_EXTERNAL;
    m->nf = asserted ? 1 : 0;
}

static void increment_timer(mb88 *m)
{
    m->tl = (uint8_t)((m->tl + 1) & 0x0f);
    if (m->tl == 0) {
        m->th = (uint8_t)((m->th + 1) & 0x0f);
        if (m->th == 0) {
            m->vf = 1;
            m->pending_interrupt |= INT_TIMER;
        }
    }
}

void mb88_set_tc(mb88 *m, bool state)
{
    if (m->tc && !state && (m->pio & 0x40)) increment_timer(m);
    m->tc = state;
}

void mb88_set_reset_line(mb88 *m, bool asserted)
{
    if (m->reset_asserted && !asserted) mb88_reset(m);
    m->reset_asserted = asserted;
}

static inline uint8_t fetch(mb88 *m) { return m->rom ? m->rom[mb88_pc(m) & m->program_mask] : 0; }
static inline uint8_t read_data(mb88 *m, uint8_t ea) { return m->ram[ea & m->data_mask] & 0x0f; }
static inline void write_data(mb88 *m, uint8_t ea, uint8_t v) { m->ram[ea & m->data_mask] = (uint8_t)(v & 0x0f); }
static inline void inc_pc(mb88 *m)
{
    if (++m->pc >= 0x40) {
        m->pc = 0;
        m->pa++;
    }
}
static inline void update_st_c(mb88 *m, uint8_t v) { m->st = (v & 0x10) ? 0 : 1; }
static inline void update_st_z(mb88 *m, uint8_t v) { m->st = (v == 0) ? 0 : 1; }
static inline void update_cf(mb88 *m, uint8_t v) { m->cf = ((v & 0x10) == 0) ? 0 : 1; }
static inline void update_zf(mb88 *m, uint8_t v) { m->zf = (v != 0) ? 0 : 1; }

static void write_pla(mb88 *m, uint8_t index)
{
    uint8_t shift = (index & 0x10) ? 4 : 0, mask = (uint8_t)(0x0f << shift);
    m->o_output = (uint8_t)((m->o_output & (uint8_t)~mask) | ((index << shift) & mask));
    if (m->write_o) m->write_o(m->user, m->o_output);
}

static void update_pio_enable(mb88 *m, uint8_t newpio)
{
    if ((m->pio ^ newpio) & 0x30) m->serial_cycle_acc = 0;
    m->pio = newpio;
}

static void serial_tick(mb88 *m)
{
    if ((m->pio & 0x30) != 0x20) return;
    m->sb_count++;
    if (m->sb_count >= SERIAL_DISABLE_THRESH) return;
    if (!m->sf) {
        int si = m->read_si ? m->read_si(m->user) : 0;
        m->sb = (uint8_t)((m->sb >> 1) | (si ? 8 : 0));
        if (m->sb_count >= 4) {
            m->sf = 1;
            m->pending_interrupt |= INT_SERIAL;
        }
    }
}

static void update_pio(mb88 *m, int cycles)
{
    if (m->pio & 0x80) {
        m->tp = (uint8_t)(m->tp + cycles);
        while (m->tp >= TIMER_PRESCALE) {
            m->tp = (uint8_t)(m->tp - TIMER_PRESCALE);
            increment_timer(m);
        }
    }
    if ((m->pio & 0x30) == 0x20) {
        if (m->sb_count >= SERIAL_DISABLE_THRESH && m->sb_count < 0xff00 && m->serial_cycle_acc == 0) {
            m->sb_count = (uint16_t)(m->sb_count + cycles);     // (serial_tick only counts here)
            goto serial_done;
        }
        m->serial_cycle_acc += cycles;
        while (m->serial_cycle_acc >= 1) {
            m->serial_cycle_acc--;
            serial_tick(m);
        }
    }
serial_done:
    if (m->pending_interrupt & m->pio) {
        m->sp[m->si] = (uint16_t)(mb88_pc(m) | (m->cf << 15) | (m->zf << 14) | (m->st << 13));
        m->si = (uint8_t)((m->si + 1) & 3);
        if (m->pending_interrupt & m->pio & INT_EXTERNAL) m->pc = 0x02;
        else if (m->pending_interrupt & m->pio & INT_TIMER) m->pc = 0x04;
        else m->pc = 0x06;
        m->pa = 0;
        m->st = 1;
        m->pending_interrupt = 0;
        m->icount -= 3;
        m->clk += 3;
    }
}

static void execute_one(mb88 *m)
{
    uint8_t opcode = fetch(m);
    inc_pc(m);
    uint8_t oc = 1;
    uint8_t arg = 0;
    const uint8_t ea = (uint8_t)((m->x << 4) + m->y);


    switch (opcode) {
        case 0x00:
            m->st = 1;
            break;
        case 0x01:
            write_pla(m, (uint8_t)((m->cf << 4) | m->a));
            m->st = 1;
            break;
        case 0x02:
            WP(m->a);
            m->st = 1;
            break;
        case 0x03:
            WR(m->y & 3, m->a);
            m->st = 1;
            break;
        case 0x04:
            m->y = m->a;
            m->st = 1;
            break;
        case 0x05:
            m->th = m->a;
            m->st = 1;
            break;
        case 0x06:
            m->tl = m->a;
            m->st = 1;
            break;
        case 0x07:
            m->sb = m->a;
            m->st = 1;
            break;
        case 0x08:
            m->y++;
            update_st_c(m, m->y);
            m->y &= 0x0f;
            update_zf(m, m->y);
            break;
        case 0x09:
            arg = read_data(m, ea);
            arg++;
            update_st_c(m, arg);
            arg &= 0x0f;
            update_zf(m, arg);
            write_data(m, ea, arg);
            break;
        case 0x0a:
            write_data(m, ea, m->a);
            m->y++;
            update_st_c(m, m->y);
            m->y &= 0x0f;
            update_zf(m, m->y);
            break;
        case 0x0b:
            arg = read_data(m, ea);
            write_data(m, ea, m->a);
            m->a = arg;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x0c:
            m->a = (uint8_t)((m->a << 1) | m->cf);
            update_st_c(m, m->a);
            m->cf = (uint8_t)(m->st ^ 1);
            m->a &= 0x0f;
            update_zf(m, m->a);
            break;
        case 0x0d:
            m->a = read_data(m, ea);
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x0e:
            arg = (uint8_t)(read_data(m, ea) + m->a + m->cf);
            update_st_c(m, arg);
            m->cf = (uint8_t)(m->st ^ 1);
            m->a = (uint8_t)(arg & 0x0f);
            update_zf(m, m->a);
            break;
        case 0x0f:
            m->a = (uint8_t)(m->a & read_data(m, ea));
            update_zf(m, m->a);
            m->st = (uint8_t)(m->zf ^ 1);
            break;
        case 0x10:
            if (m->cf || m->a > 9) m->a = (uint8_t)(m->a + 6);
            update_st_c(m, m->a);
            m->cf = (uint8_t)(m->st ^ 1);
            m->a &= 0x0f;
            break;
        case 0x11:
            if (m->cf || m->a > 9) m->a = (uint8_t)(m->a + 10);
            update_st_c(m, m->a);
            m->cf = (uint8_t)(m->st ^ 1);
            m->a &= 0x0f;
            break;
        case 0x12:
            m->a = (uint8_t)(RK() & 0x0f);
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x13:
            m->a = (uint8_t)(RR(m->y & 3) & 0x0f);
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x14:
            m->a = m->y;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x15:
            m->a = m->th;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x16:
            m->a = m->tl;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x17:
            m->a = m->sb;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x18:
            m->y--;
            update_st_c(m, m->y);
            m->y &= 0x0f;
            break;
        case 0x19:
            arg = read_data(m, ea);
            arg--;
            update_st_c(m, arg);
            arg &= 0x0f;
            update_zf(m, arg);
            write_data(m, ea, arg);
            break;
        case 0x1a:
            write_data(m, ea, m->a);
            m->y--;
            update_st_c(m, m->y);
            m->y &= 0x0f;
            update_zf(m, m->y);
            break;
        case 0x1b:
            arg = m->x;
            m->x = m->a;
            m->a = arg;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x1c:
            m->a = (uint8_t)(m->a | (m->cf << 4));
            update_st_c(m, (uint8_t)(m->a << 4));
            m->cf = (uint8_t)(m->st ^ 1);
            m->a = (uint8_t)((m->a >> 1) & 0x0f);
            update_zf(m, m->a);
            break;
        case 0x1d:
            write_data(m, ea, m->a);
            m->st = 1;
            break;
        case 0x1e:
            arg = (uint8_t)(read_data(m, ea) - m->a - m->cf);
            update_st_c(m, arg);
            m->cf = (uint8_t)(m->st ^ 1);
            m->a = (uint8_t)(arg & 0x0f);
            update_zf(m, m->a);
            break;
        case 0x1f:
            m->a = (uint8_t)(m->a | read_data(m, ea));
            update_zf(m, m->a);
            m->st = (uint8_t)(m->zf ^ 1);
            break;
        case 0x20:
            arg = RR(m->y / 4);
            WR(m->y / 4, (uint8_t)(arg | (1 << (m->y % 4))));
            m->st = 1;
            break;
        case 0x21:
            m->cf = 1;
            m->st = 1;
            break;
        case 0x22:
            arg = RR(m->y / 4);
            WR(m->y / 4, (uint8_t)(arg & ~(1 << (m->y % 4))));
            m->st = 1;
            break;
        case 0x23:
            m->cf = 0;
            m->st = 1;
            break;
        case 0x24:
            arg = RR(m->y / 4);
            m->st = (arg & (1 << (m->y % 4))) ? 0 : 1;
            break;
        case 0x25:
            m->st = (uint8_t)(m->nf ^ 1);
            break;
        case 0x26:
            m->st = (uint8_t)(m->vf ^ 1);
            m->vf = 0;
            break;
        case 0x27:
            m->st = (uint8_t)(m->sf ^ 1);
            if (m->sf) m->sb_count = 0;
            m->sf = 0;
            break;
        case 0x28:
            m->st = (uint8_t)(m->cf ^ 1);
            break;
        case 0x29:
            m->st = (uint8_t)(m->zf ^ 1);
            break;
        case 0x2a:
            write_data(m, ea, m->sb);
            update_zf(m, m->sb);
            m->st = 1;
            break;
        case 0x2b:
            m->sb = read_data(m, ea);
            update_zf(m, m->sb);
            m->st = 1;
            break;
        case 0x2c:
            m->si = (uint8_t)((m->si - 1) & 3);
            m->pc = (uint8_t)(m->sp[m->si] & 0x3f);
            m->pa = (uint8_t)((m->sp[m->si] >> 6) & 0x1f);
            m->st = 1;
            break;
        case 0x2d:
            m->a = (uint8_t)(((~m->a) + 1) & 0x0f);
            update_st_z(m, m->a);
            break;
        case 0x2e:
            arg = (uint8_t)(read_data(m, ea) - m->a);
            update_cf(m, arg);
            arg &= 0x0f;
            update_st_z(m, arg);
            m->zf = (uint8_t)(m->st ^ 1);
            break;
        case 0x2f:
            m->a = (uint8_t)(m->a ^ read_data(m, ea));
            update_st_z(m, m->a);
            m->zf = (uint8_t)(m->st ^ 1);
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            arg = read_data(m, ea);
            write_data(m, ea, (uint8_t)(arg | (1 << (opcode & 3))));
            m->st = 1;
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            arg = read_data(m, ea);
            write_data(m, ea, (uint8_t)(arg & ~(1 << (opcode & 3))));
            m->st = 1;
            break;
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
            arg = read_data(m, ea);
            m->st = (arg & (1 << (opcode & 3))) ? 0 : 1;
            break;
        case 0x3c:
            m->si = (uint8_t)((m->si - 1) & 3);
            m->pc = (uint8_t)(m->sp[m->si] & 0x3f);
            m->pa = (uint8_t)((m->sp[m->si] >> 6) & 0x1f);
            m->st = (uint8_t)((m->sp[m->si] >> 13) & 1);
            m->zf = (uint8_t)((m->sp[m->si] >> 14) & 1);
            m->cf = (uint8_t)((m->sp[m->si] >> 15) & 1);
            break;
        case 0x3d:
            m->pa = (uint8_t)(fetch(m) & 0x1f);
            m->pc = (uint8_t)(m->a * 4);
            oc = 2;
            m->st = 1;
            break;
        case 0x3e:
            update_pio_enable(m, (uint8_t)(m->pio | fetch(m)));
            inc_pc(m);
            oc = 2;
            m->st = 1;
            break;
        case 0x3f:
            update_pio_enable(m, (uint8_t)(m->pio & ~fetch(m)));
            inc_pc(m);
            oc = 2;
            m->st = 1;
            break;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            arg = RR(0);
            WR(0, (uint8_t)(arg | (1 << (opcode & 3))));
            m->st = 1;
            break;
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
            arg = RR(0);
            WR(0, (uint8_t)(arg & ~(1 << (opcode & 3))));
            m->st = 1;
            break;
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            arg = RR(2);
            m->st = (arg & (1 << (opcode & 3))) ? 0 : 1;
            break;
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            m->st = (m->a & (1 << (opcode & 3))) ? 0 : 1;
            break;
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
            arg = read_data(m, (uint8_t)(opcode & 3));
            write_data(m, (uint8_t)(opcode & 3), m->a);
            m->a = arg;
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
            arg = read_data(m, (uint8_t)((opcode & 3) + 4));
            write_data(m, (uint8_t)((opcode & 3) + 4), m->y);
            m->y = arg;
            update_zf(m, m->y);
            m->st = 1;
            break;
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
        case 0x5c:
        case 0x5d:
        case 0x5e:
        case 0x5f:
            m->x = (uint8_t)(opcode & 7);
            update_zf(m, m->x);
            m->st = 1;
            break;
        case 0x60:
        case 0x61:
        case 0x62:
        case 0x63:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
            arg = fetch(m);
            inc_pc(m);
            oc = 2;
            if (m->st) {
                m->sp[m->si] = mb88_pc(m);
                m->si = (uint8_t)((m->si + 1) & 3);
                m->pc = (uint8_t)(arg & 0x3f);
                m->pa = (uint8_t)(((opcode & 7) << 2) | (arg >> 6));
            }
            m->st = 1;
            break;
        case 0x68:
        case 0x69:
        case 0x6a:
        case 0x6b:
        case 0x6c:
        case 0x6d:
        case 0x6e:
        case 0x6f:
            arg = fetch(m);
            inc_pc(m);
            oc = 2;
            if (m->st) {
                m->pc = (uint8_t)(arg & 0x3f);
                m->pa = (uint8_t)(((opcode & 7) << 2) | (arg >> 6));
            }
            m->st = 1;
            break;
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78:
        case 0x79:
        case 0x7a:
        case 0x7b:
        case 0x7c:
        case 0x7d:
        case 0x7e:
        case 0x7f:
            arg = (uint8_t)((opcode & 0x0f) + m->a);
            update_st_c(m, arg);
            m->cf = (uint8_t)(m->st ^ 1);
            m->a = (uint8_t)(arg & 0x0f);
            update_zf(m, m->a);
            break;
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x8f:
            m->y = (uint8_t)(opcode & 0x0f);
            update_zf(m, m->y);
            m->st = 1;
            break;
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0x98:
        case 0x99:
        case 0x9a:
        case 0x9b:
        case 0x9c:
        case 0x9d:
        case 0x9e:
        case 0x9f:
            m->a = (uint8_t)(opcode & 0x0f);
            update_zf(m, m->a);
            m->st = 1;
            break;
        case 0xa0:
        case 0xa1:
        case 0xa2:
        case 0xa3:
        case 0xa4:
        case 0xa5:
        case 0xa6:
        case 0xa7:
        case 0xa8:
        case 0xa9:
        case 0xaa:
        case 0xab:
        case 0xac:
        case 0xad:
        case 0xae:
        case 0xaf:
            arg = (uint8_t)((opcode & 0x0f) - m->y);
            update_cf(m, arg);
            arg &= 0x0f;
            update_st_z(m, arg);
            m->zf = (uint8_t)(m->st ^ 1);
            break;
        case 0xb0:
        case 0xb1:
        case 0xb2:
        case 0xb3:
        case 0xb4:
        case 0xb5:
        case 0xb6:
        case 0xb7:
        case 0xb8:
        case 0xb9:
        case 0xba:
        case 0xbb:
        case 0xbc:
        case 0xbd:
        case 0xbe:
        case 0xbf:
            arg = (uint8_t)((opcode & 0x0f) - m->a);
            update_cf(m, arg);
            arg &= 0x0f;
            update_st_z(m, arg);
            m->zf = (uint8_t)(m->st ^ 1);
            break;
        default:
            if (m->st) m->pc = (uint8_t)(opcode & 0x3f);
            m->st = 1;
            break;
    }

    m->icount -= oc;
    m->clk += oc;
    update_pio(m, oc);
}

void mb88_set_idle_loop(mb88 *m, uint16_t pc, int cycles)
{
    m->idle_pc = pc;
    m->idle_cyc = (uint8_t)cycles;
    m->idle_clk = 0;
}

static void idle_check(mb88 *m)
{
    const bool same = m->clk - m->idle_clk == m->idle_cyc && m->tl == m->idle_tl && m->nf == m->idle_nf &&
                      m->a == m->idle_a && m->st == m->idle_st;
    m->idle_clk = m->clk;
    m->idle_tl = m->tl;
    m->idle_nf = m->nf;
    m->idle_a = m->a;
    m->idle_st = m->st;
    const bool serial = (m->pio & 0x30) == 0x20;
    // (a serial port left on stops reading after SERIAL_DISABLE_THRESH bits)
    if (!same || m->pending_interrupt || (serial && m->sb_count < SERIAL_DISABLE_THRESH)) return;
    int n = (m->icount + m->idle_ahead) / m->idle_cyc;  // whole iterations left in this call (+ ahead)
    if (serial) {                                       // its counter must not wrap round
        int room = (0xffff - m->sb_count) / m->idle_cyc;
        if (room < n) n = room;
    }
    if (m->pio & 0x80) {                                // timer running: up to its next tick
        int room = (TIMER_PRESCALE - 1 - m->tp) / m->idle_cyc;
        if (room < n) n = room;
    }
    if (n <= 0) return;
    const int c = n * m->idle_cyc;
    m->icount -= c;
    m->clk += (uint32_t)c;
    m->idle_clk = m->clk;
    if (m->pio & 0x80) m->tp = (uint8_t)(m->tp + c);
    if (serial) m->sb_count = (uint16_t)(m->sb_count + c);
}

int mb88_run(mb88 *m, int cycles)
{
    if (m->reset_asserted) { m->icount = 0; return 0; }
    // the overshoot of the last instruction is paid from the next call, so
    // many short calls keep the right speed
    m->icount += cycles;
    while (m->icount > 0) {
        if (m->idle_cyc && mb88_pc(m) == m->idle_pc) idle_check(m);
        if (m->icount > 0) execute_one(m);
    }
    return cycles;
}
