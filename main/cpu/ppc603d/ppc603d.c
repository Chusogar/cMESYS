// ppc603d.c - PowerPC 603: C11 port of dsp-cpp's cpu/ppc603.cpp.
#include <math.h>
#include <string.h>
#include <limits.h>
#include "ppc603d.h"

#define MSR_EE 0x8000u
#define MSR_FP 0x2000u
#define MSR_IP 0x40u
#define MSR_ILE 0x10000u

static inline uint32_t rotl(uint32_t v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
static inline uint32_t mask(int mb, int me)
{
    const uint32_t a = 0xffffffffu >> mb, b = 0xffffffffu << (31 - me);
    return mb <= me ? (a & b) : (a | b);
}

void ppcd_init(ppcd *p, uint32_t pvr)
{
    memset(p, 0, sizeof *p);
    p->pvr = pvr;
    p->timer_div = 10;
    p->idle_pc = 0xffffffffu;
    p->spin_detect = true;
    p->spin_target = p->spin_branch = 0xffffffffu;
    p->dec_fire_cycle = ~0ull;
}

void ppcd_add_fast_region(ppcd *p, uint32_t start, uint32_t size, uint8_t *data, bool writable)
{
    if (p->num_regions >= 8) return;
    ppcd_region *r = &p->regions[p->num_regions++];
    r->start = start;
    r->end = start + size - 1;
    r->data = data;
    r->writable = writable;
}

void ppcd_reset(ppcd *p)
{
    memset(p->r, 0, sizeof p->r);
    memset(p->f, 0, sizeof p->f);
    p->cr = p->lr = p->ctr = 0;
    p->xer_so = p->xer_ov = p->xer_ca = false;
    p->xer_bc = 0;
    p->fpscr = 0;
    p->srr0 = p->srr1 = p->dar = p->dsisr = p->sdr1 = 0;
    memset(p->sprg, 0, sizeof p->sprg);
    memset(p->sr, 0, sizeof p->sr);
    memset(p->bat, 0, sizeof p->bat);
    p->hid0 = p->hid1 = p->hid2 = 0;
    p->msr = MSR_IP;
    p->pc = 0xfff00100u;
    p->tb_base = 0;
    p->tb_cycle = p->total_cycles;
    p->dec_base = 0xffffffffu;
    p->dec_cycle = p->total_cycles;
    p->dec_pending = false;
    p->dec_fire_cycle = ~0ull;
    p->reserve = false;
}

// ---------------------------------------------------------------------------
// Memory

static inline const ppcd_region *find(ppcd *p, uint32_t a)
{
    const ppcd_region *last = &p->regions[p->last_region];
    if (a >= last->start && a <= last->end && last->data) return last;
    for (int i = 0; i < p->num_regions; ++i)
        if (a >= p->regions[i].start && a <= p->regions[i].end) {
            p->last_region = i;
            return &p->regions[i];
        }
    return NULL;
}

static inline uint32_t ld32(const uint8_t *d) { return (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]; }
static inline void st32(uint8_t *d, uint32_t v) { d[0] = (uint8_t)(v >> 24); d[1] = (uint8_t)(v >> 16); d[2] = (uint8_t)(v >> 8); d[3] = (uint8_t)v; }

static uint32_t fetch(ppcd *p, uint32_t a)
{
    const ppcd_region *r = find(p, a);
    if (r) return ld32(r->data + (a - r->start));
    return p->read32(p->user, a);
}

static uint8_t rd8(ppcd *p, uint32_t a)
{
    const ppcd_region *r = find(p, a);
    if (r) return r->data[a - r->start];
    return p->read8(p->user, a);
}

static uint16_t rd16(ppcd *p, uint32_t a)
{
    const ppcd_region *r = find(p, a);
    if (r && a + 1 <= r->end) {
        const uint8_t *d = r->data + (a - r->start);
        return (uint16_t)(d[0] << 8 | d[1]);
    }
    return p->read16(p->user, a);
}

static uint32_t rd32(ppcd *p, uint32_t a)
{
    const ppcd_region *r = find(p, a);
    if (r && a + 3 <= r->end) return ld32(r->data + (a - r->start));
    return p->read32(p->user, a);
}

static uint64_t rd64(ppcd *p, uint32_t a)
{
    const ppcd_region *r = find(p, a);
    if (r && a + 7 <= r->end) {
        const uint8_t *d = r->data + (a - r->start);
        return (uint64_t)ld32(d) << 32 | ld32(d + 4);
    }
    const uint32_t hi = p->read32(p->user, a);
    return (uint64_t)hi << 32 | p->read32(p->user, a + 4);
}

static void wr8(ppcd *p, uint32_t a, uint8_t v)
{
    const ppcd_region *r = find(p, a);
    if (r) {
        if (r->writable) r->data[a - r->start] = v;
        return;
    }
    p->write8(p->user, a, v);
}

static void wr16(ppcd *p, uint32_t a, uint16_t v)
{
    const ppcd_region *r = find(p, a);
    if (r && a + 1 <= r->end) {
        if (r->writable) {
            uint8_t *d = r->data + (a - r->start);
            d[0] = (uint8_t)(v >> 8);
            d[1] = (uint8_t)v;
        }
        return;
    }
    p->write16(p->user, a, v);
}

static void wr32(ppcd *p, uint32_t a, uint32_t v)
{
    const ppcd_region *r = find(p, a);
    if (r && a + 3 <= r->end) {
        if (r->writable) st32(r->data + (a - r->start), v);
        return;
    }
    p->write32(p->user, a, v);
}

static void wr64(ppcd *p, uint32_t a, uint64_t v)
{
    const ppcd_region *r = find(p, a);
    if (r && a + 7 <= r->end) {
        if (r->writable) {
            st32(r->data + (a - r->start), (uint32_t)(v >> 32));
            st32(r->data + (a - r->start) + 4, (uint32_t)v);
        }
        return;
    }
    p->write32(p->user, a, (uint32_t)(v >> 32));
    p->write32(p->user, a + 4, (uint32_t)v);
}

// ---------------------------------------------------------------------------
// SPRs, timers, exceptions

static inline void set_cr_field(ppcd *p, int field, uint32_t v)
{
    const int sh = (7 - field) * 4;
    p->cr = (p->cr & ~(0xfu << sh)) | ((v & 0xf) << sh);
}
static inline void update_cr0(ppcd *p, uint32_t res)
{
    uint32_t v = ((int32_t)res < 0) ? 8 : (res ? 4 : 2);
    if (p->xer_so) v |= 1;
    set_cr_field(p, 0, v);
}
static inline void update_cr1(ppcd *p) { set_cr_field(p, 1, p->fpscr >> 28); }

static uint32_t xer(const ppcd *p)
{
    return (p->xer_so ? 0x80000000u : 0) | (p->xer_ov ? 0x40000000u : 0) | (p->xer_ca ? 0x20000000u : 0) | (p->xer_bc & 0x7f);
}
static void set_xer(ppcd *p, uint32_t v)
{
    p->xer_so = (v & 0x80000000u) != 0;
    p->xer_ov = (v & 0x40000000u) != 0;
    p->xer_ca = (v & 0x20000000u) != 0;
    p->xer_bc = v & 0x7f;
}

static uint64_t read_tb(ppcd *p) { return p->tb_base + (p->total_cycles - p->tb_cycle) / (uint64_t)p->timer_div; }
static void write_tb(ppcd *p, uint64_t v) { p->tb_base = v; p->tb_cycle = p->total_cycles; }
static uint32_t read_dec(ppcd *p) { return p->dec_base - (uint32_t)((p->total_cycles - p->dec_cycle) / (uint64_t)p->timer_div); }
static void write_dec(ppcd *p, uint32_t v)
{
    p->dec_base = v;
    p->dec_cycle = p->total_cycles;
    if (!(v & 0x80000000u)) p->dec_fire_cycle = p->total_cycles + ((uint64_t)v + 1) * (uint64_t)p->timer_div;
    else p->dec_fire_cycle = ~0ull;
}

static uint32_t read_spr(ppcd *p, int spr)
{
    switch (spr) {
    case 1: return xer(p);
    case 8: return p->lr;
    case 9: return p->ctr;
    case 18: return p->dsisr;
    case 19: return p->dar;
    case 22: return read_dec(p);
    case 25: return p->sdr1;
    case 26: return p->srr0;
    case 27: return p->srr1;
    case 268: case 284: return (uint32_t)read_tb(p);
    case 269: case 285: return (uint32_t)(read_tb(p) >> 32);
    case 272: case 273: case 274: case 275: return p->sprg[spr - 272];
    case 282: return p->ear;
    case 287: return p->pvr;
    case 1008: return p->hid0;
    case 1009: return p->hid1;
    case 1010: return p->hid2;
    default:
        if (spr >= 528 && spr <= 543) return p->bat[spr - 528];
        if (spr >= 976 && spr <= 981) return p->spr603[spr - 976];
        return 0;
    }
}

static void write_spr(ppcd *p, int spr, uint32_t v)
{
    switch (spr) {
    case 1: set_xer(p, v); break;
    case 8: p->lr = v; break;
    case 9: p->ctr = v; break;
    case 18: p->dsisr = v; break;
    case 19: p->dar = v; break;
    case 22: write_dec(p, v); break;
    case 25: p->sdr1 = v; break;
    case 26: p->srr0 = v; break;
    case 27: p->srr1 = v; break;
    case 272: case 273: case 274: case 275: p->sprg[spr - 272] = v; break;
    case 282: p->ear = v; break;
    case 284: write_tb(p, (read_tb(p) & 0xffffffff00000000ull) | v); break;
    case 285: write_tb(p, (read_tb(p) & 0xffffffffull) | ((uint64_t)v << 32)); break;
    case 1008: p->hid0 = v; break;
    case 1009: p->hid1 = v; break;
    case 1010: p->hid2 = v; break;
    default:
        if (spr >= 528 && spr <= 543) p->bat[spr - 528] = v;
        else if (spr >= 976 && spr <= 981) p->spr603[spr - 976] = v;
        break;
    }
}

static void exception(ppcd *p, uint32_t vector, uint32_t srr0, uint32_t srr1_bits)
{
    p->srr0 = srr0;
    p->srr1 = (p->msr & 0x87c0ffffu) | srr1_bits;
    uint32_t m = p->msr & ~(0x8000u | 0x4000u | 0x2000u | 0x800u | 0x400u | 0x200u | 0x100u | 0x20u | 0x10u | 0x2u | 0x40000u | 0x1u);
    if (p->msr & MSR_ILE) m |= 1;
    p->msr = m;
    p->pc = ((p->msr & MSR_IP) ? 0xfff00000u : 0) | vector;
    p->reserve = false;
}

static void check_interrupts(ppcd *p)
{
    if (!(p->msr & MSR_EE)) return;
    if (p->irq_line) exception(p, 0x500, p->pc, 0);
    else if (p->dec_pending) {
        p->dec_pending = false;
        exception(p, 0x900, p->pc, 0);
    }
}

static bool fp_available(ppcd *p)
{
    if (p->msr & MSR_FP) return true;
    exception(p, 0x800, p->cur_pc, 0);
    return false;
}

static void illegal(ppcd *p) { exception(p, 0x700, p->cur_pc, 0x80000); }

static bool is_spin_loop(ppcd *p, uint32_t start, uint32_t branch)
{
    for (uint32_t a = start; a <= branch; a += 4) {
        const uint32_t op = fetch(p, a);
        switch (op >> 26) {
        case 10: case 11: case 21: case 28: case 29: case 32: case 34: case 40: case 42: break;
        case 16:
            if (((op >> 21) & 4) == 0) return false;
            break;
        case 18: break;
        case 31: {
            const uint32_t xo = (op >> 1) & 0x3ff;
            if (xo != 0 && xo != 32 && xo != 23 && xo != 87 && xo != 279 && xo != 28) return false;
            break;
        }
        default: return false;
        }
    }
    return true;
}

static void exec(ppcd *p, uint32_t op);

int ppcd_run(ppcd *p, int cycles)
{
    p->budget = cycles;
    p->stop = false;
    int done = 0;
    while (done < p->budget && !p->stop) {
        if (p->total_cycles >= p->dec_fire_cycle) {
            p->dec_fire_cycle = ~0ull;
            p->dec_pending = true;
        }
        if ((p->irq_line || p->dec_pending) && (p->msr & MSR_EE)) check_interrupts(p);
        p->cur_pc = p->pc;
        const uint32_t op = fetch(p, p->pc);
        p->pc += 4;
        exec(p, op);
        ++done;
        ++p->total_cycles;
        bool spin = false;
        if (p->pc < p->cur_pc && p->cur_pc - p->pc <= 32 && p->spin_detect) {
            if (p->pc != p->spin_target || p->cur_pc != p->spin_branch) {
                p->spin_target = p->pc;
                p->spin_branch = p->cur_pc;
                p->spin_ok = is_spin_loop(p, p->pc, p->cur_pc);
            }
            spin = p->spin_ok;
        }
        if ((spin || p->pc == p->idle_pc) && !p->irq_line && !p->dec_pending) {
            uint64_t skip = (uint64_t)(p->budget - done);
            if (p->dec_fire_cycle != ~0ull && p->dec_fire_cycle > p->total_cycles && p->dec_fire_cycle - p->total_cycles < skip)
                skip = p->dec_fire_cycle - p->total_cycles;
            p->total_cycles += skip;
            done += (int)skip;
        }
    }
    return done;
}

// ---------------------------------------------------------------------------
// FPU helpers

static inline double fd(const ppcd *p, int n) { double d; memcpy(&d, &p->f[n], 8); return d; }
static inline void set_fd(ppcd *p, int n, double d) { memcpy(&p->f[n], &d, 8); }
static inline void set_fs(ppcd *p, int n, double d) { set_fd(p, n, (double)(float)d); }

static void set_fprf(ppcd *p, double d)
{
    uint32_t c;
    if (isnan(d)) c = 0x11;
    else if (isinf(d)) c = d > 0 ? 0x05 : 0x09;
    else if (d == 0) c = signbit(d) ? 0x12 : 0x02;
    else if (fpclassify(d) == FP_SUBNORMAL) c = d > 0 ? 0x14 : 0x18;
    else c = d > 0 ? 0x04 : 0x08;
    p->fpscr = (p->fpscr & ~0x1f000u) | (c << 12);
}

#define RD ((int)((op >> 21) & 31))
#define RS RD
#define RA ((int)((op >> 16) & 31))
#define RB ((int)((op >> 11) & 31))
#define RC ((int)((op >> 6) & 31))
#define SIMM ((int32_t)(int16_t)(op & 0xffff))
#define UIMM (op & 0xffff)
#define RCBIT (op & 1)
#define OEBIT ((op >> 10) & 1)
#define R p->r
#define EA_D ((uint32_t)((RA ? R[RA] : 0) + (uint32_t)SIMM))
#define EA_X ((uint32_t)((RA ? R[RA] : 0) + R[RB]))

static void exec19(ppcd *p, uint32_t op)
{
    const uint32_t xo = (op >> 1) & 0x3ff;
#define CRBIT(b) ((p->cr >> (31 - (b))) & 1)
#define SETBIT(b, v) do { const uint32_t m_ = 1u << (31 - (b)); p->cr = (v) ? (p->cr | m_) : (p->cr & ~m_); } while (0)
    const int d = RD, a = RA, b = RB;
    switch (xo) {
    case 0: set_cr_field(p, (op >> 23) & 7, (p->cr >> ((7 - ((op >> 18) & 7)) * 4)) & 0xf); break;
    case 16: case 528: {
        const uint32_t bo = (uint32_t)RD, bi = (uint32_t)RA;
        bool ctr_ok = true;
        if (xo == 16) {
            if (!(bo & 4)) --p->ctr;
            ctr_ok = (bo & 4) || ((p->ctr != 0) != ((bo & 2) != 0));
        }
        const bool cond_ok = (bo & 16) || (((p->cr >> (31 - bi)) & 1) == ((bo >> 3) & 1));
        const uint32_t target = (xo == 16 ? p->lr : p->ctr) & ~3u;
        if (op & 1) p->lr = p->cur_pc + 4;
        if (ctr_ok && cond_ok) p->pc = target;
        break;
    }
    case 33: SETBIT(d, !(CRBIT(a) | CRBIT(b))); break;
    case 129: SETBIT(d, CRBIT(a) & !CRBIT(b)); break;
    case 193: SETBIT(d, CRBIT(a) ^ CRBIT(b)); break;
    case 225: SETBIT(d, !(CRBIT(a) & CRBIT(b))); break;
    case 257: SETBIT(d, CRBIT(a) & CRBIT(b)); break;
    case 289: SETBIT(d, !(CRBIT(a) ^ CRBIT(b))); break;
    case 417: SETBIT(d, CRBIT(a) | !CRBIT(b)); break;
    case 449: SETBIT(d, CRBIT(a) | CRBIT(b)); break;
    case 50:
        p->msr = (p->msr & ~0x87c0ff73u) | (p->srr1 & 0x87c0ff73u);
        p->pc = p->srr0 & ~3u;
        break;
    case 150: break;
    default: illegal(p); break;
    }
#undef CRBIT
#undef SETBIT
}

static inline bool add_ov(uint32_t x, uint32_t y, uint32_t res) { return ((x ^ res) & (y ^ res) & 0x80000000u) != 0; }

static void exec31(ppcd *p, uint32_t op)
{
    const uint32_t xo9 = (op >> 1) & 0x1ff;
    switch (xo9) {
    case 8: case 10: case 11: case 40: case 75: case 104: case 136: case 138:
    case 200: case 202: case 232: case 234: case 235: case 266: case 459: case 491: {
        const uint32_t a = R[RA], b = R[RB];
        uint32_t r = 0;
        bool ov = false;
        uint64_t s;
        switch (xo9) {
        case 266: r = a + b; ov = add_ov(a, b, r); break;
        case 10: r = a + b; p->xer_ca = r < a; ov = add_ov(a, b, r); break;
        case 138: s = (uint64_t)a + b + (p->xer_ca ? 1 : 0); r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(a, b, r); break;
        case 234: s = (uint64_t)a + 0xffffffffull + (p->xer_ca ? 1 : 0); r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(a, 0xffffffffu, r); break;
        case 202: s = (uint64_t)a + (p->xer_ca ? 1 : 0); r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(a, 0, r); break;
        case 40: r = b - a; ov = add_ov(~a, b, r); break;
        case 8: s = (uint64_t)(~a) + b + 1; r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(~a, b, r); break;
        case 136: s = (uint64_t)(~a) + b + (p->xer_ca ? 1 : 0); r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(~a, b, r); break;
        case 232: s = (uint64_t)(~a) + 0xffffffffull + (p->xer_ca ? 1 : 0); r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(~a, 0xffffffffu, r); break;
        case 200: s = (uint64_t)(~a) + (p->xer_ca ? 1 : 0); r = (uint32_t)s; p->xer_ca = (s >> 32) != 0; ov = add_ov(~a, 0, r); break;
        case 104: r = (uint32_t)(-(int64_t)(int32_t)a); ov = a == 0x80000000u; break;
        case 235: {
            const int64_t pr = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
            r = (uint32_t)pr;
            ov = pr != (int64_t)(int32_t)r;
            break;
        }
        case 75: r = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32); break;
        case 11: r = (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32); break;
        case 491:
            if (b == 0 || (a == 0x80000000u && b == 0xffffffffu)) {
                r = ((int32_t)a < 0 && b == 0) ? 0xffffffffu : 0;
                ov = true;
            } else r = (uint32_t)((int32_t)a / (int32_t)b);
            break;
        case 459:
            if (b == 0) { r = 0; ov = true; }
            else r = a / b;
            break;
        }
        R[RD] = r;
        if (OEBIT && xo9 != 75 && xo9 != 11) {
            p->xer_ov = ov;
            if (ov) p->xer_so = true;
        }
        if (RCBIT) update_cr0(p, r);
        return;
    }
    default: break;
    }

    const uint32_t xo = (op >> 1) & 0x3ff;
    switch (xo) {
    case 0: {
        const int32_t a = (int32_t)R[RA], b = (int32_t)R[RB];
        set_cr_field(p, (op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (p->xer_so ? 1 : 0));
        break;
    }
    case 32: {
        const uint32_t a = R[RA], b = R[RB];
        set_cr_field(p, (op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (p->xer_so ? 1 : 0));
        break;
    }
    case 4: {
        const int32_t a = (int32_t)R[RA], b = (int32_t)R[RB];
        const uint32_t to = (uint32_t)RD;
        if (((to & 16) && a < b) || ((to & 8) && a > b) || ((to & 4) && a == b) || ((to & 2) && (uint32_t)a < (uint32_t)b) ||
            ((to & 1) && (uint32_t)a > (uint32_t)b))
            exception(p, 0x700, p->cur_pc, 0x20000);
        break;
    }
    case 19: R[RD] = p->cr; break;
    case 20:
        p->reserve = true;
        p->reserve_addr = EA_X;
        R[RD] = rd32(p, p->reserve_addr);
        break;
    case 150:
        if (p->reserve) {
            wr32(p, EA_X, R[RS]);
            set_cr_field(p, 0, 2 | (p->xer_so ? 1 : 0));
        } else set_cr_field(p, 0, p->xer_so ? 1 : 0);
        p->reserve = false;
        break;
    case 23: R[RD] = rd32(p, EA_X); break;
    case 55: { const uint32_t ea = EA_X; R[RD] = rd32(p, ea); R[RA] = ea; break; }
    case 87: R[RD] = rd8(p, EA_X); break;
    case 119: { const uint32_t ea = EA_X; R[RD] = rd8(p, ea); R[RA] = ea; break; }
    case 279: R[RD] = rd16(p, EA_X); break;
    case 311: { const uint32_t ea = EA_X; R[RD] = rd16(p, ea); R[RA] = ea; break; }
    case 343: R[RD] = (uint32_t)(int32_t)(int16_t)rd16(p, EA_X); break;
    case 375: { const uint32_t ea = EA_X; R[RD] = (uint32_t)(int32_t)(int16_t)rd16(p, ea); R[RA] = ea; break; }
    case 151: wr32(p, EA_X, R[RS]); break;
    case 183: { const uint32_t ea = EA_X; wr32(p, ea, R[RS]); R[RA] = ea; break; }
    case 215: wr8(p, EA_X, (uint8_t)R[RS]); break;
    case 247: { const uint32_t ea = EA_X; wr8(p, ea, (uint8_t)R[RS]); R[RA] = ea; break; }
    case 407: wr16(p, EA_X, (uint16_t)R[RS]); break;
    case 439: { const uint32_t ea = EA_X; wr16(p, ea, (uint16_t)R[RS]); R[RA] = ea; break; }
    case 534: R[RD] = __builtin_bswap32(rd32(p, EA_X)); break;
    case 790: R[RD] = __builtin_bswap16(rd16(p, EA_X)); break;
    case 662: wr32(p, EA_X, __builtin_bswap32(R[RS])); break;
    case 918: wr16(p, EA_X, __builtin_bswap16((uint16_t)R[RS])); break;
    case 24: {
        const uint32_t n = R[RB] & 63;
        R[RA] = n >= 32 ? 0 : R[RS] << n;
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    }
    case 536: {
        const uint32_t n = R[RB] & 63;
        R[RA] = n >= 32 ? 0 : R[RS] >> n;
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    }
    case 792: {
        const uint32_t n = R[RB] & 63;
        const int32_t s = (int32_t)R[RS];
        if (n >= 32) {
            R[RA] = s < 0 ? 0xffffffffu : 0;
            p->xer_ca = s < 0;
        } else {
            R[RA] = (uint32_t)(s >> n);
            p->xer_ca = s < 0 && n && ((uint32_t)s & ((1u << n) - 1));
        }
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    }
    case 824: {
        const uint32_t n = (uint32_t)RB;
        const int32_t s = (int32_t)R[RS];
        R[RA] = (uint32_t)(s >> n);
        p->xer_ca = s < 0 && n && ((uint32_t)s & ((1u << n) - 1));
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    }
    case 26: R[RA] = R[RS] ? (uint32_t)__builtin_clz(R[RS]) : 32; if (RCBIT) update_cr0(p, R[RA]); break;
    case 28: R[RA] = R[RS] & R[RB]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 60: R[RA] = R[RS] & ~R[RB]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 124: R[RA] = ~(R[RS] | R[RB]); if (RCBIT) update_cr0(p, R[RA]); break;
    case 284: R[RA] = ~(R[RS] ^ R[RB]); if (RCBIT) update_cr0(p, R[RA]); break;
    case 316: R[RA] = R[RS] ^ R[RB]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 412: R[RA] = R[RS] | ~R[RB]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 444: R[RA] = R[RS] | R[RB]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 476: R[RA] = ~(R[RS] & R[RB]); if (RCBIT) update_cr0(p, R[RA]); break;
    case 922: R[RA] = (uint32_t)(int32_t)(int16_t)R[RS]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 954: R[RA] = (uint32_t)(int32_t)(int8_t)R[RS]; if (RCBIT) update_cr0(p, R[RA]); break;
    case 83: R[RD] = p->msr; break;
    case 146: p->msr = R[RS]; break;
    case 144: {
        const uint32_t crm = (op >> 12) & 0xff;
        uint32_t m = 0;
        for (int i = 0; i < 8; ++i)
            if (crm & (0x80 >> i)) m |= 0xf0000000u >> (i * 4);
        p->cr = (p->cr & ~m) | (R[RS] & m);
        break;
    }
    case 512:
        set_cr_field(p, (op >> 23) & 7, (p->xer_so ? 8 : 0) | (p->xer_ov ? 4 : 0) | (p->xer_ca ? 2 : 0));
        p->xer_so = p->xer_ov = p->xer_ca = false;
        break;
    case 339: case 371: R[RD] = read_spr(p, (int)(((op >> 16) & 31) | (((op >> 11) & 31) << 5))); break;
    case 467: write_spr(p, (int)(((op >> 16) & 31) | (((op >> 11) & 31) << 5)), R[RS]); break;
    case 595: R[RD] = p->sr[(op >> 16) & 15]; break;
    case 659: R[RD] = p->sr[R[RB] >> 28]; break;
    case 210: p->sr[(op >> 16) & 15] = R[RS]; break;
    case 242: p->sr[R[RB] >> 28] = R[RS]; break;
    case 1014: {
        const uint32_t ea = EA_X & ~31u;
        for (int i = 0; i < 32; i += 8) wr64(p, ea + (uint32_t)i, 0);
        break;
    }
    case 54: case 86: case 246: case 278: case 470: case 982:
    case 306: case 370: case 566: case 598: case 854:
    case 978: case 1010:
        break;
    case 310: R[RD] = rd32(p, EA_X); break;
    case 438: wr32(p, EA_X, R[RS]); break;
    case 597: case 533: {
        uint32_t ea = xo == 597 ? (RA ? R[RA] : 0) : EA_X;
        int n = xo == 597 ? (RB ? RB : 32) : (int)(p->xer_bc & 0x7f);
        int r = RD - 1, i = 4;
        while (n > 0) {
            if (i == 4) {
                r = (r + 1) & 31;
                R[r] = 0;
                i = 0;
            }
            R[r] |= (uint32_t)rd8(p, ea) << (24 - i * 8);
            ++ea;
            ++i;
            --n;
        }
        break;
    }
    case 725: case 661: {
        uint32_t ea = xo == 725 ? (RA ? R[RA] : 0) : EA_X;
        int n = xo == 725 ? (RB ? RB : 32) : (int)(p->xer_bc & 0x7f);
        int r = RS - 1, i = 4;
        while (n > 0) {
            if (i == 4) {
                r = (r + 1) & 31;
                i = 0;
            }
            wr8(p, ea, (uint8_t)(R[r] >> (24 - i * 8)));
            ++ea;
            ++i;
            --n;
        }
        break;
    }
    case 535: case 567: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_X, bits = rd32(p, ea);
        float f;
        memcpy(&f, &bits, 4);
        set_fd(p, RD, (double)f);
        if (xo == 567) R[RA] = ea;
        break;
    }
    case 599: case 631: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_X;
        p->f[RD] = rd64(p, ea);
        if (xo == 631) R[RA] = ea;
        break;
    }
    case 663: case 695: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_X;
        const float f = (float)fd(p, RS);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        wr32(p, ea, bits);
        if (xo == 695) R[RA] = ea;
        break;
    }
    case 727: case 759: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_X;
        wr64(p, ea, p->f[RS]);
        if (xo == 759) R[RA] = ea;
        break;
    }
    case 983:
        if (!fp_available(p)) break;
        wr32(p, EA_X, (uint32_t)p->f[RS]);
        break;
    default: illegal(p); break;
    }
}

