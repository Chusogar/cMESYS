// z80dsp.c - Zilog Z80: C11 port of dsp-cpp's cpu/z80.cpp. See z80dsp.h.
#include "z80dsp.h"
#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ZD_HOT IRAM_ATTR
#else
#define ZD_HOT
#endif

// T state tables, taken from the DSP emulator Z80 core (nz80.pas).
static const uint8_t kMain[256] = {
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
    8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
    7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
    7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 4, 10, 17, 7, 11,
    5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 4, 7, 11,
    5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 4, 7, 11,
    5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 4, 7, 11};

static const uint8_t kCb[256] = {
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4,
    4, 4, 4, 4, 4, 4, 11, 4, 4, 4, 4, 4, 4, 4, 11, 4};

static const uint8_t kIndex[256] = {
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
    8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
    7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
    7, 10, 13, 6, 19, 19, 15, 4, 7, 11, 13, 6, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    15, 15, 15, 15, 15, 15, 4, 15, 4, 4, 4, 4, 4, 4, 15, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
    5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 7, 10, 17, 7, 11,
    5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 4, 7, 11,
    5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 4, 7, 11,
    5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 4, 7, 11};

static const uint8_t kIndexCb[256] = {
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12};

static const uint8_t kEd[256] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    8, 8, 11, 16, 4, 10, 4, 5, 8, 8, 11, 16, 4, 10, 4, 5,
    8, 8, 11, 16, 4, 10, 4, 5, 8, 8, 11, 16, 4, 10, 4, 5,
    8, 8, 11, 16, 4, 10, 4, 14, 8, 8, 11, 16, 4, 10, 4, 14,
    8, 8, 11, 16, 4, 10, 4, 4, 8, 8, 11, 16, 4, 10, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    12, 12, 12, 12, 4, 4, 4, 4, 12, 12, 12, 12, 4, 4, 4, 4,
    12, 12, 12, 12, 4, 4, 4, 4, 12, 12, 12, 12, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

// Extra T states for taken conditional branches / repeated block instructions.
static const uint8_t kExtra[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,
    5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    5, 5, 5, 5, 0, 0, 0, 0, 5, 5, 5, 5, 0, 0, 0, 0,
    6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
    6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
    6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
    6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0};

static uint8_t kParity[256];
static bool tables_ready;

#define SF ZD_SF
#define ZF ZD_ZF
#define YF ZD_YF
#define HF ZD_HF
#define XF ZD_XF
#define PF ZD_PF
#define NF ZD_NF
#define CF ZD_CF

static inline uint8_t sz53(uint8_t v) { return (uint8_t)((v & (SF | YF | XF)) | (v == 0 ? ZF : 0)); }
static inline uint8_t sz53p(uint8_t v) { return (uint8_t)(sz53(v) | kParity[v]); }

// --- bus -------------------------------------------------------------------
static inline uint8_t rd(zd_z80 *z, uint16_t addr)
{
    uint8_t v = z->read(z->user, addr);
    z->t_in_instr += 3;
    return v;
}

static inline void wr(zd_z80 *z, uint16_t addr, uint8_t v)
{
    z->write(z->user, addr, v);
    z->t_in_instr += 3;
}

static inline void internal(zd_z80 *z, uint16_t addr, int count)
{
    if (!z->internal) return;
    for (int k = 0; k < count; ++k) {
        z->internal(z->user, addr);
        ++z->t_in_instr;
    }
}

static inline uint8_t fetch(zd_z80 *z)
{
    const bool m1 = z->fetching_opcode;
    uint8_t v = z->read(z->user, z->pc);
    z->fetching_opcode = false;
    z->pc = (uint16_t)(z->pc + 1);
    // M1 opcode fetch is 4 T; immediate/operand bytes are 3 T.
    z->t_in_instr += m1 ? 4 : 3;
    return v;
}

static inline uint16_t fetch16(zd_z80 *z)
{
    uint16_t v = rd(z, z->pc);
    v = (uint16_t)(v | (rd(z, (uint16_t)(z->pc + 1)) << 8));
    z->pc = (uint16_t)(z->pc + 2);
    return v;
}

static inline void push(zd_z80 *z, uint16_t v)
{
    z->sp = (uint16_t)(z->sp - 1);
    wr(z, z->sp, (uint8_t)(v >> 8));
    z->sp = (uint16_t)(z->sp - 1);
    wr(z, z->sp, (uint8_t)v);
}

static inline uint16_t pop(zd_z80 *z)
{
    uint16_t v = rd(z, z->sp);
    z->sp = (uint16_t)(z->sp + 1);
    v = (uint16_t)(v | (rd(z, z->sp) << 8));
    z->sp = (uint16_t)(z->sp + 1);
    return v;
}

static inline uint8_t in_(zd_z80 *z, uint16_t port) { return z->in(z->user, port); }
static inline void out_(zd_z80 *z, uint16_t port, uint8_t v) { z->out(z->user, port, v); }

#define BC() ((uint16_t)((z->b << 8) | z->c))
#define DE() ((uint16_t)((z->d << 8) | z->e))
#define HL() ((uint16_t)((z->h << 8) | z->l))
#define AF() ((uint16_t)((z->a << 8) | z->f))
#define IR() ((uint16_t)((z->i << 8) | z->r))
static inline void set_bc(zd_z80 *z, uint16_t v) { z->b = (uint8_t)(v >> 8); z->c = (uint8_t)v; }
static inline void set_de(zd_z80 *z, uint16_t v) { z->d = (uint8_t)(v >> 8); z->e = (uint8_t)v; }
static inline void set_hl(zd_z80 *z, uint16_t v) { z->h = (uint8_t)(v >> 8); z->l = (uint8_t)v; }
static inline void set_af(zd_z80 *z, uint16_t v) { z->a = (uint8_t)(v >> 8); z->f = (uint8_t)v; }

static inline void bump_r_m1(zd_z80 *z)
{
    z->r = (uint8_t)(((z->r + 1) & 0x7f) | (z->r & 0x80));
    if (z->m1) z->m1(z->user);
}

// B C D E H L (HL) A
static uint8_t *reg8(zd_z80 *z, int idx)
{
    switch (idx) {
    case 0: return &z->b;
    case 1: return &z->c;
    case 2: return &z->d;
    case 3: return &z->e;
    case 4: return &z->h;
    case 5: return &z->l;
    case 7: return &z->a;
    default: return NULL;
    }
}

