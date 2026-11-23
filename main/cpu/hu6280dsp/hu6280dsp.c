// hu6280dsp.c - see hu6280dsp.h. Translated from dsp-cpp's cpu/hu6280.cpp.
#include <string.h>
#include "hu6280dsp.h"

enum {
    kNone = 0x0,
    kImplied = 0x1,
    kImmediate = 0x2,
    kAbsolute = 0x3,
    kZeroPageX = 0x4,
    kAbsoluteRead = 0x5,
    kZeroPage = 0x6,
    kZeroPageRead = 0x7,
    kAbsoluteXRead = 0x8,
    kIndirectZpRead = 0x9,
    kIndirectZpYRead = 0xa,
    kIndirectZpY = 0xb,
    kAbsoluteX = 0xc,
    kIndirect = 0xd,
    kAbsoluteYRead = 0xe,
    kZeroPageXRead = 0xf,
    kAbsoluteY = 0x10,
    kIndexedIndirectRead = 0x11,
    kZeroPageYRead = 0x12,
    kZeroPageY = 0x13,
    kIndexedIndirect = 0x14,
    kIndirectZp = 0x15,
};

static const uint8_t kAddressMode[256] = {
    // 00
    kImplied, kIndexedIndirectRead, kImplied, kImmediate, kZeroPageRead, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsolute,
    kAbsoluteRead, kAbsolute, kZeroPageRead,
    // 10
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kImmediate, kZeroPageRead, kZeroPageXRead,
    kZeroPageXRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kAbsolute,
    kAbsoluteXRead, kAbsoluteX, kZeroPageRead,
    // 20
    kAbsolute, kIndexedIndirectRead, kImplied, kImmediate, kZeroPageRead, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsoluteRead,
    kAbsoluteRead, kAbsolute, kZeroPageRead,
    // 30
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kNone, kZeroPageXRead, kZeroPageXRead,
    kZeroPageXRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kAbsoluteXRead,
    kAbsoluteXRead, kAbsoluteX, kZeroPageRead,
    // 40
    kImplied, kIndexedIndirectRead, kImplied, kImmediate, kImmediate, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsolute,
    kAbsoluteRead, kAbsolute, kZeroPageRead,
    // 50
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kImmediate, kImplied, kZeroPageXRead,
    kZeroPageXRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kNone,
    kAbsoluteXRead, kAbsoluteX, kZeroPageRead,
    // 60
    kImplied, kIndexedIndirectRead, kImplied, kImplied, kZeroPage, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kIndirect,
    kAbsoluteRead, kAbsolute, kZeroPageRead,
    // 70
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kImplied, kZeroPageX, kZeroPageXRead,
    kZeroPageXRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kAbsolute,
    kAbsoluteXRead, kAbsoluteX, kZeroPageRead,
    // 80
    kImmediate, kIndexedIndirect, kImplied, kImmediate, kZeroPage, kZeroPage, kZeroPage,
    kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsolute, kAbsolute, kAbsolute,
    kZeroPageRead,
    // 90
    kImmediate, kIndirectZpY, kIndirectZp, kImmediate, kZeroPageX, kZeroPageX, kZeroPageY,
    kZeroPageRead, kImplied, kAbsoluteY, kImplied, kImplied, kAbsolute, kAbsoluteX, kAbsoluteX,
    kZeroPageRead,
    // a0
    kImmediate, kIndexedIndirectRead, kImmediate, kImmediate, kZeroPageRead, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsoluteRead,
    kAbsoluteRead, kAbsoluteRead, kZeroPageRead,
    // b0
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kImmediate, kZeroPageXRead, kZeroPageXRead,
    kZeroPageYRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kAbsoluteXRead,
    kAbsoluteXRead, kAbsoluteYRead, kZeroPageRead,
    // c0
    kImmediate, kIndexedIndirectRead, kImplied, kImplied, kZeroPageRead, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsoluteRead,
    kAbsoluteRead, kAbsolute, kZeroPageRead,
    // d0
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kImplied, kImplied, kZeroPageXRead,
    kZeroPageXRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kNone,
    kAbsoluteXRead, kAbsoluteX, kZeroPageRead,
    // e0
    kImmediate, kIndexedIndirectRead, kNone, kImplied, kZeroPageRead, kZeroPageRead,
    kZeroPageRead, kZeroPageRead, kImplied, kImmediate, kImplied, kImplied, kAbsoluteRead,
    kAbsoluteRead, kAbsolute, kZeroPageRead,
    // f0
    kImmediate, kIndirectZpYRead, kIndirectZpRead, kImplied, kImplied, kZeroPageXRead,
    kZeroPageXRead, kZeroPageRead, kImplied, kAbsoluteYRead, kImplied, kImplied, kNone,
    kAbsoluteXRead, kAbsoluteX, kZeroPageRead,
};

