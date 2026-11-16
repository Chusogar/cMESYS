#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "r3000a.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

enum { EX_INTERRUPT = 0x0, EX_LOAD_ADDRESS_ERROR = 0x4, EX_STORE_ADDRESS_ERROR = 0x5, EX_BUS_ERROR_FETCH = 0x6,
       EX_SYSCALL = 0x8, EX_BREAK = 0x9, EX_ILLEGAL_INSTR = 0xA, EX_COPROCESSOR_ERROR = 0xB, EX_OVERFLOW = 0xC };
enum { SR = 12, CAUSE = 13, EPC = 14, BADA = 8, JUMPDEST = 6 };
static inline uint32_t I_opcode(uint32_t v) { return v >> 26; }
static inline uint32_t I_rs(uint32_t v) { return (v >> 21) & 0x1F; }
static inline uint32_t I_rt(uint32_t v) { return (v >> 16) & 0x1F; }
static inline uint32_t I_imm(uint32_t v) { return v & 0xFFFFu; }
static inline uint32_t I_imm_s(uint32_t v) { return (uint32_t)(int16_t)v; }
static inline uint32_t I_rd(uint32_t v) { return (v >> 11) & 0x1F; }
static inline uint32_t I_sa(uint32_t v) { return (v >> 6) & 0x1F; }
static inline uint32_t I_function(uint32_t v) { return v & 0x3F; }
static inline uint32_t I_addr(uint32_t v) { return v & 0x3FFFFFFu; }
static inline uint32_t I_id(uint32_t v) { return I_opcode(v) & 0x3; }
static void op_nop(r3000a *P) { (void)P; }
static int fetch_decode(r3000a *P);
static void mem_access(r3000a *P);
static void write_back(r3000a *P);
static void set_gpr_delayed(r3000a *P, uint32_t reg, uint32_t value);
static void delayed_load(r3000a *P, uint32_t reg, uint32_t value);
static void exception(r3000a *P, uint32_t cause, uint32_t coprocessor);
static void branch(r3000a *P);
static uint32_t load8(r3000a *P, uint32_t addr);
static uint32_t load16(r3000a *P, uint32_t addr);
static uint32_t load32(r3000a *P, uint32_t addr);
static void store8(r3000a *P, uint32_t addr, uint8_t value);
static void store16(r3000a *P, uint32_t addr, uint16_t value);
static void store32(r3000a *P, uint32_t addr, uint32_t value);
static void op_special(r3000a *P);
static void op_na(r3000a *P);
static void op_bcond(r3000a *P);
static void op_j(r3000a *P);
static void op_jal(r3000a *P);
static void op_beq(r3000a *P);
static void op_bne(r3000a *P);
static void op_blez(r3000a *P);
static void op_bgtz(r3000a *P);
static void op_addi(r3000a *P);
static void op_addiu(r3000a *P);
static void op_slti(r3000a *P);
static void op_sltiu(r3000a *P);
static void op_andi(r3000a *P);
static void op_ori(r3000a *P);
static void op_xori(r3000a *P);
static void op_lui(r3000a *P);
static void op_cop0(r3000a *P);
static void mfc0(r3000a *P);
static void mtc0(r3000a *P);
static void rfe(r3000a *P);
static void op_cop2(r3000a *P);
static void op_lwc2(r3000a *P);
static void op_swc2(r3000a *P);
static void op_lb(r3000a *P);
static void op_lbu(r3000a *P);
static void op_lh(r3000a *P);
static void op_lhu(r3000a *P);
static void op_lw(r3000a *P);
static void op_lwl(r3000a *P);
static void op_lwr(r3000a *P);
static void op_sb(r3000a *P);
static void op_sh(r3000a *P);
static void op_sw(r3000a *P);
static void op_swr(r3000a *P);
static void op_swl(r3000a *P);
static void op_sll(r3000a *P);
static void op_srl(r3000a *P);
static void op_sra(r3000a *P);
static void op_sllv(r3000a *P);
static void op_srlv(r3000a *P);
static void op_srav(r3000a *P);
static void op_jr(r3000a *P);
static void op_jalr(r3000a *P);
static void op_syscall(r3000a *P);
static void op_break(r3000a *P);
static void op_mfhi(r3000a *P);
static void op_mthi(r3000a *P);
static void op_mflo(r3000a *P);
static void op_mtlo(r3000a *P);
static void op_mult(r3000a *P);
static void op_multu(r3000a *P);
static void op_div(r3000a *P);
static void op_divu(r3000a *P);
static void op_add(r3000a *P);
static void op_addu(r3000a *P);
static void op_sub(r3000a *P);
static void op_subu(r3000a *P);
static void op_and(r3000a *P);
static void op_or(r3000a *P);
static void op_xor(r3000a *P);
static void op_nor(r3000a *P);
static void op_slt(r3000a *P);
static void op_sltu(r3000a *P);
// Portions derived from ProjectPSX CPU.cs (MIT License)
// Copyright (c) 2019 Pedro Cortés
// See PROJECTPSX_LICENSE / https://github.com/BluestormDNA/ProjectPSX





