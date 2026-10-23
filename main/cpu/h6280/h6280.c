// h6280.c - Hudson HuC6280. C port of MAME's cpu/h6280/h6280.cpp (non-lazy
// flags). See h6280.h.
#include "h6280.h"
#include <string.h>

// (in flash by default on the ESP32: the instruction switch is ~24 KB; a
// driver may define H6280_HOT as IRAM_ATTR)
#ifndef H6280_HOT
#define H6280_HOT
#endif

#define fC 0x01
#define fZ 0x02
#define fI 0x04
#define fD 0x08
#define fB 0x10
#define fT 0x20
#define fV 0x40
#define fN 0x80

#define NMI_VEC   0xfffc
#define RESET_VEC 0xfffe
#define IRQ1_VEC  0xfff8
#define IRQ2_VEC  0xfff6
#define TIMER_VEC 0xfffa

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------
static inline uint32_t translated(const h6280 *c, uint16_t addr)
{
    return ((uint32_t)c->mmr[(addr >> 13) & 7] << 13) | (addr & 0x1fff);
}

static uint8_t internal_r(h6280 *c, uint32_t a)
{
    if (a < 0x1fec00) return c->io_buffer;                                   // PSG
    if (a < 0x1ff000) return (uint8_t)(((c->timer_value >> 10) & 0x7f) | (c->io_buffer & 0x80));
    if (a < 0x1ff400) return c->io_buffer;                                   // port (no input)
    switch (a & 3) {                                                         // interrupts
    case 3: {
        uint8_t st = 0;
        if (c->irq_state[1]) st |= 1;
        if (c->irq_state[0]) st |= 2;
        if (c->irq_state[2]) st |= 4;
        return (uint8_t)(st | (c->io_buffer & ~7));
    }
    case 2: return (uint8_t)(c->irq_mask | (c->io_buffer & ~7));
    default: return c->io_buffer;
    }
}

static inline void check_irq_lines(h6280 *c)
{
    if (!c->irq_pending) c->irq_pending = 2;
}

static void internal_w(h6280 *c, uint32_t a, uint8_t v)
{
    c->io_buffer = v;
    if (a < 0x1fec00) return;                                                // PSG (not emulated)
    if (a < 0x1ff000) {                                                      // timer
        if (!(a & 1)) {
            c->timer_load = ((v & 127) + 1) * 1024;
        } else {
            if ((v & 1) && !c->timer_status) c->timer_value = c->timer_load;
            c->timer_status = v & 1;
        }
        return;
    }
    if (a < 0x1ff400) return;                                                // port
    switch (a & 3) {
    case 2: c->irq_mask = v & 7; check_irq_lines(c); break;
    case 3: h6280_set_irq_line(c, H6280_IRQT, H6280_CLEAR); break;
    default: break;
    }
}

static inline uint8_t phys_r(h6280 *c, uint32_t a)
{
    const uint8_t *p = c->bank_r[a >> 13];
    if (p) return p[a & 0x1fff];
    if (a >= 0x1fe800 && a < 0x1ff800) return internal_r(c, a);
    return c->read(c->user, a);
}

static inline void phys_w(h6280 *c, uint32_t a, uint8_t v)
{
    uint8_t *p = c->bank_w[a >> 13];
    if (p) { p[a & 0x1fff] = v; return; }
    if (a >= 0x1fe800 && a < 0x1ff800) { internal_w(c, a, v); return; }
    c->write(c->user, a, v);
}

static inline void cycles(h6280 *c, int n)
{
    const int k = n * c->clocks_per_cycle;
    c->icount -= k;
    c->timer_value -= k;
}

static inline void vdc_penalty(h6280 *c, uint16_t addr)
{
    if ((translated(c, addr) & 0x1ff800) == 0x1fe000) cycles(c, 1);
}

static inline uint8_t rd8(h6280 *c, uint16_t addr) { vdc_penalty(c, addr); return phys_r(c, translated(c, addr)); }
static inline void wr8(h6280 *c, uint16_t addr, uint8_t v) { vdc_penalty(c, addr); phys_w(c, translated(c, addr), v); }
static inline uint32_t zpa(const h6280 *c, uint16_t addr) { return ((uint32_t)c->mmr[1] << 13) | (addr & 0x1fff); }
static inline uint8_t rd8z(h6280 *c, uint16_t addr) { return phys_r(c, zpa(c, addr)); }
static inline void wr8z(h6280 *c, uint16_t addr, uint8_t v) { phys_w(c, zpa(c, addr), v); }
static inline uint16_t rd16(h6280 *c, uint16_t addr)
{
    return (uint16_t)(phys_r(c, translated(c, addr)) | (phys_r(c, translated(c, (uint16_t)(addr + 1))) << 8));
}
static inline uint16_t rd16z(h6280 *c, uint16_t addr)
{
    const uint16_t hi = (addr & 0xff) == 0xff ? (uint16_t)(addr - 0xff) : (uint16_t)(addr + 1);
    return (uint16_t)(rd8z(c, addr) | (rd8z(c, hi) << 8));
}
static inline void push(h6280 *c, uint8_t v) { phys_w(c, ((uint32_t)c->mmr[1] << 13) | 0x100 | c->s, v); c->s--; }
static inline uint8_t pull(h6280 *c) { c->s++; return phys_r(c, ((uint32_t)c->mmr[1] << 13) | 0x100 | c->s); }
static inline uint8_t arg(h6280 *c) { return phys_r(c, translated(c, c->pc)); }

