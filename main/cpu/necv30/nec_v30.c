// nec_v30.c - NEC V20 / V30 / V33: a C11 port of dsp-cpp's cpu/nec_v30
// (from nec_v20_v30.pas, MAME's nec.cpp); see nec_v30.h.
#include <string.h>
#include "nec_v30.h"

#define kDs1 0
#define kPs 1
#define kSs 2
#define kDs0 3
#define kNmiIrq 1
#define kIntIrq 2
#define kNmiVector 2
#define kDivideVector 0
#define kChkindVector 5
#define kBrkvVector 4

static uint8_t kParity[256];

static inline uint8_t read_byte(nec_v30 *N, uint32_t a) { return N->read(N->user, a & 0xfffff); }
static inline void write_byte(nec_v30 *N, uint32_t a, uint8_t v) { N->write(N->user, a & 0xfffff, v); }
static uint32_t default_base(nec_v30 *N, int seg);
static uint16_t read_word(nec_v30 *N, uint32_t address);
static void write_word(nec_v30 *N, uint32_t address, uint16_t value);
static uint8_t get_breg(nec_v30 *N, int index);
static void set_breg(nec_v30 *N, int index, uint8_t value);
static uint16_t get_wreg(nec_v30 *N, int index);
static void set_wreg(nec_v30 *N, int index, uint16_t value);
static void execute_op(nec_v30 *N, uint8_t op);
static void i_insb(nec_v30 *N); static void i_insw(nec_v30 *N); static void i_outsb(nec_v30 *N); static void i_outsw(nec_v30 *N);
static void i_movsb(nec_v30 *N); static void i_movsw(nec_v30 *N); static void i_cmpsb(nec_v30 *N); static void i_cmpsw(nec_v30 *N);
static void i_stosb(nec_v30 *N); static void i_stosw(nec_v30 *N); static void i_lodsb(nec_v30 *N); static void i_lodsw(nec_v30 *N);
static void i_scasb(nec_v30 *N); static void i_scasw(nec_v30 *N);
static inline uint8_t get_mem_b(nec_v30 *N, int seg, uint16_t off) { return read_byte(N, default_base(N, seg) + off); }
static inline uint16_t get_mem_w(nec_v30 *N, int seg, uint16_t off) { return read_word(N, default_base(N, seg) + off); }
static inline void put_mem_b(nec_v30 *N, int seg, uint16_t off, uint8_t x) { write_byte(N, default_base(N, seg) + off, x); }
static inline void put_mem_w(nec_v30 *N, int seg, uint16_t off, uint16_t x) { write_word(N, default_base(N, seg) + off, x); }
static inline uint16_t get_next_rm_word(nec_v30 *N) { return read_word(N, N->ea_ + 2); }
static inline uint8_t reg_byte(nec_v30 *N, uint8_t m) { return get_breg(N, (m >> 3) & 7); }
static inline uint16_t reg_word(nec_v30 *N, uint8_t m) { return get_wreg(N, (m >> 3) & 7); }
static inline void set_reg_byte(nec_v30 *N, uint8_t m, uint8_t v) { set_breg(N, (m >> 3) & 7, v); }
static inline void set_reg_word(nec_v30 *N, uint8_t m, uint16_t v) { set_wreg(N, (m >> 3) & 7, v); }
static inline void clk(nec_v30 *N, int n) { N->icount_ += n; }
static inline bool cf(const nec_v30 *N) { return N->carry_val_ != 0; }
static inline bool pf(const nec_v30 *N) { return N->parity_val_ != 0; }
static inline bool af(const nec_v30 *N) { return N->aux_val_ != 0; }
static inline bool zf(const nec_v30 *N) { return N->zero_val_ == 0; }
static inline bool sf(const nec_v30 *N) { return N->sign_val_ < 0; }
static inline bool of(const nec_v30 *N) { return N->over_val_ != 0; }
static inline int dir(const nec_v30 *N) { return N->df_ ? -1 : 1; }

static bool string_once(nec_v30 *N, uint8_t opcode) {
    switch (opcode) {
        case 0x6c: i_insb(N); break;
        case 0x6d: i_insw(N); break;
        case 0x6e: i_outsb(N); break;
        case 0x6f: i_outsw(N); break;
        case 0xa4: i_movsb(N); break;
        case 0xa5: i_movsw(N); break;
        case 0xa6: i_cmpsb(N); break;
        case 0xa7: i_cmpsw(N); break;
        case 0xaa: i_stosb(N); break;
        case 0xab: i_stosw(N); break;
        case 0xac: i_lodsb(N); break;
        case 0xad: i_lodsw(N); break;
        case 0xae: i_scasb(N); break;
        case 0xaf: i_scasw(N); break;
        default: execute_op(N, opcode); return false;
    }
    return true;
}

void nec_v30_reset(nec_v30 *N) {
    N->aw_ = N->cw_ = N->dw_ = N->bw_ = 0;
    N->sp_ = N->bp_ = N->ix_ = N->iy_ = 0;
    N->ip_ = 0;
    N->tf_ = N->iff_ = N->df_ = false;
    N->mf_ = true;
    N->sign_val_ = 0;
    N->aux_val_ = 0;
    N->over_val_ = 0;
    N->zero_val_ = 1;
    N->carry_val_ = 0;
    N->parity_val_ = 1;
    N->irq_vector_ = 0xff;
    N->irq_pending_ = 0;
    N->nmi_state_ = 0;
    N->prefetch_reset_ = true;
    N->prefetch_count_ = 0;
    N->ps_ = 0xffff;
    N->ds1_ = N->ds0_ = N->ss_ = 0;
    N->ea_ = 0;
    N->eo_ = 0;
    N->seg_prefix_ = false;
    N->no_interrupt_ = false;
    N->halted_ = false;
}

static uint16_t read_word(nec_v30 *N, uint32_t address) {
    uint8_t lo = read_byte(N, address);
    return (uint16_t)(lo | ((uint16_t)(read_byte(N, address + 1)) << 8));
}

static void write_word(nec_v30 *N, uint32_t address, uint16_t value) {
    write_byte(N, address, (uint8_t)(value));
    write_byte(N, address + 1, (uint8_t)(value >> 8));
}

static uint8_t in_byte(nec_v30 *N, uint16_t port) {
    if (N->in8) return N->in8(N->user, port);
    if (N->in16) {
        uint16_t word = N->in16(N->user, port & 0xfffe);
        return (port & 1) ? (uint8_t)(word >> 8) : (uint8_t)(word);
    }
    return 0xff;
}

static void out_byte(nec_v30 *N, uint16_t port, uint8_t value) {
    if (N->out8) {
        N->out8(N->user, port, value);
        return;
    }
    if (N->out16) {
        uint16_t word = N->in16 ? N->in16(N->user, port & 0xfffe) : 0;
        if (port & 1) word = (uint16_t)((word & 0x00ff) | ((uint16_t)(value) << 8));
        else word = (uint16_t)((word & 0xff00) | value);
        N->out16(N->user, port & 0xfffe, word);
    }
}

static uint16_t in_word(nec_v30 *N, uint16_t port) {
    if (N->in16) return N->in16(N->user, port);
    return (uint16_t)(in_byte(N, port) | ((uint16_t)(in_byte(N, (uint16_t)(port + 1))) << 8));
}

static void out_word(nec_v30 *N, uint16_t port, uint16_t value) {
    if (N->out16) {
        N->out16(N->user, port, value);
        return;
    }
    out_byte(N, port, (uint8_t)(value));
    out_byte(N, (uint16_t)(port + 1), (uint8_t)(value >> 8));
}

static uint8_t fetch(nec_v30 *N) {
    N->prefetch_count_--;
    uint8_t value = read_byte(N, ((uint32_t)(N->ps_) << 4) + N->ip_);
    N->ip_++;
    return value;
}

static uint16_t fetch_word(nec_v30 *N) { return (uint16_t)(fetch(N) | ((uint16_t)(fetch(N)) << 8)); }

static uint32_t default_base(nec_v30 *N, int seg) {
    if (N->seg_prefix_ && (seg == kDs0 || seg == kSs)) return N->prefix_base_;
    switch (seg) {
        case kDs1: return (uint32_t)(N->ds1_) << 4;
        case kPs: return (uint32_t)(N->ps_) << 4;
        case kSs: return (uint32_t)(N->ss_) << 4;
        default: return (uint32_t)(N->ds0_) << 4;
    }
}

static void get_ea(nec_v30 *N, uint8_t modrm) {
    uint8_t rm = modrm & 7;
    uint8_t mod = (uint8_t)(modrm >> 6);
    uint16_t disp = 0;
    if (mod == 1) {
        disp = (uint16_t)((int16_t)((int8_t)(fetch(N))));
    } else if (mod == 2) {
        disp = fetch_word(N);
    } else if (mod == 0 && rm == 6) {
        N->eo_ = fetch_word(N);
        N->ea_ = default_base(N, kDs0) + N->eo_;
        return;
    }

    uint16_t base = 0;
    bool use_ss = false;
    switch (rm) {
        case 0: base = (uint16_t)(N->bw_ + N->ix_); break;
        case 1: base = (uint16_t)(N->bw_ + N->iy_); break;
        case 2:
            base = (uint16_t)(N->bp_ + N->ix_);
            use_ss = true;
            break;
        case 3:
            base = (uint16_t)(N->bp_ + N->iy_);
            use_ss = true;
            break;
        case 4: base = N->ix_; break;
        case 5: base = N->iy_; break;
        case 6:
            base = N->bp_;
            use_ss = true;
            break;
        case 7: base = N->bw_; break;
    }
    N->eo_ = (uint16_t)(base + disp);
    N->ea_ = default_base(N, use_ss ? kSs : kDs0) + N->eo_;
}

static uint8_t get_breg(nec_v30 *N, int index) {
    const uint16_t *words[4] = {&N->aw_, &N->cw_, &N->dw_, &N->bw_};
    const uint16_t w = *words[index & 3];
    return (index & 4) ? (uint8_t)(w >> 8) : (uint8_t)(w);
}

static void set_breg(nec_v30 *N, int index, uint8_t value) {
    uint16_t *words[4] = {&N->aw_, &N->cw_, &N->dw_, &N->bw_};
    uint16_t *w = words[index & 3];
    if (index & 4) *w = (uint16_t)((*w & 0x00ff) | ((uint16_t)(value) << 8));
    else *w = (uint16_t)((*w & 0xff00) | value);
}

