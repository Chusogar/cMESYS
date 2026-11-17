#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "spc700.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static uint8_t rd(spc700 *P, uint16_t ad);
static void wr(spc700 *P, uint16_t ad, uint8_t v);
static uint8_t fetch(spc700 *P);
static uint16_t fetch16(spc700 *P);
static uint16_t dp(spc700 *P, uint8_t off);
static void push(spc700 *P, uint8_t v);
static uint8_t pop(spc700 *P);
static uint8_t setnz(spc700 *P, uint8_t v);
static uint16_t setnz16(spc700 *P, uint16_t v);
static uint16_t addr_dp(spc700 *P);
static uint16_t addr_dpx(spc700 *P);
static uint16_t addr_dpy(spc700 *P);
static uint16_t addr_abs(spc700 *P);
static uint16_t addr_absx(spc700 *P);
static uint16_t addr_absy(spc700 *P);
static uint16_t dp_next(spc700 *P, uint16_t t);
static uint16_t addr_idx(spc700 *P);
static uint16_t addr_idy(spc700 *P);
static uint16_t bit_addr(spc700 *P, uint8_t* bit);
static uint8_t op_inc(spc700 *P, uint8_t v);
static uint8_t op_dec(spc700 *P, uint8_t v);
static uint8_t psw_byte(spc700 *P);
static void set_psw(spc700 *P, uint8_t v);
static uint8_t op_adc(spc700 *P, uint8_t m, uint8_t n);
static uint8_t op_sbc(spc700 *P, uint8_t m, uint8_t n);
static void op_cmp(spc700 *P, uint8_t m, uint8_t n);
static uint8_t op_asl(spc700 *P, uint8_t v);
static uint8_t op_lsr(spc700 *P, uint8_t v);
static uint8_t op_rol(spc700 *P, uint8_t v);
static uint8_t op_ror(spc700 *P, uint8_t v);
static void branch(spc700 *P, bool take, int* cycles);

