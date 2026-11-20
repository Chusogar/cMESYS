// tms7000.c - TMS7000 family (see tms7000.h).
#include "tms7000.h"
#include <string.h>

#define WB_NO (-1)
static const uint8_t kBcdOut[6] = { 0x00, 0x06, 0x00, 0x66, 0x60, 0x66 };

static uint8_t pf_read(tms7000 *t, uint8_t offset);
static void pf_write(tms7000 *t, uint8_t offset, uint8_t data);
static void execute_one(tms7000 *t, uint8_t op);
static void check_interrupts(tms7000 *t);

void tms7000_init(tms7000 *t, uint32_t clock, int chip, unsigned divider)
{
    t->clock = clock;
    t->divider = divider ? divider : 1;
    t->family_70x2 = chip == TMS7041 || chip == TMS7042;
    t->ram_size = chip == TMS7042 ? 0x100 : 0x80;
    t->rom_base = chip == TMS7020 ? 0xf800 : chip == TMS7000_CHIP ? 0 : 0xf000;
    memset(t->ram, 0, sizeof t->ram);
    t->port_ddr[1] = 0xff;
}

// ---- memory -------------------------------------------------------------------

static uint8_t read_mem8(tms7000 *t, uint16_t a)
{
    if (a < t->ram_size) return t->ram[a];
    if (a < 0x0100) return 0;
    if (a < 0x010c) return pf_read(t, (uint8_t)(a - 0x0100));
    if (t->family_70x2 && a >= 0x0110 && a < 0x0118) return pf_read(t, (uint8_t)(a - 0x0100));
    if (t->rom_size && a >= t->rom_base) {
        const size_t off = (size_t)(a - t->rom_base);
        if (off < t->rom_size) return t->rom[off];
    }
    return t->read ? t->read(t->user, a) : 0xff;
}

static void write_mem8(tms7000 *t, uint16_t a, uint8_t d)
{
    if (a < t->ram_size) { t->ram[a] = d; return; }
    if (a < 0x0100) return;
    if (a < 0x010c) { pf_write(t, (uint8_t)(a - 0x0100), d); return; }
    if (t->family_70x2 && a >= 0x0110 && a < 0x0118) { pf_write(t, (uint8_t)(a - 0x0100), d); return; }
    if (t->rom_size && a >= t->rom_base && (size_t)(a - t->rom_base) < t->rom_size) return;
    if (t->write) t->write(t->user, a, d);
}

static uint16_t read_mem16(tms7000 *t, uint16_t a)
{
    const uint8_t hi = read_mem8(t, a);
    const uint8_t lo = read_mem8(t, (uint16_t)(a + 1));
    return (uint16_t)((hi << 8) | lo);
}

static void write_mem16(tms7000 *t, uint16_t a, uint16_t d)
{
    write_mem8(t, a, (uint8_t)(d >> 8));
    write_mem8(t, (uint16_t)(a + 1), (uint8_t)d);
}

#define read_r8(t, a) read_mem8(t, (uint8_t)(a))
#define write_r8(t, a, d) write_mem8(t, (uint8_t)(a), d)
#define read_p(t, a) read_mem8(t, (uint16_t)(0x100 + (uint8_t)(a)))
#define write_p(t, a, d) write_mem8(t, (uint16_t)(0x100 + (uint8_t)(a)), d)

static uint16_t read_r16(tms7000 *t, uint8_t a)
{
    const uint8_t hi = read_r8(t, (uint8_t)(a - 1));
    return (uint16_t)((hi << 8) | read_r8(t, a));
}

static void write_r16(tms7000 *t, uint8_t a, uint16_t d)
{
    write_r8(t, (uint8_t)(a - 1), (uint8_t)(d >> 8));
    write_r8(t, a, (uint8_t)d);
}

static uint8_t imm8(tms7000 *t) { return read_mem8(t, t->pc++); }
static uint16_t imm16(tms7000 *t)
{
    const uint8_t hi = imm8(t);
    return (uint16_t)((hi << 8) | imm8(t));
}
static uint8_t pull8(tms7000 *t) { return read_mem8(t, t->sp--); }
static void push8(tms7000 *t, uint8_t d) { write_mem8(t, ++t->sp, d); }
static uint16_t pull16(tms7000 *t)
{
    const uint8_t lo = pull8(t);
    return (uint16_t)(lo | (pull8(t) << 8));
}
static void push16(tms7000 *t, uint16_t d)
{
    push8(t, (uint8_t)(d >> 8));
    push8(t, (uint8_t)d);
}

