// polepos_snd.c - Pole Position sound. See polepos_snd.h.
#include "polepos_snd.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// WSG
// ---------------------------------------------------------------------------
void pp_wsg_init(pp_wsg *w, uint32_t clock, uint32_t sample_rate, const uint8_t *wavetable)
{
    memset(w, 0, sizeof *w);
    w->wave = wavetable;
    int32_t nc = (int32_t)clock;
    w->fracbits = 15;
    while (nc < 192000) {
        nc *= 2;
        w->fracbits++;
    }
    w->step = (uint32_t)(((uint64_t)nc << 8) / sample_rate);
}

void pp_wsg_reset(pp_wsg *w)
{
    memset(w->regs, 0, sizeof w->regs);
    memset(w->v, 0, sizeof w->v);
}

static void recompute(pp_wsg *w, int ch)
{
    pp_wsg_voice *v = &w->v[ch];
    v->frequency = (uint32_t)w->regs[ch * 4] | ((uint32_t)w->regs[ch * 4 + 1] << 8);
    uint8_t sel = w->regs[ch * 4 + 0x23];
    v->waveform_select = (uint8_t)(sel & 7);
    v->external = (sel & 8) != 0;
    v->ext_sel = (uint8_t)(sel & 15);
    v->volume = (w->regs[ch * 4 + 3] >> 4) + (w->regs[ch * 4 + 3] & 15) + (sel >> 4) + (w->regs[ch * 4 + 2] >> 4);
}

void pp_wsg_write(pp_wsg *w, uint8_t off, uint8_t data)
{
    off &= 0x3f;
    if (w->regs[off] == data) return;
    w->regs[off] = data;
    recompute(w, (off & 0x1f) >> 2);
}

