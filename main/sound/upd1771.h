// upd1771.h - NEC uPD1771C (the Super Cassette Vision's sound): a C11 port
// of dsp-cpp's sound/upd1771 (from upd1771.pas): the tone, noise and ADPCM
// packets, the ACK back to the CPU, 44100 Hz samples for the CPU time run.
#ifndef UPD1771_H
#define UPD1771_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void (*ack)(void *user, bool asserted);
    void *user;
    uint32_t clock;
    float amp;
    uint8_t state;
    uint32_t index;
    uint8_t pc3;
    uint8_t packet[0x100];
    uint8_t t_timbre, t_offset, t_volume, t_tpos;
    uint16_t t_period, t_ppos;
    uint8_t nw_timbre, nw_volume, nw_tpos;
    uint32_t nw_period, nw_ppos;
    uint8_t n_value[3];
    uint32_t n_ppos[3], n_period[3];
    uint16_t n_volume[3];
    int16_t out;
    int ack_timer;
    double sample_acc, chip_phase;
} upd1771;

void upd1771_init(upd1771 *u, uint32_t clock, float amp);
void upd1771_reset(upd1771 *u);
void upd1771_write(upd1771 *u, uint8_t value);
void upd1771_pcm_write(upd1771 *u, uint8_t pc3);
// runs the chip for cpu_cycles of the CPU; its samples into out[*n] (up to max)
void upd1771_run(upd1771 *u, int cpu_cycles, uint32_t cpu_clock, int16_t *out, int *n, int max);
#endif
