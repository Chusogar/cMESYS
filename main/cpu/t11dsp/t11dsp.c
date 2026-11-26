// t11dsp.c - see t11dsp.h. Translated from dsp-cpp's cpu/t11 (MAME's t11
// core, license BSD-3-Clause, copyright-holders Aaron Giles): the opcode
// bodies are MAME's, with its macro shorthand.
#include <string.h>
#include "t11dsp.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define REGD(x) ((int)cpu->reg.w[x])
#define REGW(x) cpu->reg.w[x]
#define REGB(x) cpu->reg.b[(x) * 2]
#define SP REGW(6)
#define PC REGW(7)
#define SPD REGD(6)
#define PCD REGD(7)
#define PSW cpu->psw
#define icount_ cpu->icount
#define wait_state_ cpu->wait_state
#define initial_pc_ cpu->initial_pc

static const uint16_t kInitialPc[8] = {0xc000, 0x8000, 0x4000, 0x2000, 0x1000, 0x0000, 0xf600, 0xf400};
static const struct { uint8_t priority, vector; } kIrqTable[16] = {
    {0 << 5, 0000}, {4 << 5, 0070}, {4 << 5, 0064}, {4 << 5, 0060},
    {5 << 5, 0134}, {5 << 5, 0130}, {5 << 5, 0124}, {5 << 5, 0120},
    {6 << 5, 0114}, {6 << 5, 0110}, {6 << 5, 0104}, {6 << 5, 0100},
    {7 << 5, 0154}, {7 << 5, 0150}, {7 << 5, 0144}, {7 << 5, 0140}};

void t11d_init(t11d *c, uint32_t clock, uint16_t initial_mode)
{
    c->clock = clock;
    c->initial_mode = initial_mode;
    c->initial_pc = kInitialPc[initial_mode >> 13];
}

void t11d_reset(t11d *cpu)
{
    memset(&cpu->reg, 0, sizeof cpu->reg);
    SP = 0376;
    PC = cpu->initial_pc;
    PSW = 0340;
    cpu->wait_state = 0;
    cpu->power_fail = cpu->bus_error = cpu->ext_halt = false;
    cpu->cp_state = cpu->pulse_mask = 0;
    cpu->icount = 0;
}

static int t11_RWORD(t11d *cpu, int addr) { return cpu->read(cpu->user, (uint16_t)((uint16_t)addr & 0xfffe)); }
static void t11_WWORD(t11d *cpu, int addr, int data) { cpu->write(cpu->user, (uint16_t)((uint16_t)addr & 0xfffe), (uint16_t)data, 0xffff); }
static int t11_RBYTE(t11d *cpu, int addr)
{
    const uint16_t word = (uint16_t)cpu->read(cpu->user, (uint16_t)((uint16_t)addr & 0xfffe));
    return (addr & 1) ? (word >> 8) : (word & 0xff);
}
static void t11_WBYTE(t11d *cpu, int addr, int data)
{
    const uint16_t address = (uint16_t)((uint16_t)addr & 0xfffe);
    if (addr & 1) cpu->write(cpu->user, address, (uint16_t)((uint8_t)data << 8), 0xff00);
    else cpu->write(cpu->user, address, (uint16_t)(uint8_t)data, 0x00ff);
}
static int t11_ROPCODE(t11d *cpu)
{
    PC &= 0xfffe;
    const int val = t11_RWORD(cpu, PC);
    PC += 2;
    return val;
}
static void t11_PUSH(t11d *cpu, int val)
{
    SP -= 2;
    t11_WWORD(cpu, SPD, val);
}
static int t11_POP(t11d *cpu)
{
    const int result = t11_RWORD(cpu, SPD);
    SP += 2;
    return result;
}
#define RWORD(a) t11_RWORD(cpu, a)
#define WWORD(a, d) t11_WWORD(cpu, a, d)
#define RBYTE(a) t11_RBYTE(cpu, a)
#define WBYTE(a, d) t11_WBYTE(cpu, a, d)
#define ROPCODE() t11_ROPCODE(cpu)
#define PUSH(v) t11_PUSH(cpu, v)
#define POP() t11_POP(cpu)
#define trap_to(v) t11_trap_to(cpu, v)
#define take_interrupt(v) t11_take_interrupt(cpu, v)

#define CFLAG 1
#define VFLAG 2
#define ZFLAG 4
#define NFLAG 8
#define GET_C (PSW & CFLAG)
#define GET_V (PSW & VFLAG)
#define GET_Z (PSW & ZFLAG)
#define GET_N (PSW & NFLAG)
#define CLR_C (PSW &= ~CFLAG)
#define CLR_V (PSW &= ~VFLAG)
#define CLR_Z (PSW &= ~ZFLAG)
#define CLR_N (PSW &= ~NFLAG)
#define SET_C (PSW |= CFLAG)
#define SET_V (PSW |= VFLAG)
#define SET_Z (PSW |= ZFLAG)
#define SET_N (PSW |= NFLAG)

static void t11_take_interrupt(t11d *cpu, uint8_t vector)
{
    const uint16_t new_pc = (uint16_t)RWORD(vector);
    const uint16_t new_psw = (uint16_t)RWORD(vector + 2);
    PUSH(PSW);
    PUSH(PC);
    PC = new_pc;
    PSW = (uint8_t)new_psw;
    cpu->icount -= 114;
    cpu->wait_state = 0;
}

static void t11_check_irqs(t11d *cpu)
{
    if (cpu->ext_halt) {                        // (nonmaskable)
        cpu->ext_halt = false;
        PUSH(PSW);
        PUSH(PC);
        PC = (uint16_t)(cpu->initial_pc + 4);
        PSW = 0340;
        cpu->icount -= 114;
        cpu->wait_state = 0;
        return;
    }
    if (cpu->bus_error) {
        cpu->bus_error = false;
        take_interrupt(0004);
        return;
    }
    if (cpu->power_fail) {
        cpu->power_fail = false;
        take_interrupt(0024);
        return;
    }
    const int i = cpu->cp_state & 15;
    if (kIrqTable[i].priority > (PSW & 0340)) {
        cpu->cp_state &= (uint8_t)~cpu->pulse_mask;
        cpu->pulse_mask = 0;
        take_interrupt(kIrqTable[i].vector);
    }
}

void t11d_set_irq(t11d *c, int line, int state)
{
    if (line < 0 || line > 3) return;
    const uint8_t mask = (uint8_t)(1 << line);
    switch (state) {
    case T11_CLEAR:
        c->cp_state &= (uint8_t)~mask;
        c->pulse_mask &= (uint8_t)~mask;
        break;
    case T11_PULSE:
        c->cp_state |= mask;
        c->pulse_mask |= mask;
        break;
    default:
        c->cp_state |= mask;
        c->pulse_mask &= (uint8_t)~mask;
        break;
    }
}