static void exec59(ppcd *p, uint32_t op)
{
    if (!fp_available(p)) return;
    const double a = fd(p, RA), b = fd(p, RB), c = fd(p, RC);
    double r;
    switch ((op >> 1) & 31) {
    case 18: r = a / b; break;
    case 20: r = a - b; break;
    case 21: r = a + b; break;
    case 22: r = sqrt(b); break;
    case 24: r = 1.0 / b; break;
    case 25: r = a * c; break;
    case 28: r = fma(a, c, -b); break;
    case 29: r = fma(a, c, b); break;
    case 30: r = -fma(a, c, -b); break;
    case 31: r = -fma(a, c, b); break;
    default: illegal(p); return;
    }
    set_fs(p, RD, r);
    set_fprf(p, fd(p, RD));
    if (RCBIT) update_cr1(p);
}

static void exec63(ppcd *p, uint32_t op)
{
    if (!fp_available(p)) return;
    const uint32_t xo5 = (op >> 1) & 31;
    if (xo5 >= 16) {
        const double a = fd(p, RA), b = fd(p, RB), c = fd(p, RC);
        double r;
        switch (xo5) {
        case 18: r = a / b; break;
        case 20: r = a - b; break;
        case 21: r = a + b; break;
        case 22: r = sqrt(b); break;
        case 23: r = (a >= 0.0) ? c : b; break;
        case 25: r = a * c; break;
        case 26: r = 1.0 / sqrt(b); break;
        case 28: r = fma(a, c, -b); break;
        case 29: r = fma(a, c, b); break;
        case 30: r = -fma(a, c, -b); break;
        case 31: r = -fma(a, c, b); break;
        default: illegal(p); return;
        }
        set_fd(p, RD, r);
        if (xo5 != 23) set_fprf(p, r);
        if (RCBIT) update_cr1(p);
        return;
    }
    const uint32_t xo = (op >> 1) & 0x3ff;
    switch (xo) {
    case 0: case 32: {
        const double a = fd(p, RA), b = fd(p, RB);
        uint32_t c;
        if (isnan(a) || isnan(b)) c = 1;
        else if (a < b) c = 8;
        else if (a > b) c = 4;
        else c = 2;
        set_cr_field(p, (op >> 23) & 7, c);
        p->fpscr = (p->fpscr & ~0xf000u) | (c << 12);
        break;
    }
    case 12:
        set_fs(p, RD, fd(p, RB));
        set_fprf(p, fd(p, RD));
        if (RCBIT) update_cr1(p);
        break;
    case 14: case 15: {
        const double b = fd(p, RB);
        int32_t v;
        if (isnan(b)) v = INT32_MIN;
        else {
            double t;
            const int mode = xo == 15 ? 1 : (int)(p->fpscr & 3);
            switch (mode) {
            case 0: t = nearbyint(b); break;
            case 1: t = trunc(b); break;
            case 2: t = ceil(b); break;
            default: t = floor(b); break;
            }
            if (t > 2147483647.0) v = INT32_MAX;
            else if (t < -2147483648.0) v = INT32_MIN;
            else v = (int32_t)t;
        }
        p->f[RD] = 0xfff8000000000000ull | (uint32_t)v;
        if (RCBIT) update_cr1(p);
        break;
    }
    case 38: p->fpscr |= 0x80000000u >> RD; if (RCBIT) update_cr1(p); break;
    case 70: p->fpscr &= ~(0x80000000u >> RD); if (RCBIT) update_cr1(p); break;
    case 40: p->f[RD] = p->f[RB] ^ 0x8000000000000000ull; if (RCBIT) update_cr1(p); break;
    case 72: p->f[RD] = p->f[RB]; if (RCBIT) update_cr1(p); break;
    case 136: p->f[RD] = p->f[RB] | 0x8000000000000000ull; if (RCBIT) update_cr1(p); break;
    case 264: p->f[RD] = p->f[RB] & ~0x8000000000000000ull; if (RCBIT) update_cr1(p); break;
    case 64: {
        const int s = (int)((op >> 18) & 7);
        set_cr_field(p, (op >> 23) & 7, (p->fpscr >> ((7 - s) * 4)) & 0xf);
        p->fpscr &= ~((0xfu << ((7 - s) * 4)) & 0x9ff80700u);
        break;
    }
    case 134: {
        const int field = (int)((op >> 23) & 7);
        const uint32_t imm = (op >> 12) & 0xf;
        const int sh = (7 - field) * 4;
        p->fpscr = (p->fpscr & ~(0xfu << sh)) | (imm << sh);
        if (RCBIT) update_cr1(p);
        break;
    }
    case 583: p->f[RD] = 0xfff8000000000000ull | p->fpscr; if (RCBIT) update_cr1(p); break;
    case 711: {
        const uint32_t fm = (op >> 17) & 0xff;
        uint32_t m = 0;
        for (int i = 0; i < 8; ++i)
            if (fm & (0x80 >> i)) m |= 0xf0000000u >> (i * 4);
        p->fpscr = (p->fpscr & ~m) | ((uint32_t)p->f[RB] & m);
        if (RCBIT) update_cr1(p);
        break;
    }
    default: illegal(p); break;
    }
}

