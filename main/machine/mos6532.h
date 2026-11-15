// mos6532.h - MOS 6532 RIOT (128 bytes of RAM, two ports, interval timer,
// PA7 edge interrupt): a C11 migration of dsp-cpp's machine/mos6532.
#ifndef MOS6532_H
#define MOS6532_H

#include <stdbool.h>
#include <stdint.h>

typedef struct mos6532 {
    uint8_t (*pa_in)(void *user);               // optional (NULL: the pins' levels)
    uint8_t (*pb_in)(void *user);
    void (*pa_out)(void *user, uint8_t v);      // optional
    void (*pb_out)(void *user, uint8_t v);
    void (*irq)(void *user, bool on);           // optional
    void *user;

    uint8_t ram[0x80];
    uint8_t pa_pins, pa_out_reg, pa_ddr;
    uint8_t pb_pins, pb_out_reg, pb_ddr;
    int timer_count, timer_shift;
    bool timer_counting, ie_timer, irq_timer;
    bool pa7_level, pa7_positive, pa7_ie, pa7_flag;
} mos6532;

void mos6532_reset(mos6532 *r);                 // (keeps the callbacks)
static inline uint8_t mos6532_ram_read(const mos6532 *r, uint8_t offset) { return r->ram[offset & 0x7f]; }
static inline void mos6532_ram_write(mos6532 *r, uint8_t offset, uint8_t v) { r->ram[offset & 0x7f] = v; }
uint8_t mos6532_io_read(mos6532 *r, uint8_t offset);
void mos6532_io_write(mos6532 *r, uint8_t offset, uint8_t value);
void mos6532_tick(mos6532 *r, int cycles);
void mos6532_set_pa_in_bit(mos6532 *r, int bit, bool level);
void mos6532_set_pa7(mos6532 *r, bool level);

#endif