typedef void (*t11_op)(t11d *cpu, uint16_t op);
static void t11_trap_to(t11d *cpu, uint16_t op);
static void t11_op_0000(t11d *cpu, uint16_t op);
static void t11_halt(t11d *cpu, uint16_t op);
static void t11_illegal(t11d *cpu, uint16_t op);
static void t11_illegal4(t11d *cpu, uint16_t op);
static void t11_mark(t11d *cpu, uint16_t op);
static void t11_jmp_rgd(t11d *cpu, uint16_t op);
static void t11_jmp_in(t11d *cpu, uint16_t op);
static void t11_jmp_ind(t11d *cpu, uint16_t op);
static void t11_jmp_de(t11d *cpu, uint16_t op);
static void t11_jmp_ded(t11d *cpu, uint16_t op);
static void t11_jmp_ix(t11d *cpu, uint16_t op);
static void t11_jmp_ixd(t11d *cpu, uint16_t op);
static void t11_rts(t11d *cpu, uint16_t op);
static void t11_ccc(t11d *cpu, uint16_t op);
static void t11_scc(t11d *cpu, uint16_t op);
static void t11_swab_rg(t11d *cpu, uint16_t op);
static void t11_swab_rgd(t11d *cpu, uint16_t op);
static void t11_swab_in(t11d *cpu, uint16_t op);
static void t11_swab_ind(t11d *cpu, uint16_t op);
static void t11_swab_de(t11d *cpu, uint16_t op);
static void t11_swab_ded(t11d *cpu, uint16_t op);
static void t11_swab_ix(t11d *cpu, uint16_t op);
static void t11_swab_ixd(t11d *cpu, uint16_t op);
static void t11_br(t11d *cpu, uint16_t op);
static void t11_bne(t11d *cpu, uint16_t op);
static void t11_beq(t11d *cpu, uint16_t op);
static void t11_bge(t11d *cpu, uint16_t op);
static void t11_blt(t11d *cpu, uint16_t op);
static void t11_bgt(t11d *cpu, uint16_t op);
static void t11_ble(t11d *cpu, uint16_t op);
static void t11_jsr_rgd(t11d *cpu, uint16_t op);
static void t11_jsr_in(t11d *cpu, uint16_t op);
static void t11_jsr_ind(t11d *cpu, uint16_t op);
static void t11_jsr_de(t11d *cpu, uint16_t op);
static void t11_jsr_ded(t11d *cpu, uint16_t op);
static void t11_jsr_ix(t11d *cpu, uint16_t op);
static void t11_jsr_ixd(t11d *cpu, uint16_t op);
static void t11_clr_rg(t11d *cpu, uint16_t op);
static void t11_clr_rgd(t11d *cpu, uint16_t op);
static void t11_clr_in(t11d *cpu, uint16_t op);
static void t11_clr_ind(t11d *cpu, uint16_t op);
static void t11_clr_de(t11d *cpu, uint16_t op);
static void t11_clr_ded(t11d *cpu, uint16_t op);
static void t11_clr_ix(t11d *cpu, uint16_t op);
static void t11_clr_ixd(t11d *cpu, uint16_t op);
static void t11_com_rg(t11d *cpu, uint16_t op);
static void t11_com_rgd(t11d *cpu, uint16_t op);
static void t11_com_in(t11d *cpu, uint16_t op);
static void t11_com_ind(t11d *cpu, uint16_t op);
static void t11_com_de(t11d *cpu, uint16_t op);
static void t11_com_ded(t11d *cpu, uint16_t op);
static void t11_com_ix(t11d *cpu, uint16_t op);
static void t11_com_ixd(t11d *cpu, uint16_t op);
static void t11_inc_rg(t11d *cpu, uint16_t op);
static void t11_inc_rgd(t11d *cpu, uint16_t op);
static void t11_inc_in(t11d *cpu, uint16_t op);
static void t11_inc_ind(t11d *cpu, uint16_t op);
static void t11_inc_de(t11d *cpu, uint16_t op);
static void t11_inc_ded(t11d *cpu, uint16_t op);
static void t11_inc_ix(t11d *cpu, uint16_t op);
static void t11_inc_ixd(t11d *cpu, uint16_t op);
static void t11_dec_rg(t11d *cpu, uint16_t op);
static void t11_dec_rgd(t11d *cpu, uint16_t op);
static void t11_dec_in(t11d *cpu, uint16_t op);
static void t11_dec_ind(t11d *cpu, uint16_t op);
static void t11_dec_de(t11d *cpu, uint16_t op);
static void t11_dec_ded(t11d *cpu, uint16_t op);
static void t11_dec_ix(t11d *cpu, uint16_t op);
static void t11_dec_ixd(t11d *cpu, uint16_t op);
static void t11_neg_rg(t11d *cpu, uint16_t op);
static void t11_neg_rgd(t11d *cpu, uint16_t op);
static void t11_neg_in(t11d *cpu, uint16_t op);
static void t11_neg_ind(t11d *cpu, uint16_t op);
static void t11_neg_de(t11d *cpu, uint16_t op);
static void t11_neg_ded(t11d *cpu, uint16_t op);
static void t11_neg_ix(t11d *cpu, uint16_t op);
static void t11_neg_ixd(t11d *cpu, uint16_t op);
static void t11_adc_rg(t11d *cpu, uint16_t op);
static void t11_adc_rgd(t11d *cpu, uint16_t op);
static void t11_adc_in(t11d *cpu, uint16_t op);
static void t11_adc_ind(t11d *cpu, uint16_t op);
static void t11_adc_de(t11d *cpu, uint16_t op);
static void t11_adc_ded(t11d *cpu, uint16_t op);
static void t11_adc_ix(t11d *cpu, uint16_t op);
static void t11_adc_ixd(t11d *cpu, uint16_t op);
static void t11_sbc_rg(t11d *cpu, uint16_t op);
static void t11_sbc_rgd(t11d *cpu, uint16_t op);
static void t11_sbc_in(t11d *cpu, uint16_t op);
static void t11_sbc_ind(t11d *cpu, uint16_t op);
static void t11_sbc_de(t11d *cpu, uint16_t op);
static void t11_sbc_ded(t11d *cpu, uint16_t op);
static void t11_sbc_ix(t11d *cpu, uint16_t op);
static void t11_sbc_ixd(t11d *cpu, uint16_t op);
static void t11_tst_rg(t11d *cpu, uint16_t op);
static void t11_tst_rgd(t11d *cpu, uint16_t op);
static void t11_tst_in(t11d *cpu, uint16_t op);
static void t11_tst_ind(t11d *cpu, uint16_t op);
static void t11_tst_de(t11d *cpu, uint16_t op);
static void t11_tst_ded(t11d *cpu, uint16_t op);
static void t11_tst_ix(t11d *cpu, uint16_t op);
static void t11_tst_ixd(t11d *cpu, uint16_t op);
static void t11_ror_rg(t11d *cpu, uint16_t op);
static void t11_ror_rgd(t11d *cpu, uint16_t op);
static void t11_ror_in(t11d *cpu, uint16_t op);
static void t11_ror_ind(t11d *cpu, uint16_t op);
static void t11_ror_de(t11d *cpu, uint16_t op);
static void t11_ror_ded(t11d *cpu, uint16_t op);
static void t11_ror_ix(t11d *cpu, uint16_t op);
static void t11_ror_ixd(t11d *cpu, uint16_t op);
static void t11_rol_rg(t11d *cpu, uint16_t op);
static void t11_rol_rgd(t11d *cpu, uint16_t op);
static void t11_rol_in(t11d *cpu, uint16_t op);
static void t11_rol_ind(t11d *cpu, uint16_t op);
static void t11_rol_de(t11d *cpu, uint16_t op);
static void t11_rol_ded(t11d *cpu, uint16_t op);
static void t11_rol_ix(t11d *cpu, uint16_t op);
static void t11_rol_ixd(t11d *cpu, uint16_t op);
static void t11_asr_rg(t11d *cpu, uint16_t op);
static void t11_asr_rgd(t11d *cpu, uint16_t op);
static void t11_asr_in(t11d *cpu, uint16_t op);
static void t11_asr_ind(t11d *cpu, uint16_t op);
static void t11_asr_de(t11d *cpu, uint16_t op);
static void t11_asr_ded(t11d *cpu, uint16_t op);
static void t11_asr_ix(t11d *cpu, uint16_t op);
static void t11_asr_ixd(t11d *cpu, uint16_t op);
static void t11_asl_rg(t11d *cpu, uint16_t op);
static void t11_asl_rgd(t11d *cpu, uint16_t op);
static void t11_asl_in(t11d *cpu, uint16_t op);
static void t11_asl_ind(t11d *cpu, uint16_t op);
static void t11_asl_de(t11d *cpu, uint16_t op);
static void t11_asl_ded(t11d *cpu, uint16_t op);
static void t11_asl_ix(t11d *cpu, uint16_t op);
static void t11_asl_ixd(t11d *cpu, uint16_t op);
static void t11_sxt_rg(t11d *cpu, uint16_t op);
static void t11_sxt_rgd(t11d *cpu, uint16_t op);
static void t11_sxt_in(t11d *cpu, uint16_t op);
static void t11_sxt_ind(t11d *cpu, uint16_t op);
static void t11_sxt_de(t11d *cpu, uint16_t op);
static void t11_sxt_ded(t11d *cpu, uint16_t op);
static void t11_sxt_ix(t11d *cpu, uint16_t op);
static void t11_sxt_ixd(t11d *cpu, uint16_t op);
static void t11_mov_rg_rg(t11d *cpu, uint16_t op);
static void t11_mov_rg_rgd(t11d *cpu, uint16_t op);
static void t11_mov_rg_in(t11d *cpu, uint16_t op);
static void t11_mov_rg_ind(t11d *cpu, uint16_t op);
static void t11_mov_rg_de(t11d *cpu, uint16_t op);
static void t11_mov_rg_ded(t11d *cpu, uint16_t op);
static void t11_mov_rg_ix(t11d *cpu, uint16_t op);
static void t11_mov_rg_ixd(t11d *cpu, uint16_t op);
static void t11_mov_rgd_rg(t11d *cpu, uint16_t op);
static void t11_mov_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_mov_rgd_in(t11d *cpu, uint16_t op);
static void t11_mov_rgd_ind(t11d *cpu, uint16_t op);
static void t11_mov_rgd_de(t11d *cpu, uint16_t op);
static void t11_mov_rgd_ded(t11d *cpu, uint16_t op);
static void t11_mov_rgd_ix(t11d *cpu, uint16_t op);
static void t11_mov_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_mov_in_rg(t11d *cpu, uint16_t op);
static void t11_mov_in_rgd(t11d *cpu, uint16_t op);
static void t11_mov_in_in(t11d *cpu, uint16_t op);
static void t11_mov_in_ind(t11d *cpu, uint16_t op);
static void t11_mov_in_de(t11d *cpu, uint16_t op);
static void t11_mov_in_ded(t11d *cpu, uint16_t op);
static void t11_mov_in_ix(t11d *cpu, uint16_t op);
static void t11_mov_in_ixd(t11d *cpu, uint16_t op);
static void t11_mov_ind_rg(t11d *cpu, uint16_t op);
static void t11_mov_ind_rgd(t11d *cpu, uint16_t op);
static void t11_mov_ind_in(t11d *cpu, uint16_t op);
static void t11_mov_ind_ind(t11d *cpu, uint16_t op);
static void t11_mov_ind_de(t11d *cpu, uint16_t op);
static void t11_mov_ind_ded(t11d *cpu, uint16_t op);
static void t11_mov_ind_ix(t11d *cpu, uint16_t op);
static void t11_mov_ind_ixd(t11d *cpu, uint16_t op);
static void t11_mov_de_rg(t11d *cpu, uint16_t op);
static void t11_mov_de_rgd(t11d *cpu, uint16_t op);
static void t11_mov_de_in(t11d *cpu, uint16_t op);
static void t11_mov_de_ind(t11d *cpu, uint16_t op);
static void t11_mov_de_de(t11d *cpu, uint16_t op);
static void t11_mov_de_ded(t11d *cpu, uint16_t op);
static void t11_mov_de_ix(t11d *cpu, uint16_t op);
static void t11_mov_de_ixd(t11d *cpu, uint16_t op);
static void t11_mov_ded_rg(t11d *cpu, uint16_t op);
static void t11_mov_ded_rgd(t11d *cpu, uint16_t op);
static void t11_mov_ded_in(t11d *cpu, uint16_t op);
static void t11_mov_ded_ind(t11d *cpu, uint16_t op);
static void t11_mov_ded_de(t11d *cpu, uint16_t op);
static void t11_mov_ded_ded(t11d *cpu, uint16_t op);
static void t11_mov_ded_ix(t11d *cpu, uint16_t op);
static void t11_mov_ded_ixd(t11d *cpu, uint16_t op);
static void t11_mov_ix_rg(t11d *cpu, uint16_t op);
static void t11_mov_ix_rgd(t11d *cpu, uint16_t op);
static void t11_mov_ix_in(t11d *cpu, uint16_t op);
static void t11_mov_ix_ind(t11d *cpu, uint16_t op);
static void t11_mov_ix_de(t11d *cpu, uint16_t op);
static void t11_mov_ix_ded(t11d *cpu, uint16_t op);
static void t11_mov_ix_ix(t11d *cpu, uint16_t op);
static void t11_mov_ix_ixd(t11d *cpu, uint16_t op);
static void t11_mov_ixd_rg(t11d *cpu, uint16_t op);
static void t11_mov_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_mov_ixd_in(t11d *cpu, uint16_t op);
static void t11_mov_ixd_ind(t11d *cpu, uint16_t op);
static void t11_mov_ixd_de(t11d *cpu, uint16_t op);
static void t11_mov_ixd_ded(t11d *cpu, uint16_t op);
static void t11_mov_ixd_ix(t11d *cpu, uint16_t op);
static void t11_mov_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_rg_rg(t11d *cpu, uint16_t op);
static void t11_cmp_rg_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_rg_in(t11d *cpu, uint16_t op);
static void t11_cmp_rg_ind(t11d *cpu, uint16_t op);
static void t11_cmp_rg_de(t11d *cpu, uint16_t op);
static void t11_cmp_rg_ded(t11d *cpu, uint16_t op);
static void t11_cmp_rg_ix(t11d *cpu, uint16_t op);
static void t11_cmp_rg_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_rg(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_in(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_ind(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_de(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_ded(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_ix(t11d *cpu, uint16_t op);
static void t11_cmp_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_in_rg(t11d *cpu, uint16_t op);
static void t11_cmp_in_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_in_in(t11d *cpu, uint16_t op);
static void t11_cmp_in_ind(t11d *cpu, uint16_t op);
static void t11_cmp_in_de(t11d *cpu, uint16_t op);
static void t11_cmp_in_ded(t11d *cpu, uint16_t op);
static void t11_cmp_in_ix(t11d *cpu, uint16_t op);
static void t11_cmp_in_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_ind_rg(t11d *cpu, uint16_t op);
static void t11_cmp_ind_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_ind_in(t11d *cpu, uint16_t op);
static void t11_cmp_ind_ind(t11d *cpu, uint16_t op);
static void t11_cmp_ind_de(t11d *cpu, uint16_t op);
static void t11_cmp_ind_ded(t11d *cpu, uint16_t op);
static void t11_cmp_ind_ix(t11d *cpu, uint16_t op);
static void t11_cmp_ind_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_de_rg(t11d *cpu, uint16_t op);
static void t11_cmp_de_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_de_in(t11d *cpu, uint16_t op);
static void t11_cmp_de_ind(t11d *cpu, uint16_t op);
static void t11_cmp_de_de(t11d *cpu, uint16_t op);
static void t11_cmp_de_ded(t11d *cpu, uint16_t op);
static void t11_cmp_de_ix(t11d *cpu, uint16_t op);
static void t11_cmp_de_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_ded_rg(t11d *cpu, uint16_t op);
static void t11_cmp_ded_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_ded_in(t11d *cpu, uint16_t op);
static void t11_cmp_ded_ind(t11d *cpu, uint16_t op);
static void t11_cmp_ded_de(t11d *cpu, uint16_t op);
static void t11_cmp_ded_ded(t11d *cpu, uint16_t op);
static void t11_cmp_ded_ix(t11d *cpu, uint16_t op);
static void t11_cmp_ded_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_ix_rg(t11d *cpu, uint16_t op);
static void t11_cmp_ix_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_ix_in(t11d *cpu, uint16_t op);
static void t11_cmp_ix_ind(t11d *cpu, uint16_t op);
static void t11_cmp_ix_de(t11d *cpu, uint16_t op);
static void t11_cmp_ix_ded(t11d *cpu, uint16_t op);
static void t11_cmp_ix_ix(t11d *cpu, uint16_t op);
static void t11_cmp_ix_ixd(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_rg(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_in(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_ind(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_de(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_ded(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_ix(t11d *cpu, uint16_t op);
static void t11_cmp_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_bit_rg_rg(t11d *cpu, uint16_t op);
static void t11_bit_rg_rgd(t11d *cpu, uint16_t op);
static void t11_bit_rg_in(t11d *cpu, uint16_t op);
static void t11_bit_rg_ind(t11d *cpu, uint16_t op);
static void t11_bit_rg_de(t11d *cpu, uint16_t op);
static void t11_bit_rg_ded(t11d *cpu, uint16_t op);
static void t11_bit_rg_ix(t11d *cpu, uint16_t op);
static void t11_bit_rg_ixd(t11d *cpu, uint16_t op);
static void t11_bit_rgd_rg(t11d *cpu, uint16_t op);
static void t11_bit_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_bit_rgd_in(t11d *cpu, uint16_t op);
static void t11_bit_rgd_ind(t11d *cpu, uint16_t op);
static void t11_bit_rgd_de(t11d *cpu, uint16_t op);
static void t11_bit_rgd_ded(t11d *cpu, uint16_t op);
static void t11_bit_rgd_ix(t11d *cpu, uint16_t op);
static void t11_bit_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_bit_in_rg(t11d *cpu, uint16_t op);
static void t11_bit_in_rgd(t11d *cpu, uint16_t op);
static void t11_bit_in_in(t11d *cpu, uint16_t op);
static void t11_bit_in_ind(t11d *cpu, uint16_t op);
static void t11_bit_in_de(t11d *cpu, uint16_t op);
static void t11_bit_in_ded(t11d *cpu, uint16_t op);
static void t11_bit_in_ix(t11d *cpu, uint16_t op);
static void t11_bit_in_ixd(t11d *cpu, uint16_t op);
static void t11_bit_ind_rg(t11d *cpu, uint16_t op);
static void t11_bit_ind_rgd(t11d *cpu, uint16_t op);
static void t11_bit_ind_in(t11d *cpu, uint16_t op);
static void t11_bit_ind_ind(t11d *cpu, uint16_t op);
static void t11_bit_ind_de(t11d *cpu, uint16_t op);
static void t11_bit_ind_ded(t11d *cpu, uint16_t op);
static void t11_bit_ind_ix(t11d *cpu, uint16_t op);
static void t11_bit_ind_ixd(t11d *cpu, uint16_t op);
static void t11_bit_de_rg(t11d *cpu, uint16_t op);
static void t11_bit_de_rgd(t11d *cpu, uint16_t op);
static void t11_bit_de_in(t11d *cpu, uint16_t op);
static void t11_bit_de_ind(t11d *cpu, uint16_t op);
static void t11_bit_de_de(t11d *cpu, uint16_t op);
static void t11_bit_de_ded(t11d *cpu, uint16_t op);
static void t11_bit_de_ix(t11d *cpu, uint16_t op);
static void t11_bit_de_ixd(t11d *cpu, uint16_t op);
static void t11_bit_ded_rg(t11d *cpu, uint16_t op);
static void t11_bit_ded_rgd(t11d *cpu, uint16_t op);
static void t11_bit_ded_in(t11d *cpu, uint16_t op);
static void t11_bit_ded_ind(t11d *cpu, uint16_t op);
static void t11_bit_ded_de(t11d *cpu, uint16_t op);
static void t11_bit_ded_ded(t11d *cpu, uint16_t op);
static void t11_bit_ded_ix(t11d *cpu, uint16_t op);
static void t11_bit_ded_ixd(t11d *cpu, uint16_t op);
static void t11_bit_ix_rg(t11d *cpu, uint16_t op);
static void t11_bit_ix_rgd(t11d *cpu, uint16_t op);
static void t11_bit_ix_in(t11d *cpu, uint16_t op);
static void t11_bit_ix_ind(t11d *cpu, uint16_t op);
static void t11_bit_ix_de(t11d *cpu, uint16_t op);
static void t11_bit_ix_ded(t11d *cpu, uint16_t op);
static void t11_bit_ix_ix(t11d *cpu, uint16_t op);
static void t11_bit_ix_ixd(t11d *cpu, uint16_t op);
static void t11_bit_ixd_rg(t11d *cpu, uint16_t op);
static void t11_bit_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_bit_ixd_in(t11d *cpu, uint16_t op);
static void t11_bit_ixd_ind(t11d *cpu, uint16_t op);
static void t11_bit_ixd_de(t11d *cpu, uint16_t op);
static void t11_bit_ixd_ded(t11d *cpu, uint16_t op);
static void t11_bit_ixd_ix(t11d *cpu, uint16_t op);
static void t11_bit_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_bic_rg_rg(t11d *cpu, uint16_t op);
static void t11_bic_rg_rgd(t11d *cpu, uint16_t op);
static void t11_bic_rg_in(t11d *cpu, uint16_t op);
static void t11_bic_rg_ind(t11d *cpu, uint16_t op);
static void t11_bic_rg_de(t11d *cpu, uint16_t op);
static void t11_bic_rg_ded(t11d *cpu, uint16_t op);
static void t11_bic_rg_ix(t11d *cpu, uint16_t op);
static void t11_bic_rg_ixd(t11d *cpu, uint16_t op);
static void t11_bic_rgd_rg(t11d *cpu, uint16_t op);
static void t11_bic_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_bic_rgd_in(t11d *cpu, uint16_t op);
static void t11_bic_rgd_ind(t11d *cpu, uint16_t op);
static void t11_bic_rgd_de(t11d *cpu, uint16_t op);
static void t11_bic_rgd_ded(t11d *cpu, uint16_t op);
static void t11_bic_rgd_ix(t11d *cpu, uint16_t op);
static void t11_bic_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_bic_in_rg(t11d *cpu, uint16_t op);
static void t11_bic_in_rgd(t11d *cpu, uint16_t op);
static void t11_bic_in_in(t11d *cpu, uint16_t op);
static void t11_bic_in_ind(t11d *cpu, uint16_t op);
static void t11_bic_in_de(t11d *cpu, uint16_t op);
static void t11_bic_in_ded(t11d *cpu, uint16_t op);
static void t11_bic_in_ix(t11d *cpu, uint16_t op);
static void t11_bic_in_ixd(t11d *cpu, uint16_t op);
static void t11_bic_ind_rg(t11d *cpu, uint16_t op);
static void t11_bic_ind_rgd(t11d *cpu, uint16_t op);
static void t11_bic_ind_in(t11d *cpu, uint16_t op);
static void t11_bic_ind_ind(t11d *cpu, uint16_t op);
static void t11_bic_ind_de(t11d *cpu, uint16_t op);
static void t11_bic_ind_ded(t11d *cpu, uint16_t op);
static void t11_bic_ind_ix(t11d *cpu, uint16_t op);
static void t11_bic_ind_ixd(t11d *cpu, uint16_t op);
static void t11_bic_de_rg(t11d *cpu, uint16_t op);
static void t11_bic_de_rgd(t11d *cpu, uint16_t op);
static void t11_bic_de_in(t11d *cpu, uint16_t op);
static void t11_bic_de_ind(t11d *cpu, uint16_t op);
static void t11_bic_de_de(t11d *cpu, uint16_t op);
static void t11_bic_de_ded(t11d *cpu, uint16_t op);
static void t11_bic_de_ix(t11d *cpu, uint16_t op);
static void t11_bic_de_ixd(t11d *cpu, uint16_t op);
static void t11_bic_ded_rg(t11d *cpu, uint16_t op);
static void t11_bic_ded_rgd(t11d *cpu, uint16_t op);
static void t11_bic_ded_in(t11d *cpu, uint16_t op);
static void t11_bic_ded_ind(t11d *cpu, uint16_t op);
static void t11_bic_ded_de(t11d *cpu, uint16_t op);
static void t11_bic_ded_ded(t11d *cpu, uint16_t op);
static void t11_bic_ded_ix(t11d *cpu, uint16_t op);
static void t11_bic_ded_ixd(t11d *cpu, uint16_t op);
static void t11_bic_ix_rg(t11d *cpu, uint16_t op);
static void t11_bic_ix_rgd(t11d *cpu, uint16_t op);
static void t11_bic_ix_in(t11d *cpu, uint16_t op);
static void t11_bic_ix_ind(t11d *cpu, uint16_t op);
static void t11_bic_ix_de(t11d *cpu, uint16_t op);
static void t11_bic_ix_ded(t11d *cpu, uint16_t op);
static void t11_bic_ix_ix(t11d *cpu, uint16_t op);
static void t11_bic_ix_ixd(t11d *cpu, uint16_t op);
static void t11_bic_ixd_rg(t11d *cpu, uint16_t op);
static void t11_bic_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_bic_ixd_in(t11d *cpu, uint16_t op);
static void t11_bic_ixd_ind(t11d *cpu, uint16_t op);
static void t11_bic_ixd_de(t11d *cpu, uint16_t op);
static void t11_bic_ixd_ded(t11d *cpu, uint16_t op);
static void t11_bic_ixd_ix(t11d *cpu, uint16_t op);
static void t11_bic_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_bis_rg_rg(t11d *cpu, uint16_t op);
static void t11_bis_rg_rgd(t11d *cpu, uint16_t op);
static void t11_bis_rg_in(t11d *cpu, uint16_t op);
static void t11_bis_rg_ind(t11d *cpu, uint16_t op);
static void t11_bis_rg_de(t11d *cpu, uint16_t op);
static void t11_bis_rg_ded(t11d *cpu, uint16_t op);
static void t11_bis_rg_ix(t11d *cpu, uint16_t op);
static void t11_bis_rg_ixd(t11d *cpu, uint16_t op);
static void t11_bis_rgd_rg(t11d *cpu, uint16_t op);
static void t11_bis_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_bis_rgd_in(t11d *cpu, uint16_t op);
static void t11_bis_rgd_ind(t11d *cpu, uint16_t op);
static void t11_bis_rgd_de(t11d *cpu, uint16_t op);
static void t11_bis_rgd_ded(t11d *cpu, uint16_t op);
static void t11_bis_rgd_ix(t11d *cpu, uint16_t op);
static void t11_bis_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_bis_in_rg(t11d *cpu, uint16_t op);
static void t11_bis_in_rgd(t11d *cpu, uint16_t op);
static void t11_bis_in_in(t11d *cpu, uint16_t op);
static void t11_bis_in_ind(t11d *cpu, uint16_t op);
static void t11_bis_in_de(t11d *cpu, uint16_t op);
static void t11_bis_in_ded(t11d *cpu, uint16_t op);
static void t11_bis_in_ix(t11d *cpu, uint16_t op);
static void t11_bis_in_ixd(t11d *cpu, uint16_t op);
static void t11_bis_ind_rg(t11d *cpu, uint16_t op);
static void t11_bis_ind_rgd(t11d *cpu, uint16_t op);
static void t11_bis_ind_in(t11d *cpu, uint16_t op);
static void t11_bis_ind_ind(t11d *cpu, uint16_t op);
static void t11_bis_ind_de(t11d *cpu, uint16_t op);
static void t11_bis_ind_ded(t11d *cpu, uint16_t op);
static void t11_bis_ind_ix(t11d *cpu, uint16_t op);
static void t11_bis_ind_ixd(t11d *cpu, uint16_t op);
static void t11_bis_de_rg(t11d *cpu, uint16_t op);
static void t11_bis_de_rgd(t11d *cpu, uint16_t op);
static void t11_bis_de_in(t11d *cpu, uint16_t op);
static void t11_bis_de_ind(t11d *cpu, uint16_t op);
static void t11_bis_de_de(t11d *cpu, uint16_t op);
static void t11_bis_de_ded(t11d *cpu, uint16_t op);
static void t11_bis_de_ix(t11d *cpu, uint16_t op);
static void t11_bis_de_ixd(t11d *cpu, uint16_t op);
static void t11_bis_ded_rg(t11d *cpu, uint16_t op);
static void t11_bis_ded_rgd(t11d *cpu, uint16_t op);
static void t11_bis_ded_in(t11d *cpu, uint16_t op);
static void t11_bis_ded_ind(t11d *cpu, uint16_t op);
static void t11_bis_ded_de(t11d *cpu, uint16_t op);
static void t11_bis_ded_ded(t11d *cpu, uint16_t op);
static void t11_bis_ded_ix(t11d *cpu, uint16_t op);
static void t11_bis_ded_ixd(t11d *cpu, uint16_t op);
static void t11_bis_ix_rg(t11d *cpu, uint16_t op);
static void t11_bis_ix_rgd(t11d *cpu, uint16_t op);
static void t11_bis_ix_in(t11d *cpu, uint16_t op);
static void t11_bis_ix_ind(t11d *cpu, uint16_t op);
static void t11_bis_ix_de(t11d *cpu, uint16_t op);
static void t11_bis_ix_ded(t11d *cpu, uint16_t op);
static void t11_bis_ix_ix(t11d *cpu, uint16_t op);
static void t11_bis_ix_ixd(t11d *cpu, uint16_t op);
static void t11_bis_ixd_rg(t11d *cpu, uint16_t op);
static void t11_bis_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_bis_ixd_in(t11d *cpu, uint16_t op);
static void t11_bis_ixd_ind(t11d *cpu, uint16_t op);
static void t11_bis_ixd_de(t11d *cpu, uint16_t op);
static void t11_bis_ixd_ded(t11d *cpu, uint16_t op);
static void t11_bis_ixd_ix(t11d *cpu, uint16_t op);
static void t11_bis_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_add_rg_rg(t11d *cpu, uint16_t op);
static void t11_add_rg_rgd(t11d *cpu, uint16_t op);
static void t11_add_rg_in(t11d *cpu, uint16_t op);
static void t11_add_rg_ind(t11d *cpu, uint16_t op);
static void t11_add_rg_de(t11d *cpu, uint16_t op);
static void t11_add_rg_ded(t11d *cpu, uint16_t op);
static void t11_add_rg_ix(t11d *cpu, uint16_t op);
static void t11_add_rg_ixd(t11d *cpu, uint16_t op);
static void t11_add_rgd_rg(t11d *cpu, uint16_t op);
static void t11_add_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_add_rgd_in(t11d *cpu, uint16_t op);
static void t11_add_rgd_ind(t11d *cpu, uint16_t op);
static void t11_add_rgd_de(t11d *cpu, uint16_t op);
static void t11_add_rgd_ded(t11d *cpu, uint16_t op);
static void t11_add_rgd_ix(t11d *cpu, uint16_t op);
static void t11_add_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_add_in_rg(t11d *cpu, uint16_t op);
static void t11_add_in_rgd(t11d *cpu, uint16_t op);
static void t11_add_in_in(t11d *cpu, uint16_t op);
static void t11_add_in_ind(t11d *cpu, uint16_t op);
static void t11_add_in_de(t11d *cpu, uint16_t op);
static void t11_add_in_ded(t11d *cpu, uint16_t op);
static void t11_add_in_ix(t11d *cpu, uint16_t op);
static void t11_add_in_ixd(t11d *cpu, uint16_t op);
static void t11_add_ind_rg(t11d *cpu, uint16_t op);
static void t11_add_ind_rgd(t11d *cpu, uint16_t op);
static void t11_add_ind_in(t11d *cpu, uint16_t op);
static void t11_add_ind_ind(t11d *cpu, uint16_t op);
static void t11_add_ind_de(t11d *cpu, uint16_t op);
static void t11_add_ind_ded(t11d *cpu, uint16_t op);
static void t11_add_ind_ix(t11d *cpu, uint16_t op);
static void t11_add_ind_ixd(t11d *cpu, uint16_t op);
static void t11_add_de_rg(t11d *cpu, uint16_t op);
static void t11_add_de_rgd(t11d *cpu, uint16_t op);
static void t11_add_de_in(t11d *cpu, uint16_t op);
static void t11_add_de_ind(t11d *cpu, uint16_t op);
static void t11_add_de_de(t11d *cpu, uint16_t op);
static void t11_add_de_ded(t11d *cpu, uint16_t op);
static void t11_add_de_ix(t11d *cpu, uint16_t op);
static void t11_add_de_ixd(t11d *cpu, uint16_t op);
static void t11_add_ded_rg(t11d *cpu, uint16_t op);
static void t11_add_ded_rgd(t11d *cpu, uint16_t op);
static void t11_add_ded_in(t11d *cpu, uint16_t op);
static void t11_add_ded_ind(t11d *cpu, uint16_t op);
static void t11_add_ded_de(t11d *cpu, uint16_t op);
static void t11_add_ded_ded(t11d *cpu, uint16_t op);
static void t11_add_ded_ix(t11d *cpu, uint16_t op);
static void t11_add_ded_ixd(t11d *cpu, uint16_t op);
static void t11_add_ix_rg(t11d *cpu, uint16_t op);
static void t11_add_ix_rgd(t11d *cpu, uint16_t op);
static void t11_add_ix_in(t11d *cpu, uint16_t op);
static void t11_add_ix_ind(t11d *cpu, uint16_t op);
static void t11_add_ix_de(t11d *cpu, uint16_t op);
static void t11_add_ix_ded(t11d *cpu, uint16_t op);
static void t11_add_ix_ix(t11d *cpu, uint16_t op);
static void t11_add_ix_ixd(t11d *cpu, uint16_t op);
static void t11_add_ixd_rg(t11d *cpu, uint16_t op);
static void t11_add_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_add_ixd_in(t11d *cpu, uint16_t op);
static void t11_add_ixd_ind(t11d *cpu, uint16_t op);
static void t11_add_ixd_de(t11d *cpu, uint16_t op);
static void t11_add_ixd_ded(t11d *cpu, uint16_t op);
static void t11_add_ixd_ix(t11d *cpu, uint16_t op);
static void t11_add_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_xor_rg(t11d *cpu, uint16_t op);
static void t11_xor_rgd(t11d *cpu, uint16_t op);
static void t11_xor_in(t11d *cpu, uint16_t op);
static void t11_xor_ind(t11d *cpu, uint16_t op);
static void t11_xor_de(t11d *cpu, uint16_t op);
static void t11_xor_ded(t11d *cpu, uint16_t op);
static void t11_xor_ix(t11d *cpu, uint16_t op);
static void t11_xor_ixd(t11d *cpu, uint16_t op);
static void t11_sob(t11d *cpu, uint16_t op);
static void t11_bpl(t11d *cpu, uint16_t op);
static void t11_bmi(t11d *cpu, uint16_t op);
static void t11_bhi(t11d *cpu, uint16_t op);
static void t11_blos(t11d *cpu, uint16_t op);
static void t11_bvc(t11d *cpu, uint16_t op);
static void t11_bvs(t11d *cpu, uint16_t op);
static void t11_bcc(t11d *cpu, uint16_t op);
static void t11_bcs(t11d *cpu, uint16_t op);
static void t11_emt(t11d *cpu, uint16_t op);
static void t11_trap(t11d *cpu, uint16_t op);
static void t11_clrb_rg(t11d *cpu, uint16_t op);
static void t11_clrb_rgd(t11d *cpu, uint16_t op);
static void t11_clrb_in(t11d *cpu, uint16_t op);
static void t11_clrb_ind(t11d *cpu, uint16_t op);
static void t11_clrb_de(t11d *cpu, uint16_t op);
static void t11_clrb_ded(t11d *cpu, uint16_t op);
static void t11_clrb_ix(t11d *cpu, uint16_t op);
static void t11_clrb_ixd(t11d *cpu, uint16_t op);
static void t11_comb_rg(t11d *cpu, uint16_t op);
static void t11_comb_rgd(t11d *cpu, uint16_t op);
static void t11_comb_in(t11d *cpu, uint16_t op);
static void t11_comb_ind(t11d *cpu, uint16_t op);
static void t11_comb_de(t11d *cpu, uint16_t op);
static void t11_comb_ded(t11d *cpu, uint16_t op);
static void t11_comb_ix(t11d *cpu, uint16_t op);
static void t11_comb_ixd(t11d *cpu, uint16_t op);
static void t11_incb_rg(t11d *cpu, uint16_t op);
static void t11_incb_rgd(t11d *cpu, uint16_t op);
static void t11_incb_in(t11d *cpu, uint16_t op);
static void t11_incb_ind(t11d *cpu, uint16_t op);
static void t11_incb_de(t11d *cpu, uint16_t op);
static void t11_incb_ded(t11d *cpu, uint16_t op);
static void t11_incb_ix(t11d *cpu, uint16_t op);
static void t11_incb_ixd(t11d *cpu, uint16_t op);
static void t11_decb_rg(t11d *cpu, uint16_t op);
static void t11_decb_rgd(t11d *cpu, uint16_t op);
static void t11_decb_in(t11d *cpu, uint16_t op);
static void t11_decb_ind(t11d *cpu, uint16_t op);
static void t11_decb_de(t11d *cpu, uint16_t op);
static void t11_decb_ded(t11d *cpu, uint16_t op);
static void t11_decb_ix(t11d *cpu, uint16_t op);
static void t11_decb_ixd(t11d *cpu, uint16_t op);
static void t11_negb_rg(t11d *cpu, uint16_t op);
static void t11_negb_rgd(t11d *cpu, uint16_t op);
static void t11_negb_in(t11d *cpu, uint16_t op);
static void t11_negb_ind(t11d *cpu, uint16_t op);
static void t11_negb_de(t11d *cpu, uint16_t op);
static void t11_negb_ded(t11d *cpu, uint16_t op);
static void t11_negb_ix(t11d *cpu, uint16_t op);
static void t11_negb_ixd(t11d *cpu, uint16_t op);
static void t11_adcb_rg(t11d *cpu, uint16_t op);
static void t11_adcb_rgd(t11d *cpu, uint16_t op);
static void t11_adcb_in(t11d *cpu, uint16_t op);
static void t11_adcb_ind(t11d *cpu, uint16_t op);
static void t11_adcb_de(t11d *cpu, uint16_t op);
static void t11_adcb_ded(t11d *cpu, uint16_t op);
static void t11_adcb_ix(t11d *cpu, uint16_t op);
static void t11_adcb_ixd(t11d *cpu, uint16_t op);
static void t11_sbcb_rg(t11d *cpu, uint16_t op);
static void t11_sbcb_rgd(t11d *cpu, uint16_t op);
static void t11_sbcb_in(t11d *cpu, uint16_t op);
static void t11_sbcb_ind(t11d *cpu, uint16_t op);
static void t11_sbcb_de(t11d *cpu, uint16_t op);
static void t11_sbcb_ded(t11d *cpu, uint16_t op);
static void t11_sbcb_ix(t11d *cpu, uint16_t op);
static void t11_sbcb_ixd(t11d *cpu, uint16_t op);
static void t11_tstb_rg(t11d *cpu, uint16_t op);
static void t11_tstb_rgd(t11d *cpu, uint16_t op);
static void t11_tstb_in(t11d *cpu, uint16_t op);
static void t11_tstb_ind(t11d *cpu, uint16_t op);
static void t11_tstb_de(t11d *cpu, uint16_t op);
static void t11_tstb_ded(t11d *cpu, uint16_t op);
static void t11_tstb_ix(t11d *cpu, uint16_t op);
static void t11_tstb_ixd(t11d *cpu, uint16_t op);
static void t11_rorb_rg(t11d *cpu, uint16_t op);
static void t11_rorb_rgd(t11d *cpu, uint16_t op);
static void t11_rorb_in(t11d *cpu, uint16_t op);
static void t11_rorb_ind(t11d *cpu, uint16_t op);
static void t11_rorb_de(t11d *cpu, uint16_t op);
static void t11_rorb_ded(t11d *cpu, uint16_t op);
static void t11_rorb_ix(t11d *cpu, uint16_t op);
static void t11_rorb_ixd(t11d *cpu, uint16_t op);
static void t11_rolb_rg(t11d *cpu, uint16_t op);
static void t11_rolb_rgd(t11d *cpu, uint16_t op);
static void t11_rolb_in(t11d *cpu, uint16_t op);
static void t11_rolb_ind(t11d *cpu, uint16_t op);
static void t11_rolb_de(t11d *cpu, uint16_t op);
static void t11_rolb_ded(t11d *cpu, uint16_t op);
static void t11_rolb_ix(t11d *cpu, uint16_t op);
static void t11_rolb_ixd(t11d *cpu, uint16_t op);
static void t11_asrb_rg(t11d *cpu, uint16_t op);
static void t11_asrb_rgd(t11d *cpu, uint16_t op);
static void t11_asrb_in(t11d *cpu, uint16_t op);
static void t11_asrb_ind(t11d *cpu, uint16_t op);
static void t11_asrb_de(t11d *cpu, uint16_t op);
static void t11_asrb_ded(t11d *cpu, uint16_t op);
static void t11_asrb_ix(t11d *cpu, uint16_t op);
static void t11_asrb_ixd(t11d *cpu, uint16_t op);
static void t11_aslb_rg(t11d *cpu, uint16_t op);
static void t11_aslb_rgd(t11d *cpu, uint16_t op);
static void t11_aslb_in(t11d *cpu, uint16_t op);
static void t11_aslb_ind(t11d *cpu, uint16_t op);
static void t11_aslb_de(t11d *cpu, uint16_t op);
static void t11_aslb_ded(t11d *cpu, uint16_t op);
static void t11_aslb_ix(t11d *cpu, uint16_t op);
static void t11_aslb_ixd(t11d *cpu, uint16_t op);
static void t11_mtps_rg(t11d *cpu, uint16_t op);
static void t11_mtps_rgd(t11d *cpu, uint16_t op);
static void t11_mtps_in(t11d *cpu, uint16_t op);
static void t11_mtps_ind(t11d *cpu, uint16_t op);
static void t11_mtps_de(t11d *cpu, uint16_t op);
static void t11_mtps_ded(t11d *cpu, uint16_t op);
static void t11_mtps_ix(t11d *cpu, uint16_t op);
static void t11_mtps_ixd(t11d *cpu, uint16_t op);
static void t11_mfps_rg(t11d *cpu, uint16_t op);
static void t11_mfps_rgd(t11d *cpu, uint16_t op);
static void t11_mfps_in(t11d *cpu, uint16_t op);
static void t11_mfps_ind(t11d *cpu, uint16_t op);
static void t11_mfps_de(t11d *cpu, uint16_t op);
static void t11_mfps_ded(t11d *cpu, uint16_t op);
static void t11_mfps_ix(t11d *cpu, uint16_t op);
static void t11_mfps_ixd(t11d *cpu, uint16_t op);
static void t11_movb_rg_rg(t11d *cpu, uint16_t op);
static void t11_movb_rg_rgd(t11d *cpu, uint16_t op);
static void t11_movb_rg_in(t11d *cpu, uint16_t op);
static void t11_movb_rg_ind(t11d *cpu, uint16_t op);
static void t11_movb_rg_de(t11d *cpu, uint16_t op);
static void t11_movb_rg_ded(t11d *cpu, uint16_t op);
static void t11_movb_rg_ix(t11d *cpu, uint16_t op);
static void t11_movb_rg_ixd(t11d *cpu, uint16_t op);
static void t11_movb_rgd_rg(t11d *cpu, uint16_t op);
static void t11_movb_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_movb_rgd_in(t11d *cpu, uint16_t op);
static void t11_movb_rgd_ind(t11d *cpu, uint16_t op);
static void t11_movb_rgd_de(t11d *cpu, uint16_t op);
static void t11_movb_rgd_ded(t11d *cpu, uint16_t op);
static void t11_movb_rgd_ix(t11d *cpu, uint16_t op);
static void t11_movb_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_movb_in_rg(t11d *cpu, uint16_t op);
static void t11_movb_in_rgd(t11d *cpu, uint16_t op);
static void t11_movb_in_in(t11d *cpu, uint16_t op);
static void t11_movb_in_ind(t11d *cpu, uint16_t op);
static void t11_movb_in_de(t11d *cpu, uint16_t op);
static void t11_movb_in_ded(t11d *cpu, uint16_t op);
static void t11_movb_in_ix(t11d *cpu, uint16_t op);
static void t11_movb_in_ixd(t11d *cpu, uint16_t op);
static void t11_movb_ind_rg(t11d *cpu, uint16_t op);
static void t11_movb_ind_rgd(t11d *cpu, uint16_t op);
static void t11_movb_ind_in(t11d *cpu, uint16_t op);
static void t11_movb_ind_ind(t11d *cpu, uint16_t op);
static void t11_movb_ind_de(t11d *cpu, uint16_t op);
static void t11_movb_ind_ded(t11d *cpu, uint16_t op);
static void t11_movb_ind_ix(t11d *cpu, uint16_t op);
static void t11_movb_ind_ixd(t11d *cpu, uint16_t op);
static void t11_movb_de_rg(t11d *cpu, uint16_t op);
static void t11_movb_de_rgd(t11d *cpu, uint16_t op);
static void t11_movb_de_in(t11d *cpu, uint16_t op);
static void t11_movb_de_ind(t11d *cpu, uint16_t op);
static void t11_movb_de_de(t11d *cpu, uint16_t op);
static void t11_movb_de_ded(t11d *cpu, uint16_t op);
static void t11_movb_de_ix(t11d *cpu, uint16_t op);
static void t11_movb_de_ixd(t11d *cpu, uint16_t op);
static void t11_movb_ded_rg(t11d *cpu, uint16_t op);
static void t11_movb_ded_rgd(t11d *cpu, uint16_t op);
static void t11_movb_ded_in(t11d *cpu, uint16_t op);
static void t11_movb_ded_ind(t11d *cpu, uint16_t op);
static void t11_movb_ded_de(t11d *cpu, uint16_t op);
static void t11_movb_ded_ded(t11d *cpu, uint16_t op);
static void t11_movb_ded_ix(t11d *cpu, uint16_t op);
static void t11_movb_ded_ixd(t11d *cpu, uint16_t op);
static void t11_movb_ix_rg(t11d *cpu, uint16_t op);
static void t11_movb_ix_rgd(t11d *cpu, uint16_t op);
static void t11_movb_ix_in(t11d *cpu, uint16_t op);
static void t11_movb_ix_ind(t11d *cpu, uint16_t op);
static void t11_movb_ix_de(t11d *cpu, uint16_t op);
static void t11_movb_ix_ded(t11d *cpu, uint16_t op);
static void t11_movb_ix_ix(t11d *cpu, uint16_t op);
static void t11_movb_ix_ixd(t11d *cpu, uint16_t op);
static void t11_movb_ixd_rg(t11d *cpu, uint16_t op);
static void t11_movb_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_movb_ixd_in(t11d *cpu, uint16_t op);
static void t11_movb_ixd_ind(t11d *cpu, uint16_t op);
static void t11_movb_ixd_de(t11d *cpu, uint16_t op);
static void t11_movb_ixd_ded(t11d *cpu, uint16_t op);
static void t11_movb_ixd_ix(t11d *cpu, uint16_t op);
static void t11_movb_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_in(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_de(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_rg_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_in(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_de(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_in_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_in_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_in_in(t11d *cpu, uint16_t op);
static void t11_cmpb_in_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_in_de(t11d *cpu, uint16_t op);
static void t11_cmpb_in_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_in_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_in_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_in(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_de(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_ind_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_de_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_de_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_de_in(t11d *cpu, uint16_t op);
static void t11_cmpb_de_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_de_de(t11d *cpu, uint16_t op);
static void t11_cmpb_de_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_de_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_de_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_in(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_de(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_ded_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_in(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_de(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_ix_ixd(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_rg(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_in(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_ind(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_de(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_ded(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_ix(t11d *cpu, uint16_t op);
static void t11_cmpb_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_rg_rg(t11d *cpu, uint16_t op);
static void t11_bitb_rg_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_rg_in(t11d *cpu, uint16_t op);
static void t11_bitb_rg_ind(t11d *cpu, uint16_t op);
static void t11_bitb_rg_de(t11d *cpu, uint16_t op);
static void t11_bitb_rg_ded(t11d *cpu, uint16_t op);
static void t11_bitb_rg_ix(t11d *cpu, uint16_t op);
static void t11_bitb_rg_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_rg(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_in(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_ind(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_de(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_ded(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_ix(t11d *cpu, uint16_t op);
static void t11_bitb_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_in_rg(t11d *cpu, uint16_t op);
static void t11_bitb_in_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_in_in(t11d *cpu, uint16_t op);
static void t11_bitb_in_ind(t11d *cpu, uint16_t op);
static void t11_bitb_in_de(t11d *cpu, uint16_t op);
static void t11_bitb_in_ded(t11d *cpu, uint16_t op);
static void t11_bitb_in_ix(t11d *cpu, uint16_t op);
static void t11_bitb_in_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_ind_rg(t11d *cpu, uint16_t op);
static void t11_bitb_ind_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_ind_in(t11d *cpu, uint16_t op);
static void t11_bitb_ind_ind(t11d *cpu, uint16_t op);
static void t11_bitb_ind_de(t11d *cpu, uint16_t op);
static void t11_bitb_ind_ded(t11d *cpu, uint16_t op);
static void t11_bitb_ind_ix(t11d *cpu, uint16_t op);
static void t11_bitb_ind_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_de_rg(t11d *cpu, uint16_t op);
static void t11_bitb_de_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_de_in(t11d *cpu, uint16_t op);
static void t11_bitb_de_ind(t11d *cpu, uint16_t op);
static void t11_bitb_de_de(t11d *cpu, uint16_t op);
static void t11_bitb_de_ded(t11d *cpu, uint16_t op);
static void t11_bitb_de_ix(t11d *cpu, uint16_t op);
static void t11_bitb_de_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_ded_rg(t11d *cpu, uint16_t op);
static void t11_bitb_ded_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_ded_in(t11d *cpu, uint16_t op);
static void t11_bitb_ded_ind(t11d *cpu, uint16_t op);
static void t11_bitb_ded_de(t11d *cpu, uint16_t op);
static void t11_bitb_ded_ded(t11d *cpu, uint16_t op);
static void t11_bitb_ded_ix(t11d *cpu, uint16_t op);
static void t11_bitb_ded_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_ix_rg(t11d *cpu, uint16_t op);
static void t11_bitb_ix_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_ix_in(t11d *cpu, uint16_t op);
static void t11_bitb_ix_ind(t11d *cpu, uint16_t op);
static void t11_bitb_ix_de(t11d *cpu, uint16_t op);
static void t11_bitb_ix_ded(t11d *cpu, uint16_t op);
static void t11_bitb_ix_ix(t11d *cpu, uint16_t op);
static void t11_bitb_ix_ixd(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_rg(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_in(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_ind(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_de(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_ded(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_ix(t11d *cpu, uint16_t op);
static void t11_bitb_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_rg_rg(t11d *cpu, uint16_t op);
static void t11_bicb_rg_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_rg_in(t11d *cpu, uint16_t op);
static void t11_bicb_rg_ind(t11d *cpu, uint16_t op);
static void t11_bicb_rg_de(t11d *cpu, uint16_t op);
static void t11_bicb_rg_ded(t11d *cpu, uint16_t op);
static void t11_bicb_rg_ix(t11d *cpu, uint16_t op);
static void t11_bicb_rg_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_rg(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_in(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_ind(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_de(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_ded(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_ix(t11d *cpu, uint16_t op);
static void t11_bicb_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_in_rg(t11d *cpu, uint16_t op);
static void t11_bicb_in_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_in_in(t11d *cpu, uint16_t op);
static void t11_bicb_in_ind(t11d *cpu, uint16_t op);
static void t11_bicb_in_de(t11d *cpu, uint16_t op);
static void t11_bicb_in_ded(t11d *cpu, uint16_t op);
static void t11_bicb_in_ix(t11d *cpu, uint16_t op);
static void t11_bicb_in_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_ind_rg(t11d *cpu, uint16_t op);
static void t11_bicb_ind_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_ind_in(t11d *cpu, uint16_t op);
static void t11_bicb_ind_ind(t11d *cpu, uint16_t op);
static void t11_bicb_ind_de(t11d *cpu, uint16_t op);
static void t11_bicb_ind_ded(t11d *cpu, uint16_t op);
static void t11_bicb_ind_ix(t11d *cpu, uint16_t op);
static void t11_bicb_ind_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_de_rg(t11d *cpu, uint16_t op);
static void t11_bicb_de_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_de_in(t11d *cpu, uint16_t op);
static void t11_bicb_de_ind(t11d *cpu, uint16_t op);
static void t11_bicb_de_de(t11d *cpu, uint16_t op);
static void t11_bicb_de_ded(t11d *cpu, uint16_t op);
static void t11_bicb_de_ix(t11d *cpu, uint16_t op);
static void t11_bicb_de_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_ded_rg(t11d *cpu, uint16_t op);
static void t11_bicb_ded_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_ded_in(t11d *cpu, uint16_t op);
static void t11_bicb_ded_ind(t11d *cpu, uint16_t op);
static void t11_bicb_ded_de(t11d *cpu, uint16_t op);
static void t11_bicb_ded_ded(t11d *cpu, uint16_t op);
static void t11_bicb_ded_ix(t11d *cpu, uint16_t op);
static void t11_bicb_ded_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_ix_rg(t11d *cpu, uint16_t op);
static void t11_bicb_ix_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_ix_in(t11d *cpu, uint16_t op);
static void t11_bicb_ix_ind(t11d *cpu, uint16_t op);
static void t11_bicb_ix_de(t11d *cpu, uint16_t op);
static void t11_bicb_ix_ded(t11d *cpu, uint16_t op);
static void t11_bicb_ix_ix(t11d *cpu, uint16_t op);
static void t11_bicb_ix_ixd(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_rg(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_in(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_ind(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_de(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_ded(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_ix(t11d *cpu, uint16_t op);
static void t11_bicb_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_rg_rg(t11d *cpu, uint16_t op);
static void t11_bisb_rg_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_rg_in(t11d *cpu, uint16_t op);
static void t11_bisb_rg_ind(t11d *cpu, uint16_t op);
static void t11_bisb_rg_de(t11d *cpu, uint16_t op);
static void t11_bisb_rg_ded(t11d *cpu, uint16_t op);
static void t11_bisb_rg_ix(t11d *cpu, uint16_t op);
static void t11_bisb_rg_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_rg(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_in(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_ind(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_de(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_ded(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_ix(t11d *cpu, uint16_t op);
static void t11_bisb_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_in_rg(t11d *cpu, uint16_t op);
static void t11_bisb_in_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_in_in(t11d *cpu, uint16_t op);
static void t11_bisb_in_ind(t11d *cpu, uint16_t op);
static void t11_bisb_in_de(t11d *cpu, uint16_t op);
static void t11_bisb_in_ded(t11d *cpu, uint16_t op);
static void t11_bisb_in_ix(t11d *cpu, uint16_t op);
static void t11_bisb_in_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_ind_rg(t11d *cpu, uint16_t op);
static void t11_bisb_ind_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_ind_in(t11d *cpu, uint16_t op);
static void t11_bisb_ind_ind(t11d *cpu, uint16_t op);
static void t11_bisb_ind_de(t11d *cpu, uint16_t op);
static void t11_bisb_ind_ded(t11d *cpu, uint16_t op);
static void t11_bisb_ind_ix(t11d *cpu, uint16_t op);
static void t11_bisb_ind_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_de_rg(t11d *cpu, uint16_t op);
static void t11_bisb_de_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_de_in(t11d *cpu, uint16_t op);
static void t11_bisb_de_ind(t11d *cpu, uint16_t op);
static void t11_bisb_de_de(t11d *cpu, uint16_t op);
static void t11_bisb_de_ded(t11d *cpu, uint16_t op);
static void t11_bisb_de_ix(t11d *cpu, uint16_t op);
static void t11_bisb_de_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_ded_rg(t11d *cpu, uint16_t op);
static void t11_bisb_ded_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_ded_in(t11d *cpu, uint16_t op);
static void t11_bisb_ded_ind(t11d *cpu, uint16_t op);
static void t11_bisb_ded_de(t11d *cpu, uint16_t op);
static void t11_bisb_ded_ded(t11d *cpu, uint16_t op);
static void t11_bisb_ded_ix(t11d *cpu, uint16_t op);
static void t11_bisb_ded_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_ix_rg(t11d *cpu, uint16_t op);
static void t11_bisb_ix_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_ix_in(t11d *cpu, uint16_t op);
static void t11_bisb_ix_ind(t11d *cpu, uint16_t op);
static void t11_bisb_ix_de(t11d *cpu, uint16_t op);
static void t11_bisb_ix_ded(t11d *cpu, uint16_t op);
static void t11_bisb_ix_ix(t11d *cpu, uint16_t op);
static void t11_bisb_ix_ixd(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_rg(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_in(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_ind(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_de(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_ded(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_ix(t11d *cpu, uint16_t op);
static void t11_bisb_ixd_ixd(t11d *cpu, uint16_t op);
static void t11_sub_rg_rg(t11d *cpu, uint16_t op);
static void t11_sub_rg_rgd(t11d *cpu, uint16_t op);
static void t11_sub_rg_in(t11d *cpu, uint16_t op);
static void t11_sub_rg_ind(t11d *cpu, uint16_t op);
static void t11_sub_rg_de(t11d *cpu, uint16_t op);
static void t11_sub_rg_ded(t11d *cpu, uint16_t op);
static void t11_sub_rg_ix(t11d *cpu, uint16_t op);
static void t11_sub_rg_ixd(t11d *cpu, uint16_t op);
static void t11_sub_rgd_rg(t11d *cpu, uint16_t op);
static void t11_sub_rgd_rgd(t11d *cpu, uint16_t op);
static void t11_sub_rgd_in(t11d *cpu, uint16_t op);
static void t11_sub_rgd_ind(t11d *cpu, uint16_t op);
static void t11_sub_rgd_de(t11d *cpu, uint16_t op);
static void t11_sub_rgd_ded(t11d *cpu, uint16_t op);
static void t11_sub_rgd_ix(t11d *cpu, uint16_t op);
static void t11_sub_rgd_ixd(t11d *cpu, uint16_t op);
static void t11_sub_in_rg(t11d *cpu, uint16_t op);
static void t11_sub_in_rgd(t11d *cpu, uint16_t op);
static void t11_sub_in_in(t11d *cpu, uint16_t op);
static void t11_sub_in_ind(t11d *cpu, uint16_t op);
static void t11_sub_in_de(t11d *cpu, uint16_t op);
static void t11_sub_in_ded(t11d *cpu, uint16_t op);
static void t11_sub_in_ix(t11d *cpu, uint16_t op);
static void t11_sub_in_ixd(t11d *cpu, uint16_t op);
static void t11_sub_ind_rg(t11d *cpu, uint16_t op);
static void t11_sub_ind_rgd(t11d *cpu, uint16_t op);
static void t11_sub_ind_in(t11d *cpu, uint16_t op);
static void t11_sub_ind_ind(t11d *cpu, uint16_t op);
static void t11_sub_ind_de(t11d *cpu, uint16_t op);
static void t11_sub_ind_ded(t11d *cpu, uint16_t op);
static void t11_sub_ind_ix(t11d *cpu, uint16_t op);
static void t11_sub_ind_ixd(t11d *cpu, uint16_t op);
static void t11_sub_de_rg(t11d *cpu, uint16_t op);
static void t11_sub_de_rgd(t11d *cpu, uint16_t op);
static void t11_sub_de_in(t11d *cpu, uint16_t op);
static void t11_sub_de_ind(t11d *cpu, uint16_t op);
static void t11_sub_de_de(t11d *cpu, uint16_t op);
static void t11_sub_de_ded(t11d *cpu, uint16_t op);
static void t11_sub_de_ix(t11d *cpu, uint16_t op);
static void t11_sub_de_ixd(t11d *cpu, uint16_t op);
static void t11_sub_ded_rg(t11d *cpu, uint16_t op);
static void t11_sub_ded_rgd(t11d *cpu, uint16_t op);
static void t11_sub_ded_in(t11d *cpu, uint16_t op);
static void t11_sub_ded_ind(t11d *cpu, uint16_t op);
static void t11_sub_ded_de(t11d *cpu, uint16_t op);
static void t11_sub_ded_ded(t11d *cpu, uint16_t op);
static void t11_sub_ded_ix(t11d *cpu, uint16_t op);
static void t11_sub_ded_ixd(t11d *cpu, uint16_t op);
static void t11_sub_ix_rg(t11d *cpu, uint16_t op);
static void t11_sub_ix_rgd(t11d *cpu, uint16_t op);
static void t11_sub_ix_in(t11d *cpu, uint16_t op);
static void t11_sub_ix_ind(t11d *cpu, uint16_t op);
static void t11_sub_ix_de(t11d *cpu, uint16_t op);
static void t11_sub_ix_ded(t11d *cpu, uint16_t op);
static void t11_sub_ix_ix(t11d *cpu, uint16_t op);
static void t11_sub_ix_ixd(t11d *cpu, uint16_t op);
static void t11_sub_ixd_rg(t11d *cpu, uint16_t op);
static void t11_sub_ixd_rgd(t11d *cpu, uint16_t op);
static void t11_sub_ixd_in(t11d *cpu, uint16_t op);
static void t11_sub_ixd_ind(t11d *cpu, uint16_t op);
static void t11_sub_ixd_de(t11d *cpu, uint16_t op);
static void t11_sub_ixd_ded(t11d *cpu, uint16_t op);
static void t11_sub_ixd_ix(t11d *cpu, uint16_t op);
static void t11_sub_ixd_ixd(t11d *cpu, uint16_t op);

// DEC T-11 opcode dispatch table, ported from MAME's t11table.hxx
// (license:BSD-3-Clause, copyright-holders:Aaron Giles).


/*

modes:
  rg = register
  rgd = register deferred
  in = increment
  ind = increment deferred
  de = decrement
  ded = decrement deferred
  ix = index
  ixd = index deferred

*/

#define OP(x)  t11_##x

static const t11_op s_opcode_table[65536 >> 3] =
{
	/* 0x0000 */
	OP(op_0000),    OP(halt),       OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal4),   OP(jmp_rgd),    OP(jmp_in),     OP(jmp_ind),    OP(jmp_de),     OP(jmp_ded),    OP(jmp_ix),     OP(jmp_ixd),
	OP(rts),        OP(illegal),    OP(illegal),    OP(illegal),    OP(ccc),        OP(ccc),        OP(scc),        OP(scc),
	OP(swab_rg),    OP(swab_rgd),   OP(swab_in),    OP(swab_ind),   OP(swab_de),    OP(swab_ded),   OP(swab_ix),    OP(swab_ixd),
	/* 0x0100 */
	OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),
	OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),
	OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),
	OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),         OP(br),
	/* 0x0200 */
	OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),
	OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),
	OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),
	OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),        OP(bne),
	/* 0x0300 */
	OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),
	OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),
	OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),
	OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),        OP(beq),
	/* 0x0400 */
	OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),
	OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),
	OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),
	OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),        OP(bge),
	/* 0x0500 */
	OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),
	OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),
	OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),
	OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),        OP(blt),
	/* 0x0600 */
	OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),
	OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),
	OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),
	OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),        OP(bgt),
	/* 0x0700 */
	OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),
	OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),
	OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),
	OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),        OP(ble),
	/* 0x0800 */
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	/* 0x0900 */
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	OP(illegal4),   OP(jsr_rgd),    OP(jsr_in),     OP(jsr_ind),    OP(jsr_de),     OP(jsr_ded),    OP(jsr_ix),     OP(jsr_ixd),
	/* 0x0a00 */
	OP(clr_rg),     OP(clr_rgd),    OP(clr_in),     OP(clr_ind),    OP(clr_de),     OP(clr_ded),    OP(clr_ix),     OP(clr_ixd),
	OP(com_rg),     OP(com_rgd),    OP(com_in),     OP(com_ind),    OP(com_de),     OP(com_ded),    OP(com_ix),     OP(com_ixd),
	OP(inc_rg),     OP(inc_rgd),    OP(inc_in),     OP(inc_ind),    OP(inc_de),     OP(inc_ded),    OP(inc_ix),     OP(inc_ixd),
	OP(dec_rg),     OP(dec_rgd),    OP(dec_in),     OP(dec_ind),    OP(dec_de),     OP(dec_ded),    OP(dec_ix),     OP(dec_ixd),
	/* 0x0b00 */
	OP(neg_rg),     OP(neg_rgd),    OP(neg_in),     OP(neg_ind),    OP(neg_de),     OP(neg_ded),    OP(neg_ix),     OP(neg_ixd),
	OP(adc_rg),     OP(adc_rgd),    OP(adc_in),     OP(adc_ind),    OP(adc_de),     OP(adc_ded),    OP(adc_ix),     OP(adc_ixd),
	OP(sbc_rg),     OP(sbc_rgd),    OP(sbc_in),     OP(sbc_ind),    OP(sbc_de),     OP(sbc_ded),    OP(sbc_ix),     OP(sbc_ixd),
	OP(tst_rg),     OP(tst_rgd),    OP(tst_in),     OP(tst_ind),    OP(tst_de),     OP(tst_ded),    OP(tst_ix),     OP(tst_ixd),
	/* 0x0c00 */
	OP(ror_rg),     OP(ror_rgd),    OP(ror_in),     OP(ror_ind),    OP(ror_de),     OP(ror_ded),    OP(ror_ix),     OP(ror_ixd),
	OP(rol_rg),     OP(rol_rgd),    OP(rol_in),     OP(rol_ind),    OP(rol_de),     OP(rol_ded),    OP(rol_ix),     OP(rol_ixd),
	OP(asr_rg),     OP(asr_rgd),    OP(asr_in),     OP(asr_ind),    OP(asr_de),     OP(asr_ded),    OP(asr_ix),     OP(asr_ixd),
	OP(asl_rg),     OP(asl_rgd),    OP(asl_in),     OP(asl_ind),    OP(asl_de),     OP(asl_ded),    OP(asl_ix),     OP(asl_ixd),
	/* 0x0d00 */
	OP(mark),       OP(mark),       OP(mark),       OP(mark),       OP(mark),       OP(mark),       OP(mark),       OP(mark),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(sxt_rg),     OP(sxt_rgd),    OP(sxt_in),     OP(sxt_ind),    OP(sxt_de),     OP(sxt_ded),    OP(sxt_ix),     OP(sxt_ixd),
	/* 0x0e00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x0f00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),

	/* 0x1000 */
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	/* 0x1100 */
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	OP(mov_rg_rg),  OP(mov_rg_rgd), OP(mov_rg_in),  OP(mov_rg_ind), OP(mov_rg_de),  OP(mov_rg_ded), OP(mov_rg_ix),  OP(mov_rg_ixd),
	/* 0x1200 */
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	/* 0x1300 */
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	OP(mov_rgd_rg), OP(mov_rgd_rgd), OP(mov_rgd_in), OP(mov_rgd_ind), OP(mov_rgd_de), OP(mov_rgd_ded), OP(mov_rgd_ix), OP(mov_rgd_ixd),
	/* 0x1400 */
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	/* 0x1500 */
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	OP(mov_in_rg),  OP(mov_in_rgd), OP(mov_in_in),  OP(mov_in_ind), OP(mov_in_de),  OP(mov_in_ded), OP(mov_in_ix),  OP(mov_in_ixd),
	/* 0x1600 */
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	/* 0x1700 */
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	OP(mov_ind_rg), OP(mov_ind_rgd), OP(mov_ind_in), OP(mov_ind_ind), OP(mov_ind_de), OP(mov_ind_ded), OP(mov_ind_ix), OP(mov_ind_ixd),
	/* 0x1800 */
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	/* 0x1900 */
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	OP(mov_de_rg),  OP(mov_de_rgd), OP(mov_de_in),  OP(mov_de_ind), OP(mov_de_de),  OP(mov_de_ded), OP(mov_de_ix),  OP(mov_de_ixd),
	/* 0x1a00 */
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	/* 0x1b00 */
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	OP(mov_ded_rg), OP(mov_ded_rgd), OP(mov_ded_in), OP(mov_ded_ind), OP(mov_ded_de), OP(mov_ded_ded), OP(mov_ded_ix), OP(mov_ded_ixd),
	/* 0x1c00 */
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	/* 0x1d00 */
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	OP(mov_ix_rg),  OP(mov_ix_rgd), OP(mov_ix_in),  OP(mov_ix_ind), OP(mov_ix_de),  OP(mov_ix_ded), OP(mov_ix_ix),  OP(mov_ix_ixd),
	/* 0x1e00 */
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	/* 0x1f00 */
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),
	OP(mov_ixd_rg), OP(mov_ixd_rgd), OP(mov_ixd_in), OP(mov_ixd_ind), OP(mov_ixd_de), OP(mov_ixd_ded), OP(mov_ixd_ix), OP(mov_ixd_ixd),

	/* 0x2000 */
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	/* 0x2100 */
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	OP(cmp_rg_rg),  OP(cmp_rg_rgd), OP(cmp_rg_in),  OP(cmp_rg_ind), OP(cmp_rg_de),  OP(cmp_rg_ded), OP(cmp_rg_ix),  OP(cmp_rg_ixd),
	/* 0x2200 */
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	/* 0x2300 */
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	OP(cmp_rgd_rg), OP(cmp_rgd_rgd), OP(cmp_rgd_in), OP(cmp_rgd_ind), OP(cmp_rgd_de), OP(cmp_rgd_ded), OP(cmp_rgd_ix), OP(cmp_rgd_ixd),
	/* 0x2400 */
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	/* 0x2500 */
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	OP(cmp_in_rg),  OP(cmp_in_rgd), OP(cmp_in_in),  OP(cmp_in_ind), OP(cmp_in_de),  OP(cmp_in_ded), OP(cmp_in_ix),  OP(cmp_in_ixd),
	/* 0x2600 */
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	/* 0x2700 */
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	OP(cmp_ind_rg), OP(cmp_ind_rgd), OP(cmp_ind_in), OP(cmp_ind_ind), OP(cmp_ind_de), OP(cmp_ind_ded), OP(cmp_ind_ix), OP(cmp_ind_ixd),
	/* 0x2800 */
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	/* 0x2900 */
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	OP(cmp_de_rg),  OP(cmp_de_rgd), OP(cmp_de_in),  OP(cmp_de_ind), OP(cmp_de_de),  OP(cmp_de_ded), OP(cmp_de_ix),  OP(cmp_de_ixd),
	/* 0x2a00 */
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	/* 0x2b00 */
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	OP(cmp_ded_rg), OP(cmp_ded_rgd), OP(cmp_ded_in), OP(cmp_ded_ind), OP(cmp_ded_de), OP(cmp_ded_ded), OP(cmp_ded_ix), OP(cmp_ded_ixd),
	/* 0x2c00 */
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	/* 0x2d00 */
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	OP(cmp_ix_rg),  OP(cmp_ix_rgd), OP(cmp_ix_in),  OP(cmp_ix_ind), OP(cmp_ix_de),  OP(cmp_ix_ded), OP(cmp_ix_ix),  OP(cmp_ix_ixd),
	/* 0x2e00 */
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	/* 0x2f00 */
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),
	OP(cmp_ixd_rg), OP(cmp_ixd_rgd), OP(cmp_ixd_in), OP(cmp_ixd_ind), OP(cmp_ixd_de), OP(cmp_ixd_ded), OP(cmp_ixd_ix), OP(cmp_ixd_ixd),

	/* 0x3000 */
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	/* 0x3100 */
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	OP(bit_rg_rg),  OP(bit_rg_rgd), OP(bit_rg_in),  OP(bit_rg_ind), OP(bit_rg_de),  OP(bit_rg_ded), OP(bit_rg_ix),  OP(bit_rg_ixd),
	/* 0x3200 */
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	/* 0x3300 */
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	OP(bit_rgd_rg), OP(bit_rgd_rgd), OP(bit_rgd_in), OP(bit_rgd_ind), OP(bit_rgd_de), OP(bit_rgd_ded), OP(bit_rgd_ix), OP(bit_rgd_ixd),
	/* 0x3400 */
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	/* 0x3500 */
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	OP(bit_in_rg),  OP(bit_in_rgd), OP(bit_in_in),  OP(bit_in_ind), OP(bit_in_de),  OP(bit_in_ded), OP(bit_in_ix),  OP(bit_in_ixd),
	/* 0x3600 */
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	/* 0x3700 */
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	OP(bit_ind_rg), OP(bit_ind_rgd), OP(bit_ind_in), OP(bit_ind_ind), OP(bit_ind_de), OP(bit_ind_ded), OP(bit_ind_ix), OP(bit_ind_ixd),
	/* 0x3800 */
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	/* 0x3900 */
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	OP(bit_de_rg),  OP(bit_de_rgd), OP(bit_de_in),  OP(bit_de_ind), OP(bit_de_de),  OP(bit_de_ded), OP(bit_de_ix),  OP(bit_de_ixd),
	/* 0x3a00 */
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	/* 0x3b00 */
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	OP(bit_ded_rg), OP(bit_ded_rgd), OP(bit_ded_in), OP(bit_ded_ind), OP(bit_ded_de), OP(bit_ded_ded), OP(bit_ded_ix), OP(bit_ded_ixd),
	/* 0x3c00 */
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	/* 0x3d00 */
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	OP(bit_ix_rg),  OP(bit_ix_rgd), OP(bit_ix_in),  OP(bit_ix_ind), OP(bit_ix_de),  OP(bit_ix_ded), OP(bit_ix_ix),  OP(bit_ix_ixd),
	/* 0x3e00 */
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	/* 0x3f00 */
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),
	OP(bit_ixd_rg), OP(bit_ixd_rgd), OP(bit_ixd_in), OP(bit_ixd_ind), OP(bit_ixd_de), OP(bit_ixd_ded), OP(bit_ixd_ix), OP(bit_ixd_ixd),

	/* 0x4000 */
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	/* 0x4100 */
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	OP(bic_rg_rg),  OP(bic_rg_rgd), OP(bic_rg_in),  OP(bic_rg_ind), OP(bic_rg_de),  OP(bic_rg_ded), OP(bic_rg_ix),  OP(bic_rg_ixd),
	/* 0x4200 */
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	/* 0x4300 */
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	OP(bic_rgd_rg), OP(bic_rgd_rgd), OP(bic_rgd_in), OP(bic_rgd_ind), OP(bic_rgd_de), OP(bic_rgd_ded), OP(bic_rgd_ix), OP(bic_rgd_ixd),
	/* 0x4400 */
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	/* 0x4500 */
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	OP(bic_in_rg),  OP(bic_in_rgd), OP(bic_in_in),  OP(bic_in_ind), OP(bic_in_de),  OP(bic_in_ded), OP(bic_in_ix),  OP(bic_in_ixd),
	/* 0x4600 */
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	/* 0x4700 */
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	OP(bic_ind_rg), OP(bic_ind_rgd), OP(bic_ind_in), OP(bic_ind_ind), OP(bic_ind_de), OP(bic_ind_ded), OP(bic_ind_ix), OP(bic_ind_ixd),
	/* 0x4800 */
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	/* 0x4900 */
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	OP(bic_de_rg),  OP(bic_de_rgd), OP(bic_de_in),  OP(bic_de_ind), OP(bic_de_de),  OP(bic_de_ded), OP(bic_de_ix),  OP(bic_de_ixd),
	/* 0x4a00 */
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	/* 0x4b00 */
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	OP(bic_ded_rg), OP(bic_ded_rgd), OP(bic_ded_in), OP(bic_ded_ind), OP(bic_ded_de), OP(bic_ded_ded), OP(bic_ded_ix), OP(bic_ded_ixd),
	/* 0x4c00 */
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	/* 0x4d00 */
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	OP(bic_ix_rg),  OP(bic_ix_rgd), OP(bic_ix_in),  OP(bic_ix_ind), OP(bic_ix_de),  OP(bic_ix_ded), OP(bic_ix_ix),  OP(bic_ix_ixd),
	/* 0x4e00 */
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	/* 0x4f00 */
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),
	OP(bic_ixd_rg), OP(bic_ixd_rgd), OP(bic_ixd_in), OP(bic_ixd_ind), OP(bic_ixd_de), OP(bic_ixd_ded), OP(bic_ixd_ix), OP(bic_ixd_ixd),

	/* 0x5000 */
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	/* 0x5100 */
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	OP(bis_rg_rg),  OP(bis_rg_rgd), OP(bis_rg_in),  OP(bis_rg_ind), OP(bis_rg_de),  OP(bis_rg_ded), OP(bis_rg_ix),  OP(bis_rg_ixd),
	/* 0x5200 */
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	/* 0x5300 */
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	OP(bis_rgd_rg), OP(bis_rgd_rgd), OP(bis_rgd_in), OP(bis_rgd_ind), OP(bis_rgd_de), OP(bis_rgd_ded), OP(bis_rgd_ix), OP(bis_rgd_ixd),
	/* 0x5400 */
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	/* 0x5500 */
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	OP(bis_in_rg),  OP(bis_in_rgd), OP(bis_in_in),  OP(bis_in_ind), OP(bis_in_de),  OP(bis_in_ded), OP(bis_in_ix),  OP(bis_in_ixd),
	/* 0x5600 */
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	/* 0x5700 */
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	OP(bis_ind_rg), OP(bis_ind_rgd), OP(bis_ind_in), OP(bis_ind_ind), OP(bis_ind_de), OP(bis_ind_ded), OP(bis_ind_ix), OP(bis_ind_ixd),
	/* 0x5800 */
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	/* 0x5900 */
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	OP(bis_de_rg),  OP(bis_de_rgd), OP(bis_de_in),  OP(bis_de_ind), OP(bis_de_de),  OP(bis_de_ded), OP(bis_de_ix),  OP(bis_de_ixd),
	/* 0x5a00 */
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	/* 0x5b00 */
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	OP(bis_ded_rg), OP(bis_ded_rgd), OP(bis_ded_in), OP(bis_ded_ind), OP(bis_ded_de), OP(bis_ded_ded), OP(bis_ded_ix), OP(bis_ded_ixd),
	/* 0x5c00 */
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	/* 0x5d00 */
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	OP(bis_ix_rg),  OP(bis_ix_rgd), OP(bis_ix_in),  OP(bis_ix_ind), OP(bis_ix_de),  OP(bis_ix_ded), OP(bis_ix_ix),  OP(bis_ix_ixd),
	/* 0x5e00 */
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	/* 0x5f00 */
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),
	OP(bis_ixd_rg), OP(bis_ixd_rgd), OP(bis_ixd_in), OP(bis_ixd_ind), OP(bis_ixd_de), OP(bis_ixd_ded), OP(bis_ixd_ix), OP(bis_ixd_ixd),

	/* 0x6000 */
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	/* 0x6100 */
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	OP(add_rg_rg),  OP(add_rg_rgd), OP(add_rg_in),  OP(add_rg_ind), OP(add_rg_de),  OP(add_rg_ded), OP(add_rg_ix),  OP(add_rg_ixd),
	/* 0x6200 */
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	/* 0x6300 */
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	OP(add_rgd_rg), OP(add_rgd_rgd), OP(add_rgd_in), OP(add_rgd_ind), OP(add_rgd_de), OP(add_rgd_ded), OP(add_rgd_ix), OP(add_rgd_ixd),
	/* 0x6400 */
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	/* 0x6500 */
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	OP(add_in_rg),  OP(add_in_rgd), OP(add_in_in),  OP(add_in_ind), OP(add_in_de),  OP(add_in_ded), OP(add_in_ix),  OP(add_in_ixd),
	/* 0x6600 */
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	/* 0x6700 */
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	OP(add_ind_rg), OP(add_ind_rgd), OP(add_ind_in), OP(add_ind_ind), OP(add_ind_de), OP(add_ind_ded), OP(add_ind_ix), OP(add_ind_ixd),
	/* 0x6800 */
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	/* 0x6900 */
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	OP(add_de_rg),  OP(add_de_rgd), OP(add_de_in),  OP(add_de_ind), OP(add_de_de),  OP(add_de_ded), OP(add_de_ix),  OP(add_de_ixd),
	/* 0x6a00 */
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	/* 0x6b00 */
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	OP(add_ded_rg), OP(add_ded_rgd), OP(add_ded_in), OP(add_ded_ind), OP(add_ded_de), OP(add_ded_ded), OP(add_ded_ix), OP(add_ded_ixd),
	/* 0x6c00 */
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	/* 0x6d00 */
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	OP(add_ix_rg),  OP(add_ix_rgd), OP(add_ix_in),  OP(add_ix_ind), OP(add_ix_de),  OP(add_ix_ded), OP(add_ix_ix),  OP(add_ix_ixd),
	/* 0x6e00 */
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	/* 0x6f00 */
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),
	OP(add_ixd_rg), OP(add_ixd_rgd), OP(add_ixd_in), OP(add_ixd_ind), OP(add_ixd_de), OP(add_ixd_ded), OP(add_ixd_ix), OP(add_ixd_ixd),

	/* 0x7000 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7100 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7200 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7300 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7400 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7500 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7600 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7700 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7800 */
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	/* 0x7900 */
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	OP(xor_rg),     OP(xor_rgd),    OP(xor_in),     OP(xor_ind),    OP(xor_de),     OP(xor_ded),    OP(xor_ix),     OP(xor_ixd),
	/* 0x7a00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7b00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7c00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7d00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x7e00 */
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	/* 0x7f00 */
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),
	OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),        OP(sob),

	/* 0x8000 */
	OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),
	OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),
	OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),
	OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),        OP(bpl),
	/* 0x8100 */
	OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),
	OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),
	OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),
	OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),        OP(bmi),
	/* 0x8200 */
	OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),
	OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),
	OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),
	OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),        OP(bhi),
	/* 0x8300 */
	OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),
	OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),
	OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),
	OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),       OP(blos),
	/* 0x8400 */
	OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),
	OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),
	OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),
	OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),        OP(bvc),
	/* 0x8500 */
	OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),
	OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),
	OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),
	OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),        OP(bvs),
	/* 0x8600 */
	OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),
	OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),
	OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),
	OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),        OP(bcc),
	/* 0x8700 */
	OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),
	OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),
	OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),
	OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),        OP(bcs),
	/* 0x8800 */
	OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),
	OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),
	OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),
	OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),        OP(emt),
	/* 0x8900 */
	OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),
	OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),
	OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),
	OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),       OP(trap),
	/* 0x8a00 */
	OP(clrb_rg),    OP(clrb_rgd),   OP(clrb_in),    OP(clrb_ind),   OP(clrb_de),    OP(clrb_ded),   OP(clrb_ix),    OP(clrb_ixd),
	OP(comb_rg),    OP(comb_rgd),   OP(comb_in),    OP(comb_ind),   OP(comb_de),    OP(comb_ded),   OP(comb_ix),    OP(comb_ixd),
	OP(incb_rg),    OP(incb_rgd),   OP(incb_in),    OP(incb_ind),   OP(incb_de),    OP(incb_ded),   OP(incb_ix),    OP(incb_ixd),
	OP(decb_rg),    OP(decb_rgd),   OP(decb_in),    OP(decb_ind),   OP(decb_de),    OP(decb_ded),   OP(decb_ix),    OP(decb_ixd),
	/* 0x8b00 */
	OP(negb_rg),    OP(negb_rgd),   OP(negb_in),    OP(negb_ind),   OP(negb_de),    OP(negb_ded),   OP(negb_ix),    OP(negb_ixd),
	OP(adcb_rg),    OP(adcb_rgd),   OP(adcb_in),    OP(adcb_ind),   OP(adcb_de),    OP(adcb_ded),   OP(adcb_ix),    OP(adcb_ixd),
	OP(sbcb_rg),    OP(sbcb_rgd),   OP(sbcb_in),    OP(sbcb_ind),   OP(sbcb_de),    OP(sbcb_ded),   OP(sbcb_ix),    OP(sbcb_ixd),
	OP(tstb_rg),    OP(tstb_rgd),   OP(tstb_in),    OP(tstb_ind),   OP(tstb_de),    OP(tstb_ded),   OP(tstb_ix),    OP(tstb_ixd),
	/* 0x8c00 */
	OP(rorb_rg),    OP(rorb_rgd),   OP(rorb_in),    OP(rorb_ind),   OP(rorb_de),    OP(rorb_ded),   OP(rorb_ix),    OP(rorb_ixd),
	OP(rolb_rg),    OP(rolb_rgd),   OP(rolb_in),    OP(rolb_ind),   OP(rolb_de),    OP(rolb_ded),   OP(rolb_ix),    OP(rolb_ixd),
	OP(asrb_rg),    OP(asrb_rgd),   OP(asrb_in),    OP(asrb_ind),   OP(asrb_de),    OP(asrb_ded),   OP(asrb_ix),    OP(asrb_ixd),
	OP(aslb_rg),    OP(aslb_rgd),   OP(aslb_in),    OP(aslb_ind),   OP(aslb_de),    OP(aslb_ded),   OP(aslb_ix),    OP(aslb_ixd),
	/* 0x8d00 */
	OP(mtps_rg),    OP(mtps_rgd),   OP(mtps_in),    OP(mtps_ind),   OP(mtps_de),    OP(mtps_ded),   OP(mtps_ix),    OP(mtps_ixd),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(mfps_rg),    OP(mfps_rgd),   OP(mfps_in),    OP(mfps_ind),   OP(mfps_de),    OP(mfps_ded),   OP(mfps_ix),    OP(mfps_ixd),
	/* 0x8e00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0x8f00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),

	/* 0x9000 */
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	/* 0x9100 */
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	OP(movb_rg_rg), OP(movb_rg_rgd), OP(movb_rg_in), OP(movb_rg_ind), OP(movb_rg_de), OP(movb_rg_ded), OP(movb_rg_ix), OP(movb_rg_ixd),
	/* 0x9200 */
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	/* 0x9300 */
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	OP(movb_rgd_rg), OP(movb_rgd_rgd), OP(movb_rgd_in), OP(movb_rgd_ind), OP(movb_rgd_de), OP(movb_rgd_ded), OP(movb_rgd_ix), OP(movb_rgd_ixd),
	/* 0x9400 */
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	/* 0x9500 */
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	OP(movb_in_rg), OP(movb_in_rgd), OP(movb_in_in), OP(movb_in_ind), OP(movb_in_de), OP(movb_in_ded), OP(movb_in_ix), OP(movb_in_ixd),
	/* 0x9600 */
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	/* 0x9700 */
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	OP(movb_ind_rg), OP(movb_ind_rgd), OP(movb_ind_in), OP(movb_ind_ind), OP(movb_ind_de), OP(movb_ind_ded), OP(movb_ind_ix), OP(movb_ind_ixd),
	/* 0x9800 */
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	/* 0x9900 */
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	OP(movb_de_rg), OP(movb_de_rgd), OP(movb_de_in), OP(movb_de_ind), OP(movb_de_de), OP(movb_de_ded), OP(movb_de_ix), OP(movb_de_ixd),
	/* 0x9a00 */
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	/* 0x9b00 */
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	OP(movb_ded_rg), OP(movb_ded_rgd), OP(movb_ded_in), OP(movb_ded_ind), OP(movb_ded_de), OP(movb_ded_ded), OP(movb_ded_ix), OP(movb_ded_ixd),
	/* 0x9c00 */
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	/* 0x9d00 */
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	OP(movb_ix_rg), OP(movb_ix_rgd), OP(movb_ix_in), OP(movb_ix_ind), OP(movb_ix_de), OP(movb_ix_ded), OP(movb_ix_ix), OP(movb_ix_ixd),
	/* 0x9e00 */
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	/* 0x9f00 */
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),
	OP(movb_ixd_rg), OP(movb_ixd_rgd), OP(movb_ixd_in), OP(movb_ixd_ind), OP(movb_ixd_de), OP(movb_ixd_ded), OP(movb_ixd_ix), OP(movb_ixd_ixd),

	/* 0xa000 */
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	/* 0xa100 */
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	OP(cmpb_rg_rg), OP(cmpb_rg_rgd), OP(cmpb_rg_in), OP(cmpb_rg_ind), OP(cmpb_rg_de), OP(cmpb_rg_ded), OP(cmpb_rg_ix), OP(cmpb_rg_ixd),
	/* 0xa200 */
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	/* 0xa300 */
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	OP(cmpb_rgd_rg), OP(cmpb_rgd_rgd), OP(cmpb_rgd_in), OP(cmpb_rgd_ind), OP(cmpb_rgd_de), OP(cmpb_rgd_ded), OP(cmpb_rgd_ix), OP(cmpb_rgd_ixd),
	/* 0xa400 */
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	/* 0xa500 */
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	OP(cmpb_in_rg), OP(cmpb_in_rgd), OP(cmpb_in_in), OP(cmpb_in_ind), OP(cmpb_in_de), OP(cmpb_in_ded), OP(cmpb_in_ix), OP(cmpb_in_ixd),
	/* 0xa600 */
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	/* 0xa700 */
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	OP(cmpb_ind_rg), OP(cmpb_ind_rgd), OP(cmpb_ind_in), OP(cmpb_ind_ind), OP(cmpb_ind_de), OP(cmpb_ind_ded), OP(cmpb_ind_ix), OP(cmpb_ind_ixd),
	/* 0xa800 */
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	/* 0xa900 */
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	OP(cmpb_de_rg), OP(cmpb_de_rgd), OP(cmpb_de_in), OP(cmpb_de_ind), OP(cmpb_de_de), OP(cmpb_de_ded), OP(cmpb_de_ix), OP(cmpb_de_ixd),
	/* 0xaa00 */
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	/* 0xab00 */
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	OP(cmpb_ded_rg), OP(cmpb_ded_rgd), OP(cmpb_ded_in), OP(cmpb_ded_ind), OP(cmpb_ded_de), OP(cmpb_ded_ded), OP(cmpb_ded_ix), OP(cmpb_ded_ixd),
	/* 0xac00 */
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	/* 0xad00 */
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	OP(cmpb_ix_rg), OP(cmpb_ix_rgd), OP(cmpb_ix_in), OP(cmpb_ix_ind), OP(cmpb_ix_de), OP(cmpb_ix_ded), OP(cmpb_ix_ix), OP(cmpb_ix_ixd),
	/* 0xae00 */
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	/* 0xaf00 */
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),
	OP(cmpb_ixd_rg), OP(cmpb_ixd_rgd), OP(cmpb_ixd_in), OP(cmpb_ixd_ind), OP(cmpb_ixd_de), OP(cmpb_ixd_ded), OP(cmpb_ixd_ix), OP(cmpb_ixd_ixd),

	/* 0xb000 */
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	/* 0xb100 */
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	OP(bitb_rg_rg), OP(bitb_rg_rgd), OP(bitb_rg_in), OP(bitb_rg_ind), OP(bitb_rg_de), OP(bitb_rg_ded), OP(bitb_rg_ix), OP(bitb_rg_ixd),
	/* 0xb200 */
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	/* 0xb300 */
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	OP(bitb_rgd_rg), OP(bitb_rgd_rgd), OP(bitb_rgd_in), OP(bitb_rgd_ind), OP(bitb_rgd_de), OP(bitb_rgd_ded), OP(bitb_rgd_ix), OP(bitb_rgd_ixd),
	/* 0xb400 */
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	/* 0xb500 */
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	OP(bitb_in_rg), OP(bitb_in_rgd), OP(bitb_in_in), OP(bitb_in_ind), OP(bitb_in_de), OP(bitb_in_ded), OP(bitb_in_ix), OP(bitb_in_ixd),
	/* 0xb600 */
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	/* 0xb700 */
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	OP(bitb_ind_rg), OP(bitb_ind_rgd), OP(bitb_ind_in), OP(bitb_ind_ind), OP(bitb_ind_de), OP(bitb_ind_ded), OP(bitb_ind_ix), OP(bitb_ind_ixd),
	/* 0xb800 */
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	/* 0xb900 */
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	OP(bitb_de_rg), OP(bitb_de_rgd), OP(bitb_de_in), OP(bitb_de_ind), OP(bitb_de_de), OP(bitb_de_ded), OP(bitb_de_ix), OP(bitb_de_ixd),
	/* 0xba00 */
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	/* 0xbb00 */
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	OP(bitb_ded_rg), OP(bitb_ded_rgd), OP(bitb_ded_in), OP(bitb_ded_ind), OP(bitb_ded_de), OP(bitb_ded_ded), OP(bitb_ded_ix), OP(bitb_ded_ixd),
	/* 0xbc00 */
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	/* 0xbd00 */
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	OP(bitb_ix_rg), OP(bitb_ix_rgd), OP(bitb_ix_in), OP(bitb_ix_ind), OP(bitb_ix_de), OP(bitb_ix_ded), OP(bitb_ix_ix), OP(bitb_ix_ixd),
	/* 0xbe00 */
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	/* 0xbf00 */
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),
	OP(bitb_ixd_rg), OP(bitb_ixd_rgd), OP(bitb_ixd_in), OP(bitb_ixd_ind), OP(bitb_ixd_de), OP(bitb_ixd_ded), OP(bitb_ixd_ix), OP(bitb_ixd_ixd),

	/* 0xc000 */
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	/* 0xc100 */
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	OP(bicb_rg_rg), OP(bicb_rg_rgd), OP(bicb_rg_in), OP(bicb_rg_ind), OP(bicb_rg_de), OP(bicb_rg_ded), OP(bicb_rg_ix), OP(bicb_rg_ixd),
	/* 0xc200 */
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	/* 0xc300 */
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	OP(bicb_rgd_rg), OP(bicb_rgd_rgd), OP(bicb_rgd_in), OP(bicb_rgd_ind), OP(bicb_rgd_de), OP(bicb_rgd_ded), OP(bicb_rgd_ix), OP(bicb_rgd_ixd),
	/* 0xc400 */
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	/* 0xc500 */
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	OP(bicb_in_rg), OP(bicb_in_rgd), OP(bicb_in_in), OP(bicb_in_ind), OP(bicb_in_de), OP(bicb_in_ded), OP(bicb_in_ix), OP(bicb_in_ixd),
	/* 0xc600 */
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	/* 0xc700 */
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	OP(bicb_ind_rg), OP(bicb_ind_rgd), OP(bicb_ind_in), OP(bicb_ind_ind), OP(bicb_ind_de), OP(bicb_ind_ded), OP(bicb_ind_ix), OP(bicb_ind_ixd),
	/* 0xc800 */
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	/* 0xc900 */
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	OP(bicb_de_rg), OP(bicb_de_rgd), OP(bicb_de_in), OP(bicb_de_ind), OP(bicb_de_de), OP(bicb_de_ded), OP(bicb_de_ix), OP(bicb_de_ixd),
	/* 0xca00 */
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	/* 0xcb00 */
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	OP(bicb_ded_rg), OP(bicb_ded_rgd), OP(bicb_ded_in), OP(bicb_ded_ind), OP(bicb_ded_de), OP(bicb_ded_ded), OP(bicb_ded_ix), OP(bicb_ded_ixd),
	/* 0xcc00 */
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	/* 0xcd00 */
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	OP(bicb_ix_rg), OP(bicb_ix_rgd), OP(bicb_ix_in), OP(bicb_ix_ind), OP(bicb_ix_de), OP(bicb_ix_ded), OP(bicb_ix_ix), OP(bicb_ix_ixd),
	/* 0xce00 */
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	/* 0xcf00 */
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),
	OP(bicb_ixd_rg), OP(bicb_ixd_rgd), OP(bicb_ixd_in), OP(bicb_ixd_ind), OP(bicb_ixd_de), OP(bicb_ixd_ded), OP(bicb_ixd_ix), OP(bicb_ixd_ixd),

	/* 0xd000 */
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	/* 0xd100 */
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	OP(bisb_rg_rg), OP(bisb_rg_rgd), OP(bisb_rg_in), OP(bisb_rg_ind), OP(bisb_rg_de), OP(bisb_rg_ded), OP(bisb_rg_ix), OP(bisb_rg_ixd),
	/* 0xd200 */
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	/* 0xd300 */
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	OP(bisb_rgd_rg), OP(bisb_rgd_rgd), OP(bisb_rgd_in), OP(bisb_rgd_ind), OP(bisb_rgd_de), OP(bisb_rgd_ded), OP(bisb_rgd_ix), OP(bisb_rgd_ixd),
	/* 0xd400 */
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	/* 0xd500 */
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	OP(bisb_in_rg), OP(bisb_in_rgd), OP(bisb_in_in), OP(bisb_in_ind), OP(bisb_in_de), OP(bisb_in_ded), OP(bisb_in_ix), OP(bisb_in_ixd),
	/* 0xd600 */
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	/* 0xd700 */
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	OP(bisb_ind_rg), OP(bisb_ind_rgd), OP(bisb_ind_in), OP(bisb_ind_ind), OP(bisb_ind_de), OP(bisb_ind_ded), OP(bisb_ind_ix), OP(bisb_ind_ixd),
	/* 0xd800 */
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	/* 0xd900 */
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	OP(bisb_de_rg), OP(bisb_de_rgd), OP(bisb_de_in), OP(bisb_de_ind), OP(bisb_de_de), OP(bisb_de_ded), OP(bisb_de_ix), OP(bisb_de_ixd),
	/* 0xda00 */
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	/* 0xdb00 */
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	OP(bisb_ded_rg), OP(bisb_ded_rgd), OP(bisb_ded_in), OP(bisb_ded_ind), OP(bisb_ded_de), OP(bisb_ded_ded), OP(bisb_ded_ix), OP(bisb_ded_ixd),
	/* 0xdc00 */
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	/* 0xdd00 */
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	OP(bisb_ix_rg), OP(bisb_ix_rgd), OP(bisb_ix_in), OP(bisb_ix_ind), OP(bisb_ix_de), OP(bisb_ix_ded), OP(bisb_ix_ix), OP(bisb_ix_ixd),
	/* 0xde00 */
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	/* 0xdf00 */
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),
	OP(bisb_ixd_rg), OP(bisb_ixd_rgd), OP(bisb_ixd_in), OP(bisb_ixd_ind), OP(bisb_ixd_de), OP(bisb_ixd_ded), OP(bisb_ixd_ix), OP(bisb_ixd_ixd),

	/* 0xe000 */
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	/* 0xe100 */
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	OP(sub_rg_rg),  OP(sub_rg_rgd), OP(sub_rg_in),  OP(sub_rg_ind), OP(sub_rg_de),  OP(sub_rg_ded), OP(sub_rg_ix),  OP(sub_rg_ixd),
	/* 0xe200 */
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	/* 0xe300 */
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	OP(sub_rgd_rg), OP(sub_rgd_rgd), OP(sub_rgd_in), OP(sub_rgd_ind), OP(sub_rgd_de), OP(sub_rgd_ded), OP(sub_rgd_ix), OP(sub_rgd_ixd),
	/* 0xe400 */
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	/* 0xe500 */
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	OP(sub_in_rg),  OP(sub_in_rgd), OP(sub_in_in),  OP(sub_in_ind), OP(sub_in_de),  OP(sub_in_ded), OP(sub_in_ix),  OP(sub_in_ixd),
	/* 0xe600 */
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	/* 0xe700 */
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	OP(sub_ind_rg), OP(sub_ind_rgd), OP(sub_ind_in), OP(sub_ind_ind), OP(sub_ind_de), OP(sub_ind_ded), OP(sub_ind_ix), OP(sub_ind_ixd),
	/* 0xe800 */
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	/* 0xe900 */
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	OP(sub_de_rg),  OP(sub_de_rgd), OP(sub_de_in),  OP(sub_de_ind), OP(sub_de_de),  OP(sub_de_ded), OP(sub_de_ix),  OP(sub_de_ixd),
	/* 0xea00 */
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	/* 0xeb00 */
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	OP(sub_ded_rg), OP(sub_ded_rgd), OP(sub_ded_in), OP(sub_ded_ind), OP(sub_ded_de), OP(sub_ded_ded), OP(sub_ded_ix), OP(sub_ded_ixd),
	/* 0xec00 */
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	/* 0xed00 */
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	OP(sub_ix_rg),  OP(sub_ix_rgd), OP(sub_ix_in),  OP(sub_ix_ind), OP(sub_ix_de),  OP(sub_ix_ded), OP(sub_ix_ix),  OP(sub_ix_ixd),
	/* 0xee00 */
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	/* 0xef00 */
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),
	OP(sub_ixd_rg), OP(sub_ixd_rgd), OP(sub_ixd_in), OP(sub_ixd_ind), OP(sub_ixd_de), OP(sub_ixd_ded), OP(sub_ixd_ix), OP(sub_ixd_ixd),

	/* 0xf000 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf100 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf200 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf300 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf400 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf500 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf600 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf700 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf800 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xf900 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xfa00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xfb00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xfc00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xfd00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xfe00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	/* 0xff00 */
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),
	OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal),    OP(illegal)
};

