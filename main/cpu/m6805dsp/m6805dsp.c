// m6805dsp.c - Motorola 6805 / 68705: C port of dsp-cpp's cpu/m6805 as it
// is (from m6805.pas), for the arcade drivers migrated from dsp-cpp that
// must match it (cpu/m6805 is the MAME-checked one other boards use).
#include "m6805dsp.h"

static const uint8_t kCycles[256] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  //
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,   //
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   //
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,   //
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   //
    4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,   //
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,   //
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,   //
    9,  6,  2,  11, 2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,   //
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,   //
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  8,  2,  2,   //
    4,  4,  4,  4,  4,  4,  4,  5,  4,  4,  4,  4,  3,  7,  4,  5,   //
    5,  5,  5,  5,  5,  5,  5,  6,  5,  5,  5,  5,  4,  8,  5,  6,   //
    6,  6,  6,  6,  6,  6,  6,  7,  6,  6,  6,  6,  5,  9,  6,  7,   //
    5,  5,  5,  5,  5,  5,  5,  6,  5,  5,  5,  5,  4,  8,  5,  6,   //
    4,  4,  4,  4,  4,  4,  4,  5,  4,  4,  4,  4,  3,  7,  4,  5,
};

// opcodes that only need the effective address (reading the operand would
// disturb the MCU's I/O registers)
static bool writes_only(uint8_t op)
{
    switch (op & 0x0f) {
    case 0x07: case 0x0c: case 0x0d: case 0x0f: return op >= 0xb0;
    default: return false;
    }
}

static inline uint8_t rd(m6805d *c, uint16_t a) { return c->read(c->user, a); }
static inline void wr(m6805d *c, uint16_t a, uint8_t v) { c->write(c->user, a, v); }
static inline uint16_t read_word(m6805d *c, uint16_t a) { return (uint16_t)((rd(c, a) << 8) | rd(c, (uint16_t)(a + 1))); }
static inline uint8_t fetch(m6805d *c) { return rd(c, c->pc++); }
static inline uint16_t fetch_word(m6805d *c) { const uint16_t v = read_word(c, c->pc); c->pc = (uint16_t)(c->pc + 2); return v; }
static inline uint8_t get_cc(const m6805d *c)
{
    return (uint8_t)((c->fh ? 0x10 : 0) | (c->fi ? 0x08 : 0) | (c->fn ? 0x04 : 0) | (c->fz ? 0x02 : 0) | (c->fc ? 0x01 : 0));
}
static inline void set_cc(m6805d *c, uint8_t v)
{
    c->fh = (v & 0x10) != 0; c->fi = (v & 0x08) != 0; c->fn = (v & 0x04) != 0; c->fz = (v & 0x02) != 0; c->fc = (v & 0x01) != 0;
}
static inline void push(m6805d *c, uint8_t v) { wr(c, c->sp, v); c->sp = c->sp == c->sp_low ? c->sp_mask : (uint16_t)(c->sp - 1); }
static inline void push_word(m6805d *c, uint16_t v) { push(c, (uint8_t)v); push(c, (uint8_t)(v >> 8)); }
static inline uint8_t pull(m6805d *c) { c->sp = c->sp >= c->sp_mask ? c->sp_low : (uint16_t)(c->sp + 1); return rd(c, c->sp); }
static inline uint16_t pull_word(m6805d *c) { const uint8_t h = pull(c), l = pull(c); return (uint16_t)((h << 8) | l); }
static inline void branch(m6805d *c, bool taken, uint8_t off) { if (taken) c->pc = (uint16_t)(c->pc + (int8_t)off); }
#define SET_NZ(r) do { const uint8_t r_ = (uint8_t)(r); c->fz = r_ == 0; c->fn = (r_ & 0x80) != 0; } while (0)
#define SET_NZC(r) do { const uint16_t r_ = (uint16_t)(r); c->fz = (r_ & 0xff) == 0; c->fn = (r_ & 0x80) != 0; c->fc = (r_ & 0x100) != 0; } while (0)