// --- ALU -------------------------------------------------------------------
static void add_a(zd_z80 *z, uint8_t value)
{
    uint16_t result = (uint16_t)(z->a + value);
    z->f = (uint8_t)(sz53((uint8_t)result) | ((result > 0xff) ? CF : 0) |
                     (((z->a ^ value ^ (uint8_t)result) & HF) ? HF : 0) |
                     ((((z->a ^ value ^ 0x80) & (z->a ^ (uint8_t)result)) & 0x80) ? PF : 0));
    z->a = (uint8_t)result;
}

static void adc_a(zd_z80 *z, uint8_t value)
{
    uint8_t carry = z->f & CF;
    uint16_t result = (uint16_t)(z->a + value + carry);
    z->f = (uint8_t)(sz53((uint8_t)result) | ((result > 0xff) ? CF : 0) |
                     (((z->a ^ value ^ (uint8_t)result) & HF) ? HF : 0) |
                     ((((z->a ^ value ^ 0x80) & (z->a ^ (uint8_t)result)) & 0x80) ? PF : 0));
    z->a = (uint8_t)result;
}

static void sub_a(zd_z80 *z, uint8_t value)
{
    int result = z->a - value;
    z->f = (uint8_t)(sz53((uint8_t)result) | NF | ((result < 0) ? CF : 0) |
                     (((z->a ^ value ^ (uint8_t)result) & HF) ? HF : 0) |
                     ((((z->a ^ value) & (z->a ^ (uint8_t)result)) & 0x80) ? PF : 0));
    z->a = (uint8_t)result;
}

static void sbc_a(zd_z80 *z, uint8_t value)
{
    uint8_t carry = z->f & CF;
    int result = z->a - value - carry;
    z->f = (uint8_t)(sz53((uint8_t)result) | NF | ((result < 0) ? CF : 0) |
                     (((z->a ^ value ^ (uint8_t)result) & HF) ? HF : 0) |
                     ((((z->a ^ value) & (z->a ^ (uint8_t)result)) & 0x80) ? PF : 0));
    z->a = (uint8_t)result;
}

static void and_a(zd_z80 *z, uint8_t value) { z->a &= value; z->f = (uint8_t)(sz53p(z->a) | HF); }
static void xor_a(zd_z80 *z, uint8_t value) { z->a ^= value; z->f = sz53p(z->a); }
static void or_a(zd_z80 *z, uint8_t value) { z->a |= value; z->f = sz53p(z->a); }

static void cp_a(zd_z80 *z, uint8_t value)
{
    int result = z->a - value;
    z->f = (uint8_t)(((uint8_t)result & SF) | (((uint8_t)result == 0) ? ZF : 0) | NF |
                     (value & (YF | XF)) | ((result < 0) ? CF : 0) |
                     (((z->a ^ value ^ (uint8_t)result) & HF) ? HF : 0) |
                     ((((z->a ^ value) & (z->a ^ (uint8_t)result)) & 0x80) ? PF : 0));
}

static uint8_t inc8(zd_z80 *z, uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1);
    z->f = (uint8_t)((z->f & CF) | sz53(result) | ((result == 0x80) ? PF : 0) |
                     (((result & 0x0f) == 0) ? HF : 0));
    return result;
}

static uint8_t dec8(zd_z80 *z, uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1);
    z->f = (uint8_t)((z->f & CF) | NF | sz53(result) | ((result == 0x7f) ? PF : 0) |
                     (((result & 0x0f) == 0x0f) ? HF : 0));
    return result;
}

static uint16_t add16(zd_z80 *z, uint16_t x, uint16_t y)
{
    uint32_t result = (uint32_t)x + y;
    z->wz = (uint16_t)(x + 1);
    z->f = (uint8_t)((z->f & (SF | ZF | PF)) | (((x ^ y ^ (uint16_t)result) >> 8) & HF) |
                     ((result > 0xffff) ? CF : 0) | ((result >> 8) & (YF | XF)));
    return (uint16_t)result;
}

static void adc_hl(zd_z80 *z, uint16_t value)
{
    uint16_t x = HL();
    uint32_t result = (uint32_t)x + value + (z->f & CF);
    z->wz = (uint16_t)(x + 1);
    uint16_t r16 = (uint16_t)result;
    z->f = (uint8_t)(((r16 >> 8) & (SF | YF | XF)) | ((r16 == 0) ? ZF : 0) |
                     ((result > 0xffff) ? CF : 0) | (((x ^ value ^ r16) >> 8) & HF) |
                     ((((x ^ value ^ 0x8000) & (x ^ r16)) & 0x8000) ? PF : 0));
    set_hl(z, r16);
}

static void sbc_hl(zd_z80 *z, uint16_t value)
{
    uint16_t x = HL();
    int32_t result = (int32_t)x - value - (z->f & CF);
    z->wz = (uint16_t)(x + 1);
    uint16_t r16 = (uint16_t)result;
    z->f = (uint8_t)(((r16 >> 8) & (SF | YF | XF)) | ((r16 == 0) ? ZF : 0) | NF |
                     ((result < 0) ? CF : 0) | (((x ^ value ^ r16) >> 8) & HF) |
                     ((((x ^ value) & (x ^ r16)) & 0x8000) ? PF : 0));
    set_hl(z, r16);
}

static void daa(zd_z80 *z)
{
    uint8_t correction = 0;
    uint8_t carry = z->f & CF;
    if ((z->f & HF) || (z->a & 0x0f) > 9) correction |= 0x06;
    if (carry || z->a > 0x99) {
        correction |= 0x60;
        carry = CF;
    }
    uint8_t before = z->a;
    if (z->f & NF) z->a = (uint8_t)(z->a - correction);
    else z->a = (uint8_t)(z->a + correction);
    z->f = (uint8_t)(sz53p(z->a) | carry | (z->f & NF) | (((before ^ z->a) & HF) ? HF : 0));
}

static void rrd(zd_z80 *z)
{
    uint8_t value = rd(z, HL());
    internal(z, HL(), 4);
    wr(z, HL(), (uint8_t)((value >> 4) | (z->a << 4)));
    z->a = (uint8_t)((z->a & 0xf0) | (value & 0x0f));
    z->wz = (uint16_t)(HL() + 1);
    z->f = (uint8_t)((z->f & CF) | sz53p(z->a));
}