// DEC T-11 opcode implementations, ported from MAME's t11ops.hxx
// (license:BSD-3-Clause, copyright-holders:Aaron Giles).



/* given a register index 'r', this computes the effective address for a byte-sized operation
   and puts the result in 'ea' */
#define MAKE_EAB_RGD(r) ea = REGD(r)
#define MAKE_EAB_IN(r)  ea = REGD(r); REGW(r) += ((r) < 6 ? 1 : 2)
#define MAKE_EAB_INS(r) ea = REGD(r); REGW(r) += ((r) < 6 ? 1 : 2)
#define MAKE_EAB_IND(r) ea = REGD(r); REGW(r) += 2; ea = RWORD(ea)
#define MAKE_EAB_DE(r)  REGW(r) -= ((r) < 6 ? 1 : 2); ea = REGD(r)
#define MAKE_EAB_DED(r) REGW(r) -= 2; ea = REGD(r); ea = RWORD(ea)
#define MAKE_EAB_IX(r)  ea = ROPCODE(); ea = (ea + REGD(r)) & 0xffff
#define MAKE_EAB_IXD(r) ea = ROPCODE(); ea = (ea + REGD(r)) & 0xffff; ea = RWORD(ea)

/* given a register index 'r', this computes the effective address for a word-sized operation
   and puts the result in 'ea' */
