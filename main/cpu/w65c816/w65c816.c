#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "w65c816.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif



enum { MODE_IMM, MODE_DP, MODE_DPX, MODE_DPY, MODE_DPIND, MODE_DPINDX, MODE_DPINDY, MODE_DPINDLONG, MODE_DPINDLONGY,
       MODE_ABS, MODE_ABSX, MODE_ABSY, MODE_LONG, MODE_LONGX, MODE_SR, MODE_SRINDY };
typedef int Mode;

static inline uint8_t rd(w65c816 *P, uint32_t address) { return P->read ? P->read(P->user, address & 0xffffff) : 0xff; }
static inline void wr(w65c816 *P, uint32_t address, uint8_t value) { if (P->write) P->write(P->user, address & 0xffffff, value); }
static inline uint16_t rd16_long(w65c816 *P, uint32_t address) { return (uint16_t)(rd(P, address) | (rd(P, address + 1) << 8)); }
static inline void wr16_long(w65c816 *P, uint32_t address, uint16_t v) { wr(P, address, (uint8_t)v); wr(P, address + 1, (uint8_t)(v >> 8)); }
static inline uint16_t rd16_bank0(w65c816 *P, uint16_t address) { return (uint16_t)(rd(P, address) | (rd(P, (uint16_t)(address + 1)) << 8)); }
static inline void set_nz8(w65c816 *P, uint8_t value) { P->p.z = value == 0; P->p.n = (value & 0x80) != 0; }
static inline void set_nz16(w65c816 *P, uint16_t value) { P->p.z = value == 0; P->p.n = (value & 0x8000) != 0; }
static inline void push8n(w65c816 *P, uint8_t value) { wr(P, P->sp, value); P->sp = (uint16_t)(P->sp - 1); }
static inline uint8_t pull8n(w65c816 *P) { P->sp = (uint16_t)(P->sp + 1); return rd(P, P->sp); }
static inline void fix_sp(w65c816 *P) { if (P->e_) P->sp = (uint16_t)(0x100 | (P->sp & 0xff)); }
static uint16_t read_vector(w65c816 *P, uint16_t vector);

void w65c816_init(w65c816 *P, uint32_t clock)
{
    P->a = P->x = P->y = 0;
    P->sp = 0x01ff;
    P->d = 0;
    P->pbr = P->dbr = 0;
    P->p = (w65c816_flags){ false, false, false, false, true, false, true, true };
    P->e_ = true;
    P->pc_ = 0;
    P->clock_ = clock;
    P->irq_request_ = P->nmi_request_ = IRQ_CLEAR;
    P->nmi_latched_ = P->stopped_ = P->waiting_ = P->end_slice_ = false;
    P->cycles_ = 0;
    P->bank0_mode_ = false;
}

void w65c816_reset(w65c816 *P) {
    P->e_ = true;
    P->p.m = true;
    P->p.x = true;
    P->p.i = true;
    P->p.d = false;
    P->x &= 0xff;
    P->y &= 0xff;
    P->d = 0;
    P->dbr = 0;
    P->pbr = 0;
    P->sp = (uint16_t)(0x100 | (P->sp & 0xff));
    P->pc_ = read_vector(P, 0xfffc);
    P->irq_request_ = IRQ_CLEAR;
    P->nmi_request_ = IRQ_CLEAR;
    P->nmi_latched_ = false;
    P->stopped_ = false;
    P->waiting_ = false;
}
void w65c816_set_emulation(w65c816 *P, bool e) {
    P->e_ = e;
    if (P->e_) {
        P->p.m = P->p.x = true;
        P->x &= 0xff;
        P->y &= 0xff;
        P->sp = (uint16_t)(0x100 | (P->sp & 0xff));
    }
}
void w65c816_set_nmi(w65c816 *P, int state) {
    P->nmi_request_ = state;
    if (state == IRQ_CLEAR) P->nmi_latched_ = false;
}
static uint8_t fetch8(w65c816 *P) {
    uint8_t v = rd(P, ((uint32_t)(P->pbr) << 16) | P->pc_);
    P->pc_ = (uint16_t)(P->pc_ + 1);
    return v;
}
static uint16_t fetch16(w65c816 *P) {
    uint8_t lo = fetch8(P);
    uint8_t hi = fetch8(P);
    return (uint16_t)(lo | (hi << 8));
}
static uint32_t fetch24(w65c816 *P) {
    uint32_t lo = fetch16(P);
    uint32_t hi = fetch8(P);
    return lo | (hi << 16);
}
uint8_t w65c816_get_p(w65c816 *P) {
    uint8_t v = (uint8_t)((uint8_t)(P->p.c) | ((uint8_t)(P->p.z) << 1) | ((uint8_t)(P->p.i) << 2) | ((uint8_t)(P->p.d) << 3) |
                        ((uint8_t)(P->p.v) << 6) | ((uint8_t)(P->p.n) << 7));
    if (P->e_) {
        v |= 0x30;
    } else {
        v |= (uint8_t)(((uint8_t)(P->p.m) << 5) | ((uint8_t)(P->p.x) << 4));
    }
    return v;
}
void w65c816_set_p(w65c816 *P, uint8_t value) {
    P->p.c = (value & 0x01) != 0;
    P->p.z = (value & 0x02) != 0;
    P->p.i = (value & 0x04) != 0;
    P->p.d = (value & 0x08) != 0;
    P->p.v = (value & 0x40) != 0;
    P->p.n = (value & 0x80) != 0;
    if (P->e_) {
        P->p.m = P->p.x = true;
    } else {
        P->p.x = (value & 0x10) != 0;
        P->p.m = (value & 0x20) != 0;
    }
    if (P->p.x) {
        P->x &= 0xff;
        P->y &= 0xff;
    }
}
static uint32_t dp_addr(w65c816 *P, uint32_t offset) {
    if (P->e_ && (P->d & 0xff) == 0) return (uint32_t)((P->d & 0xff00) | (offset & 0xff));
    return (uint32_t)((uint16_t)(P->d + offset));
}
static void push8(w65c816 *P, uint8_t value) {
    wr(P, P->sp, value);
    P->sp = P->e_ ? (uint16_t)(0x100 | (uint8_t)(P->sp - 1)) : (uint16_t)(P->sp - 1);
}
static uint8_t pull8(w65c816 *P) {
    P->sp = P->e_ ? (uint16_t)(0x100 | (uint8_t)(P->sp + 1)) : (uint16_t)(P->sp + 1);
    return rd(P, P->sp);
}
static uint16_t read_vector(w65c816 *P, uint16_t vector) {
    uint32_t address = P->vector_handler ? P->vector_handler(P->user, vector) : vector;
    return (uint16_t)(rd(P, address) | (rd(P, address + 1) << 8));
}
static void interrupt(w65c816 *P, uint16_t vector_native, uint16_t vector_emulated, bool brk) {
    if (P->e_) {
        push8(P, (uint8_t)(P->pc_ >> 8));
        push8(P, (uint8_t)(P->pc_));
        uint8_t pv = w65c816_get_p(P);
        if (!brk) pv = (uint8_t)(pv & ~0x10);
        push8(P, pv);
        P->cycles_ += 7;
    } else {
        push8(P, P->pbr);
        push8(P, (uint8_t)(P->pc_ >> 8));
        push8(P, (uint8_t)(P->pc_));
        push8(P, w65c816_get_p(P));
        P->cycles_ += 8;
    }
    P->p.i = true;
    P->p.d = false;
    P->pbr = 0;
    P->pc_ = read_vector(P, P->e_ ? vector_emulated : vector_native);
}

