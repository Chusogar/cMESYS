// rf5c68d.h - Ricoh RF5C68 PCM (eight channels of sign / magnitude samples in
// 64 KB of RAM): a C port of dsp-cpp's sound/rf5c68 (after MAME's).
#ifndef RF5C68D_H
#define RF5C68D_H
#include <stdint.h>

typedef struct {
    struct { uint8_t enable, env, pan, start; uint32_t addr; uint16_t step, loopst; } chan[8];
    uint8_t ram[0x10000];
    uint8_t cbank, enable;
    uint16_t wbank;
    float amplitude;
    double sample_pos, sample_step;
    int32_t last_left, last_right;
} rf5c68d;

void rf5c68d_init(rf5c68d *r, uint32_t clock, float amplitude, int rate);
void rf5c68d_reset(rf5c68d *r);
void rf5c68d_write_reg(rf5c68d *r, uint8_t offset, uint8_t data);
static inline uint8_t rf5c68d_read_mem(const rf5c68d *r, uint16_t o) { return r->ram[(r->wbank | (o & 0x0fff)) & 0xffff]; }
static inline void rf5c68d_write_mem(rf5c68d *r, uint16_t o, uint8_t d) { r->ram[(r->wbank | (o & 0x0fff)) & 0xffff] = d; }
int32_t rf5c68d_update(rf5c68d *r);             // one host sample (mono)

#endif