// ---------------------------------------------------------------------------
// Flags, interrupts
// ---------------------------------------------------------------------------
static inline void set_nz(h6280 *c, uint8_t n)
{
    c->p = (uint8_t)((c->p & ~(fN | fT | fZ)) | (n & fN) | (n == 0 ? fZ : 0));
}
static inline void clear_t(h6280 *c) { c->p &= (uint8_t)~fT; }

static void do_interrupt(h6280 *c, uint16_t vector)
{
    cycles(c, 7);
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    c->p &= (uint8_t)~fB;
    push(c, c->p);
    c->p = (uint8_t)((c->p & ~fD) | fI);
    c->pc = (uint16_t)(rd8(c, vector) | (rd8(c, (uint16_t)(vector + 1)) << 8));
}

static void check_and_take_irq_lines(h6280 *c)
{
    if (c->nmi_state) {
        c->nmi_state = 0;
        do_interrupt(c, NMI_VEC);
    } else if (!(c->p & fI)) {
        if (c->irq_state[2] && !(c->irq_mask & 4)) do_interrupt(c, TIMER_VEC);
        else if (c->irq_state[0] && !(c->irq_mask & 2)) do_interrupt(c, IRQ1_VEC);
        else if (c->irq_state[1] && !(c->irq_mask & 1)) do_interrupt(c, IRQ2_VEC);
    }
}

void h6280_set_irq_line(h6280 *c, int line, int state)
{
    if (line == H6280_NMI) {
        if (state != H6280_ASSERT) return;
        c->nmi_state = 1;
        check_irq_lines(c);
    } else if (line < 3) {
        if (c->irq_state[line] == (uint8_t)state) return;
        c->irq_state[line] = (uint8_t)state;
        check_irq_lines(c);
    }
}

// ---------------------------------------------------------------------------
// Addressing
// ---------------------------------------------------------------------------
static inline void ea_zpg(h6280 *c) { c->zpl = arg(c); c->pc++; c->ea = c->zpl; }
static inline void ea_zpx(h6280 *c) { c->zpl = (uint8_t)(arg(c) + c->x); c->pc++; c->ea = c->zpl; }
static inline void ea_zpy(h6280 *c) { c->zpl = (uint8_t)(arg(c) + c->y); c->pc++; c->ea = c->zpl; }
static inline void ea_abs(h6280 *c)
{
    uint8_t lo = arg(c); c->pc++;
    uint8_t hi = arg(c); c->pc++;
    c->ea = (uint16_t)(lo | (hi << 8));
}
static inline void ea_abx(h6280 *c) { ea_abs(c); c->ea = (uint16_t)(c->ea + c->x); }
static inline void ea_aby(h6280 *c) { ea_abs(c); c->ea = (uint16_t)(c->ea + c->y); }
static inline void ea_zpi(h6280 *c) { c->zpl = arg(c); c->pc++; c->ea = rd16z(c, c->zpl); }
static inline void ea_idx(h6280 *c) { c->zpl = (uint8_t)(arg(c) + c->x); c->pc++; c->ea = rd16z(c, c->zpl); }
static inline void ea_idy(h6280 *c) { c->zpl = arg(c); c->pc++; c->ea = (uint16_t)(rd16z(c, c->zpl) + c->y); }
static inline void ea_ind(h6280 *c)
{
    ea_abs(c);
    uint8_t lo = rd8(c, c->ea);
    c->ea++;
    uint8_t hi = rd8(c, c->ea);
    c->ea = (uint16_t)(lo | (hi << 8));
}
static inline void ea_iax(h6280 *c)
{
    ea_abs(c);
    c->ea = (uint16_t)(c->ea + c->x);
    uint8_t lo = rd8(c, c->ea);
    c->ea++;
    uint8_t hi = rd8(c, c->ea);
    c->ea = (uint16_t)(lo | (hi << 8));
}

static inline uint8_t rd_imm(h6280 *c) { uint8_t v = arg(c); c->pc++; return v; }
static inline uint8_t rd_zpg(h6280 *c) { ea_zpg(c); return rd8z(c, c->ea); }
static inline uint8_t rd_zpx(h6280 *c) { ea_zpx(c); return rd8z(c, c->ea); }
static inline uint8_t rd_zpy(h6280 *c) { ea_zpy(c); return rd8z(c, c->ea); }
static inline uint8_t rd_abs(h6280 *c) { ea_abs(c); return rd8(c, c->ea); }
static inline uint8_t rd_abx(h6280 *c) { ea_abx(c); return rd8(c, c->ea); }
static inline uint8_t rd_aby(h6280 *c) { ea_aby(c); return rd8(c, c->ea); }
static inline uint8_t rd_zpi(h6280 *c) { ea_zpi(c); return rd8(c, c->ea); }
static inline uint8_t rd_idx(h6280 *c) { ea_idx(c); return rd8(c, c->ea); }
static inline uint8_t rd_idy(h6280 *c) { ea_idy(c); return rd8(c, c->ea); }
static inline uint8_t rd_tfl(h6280 *c) { c->zpl = c->x; c->ea = c->zpl; return rd8z(c, c->ea); }

