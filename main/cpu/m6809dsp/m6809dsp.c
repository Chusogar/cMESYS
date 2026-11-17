#include "m6809dsp.h"

static inline uint8_t read(m6809d *P, uint16_t a) { return P->read(P->user, a); }
static inline void write(m6809d *P, uint16_t a, uint8_t v) { P->write(P->user, a, v); }
static inline uint16_t d(m6809d *P) { return (uint16_t)((P->a << 8) | P->b); }
static inline void set_d(m6809d *P, uint16_t v) { P->a = (uint8_t)(v >> 8); P->b = (uint8_t)v; }
#define dummy_arg(P, n) ((void)0)
#define dummy_vma(P, n) ((void)0)


// Base cycle count of every opcode (estados_t in m6809.pas).
static const uint8_t kCycles[256] = {




     6,  6,  6,  6,  6,  0,  6,  6,  6,  6,  6,  0,  6,  3,  3,  6,  // 00 direct
     0,  0,  2,  4,  0,  0,  4,  9,  0,  2,  3,  0,  3,  2,  8,  6,  // 10
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  // 20 branch
     2,  2,  2,  2,  5,  5,  5,  4,  0,  5,  3,  4, 16, 11,  0,  0,  // 30
     2,  0,  0,  2,  2,  0,  2,  2,  2,  2,  2,  0,  2,  2,  0,  2,  // 40 register A
     2,  0,  0,  2,  2,  0,  2,  2,  2,  2,  2,  0,  1,  2,  0,  1,  // 50 register B
     4,  0,  0,  4,  4,  0,  4,  4,  4,  4,  4,  0,  4,  4,  1,  4,  // 60 indexed
     7,  0,  0,  7,  7,  0,  7,  7,  7,  7,  7,  0,  7,  7,  4,  7,  // 70 extended
     2,  2,  2,  4,  2,  2,  2,  0,  2,  2,  2,  2,  4,  7,  3,  0,  // 80 immediate
     4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  4,  7,  7,  5,  5,  // 90 direct
     2,  2,  2,  4,  2,  2,  2,  2,  2,  2,  2,  2,  3,  5,  3,  3,  // a0 indexed
     5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  5,  8,  8,  6,  6,  // b0 extended
     2,  2,  2,  5,  2,  2,  2,  0,  2,  2,  2,  2,  3,  0,  3,  0,  // c0 immediate
     4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  6,  5,  // d0 direct
     2,  2,  2,  4,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  // e0 indexed
     5,  5,  5,  8,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  // f0 extended




};

// Addressing mode of every opcode (paginacion in m6809.pas).
//   0 implied, 1 direct page address, 2 immediate byte, 3 extended address,
//   4 indexed address, 5 direct page byte, 6 indexed byte, 7 extended byte,
//   8 direct page word, 9 indexed word, 10 extended word.
static const uint8_t kMode[256] = {
    1, 1, 1, 1, 1, 15, 1, 1, 1, 1, 1, 15, 1, 1, 1, 1,          // 00
    0, 0, 0, 0, 15, 15, 3, 3, 15, 0, 2, 15, 2, 0, 2, 2,         // 10
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,             // 20
    4, 4, 4, 4, 2, 2, 2, 2, 15, 0, 0, 0, 2, 0, 15, 15,          // 30
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,        // 40
    0, 15, 15, 0, 0, 15, 0, 0, 0, 0, 0, 15, 0, 0, 15, 0,        // 50
    4, 15, 15, 4, 4, 15, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4,         // 60
    3, 15, 15, 3, 3, 15, 3, 3, 3, 3, 3, 15, 3, 3, 3, 3,         // 70
    2, 2, 2, 3, 2, 2, 2, 15, 2, 2, 2, 2, 3, 2, 3, 15,           // 80
    5, 5, 5, 8, 5, 5, 5, 1, 5, 5, 5, 5, 8, 1, 8, 1,             // 90
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,             // a0
    7, 7, 7, 10, 7, 7, 7, 3, 7, 7, 7, 7, 10, 3, 10, 3,          // b0
    2, 2, 2, 3, 2, 2, 2, 15, 2, 2, 2, 2, 3, 15, 3, 15,          // c0
    5, 5, 5, 8, 5, 5, 5, 1, 5, 5, 5, 5, 8, 1, 8, 1,             // d0
    6, 6, 6, 9, 6, 6, 6, 4, 6, 6, 6, 6, 9, 4, 9, 4,             // e0
    7, 7, 7, 10, 7, 7, 7, 3, 7, 7, 7, 7, 10, 3, 10, 3,          // f0
};

// Extra cycles and addressing mode of the $10 / $11 prefixed opcodes.
static const uint8_t kCycles1X[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10
    0, 0, 5, 5, 5, 5, 5, 5, 0, 0, 5, 5, 5, 5, 5, 5,  // 20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 30
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 50
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 70
    0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 4, 0,  // 80
    0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 6, 6,  // 90
    0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 4, 4,  // a0
    0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 7, 7,  // b0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0,  // c0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6,  // d0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4,  // e0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7,  // f0
};