static void set_nz(tms7000 *t, uint16_t v) { t->sr = (uint8_t)((t->sr & 0x9f) | ((v >> 1) & 0x40) | ((v & 0xff) ? 0 : 0x20)); }
static void set_c(tms7000 *t, uint16_t v) { t->sr = (uint8_t)((t->sr & 0x7f) | ((v >> 1) & 0x80)); }
static void set_cnz(tms7000 *t, uint16_t v) { t->sr = (uint8_t)((t->sr & 0x1f) | ((v >> 1) & 0xc0) | ((v & 0xff) ? 0 : 0x20)); }
static inline int get_c(const tms7000 *t) { return (t->sr >> 7) & 1; }
#define consume(t, n) ((t)->icount -= (n))

// ---- ports, interrupts, timers --------------------------------------------------

static uint8_t port_in(tms7000 *t, int port) { return t->port_in ? t->port_in(t->user, port) : 0xff; }
static void port_out(tms7000 *t, int port, uint8_t d) { if (t->port_out) t->port_out(t->user, port, d); }

static void flag_ext_interrupt(tms7000 *t, int ext)
{
    if (t->irq_state[ext]) t->io_control[0] |= (uint8_t)(0x02 << (4 * ext));
    else t->io_control[0] &= (uint8_t)~(0x02 << (4 * ext));
}

static void do_interrupt(tms7000 *t, int irqline)
{
    if (t->idle_state) {
        t->icount -= 17;
        t->pc++;
        t->idle_state = false;
    } else {
        t->icount -= 19;
    }
    push8(t, t->sr);
    push16(t, t->pc);
    t->sr = 0;
    t->pc = read_mem16(t, (uint16_t)(0xfffc - irqline * 2));
}

static void check_interrupts(tms7000 *t)
{
    if (!(t->sr & TMS7000_SR_I)) return;
    for (int irqline = 0; irqline < 5; irqline++) {
        const int shift = irqline > 2 ? irqline * 2 - 6 : irqline * 2;
        const int bank = irqline > 2 ? 1 : 0;
        if (((t->io_control[bank] >> shift) & 3) == 3) {
            t->io_control[bank] &= (uint8_t)~(0x02 << shift);
            if (irqline == 0 || irqline == 2) {
                const int ext = irqline / 2;
                if (t->irq_hold[ext]) {
                    t->irq_hold[ext] = false;
                    t->irq_state[ext] = false;
                }
                flag_ext_interrupt(t, ext);
            }
            do_interrupt(t, irqline);
            return;
        }
    }
}

void tms7000_set_input_line(tms7000 *t, int ext, int state)
{
    if (ext != TMS7000_INT1 && ext != TMS7000_INT3) return;
    const bool hold = state == TMS7000_HOLD || state == TMS7000_PULSE;
    const bool on = state != TMS7000_CLEAR;
    t->irq_hold[ext] = hold;
    if (t->irq_state[ext] == on) return;
    t->irq_state[ext] = on;
    flag_ext_interrupt(t, ext);
    if (on) {
        if (ext == TMS7000_INT3) t->timer_capture_latch[0] = (uint16_t)t->timer_decrementer[0];
        check_interrupts(t);
    }
}

static void timer_run(tms7000 *t, int tmr)
{
    t->timer_prescaler[tmr] = t->timer_control[tmr] & 0x1f;
    if ((t->timer_control[tmr] & 0xe0) == 0x80) t->timer_crystal_acc[tmr] = 0;
}

static void timer_reload(tms7000 *t, int tmr)
{
    t->timer_crystal_acc[tmr] = 0;
    if (t->timer_control[tmr] & 0x80) {
        t->timer_decrementer[tmr] = t->timer_data[tmr];
        timer_run(t, tmr);
    }
}

static void timer_tick_low(tms7000 *t, int tmr)
{
    if (--t->timer_decrementer[tmr] < 0) {
        timer_reload(t, tmr);
        t->io_control[tmr] |= 0x08;
        if (tmr == 0 && (t->timer_control[1] & 0xa0) == 0xa0) {
            if (--t->timer_prescaler[1] < 0) {
                t->timer_prescaler[1] = t->timer_control[1] & 0x1f;
                timer_tick_low(t, 1);
            }
        }
        check_interrupts(t);
    }
}