static inline void wb_ea(h6280 *c, uint8_t v) { wr8(c, c->ea, v); }
static inline void wb_eaz(h6280 *c, uint8_t v) { wr8z(c, c->ea, v); }

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------
static inline void bra(h6280 *c, bool cond)
{
    clear_t(c);
    if (cond) {
        cycles(c, 4);
        uint8_t t = arg(c);
        c->pc++;
        c->pc = (uint16_t)(c->pc + (int8_t)t);
    } else {
        c->pc++;
        cycles(c, 2);
    }
}

static inline void tflag_op_end(h6280 *c, uint8_t v) { set_nz(c, v); wb_eaz(c, v); cycles(c, 3); }

static inline void adc(h6280 *c, uint8_t tmp)
{
    const bool t = c->p & fT;
    int acc;
    if (t) { clear_t(c); acc = rd_tfl(c); } else acc = c->a;
    int res;
    if (c->p & fD) {
        int cy = c->p & fC;
        int lo = (acc & 0x0f) + (tmp & 0x0f) + cy;
        int hi = (acc & 0xf0) + (tmp & 0xf0);
        c->p &= (uint8_t)~fC;
        if (lo > 0x09) { hi += 0x10; lo += 0x06; }
        if (hi > 0x90) hi += 0x60;
        if (hi & 0xff00) c->p |= fC;
        res = (lo & 0x0f) + (hi & 0xf0);
        cycles(c, 1);
    } else {
        int cy = c->p & fC;
        int sum = acc + tmp + cy;
        c->p &= (uint8_t)~(fV | fC);
        if (~(acc ^ tmp) & (acc ^ sum) & fN) c->p |= fV;
        if (sum & 0xff00) c->p |= fC;
        res = (uint8_t)sum;
    }
    if (t) tflag_op_end(c, (uint8_t)res);
    else { c->a = (uint8_t)res; set_nz(c, c->a); }
}

static inline void sbc(h6280 *c, uint8_t tmp)
{
    const bool t = c->p & fT;
    int acc;
    if (t) { clear_t(c); acc = rd_tfl(c); } else acc = c->a;
    int res;
    if (c->p & fD) {
        int cy = (c->p & fC) ^ fC;
        int sum = acc - tmp - cy;
        int lo = (acc & 0x0f) - (tmp & 0x0f) - cy;
        int hi = (acc & 0xf0) - (tmp & 0xf0);
        c->p &= (uint8_t)~fC;
        if (lo & 0xf0) lo -= 6;
        if (lo & 0x80) hi -= 0x10;
        if (hi & 0x0f00) hi -= 0x60;
        if ((sum & 0xff00) == 0) c->p |= fC;
        res = (lo & 0x0f) + (hi & 0xf0);
        cycles(c, 1);
    } else {
        int cy = (c->p & fC) ^ fC;
        int sum = acc - tmp - cy;
        c->p &= (uint8_t)~(fV | fC);
        if ((acc ^ tmp) & (acc ^ sum) & fN) c->p |= fV;
        if ((sum & 0xff00) == 0) c->p |= fC;
        res = (uint8_t)sum;
    }
    if (t) tflag_op_end(c, (uint8_t)res);
    else { c->a = (uint8_t)res; set_nz(c, c->a); }
}

// AND / EOR / ORA (with the T flag: on the zero page byte at X)
static inline void logic(h6280 *c, int op, uint8_t tmp)
{
    if (c->p & fT) {
        clear_t(c);
        uint8_t v = rd_tfl(c);
        v = op == 0 ? (uint8_t)(v & tmp) : op == 1 ? (uint8_t)(v ^ tmp) : (uint8_t)(v | tmp);
        wb_eaz(c, v);
        set_nz(c, v);
        cycles(c, 3);
    } else {
        c->a = op == 0 ? (uint8_t)(c->a & tmp) : op == 1 ? (uint8_t)(c->a ^ tmp) : (uint8_t)(c->a | tmp);
        set_nz(c, c->a);
    }
}
#define AND(v) logic(c, 0, (v))
#define EOR(v) logic(c, 1, (v))
#define ORA(v) logic(c, 2, (v))

