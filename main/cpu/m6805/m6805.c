// m6805.c - Motorola 6805 HMOS core. See m6805.h.
#include "m6805.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define M6805_HOT IRAM_ATTR
#else
#define M6805_HOT
#endif

enum { CF = 0x01, ZF = 0x02, NF = 0x04, IF = 0x08, HF = 0x10 };
enum { INT_IRQ = 1, INT_TIMER = 2 };
enum { SRC_CLOCK = 0, SRC_CLOCK_TIMER = 1, SRC_DISABLED = 2, SRC_TIMER = 3 };
enum { TCR_PS = 0x07, TCR_PSC = 0x08, TCR_TIM = 0x40, TCR_TIR = 0x80 };

// MAME's s_hmos_cycles (illegal opcodes: their row's count)
static const uint8_t cycles[256] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    9, 6, 2, 11, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 8, 2, 2,
    4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 3, 7, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 4, 8, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 6, 5, 9, 6, 7,
    5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 4, 8, 5, 6,
    4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 3, 7, 4, 5,
};

static inline uint8_t rd(m6805 *c, uint16_t a) { return c->read(c->user, a & c->amask); }
static inline void wr(m6805 *c, uint16_t a, uint8_t v) { c->write(c->user, a & c->amask, v); }
static inline uint8_t fetch(m6805 *c) { return rd(c, c->pc++); }
static inline uint16_t rd16(m6805 *c, uint16_t a) { return (uint16_t)((rd(c, a) << 8) | rd(c, (uint16_t)(a + 1))); }

// stack: $60-$7F
static inline void push(m6805 *c, uint8_t v)
{
    wr(c, c->sp, v);
    c->sp = (uint16_t)(((c->sp - 1) & 0x7f) | 0x60);
}
static inline uint8_t pull(m6805 *c)
{
    c->sp = (uint16_t)(((c->sp + 1) & 0x7f) | 0x60);
    return rd(c, c->sp);
}
static inline void push16(m6805 *c, uint16_t v) { push(c, (uint8_t)v); push(c, (uint8_t)(v >> 8)); }
static inline uint16_t pull16(m6805 *c) { uint8_t h = pull(c); return (uint16_t)((h << 8) | pull(c)); }

static inline void nz(m6805 *c, uint8_t r)
{
    c->cc = (uint8_t)((c->cc & ~(NF | ZF)) | (r & 0x80 ? NF : 0) | (r ? 0 : ZF));
}
static inline void nzc(m6805 *c, uint16_t r)
{
    c->cc = (uint8_t)((c->cc & ~(NF | ZF | CF)) | (r & 0x80 ? NF : 0) | ((r & 0xff) ? 0 : ZF) | (r & 0x100 ? CF : 0));
}

// ---------------------------------------------------------------------------
// Timer/counter (MAME m6805_timer, programmable options)
// ---------------------------------------------------------------------------
static void timer_line(m6805 *c, bool on)
{
    if (on) c->pending |= INT_TIMER;
    else c->pending &= (uint8_t)~INT_TIMER;     // level sensitive
}

void m6805_tcr_w(m6805 *c, uint8_t data)
{
    c->divisor = data & TCR_PS;
    c->source = (data & 0x30) >> 4;
    if (data & TCR_PSC) c->prescale = 0;
    c->tcr = (uint8_t)((c->tcr & (data & TCR_TIR)) | (data & ~(TCR_TIR | TCR_PSC)));
    timer_line(c, (c->tcr & TCR_TIR) && !(c->tcr & TCR_TIM));
}

