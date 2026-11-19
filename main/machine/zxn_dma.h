// zxn_dma.h - the ZX Spectrum Next's DMA (zxnDMA), C port of dsp-cpp's
// machine/zxn_dma: a Z80-DMA register set (WR0-WR6, the read mask / status
// sequence) doing memory / I/O block transfers. Port $6B is the zxnDMA mode
// (exactly `length` bytes), $0B the Zilog compatible one (length + 1).
// Transfers run in bursts that take the bus from the CPU; with a port B
// prescaler a byte is moved every `prescaler` ticks of the 875 kHz
// reference (the Next's sampled sound through its DACs).
#ifndef ZXN_DMA_H
#define ZXN_DMA_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t (*mem_read)(void *user, uint16_t a);
    void (*mem_write)(void *user, uint16_t a, uint8_t v);
    uint8_t (*io_read)(void *user, uint16_t a);
    void (*io_write)(void *user, uint16_t a, uint8_t v);
    void *user;
    int ticks_per_t;                    // 28 MHz ticks a CPU T-state (8 at 3.5 MHz)

    uint8_t wr0, wr1, wr2, wr3, wr4, wr5;
    uint16_t port_a, port_b, length;
    uint8_t timing_a, timing_b, prescaler, read_mask;
    uint8_t follow[8];
    int follow_count, follow_pos;
    bool enabled, z80_mode;
    uint16_t addr_a, addr_b, counter;
    bool end_of_block;
    int prescaler_wait;
    uint8_t read_seq[7];
    int read_count, read_pos;
    uint8_t status;
} zxn_dma;

void zxn_dma_reset(zxn_dma *d);
void zxn_dma_write(zxn_dma *d, uint8_t value, bool z80_mode);
uint8_t zxn_dma_read(zxn_dma *d);
static inline bool zxn_dma_active(const zxn_dma *d) { return d->enabled; }
// Runs the transfer for up to `budget` 28 MHz ticks; returns those it used
// (the CPU stalled meanwhile)
int zxn_dma_run(zxn_dma *d, int budget);
static inline void zxn_dma_advance(zxn_dma *d, int ticks) { if (d->prescaler_wait > 0) d->prescaler_wait -= ticks; }
#endif
