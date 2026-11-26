// konamidsp.c - see konamidsp.h. Translated from dsp-cpp's cpu/konami.cpp:
// its members and methods are mapped onto the struct by the macros below.
#include <string.h>
#include "konamidsp.h"

static void kc_set_nmi(konamid *kc, int state);
static void kc_reset(konamid *kc);
static uint16_t kc_read_word(konamid *kc, uint16_t address);
static void kc_write_word(konamid *kc, uint16_t address, uint16_t value);
static uint8_t kc_fetch(konamid *kc);
static uint16_t kc_fetch_word(konamid *kc);
static void kc_push_s(konamid *kc, uint8_t value);
static uint8_t kc_pop_s(konamid *kc);
static void kc_push_sw(konamid *kc, uint16_t value);
static uint16_t kc_pop_sw(konamid *kc);
static void kc_push_u(konamid *kc, uint8_t value);
static uint8_t kc_pop_u(konamid *kc);
static void kc_push_uw(konamid *kc, uint16_t value);
static uint16_t kc_pop_uw(konamid *kc);
static uint8_t kc_get_cc(konamid *kc);
static void kc_set_cc(konamid *kc, uint8_t value);
static int kc_call_irq(konamid *kc);
static int kc_call_firq(konamid *kc);
static int kc_call_nmi(konamid *kc);
static void kc_trf(konamid *kc, uint8_t value);
static void kc_trf_ex(konamid *kc, uint8_t value);
static uint16_t kc_get_indexed(konamid *kc);
static uint8_t kc_op_neg(konamid *kc, uint8_t v);
static uint8_t kc_op_com(konamid *kc, uint8_t v);
static uint8_t kc_op_lsr(konamid *kc, uint8_t v);
static uint8_t kc_op_ror(konamid *kc, uint8_t v);
static uint8_t kc_op_asr(konamid *kc, uint8_t v);
static uint8_t kc_op_asl(konamid *kc, uint8_t v);
static uint8_t kc_op_rol(konamid *kc, uint8_t v);
static uint8_t kc_op_dec(konamid *kc, uint8_t v);
static uint8_t kc_op_inc(konamid *kc, uint8_t v);
static void kc_op_tst(konamid *kc, uint8_t v);
static uint8_t kc_op_sub8(konamid *kc, uint8_t left, uint8_t right);
static uint16_t kc_op_sub16(konamid *kc, uint16_t left, uint16_t right);
static uint8_t kc_op_sbc(konamid *kc, uint8_t left, uint8_t right);
static uint8_t kc_op_and(konamid *kc, uint8_t left, uint8_t right);
static uint8_t kc_op_eor(konamid *kc, uint8_t left, uint8_t right);
static uint8_t kc_op_adc(konamid *kc, uint8_t left, uint8_t right);
static uint8_t kc_op_or(konamid *kc, uint8_t left, uint8_t right);
static uint8_t kc_op_add8(konamid *kc, uint8_t left, uint8_t right);
static uint16_t kc_op_add16(konamid *kc, uint16_t left, uint16_t right);
static uint8_t kc_op_ld_st8(konamid *kc, uint8_t value);
static uint16_t kc_op_ld_st16(konamid *kc, uint16_t value);
static uint16_t kc_op_neg16(konamid *kc, uint16_t v);
static uint16_t kc_op_inc16(konamid *kc, uint16_t v);
static uint16_t kc_op_dec16(konamid *kc, uint16_t v);
static void kc_op_tst16(konamid *kc, uint16_t v);
static uint8_t kc_op_abs8(konamid *kc, uint8_t v);
static uint16_t kc_op_abs16(konamid *kc, uint16_t v);
static uint16_t kc_op_lsrd(konamid *kc, uint16_t v, uint8_t count);
static uint16_t kc_op_asrd(konamid *kc, uint16_t v, uint8_t count);
static uint16_t kc_op_asld(konamid *kc, uint16_t v, uint8_t count);
static uint16_t kc_op_lsr16(konamid *kc, uint16_t v);
static uint16_t kc_op_asr16(konamid *kc, uint16_t v);
static uint16_t kc_op_asl16(konamid *kc, uint16_t v);
static int kc_run(konamid *kc, int cycles);