static inline void timer_update(m6805 *c, unsigned count)
{
    if (c->source == SRC_DISABLED) return;
    const unsigned edges = c->source == SRC_TIMER ? 0 : count;     // (no external input)
    const unsigned prescale = (c->prescale & ((1u << c->divisor) - 1)) + edges;
    const unsigned dec = prescale >> c->divisor;
    const bool irq = (c->tdr ? (unsigned)c->tdr : 256u) <= dec;
    c->prescale = (uint8_t)(prescale & 0x7f);
    c->tdr = (uint8_t)(c->tdr - dec);
    if (irq) {
        c->tcr |= TCR_TIR;
        if (!(c->tcr & TCR_TIM)) timer_line(c, true);
    }
}

// ---------------------------------------------------------------------------
void m6805_init(m6805 *c)
{
    memset(c, 0, sizeof *c);
    c->amask = 0x7ff;
    c->divisor = 7;
    c->source = SRC_CLOCK;
}

void m6805_reset(m6805 *c)
{
    c->a = c->x = 0;
    c->cc = IF;
    c->sp = 0x7f;
    c->pending = 0;
    c->prescale = 0x7f;
    c->tdr = 0xff;
    c->tcr = 0x7f;
    c->source = SRC_CLOCK_TIMER;     // (timer pin high: counts the clock)
    c->pc = rd16(c, 0xfffe);
}

void m6805_set_int(m6805 *c, bool asserted)
{
    if (asserted == c->int_line) return;
    c->int_line = asserted;
    if (asserted) c->pending |= INT_IRQ;     // latched until taken
}

