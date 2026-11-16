// psx_xa_adpcm.c - the PlayStation CD-ROM XA-ADPCM decoder: a C11 port of
// dsp-cpp's machine/psx_xa_adpcm (from ProjectPSX, MIT License, Copyright (c)
// 2019 Pedro Cortés). A sector in, interleaved stereo at 44100 Hz out (the
// resampler state carries over between sectors, as in ProjectPSX).
#include "psx_xa_adpcm.h"

#define kBytesPerHeader 24
static const int8_t kPosFilter[] = {0, 60, 115, 98, 122};
static const int8_t kNegFilter[] = {0, 0, -52, -55, -60};
static const int16_t kZigZag[7][29] = {
    {0, 0, 0, 0, 0, -0x0002, 0x000A, -0x0022, 0x0041, -0x0054, 0x0034, 0x0009, -0x010A, 0x0400,
     -0x0A78, 0x234C, 0x6794, -0x1780, 0x0BCD, -0x0623, 0x0350, -0x016D, 0x006B, 0x000A, -0x0010,
     0x0011, -0x0008, 0x0003, -0x0001},
    {0, 0, 0, -0x0002, 0, 0x0003, -0x0013, 0x003C, -0x004B, 0x00A2, -0x00E3, 0x0132, -0x0043,
     -0x0267, 0x0C9D, 0x74BB, -0x11B4, 0x09B8, -0x05BF, 0x0372, -0x01A8, 0x00A6, -0x001B, 0x0005,
     0x0006, -0x0008, 0x0003, -0x0001, 0},
    {0, 0, -0x0001, 0x0003, -0x0002, -0x0005, 0x001F, -0x004A, 0x00B3, -0x0192, 0x02B1, -0x039E,
     0x04F8, -0x05A6, 0x7939, -0x05A6, 0x04F8, -0x039E, 0x02B1, -0x0192, 0x00B3, -0x004A, 0x001F,
     -0x0005, -0x0002, 0x0003, -0x0001, 0, 0},
    {0, -0x0001, 0x0003, -0x0008, 0x0006, 0x0005, -0x001B, 0x00A6, -0x01A8, 0x0372, -0x05BF, 0x09B8,
     -0x11B4, 0x74BB, 0x0C9D, -0x0267, -0x0043, 0x0132, -0x00E3, 0x00A2, -0x004B, 0x003C, -0x0013,
     0x0003, 0, -0x0002, 0, 0, 0},
    {-0x0001, 0x0003, -0x0008, 0x0011, -0x0010, 0x000A, 0x006B, -0x016D, 0x0350, -0x0623, 0x0BCD,
     -0x1780, 0x6794, 0x234C, -0x0A78, 0x0400, -0x010A, 0x0009, 0x0034, -0x0054, 0x0041, -0x0022,
     0x000A, -0x0001, 0, 0x0001, 0, 0, 0},
    {0x0002, -0x0008, 0x0010, -0x0023, 0x002B, 0x001A, -0x00EB, 0x027B, -0x0548, 0x0AFA, -0x16FA,
     0x53E0, 0x3C07, -0x1249, 0x080E, -0x0347, 0x015B, -0x0044, -0x0017, 0x0046, -0x0023, 0x0011,
     -0x0005, 0, 0, 0, 0, 0, 0},
    {-0x0005, 0x0011, -0x0023, 0x0046, -0x0017, -0x0044, 0x015B, -0x0347, 0x080E, -0x1249, 0x3C07,
     0x53E0, -0x16FA, 0x0AFA, -0x0548, 0x027B, -0x00EB, 0x001A, 0x002B, -0x0023, 0x0010, -0x0008,
     0x0002, 0, 0, 0, 0, 0, 0},
};


static int16_t g_old_l, g_older_l, g_old_r, g_older_r;
static int g_six_step = 6;
static int g_resample_pointer;
static int16_t g_ring[2][32];

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int signed4bit(uint8_t value) { return (int)((int32_t)((uint32_t)value << 28) >> 28); }