static uint16_t get_wreg(nec_v30 *N, int index) {
    const uint16_t* words[8] = {&N->aw_, &N->cw_, &N->dw_, &N->bw_, &N->sp_, &N->bp_, &N->ix_, &N->iy_};
    return *words[index & 7];
}

static void set_wreg(nec_v30 *N, int index, uint16_t value) {
    uint16_t* words[8] = {&N->aw_, &N->cw_, &N->dw_, &N->bw_, &N->sp_, &N->bp_, &N->ix_, &N->iy_};
    *words[index & 7] = value;
}

static uint8_t get_rm_byte(nec_v30 *N, uint8_t modrm) {
    if (modrm >= 0xc0) return get_breg(N, modrm & 7);
    get_ea(N, modrm);
    return read_byte(N, N->ea_);
}

static uint16_t get_rm_word(nec_v30 *N, uint8_t modrm) {
    if (modrm >= 0xc0) return get_wreg(N, modrm & 7);
    get_ea(N, modrm);
    return read_word(N, N->ea_);
}

static void put_rm_byte(nec_v30 *N, uint8_t modrm, uint8_t value) {
    if (modrm >= 0xc0) set_breg(N, modrm & 7, value);
    else {
        get_ea(N, modrm);
        write_byte(N, N->ea_, value);
    }
}

static void put_rm_word(nec_v30 *N, uint8_t modrm, uint16_t value) {
    if (modrm >= 0xc0) set_wreg(N, modrm & 7, value);
    else {
        get_ea(N, modrm);
        write_word(N, N->ea_, value);
    }
}

static void putback_rm_byte(nec_v30 *N, uint8_t modrm, uint8_t value) {
    if (modrm >= 0xc0) set_breg(N, modrm & 7, value);
    else write_byte(N, N->ea_, value);
}

static void putback_rm_word(nec_v30 *N, uint8_t modrm, uint16_t value) {
    if (modrm >= 0xc0) set_wreg(N, modrm & 7, value);
    else write_word(N, N->ea_, value);
}

static void put_imm_rm_byte(nec_v30 *N, uint8_t modrm) {
    if (modrm >= 0xc0) set_breg(N, modrm & 7, fetch(N));
    else {
        get_ea(N, modrm);
        write_byte(N, N->ea_, fetch(N));
    }
}

static void put_imm_rm_word(nec_v30 *N, uint8_t modrm) {
    if (modrm >= 0xc0) set_wreg(N, modrm & 7, fetch_word(N));
    else {
        get_ea(N, modrm);
        write_word(N, N->ea_, fetch_word(N));
    }
}

static void clks(nec_v30 *N, int v20, int v30, int v33) {
    clk(N, N->type == NEC_V20 ? v20 : (N->type == NEC_V33 ? v33 : v30));
}

static void clkw(nec_v30 *N, int v20o, int v30o, int v33o, int v20e, int v30e, int v33e, uint16_t addr) {
    bool odd = (addr & 1) != 0;
    if (N->type == NEC_V20) clk(N, odd ? v20o : v20e);
    else if (N->type == NEC_V33) clk(N, odd ? v33o : v33e);
    else clk(N, odd ? v30o : v30e);
}

static void clkm(nec_v30 *N, int v20, int v30, int v33, int v20m, int v30m, int v33m, uint8_t modrm) {
    if (modrm >= 0xc0) clks(N, v20, v30, v33);
    else clks(N, v20m, v30m, v33m);
}

static void clkr(nec_v30 *N, int v20o, int v30o, int v33o, int v20e, int v30e, int v33e, int vall,
                  uint8_t modrm) {
    if (modrm >= 0xc0) clk(N, vall);
    else clkw(N, v20o, v30o, v33o, v20e, v30e, v33e, (uint16_t)(N->ea_));
}

static void do_prefetch(nec_v30 *N, int previous_icount) {
    int diff = N->icount_ - previous_icount;
    while (N->prefetch_count_ < 0) {
        N->prefetch_count_++;
        if (diff > N->prefetch_cycles_) diff -= N->prefetch_cycles_;
        else N->icount_ -= N->prefetch_cycles_;
    }
    if (N->prefetch_reset_) {
        N->prefetch_count_ = 0;
        N->prefetch_reset_ = false;
        return;
    }
    while (diff >= N->prefetch_cycles_ && N->prefetch_count_ < N->prefetch_size_) {
        diff -= N->prefetch_cycles_;
        N->prefetch_count_++;
    }
}

static void set_szpf_byte(nec_v30 *N, uint32_t x) {
    uint8_t v = (uint8_t)(x);
    N->sign_val_ = (int8_t)(v);
    N->zero_val_ = v;
    N->parity_val_ = kParity[v];
}

static void set_szpf_word(nec_v30 *N, uint32_t x) {
    uint16_t v = (uint16_t)(x);
    N->sign_val_ = (int16_t)(v);
    N->zero_val_ = v;
    N->parity_val_ = kParity[v & 0xff];
}

static uint16_t compress_flags(nec_v30 *N) {
    uint16_t flags = 0x0002;
    if (cf(N)) flags |= 0x0001;
    if (pf(N)) flags |= 0x0004;
    if (af(N)) flags |= 0x0010;
    if (zf(N)) flags |= 0x0040;
    if (sf(N)) flags |= 0x0080;
    if (N->tf_) flags |= 0x0100;
    if (N->iff_) flags |= 0x0200;
    if (N->df_) flags |= 0x0400;
    if (of(N)) flags |= 0x0800;
    if (N->mf_) flags |= 0x8000;
    return flags;
}

static void expand_flags(nec_v30 *N, uint16_t value) {
    N->carry_val_ = value & 1;
    N->parity_val_ = (value & 4) ? 1 : 0;
    N->aux_val_ = value & 0x10;
    N->zero_val_ = (value & 0x40) ? 0 : 1;
    N->sign_val_ = (value & 0x80) ? -1 : 0;
    N->tf_ = (value & 0x100) != 0;
    N->iff_ = (value & 0x200) != 0;
    N->df_ = (value & 0x400) != 0;
    N->over_val_ = value & 0x800;
    N->mf_ = (value & 0x8000) != 0;
}

static uint8_t add_b(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint32_t res = dst + src;
    N->carry_val_ = res & 0x100;
    N->over_val_ = (res ^ src) & (res ^ dst) & 0x80;
    N->aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_byte(N, res);
    return (uint8_t)(res);
}

static uint16_t add_w(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint32_t res = dst + src;
    N->carry_val_ = res & 0x10000;
    N->over_val_ = (res ^ src) & (res ^ dst) & 0x8000;
    N->aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_word(N, res);
    return (uint16_t)(res);
}

static uint8_t sub_b(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint32_t res = dst - src;
    N->carry_val_ = res & 0x100;
    N->over_val_ = (dst ^ src) & (dst ^ res) & 0x80;
    N->aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_byte(N, res);
    return (uint8_t)(res);
}

static uint16_t sub_w(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint32_t res = dst - src;
    N->carry_val_ = res & 0x10000;
    N->over_val_ = (dst ^ src) & (dst ^ res) & 0x8000;
    N->aux_val_ = (res ^ (src ^ dst)) & 0x10;
    set_szpf_word(N, res);
    return (uint16_t)(res);
}

static uint8_t and_b(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint8_t res = (uint8_t)(dst & src);
    N->carry_val_ = N->over_val_ = N->aux_val_ = 0;
    set_szpf_byte(N, res);
    return res;
}

static uint16_t and_w(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint16_t res = (uint16_t)(dst & src);
    N->carry_val_ = N->over_val_ = N->aux_val_ = 0;
    set_szpf_word(N, res);
    return res;
}

static uint8_t or_b(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint8_t res = (uint8_t)(dst | src);
    N->carry_val_ = N->over_val_ = N->aux_val_ = 0;
    set_szpf_byte(N, res);
    return res;
}

static uint16_t or_w(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint16_t res = (uint16_t)(dst | src);
    N->carry_val_ = N->over_val_ = N->aux_val_ = 0;
    set_szpf_word(N, res);
    return res;
}

static uint8_t xor_b(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint8_t res = (uint8_t)(dst ^ src);
    N->carry_val_ = N->over_val_ = N->aux_val_ = 0;
    set_szpf_byte(N, res);
    return res;
}

static uint16_t xor_w(nec_v30 *N, uint32_t dst, uint32_t src) {
    uint16_t res = (uint16_t)(dst ^ src);
    N->carry_val_ = N->over_val_ = N->aux_val_ = 0;
    set_szpf_word(N, res);
    return res;
}

static uint8_t alu_b(nec_v30 *N, uint8_t op, uint32_t dst, uint32_t src) {
    switch (op) {
        case 0: return add_b(N, dst, src);
        case 1: return or_b(N, dst, src);
        case 2: return add_b(N, dst, src + (cf(N) ? 1 : 0));
        case 3: return sub_b(N, dst, src + (cf(N) ? 1 : 0));
        case 4: return and_b(N, dst, src);
        case 5: return sub_b(N, dst, src);
        case 6: return xor_b(N, dst, src);
        default: return sub_b(N, dst, src);  // CMP
    }
}

static uint16_t alu_w(nec_v30 *N, uint8_t op, uint32_t dst, uint32_t src) {
    switch (op) {
        case 0: return add_w(N, dst, src);
        case 1: return or_w(N, dst, src);
        case 2: return add_w(N, dst, src + (cf(N) ? 1 : 0));
        case 3: return sub_w(N, dst, src + (cf(N) ? 1 : 0));
        case 4: return and_w(N, dst, src);
        case 5: return sub_w(N, dst, src);
        case 6: return xor_w(N, dst, src);
        default: return sub_w(N, dst, src);
    }
}

static uint8_t rol_b(nec_v30 *N, uint8_t dst) {
    N->carry_val_ = dst & 0x80;
    return (uint8_t)((dst << 1) + (cf(N) ? 1 : 0));
}

static uint16_t rol_w(nec_v30 *N, uint16_t dst) {
    N->carry_val_ = dst & 0x8000;
    return (uint16_t)((dst << 1) + (cf(N) ? 1 : 0));
}

static uint8_t ror_b(nec_v30 *N, uint8_t dst) {
    N->carry_val_ = dst & 1;
    return (uint8_t)((dst >> 1) + (cf(N) ? 0x80 : 0));
}

static uint16_t ror_w(nec_v30 *N, uint16_t dst) {
    N->carry_val_ = dst & 1;
    return (uint16_t)((dst >> 1) + (cf(N) ? 0x8000 : 0));
}

