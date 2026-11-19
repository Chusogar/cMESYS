// es5503.h - Ensoniq ES5503 "DOC" (the Apple IIGS's sound): a C11 migration
// of dsp-cpp's sound/es5503. 32 wavetable oscillators reading 8-bit samples
// from its own 64 KB of RAM. Registers:
//   $00-$1F frequency low       $20-$3F frequency high
//   $40-$5F volume              $60-$7F last sample read (data)
//   $80-$9F wavetable pointer   $A0-$BF control (halt, mode, IE, channel)
//   $C0-$DF bank / table size / resolution
//   $E0 interrupt status        $E1 oscillators enabled   $E2 A/D converter
// A sample of $00 stops the oscillator. Modes: 0 free run, 1 one shot,
// 2 sync / AM, 3 swap (starts the partner when this one halts).
#ifndef ES5503_H
#define ES5503_H
#include <stdbool.h>
#include <stdint.h>

#define ES5503_OSCS 32
#define ES5503_RAM 0x10000

typedef struct {
    uint16_t freq, wtsize;
    uint8_t control, vol, data;
    uint32_t wavetblpointer;
    uint8_t wavetblsize, resolution;
    uint32_t accumulator;
    bool irqpend;
} es5503_osc;

typedef struct {
    uint8_t *ram;                       // ES5503_RAM bytes (the owner's)
    es5503_osc osc[ES5503_OSCS];
    int oscs_enabled;
    uint8_t rege0;
    bool irq_line;
    uint32_t clock;
    void (*irq)(void *user, bool on);
    void *user;
} es5503;

void es5503_init(es5503 *d, uint32_t clock, uint8_t *ram);
void es5503_reset(es5503 *d);
uint8_t es5503_read(es5503 *d, uint8_t reg);
void es5503_write(es5503 *d, uint8_t reg, uint8_t value);
// One output sample (all the enabled oscillators, mono)
int32_t es5503_generate(es5503 *d);
// Output samples a second with the oscillators enabled now
static inline double es5503_rate(const es5503 *d) { return (double)d->clock / 8.0 / (double)(d->oscs_enabled + 2); }
#endif