static const uint8_t kMode1X[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10
    0, 0, 3, 3, 3, 3, 3, 3, 0, 0, 3, 3, 3, 3, 3, 3,  // 20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 30
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 50
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 70
    0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0,  // 80
    0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 5, 1,  // 90
    0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 6, 4,  // a0
    0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 7, 3,  // b0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,  // c0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 1,  // d0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 4,  // e0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 3,  // f0
};
static uint16_t read_word(m6809d *P, uint16_t address) {
    uint16_t high = read(P, address);
    return (uint16_t)((high << 8) | read(P, (uint16_t)(address + 1)));
}
static void write_word(m6809d *P, uint16_t address, uint16_t value) {
    write(P, address, (uint8_t)(value >> 8));
    write(P, (uint16_t)(address + 1), (uint8_t)(value));
}
static uint8_t fetch(m6809d *P) { return read(P, P->pc_++); }
static uint8_t fetch_opcode(m6809d *P) {
    uint8_t value = read(P, P->pc_);
    P->pc_ = (uint16_t)(P->pc_ + 1);
    return value;
}
static uint16_t fetch_word(m6809d *P) {
    uint16_t value = read_word(P, P->pc_);
    P->pc_ = (uint16_t)(P->pc_ + 2);
    return value;
}
static void push_s(m6809d *P, uint8_t value) { write(P, --P->s, value); }
static uint8_t pop_s(m6809d *P) { return read(P, P->s++); }
static void push_sw(m6809d *P, uint16_t value) {
    P->s = (uint16_t)(P->s - 2);
    write(P, (uint16_t)(P->s + 1), (uint8_t)(value));
    write(P, P->s, (uint8_t)(value >> 8));
}
static uint16_t pop_sw(m6809d *P) {
    uint16_t value = read_word(P, P->s);
    P->s = (uint16_t)(P->s + 2);
    return value;
}
static void push_u(m6809d *P, uint8_t value) { write(P, --P->u, value); }
static uint8_t pop_u(m6809d *P) { return read(P, P->u++); }
static void push_uw(m6809d *P, uint16_t value) {
    P->u = (uint16_t)(P->u - 2);
    write(P, (uint16_t)(P->u + 1), (uint8_t)(value));
    write(P, P->u, (uint8_t)(value >> 8));
}
static uint16_t pop_uw(m6809d *P) {
    uint16_t value = read_word(P, P->u);
    P->u = (uint16_t)(P->u + 2);
    return value;
}
static uint8_t get_cc(m6809d *P) {
    return (uint8_t)((P->cc.e ? 0x80 : 0) | (P->cc.f ? 0x40 : 0) | (P->cc.h ? 0x20 : 0) | (P->cc.i ? 0x10 : 0) |
                   (P->cc.n ? 0x08 : 0) | (P->cc.z ? 0x04 : 0) | (P->cc.v ? 0x02 : 0) | (P->cc.c ? 0x01 : 0));
}
static void set_cc(m6809d *P, uint8_t value) {
    P->cc.e = (value & 0x80) != 0;
    P->cc.f = (value & 0x40) != 0;
    P->cc.h = (value & 0x20) != 0;
    P->cc.i = (value & 0x10) != 0;
    P->cc.n = (value & 0x08) != 0;
    P->cc.z = (value & 0x04) != 0;
    P->cc.v = (value & 0x02) != 0;
    P->cc.c = (value & 0x01) != 0;
}
void m6809d_reset(m6809d *P) {
    P->pc_ = read_word(P, 0xfffe);
    P->dp = 0;
    set_cc(P, 0x50);
    P->nmi_request_ = M6809D_CLEAR;
    P->nmi_state_ = M6809D_CLEAR;
    P->irq_state_ = M6809D_CLEAR;
    P->firq_state_ = M6809D_CLEAR;
    P->cwai_ = false;
    P->stack_init_ = false;
}
void m6809d_set_nmi(m6809d *P, int state) {
    P->nmi_request_ = state;
    if (state == M6809D_CLEAR) P->nmi_state_ = M6809D_CLEAR;
}
static int call_nmi(m6809d *P) {
    if (P->nmi_state_ != M6809D_CLEAR) return 0;
    if (!P->stack_init_) return 0;
    int cycles = 0;
    if (P->cwai_) {
        P->cwai_ = false;
        cycles = 6;
    } else {
        push_sw(P, P->pc_);
        push_sw(P, P->u);
        push_sw(P, P->y);
        push_sw(P, P->x);
        push_s(P, P->dp);
        push_s(P, P->b);
        push_s(P, P->a);
        P->cc.e = true;
        push_s(P, get_cc(P));
        cycles = 19;
    }
    P->cc.i = true;
    P->cc.f = true;
    P->pc_ = read_word(P, 0xfffc);
    if (P->nmi_request_ == M6809D_PULSE) P->nmi_request_ = M6809D_CLEAR;
    if (P->nmi_request_ == M6809D_ASSERT) P->nmi_state_ = M6809D_ASSERT;
    return cycles;
}
static int call_irq(m6809d *P) {
    int cycles = 0;
    if (P->cwai_) {
        P->cwai_ = false;
        cycles = 6;
    } else {
        push_sw(P, P->pc_);
        push_sw(P, P->u);
        push_sw(P, P->y);
        push_sw(P, P->x);
        push_s(P, P->dp);
        push_s(P, P->b);
        push_s(P, P->a);
        P->cc.e = true;
        push_s(P, get_cc(P));
        cycles = 19;
    }
    P->pc_ = read_word(P, 0xfff8);
    P->cc.i = true;
    if (P->irq_state_ == M6809D_HOLD) P->irq_state_ = M6809D_CLEAR;
    return cycles;
}
static int call_firq(m6809d *P) {
    int cycles = 0;
    if (P->cwai_) {
        P->cwai_ = false;
        cycles = 6;
    } else {
        P->cc.e = false;
        push_sw(P, P->pc_);
        push_s(P, get_cc(P));
        cycles = 10;
    }
    P->cc.f = true;
    P->cc.i = true;
    P->pc_ = read_word(P, 0xfff6);
    if (P->firq_state_ == M6809D_HOLD) P->firq_state_ = M6809D_CLEAR;
    return cycles;
}
static uint16_t *index_register(m6809d *P, uint8_t postbyte) {
    switch (postbyte & 0x60) {
        case 0x00: return &P->x;
        case 0x20: return &P->y;
        case 0x40: return &P->u;
        default: return &P->s;
    }
}
static uint16_t get_indexed(m6809d *P) {
    // The dummy_arg / dummy_vma calls reproduce the ignored bus cycles of
    // each mode (as MAME's 6809 does) for drivers that watch the bus.
    uint8_t postbyte = fetch(P);
    uint16_t* reg = index_register(P, postbyte);
    uint16_t address = 0;

    if ((postbyte & 0x80) != 0) {
        switch (postbyte & 0x0f) {
            case 0x0:  // ,R+
                address = *reg;
                *reg = (uint16_t)(*reg + 1);
                P->extra_cycles_ += 4;
                dummy_arg(P, 0);
                dummy_vma(P, 2);
                break;
            case 0x1:  // ,R++
                address = *reg;
                *reg = (uint16_t)(*reg + 2);
                P->extra_cycles_ += 5;
                dummy_arg(P, 0);
                dummy_vma(P, 3);
                break;
            case 0x2:  // ,-R
                *reg = (uint16_t)(*reg - 1);
                address = *reg;
                P->extra_cycles_ += 4;
                dummy_arg(P, 0);
                dummy_vma(P, 2);
                break;
            case 0x3:  // ,--R
                *reg = (uint16_t)(*reg - 2);
                address = *reg;
                P->extra_cycles_ += 5;
                dummy_arg(P, 0);
                dummy_vma(P, 3);
                break;
            case 0x4:  // ,R
                address = *reg;
                P->extra_cycles_ += 2;
                dummy_arg(P, 0);
                break;
            case 0x5:  // B,R
                address = (uint16_t)(*reg + (int8_t)(P->b));
                P->extra_cycles_ += 3;
                dummy_arg(P, 0);
                dummy_vma(P, 1);
                break;
            case 0x6:  // A,R
                address = (uint16_t)(*reg + (int8_t)(P->a));
                P->extra_cycles_ += 3;
                dummy_arg(P, 0);
                dummy_vma(P, 1);
                break;
            case 0x8:  // n8,R
                address = (uint16_t)(*reg + (int8_t)(fetch(P)));
                P->extra_cycles_ += 3;
                dummy_arg(P, 0);
                break;
            case 0x9:  // n16,R
                address = (uint16_t)(*reg + (int16_t)(fetch_word(P)));
                P->extra_cycles_ += 6;
                dummy_vma(P, 3);
                break;
            case 0xb:  // D,R
                address = (uint16_t)(*reg + (int16_t)(d(P)));
                P->extra_cycles_ += 6;
                dummy_arg(P, 0);
                dummy_arg(P, 1);
                dummy_vma(P, 3);
                break;
            case 0xc: {  // n8,PCR
                int8_t offset = (int8_t)(fetch(P));
                address = (uint16_t)(P->pc_ + offset);
                P->extra_cycles_ += 2;
                dummy_vma(P, 1);
                break;
            }
            case 0xd: {  // n16,PCR
                uint16_t offset = fetch_word(P);
                address = (uint16_t)(P->pc_ + offset);
                P->extra_cycles_ += 7;
                dummy_vma(P, 4);
                break;
            }
            case 0xf:  // [n16]
                address = fetch_word(P);
                P->extra_cycles_ += 4;
                dummy_vma(P, 1);
                break;
            default:  // undefined on the 6809
                address = *reg;
                P->extra_cycles_ += 2;
                break;
        }
        if ((postbyte & 0x10) != 0) {  // indirect
            address = read_word(P, address);
            P->extra_cycles_ += 2;
            dummy_vma(P, 1);
        }
    } else {  // 5 bit offset
        uint8_t offset = (uint8_t)(postbyte & 0x0f);
        address = ((postbyte & 0x10) == 0) ? (uint16_t)(*reg + offset)
                                           : (uint16_t)(*reg - (16 - offset));
        P->extra_cycles_ += 3;
        dummy_arg(P, 0);
        dummy_vma(P, 1);
    }
    return address;
}
static uint16_t transfer_source(m6809d *P, uint8_t code) {
    switch (code) {
        case 0x0: return d(P);
        case 0x1: return P->x;
        case 0x2: return P->y;
        case 0x3: return P->u;
        case 0x4: return P->s;
        case 0x5: return P->pc_;
        case 0x8: return P->a;
        case 0x9: return P->b;
        case 0xa: return get_cc(P);
        case 0xb: return P->dp;
        default: return 0xffff;
    }
}
static void transfer_target(m6809d *P, uint8_t code, uint16_t value) {
    switch (code) {
        case 0x0: set_d(P, value); break;
        case 0x1: P->x = value; break;
        case 0x2: P->y = value; break;
        case 0x3: P->u = value; break;
        case 0x4: P->s = value; break;
        case 0x5: P->pc_ = value; break;
        case 0x8: P->a = (uint8_t)(value); break;
        case 0x9: P->b = (uint8_t)(value); break;
        case 0xa: set_cc(P, (uint8_t)(value)); break;
        case 0xb: P->dp = (uint8_t)(value); break;
        default: break;
    }
}
static void tfr(m6809d *P, uint8_t value) {
    transfer_target(P, (uint8_t)(value & 0x0f), transfer_source(P, (uint8_t)(value >> 4)));
}
static void exg(m6809d *P, uint8_t value) {
    uint16_t source = transfer_source(P, (uint8_t)(value >> 4));
    uint16_t target = transfer_source(P, (uint8_t)(value & 0x0f));
    transfer_target(P, (uint8_t)(value >> 4), target);
    transfer_target(P, (uint8_t)(value & 0x0f), source);
}
static uint8_t op_neg(m6809d *P, uint8_t value) {
    uint16_t result = (uint16_t)(0u - value);
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    P->cc.c = (result & 0x100) != 0;
    P->cc.v = ((0 ^ value ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint8_t op_com(m6809d *P, uint8_t value) {
    uint8_t result = (uint8_t)(~value);
    P->cc.v = false;
    P->cc.c = true;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = result == 0;
    return result;
}
static uint8_t op_lsr(m6809d *P, uint8_t value) {
    uint8_t result = (uint8_t)(value >> 1);
    P->cc.z = result == 0;
    P->cc.n = false;
    P->cc.c = (value & 1) != 0;
    return result;
}
static uint8_t op_ror(m6809d *P, uint8_t value) {
    uint8_t result = (uint8_t)((value >> 1) | (P->cc.c ? 0x80 : 0));
    P->cc.c = (value & 1) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = result == 0;
    return result;
}
static uint8_t op_asr(m6809d *P, uint8_t value) {
    uint8_t result = (uint8_t)((value & 0x80) | (value >> 1));
    P->cc.c = (value & 1) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = result == 0;
    return result;
}
static uint8_t op_asl(m6809d *P, uint8_t value) {
    uint16_t result = (uint16_t)(value << 1);
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    P->cc.c = (result & 0x100) != 0;
    P->cc.v = ((value ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint8_t op_rol(m6809d *P, uint8_t value) {
    uint16_t result = (uint16_t)((value << 1) | (P->cc.c ? 1 : 0));
    P->cc.c = (value & 0x80) != 0;
    P->cc.v = ((value ^ result ^ (result >> 1)) & 0x80) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    return (uint8_t)(result);
}
static uint8_t op_dec(m6809d *P, uint8_t value) {
    uint16_t result = (uint16_t)(value - 1);
    P->cc.z = (result & 0xff) == 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.v = ((value ^ 1 ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint8_t op_inc(m6809d *P, uint8_t value) {
    uint16_t result = (uint16_t)(value + 1);
    P->cc.z = (result & 0xff) == 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.v = ((value ^ 1 ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static void op_tst(m6809d *P, uint8_t value) {
    P->cc.v = ((value >> 1) & 0x80) != 0;
    P->cc.n = (value & 0x80) != 0;
    P->cc.z = value == 0;
}
static uint8_t op_sub8(m6809d *P, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left - right);
    P->cc.c = (result & 0x100) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    P->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint16_t op_sub16(m6809d *P, uint16_t left, uint16_t right) {
    uint32_t result = (uint32_t)(left) - (uint32_t)(right);
    P->cc.c = (result & 0x10000) != 0;
    P->cc.n = (result & 0x8000) != 0;
    P->cc.z = (result & 0xffff) == 0;
    P->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x8000) != 0;
    return (uint16_t)(result);
}
static uint8_t op_sbc(m6809d *P, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left - right - (P->cc.c ? 1 : 0));
    P->cc.c = (result & 0x100) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    P->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint8_t op_and(m6809d *P, uint8_t left, uint8_t right) {
    uint8_t result = (uint8_t)(left & right);
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = result == 0;
    P->cc.v = false;
    return result;
}
static uint8_t op_eor(m6809d *P, uint8_t left, uint8_t right) {
    uint8_t result = (uint8_t)(left ^ right);
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = result == 0;
    P->cc.v = false;
    return result;
}
static uint8_t op_adc(m6809d *P, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left + right + (P->cc.c ? 1 : 0));
    P->cc.c = (result & 0x100) != 0;
    P->cc.h = ((left ^ right ^ result) & 0x10) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    P->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint8_t op_or(m6809d *P, uint8_t left, uint8_t right) {
    uint8_t result = (uint8_t)(left | right);
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = result == 0;
    P->cc.v = false;
    return result;
}
static uint8_t op_add8(m6809d *P, uint8_t left, uint8_t right) {
    uint16_t result = (uint16_t)(left + right);
    P->cc.c = (result & 0x100) != 0;
    P->cc.h = ((left ^ right ^ result) & 0x10) != 0;
    P->cc.n = (result & 0x80) != 0;
    P->cc.z = (result & 0xff) == 0;
    P->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x80) != 0;
    return (uint8_t)(result);
}
static uint16_t op_add16(m6809d *P, uint16_t left, uint16_t right) {
    uint32_t result = (uint32_t)(left) + (uint32_t)(right);
    P->cc.c = (result & 0x10000) != 0;
    P->cc.n = (result & 0x8000) != 0;
    P->cc.z = (result & 0xffff) == 0;
    P->cc.v = ((left ^ right ^ result ^ (result >> 1)) & 0x8000) != 0;
    return (uint16_t)(result);
}
static uint8_t op_ld_st8(m6809d *P, uint8_t value) {
    P->cc.n = (value & 0x80) != 0;
    P->cc.z = value == 0;
    P->cc.v = ((value >> 1) & 0x80) != 0;
    return value;
}
static uint16_t op_ld_st16(m6809d *P, uint16_t value) {
    P->cc.n = (value & 0x8000) != 0;
    P->cc.z = value == 0;
    P->cc.v = ((value >> 1) & 0x8000) != 0;
    return value;
}
static void branch(m6809d *P, bool condition, uint8_t offset) {
    if (condition) P->pc_ = (uint16_t)(P->pc_ + (int8_t)(offset));
}
static void long_branch(m6809d *P, bool condition, uint16_t offset) {
    if (condition) {
        P->pc_ = (uint16_t)(P->pc_ + (int16_t)(offset));
        P->extra_cycles_ += 1;
    }
}
static void page_10(m6809d *P, uint8_t opcode) {
    switch (opcode) {
        case 0x22: long_branch(P, !(P->cc.c || P->cc.z), P->address_); break;            // lbhi
        case 0x23: long_branch(P, P->cc.c || P->cc.z, P->address_); break;               // lbls
        case 0x24: long_branch(P, !P->cc.c, P->address_); break;                      // lbcc
        case 0x25: long_branch(P, P->cc.c, P->address_); break;                       // lbcs
        case 0x26: long_branch(P, !P->cc.z, P->address_); break;                      // lbne
        case 0x27: long_branch(P, P->cc.z, P->address_); break;                       // lbeq
        case 0x28: long_branch(P, !P->cc.v, P->address_); break;                      // lbvc
        case 0x29: long_branch(P, P->cc.v, P->address_); break;                       // lbvs
        case 0x2a: long_branch(P, !P->cc.n, P->address_); break;                      // lbpl
        case 0x2b: long_branch(P, P->cc.n, P->address_); break;                       // lbmi
        case 0x2c: long_branch(P, P->cc.n == P->cc.v, P->address_); break;               // lbge
        case 0x2d: long_branch(P, P->cc.n != P->cc.v, P->address_); break;               // lblt
        case 0x2e: long_branch(P, P->cc.n == P->cc.v && !P->cc.z, P->address_); break;      // lbgt
        case 0x2f: long_branch(P, !(P->cc.n == P->cc.v && !P->cc.z), P->address_); break;   // lble
        case 0x83:
        case 0x93:
        case 0xa3:
        case 0xb3: op_sub16(P, d(P), P->address_); break;  // cmpd
        case 0x8c:
        case 0x9c:
        case 0xac:
        case 0xbc: op_sub16(P, P->y, P->address_); break;  // cmpy
        case 0x8e:
        case 0x9e:
        case 0xae:
        case 0xbe: P->y = op_ld_st16(P, P->address_); break;  // ldy
        case 0x9f:
        case 0xaf:
        case 0xbf: write_word(P, P->address_, op_ld_st16(P, P->y)); break;  // sty
        case 0xce:
        case 0xde:
        case 0xee:
        case 0xfe:  // lds
            P->s = op_ld_st16(P, P->address_);
            P->stack_init_ = true;
            break;
        case 0xdf:
        case 0xef:
        case 0xff: write_word(P, P->address_, op_ld_st16(P, P->s)); break;  // sts
        default: break;
    }
}
static void page_11(m6809d *P, uint8_t opcode) {
    switch (opcode) {
        case 0x83:
        case 0x93:
        case 0xa3:
        case 0xb3: op_sub16(P, P->u, P->address_); break;  // cmpu
        case 0x8c:
        case 0x9c:
        case 0xac:
        case 0xbc: op_sub16(P, P->s, P->address_); break;  // cmps
        default: break;
    }
}
int m6809d_run(m6809d *P, int cycles) {
    int executed = 0;
    while (executed < cycles) {
        P->extra_cycles_ = 0;

        if (P->nmi_request_ != M6809D_CLEAR) {
            P->extra_cycles_ = call_nmi(P);
        } else if (P->firq_state_ != M6809D_CLEAR && !P->cc.f) {
            P->extra_cycles_ = call_firq(P);
        } else if (P->irq_state_ != M6809D_CLEAR && !P->cc.i) {
            P->extra_cycles_ = call_irq(P);
        }

        if (P->cwai_) {
            // Waiting for an interrupt: burn the rest of the slice.
            int idle = cycles - executed;
            if (P->cycles) P->cycles(P->user, idle);
            return cycles;
        }

        uint8_t opcode = fetch_opcode(P);
        switch (kMode[opcode]) {
            case 0: break;  // implied
            case 1: P->address_ = (uint16_t)((P->dp << 8) | fetch(P)); break;
            case 2: P->operand_ = fetch(P); break;
            case 3: P->address_ = fetch_word(P); break;
            case 4: P->address_ = get_indexed(P); break;
            case 5:
                P->address_ = (uint16_t)((P->dp << 8) | fetch(P));
                P->operand_ = read(P, P->address_);
                break;
            case 6:
                P->address_ = get_indexed(P);
                P->operand_ = read(P, P->address_);
                break;
            case 7:
                P->address_ = fetch_word(P);
                P->operand_ = read(P, P->address_);
                break;
            case 8:
                P->address_ = (uint16_t)((P->dp << 8) | fetch(P));
                P->address_ = read_word(P, P->address_);
                break;
            case 9: P->address_ = read_word(P, get_indexed(P)); break;
            case 10:
                P->address_ = fetch_word(P);
                P->address_ = read_word(P, P->address_);
                break;
            default: break;  // illegal opcode
        }

        switch (opcode) {
            case 0x00:
            case 0x01:
            case 0x60:
            case 0x70: write(P, P->address_, op_neg(P, read(P, P->address_))); break;
            case 0x02:
            case 0x03:
            case 0x63:
            case 0x73: write(P, P->address_, op_com(P, read(P, P->address_))); break;
            case 0x04:
            case 0x64:
            case 0x74: write(P, P->address_, op_lsr(P, read(P, P->address_))); break;
            case 0x06:
            case 0x66:
            case 0x76: write(P, P->address_, op_ror(P, read(P, P->address_))); break;
            case 0x07:
            case 0x67:
            case 0x77: write(P, P->address_, op_asr(P, read(P, P->address_))); break;
            case 0x08:
            case 0x68:
            case 0x78: write(P, P->address_, op_asl(P, read(P, P->address_))); break;
            case 0x09:
            case 0x69:
            case 0x79: write(P, P->address_, op_rol(P, read(P, P->address_))); break;
            case 0x0a:
            case 0x6a:
            case 0x7a: write(P, P->address_, op_dec(P, read(P, P->address_))); break;
            case 0x0c:
            case 0x6c:
            case 0x7c: write(P, P->address_, op_inc(P, read(P, P->address_))); break;
            case 0x0d:
            case 0x6d:
            case 0x7d: op_tst(P, read(P, P->address_)); break;
            case 0x0e:
            case 0x6e:
            case 0x7e: P->pc_ = P->address_; break;  // jmp
            case 0x0f:
            case 0x6f:
            case 0x7f:  // clr
                write(P, P->address_, 0);
                P->cc.n = false;
                P->cc.v = false;
                P->cc.c = false;
                P->cc.z = true;
                break;
            case 0x10:
            case 0x11: {
                uint8_t opcode2 = fetch_opcode(P);
                switch (kMode1X[opcode2]) {
                    case 1: P->address_ = (uint16_t)((P->dp << 8) | fetch(P)); break;
                    case 3: P->address_ = fetch_word(P); break;
                    case 4: P->address_ = get_indexed(P); break;
                    case 5:
                        P->address_ = (uint16_t)((P->dp << 8) | fetch(P));
                        P->address_ = read_word(P, P->address_);
                        break;
                    case 6: P->address_ = read_word(P, get_indexed(P)); break;
                    case 7:
                        P->address_ = fetch_word(P);
                        P->address_ = read_word(P, P->address_);
                        break;
                    default: break;
                }
                if (opcode == 0x10) {
                    page_10(P, opcode2);
                } else {
                    page_11(P, opcode2);
                }
                P->extra_cycles_ += kCycles1X[opcode2];
                break;
            }
            case 0x12: break;  // nop
            case 0x13:         // sync
                if (P->irq_state_ == M6809D_CLEAR && P->firq_state_ == M6809D_CLEAR &&
                    P->nmi_request_ == M6809D_CLEAR) {
                    P->pc_ = (uint16_t)(P->pc_ - 1);
                }
                break;
            case 0x16: long_branch(P, true, P->address_); break;  // lbra
            case 0x17:                                      // lbsr
                push_sw(P, P->pc_);
                P->pc_ = (uint16_t)(P->pc_ + (int16_t)(P->address_));
                break;
            case 0x19: {  // daa
                uint8_t correction = 0;
                uint8_t high = (uint8_t)(P->a & 0xf0);
                uint8_t low = (uint8_t)(P->a & 0x0f);
                if (low > 0x09 || P->cc.h) correction = (uint8_t)(correction | 0x06);
                if (high > 0x80 && low > 0x09) correction = (uint8_t)(correction | 0x60);
                if (high > 0x90 || P->cc.c) correction = (uint8_t)(correction | 0x60);
                uint16_t result = (uint16_t)(correction + P->a);
                P->cc.v = false;
                P->cc.n = (result & 0x80) != 0;
                P->cc.z = (result & 0xff) == 0;
                P->cc.c = P->cc.c || (result & 0x100) != 0;
                P->a = (uint8_t)(result);
                break;
            }
            case 0x1a: set_cc(P, (uint8_t)(get_cc(P) | P->operand_)); break;   // orcc
            case 0x1c: set_cc(P, (uint8_t)(get_cc(P) & P->operand_)); break;   // andcc
            case 0x1d:                                                // sex
                P->a = (uint8_t)(0xff * (P->b >> 7));
                P->cc.n = (d(P) & 0x8000) != 0;
                P->cc.z = d(P) == 0;
                break;
            case 0x1e: exg(P, P->operand_); break;
            case 0x1f: tfr(P, P->operand_); break;
            case 0x20: branch(P, true, P->operand_); break;                              // bra
            case 0x21: break;                                                      // brn
            case 0x22: branch(P, !(P->cc.c || P->cc.z), P->operand_); break;                   // bhi
            case 0x23: branch(P, P->cc.c || P->cc.z, P->operand_); break;                      // bls
            case 0x24: branch(P, !P->cc.c, P->operand_); break;                             // bcc
            case 0x25: branch(P, P->cc.c, P->operand_); break;                              // bcs
            case 0x26: branch(P, !P->cc.z, P->operand_); break;                             // bne
            case 0x27: branch(P, P->cc.z, P->operand_); break;                              // beq
            case 0x28: branch(P, !P->cc.v, P->operand_); break;                             // bvc
            case 0x29: branch(P, P->cc.v, P->operand_); break;                              // bvs
            case 0x2a: branch(P, !P->cc.n, P->operand_); break;                             // bpl
            case 0x2b: branch(P, P->cc.n, P->operand_); break;                              // bmi
            case 0x2c: branch(P, P->cc.n == P->cc.v, P->operand_); break;                      // bge
            case 0x2d: branch(P, P->cc.n != P->cc.v, P->operand_); break;                      // blt
            case 0x2e: branch(P, P->cc.n == P->cc.v && !P->cc.z, P->operand_); break;             // bgt
            case 0x2f: branch(P, !(P->cc.n == P->cc.v && !P->cc.z), P->operand_); break;          // ble
            case 0x30:                                                             // leax
                P->x = P->address_;
                P->cc.z = P->x == 0;
                break;
            case 0x31:  // leay
                P->y = P->address_;
                P->cc.z = P->y == 0;
                break;
            case 0x32: P->s = P->address_; break;  // leas
            case 0x33: P->u = P->address_; break;  // leau
            case 0x34:                       // pshs
                if ((P->operand_ & 0x80) != 0) {
                    push_sw(P, P->pc_);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x40) != 0) {
                    push_sw(P, P->u);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x20) != 0) {
                    push_sw(P, P->y);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x10) != 0) {
                    push_sw(P, P->x);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x08) != 0) {
                    push_s(P, P->dp);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x04) != 0) {
                    push_s(P, P->b);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x02) != 0) {
                    push_s(P, P->a);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x01) != 0) {
                    push_s(P, get_cc(P));
                    P->extra_cycles_ += 1;
                }
                break;
            case 0x35:  // puls
                if ((P->operand_ & 0x01) != 0) {
                    set_cc(P, pop_s(P));
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x02) != 0) {
                    P->a = pop_s(P);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x04) != 0) {
                    P->b = pop_s(P);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x08) != 0) {
                    P->dp = pop_s(P);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x10) != 0) {
                    P->x = pop_sw(P);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x20) != 0) {
                    P->y = pop_sw(P);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x40) != 0) {
                    P->u = pop_sw(P);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x80) != 0) {
                    P->pc_ = pop_sw(P);
                    P->extra_cycles_ += 2;
                }
                break;
            case 0x36:  // pshu
                if ((P->operand_ & 0x80) != 0) {
                    push_uw(P, P->pc_);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x40) != 0) {
                    push_uw(P, P->s);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x20) != 0) {
                    push_uw(P, P->y);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x10) != 0) {
                    push_uw(P, P->x);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x08) != 0) {
                    push_u(P, P->dp);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x04) != 0) {
                    push_u(P, P->b);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x02) != 0) {
                    push_u(P, P->a);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x01) != 0) {
                    push_u(P, get_cc(P));
                    P->extra_cycles_ += 1;
                }
                break;
            case 0x37:  // pulu
                if ((P->operand_ & 0x01) != 0) {
                    set_cc(P, pop_u(P));
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x02) != 0) {
                    P->a = pop_u(P);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x04) != 0) {
                    P->b = pop_u(P);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x08) != 0) {
                    P->dp = pop_u(P);
                    P->extra_cycles_ += 1;
                }
                if ((P->operand_ & 0x10) != 0) {
                    P->x = pop_uw(P);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x20) != 0) {
                    P->y = pop_uw(P);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x40) != 0) {
                    P->s = pop_uw(P);
                    P->extra_cycles_ += 2;
                }
                if ((P->operand_ & 0x80) != 0) {
                    P->pc_ = pop_uw(P);
                    P->extra_cycles_ += 2;
                }
                break;
            case 0x39: P->pc_ = pop_sw(P); break;         // rts
            case 0x3a: P->x = (uint16_t)(P->x + P->b); break;    // abx
            case 0x3b:                                // rti
                set_cc(P, pop_s(P));
                if (P->cc.e) {
                    P->extra_cycles_ += 9;
                    P->a = pop_s(P);
                    P->b = pop_s(P);
                    P->dp = pop_s(P);
                    P->x = pop_sw(P);
                    P->y = pop_sw(P);
                    P->u = pop_sw(P);
                }
                P->pc_ = pop_sw(P);
                break;
            case 0x3c:  // cwai
                set_cc(P, (uint8_t)(get_cc(P) & P->operand_));
                P->cc.e = true;
                push_sw(P, P->pc_);
                push_sw(P, P->u);
                push_sw(P, P->y);
                push_sw(P, P->x);
                push_s(P, P->dp);
                push_s(P, P->b);
                push_s(P, P->a);
                push_s(P, get_cc(P));
                P->cwai_ = true;
                break;
            case 0x3d: {  // mul
                uint16_t result = (uint16_t)((uint16_t)(P->a) * (uint16_t)(P->b));
                set_d(P, result);
                P->cc.c = (result & 0x80) != 0;
                P->cc.z = result == 0;
                break;
            }
            case 0x3f:  // swi
                P->cc.e = true;
                push_sw(P, P->pc_);
                push_sw(P, P->u);
                push_sw(P, P->y);
                push_sw(P, P->x);
                push_s(P, P->dp);
                push_s(P, P->b);
                push_s(P, P->a);
                push_s(P, get_cc(P));
                P->cc.i = true;
                P->cc.f = true;
                P->pc_ = read_word(P, 0xfffa);
                break;
            case 0x40: P->a = op_neg(P, P->a); break;
            case 0x43: P->a = op_com(P, P->a); break;
            case 0x44: P->a = op_lsr(P, P->a); break;
            case 0x46: P->a = op_ror(P, P->a); break;
            case 0x47: P->a = op_asr(P, P->a); break;
            case 0x48: P->a = op_asl(P, P->a); break;
            case 0x49: P->a = op_rol(P, P->a); break;
            case 0x4a: P->a = op_dec(P, P->a); break;
            case 0x4c: P->a = op_inc(P, P->a); break;
            case 0x4d: op_tst(P, P->a); break;
            case 0x4f:  // clra
                P->a = 0;
                P->cc.z = true;
                P->cc.n = false;
                P->cc.v = false;
                P->cc.c = false;
                break;
            case 0x50: P->b = op_neg(P, P->b); break;
            case 0x53: P->b = op_com(P, P->b); break;
            case 0x54: P->b = op_lsr(P, P->b); break;
            case 0x56: P->b = op_ror(P, P->b); break;
            case 0x57: P->b = op_asr(P, P->b); break;
            case 0x58: P->b = op_asl(P, P->b); break;
            case 0x59: P->b = op_rol(P, P->b); break;
            case 0x5a: P->b = op_dec(P, P->b); break;
            case 0x5c: P->b = op_inc(P, P->b); break;
            case 0x5d: op_tst(P, P->b); break;
            case 0x5f:  // clrb
                P->b = 0;
                P->cc.z = true;
                P->cc.n = false;
                P->cc.v = false;
                P->cc.c = false;
                break;
            case 0x80:
            case 0x90:
            case 0xa0:
            case 0xb0: P->a = op_sub8(P, P->a, P->operand_); break;  // suba
            case 0x81:
            case 0x91:
            case 0xa1:
            case 0xb1: op_sub8(P, P->a, P->operand_); break;  // cmpa
            case 0x82:
            case 0x92:
            case 0xa2:
            case 0xb2: P->a = op_sbc(P, P->a, P->operand_); break;  // sbca
            case 0x83:
            case 0x93:
            case 0xa3:
            case 0xb3: set_d(P, op_sub16(P, d(P), P->address_)); break;  // subd
            case 0x84:
            case 0x94:
            case 0xa4:
            case 0xb4: P->a = op_and(P, P->a, P->operand_); break;  // anda
            case 0x85:
            case 0x95:
            case 0xa5:
            case 0xb5: op_and(P, P->a, P->operand_); break;  // bita
            case 0x86:
            case 0x96:
            case 0xa6:
            case 0xb6: P->a = op_ld_st8(P, P->operand_); break;  // lda
            case 0x97:
            case 0xa7:
            case 0xb7: write(P, P->address_, op_ld_st8(P, P->a)); break;  // sta
            case 0x88:
            case 0x98:
            case 0xa8:
            case 0xb8: P->a = op_eor(P, P->a, P->operand_); break;  // eora
            case 0x89:
            case 0x99:
            case 0xa9:
            case 0xb9: P->a = op_adc(P, P->a, P->operand_); break;  // adca
            case 0x8a:
            case 0x9a:
            case 0xaa:
            case 0xba: P->a = op_or(P, P->a, P->operand_); break;  // ora
            case 0x8b:
            case 0x9b:
            case 0xab:
            case 0xbb: P->a = op_add8(P, P->a, P->operand_); break;  // adda
            case 0x8c:
            case 0x9c:
            case 0xac:
            case 0xbc: op_sub16(P, P->x, P->address_); break;  // cmpx
            case 0x8d:                                // bsr
                push_sw(P, P->pc_);
                P->pc_ = (uint16_t)(P->pc_ + (int8_t)(P->operand_));
                break;
            case 0x9d:
            case 0xad:
            case 0xbd:  // jsr
                push_sw(P, P->pc_);
                P->pc_ = P->address_;
                break;
            case 0x8e:
            case 0x9e:
            case 0xae:
            case 0xbe: P->x = op_ld_st16(P, P->address_); break;  // ldx
            case 0x9f:
            case 0xaf:
            case 0xbf: write_word(P, P->address_, op_ld_st16(P, P->x)); break;  // stx
            case 0xc0:
            case 0xd0:
            case 0xe0:
            case 0xf0: P->b = op_sub8(P, P->b, P->operand_); break;  // subb
            case 0xc1:
            case 0xd1:
            case 0xe1:
            case 0xf1: op_sub8(P, P->b, P->operand_); break;  // cmpb
            case 0xc2:
            case 0xd2:
            case 0xe2:
            case 0xf2: P->b = op_sbc(P, P->b, P->operand_); break;  // sbcb
            case 0xc3:
            case 0xd3:
            case 0xe3:
            case 0xf3: set_d(P, op_add16(P, d(P), P->address_)); break;  // addd
            case 0xc4:
            case 0xd4:
            case 0xe4:
            case 0xf4: P->b = op_and(P, P->b, P->operand_); break;  // andb
            case 0xc5:
            case 0xd5:
            case 0xe5:
            case 0xf5: op_and(P, P->b, P->operand_); break;  // bitb
            case 0xc6:
            case 0xd6:
            case 0xe6:
            case 0xf6: P->b = op_ld_st8(P, P->operand_); break;  // ldb
            case 0xd7:
            case 0xe7:
            case 0xf7: write(P, P->address_, op_ld_st8(P, P->b)); break;  // stb
            case 0xc8:
            case 0xd8:
            case 0xe8:
            case 0xf8: P->b = op_eor(P, P->b, P->operand_); break;  // eorb
            case 0xc9:
            case 0xd9:
            case 0xe9:
            case 0xf9: P->b = op_adc(P, P->b, P->operand_); break;  // adcb
            case 0xca:
            case 0xda:
            case 0xea:
            case 0xfa: P->b = op_or(P, P->b, P->operand_); break;  // orb
            case 0xcb:
            case 0xdb:
            case 0xeb:
            case 0xfb: P->b = op_add8(P, P->b, P->operand_); break;  // addb
            case 0xcc:
            case 0xdc:
            case 0xec:
            case 0xfc: set_d(P, op_ld_st16(P, P->address_)); break;  // ldd
            case 0xdd:
            case 0xed:
            case 0xfd: write_word(P, P->address_, op_ld_st16(P, d(P))); break;  // std
            case 0xce:
            case 0xde:
            case 0xee:
            case 0xfe: P->u = op_ld_st16(P, P->address_); break;  // ldu
            case 0xdf:
            case 0xef:
            case 0xff: write_word(P, P->address_, op_ld_st16(P, P->u)); break;  // stu
            default: break;
        }

        // Illegal opcodes have no entry in the cycle table: charge one cycle so
        // that the slice always makes progress.
        int elapsed = kCycles[opcode] + P->extra_cycles_;
        if (elapsed <= 0) elapsed = 1;
        executed += elapsed;
        if (P->cycles) P->cycles(P->user, elapsed);
    }
    return executed;
}