/* note that word accesses ignore the low bit!! this fixes APB! */
#define MAKE_EAW_RGD(r) MAKE_EAB_RGD(r)
#define MAKE_EAW_IN(r)  ea = REGD(r); REGW(r) += 2
#define MAKE_EAW_IND(r) MAKE_EAB_IND(r)
#define MAKE_EAW_DE(r)  REGW(r) -= 2; ea = REGD(r)
#define MAKE_EAW_DED(r) MAKE_EAB_DED(r)
#define MAKE_EAW_IX(r)  MAKE_EAB_IX(r)
#define MAKE_EAW_IXD(r) MAKE_EAB_IXD(r)

/* extracts the source/destination register index from the opcode into 'sreg' or 'dreg' */
#define GET_SREG sreg = (op >> 6) & 7
#define GET_DREG dreg = op & 7

/* for a byte-sized source operand: extracts 'sreg', computes 'ea', and loads the value into 'source' */
#define GET_SB_RG  GET_SREG; source = REGB(sreg)
#define GET_SB_RGD GET_SREG; MAKE_EAB_RGD(sreg); source = RBYTE(ea)
#define GET_SB_IN  GET_SREG; if (sreg == 7) { source = ROPCODE(); } else { MAKE_EAB_IN(sreg); source = RBYTE(ea); }
#define GET_SB_IND GET_SREG; if (sreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(sreg); } source = RBYTE(ea)
#define GET_SB_DE  GET_SREG; MAKE_EAB_DE(sreg); source = RBYTE(ea)
#define GET_SB_DED GET_SREG; MAKE_EAB_DED(sreg); source = RBYTE(ea)
#define GET_SB_IX  GET_SREG; MAKE_EAB_IX(sreg); source = RBYTE(ea)
#define GET_SB_IXD GET_SREG; MAKE_EAB_IXD(sreg); source = RBYTE(ea)