static inline uint8_t asl(h6280 *c, uint8_t t) { clear_t(c); c->p = (uint8_t)((c->p & ~fC) | ((t >> 7) & fC)); t = (uint8_t)(t << 1); set_nz(c, t); return t; }
static inline uint8_t lsr(h6280 *c, uint8_t t) { clear_t(c); c->p = (uint8_t)((c->p & ~fC) | (t & fC)); t = (uint8_t)(t >> 1); set_nz(c, t); return t; }
static inline uint8_t rol(h6280 *c, uint8_t t)
{
    clear_t(c);
    int t9 = (t << 1) | (c->p & fC);
    c->p = (uint8_t)((c->p & ~fC) | ((t9 >> 8) & fC));
    t = (uint8_t)t9; set_nz(c, t); return t;
}
static inline uint8_t ror(h6280 *c, uint8_t t)
{
    clear_t(c);
    int t9 = t | ((c->p & fC) << 8);
    c->p = (uint8_t)((c->p & ~fC) | (t & fC));
    t = (uint8_t)(t9 >> 1); set_nz(c, t); return t;
}
static inline uint8_t dec(h6280 *c, uint8_t t) { clear_t(c); t--; set_nz(c, t); return t; }
static inline uint8_t inc(h6280 *c, uint8_t t) { clear_t(c); t++; set_nz(c, t); return t; }
static inline void bit(h6280 *c, uint8_t t)
{
    c->p = (uint8_t)((c->p & ~(fN | fV | fT | fZ)) | ((t & 0x80) ? fN : 0) | ((t & 0x40) ? fV : 0) | ((t & c->a) ? 0 : fZ));
}
static inline void tst(h6280 *c, uint8_t imm, uint8_t t)
{
    c->p = (uint8_t)((c->p & ~(fN | fV | fT | fZ)) | ((t & 0x80) ? fN : 0) | ((t & 0x40) ? fV : 0) | ((t & imm) ? 0 : fZ));
}
static inline uint8_t trb(h6280 *c, uint8_t t)
{
    clear_t(c);
    c->p = (uint8_t)((c->p & ~(fN | fV | fT | fZ)) | ((t & 0x80) ? fN : 0) | ((t & 0x40) ? fV : 0) | ((t & ~c->a) ? 0 : fZ));
    return (uint8_t)(t & ~c->a);
}
static inline uint8_t tsb(h6280 *c, uint8_t t)
{
    clear_t(c);
    c->p = (uint8_t)((c->p & ~(fN | fV | fT | fZ)) | ((t & 0x80) ? fN : 0) | ((t & 0x40) ? fV : 0) | ((t | c->a) ? 0 : fZ));
    return (uint8_t)(t | c->a);
}
static inline void cmpr(h6280 *c, uint8_t r, uint8_t t)
{
    clear_t(c);
    c->p &= (uint8_t)~fC;
    if (r >= t) c->p |= fC;
    set_nz(c, (uint8_t)(r - t));
}
static inline uint8_t rmb(h6280 *c, int b, uint8_t t) { clear_t(c); return (uint8_t)(t & ~(1 << b)); }
static inline uint8_t smb(h6280 *c, int b, uint8_t t) { clear_t(c); return (uint8_t)(t | (1 << b)); }

// Block transfers: 0 TII, 1 TDD, 2 TIA, 3 TAI, 4 TIN
static void xfer(h6280 *c, int kind)
{
    clear_t(c);
    uint16_t from = rd16(c, c->pc), to = rd16(c, (uint16_t)(c->pc + 2));
    int length = rd16(c, (uint16_t)(c->pc + 4));
    c->pc += 6;
    if (!length) length = 0x10000;
    cycles(c, 6 * length + 17);
    int alt = 0;
    while (length--) {
        switch (kind) {
        case 0: wr8(c, to, rd8(c, from)); to++; from++; break;
        case 1: wr8(c, to, rd8(c, from)); to--; from--; break;
        case 2: wr8(c, (uint16_t)(to + alt), rd8(c, from)); from++; alt ^= 1; break;
        case 3: wr8(c, to, rd8(c, (uint16_t)(from + alt))); to++; alt ^= 1; break;
        default: wr8(c, to, rd8(c, from)); from++; break;
        }
    }
}

static inline void rti(h6280 *c)
{
    c->p = (uint8_t)(pull(c) | fB);
    uint8_t lo = pull(c), hi = pull(c);
    c->pc = (uint16_t)(lo | (hi << 8));
    check_irq_lines(c);
}

