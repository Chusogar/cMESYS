// starwars_math.h - the Star Wars matrix processor and divider: C port of
// dsp-cpp's machine/starwars_math (from MAME's starwars_m.cpp).
#ifndef STARWARS_MATH_H
#define STARWARS_MATH_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t ram[0x1000];
    uint8_t prom_str[1024], prom_mas[1024], prom_am[1024];
    int mpa, bic;
    int16_t a, b, c;
    int32_t acc;
    uint16_t divisor, dividend, dvd_shift, quotient_shift;
    int run_cycles;
} sw_math;

void sw_math_init(sw_math *m, const uint8_t *prom4k);
void sw_math_reset(sw_math *m);
void sw_math_write(sw_math *m, uint8_t offset, uint8_t data);
static inline uint8_t sw_math_div_reh(const sw_math *m) { return (uint8_t)(m->quotient_shift >> 8); }
static inline uint8_t sw_math_div_rel(const sw_math *m) { return (uint8_t)m->quotient_shift; }
static inline bool sw_math_running(const sw_math *m) { return m->run_cycles > 0; }
static inline void sw_math_tick(sw_math *m, int cycles)
{
    if (m->run_cycles > 0) {
        m->run_cycles -= cycles;
        if (m->run_cycles < 0) m->run_cycles = 0;
    }
}
#endif
