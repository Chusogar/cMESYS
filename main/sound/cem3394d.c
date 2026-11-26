// cem3394d.c - see cem3394d.h
#include <math.h>
#include <string.h>
#include "cem3394d.h"

#define WAVE_TRIANGLE 1
#define WAVE_SAWTOOTH 2
#define WAVE_PULSE 4
#define FBITS 28
#define FONE (1u << FBITS)
#define FONED ((double)(1u << FBITS))
#define FMASK (FONE - 1)
#define INV_RATE (1.0 / 44100.0)

void cem3394d_reset(cem3394d *c)
{
    memset(c->values, 0, sizeof c->values);
    c->wave_select = 0;
    c->volume = c->mixer_internal = c->mixer_external = c->position = c->step = 0;
    c->filter_step = c->modulation_depth = c->pulse_width = 0;
}

void cem3394d_init(cem3394d *c, double vco_zero_freq, double filter_zero_freq)
{
    c->vco_zero_freq = vco_zero_freq;
    c->filter_zero_freq = filter_zero_freq;
    cem3394d_reset(c);
}

static double compute_db(double v)
{
    if (v >= 4.0) return 0.0;
    if (v <= 0.0) return 90.0;
    if (v >= 2.5) return (4.0 - v) * (1.0 / 1.5) * 20.0;
    const double t = 20.0 * pow(2.0, 2.5 - v);
    return t < 90.0 ? 90.0 : t;
}

static uint32_t compute_db_volume(double v)
{
    double t;
    if (v >= 4.0) return 256;
    if (v <= 0.0) return 0;
    if (v >= 2.5) t = (4.0 - v) * (1.0 / 1.5) * 20.0;
    else {
        t = 20.0 * pow(2.0, 2.5 - v);
        if (t < 50.0) return 0;
    }
    return (uint32_t)(256.0 * pow(0.891251, t));
}

void cem3394d_set_voltage(cem3394d *c, int input, double v)
{
    if (input < 0 || input > CEM_FINAL_GAIN) return;
    if (v == c->values[input]) return;
    c->values[input] = v;
    switch (input) {
    case CEM_VCO_FREQUENCY:
        c->step = (uint32_t)(c->vco_zero_freq * pow(2.0, -v * (1.0 / 0.75)) * INV_RATE * FONED);
        break;
    case CEM_WAVE_SELECT:
        c->wave_select &= (uint8_t)~(WAVE_TRIANGLE | WAVE_SAWTOOTH);
        if (v >= -0.5 && v <= -0.2) c->wave_select |= WAVE_TRIANGLE;
        else if (v >= 0.9 && v <= 1.5) c->wave_select |= WAVE_TRIANGLE | WAVE_SAWTOOTH;
        else if (v >= 2.3 && v <= 3.9) c->wave_select |= WAVE_SAWTOOTH;
        break;
    case CEM_PULSE_WIDTH:
        if (v < 0.0) {
            c->pulse_width = 0;
            c->wave_select &= (uint8_t)~WAVE_PULSE;
        } else {
            double t = v * 0.5;
            t = 0.25 + (0.75 - 0.25) * t;
            c->pulse_width = (uint32_t)(t * FONED);
            c->wave_select |= WAVE_PULSE;
        }
        break;
    case CEM_FINAL_GAIN: c->volume = compute_db_volume(v); break;
    case CEM_MIXER_BALANCE:
        if (v >= 0.0) {
            c->mixer_internal = compute_db_volume(3.55 - v);
            c->mixer_external = compute_db_volume(3.55 + 0.45 * (v * 0.25));
        } else {
            c->mixer_internal = compute_db_volume(3.55 - 0.45 * (v * 0.25));
            c->mixer_external = compute_db_volume(3.55 + v);
        }
        break;
    case CEM_FILTER_FREQUENCY:
        c->filter_step = (uint32_t)(c->filter_zero_freq * pow(2.0, -v * (1.0 / 0.375)) * INV_RATE * FONED);
        break;
    case CEM_MODULATION_AMOUNT:
        if (v < 0.0) c->modulation_depth = (uint32_t)(0.01 * FONED);
        else if (v > 3.5) c->modulation_depth = (uint32_t)(2.00 * FONED);
        else c->modulation_depth = (uint32_t)(((v * (1.0 / 3.5)) * 1.99 + 0.01) * FONED);
        break;
    default: break;
    }
}

double cem3394d_get_parameter(const cem3394d *c, int input)
{
    if (input < 0 || input > CEM_FINAL_GAIN) return 0.0;
    const double v = c->values[input];
    switch (input) {
    case CEM_VCO_FREQUENCY: return c->vco_zero_freq * pow(2.0, -v * (1.0 / 0.75));
    case CEM_WAVE_SELECT: return v;
    case CEM_PULSE_WIDTH: return v <= 0.0 ? 0.0 : v >= 2.0 ? 1.0 : v * 0.5;
    case CEM_FINAL_GAIN: return compute_db(v);
    case CEM_MIXER_BALANCE: return v * 0.25;
    case CEM_MODULATION_AMOUNT: return v < 0.0 ? 0.01 : v > 3.5 ? 2.0 : (v * (1.0 / 3.5)) * 1.99 + 0.01;
    case CEM_FILTER_RESONANCE: return v < 0.0 ? 0.0 : v > 2.5 ? 1.0 : v * (1.0 / 2.5);
    case CEM_FILTER_FREQUENCY: return c->filter_zero_freq * pow(2.0, -v * (1.0 / 0.375));
    default: return 0.0;
    }
}

int16_t cem3394d_update(cem3394d *c)
{
    int int_volume = (int)((c->volume * c->mixer_internal) / 256);
    const uint32_t step = c->step;
    if (step > c->filter_step && (step - c->filter_step) != 0) int_volume /= (int)(step - c->filter_step);
    if (int_volume == 0) return 0;
    int16_t mix = 0;
    const uint32_t position = c->position;
    uint32_t end = c->position;
    if (c->wave_select & WAVE_PULSE) {
        const uint32_t pw = c->pulse_width;
        if (pw >= step) {
            mix = position < pw ? (int16_t)0x1932 : 0;
            end = (position + step) & FMASK;
        } else {
            // (dsp-cpp's int32 product, wrapping as it does)
            const int16_t vol = step ? (int16_t)((int32_t)((uint32_t)0x1932 * pw) / (int32_t)step) : 0;
            const uint32_t np = position + step;
            mix = ((np ^ position) & ~FMASK) ? vol : 0;
            end = np & FMASK;
        }
    }
    if (c->wave_select & WAVE_SAWTOOTH) {
        mix = (int16_t)(mix + (int16_t)(((position >> (FBITS - 14)) & 0x3fff) - 0x2000));
        end = (position + step) & FMASK;
    }
    if (c->wave_select & WAVE_TRIANGLE) {
        int16_t v;
        if (position & (1u << (FBITS - 1))) v = (int16_t)(0x2000 - ((position >> (FBITS - 14)) & 0x1fff));
        else v = (int16_t)((position >> (FBITS - 14)) & 0x1fff);
        mix = (int16_t)(mix + v + (v >> 2));
        end = (position + step) & FMASK;
    }
    c->position = end;
    return (int16_t)((int32_t)mix * int_volume / 128);
}