static const uint8_t kCycles[256] = {
    8, 7, 3, 5, 6, 4, 6, 7, 3, 2, 2, 2, 7, 5, 7, 4,
    2, 7, 7, 5, 6, 4, 6, 7, 2, 5, 2, 2, 7, 5, 7, 4,
    7, 7, 3, 5, 4, 4, 6, 7, 4, 2, 2, 2, 5, 5, 7, 4,
    2, 7, 7, 2, 4, 4, 6, 7, 2, 5, 2, 2, 5, 5, 7, 4,
    7, 7, 3, 4, 8, 4, 6, 7, 3, 2, 2, 2, 4, 5, 7, 4,
    2, 7, 7, 5, 3, 4, 6, 7, 2, 5, 3, 2, 2, 5, 7, 4,
    7, 7, 2, 4, 4, 4, 6, 7, 4, 2, 2, 2, 7, 5, 7, 4,
    2, 7, 7, 0, 4, 4, 6, 7, 2, 5, 4, 2, 7, 5, 7, 4,
    2, 7, 2, 7, 4, 4, 4, 7, 2, 2, 2, 2, 5, 5, 5, 4,
    2, 7, 7, 8, 4, 4, 4, 7, 2, 5, 2, 2, 5, 5, 5, 4,
    2, 7, 2, 7, 4, 4, 4, 7, 2, 2, 2, 2, 5, 5, 5, 4,
    2, 7, 7, 8, 4, 4, 4, 7, 2, 5, 2, 2, 5, 5, 5, 4,
    2, 7, 2, 0, 4, 4, 6, 7, 2, 2, 2, 2, 5, 5, 7, 4,
    2, 7, 7, 0, 3, 4, 6, 7, 2, 5, 3, 2, 2, 5, 7, 4,
    2, 7, 2, 0, 4, 4, 6, 7, 2, 2, 2, 2, 5, 5, 7, 4,
    2, 7, 7, 0, 2, 4, 6, 7, 2, 5, 4, 2, 2, 5, 7, 4,
};

static inline uint32_t hu_translated(const hu6280d *c, uint16_t a) { return ((uint32_t)c->mpr[a >> 13] << 13) | (a & 0x1fff); }
static inline uint32_t hu_zero_page(const hu6280d *c, uint16_t a) { return ((uint32_t)c->mpr[1] << 13) | (a & 0x1fff); }
static inline uint8_t hu_read(hu6280d *c, uint32_t a) { return c->read(c->user, a); }
static inline void hu_write(hu6280d *c, uint32_t a, uint8_t v) { c->write(c->user, a, v); }
static inline uint8_t hu_fetch(hu6280d *c) { return hu_read(c, hu_translated(c, c->pc++)); }

static uint8_t hu_get_flags(const hu6280d *c)
{
    const hu6280d_flags *f = &c->p;
    return (uint8_t)((f->n << 7) | (f->v << 6) | (f->t << 5) | (f->brk << 4) | (f->dec << 3) | (f->irq_disable << 2) | (f->z << 1) | f->c);
}

static void hu_set_flags(hu6280d *c, uint8_t v)
{
    hu6280d_flags *f = &c->p;
    f->n = v & 0x80, f->v = v & 0x40, f->t = v & 0x20, f->brk = v & 0x10;
    f->dec = v & 0x08, f->irq_disable = v & 0x04, f->z = v & 0x02, f->c = v & 0x01;
}

static void hu_push(hu6280d *c, uint8_t v)
{
    hu_write(c, ((uint32_t)c->mpr[1] << 13) | 0x100 | c->sp, v);
    c->sp--;
}

static uint8_t hu_pull(hu6280d *c)
{
    c->sp++;
    return hu_read(c, ((uint32_t)c->mpr[1] << 13) | 0x100 | c->sp);
}

static void hu_do_interrupt(hu6280d *c, uint16_t vector)
{
    c->extra_cycles += 7;
    hu_push(c, (uint8_t)(c->pc >> 8));
    hu_push(c, (uint8_t)(c->pc & 0xff));
    c->p.brk = false;
    hu_push(c, hu_get_flags(c));
    c->p.irq_disable = true;
    c->p.dec = false;
    c->pc = (uint16_t)(hu_read(c, hu_translated(c, vector)) | (hu_read(c, hu_translated(c, (uint16_t)(vector + 1))) << 8));
}

static void hu_check_and_take_irq_lines(hu6280d *c)
{
    if (c->nmi_state != HU_CLEAR) {
        c->nmi_state = HU_CLEAR;
        hu_do_interrupt(c, 0xfffc);
        return;
    }
    if (c->p.irq_disable) return;
    if (c->irq_state[2] != HU_CLEAR && !(c->irq_mask & 4)) {
        hu_do_interrupt(c, 0xfffa);
        if (c->irq_state[2] == HU_HOLD) c->irq_state[2] = HU_CLEAR;
    } else if (c->irq_state[0] != HU_CLEAR && !(c->irq_mask & 2)) {
        hu_do_interrupt(c, 0xfff8);
        if (c->irq_state[0] == HU_HOLD) c->irq_state[0] = HU_CLEAR;
    } else if (c->irq_state[1] != HU_CLEAR && !(c->irq_mask & 1)) {
        hu_do_interrupt(c, 0xfff6);
        if (c->irq_state[1] == HU_HOLD) c->irq_state[1] = HU_CLEAR;
    }
}

static inline void hu_set_nz(hu6280d *c, uint8_t v)
{
    c->p.z = v == 0;
    c->p.n = (v & 0x80) != 0;
}

static void hu_branch(hu6280d *c, bool cond, uint8_t off)
{
    c->p.t = false;
    if (!cond) return;
    c->extra_cycles += 2;
    c->pc = (uint16_t)(c->pc + (int8_t)off);
}