static void exec(ppcd *p, uint32_t op)
{
    switch (op >> 26) {
    case 3: {
        const int32_t a = (int32_t)R[RA], b = SIMM;
        const uint32_t to = (uint32_t)RD;
        if (((to & 16) && a < b) || ((to & 8) && a > b) || ((to & 4) && a == b) || ((to & 2) && (uint32_t)a < (uint32_t)b) ||
            ((to & 1) && (uint32_t)a > (uint32_t)b))
            exception(p, 0x700, p->cur_pc, 0x20000);
        break;
    }
    case 7: R[RD] = (uint32_t)((int32_t)R[RA] * SIMM); break;
    case 8: {
        const uint32_t a = R[RA], b = (uint32_t)SIMM;
        R[RD] = b - a;
        p->xer_ca = (((uint64_t)(~a) + b + 1) >> 32) != 0;
        break;
    }
    case 10: {
        const uint32_t a = R[RA], b = UIMM;
        set_cr_field(p, (op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (p->xer_so ? 1 : 0));
        break;
    }
    case 11: {
        const int32_t a = (int32_t)R[RA], b = SIMM;
        set_cr_field(p, (op >> 23) & 7, (a < b ? 8 : a > b ? 4 : 2) | (p->xer_so ? 1 : 0));
        break;
    }
    case 12: case 13: {
        const uint32_t a = R[RA], b = (uint32_t)SIMM, r = a + b;
        p->xer_ca = r < a;
        R[RD] = r;
        if (op >> 26 == 13) update_cr0(p, r);
        break;
    }
    case 14: R[RD] = (RA ? R[RA] : 0) + (uint32_t)SIMM; break;
    case 15: R[RD] = (RA ? R[RA] : 0) + ((uint32_t)UIMM << 16); break;
    case 16: {
        const uint32_t bo = (uint32_t)RD, bi = (uint32_t)RA;
        if (!(bo & 4)) --p->ctr;
        const bool ctr_ok = (bo & 4) || ((p->ctr != 0) != ((bo & 2) != 0));
        const bool cond_ok = (bo & 16) || (((p->cr >> (31 - bi)) & 1) == ((bo >> 3) & 1));
        const uint32_t target = (uint32_t)(int32_t)(int16_t)(op & 0xfffc) + ((op & 2) ? 0 : p->cur_pc);
        if (op & 1) p->lr = p->cur_pc + 4;
        if (ctr_ok && cond_ok) p->pc = target;
        break;
    }
    case 17: exception(p, 0xc00, p->pc, 0); break;
    case 18: {
        int32_t li = (int32_t)(op & 0x03fffffc);
        if (li & 0x02000000) li -= 0x04000000;
        if (op & 1) p->lr = p->cur_pc + 4;
        p->pc = (uint32_t)li + ((op & 2) ? 0 : p->cur_pc);
        break;
    }
    case 19: exec19(p, op); break;
    case 20: {
        const uint32_t m = mask((op >> 6) & 31, (op >> 1) & 31);
        R[RA] = (rotl(R[RS], RB) & m) | (R[RA] & ~m);
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    }
    case 21:
        R[RA] = rotl(R[RS], RB) & mask((op >> 6) & 31, (op >> 1) & 31);
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    case 23:
        R[RA] = rotl(R[RS], (int)(R[RB] & 31)) & mask((op >> 6) & 31, (op >> 1) & 31);
        if (RCBIT) update_cr0(p, R[RA]);
        break;
    case 24: R[RA] = R[RS] | UIMM; break;
    case 25: R[RA] = R[RS] | (UIMM << 16); break;
    case 26: R[RA] = R[RS] ^ UIMM; break;
    case 27: R[RA] = R[RS] ^ (UIMM << 16); break;
    case 28: R[RA] = R[RS] & UIMM; update_cr0(p, R[RA]); break;
    case 29: R[RA] = R[RS] & (UIMM << 16); update_cr0(p, R[RA]); break;
    case 31: exec31(p, op); break;
    case 32: R[RD] = rd32(p, EA_D); break;
    case 33: { const uint32_t ea = EA_D; R[RD] = rd32(p, ea); R[RA] = ea; break; }
    case 34: R[RD] = rd8(p, EA_D); break;
    case 35: { const uint32_t ea = EA_D; R[RD] = rd8(p, ea); R[RA] = ea; break; }
    case 36: wr32(p, EA_D, R[RS]); break;
    case 37: { const uint32_t ea = EA_D; wr32(p, ea, R[RS]); R[RA] = ea; break; }
    case 38: wr8(p, EA_D, (uint8_t)R[RS]); break;
    case 39: { const uint32_t ea = EA_D; wr8(p, ea, (uint8_t)R[RS]); R[RA] = ea; break; }
    case 40: R[RD] = rd16(p, EA_D); break;
    case 41: { const uint32_t ea = EA_D; R[RD] = rd16(p, ea); R[RA] = ea; break; }
    case 42: R[RD] = (uint32_t)(int32_t)(int16_t)rd16(p, EA_D); break;
    case 43: { const uint32_t ea = EA_D; R[RD] = (uint32_t)(int32_t)(int16_t)rd16(p, ea); R[RA] = ea; break; }
    case 44: wr16(p, EA_D, (uint16_t)R[RS]); break;
    case 45: { const uint32_t ea = EA_D; wr16(p, ea, (uint16_t)R[RS]); R[RA] = ea; break; }
    case 46: {
        uint32_t ea = EA_D;
        for (int r = RD; r < 32; ++r, ea += 4) R[r] = rd32(p, ea);
        break;
    }
    case 47: {
        uint32_t ea = EA_D;
        for (int r = RS; r < 32; ++r, ea += 4) wr32(p, ea, R[r]);
        break;
    }
    case 48: case 49: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_D, bits = rd32(p, ea);
        float f;
        memcpy(&f, &bits, 4);
        set_fd(p, RD, (double)f);
        if (op >> 26 == 49) R[RA] = ea;
        break;
    }
    case 50: case 51: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_D;
        p->f[RD] = rd64(p, ea);
        if (op >> 26 == 51) R[RA] = ea;
        break;
    }
    case 52: case 53: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_D;
        const float f = (float)fd(p, RS);
        uint32_t bits;
        memcpy(&bits, &f, 4);
        wr32(p, ea, bits);
        if (op >> 26 == 53) R[RA] = ea;
        break;
    }
    case 54: case 55: {
        if (!fp_available(p)) break;
        const uint32_t ea = EA_D;
        wr64(p, ea, p->f[RS]);
        if (op >> 26 == 55) R[RA] = ea;
        break;
    }
    case 59: exec59(p, op); break;
    case 63: exec63(p, op); break;
    default: illegal(p); break;
    }
}