// ---------------------------------------------------------------------------
// Addressing
// ---------------------------------------------------------------------------
static uint32_t ea(w65c816 *P, Mode mode, bool wide, bool is_write) {
    P->bank0_mode_ = false;
    const bool dl = (P->d & 0xff) != 0;
    switch (mode) {
        case MODE_IMM: {
            uint32_t address = ((uint32_t)(P->pbr) << 16) | P->pc_;
            P->pc_ = (uint16_t)(P->pc_ + (wide ? 2 : 1));
            P->bank0_mode_ = true;  // operand bytes wrap inside the program bank
            return address;
        }
        case MODE_DP: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            P->bank0_mode_ = true;
            return dp_addr(P, off);
        }
        case MODE_DPX: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            P->bank0_mode_ = true;
            return dp_addr(P, (uint32_t)(off) + P->x);
        }
        case MODE_DPY: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            P->bank0_mode_ = true;
            return dp_addr(P, (uint32_t)(off) + P->y);
        }
        case MODE_DPIND: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            uint16_t ptr = (uint16_t)(rd(P, dp_addr(P, off)) | (rd(P, dp_addr(P, (uint32_t)(off) + 1)) << 8));
            return ((uint32_t)(P->dbr) << 16) | ptr;
        }
        case MODE_DPINDX: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            uint32_t lo_addr = dp_addr(P, (uint32_t)(off) + P->x);
            uint16_t ptr = (uint16_t)(rd(P, lo_addr) | (rd(P, (uint16_t)(lo_addr + 1)) << 8));
            return ((uint32_t)(P->dbr) << 16) | ptr;
        }
        case MODE_DPINDY: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            uint16_t ptr = (uint16_t)(rd(P, dp_addr(P, off)) | (rd(P, dp_addr(P, (uint32_t)(off) + 1)) << 8));
            uint32_t base = ((uint32_t)(P->dbr) << 16) | ptr;
            uint32_t address = (base + P->y) & 0xffffff;
            if (is_write || !P->p.x || ((base ^ address) & 0xff00)) P->cycles_++;
            return address;
        }
        case MODE_DPINDLONG:
        case MODE_DPINDLONGY: {
            uint8_t off = fetch8(P);
            if (dl) P->cycles_++;
            uint16_t pa = (uint16_t)(P->d + off);
            uint32_t ptr = (uint32_t)(rd(P, pa)) | ((uint32_t)(rd(P, (uint16_t)(pa + 1))) << 8) |
                           ((uint32_t)(rd(P, (uint16_t)(pa + 2))) << 16);
            if (mode == MODE_DPINDLONGY) ptr = (ptr + P->y) & 0xffffff;
            return ptr;
        }
        case MODE_ABS:
            return ((uint32_t)(P->dbr) << 16) | fetch16(P);
        case MODE_ABSX:
        case MODE_ABSY: {
            uint32_t base = ((uint32_t)(P->dbr) << 16) | fetch16(P);
            uint32_t address = (base + (mode == MODE_ABSX ? P->x : P->y)) & 0xffffff;
            if (is_write || !P->p.x || ((base ^ address) & 0xff00)) P->cycles_++;
            return address;
        }
        case MODE_LONG:
            return fetch24(P);
        case MODE_LONGX:
            return (fetch24(P) + P->x) & 0xffffff;
        case MODE_SR: {
            uint8_t off = fetch8(P);
            P->bank0_mode_ = true;
            return (uint16_t)(P->sp + off);
        }
        case MODE_SRINDY: {
            uint8_t off = fetch8(P);
            uint16_t pa = (uint16_t)(P->sp + off);
            uint16_t ptr = (uint16_t)(rd(P, pa) | (rd(P, (uint16_t)(pa + 1)) << 8));
            return (((uint32_t)(P->dbr) << 16) + ptr + P->y) & 0xffffff;
        }
    }
    return 0;
}
static uint16_t load(w65c816 *P, uint32_t address, bool wide) {
    if (!wide) return rd(P, address);
    if (P->bank0_mode_) {
        return (uint16_t)(rd(P, address) | (rd(P, (address & 0xff0000) | (uint16_t)(address + 1)) << 8));
    }
    return rd16_long(P, address);
}
static void store(w65c816 *P, uint32_t address, uint16_t value, bool wide) {
    wr(P, address, (uint8_t)(value));
    if (!wide) return;
    if (P->bank0_mode_) {
        wr(P, (address & 0xff0000) | (uint16_t)(address + 1), (uint8_t)(value >> 8));
    } else {
        wr(P, address + 1, (uint8_t)(value >> 8));
    }
}

