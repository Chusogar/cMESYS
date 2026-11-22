// mcs51dsp.c - Intel MCS-51 (8051 / 8751): C port of dsp-cpp's cpu/mcs51
// (from mcs51.pas): internal RAM and SFRs, timers 0 and 1, the two external
// interrupt lines and the four ports, as the arcade MCUs need them.
#include "mcs51dsp.h"
#include <string.h>


static const uint8_t kCycles[256] = {
    1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 4, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

#define kAddrP0 0x80
#define kAddrSp 0x81
#define kAddrDpl 0x82
#define kAddrDph 0x83
#define kAddrTcon 0x88
#define kAddrTmod 0x89
#define kAddrTl0 0x8a
#define kAddrTl1 0x8b
#define kAddrTh0 0x8c
#define kAddrTh1 0x8d
#define kAddrP1 0x90
#define kAddrP2 0xa0
#define kAddrIe 0xa8
#define kAddrP3 0xb0
#define kAddrIp 0xb8
#define kAddrPsw 0xd0
#define kAddrAcc 0xe0
#define kAddrB 0xf0

#define kVectorIe0 0x003
#define kVectorTf0 0x00b
#define kVectorIe1 0x013
#define kVectorTf1 0x01b

static inline uint8_t get_bit(uint8_t value, int bit) { return (uint8_t)((value >> bit) & 1); }

static inline void set_bit_(uint8_t *value, int bit, uint8_t state) { *value = (uint8_t)((*value & ~(1 << bit)) | (state << bit)); }
#define set_bit(lv, b, s) set_bit_(&(lv), b, s)


#define RELEASE_HOLD_LINES() do { \
        if (m->irq0 == MCS51_HOLD) m->last_line_state &= 0xfffffffe; \
        if (m->irq1 == MCS51_HOLD) m->last_line_state &= 0xfffffffd; \
    } while (0)

static inline uint8_t r_reg(mcs51 *m, uint8_t a) { return m->ram[a | (m->sfr[0xd0] & 0x18)]; }
static inline void set_reg(mcs51 *m, uint8_t a, uint8_t v) { m->ram[a | (m->sfr[0xd0] & 0x18)] = v; }
static inline uint8_t fetch(mcs51 *m) { return m->rom[m->pc++ & (MCS51_ROM_SIZE - 1)]; }
static inline uint8_t peek(mcs51 *m, uint16_t a) { return m->rom[a & (MCS51_ROM_SIZE - 1)]; }
static inline uint8_t acc(mcs51 *m) { return m->sfr[0xe0]; }
static inline void set_acc(mcs51 *m, uint8_t v) { m->sfr[0xe0] = v; m->calc_parity = true; }
static uint8_t get_psw(mcs51 *m);
static void set_psw(mcs51 *m, uint8_t value);
static void add_flags(mcs51 *m, uint8_t a, uint8_t data, uint8_t carry);
static void sub_flags(mcs51 *m, uint8_t a, uint8_t data, uint8_t carry);
static void update_irq_prio(mcs51 *m, uint8_t ipl, uint8_t iph);
static void iram_w(mcs51 *m, uint8_t pos, uint8_t value);
static uint8_t iram_r(mcs51 *m, uint8_t pos);
static uint8_t iram_ir(mcs51 *m, uint8_t pos);
static void iram_iw(mcs51 *m, uint8_t pos, uint8_t value);
static uint8_t bit_address_r(mcs51 *m, uint8_t pos);
static void bit_address_w(mcs51 *m, uint8_t pos, uint8_t bit);
static void push_pc(mcs51 *m);
static void pop_pc(mcs51 *m);
static void clear_irqs(mcs51 *m);
static int evaluate_irq(mcs51 *m);
static void update_timer_t0(mcs51 *m, int cycles);
static void update_timer_t1(mcs51 *m, int cycles);





void mcs51_reset(mcs51 *m) {
    memset(m->ram, 0, sizeof m->ram);
    memset(m->sfr, 0, sizeof m->sfr);
    memset(m->forced_input, 0, sizeof m->forced_input);
    m->pc = 0;
    m->sfr[kAddrSp] = 0x07;
    set_psw(m, 0);
    m->calc_parity = false;
    m->rwm = false;
    for (int port = 3; port >= 0; port--) {
        const uint8_t address = (uint8_t)(kAddrP0 + port * 0x10);
        m->sfr[address] = 0xff;
        if (m->port_write[port]) m->port_write[port](m->user, 0xff);
    }
    m->irq0 = MCS51_CLEAR;
    m->irq1 = MCS51_CLEAR;
    m->last_line_state = 0;
    m->irq_active = 0;
    m->cur_irq_prio = -1;
    m->t0_count = 0;
    m->t1_count = 0;
}

static uint8_t get_psw(mcs51 *m) {
    uint8_t value = 0;
    if (m->psw.c) value |= 0x80;
    if (m->psw.ac) value |= 0x40;
    if (m->psw.u) value |= 0x20;
    if (m->psw.bank0) value |= 0x10;
    if (m->psw.bank1) value |= 0x08;
    if (m->psw.o) value |= 0x04;
    if (m->psw.p) value |= 0x01;
    m->calc_parity = true;
    return value;
}

static void set_psw(mcs51 *m, uint8_t value) {
    m->psw.c = (value & 0x80) != 0;
    m->psw.ac = (value & 0x40) != 0;
    m->psw.u = (value & 0x20) != 0;
    m->psw.bank0 = (value & 0x10) != 0;
    m->psw.bank1 = (value & 0x08) != 0;
    m->psw.o = (value & 0x04) != 0;
    m->psw.p = (value & 0x01) != 0;
    m->sfr[kAddrPsw] = value;
}