static int M6805_HOT step(m6805 *c)
{
    const uint8_t op = fetch(c);
    uint16_t ea = 0;
    uint8_t v = 0;
    switch (op >> 4) {
    case 0x0: case 0x1: case 0x3: case 0xb: ea = fetch(c); break;               // direct
    case 0x2: case 0xa: v = fetch(c); break;                                     // relative / immediate
    case 0x6: case 0xe: ea = (uint16_t)(fetch(c) + c->x); break;                 // indexed, 8-bit offset
    case 0x7: case 0xf: ea = c->x; break;                                        // indexed
    case 0xc: ea = (uint16_t)(fetch(c) << 8); ea |= fetch(c); break;             // extended
    case 0xd: { uint16_t o = (uint16_t)(fetch(c) << 8); o |= fetch(c); ea = (uint16_t)(o + c->x); break; }
    default: break;
    }
    // operand read (not for the stores and jumps)
    const uint8_t lo = op & 0x0f;
    const bool needs_read = (op < 0x80 && (op >> 4) != 0x2 && (op >> 4) != 0x4 && (op >> 4) != 0x5) ||
                            (op >= 0xb0 && lo != 0x7 && lo != 0xc && lo != 0xd && lo != 0xf);
    if (needs_read) v = rd(c, ea);

    switch (op) {
    // bit test and branch
    case 0x00: case 0x02: case 0x04: case 0x06: case 0x08: case 0x0a: case 0x0c: case 0x0e: {
        const int8_t off = (int8_t)fetch(c);
        c->cc &= (uint8_t)~CF;
        if (v & (1 << ((op >> 1) & 7))) { c->cc |= CF; c->pc = (uint16_t)(c->pc + off); }
        break;
    }
    case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0b: case 0x0d: case 0x0f: {
        const int8_t off = (int8_t)fetch(c);
        c->cc |= CF;
        if (!(v & (1 << ((op >> 1) & 7)))) { c->cc &= (uint8_t)~CF; c->pc = (uint16_t)(c->pc + off); }
        break;
    }
    case 0x10: case 0x12: case 0x14: case 0x16: case 0x18: case 0x1a: case 0x1c: case 0x1e:
        wr(c, ea, (uint8_t)(v | (1 << ((op >> 1) & 7)))); break;
    case 0x11: case 0x13: case 0x15: case 0x17: case 0x19: case 0x1b: case 0x1d: case 0x1f:
        wr(c, ea, (uint8_t)(v & ~(1 << ((op >> 1) & 7)))); break;

    // branches
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
        bool t;
        switch (op >> 1 & 7) {
        case 0: t = true; break;
        case 1: t = !(c->cc & (CF | ZF)); break;
        case 2: t = !(c->cc & CF); break;
        case 3: t = !(c->cc & ZF); break;
        case 4: t = !(c->cc & HF); break;
        case 5: t = !(c->cc & NF); break;
        case 6: t = !(c->cc & IF); break;
        default: t = c->int_line; break;            // BIL (BIH inverted)
        }
        if (op & 1) t = !t;
        if ((op >> 1 & 7) == 7) t = (op & 1) ? !c->int_line : c->int_line;
        if (t) c->pc = (uint16_t)(c->pc + (int8_t)v);
        break;
    }

    // read-modify-write on memory
    case 0x30: case 0x60: case 0x70: { uint16_t r = (uint16_t)(0u - v); nzc(c, r); wr(c, ea, (uint8_t)r); break; }
    case 0x33: case 0x63: case 0x73: { uint8_t r = (uint8_t)~v; nz(c, r); c->cc |= CF; wr(c, ea, r); break; }
    case 0x34: case 0x64: case 0x74: {
        uint8_t r = (uint8_t)(v >> 1);
        c->cc = (uint8_t)((c->cc & ~(NF | ZF | CF)) | (v & 1 ? CF : 0) | (r ? 0 : ZF));
        wr(c, ea, r); break;
    }
    case 0x36: case 0x66: case 0x76: {
        uint8_t r = (uint8_t)(((c->cc & CF) ? 0x80 : 0) | (v >> 1));
        c->cc = (uint8_t)((c->cc & ~CF) | (v & 1 ? CF : 0)); nz(c, r); wr(c, ea, r); break;
    }
    case 0x37: case 0x67: case 0x77: {
        uint8_t r = (uint8_t)((v & 0x80) | (v >> 1));
        c->cc = (uint8_t)((c->cc & ~CF) | (v & 1 ? CF : 0)); nz(c, r); wr(c, ea, r); break;
    }
    case 0x38: case 0x68: case 0x78: { uint16_t r = (uint16_t)(v << 1); nzc(c, r); wr(c, ea, (uint8_t)r); break; }
    case 0x39: case 0x69: case 0x79: { uint16_t r = (uint16_t)((v << 1) | (c->cc & CF)); nzc(c, r); wr(c, ea, (uint8_t)r); break; }
    case 0x3a: case 0x6a: case 0x7a: { uint8_t r = (uint8_t)(v - 1); nz(c, r); wr(c, ea, r); break; }
    case 0x3c: case 0x6c: case 0x7c: { uint8_t r = (uint8_t)(v + 1); nz(c, r); wr(c, ea, r); break; }
    case 0x3d: case 0x6d: case 0x7d: nz(c, v); break;
    case 0x3f: case 0x6f: case 0x7f: c->cc = (uint8_t)((c->cc & ~NF) | ZF); wr(c, ea, 0); break;

    // accumulator / index
    case 0x40: { uint16_t r = (uint16_t)(0u - c->a); nzc(c, r); c->a = (uint8_t)r; break; }
    case 0x43: c->a = (uint8_t)~c->a; nz(c, c->a); c->cc |= CF; break;
    case 0x44: c->cc = (uint8_t)((c->cc & ~(NF | ZF | CF)) | (c->a & 1 ? CF : 0)); c->a >>= 1; if (!c->a) c->cc |= ZF; break;
    case 0x46: { uint8_t r = (uint8_t)(((c->cc & CF) ? 0x80 : 0) | (c->a >> 1)); c->cc = (uint8_t)((c->cc & ~CF) | (c->a & 1 ? CF : 0)); c->a = r; nz(c, r); break; }
    case 0x47: c->cc = (uint8_t)((c->cc & ~CF) | (c->a & 1 ? CF : 0)); c->a = (uint8_t)((c->a & 0x80) | (c->a >> 1)); nz(c, c->a); break;
    case 0x48: { uint16_t r = (uint16_t)(c->a << 1); nzc(c, r); c->a = (uint8_t)r; break; }
    case 0x49: { uint16_t r = (uint16_t)((c->a << 1) | (c->cc & CF)); nzc(c, r); c->a = (uint8_t)r; break; }
    case 0x4a: c->a--; nz(c, c->a); break;
    case 0x4c: c->a++; nz(c, c->a); break;
    case 0x4d: nz(c, c->a); break;
    case 0x4f: c->a = 0; c->cc = (uint8_t)((c->cc & ~NF) | ZF); break;
    case 0x50: { uint16_t r = (uint16_t)(0u - c->x); nzc(c, r); c->x = (uint8_t)r; break; }
    case 0x53: c->x = (uint8_t)~c->x; nz(c, c->x); c->cc |= CF; break;
    case 0x54: c->cc = (uint8_t)((c->cc & ~(NF | ZF | CF)) | (c->x & 1 ? CF : 0)); c->x >>= 1; if (!c->x) c->cc |= ZF; break;
    case 0x56: { uint8_t r = (uint8_t)(((c->cc & CF) ? 0x80 : 0) | (c->x >> 1)); c->cc = (uint8_t)((c->cc & ~CF) | (c->x & 1 ? CF : 0)); c->x = r; nz(c, r); break; }
    case 0x57: c->cc = (uint8_t)((c->cc & ~CF) | (c->x & 1 ? CF : 0)); c->x = (uint8_t)((c->x & 0x80) | (c->x >> 1)); nz(c, c->x); break;
    case 0x58: { uint16_t r = (uint16_t)(c->x << 1); nzc(c, r); c->x = (uint8_t)r; break; }
    case 0x59: { uint16_t r = (uint16_t)((c->x << 1) | (c->cc & CF)); nzc(c, r); c->x = (uint8_t)r; break; }
    case 0x5a: c->x--; nz(c, c->x); break;
    case 0x5c: c->x++; nz(c, c->x); break;
    case 0x5d: nz(c, c->x); break;
    case 0x5f: c->x = 0; c->cc = (uint8_t)((c->cc & ~NF) | ZF); break;

    // control
    case 0x80: c->cc = pull(c); c->a = pull(c); c->x = pull(c); c->pc = pull16(c); break;   // rti
    case 0x81: c->pc = pull16(c); break;                                                    // rts
    case 0x83: push16(c, c->pc); push(c, c->x); push(c, c->a); push(c, c->cc);              // swi
        c->cc |= IF; c->pc = rd16(c, 0xfffc); break;
    case 0x97: c->x = c->a; break;
    case 0x98: c->cc &= (uint8_t)~CF; break;
    case 0x99: c->cc |= CF; break;
    case 0x9a: c->cc &= (uint8_t)~IF; break;
    case 0x9b: c->cc |= IF; break;
    case 0x9c: c->sp = 0x7f; break;
    case 0x9d: break;
    case 0x9f: c->a = c->x; break;

    // ALU
    case 0xa0: case 0xb0: case 0xc0: case 0xd0: case 0xe0: case 0xf0: { uint16_t r = (uint16_t)(c->a - v); nzc(c, r); c->a = (uint8_t)r; break; }
    case 0xa1: case 0xb1: case 0xc1: case 0xd1: case 0xe1: case 0xf1: nzc(c, (uint16_t)(c->a - v)); break;
    case 0xa2: case 0xb2: case 0xc2: case 0xd2: case 0xe2: case 0xf2: { uint16_t r = (uint16_t)(c->a - v - (c->cc & CF)); nzc(c, r); c->a = (uint8_t)r; break; }
    case 0xa3: case 0xb3: case 0xc3: case 0xd3: case 0xe3: case 0xf3: nzc(c, (uint16_t)(c->x - v)); break;
    case 0xa4: case 0xb4: case 0xc4: case 0xd4: case 0xe4: case 0xf4: c->a &= v; nz(c, c->a); break;
    case 0xa5: case 0xb5: case 0xc5: case 0xd5: case 0xe5: case 0xf5: nz(c, (uint8_t)(c->a & v)); break;
    case 0xa6: case 0xb6: case 0xc6: case 0xd6: case 0xe6: case 0xf6: c->a = v; nz(c, v); break;
    case 0xb7: case 0xc7: case 0xd7: case 0xe7: case 0xf7: nz(c, c->a); wr(c, ea, c->a); break;
    case 0xa8: case 0xb8: case 0xc8: case 0xd8: case 0xe8: case 0xf8: c->a ^= v; nz(c, c->a); break;
    case 0xa9: case 0xb9: case 0xc9: case 0xd9: case 0xe9: case 0xf9: {
        uint16_t r = (uint16_t)(c->a + v + (c->cc & CF));
        c->cc = (uint8_t)((c->cc & ~HF) | (((c->a ^ v ^ r) & 0x10) ? HF : 0)); nzc(c, r); c->a = (uint8_t)r; break;
    }
    case 0xaa: case 0xba: case 0xca: case 0xda: case 0xea: case 0xfa: c->a |= v; nz(c, c->a); break;
    case 0xab: case 0xbb: case 0xcb: case 0xdb: case 0xeb: case 0xfb: {
        uint16_t r = (uint16_t)(c->a + v);
        c->cc = (uint8_t)((c->cc & ~HF) | (((c->a ^ v ^ r) & 0x10) ? HF : 0)); nzc(c, r); c->a = (uint8_t)r; break;
    }
    case 0xbc: case 0xcc: case 0xdc: case 0xec: case 0xfc: c->pc = ea; break;
    case 0xad: push16(c, c->pc); c->pc = (uint16_t)(c->pc + (int8_t)v); break;
    case 0xbd: case 0xcd: case 0xdd: case 0xed: case 0xfd: push16(c, c->pc); c->pc = ea; break;
    case 0xae: case 0xbe: case 0xce: case 0xde: case 0xee: case 0xfe: c->x = v; nz(c, v); break;
    case 0xbf: case 0xcf: case 0xdf: case 0xef: case 0xff: nz(c, c->x); wr(c, ea, c->x); break;
    default: break;                         // illegal: nothing
    }
    return cycles[op];
}

