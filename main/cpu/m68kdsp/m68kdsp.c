// m68kdsp.c - see m68kdsp.h. Translated from dsp-cpp's cpu/m68000.cpp (from
// m68000.pas): its members and methods are mapped onto the struct by the
// macros below; the memory access, reset and the handlers written here.
#include <string.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Warray-bounds"   // (d[dir & 7] seen through inlined dead paths)
#endif
#include "m68kdsp.h"



// Cycle helpers from m68000.pas.
static int calc_ea_t_bw(uint8_t dir) {
    if (dir <= 0x0f) return 0;
    if (dir <= 0x1f) return 4;
    if (dir <= 0x27) return 6;
    if (dir <= 0x2f) return 8;
    if (dir <= 0x37) return 10;
    switch (dir) {
        case 0x38: return 8;
        case 0x39: return 12;
        case 0x3a: return 8;
        case 0x3b: return 10;
        case 0x3c: return 4;
        default: return 0;
    }
}

static int calc_ea_t_l(uint8_t dir) {
    if (dir <= 0x0f) return 0;
    if (dir <= 0x1f) return 8;
    if (dir <= 0x27) return 10;
    if (dir <= 0x2f) return 12;
    if (dir <= 0x37) return 14;
    switch (dir) {
        case 0x38: return 12;
        case 0x39: return 16;
        case 0x3a: return 12;
        case 0x3b: return 14;
        case 0x3c: return 8;
        default: return 0;
    }
}

static int calc_move_t(uint8_t dir, uint8_t dest, bool long_size) {
    static const uint8_t bw[6][8] = {
        {4, 4, 8, 8, 8, 12, 14, 12},     {8, 8, 12, 12, 12, 16, 18, 16},
        {10, 10, 14, 14, 14, 18, 20, 18}, {12, 12, 16, 16, 16, 20, 22, 20},
        {14, 14, 18, 18, 18, 22, 24, 22}, {16, 16, 20, 20, 20, 24, 26, 24},
    };
    static const uint8_t lw[6][8] = {
        {4, 4, 12, 12, 12, 16, 18, 16},   {12, 12, 20, 20, 20, 24, 26, 24},
        {14, 14, 22, 22, 22, 26, 28, 26}, {16, 16, 24, 24, 24, 28, 30, 28},
        {18, 18, 26, 26, 26, 30, 32, 30}, {20, 20, 28, 28, 28, 32, 34, 32},
    };
    int row = 0;
    if (dir <= 0x0f) row = 0;
    else if (dir <= 0x1f || dir == 0x3c) row = 1;
    else if (dir <= 0x27) row = 2;
    else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) row = 3;
    else if (dir <= 0x37 || dir == 0x3b) row = 4;
    else if (dir == 0x39) row = 5;
    int res = long_size ? lw[row][dest >> 3] : bw[row][dest >> 3];
    if (dest == 39) res += 4;
    return res;
}

static const uint8_t kShift8[65] = {
    0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff};

static uint16_t shift16(int count) {
    if (count <= 0) return 0;
    if (count >= 16) return 0xffff;
    return (uint16_t)(0xffffu << (16 - count));
}

static uint32_t shift32(int count) {
    if (count <= 0) return 0;
    if (count >= 32) return 0xffffffffu;
    return (uint32_t)(0xffffffffu << (32 - count));
}


#define R_SET_L0(r, v) ((r).l = ((r).l & 0xffffff00u) | (uint8_t)(v))
#define R_SET_WL(r, v) ((r).l = ((r).l & 0xffff0000u) | (uint16_t)(v))
#define R_SET_WH(r, v) ((r).l = ((r).l & 0x0000ffffu) | ((uint32_t)(uint16_t)(v) << 16))
#define K_DIRECT_SPAN (M68KD_DIRECT_PAGES << 12)

static uint16_t m68_getword(m68kd *mk, uint32_t address)
{
    address &= mk->address_mask;
    if (address < K_DIRECT_SPAN) {
        const uint8_t *page = mk->direct_read[address >> 12];
        if (page) {
            const uint32_t o = address & 0xfff;
            const uint8_t *odd = mk->direct_odd[address >> 12];
            if (odd) return (uint16_t)(((uint16_t)page[o >> 1] << 8) | odd[o >> 1]);
            return (uint16_t)(((uint16_t)page[o] << 8) | page[o + 1]);
        }
    }
    return mk->read_word(mk->user, address);
}

static void m68_putword(m68kd *mk, uint32_t address, uint16_t value)
{
    address &= mk->address_mask;
    if (address < K_DIRECT_SPAN) {
        uint8_t *page = mk->direct_write[address >> 12];
        if (page) {
            const uint32_t o = address & 0xfff;
            page[o] = (uint8_t)(value >> 8);
            page[o + 1] = (uint8_t)value;
            return;
        }
    }
    mk->write_word(mk->user, address, value);
}

static uint8_t m68_getbyte(m68kd *mk, uint32_t address)
{
    address &= (mk->address_mask | 1u);
    if (address < K_DIRECT_SPAN) {
        const uint8_t *page = mk->direct_read[address >> 12];
        if (page) {
            const uint8_t *odd = mk->direct_odd[address >> 12];
            if (odd) return ((address & 1) ? odd : page)[(address & 0xfff) >> 1];
            return page[address & 0xfff];
        }
    }
    if (mk->read_byte) return mk->read_byte(mk->user, address);
    const uint16_t v = m68_getword(mk, address);
    return (address & 1) ? (uint8_t)(v & 0xff) : (uint8_t)(v >> 8);
}

static void m68_putbyte(m68kd *mk, uint32_t address, uint8_t value)
{
    address &= (mk->address_mask | 1u);
    if (address < K_DIRECT_SPAN) {
        uint8_t *page = mk->direct_write[address >> 12];
        if (page) {
            page[address & 0xfff] = value;
            return;
        }
    }
    if (mk->write_byte) {
        mk->write_byte(mk->user, address, value);
        return;
    }
    const uint16_t old = m68_getword(mk, address);
    if (address & 1) m68_putword(mk, address, (uint16_t)((old & 0xff00) | value));
    else m68_putword(mk, address, (uint16_t)((old & 0x00ff) | ((uint16_t)value << 8)));
}

static uint32_t m68_read_cr(m68kd *mk, uint16_t c)
{
    switch (c) {
    case 0x800: case 0x803: return mk->other_sp.l;
    case 0x801: return mk->vbr;
    case 0x804: return mk->a[7].l;
    default: return 0;
    }
}

static void m68_write_cr(m68kd *mk, uint16_t c, uint32_t v)
{
    switch (c) {
    case 0x800: case 0x803: mk->other_sp.l = v; break;
    case 0x801: mk->vbr = v & ~3u; break;
    case 0x804: mk->a[7].l = v; break;
    default: break;
    }
}

void m68kd_init(m68kd *mk, uint32_t clock, int type)
{
    mk->clock = clock;
    mk->type = (uint8_t)type;
    mk->address_mask = 0xfffffeu;
    memset(mk->irq, M68_CLEAR, sizeof mk->irq);
}

void m68kd_set_irq(m68kd *mk, int level, int state)
{
    if (level < 0 || level >= 8) return;
    mk->irq[level] = (uint8_t)state;
    if (state == M68_CLEAR) mk->irq_lines = (uint8_t)(mk->irq_lines & ~(1u << level));
    else mk->irq_lines = (uint8_t)(mk->irq_lines | (1u << level));
}

void m68kd_reset(m68kd *mk)
{
    memset(&mk->cc, 0, sizeof mk->cc);
    mk->cc.s = true;
    mk->cc.z = true;
    mk->cc.im = 7;
    mk->opcode = true;
    mk->other_sp.l = 0;
    R_SET_WH(mk->a[7], m68_getword(mk, 0));
    R_SET_WL(mk->a[7], m68_getword(mk, 2));
    R_SET_WH(mk->pc, m68_getword(mk, 4));
    R_SET_WL(mk->pc, m68_getword(mk, 6));
    mk->opcode = false;
    memset(mk->irq, M68_CLEAR, sizeof mk->irq);
    mk->irq_lines = 0;
    mk->halt_request = M68_CLEAR;
    mk->reset_request = M68_CLEAR;
    mk->halted = false;
    mk->stopped = false;
    mk->pq_count = 0;
}
static void m68_reset(m68kd *mk) { m68kd_reset(mk); }

void m68kd_set_direct_page(m68kd *mk, uint32_t page, const uint8_t *read, uint8_t *write)
{
    if (page >= M68KD_DIRECT_PAGES) return;
    mk->direct_read[page] = read;
    mk->direct_write[page] = write;
    mk->direct_odd[page] = NULL;
}

void m68kd_set_direct_page_pair(m68kd *mk, uint32_t page, const uint8_t *even, const uint8_t *odd)
{
    if (page >= M68KD_DIRECT_PAGES) return;
    mk->direct_read[page] = even;
    mk->direct_write[page] = NULL;
    mk->direct_odd[page] = even ? odd : NULL;
}

static uint16_t m68_fetch_word(m68kd *mk);
static uint32_t m68_fetch_long(m68kd *mk);
static uint16_t m68_prog_word(m68kd *mk, uint32_t address);
static uint16_t m68_get_flags(m68kd *mk);
static void m68_set_flags(m68kd *mk, uint16_t value);
static uint32_t m68_indexed_offset(m68kd *mk, uint32_t base);
static uint8_t m68_read_b(m68kd *mk, uint8_t dir);
static void m68_write_b2(m68kd *mk, uint8_t dir, uint8_t value);
static void m68_write_b(m68kd *mk, uint8_t dir, uint8_t value);
static uint16_t m68_read_w(m68kd *mk, uint8_t dir);
static void m68_write_w2(m68kd *mk, uint8_t dir, uint16_t value);
static void m68_write_w(m68kd *mk, uint8_t dir, uint16_t value);
static uint32_t m68_read_l(m68kd *mk, uint8_t dir);
static void m68_write_l2(m68kd *mk, uint8_t dir, uint32_t value);
static void m68_write_l(m68kd *mk, uint8_t dir, uint32_t value);
static uint32_t m68_read_ea(m68kd *mk, uint8_t dir);
static bool m68_condition(m68kd *mk, uint8_t code);
static void m68_exception(m68kd *mk, uint32_t vector, int cycles);
static bool m68_check_supervisor(m68kd *mk);
static void m68_group_0(m68kd *mk, uint16_t instruction);
static void m68_group_b(m68kd *mk, uint16_t instruction);
static void m68_group_c(m68kd *mk, uint16_t instruction);
static void m68_group_d(m68kd *mk, uint16_t instruction);
static void m68_group_e(m68kd *mk, uint16_t instruction);
static void m68_group_8(m68kd *mk, uint16_t instruction);
static void m68_group_9(m68kd *mk, uint16_t instruction);
static void m68_group_5(m68kd *mk, uint16_t instruction);
static void m68_group_6(m68kd *mk, uint16_t instruction);
static void m68_group_7(m68kd *mk, uint16_t instruction);
static void m68_move_bw(m68kd *mk, uint16_t instruction, bool byte_size);
static void m68_move_l(m68kd *mk, uint16_t instruction);
static void m68_group_1(m68kd *mk, uint16_t instruction);
static void m68_group_4(m68kd *mk, uint16_t instruction);
static void m68_group_2(m68kd *mk, uint16_t instruction);
static void m68_group_3(m68kd *mk, uint16_t instruction);
static void m68_group_a(m68kd *mk, uint16_t instruction);
static void m68_group_f(m68kd *mk, uint16_t instruction);
static bool m68_take_irq(m68kd *mk);
static int m68_run(m68kd *mk, int cycles);