static const uint32_t kExceptionAddress[2] = {0x80000080u, 0xBFC00180u};
static void (*const kMainTable[64])(r3000a *) = {
    op_special, op_bcond, op_j,    op_jal,
    op_beq,     op_bne,   op_blez, op_bgtz,
    op_addi,    op_addiu, op_slti, op_sltiu,
    op_andi,    op_ori,   op_xori, op_lui,
    op_cop0,    op_nop,   op_cop2, op_nop,
    op_na,      op_na,    op_na,   op_na,
    op_na,      op_na,    op_na,   op_na,
    op_na,      op_na,    op_na,   op_na,
    op_lb,      op_lh,    op_lwl,  op_lw,
    op_lbu,     op_lhu,   op_lwr,  op_na,
    op_sb,      op_sh,    op_swl,  op_sw,
    op_na,      op_na,    op_swr,  op_na,
    op_nop,     op_nop,   op_lwc2, op_nop,
    op_na,      op_na,    op_na,   op_na,
    op_nop,     op_nop,   op_swc2, op_nop,
    op_na,      op_na,    op_na,   op_na,
};

static void (*const kSpecialTable[64])(r3000a *) = {
    op_sll,     op_na,      op_srl,  op_sra,
    op_sllv,    op_na,      op_srlv, op_srav,
    op_jr,      op_jalr,    op_na,   op_na,
    op_syscall, op_break,   op_na,   op_na,
    op_mfhi,    op_mthi,    op_mflo, op_mtlo,
    op_na,      op_na,      op_na,   op_na,
    op_mult,    op_multu,   op_div,  op_divu,
    op_na,      op_na,      op_na,   op_na,
    op_add,     op_addu,    op_sub,  op_subu,
    op_and,     op_or,      op_xor,  op_nor,
    op_na,      op_na,      op_slt,  op_sltu,
    op_na,      op_na,      op_na,   op_na,
    op_na,      op_na,      op_na,   op_na,
    op_na,      op_na,      op_na,   op_na,
    op_na,      op_na,      op_na,   op_na,
    op_na,      op_na,      op_na,   op_na,
};

