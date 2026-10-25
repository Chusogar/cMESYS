// dsp6502.c - MOS 6502 (NMOS), C port of dsp-cpp's cpu/m6502.cpp (the NMOS
// part: the 65C02 and NES variants are left out).
//
// A driver may #include this file after defining DSP6502_READ, DSP6502_WRITE
// and DSP6502_CYCLES (user, address/value/cycles) so its handlers are called
// directly (and inlined) instead of through the function pointers, and
// DSP6502_HOOK (user, cpu, budget) for a fast-forward before an instruction.
#include "dsp6502.h"

#include <string.h>

#ifndef M6502_HOT
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define M6502_HOT IRAM_ATTR   /* hot code out of the flash cache */
#else
#define M6502_HOT
#endif
#endif

static const uint8_t k_mode[256] = {
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x0, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0xd, 0xa, 0x1, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x9, 0x9, 0x0, 0x6, 0x0, 0x6, 0x2, 0x4, 0x5, 0x5,
    0x1, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x0,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x9, 0x0, 0x0, 0x6, 0x0, 0x0, 0x5, 0x5, 0x6, 0x0,
    0x1, 0xa, 0x1, 0xa, 0x7, 0x7, 0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x1, 0xa, 0x1, 0xa, 0x7, 0x7, 0x7, 0x0, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5};

static const uint8_t k_cycles[256] = {
    7, 6, 1, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 1, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 1, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 1, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    2, 6, 1, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    2, 5, 1, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 7, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 7, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7};

static inline uint8_t rd(dsp6502 *c, uint16_t a)
{
    const uint8_t *p = c->page_r[a >> 8];
    if (p) return p[a & 0xff];
#ifdef DSP6502_READ
    return DSP6502_READ(c->user, a);
#else
    return c->read(c->user, a);
#endif
}

static inline void wr(dsp6502 *c, uint16_t a, uint8_t v)
{
    uint8_t *p = c->page_w[a >> 8];
    if (p) { p[a & 0xff] = v; return; }
#ifdef DSP6502_WRITE
    DSP6502_WRITE(c->user, a, v);
#else
    c->write(c->user, a, v);
#endif
}

static inline uint8_t fetch(dsp6502 *c) { return rd(c, c->pc++); }

static inline uint8_t get_flags(const dsp6502 *c)
{
    return (uint8_t)((c->fn ? 0x80 : 0) | (c->fv ? 0x40 : 0) | (c->ft ? 0x20 : 0) | (c->fb ? 0x10 : 0) |
                     (c->fd ? 0x08 : 0) | (c->fi ? 0x04 : 0) | (c->fz ? 0x02 : 0) | (c->fc ? 0x01 : 0));
}

static inline void set_flags(dsp6502 *c, uint8_t v)
{
    c->fn = v & 0x80; c->fv = v & 0x40; c->ft = v & 0x20; c->fb = v & 0x10;
    c->fd = v & 0x08; c->fi = v & 0x04; c->fz = v & 0x02; c->fc = v & 0x01;
}

static inline void push(dsp6502 *c, uint8_t v) { wr(c, (uint16_t)(0x100 + c->sp), v); c->sp--; }
static inline uint8_t pop(dsp6502 *c) { c->sp++; return rd(c, (uint16_t)(0x100 + c->sp)); }
static inline void set_nz(dsp6502 *c, uint8_t v) { c->fz = v == 0; c->fn = (v & 0x80) != 0; }

void dsp6502_init(dsp6502 *c)
{
    memset(c->page_r, 0, sizeof c->page_r);
    memset(c->page_w, 0, sizeof c->page_w);
    c->sp = 0xfd;
}

void dsp6502_set_nmi(dsp6502 *c, uint8_t state)
{
    c->nmi_request = state;
    if (state == LINE_CLEAR) c->nmi_state = LINE_CLEAR;
}

void dsp6502_reset(dsp6502 *c)
{
    c->pc = (uint16_t)(rd(c, 0xfffc) | (rd(c, 0xfffd) << 8));
    c->a = c->x = c->y = 0;
    c->sp = 0xfd;
    set_flags(c, 0);
    c->fi = true;
    c->after_ei = false;
    c->halted = false;
    c->irq_request = c->nmi_request = c->nmi_state = LINE_CLEAR;
}

