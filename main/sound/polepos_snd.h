// polepos_snd.h - Pole Position sound: the Namco 8-voice WSG (with the
// 52xx / 54xx DACs muxed into voices) and the engine sample player with its
// three analog filters (MAME's audio/polepos.cpp). C port of dsp-cpp's
// sound/polepos_wsg and sound/polepos_engine; the filters in float.
#ifndef POLEPOS_SND_H
#define POLEPOS_SND_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t frequency, counter;
    int volume;                 // the four gains summed
    uint8_t waveform_select, ext_sel;
    bool external;
} pp_wsg_voice;

typedef struct {
    const uint8_t *wave;        // the 256-byte wavetable PROM
    uint32_t step;              // counter step per output sample, << 8
    int fracbits;
    bool enabled;
    uint8_t regs[0x40];
    pp_wsg_voice v[8];
} pp_wsg;

void pp_wsg_init(pp_wsg *w, uint32_t clock, uint32_t sample_rate, const uint8_t *wavetable);
void pp_wsg_reset(pp_wsg *w);
static inline uint8_t pp_wsg_read(const pp_wsg *w, uint8_t off) { return w->regs[off & 0x3f]; }
void pp_wsg_write(pp_wsg *w, uint8_t off, uint8_t data);
// One output sample; the DAC nibbles (0-15): 54xx 1-3, 52xx.
int32_t pp_wsg_update(pp_wsg *w, int chanl1, int chanl2, int chanl3, int chanl4);

typedef struct { float x0, x1, x2, y0, y1, y2, a1, a2, b0, b1, b2; } pp_filter2;

typedef struct {
    uint32_t clock, sample_rate, position;
    int sample_msb, sample_lsb;
    bool sample_enable;
    const uint8_t *samples;     // 16 KB of engine samples
    uint32_t samples_size;
    pp_filter2 f[3];
} pp_engine;

void pp_engine_init(pp_engine *e, uint32_t clock, uint32_t sample_rate, const uint8_t *samples, uint32_t size);
void pp_engine_reset(pp_engine *e);
void pp_engine_clson(pp_engine *e, bool enabled);
void pp_engine_lsb_w(pp_engine *e, uint8_t data);
void pp_engine_msb_w(pp_engine *e, uint8_t data);
int32_t pp_engine_update(pp_engine *e);

#endif