#define a (kc->a)
#define b (kc->b)
#define dp (kc->dp)
#define x (kc->x)
#define y (kc->y)
#define u (kc->u)
#define s (kc->s)
#define cc (kc->cc)
#define pc_ (kc->pc)
#define extra_cycles_ (kc->extra_cycles)
#define irq_state_ (kc->irq_state)
#define firq_state_ (kc->firq_state)
#define nmi_state_ (kc->nmi_state)
#define nmi_latched_ (kc->nmi_latched)
#define read(A) kc->rd(kc->user, A)
#define write(A, V) kc->wr(kc->user, A, V)
#define d() ((uint16_t)(((uint16_t)a << 8) | b))
#define set_d(V) do { const uint16_t dv_ = (V); a = (uint8_t)(dv_ >> 8); b = (uint8_t)dv_; } while (0)
#define set_nmi(...) kc_set_nmi(kc, __VA_ARGS__)
#define reset() kc_reset(kc)
#define read_word(...) kc_read_word(kc, __VA_ARGS__)
#define write_word(...) kc_write_word(kc, __VA_ARGS__)
#define fetch() kc_fetch(kc)
#define fetch_word() kc_fetch_word(kc)
#define push_s(...) kc_push_s(kc, __VA_ARGS__)
#define pop_s() kc_pop_s(kc)
#define push_sw(...) kc_push_sw(kc, __VA_ARGS__)
#define pop_sw() kc_pop_sw(kc)
#define push_u(...) kc_push_u(kc, __VA_ARGS__)
#define pop_u() kc_pop_u(kc)
#define push_uw(...) kc_push_uw(kc, __VA_ARGS__)
#define pop_uw() kc_pop_uw(kc)
#define get_cc() kc_get_cc(kc)
#define set_cc(...) kc_set_cc(kc, __VA_ARGS__)
#define call_irq() kc_call_irq(kc)
#define call_firq() kc_call_firq(kc)
#define call_nmi() kc_call_nmi(kc)
#define trf(...) kc_trf(kc, __VA_ARGS__)
#define trf_ex(...) kc_trf_ex(kc, __VA_ARGS__)
#define get_indexed() kc_get_indexed(kc)
#define op_neg(...) kc_op_neg(kc, __VA_ARGS__)
#define op_com(...) kc_op_com(kc, __VA_ARGS__)
#define op_lsr(...) kc_op_lsr(kc, __VA_ARGS__)
#define op_ror(...) kc_op_ror(kc, __VA_ARGS__)
#define op_asr(...) kc_op_asr(kc, __VA_ARGS__)
#define op_asl(...) kc_op_asl(kc, __VA_ARGS__)
#define op_rol(...) kc_op_rol(kc, __VA_ARGS__)
#define op_dec(...) kc_op_dec(kc, __VA_ARGS__)
#define op_inc(...) kc_op_inc(kc, __VA_ARGS__)
#define op_tst(...) kc_op_tst(kc, __VA_ARGS__)
#define op_sub8(...) kc_op_sub8(kc, __VA_ARGS__)
#define op_sub16(...) kc_op_sub16(kc, __VA_ARGS__)
#define op_sbc(...) kc_op_sbc(kc, __VA_ARGS__)
#define op_and(...) kc_op_and(kc, __VA_ARGS__)
#define op_eor(...) kc_op_eor(kc, __VA_ARGS__)
#define op_adc(...) kc_op_adc(kc, __VA_ARGS__)
#define op_or(...) kc_op_or(kc, __VA_ARGS__)
#define op_add8(...) kc_op_add8(kc, __VA_ARGS__)
#define op_add16(...) kc_op_add16(kc, __VA_ARGS__)
#define op_ld_st8(...) kc_op_ld_st8(kc, __VA_ARGS__)
#define op_ld_st16(...) kc_op_ld_st16(kc, __VA_ARGS__)
#define op_neg16(...) kc_op_neg16(kc, __VA_ARGS__)
#define op_inc16(...) kc_op_inc16(kc, __VA_ARGS__)
#define op_dec16(...) kc_op_dec16(kc, __VA_ARGS__)
#define op_tst16(...) kc_op_tst16(kc, __VA_ARGS__)
#define op_abs8(...) kc_op_abs8(kc, __VA_ARGS__)
#define op_abs16(...) kc_op_abs16(kc, __VA_ARGS__)
#define op_lsrd(...) kc_op_lsrd(kc, __VA_ARGS__)
#define op_asrd(...) kc_op_asrd(kc, __VA_ARGS__)
#define op_asld(...) kc_op_asld(kc, __VA_ARGS__)
#define op_lsr16(...) kc_op_lsr16(kc, __VA_ARGS__)
#define op_asr16(...) kc_op_asr16(kc, __VA_ARGS__)
#define op_asl16(...) kc_op_asl16(kc, __VA_ARGS__)
#define run(...) kc_run(kc, __VA_ARGS__)


