// mcs48.c - Intel MCS-48 / UPI-41. See mcs48.h.
#include "mcs48.h"
#include <string.h>

#ifndef MCS48_HOT
#define MCS48_HOT
#endif

enum { STS_IBF = 0x02, STS_OBF = 0x01, TIMER_ON = 0x01, COUNTER_ON = 0x02,
       P2_OBF = 0x10, P2_NIBF = 0x20, P2_DRQ = 0x40, P2_NDACK = 0x80 };
enum { EXP_READ = 0, EXP_WRITE = 1, EXP_OR = 2, EXP_AND = 3 };

static const uint8_t cycles_tab[256] = {
    1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,  // 00
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 10
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 20
    1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,  // 30
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 40
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 50
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 60
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 70
    2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,  // 80
    1, 1, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,  // 90
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // a0
    2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // b0
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // c0
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // d0
    1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,  // e0
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // f0
};

static inline void update_regptr(mcs48 *c) { c->bank_base = (c->psw & 0x10) ? 24 : 0; }
static inline void set_psw(mcs48 *c, uint8_t v) { c->psw = v; update_regptr(c); }
static inline uint8_t R(const mcs48 *c, int n) { return c->ram[c->bank_base + n]; }
static inline void setR(mcs48 *c, int n, uint8_t v) { c->ram[c->bank_base + n] = v; }
static inline uint8_t ind(const mcs48 *c, int n) { return c->ram[R(c, n) & c->ram_mask]; }
static inline void set_ind(mcs48 *c, int n, uint8_t v) { c->ram[R(c, n) & c->ram_mask] = v; }
static inline uint8_t prog(const mcs48 *c, uint16_t a) { return c->rom[a & c->rom_mask]; }
static inline uint8_t fetch(mcs48 *c)
{
    const uint8_t v = prog(c, c->pc);
    c->pc = (uint16_t)(((c->pc + 1) & 0x7ff) | (c->pc & 0x800));
    return v;
}
static inline uint8_t test_r(mcs48 *c, int which) { return c->port_in ? c->port_in(c->user, (uint16_t)(MCS48_T0 + which)) : 0xff; }
static inline uint8_t bus_r(mcs48 *c) { return c->port_in ? c->port_in(c->user, MCS48_BUS) : 0xff; }
static inline void bus_w(mcs48 *c, uint8_t v) { if (c->port_out) c->port_out(c->user, MCS48_BUS, v); }
static inline uint8_t port_r(mcs48 *c, int p) { return c->port_in ? c->port_in(c->user, (uint16_t)(MCS48_P0 + p)) : 0xff; }
static inline void port_w(mcs48 *c, int p, uint8_t v) { if (c->port_out) c->port_out(c->user, (uint16_t)(MCS48_P0 + p), v); }

static void expander(mcs48 *c, int op, uint8_t port)
{
    c->p2 = (uint8_t)((c->p2 & 0xf0) | (op << 2) | (port & 3));
    port_w(c, 2, c->p2);
    if (c->port_out) c->port_out(c->user, MCS48_PROG, 0);
    if (op != EXP_READ) {
        c->p2 = (uint8_t)((c->p2 & 0xf0) | (c->a & 0x0f));
        port_w(c, 2, c->p2);
    } else {
        c->p2 |= 0x0f;
        port_w(c, 2, c->p2);
        c->a = (uint8_t)(port_r(c, 2) & 0x0f);
    }
    if (c->port_out) c->port_out(c->user, MCS48_PROG, 1);
}

static void push_pc_psw(mcs48 *c)
{
    const uint8_t sp = c->psw & 7;
    c->ram[8 + 2 * sp] = (uint8_t)c->pc;
    c->ram[9 + 2 * sp] = (uint8_t)(((c->pc >> 8) & 0x0f) | (c->psw & 0xf0));
    set_psw(c, (uint8_t)((c->psw & 0xf0) | ((sp + 1) & 7)));
}

static inline void cond(mcs48 *c, bool t)
{
    const uint16_t dest = (uint16_t)((c->pc & 0xf00) | fetch(c));
    if (t) c->pc = dest;
}

