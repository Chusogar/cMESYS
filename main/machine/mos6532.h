// mos6532.h - MOS 6532 RIOT (128 bytes of RAM, two ports, the timer): a C11
// port of dsp-cpp's machine/mos6532. The Atari 2600 and 7800 use it.
#ifndef MOS6532_H
#define MOS6532_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t (*pa_in)(void *user);       // (NULL: pa_in_v)
    uint8_t (*pb_in)(void *user);
    void (*pa_out)(void *user, uint8_t v);
    void (*pb_out)(void *user, uint8_t v);
    void (*irq)(void *user, bool on);
    void *user;
    uint8_t ram[0x80];
    uint8_t pa_in_v, pa_out_v, pa_ddr, pb_in_v, pb_out_v, pb_ddr;
    int timer_count, timer_shift;
    bool timer_counting, ie_timer, irq_timer;
    bool pa7_level, pa7_positive, pa7_ie, pa7_flag;
} mos6532;

void mos6532_reset(mos6532 *r);
static inline uint8_t mos6532_ram_read(const mos6532 *r, uint8_t o) { return r->ram[o & 0x7f]; }
static inline void mos6532_ram_write(mos6532 *r, uint8_t o, uint8_t v) { r->ram[o & 0x7f] = v; }
uint8_t mos6532_io_read(mos6532 *r, uint8_t offset);
void mos6532_io_write(mos6532 *r, uint8_t offset, uint8_t value);
void mos6532_tick(mos6532 *r, int cycles);
void mos6532_set_pa7(mos6532 *r, bool level);
#endif