#define d (mk->d)
#define a (mk->a)
#define cc (mk->cc)
#define cycles_ (mk->cycles_)
#define pc_ (mk->pc)
#define ppc_ (mk->ppc)
#define other_sp_ (mk->other_sp)
#define opcode_ (mk->opcode)
#define prefetch_ (mk->prefetch)
#define exceptions_ (mk->exceptions)
#define pq_addr_ (mk->pq_addr)
#define pq_val_ (mk->pq_val)
#define pq_count_ (mk->pq_count)
#define ea_ (mk->ea)
#define halted_ (mk->halted)
#define stopped_ (mk->stopped)
#define irq_ (mk->irq)
#define irq_lines_ (mk->irq_lines)
#define reset_request_ (mk->reset_request)
#define halt_request_ (mk->halt_request)
#define vbr_ (mk->vbr)
#define type_ (mk->type)
#define fetch_word() m68_fetch_word(mk)
#define fetch_long() m68_fetch_long(mk)
#define prog_word(...) m68_prog_word(mk, __VA_ARGS__)
#define get_flags() m68_get_flags(mk)
#define set_flags(...) m68_set_flags(mk, __VA_ARGS__)
#define indexed_offset(...) m68_indexed_offset(mk, __VA_ARGS__)
#define read_b(...) m68_read_b(mk, __VA_ARGS__)
#define write_b2(...) m68_write_b2(mk, __VA_ARGS__)
#define write_b(...) m68_write_b(mk, __VA_ARGS__)
#define read_w(...) m68_read_w(mk, __VA_ARGS__)
#define write_w2(...) m68_write_w2(mk, __VA_ARGS__)
#define write_w(...) m68_write_w(mk, __VA_ARGS__)
#define read_l(...) m68_read_l(mk, __VA_ARGS__)
#define write_l2(...) m68_write_l2(mk, __VA_ARGS__)
#define write_l(...) m68_write_l(mk, __VA_ARGS__)
#define read_ea(...) m68_read_ea(mk, __VA_ARGS__)
#define condition(...) m68_condition(mk, __VA_ARGS__)
#define exception(...) m68_exception(mk, __VA_ARGS__)
#define check_supervisor() m68_check_supervisor(mk)
#define group_0(...) m68_group_0(mk, __VA_ARGS__)
#define group_b(...) m68_group_b(mk, __VA_ARGS__)
#define group_c(...) m68_group_c(mk, __VA_ARGS__)
#define group_d(...) m68_group_d(mk, __VA_ARGS__)
#define group_e(...) m68_group_e(mk, __VA_ARGS__)
#define group_8(...) m68_group_8(mk, __VA_ARGS__)
#define group_9(...) m68_group_9(mk, __VA_ARGS__)
#define group_5(...) m68_group_5(mk, __VA_ARGS__)
#define group_6(...) m68_group_6(mk, __VA_ARGS__)
#define group_7(...) m68_group_7(mk, __VA_ARGS__)
#define move_bw(...) m68_move_bw(mk, __VA_ARGS__)
#define move_l(...) m68_move_l(mk, __VA_ARGS__)
#define group_1(...) m68_group_1(mk, __VA_ARGS__)
#define group_4(...) m68_group_4(mk, __VA_ARGS__)
#define group_2(...) m68_group_2(mk, __VA_ARGS__)
#define group_3(...) m68_group_3(mk, __VA_ARGS__)
#define group_a(...) m68_group_a(mk, __VA_ARGS__)
#define group_f(...) m68_group_f(mk, __VA_ARGS__)
#define take_irq() m68_take_irq(mk)
#define run(...) m68_run(mk, __VA_ARGS__)
#define reset() m68_reset(mk)
#define getword(...) m68_getword(mk, __VA_ARGS__)
#define putword(...) m68_putword(mk, __VA_ARGS__)
#define getbyte(...) m68_getbyte(mk, __VA_ARGS__)
#define putbyte(...) m68_putbyte(mk, __VA_ARGS__)







static uint16_t m68_fetch_word(m68kd *mk) {
    if (!prefetch_) {
        uint16_t value = prog_word(pc_.l);
        pc_.l += 2;
        return value;
    }
    const uint16_t value = prog_word(pc_.l);
    pc_.l += 2;
    // Refill the queue with the two words after the one just consumed,
    // keeping words that were already fetched.
    uint16_t next[2];
    for (int i = 0; i < 2; i++) next[i] = prog_word(pc_.l + (uint32_t)(i) * 2);
    pq_addr_ = pc_.l;
    pq_val_[0] = next[0];
    pq_val_[1] = next[1];
    pq_count_ = 2;
    return value;
}

static uint32_t m68_fetch_long(m68kd *mk) {
    if (prefetch_) {
        const uint32_t hi = fetch_word();
        return (hi << 16) | fetch_word();
    }
    uint32_t value = ((uint32_t)(prog_word(pc_.l)) << 16) | prog_word(pc_.l + 2);
    pc_.l += 4;
    return value;
}

static uint16_t m68_prog_word(m68kd *mk, uint32_t address) {
    if (prefetch_) {
        for (int i = 0; i < pq_count_; i++) {
            if (address == pq_addr_ + (uint32_t)(i) * 2) return pq_val_[i];
        }
    }
    // Instruction stream: opcode() is true only during these reads, so the
    // drivers with encrypted ROMs serve the decrypted opcodes here and the
    // data view to every other access (ABCD, MOVEM, stack...).
    opcode_ = true;
    const uint16_t value = getword(address);
    opcode_ = false;
    return value;
}

static uint16_t m68_get_flags(m68kd *mk) {
    uint16_t value = (uint16_t)(cc.t ? 0x8000 : 0);
    if (cc.s) value |= 0x2000;
    value |= (uint16_t)(cc.im << 8);
    if (cc.x) value |= 0x10;
    if (cc.n) value |= 0x08;
    if (cc.z) value |= 0x04;
    if (cc.v) value |= 0x02;
    if (cc.c) value |= 0x01;
    return value;
}

static void m68_set_flags(m68kd *mk, uint16_t value) {
    const bool supervisor = (value & 0x2000) != 0;
    if (cc.s != supervisor) {
        m68r current = a[7];
        a[7] = other_sp_;
        other_sp_ = current;
        cc.s = supervisor;
    }
    cc.t = (value & 0x8000) != 0;
    cc.im = (uint8_t)((value >> 8) & 7);
    cc.x = (value & 0x10) != 0;
    cc.n = (value & 0x08) != 0;
    cc.z = (value & 0x04) != 0;
    cc.v = (value & 0x02) != 0;
    cc.c = (value & 0x01) != 0;
}

static uint32_t m68_indexed_offset(m68kd *mk, uint32_t base) {
    const uint16_t extension = fetch_word();
    const size_t index = (size_t)((extension >> 12) & 7);
    const m68r reg = (extension & 0x8000) ? a[index] : d[index];
    // 68020+ full extension word format (bit 8 set): adds a base-register
    // suppress bit, an index-register suppress bit, a 2-bit scale factor
    // (x1/x2/x4/x8), a variable-size (0/16/32-bit) base displacement word
    // that follows in memory, and optional memory-indirection with an
    // outer displacement. A plain 68000/68010 core never sets bit 8 (their
    // brief-format extension word only has an 8-bit displacement in that
    // position), so gate this on CPU type to avoid changing already-working
    // brief-format decoding for those cores.
    if (type_ == M68K_T_68020 && (extension & 0x100) != 0) {
        const bool base_suppress = (extension & 0x80) != 0;
        const bool index_suppress = (extension & 0x40) != 0;
        const int scale = 1 << ((extension >> 9) & 3);
        const int bd_size = (extension >> 4) & 3;  // 0=reserved(treat as none),1=none,2=word,3=long
        int32_t base_disp = 0;
        if (bd_size == 2) base_disp = (int32_t)((int16_t)(fetch_word()));
        else if (bd_size == 3) base_disp = (int32_t)(fetch_long());
        int32_t index_val = 0;
        if (!index_suppress) {
            index_val = (extension & 0x800) ? (int32_t)(reg.l) : (int32_t)((int16_t)(((uint16_t)(reg).l)));
            index_val *= scale;
        }
        const uint32_t eff_base = base_suppress ? 0u : base;
        const int iis = extension & 7;
        if (iis == 0) {
            // No memory indirection: base + base_disp + scaled index.
            return (uint32_t)((int32_t)(eff_base) + base_disp + index_val);
        }
        // Memory-indirect (pre- or post-indexed) with an outer displacement.
        uint32_t intermediate_addr = (uint32_t)((int32_t)(eff_base) + base_disp);
        uint32_t intermediate;
        if (iis <= 4) {  // preindexed: index added before the indirection
            intermediate_addr = (uint32_t)((int32_t)(intermediate_addr) + index_val);
            intermediate = ((uint32_t)(getword(intermediate_addr)) << 16) | getword(intermediate_addr + 2);
        } else {  // postindexed: index added after the indirection
            intermediate = ((uint32_t)(getword(intermediate_addr)) << 16) | getword(intermediate_addr + 2);
            intermediate = (uint32_t)((int32_t)(intermediate) + index_val);
        }
        int32_t outer_disp = 0;
        const int od_size = iis & 3;
        if (od_size == 2) outer_disp = (int32_t)((int16_t)(fetch_word()));
        else if (od_size == 3) outer_disp = (int32_t)(fetch_long());
        return (uint32_t)((int32_t)(intermediate) + outer_disp);
    }
    const int8_t displacement = (int8_t)(extension & 0xff);
    const uint32_t value = (extension & 0x800) ? reg.l : (uint32_t)((int32_t)((int16_t)(((uint16_t)(reg).l))));
    return base + value + (uint32_t)((int32_t)(displacement));
}

static uint8_t m68_read_b(m68kd *mk, uint8_t dir) {
    if (dir <= 0x07) return ((uint8_t)(d[dir & 7]).l);
    if (dir <= 0x0f) return ((uint8_t)(a[dir & 7]).l);
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l = ea_ + ((dir & 7) == 7 ? 2 : 1);
    } else if (dir <= 0x27) {
        a[dir & 7].l -= ((dir & 7) == 7 ? 2 : 1);
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    } else if (dir == 0x3a) {
        ea_ = pc_.l + (uint32_t)((int32_t)((int16_t)(prog_word(pc_.l))));
        pc_.l += 2;
    } else if (dir == 0x3b) {
        ea_ = indexed_offset(pc_.l);
    } else if (dir == 0x3c) {
        ea_ = pc_.l;
        pc_.l += 2;
        return (uint8_t)(prog_word(ea_) & 0xff);
    }
    opcode_ = false;
    const uint8_t value = getbyte(ea_);
    return value;
}

static void m68_write_b2(m68kd *mk, uint8_t dir, uint8_t value) {
    if (dir <= 0x07) R_SET_L0(d[dir & 7], value);
    else if (dir <= 0x0f) R_SET_L0(a[dir & 7], value);
    else putbyte(ea_, value);
}

static void m68_write_b(m68kd *mk, uint8_t dir, uint8_t value) {
    if (dir <= 0x07) {
        R_SET_L0(d[dir & 7], value);
        return;
    }
    if (dir <= 0x0f) {
        R_SET_L0(a[dir & 7], value);
        return;
    }
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l += ((dir & 7) == 7 ? 2 : 1);
    } else if (dir <= 0x27) {
        a[dir & 7].l -= ((dir & 7) == 7 ? 2 : 1);
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    }
    putbyte(ea_, value);
}

static uint16_t m68_read_w(m68kd *mk, uint8_t dir) {
    if (dir <= 0x07) return ((uint16_t)(d[dir & 7]).l);
    if (dir <= 0x0f) return ((uint16_t)(a[dir & 7]).l);
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l = ea_ + 2;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 2;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    } else if (dir == 0x3a) {
        ea_ = pc_.l + (uint32_t)((int32_t)((int16_t)(prog_word(pc_.l))));
        pc_.l += 2;
    } else if (dir == 0x3b) {
        ea_ = indexed_offset(pc_.l);
    } else if (dir == 0x3c) {
        ea_ = pc_.l;
        pc_.l += 2;
        return prog_word(ea_);
    }
    opcode_ = false;
    const uint16_t value = getword(ea_);
    return value;
}

static void m68_write_w2(m68kd *mk, uint8_t dir, uint16_t value) {
    if (dir <= 0x07) R_SET_WL(d[dir & 7], value);
    else if (dir <= 0x0f) R_SET_WL(a[dir & 7], value);
    else putword(ea_, value);
}

static void m68_write_w(m68kd *mk, uint8_t dir, uint16_t value) {
    if (dir <= 0x07) {
        R_SET_WL(d[dir & 7], value);
        return;
    }
    if (dir <= 0x0f) {
        R_SET_WL(a[dir & 7], value);
        return;
    }
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l += 2;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 2;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    }
    putword(ea_, value);
}

static uint32_t m68_read_l(m68kd *mk, uint8_t dir) {
    if (dir <= 0x07) return d[dir & 7].l;
    if (dir <= 0x0f) return a[dir & 7].l;
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l += 4;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 4;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    } else if (dir == 0x3a) {
        ea_ = pc_.l + (uint32_t)((int32_t)((int16_t)(prog_word(pc_.l))));
        pc_.l += 2;
    } else if (dir == 0x3b) {
        ea_ = indexed_offset(pc_.l);
    } else if (dir == 0x3c) {
        ea_ = pc_.l;
        return fetch_long();
    }
    opcode_ = false;
    const uint32_t value = ((uint32_t)(getword(ea_)) << 16) | getword(ea_ + 2);
    return value;
}

static void m68_write_l2(m68kd *mk, uint8_t dir, uint32_t value) {
    if (dir <= 0x07) {
        d[dir & 7].l = value;
    } else if (dir <= 0x0f) {
        a[dir & 7].l = value;
    } else {
        putword(ea_, (uint16_t)(value >> 16));
        putword(ea_ + 2, (uint16_t)(value));
    }
}

