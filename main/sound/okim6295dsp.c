// okim6295dsp.c - OKI MSM6295: C port of dsp-cpp's sound/okim6295.
#include "okim6295dsp.h"
#include <math.h>
#include <string.h>

static const int kIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
// only nine of the sixteen volume steps are used, the rest mute the voice
static const uint8_t kVolume[16] = {0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03,
                                    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static float diff[49 * 16];
static bool diff_ready;

static void build_diff(void)
{
    static const int nib[16][4] = {
        {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1}, {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
    for (int step = 0; step <= 48; step++) {
        const int sv = (int)floor(16.0 * pow(11.0 / 10.0, step));
        for (int n = 0; n < 16; n++) {
            const int *b = nib[n];
            diff[step * 16 + n] = (float)(b[0] * (sv * b[1] + sv / 2.0 * b[2] + sv / 4.0 * b[3] + sv / 8.0));
        }
    }
    diff_ready = true;
}

void okid_init(okim6295d *o, uint32_t clock, bool pin7_high, const uint8_t *rom, uint32_t rom_size)
{
    memset(o, 0, sizeof *o);
    if (!diff_ready) build_diff();
    o->clock = clock;
    o->divisor = pin7_high ? 132 : 165;
    o->rom = rom;
    o->rom_size = rom_size;
    okid_reset(o);
}

void okid_reset(okim6295d *o)
{
    o->command = -1;
    for (int i = 0; i < 4; i++) {
        memset(&o->voice[i], 0, sizeof o->voice[i]);
        o->voice[i].signal = -2;
    }
}

static inline uint32_t rom_byte(const okim6295d *o, uint32_t off) { return off < o->rom_size ? o->rom[off] : 0u; }

static int clock_adpcm(okid_voice *v, uint8_t nibble)
{
    v->signal += (int)diff[v->step * 16 + (nibble & 0x0f)];
    if (v->signal < -2048) v->signal = -2048;
    if (v->signal > 2047) v->signal = 2047;
    v->step += kIndexShift[nibble & 7];
    if (v->step < 0) v->step = 0;
    if (v->step > 48) v->step = 48;
    return v->signal;
}

uint8_t okid_read(const okim6295d *o)
{
    uint8_t r = 0xf0;
    for (int i = 0; i < 4; i++)
        if (o->voice[i].playing) r |= (uint8_t)(1 << i);
    return r;
}

void okid_write(okim6295d *o, uint8_t value)
{
    if (o->command != -1) {
        uint8_t mask = value >> 4;
        for (int i = 0; i < 4; i++, mask >>= 1) {
            if (!(mask & 1)) continue;
            okid_voice *v = &o->voice[i];
            if (v->playing) continue;
            const uint32_t base = o->bank_offset + (uint32_t)o->command * 8;
            const uint32_t start = (rom_byte(o, base) << 16 | rom_byte(o, base + 1) << 8 | rom_byte(o, base + 2)) & 0x3ffff;
            const uint32_t stop = (rom_byte(o, base + 3) << 16 | rom_byte(o, base + 4) << 8 | rom_byte(o, base + 5)) & 0x3ffff;
            if (start >= stop) continue;
            v->playing = true;
            v->base_offset = start;
            v->sample = 0;
            v->count = 2 * (stop - start + 1);
            v->signal = -2;
            v->step = 0;
            v->volume = kVolume[value & 0x0f];
        }
        o->command = -1;
        return;
    }
    if (value & 0x80) {
        o->command = value & 0x7f;
        return;
    }
    uint8_t mask = value >> 3;                // silence: bits 3 to 6 pick the voices
    for (int i = 0; i < 4; i++, mask >>= 1)
        if (mask & 1) o->voice[i].playing = false;
}

int32_t okid_update(okim6295d *o)
{
    int32_t out = 0;
    for (int i = 0; i < 4; i++) {
        okid_voice *v = &o->voice[i];
        if (!v->playing) continue;
        const uint8_t byte = (uint8_t)rom_byte(o, o->bank_offset + v->base_offset + (v->sample >> 1));
        const uint8_t nibble = (v->sample & 1) == 0 ? byte >> 4 : byte & 0x0f;
        if (++v->sample >= v->count) v->playing = false;
        out += clock_adpcm(v, nibble) * (v->volume >> 1);
    }
    return out < -32767 ? -32767 : out > 32767 ? 32767 : out;
}