static void rld(zd_z80 *z)
{
    uint8_t value = rd(z, HL());
    internal(z, HL(), 4);
    wr(z, HL(), (uint8_t)((value << 4) | (z->a & 0x0f)));
    z->a = (uint8_t)((z->a & 0xf0) | (value >> 4));
    z->wz = (uint16_t)(HL() + 1);
    z->f = (uint8_t)((z->f & CF) | sz53p(z->a));
}

static uint8_t rot(zd_z80 *z, int kind, uint8_t value)
{
    uint8_t carry, result;
    switch (kind) {
    case 0: carry = (uint8_t)(value >> 7); result = (uint8_t)((value << 1) | carry); break;               // rlc
    case 1: carry = (uint8_t)(value & 1); result = (uint8_t)((value >> 1) | (carry << 7)); break;         // rrc
    case 2: carry = (uint8_t)(value >> 7); result = (uint8_t)((value << 1) | (z->f & CF)); break;          // rl
    case 3: carry = (uint8_t)(value & 1); result = (uint8_t)((value >> 1) | ((z->f & CF) << 7)); break;  // rr
    case 4: carry = (uint8_t)(value >> 7); result = (uint8_t)(value << 1); break;                         // sla
    case 5: carry = (uint8_t)(value & 1); result = (uint8_t)((value >> 1) | (value & 0x80)); break;       // sra
    case 6: carry = (uint8_t)(value >> 7); result = (uint8_t)((value << 1) | 1); break;                   // sll
    default: carry = (uint8_t)(value & 1); result = (uint8_t)(value >> 1); break;                         // srl
    }
    z->f = (uint8_t)(sz53p(result) | carry);
    return result;
}

static void bit(zd_z80 *z, uint8_t index, uint8_t value, uint8_t xy_source)
{
    uint8_t masked = (uint8_t)(value & (1 << index));
    z->f = (uint8_t)((z->f & CF) | HF | (masked ? (masked & SF) : (ZF | PF)) | (xy_source & (YF | XF)));
}

static void block_ld(zd_z80 *z, int delta, bool repeat)
{
    uint8_t value = rd(z, HL());
    wr(z, DE(), value);
    internal(z, DE(), 2);
    set_hl(z, (uint16_t)(HL() + delta));
    set_de(z, (uint16_t)(DE() + delta));
    set_bc(z, (uint16_t)(BC() - 1));
    uint8_t n = (uint8_t)(z->a + value);
    z->f = (uint8_t)((z->f & (SF | ZF | CF)) | ((n & 0x02) ? YF : 0) | ((n & 0x08) ? XF : 0) |
                     (BC() != 0 ? PF : 0));
    if (repeat && BC() != 0) {
        internal(z, (uint16_t)(DE() - delta), 5);
        z->pc = (uint16_t)(z->pc - 2);
        z->wz = (uint16_t)(z->pc + 1);
        z->cycles_ += kExtra[delta > 0 ? 0xb0 : 0xb8];
    }
}

static void block_cp(zd_z80 *z, int delta, bool repeat)
{
    uint8_t value = rd(z, HL());
    internal(z, HL(), 5);
    uint8_t result = (uint8_t)(z->a - value);
    uint8_t half = (uint8_t)(((z->a ^ value ^ result) & HF) ? HF : 0);
    set_hl(z, (uint16_t)(HL() + delta));
    set_bc(z, (uint16_t)(BC() - 1));
    uint8_t n = (uint8_t)(result - (half ? 1 : 0));
    z->f = (uint8_t)((z->f & CF) | NF | (result & SF) | ((result == 0) ? ZF : 0) | half |
                     ((n & 0x02) ? YF : 0) | ((n & 0x08) ? XF : 0) | (BC() != 0 ? PF : 0));
    z->wz = (uint16_t)(z->wz + delta);
    if (repeat && BC() != 0 && result != 0) {
        internal(z, (uint16_t)(HL() - delta), 5);
        z->pc = (uint16_t)(z->pc - 2);
        z->wz = (uint16_t)(z->pc + 1);
        z->cycles_ += kExtra[delta > 0 ? 0xb1 : 0xb9];
    }
}

static void block_in(zd_z80 *z, int delta, bool repeat)
{
    internal(z, IR(), 1);
    uint8_t value = in_(z, BC());
    if (z->internal) z->t_in_instr += 4;
    wr(z, HL(), value);
    z->wz = (uint16_t)(BC() + delta);
    z->b = (uint8_t)(z->b - 1);
    set_hl(z, (uint16_t)(HL() + delta));
    unsigned sum = (unsigned)value + ((z->c + delta) & 0xff);
    z->f = (uint8_t)(sz53(z->b) | ((value & 0x80) ? NF : 0) | ((sum > 0xff) ? (HF | CF) : 0) |
                     kParity[(sum & 7) ^ z->b]);
    if (repeat && z->b != 0) {
        internal(z, (uint16_t)(HL() - delta), 5);
        z->pc = (uint16_t)(z->pc - 2);
        z->cycles_ += kExtra[delta > 0 ? 0xb2 : 0xba];
    }
}

static void block_out(zd_z80 *z, int delta, bool repeat)
{
    internal(z, IR(), 1);
    uint8_t value = rd(z, HL());
    z->b = (uint8_t)(z->b - 1);
    z->wz = (uint16_t)(BC() + delta);
    out_(z, BC(), value);
    if (z->internal) z->t_in_instr += 4;
    set_hl(z, (uint16_t)(HL() + delta));
    unsigned sum = (unsigned)value + z->l;
    z->f = (uint8_t)(sz53(z->b) | ((value & 0x80) ? NF : 0) | ((sum > 0xff) ? (HF | CF) : 0) |
                     kParity[(sum & 7) ^ z->b]);
    if (repeat && z->b != 0) {
        internal(z, BC(), 5);
        z->pc = (uint16_t)(z->pc - 2);
        z->cycles_ += kExtra[delta > 0 ? 0xb3 : 0xbb];
    }
}

static int take_nmi(zd_z80 *z)
{
    if (z->nmi_latched) return 0;
    z->halted = false;
    z->iff2 = z->iff1;
    z->iff1 = false;
    push(z, z->pc);
    z->pc = 0x0066;
    z->wz = z->pc;
    z->r = (uint8_t)(((z->r + 1) & 0x7f) | (z->r & 0x80));
    if (z->nmi_state == ZD_PULSE || z->nmi_state == ZD_HOLD) z->nmi_state = ZD_CLEAR;
    else z->nmi_latched = true;
    return 11;
}

