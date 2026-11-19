// zxn_dma.c - the ZX Spectrum Next's zxnDMA (see zxn_dma.h)
#include "zxn_dma.h"

enum { P_PORTA_LO, P_PORTA_HI, P_LEN_LO, P_LEN_HI, P_TIMING_A, P_TIMING_B, P_PRESCALER, P_READ_MASK, P_IGNORE,
       P_PORTB_LO, P_PORTB_HI };

void zxn_dma_reset(zxn_dma *d)
{
    d->wr0 = d->wr1 = d->wr2 = d->wr3 = d->wr4 = d->wr5 = 0;
    d->port_a = d->port_b = d->length = 0;
    d->timing_a = d->timing_b = 1;
    d->prescaler = 0;
    d->read_mask = 0x7f;
    d->follow_count = d->follow_pos = 0;
    d->enabled = false;
    d->addr_a = d->addr_b = 0;
    d->counter = 0;
    d->end_of_block = false;
    d->prescaler_wait = 0;
    d->read_count = d->read_pos = 0;
    d->status = 0x3a;
    if (!d->ticks_per_t) d->ticks_per_t = 8;
}

static void add(zxn_dma *d, uint8_t p) { d->follow[d->follow_count++] = p; }

static void load(zxn_dma *d)
{
    d->addr_a = d->port_a;
    d->addr_b = d->port_b;
    d->counter = 0;
    d->end_of_block = false;
}

static uint8_t status_byte(const zxn_dma *d) { return (uint8_t)(0x1a | (d->end_of_block ? 0 : 0x20) | (d->counter ? 1 : 0)); }

static void command(zxn_dma *d, uint8_t value)
{
    switch (value) {
    case 0xc3:                          // reset
        d->enabled = false;
        d->wr5 = 0;
        d->timing_a = d->timing_b = 1;
        d->prescaler = 0;
        d->read_mask = 0x7f;
        d->status = 0x3a;
        break;
    case 0xc7: d->timing_a = 1; break;                       // reset port A timing
    case 0xcb: d->timing_b = 1; d->prescaler = 0; break;     // reset port B timing
    case 0xcf: load(d); break;
    case 0xd3:                          // continue
        d->counter = 0;
        d->end_of_block = false;
        break;
    case 0x87:                          // enable DMA
        d->enabled = true;
        d->counter = 0;
        d->end_of_block = false;
        d->prescaler_wait = 0;
        break;
    case 0x83: d->enabled = false; break;                    // disable DMA
    case 0xbb: add(d, P_READ_MASK); break;
    case 0xbf:                          // read status byte
        d->read_seq[0] = status_byte(d);
        d->read_count = 1;
        d->read_pos = 0;
        break;
    case 0x8b: d->end_of_block = false; break;               // reinitialize status byte
    case 0xa7: {                        // initialize read sequence
        d->read_count = d->read_pos = 0;
        const uint8_t vals[7] = {status_byte(d), (uint8_t)d->counter, (uint8_t)(d->counter >> 8), (uint8_t)d->addr_a,
                                 (uint8_t)(d->addr_a >> 8), (uint8_t)d->addr_b, (uint8_t)(d->addr_b >> 8)};
        for (int i = 0; i < 7; ++i)
            if (d->read_mask & (1 << i)) d->read_seq[d->read_count++] = vals[i];
        break;
    }
    default:
        break;                          // interrupt and ready commands: not used on the Next
    }
}