static void m68_write_l(m68kd *mk, uint8_t dir, uint32_t value) {
    if (dir <= 0x07) {
        d[dir & 7].l = value;
        return;
    }
    if (dir <= 0x0f) {
        a[dir & 7].l = value;
        return;
    }
    if (dir <= 0x17) {
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x1f) {
        ea_ = a[dir & 7].l;
        a[dir & 7].l = ea_ + 4;
    } else if (dir <= 0x27) {
        a[dir & 7].l -= 4;
        ea_ = a[dir & 7].l;
    } else if (dir <= 0x2f) {
        ea_ = a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir <= 0x37) {
        ea_ = indexed_offset(a[dir & 7].l);
    } else if (dir == 0x38) {
        ea_ = (uint32_t)((int32_t)((int16_t)(fetch_word())));
    } else if (dir == 0x39) {
        ea_ = fetch_long();
    }
    putword(ea_, (uint16_t)(value >> 16));
    putword(ea_ + 2, (uint16_t)(value));
}

static uint32_t m68_read_ea(m68kd *mk, uint8_t dir) {
    if (dir >= 0x10 && dir <= 0x27) return a[dir & 7].l;
    if (dir >= 0x28 && dir <= 0x2f) {
        return a[dir & 7].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
    }
    if (dir >= 0x30 && dir <= 0x37) return indexed_offset(a[dir & 7].l);
    switch (dir) {
        case 0x38: return (uint32_t)((int32_t)((int16_t)(fetch_word())));
        case 0x39: return fetch_long();
        case 0x3a: {
            const uint32_t base = pc_.l;
            return base + (uint32_t)((int32_t)((int16_t)(fetch_word())));
        }
        case 0x3b: return indexed_offset(pc_.l);
        default: return 0;
    }
}

static bool m68_condition(m68kd *mk, uint8_t code) {
    switch (code & 0x0f) {
        case 0x00: return true;
        case 0x01: return false;
        case 0x02: return !cc.c && !cc.z;
        case 0x03: return cc.c || cc.z;
        case 0x04: return !cc.c;
        case 0x05: return cc.c;
        case 0x06: return !cc.z;
        case 0x07: return cc.z;
        case 0x08: return !cc.v;
        case 0x09: return cc.v;
        case 0x0a: return !cc.n;
        case 0x0b: return cc.n;
        case 0x0c: return cc.n == cc.v;
        case 0x0d: return cc.n != cc.v;
        case 0x0e: return (cc.n == cc.v) && !cc.z;
        default: return (cc.n != cc.v) || cc.z;
    }
}

static void m68_exception(m68kd *mk, uint32_t vector, int cycles) {
    exceptions_++;
    cycles_ += cycles;
    if (mk->exception_cb) mk->exception_cb(mk->user, vector, ppc_.l ? ppc_.l : pc_.l);
    const uint16_t sr = get_flags();
    set_flags((uint16_t)(sr | 0x2000));
    cc.t = false;
    const uint32_t fault_pc = ppc_.l ? ppc_.l : pc_.l;
    const uint32_t vec_num = (vector <= 255u) ? vector : (vector >> 2);
    const uint32_t vec_addr = vbr_ + (vec_num << 2);
    if (type_ != M68K_T_68000) {
        a[7].l -= 2; putword(a[7].l, (uint16_t)(vec_num & 0x0fff));
        a[7].l -= 4; putword(a[7].l, (uint16_t)(fault_pc >> 16)); putword(a[7].l + 2, (uint16_t)(fault_pc));
        a[7].l -= 2; putword(a[7].l, sr);
    } else {
        a[7].l -= 6; putword(a[7].l, sr);
        putword(a[7].l + 2, (uint16_t)(fault_pc >> 16)); putword(a[7].l + 4, (uint16_t)(fault_pc));
    }
    opcode_ = false;
    pc_.l = ((uint32_t)(getword(vec_addr)) << 16) | getword(vec_addr + 2);
}

static bool m68_check_supervisor(m68kd *mk) {
    if (cc.s) return true;
    // Privilege violation, vector 8.
    pc_.l = ppc_.l;
    exception(8, 34);
    return false;
}