static void hu_adc(hu6280d *c, uint8_t value)
{
    if (c->p.dec) {
        const int carry = c->p.c ? 1 : 0;
        int lo = (c->a & 0x0f) + (value & 0x0f) + carry, hi = (c->a & 0xf0) + (value & 0xf0);
        c->p.c = false;
        if (lo > 0x09) {
            hi += 0x10;
            lo += 0x06;
        }
        if (hi > 0x90) hi += 0x60;
        if (hi & 0xff00) c->p.c = true;
        c->a = (uint8_t)((lo & 0x0f) + (hi & 0xf0));
        c->extra_cycles += 1;
    } else {
        const int carry = c->p.c ? 1 : 0, sum = c->a + value + carry;
        c->p.v = ((~(c->a ^ value)) & (c->a ^ sum) & 0x80) != 0;
        c->p.c = (sum & 0xff00) != 0;
        c->a = (uint8_t)sum;
    }
    hu_set_nz(c, c->a);
}

static void hu_sbc(hu6280d *c, uint8_t value)
{
    if (c->p.dec) {
        const int carry = c->p.c ? 0 : 1, sum = c->a - value - carry;
        int lo = (c->a & 0x0f) - (value & 0x0f) - carry, hi = (c->a & 0xf0) - (value & 0xf0);
        c->p.c = false;
        if (lo & 0xf0) lo -= 6;
        if (lo & 0x80) hi -= 0x10;
        if (hi & 0x0f00) hi -= 0x60;
        if ((sum & 0xff00) == 0) c->p.c = true;
        c->a = (uint8_t)((lo & 0x0f) + (hi & 0xf0));
        c->extra_cycles += 1;
    } else {
        const int carry = c->p.c ? 0 : 1, sum = c->a - value - carry;
        c->p.v = ((c->a ^ value) & (c->a ^ sum) & 0x80) != 0;
        c->p.c = (sum & 0xff00) == 0;
        c->a = (uint8_t)sum;
    }
    hu_set_nz(c, c->a);
}

static void hu_compare(hu6280d *c, uint8_t reg, uint8_t value)
{
    c->p.t = false;
    c->p.c = reg >= value;
    c->p.z = (uint8_t)(reg - value) == 0;
    c->p.n = ((reg - value) & 0x80) != 0;
}

void hu6280d_init(hu6280d *c, uint32_t clock)
{
    c->clock = clock;
    c->sp = 0xff;
    c->timer_load = 128 * 1024;
    c->clocks_per_cycle = 4;
}

void hu6280d_reset(hu6280d *c)
{
    memset(&c->p, 0, sizeof c->p);
    c->p.irq_disable = true;
    c->p.brk = true;
    c->sp = 0xff;
    c->pc = (uint16_t)(hu_read(c, hu_translated(c, 0xfffe)) | (hu_read(c, hu_translated(c, 0xffff)) << 8));
    c->clocks_per_cycle = 4;
    c->timer_status = 0;
    c->timer_load = 128 * 1024;
    c->timer_value = c->timer_load;
    c->irq_state[0] = c->irq_state[1] = c->irq_state[2] = HU_CLEAR;
    c->nmi_state = HU_CLEAR;
    c->irq_pending = 0;
}

void hu6280d_set_irq_line(hu6280d *c, int line, int state)
{
    if (line == HU_NMI_LINE) {
        if (state != HU_ASSERT) return;
        c->nmi_state = (uint8_t)state;
    } else if (line < 3) {
        if (c->irq_state[line] == state) return;
        c->irq_state[line] = (uint8_t)state;
    }
    if (c->irq_pending == 0) c->irq_pending = 2;
}

void hu6280d_irq_status_w(hu6280d *c, uint8_t offset, uint8_t value)
{
    c->io_buffer = value;
    switch (offset & 3) {
    case 2:
        c->irq_mask = value & 7;
        if (c->irq_pending == 0) c->irq_pending = 2;
        break;
    case 3: c->irq_state[2] = HU_CLEAR; break;
    default: break;
    }
}

uint8_t hu6280d_irq_status_r(const hu6280d *c, uint8_t offset)
{
    switch (offset & 3) {
    case 2: return (uint8_t)(c->irq_mask | (c->io_buffer & 0xf8));
    case 3: {
        uint8_t s = 0;
        if (c->irq_state[1] != HU_CLEAR) s |= 1;
        if (c->irq_state[0] != HU_CLEAR) s |= 2;
        if (c->irq_state[2] != HU_CLEAR) s |= 4;
        return (uint8_t)(s | (c->io_buffer & 0xf8));
    }
    default: return c->io_buffer;
    }
}

uint8_t hu6280d_timer_r(const hu6280d *c) { return (uint8_t)(((c->timer_value >> 10) & 0x7f) | (c->io_buffer & 0x80)); }

void hu6280d_timer_w(hu6280d *c, uint8_t offset, uint8_t value)
{
    c->io_buffer = value;
    if (!(offset & 1)) {
        c->timer_load = (int32_t)(((value & 127) + 1) * 1024);
        c->timer_value = c->timer_load;
    } else {
        if ((value & 1) && c->timer_status == 0) c->timer_value = c->timer_load;
        c->timer_status = value & 1;
    }
}