static int take_irq(zd_z80 *z)
{
    if (!z->iff1) return 0;
    z->halted = false;
    z->iff1 = z->iff2 = false;
    if (z->irq_state == ZD_HOLD || z->irq_state == ZD_PULSE) z->irq_state = ZD_CLEAR;
    z->r = (uint8_t)(((z->r + 1) & 0x7f) | (z->r & 0x80));
    int tstates = 13;
    switch (z->im) {
    case 0:     // only the RST n form used by the supported drivers is decoded here
        push(z, z->pc);
        z->pc = (uint16_t)(z->irq_vector & 0x38);
        z->wz = z->pc;
        tstates = 13;
        break;
    case 1:
        push(z, z->pc);
        z->pc = 0x0038;
        z->wz = z->pc;
        tstates = 13;
        break;
    default: {
        uint16_t addr = (uint16_t)((z->i << 8) | z->irq_vector);
        push(z, z->pc);
        z->pc = (uint16_t)(rd(z, addr) | (rd(z, (uint16_t)(addr + 1)) << 8));
        z->wz = z->pc;
        tstates = 19;
        break;
    }
    }
    return tstates;
}

static bool cond(zd_z80 *z, int cc)
{
    switch (cc & 7) {
    case 0: return !(z->f & ZF);
    case 1: return (z->f & ZF) != 0;
    case 2: return !(z->f & CF);
    case 3: return (z->f & CF) != 0;
    case 4: return !(z->f & PF);
    case 5: return (z->f & PF) != 0;
    case 6: return !(z->f & SF);
    default: return (z->f & SF) != 0;
    }
}

static void alu(zd_z80 *z, int op, uint8_t value)
{
    switch (op & 7) {
    case 0: add_a(z, value); break;
    case 1: adc_a(z, value); break;
    case 2: sub_a(z, value); break;
    case 3: sbc_a(z, value); break;
    case 4: and_a(z, value); break;
    case 5: xor_a(z, value); break;
    case 6: or_a(z, value); break;
    default: cp_a(z, value); break;
    }
}

static void exec_cb(zd_z80 *z)
{
    z->fetching_opcode = true;
    uint8_t opcode = fetch(z);
    bump_r_m1(z);
    z->cycles_ += kCb[opcode];

    uint8_t index = (uint8_t)(opcode & 7);
    uint8_t value = (index == 6) ? rd(z, HL()) : *reg8(z, index);
    if (index == 6) internal(z, HL(), 1);
    uint8_t op = (uint8_t)(opcode >> 6);
    uint8_t bit_index = (uint8_t)((opcode >> 3) & 7);

    if (op == 1) {
        bit(z, bit_index, value, (index == 6) ? (uint8_t)(z->wz >> 8) : value);
        return;
    }
    uint8_t result;
    if (op == 0) result = rot(z, bit_index, value);
    else if (op == 2) result = (uint8_t)(value & ~(1 << bit_index));
    else result = (uint8_t)(value | (1 << bit_index));

    if (index == 6) wr(z, HL(), result);
    else *reg8(z, index) = result;
}

static uint16_t rr_of(zd_z80 *z, int sel)
{
    switch (sel & 3) {
    case 0: return BC();
    case 1: return DE();
    case 2: return HL();
    default: return z->sp;
    }
}

static void exec_ed(zd_z80 *z)
{
    z->fetching_opcode = true;
    uint8_t opcode = fetch(z);
    bump_r_m1(z);
    z->cycles_ += kEd[opcode];

    switch (opcode) {
    case 0x40: case 0x48: case 0x50: case 0x58:
    case 0x60: case 0x68: case 0x70: case 0x78: {      // in r,(c)
        uint8_t value = in_(z, BC());
        z->wz = (uint16_t)(BC() + 1);
        z->f = (uint8_t)((z->f & CF) | sz53p(value));
        uint8_t dst = (uint8_t)((opcode >> 3) & 7);
        if (dst != 6) *reg8(z, dst) = value;
        break;
    }
    case 0x41: case 0x49: case 0x51: case 0x59:
    case 0x61: case 0x69: case 0x71: case 0x79: {      // out (c),r
        uint8_t src = (uint8_t)((opcode >> 3) & 7);
        uint8_t value = (src == 6) ? 0 : *reg8(z, src);
        out_(z, BC(), value);
        z->wz = (uint16_t)(BC() + 1);
        break;
    }
    case 0x42: case 0x52: case 0x62: case 0x72:        // sbc hl,rr
        internal(z, IR(), 7);
        sbc_hl(z, rr_of(z, opcode >> 4));
        break;
    case 0x4a: case 0x5a: case 0x6a: case 0x7a:        // adc hl,rr
        internal(z, IR(), 7);
        adc_hl(z, rr_of(z, opcode >> 4));
        break;
    case 0x43: case 0x53: case 0x63: case 0x73: {      // ld (nn),rr
        uint16_t addr = fetch16(z);
        uint16_t value = rr_of(z, opcode >> 4);
        wr(z, addr, (uint8_t)value);
        wr(z, (uint16_t)(addr + 1), (uint8_t)(value >> 8));
        z->wz = (uint16_t)(addr + 1);
        break;
    }
    case 0x4b: case 0x5b: case 0x6b: case 0x7b: {      // ld rr,(nn)
        uint16_t addr = fetch16(z);
        uint16_t value = (uint16_t)(rd(z, addr) | (rd(z, (uint16_t)(addr + 1)) << 8));
        z->wz = (uint16_t)(addr + 1);
        switch ((opcode >> 4) & 3) {
        case 0: set_bc(z, value); break;
        case 1: set_de(z, value); break;
        case 2: set_hl(z, value); break;
        default: z->sp = value; break;
        }
        break;
    }
    case 0x44: case 0x4c: case 0x54: case 0x5c:
    case 0x64: case 0x6c: case 0x74: case 0x7c: {      // neg
        uint8_t value = z->a;
        z->a = 0;
        sub_a(z, value);
        break;
    }
    case 0x45: case 0x55: case 0x65: case 0x75:
    case 0x5d: case 0x6d: case 0x7d:                   // retn
    case 0x4d:                                         // reti
        z->iff1 = z->iff2;
        z->pc = pop(z);
        z->wz = z->pc;
        break;
    case 0x46: case 0x4e: case 0x66: case 0x6e: z->im = 0; break;
    case 0x56: case 0x76: z->im = 1; break;
    case 0x5e: case 0x7e: z->im = 2; break;
    case 0x47: internal(z, IR(), 1); z->i = z->a; break;
    case 0x4f: internal(z, IR(), 1); z->r = z->a; break;
    case 0x57:
        internal(z, IR(), 1);
        z->a = z->i;
        z->f = (uint8_t)((z->f & CF) | sz53(z->a) | (z->iff2 ? PF : 0));
        break;
    case 0x5f:
        internal(z, IR(), 1);
        z->a = z->r;
        z->f = (uint8_t)((z->f & CF) | sz53(z->a) | (z->iff2 ? PF : 0));
        break;
    case 0x67: rrd(z); break;
    case 0x6f: rld(z); break;
    case 0xa0: block_ld(z, 1, false); break;
    case 0xa8: block_ld(z, -1, false); break;
    case 0xb0: block_ld(z, 1, true); break;
    case 0xb8: block_ld(z, -1, true); break;
    case 0xa1: block_cp(z, 1, false); break;
    case 0xa9: block_cp(z, -1, false); break;
    case 0xb1: block_cp(z, 1, true); break;
    case 0xb9: block_cp(z, -1, true); break;
    case 0xa2: block_in(z, 1, false); break;
    case 0xaa: block_in(z, -1, false); break;
    case 0xb2: block_in(z, 1, true); break;
    case 0xba: block_in(z, -1, true); break;
    case 0xa3: block_out(z, 1, false); break;
    case 0xab: block_out(z, -1, false); break;
    case 0xb3: block_out(z, 1, true); break;
    case 0xbb: block_out(z, -1, true); break;
    default: break;  // nop
    }
}