/* for a word-sized source operand: extracts 'sreg', computes 'ea', and loads the value into 'source' */
#define GET_SW_RG  GET_SREG; source = REGD(sreg)
#define GET_SW_RGD GET_SREG; MAKE_EAW_RGD(sreg); source = RWORD(ea)
#define GET_SW_IN  GET_SREG; if (sreg == 7) { source = ROPCODE(); } else { MAKE_EAW_IN(sreg); source = RWORD(ea); }
#define GET_SW_IND GET_SREG; if (sreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(sreg); } source = RWORD(ea)
#define GET_SW_DE  GET_SREG; MAKE_EAW_DE(sreg); source = RWORD(ea)
#define GET_SW_DED GET_SREG; MAKE_EAW_DED(sreg); source = RWORD(ea)
#define GET_SW_IX  GET_SREG; MAKE_EAW_IX(sreg); source = RWORD(ea)
#define GET_SW_IXD GET_SREG; MAKE_EAW_IXD(sreg); source = RWORD(ea)

/* for a byte-sized destination operand: extracts 'dreg', computes 'ea', and loads the value into 'dest' */
#define GET_DB_RG  GET_DREG; dest = REGB(dreg)
#define GET_DB_RGD GET_DREG; MAKE_EAB_RGD(dreg); dest = RBYTE(ea)
#define GET_DB_IN  GET_DREG; MAKE_EAB_IN(dreg); dest = RBYTE(ea)
#define GET_DB_IND GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(dreg); } dest = RBYTE(ea)
#define GET_DB_DE  GET_DREG; MAKE_EAB_DE(dreg); dest = RBYTE(ea)
#define GET_DB_DED GET_DREG; MAKE_EAB_DED(dreg); dest = RBYTE(ea)
#define GET_DB_IX  GET_DREG; MAKE_EAB_IX(dreg); dest = RBYTE(ea)
#define GET_DB_IXD GET_DREG; MAKE_EAB_IXD(dreg); dest = RBYTE(ea)

/* for a word-sized destination operand: extracts 'dreg', computes 'ea', and loads the value into 'dest' */
#define GET_DW_RG  GET_DREG; dest = REGD(dreg)
#define GET_DW_RGD GET_DREG; MAKE_EAW_RGD(dreg); dest = RWORD(ea)
#define GET_DW_IN  GET_DREG; MAKE_EAW_IN(dreg); dest = RWORD(ea)
#define GET_DW_IND GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(dreg); } dest = RWORD(ea)
#define GET_DW_DE  GET_DREG; MAKE_EAW_DE(dreg); dest = RWORD(ea)
#define GET_DW_DED GET_DREG; MAKE_EAW_DED(dreg); dest = RWORD(ea)
#define GET_DW_IX  GET_DREG; MAKE_EAW_IX(dreg); dest = RWORD(ea)
#define GET_DW_IXD GET_DREG; MAKE_EAW_IXD(dreg); dest = RWORD(ea)

/* writes a value to a previously computed 'ea' */
#define PUT_DB_EA(v) WBYTE(ea, (v))
#define PUT_DW_EA(v) WWORD(ea, (v))

/* writes a value to a previously computed 'dreg' register */
#define PUT_DB_DREG(v) REGB(dreg) = (v)
#define PUT_DW_DREG(v) REGW(dreg) = (v)

/* for a byte-sized destination operand: extracts 'dreg', computes 'ea', and writes 'v' to it */
#define PUT_DB_RG(v)  GET_DREG; REGB(dreg) = (v)
#define PUT_DB_RGD(v) GET_DREG; MAKE_EAB_RGD(dreg); WBYTE(ea, (v))
#define PUT_DB_IN(v)  GET_DREG; MAKE_EAB_IN(dreg); WBYTE(ea, (v))
#define PUT_DB_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(dreg); } WBYTE(ea, (v))
#define PUT_DB_DE(v)  GET_DREG; MAKE_EAB_DE(dreg); WBYTE(ea, (v))
#define PUT_DB_DED(v) GET_DREG; MAKE_EAB_DED(dreg); WBYTE(ea, (v))
#define PUT_DB_IX(v)  GET_DREG; MAKE_EAB_IX(dreg); WBYTE(ea, (v))
#define PUT_DB_IXD(v) GET_DREG; MAKE_EAB_IXD(dreg); WBYTE(ea, (v))

/* special bus sequence for MOV, CLR, SXT */
#define PUT_DBT_RGD(v) GET_DREG; MAKE_EAB_RGD(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IN(v)  GET_DREG; MAKE_EAB_IN(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(dreg); } RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_DE(v)  GET_DREG; MAKE_EAB_DE(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_DED(v) GET_DREG; MAKE_EAB_DED(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IX(v)  GET_DREG; MAKE_EAB_IX(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IXD(v) GET_DREG; MAKE_EAB_IXD(dreg); RBYTE(ea); WBYTE(ea, (v))

/* for a word-sized destination operand: extracts 'dreg', computes 'ea', and writes 'v' to it */
#define PUT_DW_RG(v)  GET_DREG; REGW(dreg) = (v)
#define PUT_DW_RGD(v) GET_DREG; MAKE_EAW_RGD(dreg); WWORD(ea, (v))
#define PUT_DW_IN(v)  GET_DREG; MAKE_EAW_IN(dreg); WWORD(ea, (v))
#define PUT_DW_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(dreg); } WWORD(ea, (v))
#define PUT_DW_DE(v)  GET_DREG; MAKE_EAW_DE(dreg); WWORD(ea, (v))
#define PUT_DW_DED(v) GET_DREG; MAKE_EAW_DED(dreg); WWORD(ea, (v))
#define PUT_DW_IX(v)  GET_DREG; MAKE_EAW_IX(dreg); WWORD(ea, (v))
#define PUT_DW_IXD(v) GET_DREG; MAKE_EAW_IXD(dreg); WWORD(ea, (v))

/* special bus sequence for MOV, CLR, SXT */
#define PUT_DWT_RG(v)  PUT_DW_RG(v)
#define PUT_DWT_RGD(v) GET_DREG; MAKE_EAW_RGD(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IN(v)  GET_DREG; MAKE_EAW_IN(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(dreg); } RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_DE(v)  GET_DREG; MAKE_EAW_DE(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_DED(v) GET_DREG; MAKE_EAW_DED(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IX(v)  GET_DREG; MAKE_EAW_IX(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IXD(v) GET_DREG; MAKE_EAW_IXD(dreg); RWORD(ea); WWORD(ea, (v))

/* flag clearing; must be done before setting */
#define CLR_ZV   (PSW &= ~(ZFLAG | VFLAG))
#define CLR_NZV  (PSW &= ~(NFLAG | ZFLAG | VFLAG))
#define CLR_NZVC (PSW &= ~(NFLAG | ZFLAG | VFLAG | CFLAG))

/* set individual flags byte-sized */
#define SETB_N (PSW |= (result >> 4) & 0x08)
#define SETB_Z (PSW |= ((result & 0xff) == 0) << 2)
#define SETB_V (PSW |= ((source ^ dest ^ result ^ (result >> 1)) >> 6) & 0x02)
#define SETB_C (PSW |= (result >> 8) & 0x01)
#define SETB_NZ SETB_N; SETB_Z
#define SETB_NZV SETB_N; SETB_Z; SETB_V
#define SETB_NZVC SETB_N; SETB_Z; SETB_V; SETB_C

/* set individual flags word-sized */
#define SETW_N (PSW |= (result >> 12) & 0x08)
#define SETW_Z (PSW |= ((result & 0xffff) == 0) << 2)
#define SETW_V (PSW |= ((source ^ dest ^ result ^ (result >> 1)) >> 14) & 0x02)
#define SETW_C (PSW |= (result >> 16) & 0x01)
#define SETW_NZ SETW_N; SETW_Z
#define SETW_NZV SETW_N; SETW_Z; SETW_V
#define SETW_NZVC SETW_N; SETW_Z; SETW_V; SETW_C

/* operations */
/* ADC: dst += C */
#define ADC_R(d)    int dreg, source, dest, result;     source = GET_C; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_DREG(result)
#define ADC_M(d)    int dreg, source, dest, result, ea; source = GET_C; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_EA(result)
#define ADCB_R(d)   int dreg, source, dest, result;     source = GET_C; GET_DB_##d; CLR_NZVC; result = dest + source; SETB_NZVC; PUT_DB_DREG(result)
#define ADCB_M(d)   int dreg, source, dest, result, ea; source = GET_C; GET_DB_##d; CLR_NZVC; result = dest + source; SETB_NZVC; PUT_DB_EA(result)
/* ADD: dst += src */
#define ADD_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_DREG(result)
#define ADD_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_DREG(result)
#define ADD_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_EA(result)
/* ASL: dst = (dst << 1); C = (dst >> 7) */
#define ASL_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = dest << 1; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ASL_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = dest << 1; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define ASLB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = dest << 1; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define ASLB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = dest << 1; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* ASR: dst = (dst << 1); C = (dst >> 7) */
#define ASR_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x8000); SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ASR_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x8000); SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define ASRB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x80);   SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define ASRB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x80);   SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* BIC: dst &= ~src */
#define BIC_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & ~source; SETW_NZ; PUT_DW_DREG(result)
#define BIC_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & ~source; SETW_NZ; PUT_DW_DREG(result)
#define BIC_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & ~source; SETW_NZ; PUT_DW_EA(result)
#define BICB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & ~source; SETB_NZ; PUT_DB_DREG(result)
#define BICB_X(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & ~source; SETB_NZ; PUT_DB_DREG(result)
#define BICB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & ~source; SETB_NZ; PUT_DB_EA(result)
/* BIS: dst |= src */
#define BIS_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest | source; SETW_NZ; PUT_DW_DREG(result)
#define BIS_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest | source; SETW_NZ; PUT_DW_DREG(result)
#define BIS_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest | source; SETW_NZ; PUT_DW_EA(result)
#define BISB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest | source; SETB_NZ; PUT_DB_DREG(result)
#define BISB_X(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest | source; SETB_NZ; PUT_DB_DREG(result)
#define BISB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest | source; SETB_NZ; PUT_DB_EA(result)
/* BIT: flags = dst & src */
#define BIT_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & source; SETW_NZ;
#define BIT_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & source; SETW_NZ;
#define BITB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & source; SETB_NZ;
#define BITB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & source; SETB_NZ;
/* BR: if (condition) branch */
#define BR(c)       if (c) { PC += 2 * (signed char)(op & 0xff); }
/* CLR: dst = 0 */
#define CLR_R(d)    int dreg;     PUT_DW_##d(0); CLR_NZVC; SET_Z
#define CLR_M(d)    int dreg, ea; PUT_DWT_##d(0); CLR_NZVC; SET_Z
#define CLRB_R(d)   int dreg;     PUT_DB_##d(0); CLR_NZVC; SET_Z
#define CLRB_M(d)   int dreg, ea; PUT_DBT_##d(0); CLR_NZVC; SET_Z
/* CMP: flags = src - dst */
#define CMP_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZVC; result = source - dest; SETW_NZVC;
#define CMP_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = source - dest; SETW_NZVC;
#define CMPB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZVC; result = source - dest; SETB_NZVC;
#define CMPB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZVC; result = source - dest; SETB_NZVC;
/* COM: dst = ~dst */
#define COM_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = ~dest; SETW_NZ; SET_C; PUT_DW_DREG(result)
#define COM_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = ~dest; SETW_NZ; SET_C; PUT_DW_EA(result)
#define COMB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = ~dest; SETB_NZ; SET_C; PUT_DB_DREG(result)
#define COMB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = ~dest; SETB_NZ; SET_C; PUT_DB_EA(result)
/* DEC: dst -= 1 */
#define DEC_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZV; result = dest - 1; SETW_NZ; if (dest == 0x8000) SET_V; PUT_DW_DREG(result)
#define DEC_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZV; result = dest - 1; SETW_NZ; if (dest == 0x8000) SET_V; PUT_DW_EA(result)
#define DECB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZV; result = dest - 1; SETB_NZ; if (dest == 0x80)   SET_V; PUT_DB_DREG(result)
#define DECB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZV; result = dest - 1; SETB_NZ; if (dest == 0x80)   SET_V; PUT_DB_EA(result)
/* INC: dst += 1 */
#define INC_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZV; result = dest + 1; SETW_NZ; if (dest == 0x7fff) SET_V; PUT_DW_DREG(result)
#define INC_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZV; result = dest + 1; SETW_NZ; if (dest == 0x7fff) SET_V; PUT_DW_EA(result)
#define INCB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZV; result = dest + 1; SETB_NZ; if (dest == 0x7f)   SET_V; PUT_DB_DREG(result)
#define INCB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZV; result = dest + 1; SETB_NZ; if (dest == 0x7f)   SET_V; PUT_DB_EA(result)
/* JMP: PC = ea */
#define JMP(d)      int dreg, ea; GET_DREG; MAKE_EAW_##d(dreg); PC = ea
/* JSR: PUSH src, src = PC, PC = ea */
#define JSR(d)      int sreg, dreg, ea; GET_SREG; GET_DREG; MAKE_EAW_##d(dreg); PUSH(REGW(sreg)); REGW(sreg) = PC; PC = ea
/* MFPS: dst = flags */
#define MFPS_R(d)   int dreg, result;     result = PSW; CLR_NZV; SETB_NZ; PUT_DW_##d((signed char)result)
#define MFPS_M(d)   int dreg, result, ea; result = PSW; CLR_NZV; SETB_NZ; PUT_DB_##d(result)
/* MOV: dst = src */
#define MOV_R(s,d)  int sreg, dreg, source, result;     GET_SW_##s; CLR_NZV; result = source; SETW_NZ; PUT_DW_##d(result)
#define MOV_M(s,d)  int sreg, dreg, source, result, ea; GET_SW_##s; CLR_NZV; result = source; SETW_NZ; PUT_DWT_##d(result)
#define MOVB_R(s,d) int sreg, dreg, source, result;     GET_SB_##s; CLR_NZV; result = source; SETB_NZ; PUT_DW_##d((signed char)result)
#define MOVB_X(s,d) int sreg, dreg, source, result, ea; GET_SB_##s; CLR_NZV; result = source; SETB_NZ; PUT_DW_##d((signed char)result)
#define MOVB_M(s,d) int sreg, dreg, source, result, ea; GET_SB_##s; CLR_NZV; result = source; SETB_NZ; PUT_DBT_##d(result)
/* MTPS: flags = src */
#define MTPS_R(d)   int dreg, dest;     GET_DW_##d; PSW = (PSW & ~0xef) | (dest & 0xef); t11_check_irqs(cpu)
#define MTPS_M(d)   int dreg, dest, ea; GET_DW_##d; PSW = (PSW & ~0xef) | (dest & 0xef); t11_check_irqs(cpu)
/* NEG: dst = -dst */
#define NEG_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = -dest; SETW_NZ; if (dest == 0x8000) SET_V; if (result) SET_C; PUT_DW_DREG(result)
#define NEG_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = -dest; SETW_NZ; if (dest == 0x8000) SET_V; if (result) SET_C; PUT_DW_EA(result)
#define NEGB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = -dest; SETB_NZ; if (dest == 0x80)   SET_V; if (result) SET_C; PUT_DB_DREG(result)
#define NEGB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = -dest; SETB_NZ; if (dest == 0x80)   SET_V; if (result) SET_C; PUT_DB_EA(result)
/* ROL: dst = (dst << 1) | C; C = (dst >> 7) */
#define ROL_R(d)    int dreg, dest, result;     GET_DW_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ROL_M(d)    int dreg, dest, result, ea; GET_DW_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define ROLB_R(d)   int dreg, dest, result;     GET_DB_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define ROLB_M(d)   int dreg, dest, result, ea; GET_DB_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* ROR: dst = (dst >> 1) | (C << 7); C = dst & 1 */
#define ROR_R(d)    int dreg, dest, result;     GET_DW_##d; result = (dest >> 1) | (GET_C << 15); CLR_NZVC; SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ROR_M(d)    int dreg, dest, result, ea; GET_DW_##d; result = (dest >> 1) | (GET_C << 15); CLR_NZVC; SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define RORB_R(d)   int dreg, dest, result;     GET_DB_##d; result = (dest >> 1) | (GET_C << 7);  CLR_NZVC; SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define RORB_M(d)   int dreg, dest, result, ea; GET_DB_##d; result = (dest >> 1) | (GET_C << 7);  CLR_NZVC; SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* SBC: dst -= C */
#define SBC_R(d)    int dreg, source, dest, result;     source = GET_C; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_DREG(result)
#define SBC_M(d)    int dreg, source, dest, result, ea; source = GET_C; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_EA(result)
#define SBCB_R(d)   int dreg, source, dest, result;     source = GET_C; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_DREG(result)
#define SBCB_M(d)   int dreg, source, dest, result, ea; source = GET_C; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_EA(result)
/* SUB: dst -= src */
#define SUB_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_DREG(result)
#define SUB_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_DREG(result)
#define SUB_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_EA(result)
#define SUBB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_DREG(result)
#define SUBB_X(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_DREG(result)
#define SUBB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_EA(result)
/* SWAB: dst = (dst >> 8) + (dst << 8) */
#define SWAB_R(d)   int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = ((dest >> 8) & 0xff) + (dest << 8); SETB_NZ; PUT_DW_DREG(result)
#define SWAB_M(d)   int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = ((dest >> 8) & 0xff) + (dest << 8); SETB_NZ; PUT_DW_EA(result)
/* SXT: dst = sign-extend dst */
#define SXT_R(d)    int dreg, result;     CLR_ZV; if (GET_N) result = -1; else { result = 0; SET_Z; } PUT_DW_##d(result)
#define SXT_M(d)    int dreg, result, ea; CLR_ZV; if (GET_N) result = -1; else { result = 0; SET_Z; } PUT_DWT_##d(result)
/* TST: dst = ~dst */
#define TST_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = dest; SETW_NZ;
#define TST_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = dest; SETW_NZ;
#define TSTB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = dest; SETB_NZ;
#define TSTB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = dest; SETB_NZ;
/* XOR: dst ^= src */
#define XOR_R(d)    int sreg, dreg, source, dest, result;     GET_SREG; source = REGW(sreg); GET_DW_##d; CLR_NZV; result = dest ^ source; SETW_NZ; PUT_DW_DREG(result)
#define XOR_M(d)    int sreg, dreg, source, dest, result, ea; GET_SREG; source = REGW(sreg); GET_DW_##d; CLR_NZV; result = dest ^ source; SETW_NZ; PUT_DW_EA(result)


static void t11_trap_to(t11d *cpu, uint16_t vector)
{
	PUSH(PSW);
	PUSH(PC);
	PC = RWORD(vector);
	PSW = RWORD(vector + 2);
	t11_check_irqs(cpu);
}

static void t11_op_0000(t11d *cpu, uint16_t op)
{
	switch (op & 0x3f)
	{
		case 0x00:  /* HALT  */ t11_halt(cpu, op); break;
		case 0x01:  /* WAIT  */ icount_ = 0; wait_state_ = 1; break;
		case 0x02:  /* RTI   */ icount_ -= 24; PC = POP(); PSW = POP(); t11_check_irqs(cpu); break;
		case 0x03:  /* BPT   */ icount_ -= 48; trap_to(0x0c); break;
		case 0x04:  /* IOT   */ icount_ -= 48; trap_to(0x10); break;
		case 0x05:  /* RESET */ if (cpu->reset_instr) cpu->reset_instr(cpu->user); icount_ -= 110; break;
		case 0x06:  /* RTT   */ icount_ -= 33; PC = POP(); PSW = POP(); t11_check_irqs(cpu); break;
		case 0x07:  /* MFPT  */ REGB(0) = 4; break;

		default:    t11_illegal(cpu, op); break;
	}
}