// the run loop as dsp-cpp's, its member names mapped onto the struct
#define a (c->a)
#define x (c->x)
#define y (c->y)
#define sp (c->sp)
#define p (c->p)
#define mpr (c->mpr)
#define pc_ (c->pc)
#define address_ (c->address)
#define operand_ (c->operand)
#define extra_cycles_ (c->extra_cycles)
#define clocks_per_cycle_ (c->clocks_per_cycle)
#define timer_status_ (c->timer_status)
#define timer_load_ (c->timer_load)
#define timer_value_ (c->timer_value)
#define irq_pending_ (c->irq_pending)
#define read(A) hu_read(c, A)
#define write(A, V) hu_write(c, A, V)
#define fetch() hu_fetch(c)
#define translated(A) hu_translated(c, A)
#define zero_page(A) hu_zero_page(c, A)
#define push(V) hu_push(c, V)
#define pull() hu_pull(c)
#define get_flags() hu_get_flags(c)
#define set_flags(V) hu_set_flags(c, V)
#define check_and_take_irq_lines() hu_check_and_take_irq_lines(c)
#define set_nz(V) hu_set_nz(c, V)
#define branch(C, O) hu_branch(c, C, O)
#define adc(V) hu_adc(c, V)
#define sbc(V) hu_sbc(c, V)
#define compare(R, V) hu_compare(c, R, V)
#define set_irq_line(L, S) hu6280d_set_irq_line(c, L, S)