static uint16_t ea_index(zd_z80 *z, uint16_t *index_reg)
{
    int8_t offset = (int8_t)fetch(z);
    internal(z, (uint16_t)(z->pc - 1), 5);
    z->wz = (uint16_t)(*index_reg + offset);
    return z->wz;
}

static void exec_index_cb(zd_z80 *z, uint16_t *index_reg)
{
    int8_t offset = (int8_t)fetch(z);
    uint8_t opcode = fetch(z);
    internal(z, (uint16_t)(z->pc - 1), 2);
    z->cycles_ += kIndexCb[opcode];

    uint16_t addr = (uint16_t)(*index_reg + offset);
    z->wz = addr;
    uint8_t value = rd(z, addr);
    internal(z, addr, 1);
    uint8_t op = (uint8_t)(opcode >> 6);
    uint8_t bit_index = (uint8_t)((opcode >> 3) & 7);

    if (op == 1) {
        bit(z, bit_index, value, (uint8_t)(addr >> 8));
        return;
    }
    uint8_t result;
    if (op == 0) result = rot(z, bit_index, value);
    else if (op == 2) result = (uint8_t)(value & ~(1 << bit_index));
    else result = (uint8_t)(value | (1 << bit_index));
    wr(z, addr, result);

    // Undocumented: the result is also copied into the encoded register.
    uint8_t index = (uint8_t)(opcode & 7);
    if (index != 6) *reg8(z, index) = result;
}

static void exec_index(zd_z80 *z, uint16_t *index_reg)
{
    z->fetching_opcode = true;
    uint8_t opcode = fetch(z);
    bump_r_m1(z);
    z->cycles_ += kIndex[opcode];

    uint8_t index_high = (uint8_t)(*index_reg >> 8);
    uint8_t index_low = (uint8_t)*index_reg;
#define SET_INDEX(hi, lo) (*index_reg = (uint16_t)(((hi) << 8) | (lo)))

    switch (opcode) {
    case 0x09: internal(z, IR(), 7); *index_reg = add16(z, *index_reg, BC()); break;
    case 0x19: internal(z, IR(), 7); *index_reg = add16(z, *index_reg, DE()); break;
    case 0x21: *index_reg = fetch16(z); break;
    case 0x22: {
        uint16_t addr = fetch16(z);
        wr(z, addr, index_low);
        wr(z, (uint16_t)(addr + 1), index_high);
        z->wz = (uint16_t)(addr + 1);
        break;
    }
    case 0x23: internal(z, IR(), 2); *index_reg = (uint16_t)(*index_reg + 1); break;
    case 0x24: { uint8_t v = inc8(z, index_high); SET_INDEX(v, index_low); break; }
    case 0x25: { uint8_t v = dec8(z, index_high); SET_INDEX(v, index_low); break; }
    case 0x26: { uint8_t v = fetch(z); SET_INDEX(v, index_low); break; }
    case 0x29: internal(z, IR(), 7); *index_reg = add16(z, *index_reg, *index_reg); break;
    case 0x2a: {
        uint16_t addr = fetch16(z);
        *index_reg = (uint16_t)(rd(z, addr) | (rd(z, (uint16_t)(addr + 1)) << 8));
        z->wz = (uint16_t)(addr + 1);
        break;
    }
    case 0x2b: internal(z, IR(), 2); *index_reg = (uint16_t)(*index_reg - 1); break;
    case 0x2c: { uint8_t v = inc8(z, index_low); SET_INDEX(index_high, v); break; }
    case 0x2d: { uint8_t v = dec8(z, index_low); SET_INDEX(index_high, v); break; }
    case 0x2e: { uint8_t v = fetch(z); SET_INDEX(index_high, v); break; }
    case 0x34: {
        uint16_t addr = ea_index(z, index_reg);
        const uint8_t value = rd(z, addr);
        internal(z, addr, 1);
        wr(z, addr, inc8(z, value));
        break;
    }
    case 0x35: {
        uint16_t addr = ea_index(z, index_reg);
        const uint8_t value = rd(z, addr);
        internal(z, addr, 1);
        wr(z, addr, dec8(z, value));
        break;
    }
    case 0x36: {    // ld (ix+d),n: d and n, then 2 internal T-states
        int8_t offset = (int8_t)fetch(z);
        uint16_t addr = (uint16_t)(*index_reg + offset);
        z->wz = addr;
        const uint8_t value = fetch(z);
        internal(z, (uint16_t)(z->pc - 1), 2);
        wr(z, addr, value);
        break;
    }
    case 0x39: internal(z, IR(), 7); *index_reg = add16(z, *index_reg, z->sp); break;
    case 0xcb: exec_index_cb(z, index_reg); break;
    case 0xe1: *index_reg = pop(z); break;
    case 0xe3: {
        uint16_t value = (uint16_t)(rd(z, z->sp) | (rd(z, (uint16_t)(z->sp + 1)) << 8));
        internal(z, (uint16_t)(z->sp + 1), 1);
        wr(z, (uint16_t)(z->sp + 1), index_high);
        wr(z, z->sp, index_low);
        internal(z, z->sp, 2);
        *index_reg = value;
        z->wz = value;
        break;
    }
    case 0xe5: internal(z, IR(), 1); push(z, *index_reg); break;
    case 0xe9: z->pc = *index_reg; break;
    case 0xf9: internal(z, IR(), 2); z->sp = *index_reg; break;
    default:
        if (opcode >= 0x40 && opcode <= 0x7f && opcode != 0x76) {     // ld with index regs
            uint8_t dst = (uint8_t)((opcode >> 3) & 7);
            uint8_t src = (uint8_t)(opcode & 7);
            // With a displacement the H and L operands keep their normal
            // meaning, only the register to register forms use IXH/IXL.
            if (src == 6) {
                uint8_t value = rd(z, ea_index(z, index_reg));
                *reg8(z, dst) = value;
            } else if (dst == 6) {
                uint16_t addr = ea_index(z, index_reg);
                wr(z, addr, *reg8(z, src));
            } else {
                uint8_t value = (src == 4) ? index_high : (src == 5) ? index_low : *reg8(z, src);
                if (dst == 4) SET_INDEX(value, (uint8_t)*index_reg);
                else if (dst == 5) SET_INDEX((uint8_t)(*index_reg >> 8), value);
                else *reg8(z, dst) = value;
            }
        } else if (opcode >= 0x80 && opcode <= 0xbf) {                // alu with index regs
            uint8_t src = (uint8_t)(opcode & 7);
            uint8_t value;
            if (src == 6) value = rd(z, ea_index(z, index_reg));
            else if (src == 4) value = index_high;
            else if (src == 5) value = index_low;
            else value = *reg8(z, src);
            alu(z, opcode >> 3, value);
        } else {
            // Any other opcode behaves as if the prefix was not there. It is
            // fetched again by the main loop: undo this fetch's R increment
            // and keep interrupts out between the prefix and the opcode.
            z->pc = (uint16_t)(z->pc - 1);
            z->r = (uint8_t)(((z->r - 1) & 0x7f) | (z->r & 0x80));
            z->after_ei = true;
        }
        break;
    }
#undef SET_INDEX
}