static void add_flags(mcs51 *m, uint8_t a, uint8_t data, uint8_t carry) {
    const uint16_t result = (uint16_t)(a + data + carry);
    const int16_t signed_result = (int16_t)((int8_t)(a) + (int8_t)(data) + carry);
    m->psw.c = (result & 0x100) != 0;
    m->psw.ac = (((a & 0x0f) + (data & 0x0f) + carry) & 0x10) != 0;
    m->psw.o = signed_result < -128 || signed_result > 127;
    m->sfr[kAddrPsw] = get_psw(m);
}

static void sub_flags(mcs51 *m, uint8_t a, uint8_t data, uint8_t carry) {
    const uint16_t result = (uint16_t)(a - (data + carry));
    const int16_t signed_result = (int16_t)((int8_t)(a) - (int8_t)((uint8_t)(data + carry)));
    m->psw.c = (result & 0x100) != 0;
    m->psw.ac = (((a & 0x0f) - (data & 0x0f) + carry) & 0x10) != 0;
    m->psw.o = signed_result < -128 || signed_result > 127;
    m->sfr[kAddrPsw] = get_psw(m);
}

static void update_irq_prio(mcs51 *m, uint8_t ipl, uint8_t iph) {
    for (int i = 0; i < 8; i++) {
        m->irq_prio[(size_t)(i)] = (uint8_t)(((ipl >> i) & 1) | (((iph >> i) & 1) << 1));
    }
}

static void iram_w(mcs51 *m, uint8_t pos, uint8_t value) {
    if (pos < 0x80) {
        m->ram[pos] = value;
        return;
    }
    switch (pos) {
        case kAddrAcc:
            m->calc_parity = true;
            break;
        case kAddrPsw:
            set_psw(m, value);
            m->calc_parity = true;
            break;
        case kAddrP0:
        case kAddrP1:
        case kAddrP2:
        case kAddrP3: {
            const size_t port = (size_t)((pos - kAddrP0) >> 4);
            if (m->port_write[port]) m->port_write[port](m->user, value);
            break;
        }
        case kAddrIp:
            update_irq_prio(m, value, 0);
            break;
        default:
            break;
    }
    m->sfr[pos] = value;
}

static uint8_t iram_r(mcs51 *m, uint8_t pos) {
    if (pos < 0x80) return m->ram[pos];
    switch (pos) {
        case kAddrP0:
        case kAddrP1:
        case kAddrP2:
        case kAddrP3: {
            const size_t port = (size_t)((pos - kAddrP0) >> 4);
            if (m->rwm) return m->sfr[pos];
            uint8_t result = 0xff;
            if (m->port_read[port]) {
                result = (uint8_t)((m->sfr[pos] | m->forced_input[port]) & m->port_read[port](m->user));
            }
            if (pos == kAddrP3) {
                if (m->irq0 != MCS51_CLEAR) result &= 0xfb;
                if (m->irq1 != MCS51_CLEAR) result &= 0xf7;
            }
            return result;
        }
        default:
            return m->sfr[pos];
    }
}

static uint8_t iram_ir(mcs51 *m, uint8_t pos) { return pos < 0x80 ? m->ram[pos] : 0xff; }

static void iram_iw(mcs51 *m, uint8_t pos, uint8_t value) {
    if (pos < 0x80) m->ram[pos] = value;
}

static uint8_t bit_address_r(mcs51 *m, uint8_t pos) {
    const uint8_t address =
        pos < 0x80 ? (uint8_t)(((pos & 0x78) >> 3) + 0x20) : (uint8_t)(((pos & 0x78) >> 3) * 8 + 0x80);
    const int bit = pos & 0x7;
    return (uint8_t)((iram_r(m, address) >> bit) & 1);
}

static void bit_address_w(mcs51 *m, uint8_t pos, uint8_t bit) {
    const uint8_t address =
        pos < 0x80 ? (uint8_t)(((pos & 0x78) >> 3) + 0x20) : (uint8_t)(((pos & 0x78) >> 3) * 8 + 0x80);
    const int bit_pos = pos & 0x7;
    uint8_t value = (uint8_t)(iram_r(m, address) & ~(1 << bit_pos));
    value |= (uint8_t)((bit & 1) << bit_pos);
    iram_w(m, address, value);
}

static void push_pc(mcs51 *m) {
    uint8_t sp = (uint8_t)(m->sfr[kAddrSp] + 1);
    iram_iw(m, sp, (uint8_t)(m->pc & 0xff));
    sp++;
    iram_iw(m, sp, (uint8_t)(m->pc >> 8));
    m->sfr[kAddrSp] = sp;
}

static void pop_pc(mcs51 *m) {
    uint8_t sp = m->sfr[kAddrSp];
    m->pc = (uint16_t)(iram_ir(m, sp) << 8);
    sp--;
    m->pc = (uint16_t)(m->pc | iram_ir(m, sp));
    m->sfr[kAddrSp] = (uint8_t)(sp - 1);
}

static void clear_irqs(mcs51 *m) {
    if (m->cur_irq_prio >= 0) m->irq_active = (uint8_t)(m->irq_active & ~(1 << m->cur_irq_prio));
    if (m->irq_active & 4) {
        m->cur_irq_prio = 2;
    } else if (m->irq_active & 2) {
        m->cur_irq_prio = 1;
    } else if (m->irq_active & 1) {
        m->cur_irq_prio = 0;
    } else {
        m->cur_irq_prio = -1;
    }
}

