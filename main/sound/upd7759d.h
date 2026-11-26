// upd7759d.h - NEC uPD7759 ADPCM speech: a C port of dsp-cpp's
// sound/upd7759 (upd7759.pas). Slave mode (rom NULL: the data comes
// through the FIFO, DRQ asks for it) or a ROM of its own.
#ifndef UPD7759D_H
#define UPD7759D_H
#include <stdint.h>

#define UPD7759_CLOCK 640000

typedef struct {
    const uint8_t *rom;
    uint32_t rom_size;
    void (*drq_cb)(void *user, uint8_t drq);
    void *user;
    float amp, resample_pos, resample_inc;
    uint32_t pos, step, offset, repeat_offset;
    uint8_t fifo_in, reset_pin, start, drq, repeat_count, req_sample, last_sample, block_header, sample_rate,
        first_valid_header, adpcm_data;
    int state, clocks_left, post_drq_state, post_drq_clocks, adpcm_state, sample;
    uint16_t nibbles_left;
} upd7759d;

void upd7759d_init(upd7759d *u, float amp, uint32_t sample_rate);   // (clears the ROM and the DRQ handler)
void upd7759d_reset(upd7759d *u);
void upd7759d_start_w(upd7759d *u, uint8_t data);
void upd7759d_reset_w(upd7759d *u, uint8_t data);
void upd7759d_port_w(upd7759d *u, uint8_t data);
uint8_t upd7759d_busy_r(const upd7759d *u);
int32_t upd7759d_update(upd7759d *u);           // one host sample

#endif