static int call_nmi(dsp6502 *c)
{
    if (c->nmi_state != LINE_CLEAR) return 0;
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((get_flags(c) & 0xef) | 0x20));
    c->fi = true;
    c->pc = (uint16_t)(rd(c, 0xfffa) | (rd(c, 0xfffb) << 8));
    if (c->nmi_request == LINE_PULSE) c->nmi_request = LINE_CLEAR;
    if (c->nmi_request == LINE_ASSERT) c->nmi_state = LINE_ASSERT;
    return 7;
}

static int call_irq(dsp6502 *c)
{
    if (c->fi) return 0;
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((get_flags(c) & 0xef) | 0x20));
    c->fi = true;
    c->pc = (uint16_t)(rd(c, 0xfffe) | (rd(c, 0xffff) << 8));
    if (c->irq_request == LINE_HOLD) c->irq_request = LINE_CLEAR;
    return 7;
}

static inline void adc(dsp6502 *c, uint8_t value)
{
    uint8_t a = c->a;
    if (c->fd) {
        uint8_t carry = c->fc ? 1 : 0;
        uint16_t binary = (uint16_t)(a + value + carry);
        c->fv = ((~(a ^ value)) & (a ^ binary) & 0x80) != 0;
        uint16_t low = (uint16_t)((a & 0x0f) + (value & 0x0f) + carry);
        if (low > 9) low += 6;
        uint16_t high = (uint16_t)((a >> 4) + (value >> 4) + (low > 15 ? 1 : 0));
        if (high > 9) high += 6;
        c->fc = high > 15;
        c->a = (uint8_t)((high << 4) | (low & 0x0f));
        c->fz = (binary & 0xff) == 0;
        c->fn = !c->fz && (high & 8) != 0;
    } else {
        uint16_t result = (uint16_t)(a + value + (c->fc ? 1 : 0));
        c->fv = ((~(a ^ value)) & (a ^ result) & 0x80) != 0;
        c->fc = (result & 0xff00) != 0;
        c->a = (uint8_t)result;
        c->fz = c->a == 0;
        c->fn = !c->fz && (c->a & 0x80) != 0;
    }
}

static inline void sbc(dsp6502 *c, uint8_t value)
{
    uint8_t a = c->a;
    if (c->fd) {
        uint8_t carry = c->fc ? 0 : 1;
        uint16_t diff = (uint16_t)(a - value - carry);
        c->fv = ((a ^ value) & (a ^ diff) & 0x80) != 0;
        c->fc = (diff & 0xff00) == 0;
        int8_t low = (int8_t)((a & 0x0f) - (value & 0x0f) - carry);
        if (low < 0) low = (int8_t)(low - 6);
        int8_t high = (int8_t)((a >> 4) - (value >> 4) - (low < 0 ? 1 : 0));
        if (high < 0) high = (int8_t)(high - 6);
        c->a = (uint8_t)(((uint8_t)high << 4) | ((uint8_t)low & 0x0f));
        c->fz = (diff & 0xff) == 0;
        c->fn = !c->fz && (diff & 0x80) != 0;
    } else {
        uint16_t diff = (uint16_t)(a - value - (c->fc ? 0 : 1));
        c->fv = ((a ^ value) & (a ^ diff) & 0x80) != 0;
        c->fc = (diff & 0xff00) == 0;
        c->a = (uint8_t)diff;
        c->fz = c->a == 0;
        c->fn = !c->fz && (c->a & 0x80) != 0;
    }
}

static inline void compare(dsp6502 *c, uint8_t reg, uint8_t value)
{
    uint16_t diff = (uint16_t)(reg - value);
    c->fc = (diff & 0xff00) == 0;
    c->fz = (diff & 0xff) == 0;
    c->fn = (diff & 0x80) != 0;
}

static inline void branch(dsp6502 *c, bool cond, uint8_t offset)
{
    if (!cond) return;
    uint16_t target = (uint16_t)(c->pc + (int8_t)offset);
    c->extra_cycles += ((target ^ c->pc) & 0xff00) != 0 ? 2 : 1;
    c->pc = target;
}