static void m68_group_0(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);
    const uint8_t op = (uint8_t)((instruction >> 6) & 0x3f);

    switch (op) {
        case 0x00: {  // ori.b
            const uint8_t immediate = (uint8_t)(fetch_word());
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint8_t result = (uint8_t)(read_b(dir) | immediate);
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            } else {  // ori to ccr
                cycles_ += 20;
                const uint8_t result = (uint8_t)((get_flags() & 0xff) | immediate);
                cc.x = (result & 0x10) != 0;
                cc.n = (result & 0x08) != 0;
                cc.z = (result & 0x04) != 0;
                cc.v = (result & 0x02) != 0;
                cc.c = (result & 0x01) != 0;
            }
            break;
        }
        case 0x01: {  // ori.w
            const uint16_t immediate = fetch_word();
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint16_t result = (uint16_t)(read_w(dir) | immediate);
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            } else if (check_supervisor()) {  // ori to sr
                cycles_ += 20;
                set_flags((uint16_t)(get_flags() | immediate));
            }
            break;
        }
        case 0x02: {  // ori.l
            cycles_ += (dir >> 3) != 0 ? 20 + calc_ea_t_l(dir) : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t result = read_l(dir) | immediate;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x04: case 0x0c: case 0x14: case 0x1c:
        case 0x24: case 0x2c: case 0x34: case 0x3c: {  // btst dynamic / movep.w er
            if ((dir >> 3) == 0) {
                cycles_ += 6;
                const uint32_t mask = 1u << (((uint8_t)(d[dest]).l) & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
            } else if ((dir >> 3) == 1) {  // movep.w memory to register
                cycles_ += 16;
                const uint32_t address = a[orig].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
                d[dest].l = (d[dest].l & 0xffff0000u) | ((uint32_t)(getbyte(address)) << 8) |
                            getbyte(address + 2);
            } else {
                cycles_ += 4 + calc_ea_t_bw(dir);
                const uint8_t mask = (uint8_t)(1u << (((uint8_t)(d[dest]).l) & 7));
                cc.z = (read_b(dir) & mask) == 0;
            }
            break;
        }
        case 0x05: case 0x0d: case 0x15: case 0x1d:
        case 0x25: case 0x2d: case 0x35: case 0x3d: {  // bchg dynamic / movep.l er
            if ((dir >> 3) == 0) {
                cycles_ += 8;
                const uint32_t mask = 1u << (((uint8_t)(d[dest]).l) & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
                d[orig].l ^= mask;
            } else if ((dir >> 3) == 1) {  // movep.l memory to register
                cycles_ += 24;
                const uint32_t address = a[orig].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
                d[dest].l = ((uint32_t)(getbyte(address)) << 24) |
                            ((uint32_t)(getbyte(address + 2)) << 16) |
                            ((uint32_t)(getbyte(address + 4)) << 8) | getbyte(address + 6);
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t mask = (uint8_t)(1u << (((uint8_t)(d[dest]).l) & 7));
                uint8_t value = read_b(dir);
                cc.z = (value & mask) == 0;
                write_b2(dir, (uint8_t)(value ^ mask));
            }
            break;
        }
        case 0x06: case 0x0e: case 0x16: case 0x1e:
        case 0x26: case 0x2e: case 0x36: case 0x3e: {  // bclr dynamic / movep.w re
            if ((dir >> 3) == 0) {
                cycles_ += 10;
                const uint32_t mask = 1u << (((uint8_t)(d[dest]).l) & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
                d[orig].l &= ~mask;
            } else if ((dir >> 3) == 1) {  // movep.w register to memory
                cycles_ += 16;
                const uint32_t address = a[orig].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
                putbyte(address, (uint8_t)(d[dest].l >> 8));
                putbyte(address + 2, ((uint8_t)(d[dest]).l));
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                if (type_ == M68K_T_68010) cycles_ += 2;
                const uint8_t mask = (uint8_t)(1u << (((uint8_t)(d[dest]).l) & 7));
                uint8_t value = read_b(dir);
                cc.z = (value & mask) == 0;
                write_b2(dir, (uint8_t)(value & ~mask));
            }
            break;
        }
        case 0x07: case 0x0f: case 0x17: case 0x1f:
        case 0x27: case 0x2f: case 0x37: case 0x3f: {  // bset dynamic / movep.l re
            if ((dir >> 3) == 0) {
                cycles_ += 8;
                const uint32_t mask = 1u << (((uint8_t)(d[dest]).l) & 0x1f);
                cc.z = (d[orig].l & mask) == 0;
                d[orig].l |= mask;
            } else if ((dir >> 3) == 1) {  // movep.l register to memory
                cycles_ += 24;
                const uint32_t address = a[orig].l + (uint32_t)((int32_t)((int16_t)(fetch_word())));
                putbyte(address, (uint8_t)(d[dest].l >> 24));
                putbyte(address + 2, (uint8_t)(d[dest].l >> 16));
                putbyte(address + 4, (uint8_t)(d[dest].l >> 8));
                putbyte(address + 6, ((uint8_t)(d[dest]).l));
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t mask = (uint8_t)(1u << (((uint8_t)(d[dest]).l) & 7));
                uint8_t value = read_b(dir);
                cc.z = (value & mask) == 0;
                write_b2(dir, (uint8_t)(value | mask));
            }
            break;
        }
        case 0x08: {  // andi.b
            const uint8_t immediate = (uint8_t)(fetch_word());
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint8_t result = (uint8_t)(read_b(dir) & immediate);
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            } else {  // andi to ccr
                cycles_ += type_ == M68K_T_68010 ? 16 : 20;
                const uint8_t result = (uint8_t)((get_flags() & 0xff) & immediate);
                cc.x = (result & 0x10) != 0;
                cc.n = (result & 0x08) != 0;
                cc.z = (result & 0x04) != 0;
                cc.v = (result & 0x02) != 0;
                cc.c = (result & 0x01) != 0;
            }
            break;
        }
        case 0x09: {  // andi.w
            const uint16_t immediate = fetch_word();
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint16_t result = (uint16_t)(read_w(dir) & immediate);
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            } else if (check_supervisor()) {  // andi to sr
                cycles_ += type_ == M68K_T_68010 ? 16 : 20;
                set_flags((uint16_t)(get_flags() & immediate));
            }
            break;
        }
        case 0x0a: {  // andi.l
            cycles_ += (dir >> 3) != 0 ? 20 + calc_ea_t_l(dir) : 14;
            const uint32_t immediate = fetch_long();
            const uint32_t result = read_l(dir) & immediate;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = false;
            cc.c = false;
            break;
        }
        case 0x10: {  // subi.b
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint8_t immediate = (uint8_t)(fetch_word() & 0xff);
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(value - immediate);
            write_b2(dir, (uint8_t)(result));
            cc.v = (((immediate ^ value) & (result ^ value)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            break;
        }
        case 0x11: {  // subi.w
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint16_t immediate = fetch_word();
            const uint16_t value = read_w(dir);
            const uint32_t result = (uint32_t)(value) - immediate;
            write_w2(dir, (uint16_t)(result));
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x12: {  // subi.l
            if ((dir >> 3) != 0) cycles_ += 20 + calc_ea_t_l(dir);
            else cycles_ += type_ == M68K_T_68010 ? 14 : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t value = read_l(dir);
            const uint32_t result = value - immediate;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
            cc.c = ((((immediate & result) | (~value & (immediate | result))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x18: {  // addi.b
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint8_t immediate = (uint8_t)(fetch_word() & 0xff);
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(immediate + value);
            write_b2(dir, (uint8_t)(result));
            cc.v = (((immediate ^ result) & (value ^ result)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            break;
        }
        case 0x19: {  // addi.w
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
            const uint16_t immediate = fetch_word();
            const uint16_t value = read_w(dir);
            const uint32_t result = (uint32_t)(immediate) + value;
            write_w2(dir, (uint16_t)(result));
            cc.v = ((((immediate ^ result) & (value ^ result)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x1a: {  // addi.l
            if ((dir >> 3) != 0) cycles_ += 20 + calc_ea_t_l(dir);
            else cycles_ += type_ == M68K_T_68010 ? 14 : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t value = read_l(dir);
            const uint32_t result = immediate + value;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((immediate ^ result) & (value ^ result)) >> 24) & 0x80) != 0;
            cc.c = ((((immediate & value) | (~result & (immediate | value))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x20: {  // btst static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 10;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
            } else {
                cycles_ += 8 + calc_ea_t_bw(dir);
                cc.z = ((read_b(dir) >> (bit & 7)) & 1) == 0;
            }
            break;
        }
        case 0x21: {  // bchg static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 12;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
                d[orig].l ^= 1u << (bit & 0x1f);
            } else {
                cycles_ += 12 + calc_ea_t_bw(dir);
                uint8_t value = read_b(dir);
                cc.z = ((value >> (bit & 7)) & 1) == 0;
                write_b2(dir, (uint8_t)(value ^ (1u << (bit & 7))));
            }
            break;
        }
        case 0x22: {  // bclr static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 12;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
                d[orig].l &= ~(1u << (bit & 0x1f));
            } else {
                cycles_ += 12 + calc_ea_t_bw(dir);
                uint8_t value = read_b(dir);
                cc.z = ((value >> (bit & 7)) & 1) == 0;
                write_b2(dir, (uint8_t)(value & ~(1u << (bit & 7))));
            }
            break;
        }
        case 0x23: {  // bset static
            const uint16_t bit = fetch_word();
            if ((dir >> 3) == 0) {
                cycles_ += 12;
                cc.z = ((d[orig].l >> (bit & 0x1f)) & 1) == 0;
                d[orig].l |= 1u << (bit & 0x1f);
            } else {
                cycles_ += 12 + calc_ea_t_bw(dir);
                uint8_t value = read_b(dir);
                cc.z = ((value >> (bit & 7)) & 1) == 0;
                write_b2(dir, (uint8_t)(value | (1u << (bit & 7))));
            }
            break;
        }
        case 0x28: {  // eori.b
            const uint8_t immediate = (uint8_t)(fetch_word());
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint8_t result = (uint8_t)(read_b(dir) ^ immediate);
                write_b2(dir, result);
                cc.v = false;
                cc.c = false;
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
            } else {  // eori to ccr
                cycles_ += 20;
                const uint8_t result = (uint8_t)((get_flags() & 0xff) ^ immediate);
                cc.x = (result & 0x10) != 0;
                cc.n = (result & 0x08) != 0;
                cc.z = (result & 0x04) != 0;
                cc.v = (result & 0x02) != 0;
                cc.c = (result & 0x01) != 0;
            }
            break;
        }
        case 0x29: {  // eori.w
            const uint16_t immediate = fetch_word();
            if (dir != 0x3c) {
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_bw(dir) : 8;
                const uint16_t result = (uint16_t)(read_w(dir) ^ immediate);
                write_w2(dir, result);
                cc.v = false;
                cc.c = false;
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
            } else if (check_supervisor()) {  // eori to sr
                cycles_ += 20;
                set_flags((uint16_t)(get_flags() ^ immediate));
            }
            break;
        }
        case 0x2a: {  // eori.l
            if ((dir >> 3) != 0) cycles_ += 20 + calc_ea_t_l(dir);
            else cycles_ += type_ == M68K_T_68010 ? 14 : 16;
            const uint32_t immediate = fetch_long();
            const uint32_t result = read_l(dir) ^ immediate;
            write_l2(dir, result);
            cc.v = false;
            cc.c = false;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x30: {  // cmpi.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 8;
            const uint8_t immediate = (uint8_t)(fetch_word() & 0xff);
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(value - immediate);
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.v = (((immediate ^ value) & (result ^ value)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            break;
        }
        case 0x31: {  // cmpi.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 8;
            const uint16_t immediate = fetch_word();
            const uint16_t value = read_w(dir);
            const uint32_t result = (uint32_t)(value) - immediate;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            break;
        }
        case 0x32: {  // cmpi.l
            if (type_ == M68K_T_68010 || (dir >> 3) != 0) cycles_ += 12 + calc_ea_t_l(dir);
            else cycles_ += 14;
            const uint32_t immediate = fetch_long();
            const uint32_t value = read_l(dir);
            const uint32_t result = value - immediate;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((immediate ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
            cc.c = ((((immediate & result) | (~value & (immediate | result))) >> 23) & 0x100) != 0;
            // FD1094 watches CMPI.L #imm, Dn (register mode only).
            if (mk->cmpild && (dir >> 3) == 0) {
                mk->cmpild(mk->user, (uint8_t)(dir & 7), immediate);
            }
            break;
        }
        case 0x38: case 0x39: case 0x3a: {
            // moves.b/w/l (68010+): move to/from an alternate address space
            // selected by SFC/DFC. We have one flat memory map, so this
            // behaves like an ordinary move — real ROMs use it mainly to
            // probe which CPU is present (it traps on a plain 68000).
            const uint16_t ext = fetch_word();
            const int reg = (ext >> 12) & 7;
            const bool is_addr_reg = (ext & 0x0800) != 0;
            const bool reg_to_ea = (ext & 0x8000) != 0;
            if (op == 0x38) {
                cycles_ += 4 + calc_ea_t_bw(dir);
                if (reg_to_ea) {
                    write_b2(dir, is_addr_reg ? ((uint8_t)(a[reg]).l) : ((uint8_t)(d[reg]).l));
                } else {
                    const uint8_t v = read_b(dir);
                    if (is_addr_reg) R_SET_L0(a[reg], v); else R_SET_L0(d[reg], v);
                }
            } else if (op == 0x39) {
                cycles_ += 4 + calc_ea_t_bw(dir);
                if (reg_to_ea) {
                    write_w2(dir, is_addr_reg ? ((uint16_t)(a[reg]).l) : ((uint16_t)(d[reg]).l));
                } else {
                    const uint16_t v = read_w(dir);
                    if (is_addr_reg) R_SET_WL(a[reg], v); else R_SET_WL(d[reg], v);
                }
            } else {
                cycles_ += 4 + calc_ea_t_l(dir);
                if (reg_to_ea) {
                    write_l2(dir, is_addr_reg ? a[reg].l : d[reg].l);
                } else {
                    const uint32_t v = read_l(dir);
                    if (is_addr_reg) a[reg].l = v; else d[reg].l = v;
                }
            }
            break;
        }
        default:
            // Illegal instruction, vector 4.
            pc_.l = ppc_.l;
            exception(4, 34);
            break;
    }
}

static void m68_group_b(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // cmp.b
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t right = read_b(dir);
            const uint8_t left = ((uint8_t)(d[dest]).l);
            const uint16_t result = (uint16_t)(left - right);
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            break;
        }
        case 0x1: {  // cmp.w
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t right = read_w(dir);
            const uint16_t left = ((uint16_t)(d[dest]).l);
            const uint32_t result = (uint32_t)(left) - right;
            cc.n = (result & 0x8000) != 0;
            cc.z = (result & 0xffff) == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
            cc.c = (result & 0x10000) != 0;
            break;
        }
        case 0x2: {  // cmp.l
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = d[dest].l;
            const uint32_t result = left - right;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            break;
        }
        case 0x3: {  // cmpa.w
            cycles_ += 6 + calc_ea_t_bw(dir);
            const uint32_t right = (uint32_t)((int32_t)((int16_t)(read_w(dir))));
            const uint32_t left = a[dest].l;
            const uint32_t result = left - right;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 1) {  // cmpm.b
                cycles_ += 12;
                const uint8_t right = getbyte(a[orig].l);
                a[orig].l += 1;
                const uint8_t left = getbyte(a[dest].l);
                a[dest].l += 1;
                const uint16_t result = (uint16_t)(left - right);
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
            } else {  // eor.b
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
                const uint8_t result = (uint8_t)(read_b(dir) ^ ((uint8_t)(d[dest]).l));
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 1) {  // cmpm.w
                cycles_ += 12;
                const uint16_t right = getword(a[orig].l);
                a[orig].l += 2;
                const uint16_t left = getword(a[dest].l);
                a[dest].l += 2;
                const uint32_t result = (uint32_t)(left) - right;
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.c = (result & 0x10000) != 0;
            } else {  // eor.w
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
                const uint16_t result = (uint16_t)(read_w(dir) ^ ((uint16_t)(d[dest]).l));
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 1) {  // cmpm.l
                cycles_ += 20;
                const uint32_t right = ((uint32_t)(getword(a[orig].l)) << 16) | getword(a[orig].l + 2);
                a[orig].l += 4;
                const uint32_t left = ((uint32_t)(getword(a[dest].l)) << 16) | getword(a[dest].l + 2);
                a[dest].l += 4;
                const uint32_t result = left - right;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            } else {  // eor.l
                cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 8;
                const uint32_t result = read_l(dir) ^ d[dest].l;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
            }
            break;
        }
        default: {  // cmpa.l
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = a[dest].l;
            const uint32_t result = left - right;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            break;
        }
    }
}

static void m68_group_c(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // and.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t result = (uint8_t)(((uint8_t)(d[dest]).l) & read_b(dir));
            R_SET_L0(d[dest], result);
            cc.n = (result & 0x80) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x1: {  // and.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t result = (uint16_t)(((uint16_t)(d[dest]).l) & read_w(dir));
            R_SET_WL(d[dest], result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x2: {  // and.l ea,Dn
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t result = d[dest].l & read_l(dir);
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x3: {  // mulu
            cycles_ += type_ == M68K_T_68010 ? 30 : 54;
            if ((dir >> 3) == 0) cycles_ += calc_ea_t_bw(dir);
            const uint32_t result = (uint32_t)(read_w(dir)) * ((uint16_t)(d[dest]).l);
            d[dest].l = result;
            cc.z = result == 0;
            cc.n = (result & 0x80000000u) != 0;
            cc.v = false;
            cc.c = false;
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // abcd
                uint8_t right = 0;
                uint8_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 6;
                    right = ((uint8_t)(d[orig]).l);
                    left = ((uint8_t)(d[dest]).l);
                } else {
                    cycles_ += 18;
                    a[orig].l -= 1;
                    right = getbyte(a[orig].l);
                    a[dest].l -= 1;
                    left = getbyte(a[dest].l);
                }
                uint32_t result = (uint32_t)((right & 0x0f) + (left & 0x0f) + (cc.x ? 1 : 0));
                const uint32_t correction = result > 9 ? 6u : 0u;
                result += (uint32_t)((right & 0xf0) + (left & 0xf0));
                cc.v = (~result & 0x80) != 0;
                result += correction;
                cc.c = result > 0x9f;
                cc.x = cc.c;
                if (cc.c) result -= 0xa0;
                cc.n = (result & 0x80) != 0;
                cc.v = cc.v && cc.n;
                cc.z = (result & 0xff) == 0;
                if ((dir >> 3) == 0) R_SET_L0(d[dest], (uint8_t)(result));
                else putbyte(a[dest].l, (uint8_t)(result));
            } else {  // and.b Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t result = (uint8_t)(read_b(dir) & ((uint8_t)(d[dest]).l));
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 0) {  // exg Dx,Dy
                cycles_ += 6;
                do { const uint32_t t_ = d[dest].l; d[dest].l = d[orig].l; d[orig].l = t_; } while (0);
            } else if ((dir >> 3) == 1) {  // exg Ax,Ay
                cycles_ += 6;
                do { const uint32_t t_ = a[dest].l; a[dest].l = a[orig].l; a[orig].l = t_; } while (0);
            } else {  // and.w Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint16_t result = (uint16_t)(read_w(dir) & ((uint16_t)(d[dest]).l));
                write_w2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 1) {  // exg Dx,Ay
                cycles_ += 6;
                do { const uint32_t t_ = d[dest].l; d[dest].l = a[orig].l; a[orig].l = t_; } while (0);
            } else {  // and.l Dn,ea
                cycles_ += 12 + calc_ea_t_l(dir);
                const uint32_t result = read_l(dir) & d[dest].l;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        default: {  // muls
            cycles_ += type_ == M68K_T_68010 ? 32 : 54;
            if ((dir >> 3) == 0) cycles_ += calc_ea_t_bw(dir);
            const int32_t right = (int32_t)((int16_t)(read_w(dir)));
            const int32_t left = (int32_t)((int16_t)(((uint16_t)(d[dest]).l)));
            const uint32_t result = (uint32_t)(right * left);
            d[dest].l = result;
            cc.z = result == 0;
            cc.n = (result & 0x80000000u) != 0;
            cc.v = false;
            cc.c = false;
            break;
        }
    }
}

static void m68_group_d(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // add.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t right = read_b(dir);
            const uint8_t left = ((uint8_t)(d[dest]).l);
            const uint16_t result = (uint16_t)(right + left);
            R_SET_L0(d[dest], (uint8_t)(result));
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x1: {  // add.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t right = read_w(dir);
            const uint16_t left = ((uint16_t)(d[dest]).l);
            const uint32_t result = (uint32_t)(right) + left;
            R_SET_WL(d[dest], (uint16_t)(result));
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.v = ((((right ^ result) & (left ^ result)) >> 8) & 0x80) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x2: {  // add.l ea,Dn
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = d[dest].l;
            const uint32_t result = right + left;
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
            cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x3: {  // adda.w
            cycles_ += 8 + calc_ea_t_bw(dir);
            a[dest].l += (uint32_t)((int32_t)((int16_t)(read_w(dir))));
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.b
                uint8_t right = 0;
                uint8_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 4;
                    right = ((uint8_t)(d[orig]).l);
                    left = ((uint8_t)(d[dest]).l);
                } else {
                    cycles_ += 18;
                    a[orig].l -= 1;
                    right = getbyte(a[orig].l);
                    a[dest].l -= 1;
                    left = getbyte(a[dest].l);
                }
                const uint16_t result = (uint16_t)(right + left + (cc.x ? 1 : 0));
                if ((dir >> 3) == 0) R_SET_L0(d[dest], (uint8_t)(result));
                else putbyte(a[dest].l, (uint8_t)(result));
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
            } else {  // add.b Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t right = ((uint8_t)(d[dest]).l);
                const uint8_t left = read_b(dir);
                const uint16_t result = (uint16_t)(right + left);
                write_b2(dir, (uint8_t)(result));
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.w
                uint16_t right = 0;
                uint16_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 4;
                    right = ((uint16_t)(d[orig]).l);
                    left = ((uint16_t)(d[dest]).l);
                } else {
                    cycles_ += 18;
                    a[orig].l -= 2;
                    right = getword(a[orig].l);
                    a[dest].l -= 2;
                    left = getword(a[dest].l);
                }
                const uint32_t result = (uint32_t)(right) + left + (cc.x ? 1 : 0);
                if ((dir >> 3) == 0) R_SET_WL(d[dest], (uint16_t)(result));
                else putword(a[dest].l, (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
            } else {  // add.w Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint16_t right = ((uint16_t)(d[dest]).l);
                const uint16_t left = read_w(dir);
                const uint32_t result = (uint32_t)(right) + left;
                write_w2(dir, (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ result) & (left ^ result)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.l
                uint32_t right = 0;
                uint32_t left = 0;
                if ((dir >> 3) == 0) {
                    cycles_ += 8;
                    right = d[orig].l;
                    left = d[dest].l;
                } else {
                    cycles_ += 30;
                    a[orig].l -= 4;
                    right = ((uint32_t)(getword(a[orig].l)) << 16) | getword(a[orig].l + 2);
                    a[dest].l -= 4;
                    left = ((uint32_t)(getword(a[dest].l)) << 16) | getword(a[dest].l + 2);
                }
                const uint32_t result = right + left + (cc.x ? 1 : 0);
                if ((dir >> 3) == 0) {
                    d[dest].l = result;
                } else {
                    putword(a[dest].l, (uint16_t)(result >> 16));
                    putword(a[dest].l + 2, (uint16_t)(result));
                }
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else {  // add.l Dn,ea
                cycles_ += 12 + calc_ea_t_l(dir);
                const uint32_t right = d[dest].l;
                const uint32_t left = read_l(dir);
                const uint32_t result = right + left;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            }
            break;
        }
        default: {  // adda.l
            cycles_ += 6 + calc_ea_t_l(dir);
            a[dest].l += read_l(dir);
            break;
        }
    }
}

static void m68_group_e(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t orig = (size_t)(instruction & 7);

    if (((instruction >> 6) & 3) == 3) {  // memory shifts, always one bit
        cycles_ += 8 + calc_ea_t_bw(dir);
        const uint16_t value = read_w(dir);
        uint16_t result = 0;
        switch ((instruction >> 8) & 0x0f) {
            case 0x0:  // asr.w
                cc.c = (value & 1) != 0;
                result = (uint16_t)((value >> 1) | (value & 0x8000));
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x1:  // asl.w
                result = (uint16_t)(value << 1);
                cc.c = (value & 0x8000) != 0;
                cc.x = cc.c;
                cc.v = (value & 0xc000) != 0 && (value & 0xc000) != 0xc000;
                break;
            case 0x2:  // lsr.w
                cc.c = (value & 1) != 0;
                result = (uint16_t)(value >> 1);
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x3:  // lsl.w
                cc.c = (value & 0x8000) != 0;
                result = (uint16_t)(value << 1);
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x4:  // roxr.w
                cc.c = (value & 1) != 0;
                result = (uint16_t)((value >> 1) | (cc.x ? 0x8000 : 0));
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x5:  // roxl.w
                cc.c = (value & 0x8000) != 0;
                result = (uint16_t)((value << 1) | (cc.x ? 1 : 0));
                cc.x = cc.c;
                cc.v = false;
                break;
            case 0x6:  // ror.w
                cc.c = (value & 1) != 0;
                result = (uint16_t)((value >> 1) | ((value & 1) << 15));
                cc.v = false;
                break;
            case 0x7:  // rol.w
                cc.c = (value & 0x8000) != 0;
                result = (uint16_t)((value << 1) | (cc.c ? 1 : 0));
                cc.v = false;
                break;
            default: {
                if (type_ == M68K_T_68020) {
                    fetch_word(); cycles_ += 12;
                    cc.n = false; cc.z = true; cc.v = false; cc.c = false;
                    return;
                }
                pc_.l = ppc_.l; exception(4, 34); return;
            }
        }
        write_w2(dir, result);
        cc.n = (result & 0x8000) != 0;
        cc.z = result == 0;
        return;
    }

    const int count = ((instruction >> 5) & 1) == 1
                          ? (int)(d[(size_t)((instruction >> 9) & 7)].l & 0x3f)
                          : (int)(((((instruction >> 9) & 7) - 1) & 7) + 1);
    cycles_ += count * 2;
    const uint8_t op = (uint8_t)((instruction >> 3) & 0x3f);
    const int size = (op & 0x18) == 0x00 ? 8 : ((op & 0x18) == 0x08 ? 16 : 32);
    const bool left_shift = (op & 0x20) != 0;
    cycles_ += size == 32 ? 8 : 6;

    uint32_t value = size == 8 ? ((uint8_t)(d[orig]).l) : (size == 16 ? ((uint16_t)(d[orig]).l) : d[orig].l);
    const uint32_t original = value;
    const uint32_t mask = size == 32 ? 0xffffffffu : (1u << size) - 1;
    const uint32_t sign = 1u << (size - 1);
    bool overflow = false;

    switch (op & 0x23) {
        case 0x00:  // asr
            for (int step = 0; step < count; ++step) {
                cc.c = (value & 1) != 0;
                cc.x = cc.c;
                value = ((value & sign) != 0) ? (sign | (value >> 1)) : (value >> 1);
            }
            break;
        case 0x01:  // lsr
            for (int step = 0; step < count; ++step) {
                cc.c = (value & 1) != 0;
                cc.x = cc.c;
                value >>= 1;
            }
            break;
        case 0x02:  // roxr
            for (int step = 0; step < count; ++step) {
                const bool carry = (value & 1) != 0;
                value = ((value >> 1) | (cc.x ? sign : 0)) & mask;
                cc.c = carry;
                cc.x = carry;
            }
            break;
        case 0x03:  // ror
            for (int step = 0; step < count; ++step) {
                cc.c = (value & 1) != 0;
                value = ((value >> 1) | (cc.c ? sign : 0)) & mask;
            }
            break;
        case 0x20:  // asl
            for (int step = 0; step < count; ++step) {
                cc.c = (value & sign) != 0;
                cc.x = cc.c;
                value = (value << 1) & mask;
            }
            if (count != 0) {
                const uint32_t mask_out = size == 8 ? kShift8[(count + 1 < 64 ? count + 1 : 64)]
                                          : (size == 16 ? shift16(count + 1) : shift32(count + 1));
                const uint32_t bits = original & mask_out;
                overflow = bits != 0 && bits != mask_out;
            }
            break;
        case 0x21:  // lsl
            for (int step = 0; step < count; ++step) {
                cc.c = (value & sign) != 0;
                cc.x = cc.c;
                value = (value << 1) & mask;
            }
            break;
        case 0x22:  // roxl
            for (int step = 0; step < count; ++step) {
                const bool carry = (value & sign) != 0;
                value = ((value << 1) | (cc.x ? 1 : 0)) & mask;
                cc.c = carry;
                cc.x = carry;
            }
            break;
        default:  // rol
            for (int step = 0; step < count; ++step) {
                cc.c = (value & sign) != 0;
                value = ((value << 1) | (cc.c ? 1 : 0)) & mask;
            }
            break;
    }

    value &= mask;
    if (size == 8) R_SET_L0(d[orig], (uint8_t)(value));
    else if (size == 16) R_SET_WL(d[orig], (uint16_t)(value));
    else d[orig].l = value;
    cc.n = (value & sign) != 0;
    cc.z = value == 0;
    // lsr/asr always clear n on the logical variants, matching the source.
    if (!left_shift && (op & 0x03) == 0x01) cc.n = false;
    cc.v = overflow;
}

static void m68_group_8(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // or.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t result = (uint8_t)(((uint8_t)(d[dest]).l) | read_b(dir));
            R_SET_L0(d[dest], result);
            cc.n = (result & 0x80) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x1: {  // or.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t result = (uint16_t)(((uint16_t)(d[dest]).l) | read_w(dir));
            R_SET_WL(d[dest], result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x2: {  // or.l ea,Dn
            cycles_ += (dir >> 3) != 0 ? 6 + calc_ea_t_l(dir) : 8;
            const uint32_t result = d[dest].l | read_l(dir);
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x3: {  // divu
            cycles_ += (type_ == M68K_T_68010 ? 108 : 140) + calc_ea_t_bw(dir);
            const uint16_t divisor = read_w(dir);
            if (divisor == 0) {
                // Traps stack the address of the next instruction.
                ppc_.l = pc_.l;
                exception(5, 38);  // divide by zero
                break;
            }
            cc.c = false;
            const uint32_t quotient = d[dest].l / divisor;
            if (quotient < 0x10000) {
                cc.z = quotient == 0;
                cc.n = (quotient & 0x8000) != 0;
                cc.v = false;
                const uint32_t remainder = d[dest].l % divisor;
                d[dest].l = (quotient & 0xffff) | ((remainder & 0xffff) << 16);
            } else {
                cc.v = true;
            }
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0) {  // sbcd Dy,Dx
                cycles_ += 6;
                const uint8_t left = ((uint8_t)(d[dest]).l);
                const uint8_t right = ((uint8_t)(d[orig]).l);
                uint32_t result = (uint32_t)((left & 0x0f) - (right & 0x0f) - (cc.x ? 1 : 0));
                const uint32_t correction = result > 0x0f ? 6u : 0u;
                result += (uint32_t)((left & 0xf0) - (right & 0xf0));
                cc.v = result != 0;
                if (result > 0xff) {
                    result += 0xa0;
                    cc.x = true;
                    cc.c = true;
                } else if (result < correction) {
                    cc.x = true;
                    cc.c = true;
                } else {
                    cc.x = false;
                    cc.c = false;
                    cc.n = false;
                }
                result = (result - correction) & 0xff;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                cc.v = cc.v && cc.n;
                R_SET_L0(d[dest], (uint8_t)(result));
            } else if ((dir >> 3) == 1) {  // sbcd -(Ay),-(Ax)
                cycles_ += 18;
                a[orig].l -= 1;
                const uint8_t right = getbyte(a[orig].l);
                a[dest].l -= 1;
                const uint8_t left = getbyte(a[dest].l);
                uint32_t result = (uint32_t)((left & 0x0f) - (right & 0x0f) - (cc.x ? 1 : 0));
                const uint32_t correction = result > 0x0f ? 6u : 0u;
                result += (uint32_t)((left & 0xf0) - (right & 0xf0));
                cc.v = result != 0;
                if (result > 0xff) {
                    result += 0xa0;
                    cc.x = true;
                    cc.c = true;
                } else if (result < correction) {
                    cc.x = true;
                    cc.c = true;
                } else {
                    cc.x = false;
                    cc.c = false;
                }
                result = (result - correction) & 0xff;
                cc.z = result == 0;
                cc.n = (result & 0x80) != 0;
                cc.v = cc.v && cc.n;
                putbyte(a[dest].l, (uint8_t)(result));
            } else {  // or.b Dn,ea
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 12;
                const uint8_t result = (uint8_t)(read_b(dir) | ((uint8_t)(d[dest]).l));
                write_b2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
            }
            break;
        }
        case 0x5: {  // or.w Dn,ea
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 12;
            const uint16_t result = (uint16_t)(read_w(dir) | ((uint16_t)(d[dest]).l));
            write_w2(dir, result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        case 0x6: {  // or.l Dn,ea
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 20;
            const uint32_t result = read_l(dir) | d[dest].l;
            write_l2(dir, result);
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.c = false;
            cc.v = false;
            break;
        }
        default: {  // divs
            cycles_ += (type_ == M68K_T_68010 ? 122 : 158) + calc_ea_t_bw(dir);
            const int32_t divisor = (int32_t)((int16_t)(read_w(dir)));
            if (divisor == 0) {
                // Traps stack the address of the next instruction.
                ppc_.l = pc_.l;
                exception(5, 38);  // divide by zero
                break;
            }
            const uint32_t value = d[dest].l;
            cc.c = false;
            if (value == 0x80000000u && divisor == -1) {
                cc.z = true;
                cc.n = false;
                cc.v = false;
                d[dest].l = 0;
            } else {
                const int32_t quotient = (int32_t)(value) / divisor;
                const int32_t remainder = (int32_t)(value) % divisor;
                if (quotient == (int32_t)((int16_t)(quotient))) {
                    cc.z = quotient == 0;
                    cc.n = (quotient & 0x8000) != 0;
                    cc.v = false;
                    d[dest].l = ((uint32_t)(quotient) & 0xffff) | ((uint32_t)(remainder) << 16);
                } else {
                    cc.v = true;
                }
            }
            break;
        }
    }
}

static void m68_group_9(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // sub.b ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t right = read_b(dir);
            const uint8_t left = ((uint8_t)(d[dest]).l);
            const uint16_t result = (uint16_t)(left - right);
            R_SET_L0(d[dest], (uint8_t)(result));
            cc.n = (result & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            break;
        }
        case 0x1: {  // sub.w ea,Dn
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t right = read_w(dir);
            const uint16_t left = ((uint16_t)(d[dest]).l);
            const uint32_t result = (uint32_t)(left) - right;
            R_SET_WL(d[dest], (uint16_t)(result));
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
            cc.z = (result & 0xffff) == 0;
            break;
        }
        case 0x2: {  // sub.l ea,Dn
            cycles_ += 6 + calc_ea_t_l(dir);
            const uint32_t right = read_l(dir);
            const uint32_t left = d[dest].l;
            const uint32_t result = left - right;
            d[dest].l = result;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
            cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            break;
        }
        case 0x3: {  // suba.w
            cycles_ += 8 + calc_ea_t_bw(dir);
            a[dest].l -= (uint32_t)((int32_t)((int16_t)(read_w(dir))));
            break;
        }
        case 0x4: {
            if ((dir >> 3) == 0) {  // subx.b Dy,Dx
                cycles_ += 4;
                const uint8_t right = ((uint8_t)(d[orig]).l);
                const uint8_t left = ((uint8_t)(d[dest]).l);
                const uint16_t result = (uint16_t)(left - right - (cc.x ? 1 : 0));
                R_SET_L0(d[dest], (uint8_t)(result));
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
            } else if ((dir >> 3) == 1) {  // subx.b -(Ay),-(Ax)
                cycles_ += 18;
                a[orig].l -= 1;
                const uint8_t right = getbyte(a[orig].l);
                a[dest].l -= 1;
                const uint8_t left = getbyte(a[dest].l);
                const uint16_t result = (uint16_t)(left - right - (cc.x ? 1 : 0));
                putbyte(a[dest].l, (uint8_t)(result));
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
            } else {  // sub.b Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint8_t right = ((uint8_t)(d[dest]).l);
                const uint8_t left = read_b(dir);
                const uint16_t result = (uint16_t)(left - right);
                write_b2(dir, (uint8_t)(result));
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
            }
            break;
        }
        case 0x5: {
            if ((dir >> 3) == 0) {  // subx.w Dy,Dx
                cycles_ += 4;
                const uint16_t right = ((uint16_t)(d[orig]).l);
                const uint16_t left = ((uint16_t)(d[dest]).l);
                const uint32_t result = (uint32_t)(left) - right - (cc.x ? 1 : 0);
                R_SET_WL(d[dest], (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            } else if ((dir >> 3) == 1) {  // subx.w -(Ay),-(Ax)
                cycles_ += 18;
                a[orig].l -= 2;
                const uint16_t right = getword(a[orig].l);
                a[dest].l -= 2;
                const uint16_t left = getword(a[dest].l);
                const uint32_t result = (uint32_t)(left) - right - (cc.x ? 1 : 0);
                putword(a[dest].l, (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            } else {  // sub.w Dn,ea
                cycles_ += 8 + calc_ea_t_bw(dir);
                const uint16_t right = ((uint16_t)(d[dest]).l);
                const uint16_t left = read_w(dir);
                const uint32_t result = (uint32_t)(left) - right;
                write_w2(dir, (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
            }
            break;
        }
        case 0x6: {
            if ((dir >> 3) == 0) {  // subx.l Dy,Dx
                cycles_ += 8;
                const uint32_t right = d[orig].l;
                const uint32_t left = d[dest].l;
                const uint32_t result = left - right - (cc.x ? 1 : 0);
                d[dest].l = result;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else if ((dir >> 3) == 1) {  // subx.l -(Ay),-(Ax)
                cycles_ += 30;
                a[orig].l -= 4;
                const uint32_t right = ((uint32_t)(getword(a[orig].l)) << 16) | getword(a[orig].l + 2);
                a[dest].l -= 4;
                const uint32_t left = ((uint32_t)(getword(a[dest].l)) << 16) | getword(a[dest].l + 2);
                const uint32_t result = left - right - (cc.x ? 1 : 0);
                putword(a[dest].l, (uint16_t)(result >> 16));
                putword(a[dest].l + 2, (uint16_t)(result));
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else {  // sub.l Dn,ea
                cycles_ += 12 + calc_ea_t_l(dir);
                const uint32_t right = d[dest].l;
                const uint32_t left = read_l(dir);
                const uint32_t result = left - right;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            }
            break;
        }
        default: {  // suba.l
            cycles_ += 6 + calc_ea_t_l(dir);
            a[dest].l -= read_l(dir);
            break;
        }
    }
}

static void m68_group_5(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t orig = (size_t)(instruction & 7);
    const uint8_t quick = (uint8_t)(((((instruction >> 9) & 7) - 1) & 7) + 1);

    switch ((instruction >> 6) & 7) {
        case 0x0: {  // addq.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(quick + value);
            write_b2(dir, (uint8_t)(result));
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.v = (((quick ^ result) & (value ^ result)) & 0x80) != 0;
            break;
        }
        case 0x1: {  // addq.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            if ((dir >> 3) != 1) {
                const uint16_t value = read_w(dir);
                const uint32_t result = (uint32_t)(value) + quick;
                write_w2(dir, (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((quick ^ result) & (value ^ result)) >> 8) & 0x80) != 0;
            } else {
                a[orig].l += quick;
            }
            break;
        }
        case 0x2: {  // addq.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 8;
            if ((dir >> 3) != 1) {
                const uint32_t value = read_l(dir);
                const uint32_t result = value + quick;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.c = ((((quick & value) | (~result & (quick | value))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                cc.v = ((((quick ^ result) & (value ^ result)) >> 24) & 0x80) != 0;
            } else {
                a[orig].l += quick;
            }
            break;
        }
        case 0x3: case 0x7: {
            if (((dir >> 3) & 7) == 1) {  // dbcc
                cycles_ += 12;
                if (!condition((uint8_t)((instruction >> 8) & 0x0f))) {
                    R_SET_WL(d[orig], (uint16_t)(((uint16_t)(d[orig]).l) - 1));
                    if (((uint16_t)(d[orig]).l) != 0xffff) {
                        cycles_ -= 2;
                        pc_.l += (uint32_t)((int32_t)((int16_t)(prog_word(pc_.l))));
                    } else {
                        pc_.l += 2;
                    }
                } else {
                    pc_.l += 2;
                }
            } else {  // scc
                cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
                write_b(dir, condition((uint8_t)((instruction >> 8) & 0x0f)) ? 0xff : 0x00);
            }
            break;
        }
        case 0x4: {  // subq.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(value - quick);
            write_b2(dir, (uint8_t)(result));
            cc.n = (result & 0x80) != 0;
            cc.z = (result & 0xff) == 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.v = (((quick ^ value) & (result ^ value)) & 0x80) != 0;
            break;
        }
        case 0x5: {  // subq.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            if ((dir >> 3) != 1) {
                const uint16_t value = read_w(dir);
                const uint32_t result = (uint32_t)(value) - quick;
                write_w2(dir, (uint16_t)(result));
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((quick ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
            } else {
                a[orig].l -= quick;
            }
            break;
        }
        default: {  // subq.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 8;
            if ((dir >> 3) != 1) {
                const uint32_t value = read_l(dir);
                const uint32_t result = value - quick;
                write_l2(dir, result);
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                cc.v = ((((quick ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
                cc.c = ((((quick & result) | (~value & (quick | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
            } else {
                a[orig].l -= quick;
            }
            break;
        }
    }
}

static void m68_group_6(m68kd *mk, uint16_t instruction) {
    const uint8_t offset = (uint8_t)(instruction & 0xff);
    const uint8_t code = (uint8_t)((instruction >> 8) & 0x0f);
    if (code == 1) {
        cycles_ += 18;
        if (offset == 0x00) {
            const uint16_t displacement = prog_word(pc_.l);
            a[7].l -= 4;
            putword(a[7].l, (uint16_t)((pc_.l + 2) >> 16));
            putword(a[7].l + 2, (uint16_t)(pc_.l + 2));
            pc_.l += (uint32_t)((int32_t)((int16_t)(displacement)));
        } else if (offset == 0xff && type_ != M68K_T_68000) {
            const uint32_t hi = prog_word(pc_.l), lo = prog_word(pc_.l + 2);
            const uint32_t ret = pc_.l + 4;
            a[7].l -= 4;
            putword(a[7].l, (uint16_t)(ret >> 16));
            putword(a[7].l + 2, (uint16_t)(ret));
            pc_.l = pc_.l + (uint32_t)((int32_t)((hi << 16) | lo));
        } else {
            a[7].l -= 4;
            putword(a[7].l, (uint16_t)(pc_.l >> 16));
            putword(a[7].l + 2, (uint16_t)(pc_.l));
            pc_.l += (uint32_t)((int32_t)((int8_t)(offset)));
        }
        return;
    }
    if (condition(code)) {
        cycles_ += 10;
        if (offset == 0x00) pc_.l += (uint32_t)((int32_t)((int16_t)(prog_word(pc_.l))));
        else if (offset == 0xff && type_ != M68K_T_68000) {
            const uint32_t hi = prog_word(pc_.l), lo = prog_word(pc_.l + 2);
            pc_.l = pc_.l + (uint32_t)((int32_t)((hi << 16) | lo));
        } else pc_.l += (uint32_t)((int32_t)((int8_t)(offset)));
    } else {
        cycles_ += 8;
        if (offset == 0x00) pc_.l += 2;
        else if (offset == 0xff && type_ != M68K_T_68000) pc_.l += 4;
    }
}

static void m68_group_7(m68kd *mk, uint16_t instruction) {  // moveq / Basilisk EMUL_OP
    cycles_ += 4;
    if (0) {
        (void)0;
        return;
    }
    const size_t dest = (size_t)((instruction >> 9) & 7);
    d[dest].l = (uint32_t)((int32_t)((int8_t)(instruction & 0xff)));
    cc.c = false; cc.v = false;
    cc.z = d[dest].l == 0;
    cc.n = (d[dest].l & 0x80000000u) != 0;
}

static void m68_move_bw(m68kd *mk, uint16_t instruction, bool byte_size) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const uint8_t target = (uint8_t)(((instruction >> 9) & 7) | (((instruction >> 6) & 7) << 3));
    if (byte_size) {
        const uint8_t value = read_b(dir);
        write_b(target, value);
        cycles_ += calc_move_t(dir, target, false);
        cc.n = (value & 0x80) != 0;
        cc.z = value == 0;
    } else {
        const uint16_t value = read_w(dir);
        write_w(target, value);
        cycles_ += calc_move_t(dir, target, false);
        cc.n = (value & 0x8000) != 0;
        cc.z = value == 0;
    }
    cc.v = false;
    cc.c = false;
}

static void m68_move_l(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const uint8_t target = (uint8_t)(((instruction >> 9) & 7) | (((instruction >> 6) & 7) << 3));
    const uint32_t value = read_l(dir);
    write_l(target, value);
    cycles_ += calc_move_t(dir, target, true);
    cc.v = false;
    cc.c = false;
    cc.n = (value & 0x80000000u) != 0;
    cc.z = value == 0;
}

static void m68_group_1(m68kd *mk, uint16_t instruction) { move_bw(instruction, true); }

static void m68_group_4(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    const size_t dest = (size_t)((instruction >> 9) & 7);
    const size_t orig = (size_t)(instruction & 7);
    const uint8_t op = (uint8_t)((instruction >> 6) & 0x3f);

    switch (op) {
        case 0x00: {  // negx.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(0 - value - (cc.x ? 1 : 0));
            cc.n = (result & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            if ((result & 0xff) != 0) cc.z = false;
            cc.v = ((value & result) & 0x80) != 0;
            write_b2(dir, (uint8_t)(result));
            break;
        }
        case 0x01: {  // negx.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint16_t value = read_w(dir);
            const uint32_t result = (uint32_t)(0 - value - (cc.x ? 1 : 0));
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            if ((result & 0xffff) != 0) cc.z = false;
            cc.v = (((value & result) >> 8) & 0x80) != 0;
            write_w2(dir, (uint16_t)(result));
            break;
        }
        case 0x02: {  // negx.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            const uint32_t value = read_l(dir);
            const uint32_t result = 0 - value - (cc.x ? 1 : 0);
            cc.n = (result & 0x80000000u) != 0;
            cc.c = ((((value & result) | (value | result)) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            cc.z = result == 0;
            cc.v = (((value & result) >> 24) & 0x80) != 0;
            write_l2(dir, result);
            break;
        }
        case 0x03: {  // move from sr
            if (type_ == M68K_T_68000) {
                cycles_ += (dir >> 3) == 0 ? 6 : 8 + calc_ea_t_bw(dir);
                write_w(dir, get_flags());
            } else if (check_supervisor()) {
                cycles_ += (dir >> 3) == 0 ? 4 : 8 + calc_ea_t_bw(dir);
                write_w(dir, get_flags());
            }
            break;
        }
        case 0x06: case 0x0e: case 0x16: case 0x1e:
        case 0x26: case 0x2e: case 0x36: case 0x3e: {  // chk
            cycles_ += 10 + calc_ea_t_bw(dir);
            const int16_t bound = (int16_t)(read_w(dir));
            const int16_t value = (int16_t)(((uint16_t)(d[dest]).l));
            cc.n = value < 0;
            if (value < 0 || value > bound) {
                ppc_.l = pc_.l;   // stack the next instruction
                exception(6, 30);
            }
            break;
        }
        case 0x07: case 0x0f: case 0x17: case 0x1f:
        case 0x27: case 0x2f: case 0x37: case 0x3f: {  // lea
            a[dest].l = read_ea(dir);
            if (dir <= 0x17) cycles_ += 4;
            else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 8;
            else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b || dir == 0x39) cycles_ += 12;
            break;
        }
        case 0x08: {  // clr.b
            if (type_ == M68K_T_68000) cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            else cycles_ += 4 + calc_ea_t_bw(dir);
            write_b(dir, 0);
            cc.n = false;
            cc.v = false;
            cc.c = false;
            cc.z = true;
            break;
        }
        case 0x09: {  // clr.w
            if (type_ == M68K_T_68000) cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            else cycles_ += 4 + calc_ea_t_bw(dir);
            write_w(dir, 0);
            cc.n = false;
            cc.v = false;
            cc.c = false;
            cc.z = true;
            break;
        }
        case 0x0a: {  // clr.l
            if (type_ == M68K_T_68000) cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            else cycles_ += (dir >> 3) != 0 ? 4 + calc_ea_t_l(dir) : 6;
            write_l(dir, 0);
            cc.n = false;
            cc.v = false;
            cc.c = false;
            cc.z = true;
            break;
        }
        case 0x10: {  // neg.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            const uint16_t result = (uint16_t)(0 - value);
            cc.n = (result & 0x80) != 0;
            cc.c = (result & 0x100) != 0;
            cc.x = cc.c;
            cc.z = (result & 0xff) == 0;
            cc.v = ((value & result) & 0x80) != 0;
            write_b2(dir, (uint8_t)(result));
            break;
        }
        case 0x11: {  // neg.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint16_t value = read_w(dir);
            const uint32_t result = (uint32_t)(0 - value);
            cc.n = (result & 0x8000) != 0;
            cc.c = (result & 0x10000) != 0;
            cc.x = cc.c;
            cc.z = (result & 0xffff) == 0;
            cc.v = (((value & result) >> 8) & 0x80) != 0;
            write_w2(dir, (uint16_t)(result));
            break;
        }
        case 0x12: {  // neg.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            const uint32_t value = read_l(dir);
            const uint32_t result = 0 - value;
            cc.n = (result & 0x80000000u) != 0;
            cc.c = ((((value & result) | (value | result)) >> 23) & 0x100) != 0;
            cc.x = cc.c;
            cc.z = result == 0;
            cc.v = (((value & result) >> 24) & 0x80) != 0;
            write_l2(dir, result);
            break;
        }
        case 0x13: {  // move to ccr
            cycles_ += 12 + calc_ea_t_bw(dir);
            const uint16_t value = read_w(dir);
            cc.x = (value & 0x10) != 0;
            cc.n = (value & 0x08) != 0;
            cc.z = (value & 0x04) != 0;
            cc.v = (value & 0x02) != 0;
            cc.c = (value & 0x01) != 0;
            break;
        }
        case 0x18: {  // not.b
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint8_t result = (uint8_t)(~read_b(dir));
            write_b2(dir, result);
            cc.c = false;
            cc.v = false;
            cc.n = (result & 0x80) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x19: {  // not.w
            cycles_ += (dir >> 3) != 0 ? 8 + calc_ea_t_bw(dir) : 4;
            const uint16_t result = (uint16_t)(~read_w(dir));
            write_w2(dir, result);
            cc.c = false;
            cc.v = false;
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x1a: {  // not.l
            cycles_ += (dir >> 3) != 0 ? 12 + calc_ea_t_l(dir) : 6;
            const uint32_t result = ~read_l(dir);
            write_l2(dir, result);
            cc.c = false;
            cc.v = false;
            cc.n = (result & 0x80000000u) != 0;
            cc.z = result == 0;
            break;
        }
        case 0x1b: {  // move to sr
            if (check_supervisor()) {
                cycles_ += 12 + calc_ea_t_bw(dir);
                set_flags(read_w(dir));
            }
            break;
        }
        case 0x21: {  // swap / pea
            if (dir <= 0x07) {  // swap
                cycles_ += 4;
                const uint32_t result = ((uint32_t)(((uint16_t)(d[orig]).l)) << 16) | ((uint16_t)((d[orig]).l >> 16));
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                d[orig].l = result;
            } else {  // pea
                const uint32_t address = read_ea(dir);
                if (dir <= 0x17) cycles_ += 12;
                else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 16;
                else if (dir >= 0x30 && dir <= 0x37) cycles_ += 20;
                else if (dir == 0x39) cycles_ += 20;
                a[7].l -= 4;
                putword(a[7].l, (uint16_t)(address >> 16));
                putword(a[7].l + 2, (uint16_t)(address));
            }
            break;
        }
        case 0x22: {  // ext.w / movem.w register to memory
            if ((dir >> 3) == 0) {
                cycles_ += 4;
                const uint16_t result = (uint16_t)((int16_t)((int8_t)(((uint8_t)(d[orig]).l))));
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                R_SET_WL(d[orig], result);
            } else {
                const uint16_t mask = fetch_word();
                int count = 0;
                for (int bit = 0; bit < 16; ++bit) {
                    if (mask & (1u << bit)) ++count;
                }
                cycles_ += count << 2;
                if (dir <= 0x27) cycles_ += 12;
                else if (dir <= 0x2f || dir == 0x38) cycles_ += 16;
                else if (dir <= 0x37) cycles_ += 18;
                else if (dir == 0x39) cycles_ += 20;
                if (dir >= 0x20 && dir <= 0x27) {  // -(An), reversed order
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        const int index = 15 - bit;
                        write_w(dir, index < 8 ? ((uint16_t)(d[(size_t)(index)]).l) : ((uint16_t)(a[(size_t)(index - 8)]).l));
                    }
                } else {
                    uint32_t address = read_ea(dir);
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        putword(address, bit < 8 ? ((uint16_t)(d[(size_t)(bit)]).l) : ((uint16_t)(a[(size_t)(bit - 8)]).l));
                        address += 2;
                    }
                }
            }
            break;
        }
        case 0x23: {  // ext.l / movem.l register to memory
            if ((dir >> 3) == 0) {
                cycles_ += 4;
                const uint32_t result = (uint32_t)((int32_t)((int16_t)(((uint16_t)(d[orig]).l))));
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x80000000u) != 0;
                cc.z = result == 0;
                d[orig].l = result;
            } else {
                const uint16_t mask = fetch_word();
                int count = 0;
                for (int bit = 0; bit < 16; ++bit) {
                    if (mask & (1u << bit)) ++count;
                }
                cycles_ += count << 3;
                if (dir <= 0x27) cycles_ += 8;
                else if (dir <= 0x2f || dir == 0x38) cycles_ += 12;
                else if (dir <= 0x37) cycles_ += 14;
                else if (dir == 0x39) cycles_ += 16;
                if (dir >= 0x20 && dir <= 0x27) {
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        const int index = 15 - bit;
                        write_l(dir, index < 8 ? d[(size_t)(index)].l : a[(size_t)(index - 8)].l);
                    }
                } else {
                    uint32_t address = read_ea(dir);
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1u << bit)) == 0) continue;
                        const uint32_t value = bit < 8 ? d[(size_t)(bit)].l : a[(size_t)(bit - 8)].l;
                        putword(address, (uint16_t)(value >> 16));
                        putword(address + 2, (uint16_t)(value));
                        address += 4;
                    }
                }
            }
            break;
        }
        case 0x28: {  // tst.b
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint8_t value = read_b(dir);
            cc.v = false;
            cc.c = false;
            cc.n = (value & 0x80) != 0;
            cc.z = value == 0;
            break;
        }
        case 0x29: {  // tst.w
            cycles_ += 4 + calc_ea_t_bw(dir);
            const uint16_t value = read_w(dir);
            cc.v = false;
            cc.c = false;
            cc.n = (value & 0x8000) != 0;
            cc.z = value == 0;
            break;
        }
        case 0x2a: {  // tst.l
            cycles_ += 4 + calc_ea_t_l(dir);
            const uint32_t value = read_l(dir);
            cc.v = false;
            cc.c = false;
            cc.n = (value & 0x80000000u) != 0;
            cc.z = value == 0;
            break;
        }
        case 0x2b: {  // tas
            cycles_ += (dir >> 3) != 0 ? 14 + calc_ea_t_bw(dir) : 4;
            const uint8_t value = read_b(dir);
            cc.z = value == 0;
            cc.n = (value & 0x80) != 0;
            cc.v = false;
            cc.c = false;
            write_b2(dir, (uint8_t)(value | 0x80));
            break;
        }
        case 0x30: {
            if (type_ != M68K_T_68020) { pc_.l = ppc_.l; exception(4, 34); break; }
            const uint16_t ext = fetch_word();
            const size_t dq = (size_t)(ext & 7);
            const size_t dr = (size_t)((ext >> 12) & 7);
            const bool size64 = (ext & 0x0400) != 0;
            const bool is_signed = (ext & 0x0800) != 0;
            const uint32_t ea_val = (dir <= 7) ? d[(size_t)(dir)].l : read_l(dir);
            cycles_ += 40;
            if (is_signed) {
                const int64_t r = (int64_t)((int32_t)(d[dq].l)) * (int64_t)((int32_t)(ea_val));
                d[dq].l = (uint32_t)((uint64_t)(r));
                if (size64) d[dr].l = (uint32_t)((uint64_t)(r) >> 32);
                cc.n = r < 0; cc.z = (r == 0);
            } else {
                const uint64_t r = (uint64_t)(d[dq].l) * (uint64_t)(ea_val);
                d[dq].l = (uint32_t)(r);
                if (size64) d[dr].l = (uint32_t)(r >> 32);
                cc.n = size64 ? ((d[dr].l & 0x80000000u) != 0) : ((d[dq].l & 0x80000000u) != 0);
                cc.z = (r == 0);
            }
            cc.v = false; cc.c = false;
            break;
        }
        case 0x32: {  // movem.w memory to register
            const uint16_t mask = fetch_word();
            int count = 0;
            for (int bit = 0; bit < 16; ++bit) {
                if (mask & (1u << bit)) ++count;
            }
            cycles_ += count << 2;
            if (dir <= 0x1f) cycles_ += 12;
            else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) cycles_ += 16;
            else if (dir <= 0x37 || dir == 0x3b) cycles_ += 18;
            else if (dir == 0x39) cycles_ += 20;
            if (dir >= 0x18 && dir <= 0x1f) {  // (An)+
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = (uint32_t)((int32_t)((int16_t)(read_w(dir))));
                    if (bit < 8) d[(size_t)(bit)].l = value;
                    else a[(size_t)(bit - 8)].l = value;
                }
            } else {
                uint32_t address = read_ea(dir);
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = (uint32_t)((int32_t)((int16_t)(getword(address))));
                    if (bit < 8) d[(size_t)(bit)].l = value;
                    else a[(size_t)(bit - 8)].l = value;
                    address += 2;
                }
            }
            break;
        }
        case 0x33: {  // movem.l memory to register
            const uint16_t mask = fetch_word();
            int count = 0;
            for (int bit = 0; bit < 16; ++bit) {
                if (mask & (1u << bit)) ++count;
            }
            cycles_ += count << 3;
            if (dir <= 0x1f) cycles_ += 12;
            else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) cycles_ += 16;
            else if (dir <= 0x37 || dir == 0x3b) cycles_ += 18;
            else if (dir == 0x39) cycles_ += 20;
            if (dir >= 0x18 && dir <= 0x1f) {  // (An)+
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = read_l(dir);
                    if (bit < 8) d[(size_t)(bit)].l = value;
                    else a[(size_t)(bit - 8)].l = value;
                }
            } else {
                uint32_t address = read_ea(dir);
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1u << bit)) == 0) continue;
                    const uint32_t value = ((uint32_t)(getword(address)) << 16) | getword(address + 2);
                    if (bit < 8) d[(size_t)(bit)].l = value;
                    else a[(size_t)(bit - 8)].l = value;
                    address += 4;
                }
            }
            break;
        }
        case 0x39: {  // trap, link, unlk, usp, reset, nop, stop, rte, rts, rtr
            if (dir <= 0x0f) {  // trap
                cycles_ += 38;
                const uint16_t flags = get_flags();
                set_flags((uint16_t)(flags | 0x2000));
                cc.t = false;
                a[7].l -= 6;
                putword(a[7].l + 4, ((uint16_t)(pc_).l));
                putword(a[7].l + 2, ((uint16_t)((pc_).l >> 16)));
                putword(a[7].l, flags);
                opcode_ = false;
                const uint32_t vec_addr = vbr_ + 0x80u + (uint32_t)(instruction & 0x0f) * 4u;
                R_SET_WH(pc_, getword(vec_addr));
                R_SET_WL(pc_, getword(vec_addr + 2));
            } else if (dir <= 0x17) {  // link
                cycles_ += 16;
                const int16_t displacement = (int16_t)(fetch_word());
                a[7].l -= 4;
                putword(a[7].l, ((uint16_t)((a[orig]).l >> 16)));
                putword(a[7].l + 2, ((uint16_t)(a[orig]).l));
                a[orig].l = a[7].l;
                a[7].l += (uint32_t)((int32_t)(displacement));
            } else if (dir <= 0x1f) {  // unlk
                cycles_ += 12;
                a[7].l = a[orig].l;
                R_SET_WH(a[orig], getword(a[7].l));
                R_SET_WL(a[orig], getword(a[7].l + 2));
                a[7].l += 4;
            } else if (dir <= 0x2f) {  // move usp
                cycles_ += 4;
                if (check_supervisor()) {
                    if (((dir >> 3) & 1) == 1) a[orig].l = other_sp_.l;
                    else other_sp_.l = a[orig].l;
                }
            } else {
                switch (dir) {
                    case 0x30:  // reset
                        if (check_supervisor()) {
                            cycles_ += 40;
                            if (mk->reset_instr) mk->reset_instr(mk->user);
                        }
                        break;
                    case 0x31:  // nop
                        cycles_ += 4;
                        break;
                    case 0x32:  // stop
                        if (check_supervisor()) {
                            set_flags(fetch_word());
                            cycles_ += 4;
                            halted_ = true;
                        }
                        break;
                    case 0x33:  // rte
                        if (check_supervisor()) {
                            if (type_ == M68K_T_68000) {
                                cycles_ += 20;
                                const uint16_t flags = getword(a[7].l);
                                R_SET_WH(pc_, getword(a[7].l + 2));
                                R_SET_WL(pc_, getword(a[7].l + 4));
                                a[7].l += 6;
                                set_flags(flags);
                            } else {
                                cycles_ += 24;
                                const uint16_t flags = getword(a[7].l);
                                R_SET_WH(pc_, getword(a[7].l + 2));
                                R_SET_WL(pc_, getword(a[7].l + 4));
                                a[7].l += 8;  // the 68010 also stacks a format word
                                set_flags(flags);
                            }
                            
                        }
                        break;
                    case 0x35:  // rts
                        cycles_ += 16;
                        R_SET_WH(pc_, getword(a[7].l));
                        R_SET_WL(pc_, getword(a[7].l + 2));
                        a[7].l += 4;
                        break;
                    case 0x36:  // trapv: vector 7 when V is set, else a no-op
                        if (cc.v) {
                            cycles_ += 34;
                            const uint16_t flags = get_flags();
                            set_flags((uint16_t)(flags | 0x2000));
                            cc.t = false;
                            // Stack the address of the next instruction.
                            if (type_ != M68K_T_68000) {
                                a[7].l -= 2;
                                putword(a[7].l, (uint16_t)(7 << 2));
                            }
                            a[7].l -= 6;
                            putword(a[7].l + 4, ((uint16_t)(pc_).l));
                            putword(a[7].l + 2, ((uint16_t)((pc_).l >> 16)));
                            putword(a[7].l, flags);
                            opcode_ = false;
                            const uint32_t vec_addr = vbr_ + 7u * 4u;
                            R_SET_WH(pc_, getword(vec_addr));
                            R_SET_WL(pc_, getword(vec_addr + 2));
                        } else {
                            cycles_ += 4;
                        }
                        break;
                    case 0x37:  // rtr
                        cycles_ += 20;
                        set_flags(getword(a[7].l));
                        R_SET_WH(pc_, getword(a[7].l + 2));
                        R_SET_WL(pc_, getword(a[7].l + 4));
                        a[7].l += 6;
                        break;
                    case 0x3a:
                    case 0x3b: {
                        if (type_ == M68K_T_68000) { pc_.l = ppc_.l; exception(4, 34); break; }
                        if (!check_supervisor()) break;
                        cycles_ += 12;
                        const uint16_t ext = fetch_word();
                        const size_t reg = (size_t)((ext >> 12) & 0xf);
                        const uint16_t cr = (uint16_t)(ext & 0xfff);
                        uint32_t* rp = (reg & 8) ? &a[reg & 7].l : &d[reg & 7].l;
                        if ((instruction & 1) == 0) *rp = m68_read_cr(mk, cr);
                        else m68_write_cr(mk, cr, *rp);
                        break;
                    }
                    default:
                        pc_.l = ppc_.l;
                        exception(4, 34);
                        break;
                }
            }
            break;
        }
        case 0x3a: {  // jsr
            const uint32_t address = read_ea(dir);
            a[7].l -= 4;
            putword(a[7].l, ((uint16_t)((pc_).l >> 16)));
            putword(a[7].l + 2, ((uint16_t)(pc_).l));
            pc_.l = address;
            if (dir <= 0x17) cycles_ += 16;
            else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 18;
            else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b) cycles_ += 22;
            else if (dir == 0x39) cycles_ += 20;
            break;
        }
        case 0x3b: {  // jmp
            pc_.l = read_ea(dir);
            if (dir <= 0x17) cycles_ += 8;
            else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles_ += 10;
            else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b) cycles_ += 14;
            else if (dir == 0x39) cycles_ += 12;
            break;
        }
        default:
            pc_.l = ppc_.l;
            exception(4, 34);
            break;
    }
}

static void m68_group_2(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    if (((instruction >> 6) & 7) == 1) {  // movea.l
        cycles_ += (dir >> 3) > 1 ? 4 + calc_ea_t_l(dir) : 4;
        a[(instruction >> 9) & 7].l = read_l(dir);
    } else {
        move_l(instruction);
    }
}

static void m68_group_3(m68kd *mk, uint16_t instruction) {
    const uint8_t dir = (uint8_t)(instruction & 0x3f);
    if (((instruction >> 6) & 7) == 1) {  // movea.w
        cycles_ += (dir >> 3) > 1 ? 4 + calc_ea_t_bw(dir) : 4;
        a[(instruction >> 9) & 7].l = (uint32_t)((int32_t)((int16_t)(read_w(dir))));
    } else {
        move_bw(instruction, false);
    }
}

static void m68_group_a(m68kd *mk, uint16_t instruction) {
    (void)instruction;
    cycles_ += 34;
    
    exception(10, 34);
}

static void m68_group_f(m68kd *mk, uint16_t instruction) {
    // A real 68020 without an FPU coprocessor attached traps every F-line
    // opcode straight to the Line 1111 Emulator exception (vector 11) as
    // soon as it recognizes the F-line prefix — it never needs to know the
    // full instruction's length, since that's entirely the software
    // handler's job (real FPU emulation, or just "no coprocessor present").
    // The previous code tried to guess how many extension words to skip
    // and silently continue, which both executes FPU instructions as
    // no-ops when real ROMs expect them to fault, and guesses wrong for
    // several addressing-mode/format combinations (e.g. an immediate
    // extended-precision source needs several more words than modes 5-7
    // account for) — landing PC at a garbage mid-instruction address on
    // return. Just raise the exception unconditionally, like the A-line
    // path already does.
    (void)instruction;
    exception(11, 34);
}



static bool m68_take_irq(m68kd *mk) {
    // Before every instruction: nothing asserted above the mask, nothing to do.
    if ((irq_lines_ >> (cc.im + 1)) == 0) return false;
    for (int level = 7; level >= 1; --level) {
        if (cc.im >= level || irq_[level] == M68_CLEAR) continue;
        halted_ = false;
        cycles_ += 44;
        const uint16_t flags = get_flags();
        set_flags((uint16_t)(flags | 0x2000));
        cc.t = false;
        if (type_ != M68K_T_68000) {
            a[7].l -= 2;
            putword(a[7].l, (uint16_t)(level << 2));
        }
        a[7].l -= 6;
        putword(a[7].l, flags);
        putword(a[7].l + 2, ((uint16_t)((pc_).l >> 16)));
        putword(a[7].l + 4, ((uint16_t)(pc_).l));
        
        opcode_ = false;
        uint32_t vec_addr = 0x64 + (uint32_t)((level - 1) * 4);
        if (mk->irq_ack) {
            const int vec = mk->irq_ack(mk->user, level);
            if (vec >= 0) vec_addr = (uint32_t)(vec) * 4u;
        }
        R_SET_WH(pc_, getword(vbr_ + vec_addr));
        R_SET_WL(pc_, getword(vbr_ + vec_addr + 2));
        if (irq_[level] == M68_HOLD) {
            irq_[level] = M68_CLEAR;
            irq_lines_ = (uint8_t)(irq_lines_ & ~(1u << level));
        }
        cc.im = (uint8_t)(level);
        return true;
    }
    return false;
}

static int m68_run(m68kd *mk, int cycles) {
    cycles_ = 0;
    while (cycles_ < cycles) {
        if (reset_request_ != M68_CLEAR) {
            const uint8_t request = reset_request_;
            reset();
            if (request == M68_ASSERT) reset_request_ = M68_ASSERT;
            cycles_ = cycles;
            break;
        }
        if (halt_request_ != M68_CLEAR) {
            cycles_ += 4;
            if (mk->cycles) mk->cycles(mk->user, 4);
            continue;
        }

        const int start = cycles_;
        if ((irq_lines_ >> (cc.im + 1)) != 0) take_irq();
        if (halted_) {  // stopped by the stop instruction until an interrupt arrives
            cycles_ += 4;
            if (mk->cycles) mk->cycles(mk->user, 4);
            continue;
        }

        ppc_ = pc_;
        if (pc_.l & 1u) {
            // Address Error: real 68000-family hardware refuses to fetch an
            // instruction from an odd address at all and traps immediately
            // (vector 3) instead. Without this, a bad computed jump/return
            // that lands on an odd address was previously left to "execute"
            // whatever garbage instruction decoding fell out of it —
            // walking off into unrelated memory instead of hitting the
            // ROM's (or our seed stub's) real error handler like actual
            // hardware would.
            exception(3, 34);
            continue;
        }
        // Trace: T set when the instruction starts means a trace exception
        // after it (not after one that faulted: illegal, privilege, ...).
        const bool tracing = cc.t;
        const uint32_t exceptions_before = exceptions_;
        
        // A jump, exception or interrupt empties the prefetch queue.
        if (prefetch_ && (pq_count_ == 0 || pq_addr_ != pc_.l)) pq_count_ = 0;
        const uint16_t instruction = fetch_word();
        switch (instruction >> 12) {
            case 0x0: group_0(instruction); break;
            case 0x1: group_1(instruction); break;
            case 0x2: group_2(instruction); break;
            case 0x3: group_3(instruction); break;
            case 0x4: group_4(instruction); break;
            case 0x5: group_5(instruction); break;
            case 0x6: group_6(instruction); break;
            case 0x7: group_7(instruction); break;
            case 0x8: group_8(instruction); break;
            case 0x9: group_9(instruction); break;
            case 0xa: group_a(instruction); break;
            case 0xb: group_b(instruction); break;
            case 0xc: group_c(instruction); break;
            case 0xd: group_d(instruction); break;
            case 0xe: group_e(instruction); break;
            case 0xf: group_f(instruction); break;
            default:
                pc_.l = ppc_.l;
                exception(4, 34);  // illegal instruction
                break;
        }
        // MOVE writes its destination before the last prefetch, so the
        // second queued word is read after the write (other read-modify-
        // write instructions prefetch first, then write).
        if (prefetch_ && pq_count_ == 2 && (instruction >> 12) >= 1 && (instruction >> 12) <= 3) pq_count_ = 1;
        if (tracing && exceptions_ == exceptions_before && !halted_) {
            ppc_.l = pc_.l;  // stack the next instruction
            exception(9, 34);
        }
        if (mk->cycles) mk->cycles(mk->user, cycles_ - start);
    }
    return cycles_;
}


#undef cycles_
int m68kd_run(m68kd *mk, int cycles) { return m68_run(mk, cycles); }