static void exec_main(zd_z80 *z, uint8_t opcode)
{
    switch (opcode) {
    case 0x00: break;
    case 0x01: set_bc(z, fetch16(z)); break;
    case 0x02: wr(z, BC(), z->a); z->wz = (uint16_t)(((BC() + 1) & 0xff) | (z->a << 8)); break;
    case 0x03: internal(z, IR(), 2); set_bc(z, (uint16_t)(BC() + 1)); break;
    case 0x04: z->b = inc8(z, z->b); break;
    case 0x05: z->b = dec8(z, z->b); break;
    case 0x06: z->b = fetch(z); break;
    case 0x07:  // rlca
        z->f = (uint8_t)((z->f & (SF | ZF | PF)) | (z->a >> 7));
        z->a = (uint8_t)((z->a << 1) | (z->a >> 7));
        z->f = (uint8_t)(z->f | (z->a & (YF | XF)));
        break;
    case 0x08: { uint8_t t = z->a; z->a = z->a2; z->a2 = t; t = z->f; z->f = z->f2; z->f2 = t; break; }
    case 0x09: internal(z, IR(), 7); set_hl(z, add16(z, HL(), BC())); break;
    case 0x0a: z->a = rd(z, BC()); z->wz = (uint16_t)(BC() + 1); break;
    case 0x0b: internal(z, IR(), 2); set_bc(z, (uint16_t)(BC() - 1)); break;
    case 0x0c: z->c = inc8(z, z->c); break;
    case 0x0d: z->c = dec8(z, z->c); break;
    case 0x0e: z->c = fetch(z); break;
    case 0x0f:  // rrca
        z->f = (uint8_t)((z->f & (SF | ZF | PF)) | (z->a & CF));
        z->a = (uint8_t)((z->a >> 1) | (z->a << 7));
        z->f = (uint8_t)(z->f | (z->a & (YF | XF)));
        break;
    case 0x10: {  // djnz
        internal(z, IR(), 1);
        int8_t offset = (int8_t)fetch(z);
        z->b = (uint8_t)(z->b - 1);
        if (z->b != 0) {
            internal(z, (uint16_t)(z->pc - 1), 5);
            z->pc = (uint16_t)(z->pc + offset);
            z->wz = z->pc;
            z->cycles_ += kExtra[opcode];
        }
        break;
    }
    case 0x11: set_de(z, fetch16(z)); break;
    case 0x12: wr(z, DE(), z->a); z->wz = (uint16_t)(((DE() + 1) & 0xff) | (z->a << 8)); break;
    case 0x13: internal(z, IR(), 2); set_de(z, (uint16_t)(DE() + 1)); break;
    case 0x14: z->d = inc8(z, z->d); break;
    case 0x15: z->d = dec8(z, z->d); break;
    case 0x16: z->d = fetch(z); break;
    case 0x17: {  // rla
        uint8_t carry = (uint8_t)(z->a >> 7);
        z->a = (uint8_t)((z->a << 1) | (z->f & CF));
        z->f = (uint8_t)((z->f & (SF | ZF | PF)) | carry | (z->a & (YF | XF)));
        break;
    }
    case 0x18: {  // jr
        int8_t offset = (int8_t)fetch(z);
        internal(z, (uint16_t)(z->pc - 1), 5);
        z->pc = (uint16_t)(z->pc + offset);
        z->wz = z->pc;
        break;
    }
    case 0x19: internal(z, IR(), 7); set_hl(z, add16(z, HL(), DE())); break;
    case 0x1a: z->a = rd(z, DE()); z->wz = (uint16_t)(DE() + 1); break;
    case 0x1b: internal(z, IR(), 2); set_de(z, (uint16_t)(DE() - 1)); break;
    case 0x1c: z->e = inc8(z, z->e); break;
    case 0x1d: z->e = dec8(z, z->e); break;
    case 0x1e: z->e = fetch(z); break;
    case 0x1f: {  // rra
        uint8_t carry = (uint8_t)(z->a & CF);
        z->a = (uint8_t)((z->a >> 1) | ((z->f & CF) << 7));
        z->f = (uint8_t)((z->f & (SF | ZF | PF)) | carry | (z->a & (YF | XF)));
        break;
    }
    case 0x20: case 0x28: case 0x30: case 0x38: {  // jr cc
        int8_t offset = (int8_t)fetch(z);
        bool taken;
        switch (opcode) {
        case 0x20: taken = !(z->f & ZF); break;
        case 0x28: taken = (z->f & ZF) != 0; break;
        case 0x30: taken = !(z->f & CF); break;
        default: taken = (z->f & CF) != 0; break;
        }
        if (taken) {
            internal(z, (uint16_t)(z->pc - 1), 5);
            z->pc = (uint16_t)(z->pc + offset);
            z->wz = z->pc;
            z->cycles_ += kExtra[opcode];
        }
        break;
    }
    case 0x21: set_hl(z, fetch16(z)); break;
    case 0x22: {
        uint16_t addr = fetch16(z);
        wr(z, addr, z->l);
        wr(z, (uint16_t)(addr + 1), z->h);
        z->wz = (uint16_t)(addr + 1);
        break;
    }
    case 0x23: internal(z, IR(), 2); set_hl(z, (uint16_t)(HL() + 1)); break;
    case 0x24: z->h = inc8(z, z->h); break;
    case 0x25: z->h = dec8(z, z->h); break;
    case 0x26: z->h = fetch(z); break;
    case 0x27: daa(z); break;
    case 0x29: internal(z, IR(), 7); set_hl(z, add16(z, HL(), HL())); break;
    case 0x2a: {
        uint16_t addr = fetch16(z);
        z->l = rd(z, addr);
        z->h = rd(z, (uint16_t)(addr + 1));
        z->wz = (uint16_t)(addr + 1);
        break;
    }
    case 0x2b: internal(z, IR(), 2); set_hl(z, (uint16_t)(HL() - 1)); break;
    case 0x2c: z->l = inc8(z, z->l); break;
    case 0x2d: z->l = dec8(z, z->l); break;
    case 0x2e: z->l = fetch(z); break;
    case 0x2f:  // cpl
        z->a = (uint8_t)~z->a;
        z->f = (uint8_t)((z->f & (SF | ZF | PF | CF)) | HF | NF | (z->a & (YF | XF)));
        break;
    case 0x31: z->sp = fetch16(z); break;
    case 0x32: {
        uint16_t addr = fetch16(z);
        wr(z, addr, z->a);
        z->wz = (uint16_t)(((addr + 1) & 0xff) | (z->a << 8));
        break;
    }
    case 0x33: internal(z, IR(), 2); z->sp = (uint16_t)(z->sp + 1); break;
    case 0x34: {
        const uint8_t value = rd(z, HL());
        internal(z, HL(), 1);
        wr(z, HL(), inc8(z, value));
        break;
    }
    case 0x35: {
        const uint8_t value = rd(z, HL());
        internal(z, HL(), 1);
        wr(z, HL(), dec8(z, value));
        break;
    }
    case 0x36: { uint8_t v = fetch(z); wr(z, HL(), v); break; }
    case 0x37:  // scf
        z->f = (uint8_t)((z->f & (SF | ZF | PF)) | CF | (z->a & (YF | XF)));
        break;
    case 0x39: internal(z, IR(), 7); set_hl(z, add16(z, HL(), z->sp)); break;
    case 0x3a: {
        uint16_t addr = fetch16(z);
        z->a = rd(z, addr);
        z->wz = (uint16_t)(addr + 1);
        break;
    }
    case 0x3b: internal(z, IR(), 2); z->sp = (uint16_t)(z->sp - 1); break;
    case 0x3c: z->a = inc8(z, z->a); break;
    case 0x3d: z->a = dec8(z, z->a); break;
    case 0x3e: z->a = fetch(z); break;
    case 0x3f:  // ccf
        z->f = (uint8_t)((z->f & (SF | ZF | PF)) | ((z->f & CF) ? HF : CF) | (z->a & (YF | XF)));
        break;
    case 0x76: z->halted = true; break;
    default:
        if (opcode >= 0x40 && opcode <= 0x7f) {             // ld r,r'
            uint8_t dst = (uint8_t)((opcode >> 3) & 7);
            uint8_t src = (uint8_t)(opcode & 7);
            uint8_t value = (src == 6) ? rd(z, HL()) : *reg8(z, src);
            if (dst == 6) wr(z, HL(), value);
            else *reg8(z, dst) = value;
        } else if (opcode >= 0x80 && opcode <= 0xbf) {      // alu a,r
            uint8_t src = (uint8_t)(opcode & 7);
            uint8_t value = (src == 6) ? rd(z, HL()) : *reg8(z, src);
            alu(z, opcode >> 3, value);
        } else {
            switch (opcode) {
            case 0xc0: case 0xc8: case 0xd0: case 0xd8:
            case 0xe0: case 0xe8: case 0xf0: case 0xf8:     // ret cc
                internal(z, IR(), 1);
                if (cond(z, opcode >> 3)) {
                    z->pc = pop(z);
                    z->wz = z->pc;
                    z->cycles_ += kExtra[opcode];
                }
                break;
            case 0xc1: set_bc(z, pop(z)); break;
            case 0xd1: set_de(z, pop(z)); break;
            case 0xe1: set_hl(z, pop(z)); break;
            case 0xf1: set_af(z, pop(z)); break;
            case 0xc5: internal(z, IR(), 1); push(z, BC()); break;
            case 0xd5: internal(z, IR(), 1); push(z, DE()); break;
            case 0xe5: internal(z, IR(), 1); push(z, HL()); break;
            case 0xf5: internal(z, IR(), 1); push(z, AF()); break;
            case 0xc2: case 0xca: case 0xd2: case 0xda:
            case 0xe2: case 0xea: case 0xf2: case 0xfa: {   // jp cc,nn
                uint16_t addr = fetch16(z);
                z->wz = addr;
                if (cond(z, opcode >> 3)) z->pc = addr;
                break;
            }
            case 0xc3: z->pc = fetch16(z); z->wz = z->pc; break;
            case 0xc4: case 0xcc: case 0xd4: case 0xdc:
            case 0xe4: case 0xec: case 0xf4: case 0xfc: {   // call cc,nn
                uint16_t addr = fetch16(z);
                z->wz = addr;
                if (cond(z, opcode >> 3)) {
                    internal(z, (uint16_t)(z->pc - 1), 1);
                    push(z, z->pc);
                    z->pc = addr;
                    z->cycles_ += kExtra[opcode];
                }
                break;
            }
            case 0xc6: case 0xce: case 0xd6: case 0xde:
            case 0xe6: case 0xee: case 0xf6: case 0xfe: { uint8_t v = fetch(z); alu(z, opcode >> 3, v); break; }
            case 0xc7: case 0xcf: case 0xd7: case 0xdf:
            case 0xe7: case 0xef: case 0xf7: case 0xff:     // rst
                internal(z, IR(), 1);
                push(z, z->pc);
                z->pc = (uint16_t)(opcode & 0x38);
                z->wz = z->pc;
                break;
            case 0xc9: z->pc = pop(z); z->wz = z->pc; break;
            case 0xcb: exec_cb(z); break;
            case 0xcd: {
                uint16_t addr = fetch16(z);
                internal(z, (uint16_t)(z->pc - 1), 1);
                push(z, z->pc);
                z->pc = addr;
                z->wz = z->pc;
                break;
            }
            case 0xd3: {  // out (n),a
                uint8_t port = fetch(z);
                out_(z, (uint16_t)((z->a << 8) | port), z->a);
                z->wz = (uint16_t)((z->a << 8) | ((port + 1) & 0xff));
                break;
            }
            case 0xd9: {
                uint8_t t;
                t = z->b; z->b = z->b2; z->b2 = t;
                t = z->c; z->c = z->c2; z->c2 = t;
                t = z->d; z->d = z->d2; z->d2 = t;
                t = z->e; z->e = z->e2; z->e2 = t;
                t = z->h; z->h = z->h2; z->h2 = t;
                t = z->l; z->l = z->l2; z->l2 = t;
                break;
            }
            case 0xdb: {  // in a,(n)
                uint8_t port = fetch(z);
                uint16_t addr = (uint16_t)((z->a << 8) | port);
                z->a = in_(z, addr);
                if (z->internal) z->t_in_instr += 4;
                z->wz = (uint16_t)(addr + 1);
                break;
            }
            case 0xdd: exec_index(z, &z->ix); break;
            case 0xe3: {  // ex (sp),hl
                uint16_t value = (uint16_t)(rd(z, z->sp) | (rd(z, (uint16_t)(z->sp + 1)) << 8));
                internal(z, (uint16_t)(z->sp + 1), 1);
                wr(z, (uint16_t)(z->sp + 1), z->h);
                wr(z, z->sp, z->l);
                internal(z, z->sp, 2);
                set_hl(z, value);
                z->wz = value;
                break;
            }
            case 0xe9: z->pc = HL(); break;
            case 0xeb: { uint8_t t = z->d; z->d = z->h; z->h = t; t = z->e; z->e = z->l; z->l = t; break; }
            case 0xed: exec_ed(z); break;
            case 0xf3: z->iff1 = z->iff2 = false; break;
            case 0xf9: internal(z, IR(), 2); z->sp = HL(); break;
            case 0xfb: z->iff1 = z->iff2 = true; z->after_ei = true; break;
            case 0xfd: exec_index(z, &z->iy); break;
            default: break;
            }
        }
        break;
    }
}