static void tick_timers(tms7000 *t, int cpu_cycles)
{
    // the internal source: the machine clock / 8
    for (int tmr = 0; tmr < 2; tmr++) {
        if ((t->timer_control[tmr] & 0xe0) != 0x80) continue;
        const int period = 8 * ((t->timer_control[tmr] & 0x1f) + 1);
        t->timer_crystal_acc[tmr] += cpu_cycles;
        while (t->timer_crystal_acc[tmr] >= period) {
            t->timer_crystal_acc[tmr] -= period;
            timer_tick_low(t, tmr);
        }
    }
}

static uint8_t pf_read(tms7000 *t, uint8_t offset)
{
    switch (offset) {
    case 0x00: case 0x10: return t->io_control[offset >> 4];
    case 0x02: case 0x12: return (uint8_t)t->timer_decrementer[offset >> 4];
    case 0x03: return (uint8_t)t->timer_capture_latch[0];
    case 0x04: case 0x06: case 0x08: case 0x0a: {
        const int port = offset / 2 - 2;
        return (uint8_t)((port_in(t, port) & ~t->port_ddr[port]) | (t->port_latch[port] & t->port_ddr[port]));
    }
    case 0x05: case 0x09: case 0x0b: return t->port_ddr[offset / 2 - 2];
    default: return 0;
    }
}

static void pf_write(tms7000 *t, uint8_t offset, uint8_t data)
{
    switch (offset) {
    case 0x00:
        t->io_control[0] = (uint8_t)((t->io_control[0] & (~data & 0x2a)) | (data & 0xd5));
        if (data & 0x02) flag_ext_interrupt(t, TMS7000_INT1);
        if (data & 0x20) flag_ext_interrupt(t, TMS7000_INT3);
        check_interrupts(t);
        break;
    case 0x10:
        t->io_control[1] = (uint8_t)((t->io_control[1] & (~data & 0x0a)) | (data & 0x05));
        check_interrupts(t);
        break;
    case 0x02: case 0x12:
        t->timer_data[offset >> 4] = data;
        break;
    case 0x03:
        data &= (uint8_t)~0x20;
        /* fall through */
    case 0x13:
        t->timer_control[offset >> 4] = data;
        timer_reload(t, offset >> 4);
        break;
    case 0x04: case 0x06: case 0x08: case 0x0a: {
        if (!t->family_70x2 && offset == 0x04) return;
        const int port = offset / 2 - 2;
        port_out(t, port, (uint8_t)(data & t->port_ddr[port]));
        t->port_latch[port] = data;
        break;
    }
    case 0x05: case 0x09: case 0x0b:
        if (!t->family_70x2 && offset == 0x05) return;
        t->port_ddr[offset / 2 - 2] = data;
        break;
    default:
        break;
    }
}

void tms7000_reset(tms7000 *t)
{
    if (t->idle_state) {
        t->pc++;
        t->idle_state = false;
    }
    t->irq_state[0] = t->irq_state[1] = false;
    t->irq_hold[0] = t->irq_hold[1] = false;
    memset(t->io_control, 0, sizeof t->io_control);
    memset(t->port_latch, 0, sizeof t->port_latch);
    memset(t->port_ddr, 0, sizeof t->port_ddr);
    t->port_ddr[1] = 0xff;
    memset(t->timer_data, 0, sizeof t->timer_data);
    memset(t->timer_control, 0, sizeof t->timer_control);
    memset(t->timer_decrementer, 0, sizeof t->timer_decrementer);
    memset(t->timer_prescaler, 0, sizeof t->timer_prescaler);
    memset(t->timer_capture_latch, 0, sizeof t->timer_capture_latch);
    memset(t->timer_crystal_acc, 0, sizeof t->timer_crystal_acc);
    memset(t->ram, 0, sizeof t->ram);
    write_p(t, 0x04, 0xff);
    write_p(t, 0x06, 0xff);
    write_p(t, 0x05, 0x00);
    write_p(t, 0x09, 0x00);
    write_p(t, 0x0b, 0x00);
    write_p(t, 0x08, 0xff);
    write_p(t, 0x0a, 0xff);
    t->sr = 0;
    write_p(t, 0x00, 0x00);
    if (t->family_70x2) write_p(t, 0x10, 0x00);
    t->sp = 0xff;
    t->idle_state = false;
    execute_one(t, 0xff);
    t->icount -= 3;
}

int tms7000_run(tms7000 *t, int cycles)
{
    if (cycles <= 0) return 0;
    t->icount = cycles;
    while (t->icount > 0) {
        check_interrupts(t);
        const int before = t->icount;
        const uint8_t op = read_mem8(t, t->pc++);
        execute_one(t, op);
        int used = before - t->icount;
        if (used < 1) {
            consume(t, 1);
            used = 1;
        }
        tick_timers(t, used);
        if (t->cycles) t->cycles(t->user, used);
    }
    return cycles - t->icount;
}