// Cycle counts per opcode (estados_t in konami.pas).
static const uint8_t kCycles[256] = {
    // 0x00
    0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 5, 5, 4, 4,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 8, 6,
    4, 3, 4, 3, 4, 3, 4, 3, 4, 3, 5, 3, 5, 3, 5, 3,
    5, 3, 5, 3, 5, 4, 5, 4, 3, 3, 3, 3, 3, 0, 0, 0,
    3, 3, 3, 3, 0, 3, 3, 3, 4, 4, 4, 4, 0, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 0, 4, 4, 4,
    1, 1, 4, 2, 2, 4, 2, 2, 4, 2, 2, 4, 2, 2, 4, 4,
    2, 2, 4, 2, 2, 4, 2, 2, 4, 2, 2, 4, 2, 2, 4, 4,
    2, 2, 4, 2, 0, 2, 2, 0, 1, 5, 7, 9, 3, 3, 2, 0,
    3, 2, 2, 11, 21, 10, 1, 0, 2, 0, 0, 0, 2, 0, 2, 0,
    0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,  // C0 c8=decd
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Addressing mode class (paginacion): 0=imp, 2=imm8, 3=ext, 4=idx, 6=idx-byte, 9=idx-word, 0xf=illegal
static const uint8_t kPage[256] = {
    0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf, 4, 4, 4, 4, 2, 2, 2, 2,
     2, 2, 6, 6, 2, 2, 6, 6, 2, 2, 6, 6, 2, 2, 6, 6,
     2, 2, 6, 6, 2, 2, 6, 6, 2, 2, 6, 6, 2, 2, 6, 6,
     2, 2, 6, 6, 2, 2, 6, 6, 2, 6, 4, 4, 2, 2, 2, 2,
     3, 9, 3, 9, 3, 9, 3, 9, 3, 9, 3, 9, 3, 9, 3, 9,
     3, 9, 3, 9, 3, 9, 3, 9, 4, 4, 4, 4, 4,0xf,0xf,0xf,
     2, 2, 2, 2,0xf, 2, 2, 2, 3, 3, 3, 3,0xf, 3, 3, 3,
     2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,0xf, 3, 3, 3,
     0, 0, 4, 0, 0, 4, 0, 0, 4, 0, 0, 4, 0, 0, 4, 0,
     0, 0, 4, 0, 0, 4, 0, 0, 4, 0, 0, 4, 0, 0, 4, 0,
     0, 0, 4, 4,0xf, 4, 4,0xf, 4, 4, 2, 3, 2, 2, 0,0xf,
     0, 0, 0, 0, 0, 0, 0,0xf, 2,0xf,0xf,0xf, 2,0xf, 2,0xf,
    0xf,0xf, 0, 4, 0, 4, 4, 4, 0, 4, 0, 4, 0, 0, 0, 0,  // c8=decd inherent
     0,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,
    0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,
    0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,0xf,
};




static void kc_set_nmi(konamid *kc, int state) {
    if (nmi_state_ == KC_CLEAR && state != KC_CLEAR) nmi_latched_ = state;
    nmi_state_ = state;
}

static void kc_reset(konamid *kc) {
    a = b = dp = 0;
    x = y = u = s = 0;
    pc_ = 0;
    memset(&cc, 0, sizeof cc);
    cc.i = true;
    cc.f = true;
    extra_cycles_ = 0;
    irq_state_ = firq_state_ = nmi_state_ = nmi_latched_ = KC_CLEAR;
    pc_ = read_word(0xfffe);
}

static uint16_t kc_read_word(konamid *kc, uint16_t address) {
    return (uint16_t)(((uint16_t)(read(address)) << 8) | read((uint16_t)(address + 1)));
}
static void kc_write_word(konamid *kc, uint16_t address, uint16_t value) {
    write(address, (uint8_t)(value >> 8));
    write((uint16_t)(address + 1), (uint8_t)(value));
}
static uint8_t kc_fetch(konamid *kc) { return read(pc_++); }
static uint16_t kc_fetch_word(konamid *kc) {
    uint16_t v = read_word(pc_);
    pc_ = (uint16_t)(pc_ + 2);
    return v;
}
static void kc_push_s(konamid *kc, uint8_t value) { write(--s, value); }
static uint8_t kc_pop_s(konamid *kc) { return read(s++); }
static void kc_push_sw(konamid *kc, uint16_t value) {
    push_s((uint8_t)(value));
    push_s((uint8_t)(value >> 8));
}
static uint16_t kc_pop_sw(konamid *kc) {
    uint16_t hi = pop_s();
    return (uint16_t)((hi << 8) | pop_s());
}
static void kc_push_u(konamid *kc, uint8_t value) { write(--u, value); }
static uint8_t kc_pop_u(konamid *kc) { return read(u++); }
static void kc_push_uw(konamid *kc, uint16_t value) {
    push_u((uint8_t)(value));
    push_u((uint8_t)(value >> 8));
}
static uint16_t kc_pop_uw(konamid *kc) {
    uint16_t hi = pop_u();
    return (uint16_t)((hi << 8) | pop_u());
}

static uint8_t kc_get_cc(konamid *kc) {
    return (uint8_t)((cc.e ? 0x80 : 0) | (cc.f ? 0x40 : 0) | (cc.h ? 0x20 : 0) | (cc.i ? 0x10 : 0) |
                   (cc.n ? 0x08 : 0) | (cc.z ? 0x04 : 0) | (cc.v ? 0x02 : 0) | (cc.c ? 0x01 : 0));
}
static void kc_set_cc(konamid *kc, uint8_t value) {
    cc.e = (value & 0x80) != 0;
    cc.f = (value & 0x40) != 0;
    cc.h = (value & 0x20) != 0;
    cc.i = (value & 0x10) != 0;
    cc.n = (value & 0x08) != 0;
    cc.z = (value & 0x04) != 0;
    cc.v = (value & 0x02) != 0;
    cc.c = (value & 0x01) != 0;
}

static int kc_call_irq(konamid *kc) {
    push_sw(pc_);
    push_sw(u);
    push_sw(y);
    push_sw(x);
    push_s(dp);
    push_s(b);
    push_s(a);
    cc.e = true;
    push_s(get_cc());
    pc_ = read_word(0xfff8);
    cc.i = true;
    if (irq_state_ == KC_HOLD) irq_state_ = KC_CLEAR;
    return 19;
}
static int kc_call_firq(konamid *kc) {
    cc.e = false;
    push_sw(pc_);
    push_s(get_cc());
    cc.f = true;
    cc.i = true;
    pc_ = read_word(0xfff6);
    if (firq_state_ == KC_HOLD) firq_state_ = KC_CLEAR;
    return 10;
}
static int kc_call_nmi(konamid *kc) {
    push_sw(pc_);
    push_sw(u);
    push_sw(y);
    push_sw(x);
    push_s(dp);
    push_s(b);
    push_s(a);
    cc.e = true;
    push_s(get_cc());
    pc_ = read_word(0xfffc);
    cc.i = true;
    cc.f = true;
    nmi_latched_ = KC_CLEAR;
    return 19;
}

static void kc_trf(konamid *kc, uint8_t value) {
    uint16_t temp = 0;
    switch (value & 7) {
        case 0: temp = a; break;
        case 1: temp = b; break;
        case 2: temp = x; break;
        case 3: temp = y; break;
        case 4: temp = s; break;
        case 5: temp = u; break;
    }
    switch ((value >> 4) & 7) {
        case 0: a = (uint8_t)(temp); break;
        case 1: b = (uint8_t)(temp); break;
        case 2: x = temp; break;
        case 3: y = temp; break;
        case 4: s = temp; break;
        case 5: u = temp; break;
    }
}
static void kc_trf_ex(konamid *kc, uint8_t value) {
    uint16_t t1 = 0, t2 = 0;
    switch (value & 7) {
        case 0: t1 = a; break;
        case 1: t1 = b; break;
        case 2: t1 = x; break;
        case 3: t1 = y; break;
        case 4: t1 = s; break;
        case 5: t1 = u; break;
    }
    switch ((value >> 4) & 7) {
        case 0: t2 = a; break;
        case 1: t2 = b; break;
        case 2: t2 = x; break;
        case 3: t2 = y; break;
        case 4: t2 = s; break;
        case 5: t2 = u; break;
    }
    switch (value & 7) {
        case 0: a = (uint8_t)(t2); break;
        case 1: b = (uint8_t)(t2); break;
        case 2: x = t2; break;
        case 3: y = t2; break;
        case 4: s = t2; break;
        case 5: u = t2; break;
    }
    switch ((value >> 4) & 7) {
        case 0: a = (uint8_t)(t1); break;
        case 1: b = (uint8_t)(t1); break;
        case 2: x = t1; break;
        case 3: y = t1; break;
        case 4: s = t1; break;
        case 5: u = t1; break;
    }
}

static uint16_t kc_get_indexed(konamid *kc) {
    const uint8_t post = fetch();
    uint16_t* reg = NULL;
    switch (post & 0x70) {
        case 0x20: reg = &x; break;
        case 0x30: reg = &y; break;
        case 0x50: reg = &u; break;
        case 0x60: reg = &s; break;
        case 0x70: reg = &pc_; break;
        default: break;
    }
    uint16_t address = 0xffff;
    switch (post & 0xf7) {
        case 0x07:
            address = fetch_word();
            extra_cycles_ += 3;
            break;
        case 0x20: case 0x30: case 0x50: case 0x60: case 0x70:
            if (reg) {
                address = *reg;
                *reg = (uint16_t)(*reg + 1);
            }
            extra_cycles_ += 3;
            break;
        case 0x21: case 0x31: case 0x51: case 0x61: case 0x71:
            if (reg) {
                address = *reg;
                *reg = (uint16_t)(*reg + 2);
            }
            extra_cycles_ += 4;
            break;
        case 0x22: case 0x32: case 0x52: case 0x62: case 0x72:
            if (reg) {
                *reg = (uint16_t)(*reg - 1);
                address = *reg;
            }
            extra_cycles_ += 4;
            break;
        case 0x23: case 0x33: case 0x53: case 0x63: case 0x73:
            if (reg) {
                *reg = (uint16_t)(*reg - 2);
                address = *reg;
            }
            extra_cycles_ += 4;
            break;
        case 0x24: case 0x34: case 0x54: case 0x64: case 0x74: {
            if (reg) address = *reg;
            const int8_t off = (int8_t)(fetch());
            address = (uint16_t)(address + off);
            extra_cycles_ += 3;
            break;
        }
        case 0x25: case 0x35: case 0x55: case 0x65: case 0x75: {
            if (reg) address = *reg;
            const int16_t off = (int16_t)(fetch_word());
            address = (uint16_t)(address + off);
            extra_cycles_ += 6;
            break;
        }
        case 0x26: case 0x36: case 0x56: case 0x66: case 0x76:
            if (reg) address = *reg;
            extra_cycles_ += 1;
            break;
        case 0xc4:
            address = (uint16_t)(((uint16_t)(dp) << 8) | fetch());
            extra_cycles_ += 2;
            break;
        case 0xa0: case 0xb0: case 0xd0: case 0xe0: case 0xf0:
            if (reg) address = (uint16_t)(*reg + (int8_t)(a));
            extra_cycles_ += 2;
            break;
        case 0xa1: case 0xb1: case 0xd1: case 0xe1: case 0xf1:
            if (reg) address = (uint16_t)(*reg + (int8_t)(b));
            extra_cycles_ += 2;
            break;
        case 0xa7: case 0xb7: case 0xd7: case 0xe7: case 0xf7:
            if (reg) address = (uint16_t)(*reg + (int16_t)(d()));
            extra_cycles_ += 5;
            break;
        default:
            break;
    }
    if (post & 0x08) {
        address = read_word(address);
        extra_cycles_ += 2;
    }
    return address;
}

// ---- ALU (m6809.inc equivalents) ----
static uint8_t kc_op_neg(konamid *kc, uint8_t v) {
    const uint16_t r = (uint16_t)(-(int16_t)(v));
    cc.c = (r & 0x100) != 0;
    cc.v = (v == 0x80);
    v = (uint8_t)(r);
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_com(konamid *kc, uint8_t v) {
    v = (uint8_t)(~v);
    cc.c = true;
    cc.v = false;
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_lsr(konamid *kc, uint8_t v) {
    cc.c = (v & 1) != 0;
    v >>= 1;
    cc.n = false;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_ror(konamid *kc, uint8_t v) {
    const bool c = cc.c;
    cc.c = (v & 1) != 0;
    v = (uint8_t)((v >> 1) | (c ? 0x80 : 0));
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_asr(konamid *kc, uint8_t v) {
    cc.c = (v & 1) != 0;
    v = (uint8_t)(((int8_t)(v) >> 1));
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_asl(konamid *kc, uint8_t v) {
    cc.c = (v & 0x80) != 0;
    cc.v = ((v ^ (v << 1)) & 0x80) != 0;
    v = (uint8_t)(v << 1);
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_rol(konamid *kc, uint8_t v) {
    const bool c = cc.c;
    cc.c = (v & 0x80) != 0;
    v = (uint8_t)((v << 1) | (c ? 1 : 0));
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_dec(konamid *kc, uint8_t v) {
    cc.v = (v == 0x80);
    v = (uint8_t)(v - 1);
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static uint8_t kc_op_inc(konamid *kc, uint8_t v) {
    cc.v = (v == 0x7f);
    v = (uint8_t)(v + 1);
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
    return v;
}
static void kc_op_tst(konamid *kc, uint8_t v) {
    cc.v = false;
    cc.n = (v & 0x80) != 0;
    cc.z = v == 0;
}
static uint8_t kc_op_sub8(konamid *kc, uint8_t left, uint8_t right) {
    const uint16_t r = (uint16_t)(left) - right;
    cc.c = (r & 0x100) != 0;
    cc.v = ((left ^ right) & (left ^ r) & 0x80) != 0;
    const uint8_t out = (uint8_t)(r);
    cc.n = (out & 0x80) != 0;
    cc.z = out == 0;
    return out;
}
static uint16_t kc_op_sub16(konamid *kc, uint16_t left, uint16_t right) {
    const uint32_t r = (uint32_t)(left) - right;
    cc.c = (r & 0x10000) != 0;
    cc.v = ((left ^ right) & (left ^ r) & 0x8000) != 0;
    const uint16_t out = (uint16_t)(r);
    cc.n = (out & 0x8000) != 0;
    cc.z = out == 0;
    return out;
}
static uint8_t kc_op_sbc(konamid *kc, uint8_t left, uint8_t right) {
    return op_sub8(left, (uint8_t)(right + (cc.c ? 1 : 0)));
}
static uint8_t kc_op_and(konamid *kc, uint8_t left, uint8_t right) {
    const uint8_t out = (uint8_t)(left & right);
    cc.v = false;
    cc.n = (out & 0x80) != 0;
    cc.z = out == 0;
    return out;
}
static uint8_t kc_op_eor(konamid *kc, uint8_t left, uint8_t right) {
    const uint8_t out = (uint8_t)(left ^ right);
    cc.v = false;
    cc.n = (out & 0x80) != 0;
    cc.z = out == 0;
    return out;
}
static uint8_t kc_op_adc(konamid *kc, uint8_t left, uint8_t right) {
    const uint16_t r = (uint16_t)(left) + right + (cc.c ? 1 : 0);
    cc.h = ((left ^ right ^ r) & 0x10) != 0;
    cc.c = (r & 0x100) != 0;
    cc.v = (~(left ^ right) & (left ^ r) & 0x80) != 0;
    const uint8_t out = (uint8_t)(r);
    cc.n = (out & 0x80) != 0;
    cc.z = out == 0;
    return out;
}
static uint8_t kc_op_or(konamid *kc, uint8_t left, uint8_t right) {
    const uint8_t out = (uint8_t)(left | right);
    cc.v = false;
    cc.n = (out & 0x80) != 0;
    cc.z = out == 0;
    return out;
}
static uint8_t kc_op_add8(konamid *kc, uint8_t left, uint8_t right) {
    const uint16_t r = (uint16_t)(left) + right;
    cc.h = ((left ^ right ^ r) & 0x10) != 0;
    cc.c = (r & 0x100) != 0;
    cc.v = (~(left ^ right) & (left ^ r) & 0x80) != 0;
    const uint8_t out = (uint8_t)(r);
    cc.n = (out & 0x80) != 0;
    cc.z = out == 0;
    return out;
}
static uint16_t kc_op_add16(konamid *kc, uint16_t left, uint16_t right) {
    const uint32_t r = (uint32_t)(left) + right;
    cc.c = (r & 0x10000) != 0;
    cc.v = (~(left ^ right) & (left ^ r) & 0x8000) != 0;
    const uint16_t out = (uint16_t)(r);
    cc.n = (out & 0x8000) != 0;
    cc.z = out == 0;
    return out;
}
static uint8_t kc_op_ld_st8(konamid *kc, uint8_t value) {
    cc.v = false;
    cc.n = (value & 0x80) != 0;
    cc.z = value == 0;
    return value;
}
static uint16_t kc_op_ld_st16(konamid *kc, uint16_t value) {
    cc.v = false;
    cc.n = (value & 0x8000) != 0;
    cc.z = value == 0;
    return value;
}
static uint16_t kc_op_neg16(konamid *kc, uint16_t v) {
    const uint32_t r = (uint32_t)(-(int32_t)(v));
    cc.c = (r & 0x10000) != 0;
    cc.v = (v == 0x8000);
    v = (uint16_t)(r);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}
static uint16_t kc_op_inc16(konamid *kc, uint16_t v) {
    cc.v = (v == 0x7fff);
    v = (uint16_t)(v + 1);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}
static uint16_t kc_op_dec16(konamid *kc, uint16_t v) {
    cc.v = (v == 0x8000);
    v = (uint16_t)(v - 1);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}
static void kc_op_tst16(konamid *kc, uint16_t v) {
    cc.v = false;
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
}
static uint8_t kc_op_abs8(konamid *kc, uint8_t v) {
    if (v & 0x80) return op_neg(v);
    op_tst(v);
    cc.c = false;
    return v;
}
static uint16_t kc_op_abs16(konamid *kc, uint16_t v) {
    if (v & 0x8000) return op_neg16(v);
    op_tst16(v);
    cc.c = false;
    return v;
}
static uint16_t kc_op_lsrd(konamid *kc, uint16_t v, uint8_t count) {
    if (count == 0) return v;
    cc.c = ((v >> (count - 1)) & 1) != 0;
    v >>= count;
    cc.n = false;
    cc.z = v == 0;
    return v;
}
static uint16_t kc_op_asrd(konamid *kc, uint16_t v, uint8_t count) {
    if (count == 0) return v;
    const int16_t sv = (int16_t)(v);
    cc.c = (((uint16_t)(sv) >> (count - 1)) & 1) != 0;
    v = (uint16_t)(sv >> count);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}
static uint16_t kc_op_asld(konamid *kc, uint16_t v, uint8_t count) {
    if (count == 0) return v;
    cc.c = ((v << (count - 1)) & 0x8000) != 0;
    v = (uint16_t)(v << count);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}


static uint16_t kc_op_lsr16(konamid *kc, uint16_t v) {
    cc.c = (v & 1) != 0;
    v >>= 1;
    cc.n = false;
    cc.z = v == 0;
    return v;
}
static uint16_t kc_op_asr16(konamid *kc, uint16_t v) {
    cc.c = (v & 1) != 0;
    v = (uint16_t)((int16_t)(v) >> 1);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}
static uint16_t kc_op_asl16(konamid *kc, uint16_t v) {
    cc.c = (v & 0x8000) != 0;
    v = (uint16_t)(v << 1);
    cc.n = (v & 0x8000) != 0;
    cc.z = v == 0;
    return v;
}

static int kc_run(konamid *kc, int cycles) {
    int executed = 0;
    while (executed < cycles) {
        extra_cycles_ = 0;
        if (nmi_latched_ != KC_CLEAR) {
            executed += call_nmi();
            continue;
        }
        if (firq_state_ != KC_CLEAR && !cc.f) {
            executed += call_firq();
            continue;
        }
        if (irq_state_ != KC_CLEAR && !cc.i) {
            executed += call_irq();
            continue;
        }

        const uint8_t op = fetch();
        uint8_t numero = 0;
        uint16_t posicion = 0;
        switch (kPage[op]) {
            case 0: break;
            case 2: numero = fetch(); break;
            case 3: posicion = fetch_word(); break;
            case 4: posicion = get_indexed(); break;
            case 6: numero = read(get_indexed()); break;
            case 9: posicion = read_word(get_indexed()); break;
            default: break;  // illegal
        }

        switch (op) {
            case 0x08: x = posicion; cc.z = (x == 0); break;
            case 0x09: y = posicion; cc.z = (y == 0); break;
            case 0x0a: u = posicion; break;
            case 0x0b: s = posicion; break;
            case 0x0c: {  // pshs
                if (numero & 0x80) { push_sw(pc_); extra_cycles_ += 2; }
                if (numero & 0x40) { push_sw(u); extra_cycles_ += 2; }
                if (numero & 0x20) { push_sw(y); extra_cycles_ += 2; }
                if (numero & 0x10) { push_sw(x); extra_cycles_ += 2; }
                if (numero & 0x08) { push_s(dp); extra_cycles_ += 1; }
                if (numero & 0x04) { push_s(b); extra_cycles_ += 1; }
                if (numero & 0x02) { push_s(a); extra_cycles_ += 1; }
                if (numero & 0x01) { push_s(get_cc()); extra_cycles_ += 1; }
                break;
            }
            case 0x0d: {  // pshu
                if (numero & 0x80) { push_uw(pc_); extra_cycles_ += 2; }
                if (numero & 0x40) { push_uw(s); extra_cycles_ += 2; }
                if (numero & 0x20) { push_uw(y); extra_cycles_ += 2; }
                if (numero & 0x10) { push_uw(x); extra_cycles_ += 2; }
                if (numero & 0x08) { push_u(dp); extra_cycles_ += 1; }
                if (numero & 0x04) { push_u(b); extra_cycles_ += 1; }
                if (numero & 0x02) { push_u(a); extra_cycles_ += 1; }
                if (numero & 0x01) { push_u(get_cc()); extra_cycles_ += 1; }
                break;
            }
            case 0x0e: {  // puls
                if (numero & 0x01) { set_cc(pop_s()); extra_cycles_ += 1; }
                if (numero & 0x02) { a = pop_s(); extra_cycles_ += 1; }
                if (numero & 0x04) { b = pop_s(); extra_cycles_ += 1; }
                if (numero & 0x08) { dp = pop_s(); extra_cycles_ += 1; }
                if (numero & 0x10) { x = pop_sw(); extra_cycles_ += 2; }
                if (numero & 0x20) { y = pop_sw(); extra_cycles_ += 2; }
                if (numero & 0x40) { u = pop_sw(); extra_cycles_ += 2; }
                if (numero & 0x80) { pc_ = pop_sw(); extra_cycles_ += 2; }
                break;
            }
            case 0x0f: {  // pulu
                if (numero & 0x01) { set_cc(pop_u()); extra_cycles_ += 1; }
                if (numero & 0x02) { a = pop_u(); extra_cycles_ += 1; }
                if (numero & 0x04) { b = pop_u(); extra_cycles_ += 1; }
                if (numero & 0x08) { dp = pop_u(); extra_cycles_ += 1; }
                if (numero & 0x10) { x = pop_uw(); extra_cycles_ += 2; }
                if (numero & 0x20) { y = pop_uw(); extra_cycles_ += 2; }
                if (numero & 0x40) { s = pop_uw(); extra_cycles_ += 2; }
                if (numero & 0x80) { pc_ = pop_uw(); extra_cycles_ += 2; }
                break;
            }
            case 0x10: case 0x12: a = op_ld_st8(numero); break;
            case 0x11: case 0x13: b = op_ld_st8(numero); break;
            case 0x14: case 0x16: a = op_add8(a, numero); break;
            case 0x15: case 0x17: b = op_add8(b, numero); break;
            case 0x18: case 0x1a: a = op_adc(a, numero); break;
            case 0x19: case 0x1b: b = op_adc(b, numero); break;
            case 0x1c: case 0x1e: a = op_sub8(a, numero); break;
            case 0x1d: case 0x1f: b = op_sub8(b, numero); break;
            case 0x20: case 0x22: a = op_sbc(a, numero); break;
            case 0x21: case 0x23: b = op_sbc(b, numero); break;
            case 0x24: case 0x26: a = op_and(a, numero); break;
            case 0x25: case 0x27: b = op_and(b, numero); break;
            case 0x28: case 0x2a: op_and(a, numero); break;
            case 0x29: case 0x2b: op_and(b, numero); break;
            case 0x2c: case 0x2e: a = op_eor(a, numero); break;
            case 0x2d: case 0x2f: b = op_eor(b, numero); break;
            case 0x30: case 0x32: a = op_or(a, numero); break;
            case 0x31: case 0x33: b = op_or(b, numero); break;
            case 0x34: case 0x36: op_sub8(a, numero); break;
            case 0x35: case 0x37: op_sub8(b, numero); break;
            case 0x38: case 0x39:
                if (kc->set_lines) kc->set_lines(kc->user, numero);
                break;
            case 0x3a: write(posicion, op_ld_st8(a)); break;
            case 0x3b: write(posicion, op_ld_st8(b)); break;
            case 0x3c: set_cc((uint8_t)(get_cc() & numero)); break;
            case 0x3d: set_cc((uint8_t)(get_cc() | numero)); break;
            case 0x3e: trf_ex(numero); break;
            case 0x3f: trf(numero); break;
            case 0x40: case 0x41: set_d(op_ld_st16(posicion)); break;
            case 0x42: case 0x43: x = op_ld_st16(posicion); break;
            case 0x44: case 0x45: y = op_ld_st16(posicion); break;
            case 0x46: case 0x47: u = op_ld_st16(posicion); break;
            case 0x48: case 0x49: s = op_ld_st16(posicion); break;
            case 0x4a: case 0x4b: op_sub16(d(), posicion); break;
            case 0x4c: case 0x4d: op_sub16(x, posicion); break;
            case 0x4e: case 0x4f: op_sub16(y, posicion); break;
            case 0x50: case 0x51: op_sub16(u, posicion); break;
            case 0x52: case 0x53: op_sub16(s, posicion); break;
            case 0x54: case 0x55: set_d(op_add16(d(), posicion)); break;
            case 0x56: case 0x57: set_d(op_sub16(d(), posicion)); break;
            case 0x58: write_word(posicion, op_ld_st16(d())); break;
            case 0x59: write_word(posicion, op_ld_st16(x)); break;
            case 0x5a: write_word(posicion, op_ld_st16(y)); break;
            case 0x5b: write_word(posicion, op_ld_st16(u)); break;
            case 0x5c: write_word(posicion, op_ld_st16(s)); break;
            case 0x60: pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x61: if (!(cc.c || cc.z)) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x62: if (!cc.c) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x63: if (!cc.z) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x64: if (!cc.v) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;  // bvc
            case 0x65: if (!cc.n) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x66: if (cc.n == cc.v) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x67: if (cc.n == cc.v && !cc.z) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x68: pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x69: if (!(cc.c || cc.z)) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x6a: if (!cc.c) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x6b: if (!cc.z) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x6c: if (!cc.v) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;  // lbvc
            case 0x6d: if (!cc.n) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x6e: if (cc.n == cc.v) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x6f: if (cc.n == cc.v && !cc.z) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x70: break;  // brn (never)
            case 0x71: if (cc.c || cc.z) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x72: if (cc.c) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x73: if (cc.z) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x74: if (cc.v) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x75: if (cc.n) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x76: if (cc.n != cc.v) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x77: if (!(cc.n == cc.v && !cc.z)) pc_ = (uint16_t)(pc_ + (int8_t)(numero)); break;
            case 0x78: break;  // lbrn
            case 0x79: if (cc.c || cc.z) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x7a: if (cc.c) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x7b: if (cc.z) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x7c: if (cc.v) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;  // lbvs
            case 0x7d: if (cc.n) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x7e: if (cc.n != cc.v) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x7f: if (!(cc.n == cc.v && !cc.z)) pc_ = (uint16_t)(pc_ + (int16_t)(posicion)); break;
            case 0x80:  // clra
                a = 0; cc.z = true; cc.n = cc.v = cc.c = false;
                break;
            case 0x81:  // clrb
                b = 0; cc.z = true; cc.n = cc.v = cc.c = false;
                break;
            case 0x82:  // clr
                write(posicion, 0); cc.z = true; cc.n = cc.v = cc.c = false;
                break;
            case 0x83: a = op_com(a); break;
            case 0x84: b = op_com(b); break;
            case 0x85: write(posicion, op_com(read(posicion))); break;
            case 0x86: a = op_neg(a); break;
            case 0x87: b = op_neg(b); break;
            case 0x88: write(posicion, op_neg(read(posicion))); break;
            case 0x89: a = op_inc(a); break;
            case 0x8a: b = op_inc(b); break;
            case 0x8b: write(posicion, op_inc(read(posicion))); break;
            case 0x8c: a = op_dec(a); break;
            case 0x8d: b = op_dec(b); break;
            case 0x8e: write(posicion, op_dec(read(posicion))); break;
            case 0x8f: pc_ = pop_sw(); break;  // rts
            case 0x90: op_tst(a); break;
            case 0x91: op_tst(b); break;
            case 0x92: op_tst(read(posicion)); break;
            case 0x93: a = op_lsr(a); break;
            case 0x94: b = op_lsr(b); break;
            case 0x95: write(posicion, op_lsr(read(posicion))); break;
            case 0x96: a = op_ror(a); break;
            case 0x97: b = op_ror(b); break;
            case 0x98: write(posicion, op_ror(read(posicion))); break;
            case 0x99: a = op_asr(a); break;
            case 0x9a: b = op_asr(b); break;
            case 0x9b: write(posicion, op_asr(read(posicion))); break;
            case 0x9c: a = op_asl(a); break;
            case 0x9d: b = op_asl(b); break;
            case 0x9e: write(posicion, op_asl(read(posicion))); break;
            case 0x9f: {  // rti
                set_cc(pop_s());
                if (cc.e) {
                    extra_cycles_ += 9;
                    a = pop_s();
                    b = pop_s();
                    dp = pop_s();
                    x = pop_sw();
                    y = pop_sw();
                    u = pop_sw();
                }
                pc_ = pop_sw();
                break;
            }
            case 0xa0: a = op_rol(a); break;
            case 0xa1: b = op_rol(b); break;
            case 0xa2: write(posicion, op_rol(read(posicion))); break;
            case 0xa3:  // lsr16 mem
                write_word(posicion, op_lsr16(read_word(posicion)));
                break;
            case 0xa5:  // asr16 mem
                write_word(posicion, op_asr16(read_word(posicion)));
                break;
            case 0xa6:  // asl16 mem
                write_word(posicion, op_asl16(read_word(posicion)));
                break;
            case 0xa8: pc_ = posicion; break;  // jmp
            case 0xa9:  // jsr
                push_sw(pc_);
                pc_ = posicion;
                break;
            case 0xaa:  // bsr
                push_sw(pc_);
                pc_ = (uint16_t)(pc_ + (int8_t)(numero));
                break;
            case 0xab:  // lbsr
                push_sw(pc_);
                pc_ = (uint16_t)(pc_ + (int16_t)(posicion));
                break;
            case 0xac: {  // decbjnz
                const uint16_t tempw = (uint16_t)(b - 1);
                cc.z = (tempw & 0xff) == 0;
                cc.n = (tempw & 0x80) != 0;
                b = (uint8_t)(tempw);
                if (!cc.z) pc_ = (uint16_t)(pc_ + (int8_t)(numero));
                break;
            }
            case 0xad: {  // decxjnz
                const uint32_t templ = (uint32_t)(x) - 1;
                cc.z = (templ & 0xffff) == 0;
                cc.n = (templ & 0x8000) != 0;
                x = (uint16_t)(templ);
                if (!cc.z) pc_ = (uint16_t)(pc_ + (int8_t)(numero));
                break;
            }
            case 0xb0: x = (uint16_t)(x + b); break;  // abx
            case 0xb1: {  // daa — match konami.pas / m680x_daa
                uint8_t cf = 0;
                const uint8_t msn = a & 0xf0;
                const uint8_t lsn = a & 0x0f;
                if (lsn > 0x09 || cc.h) cf |= 0x06;
                if ((msn > 0x80 && lsn > 0x09)) cf |= 0x60;
                if (msn > 0x90 || cc.c) cf |= 0x60;
                const uint16_t tempw = (uint16_t)(cf + a);
                cc.v = false;
                cc.n = (tempw & 0x80) != 0;
                cc.z = (tempw & 0xff) == 0;
                cc.c = cc.c || ((tempw & 0x100) != 0);
                a = (uint8_t)(tempw);
                break;
            }
            case 0xb2:  // sex
                a = (b & 0x80) ? 0xff : 0x00;
                cc.n = (d() & 0x8000) != 0;
                cc.z = d() == 0;
                break;
            case 0xb3:  // mul
                set_d((uint16_t)((uint16_t)(a) * b));
                cc.c = (d() & 0x80) != 0;
                cc.z = d() == 0;
                break;
            case 0xb4: {  // lmul
                const uint32_t t = (uint32_t)(x) * y;
                x = (uint16_t)(t >> 16);
                y = (uint16_t)(t);
                cc.z = t == 0;
                cc.c = (t & 0x8000) != 0;
                break;
            }
            case 0xb5: {  // divx
                uint16_t q = 0, r = 0;
                if (b != 0) {
                    q = (uint16_t)(x / b);
                    r = (uint16_t)(x % b);
                }
                x = q;
                b = (uint8_t)(r);
                cc.c = (q & 0x80) != 0;
                cc.z = q == 0;
                break;
            }
            case 0xb6:  // bmove
                while (u != 0) {
                    write(x, read(y));
                    y = (uint16_t)(y + 1);
                    x = (uint16_t)(x + 1);
                    u = (uint16_t)(u - 1);
                    extra_cycles_ += 2;
                }
                break;
            case 0xb8: set_d(op_lsrd(d(), numero)); break;
            case 0xbc: set_d(op_asrd(d(), numero)); break;
            case 0xbe: set_d(op_asld(d(), numero)); break;
            case 0xc2:
                set_d(0);
                cc.z = true;
                cc.n = cc.v = cc.c = false;
                break;
            case 0xc3:
                write_word(posicion, 0);
                cc.z = true;
                cc.n = cc.v = cc.c = false;
                break;
            case 0xc4: set_d(op_neg16(d())); break;
            case 0xc5: write_word(posicion, op_neg16(read_word(posicion))); break;
            case 0xc6: set_d(op_inc16(d())); break;
            case 0xc7: write_word(posicion, op_inc16(read_word(posicion))); break;
            case 0xc8: set_d(op_dec16(d())); break;  // decd (symmetric to incd)
            case 0xc9: write_word(posicion, op_dec16(read_word(posicion))); break;
            case 0xca: op_tst16(d()); break;
            case 0xcb: op_tst16(read_word(posicion)); break;
            case 0xcc: a = op_abs8(a); break;
            case 0xcd: b = op_abs8(b); break;
            case 0xce: set_d(op_abs16(d())); break;
            case 0xcf:
                while (u != 0) {
                    write(x, a);
                    x = (uint16_t)(x + 1);
                    u = (uint16_t)(u - 1);
                    extra_cycles_ += 2;
                }
                break;
            case 0xd0:
                while (u != 0) {
                    write_word(x, d());
                    x = (uint16_t)(x + 2);
                    u = (uint16_t)(u - 1);
                    extra_cycles_ += 3;
                }
                break;
            default:
                break;
        }

        const int used = (int)(kCycles[op]) + extra_cycles_;
        executed += used > 0 ? used : 1;
        if (kc->cycles) kc->cycles(kc->user, used > 0 ? used : 1);
    }
    return executed;
}


#undef read
#undef write
void konamid_reset(konamid *kc) { kc_reset(kc); }
int konamid_run(konamid *kc, int cycles) { return kc_run(kc, cycles); }
void konamid_set_nmi(konamid *kc, int state) { kc_set_nmi(kc, state); }
