#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "arm7tdmi.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void switch_mode(arm7tdmi *P, uint32_t mode);
static uint32_t spsr(arm7tdmi *P);
static void set_spsr(arm7tdmi *P, uint32_t value);
static void write_pc(arm7tdmi *P, uint32_t value);
static void exception(arm7tdmi *P, uint32_t vector, uint32_t mode, uint32_t return_address, bool disable_fiq);
static bool condition(arm7tdmi *P, uint32_t cond);
static uint32_t add_flags(arm7tdmi *P, uint32_t a, uint32_t b, uint32_t carry_in, bool set);
static uint32_t sub_flags(arm7tdmi *P, uint32_t a, uint32_t b, uint32_t carry_in, bool set);
static uint32_t shift(arm7tdmi *P, uint32_t type, uint32_t value, uint32_t amount, bool by_register, bool* carry);
static uint32_t read_word_rotated(arm7tdmi *P, uint32_t address);
static void multiply_cycles(arm7tdmi *P, uint32_t rs, bool sign_extend);
static void exec_arm(arm7tdmi *P, uint32_t op);
static void arm_data_processing(arm7tdmi *P, uint32_t op);
static void arm_psr_transfer(arm7tdmi *P, uint32_t op);
static void arm_multiply(arm7tdmi *P, uint32_t op);
static void arm_multiply_long(arm7tdmi *P, uint32_t op);
static void arm_swap(arm7tdmi *P, uint32_t op);
static void arm_halfword(arm7tdmi *P, uint32_t op);
static void arm_single_transfer(arm7tdmi *P, uint32_t op);
static uint32_t* user_reg_ptr(arm7tdmi *P, int bank, int i);
static void arm_block_transfer(arm7tdmi *P, uint32_t op);
static void exec_thumb(arm7tdmi *P, uint16_t op);