static uint8_t rolc_b(nec_v30 *N, uint8_t dst) {
    uint32_t temp = ((uint32_t)(dst) << 1) + (cf(N) ? 1 : 0);
    N->carry_val_ = temp & 0x100;
    return (uint8_t)(temp);
}

static uint16_t rolc_w(nec_v30 *N, uint16_t dst) {
    uint32_t temp = ((uint32_t)(dst) << 1) + (cf(N) ? 1 : 0);
    N->carry_val_ = temp & 0x10000;
    return (uint16_t)(temp);
}

static uint8_t rorc_b(nec_v30 *N, uint8_t dst) {
    uint32_t temp = dst + (cf(N) ? 0x100u : 0);
    N->carry_val_ = temp & 1;
    return (uint8_t)(temp >> 1);
}

static uint16_t rorc_w(nec_v30 *N, uint16_t dst) {
    uint32_t temp = dst + (cf(N) ? 0x10000u : 0);
    N->carry_val_ = temp & 1;
    return (uint16_t)(temp >> 1);
}

static uint8_t shl_b(nec_v30 *N, uint8_t dst, uint8_t c) {
    clk(N, c);
    uint32_t res = (uint32_t)(dst) << c;
    N->carry_val_ = res & 0x100;
    set_szpf_byte(N, res);
    return (uint8_t)(res);
}

static uint16_t shl_w(nec_v30 *N, uint16_t dst, uint8_t c) {
    clk(N, c);
    uint32_t res = (uint32_t)(dst) << c;
    N->carry_val_ = res & 0x10000;
    set_szpf_word(N, res);
    return (uint16_t)(res);
}

static uint8_t shr_b(nec_v30 *N, uint8_t dst, uint8_t c) {
    clk(N, c);
    dst = (uint8_t)(dst >> (c - 1));
    N->carry_val_ = dst & 1;
    dst = (uint8_t)(dst >> 1);
    set_szpf_byte(N, dst);
    return dst;
}

static uint16_t shr_w(nec_v30 *N, uint16_t dst, uint8_t c) {
    clk(N, c);
    dst = (uint16_t)(dst >> (c - 1));
    N->carry_val_ = dst & 1;
    dst = (uint16_t)(dst >> 1);
    set_szpf_word(N, dst);
    return dst;
}

static uint8_t shra_b(nec_v30 *N, uint8_t dst, uint8_t c) {
    clk(N, c);
    int8_t temp = (int8_t)(dst);
    temp = (int8_t)(temp >> (c - 1));
    N->carry_val_ = temp & 1;
    temp = (int8_t)(temp >> 1);
    set_szpf_byte(N, (uint8_t)(temp));
    return (uint8_t)(temp);
}

static uint16_t shra_w(nec_v30 *N, uint16_t dst, uint8_t c) {
    clk(N, c);
    int16_t temp = (int16_t)(dst);
    temp = (int16_t)(temp >> (c - 1));
    N->carry_val_ = temp & 1;
    temp = (int16_t)(temp >> 1);
    set_szpf_word(N, (uint16_t)(temp));
    return (uint16_t)(temp);
}

static void push(nec_v30 *N, uint16_t value) {
    N->sp_ = (uint16_t)(N->sp_ - 2);
    write_word(N, ((uint32_t)(N->ss_) << 4) + N->sp_, value);
}

static uint16_t pop(nec_v30 *N) {
    uint16_t value = read_word(N, ((uint32_t)(N->ss_) << 4) + N->sp_);
    N->sp_ = (uint16_t)(N->sp_ + 2);
    return value;
}

static void i_pushf(nec_v30 *N) {
    push(N, compress_flags(N));
    clks(N, 12, 8, 3);
}

static void i_popf(nec_v30 *N) {
    expand_flags(N, pop(N));
    clks(N, 12, 8, 5);
}

static void nec_interrupt(nec_v30 *N, uint16_t vector) {
    i_pushf(N);
    N->tf_ = false;
    N->iff_ = false;
    N->mf_ = true;
    push(N, N->ps_);
    push(N, N->ip_);
    N->ip_ = read_word(N, (uint32_t)(vector) * 4);
    N->ps_ = read_word(N, (uint32_t)(vector) * 4 + 2);
    N->prefetch_reset_ = true;
    N->halted_ = false;
}

static void adj4(nec_v30 *N, int param1, int param2) {
    if (af(N) || ((N->aw_ & 0xf) > 9)) {
        uint16_t tmp = (uint16_t)((N->aw_ & 0xff) + param1);
        set_breg(N, 0, (uint8_t)(tmp));
        N->aux_val_ = 1;
        if (tmp & 0x100) N->carry_val_ = 1;
    }
    if (cf(N) || ((N->aw_ & 0xff) > 0x9f)) {
        set_breg(N, 0, (uint8_t)(get_breg(N, 0) + param2));
        N->carry_val_ = 1;
    }
    set_szpf_byte(N, get_breg(N, 0));
}

static void adjb(nec_v30 *N, int param1, int param2) {
    if (af(N) || ((N->aw_ & 0xf) > 9)) {
        set_breg(N, 0, (uint8_t)(get_breg(N, 0) + param1));
        set_breg(N, 4, (uint8_t)(get_breg(N, 4) + param2));
        N->aux_val_ = 1;
        N->carry_val_ = 1;
    } else {
        N->aux_val_ = 0;
        N->carry_val_ = 0;
    }
    set_breg(N, 0, (uint8_t)(N->aw_ & 0x0f));
}

static void i_movsb(nec_v30 *N) {
    put_mem_b(N, kDs1, N->iy_, get_mem_b(N, kDs0, N->ix_));
    N->iy_ = (uint16_t)(N->iy_ + dir(N));
    N->ix_ = (uint16_t)(N->ix_ + dir(N));
    clks(N, 8, 8, 6);
}

static void i_movsw(nec_v30 *N) {
    put_mem_w(N, kDs1, N->iy_, get_mem_w(N, kDs0, N->ix_));
    N->iy_ = (uint16_t)(N->iy_ + 2 * dir(N));
    N->ix_ = (uint16_t)(N->ix_ + 2 * dir(N));
    clks(N, 16, 16, 10);
}

static void i_cmpsb(nec_v30 *N) {
    uint32_t src = get_mem_b(N, kDs1, N->iy_);
    uint32_t dst = get_mem_b(N, kDs0, N->ix_);
    sub_b(N, dst, src);
    N->iy_ = (uint16_t)(N->iy_ + dir(N));
    N->ix_ = (uint16_t)(N->ix_ + dir(N));
    clks(N, 14, 14, 14);
}

static void i_cmpsw(nec_v30 *N) {
    uint32_t src = get_mem_w(N, kDs1, N->iy_);
    uint32_t dst = get_mem_w(N, kDs0, N->ix_);
    sub_w(N, dst, src);
    N->iy_ = (uint16_t)(N->iy_ + 2 * dir(N));
    N->ix_ = (uint16_t)(N->ix_ + 2 * dir(N));
    clks(N, 14, 14, 14);
}

static void i_stosb(nec_v30 *N) {
    put_mem_b(N, kDs1, N->iy_, get_breg(N, 0));
    N->iy_ = (uint16_t)(N->iy_ + dir(N));
    clks(N, 4, 4, 3);
}

static void i_stosw(nec_v30 *N) {
    put_mem_w(N, kDs1, N->iy_, N->aw_);
    N->iy_ = (uint16_t)(N->iy_ + 2 * dir(N));
    clkw(N, 8, 8, 5, 8, 4, 3, N->iy_);
}

static void i_lodsb(nec_v30 *N) {
    set_breg(N, 0, get_mem_b(N, kDs0, N->ix_));
    N->ix_ = (uint16_t)(N->ix_ + dir(N));
    clks(N, 4, 4, 3);
}

static void i_lodsw(nec_v30 *N) {
    N->aw_ = get_mem_w(N, kDs0, N->ix_);
    N->ix_ = (uint16_t)(N->ix_ + 2 * dir(N));
    clkw(N, 8, 8, 5, 8, 4, 3, N->ix_);
}

static void i_scasb(nec_v30 *N) {
    sub_b(N, get_breg(N, 0), get_mem_b(N, kDs1, N->iy_));
    N->iy_ = (uint16_t)(N->iy_ + dir(N));
    clks(N, 4, 4, 3);
}

static void i_scasw(nec_v30 *N) {
    sub_w(N, N->aw_, get_mem_w(N, kDs1, N->iy_));
    N->iy_ = (uint16_t)(N->iy_ + 2 * dir(N));
    clkw(N, 8, 8, 5, 8, 4, 3, N->iy_);
}

static void i_insb(nec_v30 *N) {
    put_mem_b(N, kDs1, N->iy_, in_byte(N, N->dw_));
    N->iy_ = (uint16_t)(N->iy_ + dir(N));
    clk(N, 8);
}

static void i_insw(nec_v30 *N) {
    put_mem_w(N, kDs1, N->iy_, in_word(N, N->dw_));
    N->iy_ = (uint16_t)(N->iy_ + 2 * dir(N));
    clks(N, 18, 10, 8);
}

static void i_outsb(nec_v30 *N) {
    out_byte(N, N->dw_, get_mem_b(N, kDs0, N->ix_));
    N->ix_ = (uint16_t)(N->ix_ + dir(N));
    clk(N, 8);
}

static void i_outsw(nec_v30 *N) {
    out_word(N, N->dw_, get_mem_w(N, kDs0, N->ix_));
    N->ix_ = (uint16_t)(N->ix_ + 2 * dir(N));
    clks(N, 18, 10, 8);
}

static bool i_jcc(nec_v30 *N, bool flag);
static void i_jmp(nec_v30 *N, bool flag) { i_jcc(N, flag); }

static bool i_jcc(nec_v30 *N, bool flag) {
    int8_t tmp = (int8_t)(fetch(N));
    if (flag) {
        N->ip_ = (uint16_t)(N->ip_ + tmp);
        N->prefetch_reset_ = true;
        clks(N, 3, 10, 10);
        return true;
    }
    return false;
}

static void add4s(nec_v30 *N) {
    int count = (get_breg(N, 1) + 1) / 2;
    uint16_t di = N->iy_;
    uint16_t si = N->ix_;
    N->zero_val_ = 0;
    N->carry_val_ = 0;
    for (int i = 0; i < count; i++) {
        clks(N, 18, 19, 19);
        uint8_t tmp = get_mem_b(N, kDs0, si);
        uint8_t tmp2 = get_mem_b(N, kDs1, di);
        int v1 = (tmp >> 4) * 10 + (tmp & 0xf);
        int v2 = (tmp2 >> 4) * 10 + (tmp2 & 0xf);
        int result = v1 + v2 + (cf(N) ? 1 : 0);
        N->carry_val_ = result > 99 ? 1 : 0;
        result %= 100;
        uint8_t packed = (uint8_t)(((result / 10) << 4) | (result % 10));
        put_mem_b(N, kDs1, di, packed);
        if (packed) N->zero_val_ = 1;
        si++;
        di++;
    }
}