int M6805_HOT m6805_run(m6805 *c, int budget)
{
    int done = 0;
    while (done < budget) {
        if (c->pending && !(c->cc & IF)) {
            push16(c, c->pc); push(c, c->x); push(c, c->a); push(c, c->cc);
            c->cc |= IF;
            if (c->pending & INT_IRQ) {
                c->pending &= (uint8_t)~INT_IRQ;
                c->pc = rd16(c, 0xfffa);
            } else {
                c->pc = rd16(c, 0xfff8);
            }
            done += 11;
            timer_update(c, 11);
        }
        if (c->idle_ok && c->pc == c->idle_pc && !(c->pending && !(c->cc & IF)) && c->idle_ok(c->user, c)) {
            int passes = (budget - done) / c->idle_pass;
            // stop before a timer interrupt that would be taken
            if (!(c->cc & IF) && c->source != SRC_DISABLED && c->source != SRC_TIMER && !(c->tcr & TCR_TIM)) {
                const unsigned to_irq = ((c->tdr ? (unsigned)c->tdr : 256u) << c->divisor) -
                                        (c->prescale & ((1u << c->divisor) - 1));
                const int lim = (int)((to_irq - 1) / c->idle_pass);
                if (passes > lim) passes = lim;
            }
            if (passes > 0) {
                const int n = passes * c->idle_pass;
                done += n;
                timer_update(c, (unsigned)n);
                c->idle_skipped += (uint32_t)n;
                continue;
            }
        }
        const int n = step(c);
        done += n;
        timer_update(c, (unsigned)n);
    }
    return done;
}