static void decode_nibbles(const uint8_t *xa, int position, int blk, int nibble, int16_t *old, int16_t *older, int16_t out[28])
{
    int header_shift = xa[position + 4 + blk * 2 + nibble] & 0x0F;
    if (header_shift > 12) header_shift = 9;
    const int shift = 12 - header_shift;
    const int filter = (xa[position + 4 + blk * 2 + nibble] & 0x30) >> 4;
    const int f0 = kPosFilter[filter];
    const int f1 = kNegFilter[filter];
    for (int i = 0; i < 28; i++) {
        const int t = signed4bit((uint8_t)((xa[position + 16 + blk + i * 4] >> (nibble * 4)) & 0x0F));
        int s = (t << shift) + ((*old * f0 + *older * f1 + 32) / 64);
        s = clampi(s, -0x8000, 0x7FFF);
        out[i] = (int16_t)s;
        *older = *old;
        *old = (int16_t)s;
    }
}

static int16_t zig_zag_interpolate(int resample_pointer, int table, int channel)
{
    int sum = 0;
    for (int i = 0; i < 29; i++) sum += (g_ring[channel][(resample_pointer - i) & 0x1F] * kZigZag[table][i]) / 0x8000;
    return (int16_t)clampi(sum, -0x8000, 0x7FFF);
}

static int resample_to_44100(const int16_t *samples, int n, bool is_18900, int channel, int16_t *out)
{
    int k = 0;
    for (int j = 0; j < n; j++) {
        g_ring[channel][g_resample_pointer++ & 0x1F] = samples[j];
        if (--g_six_step == 0) {
            g_six_step = 6;
            for (int table = 0; table < 7; table++) {
                const int16_t sample = zig_zag_interpolate(g_resample_pointer, table, channel);
                out[k++] = sample;
                if (is_18900) out[k++] = sample;
            }
        }
    }
    return k;
}

int decode_xa_adpcm_sector(const uint8_t *sector, uint8_t coding_info, int16_t *out)
{
    const bool is_stereo = (coding_info & 0x1) == 0x1;
    const bool is_18900 = ((coding_info >> 2) & 0x1) == 0x1;
    const bool is_8bit = ((coding_info >> 4) & 0x1) == 0x1;
    if (is_8bit) return 0;   // rare; skipped
    static int16_t left[2016], right[2016], mono[4032], rl[XA_MAX_OUT / 2], rr[XA_MAX_OUT / 2];
    int nl = 0, nr = 0, nm = 0;
    int position = kBytesPerHeader;
    for (int i = 0; i < 18; i++) {
        for (int blk = 0; blk < 4; blk++) {
            if (is_stereo) {
                decode_nibbles(sector, position, blk, 0, &g_old_l, &g_older_l, left + nl);
                nl += 28;
                decode_nibbles(sector, position, blk, 1, &g_old_r, &g_older_r, right + nr);
                nr += 28;
            } else {
                decode_nibbles(sector, position, blk, 0, &g_old_l, &g_older_l, mono + nm);
                nm += 28;
                decode_nibbles(sector, position, blk, 1, &g_old_l, &g_older_l, mono + nm);
                nm += 28;
            }
        }
        position += 128;
    }
    int k = 0;
    if (is_stereo) {
        const int a = resample_to_44100(left, nl, is_18900, 0, rl);
        const int b = resample_to_44100(right, nr, is_18900, 1, rr);
        for (int i = 0; i < a; i++) {
            out[k++] = rl[i];
            out[k++] = i < b ? rr[i] : 0;
        }
    } else {
        const int a = resample_to_44100(mono, nm, is_18900, 0, rl);
        for (int i = 0; i < a; i++) {
            out[k++] = rl[i];
            out[k++] = rl[i];
        }
    }
    return k;
}