static int evaluate_irq(mcs51 *m) {
    uint8_t ints = (uint8_t)(get_bit(m->sfr[kAddrTcon], 1) | (get_bit(m->sfr[kAddrTcon], 5) << 1) |
                           (get_bit(m->sfr[kAddrTcon], 3) << 2) | (get_bit(m->sfr[kAddrTcon], 7) << 3));
    const uint8_t int_mask = get_bit(m->sfr[kAddrIe], 7) != 0 ? m->sfr[kAddrIe] : 0;
    ints = (uint8_t)(ints & int_mask);


    if (ints == 0) {
        RELEASE_HOLD_LINES();
        return 0;
    }

    int priority_request = -1;
    uint16_t vector = 0;
    for (int i = 0; i < m->num_interrupts; i++) {
        if ((ints & (1 << i)) == 0) continue;
        if (m->irq_prio[(size_t)(i)] > priority_request) {
            priority_request = m->irq_prio[(size_t)(i)];
            vector = (uint16_t)((i << 3) | 3);
        }
    }
    if (m->irq_active != 0 && priority_request <= m->cur_irq_prio) {
        RELEASE_HOLD_LINES();
        return 0;
    }
    // Work around the polling latency of "jb int0/int1" loops.
    if (vector == kVectorIe0 && peek(m, m->pc) == 0x20 && peek(m, (uint16_t)(m->pc + 1)) == 0xb2) m->pc += 3;
    if (vector == kVectorIe1 && peek(m, m->pc) == 0x20 && peek(m, (uint16_t)(m->pc + 1)) == 0xb3) m->pc += 3;

    push_pc(m);
    m->pc = vector;
    m->cur_irq_prio = priority_request;
    m->irq_active = (uint8_t)(m->irq_active | (1 << priority_request));
    switch (vector) {
        case kVectorIe0:
            if (get_bit(m->sfr[kAddrTcon], 0) != 0) set_bit(m->sfr[kAddrTcon], 1, 0);
            if (m->irq0 == MCS51_HOLD) mcs51_set_irq0_line(m, MCS51_CLEAR);
            break;
        case kVectorTf0:
            set_bit(m->sfr[kAddrTcon], 5, 0);
            break;
        case kVectorIe1:
            if (get_bit(m->sfr[kAddrTcon], 2) != 0) set_bit(m->sfr[kAddrTcon], 3, 0);
            if (m->irq1 == MCS51_HOLD) mcs51_set_irq1_line(m, MCS51_CLEAR);
            break;
        case kVectorTf1:
            set_bit(m->sfr[kAddrTcon], 7, 0);
            break;
        default:
            break;
    }
    return 2;
}

void mcs51_set_irq0_line(mcs51 *m, int state) {
    const uint32_t new_state = (m->last_line_state & 0xfffffffe) | (state != MCS51_CLEAR ? 1 : 0);
    const uint32_t transition = ~m->last_line_state & new_state;
    if (state != MCS51_CLEAR) {
        if (get_bit(m->sfr[kAddrTcon], 0) != 0) {
            if ((transition & 1) != 0) set_bit(m->sfr[kAddrTcon], 1, 1);
        } else {
            set_bit(m->sfr[kAddrTcon], 1, 1);
        }
    } else if (get_bit(m->sfr[kAddrTcon], 0) == 0) {
        set_bit(m->sfr[kAddrTcon], 1, 0);
    }
    m->last_line_state = new_state;
    m->irq0 = state;
}

void mcs51_set_irq1_line(mcs51 *m, int state) {
    const uint32_t new_state =
        (m->last_line_state & 0xfffffffd) | (uint32_t)((state != MCS51_CLEAR ? 1 : 0) << 1);
    const uint32_t transition = ~m->last_line_state & new_state;
    if (state != MCS51_CLEAR) {
        if (get_bit(m->sfr[kAddrTcon], 2) != 0) {
            if ((transition & 2) != 0) set_bit(m->sfr[kAddrTcon], 3, 1);
        } else {
            set_bit(m->sfr[kAddrTcon], 3, 1);
        }
    } else if (get_bit(m->sfr[kAddrTcon], 2) == 0) {
        set_bit(m->sfr[kAddrTcon], 3, 0);
    }
    m->last_line_state = new_state;
    m->irq1 = state;
}

static void update_timer_t0(mcs51 *m, int cycles) {
    const uint8_t mode = (uint8_t)((get_bit(m->sfr[kAddrTmod], 1) << 1) | get_bit(m->sfr[kAddrTmod], 0));
    if (get_bit(m->sfr[kAddrTcon], 4) != 0) {
        uint32_t delta = get_bit(m->sfr[kAddrTmod], 2) != 0 ? m->t0_count : (uint32_t)(cycles);
        m->t0_count = 0;
        if (get_bit(m->sfr[kAddrTmod], 3) != 0 && get_bit(m->sfr[kAddrTcon], 1) == 0) delta = 0;
        uint32_t count = 0;
        switch (mode) {
            case 0:
                count = (uint32_t)((m->sfr[kAddrTh0] << 5) | (m->sfr[kAddrTl0] & 0x1f)) + delta;
                if ((count & 0xffffe000) != 0) set_bit(m->sfr[kAddrTcon], 5, 1);
                m->sfr[kAddrTh0] = (uint8_t)((count >> 5) & 0xff);
                m->sfr[kAddrTl0] = (uint8_t)(count & 0x1f);
                break;
            case 1:
                count = (uint32_t)((m->sfr[kAddrTh0] << 8) | m->sfr[kAddrTl0]) + delta;
                if ((count & 0xffff0000) != 0) set_bit(m->sfr[kAddrTcon], 5, 1);
                m->sfr[kAddrTh0] = (uint8_t)((count >> 8) & 0xff);
                m->sfr[kAddrTl0] = (uint8_t)(count & 0xff);
                break;
            case 2:
                count = m->sfr[kAddrTl0] + delta;
                if ((count & 0xffffff00) != 0) {
                    set_bit(m->sfr[kAddrTcon], 5, 1);
                    count += m->sfr[kAddrTh0];
                }
                m->sfr[kAddrTl0] = (uint8_t)(count & 0xff);
                break;
            default:
                count = m->sfr[kAddrTl0] + delta;
                if ((count & 0xffffff00) != 0) set_bit(m->sfr[kAddrTcon], 5, 1);
                m->sfr[kAddrTl0] = (uint8_t)(count & 0xff);
                break;
        }
    }
    if (mode == 3 && get_bit(m->sfr[kAddrTcon], 6) != 0) {
        const uint32_t count = m->sfr[kAddrTh0] + (uint32_t)(cycles);
        if ((count & 0xffffff00) != 0) set_bit(m->sfr[kAddrTcon], 7, 1);
        m->sfr[kAddrTh0] = (uint8_t)(count & 0xff);
    }
}