int ZD_HOT zd_run(zd_z80 *z, int cycles)
{
    z->executed = 0;
    while (z->executed < cycles) {
        z->cycles_ = 0;
        if (!z->after_ei) {
            // NMI is edge triggered: once taken, a line still held low must
            // not block maskable interrupts.
            if (z->nmi_state != ZD_CLEAR && !z->nmi_latched) z->cycles_ += take_nmi(z);
            else if (z->irq_state != ZD_CLEAR) z->cycles_ += take_irq(z);
        }
        z->after_ei = false;

        if (z->halted) {
            z->cycles_ += 4;
            bump_r_m1(z);
            z->executed += z->cycles_;
            if (z->cycles) z->cycles(z->user, z->cycles_);
            z->t_in_instr = 0;
            continue;
        }

        if (z->hook) z->hook(z->user, z->pc);
        z->fetching_opcode = true;
        z->t_in_instr = 0;
        uint8_t opcode = fetch(z);
        bump_r_m1(z);
        z->cycles_ += kMain[opcode];
        exec_main(z, opcode);

        z->executed += z->cycles_;
        if (z->cycles) z->cycles(z->user, z->cycles_);
        z->t_in_instr = 0;
    }
    return z->executed;
}

void zd_reset(zd_z80 *z)
{
    z->a = z->f = z->b = z->c = z->d = z->e = z->h = z->l = 0;
    z->a2 = z->f2 = z->b2 = z->c2 = z->d2 = z->e2 = z->h2 = z->l2 = 0;
    z->ix = z->iy = z->wz = 0;
    z->sp = 0xffff;
    z->pc = 0;
    z->i = z->r = z->im = 0;
    z->iff1 = z->iff2 = false;
    z->halted = false;
    z->after_ei = false;
    z->irq_state = ZD_CLEAR;
    z->nmi_state = ZD_CLEAR;
    z->nmi_latched = false;
    z->irq_vector = 0xff;
}

void zd_init(zd_z80 *z)
{
    if (!tables_ready) {
        for (int v = 0; v < 256; v++) {
            int bits = 0;
            for (int b = 0; b < 8; b++) bits += (v >> b) & 1;
            kParity[v] = (bits & 1) ? 0 : PF;
        }
        tables_ready = true;
    }
    z->fetching_opcode = true;
    z->t_in_instr = 0;
    zd_reset(z);
}

void zd_set_irq(zd_z80 *z, int state, uint8_t vector)
{
    z->irq_state = (uint8_t)state;
    z->irq_vector = vector;
}

void zd_set_nmi(zd_z80 *z, int state)
{
    z->nmi_state = (uint8_t)state;
    if (state == ZD_CLEAR) z->nmi_latched = false;
}
