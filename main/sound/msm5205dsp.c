// msm5205dsp.c - OKI MSM5205: C port of dsp-cpp's sound/msm5205.
#include "msm5205dsp.h"
#include <math.h>
#include <string.h>

static const int kIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static int diff[49 * 16];
static bool diff_ready;

static void build(void)
{
    static const int nib[16][4] = {
        {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1}, {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
    for (int step = 0; step <= 48; step++) {
        const int sv = (int)floor(16.0 * pow(11.0 / 10.0, step));
        for (int n = 0; n < 16; n++)
            diff[step * 16 + n] = nib[n][0] * (sv * nib[n][1] + (sv >> 1) * nib[n][2] + (sv >> 2) * nib[n][3] + (sv >> 3));
    }
    diff_ready = true;
}

void msm5205d_init(msm5205d *m, uint32_t clock, int prescaler, int bits)
{
    if (!diff_ready) build();
    memset(m, 0, sizeof *m);
    m->clock = clock;
    m->prescaler = prescaler;
    m->bits = bits;
    msm5205d_reset(m);
}

void msm5205d_reset(msm5205d *m)
{
    m->position = m->end = 0;
    m->data_value = -1;
    m->data = 0;
    m->reset = m->idle = true;
    m->signal = m->step = 0;
}

void msm5205d_data_w(msm5205d *m, uint8_t v) { m->data = m->bits == 4 ? (uint8_t)(v & 0x0f) : (uint8_t)((v & 7) << 1); }

void msm5205d_set_reset(msm5205d *m, bool state)
{
    m->reset = m->idle = state;
    if (state) {
        m->signal = m->step = 0;
        m->data_value = -1;
    }
}

static void clock_nibble(msm5205d *m, uint8_t d)
{
    m->signal += diff[m->step * 16 + (d & 0x0f)];
    if (m->signal > 2047) m->signal = 2047;
    else if (m->signal < -2048) m->signal = -2048;
    m->step += kIndexShift[d & 7];
    if (m->step > 48) m->step = 48;
    else if (m->step < 0) m->step = 0;
}

static void decode(msm5205d *m, uint8_t nibble)
{
    const uint8_t d = m->bits == 4 ? (uint8_t)(nibble & 0x0f) : (uint8_t)((nibble & 7) << 1);
    if (m->reset) {
        m->signal = m->step = 0;
        m->data_value = -1;
        return;
    }
    clock_nibble(m, d);
}

void msm5205d_vclk(msm5205d *m)
{
    if (m->vclk_handler) m->vclk_handler(m->user);
    if (!m->rom) {
        // streaming: the nibble written with data_w() (the VCLK still fires
        // while reset is held)
        if (m->reset) {
            m->signal = m->step = 0;
            m->data_value = -1;
            return;
        }
        clock_nibble(m, m->data);
        return;
    }
    if (m->idle) return;
    if (m->data_value != -1) {
        decode(m, (uint8_t)(m->data_value & 0x0f));
        m->data_value = -1;
        m->position++;
        if (m->position >= m->end || m->position >= m->rom_size) msm5205d_set_reset(m, true);
    } else {
        m->data_value = m->position < m->rom_size ? m->rom[m->position] : 0;
        decode(m, (uint8_t)(m->data_value >> 4));
    }
}
