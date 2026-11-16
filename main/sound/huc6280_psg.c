// huc6280_psg.c - the HuC6280's PSG: a C11 port of dsp-cpp's sound/huc6280_psg.
#include <math.h>
#include <string.h>
#include "huc6280_psg.h"

static double attenuation(int steps, double db) { return pow(10.0, -db * (double)steps / 20.0); }

static void gain(huc6280_psg *p, huc6280_psg_ch *c)
{
    const int vol = c->control & 0x1f, cl = (c->balance >> 4) & 0x0f, cr = c->balance & 0x0f;
    const int gl = (p->balance >> 4) & 0x0f, gr = p->balance & 0x0f;
    const double level = attenuation(31 - vol, 1.5);
    const double l = attenuation((15 - cl) + (15 - gl), 3.0), r = attenuation((15 - cr) + (15 - gr), 3.0);
    c->gain = level * (l + r) * 0.5;
}

void huc6280_psg_init(huc6280_psg *p, uint32_t clock, int rate)
{
    p->clock = clock;
    p->rate = rate;
    huc6280_psg_reset(p);
}

void huc6280_psg_reset(huc6280_psg *p)
{
    memset(p->ch, 0, sizeof p->ch);
    for (int i = 0; i < 6; i++) p->ch[i].noise_shift = 0x0008000;
    p->selected = 0;
    p->balance = 0xff;
    p->lfo_frequency = p->lfo_control = 0;
    p->cycles_per_sample = (int)(p->clock / (uint32_t)p->rate);
    p->cycle_error = 0;
    for (int i = 0; i < 6; i++) gain(p, &p->ch[i]);
}

void huc6280_psg_write(huc6280_psg *p, uint8_t o, uint8_t v)
{
    switch (o & 0x0f) {
    case 0: p->selected = v & 7; return;
    case 1:
        p->balance = v;
        for (int i = 0; i < 6; i++) gain(p, &p->ch[i]);
        return;
    case 8: p->lfo_frequency = v; return;
    case 9: p->lfo_control = v; return;
    default: break;
    }
    if (p->selected >= 6) return;
    huc6280_psg_ch *c = &p->ch[p->selected];
    switch (o & 0x0f) {
    case 2: c->frequency = (uint16_t)((c->frequency & 0xf00) | v); break;
    case 3: c->frequency = (uint16_t)((c->frequency & 0x0ff) | ((v & 0x0f) << 8)); break;
    case 4:
        if (!(c->control & 0x80) && (v & 0x80)) c->wave_index = 0;
        if (!(v & 0x80)) c->write_index = 0;
        c->control = v;
        gain(p, c);
        break;
    case 5: c->balance = v; gain(p, c); break;
    case 6:
        if ((c->control & 0xc0) == 0xc0) {
            c->dda = v & 0x1f;
        } else {
            c->wave[c->write_index & 0x1f] = v & 0x1f;
            c->write_index = (c->write_index + 1) & 0x1f;
        }
        break;
    case 7: c->noise_control = v; break;
    default: break;
    }
}

static void step(huc6280_psg_ch *c, int cycles, int noise)
{
    if (noise && (c->noise_control & 0x80)) {
        const int period = (32 - (c->noise_control & 0x1f)) * 512;
        c->noise_counter -= cycles;
        while (c->noise_counter <= 0) {
            c->noise_counter += period;
            const uint32_t fb = ((c->noise_shift >> 1) ^ c->noise_shift) & 1;
            c->noise_shift = (c->noise_shift >> 1) | (fb << 16);
            c->noise_level = (c->noise_shift & 1) ? 0x1f : 0x00;
        }
        return;
    }
    const int period = c->frequency ? (int)c->frequency : 0x1000;
    c->counter -= cycles;
    while (c->counter <= 0) {
        c->counter += period;
        c->wave_index = (c->wave_index + 1) & 0x1f;
    }
}

int16_t huc6280_psg_update(huc6280_psg *p)
{
    p->cycle_error += (int)p->clock - p->cycles_per_sample * p->rate;
    int cycles = p->cycles_per_sample;
    if (p->cycle_error >= p->rate) {
        p->cycle_error -= p->rate;
        cycles++;
    }
    double mix = 0;
    for (int i = 0; i < 6; i++) {
        huc6280_psg_ch *c = &p->ch[i];
        const int noise = i >= 4;
        step(c, cycles, noise);
        if (!(c->control & 0x80)) continue;
        uint8_t s;
        if (c->control & 0x40) s = c->dda;
        else if (noise && (c->noise_control & 0x80)) s = c->noise_level;
        else s = c->wave[c->wave_index & 0x1f];
        mix += ((double)s - 16.0) * c->gain;
    }
    double scaled = mix * (32767.0 / (16.0 * 6));
    if (scaled < -32768.0) scaled = -32768.0;
    if (scaled > 32767.0) scaled = 32767.0;
    return (int16_t)scaled;
}