// ---- the ALU operations ---------------------------------------------------------

typedef int (*op_fn)(tms7000 *t, uint8_t p1, uint8_t p2);

static void shortbranch(tms7000 *t, bool check)
{
    consume(t, 2);
    const int8_t d = (int8_t)imm8(t);
    if (check) {
        t->pc = (uint16_t)(t->pc + d);
        consume(t, 2);
    }
}

static int op_clr(tms7000 *t, uint8_t p1, uint8_t p2) { (void)p1, (void)p2; set_cnz(t, 0); return 0; }
static int op_dec(tms7000 *t, uint8_t p1, uint8_t p2)
{
    (void)p2;
    const uint16_t v = (uint16_t)(p1 - 1);
    set_nz(t, v);
    set_c(t, (uint16_t)~v);
    return v;
}
static int op_inc(tms7000 *t, uint8_t p1, uint8_t p2) { (void)p2; const uint16_t v = (uint16_t)(p1 + 1); set_cnz(t, v); return v; }
static int op_inv(tms7000 *t, uint8_t p1, uint8_t p2) { (void)p2; const uint8_t v = (uint8_t)~p1; set_cnz(t, v); return v; }
static int op_rl(tms7000 *t, uint8_t p1, uint8_t p2) { (void)p2; const uint16_t v = (uint16_t)((p1 << 1) | (p1 >> 7)); set_cnz(t, v); return v; }
static int op_rlc(tms7000 *t, uint8_t p1, uint8_t p2) { (void)p2; const uint16_t v = (uint16_t)((p1 << 1) | get_c(t)); set_cnz(t, v); return v; }
static int op_rr(tms7000 *t, uint8_t p1, uint8_t p2)
{
    (void)p2;
    const uint16_t v = (uint16_t)((p1 >> 1) | (p1 << 8) | ((p1 << 7) & 0x80));
    set_cnz(t, v);
    return v;
}
static int op_rrc(tms7000 *t, uint8_t p1, uint8_t p2)
{
    (void)p2;
    const uint16_t v = (uint16_t)((p1 >> 1) | (p1 << 8) | (get_c(t) << 7));
    set_cnz(t, v);
    return v;
}
static int op_swap(tms7000 *t, uint8_t p1, uint8_t p2)
{
    (void)p2;
    consume(t, 3);
    const uint16_t v = (uint16_t)((p1 >> 4) | (p1 << 4));
    set_cnz(t, v);
    return v;
}
static int op_xchb(tms7000 *t, uint8_t p1, uint8_t p2)
{
    (void)p2;
    consume(t, 1);
    const uint8_t v = read_r8(t, 1);
    set_cnz(t, v);
    write_r8(t, 1, p1);
    return v;
}
static int op_adc(tms7000 *t, uint8_t p1, uint8_t p2) { const uint16_t v = (uint16_t)(p1 + p2 + get_c(t)); set_cnz(t, v); return v; }
static int op_add(tms7000 *t, uint8_t p1, uint8_t p2) { const uint16_t v = (uint16_t)(p1 + p2); set_cnz(t, v); return v; }
static int op_and(tms7000 *t, uint8_t p1, uint8_t p2) { const uint8_t v = (uint8_t)(p1 & p2); set_cnz(t, v); return v; }
static int op_cmp(tms7000 *t, uint8_t p1, uint8_t p2)
{
    const uint16_t v = (uint16_t)(p1 - p2);
    set_nz(t, v);
    set_c(t, (uint16_t)~v);
    return WB_NO;
}
static int op_mpy(tms7000 *t, uint8_t p1, uint8_t p2)
{
    consume(t, 39);
    const uint16_t v = (uint16_t)(p1 * p2);
    set_cnz(t, (uint16_t)(v >> 8));
    write_mem16(t, 0, v);
    return WB_NO;
}
static int op_mov(tms7000 *t, uint8_t p1, uint8_t p2) { (void)p1; set_cnz(t, p2); return p2; }
static int op_or(tms7000 *t, uint8_t p1, uint8_t p2) { const uint8_t v = (uint8_t)(p1 | p2); set_cnz(t, v); return v; }
static int op_sbb(tms7000 *t, uint8_t p1, uint8_t p2)
{
    const uint16_t v = (uint16_t)(p1 - p2 - (get_c(t) ? 0 : 1));
    set_nz(t, v);
    set_c(t, (uint16_t)~v);
    return v;
}
static int op_sub(tms7000 *t, uint8_t p1, uint8_t p2)
{
    const uint16_t v = (uint16_t)(p1 - p2);
    set_nz(t, v);
    set_c(t, (uint16_t)~v);
    return v;
}
static int op_xor(tms7000 *t, uint8_t p1, uint8_t p2) { const uint8_t v = (uint8_t)(p1 ^ p2); set_cnz(t, v); return v; }
static int op_dac(tms7000 *t, uint8_t p1, uint8_t p2)
{
    consume(t, 2);
    const int c = get_c(t);
    const uint8_t h1 = (p1 >> 4) & 0xf, l1 = p1 & 0xf, h2 = (p2 >> 4) & 0xf, l2 = p2 & 0xf;
    uint8_t d = (l1 + l2 + c) < 10 ? 0 : 1;
    if (h1 + h2 == 9) d |= 2;
    else if (h1 + h2 > 9) d |= 4;
    const uint8_t v = (uint8_t)(p1 + p2 + c + kBcdOut[d]);
    set_cnz(t, v);
    if (d > 2) t->sr |= TMS7000_SR_C;
    return v;
}
static int op_dsb(tms7000 *t, uint8_t p1, uint8_t p2)
{
    consume(t, 2);
    const int c = get_c(t) ? 0 : 1;
    const uint8_t h1 = (p1 >> 4) & 0xf, l1 = p1 & 0xf, h2 = (p2 >> 4) & 0xf, l2 = p2 & 0xf;
    uint8_t d = (l1 - c) >= l2 ? 0 : 1;
    if (h1 == h2) d |= 2;
    else if (h1 < h2) d |= 4;
    const uint8_t v = (uint8_t)(p1 - p2 - c - kBcdOut[d]);
    set_cnz(t, v);
    if (d <= 2) t->sr |= TMS7000_SR_C;
    return v;
}
static int op_djnz(tms7000 *t, uint8_t p1, uint8_t p2)
{
    (void)p2;
    const uint16_t v = (uint16_t)(p1 - 1);
    shortbranch(t, v != 0);
    return v;
}
static int op_btjo(tms7000 *t, uint8_t p1, uint8_t p2)
{
    const uint8_t v = (uint8_t)(p1 & p2);
    set_cnz(t, v);
    shortbranch(t, v != 0);
    return WB_NO;
}
static int op_btjz(tms7000 *t, uint8_t p1, uint8_t p2)
{
    const uint8_t v = (uint8_t)(~p1 & p2);
    set_cnz(t, v);
    shortbranch(t, v != 0);
    return WB_NO;
}

