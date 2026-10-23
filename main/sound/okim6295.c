// okim6295.c - OKI MSM6295. See okim6295.h.
#include "okim6295.h"
#include <math.h>
#include <string.h>

static const int8_t index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const uint8_t volume_table[16] = { 0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03, 0x02, 0, 0, 0, 0, 0, 0, 0 };
static int16_t diff_lookup[49 * 16];
static bool tables_done;

static void compute_tables(void)
{
    static const int8_t nbl2bit[16][4] = {
        { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
        { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
    };
    for (int step = 0; step <= 48; step++) {
        int stepval = (int)floor(16.0 * pow(11.0 / 10.0, (double)step));
        for (int nib = 0; nib < 16; nib++)
            diff_lookup[step * 16 + nib] = (int16_t)(nbl2bit[nib][0] *
                (stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] + stepval / 4 * nbl2bit[nib][3] + stepval / 8));
    }
    tables_done = true;
}

static inline uint8_t rom_r(const okim6295 *o, uint32_t a)
{
    a &= 0x3ffff;
    return a < o->rom_size ? o->rom[a] : 0;
}

void okim6295_init(okim6295 *o, const uint8_t *rom, uint32_t rom_size)
{
    if (!tables_done) compute_tables();
    o->rom = rom;
    o->rom_size = rom_size;
    okim6295_reset(o);
}

void okim6295_reset(okim6295 *o)
{
    memset(o->voice, 0, sizeof o->voice);
    o->command = -1;
    o->out = 0;
}

uint8_t okim6295_read(const okim6295 *o)
{
    uint8_t r = 0xf0;
    for (int v = 0; v < 4; v++) if (o->voice[v].playing) r |= (uint8_t)(1 << v);
    return r;
}

void okim6295_write(okim6295 *o, uint8_t command)
{
    if (o->command != -1) {
        int mask = command >> 4;
        for (int v = 0; v < 4; v++, mask >>= 1) {
            if (!(mask & 1)) continue;
            okim6295_voice *vo = &o->voice[v];
            if (vo->playing) continue;
            const uint32_t base = (uint32_t)o->command * 8;
            uint32_t start = ((uint32_t)rom_r(o, base) << 16 | (uint32_t)rom_r(o, base + 1) << 8 | rom_r(o, base + 2)) & 0x3ffff;
            uint32_t stop = ((uint32_t)rom_r(o, base + 3) << 16 | (uint32_t)rom_r(o, base + 4) << 8 | rom_r(o, base + 5)) & 0x3ffff;
            if (start < stop) {
                vo->playing = true;
                vo->base = start;
                vo->sample = 0;
                vo->count = 2 * (stop - start + 1);
                vo->signal = 0;
                vo->step = 0;
                vo->volume = volume_table[command & 0x0f];
            }
        }
        o->command = -1;
    } else if (command & 0x80) {
        o->command = command & 0x7f;
    } else {
        int mask = command >> 3;
        for (int v = 0; v < 4; v++, mask >>= 1)
            if (mask & 1) o->voice[v].playing = false;
    }
}

int16_t okim6295_tick(okim6295 *o)
{
    int32_t sum = 0;        // in units of 1/32768 x 32 (signal * volume)
    for (int v = 0; v < 4; v++) {
        okim6295_voice *vo = &o->voice[v];
        if (!vo->playing) continue;
        const uint8_t nib = (uint8_t)(rom_r(o, vo->base + vo->sample / 2) >> (((vo->sample & 1) << 2) ^ 4));
        vo->signal += diff_lookup[vo->step * 16 + (nib & 15)];
        if (vo->signal > 2047) vo->signal = 2047;
        else if (vo->signal < -2048) vo->signal = -2048;
        vo->step += index_shift[nib & 7];
        if (vo->step > 48) vo->step = 48;
        else if (vo->step < 0) vo->step = 0;
        sum += vo->signal * vo->volume;
        if (++vo->sample >= vo->count) vo->playing = false;
    }
    // MAME: signal * (volume / 32) / 2048 per voice, summed and clamped to +-1
    int32_t s = sum / 2;
    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
    o->out = (int16_t)s;
    return o->out;
}