static void update_timer_t1(mcs51 *m, int cycles) {
    const uint8_t mode = (uint8_t)((get_bit(m->sfr[kAddrTmod], 5) << 1) | get_bit(m->sfr[kAddrTmod], 4));
    const uint8_t mode_t0 =
        (uint8_t)((get_bit(m->sfr[kAddrTmod], 1) << 1) | get_bit(m->sfr[kAddrTmod], 0));
    uint32_t delta = (uint32_t)(cycles);
    if (mode_t0 != 3) {
        if (get_bit(m->sfr[kAddrTcon], 6) == 0) return;
        if (get_bit(m->sfr[kAddrTmod], 6) != 0) delta = m->t1_count;
        if (get_bit(m->sfr[kAddrTmod], 7) != 0 && get_bit(m->sfr[kAddrTcon], 3) == 0) delta = 0;
    }
    m->t1_count = 0;
    uint32_t count = 0;
    uint32_t overflow = 0;
    switch (mode) {
        case 0:
            count = (uint32_t)((m->sfr[kAddrTh1] << 5) | (m->sfr[kAddrTl1] & 0x1f)) + delta;
            overflow = count & 0xffffe000;
            m->sfr[kAddrTh1] = (uint8_t)((count >> 5) & 0xff);
            m->sfr[kAddrTl1] = (uint8_t)(count & 0x1f);
            break;
        case 1:
            count = (uint32_t)((m->sfr[kAddrTh1] << 8) | m->sfr[kAddrTl1]) + delta;
            overflow = count & 0xffff0000;
            m->sfr[kAddrTh1] = (uint8_t)((count >> 8) & 0xff);
            m->sfr[kAddrTl1] = (uint8_t)(count & 0xff);
            break;
        case 2:
            count = m->sfr[kAddrTl1] + delta;
            overflow = count & 0xffffff00;
            if (overflow != 0) count += m->sfr[kAddrTh1];
            m->sfr[kAddrTl1] = (uint8_t)(count & 0xff);
            break;
        default:
            break;
    }
    if (overflow != 0 && mode_t0 != 3) set_bit(m->sfr[kAddrTcon], 7, 1);
}

