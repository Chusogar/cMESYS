// lr35902.c - Sharp LR35902: a C11 port of dsp-cpp's cpu/lr35902; see lr35902.h.
#include "lr35902.h"

static const uint8_t kCycles[256] = {
     4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,  // 0
     4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,  // 1
     8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,  // 2
     8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,  // 3
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 4
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 5
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 6
     8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,  // 7
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 8
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // 9
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // a
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,  // b
     8,12,12,16,12,16, 8,16, 8,16,12, 0,12,24, 8,16,  // c
     8,12,12, 4,12,16, 8,16, 8,16,12, 4,12, 4, 8,16,  // d
    12,12, 8, 4, 4,16, 8,16,16, 4,16, 4, 4, 4, 8,16,  // e
    12,12, 8, 4, 4,16, 8,16,12, 8,16, 4, 4, 4, 8,16,  // f
};

static const uint8_t kCyclesCb[256] = {
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 0
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 1
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 2
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 3
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 4
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 5
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 6
     8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,  // 7
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 8
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // 9
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // a
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // b
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // c
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // d
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // e
     8, 8, 8, 8, 8, 8,16, 8, 8, 8, 8, 8, 8, 8,16, 8,  // f
};


static inline uint8_t rd(lr35902 *R, uint16_t a) { return R->read(R->user, a); }
static inline void wr(lr35902 *R, uint16_t a, uint8_t v) { R->write(R->user, a, v); }
static uint8_t inc8(lr35902 *R, uint8_t v); static uint8_t dec8(lr35902 *R, uint8_t v); static void exec_cb(lr35902 *R);
static uint8_t fetch8(lr35902 *R) { return rd(R, R->pc++); }
static uint16_t fetch16(lr35902 *R) {
    uint16_t v = (uint16_t)(rd(R, R->pc) | (rd(R, (uint16_t)(R->pc + 1)) << 8));
    R->pc = (uint16_t)(R->pc + 2);
    return v;
}
static void push16(lr35902 *R, uint16_t value) {
    R->sp = (uint16_t)(R->sp - 2);
    wr(R, R->sp, (uint8_t)(value & 0xff));
    wr(R, (uint16_t)(R->sp + 1), (uint8_t)(value >> 8));
}
static uint16_t pop16(lr35902 *R) {
    uint16_t v = (uint16_t)(rd(R, R->sp) | (rd(R, (uint16_t)(R->sp + 1)) << 8));
    R->sp = (uint16_t)(R->sp + 2);
    return v;
}

static uint8_t get_f(lr35902 *R) {
    return (uint8_t)((R->fz ? 0x80 : 0) | (R->fn ? 0x40 : 0) | (R->fh ? 0x20 : 0) | (R->fc ? 0x10 : 0));
}
static void set_f(lr35902 *R, uint8_t value) {
    R->fz = (value & 0x80) != 0;
    R->fn = (value & 0x40) != 0;
    R->fh = (value & 0x20) != 0;
    R->fc = (value & 0x10) != 0;
}