// ---------------------------------------------------------------------------
// ALU
// ---------------------------------------------------------------------------
static void op_adc(w65c816 *P, uint16_t data) {
    int c = P->p.c ? 1 : 0;
    if (P->p.m) {
        int al = P->a & 0xff;
        int result;
        if (!P->p.d) {
            result = al + data + c;
        } else {
            result = (al & 0x0f) + (data & 0x0f) + c;
            if (result > 0x09) result += 0x06;
            c = result > 0x0f;
            result = (al & 0xf0) + (data & 0xf0) + (c << 4) + (result & 0x0f);
        }
        P->p.v = (~(al ^ data) & (al ^ result) & 0x80) != 0;
        if (P->p.d && result > 0x9f) result += 0x60;
        P->p.c = result > 0xff;
        set_nz8(P, (uint8_t)(result));
        P->a = (uint16_t)((P->a & 0xff00) | (uint8_t)(result));
    } else {
        int aw = P->a;
        int result;
        if (!P->p.d) {
            result = aw + data + c;
        } else {
            result = (aw & 0x000f) + (data & 0x000f) + c;
            if (result > 0x0009) result += 0x0006;
            c = result > 0x000f;
            result = (aw & 0x00f0) + (data & 0x00f0) + (c << 4) + (result & 0x000f);
            if (result > 0x009f) result += 0x0060;
            c = result > 0x00ff;
            result = (aw & 0x0f00) + (data & 0x0f00) + (c << 8) + (result & 0x00ff);
            if (result > 0x09ff) result += 0x0600;
            c = result > 0x0fff;
            result = (aw & 0xf000) + (data & 0xf000) + (c << 12) + (result & 0x0fff);
        }
        P->p.v = (~(aw ^ data) & (aw ^ result) & 0x8000) != 0;
        if (P->p.d && result > 0x9fff) result += 0x6000;
        P->p.c = result > 0xffff;
        set_nz16(P, (uint16_t)(result));
        P->a = (uint16_t)(result);
    }
}
static void op_sbc(w65c816 *P, uint16_t data) {
    int c = P->p.c ? 1 : 0;
    if (P->p.m) {
        data = (uint16_t)(data ^ 0xff);
        int al = P->a & 0xff;
        int result;
        if (!P->p.d) {
            result = al + data + c;
        } else {
            result = (al & 0x0f) + (data & 0x0f) + c;
            if (result <= 0x0f) result -= 0x06;
            c = result > 0x0f;
            result = (al & 0xf0) + (data & 0xf0) + (c << 4) + (result & 0x0f);
        }
        P->p.v = (~(al ^ data) & (al ^ result) & 0x80) != 0;
        if (P->p.d && result <= 0xff) result -= 0x60;
        P->p.c = result > 0xff;
        set_nz8(P, (uint8_t)(result));
        P->a = (uint16_t)((P->a & 0xff00) | (uint8_t)(result));
    } else {
        data = (uint16_t)(data ^ 0xffff);
        int aw = P->a;
        int result;
        if (!P->p.d) {
            result = aw + data + c;
        } else {
            result = (aw & 0x000f) + (data & 0x000f) + c;
            if (result <= 0x000f) result -= 0x0006;
            c = result > 0x000f;
            result = (aw & 0x00f0) + (data & 0x00f0) + (c << 4) + (result & 0x000f);
            if (result <= 0x00ff) result -= 0x0060;
            c = result > 0x00ff;
            result = (aw & 0x0f00) + (data & 0x0f00) + (c << 8) + (result & 0x00ff);
            if (result <= 0x0fff) result -= 0x0600;
            c = result > 0x0fff;
            result = (aw & 0xf000) + (data & 0xf000) + (c << 12) + (result & 0x0fff);
        }
        P->p.v = (~(aw ^ data) & (aw ^ result) & 0x8000) != 0;
        if (P->p.d && result <= 0xffff) result -= 0x6000;
        P->p.c = result > 0xffff;
        set_nz16(P, (uint16_t)(result));
        P->a = (uint16_t)(result);
    }
}
static void op_cmp(w65c816 *P, uint16_t reg, uint16_t value, bool wide) {
    if (wide) {
        int t = (int)(reg) - (int)(value);
        P->p.c = t >= 0;
        set_nz16(P, (uint16_t)(t));
    } else {
        int t = (int)(reg & 0xff) - (int)(value & 0xff);
        P->p.c = t >= 0;
        set_nz8(P, (uint8_t)(t));
    }
}
static void branch(w65c816 *P, bool condition) {
    int8_t off = (int8_t)(fetch8(P));
    P->cycles_ += 2;
    if (condition) {
        uint16_t target = (uint16_t)(P->pc_ + off);
        P->cycles_ += 1;
        if (P->e_ && ((target ^ P->pc_) & 0xff00)) P->cycles_ += 1;
        P->pc_ = target;
    }
}

