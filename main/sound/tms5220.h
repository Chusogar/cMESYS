// tms5220.h - TI TMS5220 LPC speech synthesizer (speak-external / FIFO only).
// C port of dsp-cpp's sound/tms5220.
#ifndef TMS5220_H
#define TMS5220_H

#include <stdint.h>
#include <stdbool.h>

#define TMS5220_NUM_K 10
#define TMS5220_FIFO 16

typedef struct tms5220 {
    uint32_t clock;
    bool rate_control;   // TMS5220C

    uint8_t fifo[TMS5220_FIFO];
    int fifo_head, fifo_tail, fifo_count, fifo_bits_taken;
    uint8_t write_latch;
    bool data_latched;
    uint8_t rs_ws;
    bool io_ready;
    int io_ready_delay;
    bool irq_pin;

    bool spen, ddis, talk, talkd, previous_talk_status, buffer_low, buffer_empty;
    uint8_t c_variant_rate;

    int new_energy_idx, new_pitch_idx;
    int new_k_idx[TMS5220_NUM_K];
    int current_energy, previous_energy, current_pitch;
    int current_k[TMS5220_NUM_K];
    bool zpar, uv_zpar, olde, oldp, inhibit, pitch_zero;

    int subcycle, pc, ip, pitch_count, excitation_data;
    uint16_t rng;
    int32_t u[TMS5220_NUM_K + 1];
    int32_t x[TMS5220_NUM_K];

    int32_t out_sample;
    int64_t cycle_acc;
} tms5220;

void tms5220_init(tms5220 *t, uint32_t clock, bool tms5220c);
void tms5220_reset(tms5220 *t);
void tms5220_write_data(tms5220 *t, uint8_t value);
uint8_t tms5220_status(const tms5220 *t);
void tms5220_set_wsq(tms5220 *t, bool level);
void tms5220_set_rsq(tms5220 *t, bool level);
bool tms5220_readyq(const tms5220 *t);
static inline bool tms5220_intq(const tms5220 *t) { return !t->irq_pin; }
static inline void tms5220_set_data_latch(tms5220 *t, uint8_t v) { t->write_latch = v; t->data_latched = true; }
static inline void tms5220_set_clock(tms5220 *t, uint32_t clock) { t->clock = clock ? clock : 1; }
void tms5220_tick(tms5220 *t, int cycles);
static inline int16_t tms5220_last_sample(const tms5220 *t) { return (int16_t)t->out_sample; }

#endif
