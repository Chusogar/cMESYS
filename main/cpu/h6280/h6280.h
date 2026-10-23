// h6280.h - Hudson HuC6280 (65C02 core + MMU, timer, interrupt controller).
// C port of MAME's cpu/h6280 (cycle counts, the T flag, block transfers, the
// one-instruction delay before an interrupt is taken, low/high speed mode,
// the timer at $1FEC00 and the interrupt registers at $1FF400). The internal
// PSG is not emulated (Data East uses the chip as a protection / sound CPU
// with the PSG output unrouted).
//
// The logical 16-bit space is mapped through eight 8 KB MPRs to a 21-bit
// physical space: memory handlers see physical addresses. Direct 8 KB banks
// can be set for plain RAM / ROM (physical bank = address >> 13).
#ifndef H6280_H
#define H6280_H

#include <stdint.h>
#include <stdbool.h>

enum { H6280_CLEAR = 0, H6280_ASSERT = 1 };
enum { H6280_IRQ1 = 0, H6280_IRQ2 = 1, H6280_IRQT = 2, H6280_NMI = 3 };

typedef struct h6280 {
    uint8_t (*read)(void *user, uint32_t addr);         // 21-bit physical address
    void (*write)(void *user, uint32_t addr, uint8_t value);
    void *user;
    const uint8_t *bank_r[256];                         // direct 8 KB banks (NULL: handler)
    uint8_t *bank_w[256];

    uint16_t pc, ppc;
    uint8_t a, x, y, p, s;
    uint16_t ea;                                        // effective address (logical)
    uint8_t zpl;                                        // zero page offset
    uint8_t mmr[8];

    int32_t icount;                                     // clocks left in this slice
    uint8_t clocks_per_cycle;                           // 4 low speed, 1 high speed

    uint8_t irq_mask;
    uint8_t irq_state[3];                               // IRQ1, IRQ2, timer
    uint8_t nmi_state;
    uint8_t irq_pending;                                // 2: after the next instruction, 1: now
    uint8_t io_buffer;

    uint8_t timer_status;
    int32_t timer_value, timer_load;

    uint64_t total;                                     // clocks run since reset
} h6280;

void h6280_init(h6280 *c);
void h6280_reset(h6280 *c);
// Runs for `clocks` clocks of the device clock (plus what the last
// instruction overshoots, carried to the next call). Returns the clocks run.
int h6280_run(h6280 *c, int clocks);
void h6280_set_irq_line(h6280 *c, int line, int state);
static inline void h6280_set_bank(h6280 *c, uint8_t bank, const uint8_t *r, uint8_t *w)
{
    c->bank_r[bank] = r;
    c->bank_w[bank] = w;
}

#endif