// op: 0=ASL 1=ROL 2=LSR 3=ROR 4=INC 5=DEC 6=TSB 7=TRB
static void rmw(w65c816 *P, uint32_t address, int op) {
    const bool wide = !P->p.m;
    uint16_t v = load(P, address, wide);
    const uint16_t msb = wide ? 0x8000 : 0x80;
    const uint16_t mask = wide ? 0xffff : 0xff;
    switch (op) {
        case 0: P->p.c = (v & msb) != 0; v = (uint16_t)((v << 1) & mask); break;
        case 1: { bool c = (v & msb) != 0; v = (uint16_t)(((v << 1) | (P->p.c ? 1 : 0)) & mask); P->p.c = c; break; }
        case 2: P->p.c = (v & 1) != 0; v = (uint16_t)(v >> 1); break;
        case 3: { bool c = (v & 1) != 0; v = (uint16_t)((v >> 1) | (P->p.c ? msb : 0)); P->p.c = c; break; }
        case 4: v = (uint16_t)((v + 1) & mask); break;
        case 5: v = (uint16_t)((v - 1) & mask); break;
        case 6: P->p.z = (v & P->a & mask) == 0; v = (uint16_t)((v | P->a) & mask); break;
        case 7: P->p.z = (v & P->a & mask) == 0; v = (uint16_t)(v & ~P->a & mask); break;
    }
    if (op < 6) {
        if (wide) set_nz16(P, v); else set_nz8(P, (uint8_t)(v));
    }
    if (wide) P->cycles_ += 2;
    store(P, address, v, wide);
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------
int w65c816_run(w65c816 *P, int cycles) {
    int executed = 0;
    P->end_slice_ = false;
    while (executed < cycles && !P->end_slice_) {
        executed += w65c816_step(P);
    }
    return executed;
}
int w65c816_step(w65c816 *P) {
    P->cycles_ = 0;

    bool nmi = false;
    if (P->nmi_request_ != IRQ_CLEAR && !P->nmi_latched_) {
        nmi = true;
        P->nmi_latched_ = true;
        if (P->nmi_request_ == IRQ_PULSE) {
            P->nmi_request_ = IRQ_CLEAR;
            P->nmi_latched_ = false;
        }
    }
    const bool irq = P->irq_request_ != IRQ_CLEAR;

    if (P->waiting_ && (irq || nmi)) P->waiting_ = false;

    if (nmi) {
        P->stopped_ = false;
        interrupt(P, 0xffea, 0xfffa, false);
    } else if (irq && !P->p.i && !P->stopped_) {
        interrupt(P, 0xffee, 0xfffe, false);
        if (P->irq_request_ == IRQ_PULSE) P->irq_request_ = IRQ_CLEAR;
    }
    if (P->cycles_ != 0) {
        if (P->cycles) P->cycles(P->user, P->cycles_);
        return P->cycles_;
    }

    if (P->stopped_ || P->waiting_) {
        P->cycles_ = 2;
        if (P->cycles) P->cycles(P->user, P->cycles_);
        return P->cycles_;
    }

    
    const uint8_t op = fetch8(P);
    const bool m16 = !P->p.m;
    const bool x16 = !P->p.x;

    // --- Regular ALU group (ORA/AND/EOR/ADC/STA/LDA/CMP/SBC) ---
    if (((op & 0x01) && (op & 0x0f) != 0x0b) || (op & 0x1f) == 0x12) {
        if (op != 0x89) {
            Mode mode;
            int base;
            switch (op & 0x1f) {
                case 0x01: mode = MODE_DPINDX; base = 6; break;
                case 0x03: mode = MODE_SR; base = 4; break;
                case 0x05: mode = MODE_DP; base = 3; break;
                case 0x07: mode = MODE_DPINDLONG; base = 6; break;
                case 0x09: mode = MODE_IMM; base = 2; break;
                case 0x0d: mode = MODE_ABS; base = 4; break;
                case 0x0f: mode = MODE_LONG; base = 5; break;
                case 0x11: mode = MODE_DPINDY; base = 5; break;
                case 0x12: mode = MODE_DPIND; base = 5; break;
                case 0x13: mode = MODE_SRINDY; base = 7; break;
                case 0x15: mode = MODE_DPX; base = 4; break;
                case 0x17: mode = MODE_DPINDLONGY; base = 6; break;
                case 0x19: mode = MODE_ABSY; base = 4; break;
                case 0x1d: mode = MODE_ABSX; base = 4; break;
                case 0x1f: mode = MODE_LONGX; base = 5; break;
                default: mode = MODE_ABS; base = 4; break;
            }
            const int group = op >> 5;
            const bool is_store = group == 4;
            uint32_t address = ea(P, mode, m16, is_store);
            P->cycles_ += base + (m16 ? 1 : 0);
            switch (group) {
                case 0: { uint16_t v = load(P, address, m16); if (m16) { P->a |= v; set_nz16(P, P->a); } else { P->a = (uint16_t)(P->a | v); set_nz8(P, (uint8_t)(P->a)); } break; }
                case 1: { uint16_t v = load(P, address, m16); if (m16) { P->a &= v; set_nz16(P, P->a); } else { P->a = (uint16_t)(P->a & (0xff00 | v)); set_nz8(P, (uint8_t)(P->a)); } break; }
                case 2: { uint16_t v = load(P, address, m16); P->a = (uint16_t)(P->a ^ v); if (m16) set_nz16(P, P->a); else set_nz8(P, (uint8_t)(P->a)); break; }
                case 3: op_adc(P, load(P, address, m16)); break;
                case 4: store(P, address, P->a, m16); break;
                case 5: { uint16_t v = load(P, address, m16); if (m16) { P->a = v; set_nz16(P, P->a); } else { P->a = (uint16_t)((P->a & 0xff00) | v); set_nz8(P, (uint8_t)(v)); } break; }
                case 6: op_cmp(P, P->a, load(P, address, m16), m16); break;
                case 7: op_sbc(P, load(P, address, m16)); break;
            }
            if (P->cycles) P->cycles(P->user, P->cycles_);
            return P->cycles_;
        }
    }

#define LDX(mode, reg, base) do { uint32_t address_ = ea(P, mode, x16, false); (reg) = load(P, address_, x16); if (x16) set_nz16(P, reg); else set_nz8(P, (uint8_t)(reg)); P->cycles_ += (base) + (x16 ? 1 : 0); } while (0)
#define STX(mode, reg, base) do { uint32_t address_ = ea(P, mode, x16, true); store(P, address_, reg, x16); P->cycles_ += (base) + (x16 ? 1 : 0); } while (0)
#define CPX(mode, reg, base) do { uint32_t address_ = ea(P, mode, x16, false); op_cmp(P, reg, load(P, address_, x16), x16); P->cycles_ += (base) + (x16 ? 1 : 0); } while (0)
#define STZ(mode, base) do { uint32_t address_ = ea(P, mode, m16, true); store(P, address_, 0, m16); P->cycles_ += (base) + (m16 ? 1 : 0); } while (0)
#define BIT(mode, base) do { const int mode_ = (mode); uint32_t address_ = ea(P, mode_, m16, false); uint16_t v_ = load(P, address_, m16); P->cycles_ += (base) + (m16 ? 1 : 0); \
    if (m16) { P->p.z = (v_ & P->a) == 0; if (mode_ != MODE_IMM) { P->p.n = (v_ & 0x8000) != 0; P->p.v = (v_ & 0x4000) != 0; } } \
    else { P->p.z = (v_ & P->a & 0xff) == 0; if (mode_ != MODE_IMM) { P->p.n = (v_ & 0x80) != 0; P->p.v = (v_ & 0x40) != 0; } } } while (0)
#define RMW_OP(mode, which, base) do { uint32_t address_ = ea(P, mode, m16, true); P->cycles_ += (base); rmw(P, address_, which); } while (0)
#define SET_X_NZ(v) do { if (x16) set_nz16(P, v); else set_nz8(P, (uint8_t)(v)); } while (0)
#define SET_A_NZ() do { if (m16) set_nz16(P, P->a); else set_nz8(P, (uint8_t)(P->a)); } while (0)

    switch (op) {
        // --- Interrupts / control ---
        case 0x00: fetch8(P); interrupt(P, 0xffe6, 0xfffe, true); break;  // BRK
        case 0x02: fetch8(P); interrupt(P, 0xffe4, 0xfff4, true); break;  // COP
        case 0xdb: P->stopped_ = true; P->cycles_ = 3; break;                // STP
        case 0xcb: P->waiting_ = true; P->cycles_ = 3; break;                // WAI
        case 0xea: P->cycles_ = 2; break;                                 // NOP
        case 0x42: {                                                   // WDM
            uint8_t sig = fetch8(P);
            P->cycles_ = 2;
            if (P->wdm) P->wdm(P->user, sig);
            break;
        }

        case 0x18: P->p.c = false; P->cycles_ = 2; break;
        case 0x38: P->p.c = true; P->cycles_ = 2; break;
        case 0x58: P->p.i = false; P->cycles_ = 2; break;
        case 0x78: P->p.i = true; P->cycles_ = 2; break;
        case 0xb8: P->p.v = false; P->cycles_ = 2; break;
        case 0xd8: P->p.d = false; P->cycles_ = 2; break;
        case 0xf8: P->p.d = true; P->cycles_ = 2; break;

        case 0xfb: {  // XCE
            bool old_e = P->e_;
            w65c816_set_emulation(P, P->p.c);
            P->p.c = old_e;
            P->cycles_ = 2;
            break;
        }
        case 0xc2: w65c816_set_p(P, (uint8_t)(w65c816_get_p(P) & ~fetch8(P))); P->cycles_ = 3; break;  // REP
        case 0xe2: w65c816_set_p(P, (uint8_t)(w65c816_get_p(P) | fetch8(P))); P->cycles_ = 3; break;   // SEP

        // --- Stack ---
        case 0x08: push8(P, w65c816_get_p(P)); P->cycles_ = 3; break;          // PHP
        case 0x28: w65c816_set_p(P, pull8(P)); P->cycles_ = 4; break;          // PLP
        case 0x48:                                              // PHA
            if (m16) push8(P, (uint8_t)(P->a >> 8));
            push8(P, (uint8_t)(P->a));
            P->cycles_ = m16 ? 4 : 3;
            break;
        case 0x68:                                              // PLA
            if (m16) { uint8_t lo = pull8(P); P->a = (uint16_t)(lo | (pull8(P) << 8)); }
            else P->a = (uint16_t)((P->a & 0xff00) | pull8(P));
            SET_A_NZ();
            P->cycles_ = m16 ? 5 : 4;
            break;
        case 0xda:                                              // PHX
            if (x16) push8(P, (uint8_t)(P->x >> 8));
            push8(P, (uint8_t)(P->x));
            P->cycles_ = x16 ? 4 : 3;
            break;
        case 0xfa:                                              // PLX
            if (x16) { uint8_t lo = pull8(P); P->x = (uint16_t)(lo | (pull8(P) << 8)); }
            else P->x = pull8(P);
            SET_X_NZ(P->x);
            P->cycles_ = x16 ? 5 : 4;
            break;
        case 0x5a:                                              // PHY
            if (x16) push8(P, (uint8_t)(P->y >> 8));
            push8(P, (uint8_t)(P->y));
            P->cycles_ = x16 ? 4 : 3;
            break;
        case 0x7a:                                              // PLY
            if (x16) { uint8_t lo = pull8(P); P->y = (uint16_t)(lo | (pull8(P) << 8)); }
            else P->y = pull8(P);
            SET_X_NZ(P->y);
            P->cycles_ = x16 ? 5 : 4;
            break;
        case 0x8b: push8(P, P->dbr); P->cycles_ = 3; break;                              // PHB
        case 0x4b: push8(P, P->pbr); P->cycles_ = 3; break;                              // PHK
        case 0xab: P->dbr = pull8n(P); fix_sp(P); set_nz8(P, P->dbr); P->cycles_ = 4; break;  // PLB
        case 0x0b: push8n(P, (uint8_t)(P->d >> 8)); push8n(P, (uint8_t)(P->d)); fix_sp(P); P->cycles_ = 4; break;  // PHD
        case 0x2b: {                                                                              // PLD
            uint8_t lo = pull8n(P);
            P->d = (uint16_t)(lo | (pull8n(P) << 8));
            fix_sp(P);
            set_nz16(P, P->d);
            P->cycles_ = 5;
            break;
        }
        case 0xf4: {  // PEA
            uint16_t v = fetch16(P);
            push8n(P, (uint8_t)(v >> 8));
            push8n(P, (uint8_t)(v));
            fix_sp(P);
            P->cycles_ = 5;
            break;
        }
        case 0xd4: {  // PEI
            uint8_t off = fetch8(P);
            if (P->d & 0xff) P->cycles_++;
            uint16_t v = rd16_bank0(P, (uint16_t)(P->d + off));
            push8n(P, (uint8_t)(v >> 8));
            push8n(P, (uint8_t)(v));
            fix_sp(P);
            P->cycles_ += 6;
            break;
        }
        case 0x62: {  // PER
            uint16_t off = fetch16(P);
            uint16_t v = (uint16_t)(P->pc_ + off);
            push8n(P, (uint8_t)(v >> 8));
            push8n(P, (uint8_t)(v));
            fix_sp(P);
            P->cycles_ = 6;
            break;
        }

        // --- Transfers ---
        case 0x1b: P->sp = P->e_ ? (uint16_t)(0x100 | (P->a & 0xff)) : P->a; P->cycles_ = 2; break;  // TCS
        case 0x3b: P->a = P->sp; set_nz16(P, P->a); P->cycles_ = 2; break;                          // TSC
        case 0x5b: P->d = P->a; set_nz16(P, P->d); P->cycles_ = 2; break;                           // TCD
        case 0x7b: P->a = P->d; set_nz16(P, P->a); P->cycles_ = 2; break;                           // TDC
        case 0xeb: P->a = (uint16_t)((P->a << 8) | (P->a >> 8)); set_nz8(P, (uint8_t)(P->a)); P->cycles_ = 3; break;  // XBA
        case 0xaa: P->x = x16 ? P->a : (uint16_t)(P->a & 0xff); SET_X_NZ(P->x); P->cycles_ = 2; break;  // TAX
        case 0xa8: P->y = x16 ? P->a : (uint16_t)(P->a & 0xff); SET_X_NZ(P->y); P->cycles_ = 2; break;  // TAY
        case 0x8a: P->a = m16 ? P->x : (uint16_t)((P->a & 0xff00) | (P->x & 0xff)); SET_A_NZ(); P->cycles_ = 2; break;  // TXA
        case 0x98: P->a = m16 ? P->y : (uint16_t)((P->a & 0xff00) | (P->y & 0xff)); SET_A_NZ(); P->cycles_ = 2; break;  // TYA
        case 0xba: P->x = x16 ? P->sp : (uint16_t)(P->sp & 0xff); SET_X_NZ(P->x); P->cycles_ = 2; break;  // TSX
        case 0x9a: P->sp = P->e_ ? (uint16_t)(0x100 | (P->x & 0xff)) : P->x; P->cycles_ = 2; break;       // TXS
        case 0x9b: P->y = P->x; SET_X_NZ(P->y); P->cycles_ = 2; break;                                // TXY
        case 0xbb: P->x = P->y; SET_X_NZ(P->x); P->cycles_ = 2; break;                                // TYX

        // --- Register inc/dec ---
        case 0xc8: P->y = x16 ? (uint16_t)(P->y + 1) : (uint16_t)((P->y + 1) & 0xff); SET_X_NZ(P->y); P->cycles_ = 2; break;
        case 0xe8: P->x = x16 ? (uint16_t)(P->x + 1) : (uint16_t)((P->x + 1) & 0xff); SET_X_NZ(P->x); P->cycles_ = 2; break;
        case 0x88: P->y = x16 ? (uint16_t)(P->y - 1) : (uint16_t)((P->y - 1) & 0xff); SET_X_NZ(P->y); P->cycles_ = 2; break;
        case 0xca: P->x = x16 ? (uint16_t)(P->x - 1) : (uint16_t)((P->x - 1) & 0xff); SET_X_NZ(P->x); P->cycles_ = 2; break;
        case 0x1a:
            P->a = m16 ? (uint16_t)(P->a + 1) : (uint16_t)((P->a & 0xff00) | (uint8_t)(P->a + 1));
            SET_A_NZ();
            P->cycles_ = 2;
            break;
        case 0x3a:
            P->a = m16 ? (uint16_t)(P->a - 1) : (uint16_t)((P->a & 0xff00) | (uint8_t)(P->a - 1));
            SET_A_NZ();
            P->cycles_ = 2;
            break;

        // --- Accumulator shifts ---
        case 0x0a:
            if (m16) { P->p.c = (P->a & 0x8000) != 0; P->a = (uint16_t)(P->a << 1); }
            else { P->p.c = (P->a & 0x80) != 0; P->a = (uint16_t)((P->a & 0xff00) | (uint8_t)(P->a << 1)); }
            SET_A_NZ(); P->cycles_ = 2; break;
        case 0x4a:
            if (m16) { P->p.c = (P->a & 1) != 0; P->a = (uint16_t)(P->a >> 1); }
            else { P->p.c = (P->a & 1) != 0; P->a = (uint16_t)((P->a & 0xff00) | ((P->a & 0xff) >> 1)); }
            SET_A_NZ(); P->cycles_ = 2; break;
        case 0x2a: {
            bool c = P->p.c;
            if (m16) { P->p.c = (P->a & 0x8000) != 0; P->a = (uint16_t)((P->a << 1) | (c ? 1 : 0)); }
            else { P->p.c = (P->a & 0x80) != 0; P->a = (uint16_t)((P->a & 0xff00) | (uint8_t)((P->a << 1) | (c ? 1 : 0))); }
            SET_A_NZ(); P->cycles_ = 2; break;
        }
        case 0x6a: {
            bool c = P->p.c;
            if (m16) { P->p.c = (P->a & 1) != 0; P->a = (uint16_t)((P->a >> 1) | (c ? 0x8000 : 0)); }
            else { P->p.c = (P->a & 1) != 0; P->a = (uint16_t)((P->a & 0xff00) | (((P->a & 0xff) >> 1) | (c ? 0x80 : 0))); }
            SET_A_NZ(); P->cycles_ = 2; break;
        }

        // --- Branches ---
        case 0x10: branch(P, !P->p.n); break;
        case 0x30: branch(P, P->p.n); break;
        case 0x50: branch(P, !P->p.v); break;
        case 0x70: branch(P, P->p.v); break;
        case 0x90: branch(P, !P->p.c); break;
        case 0xb0: branch(P, P->p.c); break;
        case 0xd0: branch(P, !P->p.z); break;
        case 0xf0: branch(P, P->p.z); break;
        case 0x80: branch(P, true); break;  // BRA
        case 0x82: {                     // BRL
            uint16_t off = fetch16(P);
            P->pc_ = (uint16_t)(P->pc_ + off);
            P->cycles_ = 4;
            break;
        }

        // --- Jumps / calls ---
        case 0x4c: P->pc_ = fetch16(P); P->cycles_ = 3; break;  // JMP abs
        case 0x6c: {                                     // JMP (abs)
            uint16_t ptr = fetch16(P);
            P->pc_ = rd16_bank0(P, ptr);
            P->cycles_ = 5;
            break;
        }
        case 0x7c: {  // JMP (abs,X)
            uint16_t ptr = (uint16_t)(fetch16(P) + P->x);
            P->pc_ = (uint16_t)(rd(P, ((uint32_t)(P->pbr) << 16) | ptr) | (rd(P, ((uint32_t)(P->pbr) << 16) | (uint16_t)(ptr + 1)) << 8));
            P->cycles_ = 6;
            break;
        }
        case 0xdc: {  // JML [abs]
            uint16_t ptr = fetch16(P);
            uint16_t target = rd16_bank0(P, ptr);
            P->pbr = rd(P, (uint16_t)(ptr + 2));
            P->pc_ = target;
            P->cycles_ = 6;
            break;
        }
        case 0x5c: {  // JML long
            uint32_t target = fetch24(P);
            P->pc_ = (uint16_t)(target);
            P->pbr = (uint8_t)(target >> 16);
            P->cycles_ = 4;
            break;
        }
        case 0x20: {  // JSR abs
            uint16_t target = fetch16(P);
            uint16_t ret = (uint16_t)(P->pc_ - 1);
            push8(P, (uint8_t)(ret >> 8));
            push8(P, (uint8_t)(ret));
            P->pc_ = target;
            P->cycles_ = 6;
            break;
        }
        case 0xfc: {  // JSR (abs,X)
            uint8_t lo = fetch8(P);
            uint16_t ret = P->pc_;  // points at the operand's high byte
            push8(P, (uint8_t)(ret >> 8));
            push8(P, (uint8_t)(ret));
            uint8_t hi = fetch8(P);
            uint16_t ptr = (uint16_t)((lo | (hi << 8)) + P->x);
            P->pc_ = (uint16_t)(rd(P, ((uint32_t)(P->pbr) << 16) | ptr) | (rd(P, ((uint32_t)(P->pbr) << 16) | (uint16_t)(ptr + 1)) << 8));
            fix_sp(P);
            P->cycles_ = 8;
            break;
        }
        case 0x22: {  // JSL long
            uint16_t lo = fetch16(P);
            push8n(P, P->pbr);
            uint8_t bank = fetch8(P);
            uint16_t ret = (uint16_t)(P->pc_ - 1);
            push8n(P, (uint8_t)(ret >> 8));
            push8n(P, (uint8_t)(ret));
            fix_sp(P);
            P->pbr = bank;
            P->pc_ = lo;
            P->cycles_ = 8;
            break;
        }
        case 0x60: {  // RTS
            uint8_t lo = pull8(P);
            P->pc_ = (uint16_t)((lo | (pull8(P) << 8)) + 1);
            P->cycles_ = 6;
            break;
        }
        case 0x6b: {  // RTL
            uint8_t lo = pull8n(P);
            uint8_t hi = pull8n(P);
            P->pbr = pull8n(P);
            fix_sp(P);
            P->pc_ = (uint16_t)((lo | (hi << 8)) + 1);
            P->cycles_ = 6;
            break;
        }
        case 0x40: {  // RTI
            w65c816_set_p(P, pull8(P));
            uint8_t lo = pull8(P);
            P->pc_ = (uint16_t)(lo | (pull8(P) << 8));
            if (!P->e_) P->pbr = pull8(P);
            P->cycles_ = P->e_ ? 6 : 7;
            break;
        }

        // --- Block moves ---
        case 0x54:
        case 0x44: {
            uint8_t dst_bank = fetch8(P), src_bank = fetch8(P);
            P->dbr = dst_bank;
            wr(P, ((uint32_t)(dst_bank) << 16) | P->y, rd(P, ((uint32_t)(src_bank) << 16) | P->x));
            int delta = op == 0x54 ? 1 : -1;
            if (x16) {
                P->x = (uint16_t)(P->x + delta);
                P->y = (uint16_t)(P->y + delta);
            } else {
                P->x = (uint16_t)((P->x + delta) & 0xff);
                P->y = (uint16_t)((P->y + delta) & 0xff);
            }
            P->a = (uint16_t)(P->a - 1);
            if (P->a != 0xffff) P->pc_ = (uint16_t)(P->pc_ - 3);
            P->cycles_ = 7;
            break;
        }

        // --- BIT / TSB / TRB / STZ ---
        case 0x24: BIT(MODE_DP, 3); break;
        case 0x2c: BIT(MODE_ABS, 4); break;
        case 0x34: BIT(MODE_DPX, 4); break;
        case 0x3c: BIT(MODE_ABSX, 4); break;
        case 0x89: BIT(MODE_IMM, 2); break;
        case 0x04: RMW_OP(MODE_DP, 6, 5); break;                       // TSB dp
        case 0x0c: RMW_OP(MODE_ABS, 6, 6); break;                      // TSB abs
        case 0x14: RMW_OP(MODE_DP, 7, 5); break;                       // TRB dp
        case 0x1c: RMW_OP(MODE_ABS, 7, 6); break;                      // TRB abs
        case 0x64: STZ(MODE_DP, 3); break;
        case 0x74: STZ(MODE_DPX, 4); break;
        case 0x9c: STZ(MODE_ABS, 4); break;
        case 0x9e: STZ(MODE_ABSX, 4); break;

        // --- Memory shifts / inc / dec ---
        case 0x06: RMW_OP(MODE_DP, 0, 5); break;
        case 0x0e: RMW_OP(MODE_ABS, 0, 6); break;
        case 0x16: RMW_OP(MODE_DPX, 0, 6); break;
        case 0x1e: RMW_OP(MODE_ABSX, 0, 6); break;
        case 0x26: RMW_OP(MODE_DP, 1, 5); break;
        case 0x2e: RMW_OP(MODE_ABS, 1, 6); break;
        case 0x36: RMW_OP(MODE_DPX, 1, 6); break;
        case 0x3e: RMW_OP(MODE_ABSX, 1, 6); break;
        case 0x46: RMW_OP(MODE_DP, 2, 5); break;
        case 0x4e: RMW_OP(MODE_ABS, 2, 6); break;
        case 0x56: RMW_OP(MODE_DPX, 2, 6); break;
        case 0x5e: RMW_OP(MODE_ABSX, 2, 6); break;
        case 0x66: RMW_OP(MODE_DP, 3, 5); break;
        case 0x6e: RMW_OP(MODE_ABS, 3, 6); break;
        case 0x76: RMW_OP(MODE_DPX, 3, 6); break;
        case 0x7e: RMW_OP(MODE_ABSX, 3, 6); break;
        case 0xe6: RMW_OP(MODE_DP, 4, 5); break;
        case 0xee: RMW_OP(MODE_ABS, 4, 6); break;
        case 0xf6: RMW_OP(MODE_DPX, 4, 6); break;
        case 0xfe: RMW_OP(MODE_ABSX, 4, 6); break;
        case 0xc6: RMW_OP(MODE_DP, 5, 5); break;
        case 0xce: RMW_OP(MODE_ABS, 5, 6); break;
        case 0xd6: RMW_OP(MODE_DPX, 5, 6); break;
        case 0xde: RMW_OP(MODE_ABSX, 5, 6); break;

        // --- X/Y loads, stores, compares ---
        case 0xa2: LDX(MODE_IMM, P->x, 2); break;
        case 0xa6: LDX(MODE_DP, P->x, 3); break;
        case 0xae: LDX(MODE_ABS, P->x, 4); break;
        case 0xb6: LDX(MODE_DPY, P->x, 4); break;
        case 0xbe: LDX(MODE_ABSY, P->x, 4); break;
        case 0xa0: LDX(MODE_IMM, P->y, 2); break;
        case 0xa4: LDX(MODE_DP, P->y, 3); break;
        case 0xac: LDX(MODE_ABS, P->y, 4); break;
        case 0xb4: LDX(MODE_DPX, P->y, 4); break;
        case 0xbc: LDX(MODE_ABSX, P->y, 4); break;
        case 0x86: STX(MODE_DP, P->x, 3); break;
        case 0x8e: STX(MODE_ABS, P->x, 4); break;
        case 0x96: STX(MODE_DPY, P->x, 4); break;
        case 0x84: STX(MODE_DP, P->y, 3); break;
        case 0x8c: STX(MODE_ABS, P->y, 4); break;
        case 0x94: STX(MODE_DPX, P->y, 4); break;
        case 0xc0: CPX(MODE_IMM, P->y, 2); break;
        case 0xc4: CPX(MODE_DP, P->y, 3); break;
        case 0xcc: CPX(MODE_ABS, P->y, 4); break;
        case 0xe0: CPX(MODE_IMM, P->x, 2); break;
        case 0xe4: CPX(MODE_DP, P->x, 3); break;
        case 0xec: CPX(MODE_ABS, P->x, 4); break;

        default:
            P->cycles_ = 2;
            break;
    }

    if (P->cycles) P->cycles(P->user, P->cycles_);
    return P->cycles_;
}