// ---- the addressing modes ----------------------------------------------------------

static void apply_write(tms7000 *t, uint16_t a, int result, bool to_p)
{
    if (result > WB_NO) {
        if (to_p) write_p(t, a, (uint8_t)result);
        else write_r8(t, a, (uint8_t)result);
    }
}

static void am_a(tms7000 *t, op_fn op) { consume(t, 5); apply_write(t, 0, op(t, read_r8(t, 0), 0), false); }
static void am_b(tms7000 *t, op_fn op) { consume(t, 5); apply_write(t, 1, op(t, read_r8(t, 1), 0), false); }
static void am_r(tms7000 *t, op_fn op)
{
    consume(t, 7);
    const uint8_t r = imm8(t);
    apply_write(t, r, op(t, read_r8(t, r), 0), false);
}
static void am_a2a(tms7000 *t, op_fn op) { consume(t, 6); apply_write(t, 0, op(t, read_r8(t, 0), read_r8(t, 0)), false); }
static void am_a2b(tms7000 *t, op_fn op) { consume(t, 6); apply_write(t, 1, op(t, read_r8(t, 1), read_r8(t, 0)), false); }
static void am_a2p(tms7000 *t, op_fn op)
{
    consume(t, 10);
    const uint8_t r = imm8(t);
    const uint8_t p1 = read_p(t, r);
    apply_write(t, r, op(t, p1, read_r8(t, 0)), true);
}
static void am_a2r(tms7000 *t, op_fn op)
{
    consume(t, 8);
    const uint8_t r = imm8(t);
    apply_write(t, r, op(t, read_r8(t, r), read_r8(t, 0)), false);
}
static void am_b2a(tms7000 *t, op_fn op) { consume(t, 5); apply_write(t, 0, op(t, read_r8(t, 0), read_r8(t, 1)), false); }
static void am_b2b(tms7000 *t, op_fn op) { consume(t, 6); apply_write(t, 1, op(t, read_r8(t, 1), read_r8(t, 1)), false); }
static void am_b2r(tms7000 *t, op_fn op)
{
    consume(t, 7);
    const uint8_t r = imm8(t);
    apply_write(t, r, op(t, read_r8(t, r), read_r8(t, 1)), false);
}
static void am_b2p(tms7000 *t, op_fn op)
{
    consume(t, 9);
    const uint8_t r = imm8(t);
    const uint8_t p1 = read_p(t, r);
    apply_write(t, r, op(t, p1, read_r8(t, 1)), true);
}
static void am_r2a(tms7000 *t, op_fn op)
{
    consume(t, 8);
    const uint8_t p2 = read_r8(t, imm8(t));
    apply_write(t, 0, op(t, read_r8(t, 0), p2), false);
}
static void am_r2b(tms7000 *t, op_fn op)
{
    consume(t, 8);
    const uint8_t p2 = read_r8(t, imm8(t));
    apply_write(t, 1, op(t, read_r8(t, 1), p2), false);
}
static void am_r2r(tms7000 *t, op_fn op)
{
    consume(t, 10);
    const uint8_t p2 = read_r8(t, imm8(t));
    const uint8_t r = imm8(t);
    apply_write(t, r, op(t, read_r8(t, r), p2), false);
}
static void am_i2a(tms7000 *t, op_fn op)
{
    consume(t, 7);
    const uint8_t p2 = imm8(t);
    apply_write(t, 0, op(t, read_r8(t, 0), p2), false);
}
static void am_i2b(tms7000 *t, op_fn op)
{
    consume(t, 7);
    const uint8_t p2 = imm8(t);
    apply_write(t, 1, op(t, read_r8(t, 1), p2), false);
}
static void am_i2r(tms7000 *t, op_fn op)
{
    consume(t, 9);
    const uint8_t p2 = imm8(t);
    const uint8_t r = imm8(t);
    apply_write(t, r, op(t, read_r8(t, r), p2), false);
}
static void am_i2p(tms7000 *t, op_fn op)
{
    consume(t, 11);
    const uint8_t p2 = imm8(t);
    const uint8_t r = imm8(t);
    const uint8_t p1 = read_p(t, r);
    apply_write(t, r, op(t, p1, p2), true);
}
static void am_p2a(tms7000 *t, op_fn op)
{
    consume(t, 9);
    const uint8_t p2 = read_p(t, imm8(t));
    apply_write(t, 0, op(t, read_r8(t, 0), p2), false);
}
static void am_p2b(tms7000 *t, op_fn op)
{
    consume(t, 8);
    const uint8_t p2 = read_p(t, imm8(t));
    apply_write(t, 1, op(t, read_r8(t, 1), p2), false);
}