static void sub4s(nec_v30 *N) {
    int count = (get_breg(N, 1) + 1) / 2;
    uint16_t di = N->iy_;
    uint16_t si = N->ix_;
    N->zero_val_ = 0;
    N->carry_val_ = 0;
    for (int i = 0; i < count; i++) {
        clks(N, 18, 19, 19);
        uint8_t tmp = get_mem_b(N, kDs1, di);
        uint8_t tmp2 = get_mem_b(N, kDs0, si);
        int v1 = (tmp >> 4) * 10 + (tmp & 0xf);
        int v2 = (tmp2 >> 4) * 10 + (tmp2 & 0xf);
        int result;
        if (v1 < v2 + (cf(N) ? 1 : 0)) {
            v1 += 100;
            result = v1 - (v2 + (cf(N) ? 1 : 0));
            N->carry_val_ = 1;
        } else {
            result = v1 - (v2 + (cf(N) ? 1 : 0));
            N->carry_val_ = 0;
        }
        uint8_t packed = (uint8_t)(((result / 10) << 4) | (result % 10));
        put_mem_b(N, kDs1, di, packed);
        if (packed) N->zero_val_ = 1;
        si++;
        di++;
    }
}

static void cmp4s(nec_v30 *N) {
    int count = (get_breg(N, 1) + 1) / 2;
    uint16_t di = N->iy_;
    uint16_t si = N->ix_;
    N->zero_val_ = 0;
    N->carry_val_ = 0;
    for (int i = 0; i < count; i++) {
        clks(N, 14, 19, 19);
        uint8_t tmp = get_mem_b(N, kDs1, di);
        uint8_t tmp2 = get_mem_b(N, kDs0, si);
        int v1 = (tmp >> 4) * 10 + (tmp & 0xf);
        int v2 = (tmp2 >> 4) * 10 + (tmp2 & 0xf);
        int result;
        if (v1 < v2 + (cf(N) ? 1 : 0)) {
            v1 += 100;
            result = v1 - (v2 + (cf(N) ? 1 : 0));
            N->carry_val_ = 1;
        } else {
            result = v1 - (v2 + (cf(N) ? 1 : 0));
            N->carry_val_ = 0;
        }
        uint8_t packed = (uint8_t)(((result / 10) << 4) | (result % 10));
        if (packed) N->zero_val_ = 1;
        si++;
        di++;
    }
}

static void i_pre_nec(nec_v30 *N) {
    uint8_t op = fetch(N);
    uint8_t modrm = 0;
    uint32_t tmp = 0;
    uint32_t tmp2 = 0;
    switch (op) {
        case 0x10:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 3, 3, 4);
            tmp2 = get_breg(N, 1) & 7;
            N->zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            N->carry_val_ = N->over_val_ = 0;
            break;
        case 0x11:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 3, 3, 4);
            tmp2 = get_breg(N, 1) & 0xf;
            N->zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            N->carry_val_ = N->over_val_ = 0;
            break;
        case 0x12:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 5, 5, 4);
            tmp2 = get_breg(N, 1) & 7;
            tmp &= ~(1u << tmp2);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            break;
        case 0x13:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 5, 5, 4);
            tmp2 = get_breg(N, 1) & 0xf;
            tmp &= ~(1u << tmp2);
            putback_rm_word(N, modrm, (uint16_t)(tmp));
            break;
        case 0x14:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 4, 4, 4);
            tmp2 = get_breg(N, 1) & 7;
            tmp |= (1u << tmp2);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            break;
        case 0x15:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 4, 4, 4);
            tmp2 = get_breg(N, 1) & 0xf;
            tmp |= (1u << tmp2);
            putback_rm_word(N, modrm, (uint16_t)(tmp));
            break;
        case 0x16:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 4, 4, 4);
            tmp2 = get_breg(N, 1) & 7;
            tmp ^= (1u << tmp2);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            break;
        case 0x17:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 4, 4, 4);
            tmp2 = get_breg(N, 1) & 0xf;
            tmp ^= (1u << tmp2);
            putback_rm_word(N, modrm, (uint16_t)(tmp));
            break;
        case 0x18:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 4, 4, 4);
            tmp2 = fetch(N) & 7;
            N->zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            N->carry_val_ = N->over_val_ = 0;
            break;
        case 0x19:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 4, 4, 4);
            tmp2 = fetch(N) & 0xf;
            N->zero_val_ = (tmp & (1u << tmp2)) ? 1 : 0;
            N->carry_val_ = N->over_val_ = 0;
            break;
        case 0x1a:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 6, 6, 4);
            tmp2 = fetch(N) & 7;
            tmp &= ~(1u << tmp2);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            break;
        case 0x1b:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 6, 6, 4);
            tmp2 = fetch(N) & 0xf;
            tmp &= ~(1u << tmp2);
            putback_rm_word(N, modrm, (uint16_t)(tmp));
            break;
        case 0x1c:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 5, 5, 4);
            tmp2 = fetch(N) & 7;
            tmp |= (1u << tmp2);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            break;
        case 0x1d:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 5, 5, 4);
            tmp2 = fetch(N) & 0xf;
            tmp |= (1u << tmp2);
            putback_rm_word(N, modrm, (uint16_t)(tmp));
            break;
        case 0x1e:
            modrm = fetch(N); tmp = get_rm_byte(N, modrm);
            clks(N, 5, 5, 4);
            tmp2 = fetch(N) & 7;
            tmp ^= (1u << tmp2);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            break;
        case 0x1f:
            modrm = fetch(N); tmp = get_rm_word(N, modrm);
            clks(N, 5, 5, 4);
            tmp2 = fetch(N) & 0xf;
            tmp ^= (1u << tmp2);
            putback_rm_word(N, modrm, (uint16_t)(tmp));
            break;
        case 0x20:
            add4s(N);
            clks(N, 7, 7, 2);
            break;
        case 0x22:
            sub4s(N);
            clks(N, 7, 7, 2);
            break;
        case 0x26:
            cmp4s(N);
            clks(N, 7, 7, 2);
            break;
        case 0x28: {
            modrm = fetch(N);
            tmp = get_rm_byte(N, modrm);
            tmp <<= 4;
            tmp |= get_breg(N, 0) & 0xf;
            set_breg(N, 0, (uint8_t)((get_breg(N, 0) & 0xf0) | ((tmp >> 8) & 0xf)));
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            clkm(N, 13, 13, 9, 28, 28, 15, modrm);
            break;
        }
        case 0x2a: {
            modrm = fetch(N);
            tmp = get_rm_byte(N, modrm);
            tmp2 = (uint32_t)(get_breg(N, 0) & 0xf) << 4;
            set_breg(N, 0, (uint8_t)((get_breg(N, 0) & 0xf0) | (tmp & 0xf)));
            tmp = tmp2 | (tmp >> 4);
            putback_rm_byte(N, modrm, (uint8_t)(tmp));
            clkm(N, 17, 17, 13, 32, 32, 19, modrm);
            break;
        }
        default:
            clk(N, 2);
            break;
    }
}

static void repeat_string(nec_v30 *N, uint8_t opcode, bool check_zf, bool zf_must_be) {
    uint16_t c = N->cw_;
    clk(N, 2);
    if (opcode < 0x6c || (opcode > 0x6f && opcode < 0xa4) || opcode > 0xaf) {
        string_once(N, opcode);
        N->seg_prefix_ = false;
        return;
    }
    if (c) {
        do {
            string_once(N, opcode);
            c--;
        } while (c > 0 && (!check_zf || zf(N) == zf_must_be));
        N->cw_ = c;
    }
    N->seg_prefix_ = false;
}