static int check_irqs(mcs48 *c)
{
    if (c->irq_in_progress) return 0;
    if ((c->irq || (c->sts & STS_IBF)) && c->xirq_enabled) {
        if (c->irq_polled) {
            c->pc = (uint16_t)(((c->old_pc + 1) & 0x7ff) | (c->old_pc & 0x800));
            cond(c, true);
        }
        push_pc_psw(c);
        c->pc = 0x03;
        c->irq_in_progress = true;
        return 2;
    }
    if (c->timer_overflow && c->tirq_enabled) {
        push_pc_psw(c);
        c->pc = 0x07;
        c->irq_in_progress = true;
        c->timer_overflow = false;
        return 2;
    }
    return 0;
}

static void burn_cycles(mcs48 *c, uint8_t count)
{
    bool over = false;
    if (c->timecount == TIMER_ON) {
        const uint8_t old = c->timer;
        c->prescaler = (uint8_t)(c->prescaler + count);
        c->timer = (uint8_t)(c->timer + (c->prescaler >> 5));
        c->prescaler &= 0x1f;
        over = old != 0 && c->timer == 0;
    } else if (c->timecount == COUNTER_ON) {
        for (uint8_t i = 0; i < count; i++) {
            c->t1_history = (uint8_t)((c->t1_history << 1) | (test_r(c, 1) & 1));
            if ((c->t1_history & 3) == 2) {
                c->timer++;
                over = c->timer == 0;
            }
        }
    }
    if (over) {
        c->timer_flag = true;
        if (c->tirq_enabled) c->timer_overflow = true;
    }
}

static inline uint8_t p2_mask(const mcs48 *c)
{
    uint8_t m = 0xff;
    if (!c->upi41) return m;
    if (c->flags_enabled) m &= (uint8_t)~(P2_OBF | P2_NIBF);
    if (c->dma_enabled) m &= (uint8_t)~(P2_DRQ | P2_NDACK);
    return m;
}

static inline void add(mcs48 *c, uint8_t v)
{
    const uint16_t r = (uint16_t)(c->a + v), n = (uint16_t)((c->a & 15) + (v & 15));
    c->psw = (uint8_t)((c->psw & 0x3f) | ((n & 0x10) ? 0x40 : 0) | ((r & 0x100) ? 0x80 : 0));
    c->a = (uint8_t)r;
}
static inline void addc(mcs48 *c, uint8_t v)
{
    const uint8_t cy = (c->psw & 0x80) != 0;
    const uint16_t r = (uint16_t)(c->a + v + cy), n = (uint16_t)((c->a & 15) + (v & 15) + cy);
    c->psw = (uint8_t)((c->psw & 0x3f) | ((n & 0x10) ? 0x40 : 0) | ((r & 0x100) ? 0x80 : 0));
    c->a = (uint8_t)r;
}

void mcs48_init(mcs48 *c, int chip, const uint8_t *rom)
{
    memset(c, 0, sizeof *c);
    c->rom = rom;
    c->chip = (uint8_t)chip;
    switch (chip) {
    case MCS48_I8035: c->rom_mask = 0x7ff; c->ram_mask = 0x3f; break;
    case MCS48_I8039: c->rom_mask = 0x7ff; c->ram_mask = 0x7f; break;
    case MCS48_N7751: c->rom_mask = 0x3ff; c->ram_mask = 0x3f; break;
    case MCS48_I8042: c->rom_mask = 0x7ff; c->ram_mask = 0x7f; c->upi41 = true; break;
    default:          c->rom_mask = 0x7ff; c->ram_mask = 0x7f; break;    // 8749
    }
}

void mcs48_reset(mcs48 *c)
{
    c->pc = c->old_pc = 0;
    set_psw(c, 0);
    c->f1 = false;
    c->a11 = 0;
    c->a = 0;
    c->dbbo = 0xff;
    bus_w(c, 0xff);
    c->p1 = c->p2 = 0xff;
    port_w(c, 1, c->p1);
    port_w(c, 2, c->p2);
    c->tirq_enabled = c->xirq_enabled = false;
    c->timecount = 0;
    c->timer_flag = false;
    c->sts = 0;
    c->flags_enabled = c->dma_enabled = false;
    c->irq_in_progress = c->timer_overflow = c->irq_polled = false;
    c->irq = c->reset_line = false;
    c->timer = c->prescaler = c->t1_history = 0;
    c->dbbi = 0;
}