// ---- the other instructions ------------------------------------------------------------

static void decd(tms7000 *t, uint8_t r, int cyc)
{
    consume(t, cyc);
    const uint32_t v = (uint32_t)(read_r16(t, r) - 1);
    write_r16(t, r, (uint16_t)v);
    set_nz(t, (uint16_t)(v >> 8));
    set_c(t, (uint16_t)~(v >> 8));
}

static void cmpa(tms7000 *t, uint8_t m, int cyc)
{
    consume(t, cyc);
    const uint16_t v = (uint16_t)(read_r8(t, 0) - m);
    set_nz(t, v);
    set_c(t, (uint16_t)~v);
}

static void lda(tms7000 *t, uint16_t a, int cyc)
{
    consume(t, cyc);
    const uint8_t v = read_mem8(t, a);
    write_r8(t, 0, v);
    set_cnz(t, v);
}

static void sta(tms7000 *t, uint16_t a, int cyc)
{
    consume(t, cyc);
    const uint8_t v = read_r8(t, 0);
    write_mem8(t, a, v);
    set_cnz(t, v);
}

static void call(tms7000 *t, uint16_t a, int cyc)
{
    consume(t, cyc);
    push16(t, t->pc);
    t->pc = a;
}

static void movd(tms7000 *t, uint16_t v, int cyc)
{
    consume(t, cyc);
    write_r16(t, imm8(t), v);
    set_cnz(t, (uint16_t)(v >> 8));
}

static void lvdp(tms7000 *t)
{
    consume(t, 10);
    imm8(t);
    (void)read_p(t, 0x28);
    const uint8_t v = read_p(t, 0x24);
    write_r8(t, 0, v);
    set_cnz(t, v);
}