static inline bool no_page_penalty_x(uint8_t o) { return o == 0x1f || o == 0x3f || o == 0x5f || o == 0x7f; }
static inline bool no_page_penalty_y(uint8_t o) { return o == 0x1b || o == 0x3b || o == 0x5b || o == 0x7b || o == 0x99; }
static inline bool no_page_penalty_iy(uint8_t o)
{
    return o == 0x13 || o == 0x33 || o == 0x53 || o == 0x73 || o == 0x93 || o == 0x91;
}

#define RMW(expr_result)                          \
    do {                                          \
        uint8_t value = rd(c, addr);              \
        uint8_t result;                           \
        expr_result;                              \
        wr(c, addr, value);                       \
        wr(c, addr, result);                      \
        set_nz(c, result);                        \
    } while (0)

int M6502_HOT dsp6502_run(dsp6502 *c, int cycles)
{
    int executed = 0;
    while (executed < cycles) {
        c->extra_cycles = 0;
        if (c->halted) {
            int step = cycles - executed < 8 ? cycles - executed : 8;
            executed += step;
#ifdef DSP6502_CYCLES
            DSP6502_CYCLES(c->user, step);
#else
            if (c->cycles) c->cycles(c->user, step);
#endif
            continue;
        }
        if (!c->after_ei) {
            if (c->nmi_request != LINE_CLEAR) c->extra_cycles = call_nmi(c);
            else if (c->irq_request != LINE_CLEAR) c->extra_cycles = call_irq(c);
        }
        c->after_ei = false;
        if (c->pc == c->idle_pc && c->idle) {
            executed += c->idle(c->user, cycles - executed);
            if (executed >= cycles) break;
        }
#ifdef DSP6502_HOOK
        // driver hook before an instruction (a fast-forward): cycles it took
        // the place of (0: none); the interrupts are checked again after it
        {
            const int n = DSP6502_HOOK(c->user, c, cycles - executed);
            if (n) { executed += n; continue; }
        }
#endif
        c->ppc = c->pc;

#ifdef DSP6502_PROFILE
        { extern void prof65(uint16_t pc); prof65(c->pc); }
#endif
        uint8_t ins = fetch(c);
        uint8_t offset = 0;
        uint16_t addr = c->address;
        switch (k_mode[ins]) {
        case 0x0: break;
        case 0x1: addr = c->pc; c->pc += 1; break;
        case 0x2: addr = (uint16_t)(rd(c, c->pc) | (rd(c, (uint16_t)(c->pc + 1)) << 8)); c->pc += 2; break;
        case 0x4: addr = (uint16_t)(rd(c, c->pc) + (rd(c, (uint16_t)(c->pc + 1)) << 8) + c->x); c->pc += 2; break;
        case 0x5: {
            uint16_t base = (uint16_t)(rd(c, c->pc) | (rd(c, (uint16_t)(c->pc + 1)) << 8));
            if (!no_page_penalty_x(ins) && (((base + c->x) ^ base) & 0xff00) != 0) c->extra_cycles += 1;
            addr = (uint16_t)(base + c->x);
            c->pc += 2;
            break;
        }
        case 0x6: {
            uint16_t base = (uint16_t)(rd(c, c->pc) | (rd(c, (uint16_t)(c->pc + 1)) << 8));
            if (!no_page_penalty_y(ins) && (((base + c->y) ^ base) & 0xff00) != 0) c->extra_cycles += 1;
            addr = (uint16_t)(base + c->y);
            c->pc += 2;
            break;
        }
        case 0x7: addr = fetch(c); break;
        case 0x8: addr = (uint8_t)(fetch(c) + c->x); break;
        case 0x9: addr = (uint8_t)(fetch(c) + c->y); break;
        case 0xa: {
            uint8_t ptr = (uint8_t)(fetch(c) + c->x);
            addr = (uint16_t)(rd(c, ptr) | (rd(c, (uint8_t)(ptr + 1)) << 8));
            break;
        }
        case 0xb: {
            uint8_t ptr = fetch(c);
            uint16_t base = (uint16_t)(rd(c, ptr) | (rd(c, (uint8_t)(ptr + 1)) << 8));
            if (!no_page_penalty_iy(ins) && (((base + c->y) ^ base) & 0xff00) != 0) c->extra_cycles += 1;
            addr = (uint16_t)(base + c->y);
            break;
        }
        case 0xc: {
            uint8_t ptr = fetch(c);
            addr = (uint16_t)(rd(c, ptr) | (rd(c, (uint8_t)(ptr + 1)) << 8));
            break;
        }
        default: offset = fetch(c); break;
        }
        c->address = addr;

        switch (ins) {
        case 0x00:
            c->pc += 1;
            push(c, (uint8_t)(c->pc >> 8));
            push(c, (uint8_t)c->pc);
            push(c, (uint8_t)(get_flags(c) | 0x30));
            c->fb = true;
            c->fi = true;
            c->pc = (uint16_t)(rd(c, 0xfffe) | (rd(c, 0xffff) << 8));
            break;
        case 0x01: case 0x05: case 0x09: case 0x0d: case 0x11: case 0x15: case 0x19: case 0x1d:
            c->a |= rd(c, addr); set_nz(c, c->a); break;
        case 0x06: case 0x0e: case 0x16: case 0x1e:
            RMW(c->fc = (value & 0x80) != 0; result = (uint8_t)(value << 1)); break;
        case 0x0a: c->fc = (c->a & 0x80) != 0; c->a = (uint8_t)(c->a << 1); set_nz(c, c->a); break;
        case 0x08: push(c, (uint8_t)(get_flags(c) | 0x30)); break;
        case 0x10: branch(c, !c->fn, offset); break;
        case 0x18: c->fc = false; break;
        case 0x20: {
            uint16_t target = (uint16_t)(rd(c, c->pc) | (rd(c, (uint16_t)(c->pc + 1)) << 8));
            c->pc += 1;
            push(c, (uint8_t)(c->pc >> 8));
            push(c, (uint8_t)c->pc);
            c->pc = target;
            break;
        }
        case 0x21: case 0x25: case 0x29: case 0x2d: case 0x31: case 0x35: case 0x39: case 0x3d:
            c->a &= rd(c, addr); set_nz(c, c->a); break;
        case 0x24: case 0x2c: {
            uint8_t v = rd(c, addr);
            c->fz = (c->a & v) == 0; c->fn = (v & 0x80) != 0; c->fv = (v & 0x40) != 0;
            break;
        }
        case 0x26: case 0x2e: case 0x36: case 0x3e:
            RMW(result = (uint8_t)((value << 1) | (c->fc ? 1 : 0)); c->fc = (value & 0x80) != 0); break;
        case 0x2a: {
            uint8_t r = (uint8_t)((c->a << 1) | (c->fc ? 1 : 0));
            c->fc = (c->a & 0x80) != 0; c->a = r; set_nz(c, c->a);
            break;
        }
        case 0x28: set_flags(c, pop(c)); c->after_ei = true; break;
        case 0x30: branch(c, c->fn, offset); break;
        case 0x38: c->fc = true; break;
        case 0x40:
            set_flags(c, pop(c));
            c->pc = pop(c);
            c->pc |= (uint16_t)(pop(c) << 8);
            break;
        case 0x41: case 0x45: case 0x49: case 0x4d: case 0x51: case 0x55: case 0x59: case 0x5d:
            c->a ^= rd(c, addr); set_nz(c, c->a); break;
        case 0x46: case 0x4e: case 0x56: case 0x5e:
            RMW(c->fc = (value & 1) != 0; result = (uint8_t)(value >> 1)); break;
        case 0x4a: c->fc = (c->a & 1) != 0; c->a = (uint8_t)(c->a >> 1); set_nz(c, c->a); break;
        case 0x48: push(c, c->a); break;
        case 0x4c: c->pc = addr; break;
        case 0x50: branch(c, !c->fv, offset); break;
        case 0x58: c->fi = false; c->after_ei = true; break;
        case 0x60:
            c->pc = pop(c);
            c->pc |= (uint16_t)(pop(c) << 8);
            c->pc += 1;
            break;
        case 0x61: case 0x65: case 0x69: case 0x6d: case 0x71: case 0x75: case 0x79: case 0x7d:
            adc(c, rd(c, addr)); break;
        case 0x66: case 0x6e: case 0x76: case 0x7e:
            RMW(result = (uint8_t)((value >> 1) | (c->fc ? 0x80 : 0)); c->fc = (value & 1) != 0); break;
        case 0x6a: {
            uint8_t r = (uint8_t)((c->a >> 1) | (c->fc ? 0x80 : 0));
            c->fc = (c->a & 1) != 0; c->a = r; set_nz(c, c->a);
            break;
        }
        case 0x68: c->a = pop(c); set_nz(c, c->a); break;
        case 0x6c: {
            uint16_t ptr = (uint16_t)(rd(c, c->pc) | (rd(c, (uint16_t)(c->pc + 1)) << 8));
            uint16_t high = (uint16_t)((ptr & 0xff00) | (uint8_t)(ptr + 1));
            c->pc = (uint16_t)(rd(c, ptr) | (rd(c, high) << 8));
            break;
        }
        case 0x70: branch(c, c->fv, offset); break;
        case 0x78: c->fi = true; c->after_ei = true; break;
        case 0x81: case 0x85: case 0x8d: case 0x91: case 0x95: case 0x99: case 0x9d: wr(c, addr, c->a); break;
        case 0x84: case 0x8c: case 0x94: wr(c, addr, c->y); break;
        case 0x86: case 0x8e: case 0x96: wr(c, addr, c->x); break;
        case 0x88: c->y--; set_nz(c, c->y); break;
        case 0x8a: c->a = c->x; set_nz(c, c->a); break;
        case 0x90: branch(c, !c->fc, offset); break;
        case 0x98: c->a = c->y; set_nz(c, c->a); break;
        case 0x9a: c->sp = c->x; break;
        case 0xa0: case 0xa4: case 0xac: case 0xb4: case 0xbc: c->y = rd(c, addr); set_nz(c, c->y); break;
        case 0xa1: case 0xa5: case 0xa9: case 0xad: case 0xb1: case 0xb5: case 0xb9: case 0xbd:
            c->a = rd(c, addr); set_nz(c, c->a); break;
        case 0xa2: case 0xa6: case 0xae: case 0xb6: case 0xbe: c->x = rd(c, addr); set_nz(c, c->x); break;
        case 0xa8: c->y = c->a; set_nz(c, c->y); break;
        case 0xaa: c->x = c->a; set_nz(c, c->x); break;
        case 0xb0: branch(c, c->fc, offset); break;
        case 0xb8: c->fv = false; break;
        case 0xba: c->x = c->sp; set_nz(c, c->x); break;
        case 0xc0: case 0xc4: case 0xcc: compare(c, c->y, rd(c, addr)); break;
        case 0xc1: case 0xc5: case 0xc9: case 0xcd: case 0xd1: case 0xd5: case 0xd9: case 0xdd:
            compare(c, c->a, rd(c, addr)); break;
        case 0xc6: case 0xce: case 0xd6: case 0xde: RMW(result = (uint8_t)(value - 1)); break;
        case 0xc8: c->y++; set_nz(c, c->y); break;
        case 0xca: c->x--; set_nz(c, c->x); break;
        case 0xd0: branch(c, !c->fz, offset); break;
        case 0xd8: c->fd = false; break;
        case 0xe0: case 0xe4: case 0xec: compare(c, c->x, rd(c, addr)); break;
        case 0xe1: case 0xe5: case 0xe9: case 0xeb: case 0xed: case 0xf1: case 0xf5: case 0xf9: case 0xfd:
            sbc(c, rd(c, addr)); break;
        case 0xe6: case 0xee: case 0xf6: case 0xfe: RMW(result = (uint8_t)(value + 1)); break;
        case 0xe8: c->x++; set_nz(c, c->x); break;
        case 0xf0: branch(c, c->fz, offset); break;
        case 0xf8: c->fd = true; break;
        case 0x02: case 0x42: c->pc -= 1; break;  // kil / jam
        case 0x03: case 0x07: case 0x0f: case 0x13: case 0x17: case 0x1b: case 0x1f: {  // slo
            uint8_t v = rd(c, addr);
            wr(c, addr, v);
            c->fc = (v & 0x80) != 0;
            v = (uint8_t)(v << 1);
            c->a |= v; set_nz(c, c->a);
            wr(c, addr, v);
            break;
        }
        case 0x0b: case 0x2b: {  // anc
            c->a &= rd(c, addr);
            uint8_t carry = c->fc ? 1 : 0;
            c->fc = (c->a & 0x80) != 0;
            c->a = (uint8_t)((c->a << 1) | carry);
            set_nz(c, c->a);
            break;
        }
        case 0x23: case 0x27: case 0x2f: case 0x33: case 0x37: case 0x3b: case 0x3f: {  // rla
            uint8_t v = rd(c, addr);
            wr(c, addr, v);
            uint16_t rot = (uint16_t)((v << 1) | (c->fc ? 1 : 0));
            c->fc = (rot & 0x100) != 0;
            c->a &= (uint8_t)rot; set_nz(c, c->a);
            wr(c, addr, (uint8_t)rot);
            break;
        }
        case 0x43: case 0x47: case 0x4f: case 0x53: case 0x57: case 0x5b: case 0x5f: {  // sre
            uint8_t v = rd(c, addr);
            wr(c, addr, v);
            c->fc = (v & 1) != 0;
            v = (uint8_t)(v >> 1);
            c->a ^= v; set_nz(c, c->a);
            wr(c, addr, v);
            break;
        }
        case 0x4b:  // alr
            c->a &= rd(c, addr);
            c->fc = (c->a & 1) != 0;
            c->a = (uint8_t)(c->a >> 1);
            c->fz = c->a == 0; c->fn = false;
            break;
        case 0x63: case 0x67: case 0x6f: case 0x73: case 0x77: case 0x7b: case 0x7f: {  // rra
            uint8_t v = rd(c, addr);
            wr(c, addr, v);
            uint16_t rot = (uint16_t)((c->fc ? 0x100 : 0) | v);
            c->fc = (v & 1) != 0;
            v = (uint8_t)(rot >> 1);
            adc(c, v);
            wr(c, addr, v);
            break;
        }
        case 0x6b: {  // arr
            c->a &= rd(c, addr);
            uint8_t carry = c->fc ? 0x80 : 0;
            c->fc = (c->a & 1) != 0;
            c->a = (uint8_t)((c->a >> 1) | carry);
            set_nz(c, c->a);
            break;
        }
        case 0x83: case 0x87: case 0x8f: case 0x97: wr(c, addr, (uint8_t)(c->a & c->x)); break;  // sax
        case 0x8b: c->a = (uint8_t)(c->x & rd(c, addr)); set_nz(c, c->a); break;              // xaa
        case 0xa3: case 0xaf: case 0xb3: case 0xb7: case 0xbf:                               // lax
            c->a = c->x = rd(c, addr); set_nz(c, c->a); break;
        case 0xc3: case 0xcf: case 0xd3: case 0xd7: case 0xdb: case 0xdf: {  // dcp
            uint8_t v = rd(c, addr);
            wr(c, addr, v);
            v = (uint8_t)(v - 1);
            c->fc = c->a >= v;
            uint8_t d = (uint8_t)(c->a - v);
            c->fz = d == 0; c->fn = (d & 0x80) != 0;
            wr(c, addr, v);
            break;
        }
        case 0xe3: case 0xef: case 0xf3: case 0xf7: case 0xfb: case 0xff: {  // isb
            uint8_t v = rd(c, addr);
            wr(c, addr, v);
            v = (uint8_t)(v + 1);
            sbc(c, v);
            wr(c, addr, v);
            break;
        }
        default: break;
        }

        int elapsed = k_cycles[ins] + c->extra_cycles;
        executed += elapsed;
#ifdef DSP6502_CYCLES
        DSP6502_CYCLES(c->user, elapsed);
#else
        if (c->cycles) c->cycles(c->user, elapsed);
#endif
    }
    return executed;
}