uint8_t mcs48_upi41_master_r(mcs48 *c, uint8_t address)
{
    if (address & 1) return (uint8_t)((c->sts & 0xf3) | (c->f1 ? 8 : 0) | ((c->psw & 0x20) ? 4 : 0));
    if (c->sts & STS_OBF) {
        c->sts &= (uint8_t)~STS_OBF;
        if (c->flags_enabled) { c->p2 &= (uint8_t)~P2_OBF; port_w(c, 2, c->p2); }
    }
    return c->dbbo;
}

void mcs48_upi41_master_w(mcs48 *c, uint8_t address, uint8_t value)
{
    c->dbbi = value;
    if (!(c->sts & STS_IBF)) {
        c->sts |= STS_IBF;
        if (c->flags_enabled) { c->p2 &= (uint8_t)~P2_NIBF; port_w(c, 2, c->p2); }
    }
    c->f1 = (address & 1) != 0;
}

int MCS48_HOT mcs48_step(mcs48 *c)
{
    int extra = check_irqs(c);
    c->old_pc = c->pc;
    const uint8_t op = fetch(c);
    const bool upi = c->upi41;
    uint8_t t;
    switch (op) {
    case 0x00: break;
    case 0x02:
        if (upi) {
            c->dbbo = c->a;
            c->sts |= STS_OBF;
            if (c->flags_enabled && !(c->p2 & P2_OBF)) { c->p2 |= P2_OBF; port_w(c, 2, c->p2); }
        } else bus_w(c, c->a);
        break;
    case 0x03: add(c, fetch(c)); break;
    case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xa4: case 0xc4: case 0xe4: {
        const uint8_t off = fetch(c);
        c->pc = (uint16_t)(off | (c->irq_in_progress ? 0 : c->a11) | ((op >> 5) << 8));
        break;
    }
    case 0x05: c->xirq_enabled = true; break;
    case 0x07: c->a--; break;
    case 0x08: if (!upi) c->a = bus_r(c); break;
    case 0x09: c->a = (uint8_t)(port_r(c, 1) & c->p1); break;
    case 0x0a: c->a = (uint8_t)(port_r(c, 2) & c->p2); break;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f: expander(c, EXP_READ, (uint8_t)(4 + (op & 3))); break;
    case 0x10: case 0x11: set_ind(c, op & 1, (uint8_t)(ind(c, op & 1) + 1)); break;
    case 0x12: cond(c, c->a & 1); break;
    case 0x13: addc(c, fetch(c)); break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xb4: case 0xd4: case 0xf4: {
        const uint8_t off = fetch(c);
        push_pc_psw(c);
        c->pc = (uint16_t)(off | (c->irq_in_progress ? 0 : c->a11) | ((op >> 5) << 8));
        break;
    }
    case 0x15: c->xirq_enabled = false; break;
    case 0x16: cond(c, c->timer_flag); c->timer_flag = false; break;
    case 0x17: c->a++; break;
    case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        setR(c, op & 7, (uint8_t)(R(c, op & 7) + 1)); break;
    case 0x20: case 0x21: t = c->a; c->a = ind(c, op & 1); set_ind(c, op & 1, t); break;
    case 0x22:
        if (upi) {
            c->sts &= (uint8_t)~STS_IBF;
            if (c->flags_enabled && !(c->p2 & P2_NIBF)) { c->p2 |= P2_NIBF; port_w(c, 2, c->p2); }
            c->a = c->dbbi;
        }
        break;
    case 0x23: c->a = fetch(c); break;
    case 0x25: c->tirq_enabled = true; break;
    case 0x26: cond(c, test_r(c, 0) == 0); break;
    case 0x27: c->a = 0; break;
    case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
        t = c->a; c->a = R(c, op & 7); setR(c, op & 7, t); break;
    case 0x30: case 0x31: {
        const uint8_t mem = ind(c, op & 1), acc = c->a;
        c->a = (uint8_t)((acc & 0xf0) | (mem & 0x0f));
        set_ind(c, op & 1, (uint8_t)((mem & 0xf0) | (acc & 0x0f)));
        break;
    }
    case 0x32: cond(c, c->a & 2); break;
    case 0x35: c->tirq_enabled = false; c->timer_overflow = false; break;
    case 0x36: cond(c, test_r(c, 0) != 0); break;
    case 0x37: c->a ^= 0xff; break;
    case 0x39: c->p1 = c->a; port_w(c, 1, c->p1); break;
    case 0x3a: { const uint8_t m = p2_mask(c); c->p2 = (uint8_t)((c->p2 & ~m) | (c->a & m)); port_w(c, 2, c->p2); break; }
    case 0x3c: case 0x3d: case 0x3e: case 0x3f: expander(c, EXP_WRITE, (uint8_t)(4 + (op & 3))); break;
    case 0x40: case 0x41: c->a |= ind(c, op & 1); break;
    case 0x42: c->a = c->timer; break;
    case 0x43: c->a |= fetch(c); break;
    case 0x45:
        if (c->timecount != COUNTER_ON) c->t1_history = test_r(c, 1);
        c->timecount = COUNTER_ON;
        break;
    case 0x46: cond(c, test_r(c, 1) == 0); break;
    case 0x47: c->a = (uint8_t)((c->a << 4) | (c->a >> 4)); break;
    case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: c->a |= R(c, op & 7); break;
    case 0x50: case 0x51: c->a &= ind(c, op & 1); break;
    case 0x52: cond(c, c->a & 4); break;
    case 0x53: c->a &= fetch(c); break;
    case 0x55: c->timecount = TIMER_ON; c->prescaler = 0; break;
    case 0x56: cond(c, test_r(c, 1) != 0); break;
    case 0x57:
        if ((c->a & 0x0f) > 0x09 || (c->psw & 0x40)) {
            if (c->a > 0xf9) c->psw |= 0x80;
            c->a = (uint8_t)(c->a + 0x06);
        }
        if ((c->a & 0xf0) > 0x90 || (c->psw & 0x80)) {
            c->a = (uint8_t)(c->a + 0x60);
            c->psw |= 0x80;
        }
        break;
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: c->a &= R(c, op & 7); break;
    case 0x60: case 0x61: add(c, ind(c, op & 1)); break;
    case 0x62: c->timer = c->a; break;
    case 0x65: c->timecount = 0; break;
    case 0x67: t = c->a; c->a = (uint8_t)((c->a >> 1) | ((c->psw & 0x80) ? 0x80 : 0));
               c->psw = (uint8_t)((c->psw & 0x7f) | ((t & 1) ? 0x80 : 0)); break;
    case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f: add(c, R(c, op & 7)); break;
    case 0x70: case 0x71: addc(c, ind(c, op & 1)); break;
    case 0x72: cond(c, c->a & 8); break;
    case 0x75: break;
    case 0x76: cond(c, c->f1); break;
    case 0x77: c->a = (uint8_t)((c->a >> 1) | (c->a << 7)); break;
    case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f: addc(c, R(c, op & 7)); break;
    case 0x80: case 0x81: if (!upi) c->a = c->ext_in ? c->ext_in(c->user, R(c, op & 1)) : 0xff; break;
    case 0x83: {
        const uint8_t sp = (uint8_t)((c->psw - 1) & 7);
        c->pc = (uint16_t)(c->ram[8 + 2 * sp] | (c->ram[9 + 2 * sp] << 8));
        c->pc &= c->irq_in_progress ? 0x7ff : 0xfff;
        set_psw(c, (uint8_t)((c->psw & 0xf0) | sp));
        break;
    }
    case 0x85: c->psw &= (uint8_t)~0x20; break;
    case 0x86:
        if (upi) cond(c, c->sts & STS_OBF);
        else { c->irq_polled = c->irq; cond(c, c->irq); }
        break;
    case 0x88: bus_w(c, (uint8_t)(bus_r(c) | fetch(c))); break;
    case 0x89: c->p1 |= fetch(c); port_w(c, 1, c->p1); break;
    case 0x8a: c->p2 |= (uint8_t)(fetch(c) & p2_mask(c)); port_w(c, 2, c->p2); break;
    case 0x8c: case 0x8d: case 0x8e: case 0x8f: expander(c, EXP_OR, (uint8_t)(4 + (op & 3))); break;
    case 0x90:
        if (upi) c->sts = (uint8_t)((c->sts & 0x0f) | (c->a & 0xf0));
        else { extra++; if (c->ext_out) c->ext_out(c->user, R(c, 0), c->a); }
        break;
    case 0x91: extra++; if (!upi && c->ext_out) c->ext_out(c->user, R(c, 1), c->a); break;
    case 0x92: cond(c, c->a & 0x10); break;
    case 0x93: {
        c->irq_in_progress = false;
        const uint8_t sp = (uint8_t)((c->psw - 1) & 7);
        c->pc = (uint16_t)(c->ram[8 + 2 * sp] | (c->ram[9 + 2 * sp] << 8));
        set_psw(c, (uint8_t)(((c->pc >> 8) & 0xf0) | sp));
        c->pc &= 0xfff;
        break;
    }
    case 0x95: c->psw ^= 0x20; break;
    case 0x96: cond(c, c->a != 0); break;
    case 0x97: c->psw &= (uint8_t)~0x80; break;
    case 0x98: bus_w(c, (uint8_t)(bus_r(c) & fetch(c))); break;
    case 0x99: c->p1 &= fetch(c); port_w(c, 1, c->p1); break;
    case 0x9a: c->p2 &= (uint8_t)(fetch(c) | (uint8_t)~p2_mask(c)); port_w(c, 2, c->p2); break;
    case 0x9c: case 0x9d: case 0x9e: case 0x9f: expander(c, EXP_AND, (uint8_t)(4 + (op & 3))); break;
    case 0xa0: case 0xa1: set_ind(c, op & 1, c->a); break;
    case 0xa3: c->a = prog(c, (uint16_t)((c->pc & 0xf00) | c->a)); break;
    case 0xa5: c->f1 = false; break;
    case 0xa7: c->psw ^= 0x80; break;
    case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf: setR(c, op & 7, c->a); break;
    case 0xb0: case 0xb1: set_ind(c, op & 1, fetch(c)); break;
    case 0xb2: cond(c, c->a & 0x20); break;
    case 0xb3: c->pc = (uint16_t)((c->pc & 0xf00) | prog(c, (uint16_t)((c->pc & 0xf00) | c->a))); break;
    case 0xb5: c->f1 = !c->f1; break;
    case 0xb6: cond(c, c->psw & 0x20); break;
    case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf: setR(c, op & 7, fetch(c)); break;
    case 0xc5: c->psw &= (uint8_t)~0x10; update_regptr(c); break;
    case 0xc6: cond(c, c->a == 0); break;
    case 0xc7: c->a = (uint8_t)(c->psw | 0x08); break;
    case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
        setR(c, op & 7, (uint8_t)(R(c, op & 7) - 1)); break;
    case 0xd0: case 0xd1: c->a ^= ind(c, op & 1); break;
    case 0xd2: cond(c, c->a & 0x40); break;
    case 0xd3: c->a ^= fetch(c); break;
    case 0xd5: c->psw |= 0x10; update_regptr(c); break;
    case 0xd6: if (upi) { c->irq_polled = (c->sts & STS_IBF) != 0; cond(c, !(c->sts & STS_IBF)); } break;
    case 0xd7: set_psw(c, (uint8_t)(c->a & 0xf7)); break;
    case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf: c->a ^= R(c, op & 7); break;
    case 0xe3: c->a = prog(c, (uint16_t)(0x300 | c->a)); break;
    case 0xe5: if (!upi) c->a11 = 0; break;
    case 0xe6: cond(c, !(c->psw & 0x80)); break;
    case 0xe7: c->a = (uint8_t)((c->a << 1) | (c->a >> 7)); break;
    case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef: {
        const uint8_t v = (uint8_t)(R(c, op & 7) - 1);
        setR(c, op & 7, v);
        cond(c, v != 0);
        break;
    }
    case 0xf0: case 0xf1: c->a = ind(c, op & 1); break;
    case 0xf2: cond(c, c->a & 0x80); break;
    case 0xf5: if (!upi) c->a11 = 0x800; break;
    case 0xf6: cond(c, c->psw & 0x80); break;
    case 0xf7: t = c->a; c->a = (uint8_t)((c->a << 1) | ((c->psw & 0x80) ? 1 : 0));
               c->psw = (uint8_t)((c->psw & 0x7f) | ((t & 0x80) ? 0x80 : 0)); break;
    case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff: c->a = R(c, op & 7); break;
    default: break;
    }
    const int spent = cycles_tab[op] + extra;
    if (c->timecount) burn_cycles(c, (uint8_t)spent);
    return spent;
}

int mcs48_run(mcs48 *c, int cycles)
{
    int done = 0;
    update_regptr(c);
    while (done < cycles) {
        if (c->reset_line) {
            mcs48_reset(c);
            c->reset_line = true;
            return cycles;
        }
        done += mcs48_step(c);
    }
    return done;
}