int hu6280d_run(hu6280d *c, int cycles)
{
    int executed = 0;
    while (executed < cycles) {
        if (irq_pending_ == 2) irq_pending_--;
        extra_cycles_ = 0;
        const uint8_t opcode = fetch();
        switch (kAddressMode[opcode]) {
            case 0x1:  // implicit
                break;
            case 0x2:  // immediate
                operand_ = fetch();
                break;
            case 0x3:  // absolute
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                break;
            case 0x4:  // zero page, x
                address_ = (uint8_t)(fetch() + x);
                break;
            case 0x5:  // absolute with fetch
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                operand_ = read(translated(address_));
                break;
            case 0x6:  // zero page
                address_ = fetch();
                break;
            case 0x7:  // zero page with fetch
                address_ = fetch();
                operand_ = read(zero_page(address_));
                break;
            case 0x8:  // absolute, x with fetch
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                address_ = (uint16_t)(address_ + x);
                operand_ = read(translated(address_));
                break;
            case 0x9: {  // zero page indirect with fetch
                uint8_t zp = fetch();
                address_ = read(zero_page(zp));
                zp++;
                address_ |= (uint16_t)(read(zero_page(zp)) << 8);
                operand_ = read(translated(address_));
                break;
            }
            case 0xa: {  // zero page indirect, y with fetch
                uint8_t zp = fetch();
                address_ = read(zero_page(zp));
                zp++;
                address_ |= (uint16_t)(read(zero_page(zp)) << 8);
                address_ = (uint16_t)(address_ + y);
                operand_ = read(translated(address_));
                break;
            }
            case 0xb: {  // zero page indirect, y
                uint8_t zp = fetch();
                address_ = read(zero_page(zp));
                zp++;
                address_ |= (uint16_t)(read(zero_page(zp)) << 8);
                address_ = (uint16_t)(address_ + y);
                break;
            }
            case 0xc:  // absolute, x
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                address_ = (uint16_t)(address_ + x);
                break;
            case 0xd: {  // indirect
                uint16_t pointer = fetch();
                pointer |= (uint16_t)(fetch() << 8);
                address_ = read(translated(pointer));
                address_ |= (uint16_t)(read(translated((uint16_t)(pointer + 1))) << 8);
                break;
            }
            case 0xe:  // absolute, y with fetch
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                address_ = (uint16_t)(address_ + y);
                operand_ = read(translated(address_));
                break;
            case 0xf:  // zero page, x with fetch
                address_ = (uint8_t)(fetch() + x);
                operand_ = read(zero_page(address_));
                break;
            case 0x10:  // absolute, y
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                address_ = (uint16_t)(address_ + y);
                break;
            case 0x11:  // indexed indirect with fetch
            case 0x14: {  // indexed indirect
                uint8_t zp = (uint8_t)(fetch() + x);
                address_ = read(zero_page(zp));
                zp++;
                address_ |= (uint16_t)(read(zero_page(zp)) << 8);
                if (kAddressMode[opcode] == 0x11) operand_ = read(translated(address_));
                break;
            }
            case 0x12:  // zero page, y with fetch
                address_ = (uint8_t)(fetch() + y);
                operand_ = read(zero_page(address_));
                break;
            case 0x13:  // zero page, y
                address_ = (uint8_t)(fetch() + y);
                break;
            case 0x15: {  // zero page indirect
                uint8_t zp = fetch();
                address_ = read(zero_page(zp));
                zp++;
                address_ |= (uint16_t)(read(zero_page(zp)) << 8);
                break;
            }
            default:
                break;
        }
        switch (opcode) {
            case 0x00:  // brk
                p.t = false;
                pc_++;
                push((uint8_t)(pc_ >> 8));
                push((uint8_t)(pc_ & 0xff));
                push(get_flags());
                p.irq_disable = true;
                p.dec = false;
                pc_ = (uint16_t)(read(translated(0xfff6)) | (read(translated(0xfff7)) << 8));
                break;
            case 0x02: {  // sxy
                p.t = false;
                const uint8_t temp = x;
                x = y;
                y = temp;
                break;
            }
            case 0x04:  // tsb zp
                p.t = false;
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                operand_ |= a;
                p.z = operand_ == 0;
                write(zero_page(address_), operand_);
                break;
            case 0x0c:  // tsb abs
                p.t = false;
                operand_ = read(translated(address_));
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                operand_ |= a;
                p.z = operand_ == 0;
                write(translated(address_), operand_);
                break;
            case 0x14:  // trb zp
                p.t = false;
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                operand_ &= (uint8_t)(~a);
                p.z = operand_ == 0;
                write(zero_page(address_), operand_);
                break;
            case 0x01:
            case 0x05:
            case 0x09:
            case 0x0d:
            case 0x11:
            case 0x12:
            case 0x15:
            case 0x19:
            case 0x1d:  // ora
                p.t = false;
                a |= operand_;
                set_nz(a);
                break;
            case 0x06:  // asl zp
                p.t = false;
                p.c = (operand_ & 0x80) != 0;
                operand_ = (uint8_t)(operand_ << 1);
                set_nz(operand_);
                write(zero_page(address_), operand_);
                break;
            case 0x0e:
            case 0x1e:  // asl abs
                p.t = false;
                operand_ = read(translated(address_));
                p.c = (operand_ & 0x80) != 0;
                operand_ = (uint8_t)(operand_ << 1);
                set_nz(operand_);
                write(translated(address_), operand_);
                break;
            case 0x07:
            case 0x17:
            case 0x27:
            case 0x37:
            case 0x47:
            case 0x57:
            case 0x67:
            case 0x77:  // rmb0-7
                p.t = false;
                operand_ &= (uint8_t)(~(1 << ((opcode >> 4) & 0x7)));
                write(zero_page(address_), operand_);
                break;
            case 0x08:  // php
                p.t = false;
                push(get_flags());
                break;
            case 0x0a:  // asl a
                p.t = false;
                p.c = (a & 0x80) != 0;
                a = (uint8_t)(a << 1);
                set_nz(a);
                break;
            case 0x0b:
            case 0x1b:
            case 0x2b:
            case 0x3b:
            case 0x4b:
            case 0x5b:
            case 0x6b:
            case 0x7b:
            case 0x8b:
            case 0x9b:
            case 0xab:
            case 0xbb:
            case 0xcb:
            case 0xdb:
            case 0xeb:
            case 0xfb:  // nop
                p.t = false;
                break;
            case 0x0f:
            case 0x1f:
            case 0x2f:
            case 0x3f:
            case 0x4f:
            case 0x5f:
            case 0x6f:
            case 0x7f: {  // bbr0-7
                p.t = false;
                const uint8_t offset = fetch();
                if ((operand_ & (1 << ((opcode >> 4) & 0x7))) == 0) {
                    extra_cycles_ += 2;
                    pc_ = (uint16_t)(pc_ + (int8_t)(offset));
                }
                break;
            }
            case 0x10:  // bpl
                branch(!p.n, operand_);
                break;
            case 0x18:  // clc
                p.t = false;
                p.c = false;
                break;
            case 0x1a:  // ina
                p.t = false;
                a++;
                set_nz(a);
                break;
            case 0x20:  // jsr
                p.t = false;
                pc_--;
                push((uint8_t)(pc_ >> 8));
                push((uint8_t)(pc_ & 0xff));
                pc_ = address_;
                break;
            case 0x22: {  // sax
                p.t = false;
                const uint8_t temp = x;
                x = a;
                a = temp;
                break;
            }
            case 0x24:
            case 0x34:  // bit zp
                p.t = false;
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                p.z = (operand_ & a) == 0;
                break;
            case 0x2c:
            case 0x3c:  // bit abs
                p.t = false;
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                p.z = (operand_ & a) == 0;
                break;
            case 0x89:  // bit imm
                p.t = false;
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                p.z = (operand_ & a) == 0;
                break;
            case 0x1c:  // trb abs
                p.t = false;
                operand_ = read(translated(address_));
                p.n = (operand_ & 0x80) != 0;
                p.v = (operand_ & 0x40) != 0;
                operand_ &= (uint8_t)(~a);
                p.z = operand_ == 0;
                write(translated(address_), operand_);
                break;
            case 0x21:
            case 0x25:
            case 0x29:
            case 0x2d:
            case 0x31:
            case 0x32:
            case 0x35:
            case 0x39:
            case 0x3d:  // and
                p.t = false;
                a &= operand_;
                set_nz(a);
                break;
            case 0x26: {  // rol zp
                p.t = false;
                const uint16_t value = (uint16_t)((operand_ << 1) | (p.c ? 1 : 0));
                p.c = (value & 0x100) != 0;
                operand_ = (uint8_t)(value);
                set_nz(operand_);
                write(zero_page(address_), operand_);
                break;
            }
            case 0x2e:
            case 0x3e: {  // rol abs
                p.t = false;
                operand_ = read(translated(address_));
                const uint16_t value = (uint16_t)((operand_ << 1) | (p.c ? 1 : 0));
                p.c = (value & 0x100) != 0;
                operand_ = (uint8_t)(value);
                set_nz(operand_);
                write(translated(address_), operand_);
                break;
            }
            case 0x28:  // plp
                set_flags(pull());
                p.brk = true;
                if (irq_pending_ == 0) irq_pending_ = 2;
                break;
            case 0x2a: {  // rol a
                p.t = false;
                const uint16_t value = (uint16_t)((a << 1) | (p.c ? 1 : 0));
                p.c = (value & 0x100) != 0;
                a = (uint8_t)(value);
                set_nz(a);
                break;
            }
            case 0x30:  // bmi
                branch(p.n, operand_);
                break;
            case 0x38:  // sec
                p.t = false;
                p.c = true;
                break;
            case 0x3a:  // dea
                p.t = false;
                a--;
                set_nz(a);
                break;
            case 0x40:  // rti
                set_flags(pull());
                p.brk = true;
                pc_ = pull();
                pc_ |= (uint16_t)(pull() << 8);
                if (irq_pending_ == 0) irq_pending_ = 2;
                break;
            case 0x03:  // st0, VDC register select
                p.t = false;
                write(0x1fe000, operand_);
                break;
            case 0x13:  // st1, VDC data low
                p.t = false;
                write(0x1fe002, operand_);
                break;
            case 0x23:  // st2, VDC data high
                p.t = false;
                write(0x1fe003, operand_);
                break;
            case 0x42: {  // say
                p.t = false;
                const uint8_t temp = y;
                y = a;
                a = temp;
                break;
            }
            case 0x70:  // bvs
                branch(p.v, operand_);
                break;
            case 0xb8:  // clv
                p.t = false;
                p.v = false;
                break;
            case 0x41:
            case 0x45:
            case 0x49:
            case 0x4d:
            case 0x51:
            case 0x52:
            case 0x55:
            case 0x59:
            case 0x5d:  // eor
                p.t = false;
                a ^= operand_;
                set_nz(a);
                break;
            case 0x43:  // tma
                p.t = false;
                for (int bit = 0; bit < 8; bit++) {
                    if ((operand_ & (1 << bit)) != 0) a = mpr[(size_t)(bit)];
                }
                break;
            case 0x44:  // bsr
                p.t = false;
                push((uint8_t)((pc_ - 1) >> 8));
                push((uint8_t)((pc_ - 1) & 0xff));
                pc_ = (uint16_t)(pc_ + (int8_t)(operand_));
                break;
            case 0x46:  // lsr zp
                p.t = false;
                p.c = (operand_ & 1) != 0;
                operand_ = (uint8_t)(operand_ >> 1);
                set_nz(operand_);
                write(zero_page(address_), operand_);
                break;
            case 0x4e:
            case 0x5e:  // lsr abs
                p.t = false;
                operand_ = read(translated(address_));
                p.c = (operand_ & 1) != 0;
                operand_ = (uint8_t)(operand_ >> 1);
                set_nz(operand_);
                write(translated(address_), operand_);
                break;
            case 0x48:  // pha
                p.t = false;
                push(a);
                break;
            case 0x4a:  // lsr a
                p.t = false;
                p.c = (a & 1) != 0;
                a = (uint8_t)(a >> 1);
                set_nz(a);
                break;
            case 0x4c:
            case 0x6c:  // jmp
                p.t = false;
                pc_ = address_;
                break;
            case 0x50:  // bvc
                branch(!p.v, operand_);
                break;
            case 0x53:  // tam
                p.t = false;
                for (int bit = 0; bit < 8; bit++) {
                    if ((operand_ & (1 << bit)) != 0) mpr[(size_t)(bit)] = a;
                }
                break;
            case 0x54:  // csl
                p.t = false;
                clocks_per_cycle_ = 4;
                break;
            case 0x58:  // cli
                p.t = false;
                if (p.irq_disable) {
                    p.irq_disable = false;
                    if (irq_pending_ == 0) irq_pending_ = 2;
                }
                break;
            case 0x5a:  // phy
                p.t = false;
                push(y);
                break;
            case 0x60:  // rts
                p.t = false;
                pc_ = pull();
                pc_ |= (uint16_t)(pull() << 8);
                pc_++;
                break;
            case 0x62:  // cla
                p.t = false;
                a = 0;
                break;
            case 0x64:
            case 0x74:  // stz zp
                p.t = false;
                write(zero_page(address_), 0);
                break;
            case 0x61:
            case 0x65:
            case 0x69:
            case 0x6d:
            case 0x71:
            case 0x72:
            case 0x75:
            case 0x79:
            case 0x7d:  // adc
                p.t = false;
                adc(operand_);
                break;
            case 0x66:  // ror zp
                p.t = false;
                {
                    const uint16_t value = (uint16_t)(operand_ | (p.c ? 0x100 : 0));
                    p.c = (operand_ & 1) != 0;
                    operand_ = (uint8_t)(value >> 1);
                }
                set_nz(operand_);
                write(zero_page(address_), operand_);
                break;
            case 0x6e:
            case 0x7e:  // ror abs
                p.t = false;
                operand_ = read(translated(address_));
                {
                    const uint16_t value = (uint16_t)(operand_ | (p.c ? 0x100 : 0));
                    p.c = (operand_ & 1) != 0;
                    operand_ = (uint8_t)(value >> 1);
                }
                set_nz(operand_);
                write(translated(address_), operand_);
                break;
            case 0x68:  // pla
                p.t = false;
                a = pull();
                set_nz(a);
                break;
            case 0x6a: {  // ror a
                p.t = false;
                const uint16_t value = (uint16_t)(a | (p.c ? 0x100 : 0));
                p.c = (a & 1) != 0;
                a = (uint8_t)(value >> 1);
                set_nz(a);
                break;
            }
            case 0x73: {  // tii
                p.t = false;
                uint16_t from = fetch();
                from |= (uint16_t)(fetch() << 8);
                uint16_t to = fetch();
                to |= (uint16_t)(fetch() << 8);
                uint32_t length = fetch();
                length |= (uint32_t)(fetch() << 8);
                if (length == 0) length = 0x10000;
                extra_cycles_ += (int)(6 * length) + 17 - kCycles[opcode];
                while (length-- != 0) {
                    write(translated(to), read(translated(from)));
                    to++;
                    from++;
                }
                break;
            }
            case 0x78:  // sei
                p.t = false;
                p.irq_disable = true;
                break;
            case 0x7a:  // ply
                p.t = false;
                y = pull();
                set_nz(y);
                break;
            case 0x7c:  // jmp (abs,x)
                p.t = false;
                address_ = (uint16_t)(address_ + x);
                pc_ = read(translated(address_));
                pc_ |= (uint16_t)(read(translated((uint16_t)(address_ + 1))) << 8);
                break;
            case 0x80:  // bra
                p.t = false;
                extra_cycles_ += 2;
                pc_ = (uint16_t)(pc_ + (int8_t)(operand_));
                break;
            case 0x82:  // clx
                p.t = false;
                x = 0;
                break;
            case 0x84:
            case 0x94:  // sty zp
                p.t = false;
                write(zero_page(address_), y);
                break;
            case 0x85:
            case 0x95:  // sta zp
                p.t = false;
                write(zero_page(address_), a);
                break;
            case 0x86:
            case 0x96:  // stx zp
                p.t = false;
                write(zero_page(address_), x);
                break;
            case 0x87:
            case 0x97:
            case 0xa7:
            case 0xb7:
            case 0xc7:
            case 0xd7:
            case 0xe7:
            case 0xf7:  // smb0-7
                p.t = false;
                operand_ |= (uint8_t)(1 << ((opcode >> 4) & 0x7));
                write(zero_page(address_), operand_);
                break;
            case 0x88:  // dey
                p.t = false;
                y--;
                set_nz(y);
                break;
            case 0x8a:  // txa
                p.t = false;
                a = x;
                set_nz(a);
                break;
            case 0x8c:  // sty abs
                p.t = false;
                write(translated(address_), y);
                break;
            case 0x81:
            case 0x8d:
            case 0x91:
            case 0x92:
            case 0x99:
            case 0x9d:  // sta
                p.t = false;
                write(translated(address_), a);
                break;
            case 0x8e:  // stx abs
                p.t = false;
                write(translated(address_), x);
                break;
            case 0x8f:
            case 0x9f:
            case 0xaf:
            case 0xbf:
            case 0xcf:
            case 0xdf:
            case 0xef:
            case 0xff: {  // bbs0-7
                p.t = false;
                const uint8_t offset = fetch();
                if ((operand_ & (1 << ((opcode >> 4) & 0x7))) != 0) {
                    extra_cycles_ += 2;
                    pc_ = (uint16_t)(pc_ + (int8_t)(offset));
                }
                break;
            }
            case 0x90:  // bcc
                branch(!p.c, operand_);
                break;
            case 0x93: {  // tst abs
                p.t = false;
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                const uint8_t value = read(translated(address_));
                p.n = (value & 0x80) != 0;
                p.v = (value & 0x40) != 0;
                p.z = (value & operand_) == 0;
                break;
            }
            case 0x83: {  // tst zp
                p.t = false;
                const uint8_t zp = fetch();
                const uint8_t value = read(zero_page(zp));
                p.n = (value & 0x80) != 0;
                p.v = (value & 0x40) != 0;
                p.z = (value & operand_) == 0;
                break;
            }
            case 0xa3: {  // tst zp,x
                p.t = false;
                const uint8_t zp = (uint8_t)(fetch() + x);
                const uint8_t value = read(zero_page(zp));
                p.n = (value & 0x80) != 0;
                p.v = (value & 0x40) != 0;
                p.z = (value & operand_) == 0;
                break;
            }
            case 0x98:  // tya
                p.t = false;
                a = y;
                set_nz(a);
                break;
            case 0x9a:  // txs
                p.t = false;
                sp = x;
                break;
            case 0x9c:
            case 0x9e:  // stz abs
                p.t = false;
                write(translated(address_), 0);
                break;
            case 0xa0:
            case 0xa4:
            case 0xac:
            case 0xb4:
            case 0xbc:  // ldy
                p.t = false;
                y = operand_;
                set_nz(y);
                break;
            case 0xa2:
            case 0xa6:
            case 0xae:
            case 0xb6:
            case 0xbe:  // ldx
                p.t = false;
                x = operand_;
                set_nz(x);
                break;
            case 0xa1:
            case 0xa5:
            case 0xa9:
            case 0xad:
            case 0xb1:
            case 0xb2:
            case 0xb5:
            case 0xb9:
            case 0xbd:  // lda
                p.t = false;
                a = operand_;
                set_nz(a);
                break;
            case 0xa8:  // tay
                p.t = false;
                y = a;
                set_nz(y);
                break;
            case 0xaa:  // tax
                p.t = false;
                x = a;
                set_nz(x);
                break;
            case 0xb0:  // bcs
                branch(p.c, operand_);
                break;
            case 0xb3: {  // tst abs,x
                p.t = false;
                address_ = fetch();
                address_ |= (uint16_t)(fetch() << 8);
                const uint8_t value = read(translated((uint16_t)(address_ + x)));
                p.n = (value & 0x80) != 0;
                p.v = (value & 0x40) != 0;
                p.z = (value & operand_) == 0;
                break;
            }
            case 0xba:  // tsx
                p.t = false;
                x = sp;
                set_nz(x);
                break;
            case 0xc0:
            case 0xc4:
            case 0xcc:  // cpy
                compare(y, operand_);
                break;
            case 0xc2:  // cly
                p.t = false;
                y = 0;
                break;
            case 0xc1:
            case 0xc5:
            case 0xc9:
            case 0xcd:
            case 0xd1:
            case 0xd2:
            case 0xd5:
            case 0xd9:
            case 0xdd:  // cmp
                compare(a, operand_);
                break;
            case 0xc6:
            case 0xd6:  // dec zp
                p.t = false;
                operand_--;
                set_nz(operand_);
                write(zero_page(address_), operand_);
                break;
            case 0xc8:  // iny
                p.t = false;
                y++;
                set_nz(y);
                break;
            case 0xca:  // dex
                p.t = false;
                x--;
                set_nz(x);
                break;
            case 0xce:
            case 0xde:  // dec abs
                p.t = false;
                operand_ = (uint8_t)(read(translated(address_)) - 1);
                set_nz(operand_);
                write(translated(address_), operand_);
                break;
            case 0xd0:  // bne
                branch(!p.z, operand_);
                break;
            case 0xc3: {  // tdd
                p.t = false;
                uint16_t from = fetch();
                from |= (uint16_t)(fetch() << 8);
                uint16_t to = fetch();
                to |= (uint16_t)(fetch() << 8);
                uint32_t length = fetch();
                length |= (uint32_t)(fetch() << 8);
                if (length == 0) length = 0x10000;
                extra_cycles_ += (int)(6 * length) + 17 - kCycles[opcode];
                while (length-- != 0) {
                    write(translated(to), read(translated(from)));
                    to--;
                    from--;
                }
                break;
            }
            case 0xe3: {  // tia
                p.t = false;
                uint16_t from = fetch();
                from |= (uint16_t)(fetch() << 8);
                uint16_t to = fetch();
                to |= (uint16_t)(fetch() << 8);
                uint32_t length = fetch();
                length |= (uint32_t)(fetch() << 8);
                if (length == 0) length = 0x10000;
                extra_cycles_ += (int)(6 * length) + 17 - kCycles[opcode];
                uint16_t alternate = 0;
                while (length-- != 0) {
                    write(translated((uint16_t)(to + alternate)), read(translated(from)));
                    from++;
                    alternate ^= 1;
                }
                break;
            }
            case 0xd3: {  // tin
                p.t = false;
                uint16_t from = fetch();
                from |= (uint16_t)(fetch() << 8);
                uint16_t to = fetch();
                to |= (uint16_t)(fetch() << 8);
                uint32_t length = fetch();
                length |= (uint32_t)(fetch() << 8);
                if (length == 0) length = 0x10000;
                extra_cycles_ += (int)(6 * length) + 17 - kCycles[opcode];
                while (length-- != 0) {
                    write(translated(to), read(translated(from)));
                    from++;
                }
                break;
            }
            case 0xd4:  // csh
                clocks_per_cycle_ = 1;
                break;
            case 0xd8:  // cld
                p.t = false;
                p.dec = false;
                break;
            case 0xda:  // phx
                p.t = false;
                push(x);
                break;
            case 0xe0:
            case 0xe4:
            case 0xec:  // cpx
                compare(x, operand_);
                break;
            case 0xe1:
            case 0xe5:
            case 0xe9:
            case 0xed:
            case 0xf1:
            case 0xf2:
            case 0xf5:
            case 0xf9:
            case 0xfd:  // sbc
                p.t = false;
                sbc(operand_);
                break;
            case 0xe6:
            case 0xf6:  // inc zp
                p.t = false;
                operand_++;
                set_nz(operand_);
                write(zero_page(address_), operand_);
                break;
            case 0xe8:  // inx
                p.t = false;
                x++;
                set_nz(x);
                break;
            case 0xea:  // nop
                p.t = false;
                break;
            case 0xee:
            case 0xfe:  // inc abs
                p.t = false;
                operand_ = (uint8_t)(read(translated(address_)) + 1);
                set_nz(operand_);
                write(translated(address_), operand_);
                break;
            case 0xf0:  // beq
                branch(p.z, operand_);
                break;
            case 0xf3: {  // tai
                p.t = false;
                uint16_t from = fetch();
                from |= (uint16_t)(fetch() << 8);
                uint16_t to = fetch();
                to |= (uint16_t)(fetch() << 8);
                uint32_t length = fetch();
                length |= (uint32_t)(fetch() << 8);
                if (length == 0) length = 0x10000;
                extra_cycles_ += (int)(6 * length) + 17 - kCycles[opcode];
                uint16_t alternate = 0;
                while (length-- != 0) {
                    write(translated(to), read(translated((uint16_t)(from + alternate))));
                    to++;
                    alternate ^= 1;
                }
                break;
            }
            case 0xf4:  // set
                p.t = true;
                break;
            case 0xf8:  // sed
                p.t = false;
                p.dec = true;
                break;
            case 0xfa:  // plx
                p.t = false;
                x = pull();
                set_nz(x);
                break;
            default:
                break;
        }
        const int spent = (kCycles[opcode] + extra_cycles_) * clocks_per_cycle_;
        executed += spent;
        timer_value_ -= spent;
        if (irq_pending_ != 0) {
            if (irq_pending_ == 1) {
                if (!p.irq_disable) {
                    irq_pending_--;
                    check_and_take_irq_lines();
                }
            } else {
                irq_pending_--;
            }
        }
        if (timer_status_ != 0 && timer_value_ <= 0) {
            if (irq_pending_ != 0) irq_pending_ = 1;
            while (timer_value_ <= 0) timer_value_ += timer_load_;
            set_irq_line(2, HU_ASSERT);
        }
        if (c->cycles) c->cycles(c->user, spent);
    }
    return executed;
}