static void t11_halt(t11d *cpu, uint16_t op)
{
	icount_ -= 48;
	PUSH(PSW);
	PUSH(PC);
	PC = initial_pc_ + 4;
	PSW = 0340;
	t11_check_irqs(cpu);
}

static void t11_illegal(t11d *cpu, uint16_t op)
{
	icount_ -= 48;
	trap_to(0x08);
}

static void t11_illegal4(t11d *cpu, uint16_t op)
{
	icount_ -= 48;
	trap_to(0x04);
}

static void t11_mark(t11d *cpu, uint16_t op)
{
	icount_ -= 36;

	SP = PC + 2 * (op & 0x3f);
	PC = REGW(5);
	REGW(5) = POP();
}

static void t11_jmp_rgd(t11d *cpu, uint16_t op)       { icount_ -= 15; { JMP(RGD); } }
static void t11_jmp_in(t11d *cpu, uint16_t op)        { icount_ -= 18; { JMP(IN);  } }
static void t11_jmp_ind(t11d *cpu, uint16_t op)       { icount_ -= 18; { JMP(IND); } }
static void t11_jmp_de(t11d *cpu, uint16_t op)        { icount_ -= 18; { JMP(DE);  } }
static void t11_jmp_ded(t11d *cpu, uint16_t op)       { icount_ -= 21; { JMP(DED); } }
static void t11_jmp_ix(t11d *cpu, uint16_t op)        { icount_ -= 21; { JMP(IX);  } }
static void t11_jmp_ixd(t11d *cpu, uint16_t op)       { icount_ -= 27; { JMP(IXD); } }

static void t11_rts(t11d *cpu, uint16_t op)
{
	int dreg;
	icount_ -= 21;
	GET_DREG;
	PC = REGD(dreg);
	REGW(dreg) = POP();
}

static void t11_ccc(t11d *cpu, uint16_t op)         { icount_ -= 18; { PSW &= ~(op & 15); } }
static void t11_scc(t11d *cpu, uint16_t op)         { icount_ -= 18; { PSW |=  (op & 15); } }

static void t11_swab_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { SWAB_R(RG); } }
static void t11_swab_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { SWAB_M(RGD); } }
static void t11_swab_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { SWAB_M(IN); } }
static void t11_swab_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { SWAB_M(IND); } }
static void t11_swab_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { SWAB_M(DE); } }
static void t11_swab_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { SWAB_M(DED); } }
static void t11_swab_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { SWAB_M(IX); } }
static void t11_swab_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { SWAB_M(IXD); } }

static void t11_br(t11d *cpu, uint16_t op)            { icount_ -= 12; { BR(1); } }
static void t11_bne(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!GET_Z); } }
static void t11_beq(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR( GET_Z); } }
static void t11_bge(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!((GET_N >> 2) ^ GET_V)); } }
static void t11_blt(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(((GET_N >> 2) ^ GET_V)); } }
static void t11_bgt(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!GET_Z && !((GET_N >> 2) ^ GET_V)); } }
static void t11_ble(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR( GET_Z || ((GET_N >> 2) ^ GET_V)); } }

static void t11_jsr_rgd(t11d *cpu, uint16_t op)       { icount_ -= 27; { JSR(RGD); } }
static void t11_jsr_in(t11d *cpu, uint16_t op)        { icount_ -= 30; { JSR(IN);  } }
static void t11_jsr_ind(t11d *cpu, uint16_t op)       { icount_ -= 30; { JSR(IND); } }
static void t11_jsr_de(t11d *cpu, uint16_t op)        { icount_ -= 30; { JSR(DE);  } }
static void t11_jsr_ded(t11d *cpu, uint16_t op)       { icount_ -= 33; { JSR(DED); } }
static void t11_jsr_ix(t11d *cpu, uint16_t op)        { icount_ -= 33; { JSR(IX);  } }
static void t11_jsr_ixd(t11d *cpu, uint16_t op)       { icount_ -= 39; { JSR(IXD); } }

static void t11_clr_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { CLR_R(RG);  } }
static void t11_clr_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { CLR_M(RGD); } }
static void t11_clr_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { CLR_M(IN);  } }
static void t11_clr_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { CLR_M(IND); } }
static void t11_clr_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { CLR_M(DE);  } }
static void t11_clr_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { CLR_M(DED); } }
static void t11_clr_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { CLR_M(IX);  } }
static void t11_clr_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { CLR_M(IXD); } }

static void t11_com_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { COM_R(RG);  } }
static void t11_com_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { COM_M(RGD); } }
static void t11_com_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { COM_M(IN);  } }
static void t11_com_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { COM_M(IND); } }
static void t11_com_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { COM_M(DE);  } }
static void t11_com_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { COM_M(DED); } }
static void t11_com_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { COM_M(IX);  } }
static void t11_com_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { COM_M(IXD); } }

static void t11_inc_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { INC_R(RG);  } }
static void t11_inc_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { INC_M(RGD); } }
static void t11_inc_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { INC_M(IN);  } }
static void t11_inc_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { INC_M(IND); } }
static void t11_inc_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { INC_M(DE);  } }
static void t11_inc_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { INC_M(DED); } }
static void t11_inc_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { INC_M(IX);  } }
static void t11_inc_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { INC_M(IXD); } }

static void t11_dec_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { DEC_R(RG);  } }
static void t11_dec_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { DEC_M(RGD); } }
static void t11_dec_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { DEC_M(IN);  } }
static void t11_dec_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { DEC_M(IND); } }
static void t11_dec_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { DEC_M(DE);  } }
static void t11_dec_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { DEC_M(DED); } }
static void t11_dec_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { DEC_M(IX);  } }
static void t11_dec_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { DEC_M(IXD); } }

static void t11_neg_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { NEG_R(RG);  } }
static void t11_neg_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { NEG_M(RGD); } }
static void t11_neg_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { NEG_M(IN);  } }
static void t11_neg_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { NEG_M(IND); } }
static void t11_neg_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { NEG_M(DE);  } }
static void t11_neg_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { NEG_M(DED); } }
static void t11_neg_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { NEG_M(IX);  } }
static void t11_neg_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { NEG_M(IXD); } }

static void t11_adc_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { ADC_R(RG);  } }
static void t11_adc_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { ADC_M(RGD); } }
static void t11_adc_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { ADC_M(IN);  } }
static void t11_adc_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { ADC_M(IND); } }
static void t11_adc_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { ADC_M(DE);  } }
static void t11_adc_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { ADC_M(DED); } }
static void t11_adc_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { ADC_M(IX);  } }
static void t11_adc_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { ADC_M(IXD); } }

static void t11_sbc_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { SBC_R(RG);  } }
static void t11_sbc_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { SBC_M(RGD); } }
static void t11_sbc_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { SBC_M(IN);  } }
static void t11_sbc_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { SBC_M(IND); } }
static void t11_sbc_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { SBC_M(DE);  } }
static void t11_sbc_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { SBC_M(DED); } }
static void t11_sbc_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { SBC_M(IX);  } }
static void t11_sbc_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { SBC_M(IXD); } }

static void t11_tst_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { TST_R(RG);  } }
static void t11_tst_rgd(t11d *cpu, uint16_t op)       { icount_ -= 18; { TST_M(RGD); } }
static void t11_tst_in(t11d *cpu, uint16_t op)        { icount_ -= 18; { TST_M(IN);  } }
static void t11_tst_ind(t11d *cpu, uint16_t op)       { icount_ -= 24; { TST_M(IND); } }
static void t11_tst_de(t11d *cpu, uint16_t op)        { icount_ -= 21; { TST_M(DE);  } }
static void t11_tst_ded(t11d *cpu, uint16_t op)       { icount_ -= 27; { TST_M(DED); } }
static void t11_tst_ix(t11d *cpu, uint16_t op)        { icount_ -= 27; { TST_M(IX);  } }
static void t11_tst_ixd(t11d *cpu, uint16_t op)       { icount_ -= 33; { TST_M(IXD); } }

static void t11_ror_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { ROR_R(RG);  } }
static void t11_ror_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { ROR_M(RGD); } }
static void t11_ror_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { ROR_M(IN);  } }
static void t11_ror_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { ROR_M(IND); } }
static void t11_ror_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { ROR_M(DE);  } }
static void t11_ror_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { ROR_M(DED); } }
static void t11_ror_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { ROR_M(IX);  } }
static void t11_ror_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { ROR_M(IXD); } }

static void t11_rol_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { ROL_R(RG);  } }
static void t11_rol_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { ROL_M(RGD); } }
static void t11_rol_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { ROL_M(IN);  } }
static void t11_rol_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { ROL_M(IND); } }
static void t11_rol_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { ROL_M(DE);  } }
static void t11_rol_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { ROL_M(DED); } }
static void t11_rol_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { ROL_M(IX);  } }
static void t11_rol_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { ROL_M(IXD); } }

static void t11_asr_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { ASR_R(RG);  } }
static void t11_asr_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { ASR_M(RGD); } }
static void t11_asr_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { ASR_M(IN);  } }
static void t11_asr_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { ASR_M(IND); } }
static void t11_asr_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { ASR_M(DE);  } }
static void t11_asr_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { ASR_M(DED); } }
static void t11_asr_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { ASR_M(IX);  } }
static void t11_asr_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { ASR_M(IXD); } }

static void t11_asl_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { ASL_R(RG);  } }
static void t11_asl_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { ASL_M(RGD); } }
static void t11_asl_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { ASL_M(IN);  } }
static void t11_asl_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { ASL_M(IND); } }
static void t11_asl_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { ASL_M(DE);  } }
static void t11_asl_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { ASL_M(DED); } }
static void t11_asl_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { ASL_M(IX);  } }
static void t11_asl_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { ASL_M(IXD); } }

static void t11_sxt_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { SXT_R(RG);  } }
static void t11_sxt_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { SXT_M(RGD); } }
static void t11_sxt_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { SXT_M(IN);  } }
static void t11_sxt_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { SXT_M(IND); } }
static void t11_sxt_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { SXT_M(DE);  } }
static void t11_sxt_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { SXT_M(DED); } }
static void t11_sxt_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { SXT_M(IX);  } }
static void t11_sxt_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { SXT_M(IXD); } }

static void t11_mov_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { MOV_R(RG,RG);   } }
static void t11_mov_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { MOV_M(RG,RGD);  } }
static void t11_mov_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { MOV_M(RG,IN);   } }
static void t11_mov_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { MOV_M(RG,IND);  } }
static void t11_mov_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { MOV_M(RG,DE);   } }
static void t11_mov_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { MOV_M(RG,DED);  } }
static void t11_mov_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { MOV_M(RG,IX);   } }
static void t11_mov_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { MOV_M(RG,IXD);  } }
static void t11_mov_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { MOV_M(RGD,RG);  } }
static void t11_mov_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { MOV_M(RGD,RGD); } }
static void t11_mov_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { MOV_M(RGD,IN);  } }
static void t11_mov_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { MOV_M(RGD,IND); } }
static void t11_mov_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { MOV_M(RGD,DE);  } }
static void t11_mov_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { MOV_M(RGD,DED); } }
static void t11_mov_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { MOV_M(RGD,IX);  } }
static void t11_mov_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { MOV_M(RGD,IXD); } }
static void t11_mov_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { MOV_M(IN,RG);   } }
static void t11_mov_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { MOV_M(IN,RGD);  } }
static void t11_mov_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { MOV_M(IN,IN);   } }
static void t11_mov_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { MOV_M(IN,IND);  } }
static void t11_mov_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { MOV_M(IN,DE);   } }
static void t11_mov_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { MOV_M(IN,DED);  } }
static void t11_mov_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { MOV_M(IN,IX);   } }
static void t11_mov_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { MOV_M(IN,IXD);  } }
static void t11_mov_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { MOV_M(IND,RG);  } }
static void t11_mov_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { MOV_M(IND,RGD); } }
static void t11_mov_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { MOV_M(IND,IN);  } }
static void t11_mov_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { MOV_M(IND,IND); } }
static void t11_mov_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { MOV_M(IND,DE);  } }
static void t11_mov_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { MOV_M(IND,DED); } }
static void t11_mov_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { MOV_M(IND,IX);  } }
static void t11_mov_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { MOV_M(IND,IXD); } }
static void t11_mov_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { MOV_M(DE,RG);   } }
static void t11_mov_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { MOV_M(DE,RGD);  } }
static void t11_mov_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { MOV_M(DE,IN);   } }
static void t11_mov_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { MOV_M(DE,IND);  } }
static void t11_mov_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { MOV_M(DE,DE);   } }
static void t11_mov_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { MOV_M(DE,DED);  } }
static void t11_mov_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { MOV_M(DE,IX);   } }
static void t11_mov_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { MOV_M(DE,IXD);  } }
static void t11_mov_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { MOV_M(DED,RG);  } }
static void t11_mov_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { MOV_M(DED,RGD); } }
static void t11_mov_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { MOV_M(DED,IN);  } }
static void t11_mov_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { MOV_M(DED,IND); } }
static void t11_mov_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { MOV_M(DED,DE);  } }
static void t11_mov_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { MOV_M(DED,DED); } }
static void t11_mov_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { MOV_M(DED,IX);  } }
static void t11_mov_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { MOV_M(DED,IXD); } }
static void t11_mov_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { MOV_M(IX,RG);   } }
static void t11_mov_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { MOV_M(IX,RGD);  } }
static void t11_mov_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { MOV_M(IX,IN);   } }
static void t11_mov_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { MOV_M(IX,IND);  } }
static void t11_mov_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { MOV_M(IX,DE);   } }
static void t11_mov_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { MOV_M(IX,DED);  } }
static void t11_mov_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { MOV_M(IX,IX);   } }
static void t11_mov_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { MOV_M(IX,IXD);  } }
static void t11_mov_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { MOV_M(IXD,RG);  } }
static void t11_mov_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { MOV_M(IXD,RGD); } }
static void t11_mov_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { MOV_M(IXD,IN);  } }
static void t11_mov_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { MOV_M(IXD,IND); } }
static void t11_mov_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { MOV_M(IXD,DE);  } }
static void t11_mov_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { MOV_M(IXD,DED); } }
static void t11_mov_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { MOV_M(IXD,IX);  } }
static void t11_mov_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { MOV_M(IXD,IXD); } }

static void t11_cmp_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { CMP_R(RG,RG);   } }
static void t11_cmp_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+ 9; { CMP_M(RG,RGD);  } }
static void t11_cmp_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+ 9; { CMP_M(RG,IN);   } }
static void t11_cmp_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+15; { CMP_M(RG,IND);  } }
static void t11_cmp_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { CMP_M(RG,DE);   } }
static void t11_cmp_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { CMP_M(RG,DED);  } }
static void t11_cmp_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+18; { CMP_M(RG,IX);   } }
static void t11_cmp_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+24; { CMP_M(RG,IXD);  } }
static void t11_cmp_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { CMP_M(RGD,RG);  } }
static void t11_cmp_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+ 9; { CMP_M(RGD,RGD); } }
static void t11_cmp_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { CMP_M(RGD,IN);  } }
static void t11_cmp_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+15; { CMP_M(RGD,IND); } }
static void t11_cmp_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { CMP_M(RGD,DE);  } }
static void t11_cmp_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { CMP_M(RGD,DED); } }
static void t11_cmp_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { CMP_M(RGD,IX);  } }
static void t11_cmp_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+24; { CMP_M(RGD,IXD); } }
static void t11_cmp_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { CMP_M(IN,RG);   } }
static void t11_cmp_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { CMP_M(IN,RGD);  } }
static void t11_cmp_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+ 9; { CMP_M(IN,IN);   } }
static void t11_cmp_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { CMP_M(IN,IND);  } }
static void t11_cmp_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { CMP_M(IN,DE);   } }
static void t11_cmp_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { CMP_M(IN,DED);  } }
static void t11_cmp_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+18; { CMP_M(IN,IX);   } }
static void t11_cmp_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+24; { CMP_M(IN,IXD);  } }
static void t11_cmp_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { CMP_M(IND,RG);  } }
static void t11_cmp_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+ 9; { CMP_M(IND,RGD); } }
static void t11_cmp_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+ 9; { CMP_M(IND,IN);  } }
static void t11_cmp_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+15; { CMP_M(IND,IND); } }
static void t11_cmp_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { CMP_M(IND,DE);  } }
static void t11_cmp_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { CMP_M(IND,DED); } }
static void t11_cmp_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+18; { CMP_M(IND,IX);  } }
static void t11_cmp_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+24; { CMP_M(IND,IXD); } }
static void t11_cmp_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { CMP_M(DE,RG);   } }
static void t11_cmp_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+ 9; { CMP_M(DE,RGD);  } }
static void t11_cmp_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+ 9; { CMP_M(DE,IN);   } }
static void t11_cmp_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+15; { CMP_M(DE,IND);  } }
static void t11_cmp_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { CMP_M(DE,DE);   } }
static void t11_cmp_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { CMP_M(DE,DED);  } }
static void t11_cmp_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+18; { CMP_M(DE,IX);   } }
static void t11_cmp_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+24; { CMP_M(DE,IXD);  } }
static void t11_cmp_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { CMP_M(DED,RG);  } }
static void t11_cmp_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+ 9; { CMP_M(DED,RGD); } }
static void t11_cmp_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { CMP_M(DED,IN);  } }
static void t11_cmp_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+15; { CMP_M(DED,IND); } }
static void t11_cmp_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { CMP_M(DED,DE);  } }
static void t11_cmp_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { CMP_M(DED,DED); } }
static void t11_cmp_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { CMP_M(DED,IX);  } }
static void t11_cmp_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+24; { CMP_M(DED,IXD); } }
static void t11_cmp_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { CMP_M(IX,RG);   } }
static void t11_cmp_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { CMP_M(IX,RGD);  } }
static void t11_cmp_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+ 9; { CMP_M(IX,IN);   } }
static void t11_cmp_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { CMP_M(IX,IND);  } }
static void t11_cmp_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { CMP_M(IX,DE);   } }
static void t11_cmp_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { CMP_M(IX,DED);  } }
static void t11_cmp_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+18; { CMP_M(IX,IX);   } }
static void t11_cmp_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+24; { CMP_M(IX,IXD);  } }
static void t11_cmp_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { CMP_M(IXD,RG);  } }
static void t11_cmp_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+ 9; { CMP_M(IXD,RGD); } }
static void t11_cmp_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+ 9; { CMP_M(IXD,IN);  } }
static void t11_cmp_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+15; { CMP_M(IXD,IND); } }
static void t11_cmp_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { CMP_M(IXD,DE);  } }
static void t11_cmp_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { CMP_M(IXD,DED); } }
static void t11_cmp_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+18; { CMP_M(IXD,IX);  } }
static void t11_cmp_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+24; { CMP_M(IXD,IXD); } }

static void t11_bit_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { BIT_R(RG,RG);   } }
static void t11_bit_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+ 9; { BIT_M(RG,RGD);  } }
static void t11_bit_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+ 9; { BIT_M(RG,IN);   } }
static void t11_bit_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+15; { BIT_M(RG,IND);  } }
static void t11_bit_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { BIT_M(RG,DE);   } }
static void t11_bit_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { BIT_M(RG,DED);  } }
static void t11_bit_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+18; { BIT_M(RG,IX);   } }
static void t11_bit_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+24; { BIT_M(RG,IXD);  } }
static void t11_bit_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { BIT_M(RGD,RG);  } }
static void t11_bit_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+ 9; { BIT_M(RGD,RGD); } }
static void t11_bit_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { BIT_M(RGD,IN);  } }
static void t11_bit_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+15; { BIT_M(RGD,IND); } }
static void t11_bit_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BIT_M(RGD,DE);  } }
static void t11_bit_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { BIT_M(RGD,DED); } }
static void t11_bit_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BIT_M(RGD,IX);  } }
static void t11_bit_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+24; { BIT_M(RGD,IXD); } }
static void t11_bit_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { BIT_M(IN,RG);   } }
static void t11_bit_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { BIT_M(IN,RGD);  } }
static void t11_bit_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+ 9; { BIT_M(IN,IN);   } }
static void t11_bit_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { BIT_M(IN,IND);  } }
static void t11_bit_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { BIT_M(IN,DE);   } }
static void t11_bit_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BIT_M(IN,DED);  } }
static void t11_bit_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+18; { BIT_M(IN,IX);   } }
static void t11_bit_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+24; { BIT_M(IN,IXD);  } }
static void t11_bit_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { BIT_M(IND,RG);  } }
static void t11_bit_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+ 9; { BIT_M(IND,RGD); } }
static void t11_bit_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+ 9; { BIT_M(IND,IN);  } }
static void t11_bit_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+15; { BIT_M(IND,IND); } }
static void t11_bit_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { BIT_M(IND,DE);  } }
static void t11_bit_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { BIT_M(IND,DED); } }
static void t11_bit_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+18; { BIT_M(IND,IX);  } }
static void t11_bit_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+24; { BIT_M(IND,IXD); } }
static void t11_bit_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { BIT_M(DE,RG);   } }
static void t11_bit_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+ 9; { BIT_M(DE,RGD);  } }
static void t11_bit_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+ 9; { BIT_M(DE,IN);   } }
static void t11_bit_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+15; { BIT_M(DE,IND);  } }
static void t11_bit_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { BIT_M(DE,DE);   } }
static void t11_bit_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { BIT_M(DE,DED);  } }
static void t11_bit_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+18; { BIT_M(DE,IX);   } }
static void t11_bit_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+24; { BIT_M(DE,IXD);  } }
static void t11_bit_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { BIT_M(DED,RG);  } }
static void t11_bit_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+ 9; { BIT_M(DED,RGD); } }
static void t11_bit_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { BIT_M(DED,IN);  } }
static void t11_bit_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+15; { BIT_M(DED,IND); } }
static void t11_bit_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BIT_M(DED,DE);  } }
static void t11_bit_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { BIT_M(DED,DED); } }
static void t11_bit_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BIT_M(DED,IX);  } }
static void t11_bit_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+24; { BIT_M(DED,IXD); } }
static void t11_bit_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { BIT_M(IX,RG);   } }
static void t11_bit_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { BIT_M(IX,RGD);  } }
static void t11_bit_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+ 9; { BIT_M(IX,IN);   } }
static void t11_bit_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { BIT_M(IX,IND);  } }
static void t11_bit_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { BIT_M(IX,DE);   } }
static void t11_bit_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BIT_M(IX,DED);  } }
static void t11_bit_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+18; { BIT_M(IX,IX);   } }
static void t11_bit_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+24; { BIT_M(IX,IXD);  } }
static void t11_bit_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { BIT_M(IXD,RG);  } }
static void t11_bit_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+ 9; { BIT_M(IXD,RGD); } }
static void t11_bit_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+ 9; { BIT_M(IXD,IN);  } }
static void t11_bit_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+15; { BIT_M(IXD,IND); } }
static void t11_bit_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { BIT_M(IXD,DE);  } }
static void t11_bit_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { BIT_M(IXD,DED); } }
static void t11_bit_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+18; { BIT_M(IXD,IX);  } }
static void t11_bit_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+24; { BIT_M(IXD,IXD); } }

static void t11_bic_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { BIC_R(RG,RG);   } }
static void t11_bic_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { BIC_M(RG,RGD);  } }
static void t11_bic_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { BIC_M(RG,IN);   } }
static void t11_bic_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { BIC_M(RG,IND);  } }
static void t11_bic_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { BIC_M(RG,DE);   } }
static void t11_bic_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { BIC_M(RG,DED);  } }
static void t11_bic_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { BIC_M(RG,IX);   } }
static void t11_bic_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { BIC_M(RG,IXD);  } }
static void t11_bic_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { BIC_X(RGD,RG);  } }
static void t11_bic_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { BIC_M(RGD,RGD); } }
static void t11_bic_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BIC_M(RGD,IN);  } }
static void t11_bic_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { BIC_M(RGD,IND); } }
static void t11_bic_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { BIC_M(RGD,DE);  } }
static void t11_bic_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { BIC_M(RGD,DED); } }
static void t11_bic_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BIC_M(RGD,IX);  } }
static void t11_bic_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { BIC_M(RGD,IXD); } }
static void t11_bic_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { BIC_X(IN,RG);   } }
static void t11_bic_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BIC_M(IN,RGD);  } }
static void t11_bic_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { BIC_M(IN,IN);   } }
static void t11_bic_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BIC_M(IN,IND);  } }
static void t11_bic_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { BIC_M(IN,DE);   } }
static void t11_bic_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BIC_M(IN,DED);  } }
static void t11_bic_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { BIC_M(IN,IX);   } }
static void t11_bic_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { BIC_M(IN,IXD);  } }
static void t11_bic_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { BIC_X(IND,RG);  } }
static void t11_bic_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { BIC_M(IND,RGD); } }
static void t11_bic_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { BIC_M(IND,IN);  } }
static void t11_bic_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { BIC_M(IND,IND); } }
static void t11_bic_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { BIC_M(IND,DE);  } }
static void t11_bic_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { BIC_M(IND,DED); } }
static void t11_bic_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { BIC_M(IND,IX);  } }
static void t11_bic_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { BIC_M(IND,IXD); } }
static void t11_bic_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { BIC_X(DE,RG);   } }
static void t11_bic_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { BIC_M(DE,RGD);  } }
static void t11_bic_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { BIC_M(DE,IN);   } }
static void t11_bic_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { BIC_M(DE,IND);  } }
static void t11_bic_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { BIC_M(DE,DE);   } }
static void t11_bic_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { BIC_M(DE,DED);  } }
static void t11_bic_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { BIC_M(DE,IX);   } }
static void t11_bic_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { BIC_M(DE,IXD);  } }
static void t11_bic_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { BIC_X(DED,RG);  } }
static void t11_bic_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { BIC_M(DED,RGD); } }
static void t11_bic_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BIC_M(DED,IN);  } }
static void t11_bic_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { BIC_M(DED,IND); } }
static void t11_bic_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { BIC_M(DED,DE);  } }
static void t11_bic_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { BIC_M(DED,DED); } }
static void t11_bic_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BIC_M(DED,IX);  } }
static void t11_bic_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { BIC_M(DED,IXD); } }
static void t11_bic_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { BIC_X(IX,RG);   } }
static void t11_bic_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BIC_M(IX,RGD);  } }
static void t11_bic_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { BIC_M(IX,IN);   } }
static void t11_bic_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BIC_M(IX,IND);  } }
static void t11_bic_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { BIC_M(IX,DE);   } }
static void t11_bic_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BIC_M(IX,DED);  } }
static void t11_bic_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { BIC_M(IX,IX);   } }
static void t11_bic_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { BIC_M(IX,IXD);  } }
static void t11_bic_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { BIC_X(IXD,RG);  } }
static void t11_bic_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { BIC_M(IXD,RGD); } }
static void t11_bic_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { BIC_M(IXD,IN);  } }
static void t11_bic_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { BIC_M(IXD,IND); } }
static void t11_bic_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { BIC_M(IXD,DE);  } }
static void t11_bic_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { BIC_M(IXD,DED); } }
static void t11_bic_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { BIC_M(IXD,IX);  } }
static void t11_bic_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { BIC_M(IXD,IXD); } }