void r3000a_reset(r3000a *P) {
  P->pc_now_ = 0;
  P->pc_ = 0xBFC00000u;
  P->pc_predictor_ = 0xBFC00004u;
  memset(P->gpr_, 0, sizeof(P->gpr_));
  P->hi_ = P->lo_ = 0;
  memset(P->cop0_, 0, sizeof(P->cop0_));
  P->cop0_[15] = 0x2;  // PRID
  P->opcode_is_branch_ = false;
  P->opcode_is_delay_slot_ = false;
  P->opcode_took_branch_ = false;
  P->opcode_in_delay_slot_took_branch_ = false;
  // SR.IsC cleared => loads/stores allowed (ProjectPSX zero-inits the flag
  // the other way until the first MTC0 SR; usable reset prefers IsC=0).
  P->dont_isolate_cache_ = true;
  P->instr_.value = 0;
  P->write_back_.register_ = 0; P->write_back_.value = 0;
  P->memory_load_.register_ = 0; P->memory_load_.value = 0;
  P->delayed_memory_load_.register_ = 0; P->memory_load_.value = 0;
  psx_gte_reset(&P->gte_);
}
void r3000a_set_pc(r3000a *P, uint32_t pc) {
  P->pc_ = pc;
  P->pc_predictor_ = pc + 4;
  P->pc_now_ = pc;
}
void r3000a_set_gpr(r3000a *P, int n, uint32_t value) {
  const size_t i = (size_t)(n) & 31;
  P->gpr_[i] = value;
  P->gpr_[0] = 0;
}
int r3000a_run(r3000a *P, int cycles) {
  int executed = 0;
  while (executed < cycles) {
    r3000a_handle_interrupts(P);
    fetch_decode(P);
    if (P->instr_.value != 0) {
      kMainTable[I_opcode(P->instr_.value)](P);
    }
    mem_access(P);
    write_back(P);
    ++executed;
  }
  return executed;
}
void r3000a_handle_interrupts(r3000a *P) {
  // ProjectPSX peeks the next opcode and skips IRQ if it is COP2, to avoid
  // glitching titles such as Crash Bandicoot's intro.
  if (P->read32_) {
    const uint32_t next = P->read32_(P->user_, P->pc_);
    if ((next >> 26) == 0x12u) {
      return;
    }
  }

  if (P->irq_pending_ && P->irq_pending_(P->user_)) {
    P->cop0_[CAUSE] |= 0x400u;
  } else {
    P->cop0_[CAUSE] &= ~0x400u;
  }

  const bool iec = (P->cop0_[SR] & 0x1u) == 1;
  const uint8_t im = (uint8_t)((P->cop0_[SR] >> 8) & 0xFF);
  const uint8_t ip = (uint8_t)((P->cop0_[CAUSE] >> 8) & 0xFF);
  if (iec && (im & ip) != 0) {
    exception(P, EX_INTERRUPT, 0);
  }
}
static int fetch_decode(r3000a *P) {
  P->pc_now_ = P->pc_;
  P->pc_ = P->pc_predictor_;
  P->pc_predictor_ += 4;

  P->opcode_is_delay_slot_ = P->opcode_is_branch_;
  P->opcode_in_delay_slot_took_branch_ = P->opcode_took_branch_;
  P->opcode_is_branch_ = false;
  P->opcode_took_branch_ = false;

  P->instr_.value = load32(P, P->pc_now_);
  return 1;
}
static void mem_access(r3000a *P) {
  // If a new load targets the same register as the pending one, the pending
  // value is overwritten / lost (amidog tests).
  if (P->delayed_memory_load_.register_ != P->memory_load_.register_) {
    P->gpr_[P->memory_load_.register_] = P->memory_load_.value;
  }
  P->memory_load_ = P->delayed_memory_load_;
  P->delayed_memory_load_.register_ = 0;
  P->gpr_[0] = 0;
}
static void write_back(r3000a *P) {
  P->gpr_[P->write_back_.register_] = P->write_back_.value;
  P->write_back_.register_ = 0;
  P->gpr_[0] = 0;
}
static void set_gpr_delayed(r3000a *P, uint32_t reg, uint32_t value) {
  P->write_back_.register_ = reg;
  P->write_back_.value = value;
}
static void delayed_load(r3000a *P, uint32_t reg, uint32_t value) {
  P->delayed_memory_load_.register_ = reg;
  P->delayed_memory_load_.value = value;
}
static void exception(r3000a *P, uint32_t cause, uint32_t coprocessor) {
  const uint32_t mode = P->cop0_[SR] & 0x3Fu;
  P->cop0_[SR] &= ~0x3Fu;
  P->cop0_[SR] |= (mode << 2) & 0x3Fu;

  const uint32_t old_cause = P->cop0_[CAUSE] & 0xFF00u;
  P->cop0_[CAUSE] = (uint32_t)(cause) << 2;
  P->cop0_[CAUSE] |= old_cause;
  P->cop0_[CAUSE] |= coprocessor << 28;

  if (cause == EX_INTERRUPT) {
    P->cop0_[EPC] = P->pc_;
    P->opcode_is_delay_slot_ = P->opcode_is_branch_;
    P->opcode_in_delay_slot_took_branch_ = P->opcode_took_branch_;
  } else {
    P->cop0_[EPC] = P->pc_now_;
  }

  if (P->opcode_is_delay_slot_) {
    P->cop0_[EPC] -= 4;
    P->cop0_[CAUSE] |= 1u << 31;
    P->cop0_[JUMPDEST] = P->pc_;
    if (P->opcode_in_delay_slot_took_branch_) {
      P->cop0_[CAUSE] |= 1u << 30;
    }
  }

  // Correct BEV select. ProjectPSX writes `SR & 0x400000 >> 22` which, due to
  // C# operator precedence, is `SR & 1` and always picks the RAM vector after
  // the SR shift above. Use the intended BEV bit (22).
  const uint32_t bev = (P->cop0_[SR] & 0x400000u) >> 22;
  P->pc_ = kExceptionAddress[bev];
  P->pc_predictor_ = P->pc_ + 4;
}
static void branch(r3000a *P) {
  P->opcode_took_branch_ = true;
  P->pc_predictor_ = P->pc_ + (I_imm_s(P->instr_.value) << 2);
}
static uint32_t load8(r3000a *P, uint32_t addr) {
  return P->read8_ ? P->read8_(P->user_, addr) : 0;
}
static uint32_t load16(r3000a *P, uint32_t addr) {
  return P->read16_ ? P->read16_(P->user_, addr) : 0;
}
static uint32_t load32(r3000a *P, uint32_t addr) {
  return P->read32_ ? P->read32_(P->user_, addr) : 0;
}
static void store8(r3000a *P, uint32_t addr, uint8_t value) {
  if (P->write8_) P->write8_(P->user_, addr, value);
}
static void store16(r3000a *P, uint32_t addr, uint16_t value) {
  if (P->write16_) P->write16_(P->user_, addr, value);
}
static void store32(r3000a *P, uint32_t addr, uint32_t value) {
  if (P->write32_) P->write32_(P->user_, addr, value);
}
static void op_special(r3000a *P) {
  kSpecialTable[I_function(P->instr_.value)](P);
}
static void op_na(r3000a *P) {
  exception(P, EX_ILLEGAL_INSTR, I_id(P->instr_.value));
}
static void op_bcond(r3000a *P) {
  P->opcode_is_branch_ = true;
  const uint32_t op = I_rt(P->instr_.value);
  const bool should_link = (op & 0x1Eu) == 0x10u;
  const bool should_branch =
      (int32_t)(P->gpr_[I_rs(P->instr_.value)] ^ (op << 31)) < 0;

  // ProjectPSX writes r31 immediately for Bcond link (bypasses writeback).
  if (should_link) P->gpr_[31] = P->pc_predictor_;
  if (should_branch) branch(P);
}
static void op_j(r3000a *P) {
  P->opcode_is_branch_ = true;
  P->opcode_took_branch_ = true;
  P->pc_predictor_ = (P->pc_predictor_ & 0xF0000000u) | (I_addr(P->instr_.value) << 2);
}
static void op_jal(r3000a *P) {
  set_gpr_delayed(P, 31, P->pc_predictor_);
  op_j(P);
}
static void op_beq(r3000a *P) {
  P->opcode_is_branch_ = true;
  if (P->gpr_[I_rs(P->instr_.value)] == P->gpr_[I_rt(P->instr_.value)]) {
    branch(P);
  }
}
static void op_bne(r3000a *P) {
  P->opcode_is_branch_ = true;
  if (P->gpr_[I_rs(P->instr_.value)] != P->gpr_[I_rt(P->instr_.value)]) {
    branch(P);
  }
}
static void op_blez(r3000a *P) {
  P->opcode_is_branch_ = true;
  if ((int32_t)(P->gpr_[I_rs(P->instr_.value)]) <= 0) {
    branch(P);
  }
}
static void op_bgtz(r3000a *P) {
  P->opcode_is_branch_ = true;
  if ((int32_t)(P->gpr_[I_rs(P->instr_.value)]) > 0) {
    branch(P);
  }
}
static void op_addi(r3000a *P) {
  // CPU_EXCEPTIONS overflow check omitted (matches default ProjectPSX build).
  set_gpr_delayed(P, I_rt(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value));
}
static void op_addiu(r3000a *P) {
  set_gpr_delayed(P, I_rt(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value));
}
static void op_slti(r3000a *P) {
  const bool condition =
      (int32_t)(P->gpr_[I_rs(P->instr_.value)]) <
      (int32_t)(I_imm_s(P->instr_.value));
  set_gpr_delayed(P, I_rt(P->instr_.value), condition ? 1u : 0u);
}
static void op_sltiu(r3000a *P) {
  const bool condition = P->gpr_[I_rs(P->instr_.value)] < I_imm_s(P->instr_.value);
  set_gpr_delayed(P, I_rt(P->instr_.value), condition ? 1u : 0u);
}
static void op_andi(r3000a *P) {
  set_gpr_delayed(P, I_rt(P->instr_.value), P->gpr_[I_rs(P->instr_.value)] & I_imm(P->instr_.value));
}
static void op_ori(r3000a *P) {
  set_gpr_delayed(P, I_rt(P->instr_.value), P->gpr_[I_rs(P->instr_.value)] | I_imm(P->instr_.value));
}
static void op_xori(r3000a *P) {
  set_gpr_delayed(P, I_rt(P->instr_.value), P->gpr_[I_rs(P->instr_.value)] ^ I_imm(P->instr_.value));
}
static void op_lui(r3000a *P) {
  set_gpr_delayed(P, I_rt(P->instr_.value), I_imm(P->instr_.value) << 16);
}
static void op_cop0(r3000a *P) {
  if (I_rs(P->instr_.value) == 0b00000) {
    mfc0(P);
  } else if (I_rs(P->instr_.value) == 0b00100) {
    mtc0(P);
  } else if (I_rs(P->instr_.value) == 0b10000) {
    rfe(P);
  } else {
    exception(P, EX_ILLEGAL_INSTR, I_id(P->instr_.value));
  }
}
static void mfc0(r3000a *P) {
  const uint32_t mfc = I_rd(P->instr_.value);
  if (mfc == 3 || (mfc >= 5 && mfc <= 9) || (mfc >= 11 && mfc <= 15)) {
    delayed_load(P, I_rt(P->instr_.value), P->cop0_[mfc]);
  } else {
    exception(P, EX_ILLEGAL_INSTR, I_id(P->instr_.value));
  }
}
static void mtc0(r3000a *P) {
  const uint32_t value = P->gpr_[I_rt(P->instr_.value)];
  const uint32_t reg = I_rd(P->instr_.value);

  if (reg == CAUSE) {
    P->cop0_[CAUSE] &= ~0x300u;
    P->cop0_[CAUSE] |= value & 0x300u;
  } else if (reg == SR) {
    P->dont_isolate_cache_ = (value & 0x10000u) == 0;
    const bool prev_iec = (P->cop0_[SR] & 0x1u) == 1;
    const bool current_iec = (value & 0x1u) == 1;
    P->cop0_[SR] = value;

    const uint32_t im = (value >> 8) & 0x3u;
    const uint32_t ip = (P->cop0_[CAUSE] >> 8) & 0x3u;
    if (!prev_iec && current_iec && (im & ip) > 0) {
      P->pc_ = P->pc_predictor_;
      exception(P, EX_INTERRUPT, I_id(P->instr_.value));
    }
  } else {
    P->cop0_[reg] = value;
  }
}
static void rfe(r3000a *P) {
  const uint32_t mode = P->cop0_[SR] & 0x3Fu;
  P->cop0_[SR] &= ~0xFu;
  P->cop0_[SR] |= mode >> 2;
}
static void op_cop2(r3000a *P) {
  if ((I_rs(P->instr_.value) & 0x10u) == 0) {
    switch (I_rs(P->instr_.value)) {
      case 0b00000:
        delayed_load(P, I_rt(P->instr_.value), psx_gte_read_data(&P->gte_, I_rd(P->instr_.value)));
        break;
      case 0b00010:
        delayed_load(P, I_rt(P->instr_.value), psx_gte_read_control(&P->gte_, I_rd(P->instr_.value)));
        break;
      case 0b00100:
        psx_gte_write_data(&P->gte_, I_rd(P->instr_.value), P->gpr_[I_rt(P->instr_.value)]);
        break;
      case 0b00110:
        psx_gte_write_control(&P->gte_, I_rd(P->instr_.value), P->gpr_[I_rt(P->instr_.value)]);
        break;
      default:
        exception(P, EX_ILLEGAL_INSTR, I_id(P->instr_.value));
        break;
    }
  } else {
    psx_gte_execute(&P->gte_, P->instr_.value);
  }
}
static void op_lwc2(r3000a *P) {
  const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
  psx_gte_write_data(&P->gte_, I_rt(P->instr_.value), load32(P, addr));
}
static void op_swc2(r3000a *P) {
  const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
  store32(P, addr, psx_gte_read_data(&P->gte_, I_rt(P->instr_.value)));
}
static void op_lb(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t value =
        (uint32_t)((int8_t)(load8(P, 
            P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value))));
    delayed_load(P, I_rt(P->instr_.value), value);
  }
}
static void op_lbu(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t value =
        load8(P, P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value)) & 0xFFu;
    delayed_load(P, I_rt(P->instr_.value), value);
  }
}
static void op_lh(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
    const uint32_t value =
        (uint32_t)((int16_t)(load16(P, addr)));
    delayed_load(P, I_rt(P->instr_.value), value);
  }
}
static void op_lhu(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
    delayed_load(P, I_rt(P->instr_.value), load16(P, addr) & 0xFFFFu);
  }
}
static void op_lw(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
    delayed_load(P, I_rt(P->instr_.value), load32(P, addr));
  }
}
static void op_lwl(r3000a *P) {
  const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = load32(P, aligned_addr);

  uint32_t lr_value = P->gpr_[I_rt(P->instr_.value)];
  if (I_rt(P->instr_.value) == P->memory_load_.register_) {
    lr_value = P->memory_load_.value;
  }

  const int shift = (int)((addr & 0x3u) << 3);
  const uint32_t mask = 0x00FFFFFFu >> shift;
  const uint32_t value = (lr_value & mask) | (aligned_load << (24 - shift));
  delayed_load(P, I_rt(P->instr_.value), value);
}
static void op_lwr(r3000a *P) {
  const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = load32(P, aligned_addr);

  uint32_t lr_value = P->gpr_[I_rt(P->instr_.value)];
  if (I_rt(P->instr_.value) == P->memory_load_.register_) {
    lr_value = P->memory_load_.value;
  }

  const int shift = (int)((addr & 0x3u) << 3);
  const uint32_t mask = 0xFFFFFF00u << (24 - shift);
  const uint32_t value = (lr_value & mask) | (aligned_load >> shift);
  delayed_load(P, I_rt(P->instr_.value), value);
}
static void op_sb(r3000a *P) {
  if (P->dont_isolate_cache_) {
    store8(P, P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value),
               (uint8_t)(P->gpr_[I_rt(P->instr_.value)]));
  }
}
static void op_sh(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
    store16(P, addr, (uint16_t)(P->gpr_[I_rt(P->instr_.value)]));
  }
}
static void op_sw(r3000a *P) {
  if (P->dont_isolate_cache_) {
    const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
    store32(P, addr, P->gpr_[I_rt(P->instr_.value)]);
  }
}
static void op_swr(r3000a *P) {
  const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = load32(P, aligned_addr);

  const int shift = (int)((addr & 0x3u) << 3);
  const uint32_t mask = 0x00FFFFFFu >> (24 - shift);
  const uint32_t value =
      (aligned_load & mask) | (P->gpr_[I_rt(P->instr_.value)] << shift);
  store32(P, aligned_addr, value);
}
static void op_swl(r3000a *P) {
  const uint32_t addr = P->gpr_[I_rs(P->instr_.value)] + I_imm_s(P->instr_.value);
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = load32(P, aligned_addr);

  const int shift = (int)((addr & 0x3u) << 3);
  const uint32_t mask = 0xFFFFFF00u << shift;
  const uint32_t value =
      (aligned_load & mask) | (P->gpr_[I_rt(P->instr_.value)] >> (24 - shift));
  store32(P, aligned_addr, value);
}
static void op_sll(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rt(P->instr_.value)] << (int)(I_sa(P->instr_.value)));
}
static void op_srl(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rt(P->instr_.value)] >> (int)(I_sa(P->instr_.value)));
}
static void op_sra(r3000a *P) {
  set_gpr_delayed(P, 
      I_rd(P->instr_.value),
      (uint32_t)((int32_t)(P->gpr_[I_rt(P->instr_.value)]) >>
                            (int)(I_sa(P->instr_.value))));
}
static void op_sllv(r3000a *P) {
  set_gpr_delayed(P, 
      I_rd(P->instr_.value),
      P->gpr_[I_rt(P->instr_.value)] << (int)(P->gpr_[I_rs(P->instr_.value)] & 0x1Fu));
}
static void op_srlv(r3000a *P) {
  set_gpr_delayed(P, 
      I_rd(P->instr_.value),
      P->gpr_[I_rt(P->instr_.value)] >> (int)(P->gpr_[I_rs(P->instr_.value)] & 0x1Fu));
}
static void op_srav(r3000a *P) {
  set_gpr_delayed(P, 
      I_rd(P->instr_.value),
      (uint32_t)(
          (int32_t)(P->gpr_[I_rt(P->instr_.value)]) >>
          (int)(P->gpr_[I_rs(P->instr_.value)] & 0x1Fu)));
}
static void op_jr(r3000a *P) {
  P->opcode_is_branch_ = true;
  P->opcode_took_branch_ = true;
  P->pc_predictor_ = P->gpr_[I_rs(P->instr_.value)];
}
static void op_jalr(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value), P->pc_predictor_);
  op_jr(P);
}
static void op_syscall(r3000a *P) {
  exception(P, EX_SYSCALL, I_id(P->instr_.value));
}
static void op_break(r3000a *P) {
  exception(P, EX_BREAK, 0);
}
static void op_mfhi(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value), P->hi_);
}
static void op_mthi(r3000a *P) {
  P->hi_ = P->gpr_[I_rs(P->instr_.value)];
}
static void op_mflo(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value), P->lo_);
}
static void op_mtlo(r3000a *P) {
  P->lo_ = P->gpr_[I_rs(P->instr_.value)];
}
static void op_mult(r3000a *P) {
  const int64_t value = (int64_t)((int32_t)(P->gpr_[I_rs(P->instr_.value)])) *
                        (int64_t)((int32_t)(P->gpr_[I_rt(P->instr_.value)]));
  P->hi_ = (uint32_t)(value >> 32);
  P->lo_ = (uint32_t)(value);
}
static void op_multu(r3000a *P) {
  const uint64_t value = (uint64_t)(P->gpr_[I_rs(P->instr_.value)]) *
                         (uint64_t)(P->gpr_[I_rt(P->instr_.value)]);
  P->hi_ = (uint32_t)(value >> 32);
  P->lo_ = (uint32_t)(value);
}
static void op_div(r3000a *P) {
  const int32_t n = (int32_t)(P->gpr_[I_rs(P->instr_.value)]);
  const int32_t d = (int32_t)(P->gpr_[I_rt(P->instr_.value)]);

  if (d == 0) {
    P->hi_ = (uint32_t)(n);
    P->lo_ = (n >= 0) ? 0xFFFFFFFFu : 1u;
  } else if ((uint32_t)(n) == 0x80000000u && d == -1) {
    P->hi_ = 0;
    P->lo_ = 0x80000000u;
  } else {
    P->hi_ = (uint32_t)(n % d);
    P->lo_ = (uint32_t)(n / d);
  }
}
static void op_divu(r3000a *P) {
  const uint32_t n = P->gpr_[I_rs(P->instr_.value)];
  const uint32_t d = P->gpr_[I_rt(P->instr_.value)];
  if (d == 0) {
    P->hi_ = n;
    P->lo_ = 0xFFFFFFFFu;
  } else {
    P->hi_ = n % d;
    P->lo_ = n / d;
  }
}
static void op_add(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] + P->gpr_[I_rt(P->instr_.value)]);
}
static void op_addu(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] + P->gpr_[I_rt(P->instr_.value)]);
}
static void op_sub(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] - P->gpr_[I_rt(P->instr_.value)]);
}
static void op_subu(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] - P->gpr_[I_rt(P->instr_.value)]);
}
static void op_and(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] & P->gpr_[I_rt(P->instr_.value)]);
}
static void op_or(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] | P->gpr_[I_rt(P->instr_.value)]);
}
static void op_xor(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      P->gpr_[I_rs(P->instr_.value)] ^ P->gpr_[I_rt(P->instr_.value)]);
}
static void op_nor(r3000a *P) {
  set_gpr_delayed(P, I_rd(P->instr_.value),
                      ~(P->gpr_[I_rs(P->instr_.value)] | P->gpr_[I_rt(P->instr_.value)]));
}
static void op_slt(r3000a *P) {
  const bool condition = (int32_t)(P->gpr_[I_rs(P->instr_.value)]) <
                         (int32_t)(P->gpr_[I_rt(P->instr_.value)]);
  set_gpr_delayed(P, I_rd(P->instr_.value), condition ? 1u : 0u);
}
static void op_sltu(r3000a *P) {
  const bool condition = P->gpr_[I_rs(P->instr_.value)] < P->gpr_[I_rt(P->instr_.value)];
  set_gpr_delayed(P, I_rd(P->instr_.value), condition ? 1u : 0u);
}