static int execute(m6805d *c)
{
    const uint8_t opcode = fetch(c);
    uint16_t address = 0;
    uint8_t value = 0;
    bool has_address = false;

    switch (opcode >> 4) {
        case 0x0:  // bit test and branch: direct operand plus c->a relative offset
        case 0x1:  // bit set/clear: direct
        case 0x3:  // read/modify/write: direct
        case 0xb:  // direct
            address = fetch(c);
            has_address = 1;
            break;
        case 0x2:  // relative
        case 0xa:  // immediate
            value = fetch(c);
            break;
        case 0x6:  // indexed, 8 bit offset
        case 0xe:
            address = (uint16_t)(fetch(c) + c->x);
            has_address = 1;
            break;
        case 0x7:  // indexed, no offset
        case 0xf:
            address = c->x;
            has_address = 1;
            break;
        case 0xc:  // extended
            address = fetch_word(c);
            has_address = 1;
            break;
        case 0xd:  // indexed, 16 bit offset
            address = (uint16_t)(fetch_word(c) + c->x);
            has_address = 1;
            break;
        default:
            break;  // inherent
    }
    if (has_address && !writes_only(opcode)) value = rd(c, address);


    switch (opcode) {
        // Bit test and branch.
        case 0x00: case 0x02: case 0x04: case 0x06:
        case 0x08: case 0x0a: case 0x0c: case 0x0e: {  // brset
            const uint8_t offset = fetch(c);
            c->fc = (value & (1 << ((opcode >> 1) & 7))) != 0;
            branch(c, c->fc, offset);
            break;
        }
        case 0x01: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0b: case 0x0d: case 0x0f: {  // brclr
            const uint8_t offset = fetch(c);
            c->fc = (value & (1 << ((opcode >> 1) & 7))) != 0;
            branch(c, !c->fc, offset);
            break;
        }
        case 0x10: case 0x12: case 0x14: case 0x16:
        case 0x18: case 0x1a: case 0x1c: case 0x1e:  // bset
            wr(c, address, (uint8_t)(value | (1 << ((opcode >> 1) & 7))));
            break;
        case 0x11: case 0x13: case 0x15: case 0x17:
        case 0x19: case 0x1b: case 0x1d: case 0x1f:  // bclr
            wr(c, address, (uint8_t)(value & ~(1 << ((opcode >> 1) & 7))));
            break;

        // Branches.
        case 0x20: branch(c, true, value); break;                    // bra
        case 0x21: break;                                          // brn
        case 0x22: branch(c, !c->fz && !c->fc, value); break;            // bhi
        case 0x23: branch(c, c->fz || c->fc, value); break;              // bls
        case 0x24: branch(c, !c->fc, value); break;                     // bcc
        case 0x25: branch(c, c->fc, value); break;                      // bcs
        case 0x26: branch(c, !c->fz, value); break;                     // bne
        case 0x27: branch(c, c->fz, value); break;                      // beq
        case 0x28: branch(c, !c->fh, value); break;                     // bhcc
        case 0x29: branch(c, c->fh, value); break;                      // bhcs
        case 0x2a: branch(c, !c->fn, value); break;                     // bpl
        case 0x2b: branch(c, c->fn, value); break;                      // bmi
        case 0x2c: branch(c, !c->fi, value); break;                     // bmc
        case 0x2d: branch(c, c->fi, value); break;                      // bms
        case 0x2e: branch(c, c->irq_state != M6805D_CLEAR, value); break;  // bil
        case 0x2f: branch(c, c->irq_state == M6805D_CLEAR, value); break;  // bih

        // Read/modify/write on memory.
        case 0x30: case 0x60: case 0x70: {  // neg
            const uint16_t result = (uint16_t)(0u - value);
            SET_NZC(result);
            wr(c, address, (uint8_t)(result));
            break;
        }
        case 0x33: case 0x63: case 0x73: {  // com
            const uint8_t result = (uint8_t)(~value);
            SET_NZ(result);
            c->fc = 1;
            wr(c, address, result);
            break;
        }
        case 0x34: case 0x64: case 0x74: {  // lsr
            c->fc = (value & 0x01) != 0;
            const uint8_t result = (uint8_t)(value >> 1);
            c->fn = 0;
            c->fz = result == 0;
            wr(c, address, result);
            break;
        }
        case 0x36: case 0x66: case 0x76: {  // ror
            const uint8_t result = (uint8_t)((c->fc ? 0x80 : 0) | (value >> 1));
            c->fc = (value & 0x01) != 0;
            SET_NZ(result);
            wr(c, address, result);
            break;
        }
        case 0x37: case 0x67: case 0x77: {  // asr
            c->fc = (value & 0x01) != 0;
            const uint8_t result = (uint8_t)((value & 0x80) | (value >> 1));
            SET_NZ(result);
            wr(c, address, result);
            break;
        }
        case 0x38: case 0x68: case 0x78: {  // asl / lsl
            const uint16_t result = (uint16_t)(value << 1);
            SET_NZC(result);
            wr(c, address, (uint8_t)(result));
            break;
        }
        case 0x39: case 0x69: case 0x79: {  // rol
            const uint16_t result = (uint16_t)((value << 1) | (c->fc ? 1 : 0));
            SET_NZC(result);
            wr(c, address, (uint8_t)(result));
            break;
        }
        case 0x3a: case 0x6a: case 0x7a: {  // dec
            const uint8_t result = (uint8_t)(value - 1);
            SET_NZ(result);
            wr(c, address, result);
            break;
        }
        case 0x3c: case 0x6c: case 0x7c: {  // inc
            const uint8_t result = (uint8_t)(value + 1);
            SET_NZ(result);
            wr(c, address, result);
            break;
        }
        case 0x3d: case 0x6d: case 0x7d:  // tst
            SET_NZ(value);
            break;
        case 0x3f: case 0x6f: case 0x7f:  // clr
            c->fn = 0;
            c->fc = 0;
            c->fz = 1;
            wr(c, address, 0);
            break;

        // Accumulator and index register.
        case 0x40: {  // nega
            const uint16_t result = (uint16_t)(0u - c->a);
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0x43:  // coma
            c->a = (uint8_t)(~c->a);
            SET_NZ(c->a);
            c->fc = 1;
            break;
        case 0x44:  // lsra
            c->fc = (c->a & 0x01) != 0;
            c->a = (uint8_t)(c->a >> 1);
            c->fn = 0;
            c->fz = c->a == 0;
            break;
        case 0x46:  // rora
            {
                const uint8_t result = (uint8_t)((c->fc ? 0x80 : 0) | (c->a >> 1));
                c->fc = (c->a & 0x01) != 0;
                c->a = result;
                SET_NZ(c->a);
            }
            break;
        case 0x47:  // asra
            c->fc = (c->a & 0x01) != 0;
            c->a = (uint8_t)((c->a & 0x80) | (c->a >> 1));
            SET_NZ(c->a);
            break;
        case 0x48: {  // lsla
            const uint16_t result = (uint16_t)(c->a << 1);
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0x49: {  // rola
            const uint16_t result = (uint16_t)((c->a << 1) | (c->fc ? 1 : 0));
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0x4a:  // deca
            c->a = (uint8_t)(c->a - 1);
            SET_NZ(c->a);
            break;
        case 0x4c:  // inca
            c->a = (uint8_t)(c->a + 1);
            SET_NZ(c->a);
            break;
        case 0x4d:  // tsta
            SET_NZ(c->a);
            break;
        case 0x4f:  // clra
            c->a = 0;
            c->fn = 0;
            c->fz = 1;
            break;
        case 0x50: {  // negx
            const uint16_t result = (uint16_t)(0u - c->x);
            SET_NZC(result);
            c->x = (uint8_t)(result);
            break;
        }
        case 0x53:  // comx
            c->x = (uint8_t)(~c->x);
            SET_NZ(c->x);
            c->fc = 1;
            break;
        case 0x54:  // lsrx
            c->fc = (c->x & 0x01) != 0;
            c->x = (uint8_t)(c->x >> 1);
            c->fn = 0;
            c->fz = c->x == 0;
            break;
        case 0x56: {  // rorx
            const uint8_t result = (uint8_t)((c->fc ? 0x80 : 0) | (c->x >> 1));
            c->fc = (c->x & 0x01) != 0;
            c->x = result;
            SET_NZ(c->x);
            break;
        }
        case 0x57:  // asrx
            c->fc = (c->x & 0x01) != 0;
            c->x = (uint8_t)((c->x & 0x80) | (c->x >> 1));
            SET_NZ(c->x);
            break;
        case 0x58: {  // aslx
            const uint16_t result = (uint16_t)(c->x << 1);
            SET_NZC(result);
            c->x = (uint8_t)(result);
            break;
        }
        case 0x59: {  // rolx
            const uint16_t result = (uint16_t)((c->x << 1) | (c->fc ? 1 : 0));
            SET_NZC(result);
            c->x = (uint8_t)(result);
            break;
        }
        case 0x5a:  // decx
            c->x = (uint8_t)(c->x - 1);
            SET_NZ(c->x);
            break;
        case 0x5c:  // incx
            c->x = (uint8_t)(c->x + 1);
            SET_NZ(c->x);
            break;
        case 0x5d:  // tstx
            SET_NZ(c->x);
            break;
        case 0x5f:  // clrx
            c->x = 0;
            c->fn = 0;
            c->fz = 1;
            break;

        // Control.
        case 0x80:  // rti
            set_cc(c, pull(c));
            c->a = pull(c);
            c->x = pull(c);
            c->pc = pull_word(c);
            break;
        case 0x81:  // rts
            c->pc = pull_word(c);
            break;
        case 0x83:  // swi
            push_word(c, c->pc);
            push(c, c->x);
            push(c, c->a);
            push(c, get_cc(c));
            c->fi = 1;
            c->pc = read_word(c, 0xfffc);
            break;
        case 0x97: c->x = c->a; break;         // tax
        case 0x98: c->fc = 0; break;  // clc
        case 0x99: c->fc = 1; break;   // sec
        case 0x9a: c->fi = 0; break;  // cli
        case 0x9b: c->fi = 1; break;   // sei
        case 0x9c: c->sp = c->sp_mask; break;  // rsp
        case 0x9d: break;                 // nop
        case 0x9f: c->a = c->x; break;          // txa

        // ALU on the accumulator.
        case 0xa0: case 0xb0: case 0xc0: case 0xd0: case 0xe0: case 0xf0: {  // sub
            const uint16_t result = (uint16_t)(c->a - value);
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0xa1: case 0xb1: case 0xc1: case 0xd1: case 0xe1: case 0xf1:  // cmp
            SET_NZC((uint16_t)(c->a - value));
            break;
        case 0xa2: case 0xb2: case 0xc2: case 0xd2: case 0xe2: case 0xf2: {  // sbc
            const uint16_t result = (uint16_t)(c->a - value - (c->fc ? 1 : 0));
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0xa3: case 0xb3: case 0xc3: case 0xd3: case 0xe3: case 0xf3:  // cpx
            SET_NZC((uint16_t)(c->x - value));
            break;
        case 0xa4: case 0xb4: case 0xc4: case 0xd4: case 0xe4: case 0xf4:  // and
            c->a = (uint8_t)(c->a & value);
            SET_NZ(c->a);
            break;
        case 0xa5: case 0xb5: case 0xc5: case 0xd5: case 0xe5: case 0xf5:  // bit
            SET_NZ((uint8_t)(c->a & value));
            break;
        case 0xa6: case 0xb6: case 0xc6: case 0xd6: case 0xe6: case 0xf6:  // lda
            c->a = value;
            SET_NZ(c->a);
            break;
        case 0xb7: case 0xc7: case 0xd7: case 0xe7: case 0xf7:  // sta
            SET_NZ(c->a);
            wr(c, address, c->a);
            break;
        case 0xa8: case 0xb8: case 0xc8: case 0xd8: case 0xe8: case 0xf8:  // eor
            c->a = (uint8_t)(c->a ^ value);
            SET_NZ(c->a);
            break;
        case 0xa9: case 0xb9: case 0xc9: case 0xd9: case 0xe9: case 0xf9: {  // adc
            const uint16_t result = (uint16_t)(c->a + value + (c->fc ? 1 : 0));
            c->fh = ((c->a ^ value ^ result) & 0x10) != 0;
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0xaa: case 0xba: case 0xca: case 0xda: case 0xea: case 0xfa:  // ora
            c->a = (uint8_t)(c->a | value);
            SET_NZ(c->a);
            break;
        case 0xab: case 0xbb: case 0xcb: case 0xdb: case 0xeb: case 0xfb: {  // add
            const uint16_t result = (uint16_t)(c->a + value);
            c->fh = ((c->a ^ value ^ result) & 0x10) != 0;
            SET_NZC(result);
            c->a = (uint8_t)(result);
            break;
        }
        case 0xbc: case 0xcc: case 0xdc: case 0xec: case 0xfc:  // jmp
            c->pc = address;
            break;
        case 0xad:  // bsr
            push_word(c, c->pc);
            c->pc = (uint16_t)(c->pc + (int8_t)(value));
            break;
        case 0xbd: case 0xcd: case 0xdd: case 0xed: case 0xfd:  // jsr
            push_word(c, c->pc);
            c->pc = address;
            break;
        case 0xae: case 0xbe: case 0xce: case 0xde: case 0xee: case 0xfe:  // ldx
            c->x = value;
            SET_NZ(c->x);
            break;
        case 0xbf: case 0xcf: case 0xdf: case 0xef: case 0xff:  // stx
            SET_NZ(c->x);
            wr(c, address, c->x);
            break;
        default:
            break;  // illegal opcodes behave as c->a nop
    }

    return kCycles[opcode];
}

void m6805d_reset(m6805d *c)
{
    c->fh = c->fn = c->fz = c->fc = false;
    c->fi = true;
    c->a = c->x = 0;
    c->sp_mask = 0x7f;
    c->sp_low = 0x60;
    c->sp = c->sp_mask;
    c->irq_state = M6805D_CLEAR;
    c->irq_pending = false;
    c->pc = read_word(c, 0xfffe);
}

void m6805d_set_irq(m6805d *c, int state)
{
    c->irq_state = state;
    if (state != M6805D_CLEAR) c->irq_pending = true;
}

int m6805d_run(m6805d *c, int cycles)
{
    int elapsed = 0;
    while (elapsed < cycles) {
        int extra = 0;
        if (c->m68705 && c->irq_pending && !c->fi) {
            push_word(c, c->pc);
            push(c, c->x);
            push(c, c->a);
            push(c, get_cc(c));
            c->fi = true;
            c->irq_pending = false;
            c->pc = read_word(c, 0xfffa);
            extra = 11;
        }
        const int used = execute(c) + extra;
        elapsed += used;
        if (c->cycles) c->cycles(c->user, used);
    }
    return elapsed;
}