static void t11_bis_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { BIS_R(RG,RG);   } }
static void t11_bis_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { BIS_M(RG,RGD);  } }
static void t11_bis_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { BIS_M(RG,IN);   } }
static void t11_bis_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { BIS_M(RG,IND);  } }
static void t11_bis_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { BIS_M(RG,DE);   } }
static void t11_bis_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { BIS_M(RG,DED);  } }
static void t11_bis_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { BIS_M(RG,IX);   } }
static void t11_bis_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { BIS_M(RG,IXD);  } }
static void t11_bis_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { BIS_X(RGD,RG);  } }
static void t11_bis_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { BIS_M(RGD,RGD); } }
static void t11_bis_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BIS_M(RGD,IN);  } }
static void t11_bis_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { BIS_M(RGD,IND); } }
static void t11_bis_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { BIS_M(RGD,DE);  } }
static void t11_bis_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { BIS_M(RGD,DED); } }
static void t11_bis_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BIS_M(RGD,IX);  } }
static void t11_bis_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { BIS_M(RGD,IXD); } }
static void t11_bis_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { BIS_X(IN,RG);   } }
static void t11_bis_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BIS_M(IN,RGD);  } }
static void t11_bis_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { BIS_M(IN,IN);   } }
static void t11_bis_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BIS_M(IN,IND);  } }
static void t11_bis_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { BIS_M(IN,DE);   } }
static void t11_bis_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BIS_M(IN,DED);  } }
static void t11_bis_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { BIS_M(IN,IX);   } }
static void t11_bis_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { BIS_M(IN,IXD);  } }
static void t11_bis_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { BIS_X(IND,RG);  } }
static void t11_bis_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { BIS_M(IND,RGD); } }
static void t11_bis_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { BIS_M(IND,IN);  } }
static void t11_bis_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { BIS_M(IND,IND); } }
static void t11_bis_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { BIS_M(IND,DE);  } }
static void t11_bis_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { BIS_M(IND,DED); } }
static void t11_bis_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { BIS_M(IND,IX);  } }
static void t11_bis_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { BIS_M(IND,IXD); } }
static void t11_bis_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { BIS_X(DE,RG);   } }
static void t11_bis_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { BIS_M(DE,RGD);  } }
static void t11_bis_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { BIS_M(DE,IN);   } }
static void t11_bis_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { BIS_M(DE,IND);  } }
static void t11_bis_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { BIS_M(DE,DE);   } }
static void t11_bis_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { BIS_M(DE,DED);  } }
static void t11_bis_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { BIS_M(DE,IX);   } }
static void t11_bis_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { BIS_M(DE,IXD);  } }
static void t11_bis_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { BIS_X(DED,RG);  } }
static void t11_bis_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { BIS_M(DED,RGD); } }
static void t11_bis_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BIS_M(DED,IN);  } }
static void t11_bis_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { BIS_M(DED,IND); } }
static void t11_bis_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { BIS_M(DED,DE);  } }
static void t11_bis_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { BIS_M(DED,DED); } }
static void t11_bis_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BIS_M(DED,IX);  } }
static void t11_bis_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { BIS_M(DED,IXD); } }
static void t11_bis_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { BIS_X(IX,RG);   } }
static void t11_bis_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BIS_M(IX,RGD);  } }
static void t11_bis_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { BIS_M(IX,IN);   } }
static void t11_bis_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BIS_M(IX,IND);  } }
static void t11_bis_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { BIS_M(IX,DE);   } }
static void t11_bis_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BIS_M(IX,DED);  } }
static void t11_bis_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { BIS_M(IX,IX);   } }
static void t11_bis_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { BIS_M(IX,IXD);  } }
static void t11_bis_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { BIS_X(IXD,RG);  } }
static void t11_bis_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { BIS_M(IXD,RGD); } }
static void t11_bis_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { BIS_M(IXD,IN);  } }
static void t11_bis_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { BIS_M(IXD,IND); } }
static void t11_bis_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { BIS_M(IXD,DE);  } }
static void t11_bis_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { BIS_M(IXD,DED); } }
static void t11_bis_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { BIS_M(IXD,IX);  } }
static void t11_bis_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { BIS_M(IXD,IXD); } }

static void t11_add_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { ADD_R(RG,RG);   } }
static void t11_add_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { ADD_M(RG,RGD);  } }
static void t11_add_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { ADD_M(RG,IN);   } }
static void t11_add_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { ADD_M(RG,IND);  } }
static void t11_add_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { ADD_M(RG,DE);   } }
static void t11_add_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { ADD_M(RG,DED);  } }
static void t11_add_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { ADD_M(RG,IX);   } }
static void t11_add_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { ADD_M(RG,IXD);  } }
static void t11_add_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { ADD_X(RGD,RG);  } }
static void t11_add_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { ADD_M(RGD,RGD); } }
static void t11_add_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { ADD_M(RGD,IN);  } }
static void t11_add_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { ADD_M(RGD,IND); } }
static void t11_add_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { ADD_M(RGD,DE);  } }
static void t11_add_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { ADD_M(RGD,DED); } }
static void t11_add_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { ADD_M(RGD,IX);  } }
static void t11_add_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { ADD_M(RGD,IXD); } }
static void t11_add_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { ADD_X(IN,RG);   } }
static void t11_add_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { ADD_M(IN,RGD);  } }
static void t11_add_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { ADD_M(IN,IN);   } }
static void t11_add_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { ADD_M(IN,IND);  } }
static void t11_add_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { ADD_M(IN,DE);   } }
static void t11_add_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { ADD_M(IN,DED);  } }
static void t11_add_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { ADD_M(IN,IX);   } }
static void t11_add_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { ADD_M(IN,IXD);  } }
static void t11_add_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { ADD_X(IND,RG);  } }
static void t11_add_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { ADD_M(IND,RGD); } }
static void t11_add_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { ADD_M(IND,IN);  } }
static void t11_add_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { ADD_M(IND,IND); } }
static void t11_add_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { ADD_M(IND,DE);  } }
static void t11_add_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { ADD_M(IND,DED); } }
static void t11_add_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { ADD_M(IND,IX);  } }
static void t11_add_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { ADD_M(IND,IXD); } }
static void t11_add_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { ADD_X(DE,RG);   } }
static void t11_add_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { ADD_M(DE,RGD);  } }
static void t11_add_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { ADD_M(DE,IN);   } }
static void t11_add_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { ADD_M(DE,IND);  } }
static void t11_add_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { ADD_M(DE,DE);   } }
static void t11_add_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { ADD_M(DE,DED);  } }
static void t11_add_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { ADD_M(DE,IX);   } }
static void t11_add_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { ADD_M(DE,IXD);  } }
static void t11_add_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { ADD_X(DED,RG);  } }
static void t11_add_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { ADD_M(DED,RGD); } }
static void t11_add_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { ADD_M(DED,IN);  } }
static void t11_add_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { ADD_M(DED,IND); } }
static void t11_add_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { ADD_M(DED,DE);  } }
static void t11_add_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { ADD_M(DED,DED); } }
static void t11_add_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { ADD_M(DED,IX);  } }
static void t11_add_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { ADD_M(DED,IXD); } }
static void t11_add_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { ADD_X(IX,RG);   } }
static void t11_add_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { ADD_M(IX,RGD);  } }
static void t11_add_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { ADD_M(IX,IN);   } }
static void t11_add_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { ADD_M(IX,IND);  } }
static void t11_add_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { ADD_M(IX,DE);   } }
static void t11_add_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { ADD_M(IX,DED);  } }
static void t11_add_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { ADD_M(IX,IX);   } }
static void t11_add_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { ADD_M(IX,IXD);  } }
static void t11_add_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { ADD_X(IXD,RG);  } }
static void t11_add_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { ADD_M(IXD,RGD); } }
static void t11_add_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { ADD_M(IXD,IN);  } }
static void t11_add_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { ADD_M(IXD,IND); } }
static void t11_add_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { ADD_M(IXD,DE);  } }
static void t11_add_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { ADD_M(IXD,DED); } }
static void t11_add_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { ADD_M(IXD,IX);  } }
static void t11_add_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { ADD_M(IXD,IXD); } }

static void t11_xor_rg(t11d *cpu, uint16_t op)        { icount_ -= 12; { XOR_R(RG);  } }
static void t11_xor_rgd(t11d *cpu, uint16_t op)       { icount_ -= 21; { XOR_M(RGD); } }
static void t11_xor_in(t11d *cpu, uint16_t op)        { icount_ -= 21; { XOR_M(IN);  } }
static void t11_xor_ind(t11d *cpu, uint16_t op)       { icount_ -= 27; { XOR_M(IND); } }
static void t11_xor_de(t11d *cpu, uint16_t op)        { icount_ -= 24; { XOR_M(DE);  } }
static void t11_xor_ded(t11d *cpu, uint16_t op)       { icount_ -= 30; { XOR_M(DED); } }
static void t11_xor_ix(t11d *cpu, uint16_t op)        { icount_ -= 30; { XOR_M(IX);  } }
static void t11_xor_ixd(t11d *cpu, uint16_t op)       { icount_ -= 36; { XOR_M(IXD); } }

static void t11_sob(t11d *cpu, uint16_t op)
{
	int sreg, source;

	icount_ -= 18;
	GET_SREG; source = REGD(sreg);
	source -= 1;
	REGW(sreg) = source;
	if (source)
		PC -= 2 * (op & 0x3f);
}

static void t11_bpl(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!GET_N); } }
static void t11_bmi(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR( GET_N); } }
static void t11_bhi(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!GET_C && !GET_Z); } }
static void t11_blos(t11d *cpu, uint16_t op)          { icount_ -= 12; { BR( GET_C ||  GET_Z); } }
static void t11_bvc(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!GET_V); } }
static void t11_bvs(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR( GET_V); } }
static void t11_bcc(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR(!GET_C); } }
static void t11_bcs(t11d *cpu, uint16_t op)           { icount_ -= 12; { BR( GET_C); } }

static void t11_emt(t11d *cpu, uint16_t op)
{
	icount_ -= 48;
	trap_to(0x18);
}

static void t11_trap(t11d *cpu, uint16_t op)
{
	icount_ -= 48;
	trap_to(0x1c);
}

static void t11_clrb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { CLRB_R(RG);  } }
static void t11_clrb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { CLRB_M(RGD); } }
static void t11_clrb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { CLRB_M(IN);  } }
static void t11_clrb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { CLRB_M(IND); } }
static void t11_clrb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { CLRB_M(DE);  } }
static void t11_clrb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { CLRB_M(DED); } }
static void t11_clrb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { CLRB_M(IX);  } }
static void t11_clrb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { CLRB_M(IXD); } }

static void t11_comb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { COMB_R(RG);  } }
static void t11_comb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { COMB_M(RGD); } }
static void t11_comb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { COMB_M(IN);  } }
static void t11_comb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { COMB_M(IND); } }
static void t11_comb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { COMB_M(DE);  } }
static void t11_comb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { COMB_M(DED); } }
static void t11_comb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { COMB_M(IX);  } }
static void t11_comb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { COMB_M(IXD); } }

static void t11_incb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { INCB_R(RG);  } }
static void t11_incb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { INCB_M(RGD); } }
static void t11_incb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { INCB_M(IN);  } }
static void t11_incb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { INCB_M(IND); } }
static void t11_incb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { INCB_M(DE);  } }
static void t11_incb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { INCB_M(DED); } }
static void t11_incb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { INCB_M(IX);  } }
static void t11_incb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { INCB_M(IXD); } }

static void t11_decb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { DECB_R(RG);  } }
static void t11_decb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { DECB_M(RGD); } }
static void t11_decb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { DECB_M(IN);  } }
static void t11_decb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { DECB_M(IND); } }
static void t11_decb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { DECB_M(DE);  } }
static void t11_decb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { DECB_M(DED); } }
static void t11_decb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { DECB_M(IX);  } }
static void t11_decb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { DECB_M(IXD); } }

static void t11_negb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { NEGB_R(RG);  } }
static void t11_negb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { NEGB_M(RGD); } }
static void t11_negb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { NEGB_M(IN);  } }
static void t11_negb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { NEGB_M(IND); } }
static void t11_negb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { NEGB_M(DE);  } }
static void t11_negb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { NEGB_M(DED); } }
static void t11_negb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { NEGB_M(IX);  } }
static void t11_negb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { NEGB_M(IXD); } }

static void t11_adcb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { ADCB_R(RG);  } }
static void t11_adcb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { ADCB_M(RGD); } }
static void t11_adcb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { ADCB_M(IN);  } }
static void t11_adcb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { ADCB_M(IND); } }
static void t11_adcb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { ADCB_M(DE);  } }
static void t11_adcb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { ADCB_M(DED); } }
static void t11_adcb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { ADCB_M(IX);  } }
static void t11_adcb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { ADCB_M(IXD); } }

static void t11_sbcb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { SBCB_R(RG);  } }
static void t11_sbcb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { SBCB_M(RGD); } }
static void t11_sbcb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { SBCB_M(IN);  } }
static void t11_sbcb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { SBCB_M(IND); } }
static void t11_sbcb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { SBCB_M(DE);  } }
static void t11_sbcb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { SBCB_M(DED); } }
static void t11_sbcb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { SBCB_M(IX);  } }
static void t11_sbcb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { SBCB_M(IXD); } }

static void t11_tstb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { TSTB_R(RG);  } }
static void t11_tstb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 18; { TSTB_M(RGD); } }
static void t11_tstb_in(t11d *cpu, uint16_t op)       { icount_ -= 18; { TSTB_M(IN);  } }
static void t11_tstb_ind(t11d *cpu, uint16_t op)      { icount_ -= 24; { TSTB_M(IND); } }
static void t11_tstb_de(t11d *cpu, uint16_t op)       { icount_ -= 21; { TSTB_M(DE);  } }
static void t11_tstb_ded(t11d *cpu, uint16_t op)      { icount_ -= 27; { TSTB_M(DED); } }
static void t11_tstb_ix(t11d *cpu, uint16_t op)       { icount_ -= 27; { TSTB_M(IX);  } }
static void t11_tstb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 33; { TSTB_M(IXD); } }

static void t11_rorb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { RORB_R(RG);  } }
static void t11_rorb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { RORB_M(RGD); } }
static void t11_rorb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { RORB_M(IN);  } }
static void t11_rorb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { RORB_M(IND); } }
static void t11_rorb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { RORB_M(DE);  } }
static void t11_rorb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { RORB_M(DED); } }
static void t11_rorb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { RORB_M(IX);  } }
static void t11_rorb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { RORB_M(IXD); } }

static void t11_rolb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { ROLB_R(RG);  } }
static void t11_rolb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { ROLB_M(RGD); } }
static void t11_rolb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { ROLB_M(IN);  } }
static void t11_rolb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { ROLB_M(IND); } }
static void t11_rolb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { ROLB_M(DE);  } }
static void t11_rolb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { ROLB_M(DED); } }
static void t11_rolb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { ROLB_M(IX);  } }
static void t11_rolb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { ROLB_M(IXD); } }

static void t11_asrb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { ASRB_R(RG);  } }
static void t11_asrb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { ASRB_M(RGD); } }
static void t11_asrb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { ASRB_M(IN);  } }
static void t11_asrb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { ASRB_M(IND); } }
static void t11_asrb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { ASRB_M(DE);  } }
static void t11_asrb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { ASRB_M(DED); } }
static void t11_asrb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { ASRB_M(IX);  } }
static void t11_asrb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { ASRB_M(IXD); } }

static void t11_aslb_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { ASLB_R(RG);  } }
static void t11_aslb_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { ASLB_M(RGD); } }
static void t11_aslb_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { ASLB_M(IN);  } }
static void t11_aslb_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { ASLB_M(IND); } }
static void t11_aslb_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { ASLB_M(DE);  } }
static void t11_aslb_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { ASLB_M(DED); } }
static void t11_aslb_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { ASLB_M(IX);  } }
static void t11_aslb_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { ASLB_M(IXD); } }

static void t11_mtps_rg(t11d *cpu, uint16_t op)       { icount_ -= 24; { MTPS_R(RG);  } }
static void t11_mtps_rgd(t11d *cpu, uint16_t op)      { icount_ -= 30; { MTPS_M(RGD); } }
static void t11_mtps_in(t11d *cpu, uint16_t op)       { icount_ -= 30; { MTPS_M(IN);  } }
static void t11_mtps_ind(t11d *cpu, uint16_t op)      { icount_ -= 36; { MTPS_M(IND); } }
static void t11_mtps_de(t11d *cpu, uint16_t op)       { icount_ -= 33; { MTPS_M(DE);  } }
static void t11_mtps_ded(t11d *cpu, uint16_t op)      { icount_ -= 39; { MTPS_M(DED); } }
static void t11_mtps_ix(t11d *cpu, uint16_t op)       { icount_ -= 39; { MTPS_M(IX);  } }
static void t11_mtps_ixd(t11d *cpu, uint16_t op)      { icount_ -= 45; { MTPS_M(IXD); } }

static void t11_mfps_rg(t11d *cpu, uint16_t op)       { icount_ -= 12; { MFPS_R(RG);  } }
static void t11_mfps_rgd(t11d *cpu, uint16_t op)      { icount_ -= 21; { MFPS_M(RGD); } }
static void t11_mfps_in(t11d *cpu, uint16_t op)       { icount_ -= 21; { MFPS_M(IN);  } }
static void t11_mfps_ind(t11d *cpu, uint16_t op)      { icount_ -= 27; { MFPS_M(IND); } }
static void t11_mfps_de(t11d *cpu, uint16_t op)       { icount_ -= 24; { MFPS_M(DE);  } }
static void t11_mfps_ded(t11d *cpu, uint16_t op)      { icount_ -= 30; { MFPS_M(DED); } }
static void t11_mfps_ix(t11d *cpu, uint16_t op)       { icount_ -= 30; { MFPS_M(IX);  } }
static void t11_mfps_ixd(t11d *cpu, uint16_t op)      { icount_ -= 36; { MFPS_M(IXD); } }

static void t11_movb_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { MOVB_R(RG,RG);   } }
static void t11_movb_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { MOVB_M(RG,RGD);  } }
static void t11_movb_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { MOVB_M(RG,IN);   } }
static void t11_movb_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { MOVB_M(RG,IND);  } }
static void t11_movb_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { MOVB_M(RG,DE);   } }
static void t11_movb_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { MOVB_M(RG,DED);  } }
static void t11_movb_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { MOVB_M(RG,IX);   } }
static void t11_movb_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { MOVB_M(RG,IXD);  } }
static void t11_movb_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { MOVB_X(RGD,RG);  } }
static void t11_movb_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { MOVB_M(RGD,RGD); } }
static void t11_movb_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { MOVB_M(RGD,IN);  } }
static void t11_movb_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { MOVB_M(RGD,IND); } }
static void t11_movb_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { MOVB_M(RGD,DE);  } }
static void t11_movb_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { MOVB_M(RGD,DED); } }
static void t11_movb_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { MOVB_M(RGD,IX);  } }
static void t11_movb_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { MOVB_M(RGD,IXD); } }
static void t11_movb_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { MOVB_X(IN,RG);   } }
static void t11_movb_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { MOVB_M(IN,RGD);  } }
static void t11_movb_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { MOVB_M(IN,IN);   } }
static void t11_movb_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { MOVB_M(IN,IND);  } }
static void t11_movb_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { MOVB_M(IN,DE);   } }
static void t11_movb_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { MOVB_M(IN,DED);  } }
static void t11_movb_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { MOVB_M(IN,IX);   } }
static void t11_movb_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { MOVB_M(IN,IXD);  } }
static void t11_movb_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { MOVB_X(IND,RG);  } }
static void t11_movb_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { MOVB_M(IND,RGD); } }
static void t11_movb_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { MOVB_M(IND,IN);  } }
static void t11_movb_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { MOVB_M(IND,IND); } }
static void t11_movb_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { MOVB_M(IND,DE);  } }
static void t11_movb_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { MOVB_M(IND,DED); } }
static void t11_movb_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { MOVB_M(IND,IX);  } }
static void t11_movb_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { MOVB_M(IND,IXD); } }
static void t11_movb_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { MOVB_X(DE,RG);   } }
static void t11_movb_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { MOVB_M(DE,RGD);  } }
static void t11_movb_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { MOVB_M(DE,IN);   } }
static void t11_movb_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { MOVB_M(DE,IND);  } }
static void t11_movb_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { MOVB_M(DE,DE);   } }
static void t11_movb_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { MOVB_M(DE,DED);  } }
static void t11_movb_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { MOVB_M(DE,IX);   } }
static void t11_movb_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { MOVB_M(DE,IXD);  } }
static void t11_movb_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { MOVB_X(DED,RG);  } }
static void t11_movb_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { MOVB_M(DED,RGD); } }
static void t11_movb_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { MOVB_M(DED,IN);  } }
static void t11_movb_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { MOVB_M(DED,IND); } }
static void t11_movb_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { MOVB_M(DED,DE);  } }
static void t11_movb_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { MOVB_M(DED,DED); } }
static void t11_movb_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { MOVB_M(DED,IX);  } }
static void t11_movb_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { MOVB_M(DED,IXD); } }
static void t11_movb_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { MOVB_X(IX,RG);   } }
static void t11_movb_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { MOVB_M(IX,RGD);  } }
static void t11_movb_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { MOVB_M(IX,IN);   } }
static void t11_movb_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { MOVB_M(IX,IND);  } }
static void t11_movb_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { MOVB_M(IX,DE);   } }
static void t11_movb_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { MOVB_M(IX,DED);  } }
static void t11_movb_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { MOVB_M(IX,IX);   } }
static void t11_movb_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { MOVB_M(IX,IXD);  } }
static void t11_movb_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { MOVB_X(IXD,RG);  } }
static void t11_movb_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { MOVB_M(IXD,RGD); } }
static void t11_movb_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { MOVB_M(IXD,IN);  } }
static void t11_movb_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { MOVB_M(IXD,IND); } }
static void t11_movb_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { MOVB_M(IXD,DE);  } }
static void t11_movb_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { MOVB_M(IXD,DED); } }
static void t11_movb_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { MOVB_M(IXD,IX);  } }
static void t11_movb_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { MOVB_M(IXD,IXD); } }