int32_t pp_wsg_update(pp_wsg *w, int c1, int c2, int c3, int c4)
{
    if (!w->enabled) return 0;
    // The 54xx / 52xx outputs are 4-bit DACs that rest at 0 (MAME clamps the
    // discrete nodes at 0), so they are not centred like the wavetable.
    const int ext[4] = { c1, c2, c3, c4 };
    int32_t mix = 0;
    for (int i = 0; i < 8; i++) {
        pp_wsg_voice *v = &w->v[i];
        v->counter += (uint32_t)(((uint64_t)v->frequency * w->step) >> 8);
        if (!v->volume) continue;
        int s = 0;
        if (v->external) {
            if (v->ext_sel >= 8 && v->ext_sel <= 11) s = ext[v->ext_sel - 8];
        } else {
            uint8_t pos = (uint8_t)((v->counter >> w->fracbits) & 0x1f);
            s = (int)(w->wave[((v->waveform_select << 5) + pos) & 0xff] & 15) - 8;
        }
        mix += s * v->volume;
    }
    return mix;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
#define R166 1000.0
#define R167 2200.0
#define R168 4700.0
#define RSHUNT (1.0 / (1.0 / R166 + 1.0 / 250.0))
static const float volume_table[8] = {
    (float)((RSHUNT + RSHUNT + RSHUNT + 2200) / 10000), (float)((RSHUNT + RSHUNT + R166 + 2200) / 10000),
    (float)((RSHUNT + R167 + RSHUNT + 2200) / 10000),   (float)((RSHUNT + R167 + R166 + 2200) / 10000),
    (float)((R168 + RSHUNT + RSHUNT + 2200) / 10000),   (float)((R168 + RSHUNT + R166 + 2200) / 10000),
    (float)((R168 + R167 + RSHUNT + 2200) / 10000),     (float)((R168 + R167 + R166 + 2200) / 10000),
};
static const float rfilt_out[3] = { 4700.0f, 7500.0f, 10000.0f };
#define RFILT_TOTAL (1.0 / (1.0 / 4700.0 + 1.0 / 7500.0 + 1.0 / 10000.0))
enum { LOWPASS, HIGHPASS, BANDPASS };

static void filter_setup(pp_filter2 *f, double sr, int type, double fc, double d, double gain)
{
    double tt = 2 * sr, tt2 = tt * tt;
    double w = sr * 2.0 * tan(M_PI * fc / sr), w2 = w * w;
    double den = tt2 + d * w * tt + w2;
    double a1 = 2.0 * (-tt2 + w2) / den, a2 = (tt2 - d * w * tt + w2) / den, b0, b1, b2;
    if (type == LOWPASS) { b0 = b2 = w2 / den; b1 = 2.0 * b0; }
    else if (type == BANDPASS) { b0 = d * w * tt / den; b1 = 0.0; b2 = -b0; }
    else { b0 = b2 = tt2 / den; b1 = -2.0 * b0; }
    memset(f, 0, sizeof *f);
    f->a1 = (float)a1; f->a2 = (float)a2;
    f->b0 = (float)(b0 * gain); f->b1 = (float)(b1 * gain); f->b2 = (float)(b2 * gain);
}

static void opamp_m_bandpass(pp_filter2 *f, double sr, double r1, double r2, double r3, double c1, double c2)
{
    double r_in, gain;
    if (r2 == 0) { gain = 1; r_in = r1; }
    else { gain = r2 / (r1 + r2); r_in = 1.0 / (1.0 / r1 + 1.0 / r2); }
    double fc = 1.0 / (2 * M_PI * sqrt(r_in * r3 * c1 * c2));
    double d = (c1 + c2) / sqrt(r3 / r_in * c1 * c2);
    gain *= -r3 / r_in * c2 / (c1 + c2);
    filter_setup(f, sr, BANDPASS, fc, d, gain);
}

void pp_engine_init(pp_engine *e, uint32_t clock, uint32_t sample_rate, const uint8_t *samples, uint32_t size)
{
    memset(e, 0, sizeof *e);
    e->clock = clock;
    e->sample_rate = sample_rate;
    e->samples = samples;
    e->samples_size = size;
    opamp_m_bandpass(&e->f[0], sample_rate, 220e3, 33e3, 390e3, 0.01e-6, 0.01e-6);
    opamp_m_bandpass(&e->f[1], sample_rate, 150e3, 22e3, 330e3, 0.0047e-6, 0.0047e-6);
    filter_setup(&e->f[2], sample_rate, HIGHPASS, 950, 1.0 / 0.707, 1);
    pp_engine_reset(e);
}

void pp_engine_reset(pp_engine *e)
{
    e->position = 0;
    e->sample_msb = e->sample_lsb = 0;
    e->sample_enable = false;
    for (int i = 0; i < 3; i++) e->f[i].x0 = e->f[i].x1 = e->f[i].x2 = e->f[i].y0 = e->f[i].y1 = e->f[i].y2 = 0;
}

void pp_engine_lsb_w(pp_engine *e, uint8_t d) { e->sample_lsb = d & 62; e->sample_enable = (d & 1) != 0; }
void pp_engine_msb_w(pp_engine *e, uint8_t d) { e->sample_msb = d & 63; }
void pp_engine_clson(pp_engine *e, bool en) { if (!en) { pp_engine_lsb_w(e, 0); pp_engine_msb_w(e, 0); } }

int32_t pp_engine_update(pp_engine *e)
{
    if (!e->sample_enable || !e->samples) return 0;
    uint32_t clock = (uint32_t)(((uint64_t)(e->clock / 16) * (uint64_t)((e->sample_msb + 1) * 64 + e->sample_lsb + 1)) / (64u * 64u));
    uint32_t step = (uint32_t)(((uint64_t)clock << 12) / e->sample_rate);
    int slot = (e->sample_msb >> 3) & 7;
    uint32_t index = (uint32_t)slot * 0x800 + ((e->position >> 12) & 0x7ff);
    uint8_t s = index < e->samples_size ? e->samples[index] : 0x80;
    float x = (3.4f / 255.0f * (float)s - 2.0f) * volume_table[slot];
    float i_total = 0;
    for (int i = 0; i < 3; i++) {
        pp_filter2 *f = &e->f[i];
        f->x0 = x;
        f->y0 = -f->a1 * f->y1 - f->a2 * f->y2 + f->b0 * f->x0 + f->b1 * f->x1 + f->b2 * f->x2;
        f->x2 = f->x1; f->x1 = f->x0; f->y2 = f->y1; f->y1 = f->y0;
        if (f->y0 > 1.5f) f->y0 = 1.5f;
        if (f->y0 < -2.0f) f->y0 = -2.0f;
        i_total += f->y0 / rfilt_out[i];
    }
    i_total *= (float)(RFILT_TOTAL * 32000.0 / 2.0);
    e->position += step;
    return (int32_t)i_total;
}