static uint8_t inc8(lr35902 *R, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    R->fz = r == 0;
    R->fn = false;
    R->fh = (v & 0x0f) == 0x0f;
    return r;
}
static uint8_t dec8(lr35902 *R, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    R->fz = r == 0;
    R->fn = true;
    R->fh = (v & 0x0f) == 0;
    return r;
}
static void add_a(lr35902 *R, uint8_t v) {
    int sum = R->a + v;
    R->fh = ((R->a & 0xf) + (v & 0xf)) > 0xf;
    R->fc = sum > 0xff;
    R->a = (uint8_t)(sum);
    R->fz = R->a == 0;
    R->fn = false;
}
static void adc_a(lr35902 *R, uint8_t v) {
    int carry = R->fc ? 1 : 0;
    int sum = R->a + v + carry;
    R->fh = ((R->a & 0xf) + (v & 0xf) + carry) > 0xf;
    R->fc = sum > 0xff;
    R->a = (uint8_t)(sum);
    R->fz = R->a == 0;
    R->fn = false;
}
static void sub_a(lr35902 *R, uint8_t v) {
    R->fh = (R->a & 0xf) < (v & 0xf);
    R->fc = R->a < v;
    R->a = (uint8_t)(R->a - v);
    R->fz = R->a == 0;
    R->fn = true;
}
static void sbc_a(lr35902 *R, uint8_t v) {
    int carry = R->fc ? 1 : 0;
    int diff = R->a - v - carry;
    R->fh = ((R->a & 0xf) - (v & 0xf) - carry) < 0;
    R->fc = diff < 0;
    R->a = (uint8_t)(diff);
    R->fz = R->a == 0;
    R->fn = true;
}
static void and_a(lr35902 *R, uint8_t v) {
    R->a = (uint8_t)(R->a & v);
    R->fz = R->a == 0;
    R->fn = false;
    R->fh = true;
    R->fc = false;
}
static void or_a(lr35902 *R, uint8_t v) {
    R->a = (uint8_t)(R->a | v);
    R->fz = R->a == 0;
    R->fn = false;
    R->fh = false;
    R->fc = false;
}
static void xor_a(lr35902 *R, uint8_t v) {
    R->a = (uint8_t)(R->a ^ v);
    R->fz = R->a == 0;
    R->fn = false;
    R->fh = false;
    R->fc = false;
}
static void cp_a(lr35902 *R, uint8_t v) {
    R->fh = (R->a & 0xf) < (v & 0xf);
    R->fc = R->a < v;
    uint8_t r = (uint8_t)(R->a - v);
    R->fz = r == 0;
    R->fn = true;
}
static void add_hl(lr35902 *R, uint16_t v) {
    uint16_t hl = (uint16_t)((R->h << 8) | R->l);
    uint32_t sum = (uint32_t)(hl) + v;
    R->fh = ((hl & 0xfff) + (v & 0xfff)) > 0xfff;
    R->fc = sum > 0xffff;
    hl = (uint16_t)(sum);
    R->h = (uint8_t)(hl >> 8);
    R->l = (uint8_t)(hl & 0xff);
    R->fn = false;
}
static uint8_t rlc(lr35902 *R, uint8_t v) {
    R->fc = (v & 0x80) != 0;
    v = (uint8_t)((v << 1) | (v >> 7));
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t rrc(lr35902 *R, uint8_t v) {
    R->fc = (v & 1) != 0;
    v = (uint8_t)((v >> 1) | (v << 7));
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t rl(lr35902 *R, uint8_t v) {
    uint8_t carry_in = R->fc ? 1 : 0;
    R->fc = (v & 0x80) != 0;
    v = (uint8_t)((v << 1) | carry_in);
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t rr(lr35902 *R, uint8_t v) {
    uint8_t carry_in = R->fc ? 0x80 : 0;
    R->fc = (v & 1) != 0;
    v = (uint8_t)((v >> 1) | carry_in);
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t sla(lr35902 *R, uint8_t v) {
    R->fc = (v & 0x80) != 0;
    v = (uint8_t)(v << 1);
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t sra(lr35902 *R, uint8_t v) {
    R->fc = (v & 1) != 0;
    v = (uint8_t)((v >> 1) | (v & 0x80));
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t srl(lr35902 *R, uint8_t v) {
    R->fc = (v & 1) != 0;
    v = (uint8_t)(v >> 1);
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    return v;
}
static uint8_t swap(lr35902 *R, uint8_t v) {
    v = (uint8_t)((v << 4) | (v >> 4));
    R->fz = v == 0;
    R->fn = false;
    R->fh = false;
    R->fc = false;
    return v;
}
static void bit(lr35902 *R, uint8_t n, uint8_t v) {
    R->fz = ((v >> n) & 1) == 0;
    R->fn = false;
    R->fh = true;
}


static uint8_t reg8(lr35902 *R, uint8_t index) {
    switch (index) {
        case 0: return R->b;
        case 1: return R->c;
        case 2: return R->d;
        case 3: return R->e;
        case 4: return R->h;
        case 5: return R->l;
        case 6: return rd(R, (uint16_t)((R->h << 8) | R->l));
        default: return R->a;
    }
}
static void set_reg8(lr35902 *R, uint8_t index, uint8_t value) {
    switch (index) {
        case 0: R->b = value; break;
        case 1: R->c = value; break;
        case 2: R->d = value; break;
        case 3: R->e = value; break;
        case 4: R->h = value; break;
        case 5: R->l = value; break;
        case 6: wr(R, (uint16_t)((R->h << 8) | R->l), value); break;
        default: R->a = value; break;
    }
}


static void exec_cb(lr35902 *R) {
    uint8_t op = fetch8(R);
    uint8_t reg = op & 7;
    uint8_t kind = op >> 3;
    uint8_t v = reg8(R, reg);
    if (kind < 8) {
        switch (kind) {
            case 0: set_reg8(R, reg, rlc(R, v)); break;
            case 1: set_reg8(R, reg, rrc(R, v)); break;
            case 2: set_reg8(R, reg, rl(R, v)); break;
            case 3: set_reg8(R, reg, rr(R, v)); break;
            case 4: set_reg8(R, reg, sla(R, v)); break;
            case 5: set_reg8(R, reg, sra(R, v)); break;
            case 6: set_reg8(R, reg, swap(R, v)); break;
            default: set_reg8(R, reg, srl(R, v)); break;
        }
    } else if (kind < 16) {
        bit(R, (uint8_t)(kind - 8), v);
    } else if (kind < 24) {
        set_reg8(R, reg, (uint8_t)(v & ~(1 << (kind - 16))));
    } else {
        set_reg8(R, reg, (uint8_t)(v | (1 << (kind - 24))));
    }
    R->extra_cycles_ = (int)kCyclesCb[op];
}

static void exec(lr35902 *R, uint8_t op) {
    uint16_t hl = (uint16_t)((R->h << 8) | R->l);
    switch (op) {
        // 0x00-0x0F
        case 0x00: break;  // NOP
        case 0x01: { uint16_t v = fetch16(R); R->b = (uint8_t)(v >> 8); R->c = (uint8_t)(v); break; }
        case 0x02: wr(R, (uint16_t)((R->b << 8) | R->c), R->a); break;
        case 0x03: { uint16_t v = (uint16_t)(((R->b << 8) | R->c) + 1); R->b = (uint8_t)(v >> 8); R->c = (uint8_t)(v); break; }
        case 0x04: R->b = inc8(R, R->b); break;
        case 0x05: R->b = dec8(R, R->b); break;
        case 0x06: R->b = fetch8(R); break;
        case 0x07: R->fc = (R->a & 0x80) != 0; R->a = (uint8_t)((R->a << 1) | (R->a >> 7)); R->fz = R->fn = R->fh = false; break;  // RLCA
        case 0x08: { uint16_t addr = fetch16(R); wr(R, addr, (uint8_t)(R->sp & 0xff)); wr(R, (uint16_t)(addr + 1), (uint8_t)(R->sp >> 8)); break; }
        case 0x09: add_hl(R, (uint16_t)((R->b << 8) | R->c)); break;
        case 0x0a: R->a = rd(R, (uint16_t)((R->b << 8) | R->c)); break;
        case 0x0b: { uint16_t v = (uint16_t)(((R->b << 8) | R->c) - 1); R->b = (uint8_t)(v >> 8); R->c = (uint8_t)(v); break; }
        case 0x0c: R->c = inc8(R, R->c); break;
        case 0x0d: R->c = dec8(R, R->c); break;
        case 0x0e: R->c = fetch8(R); break;
        case 0x0f: R->fc = (R->a & 1) != 0; R->a = (uint8_t)((R->a >> 1) | (R->a << 7)); R->fz = R->fn = R->fh = false; break;  // RRCA

        // 0x10-0x1F
        case 0x10:  // STOP
            fetch8(R);
            if (R->change_speed) { R->speed ^= 1; R->change_speed = false; R->changed_speed = true; }
            break;
        case 0x11: { uint16_t v = fetch16(R); R->d = (uint8_t)(v >> 8); R->e = (uint8_t)(v); break; }
        case 0x12: wr(R, (uint16_t)((R->d << 8) | R->e), R->a); break;
        case 0x13: { uint16_t v = (uint16_t)(((R->d << 8) | R->e) + 1); R->d = (uint8_t)(v >> 8); R->e = (uint8_t)(v); break; }
        case 0x14: R->d = inc8(R, R->d); break;
        case 0x15: R->d = dec8(R, R->d); break;
        case 0x16: R->d = fetch8(R); break;
        case 0x17: { bool carry_in = R->fc; R->fc = (R->a & 0x80) != 0; R->a = (uint8_t)((R->a << 1) | (carry_in ? 1 : 0)); R->fz = R->fn = R->fh = false; break; }  // RLA
        case 0x18: R->pc = (uint16_t)(R->pc + 1 + (int8_t)(fetch8(R))); break;  // JR n
        case 0x19: add_hl(R, (uint16_t)((R->d << 8) | R->e)); break;
        case 0x1a: R->a = rd(R, (uint16_t)((R->d << 8) | R->e)); break;
        case 0x1b: { uint16_t v = (uint16_t)(((R->d << 8) | R->e) - 1); R->d = (uint8_t)(v >> 8); R->e = (uint8_t)(v); break; }
        case 0x1c: R->e = inc8(R, R->e); break;
        case 0x1d: R->e = dec8(R, R->e); break;
        case 0x1e: R->e = fetch8(R); break;
        case 0x1f: { bool carry_in = R->fc; R->fc = (R->a & 1) != 0; R->a = (uint8_t)((R->a >> 1) | (carry_in ? 0x80 : 0)); R->fz = R->fn = R->fh = false; break; }  // RRA

        // 0x20-0x2F
        case 0x20: { int8_t rel = (int8_t)(fetch8(R)); if (!R->fz) { R->pc = (uint16_t)(R->pc + rel); R->extra_cycles_ += 4; } break; }
        case 0x21: { uint16_t v = fetch16(R); R->h = (uint8_t)(v >> 8); R->l = (uint8_t)(v); break; }
        case 0x22: wr(R, hl, R->a); hl++; R->h = (uint8_t)(hl >> 8); R->l = (uint8_t)(hl); break;
        case 0x23: { uint16_t v = (uint16_t)(hl + 1); R->h = (uint8_t)(v >> 8); R->l = (uint8_t)(v); break; }
        case 0x24: R->h = inc8(R, R->h); break;
        case 0x25: R->h = dec8(R, R->h); break;
        case 0x26: R->h = fetch8(R); break;
        case 0x27: {  // DAA
            int t = R->a;
            if (!R->fn) {
                if (R->fh || (t & 0xf) > 9) t += 0x6;
                if (R->fc || t > 0x9f) t += 0x60;
            } else {
                if (R->fh) { t -= 6; if (!R->fc) t &= 0xff; }
                if (R->fc) t -= 0x60;
            }
            R->fh = false;
            if (t & 0x100) R->fc = true;
            R->a = (uint8_t)(t & 0xff);
            R->fz = R->a == 0;
            break;
        }
        case 0x28: { int8_t rel = (int8_t)(fetch8(R)); if (R->fz) { R->pc = (uint16_t)(R->pc + rel); R->extra_cycles_ += 4; } break; }
        case 0x29: add_hl(R, hl); break;
        case 0x2a: R->a = rd(R, hl); hl++; R->h = (uint8_t)(hl >> 8); R->l = (uint8_t)(hl); break;
        case 0x2b: { uint16_t v = (uint16_t)(hl - 1); R->h = (uint8_t)(v >> 8); R->l = (uint8_t)(v); break; }
        case 0x2c: R->l = inc8(R, R->l); break;
        case 0x2d: R->l = dec8(R, R->l); break;
        case 0x2e: R->l = fetch8(R); break;
        case 0x2f: R->a = (uint8_t)(~R->a); R->fn = R->fh = true; break;  // CPL

        // 0x30-0x3F
        case 0x30: { int8_t rel = (int8_t)(fetch8(R)); if (!R->fc) { R->pc = (uint16_t)(R->pc + rel); R->extra_cycles_ += 4; } break; }
        case 0x31: R->sp = fetch16(R); break;
        case 0x32: wr(R, hl, R->a); hl--; R->h = (uint8_t)(hl >> 8); R->l = (uint8_t)(hl); break;
        case 0x33: R->sp++; break;
        case 0x34: wr(R, hl, inc8(R, rd(R, hl))); break;
        case 0x35: wr(R, hl, dec8(R, rd(R, hl))); break;
        case 0x36: wr(R, hl, fetch8(R)); break;
        case 0x37: R->fc = true; R->fn = R->fh = false; break;  // SCF
        case 0x38: { int8_t rel = (int8_t)(fetch8(R)); if (R->fc) { R->pc = (uint16_t)(R->pc + rel); R->extra_cycles_ += 4; } break; }
        case 0x39: add_hl(R, R->sp); break;
        case 0x3a: R->a = rd(R, hl); hl--; R->h = (uint8_t)(hl >> 8); R->l = (uint8_t)(hl); break;
        case 0x3b: R->sp--; break;
        case 0x3c: R->a = inc8(R, R->a); break;
        case 0x3d: R->a = dec8(R, R->a); break;
        case 0x3e: R->a = fetch8(R); break;
        case 0x3f: R->fc = !R->fc; R->fn = R->fh = false; break;  // CCF

        // 0x40-0x7F: LD r,r' and HALT
        case 0x76: R->halt_ = true; break;

        // 0x80-0xBF: ALU A,r
        case 0xc0: if (!R->fz) { R->pc = pop16(R); R->extra_cycles_ += 12; } break;
        case 0xc1: { uint16_t v = pop16(R); R->b = (uint8_t)(v >> 8); R->c = (uint8_t)(v); break; }
        case 0xc2: { uint16_t addr = fetch16(R); if (!R->fz) { R->pc = addr; R->extra_cycles_ += 4; } break; }
        case 0xc3: R->pc = fetch16(R); break;
        case 0xc4: { uint16_t addr = fetch16(R); if (!R->fz) { push16(R, R->pc); R->pc = addr; R->extra_cycles_ += 12; } break; }
        case 0xc5: push16(R, (uint16_t)((R->b << 8) | R->c)); break;
        case 0xc6: add_a(R, fetch8(R)); break;
        case 0xc7: push16(R, R->pc); R->pc = 0x00; break;
        case 0xc8: if (R->fz) { R->pc = pop16(R); R->extra_cycles_ += 12; } break;
        case 0xc9: R->pc = pop16(R); break;
        case 0xca: { uint16_t addr = fetch16(R); if (R->fz) { R->pc = addr; R->extra_cycles_ += 4; } break; }
        case 0xcb: exec_cb(R); break;
        case 0xcc: { uint16_t addr = fetch16(R); if (R->fz) { push16(R, R->pc); R->pc = addr; R->extra_cycles_ += 12; } break; }
        case 0xcd: { uint16_t addr = fetch16(R); push16(R, R->pc); R->pc = addr; break; }
        case 0xce: adc_a(R, fetch8(R)); break;
        case 0xcf: push16(R, R->pc); R->pc = 0x08; break;

        case 0xd0: if (!R->fc) { R->pc = pop16(R); R->extra_cycles_ += 12; } break;
        case 0xd1: { uint16_t v = pop16(R); R->d = (uint8_t)(v >> 8); R->e = (uint8_t)(v); break; }
        case 0xd2: { uint16_t addr = fetch16(R); if (!R->fc) { R->pc = addr; R->extra_cycles_ += 4; } break; }
        case 0xd4: { uint16_t addr = fetch16(R); if (!R->fc) { push16(R, R->pc); R->pc = addr; R->extra_cycles_ += 12; } break; }
        case 0xd5: push16(R, (uint16_t)((R->d << 8) | R->e)); break;
        case 0xd6: sub_a(R, fetch8(R)); break;
        case 0xd7: push16(R, R->pc); R->pc = 0x10; break;
        case 0xd8: if (R->fc) { R->pc = pop16(R); R->extra_cycles_ += 12; } break;
        case 0xd9: R->pc = pop16(R); R->ime = true; R->after_ei_ = true; break;  // RETI (matches gb.pas: one-instruction delay, same as EI)
        case 0xda: { uint16_t addr = fetch16(R); if (R->fc) { R->pc = addr; R->extra_cycles_ += 4; } break; }
        case 0xdc: { uint16_t addr = fetch16(R); if (R->fc) { push16(R, R->pc); R->pc = addr; R->extra_cycles_ += 12; } break; }
        case 0xde: sbc_a(R, fetch8(R)); break;
        case 0xdf: push16(R, R->pc); R->pc = 0x18; break;

        case 0xe0: wr(R, (uint16_t)(0xff00 + fetch8(R)), R->a); break;  // LDH (n),A
        case 0xe1: { uint16_t v = pop16(R); R->h = (uint8_t)(v >> 8); R->l = (uint8_t)(v); break; }
        case 0xe2: wr(R, (uint16_t)(0xff00 + R->c), R->a); break;  // LD (C),A
        case 0xe5: push16(R, hl); break;
        case 0xe6: and_a(R, fetch8(R)); break;
        case 0xe7: push16(R, R->pc); R->pc = 0x20; break;
        case 0xe8: {  // ADD SP,n
            int8_t rel = (int8_t)(fetch8(R));
            R->fz = R->fn = false;
            R->fc = ((R->sp & 0xff) + ((uint8_t)(rel) & 0xff)) > 0xff;
            R->fh = ((R->sp & 0xf) + ((uint8_t)(rel) & 0xf)) > 0xf;
            R->sp = (uint16_t)(R->sp + rel);
            break;
        }
        case 0xe9: R->pc = hl; break;  // JP (HL)
        case 0xea: wr(R, fetch16(R), R->a); break;
        case 0xee: xor_a(R, fetch8(R)); break;
        case 0xef: push16(R, R->pc); R->pc = 0x28; break;

        case 0xf0: R->a = rd(R, (uint16_t)(0xff00 + fetch8(R))); break;  // LDH A,(n)
        case 0xf1: set_f(R, (uint8_t)(rd(R, R->sp) & 0xf0)); R->a = rd(R, (uint16_t)(R->sp + 1)); R->sp = (uint16_t)(R->sp + 2); break;  // POP AF
        case 0xf2: R->a = rd(R, (uint16_t)(0xff00 + R->c)); break;  // LD A,(C)
        case 0xf3: R->ime = false; R->after_ei_ = false; break;  // DI
        case 0xf5: push16(R, (uint16_t)((R->a << 8) | get_f(R))); break;  // PUSH AF
        case 0xf6: or_a(R, fetch8(R)); break;
        case 0xf7: push16(R, R->pc); R->pc = 0x30; break;
        case 0xf8: {  // LD HL,SP+n
            R->fz = R->fn = false;
            uint8_t rel = fetch8(R);
            R->fc = ((R->sp & 0xff) + rel) > 0xff;
            R->fh = ((R->sp & 0xf) + (rel & 0xf)) > 0xf;
            hl = (uint16_t)(R->sp + (int8_t)(rel));
            R->h = (uint8_t)(hl >> 8);
            R->l = (uint8_t)(hl);
            break;
        }
        case 0xf9: R->sp = hl; break;  // LD SP,HL
        case 0xfa: R->a = rd(R, fetch16(R)); break;
        case 0xfb: R->ime = true; R->after_ei_ = true; break;  // EI
        case 0xfe: cp_a(R, fetch8(R)); break;
        case 0xff: push16(R, R->pc); R->pc = 0x38; break;

        default:
            // 0x40-0x7f (LD r,r') and 0x80-0xbf (ALU A,r) share the same
            // "destination/operation in bits 3-5, source register in bits
            // 0-2" layout as the CB table, so they are decoded uniformly
            // here instead of as 128 separate cases.
            if (op >= 0x40 && op <= 0x7f) {
                set_reg8(R, (uint8_t)((op >> 3) & 7), reg8(R, (uint8_t)(op & 7)));
            } else if (op >= 0x80 && op <= 0xbf) {
                uint8_t v = reg8(R, (uint8_t)(op & 7));
                switch ((op >> 3) & 7) {
                    case 0: add_a(R, v); break;
                    case 1: adc_a(R, v); break;
                    case 2: sub_a(R, v); break;
                    case 3: sbc_a(R, v); break;
                    case 4: and_a(R, v); break;
                    case 5: xor_a(R, v); break;
                    case 6: or_a(R, v); break;
                    default: cp_a(R, v); break;
                }
            }
            break;
    }
}



void lr35902_reset(lr35902 *R)
{
    R->speed = 0;
    R->change_speed = R->changed_speed = false;
    R->sp = R->pc = 0;
    R->a = R->b = R->c = R->d = R->e = R->h = R->l = 0;
    R->fz = R->fn = R->fh = R->fc = false;
    R->ime = R->after_ei_ = R->halt_ = false;
    R->extra_cycles_ = R->stall_cycles_ = 0;
    R->vblank_ena = R->lcdstat_ena = R->timer_ena = R->serial_ena = R->joystick_ena = false;
    R->vblank_req = R->lcdstat_req = R->timer_req = R->serial_req = R->joystick_req = false;
}

static int service_interrupt(lr35902 *R)
{
    bool *const req[5] = { &R->vblank_req, &R->lcdstat_req, &R->timer_req, &R->serial_req, &R->joystick_req };
    const bool ena[5] = { R->vblank_ena, R->lcdstat_ena, R->timer_ena, R->serial_ena, R->joystick_ena };
    for (int i = 0; i < 5; i++) {
        if (ena[i] && *req[i]) {
            int extra = R->halt_ ? 4 : 0;
            R->halt_ = false;
            if (R->ime) {
                R->ime = false;
                *req[i] = false;
                push16(R, R->pc);
                R->pc = (uint16_t)(0x40 + i * 8);
                extra += 20;
            }
            return extra;
        }
    }
    return 0;
}

int lr35902_run(lr35902 *R, int cycles)
{
    int total = 0;
    while (total < cycles) {
        const bool check = !R->after_ei_;
        R->after_ei_ = false;
        const int extra = check ? service_interrupt(R) : 0;
        if (R->halt_) {
            const int n = 4 + extra;
            if (R->cycles) R->cycles(R->user, n);
            total += n;
            continue;
        }
        const uint8_t opcode = fetch8(R);
        R->extra_cycles_ = 0;
        exec(R, opcode);
        const int stall = R->stall_cycles_;
        R->stall_cycles_ = 0;
        const int n = kCycles[opcode] + R->extra_cycles_ + extra + stall;
        if (R->cycles) R->cycles(R->user, n);
        total += n;
    }
    return total;
}