static void t11_cmpb_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { CMPB_R(RG,RG);   } }
static void t11_cmpb_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+ 9; { CMPB_M(RG,RGD);  } }
static void t11_cmpb_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+ 9; { CMPB_M(RG,IN);   } }
static void t11_cmpb_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+15; { CMPB_M(RG,IND);  } }
static void t11_cmpb_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { CMPB_M(RG,DE);   } }
static void t11_cmpb_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { CMPB_M(RG,DED);  } }
static void t11_cmpb_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+18; { CMPB_M(RG,IX);   } }
static void t11_cmpb_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+24; { CMPB_M(RG,IXD);  } }
static void t11_cmpb_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { CMPB_M(RGD,RG);  } }
static void t11_cmpb_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+ 9; { CMPB_M(RGD,RGD); } }
static void t11_cmpb_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { CMPB_M(RGD,IN);  } }
static void t11_cmpb_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+15; { CMPB_M(RGD,IND); } }
static void t11_cmpb_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { CMPB_M(RGD,DE);  } }
static void t11_cmpb_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { CMPB_M(RGD,DED); } }
static void t11_cmpb_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { CMPB_M(RGD,IX);  } }
static void t11_cmpb_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+24; { CMPB_M(RGD,IXD); } }
static void t11_cmpb_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { CMPB_M(IN,RG);   } }
static void t11_cmpb_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { CMPB_M(IN,RGD);  } }
static void t11_cmpb_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+ 9; { CMPB_M(IN,IN);   } }
static void t11_cmpb_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { CMPB_M(IN,IND);  } }
static void t11_cmpb_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { CMPB_M(IN,DE);   } }
static void t11_cmpb_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { CMPB_M(IN,DED);  } }
static void t11_cmpb_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+18; { CMPB_M(IN,IX);   } }
static void t11_cmpb_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+24; { CMPB_M(IN,IXD);  } }
static void t11_cmpb_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { CMPB_M(IND,RG);  } }
static void t11_cmpb_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+ 9; { CMPB_M(IND,RGD); } }
static void t11_cmpb_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+ 9; { CMPB_M(IND,IN);  } }
static void t11_cmpb_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+15; { CMPB_M(IND,IND); } }
static void t11_cmpb_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { CMPB_M(IND,DE);  } }
static void t11_cmpb_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { CMPB_M(IND,DED); } }
static void t11_cmpb_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+18; { CMPB_M(IND,IX);  } }
static void t11_cmpb_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+24; { CMPB_M(IND,IXD); } }
static void t11_cmpb_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { CMPB_M(DE,RG);   } }
static void t11_cmpb_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+ 9; { CMPB_M(DE,RGD);  } }
static void t11_cmpb_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+ 9; { CMPB_M(DE,IN);   } }
static void t11_cmpb_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+15; { CMPB_M(DE,IND);  } }
static void t11_cmpb_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { CMPB_M(DE,DE);   } }
static void t11_cmpb_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { CMPB_M(DE,DED);  } }
static void t11_cmpb_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+18; { CMPB_M(DE,IX);   } }
static void t11_cmpb_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+24; { CMPB_M(DE,IXD);  } }
static void t11_cmpb_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { CMPB_M(DED,RG);  } }
static void t11_cmpb_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+ 9; { CMPB_M(DED,RGD); } }
static void t11_cmpb_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { CMPB_M(DED,IN);  } }
static void t11_cmpb_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+15; { CMPB_M(DED,IND); } }
static void t11_cmpb_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { CMPB_M(DED,DE);  } }
static void t11_cmpb_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { CMPB_M(DED,DED); } }
static void t11_cmpb_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { CMPB_M(DED,IX);  } }
static void t11_cmpb_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+24; { CMPB_M(DED,IXD); } }
static void t11_cmpb_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { CMPB_M(IX,RG);   } }
static void t11_cmpb_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { CMPB_M(IX,RGD);  } }
static void t11_cmpb_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+ 9; { CMPB_M(IX,IN);   } }
static void t11_cmpb_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { CMPB_M(IX,IND);  } }
static void t11_cmpb_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { CMPB_M(IX,DE);   } }
static void t11_cmpb_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { CMPB_M(IX,DED);  } }
static void t11_cmpb_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+18; { CMPB_M(IX,IX);   } }
static void t11_cmpb_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+24; { CMPB_M(IX,IXD);  } }
static void t11_cmpb_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { CMPB_M(IXD,RG);  } }
static void t11_cmpb_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+ 9; { CMPB_M(IXD,RGD); } }
static void t11_cmpb_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+ 9; { CMPB_M(IXD,IN);  } }
static void t11_cmpb_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+15; { CMPB_M(IXD,IND); } }
static void t11_cmpb_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { CMPB_M(IXD,DE);  } }
static void t11_cmpb_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { CMPB_M(IXD,DED); } }
static void t11_cmpb_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+18; { CMPB_M(IXD,IX);  } }
static void t11_cmpb_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+24; { CMPB_M(IXD,IXD); } }

static void t11_bitb_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { BITB_R(RG,RG);   } }
static void t11_bitb_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+ 9; { BITB_M(RG,RGD);  } }
static void t11_bitb_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+ 9; { BITB_M(RG,IN);   } }
static void t11_bitb_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+15; { BITB_M(RG,IND);  } }
static void t11_bitb_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { BITB_M(RG,DE);   } }
static void t11_bitb_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { BITB_M(RG,DED);  } }
static void t11_bitb_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+18; { BITB_M(RG,IX);   } }
static void t11_bitb_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+24; { BITB_M(RG,IXD);  } }
static void t11_bitb_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { BITB_M(RGD,RG);  } }
static void t11_bitb_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+ 9; { BITB_M(RGD,RGD); } }
static void t11_bitb_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { BITB_M(RGD,IN);  } }
static void t11_bitb_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+15; { BITB_M(RGD,IND); } }
static void t11_bitb_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BITB_M(RGD,DE);  } }
static void t11_bitb_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { BITB_M(RGD,DED); } }
static void t11_bitb_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BITB_M(RGD,IX);  } }
static void t11_bitb_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+24; { BITB_M(RGD,IXD); } }
static void t11_bitb_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { BITB_M(IN,RG);   } }
static void t11_bitb_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+ 9; { BITB_M(IN,RGD);  } }
static void t11_bitb_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+ 9; { BITB_M(IN,IN);   } }
static void t11_bitb_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { BITB_M(IN,IND);  } }
static void t11_bitb_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { BITB_M(IN,DE);   } }
static void t11_bitb_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BITB_M(IN,DED);  } }
static void t11_bitb_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+18; { BITB_M(IN,IX);   } }
static void t11_bitb_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+24; { BITB_M(IN,IXD);  } }
static void t11_bitb_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { BITB_M(IND,RG);  } }
static void t11_bitb_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+ 9; { BITB_M(IND,RGD); } }
static void t11_bitb_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+ 9; { BITB_M(IND,IN);  } }
static void t11_bitb_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+15; { BITB_M(IND,IND); } }
static void t11_bitb_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { BITB_M(IND,DE);  } }
static void t11_bitb_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { BITB_M(IND,DED); } }
static void t11_bitb_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+18; { BITB_M(IND,IX);  } }
static void t11_bitb_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+24; { BITB_M(IND,IXD); } }
static void t11_bitb_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { BITB_M(DE,RG);   } }
static void t11_bitb_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+ 9; { BITB_M(DE,RGD);  } }
static void t11_bitb_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+ 9; { BITB_M(DE,IN);   } }
static void t11_bitb_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+15; { BITB_M(DE,IND);  } }
static void t11_bitb_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { BITB_M(DE,DE);   } }
static void t11_bitb_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { BITB_M(DE,DED);  } }
static void t11_bitb_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+18; { BITB_M(DE,IX);   } }
static void t11_bitb_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+24; { BITB_M(DE,IXD);  } }
static void t11_bitb_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { BITB_M(DED,RG);  } }
static void t11_bitb_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+ 9; { BITB_M(DED,RGD); } }
static void t11_bitb_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { BITB_M(DED,IN);  } }
static void t11_bitb_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+15; { BITB_M(DED,IND); } }
static void t11_bitb_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BITB_M(DED,DE);  } }
static void t11_bitb_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { BITB_M(DED,DED); } }
static void t11_bitb_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BITB_M(DED,IX);  } }
static void t11_bitb_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+24; { BITB_M(DED,IXD); } }
static void t11_bitb_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { BITB_M(IX,RG);   } }
static void t11_bitb_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+ 9; { BITB_M(IX,RGD);  } }
static void t11_bitb_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+ 9; { BITB_M(IX,IN);   } }
static void t11_bitb_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { BITB_M(IX,IND);  } }
static void t11_bitb_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { BITB_M(IX,DE);   } }
static void t11_bitb_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BITB_M(IX,DED);  } }
static void t11_bitb_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+18; { BITB_M(IX,IX);   } }
static void t11_bitb_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+24; { BITB_M(IX,IXD);  } }
static void t11_bitb_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { BITB_M(IXD,RG);  } }
static void t11_bitb_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+ 9; { BITB_M(IXD,RGD); } }
static void t11_bitb_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+ 9; { BITB_M(IXD,IN);  } }
static void t11_bitb_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+15; { BITB_M(IXD,IND); } }
static void t11_bitb_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { BITB_M(IXD,DE);  } }
static void t11_bitb_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { BITB_M(IXD,DED); } }
static void t11_bitb_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+18; { BITB_M(IXD,IX);  } }
static void t11_bitb_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+24; { BITB_M(IXD,IXD); } }

static void t11_bicb_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { BICB_R(RG,RG);   } }
static void t11_bicb_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { BICB_M(RG,RGD);  } }
static void t11_bicb_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { BICB_M(RG,IN);   } }
static void t11_bicb_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { BICB_M(RG,IND);  } }
static void t11_bicb_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { BICB_M(RG,DE);   } }
static void t11_bicb_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { BICB_M(RG,DED);  } }
static void t11_bicb_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { BICB_M(RG,IX);   } }
static void t11_bicb_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { BICB_M(RG,IXD);  } }
static void t11_bicb_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { BICB_X(RGD,RG);  } }
static void t11_bicb_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { BICB_M(RGD,RGD); } }
static void t11_bicb_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BICB_M(RGD,IN);  } }
static void t11_bicb_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { BICB_M(RGD,IND); } }
static void t11_bicb_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { BICB_M(RGD,DE);  } }
static void t11_bicb_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { BICB_M(RGD,DED); } }
static void t11_bicb_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BICB_M(RGD,IX);  } }
static void t11_bicb_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { BICB_M(RGD,IXD); } }
static void t11_bicb_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { BICB_X(IN,RG);   } }
static void t11_bicb_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BICB_M(IN,RGD);  } }
static void t11_bicb_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { BICB_M(IN,IN);   } }
static void t11_bicb_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BICB_M(IN,IND);  } }
static void t11_bicb_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { BICB_M(IN,DE);   } }
static void t11_bicb_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BICB_M(IN,DED);  } }
static void t11_bicb_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { BICB_M(IN,IX);   } }
static void t11_bicb_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { BICB_M(IN,IXD);  } }
static void t11_bicb_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { BICB_X(IND,RG);  } }
static void t11_bicb_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { BICB_M(IND,RGD); } }
static void t11_bicb_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { BICB_M(IND,IN);  } }
static void t11_bicb_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { BICB_M(IND,IND); } }
static void t11_bicb_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { BICB_M(IND,DE);  } }
static void t11_bicb_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { BICB_M(IND,DED); } }
static void t11_bicb_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { BICB_M(IND,IX);  } }
static void t11_bicb_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { BICB_M(IND,IXD); } }
static void t11_bicb_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { BICB_X(DE,RG);   } }
static void t11_bicb_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { BICB_M(DE,RGD);  } }
static void t11_bicb_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { BICB_M(DE,IN);   } }
static void t11_bicb_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { BICB_M(DE,IND);  } }
static void t11_bicb_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { BICB_M(DE,DE);   } }
static void t11_bicb_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { BICB_M(DE,DED);  } }
static void t11_bicb_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { BICB_M(DE,IX);   } }
static void t11_bicb_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { BICB_M(DE,IXD);  } }
static void t11_bicb_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { BICB_X(DED,RG);  } }
static void t11_bicb_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { BICB_M(DED,RGD); } }
static void t11_bicb_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BICB_M(DED,IN);  } }
static void t11_bicb_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { BICB_M(DED,IND); } }
static void t11_bicb_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { BICB_M(DED,DE);  } }
static void t11_bicb_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { BICB_M(DED,DED); } }
static void t11_bicb_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BICB_M(DED,IX);  } }
static void t11_bicb_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { BICB_M(DED,IXD); } }
static void t11_bicb_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { BICB_X(IX,RG);   } }
static void t11_bicb_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BICB_M(IX,RGD);  } }
static void t11_bicb_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { BICB_M(IX,IN);   } }
static void t11_bicb_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BICB_M(IX,IND);  } }
static void t11_bicb_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { BICB_M(IX,DE);   } }
static void t11_bicb_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BICB_M(IX,DED);  } }
static void t11_bicb_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { BICB_M(IX,IX);   } }
static void t11_bicb_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { BICB_M(IX,IXD);  } }
static void t11_bicb_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { BICB_X(IXD,RG);  } }
static void t11_bicb_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { BICB_M(IXD,RGD); } }
static void t11_bicb_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { BICB_M(IXD,IN);  } }
static void t11_bicb_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { BICB_M(IXD,IND); } }
static void t11_bicb_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { BICB_M(IXD,DE);  } }
static void t11_bicb_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { BICB_M(IXD,DED); } }
static void t11_bicb_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { BICB_M(IXD,IX);  } }
static void t11_bicb_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { BICB_M(IXD,IXD); } }

static void t11_bisb_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { BISB_R(RG,RG);   } }
static void t11_bisb_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { BISB_M(RG,RGD);  } }
static void t11_bisb_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { BISB_M(RG,IN);   } }
static void t11_bisb_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { BISB_M(RG,IND);  } }
static void t11_bisb_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { BISB_M(RG,DE);   } }
static void t11_bisb_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { BISB_M(RG,DED);  } }
static void t11_bisb_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { BISB_M(RG,IX);   } }
static void t11_bisb_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { BISB_M(RG,IXD);  } }
static void t11_bisb_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { BISB_X(RGD,RG);  } }
static void t11_bisb_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { BISB_M(RGD,RGD); } }
static void t11_bisb_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BISB_M(RGD,IN);  } }
static void t11_bisb_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { BISB_M(RGD,IND); } }
static void t11_bisb_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { BISB_M(RGD,DE);  } }
static void t11_bisb_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { BISB_M(RGD,DED); } }
static void t11_bisb_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BISB_M(RGD,IX);  } }
static void t11_bisb_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { BISB_M(RGD,IXD); } }
static void t11_bisb_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { BISB_X(IN,RG);   } }
static void t11_bisb_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { BISB_M(IN,RGD);  } }
static void t11_bisb_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { BISB_M(IN,IN);   } }
static void t11_bisb_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { BISB_M(IN,IND);  } }
static void t11_bisb_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { BISB_M(IN,DE);   } }
static void t11_bisb_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { BISB_M(IN,DED);  } }
static void t11_bisb_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { BISB_M(IN,IX);   } }
static void t11_bisb_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { BISB_M(IN,IXD);  } }
static void t11_bisb_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { BISB_X(IND,RG);  } }
static void t11_bisb_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { BISB_M(IND,RGD); } }
static void t11_bisb_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { BISB_M(IND,IN);  } }
static void t11_bisb_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { BISB_M(IND,IND); } }
static void t11_bisb_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { BISB_M(IND,DE);  } }
static void t11_bisb_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { BISB_M(IND,DED); } }
static void t11_bisb_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { BISB_M(IND,IX);  } }
static void t11_bisb_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { BISB_M(IND,IXD); } }
static void t11_bisb_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { BISB_X(DE,RG);   } }
static void t11_bisb_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { BISB_M(DE,RGD);  } }
static void t11_bisb_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { BISB_M(DE,IN);   } }
static void t11_bisb_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { BISB_M(DE,IND);  } }
static void t11_bisb_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { BISB_M(DE,DE);   } }
static void t11_bisb_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { BISB_M(DE,DED);  } }
static void t11_bisb_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { BISB_M(DE,IX);   } }
static void t11_bisb_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { BISB_M(DE,IXD);  } }
static void t11_bisb_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { BISB_X(DED,RG);  } }
static void t11_bisb_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { BISB_M(DED,RGD); } }
static void t11_bisb_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BISB_M(DED,IN);  } }
static void t11_bisb_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { BISB_M(DED,IND); } }
static void t11_bisb_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { BISB_M(DED,DE);  } }
static void t11_bisb_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { BISB_M(DED,DED); } }
static void t11_bisb_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BISB_M(DED,IX);  } }
static void t11_bisb_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { BISB_M(DED,IXD); } }
static void t11_bisb_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { BISB_X(IX,RG);   } }
static void t11_bisb_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { BISB_M(IX,RGD);  } }
static void t11_bisb_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { BISB_M(IX,IN);   } }
static void t11_bisb_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { BISB_M(IX,IND);  } }
static void t11_bisb_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { BISB_M(IX,DE);   } }
static void t11_bisb_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { BISB_M(IX,DED);  } }
static void t11_bisb_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { BISB_M(IX,IX);   } }
static void t11_bisb_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { BISB_M(IX,IXD);  } }
static void t11_bisb_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { BISB_X(IXD,RG);  } }
static void t11_bisb_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { BISB_M(IXD,RGD); } }
static void t11_bisb_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { BISB_M(IXD,IN);  } }
static void t11_bisb_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { BISB_M(IXD,IND); } }
static void t11_bisb_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { BISB_M(IXD,DE);  } }
static void t11_bisb_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { BISB_M(IXD,DED); } }
static void t11_bisb_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { BISB_M(IXD,IX);  } }
static void t11_bisb_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { BISB_M(IXD,IXD); } }

static void t11_sub_rg_rg(t11d *cpu, uint16_t op)     { icount_ -=  9+ 3; { SUB_R(RG,RG);   } }
static void t11_sub_rg_rgd(t11d *cpu, uint16_t op)    { icount_ -=  9+12; { SUB_M(RG,RGD);  } }
static void t11_sub_rg_in(t11d *cpu, uint16_t op)     { icount_ -=  9+12; { SUB_M(RG,IN);   } }
static void t11_sub_rg_ind(t11d *cpu, uint16_t op)    { icount_ -=  9+18; { SUB_M(RG,IND);  } }
static void t11_sub_rg_de(t11d *cpu, uint16_t op)     { icount_ -=  9+15; { SUB_M(RG,DE);   } }
static void t11_sub_rg_ded(t11d *cpu, uint16_t op)    { icount_ -=  9+21; { SUB_M(RG,DED);  } }
static void t11_sub_rg_ix(t11d *cpu, uint16_t op)     { icount_ -=  9+21; { SUB_M(RG,IX);   } }
static void t11_sub_rg_ixd(t11d *cpu, uint16_t op)    { icount_ -=  9+27; { SUB_M(RG,IXD);  } }
static void t11_sub_rgd_rg(t11d *cpu, uint16_t op)    { icount_ -= 15+ 3; { SUB_X(RGD,RG);  } }
static void t11_sub_rgd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 15+12; { SUB_M(RGD,RGD); } }
static void t11_sub_rgd_in(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { SUB_M(RGD,IN);  } }
static void t11_sub_rgd_ind(t11d *cpu, uint16_t op)   { icount_ -= 15+18; { SUB_M(RGD,IND); } }
static void t11_sub_rgd_de(t11d *cpu, uint16_t op)    { icount_ -= 15+15; { SUB_M(RGD,DE);  } }
static void t11_sub_rgd_ded(t11d *cpu, uint16_t op)   { icount_ -= 15+21; { SUB_M(RGD,DED); } }
static void t11_sub_rgd_ix(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { SUB_M(RGD,IX);  } }
static void t11_sub_rgd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 15+27; { SUB_M(RGD,IXD); } }
static void t11_sub_in_rg(t11d *cpu, uint16_t op)     { icount_ -= 15+ 3; { SUB_X(IN,RG);   } }
static void t11_sub_in_rgd(t11d *cpu, uint16_t op)    { icount_ -= 15+12; { SUB_M(IN,RGD);  } }
static void t11_sub_in_in(t11d *cpu, uint16_t op)     { icount_ -= 15+12; { SUB_M(IN,IN);   } }
static void t11_sub_in_ind(t11d *cpu, uint16_t op)    { icount_ -= 15+18; { SUB_M(IN,IND);  } }
static void t11_sub_in_de(t11d *cpu, uint16_t op)     { icount_ -= 15+15; { SUB_M(IN,DE);   } }
static void t11_sub_in_ded(t11d *cpu, uint16_t op)    { icount_ -= 15+21; { SUB_M(IN,DED);  } }
static void t11_sub_in_ix(t11d *cpu, uint16_t op)     { icount_ -= 15+21; { SUB_M(IN,IX);   } }
static void t11_sub_in_ixd(t11d *cpu, uint16_t op)    { icount_ -= 15+27; { SUB_M(IN,IXD);  } }
static void t11_sub_ind_rg(t11d *cpu, uint16_t op)    { icount_ -= 21+ 3; { SUB_X(IND,RG);  } }
static void t11_sub_ind_rgd(t11d *cpu, uint16_t op)   { icount_ -= 21+12; { SUB_M(IND,RGD); } }
static void t11_sub_ind_in(t11d *cpu, uint16_t op)    { icount_ -= 21+12; { SUB_M(IND,IN);  } }
static void t11_sub_ind_ind(t11d *cpu, uint16_t op)   { icount_ -= 21+18; { SUB_M(IND,IND); } }
static void t11_sub_ind_de(t11d *cpu, uint16_t op)    { icount_ -= 21+15; { SUB_M(IND,DE);  } }
static void t11_sub_ind_ded(t11d *cpu, uint16_t op)   { icount_ -= 21+21; { SUB_M(IND,DED); } }
static void t11_sub_ind_ix(t11d *cpu, uint16_t op)    { icount_ -= 21+21; { SUB_M(IND,IX);  } }
static void t11_sub_ind_ixd(t11d *cpu, uint16_t op)   { icount_ -= 21+27; { SUB_M(IND,IXD); } }
static void t11_sub_de_rg(t11d *cpu, uint16_t op)     { icount_ -= 18+ 3; { SUB_X(DE,RG);   } }
static void t11_sub_de_rgd(t11d *cpu, uint16_t op)    { icount_ -= 18+12; { SUB_M(DE,RGD);  } }
static void t11_sub_de_in(t11d *cpu, uint16_t op)     { icount_ -= 18+12; { SUB_M(DE,IN);   } }
static void t11_sub_de_ind(t11d *cpu, uint16_t op)    { icount_ -= 18+18; { SUB_M(DE,IND);  } }
static void t11_sub_de_de(t11d *cpu, uint16_t op)     { icount_ -= 18+15; { SUB_M(DE,DE);   } }
static void t11_sub_de_ded(t11d *cpu, uint16_t op)    { icount_ -= 18+21; { SUB_M(DE,DED);  } }
static void t11_sub_de_ix(t11d *cpu, uint16_t op)     { icount_ -= 18+21; { SUB_M(DE,IX);   } }
static void t11_sub_de_ixd(t11d *cpu, uint16_t op)    { icount_ -= 18+27; { SUB_M(DE,IXD);  } }
static void t11_sub_ded_rg(t11d *cpu, uint16_t op)    { icount_ -= 24+ 3; { SUB_X(DED,RG);  } }
static void t11_sub_ded_rgd(t11d *cpu, uint16_t op)   { icount_ -= 24+12; { SUB_M(DED,RGD); } }
static void t11_sub_ded_in(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { SUB_M(DED,IN);  } }
static void t11_sub_ded_ind(t11d *cpu, uint16_t op)   { icount_ -= 24+18; { SUB_M(DED,IND); } }
static void t11_sub_ded_de(t11d *cpu, uint16_t op)    { icount_ -= 24+15; { SUB_M(DED,DE);  } }
static void t11_sub_ded_ded(t11d *cpu, uint16_t op)   { icount_ -= 24+21; { SUB_M(DED,DED); } }
static void t11_sub_ded_ix(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { SUB_M(DED,IX);  } }
static void t11_sub_ded_ixd(t11d *cpu, uint16_t op)   { icount_ -= 24+27; { SUB_M(DED,IXD); } }
static void t11_sub_ix_rg(t11d *cpu, uint16_t op)     { icount_ -= 24+ 3; { SUB_X(IX,RG);   } }
static void t11_sub_ix_rgd(t11d *cpu, uint16_t op)    { icount_ -= 24+12; { SUB_M(IX,RGD);  } }
static void t11_sub_ix_in(t11d *cpu, uint16_t op)     { icount_ -= 24+12; { SUB_M(IX,IN);   } }
static void t11_sub_ix_ind(t11d *cpu, uint16_t op)    { icount_ -= 24+18; { SUB_M(IX,IND);  } }
static void t11_sub_ix_de(t11d *cpu, uint16_t op)     { icount_ -= 24+15; { SUB_M(IX,DE);   } }
static void t11_sub_ix_ded(t11d *cpu, uint16_t op)    { icount_ -= 24+21; { SUB_M(IX,DED);  } }
static void t11_sub_ix_ix(t11d *cpu, uint16_t op)     { icount_ -= 24+21; { SUB_M(IX,IX);   } }
static void t11_sub_ix_ixd(t11d *cpu, uint16_t op)    { icount_ -= 24+27; { SUB_M(IX,IXD);  } }
static void t11_sub_ixd_rg(t11d *cpu, uint16_t op)    { icount_ -= 30+ 3; { SUB_X(IXD,RG);  } }
static void t11_sub_ixd_rgd(t11d *cpu, uint16_t op)   { icount_ -= 30+12; { SUB_M(IXD,RGD); } }
static void t11_sub_ixd_in(t11d *cpu, uint16_t op)    { icount_ -= 30+12; { SUB_M(IXD,IN);  } }
static void t11_sub_ixd_ind(t11d *cpu, uint16_t op)   { icount_ -= 30+18; { SUB_M(IXD,IND); } }
static void t11_sub_ixd_de(t11d *cpu, uint16_t op)    { icount_ -= 30+15; { SUB_M(IXD,DE);  } }
static void t11_sub_ixd_ded(t11d *cpu, uint16_t op)   { icount_ -= 30+21; { SUB_M(IXD,DED); } }
static void t11_sub_ixd_ix(t11d *cpu, uint16_t op)    { icount_ -= 30+21; { SUB_M(IXD,IX);  } }
static void t11_sub_ixd_ixd(t11d *cpu, uint16_t op)   { icount_ -= 30+27; { SUB_M(IXD,IXD); } }

int t11d_run(t11d *cpu, int cycles)
{
    cpu->icount = cycles;
    while (cpu->icount > 0) {
        t11_check_irqs(cpu);
        if (cpu->wait_state) {
            cpu->icount = 0;
            break;
        }
        const uint16_t op = (uint16_t)ROPCODE();
        s_opcode_table[op >> 3](cpu, op);
    }
    return cycles - cpu->icount;
}