void zxn_dma_write(zxn_dma *d, uint8_t value, bool z80_mode)
{
    d->z80_mode = z80_mode;
    if (d->follow_pos < d->follow_count) {
        switch (d->follow[d->follow_pos++]) {
        case P_PORTA_LO: d->port_a = (uint16_t)((d->port_a & 0xff00) | value); break;
        case P_PORTA_HI: d->port_a = (uint16_t)((d->port_a & 0x00ff) | (value << 8)); break;
        case P_LEN_LO: d->length = (uint16_t)((d->length & 0xff00) | value); break;
        case P_LEN_HI: d->length = (uint16_t)((d->length & 0x00ff) | (value << 8)); break;
        case P_TIMING_A: d->timing_a = value; break;
        case P_TIMING_B:
            d->timing_b = value;
            if (value & 0x20) add(d, P_PRESCALER);
            break;
        case P_PRESCALER: d->prescaler = value; break;
        case P_READ_MASK: d->read_mask = value & 0x7f; break;
        case P_PORTB_LO: d->port_b = (uint16_t)((d->port_b & 0xff00) | value); break;
        case P_PORTB_HI: d->port_b = (uint16_t)((d->port_b & 0x00ff) | (value << 8)); break;
        default: break;
        }
        return;
    }
    d->follow_count = d->follow_pos = 0;
    if ((value & 0x80) == 0) {
        if ((value & 0x03) != 0) {      // WR0 (any: a transfer, no search modes)
            d->wr0 = value;
            if (value & 0x08) add(d, P_PORTA_LO);
            if (value & 0x10) add(d, P_PORTA_HI);
            if (value & 0x20) add(d, P_LEN_LO);
            if (value & 0x40) add(d, P_LEN_HI);
        } else if ((value & 0x07) == 0x04) {     // WR1
            d->wr1 = value;
            if (value & 0x40) add(d, P_TIMING_A);
        } else if ((value & 0x07) == 0x00) {     // WR2
            d->wr2 = value;
            if (value & 0x40) add(d, P_TIMING_B);
        }
        return;
    }
    switch (value & 0x83) {
    case 0x80:                          // WR3
        d->wr3 = value;
        if (value & 0x08) add(d, P_IGNORE);      // mask byte
        if (value & 0x10) add(d, P_IGNORE);      // match byte
        if (value & 0x40) d->enabled = true;
        break;
    case 0x81:                          // WR4
        d->wr4 = value;
        if (value & 0x04) add(d, P_PORTB_LO);
        if (value & 0x08) add(d, P_PORTB_HI);
        if (value & 0x10) add(d, P_IGNORE);      // interrupt control
        break;
    case 0x82:                          // WR5
        if ((value & 0x07) == 0x02) d->wr5 = value;
        break;
    case 0x83:                          // WR6
        command(d, value);
        break;
    }
}

uint8_t zxn_dma_read(zxn_dma *d)
{
    if (d->read_count == 0) {
        command(d, 0xa7);
        if (d->read_count == 0) return 0;
    }
    const uint8_t v = d->read_seq[d->read_pos];
    d->read_pos = (d->read_pos + 1) % d->read_count;
    return v;
}

static void step(uint16_t *addr, uint8_t wr)
{
    switch ((wr >> 4) & 3) {
    case 0: *addr = (uint16_t)(*addr - 1); break;
    case 1: *addr = (uint16_t)(*addr + 1); break;
    default: break;
    }
}

static void transfer_byte(zxn_dma *d)
{
    const bool a_to_b = (d->wr0 & 0x04) != 0;
    const bool a_io = (d->wr1 & 0x08) != 0, b_io = (d->wr2 & 0x08) != 0;
    const uint16_t src = a_to_b ? d->addr_a : d->addr_b, dst = a_to_b ? d->addr_b : d->addr_a;
    const bool src_io = a_to_b ? a_io : b_io, dst_io = a_to_b ? b_io : a_io;
    const uint8_t v = src_io ? d->io_read(d->user, src) : d->mem_read(d->user, src);
    if (dst_io) d->io_write(d->user, dst, v);
    else d->mem_write(d->user, dst, v);
    step(&d->addr_a, d->wr1);
    step(&d->addr_b, d->wr2);
    d->counter++;
}

static void finish(zxn_dma *d)
{
    d->end_of_block = true;
    if (d->wr5 & 0x20) {                // auto restart
        load(d);
        return;
    }
    d->enabled = false;
}

static int cycle_len(uint8_t timing)
{
    switch (timing & 3) {
    case 0: return 4;
    case 1: return 3;
    case 2: return 2;
    default: return 4;
    }
}

int zxn_dma_run(zxn_dma *d, int budget)
{
    if (!d->enabled) return 0;
    const uint32_t total = d->z80_mode ? (uint32_t)d->length + 1 : (uint32_t)d->length;
    if (total == 0) {
        finish(d);
        return 0;
    }
    const int mode = (d->wr4 >> 5) & 3;     // 0 byte, 1 continuous, 2 burst
    const int ticks_per_byte = (cycle_len(d->timing_a) + cycle_len(d->timing_b)) * d->ticks_per_t;
    int used = 0;
    while (d->enabled && used < budget) {
        if (d->prescaler && d->prescaler_wait > 0) {
            if (mode == 1) {                // continuous: the bus stays held while waiting
                const int w = d->prescaler_wait < budget - used ? d->prescaler_wait : budget - used;
                d->prescaler_wait -= w;
                used += w;
                continue;
            }
            break;                          // burst: the CPU runs until the next sample is due
        }
        transfer_byte(d);
        used += ticks_per_byte;
        if (d->prescaler) d->prescaler_wait += (int)d->prescaler * 32;
        if (d->counter >= total) finish(d);
        if (mode == 0) break;               // byte mode: one byte a bus request
    }
    return used;
}