int mcs51_run(mcs51 *m, int cycles) {
    int executed = 0;
    while (executed < cycles) {
        if (m->calc_parity) {
            uint8_t parity = 0;
            uint8_t value = acc(m);
            for (int i = 0; i < 8; i++) {
                parity = (uint8_t)(parity ^ (value & 1));
                value = (uint8_t)(value >> 1);
            }
            m->psw.p = (parity & 1) != 0;
            m->sfr[kAddrPsw] = get_psw(m);
            m->calc_parity = false;
        }
        const int irq_cycles = evaluate_irq(m);
        const uint8_t opcode = fetch(m);
        switch (opcode) {
            case 0x00:  // nop
                break;
            case 0x01:
            case 0x21:
            case 0x41:
            case 0x61:
            case 0x81:
            case 0xa1:
            case 0xc1:
            case 0xe1: {  // ajmp
                const uint8_t low = fetch(m);
                m->pc = (uint16_t)((m->pc & 0xf800) | ((opcode & 0xe0) << 3) | low);
                break;
            }
            case 0x02: {  // ljmp
                const uint8_t high = fetch(m);
                const uint8_t low = fetch(m);
                m->pc = (uint16_t)((high << 8) | low);
                break;
            }
            case 0x03:  // rr a
                set_acc(m, (uint8_t)((acc(m) >> 1) | ((acc(m) & 1) << 7)));
                break;
            case 0x04:  // inc a
                set_acc(m, (uint8_t)(acc(m) + 1));
                break;
            case 0x05: {  // inc mem
                m->rwm = true;
                const uint8_t pos = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) + 1));
                m->rwm = false;
                break;
            }
            case 0x06:
            case 0x07: {  // inc @ri
                const uint8_t pos = r_reg(m, (uint8_t)(opcode & 1));
                iram_iw(m, pos, (uint8_t)(iram_ir(m, pos) + 1));
                break;
            }
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x0d:
            case 0x0e:
            case 0x0f:  // inc rn
                set_reg(m, (uint8_t)(opcode & 7), (uint8_t)(r_reg(m, (uint8_t)(opcode & 7)) + 1));
                break;
            case 0x10: {  // jbc
                m->rwm = true;
                const uint8_t pos = fetch(m);
                const uint8_t offset = fetch(m);
                if (bit_address_r(m, pos) != 0) {
                    m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                    bit_address_w(m, pos, 0);
                }
                m->rwm = false;
                break;
            }
            case 0x11:
            case 0x31:
            case 0x51:
            case 0x71:
            case 0x91:
            case 0xb1:
            case 0xd1:
            case 0xf1: {  // acall
                const uint8_t low = fetch(m);
                push_pc(m);
                m->pc = (uint16_t)((m->pc & 0xf800) | ((opcode & 0xe0) << 3) | low);
                break;
            }
            case 0x12: {  // lcall
                const uint8_t high = fetch(m);
                const uint8_t low = fetch(m);
                push_pc(m);
                m->pc = (uint16_t)((high << 8) | low);
                break;
            }
            case 0x13: {  // rrc a
                const uint8_t value = acc(m);
                set_acc(m, (uint8_t)((value >> 1) | (m->psw.c ? 0x80 : 0)));
                m->psw.c = (value & 1) != 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0x14:  // dec a
                set_acc(m, (uint8_t)(acc(m) - 1));
                break;
            case 0x15: {  // dec mem
                m->rwm = true;
                const uint8_t pos = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) - 1));
                m->rwm = false;
                break;
            }
            case 0x16:
            case 0x17: {  // dec @ri
                const uint8_t pos = r_reg(m, (uint8_t)(opcode & 1));
                iram_iw(m, pos, (uint8_t)(iram_ir(m, pos) - 1));
                break;
            }
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:  // dec rn
                set_reg(m, (uint8_t)(opcode & 7), (uint8_t)(r_reg(m, (uint8_t)(opcode & 7)) - 1));
                break;
            case 0x20: {  // jb
                const uint8_t pos = fetch(m);
                const uint8_t offset = fetch(m);
                if (bit_address_r(m, pos) != 0) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x22:  // ret
                pop_pc(m);
                break;
            case 0x23:  // rl a
                set_acc(m, (uint8_t)((acc(m) << 1) | ((acc(m) & 0x80) >> 7)));
                break;
            case 0x24: {  // add a,#byte
                const uint8_t data = fetch(m);
                const uint8_t result = (uint8_t)(acc(m) + data);
                add_flags(m, acc(m), data, 0);
                set_acc(m, result);
                break;
            }
            case 0x25: {  // add a,mem
                const uint8_t data = iram_r(m, fetch(m));
                const uint8_t result = (uint8_t)(acc(m) + data);
                add_flags(m, acc(m), data, 0);
                set_acc(m, result);
                break;
            }
            case 0x26:
            case 0x27: {  // add a,@ri
                const uint8_t data = iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)));
                const uint8_t result = (uint8_t)(acc(m) + data);
                add_flags(m, acc(m), data, 0);
                set_acc(m, result);
                break;
            }
            case 0x28:
            case 0x29:
            case 0x2a:
            case 0x2b:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f: {  // add a,rn
                const uint8_t data = r_reg(m, (uint8_t)(opcode & 7));
                const uint8_t result = (uint8_t)(acc(m) + data);
                add_flags(m, acc(m), data, 0);
                set_acc(m, result);
                break;
            }
            case 0x30: {  // jnb
                const uint8_t pos = fetch(m);
                const uint8_t offset = fetch(m);
                if (bit_address_r(m, pos) == 0) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x32:  // reti
                pop_pc(m);
                clear_irqs(m);
                break;
            case 0x33: {  // rlc a
                const uint8_t value = acc(m);
                set_acc(m, (uint8_t)((value << 1) | (m->psw.c ? 1 : 0)));
                m->psw.c = (value & 0x80) != 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0x34: {  // addc a,#byte
                const uint8_t data = fetch(m);
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) + data + carry);
                add_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x35: {  // addc a,mem
                const uint8_t data = iram_r(m, fetch(m));
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) + data + carry);
                add_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x36:
            case 0x37: {  // addc a,@ri
                const uint8_t data = iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)));
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) + data + carry);
                add_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x38:
            case 0x39:
            case 0x3a:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f: {  // addc a,rn
                const uint8_t data = r_reg(m, (uint8_t)(opcode & 7));
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) + data + carry);
                add_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x40: {  // jc
                const uint8_t offset = fetch(m);
                if (m->psw.c) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x42: {  // orl mem,a
                m->rwm = true;
                const uint8_t pos = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) | acc(m)));
                m->rwm = false;
                break;
            }
            case 0x43: {  // orl mem,#byte
                m->rwm = true;
                const uint8_t pos = fetch(m);
                const uint8_t data = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) | data));
                m->rwm = false;
                break;
            }
            case 0x44:  // orl a,#byte
                set_acc(m, (uint8_t)(acc(m) | fetch(m)));
                break;
            case 0x45:  // orl a,mem
                set_acc(m, (uint8_t)(acc(m) | iram_r(m, fetch(m))));
                break;
            case 0x46:
            case 0x47:  // orl a,@ri
                set_acc(m, (uint8_t)(acc(m) | iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)))));
                break;
            case 0x48:
            case 0x49:
            case 0x4a:
            case 0x4b:
            case 0x4c:
            case 0x4d:
            case 0x4e:
            case 0x4f:  // orl a,rn
                set_acc(m, (uint8_t)(acc(m) | r_reg(m, (uint8_t)(opcode & 7))));
                break;
            case 0x50: {  // jnc
                const uint8_t offset = fetch(m);
                if (!m->psw.c) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x52: {  // anl mem,a
                m->rwm = true;
                const uint8_t pos = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) & acc(m)));
                m->rwm = false;
                break;
            }
            case 0x53: {  // anl mem,#byte
                m->rwm = true;
                const uint8_t pos = fetch(m);
                const uint8_t data = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) & data));
                m->rwm = false;
                break;
            }
            case 0x54:  // anl a,#byte
                set_acc(m, (uint8_t)(acc(m) & fetch(m)));
                break;
            case 0x55:  // anl a,mem
                set_acc(m, (uint8_t)(acc(m) & iram_r(m, fetch(m))));
                break;
            case 0x56:
            case 0x57:  // anl a,@ri
                set_acc(m, (uint8_t)(acc(m) & iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)))));
                break;
            case 0x58:
            case 0x59:
            case 0x5a:
            case 0x5b:
            case 0x5c:
            case 0x5d:
            case 0x5e:
            case 0x5f:  // anl a,rn
                set_acc(m, (uint8_t)(acc(m) & r_reg(m, (uint8_t)(opcode & 7))));
                break;
            case 0x60: {  // jz
                const uint8_t offset = fetch(m);
                if (acc(m) == 0) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x62: {  // xrl mem,a
                m->rwm = true;
                const uint8_t pos = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) ^ acc(m)));
                m->rwm = false;
                break;
            }
            case 0x63: {  // xrl mem,#byte
                m->rwm = true;
                const uint8_t pos = fetch(m);
                const uint8_t data = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) ^ data));
                m->rwm = false;
                break;
            }
            case 0x64:  // xrl a,#byte
                set_acc(m, (uint8_t)(acc(m) ^ fetch(m)));
                break;
            case 0x65:  // xrl a,mem
                set_acc(m, (uint8_t)(acc(m) ^ iram_r(m, fetch(m))));
                break;
            case 0x66:
            case 0x67:  // xrl a,@ri
                set_acc(m, (uint8_t)(acc(m) ^ iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)))));
                break;
            case 0x68:
            case 0x69:
            case 0x6a:
            case 0x6b:
            case 0x6c:
            case 0x6d:
            case 0x6e:
            case 0x6f:  // xrl a,rn
                set_acc(m, (uint8_t)(acc(m) ^ r_reg(m, (uint8_t)(opcode & 7))));
                break;
            case 0x70: {  // jnz
                const uint8_t offset = fetch(m);
                if (acc(m) != 0) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x72: {  // orl c,bit
                const uint8_t pos = fetch(m);
                m->psw.c = m->psw.c || bit_address_r(m, pos) != 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0x73:  // jmp @a+dptr
                m->pc = (uint16_t)(((m->sfr[kAddrDph] << 8) | m->sfr[kAddrDpl]) + acc(m));
                break;
            case 0x74:  // mov a,#byte
                set_acc(m, fetch(m));
                break;
            case 0x75: {  // mov mem,#byte
                const uint8_t pos = fetch(m);
                iram_w(m, pos, fetch(m));
                break;
            }
            case 0x76:
            case 0x77:  // mov @ri,#byte
                iram_iw(m, r_reg(m, (uint8_t)(opcode & 1)), fetch(m));
                break;
            case 0x78:
            case 0x79:
            case 0x7a:
            case 0x7b:
            case 0x7c:
            case 0x7d:
            case 0x7e:
            case 0x7f:  // mov rn,#byte
                set_reg(m, (uint8_t)(opcode & 7), fetch(m));
                break;
            case 0x80: {  // sjmp
                const uint8_t offset = fetch(m);
                m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0x82: {  // anl c,bit
                const uint8_t pos = fetch(m);
                m->psw.c = m->psw.c && bit_address_r(m, pos) != 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0x83:  // movc a,@a+pc
                set_acc(m, peek(m, (uint16_t)(m->pc + acc(m))));
                break;
            case 0x84:  // div ab
                if (m->sfr[kAddrB] == 0) {
                    m->psw.o = true;
                } else {
                    const uint8_t quotient = (uint8_t)(acc(m) / m->sfr[kAddrB]);
                    const uint8_t remainder = (uint8_t)(acc(m) % m->sfr[kAddrB]);
                    m->sfr[kAddrAcc] = quotient;
                    m->sfr[kAddrB] = remainder;
                    m->psw.o = false;
                }
                m->psw.c = false;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            case 0x85: {  // mov mem,mem
                const uint8_t source = fetch(m);
                const uint8_t target = fetch(m);
                iram_w(m, target, iram_r(m, source));
                break;
            }
            case 0x86:
            case 0x87: {  // mov mem,@ri
                const uint8_t pos = fetch(m);
                iram_w(m, pos, iram_ir(m, r_reg(m, (uint8_t)(opcode & 1))));
                break;
            }
            case 0x88:
            case 0x89:
            case 0x8a:
            case 0x8b:
            case 0x8c:
            case 0x8d:
            case 0x8e:
            case 0x8f: {  // mov mem,rn
                const uint8_t pos = fetch(m);
                iram_w(m, pos, r_reg(m, (uint8_t)(opcode & 7)));
                break;
            }
            case 0x90:  // mov dptr,#word
                m->sfr[kAddrDph] = fetch(m);
                m->sfr[kAddrDpl] = fetch(m);
                break;
            case 0x92: {  // mov bit,c
                m->rwm = true;
                const uint8_t pos = fetch(m);
                bit_address_w(m, pos, m->psw.c ? 1 : 0);
                m->rwm = false;
                break;
            }
            case 0x93:  // movc a,@a+dptr
                set_acc(m, peek(m, (uint16_t)(((m->sfr[kAddrDph] << 8) | m->sfr[kAddrDpl]) + acc(m))));
                break;
            case 0x94: {  // subb a,#byte
                const uint8_t data = fetch(m);
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) - data - carry);
                sub_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x95: {  // subb a,mem
                const uint8_t data = iram_r(m, fetch(m));
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) - data - carry);
                sub_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x96:
            case 0x97: {  // subb a,@ri
                const uint8_t data = iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)));
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) - data - carry);
                sub_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0x98:
            case 0x99:
            case 0x9a:
            case 0x9b:
            case 0x9c:
            case 0x9d:
            case 0x9e:
            case 0x9f: {  // subb a,rn
                const uint8_t data = r_reg(m, (uint8_t)(opcode & 7));
                const uint8_t carry = m->psw.c ? 1 : 0;
                const uint8_t result = (uint8_t)(acc(m) - data - carry);
                sub_flags(m, acc(m), data, carry);
                set_acc(m, result);
                break;
            }
            case 0xa0: {  // orl c,/bit
                const uint8_t pos = fetch(m);
                m->psw.c = m->psw.c || bit_address_r(m, pos) == 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xa2: {  // mov c,bit
                const uint8_t pos = fetch(m);
                m->psw.c = bit_address_r(m, pos) != 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xa3: {  // inc dptr
                const uint16_t dptr = (uint16_t)(((m->sfr[kAddrDph] << 8) | m->sfr[kAddrDpl]) + 1);
                m->sfr[kAddrDph] = (uint8_t)(dptr >> 8);
                m->sfr[kAddrDpl] = (uint8_t)(dptr & 0xff);
                break;
            }
            case 0xa4: {  // mul ab
                const uint16_t result = (uint16_t)(acc(m) * m->sfr[kAddrB]);
                m->sfr[kAddrB] = (uint8_t)(result >> 8);
                m->sfr[kAddrAcc] = (uint8_t)(result & 0xff);
                m->calc_parity = true;
                m->psw.o = (result & 0xff00) != 0;
                m->psw.c = false;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xa6:
            case 0xa7: {  // mov @ri,mem
                const uint8_t pos = fetch(m);
                iram_iw(m, r_reg(m, (uint8_t)(opcode & 1)), iram_r(m, pos));
                break;
            }
            case 0xa8:
            case 0xa9:
            case 0xaa:
            case 0xab:
            case 0xac:
            case 0xad:
            case 0xae:
            case 0xaf:  // mov rn,mem
                set_reg(m, (uint8_t)(opcode & 7), iram_r(m, fetch(m)));
                break;
            case 0xb0: {  // anl c,/bit
                const uint8_t pos = fetch(m);
                m->psw.c = m->psw.c && bit_address_r(m, pos) == 0;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xb2: {  // cpl bit
                m->rwm = true;
                const uint8_t pos = fetch(m);
                bit_address_w(m, pos, (uint8_t)(~bit_address_r(m, pos) & 1));
                m->rwm = false;
                break;
            }
            case 0xb3:  // cpl c
                m->psw.c = !m->psw.c;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            case 0xb4: {  // cjne a,#byte,rel
                const uint8_t data = fetch(m);
                const uint8_t offset = fetch(m);
                if (acc(m) != data) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                m->psw.c = acc(m) < data;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xb5: {  // cjne a,mem,rel
                const uint8_t data = iram_r(m, fetch(m));
                const uint8_t offset = fetch(m);
                if (acc(m) != data) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                m->psw.c = acc(m) < data;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xb6:
            case 0xb7: {  // cjne @ri,#byte,rel
                const uint8_t data = fetch(m);
                const uint8_t offset = fetch(m);
                const uint8_t value = iram_ir(m, r_reg(m, (uint8_t)(opcode & 1)));
                if (value != data) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                m->psw.c = value < data;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xb8:
            case 0xb9:
            case 0xba:
            case 0xbb:
            case 0xbc:
            case 0xbd:
            case 0xbe:
            case 0xbf: {  // cjne rn,#byte,rel
                const uint8_t data = fetch(m);
                const uint8_t offset = fetch(m);
                const uint8_t value = r_reg(m, (uint8_t)(opcode & 7));
                if (value != data) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                m->psw.c = value < data;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            }
            case 0xc0: {  // push
                const uint8_t pos = fetch(m);
                const uint8_t sp = (uint8_t)(m->sfr[kAddrSp] + 1);
                m->sfr[kAddrSp] = sp;
                iram_iw(m, sp, iram_r(m, pos));
                break;
            }
            case 0xc2: {  // clr bit
                const uint8_t pos = fetch(m);
                m->rwm = true;
                bit_address_w(m, pos, 0);
                m->rwm = false;
                break;
            }
            case 0xc3:  // clr c
                m->psw.c = false;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            case 0xc4:  // swap a
                set_acc(m, (uint8_t)(((acc(m) & 0x0f) << 4) | ((acc(m) & 0xf0) >> 4)));
                break;
            case 0xc5: {  // xch a,mem
                const uint8_t pos = fetch(m);
                const uint8_t value = iram_r(m, pos);
                const uint8_t old = acc(m);
                set_acc(m, value);
                iram_w(m, pos, old);
                break;
            }
            case 0xc6:
            case 0xc7: {  // xch a,@ri
                const uint8_t pos = r_reg(m, (uint8_t)(opcode & 1));
                const uint8_t value = iram_ir(m, pos);
                const uint8_t old = acc(m);
                set_acc(m, value);
                iram_iw(m, pos, old);
                break;
            }
            case 0xc8:
            case 0xc9:
            case 0xca:
            case 0xcb:
            case 0xcc:
            case 0xcd:
            case 0xce:
            case 0xcf: {  // xch a,rn
                const uint8_t value = r_reg(m, (uint8_t)(opcode & 7));
                const uint8_t old = acc(m);
                set_acc(m, value);
                set_reg(m, (uint8_t)(opcode & 7), old);
                break;
            }
            case 0xd0: {  // pop
                const uint8_t pos = fetch(m);
                iram_w(m, pos, iram_ir(m, m->sfr[kAddrSp]));
                m->sfr[kAddrSp] = (uint8_t)(m->sfr[kAddrSp] - 1);
                break;
            }
            case 0xd2: {  // setb bit
                const uint8_t pos = fetch(m);
                m->rwm = true;
                bit_address_w(m, pos, 1);
                m->rwm = false;
                break;
            }
            case 0xd3:  // setb c
                m->psw.c = true;
                m->sfr[kAddrPsw] = get_psw(m);
                break;
            case 0xd4: {  // da a
                uint16_t value = acc(m);
                if (m->psw.ac || (value & 0x0f) > 0x09) value = (uint16_t)(value + 0x06);
                if (m->psw.c || (value & 0xf0) > 0x90 || (value & 0xff00) != 0) {
                    value = (uint16_t)(value + 0x60);
                }
                set_acc(m, (uint8_t)(value & 0xff));
                if ((value & 0xff00) != 0) {
                    m->psw.c = true;
                    m->sfr[kAddrPsw] = get_psw(m);
                }
                break;
            }
            case 0xd5: {  // djnz mem,rel
                m->rwm = true;
                const uint8_t pos = fetch(m);
                const uint8_t offset = fetch(m);
                iram_w(m, pos, (uint8_t)(iram_r(m, pos) - 1));
                if (iram_r(m, pos) != 0) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                m->rwm = false;
                break;
            }
            case 0xd6:
            case 0xd7: {  // xchd a,@ri
                const uint8_t pos = r_reg(m, (uint8_t)(opcode & 1));
                const uint8_t value = iram_ir(m, pos);
                const uint8_t old = acc(m);
                set_acc(m, (uint8_t)((old & 0xf0) | (value & 0x0f)));
                iram_iw(m, pos, (uint8_t)((value & 0xf0) | (old & 0x0f)));
                break;
            }
            case 0xd8:
            case 0xd9:
            case 0xda:
            case 0xdb:
            case 0xdc:
            case 0xdd:
            case 0xde:
            case 0xdf: {  // djnz rn,rel
                const uint8_t offset = fetch(m);
                const uint8_t value = (uint8_t)(r_reg(m, (uint8_t)(opcode & 7)) - 1);
                set_reg(m, (uint8_t)(opcode & 7), value);
                if (value != 0) m->pc = (uint16_t)(m->pc + (int8_t)(offset));
                break;
            }
            case 0xe0: {  // movx a,@dptr
                const uint16_t address = (uint16_t)((m->sfr[kAddrDph] << 8) | m->sfr[kAddrDpl]);
                set_acc(m, m->read_external ? m->read_external(m->user, address) : 0xff);
                break;
            }
            case 0xe2:
            case 0xe3: {  // movx a,@ri
                const uint16_t address =
                    (uint16_t)((m->sfr[kAddrP2] << 8) | r_reg(m, (uint8_t)(opcode & 1)));
                set_acc(m, m->read_external ? m->read_external(m->user, address) : 0xff);
                break;
            }
            case 0xe4:  // clr a
                set_acc(m, 0);
                break;
            case 0xe5:  // mov a,mem
                set_acc(m, iram_r(m, fetch(m)));
                break;
            case 0xe6:
            case 0xe7:  // mov a,@ri
                set_acc(m, iram_ir(m, r_reg(m, (uint8_t)(opcode & 1))));
                break;
            case 0xe8:
            case 0xe9:
            case 0xea:
            case 0xeb:
            case 0xec:
            case 0xed:
            case 0xee:
            case 0xef:  // mov a,rn
                set_acc(m, r_reg(m, (uint8_t)(opcode & 7)));
                break;
            case 0xf0: {  // movx @dptr,a
                const uint16_t address = (uint16_t)((m->sfr[kAddrDph] << 8) | m->sfr[kAddrDpl]);
                if (m->write_external) m->write_external(m->user, address, acc(m));
                break;
            }
            case 0xf2:
            case 0xf3: {  // movx @ri,a
                const uint16_t address =
                    (uint16_t)((m->sfr[kAddrP2] << 8) | r_reg(m, (uint8_t)(opcode & 1)));
                if (m->write_external) m->write_external(m->user, address, acc(m));
                break;
            }
            case 0xf4:  // cpl a
                set_acc(m, (uint8_t)(~acc(m)));
                break;
            case 0xf5:  // mov mem,a
                iram_w(m, fetch(m), acc(m));
                break;
            case 0xf6:
            case 0xf7:  // mov @ri,a
                iram_iw(m, r_reg(m, (uint8_t)(opcode & 1)), acc(m));
                break;
            default:  // 0xf8-0xff: mov rn,a
                set_reg(m, (uint8_t)(opcode & 7), acc(m));
                break;
        }
        const int spent = kCycles[opcode] + irq_cycles;
        executed += spent;
        update_timer_t0(m, spent);
        update_timer_t1(m, spent);
    }
    return executed;
}


void mcs51_init(mcs51 *m)
{
    memset(m, 0, sizeof *m);
    m->num_interrupts = 5;
    m->cur_irq_prio = -1;
}