// ---------------------------------------------------------------------------
// One instruction
// ---------------------------------------------------------------------------
static void H6280_HOT step(h6280 *c, uint8_t op)
{
    uint8_t t;
    switch (op) {
    case 0x00: cycles(c, 8); clear_t(c); c->pc++; push(c, (uint8_t)(c->pc >> 8)); push(c, (uint8_t)c->pc); push(c, c->p);
               c->p = (uint8_t)((c->p & ~fD) | fI); c->pc = (uint16_t)(rd8(c, IRQ2_VEC) | (rd8(c, IRQ2_VEC + 1) << 8)); break;
    case 0x20: cycles(c, 7); ea_abs(c); clear_t(c); c->pc--; push(c, (uint8_t)(c->pc >> 8)); push(c, (uint8_t)c->pc); c->pc = c->ea; break;
    case 0x40: cycles(c, 7); rti(c); break;
    case 0x60: { cycles(c, 7); clear_t(c); uint8_t lo = pull(c), hi = pull(c); c->pc = (uint16_t)((lo | (hi << 8)) + 1); break; }
    case 0x80: bra(c, true); break;
    case 0xa0: cycles(c, 2); t = rd_imm(c); clear_t(c); c->y = t; set_nz(c, t); break;
    case 0xc0: cycles(c, 2); cmpr(c, c->y, rd_imm(c)); break;
    case 0xe0: cycles(c, 2); cmpr(c, c->x, rd_imm(c)); break;

    case 0x10: bra(c, !(c->p & fN)); break;
    case 0x30: bra(c, c->p & fN); break;
    case 0x50: bra(c, !(c->p & fV)); break;
    case 0x70: bra(c, c->p & fV); break;
    case 0x90: bra(c, !(c->p & fC)); break;
    case 0xb0: bra(c, c->p & fC); break;
    case 0xd0: bra(c, !(c->p & fZ)); break;
    case 0xf0: bra(c, c->p & fZ); break;

    case 0x01: cycles(c, 7); ORA(rd_idx(c)); break;
    case 0x21: cycles(c, 7); AND(rd_idx(c)); break;
    case 0x41: cycles(c, 7); EOR(rd_idx(c)); break;
    case 0x61: cycles(c, 7); adc(c, rd_idx(c)); break;
    case 0x81: cycles(c, 7); ea_idx(c); clear_t(c); wb_ea(c, c->a); break;
    case 0xa1: cycles(c, 7); t = rd_idx(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xc1: cycles(c, 7); cmpr(c, c->a, rd_idx(c)); break;
    case 0xe1: cycles(c, 7); sbc(c, rd_idx(c)); break;

    case 0x11: cycles(c, 7); ORA(rd_idy(c)); break;
    case 0x31: cycles(c, 7); AND(rd_idy(c)); break;
    case 0x51: cycles(c, 7); EOR(rd_idy(c)); break;
    case 0x71: cycles(c, 7); adc(c, rd_idy(c)); break;
    case 0x91: cycles(c, 7); ea_idy(c); clear_t(c); wb_ea(c, c->a); break;
    case 0xb1: cycles(c, 7); t = rd_idy(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xd1: cycles(c, 7); cmpr(c, c->a, rd_idy(c)); break;
    case 0xf1: cycles(c, 7); sbc(c, rd_idy(c)); break;

    case 0x02: cycles(c, 3); clear_t(c); t = c->x; c->x = c->y; c->y = t; break;          // SXY
    case 0x22: cycles(c, 3); clear_t(c); t = c->x; c->x = c->a; c->a = t; break;          // SAX
    case 0x42: cycles(c, 3); clear_t(c); t = c->y; c->y = c->a; c->a = t; break;          // SAY
    case 0x62: cycles(c, 2); clear_t(c); c->a = 0; break;                                  // CLA
    case 0x82: cycles(c, 2); clear_t(c); c->x = 0; break;                                  // CLX
    case 0xa2: cycles(c, 2); t = rd_imm(c); clear_t(c); c->x = t; set_nz(c, t); break;
    case 0xc2: cycles(c, 2); clear_t(c); c->y = 0; break;                                  // CLY
    case 0xe2: cycles(c, 2); clear_t(c); break;

    case 0x12: cycles(c, 7); ORA(rd_zpi(c)); break;
    case 0x32: cycles(c, 7); AND(rd_zpi(c)); break;
    case 0x52: cycles(c, 7); EOR(rd_zpi(c)); break;
    case 0x72: cycles(c, 7); adc(c, rd_zpi(c)); break;
    case 0x92: cycles(c, 7); ea_zpi(c); clear_t(c); wb_ea(c, c->a); break;
    case 0xb2: cycles(c, 7); t = rd_zpi(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xd2: cycles(c, 7); cmpr(c, c->a, rd_zpi(c)); break;
    case 0xf2: cycles(c, 7); sbc(c, rd_zpi(c)); break;

    case 0x03: case 0x13: case 0x23: cycles(c, 5); rd_imm(c); clear_t(c); break;        // ST0/ST1/ST2 (no VDC)
    case 0x43: { cycles(c, 4); t = rd_imm(c); clear_t(c);                                 // TMA
                 for (int i = 0; i < 8; i++) { if (t & (1 << i)) c->a = c->mmr[i]; }
                 break; }
    case 0x63: cycles(c, 4); clear_t(c); break;
    case 0x83: { cycles(c, 7); uint8_t imm = rd_imm(c); tst(c, imm, rd_zpg(c)); break; }
    case 0xa3: { cycles(c, 7); uint8_t imm = rd_imm(c); tst(c, imm, rd_zpx(c)); break; }
    case 0xc3: xfer(c, 1); break;
    case 0xe3: xfer(c, 2); break;

    case 0x33: cycles(c, 2); clear_t(c); break;
    case 0x53: { cycles(c, 5); t = rd_imm(c); clear_t(c);                                 // TAM
                 for (int i = 0; i < 8; i++) { if (t & (1 << i)) c->mmr[i] = c->a; }
                 break; }
    case 0x73: xfer(c, 0); break;
    case 0x93: { cycles(c, 8); uint8_t imm = rd_imm(c); tst(c, imm, rd_abs(c)); break; }
    case 0xb3: { cycles(c, 8); uint8_t imm = rd_imm(c); tst(c, imm, rd_abx(c)); break; }
    case 0xd3: xfer(c, 4); break;
    case 0xf3: xfer(c, 3); break;

    case 0x04: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, tsb(c, t)); break;
    case 0x24: cycles(c, 4); bit(c, rd_zpg(c)); break;
    case 0x44: push(c, (uint8_t)(c->pc >> 8)); push(c, (uint8_t)c->pc); cycles(c, 4); bra(c, true); break;   // BSR
    case 0x64: cycles(c, 4); ea_zpg(c); clear_t(c); wb_eaz(c, 0); break;
    case 0x84: cycles(c, 4); ea_zpg(c); clear_t(c); wb_eaz(c, c->y); break;
    case 0xa4: cycles(c, 4); t = rd_zpg(c); clear_t(c); c->y = t; set_nz(c, t); break;
    case 0xc4: cycles(c, 4); cmpr(c, c->y, rd_zpg(c)); break;
    case 0xe4: cycles(c, 4); cmpr(c, c->x, rd_zpg(c)); break;

    case 0x14: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, trb(c, t)); break;
    case 0x34: cycles(c, 4); bit(c, rd_zpx(c)); break;
    case 0x54: cycles(c, 3); c->clocks_per_cycle = 4; break;                               // CSL
    case 0x74: cycles(c, 4); ea_zpx(c); clear_t(c); wb_eaz(c, 0); break;
    case 0x94: cycles(c, 4); ea_zpx(c); clear_t(c); wb_eaz(c, c->y); break;
    case 0xb4: cycles(c, 4); t = rd_zpx(c); clear_t(c); c->y = t; set_nz(c, t); break;
    case 0xd4: cycles(c, 3); c->clocks_per_cycle = 1; break;                               // CSH
    case 0xf4: cycles(c, 2); c->p |= fT; break;                                            // SET

    case 0x05: cycles(c, 4); ORA(rd_zpg(c)); break;
    case 0x25: cycles(c, 4); AND(rd_zpg(c)); break;
    case 0x45: cycles(c, 4); EOR(rd_zpg(c)); break;
    case 0x65: cycles(c, 4); adc(c, rd_zpg(c)); break;
    case 0x85: cycles(c, 4); ea_zpg(c); clear_t(c); wb_eaz(c, c->a); break;
    case 0xa5: cycles(c, 4); t = rd_zpg(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xc5: cycles(c, 4); cmpr(c, c->a, rd_zpg(c)); break;
    case 0xe5: cycles(c, 4); sbc(c, rd_zpg(c)); break;

    case 0x15: cycles(c, 4); ORA(rd_zpx(c)); break;
    case 0x35: cycles(c, 4); AND(rd_zpx(c)); break;
    case 0x55: cycles(c, 4); EOR(rd_zpx(c)); break;
    case 0x75: cycles(c, 4); adc(c, rd_zpx(c)); break;
    case 0x95: cycles(c, 4); ea_zpx(c); clear_t(c); wb_eaz(c, c->a); break;
    case 0xb5: cycles(c, 4); t = rd_zpx(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xd5: cycles(c, 4); cmpr(c, c->a, rd_zpx(c)); break;
    case 0xf5: cycles(c, 4); sbc(c, rd_zpx(c)); break;

    case 0x06: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, asl(c, t)); break;
    case 0x26: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, rol(c, t)); break;
    case 0x46: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, lsr(c, t)); break;
    case 0x66: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, ror(c, t)); break;
    case 0x86: cycles(c, 4); ea_zpg(c); clear_t(c); wb_eaz(c, c->x); break;
    case 0xa6: cycles(c, 4); t = rd_zpg(c); clear_t(c); c->x = t; set_nz(c, t); break;
    case 0xc6: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, dec(c, t)); break;
    case 0xe6: cycles(c, 6); t = rd_zpg(c); wb_eaz(c, inc(c, t)); break;

    case 0x16: cycles(c, 6); t = rd_zpx(c); wb_eaz(c, asl(c, t)); break;
    case 0x36: cycles(c, 6); t = rd_zpx(c); wb_eaz(c, rol(c, t)); break;
    case 0x56: cycles(c, 6); t = rd_zpx(c); wb_eaz(c, lsr(c, t)); break;
    case 0x76: cycles(c, 6); t = rd_zpx(c); wb_eaz(c, ror(c, t)); break;
    case 0x96: cycles(c, 4); ea_zpy(c); clear_t(c); wb_eaz(c, c->x); break;
    case 0xb6: cycles(c, 4); t = rd_zpy(c); clear_t(c); c->x = t; set_nz(c, t); break;
    case 0xd6: cycles(c, 6); t = rd_zpx(c); wb_eaz(c, dec(c, t)); break;
    case 0xf6: cycles(c, 6); t = rd_zpx(c); wb_eaz(c, inc(c, t)); break;

    case 0x07: case 0x17: case 0x27: case 0x37: case 0x47: case 0x57: case 0x67: case 0x77:
        cycles(c, 7); t = rd_zpg(c); wb_eaz(c, rmb(c, op >> 4, t)); break;
    case 0x87: case 0x97: case 0xa7: case 0xb7: case 0xc7: case 0xd7: case 0xe7: case 0xf7:
        cycles(c, 7); t = rd_zpg(c); wb_eaz(c, smb(c, (op >> 4) - 8, t)); break;

    case 0x08: cycles(c, 3); clear_t(c); push(c, c->p); break;                             // PHP
    case 0x28: cycles(c, 4); c->p = (uint8_t)(pull(c) | fB); check_irq_lines(c); break;    // PLP
    case 0x48: cycles(c, 3); clear_t(c); push(c, c->a); break;
    case 0x68: cycles(c, 4); clear_t(c); c->a = pull(c); set_nz(c, c->a); break;
    case 0x88: cycles(c, 2); clear_t(c); c->y--; set_nz(c, c->y); break;
    case 0xa8: cycles(c, 2); clear_t(c); c->y = c->a; set_nz(c, c->y); break;
    case 0xc8: cycles(c, 2); clear_t(c); c->y++; set_nz(c, c->y); break;
    case 0xe8: cycles(c, 2); clear_t(c); c->x++; set_nz(c, c->x); break;

    case 0x18: cycles(c, 2); clear_t(c); c->p &= (uint8_t)~fC; break;
    case 0x38: cycles(c, 2); clear_t(c); c->p |= fC; break;
    case 0x58: cycles(c, 2); clear_t(c); if (c->p & fI) { c->p &= (uint8_t)~fI; check_irq_lines(c); } break;
    case 0x78: cycles(c, 2); clear_t(c); c->p |= fI; break;
    case 0x98: cycles(c, 2); clear_t(c); c->a = c->y; set_nz(c, c->a); break;
    case 0xb8: cycles(c, 2); clear_t(c); c->p &= (uint8_t)~fV; break;
    case 0xd8: cycles(c, 2); clear_t(c); c->p &= (uint8_t)~fD; break;
    case 0xf8: cycles(c, 2); clear_t(c); c->p |= fD; break;

    case 0x09: cycles(c, 2); ORA(rd_imm(c)); break;
    case 0x29: cycles(c, 2); AND(rd_imm(c)); break;
    case 0x49: cycles(c, 2); EOR(rd_imm(c)); break;
    case 0x69: cycles(c, 2); adc(c, rd_imm(c)); break;
    case 0x89: cycles(c, 2); bit(c, rd_imm(c)); break;
    case 0xa9: cycles(c, 2); t = rd_imm(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xc9: cycles(c, 2); cmpr(c, c->a, rd_imm(c)); break;
    case 0xe9: cycles(c, 2); sbc(c, rd_imm(c)); break;

    case 0x19: cycles(c, 5); ORA(rd_aby(c)); break;
    case 0x39: cycles(c, 5); AND(rd_aby(c)); break;
    case 0x59: cycles(c, 5); EOR(rd_aby(c)); break;
    case 0x79: cycles(c, 5); adc(c, rd_aby(c)); break;
    case 0x99: cycles(c, 5); ea_aby(c); clear_t(c); wb_ea(c, c->a); break;
    case 0xb9: cycles(c, 5); t = rd_aby(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xd9: cycles(c, 5); cmpr(c, c->a, rd_aby(c)); break;
    case 0xf9: cycles(c, 5); sbc(c, rd_aby(c)); break;

    case 0x0a: cycles(c, 2); c->a = asl(c, c->a); break;
    case 0x2a: cycles(c, 2); c->a = rol(c, c->a); break;
    case 0x4a: cycles(c, 2); c->a = lsr(c, c->a); break;
    case 0x6a: cycles(c, 2); c->a = ror(c, c->a); break;
    case 0x8a: cycles(c, 2); clear_t(c); c->a = c->x; set_nz(c, c->a); break;
    case 0xaa: cycles(c, 2); clear_t(c); c->x = c->a; set_nz(c, c->x); break;
    case 0xca: cycles(c, 2); clear_t(c); c->x--; set_nz(c, c->x); break;
    case 0xea: cycles(c, 2); clear_t(c); break;

    case 0x1a: cycles(c, 2); c->a = inc(c, c->a); break;
    case 0x3a: cycles(c, 2); c->a = dec(c, c->a); break;
    case 0x5a: cycles(c, 3); clear_t(c); push(c, c->y); break;
    case 0x7a: cycles(c, 4); clear_t(c); c->y = pull(c); set_nz(c, c->y); break;
    case 0x9a: cycles(c, 2); clear_t(c); c->s = c->x; break;
    case 0xba: cycles(c, 2); clear_t(c); c->x = c->s; set_nz(c, c->x); break;
    case 0xda: cycles(c, 3); clear_t(c); push(c, c->x); break;
    case 0xfa: cycles(c, 4); clear_t(c); c->x = pull(c); set_nz(c, c->x); break;

    case 0x0b: case 0x1b: case 0x2b: case 0x3b: case 0x4b: case 0x5b: case 0x6b: case 0x7b:
    case 0x8b: case 0x9b: case 0xab: case 0xbb: case 0xcb: case 0xdb: case 0xeb: case 0xfb:
    case 0x5c: case 0xdc: case 0xfc:
        cycles(c, 2); clear_t(c); break;

    case 0x0c: cycles(c, 7); t = rd_abs(c); wb_ea(c, tsb(c, t)); break;
    case 0x2c: cycles(c, 5); bit(c, rd_abs(c)); break;
    case 0x4c: cycles(c, 4); ea_abs(c); clear_t(c); c->pc = c->ea; break;
    case 0x6c: cycles(c, 7); ea_ind(c); clear_t(c); c->pc = c->ea; break;
    case 0x8c: cycles(c, 5); ea_abs(c); clear_t(c); wb_ea(c, c->y); break;
    case 0xac: cycles(c, 5); t = rd_abs(c); clear_t(c); c->y = t; set_nz(c, t); break;
    case 0xcc: cycles(c, 5); cmpr(c, c->y, rd_abs(c)); break;
    case 0xec: cycles(c, 5); cmpr(c, c->x, rd_abs(c)); break;

    case 0x1c: cycles(c, 7); t = rd_abs(c); wb_ea(c, trb(c, t)); break;
    case 0x3c: cycles(c, 5); bit(c, rd_abx(c)); break;
    case 0x7c: cycles(c, 7); ea_iax(c); clear_t(c); c->pc = c->ea; break;
    case 0x9c: cycles(c, 5); ea_abs(c); clear_t(c); wb_ea(c, 0); break;
    case 0xbc: cycles(c, 5); t = rd_abx(c); clear_t(c); c->y = t; set_nz(c, t); break;

    case 0x0d: cycles(c, 5); ORA(rd_abs(c)); break;
    case 0x2d: cycles(c, 5); AND(rd_abs(c)); break;
    case 0x4d: cycles(c, 5); EOR(rd_abs(c)); break;
    case 0x6d: cycles(c, 5); adc(c, rd_abs(c)); break;
    case 0x8d: cycles(c, 5); ea_abs(c); clear_t(c); wb_ea(c, c->a); break;
    case 0xad: cycles(c, 5); t = rd_abs(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xcd: cycles(c, 5); cmpr(c, c->a, rd_abs(c)); break;
    case 0xed: cycles(c, 5); sbc(c, rd_abs(c)); break;

    case 0x1d: cycles(c, 5); ORA(rd_abx(c)); break;
    case 0x3d: cycles(c, 5); AND(rd_abx(c)); break;
    case 0x5d: cycles(c, 5); EOR(rd_abx(c)); break;
    case 0x7d: cycles(c, 5); adc(c, rd_abx(c)); break;
    case 0x9d: cycles(c, 5); ea_abx(c); clear_t(c); wb_ea(c, c->a); break;
    case 0xbd: cycles(c, 5); t = rd_abx(c); clear_t(c); c->a = t; set_nz(c, t); break;
    case 0xdd: cycles(c, 5); cmpr(c, c->a, rd_abx(c)); break;
    case 0xfd: cycles(c, 5); sbc(c, rd_abx(c)); break;

    case 0x0e: cycles(c, 7); t = rd_abs(c); wb_ea(c, asl(c, t)); break;
    case 0x2e: cycles(c, 7); t = rd_abs(c); wb_ea(c, rol(c, t)); break;
    case 0x4e: cycles(c, 7); t = rd_abs(c); wb_ea(c, lsr(c, t)); break;
    case 0x6e: cycles(c, 7); t = rd_abs(c); wb_ea(c, ror(c, t)); break;
    case 0x8e: cycles(c, 5); ea_abs(c); clear_t(c); wb_ea(c, c->x); break;
    case 0xae: cycles(c, 5); t = rd_abs(c); clear_t(c); c->x = t; set_nz(c, t); break;
    case 0xce: cycles(c, 7); t = rd_abs(c); wb_ea(c, dec(c, t)); break;
    case 0xee: cycles(c, 7); t = rd_abs(c); wb_ea(c, inc(c, t)); break;

    case 0x1e: cycles(c, 7); t = rd_abx(c); wb_ea(c, asl(c, t)); break;
    case 0x3e: cycles(c, 7); t = rd_abx(c); wb_ea(c, rol(c, t)); break;
    case 0x5e: cycles(c, 7); t = rd_abx(c); wb_ea(c, lsr(c, t)); break;
    case 0x7e: cycles(c, 7); t = rd_abx(c); wb_ea(c, ror(c, t)); break;
    case 0x9e: cycles(c, 5); ea_abx(c); clear_t(c); wb_ea(c, 0); break;
    case 0xbe: cycles(c, 5); t = rd_aby(c); clear_t(c); c->x = t; set_nz(c, t); break;
    case 0xde: cycles(c, 7); t = rd_abx(c); wb_ea(c, dec(c, t)); break;
    case 0xfe: cycles(c, 7); t = rd_abx(c); wb_ea(c, inc(c, t)); break;

    default: {      // 0x0f..0xff step 0x10: BBRn / BBSn zp,rel
        cycles(c, 4);
        t = rd_zpg(c);
        const int b = (op >> 4) & 7;
        if (op & 0x80) bra(c, (t >> b) & 1);
        else bra(c, !((t >> b) & 1));
        break;
    }
    }
}

// ---------------------------------------------------------------------------
void h6280_init(h6280 *c)
{
    memset(c->bank_r, 0, sizeof c->bank_r);
    memset(c->bank_w, 0, sizeof c->bank_w);
    memset(c->irq_state, 0, sizeof c->irq_state);
    c->nmi_state = 0;
    c->icount = 0;
}

void h6280_reset(h6280 *c)
{
    c->ppc = c->pc = 0;
    c->ea = 0;
    c->zpl = 0;
    c->a = c->x = c->y = 0;
    memset(c->mmr, 0, sizeof c->mmr);
    c->irq_mask = 0;
    c->timer_value = 0;
    c->io_buffer = 0;
    c->p = fI | fB;
    c->s = 0xff;
    c->pc = (uint16_t)(rd8(c, RESET_VEC) | (rd8(c, RESET_VEC + 1) << 8));
    c->clocks_per_cycle = 4;
    c->timer_status = 0;
    c->timer_load = 128 * 1024;
    c->irq_pending = 0;
    c->total = 0;
}

int H6280_HOT h6280_run(h6280 *c, int clocks)
{
    const int32_t start = c->icount + clocks;
    c->icount = start;
    if (c->irq_pending == 2) c->irq_pending--;
    while (c->icount > 0) {
        c->ppc = c->pc;
        const uint8_t op = arg(c);
        c->pc++;
        step(c, op);
        if (c->irq_pending) {
            if (c->irq_pending == 1) {
                if (!(c->p & fI)) {
                    c->irq_pending--;
                    check_and_take_irq_lines(c);
                }
            } else {
                c->irq_pending--;
            }
        }
        if (c->timer_status && c->timer_value <= 0) {
            if (!c->irq_pending) c->irq_pending = 1;
            while (c->timer_value <= 0) c->timer_value += c->timer_load;
            h6280_set_irq_line(c, H6280_IRQT, H6280_ASSERT);
        }
    }
    const int ran = start - c->icount;
    c->total += (uint64_t)ran;
    return ran;
}