// Cycle counts per opcode, from the published timing table.
const uint8_t kCycles[256] = {
    2,8,4,5,3,4,3,6,2,6,5,4,5,4,6,8,   // 00
    2,8,4,5,4,5,5,6,5,5,6,5,2,2,4,6,   // 10
    2,8,4,5,3,4,3,6,2,6,5,4,5,4,5,4,   // 20
    2,8,4,5,4,5,5,6,5,5,6,5,2,2,3,8,   // 30
    2,8,4,5,3,4,3,6,2,6,4,4,5,4,6,6,   // 40
    2,8,4,5,4,5,5,6,5,5,4,5,2,2,4,3,   // 50
    2,8,4,5,3,4,3,6,2,6,4,4,5,4,5,5,   // 60
    2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,6,   // 70
    2,8,4,5,3,4,3,6,2,6,5,4,5,2,4,5,   // 80
    2,8,4,5,4,5,5,6,5,5,5,5,2,2,12,5,  // 90
    3,8,4,5,3,4,3,6,2,6,4,4,5,2,4,4,   // A0
    2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,4,   // B0
    3,8,4,5,4,5,4,7,2,5,6,4,5,2,4,9,   // C0
    2,8,4,5,5,6,6,7,4,5,5,5,2,2,6,3,   // D0
    2,8,4,5,3,4,3,6,2,4,5,3,4,3,4,3,   // E0
    2,8,4,5,4,5,5,6,3,4,5,4,2,2,4,3,   // F0
};
static uint8_t rd(spc700 *P, uint16_t ad) { return P->read_ ? P->read_(P->user_, ad) : 0; }
static void wr(spc700 *P, uint16_t ad, uint8_t v) { if (P->write_) P->write_(P->user_, ad, v); }
static uint8_t fetch(spc700 *P) { return rd(P, P->pc_++); }
static uint16_t fetch16(spc700 *P) { const uint8_t l = fetch(P); return (uint16_t)(l | (fetch(P) << 8)); }
static uint16_t dp(spc700 *P, uint8_t off) { return (uint16_t)((P->psw_.p ? 0x100 : 0) | off); }
static void push(spc700 *P, uint8_t v) { wr(P, (uint16_t)(0x100 | P->sp), v); P->sp--; }
static uint8_t pop(spc700 *P) { P->sp++; return rd(P, (uint16_t)(0x100 | P->sp)); }
static uint8_t setnz(spc700 *P, uint8_t v) { P->psw_.n = (v & 0x80) != 0; P->psw_.z = (v == 0); return v; }
static uint16_t setnz16(spc700 *P, uint16_t v) { P->psw_.n = (v & 0x8000) != 0; P->psw_.z = (v == 0); return v; }
static uint16_t addr_dp(spc700 *P) { return dp(P, fetch(P)); }
static uint16_t addr_dpx(spc700 *P) { return dp(P, (uint8_t)(fetch(P) + P->x)); }
static uint16_t addr_dpy(spc700 *P) { return dp(P, (uint8_t)(fetch(P) + P->y)); }
static uint16_t addr_abs(spc700 *P) { return fetch16(P); }
static uint16_t addr_absx(spc700 *P) { return (uint16_t)(fetch16(P) + P->x); }
static uint16_t addr_absy(spc700 *P) { return (uint16_t)(fetch16(P) + P->y); }
static uint16_t dp_next(spc700 *P, uint16_t t) { return (uint16_t)((t & 0xff00) | (uint8_t)(t + 1)); }
static uint16_t addr_idx(spc700 *P) {
    const uint16_t p = dp(P, (uint8_t)(fetch(P) + P->x));
    return (uint16_t)(rd(P, p) | (rd(P, dp_next(P, p)) << 8));
}
static uint16_t addr_idy(spc700 *P) {
    const uint16_t p = addr_dp(P);
    return (uint16_t)((rd(P, p) | (rd(P, dp_next(P, p)) << 8)) + P->y);
}
static uint16_t bit_addr(spc700 *P, uint8_t* bit) {
    const uint16_t v = fetch16(P);
    *bit = (uint8_t)(v >> 13);
    return (uint16_t)(v & 0x1fff);
}
static uint8_t op_inc(spc700 *P, uint8_t v) { return setnz(P, (uint8_t)(v + 1)); }
static uint8_t op_dec(spc700 *P, uint8_t v) { return setnz(P, (uint8_t)(v - 1)); }
void spc700_reset(spc700 *P) {
    P->halted_ = false;
    P->a = P->x = P->y = 0;
    P->sp = 0xef;
    P->psw_ = (spc700_psw){0};
    // The reset vector lives at the top of the IPL ROM.
    P->pc_ = (uint16_t)(rd(P, 0xfffe) | (rd(P, 0xffff) << 8));
}
static uint8_t psw_byte(spc700 *P) {
    return (uint8_t)((P->psw_.n ? 0x80 : 0) | (P->psw_.v ? 0x40 : 0) | (P->psw_.p ? 0x20 : 0) |
                   (P->psw_.b ? 0x10 : 0) | (P->psw_.h ? 0x08 : 0) | (P->psw_.i ? 0x04 : 0) |
                   (P->psw_.z ? 0x02 : 0) | (P->psw_.c ? 0x01 : 0));
}
static void set_psw(spc700 *P, uint8_t v) {
    P->psw_.n = v & 0x80; P->psw_.v = v & 0x40; P->psw_.p = v & 0x20; P->psw_.b = v & 0x10;
    P->psw_.h = v & 0x08; P->psw_.i = v & 0x04; P->psw_.z = v & 0x02; P->psw_.c = v & 0x01;
}
static uint8_t op_adc(spc700 *P, uint8_t m, uint8_t n) {
    const int r = m + n + (P->psw_.c ? 1 : 0);
    P->psw_.c = r > 0xff;
    P->psw_.h = ((m ^ n ^ (uint8_t)(r)) & 0x10) != 0;
    P->psw_.v = (~(m ^ n) & (m ^ (uint8_t)(r)) & 0x80) != 0;
    return setnz(P, (uint8_t)(r));
}
static uint8_t op_sbc(spc700 *P, uint8_t m, uint8_t n) {
    const int r = m - n - (P->psw_.c ? 0 : 1);
    P->psw_.c = r >= 0;
    P->psw_.h = ((m ^ n ^ (uint8_t)(r)) & 0x10) == 0;
    P->psw_.v = ((m ^ n) & (m ^ (uint8_t)(r)) & 0x80) != 0;
    return setnz(P, (uint8_t)(r));
}
static void op_cmp(spc700 *P, uint8_t m, uint8_t n) {
    const int r = m - n;
    P->psw_.c = r >= 0;
    setnz(P, (uint8_t)(r));
}
static uint8_t op_asl(spc700 *P, uint8_t v) { P->psw_.c = (v & 0x80) != 0; return setnz(P, (uint8_t)(v << 1)); }
static uint8_t op_lsr(spc700 *P, uint8_t v) { P->psw_.c = (v & 0x01) != 0; return setnz(P, (uint8_t)(v >> 1)); }
static uint8_t op_rol(spc700 *P, uint8_t v) {
    const uint8_t c = P->psw_.c ? 1 : 0;
    P->psw_.c = (v & 0x80) != 0;
    return setnz(P, (uint8_t)((v << 1) | c));
}
static uint8_t op_ror(spc700 *P, uint8_t v) {
    const uint8_t c = P->psw_.c ? 0x80 : 0;
    P->psw_.c = (v & 0x01) != 0;
    return setnz(P, (uint8_t)((v >> 1) | c));
}
static void branch(spc700 *P, bool take, int* cycles) {
    const int8_t rel = (int8_t)(fetch(P));
    if (take) { P->pc_ = (uint16_t)(P->pc_ + rel); *cycles += 2; }
}
int spc700_step(spc700 *P) {
    if (P->halted_) return 2;
    const uint8_t op = fetch(P);
    int cycles = kCycles[op];

    switch (op) {
        case 0x00: break;                                    // NOP
        case 0xef: case 0xff: P->halted_ = true; break;         // SLEEP / STOP
        case 0x8f: { const uint8_t v = fetch(P); wr(P, addr_dp(P), v); break; }   // MOV dp,#i
        case 0xfa: { const uint16_t s = addr_dp(P); wr(P, addr_dp(P), rd(P, s)); break; }  // MOV dd,ds

        // --- MOV A ---
        case 0xe8: setnz(P, P->a = fetch(P)); break;
        case 0xe4: setnz(P, P->a = rd(P, addr_dp(P))); break;
        case 0xf4: setnz(P, P->a = rd(P, addr_dpx(P))); break;
        case 0xe5: setnz(P, P->a = rd(P, addr_abs(P))); break;
        case 0xf5: setnz(P, P->a = rd(P, addr_absx(P))); break;
        case 0xf6: setnz(P, P->a = rd(P, addr_absy(P))); break;
        case 0xe6: setnz(P, P->a = rd(P, dp(P, P->x))); break;
        case 0xbf: setnz(P, P->a = rd(P, dp(P, P->x))); P->x++; break;         // MOV A,(X)+
        case 0xe7: setnz(P, P->a = rd(P, addr_idx(P))); break;
        case 0xf7: setnz(P, P->a = rd(P, addr_idy(P))); break;
        case 0x7d: setnz(P, P->a = P->x); break;                      // MOV A,X
        case 0xdd: setnz(P, P->a = P->y); break;                      // MOV A,Y

        case 0xc4: wr(P, addr_dp(P), P->a); break;
        case 0xd4: wr(P, addr_dpx(P), P->a); break;
        case 0xc5: wr(P, addr_abs(P), P->a); break;
        case 0xd5: wr(P, addr_absx(P), P->a); break;
        case 0xd6: wr(P, addr_absy(P), P->a); break;
        case 0xc6: wr(P, dp(P, P->x), P->a); break;
        case 0xaf: wr(P, dp(P, P->x), P->a); P->x++; break;                 // MOV (X)+,A
        case 0xc7: wr(P, addr_idx(P), P->a); break;
        case 0xd7: wr(P, addr_idy(P), P->a); break;
        case 0x5d: setnz(P, P->x = P->a); break;                      // MOV X,A
        case 0xfd: setnz(P, P->y = P->a); break;                      // MOV Y,A

        // --- MOV X / Y ---
        case 0xcd: setnz(P, P->x = fetch(P)); break;
        case 0xf8: setnz(P, P->x = rd(P, addr_dp(P))); break;
        case 0xf9: setnz(P, P->x = rd(P, addr_dpy(P))); break;
        case 0xe9: setnz(P, P->x = rd(P, addr_abs(P))); break;
        case 0xd8: wr(P, addr_dp(P), P->x); break;
        case 0xd9: wr(P, addr_dpy(P), P->x); break;
        case 0xc9: wr(P, addr_abs(P), P->x); break;
        case 0x9d: setnz(P, P->x = P->sp); break;                     // MOV X,SP
        case 0xbd: P->sp = P->x; break;                            // MOV SP,X

        case 0x8d: setnz(P, P->y = fetch(P)); break;
        case 0xeb: setnz(P, P->y = rd(P, addr_dp(P))); break;
        case 0xfb: setnz(P, P->y = rd(P, addr_dpx(P))); break;
        case 0xec: setnz(P, P->y = rd(P, addr_abs(P))); break;
        case 0xcb: wr(P, addr_dp(P), P->y); break;
        case 0xdb: wr(P, addr_dpx(P), P->y); break;
        case 0xcc: wr(P, addr_abs(P), P->y); break;

        // --- arithmetic/logic on A, one family per operation ---
        #define ALU_A(base, EXPR)                                            \
            case base + 0x08: { const uint8_t m = fetch(P);      EXPR; break; } \
            case base + 0x06: { const uint8_t m = rd(P, dp(P, P->x));    EXPR; break; } \
            case base + 0x04: { const uint8_t m = rd(P, addr_dp(P));  EXPR; break; } \
            case base + 0x14: { const uint8_t m = rd(P, addr_dpx(P)); EXPR; break; } \
            case base + 0x05: { const uint8_t m = rd(P, addr_abs(P)); EXPR; break; } \
            case base + 0x15: { const uint8_t m = rd(P, addr_absx(P));EXPR; break; } \
            case base + 0x16: { const uint8_t m = rd(P, addr_absy(P));EXPR; break; } \
            case base + 0x07: { const uint8_t m = rd(P, addr_idx(P)); EXPR; break; } \
            case base + 0x17: { const uint8_t m = rd(P, addr_idy(P)); EXPR; break; }
        ALU_A(0x00, setnz(P, P->a = (uint8_t)(P->a | m)))      // OR
        ALU_A(0x20, setnz(P, P->a = (uint8_t)(P->a & m)))      // AND
        ALU_A(0x40, setnz(P, P->a = (uint8_t)(P->a ^ m)))      // EOR
        ALU_A(0x60, op_cmp(P, P->a, m))                   // CMP
        ALU_A(0x80, P->a = op_adc(P, P->a, m))               // ADC
        ALU_A(0xa0, P->a = op_sbc(P, P->a, m))               // SBC
        #undef ALU_A

        // (X),(Y) and dp,dp forms
        case 0x19: { const uint8_t r = (uint8_t)(rd(P, dp(P, P->x)) | rd(P, dp(P, P->y))); wr(P, dp(P, P->x), setnz(P, r)); break; }
        case 0x39: { const uint8_t r = (uint8_t)(rd(P, dp(P, P->x)) & rd(P, dp(P, P->y))); wr(P, dp(P, P->x), setnz(P, r)); break; }
        case 0x59: { const uint8_t r = (uint8_t)(rd(P, dp(P, P->x)) ^ rd(P, dp(P, P->y))); wr(P, dp(P, P->x), setnz(P, r)); break; }
        case 0x79: op_cmp(P, rd(P, dp(P, P->x)), rd(P, dp(P, P->y))); break;
        case 0x99: { const uint8_t r = op_adc(P, rd(P, dp(P, P->x)), rd(P, dp(P, P->y))); wr(P, dp(P, P->x), r); break; }
        case 0xb9: { const uint8_t r = op_sbc(P, rd(P, dp(P, P->x)), rd(P, dp(P, P->y))); wr(P, dp(P, P->x), r); break; }
        case 0x09: { const uint8_t s = rd(P, addr_dp(P)); const uint16_t d = addr_dp(P);
                     wr(P, d, setnz(P, (uint8_t)(rd(P, d) | s))); break; }
        case 0x29: { const uint8_t s = rd(P, addr_dp(P)); const uint16_t d = addr_dp(P);
                     wr(P, d, setnz(P, (uint8_t)(rd(P, d) & s))); break; }
        case 0x49: { const uint8_t s = rd(P, addr_dp(P)); const uint16_t d = addr_dp(P);
                     wr(P, d, setnz(P, (uint8_t)(rd(P, d) ^ s))); break; }
        case 0x69: { const uint8_t s = rd(P, addr_dp(P)); op_cmp(P, rd(P, addr_dp(P)), s); break; }
        case 0x89: { const uint8_t s = rd(P, addr_dp(P)); const uint16_t d = addr_dp(P);
                     wr(P, d, op_adc(P, rd(P, d), s)); break; }
        case 0xa9: { const uint8_t s = rd(P, addr_dp(P)); const uint16_t d = addr_dp(P);
                     wr(P, d, op_sbc(P, rd(P, d), s)); break; }
        case 0x98: { const uint8_t s = fetch(P); const uint16_t d = addr_dp(P);
                     wr(P, d, op_adc(P, rd(P, d), s)); break; }
        case 0xb8: { const uint8_t s = fetch(P); const uint16_t d = addr_dp(P);
                     wr(P, d, op_sbc(P, rd(P, d), s)); break; }
        case 0x18: { const uint8_t s = fetch(P); const uint16_t d = addr_dp(P);
                     wr(P, d, setnz(P, (uint8_t)(rd(P, d) | s))); break; }
        case 0x38: { const uint8_t s = fetch(P); const uint16_t d = addr_dp(P);
                     wr(P, d, setnz(P, (uint8_t)(rd(P, d) & s))); break; }
        case 0x58: { const uint8_t s = fetch(P); const uint16_t d = addr_dp(P);
                     wr(P, d, setnz(P, (uint8_t)(rd(P, d) ^ s))); break; }
        case 0x78: { const uint8_t s = fetch(P); op_cmp(P, rd(P, addr_dp(P)), s); break; }

        // CMP X / CMP Y
        case 0xc8: op_cmp(P, P->x, fetch(P)); break;
        case 0x3e: op_cmp(P, P->x, rd(P, addr_dp(P))); break;
        case 0x1e: op_cmp(P, P->x, rd(P, addr_abs(P))); break;
        case 0xad: op_cmp(P, P->y, fetch(P)); break;
        case 0x7e: op_cmp(P, P->y, rd(P, addr_dp(P))); break;
        case 0x5e: op_cmp(P, P->y, rd(P, addr_abs(P))); break;

        // --- shifts and increments ---
        #define RMW(opc_a, opc_dp, opc_dpx, opc_abs, FN)                       \
            case opc_a:   P->a = FN(P, P->a); break;                                    \
            case opc_dp:  { const uint16_t t = addr_dp(P);  wr(P, t, FN(P, rd(P, t))); break; } \
            case opc_dpx: { const uint16_t t = addr_dpx(P); wr(P, t, FN(P, rd(P, t))); break; } \
            case opc_abs: { const uint16_t t = addr_abs(P); wr(P, t, FN(P, rd(P, t))); break; }
        RMW(0x1c, 0x0b, 0x1b, 0x0c, op_asl)
        RMW(0x5c, 0x4b, 0x5b, 0x4c, op_lsr)
        RMW(0x3c, 0x2b, 0x3b, 0x2c, op_rol)
        RMW(0x7c, 0x6b, 0x7b, 0x6c, op_ror)
        RMW(0xbc, 0xab, 0xbb, 0xac, op_inc)
        RMW(0x9c, 0x8b, 0x9b, 0x8c, op_dec)
        #undef RMW
        case 0x3d: setnz(P, ++P->x); break;
        case 0x1d: setnz(P, --P->x); break;
        case 0xfc: setnz(P, ++P->y); break;
        case 0xdc: setnz(P, --P->y); break;

        // --- 16-bit (YA and word memory) ---
        case 0xba: { const uint16_t t = addr_dp(P);            // MOVW YA,dp
                     P->a = rd(P, t); P->y = rd(P, dp_next(P, t));
                     setnz16(P, (uint16_t)(P->a | (P->y << 8))); break; }
        case 0xda: { const uint16_t t = addr_dp(P);            // MOVW dp,YA
                     wr(P, t, P->a); wr(P, dp_next(P, t), P->y); break; }
        case 0x3a: { const uint16_t t = addr_dp(P);            // INCW dp
                     uint16_t v = (uint16_t)(rd(P, t) | (rd(P, dp_next(P, t)) << 8));
                     v++; wr(P, t, (uint8_t)(v)); wr(P, dp_next(P, t), (uint8_t)(v >> 8));
                     setnz16(P, v); break; }
        case 0x1a: { const uint16_t t = addr_dp(P);            // DECW dp
                     uint16_t v = (uint16_t)(rd(P, t) | (rd(P, dp_next(P, t)) << 8));
                     v--; wr(P, t, (uint8_t)(v)); wr(P, dp_next(P, t), (uint8_t)(v >> 8));
                     setnz16(P, v); break; }
        case 0x7a: { const uint16_t t = addr_dp(P);            // ADDW YA,dp
                     const uint16_t m = (uint16_t)(rd(P, t) | (rd(P, dp_next(P, t)) << 8));
                     const uint16_t ya = (uint16_t)(P->a | (P->y << 8));
                     const uint32_t r = (uint32_t)(ya) + m;
                     P->psw_.c = r > 0xffff;
                     P->psw_.h = ((ya ^ m ^ (uint16_t)(r)) & 0x1000) != 0;
                     P->psw_.v = (~(ya ^ m) & (ya ^ (uint16_t)(r)) & 0x8000) != 0;
                     P->a = (uint8_t)(r); P->y = (uint8_t)(r >> 8); setnz16(P, (uint16_t)(r)); break; }
        case 0x9a: { const uint16_t t = addr_dp(P);            // SUBW YA,dp
                     const uint16_t m = (uint16_t)(rd(P, t) | (rd(P, dp_next(P, t)) << 8));
                     const uint16_t ya = (uint16_t)(P->a | (P->y << 8));
                     const int32_t r = (int32_t)(ya) - m;
                     P->psw_.c = r >= 0;
                     P->psw_.h = ((ya ^ m ^ (uint16_t)(r)) & 0x1000) == 0;
                     P->psw_.v = ((ya ^ m) & (ya ^ (uint16_t)(r)) & 0x8000) != 0;
                     P->a = (uint8_t)(r); P->y = (uint8_t)((uint16_t)(r) >> 8); setnz16(P, (uint16_t)(r)); break; }
        case 0x5a: { const uint16_t t = addr_dp(P);            // CMPW YA,dp
                     const uint16_t m = (uint16_t)(rd(P, t) | (rd(P, dp_next(P, t)) << 8));
                     const uint16_t ya = (uint16_t)(P->a | (P->y << 8));
                     const int32_t r = (int32_t)(ya) - m;
                     P->psw_.c = r >= 0; setnz16(P, (uint16_t)(r)); break; }
        case 0xcf: { const uint16_t r = (uint16_t)(P->a) * P->y;      // MUL YA
                     P->a = (uint8_t)(r); P->y = (uint8_t)(r >> 8); setnz(P, P->y); break; }
        case 0x9e: {                                          // DIV YA,X
                     // The hardware's shift-subtract divider: results past
                     // 8 bits come out as below (bsnes).
                     const uint32_t ya = (uint32_t)(P->a | (P->y << 8));
                     P->psw_.h = (P->y & 0x0f) >= (P->x & 0x0f);
                     P->psw_.v = P->y >= P->x;
                     if (P->y < (P->x << 1)) {
                         P->a = (uint8_t)(ya / P->x);
                         P->y = (uint8_t)(ya % P->x);
                     } else {
                         P->a = (uint8_t)(255 - (ya - ((uint32_t)(P->x) << 9)) / (256 - P->x));
                         P->y = (uint8_t)(P->x + (ya - ((uint32_t)(P->x) << 9)) % (256 - P->x));
                     }
                     setnz(P, P->a); break; }

        // --- branches ---
        case 0x2f: { const int8_t r = (int8_t)(fetch(P)); P->pc_ = (uint16_t)(P->pc_ + r); break; }  // BRA
        case 0xf0: branch(P, P->psw_.z, &cycles); break;
        case 0xd0: branch(P, !P->psw_.z, &cycles); break;
        case 0xb0: branch(P, P->psw_.c, &cycles); break;
        case 0x90: branch(P, !P->psw_.c, &cycles); break;
        case 0x70: branch(P, P->psw_.v, &cycles); break;
        case 0x50: branch(P, !P->psw_.v, &cycles); break;
        case 0x30: branch(P, P->psw_.n, &cycles); break;
        case 0x10: branch(P, !P->psw_.n, &cycles); break;
        case 0x2e: { const uint8_t m = rd(P, addr_dp(P)); branch(P, P->a != m, &cycles); break; }  // CBNE dp,rel
        case 0xde: { const uint8_t m = rd(P, addr_dpx(P)); branch(P, P->a != m, &cycles); break; }
        case 0x6e: { const uint16_t t = addr_dp(P);            // DBNZ dp,rel
                     const uint8_t v = (uint8_t)(rd(P, t) - 1); wr(P, t, v);
                     branch(P, v != 0, &cycles); break; }
        case 0xfe: { P->y--; branch(P, P->y != 0, &cycles); break; }    // DBNZ Y,rel

        // --- calls and returns ---
        case 0x3f: { const uint16_t t = fetch16(P);            // CALL
                     push(P, (uint8_t)(P->pc_ >> 8)); push(P, (uint8_t)(P->pc_)); P->pc_ = t; break; }
        case 0x01: case 0x11: case 0x21: case 0x31: case 0x41: case 0x51: case 0x61: case 0x71:
        case 0x81: case 0x91: case 0xa1: case 0xb1: case 0xc1: case 0xd1: case 0xe1: case 0xf1: {
            // TCALL n: call through the vector table at $FFDE - 2n.
            const uint16_t v = (uint16_t)(0xffde - 2 * (op >> 4));
            push(P, (uint8_t)(P->pc_ >> 8)); push(P, (uint8_t)(P->pc_));
            P->pc_ = (uint16_t)(rd(P, v) | (rd(P, (uint16_t)(v + 1)) << 8));
            break;
        }
        case 0x4f: { const uint8_t t = fetch(P);               // PCALL
                     push(P, (uint8_t)(P->pc_ >> 8)); push(P, (uint8_t)(P->pc_));
                     P->pc_ = (uint16_t)(0xff00 | t); break; }
        case 0x6f: { const uint8_t l = pop(P); P->pc_ = (uint16_t)(l | (pop(P) << 8)); break; }  // RET
        case 0x7f: { set_psw(P, pop(P)); const uint8_t l = pop(P);
                     P->pc_ = (uint16_t)(l | (pop(P) << 8)); break; }                           // RETI
        case 0x5f: P->pc_ = fetch16(P); break;                    // JMP !P->a
        case 0x1f: { const uint16_t t = (uint16_t)(fetch16(P) + P->x);   // JMP [!P->a+X]
                     P->pc_ = (uint16_t)(rd(P, t) | (rd(P, (uint16_t)(t + 1)) << 8)); break; }
        case 0x0f: { push(P, (uint8_t)(P->pc_ >> 8)); push(P, (uint8_t)(P->pc_)); push(P, psw_byte(P));
                     P->psw_.b = true; P->psw_.i = false;
                     P->pc_ = (uint16_t)(rd(P, 0xffde) | (rd(P, 0xffdf) << 8)); break; }             // BRK

        // --- stack ---
        case 0x2d: push(P, P->a); break;
        case 0x4d: push(P, P->x); break;
        case 0x6d: push(P, P->y); break;
        case 0x0d: push(P, psw_byte(P)); break;
        case 0xae: P->a = pop(P); break;
        case 0xce: P->x = pop(P); break;
        case 0xee: P->y = pop(P); break;
        case 0x8e: set_psw(P, pop(P)); break;

        // --- flags ---
        case 0x60: P->psw_.c = false; break;
        case 0x80: P->psw_.c = true; break;
        case 0xed: P->psw_.c = !P->psw_.c; break;
        case 0xe0: P->psw_.v = false; P->psw_.h = false; break;
        case 0x20: P->psw_.p = false; break;
        case 0x40: P->psw_.p = true; break;
        case 0xa0: P->psw_.i = true; break;
        case 0xc0: P->psw_.i = false; break;
        case 0x9f: P->a = (uint8_t)((P->a >> 4) | (P->a << 4)); setnz(P, P->a); break;   // XCN
        case 0xdf: if (P->psw_.c || P->a > 0x99) { P->a = (uint8_t)(P->a + 0x60); P->psw_.c = true; }   // DAA
                   if (P->psw_.h || (P->a & 0x0f) > 9) P->a = (uint8_t)(P->a + 6);
                   setnz(P, P->a); break;
        case 0xbe: if (!P->psw_.c || P->a > 0x99) { P->a = (uint8_t)(P->a - 0x60); P->psw_.c = false; } // DAS
                   if (!P->psw_.h || (P->a & 0x0f) > 9) P->a = (uint8_t)(P->a - 6);
                   setnz(P, P->a); break;

        // --- single-bit operations on dp ---
        case 0x02: case 0x22: case 0x42: case 0x62:
        case 0x82: case 0xa2: case 0xc2: case 0xe2: {          // SET1 dp.bit
            const uint16_t t = addr_dp(P);
            wr(P, t, (uint8_t)(rd(P, t) | (1 << (op >> 5))));
            break;
        }
        case 0x12: case 0x32: case 0x52: case 0x72:
        case 0x92: case 0xb2: case 0xd2: case 0xf2: {          // CLR1 dp.bit
            const uint16_t t = addr_dp(P);
            wr(P, t, (uint8_t)(rd(P, t) & ~(1 << (op >> 5))));
            break;
        }
        case 0x03: case 0x23: case 0x43: case 0x63:
        case 0x83: case 0xa3: case 0xc3: case 0xe3: {          // BBS dp.bit,rel
            const uint8_t v = rd(P, addr_dp(P));
            branch(P, (v & (1 << (op >> 5))) != 0, &cycles);
            break;
        }
        case 0x13: case 0x33: case 0x53: case 0x73:
        case 0x93: case 0xb3: case 0xd3: case 0xf3: {          // BBC dp.bit,rel
            const uint8_t v = rd(P, addr_dp(P));
            branch(P, (v & (1 << (op >> 5))) == 0, &cycles);
            break;
        }

        // --- carry/bit against an absolute bit address ---
        case 0x4a: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // AND1 C,m.b
                     P->psw_.c = P->psw_.c && ((rd(P, t) >> b) & 1); break; }
        case 0x6a: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // AND1 C,/m.b
                     P->psw_.c = P->psw_.c && !((rd(P, t) >> b) & 1); break; }
        case 0x0a: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // OR1
                     P->psw_.c = P->psw_.c || ((rd(P, t) >> b) & 1); break; }
        case 0x2a: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // OR1 /m.b
                     P->psw_.c = P->psw_.c || !((rd(P, t) >> b) & 1); break; }
        case 0x8a: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // EOR1
                     P->psw_.c = P->psw_.c != (((rd(P, t) >> b) & 1) != 0); break; }
        case 0xaa: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // MOV1 C,m.b
                     P->psw_.c = ((rd(P, t) >> b) & 1) != 0; break; }
        case 0xca: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // MOV1 m.b,C
                     const uint8_t v = rd(P, t);
                     wr(P, t, (uint8_t)(P->psw_.c ? (v | (1 << b)) : (v & ~(1 << b)))); break; }
        case 0xea: { uint8_t b; const uint16_t t = bit_addr(P, &b);           // NOT1 m.b
                     wr(P, t, (uint8_t)(rd(P, t) ^ (1 << b))); break; }

        // --- TSET1 / TCLR1 ---
        case 0x0e: { const uint16_t t = addr_abs(P); const uint8_t v = rd(P, t);
                     setnz(P, (uint8_t)(P->a - v)); wr(P, t, (uint8_t)(v | P->a)); break; }
        case 0x4e: { const uint16_t t = addr_abs(P); const uint8_t v = rd(P, t);
                     setnz(P, (uint8_t)(P->a - v)); wr(P, t, (uint8_t)(v & ~P->a)); break; }

        default:
            // Any remaining encoding behaves as P->a no-operation rather than
            // stopping the core dead.
            break;
    }
    return cycles;
}