static void execute_op(nec_v30 *N, uint8_t op) {
    switch (op) {
        case 0x00: case 0x08: case 0x10: case 0x18:
        case 0x20: case 0x28: case 0x30: case 0x38: {
            uint8_t modrm = fetch(N);
            uint8_t dst = get_rm_byte(N, modrm);
            uint8_t src = reg_byte(N, modrm);
            uint8_t res = alu_b(N, (uint8_t)(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) putback_rm_byte(N, modrm, res);
            clkm(N, 2, 2, 2, 16, 16, 7, modrm);
            break;
        }
        case 0x01: case 0x09: case 0x11: case 0x19:
        case 0x21: case 0x29: case 0x31: case 0x39: {
            uint8_t modrm = fetch(N);
            uint16_t dst = get_rm_word(N, modrm);
            uint16_t src = reg_word(N, modrm);
            uint16_t res = alu_w(N, (uint8_t)(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) putback_rm_word(N, modrm, res);
            clkr(N, 24, 24, 11, 24, 16, 7, 2, modrm);
            break;
        }
        case 0x02: case 0x0a: case 0x12: case 0x1a:
        case 0x22: case 0x2a: case 0x32: case 0x3a: {
            uint8_t modrm = fetch(N);
            uint8_t dst = reg_byte(N, modrm);
            uint8_t src = get_rm_byte(N, modrm);
            uint8_t res = alu_b(N, (uint8_t)(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) set_reg_byte(N, modrm, res);
            clkm(N, 2, 2, 2, 11, 11, 6, modrm);
            break;
        }
        case 0x03: case 0x0b: case 0x13: case 0x1b:
        case 0x23: case 0x2b: case 0x33: case 0x3b: {
            uint8_t modrm = fetch(N);
            uint16_t dst = reg_word(N, modrm);
            uint16_t src = get_rm_word(N, modrm);
            uint16_t res = alu_w(N, (uint8_t)(op >> 3), dst, src);
            if ((op & 0x38) != 0x38) set_reg_word(N, modrm, res);
            clkr(N, 15, 15, 8, 15, 11, 6, 2, modrm);
            break;
        }
        case 0x04: case 0x0c: case 0x14: case 0x1c:
        case 0x24: case 0x2c: case 0x34: case 0x3c: {
            uint8_t src = fetch(N);
            uint8_t res = alu_b(N, (uint8_t)(op >> 3), get_breg(N, 0), src);
            if ((op & 0x38) != 0x38) set_breg(N, 0, res);
            clks(N, 4, 4, 2);
            break;
        }
        case 0x05: case 0x0d: case 0x15: case 0x1d:
        case 0x25: case 0x2d: case 0x35: case 0x3d: {
            uint16_t src = fetch_word(N);
            uint16_t res = alu_w(N, (uint8_t)(op >> 3), N->aw_, src);
            if ((op & 0x38) != 0x38) N->aw_ = res;
            clks(N, 4, 4, 2);
            break;
        }
        case 0x06: push(N, N->ds1_); clks(N, 12, 8, 3); break;
        case 0x07: N->ds1_ = pop(N); clks(N, 12, 8, 5); break;
        case 0x0e: push(N, N->ps_); clks(N, 12, 8, 3); break;
        case 0x0f: i_pre_nec(N); break;
        case 0x16: push(N, N->ss_); clks(N, 12, 8, 3); break;
        case 0x17: N->ss_ = pop(N); clks(N, 12, 8, 5); N->no_interrupt_ = true; break;
        case 0x1e: push(N, N->ds0_); clks(N, 12, 8, 3); break;
        case 0x1f: N->ds0_ = pop(N); clks(N, 12, 8, 5); break;
        case 0x26:
            N->seg_prefix_ = true;
            N->prefix_base_ = (uint32_t)(N->ds1_) << 4;
            clk(N, 2);
            execute_op(N, fetch(N));
            N->seg_prefix_ = false;
            break;
        case 0x27: adj4(N, 6, 0x60); clks(N, 3, 3, 2); break;
        case 0x2e:
            N->seg_prefix_ = true;
            N->prefix_base_ = (uint32_t)(N->ps_) << 4;
            clk(N, 2);
            execute_op(N, fetch(N));
            N->seg_prefix_ = false;
            break;
        case 0x2f: adj4(N, -6, -0x60); clks(N, 3, 3, 2); break;
        case 0x36:
            N->seg_prefix_ = true;
            N->prefix_base_ = (uint32_t)(N->ss_) << 4;
            clk(N, 2);
            execute_op(N, fetch(N));
            N->seg_prefix_ = false;
            break;
        case 0x37: adjb(N, 6, (get_breg(N, 0) > 0xf9) ? 2 : 1); clks(N, 7, 7, 4); break;
        case 0x3e:
            N->seg_prefix_ = true;
            N->prefix_base_ = (uint32_t)(N->ds0_) << 4;
            clk(N, 2);
            execute_op(N, fetch(N));
            N->seg_prefix_ = false;
            break;
        case 0x3f: adjb(N, -6, (get_breg(N, 0) < 6) ? -2 : -1); clks(N, 7, 7, 4); break;
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47: {
            uint16_t tmp = get_wreg(N, op & 7);
            uint16_t tmp1 = (uint16_t)(tmp + 1);
            N->over_val_ = (tmp == 0x7fff);
            N->aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
            set_szpf_word(N, tmp1);
            set_wreg(N, op & 7, tmp1);
            clk(N, 2);
            break;
        }
        case 0x48: case 0x49: case 0x4a: case 0x4b:
        case 0x4c: case 0x4d: case 0x4e: case 0x4f: {
            uint16_t tmp = get_wreg(N, op & 7);
            uint16_t tmp1 = (uint16_t)(tmp - 1);
            N->over_val_ = (tmp == 0x8000);
            N->aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
            set_szpf_word(N, tmp1);
            set_wreg(N, op & 7, tmp1);
            clk(N, 2);
            break;
        }
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            push(N, get_wreg(N, op & 7));
            clks(N, 12, 8, 3);
            break;
        case 0x58: case 0x59: case 0x5a: case 0x5b:
        case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            set_wreg(N, op & 7, pop(N));
            clks(N, 12, 8, 5);
            break;
        case 0x60: {
            uint16_t tmp = N->sp_;
            push(N, N->aw_);
            push(N, N->cw_);
            push(N, N->dw_);
            push(N, N->bw_);
            push(N, tmp);
            push(N, N->bp_);
            push(N, N->ix_);
            push(N, N->iy_);
            clks(N, 67, 35, 20);
            break;
        }
        case 0x61: {
            N->iy_ = pop(N);
            N->ix_ = pop(N);
            N->bp_ = pop(N);
            pop(N);
            N->bw_ = pop(N);
            N->dw_ = pop(N);
            N->cw_ = pop(N);
            N->aw_ = pop(N);
            clks(N, 75, 43, 22);
            break;
        }
        case 0x62: {
            uint8_t modrm = fetch(N);
            uint16_t low = get_rm_word(N, modrm);
            uint16_t high = get_next_rm_word(N);
            uint16_t tmp = reg_word(N, modrm);
            if (tmp < low || tmp > high) nec_interrupt(N, kChkindVector);
            clk(N, 20);
            break;
        }
        case 0x64:
        case 0x65: {
            uint8_t next = fetch(N);
            if (next == 0x26 || next == 0x2e || next == 0x36 || next == 0x3e) {
                N->seg_prefix_ = true;
                N->prefix_base_ = (uint32_t)(next == 0x26 ? N->ds1_ : next == 0x2e ? N->ps_ : next == 0x36 ? N->ss_ : N->ds0_)
                               << 4;
                next = fetch(N);
                clk(N, 2);
            }
            bool want_cf = (op == 0x65);
            uint16_t c = N->cw_;
            clk(N, 2);
            if (c) {
                do {
                    switch (next) {
                        case 0xa4: i_movsb(N); break;
                        case 0xa5: i_movsw(N); break;
                        case 0xa6: i_cmpsb(N); break;
                        case 0xa7: i_cmpsw(N); break;
                        case 0xaa: i_stosb(N); break;
                        case 0xab: i_stosw(N); break;
                        case 0xac: i_lodsb(N); break;
                        case 0xad: i_lodsw(N); break;
                        case 0xae: i_scasb(N); break;
                        case 0xaf: i_scasw(N); break;
                        default: execute_op(N, next); c = 1; break;
                    }
                    c--;
                } while (c > 0 && (cf(N) == want_cf));
                N->cw_ = c;
            }
            N->seg_prefix_ = false;
            break;
        }
        case 0x68: push(N, fetch_word(N)); clkw(N, 12, 12, 5, 12, 8, 5, N->sp_); break;
        case 0x69: {
            uint8_t modrm = fetch(N);
            uint16_t src = get_rm_word(N, modrm);
            uint16_t tmp = fetch_word(N);
            int32_t dst = (int32_t)((int16_t)(src)) * (int32_t)((int16_t)(tmp));
            N->carry_val_ = N->over_val_ = (((dst >> 15) != 0) && ((dst >> 15) != -1)) ? 1 : 0;
            set_reg_word(N, modrm, (uint16_t)(dst));
            clk(N, modrm >= 0xc0 ? 38 : 47);
            break;
        }
        case 0x6a: push(N, (uint16_t)((int16_t)((int8_t)(fetch(N))))); clkw(N, 11, 11, 5, 11, 7, 3, N->sp_); break;
        case 0x6b: {
            uint8_t modrm = fetch(N);
            uint16_t src = get_rm_word(N, modrm);
            uint16_t src2 = (uint16_t)((int16_t)((int8_t)(fetch(N))));
            int32_t dst = (int32_t)((int16_t)(src)) * (int32_t)((int16_t)(src2));
            N->carry_val_ = N->over_val_ = (((dst >> 15) != 0) && ((dst >> 15) != -1)) ? 1 : 0;
            set_reg_word(N, modrm, (uint16_t)(dst));
            clk(N, modrm >= 0xc0 ? 31 : 39);
            break;
        }
        case 0x6c: i_insb(N); break;
        case 0x6d: i_insw(N); break;
        case 0x6e: i_outsb(N); break;
        case 0x6f: i_outsw(N); break;
        case 0x70: if (!i_jcc(N, of(N))) clks(N, 4, 4, 3); break;
        case 0x71: if (!i_jcc(N, !of(N))) clks(N, 4, 4, 3); break;
        case 0x72: if (!i_jcc(N, cf(N))) clks(N, 4, 4, 3); break;
        case 0x73: if (!i_jcc(N, !cf(N))) clks(N, 4, 4, 3); break;
        case 0x74: if (!i_jcc(N, zf(N))) clks(N, 4, 4, 3); break;
        case 0x75: if (!i_jcc(N, !zf(N))) clks(N, 4, 4, 3); break;
        case 0x76: if (!i_jcc(N, cf(N) || zf(N))) clks(N, 4, 4, 3); break;
        case 0x77: if (!i_jcc(N, !(cf(N) || zf(N)))) clks(N, 4, 4, 3); break;
        case 0x78: if (!i_jcc(N, sf(N))) clks(N, 4, 4, 3); break;
        case 0x79: if (!i_jcc(N, !sf(N))) clks(N, 4, 4, 3); break;
        case 0x7a: if (!i_jcc(N, pf(N))) clks(N, 4, 4, 3); break;
        case 0x7b: if (!i_jcc(N, !pf(N))) clks(N, 4, 4, 3); break;
        case 0x7c: if (!i_jcc(N, (sf(N) != of(N)) && !zf(N))) clks(N, 4, 4, 3); break;
        case 0x7d: if (!i_jcc(N, zf(N) || (sf(N) == of(N)))) clks(N, 4, 4, 3); break;
        case 0x7e: if (!i_jcc(N, zf(N) || (sf(N) != of(N)))) clks(N, 4, 4, 3); break;
        case 0x7f: if (!i_jcc(N, (sf(N) == of(N)) && !zf(N))) clks(N, 4, 4, 3); break;
        case 0x80: case 0x82: {
            uint8_t modrm = fetch(N);
            uint32_t dst = get_rm_byte(N, modrm);
            uint32_t src = (op == 0x82) ? (uint32_t)((uint8_t)((int8_t)(fetch(N)))) : fetch(N);
            if (modrm >= 0xc0) clks(N, 4, 4, 2);
            else if ((modrm & 0x38) == 0x38) clks(N, 13, 13, 6);
            else clks(N, 18, 18, 7);
            uint8_t res = alu_b(N, (uint8_t)((modrm >> 3) & 7), dst, src);
            if ((modrm & 0x38) != 0x38) putback_rm_byte(N, modrm, res);
            break;
        }
        case 0x81: case 0x83: {
            uint8_t modrm = fetch(N);
            uint32_t dst = get_rm_word(N, modrm);
            uint32_t src = (op == 0x83) ? (uint32_t)((uint16_t)((int16_t)((int8_t)(fetch(N))))) : fetch_word(N);
            if (modrm >= 0xc0) clks(N, 4, 4, 2);
            else if ((modrm & 0x38) == 0x38) clkw(N, 17, 17, 8, 17, 13, 6, (uint16_t)(N->ea_));
            else clkw(N, 26, 26, 11, 26, 18, 7, (uint16_t)(N->ea_));
            uint16_t res = alu_w(N, (uint8_t)((modrm >> 3) & 7), dst, src);
            if ((modrm & 0x38) != 0x38) putback_rm_word(N, modrm, res);
            break;
        }
        case 0x84: {
            uint8_t modrm = fetch(N);
            and_b(N, get_rm_byte(N, modrm), reg_byte(N, modrm));
            clkm(N, 2, 2, 2, 10, 10, 6, modrm);
            break;
        }
        case 0x85: {
            uint8_t modrm = fetch(N);
            and_w(N, get_rm_word(N, modrm), reg_word(N, modrm));
            clkr(N, 14, 14, 8, 14, 10, 6, 2, modrm);
            break;
        }
        case 0x86: {
            uint8_t modrm = fetch(N);
            uint8_t dst = get_rm_byte(N, modrm);
            uint8_t src = reg_byte(N, modrm);
            set_reg_byte(N, modrm, dst);
            putback_rm_byte(N, modrm, src);
            clkm(N, 3, 3, 3, 16, 18, 8, modrm);
            break;
        }
        case 0x87: {
            uint8_t modrm = fetch(N);
            uint16_t dst = get_rm_word(N, modrm);
            uint16_t src = reg_word(N, modrm);
            set_reg_word(N, modrm, dst);
            putback_rm_word(N, modrm, src);
            clkr(N, 24, 24, 12, 24, 16, 8, 3, modrm);
            break;
        }
        case 0x88: {
            uint8_t modrm = fetch(N);
            put_rm_byte(N, modrm, reg_byte(N, modrm));
            clkm(N, 2, 2, 2, 9, 9, 3, modrm);
            break;
        }
        case 0x89: {
            uint8_t modrm = fetch(N);
            put_rm_word(N, modrm, reg_word(N, modrm));
            clkr(N, 13, 13, 5, 13, 9, 3, 2, modrm);
            break;
        }
        case 0x8a: {
            uint8_t modrm = fetch(N);
            set_reg_byte(N, modrm, get_rm_byte(N, modrm));
            clkm(N, 2, 2, 2, 11, 11, 5, modrm);
            break;
        }
        case 0x8b: {
            uint8_t modrm = fetch(N);
            set_reg_word(N, modrm, get_rm_word(N, modrm));
            clkr(N, 15, 15, 7, 15, 11, 5, 2, modrm);
            break;
        }
        case 0x8c: {
            uint8_t modrm = fetch(N);
            uint16_t src = 0;
            switch (modrm & 0x38) {
                case 0x00: src = N->ds1_; break;
                case 0x08: src = N->ps_; break;
                case 0x10: src = N->ss_; break;
                default: src = N->ds0_; break;
            }
            put_rm_word(N, modrm, src);
            clkr(N, 14, 14, 5, 14, 10, 3, 2, modrm);
            break;
        }
        case 0x8d: {
            uint8_t modrm = fetch(N);
            get_ea(N, modrm);
            set_reg_word(N, modrm, N->eo_);
            clks(N, 4, 4, 2);
            break;
        }
        case 0x8e: {
            uint8_t modrm = fetch(N);
            uint16_t src = get_rm_word(N, modrm);
            clkr(N, 15, 15, 7, 15, 11, 5, 2, modrm);
            switch (modrm & 0x38) {
                case 0x00: N->ds1_ = src; break;
                case 0x08: N->ps_ = src; break;
                case 0x10: N->ss_ = src; break;
                default: N->ds0_ = src; break;
            }
            N->no_interrupt_ = true;
            break;
        }
        case 0x8f: {
            uint8_t modrm = fetch(N);
            uint16_t tmp = pop(N);
            put_rm_word(N, modrm, tmp);
            clk(N, 21);
            break;
        }
        case 0x90: clk(N, 3); break;
        case 0x91: case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97: {
            uint16_t tmp = get_wreg(N, op & 7);
            set_wreg(N, op & 7, N->aw_);
            N->aw_ = tmp;
            clk(N, 3);
            break;
        }
        case 0x98:
            set_breg(N, 4, (get_breg(N, 0) & 0x80) ? 0xff : 0);
            clk(N, 2);
            break;
        case 0x99:
            N->dw_ = (get_breg(N, 4) & 0x80) ? 0xffff : 0;
            clk(N, 4);
            break;
        case 0x9a: {
            uint16_t off = fetch_word(N);
            uint16_t seg = fetch_word(N);
            push(N, N->ps_);
            push(N, N->ip_);
            N->ip_ = off;
            N->ps_ = seg;
            N->prefetch_reset_ = true;
            clkw(N, 29, 29, 13, 29, 21, 9, N->sp_);
            break;
        }
        case 0x9b: clk(N, 5); break;
        case 0x9c: i_pushf(N); break;
        case 0x9d: i_popf(N); break;
        case 0x9e: {
            uint16_t tmp = (uint16_t)((compress_flags(N) & 0xff00) | (get_breg(N, 4) & 0xd5));
            expand_flags(N, tmp);
            clks(N, 3, 3, 2);
            break;
        }
        case 0x9f:
            set_breg(N, 4, (uint8_t)(compress_flags(N)));
            clks(N, 3, 3, 2);
            break;
        case 0xa0: set_breg(N, 0, get_mem_b(N, kDs0, fetch_word(N))); clks(N, 10, 10, 5); break;
        case 0xa1: {
            uint16_t addr = fetch_word(N);
            N->aw_ = get_mem_w(N, kDs0, addr);
            clkw(N, 14, 14, 7, 14, 10, 5, addr);
            break;
        }
        case 0xa2: put_mem_b(N, kDs0, fetch_word(N), get_breg(N, 0)); clks(N, 9, 9, 3); break;
        case 0xa3: {
            uint16_t addr = fetch_word(N);
            put_mem_w(N, kDs0, addr, N->aw_);
            clkw(N, 13, 13, 5, 13, 9, 3, addr);
            break;
        }
        case 0xa4: i_movsb(N); break;
        case 0xa5: i_movsw(N); break;
        case 0xa6: i_cmpsb(N); break;
        case 0xa7: i_cmpsw(N); break;
        case 0xa8: and_b(N, get_breg(N, 0), fetch(N)); clks(N, 4, 4, 2); break;
        case 0xa9: and_w(N, N->aw_, fetch_word(N)); clks(N, 4, 4, 2); break;
        case 0xaa: i_stosb(N); break;
        case 0xab: i_stosw(N); break;
        case 0xac: i_lodsb(N); break;
        case 0xad: i_lodsw(N); break;
        case 0xae: i_scasb(N); break;
        case 0xaf: i_scasw(N); break;
        case 0xb0: case 0xb1: case 0xb2: case 0xb3:
        case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            set_breg(N, op & 7, fetch(N));
            clks(N, 4, 4, 2);
            break;
        case 0xb8: case 0xb9: case 0xba: case 0xbb:
        case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            set_wreg(N, op & 7, fetch_word(N));
            clks(N, 4, 4, 2);
            break;
        case 0xc0: {
            uint8_t modrm = fetch(N);
            uint8_t dst = get_rm_byte(N, modrm);
            uint8_t c = fetch(N);
            clkm(N, 7, 7, 2, 19, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x20: putback_rm_byte(N, modrm, shl_b(N, dst, c)); break;
                    case 0x28: putback_rm_byte(N, modrm, shr_b(N, dst, c)); break;
                    case 0x38: putback_rm_byte(N, modrm, shra_b(N, dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xc1: {
            uint8_t modrm = fetch(N);
            uint16_t dst = get_rm_word(N, modrm);
            uint8_t c = fetch(N);
            clkm(N, 7, 7, 2, 27, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x20: putback_rm_word(N, modrm, shl_w(N, dst, c)); break;
                    case 0x28: putback_rm_word(N, modrm, shr_w(N, dst, c)); break;
                    case 0x38: putback_rm_word(N, modrm, shra_w(N, dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xc2: {
            uint16_t count = fetch_word(N);
            N->ip_ = pop(N);
            N->sp_ = (uint16_t)(N->sp_ + count);
            N->prefetch_reset_ = true;
            clks(N, 24, 24, 10);
            break;
        }
        case 0xc3:
            N->ip_ = pop(N);
            N->prefetch_reset_ = true;
            clks(N, 19, 19, 10);
            break;
        case 0xc4: {
            uint8_t modrm = fetch(N);
            uint16_t tmp = get_rm_word(N, modrm);
            set_reg_word(N, modrm, tmp);
            N->ds1_ = get_next_rm_word(N);
            clkw(N, 26, 26, 14, 26, 18, 10, (uint16_t)(N->ea_));
            break;
        }
        case 0xc5: {
            uint8_t modrm = fetch(N);
            uint16_t tmp = get_rm_word(N, modrm);
            set_reg_word(N, modrm, tmp);
            N->ds0_ = get_next_rm_word(N);
            clkw(N, 26, 26, 14, 26, 18, 10, (uint16_t)(N->ea_));
            break;
        }
        case 0xc6: {
            uint8_t modrm = fetch(N);
            put_imm_rm_byte(N, modrm);
            clk(N, modrm >= 0xc0 ? 4 : 11);
            break;
        }
        case 0xc7: {
            uint8_t modrm = fetch(N);
            put_imm_rm_word(N, modrm);
            clk(N, modrm >= 0xc0 ? 4 : 15);
            break;
        }
        case 0xc8: {
            uint16_t nb = fetch_word(N);
            uint8_t level = fetch(N);
            clk(N, 23);
            push(N, N->bp_);
            N->bp_ = N->sp_;
            N->sp_ = (uint16_t)(N->sp_ - nb);
            for (uint8_t i = 1; i < level; i++) {
                push(N, get_mem_w(N, kSs, (uint16_t)(N->bp_ - i * 2)));
                clk(N, 16);
            }
            if (level) push(N, N->bp_);
            break;
        }
        case 0xc9:
            N->sp_ = N->bp_;
            N->bp_ = pop(N);
            clk(N, 8);
            break;
        case 0xca: {
            uint16_t count = fetch_word(N);
            N->ip_ = pop(N);
            N->ps_ = pop(N);
            N->sp_ = (uint16_t)(N->sp_ + count);
            N->prefetch_reset_ = true;
            clks(N, 32, 32, 16);
            break;
        }
        case 0xcb:
            N->ip_ = pop(N);
            N->ps_ = pop(N);
            N->prefetch_reset_ = true;
            clks(N, 29, 29, 16);
            break;
        case 0xcc: nec_interrupt(N, 3); clks(N, 50, 50, 24); break;
        case 0xcd: nec_interrupt(N, fetch(N)); clks(N, 50, 50, 24); break;
        case 0xce:
            if (of(N)) {
                nec_interrupt(N, kBrkvVector);
                clks(N, 52, 52, 26);
            } else clk(N, 3);
            break;
        case 0xcf:
            N->ip_ = pop(N);
            N->ps_ = pop(N);
            i_popf(N);
            N->prefetch_reset_ = true;
            clks(N, 39, 39, 19);
            break;
        case 0xd0: {
            uint8_t modrm = fetch(N);
            uint8_t src = get_rm_byte(N, modrm);
            uint8_t dst = src;
            clkm(N, 6, 6, 2, 16, 16, 7, modrm);
            switch (modrm & 0x38) {
                case 0x00: dst = rol_b(N, dst); N->over_val_ = (src ^ dst) & 0x80; break;
                case 0x08: dst = ror_b(N, dst); N->over_val_ = (src ^ dst) & 0x80; break;
                case 0x10: dst = rolc_b(N, dst); N->over_val_ = (src ^ dst) & 0x80; break;
                case 0x18: dst = rorc_b(N, dst); N->over_val_ = (src ^ dst) & 0x80; break;
                case 0x20: dst = shl_b(N, dst, 1); N->over_val_ = (src ^ dst) & 0x80; break;
                case 0x28: dst = shr_b(N, dst, 1); N->over_val_ = (src ^ dst) & 0x80; break;
                case 0x38: dst = shra_b(N, dst, 1); N->over_val_ = 0; break;
                default: break;
            }
            putback_rm_byte(N, modrm, dst);
            break;
        }
        case 0xd1: {
            uint8_t modrm = fetch(N);
            uint16_t src = get_rm_word(N, modrm);
            uint16_t dst = src;
            clkm(N, 6, 6, 2, 24, 16, 7, modrm);
            switch (modrm & 0x38) {
                case 0x00: dst = rol_w(N, dst); N->over_val_ = (src ^ dst) & 0x8000; break;
                case 0x08: dst = ror_w(N, dst); N->over_val_ = (src ^ dst) & 0x8000; break;
                case 0x10: dst = rolc_w(N, dst); N->over_val_ = (src ^ dst) & 0x8000; break;
                case 0x18: dst = rorc_w(N, dst); N->over_val_ = (src ^ dst) & 0x8000; break;
                case 0x20: dst = shl_w(N, dst, 1); N->over_val_ = (src ^ dst) & 0x8000; break;
                case 0x28: dst = shr_w(N, dst, 1); N->over_val_ = (src ^ dst) & 0x8000; break;
                case 0x38: dst = shra_w(N, dst, 1); N->over_val_ = 0; break;
                default: break;
            }
            putback_rm_word(N, modrm, dst);
            break;
        }
        case 0xd2: {
            uint8_t modrm = fetch(N);
            uint8_t dst = get_rm_byte(N, modrm);
            uint8_t c = get_breg(N, 1);
            clkm(N, 7, 7, 2, 19, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_b(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_byte(N, modrm, dst);
                        break;
                    case 0x20: putback_rm_byte(N, modrm, shl_b(N, dst, c)); break;
                    case 0x28: putback_rm_byte(N, modrm, shr_b(N, dst, c)); break;
                    case 0x38: putback_rm_byte(N, modrm, shra_b(N, dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xd3: {
            uint8_t modrm = fetch(N);
            uint16_t dst = get_rm_word(N, modrm);
            uint8_t c = get_breg(N, 1);
            clkm(N, 7, 7, 2, 27, 19, 6, modrm);
            if (c) {
                switch (modrm & 0x38) {
                    case 0x00:
                        do { dst = rol_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x08:
                        do { dst = ror_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x10:
                        do { dst = rolc_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x18:
                        do { dst = rorc_w(N, dst); c--; clk(N, 1); } while (c);
                        putback_rm_word(N, modrm, dst);
                        break;
                    case 0x20: putback_rm_word(N, modrm, shl_w(N, dst, c)); break;
                    case 0x28: putback_rm_word(N, modrm, shr_w(N, dst, c)); break;
                    case 0x38: putback_rm_word(N, modrm, shra_w(N, dst, c)); break;
                    default: break;
                }
            }
            break;
        }
        case 0xd4:
            fetch(N);
            set_breg(N, 4, (uint8_t)(get_breg(N, 0) / 10));
            set_breg(N, 0, (uint8_t)(get_breg(N, 0) % 10));
            set_szpf_word(N, N->aw_);
            clks(N, 15, 15, 12);
            break;
        case 0xd5:
            fetch(N);
            set_breg(N, 0, (uint8_t)(get_breg(N, 4) * 10 + get_breg(N, 0)));
            set_breg(N, 4, 0);
            set_szpf_byte(N, get_breg(N, 0));
            clks(N, 7, 7, 8);
            break;
        case 0xd6:
            set_breg(N, 0, cf(N) ? 0xff : 0x00);
            clk(N, 3);
            break;
        case 0xd7: {
            uint16_t dest = (uint16_t)(N->bw_ + get_breg(N, 0));
            set_breg(N, 0, get_mem_b(N, kDs0, dest));
            clks(N, 9, 9, 5);
            break;
        }
        case 0xd8: case 0xd9: case 0xda: case 0xdb:
        case 0xdc: case 0xdd: case 0xde: case 0xdf: {
            uint8_t modrm = fetch(N);
            get_rm_byte(N, modrm);
            clk(N, 2);
            break;
        }
        case 0xe0: {
            int8_t disp = (int8_t)(fetch(N));
            N->cw_--;
            if (!zf(N) && N->cw_) {
                N->ip_ = (uint16_t)(N->ip_ + disp);
                clks(N, 14, 14, 6);
            } else clks(N, 5, 5, 3);
            break;
        }
        case 0xe1: {
            int8_t disp = (int8_t)(fetch(N));
            N->cw_--;
            if (zf(N) && N->cw_) {
                N->ip_ = (uint16_t)(N->ip_ + disp);
                clks(N, 14, 14, 6);
            } else clks(N, 5, 5, 3);
            break;
        }
        case 0xe2: {
            int8_t disp = (int8_t)(fetch(N));
            N->cw_--;
            if (N->cw_) {
                N->ip_ = (uint16_t)(N->ip_ + disp);
                clks(N, 13, 13, 6);
            } else clks(N, 5, 5, 3);
            break;
        }
        case 0xe3: {
            int8_t disp = (int8_t)(fetch(N));
            if (N->cw_ == 0) {
                N->ip_ = (uint16_t)(N->ip_ + disp);
                clks(N, 13, 13, 6);
            } else clks(N, 5, 5, 3);
            break;
        }
        case 0xe4: set_breg(N, 0, in_byte(N, fetch(N))); clks(N, 9, 9, 5); break;
        case 0xe5: {
            uint8_t port = fetch(N);
            N->aw_ = in_word(N, port);
            clkw(N, 13, 13, 7, 13, 9, 5, port);
            break;
        }
        case 0xe6: out_byte(N, fetch(N), get_breg(N, 0)); clks(N, 8, 8, 3); break;
        case 0xe7: {
            uint8_t port = fetch(N);
            out_word(N, port, N->aw_);
            clkw(N, 12, 12, 5, 12, 8, 3, port);
            break;
        }
        case 0xe8: {
            uint16_t tmp = fetch_word(N);
            push(N, N->ip_);
            N->ip_ = (uint16_t)(N->ip_ + (int16_t)(tmp));
            N->prefetch_reset_ = true;
            clk(N, 24);
            break;
        }
        case 0xe9: {
            uint16_t tmp = fetch_word(N);
            N->ip_ = (uint16_t)(N->ip_ + (int16_t)(tmp));
            N->prefetch_reset_ = true;
            clk(N, 15);
            break;
        }
        case 0xea: {
            uint16_t off = fetch_word(N);
            uint16_t seg = fetch_word(N);
            N->ip_ = off;
            N->ps_ = seg;
            N->prefetch_reset_ = true;
            clk(N, 27);
            break;
        }
        case 0xeb: {
            int8_t tmp = (int8_t)(fetch(N));
            clk(N, 12);
            N->ip_ = (uint16_t)(N->ip_ + tmp);
            break;
        }
        case 0xec: set_breg(N, 0, in_byte(N, N->dw_)); clks(N, 8, 8, 5); break;
        case 0xed: N->aw_ = in_word(N, N->dw_); clkw(N, 12, 12, 7, 12, 8, 5, N->dw_); break;
        case 0xee: out_byte(N, N->dw_, get_breg(N, 0)); clks(N, 8, 8, 3); break;
        case 0xef: out_word(N, N->dw_, N->aw_); clkw(N, 12, 12, 5, 12, 8, 3, N->dw_); break;
        case 0xf0:
            N->no_interrupt_ = true;
            clk(N, 2);
            execute_op(N, fetch(N));
            break;
        case 0xf1: clk(N, 2); break;
        case 0xf2:
        case 0xf3: {
            uint8_t next = fetch(N);
            if (next == 0x26 || next == 0x2e || next == 0x36 || next == 0x3e) {
                N->seg_prefix_ = true;
                N->prefix_base_ = (uint32_t)(next == 0x26 ? N->ds1_ : next == 0x2e ? N->ps_ : next == 0x36 ? N->ss_ : N->ds0_)
                               << 4;
                next = fetch(N);
                clk(N, 2);
            }
            bool check_zf = (next == 0xa6 || next == 0xa7 || next == 0xae || next == 0xaf);
            repeat_string(N, next, check_zf, op == 0xf3);
            break;
        }
        case 0xf4:
            N->halted_ = true;
            break;
        case 0xf5:
            N->carry_val_ = cf(N) ? 0 : 1;
            clk(N, 2);
            break;
        case 0xf6: {
            uint8_t modrm = fetch(N);
            uint32_t tmp = get_rm_byte(N, modrm);
            switch (modrm & 0x38) {
                case 0x00:
                    tmp &= fetch(N);
                    N->carry_val_ = N->over_val_ = 0;
                    set_szpf_byte(N, tmp);
                    clk(N, modrm >= 0xc0 ? 4 : 11);
                    break;
                case 0x10:
                    putback_rm_byte(N, modrm, (uint8_t)(~tmp));
                    clk(N, modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x18:
                    N->carry_val_ = (tmp != 0);
                    tmp = (uint8_t)(~tmp) + 1;
                    set_szpf_byte(N, tmp);
                    putback_rm_byte(N, modrm, (uint8_t)(tmp));
                    clk(N, modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x20: {
                    uint32_t uresult = (uint32_t)(get_breg(N, 0)) * tmp;
                    N->aw_ = (uint16_t)(uresult);
                    N->carry_val_ = N->over_val_ = (get_breg(N, 4) != 0);
                    clk(N, modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x28: {
                    int32_t result = (int16_t)((int8_t)(get_breg(N, 0))) * (int16_t)((int8_t)(tmp));
                    N->aw_ = (uint16_t)(result);
                    N->carry_val_ = N->over_val_ = (get_breg(N, 4) != 0);
                    clk(N, modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x30:
                    if (tmp) {
                        uint32_t uresult = N->aw_;
                        uint32_t uresult2 = uresult % tmp;
                        uresult /= tmp;
                        if (uresult > 0xff) nec_interrupt(N, kDivideVector);
                        else {
                            set_breg(N, 0, (uint8_t)(uresult));
                            set_breg(N, 4, (uint8_t)(uresult2));
                        }
                    } else nec_interrupt(N, kDivideVector);
                    clk(N, modrm >= 0xc0 ? 43 : 53);
                    break;
                case 0x38:
                    if (tmp) {
                        int32_t result = (int16_t)(N->aw_);
                        int32_t result2 = result % (int16_t)((int8_t)(tmp));
                        result /= (int16_t)((int8_t)(tmp));
                        if (result > 0xff || result < -0x80) nec_interrupt(N, kDivideVector);
                        else {
                            set_breg(N, 0, (uint8_t)(result));
                            set_breg(N, 4, (uint8_t)(result2));
                        }
                    } else nec_interrupt(N, kDivideVector);
                    clk(N, modrm >= 0xc0 ? 43 : 53);
                    break;
                default: break;
            }
            break;
        }
        case 0xf7: {
            uint8_t modrm = fetch(N);
            uint32_t tmp = get_rm_word(N, modrm);
            switch (modrm & 0x38) {
                case 0x00:
                    tmp &= fetch_word(N);
                    N->carry_val_ = N->over_val_ = 0;
                    set_szpf_word(N, tmp);
                    clk(N, modrm >= 0xc0 ? 4 : 11);
                    break;
                case 0x10:
                    putback_rm_word(N, modrm, (uint16_t)(~tmp));
                    clk(N, modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x18:
                    N->carry_val_ = (tmp != 0);
                    tmp = (uint16_t)(~tmp) + 1;
                    set_szpf_word(N, tmp);
                    putback_rm_word(N, modrm, (uint16_t)(tmp));
                    clk(N, modrm >= 0xc0 ? 2 : 16);
                    break;
                case 0x20: {
                    uint32_t uresult = (uint32_t)(N->aw_) * tmp;
                    N->aw_ = (uint16_t)(uresult);
                    N->dw_ = (uint16_t)(uresult >> 16);
                    N->carry_val_ = N->over_val_ = (N->dw_ != 0);
                    clk(N, modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x28: {
                    int32_t result = (int32_t)((int16_t)(N->aw_)) * (int32_t)((int16_t)(tmp));
                    N->aw_ = (uint16_t)(result);
                    N->dw_ = (uint16_t)((uint32_t)(result) >> 16);
                    N->carry_val_ = N->over_val_ = (N->dw_ != 0);
                    clk(N, modrm >= 0xc0 ? 30 : 36);
                    break;
                }
                case 0x30:
                    if (tmp) {
                        uint32_t uresult = ((uint32_t)(N->dw_) << 16) | N->aw_;
                        uint32_t uresult2 = uresult % tmp;
                        uresult /= tmp;
                        if (uresult > 0xffff) nec_interrupt(N, kDivideVector);
                        else {
                            N->aw_ = (uint16_t)(uresult);
                            N->dw_ = (uint16_t)(uresult2);
                        }
                    } else nec_interrupt(N, kDivideVector);
                    clk(N, modrm >= 0xc0 ? 43 : 53);
                    break;
                case 0x38:
                    if (tmp) {
                        int32_t result = (int32_t)(((uint32_t)(N->dw_) << 16) | N->aw_);
                        int32_t result2 = result % (int32_t)((int16_t)(tmp));
                        result /= (int32_t)((int16_t)(tmp));
                        if (result > 0xffff || result < -0x8000) nec_interrupt(N, kDivideVector);
                        else {
                            N->aw_ = (uint16_t)(result);
                            N->dw_ = (uint16_t)(result2);
                        }
                    } else nec_interrupt(N, kDivideVector);
                    clk(N, modrm >= 0xc0 ? 43 : 53);
                    break;
                default: break;
            }
            break;
        }
        case 0xf8: N->carry_val_ = 0; clk(N, 2); break;
        case 0xf9: N->carry_val_ = 1; clk(N, 2); break;
        case 0xfa: N->iff_ = false; clk(N, 2); break;
        case 0xfb: N->iff_ = true; clk(N, 2); break;
        case 0xfc: N->df_ = false; clk(N, 2); break;
        case 0xfd: N->df_ = true; clk(N, 2); break;
        case 0xfe: {
            uint8_t modrm = fetch(N);
            uint32_t tmp = get_rm_byte(N, modrm);
            if ((modrm & 0x38) == 0x00) {
                uint32_t tmp1 = tmp + 1;
                N->over_val_ = (tmp == 0x7f);
                N->aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                set_szpf_byte(N, tmp1);
                putback_rm_byte(N, modrm, (uint8_t)(tmp1));
            } else {
                uint32_t tmp1 = tmp - 1;
                N->over_val_ = (tmp == 0x80);
                N->aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                set_szpf_byte(N, tmp1);
                putback_rm_byte(N, modrm, (uint8_t)(tmp1));
            }
            clkm(N, 2, 2, 2, 16, 16, 7, modrm);
            break;
        }
        case 0xff: {
            uint8_t modrm = fetch(N);
            uint32_t tmp = get_rm_word(N, modrm);
            switch (modrm & 0x38) {
                case 0x00: {
                    uint32_t tmp1 = tmp + 1;
                    N->over_val_ = (tmp == 0x7fff);
                    N->aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                    set_szpf_word(N, tmp1);
                    putback_rm_word(N, modrm, (uint16_t)(tmp1));
                    clkm(N, 2, 2, 2, 24, 16, 7, modrm);
                    break;
                }
                case 0x08: {
                    uint32_t tmp1 = tmp - 1;
                    N->over_val_ = (tmp == 0x8000);
                    N->aux_val_ = (tmp1 ^ (tmp ^ 1)) & 0x10;
                    set_szpf_word(N, tmp1);
                    putback_rm_word(N, modrm, (uint16_t)(tmp1));
                    clkm(N, 2, 2, 2, 24, 16, 7, modrm);
                    break;
                }
                case 0x10:
                    push(N, N->ip_);
                    N->ip_ = (uint16_t)(tmp);
                    N->prefetch_reset_ = true;
                    clk(N, modrm >= 0xc0 ? 16 : 20);
                    break;
                case 0x18: {
                    uint16_t tmp1 = N->ps_;
                    N->ps_ = get_next_rm_word(N);
                    push(N, tmp1);
                    push(N, N->ip_);
                    N->ip_ = (uint16_t)(tmp);
                    N->prefetch_reset_ = true;
                    clk(N, modrm >= 0xc0 ? 16 : 26);
                    break;
                }
                case 0x20:
                    N->ip_ = (uint16_t)(tmp);
                    N->prefetch_reset_ = true;
                    clk(N, 13);
                    break;
                case 0x28:
                    N->ip_ = (uint16_t)(tmp);
                    N->ps_ = get_next_rm_word(N);
                    N->prefetch_reset_ = true;
                    clk(N, 15);
                    break;
                case 0x30:
                    push(N, (uint16_t)(tmp));
                    clk(N, 4);
                    break;
                default: clk(N, 2); break;
            }
            break;
        }
        default:
            clk(N, 10);
            break;
    }
}

void nec_v30_set_irq(nec_v30 *N, int state, uint8_t vector) {
    if (state == 0) N->irq_pending_ &= (uint8_t)(~kIntIrq);
    else {
        N->irq_vector_ = vector;
        N->irq_pending_ |= kIntIrq;
        N->halted_ = false;
    }
}

void nec_v30_set_nmi(nec_v30 *N, int state) {
    if (N->nmi_state_ == state) return;
    N->nmi_state_ = state;
    if (state != 0) {
        N->irq_pending_ |= kNmiIrq;
        N->halted_ = false;
    }
}

int nec_v30_run(nec_v30 *N, int cycles) {
    N->executed_ = 0;
    N->icount_ = 0;
    if (N->halted_) {
        N->executed_ = cycles;
        if (N->cycles) N->cycles(N->user, cycles);
        return N->executed_;
    }
    while (N->icount_ < cycles) {
        if (N->irq_pending_ && !N->no_interrupt_) {
            if (N->irq_pending_ & kNmiIrq) {
                nec_interrupt(N, kNmiVector);
                N->irq_pending_ &= (uint8_t)(~kNmiIrq);
                N->nmi_state_ = 0;
                clk(N, 9);
            } else if (N->iff_ && N->irq_pending_) {
                nec_interrupt(N, N->irq_vector_);
                N->irq_pending_ &= (uint8_t)(~kIntIrq);
                clk(N, 14);
            }
        }
        N->no_interrupt_ = false;
        if (N->halted_) {
            int remain = cycles - N->icount_;
            if (remain > 0) clk(N, remain);
            break;
        }
        int prev = N->icount_;
        execute_op(N, fetch(N));
        int delta = N->icount_ - prev;
        if (N->cycles && delta) N->cycles(N->user, delta);
        do_prefetch(N, prev);
    }
    N->executed_ = N->icount_;
    return N->executed_;
}


void nec_v30_init(nec_v30 *N, int type)
{
    for (int i = 0; i < 256; i++) {
        int bits = 0;
        for (int j = i; j; j >>= 1) bits += j & 1;
        kParity[i] = (uint8_t)((bits & 1) == 0);
    }
    N->type = type;
    if (type == NEC_V20) {
        N->prefetch_size_ = 4;
        N->prefetch_cycles_ = 4;
    } else if (type == NEC_V33) {
        N->prefetch_size_ = 6;
        N->prefetch_cycles_ = 1;
    } else {
        N->prefetch_size_ = 6;
        N->prefetch_cycles_ = 2;
    }
    nec_v30_reset(N);
}