static const op_fn kAlu[16] = { NULL, NULL, op_mov, op_and, op_or, op_xor, op_btjo, op_btjz,
                                op_add, op_adc, op_sub, op_sbb, op_mpy, op_cmp, op_dac, op_dsb };
static const op_fn kUnary[16] = { NULL, NULL, op_dec, op_inc, op_inv, op_clr, op_xchb, op_swap,
                                  NULL, NULL, op_djnz, NULL, op_rr, op_rrc, op_rl, op_rlc };

static void jmp(tms7000 *t, bool check)
{
    consume(t, 3);
    shortbranch(t, check);
}

static void execute_one(tms7000 *t, uint8_t op)
{
    if (t->exl_lvdp && op == 0xd7) { lvdp(t); return; }
    const uint8_t hi = op >> 4, lo = op & 15;
    // the regular ALU blocks: $1x-$7x with x = 2..F
    if (hi >= 1 && hi <= 7 && lo >= 2) {
        const op_fn f = kAlu[lo];
        switch (hi) {
        case 1: am_r2a(t, f); return;
        case 2: am_i2a(t, f); return;
        case 3: am_r2b(t, f); return;
        case 4: am_r2r(t, f); return;
        case 5: am_i2b(t, f); return;
        case 6: am_b2a(t, f); return;
        case 7: am_i2r(t, f); return;
        }
    }
    // the single-operand blocks: $Bx (A), $Cx (B), $Dx (Rn)
    if (hi >= 0xb && hi <= 0xd && kUnary[lo]) {
        if (hi == 0xb) am_a(t, kUnary[lo]);
        else if (hi == 0xc) am_b(t, kUnary[lo]);
        else am_r(t, kUnary[lo]);
        return;
    }
    if (op >= 0xe8) {                            // TRAP 23..0
        consume(t, 14);
        push16(t, t->pc);
        t->pc = read_mem16(t, (uint16_t)(0xff00 | (uint8_t)(op << 1)));
        return;
    }
    uint8_t v;
    switch (op) {
    case 0x00: consume(t, 5); break;                                   // NOP
    case 0x01: consume(t, 6); t->pc--; t->idle_state = true; break;    // IDLE
    case 0x05:                                                         // EINT
        consume(t, 5);
        t->sr |= TMS7000_SR_N | TMS7000_SR_Z | TMS7000_SR_C | TMS7000_SR_I;
        check_interrupts(t);
        break;
    case 0x06:                                                         // DINT
        consume(t, 5);
        t->sr &= (uint8_t)~(TMS7000_SR_N | TMS7000_SR_Z | TMS7000_SR_C | TMS7000_SR_I);
        break;
    case 0x07: consume(t, 5); t->sr = (uint8_t)((t->sr & ~TMS7000_SR_N) | TMS7000_SR_C | TMS7000_SR_Z); break;  // SETC
    case 0x08: consume(t, 6); t->sr = (uint8_t)(pull8(t) & 0xf0); check_interrupts(t); break;  // POP ST
    case 0x09: consume(t, 6); write_r8(t, 1, t->sp); break;           // STSP
    case 0x0a: consume(t, 7); t->pc = pull16(t); break;               // RETS
    case 0x0b:                                                         // RETI
        consume(t, 9);
        t->pc = pull16(t);
        t->sr = (uint8_t)(pull8(t) & 0xf0);
        check_interrupts(t);
        break;
    case 0x0d: consume(t, 5); t->sp = read_r8(t, 1); break;           // LDSP
    case 0x0e: consume(t, 6); push8(t, t->sr); break;                 // PUSH ST

    case 0x80: am_p2a(t, op_mov); break;
    case 0x82: am_a2p(t, op_mov); break;
    case 0x83: am_a2p(t, op_and); break;
    case 0x84: am_a2p(t, op_or); break;
    case 0x85: am_a2p(t, op_xor); break;
    case 0x86: am_a2p(t, op_btjo); break;
    case 0x87: am_a2p(t, op_btjz); break;
    case 0x88: { const uint16_t a = imm16(t); movd(t, a, 15); break; }
    case 0x8a: { const uint16_t a = imm16(t); lda(t, a, 11); break; }
    case 0x8b: { const uint16_t a = imm16(t); sta(t, a, 11); break; }
    case 0x8c: consume(t, 10); t->pc = imm16(t); break;
    case 0x8d: { const uint16_t a = imm16(t); cmpa(t, read_mem8(t, a), 12); break; }
    case 0x8e: { const uint16_t a = imm16(t); call(t, a, 14); break; }

    case 0x91: am_p2b(t, op_mov); break;
    case 0x92: am_b2p(t, op_mov); break;
    case 0x93: am_b2p(t, op_and); break;
    case 0x94: am_b2p(t, op_or); break;
    case 0x95: am_b2p(t, op_xor); break;
    case 0x96: am_b2p(t, op_btjo); break;
    case 0x97: am_b2p(t, op_btjz); break;
    case 0x98: { const uint16_t a = read_r16(t, imm8(t)); movd(t, a, 14); break; }
    case 0x9a: { const uint16_t a = read_r16(t, imm8(t)); lda(t, a, 10); break; }
    case 0x9b: { const uint16_t a = read_r16(t, imm8(t)); sta(t, a, 10); break; }
    case 0x9c: consume(t, 9); t->pc = read_r16(t, imm8(t)); break;
    case 0x9d: { const uint16_t a = read_r16(t, imm8(t)); cmpa(t, read_mem8(t, a), 11); break; }
    case 0x9e: { const uint16_t a = read_r16(t, imm8(t)); call(t, a, 13); break; }

    case 0xa2: am_i2p(t, op_mov); break;
    case 0xa3: am_i2p(t, op_and); break;
    case 0xa4: am_i2p(t, op_or); break;
    case 0xa5: am_i2p(t, op_xor); break;
    case 0xa6: am_i2p(t, op_btjo); break;
    case 0xa7: am_i2p(t, op_btjz); break;
    case 0xa8: { const uint16_t a = (uint16_t)(imm16(t) + read_r8(t, 1)); movd(t, a, 17); break; }
    case 0xaa: { const uint16_t a = (uint16_t)(imm16(t) + read_r8(t, 1)); lda(t, a, 13); break; }
    case 0xab: { const uint16_t a = (uint16_t)(imm16(t) + read_r8(t, 1)); sta(t, a, 13); break; }
    case 0xac: { consume(t, 12); const uint16_t a = imm16(t); t->pc = (uint16_t)(a + read_r8(t, 1)); break; }
    case 0xad: { const uint16_t a = (uint16_t)(imm16(t) + read_r8(t, 1)); cmpa(t, read_mem8(t, a), 14); break; }
    case 0xae: { const uint16_t a = (uint16_t)(imm16(t) + read_r8(t, 1)); call(t, a, 16); break; }

    case 0xb0: am_a2a(t, op_mov); break;
    case 0xb1: am_b2a(t, op_mov); break;
    case 0xb8: consume(t, 6); v = read_r8(t, 0); push8(t, v); set_cnz(t, v); break;
    case 0xb9: consume(t, 6); v = pull8(t); write_r8(t, 0, v); set_cnz(t, v); break;
    case 0xbb: decd(t, 0, 9); break;
    case 0xc0: am_a2b(t, op_mov); break;
    case 0xc1: am_b2b(t, op_mov); break;
    case 0xc8: consume(t, 6); v = read_r8(t, 1); push8(t, v); set_cnz(t, v); break;
    case 0xc9: consume(t, 6); v = pull8(t); write_r8(t, 1, v); set_cnz(t, v); break;
    case 0xcb: decd(t, 1, 9); break;
    case 0xd0: am_a2r(t, op_mov); break;
    case 0xd1: am_b2r(t, op_mov); break;
    case 0xd8: consume(t, 8); v = read_r8(t, imm8(t)); push8(t, v); set_cnz(t, v); break;
    case 0xd9: { consume(t, 8); v = pull8(t); write_r8(t, imm8(t), v); set_cnz(t, v); break; }
    case 0xdb: { consume(t, 0); const uint8_t r = imm8(t); decd(t, r, 11); break; }

    case 0xe0: jmp(t, true); break;
    case 0xe1: jmp(t, (t->sr & TMS7000_SR_N) != 0); break;
    case 0xe2: jmp(t, (t->sr & TMS7000_SR_Z) != 0); break;
    case 0xe3: jmp(t, (t->sr & TMS7000_SR_C) != 0); break;
    case 0xe4: jmp(t, !(t->sr & (TMS7000_SR_Z | TMS7000_SR_N))); break;
    case 0xe5: jmp(t, !(t->sr & TMS7000_SR_N)); break;
    case 0xe6: jmp(t, !(t->sr & TMS7000_SR_Z)); break;
    case 0xe7: jmp(t, !(t->sr & TMS7000_SR_C)); break;
    default: consume(t, 5); break;                                     // illegal
    }
}