static inline uint32_t ror32(uint32_t value, uint32_t amount) {
    amount &= 31;
    return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

static inline int popcount16(uint32_t v) {
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}
static Bank bank_of(arm7tdmi *P, uint32_t mode) {
    switch (mode & 0x1F) {
        case kFiq: return kBankFiq;
        case kIrq: return kBankIrq;
        case kSvc: return kBankSvc;
        case kAbort: return kBankAbort;
        case kUndefined: return kBankUndefined;
        default: return kBankUser;
    }
}
static inline void set_nz(arm7tdmi *P, uint32_t value)
{
    P->n_ = (value >> 31) != 0;
    P->z_ = value == 0;
}

void arm7_init(arm7tdmi *P)
{
    const arm_bus bus = P->bus_;
    memset(P, 0, sizeof *P);
    P->bus_ = bus;
    P->i_ = P->f_ = true;
    P->mode_ = kSvc;
}

void arm7_reset(arm7tdmi *P) {
    FILL(P->r_, 0);
    FILL(P->bank_r13_, 0);
    FILL(P->bank_r14_, 0);
    FILL(P->bank_spsr_, 0);
    FILL(P->fiq_r8_12_, 0);
    FILL(P->user_r8_12_, 0);
    P->n_ = P->z_ = P->c_ = P->v_ = false;
    P->mode_ = kSvc;
    P->i_ = P->f_ = true;
    P->t_ = false;
    P->pc_ = 0;
    P->r_[15] = 8;
    P->irq_line_ = false;
}
static void switch_mode(arm7tdmi *P, uint32_t mode) {
    mode = (mode & 0x1F) | 0x10;
    const Bank from = bank_of(P, P->mode_);
    const Bank to = bank_of(P, mode);
    if (from != to) {
        P->bank_r13_[from] = P->r_[13];
        P->bank_r14_[from] = P->r_[14];
        if (from == kBankFiq || to == kBankFiq) {
            uint32_t* save = from == kBankFiq ? P->fiq_r8_12_ : P->user_r8_12_;
            for (int i = 0; i < 5; i++) save[(size_t)(i)] = P->r_[(size_t)(8 + i)];
            const uint32_t* load = to == kBankFiq ? P->fiq_r8_12_ : P->user_r8_12_;
            for (int i = 0; i < 5; i++) P->r_[(size_t)(8 + i)] = load[(size_t)(i)];
        }
        P->r_[13] = P->bank_r13_[to];
        P->r_[14] = P->bank_r14_[to];
    }
    P->mode_ = mode;
}
uint32_t arm7_cpsr(arm7tdmi *P) {
    return (P->n_ ? 0x80000000u : 0) | (P->z_ ? 0x40000000u : 0) | (P->c_ ? 0x20000000u : 0) |
           (P->v_ ? 0x10000000u : 0) | (P->i_ ? 0x80u : 0) | (P->f_ ? 0x40u : 0) | (P->t_ ? 0x20u : 0) | P->mode_;
}
void arm7_set_cpsr(arm7tdmi *P, uint32_t value) {
    P->n_ = (value >> 31) & 1;
    P->z_ = (value >> 30) & 1;
    P->c_ = (value >> 29) & 1;
    P->v_ = (value >> 28) & 1;
    P->i_ = (value >> 7) & 1;
    P->f_ = (value >> 6) & 1;
    P->t_ = (value >> 5) & 1;
    if (((value & 0x1F) | 0x10) != P->mode_) switch_mode(P, value);
}
static uint32_t spsr(arm7tdmi *P) {
    const Bank b = bank_of(P, P->mode_);
    return b == kBankUser ? arm7_cpsr(P) : P->bank_spsr_[b];
}
static void set_spsr(arm7tdmi *P, uint32_t value) {
    const Bank b = bank_of(P, P->mode_);
    if (b != kBankUser) P->bank_spsr_[b] = value;
}
void arm7_set_reg(arm7tdmi *P, int n, uint32_t value) {
    if (n == 15) {
        P->pc_ = value & (P->t_ ? ~1u : ~3u);
        P->r_[15] = P->pc_;
    } else {
        P->r_[(size_t)(n)] = value;
    }
}
void arm7_set_pc(arm7tdmi *P, uint32_t value, bool thumb) {
    P->t_ = thumb;
    arm7_set_reg(P, 15, value);
}
static void write_pc(arm7tdmi *P, uint32_t value) {
    P->pc_ = value & (P->t_ ? ~1u : ~3u);
    P->r_[15] = P->pc_;
    P->branched_ = true;
    P->bus_.idle(P->bus_.user, 2);  // pipeline refill
}
static void exception(arm7tdmi *P, uint32_t vector, uint32_t mode, uint32_t return_address, bool disable_fiq) {
    const uint32_t saved = arm7_cpsr(P);
    switch_mode(P, mode);
    set_spsr(P, saved);
    P->r_[14] = return_address;
    P->t_ = false;
    P->i_ = true;
    if (disable_fiq) P->f_ = true;
    write_pc(P, vector);
}
static bool condition(arm7tdmi *P, uint32_t cond) {
    switch (cond & 15) {
        case 0x0: return P->z_;
        case 0x1: return !P->z_;
        case 0x2: return P->c_;
        case 0x3: return !P->c_;
        case 0x4: return P->n_;
        case 0x5: return !P->n_;
        case 0x6: return P->v_;
        case 0x7: return !P->v_;
        case 0x8: return P->c_ && !P->z_;
        case 0x9: return !P->c_ || P->z_;
        case 0xA: return P->n_ == P->v_;
        case 0xB: return P->n_ != P->v_;
        case 0xC: return !P->z_ && P->n_ == P->v_;
        case 0xD: return P->z_ || P->n_ != P->v_;
        case 0xE: return true;
        default: return false;  // NV: never on ARMv4
    }
}
void arm7_step(arm7tdmi *P) {
    if (P->irq_line_ && !P->i_) {
        // LR_irq = next instruction + 4 (SUBS PC, LR, #4 returns), both states.
        exception(P, 0x18, kIrq, P->pc_ + 4, false);
        return;
    }
    P->exec_addr_ = P->pc_;
    P->branched_ = false;
    if (P->t_) {
        const uint16_t op = P->bus_.read16(P->bus_.user, P->pc_, true);
        P->r_[15] = P->pc_ + 4;
        exec_thumb(P, op);
        if (!P->branched_) P->pc_ = P->exec_addr_ + 2;
    } else {
        const uint32_t op = P->bus_.read32(P->bus_.user, P->pc_, true);
        P->r_[15] = P->pc_ + 8;
        if (condition(P, op >> 28)) exec_arm(P, op);
        if (!P->branched_) P->pc_ = P->exec_addr_ + 4;
    }
}
static uint32_t add_flags(arm7tdmi *P, uint32_t a, uint32_t b, uint32_t carry_in, bool set) {
    const uint64_t wide = (uint64_t)(a) + (uint64_t)(b) + (uint64_t)(carry_in);
    const uint32_t result = (uint32_t)(wide);
    if (set) {
        set_nz(P, result);
        P->c_ = (wide >> 32) != 0;
        P->v_ = ((~(a ^ b) & (a ^ result)) >> 31) != 0;
    }
    return result;
}
static uint32_t sub_flags(arm7tdmi *P, uint32_t a, uint32_t b, uint32_t carry_in, bool set) {
    // a - b - !carry == a + ~b + carry; C is "no borrow".
    return add_flags(P, a, ~b, carry_in, set);
}
static uint32_t shift(arm7tdmi *P, uint32_t type, uint32_t value, uint32_t amount, bool by_register, bool* carry) {
    if (by_register) {
        if (amount == 0) return value;
        switch (type) {
            case 0:  // LSL
                if (amount < 32) {
                    *carry = (value >> (32 - amount)) & 1;
                    return value << amount;
                }
                *carry = amount == 32 ? (value & 1) : false;
                return 0;
            case 1:  // LSR
                if (amount < 32) {
                    *carry = (value >> (amount - 1)) & 1;
                    return value >> amount;
                }
                *carry = amount == 32 ? (value >> 31) : false;
                return 0;
            case 2:  // ASR
                if (amount < 32) {
                    *carry = ((uint32_t)((int32_t)(value) >> (amount - 1))) & 1;
                    return (uint32_t)((int32_t)(value) >> amount);
                }
                *carry = value >> 31;
                return *carry ? 0xFFFFFFFFu : 0;
            default: {  // ROR
                const uint32_t r = ror32(value, amount);
                *carry = r >> 31;
                return r;
            }
        }
    }
    switch (type) {
        case 0:
            if (amount == 0) return value;
            *carry = (value >> (32 - amount)) & 1;
            return value << amount;
        case 1:
            if (amount == 0) {
                *carry = value >> 31;
                return 0;
            }
            *carry = (value >> (amount - 1)) & 1;
            return value >> amount;
        case 2:
            if (amount == 0) {
                *carry = value >> 31;
                return *carry ? 0xFFFFFFFFu : 0;
            }
            *carry = ((uint32_t)((int32_t)(value) >> (amount - 1))) & 1;
            return (uint32_t)((int32_t)(value) >> amount);
        default:
            if (amount == 0) {  // RRX
                const bool out = value & 1;
                const uint32_t r = (P->c_ ? 0x80000000u : 0) | (value >> 1);
                *carry = out;
                return r;
            }
            {
                const uint32_t r = ror32(value, amount);
                *carry = r >> 31;
                return r;
            }
    }
}
static uint32_t read_word_rotated(arm7tdmi *P, uint32_t address) {
    // The bus sees the unaligned address (8-bit SRAM uses the low bits).
    const uint32_t v = P->bus_.read32(P->bus_.user, address, false);
    return ror32(v, (address & 3) * 8);
}
static void multiply_cycles(arm7tdmi *P, uint32_t rs, bool sign_extend) {
    int m = 4;
    if ((rs >> 8) == 0 || (sign_extend && (rs >> 8) == 0xFFFFFF)) m = 1;
    else if ((rs >> 16) == 0 || (sign_extend && (rs >> 16) == 0xFFFF)) m = 2;
    else if ((rs >> 24) == 0 || (sign_extend && (rs >> 24) == 0xFF)) m = 3;
    P->bus_.idle(P->bus_.user, m);
}

// ---------------------------------------------------------------------------
// ARM
// ---------------------------------------------------------------------------
static void exec_arm(arm7tdmi *P, uint32_t op) {
    if ((op & 0x0FFFFFF0u) == 0x012FFF10u) {  // BX
        const uint32_t target = P->r_[op & 15];
        P->t_ = (target & 1) != 0;
        write_pc(P, target);
        return;
    }
    if ((op & 0x0E000000u) == 0x0A000000u) {  // B / BL
        int32_t offset = (int32_t)(op << 8) >> 6;
        if (op & (1u << 24)) P->r_[14] = P->exec_addr_ + 4;
        write_pc(P, P->r_[15] + (uint32_t)(offset));
        return;
    }
    if ((op & 0x0F000000u) == 0x0F000000u) {  // SWI
        exception(P, 0x08, kSvc, P->exec_addr_ + 4, false);
        return;
    }
    if ((op & 0x0C000000u) == 0x04000000u) {
        if ((op & 0x02000010u) == 0x02000010u) {  // undefined
            exception(P, 0x04, kUndefined, P->exec_addr_ + 4, false);
            return;
        }
        arm_single_transfer(P, op);
        return;
    }
    if ((op & 0x0E000000u) == 0x08000000u) {
        arm_block_transfer(P, op);
        return;
    }
    if ((op & 0x0C000000u) == 0x00000000u) {
        if ((op & 0x0FC000F0u) == 0x00000090u) return arm_multiply(P, op);
        if ((op & 0x0F8000F0u) == 0x00800090u) return arm_multiply_long(P, op);
        if ((op & 0x0FB00FF0u) == 0x01000090u) return arm_swap(P, op);
        if ((op & 0x0E000090u) == 0x00000090u && (op & 0x60u) != 0) return arm_halfword(P, op);
        if ((op & 0x0FBF0FFFu) == 0x010F0000u || (op & 0x0DB0F000u) == 0x0120F000u) {
            return arm_psr_transfer(P, op);
        }
        arm_data_processing(P, op);
        return;
    }
    // Coprocessor instructions: no coprocessor on the GBA.
    exception(P, 0x04, kUndefined, P->exec_addr_ + 4, false);
}
static void arm_data_processing(arm7tdmi *P, uint32_t op) {
    const uint32_t opcode = (op >> 21) & 15;
    const bool s = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    const uint32_t rd = (op >> 12) & 15;
    bool carry = P->c_;
    uint32_t op2;
    uint32_t a = P->r_[rn];
    if (op & (1u << 25)) {
        const uint32_t rot = (op >> 7) & 0x1E;
        op2 = ror32(op & 0xFF, rot);
        if (rot) carry = op2 >> 31;
    } else {
        const uint32_t rm = op & 15;
        const uint32_t type = (op >> 5) & 3;
        uint32_t value = P->r_[rm];
        if (op & 0x10) {
            // Register-specified shift: one extra cycle, r15 reads + 12.
            P->bus_.idle(P->bus_.user, 1);
            if (rm == 15) value += 4;
            if (rn == 15) a += 4;
            op2 = shift(P, type, value, P->r_[(op >> 8) & 15] & 0xFF, true, &carry);
        } else {
            op2 = shift(P, type, value, (op >> 7) & 31, false, &carry);
        }
    }
    const bool flags = s && rd != 15;
    uint32_t result = 0;
    bool write = true;
    switch (opcode) {
        case 0x0: result = a & op2; break;
        case 0x1: result = a ^ op2; break;
        case 0x2: result = sub_flags(P, a, op2, 1, flags); break;
        case 0x3: result = sub_flags(P, op2, a, 1, flags); break;
        case 0x4: result = add_flags(P, a, op2, 0, flags); break;
        case 0x5: result = add_flags(P, a, op2, P->c_ ? 1 : 0, flags); break;
        case 0x6: result = sub_flags(P, a, op2, P->c_ ? 1 : 0, flags); break;
        case 0x7: result = sub_flags(P, op2, a, P->c_ ? 1 : 0, flags); break;
        case 0x8: result = a & op2; write = false; break;
        case 0x9: result = a ^ op2; write = false; break;
        case 0xA: result = sub_flags(P, a, op2, 1, s); write = false; break;
        case 0xB: result = add_flags(P, a, op2, 0, s); write = false; break;
        case 0xC: result = a | op2; break;
        case 0xD: result = op2; break;
        case 0xE: result = a & ~op2; break;
        default: result = ~op2; break;
    }
    const bool logical = opcode <= 1 || (opcode >= 8 && opcode <= 9) || opcode >= 0xC;
    if (logical && (flags || (!write && s))) {
        set_nz(P, result);
        P->c_ = carry;
    }
    if (!write) {
        if (rd == 15 && s) arm7_set_cpsr(P, spsr(P));  // TSTP & co (ARMv2 leftovers)
        return;
    }
    if (rd == 15) {
        if (s) arm7_set_cpsr(P, spsr(P));  // exception return: restores mode and T
        write_pc(P, result);
    } else {
        P->r_[rd] = result;
    }
}
static void arm_psr_transfer(arm7tdmi *P, uint32_t op) {
    const bool use_spsr = (op >> 22) & 1;
    if ((op & 0x0FBF0FFFu) == 0x010F0000u) {  // MRS
        P->r_[(op >> 12) & 15] = use_spsr ? spsr(P) : arm7_cpsr(P);
        return;
    }
    uint32_t value;
    if (op & (1u << 25)) value = ror32(op & 0xFF, (op >> 7) & 0x1E);
    else value = P->r_[op & 15];
    uint32_t mask = 0;
    if (op & (1u << 19)) mask |= 0xFF000000u;
    if (op & (1u << 16)) mask |= 0x000000FFu;
    if (use_spsr) {
        set_spsr(P, (spsr(P) & ~mask) | (value & mask));
        return;
    }
    if (P->mode_ == kUser) mask &= 0xFF000000u;
    uint32_t next = (arm7_cpsr(P) & ~mask) | (value & mask);
    next = (next & ~0x20u) | (P->t_ ? 0x20u : 0);  // MSR cannot change the state
    arm7_set_cpsr(P, next);
}
static void arm_multiply(arm7tdmi *P, uint32_t op) {
    const uint32_t rd = (op >> 16) & 15;
    const uint32_t rs = P->r_[(op >> 8) & 15];
    uint32_t result = P->r_[op & 15] * rs;
    multiply_cycles(P, rs, true);
    if (op & (1u << 21)) {
        result += P->r_[(op >> 12) & 15];
        P->bus_.idle(P->bus_.user, 1);
    }
    P->r_[rd] = result;
    if (op & (1u << 20)) set_nz(P, result);
}
static void arm_multiply_long(arm7tdmi *P, uint32_t op) {
    const uint32_t hi = (op >> 16) & 15;
    const uint32_t lo = (op >> 12) & 15;
    const uint32_t rs = P->r_[(op >> 8) & 15];
    const uint32_t rm = P->r_[op & 15];
    const bool sign = (op >> 22) & 1;
    uint64_t result;
    if (sign) result = (uint64_t)((int64_t)((int32_t)(rm)) * (int64_t)((int32_t)(rs)));
    else result = (uint64_t)(rm) * (uint64_t)(rs);
    multiply_cycles(P, rs, sign);
    P->bus_.idle(P->bus_.user, 1);
    if (op & (1u << 21)) {
        result += ((uint64_t)(P->r_[hi]) << 32) | P->r_[lo];
        P->bus_.idle(P->bus_.user, 1);
    }
    P->r_[lo] = (uint32_t)(result);
    P->r_[hi] = (uint32_t)(result >> 32);
    if (op & (1u << 20)) {
        P->n_ = (result >> 63) != 0;
        P->z_ = result == 0;
    }
}
static void arm_swap(arm7tdmi *P, uint32_t op) {
    const uint32_t address = P->r_[(op >> 16) & 15];
    const uint32_t rd = (op >> 12) & 15;
    const uint32_t source = P->r_[op & 15];
    if (op & (1u << 22)) {
        const uint8_t old = P->bus_.read8(P->bus_.user, address);
        P->bus_.write8(P->bus_.user, address, (uint8_t)(source));
        P->r_[rd] = old;
    } else {
        const uint32_t old = read_word_rotated(P, address);
        P->bus_.write32(P->bus_.user, address & ~3u, source);
        P->r_[rd] = old;
    }
    P->bus_.idle(P->bus_.user, 1);
}
static void arm_halfword(arm7tdmi *P, uint32_t op) {
    const bool pre = (op >> 24) & 1;
    const bool up = (op >> 23) & 1;
    const bool writeback = !pre || ((op >> 21) & 1);
    const bool load = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    const uint32_t rd = (op >> 12) & 15;
    const uint32_t sh = (op >> 5) & 3;
    const uint32_t offset = (op & (1u << 22)) ? (((op >> 4) & 0xF0) | (op & 0x0F)) : P->r_[op & 15];
    const uint32_t base = P->r_[rn];
    const uint32_t moved = up ? base + offset : base - offset;
    const uint32_t address = pre ? moved : base;
    if (load) {
        uint32_t value;
        if (sh == 1) {
            value = ror32(P->bus_.read16(P->bus_.user, address, false), (address & 1) * 8);
        } else if (sh == 2) {
            value = (uint32_t)((int32_t)((int8_t)(P->bus_.read8(P->bus_.user, address))));
        } else if (address & 1) {
            value = (uint32_t)((int32_t)((int8_t)(P->bus_.read8(P->bus_.user, address))));  // misaligned LDRSH
        } else {
            value = (uint32_t)((int32_t)((int16_t)(P->bus_.read16(P->bus_.user, address, false))));
        }
        P->bus_.idle(P->bus_.user, 1);
        if (writeback && rn != rd && rn != 15) P->r_[rn] = moved;
        if (rd == 15) write_pc(P, value);
        else P->r_[rd] = value;
    } else {
        if (sh == 1) {
            uint32_t value = P->r_[rd];
            if (rd == 15) value += 4;
            P->bus_.write16(P->bus_.user, address, (uint16_t)(value));
        }
        // sh 2/3 with L=0 are ARMv5 LDRD/STRD: no effect on ARMv4.
        if (writeback && rn != 15) P->r_[rn] = moved;
    }
}
static void arm_single_transfer(arm7tdmi *P, uint32_t op) {
    const bool reg_offset = (op >> 25) & 1;
    const bool pre = (op >> 24) & 1;
    const bool up = (op >> 23) & 1;
    const bool byte = (op >> 22) & 1;
    const bool writeback = !pre || ((op >> 21) & 1);
    const bool load = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    const uint32_t rd = (op >> 12) & 15;
    uint32_t offset;
    if (reg_offset) {
        bool dummy = P->c_;
        offset = shift(P, (op >> 5) & 3, P->r_[op & 15], (op >> 7) & 31, false, &dummy);
    } else {
        offset = op & 0xFFF;
    }
    const uint32_t base = P->r_[rn];
    const uint32_t moved = up ? base + offset : base - offset;
    const uint32_t address = pre ? moved : base;
    if (load) {
        const uint32_t value = byte ? P->bus_.read8(P->bus_.user, address) : read_word_rotated(P, address);
        P->bus_.idle(P->bus_.user, 1);
        if (writeback && rn != rd && rn != 15) P->r_[rn] = moved;
        if (rd == 15) write_pc(P, value);
        else P->r_[rd] = value;
    } else {
        uint32_t value = P->r_[rd];
        if (rd == 15) value += 4;  // STR PC stores the instruction + 12
        if (byte) P->bus_.write8(P->bus_.user, address, (uint8_t)(value));
        else P->bus_.write32(P->bus_.user, address, value);
        if (writeback && rn != 15) P->r_[rn] = moved;
    }
}
static uint32_t* user_reg_ptr(arm7tdmi *P, int bank, int i) {
    if (i >= 8 && i <= 12 && bank == kBankFiq) return &P->user_r8_12_[(size_t)(i - 8)];
    if ((i == 13 || i == 14) && bank != kBankUser) {
        return i == 13 ? &P->bank_r13_[kBankUser] : &P->bank_r14_[kBankUser];
    }
    return &P->r_[(size_t)(i)];
}
static void arm_block_transfer(arm7tdmi *P, uint32_t op) {
    const bool pre = (op >> 24) & 1;
    const bool up = (op >> 23) & 1;
    const bool psr = (op >> 22) & 1;
    const bool writeback = (op >> 21) & 1;
    const bool load = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    uint32_t list = op & 0xFFFF;
    int count = popcount16(list);
    if (list == 0) {  // ARMv4 quirk: transfers r15, base moves by 0x40
        list = 0x8000;
        count = 16;
    }
    const uint32_t base = P->r_[rn];
    uint32_t address;
    uint32_t final_base;
    if (up) {
        address = base + (pre ? 4 : 0);
        final_base = base + (uint32_t)(count) * 4;
    } else {
        address = base - (uint32_t)(count) * 4 + (pre ? 0 : 4);
        final_base = base - (uint32_t)(count) * 4;
    }
    // S bit without r15 in an LDM (or any STM): user-bank registers.
    const bool user_bank = psr && !(load && (list & 0x8000));
    const Bank bank = bank_of(P, P->mode_);

    if (load) {
        P->bus_.idle(P->bus_.user, 1);
        if (writeback && !(list & (1u << rn))) P->r_[rn] = final_base;
        for (int i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            const uint32_t value = P->bus_.read32(P->bus_.user, address & ~3u, false);
            address += 4;
            if (i == 15) {
                if (psr) arm7_set_cpsr(P, spsr(P));
                write_pc(P, value);
            } else if (user_bank) {
                (*user_reg_ptr(P, bank, i)) = value;
            } else {
                P->r_[(size_t)(i)] = value;
            }
        }
        return;
    }
    bool first = true;
    for (int i = 0; i < 16; i++) {
        if (!(list & (1u << i))) continue;
        uint32_t value = user_bank ? (*user_reg_ptr(P, bank, i)) : P->r_[(size_t)(i)];
        if (i == 15) value += 4;
        P->bus_.write32(P->bus_.user, address & ~3u, value);
        address += 4;
        if (first) {
            // The base is written back after the first transfer: a base that is
            // the lowest listed register is stored unchanged, later ones see
            // the new value.
            if (writeback) P->r_[rn] = final_base;
            first = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Thumb
// ---------------------------------------------------------------------------
static void exec_thumb(arm7tdmi *P, uint16_t op) {
    switch (op >> 13) {
        case 0: {
            if (((op >> 11) & 3) == 3) {  // format 2: ADD/SUB
                const uint32_t rd = op & 7;
                const uint32_t a = P->r_[(op >> 3) & 7];
                const uint32_t b = (op & 0x0400) ? (uint32_t)((op >> 6) & 7) : P->r_[(op >> 6) & 7];
                P->r_[rd] = (op & 0x0200) ? sub_flags(P, a, b, 1, true) : add_flags(P, a, b, 0, true);
                return;
            }
            // format 1: shift by immediate
            bool carry = P->c_;
            const uint32_t result = shift(P, (op >> 11) & 3, P->r_[(op >> 3) & 7], (op >> 6) & 31, false, &carry);
            P->r_[op & 7] = result;
            set_nz(P, result);
            P->c_ = carry;
            return;
        }
        case 1: {  // format 3: MOV/CMP/ADD/SUB imm8
            const uint32_t rd = (op >> 8) & 7;
            const uint32_t imm = op & 0xFF;
            switch ((op >> 11) & 3) {
                case 0: P->r_[rd] = imm; set_nz(P, imm); break;
                case 1: sub_flags(P, P->r_[rd], imm, 1, true); break;
                case 2: P->r_[rd] = add_flags(P, P->r_[rd], imm, 0, true); break;
                default: P->r_[rd] = sub_flags(P, P->r_[rd], imm, 1, true); break;
            }
            return;
        }
        case 2: {
            if ((op & 0xFC00) == 0x4000) {  // format 4: ALU
                const uint32_t rd = op & 7;
                const uint32_t rs = P->r_[(op >> 3) & 7];
                uint32_t* dlocal = &P->r_[rd];
                bool carry = P->c_;
                switch ((op >> 6) & 15) {
                    case 0x0: (*dlocal) &= rs; set_nz(P, (*dlocal)); break;
                    case 0x1: (*dlocal) ^= rs; set_nz(P, (*dlocal)); break;
                    case 0x2: P->bus_.idle(P->bus_.user, 1); (*dlocal) = shift(P, 0, (*dlocal), rs & 0xFF, true, &carry); set_nz(P, (*dlocal)); P->c_ = carry; break;
                    case 0x3: P->bus_.idle(P->bus_.user, 1); (*dlocal) = shift(P, 1, (*dlocal), rs & 0xFF, true, &carry); set_nz(P, (*dlocal)); P->c_ = carry; break;
                    case 0x4: P->bus_.idle(P->bus_.user, 1); (*dlocal) = shift(P, 2, (*dlocal), rs & 0xFF, true, &carry); set_nz(P, (*dlocal)); P->c_ = carry; break;
                    case 0x5: (*dlocal) = add_flags(P, (*dlocal), rs, P->c_ ? 1 : 0, true); break;
                    case 0x6: (*dlocal) = sub_flags(P, (*dlocal), rs, P->c_ ? 1 : 0, true); break;
                    case 0x7: P->bus_.idle(P->bus_.user, 1); (*dlocal) = shift(P, 3, (*dlocal), rs & 0xFF, true, &carry); set_nz(P, (*dlocal)); P->c_ = carry; break;
                    case 0x8: set_nz(P, (*dlocal) & rs); break;
                    case 0x9: (*dlocal) = sub_flags(P, 0, rs, 1, true); break;
                    case 0xA: sub_flags(P, (*dlocal), rs, 1, true); break;
                    case 0xB: add_flags(P, (*dlocal), rs, 0, true); break;
                    case 0xC: (*dlocal) |= rs; set_nz(P, (*dlocal)); break;
                    case 0xD: multiply_cycles(P, (*dlocal), true); (*dlocal) *= rs; set_nz(P, (*dlocal)); break;
                    case 0xE: (*dlocal) &= ~rs; set_nz(P, (*dlocal)); break;
                    default: (*dlocal) = ~rs; set_nz(P, (*dlocal)); break;
                }
                return;
            }
            if ((op & 0xFC00) == 0x4400) {  // format 5: hi registers / BX
                const uint32_t rd = (op & 7) | ((op >> 4) & 8);
                const uint32_t rs = (op >> 3) & 15;
                const uint32_t value = P->r_[rs];
                switch ((op >> 8) & 3) {
                    case 0:
                        if (rd == 15) write_pc(P, P->r_[15] + value);
                        else P->r_[rd] += value;
                        break;
                    case 1: sub_flags(P, P->r_[rd], value, 1, true); break;
                    case 2:
                        if (rd == 15) write_pc(P, value);
                        else P->r_[rd] = value;
                        break;
                    default:
                        P->t_ = (value & 1) != 0;
                        write_pc(P, value);
                        break;
                }
                return;
            }
            if ((op & 0xF800) == 0x4800) {  // format 6: LDR PC-relative
                P->r_[(op >> 8) & 7] = P->bus_.read32(P->bus_.user, (P->r_[15] & ~2u) + (op & 0xFF) * 4u, false);
                P->bus_.idle(P->bus_.user, 1);
                return;
            }
            // formats 7/8: register offset
            const uint32_t address = P->r_[(op >> 3) & 7] + P->r_[(op >> 6) & 7];
            const uint32_t rd = op & 7;
            if (!(op & 0x0200)) {
                switch ((op >> 10) & 3) {
                    case 0: P->bus_.write32(P->bus_.user, address, P->r_[rd]); break;
                    case 1: P->bus_.write8(P->bus_.user, address, (uint8_t)(P->r_[rd])); break;
                    case 2: P->r_[rd] = read_word_rotated(P, address); P->bus_.idle(P->bus_.user, 1); break;
                    default: P->r_[rd] = P->bus_.read8(P->bus_.user, address); P->bus_.idle(P->bus_.user, 1); break;
                }
            } else {
                switch ((op >> 10) & 3) {
                    case 0: P->bus_.write16(P->bus_.user, address, (uint16_t)(P->r_[rd])); break;
                    case 1: P->r_[rd] = (uint32_t)((int32_t)((int8_t)(P->bus_.read8(P->bus_.user, address)))); P->bus_.idle(P->bus_.user, 1); break;
                    case 2:
                        P->r_[rd] = ror32(P->bus_.read16(P->bus_.user, address, false), (address & 1) * 8);
                        P->bus_.idle(P->bus_.user, 1);
                        break;
                    default:
                        if (address & 1) P->r_[rd] = (uint32_t)((int32_t)((int8_t)(P->bus_.read8(P->bus_.user, address))));
                        else P->r_[rd] = (uint32_t)((int32_t)((int16_t)(P->bus_.read16(P->bus_.user, address, false))));
                        P->bus_.idle(P->bus_.user, 1);
                        break;
                }
            }
            return;
        }
        case 3: {  // format 9: immediate offset
            const uint32_t rd = op & 7;
            const uint32_t base = P->r_[(op >> 3) & 7];
            const uint32_t imm = (op >> 6) & 31;
            const bool byte = (op >> 12) & 1;
            const bool load = (op >> 11) & 1;
            const uint32_t address = base + (byte ? imm : imm * 4);
            if (load) {
                P->r_[rd] = byte ? P->bus_.read8(P->bus_.user, address) : read_word_rotated(P, address);
                P->bus_.idle(P->bus_.user, 1);
            } else if (byte) {
                P->bus_.write8(P->bus_.user, address, (uint8_t)(P->r_[rd]));
            } else {
                P->bus_.write32(P->bus_.user, address, P->r_[rd]);
            }
            return;
        }
        case 4: {
            if (!(op & 0x1000)) {  // format 10: halfword immediate
                const uint32_t rd = op & 7;
                const uint32_t address = P->r_[(op >> 3) & 7] + ((op >> 6) & 31) * 2u;
                if (op & 0x0800) {
                    P->r_[rd] = ror32(P->bus_.read16(P->bus_.user, address, false), (address & 1) * 8);
                    P->bus_.idle(P->bus_.user, 1);
                } else {
                    P->bus_.write16(P->bus_.user, address, (uint16_t)(P->r_[rd]));
                }
                return;
            }
            // format 11: SP-relative
            const uint32_t rd = (op >> 8) & 7;
            const uint32_t address = P->r_[13] + (op & 0xFF) * 4u;
            if (op & 0x0800) {
                P->r_[rd] = read_word_rotated(P, address);
                P->bus_.idle(P->bus_.user, 1);
            } else {
                P->bus_.write32(P->bus_.user, address, P->r_[rd]);
            }
            return;
        }
        case 5: {
            if (!(op & 0x1000)) {  // format 12: load address
                const uint32_t rd = (op >> 8) & 7;
                const uint32_t base = (op & 0x0800) ? P->r_[13] : (P->r_[15] & ~2u);
                P->r_[rd] = base + (op & 0xFF) * 4u;
                return;
            }
            if ((op & 0xFF00) == 0xB000) {  // format 13: ADD SP
                const uint32_t imm = (op & 0x7F) * 4u;
                P->r_[13] = (op & 0x80) ? P->r_[13] - imm : P->r_[13] + imm;
                return;
            }
            if ((op & 0xF600) == 0xB400) {  // format 14: PUSH/POP
                const bool pop = (op >> 11) & 1;
                const bool extra = (op >> 8) & 1;
                const uint32_t list = op & 0xFF;
                if (pop) {
                    uint32_t address = P->r_[13];
                    P->bus_.idle(P->bus_.user, 1);
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        P->r_[(size_t)(i)] = P->bus_.read32(P->bus_.user, address & ~3u, false);
                        address += 4;
                    }
                    if (extra) {
                        const uint32_t value = P->bus_.read32(P->bus_.user, address & ~3u, false);
                        address += 4;
                        P->r_[13] = address;
                        write_pc(P, value);  // ARMv4: stays in Thumb
                        return;
                    }
                    P->r_[13] = address;
                } else {
                    const int count = popcount16(list) + (extra ? 1 : 0);
                    uint32_t address = P->r_[13] - (uint32_t)(count) * 4;
                    P->r_[13] = address;
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        P->bus_.write32(P->bus_.user, address & ~3u, P->r_[(size_t)(i)]);
                        address += 4;
                    }
                    if (extra) P->bus_.write32(P->bus_.user, address & ~3u, P->r_[14]);
                }
                return;
            }
            // Unused (ARMv5 BKPT etc.)
            exception(P, 0x04, kUndefined, P->exec_addr_ + 2, false);
            return;
        }
        case 6: {
            if (!(op & 0x1000)) {  // format 15: LDMIA/STMIA
                const uint32_t rb = (op >> 8) & 7;
                uint32_t list = op & 0xFF;
                uint32_t address = P->r_[rb];
                if (list == 0) {  // ARMv4 quirk: r15, base += 0x40
                    if (op & 0x0800) {
                        const uint32_t value = P->bus_.read32(P->bus_.user, address & ~3u, false);
                        P->r_[rb] = address + 0x40;
                        write_pc(P, value);
                    } else {
                        P->bus_.write32(P->bus_.user, address & ~3u, P->r_[15] + 2);
                        P->r_[rb] = address + 0x40;
                    }
                    return;
                }
                const uint32_t final_base = address + (uint32_t)(popcount16(list)) * 4;
                if (op & 0x0800) {
                    P->bus_.idle(P->bus_.user, 1);
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        P->r_[(size_t)(i)] = P->bus_.read32(P->bus_.user, address & ~3u, false);
                        address += 4;
                    }
                    if (!(list & (1u << rb))) P->r_[rb] = final_base;
                } else {
                    bool first = true;
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        P->bus_.write32(P->bus_.user, address & ~3u, P->r_[(size_t)(i)]);
                        address += 4;
                        if (first) {
                            P->r_[rb] = final_base;
                            first = false;
                        }
                    }
                }
                return;
            }
            const uint32_t cond = (op >> 8) & 15;
            if (cond == 15) {  // format 17: SWI
                exception(P, 0x08, kSvc, P->exec_addr_ + 2, false);
                return;
            }
            if (cond == 14) {
                exception(P, 0x04, kUndefined, P->exec_addr_ + 2, false);
                return;
            }
            if (condition(P, cond)) {  // format 16
                const int32_t offset = (int32_t)((int8_t)(op & 0xFF)) * 2;
                write_pc(P, P->r_[15] + (uint32_t)(offset));
            }
            return;
        }
        default: {
            if (!(op & 0x1000)) {
                if (op & 0x0800) {  // ARMv5 BLX suffix: undefined here
                    exception(P, 0x04, kUndefined, P->exec_addr_ + 2, false);
                    return;
                }
                const int32_t offset = ((int32_t)((uint32_t)(op) << 21) >> 20);  // format 18
                write_pc(P, P->r_[15] + (uint32_t)(offset));
                return;
            }
            // format 19: BL, two halves
            if (!(op & 0x0800)) {
                const int32_t offset = (int32_t)((uint32_t)(op) << 21) >> 9;
                P->r_[14] = P->r_[15] + (uint32_t)(offset);
            } else {
                const uint32_t next = P->exec_addr_ + 2;
                write_pc(P, P->r_[14] + ((uint32_t)(op & 0x7FF) << 1));
                P->r_[14] = next | 1;
            }
            return;
        }
    }
}

