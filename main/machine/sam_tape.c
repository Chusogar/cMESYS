// sam_tape.c - SAM Coupe tapes (see sam_tape.h).
#include "sam_tape.h"
#include <stdlib.h>
#include <string.h>

#define PILOT 2168
#define SYNC1 667
#define SYNC2 735
#define ZERO 855
#define ONE 1710
#define PILOT_HEADER 8063
#define PILOT_DATA 3223
#define TSTATES_PER_MS 3500

static bool push(sam_tape *t, uint32_t v)
{
    if (t->n == t->cap) {
        const uint32_t cap = t->cap ? t->cap * 2 : 65536;
        uint32_t *p = (uint32_t *)realloc(t->pulses, (size_t)cap * sizeof *p);
        if (!p) return false;
        t->pulses = p;
        t->cap = cap;
    }
    t->pulses[t->n++] = v;
    return true;
}

void sam_tape_eject(sam_tape *t)
{
    free(t->pulses);
    memset(t, 0, sizeof *t);
}

// 3.5 MHz T-states -> 6 MHz, the remainder carried
static uint32_t to_sam(sam_tape *t, uint32_t zx)
{
    const uint64_t v = (uint64_t)zx * 6000000ull + t->frac;
    t->frac = v % 3500000ull;
    return (uint32_t)(v / 3500000ull);
}

void sam_tape_rewind(sam_tape *t)
{
    t->index = 0;
    t->frac = 0;
    t->counter = t->n ? (int64_t)to_sam(t, t->pulses[0]) : 0;
    t->ear = false;
}

static bool emit_pause(sam_tape *t, uint32_t ms) { return ms == 0 || push(t, ms * TSTATES_PER_MS); }

static bool emit_block(sam_tape *t, const uint8_t *data, uint32_t len, uint32_t pilot, uint32_t sync1,
                       uint32_t sync2, uint32_t zero, uint32_t one, int pilot_pulses, int used_bits, uint32_t pause)
{
    for (int i = 0; i < pilot_pulses; i++)
        if (!push(t, pilot)) return false;
    if (sync1 && !push(t, sync1)) return false;
    if (sync2 && !push(t, sync2)) return false;
    for (uint32_t i = 0; i < len; i++) {
        const int bits = i + 1 == len ? used_bits : 8;
        for (int b = 0; b < bits; b++) {
            const uint32_t w = (data[i] & (0x80 >> b)) ? one : zero;
            if (!push(t, w) || !push(t, w)) return false;
        }
    }
    return emit_pause(t, pause);
}

// A Spectrum header (17 bytes after the flag) as the SAM's 80-byte one
static bool emit_zx_header_as_sam(sam_tape *t, const uint8_t *zx, uint32_t pause)
{
    const uint8_t type = zx[0];
    const uint16_t length = (uint16_t)(zx[11] | (zx[12] << 8));
    const uint16_t param1 = (uint16_t)(zx[13] | (zx[14] << 8));
    uint8_t h[82] = { 0 };
    h[0] = 1;
    h[1] = type == 0 ? 16 : 19;             // BASIC program / CODE
    for (int i = 0; i < 10; i++) h[2 + i] = zx[1 + i];
    const uint16_t start = type == 0 ? 0 : param1;
    h[1 + 31] = (uint8_t)(start >> 14);     // page, then the offset in it
    h[1 + 32] = (uint8_t)(start & 0xff);
    h[1 + 33] = (uint8_t)((start & 0x3fff) >> 8);
    h[1 + 34] = (uint8_t)(length >> 14);
    h[1 + 35] = (uint8_t)(length & 0xff);
    h[1 + 36] = (uint8_t)((length & 0x3fff) >> 8);
    if (type == 0 && param1 < 32768) {
        h[1 + 37] = 0;                      // auto-run line
        h[1 + 38] = (uint8_t)(param1 & 0xff);
        h[1 + 39] = (uint8_t)(param1 >> 8);
    } else {
        h[1 + 37] = 0xff;
    }
    uint8_t parity = 0;
    for (int i = 0; i < 81; i++) parity ^= h[i];
    h[81] = parity;
    return emit_block(t, h, sizeof h, PILOT, SYNC1, SYNC2, ZERO, ONE, PILOT_HEADER, 8, pause);
}

static bool std_block(sam_tape *t, const uint8_t *p, uint32_t len, uint32_t pause)
{
    if (p[0] == 0 && len == 19) return emit_zx_header_as_sam(t, p + 1, pause);
    return emit_block(t, p, len, PILOT, SYNC1, SYNC2, ZERO, ONE, p[0] < 128 ? PILOT_HEADER : PILOT_DATA, 8, pause);
}

static void parse_tap(sam_tape *t, const uint8_t *d, uint32_t size)
{
    uint32_t pos = 0;
    while (pos + 2 <= size) {
        const uint32_t len = d[pos] | ((uint32_t)d[pos + 1] << 8);
        pos += 2;
        if (len == 0 || pos + len > size) break;
        if (!std_block(t, d + pos, len, 1000)) return;
        pos += len;
    }
}

static void parse_tzx(sam_tape *t, const uint8_t *d, uint32_t size)
{
#define U16(o) ((uint32_t)d[o] | ((uint32_t)d[(o) + 1] << 8))
#define U24(o) (U16(o) | ((uint32_t)d[(o) + 2] << 16))
    uint32_t pos = 10;
    while (pos < size) {
        const uint8_t id = d[pos++];
        switch (id) {
        case 0x10: {
            if (pos + 4 > size) return;
            const uint32_t pause = U16(pos), len = U16(pos + 2);
            pos += 4;
            if (pos + len > size || !std_block(t, d + pos, len, pause)) return;
            pos += len;
            break;
        }
        case 0x11: {
            if (pos + 18 > size) return;
            const uint32_t len = U24(pos + 15);
            const int used = d[pos + 12];
            if (pos + 18 + len > size) return;
            if (!emit_block(t, d + pos + 18, len, U16(pos), U16(pos + 2), U16(pos + 4), U16(pos + 6), U16(pos + 8),
                            (int)U16(pos + 10), used ? used : 8, U16(pos + 13)))
                return;
            pos += 18 + len;
            break;
        }
        case 0x12: {
            if (pos + 4 > size) return;
            for (uint32_t i = 0; i < U16(pos + 2); i++)
                if (!push(t, U16(pos))) return;
            pos += 4;
            break;
        }
        case 0x13: {
            if (pos >= size) return;
            const uint32_t count = d[pos++];
            if (pos + count * 2 > size) return;
            for (uint32_t i = 0; i < count; i++)
                if (!push(t, U16(pos + i * 2))) return;
            pos += count * 2;
            break;
        }
        case 0x14: {
            if (pos + 10 > size) return;
            const uint32_t len = U24(pos + 7);
            const int used = d[pos + 4];
            if (pos + 10 + len > size) return;
            if (!emit_block(t, d + pos + 10, len, 0, 0, 0, U16(pos), U16(pos + 2), 0, used ? used : 8, U16(pos + 5)))
                return;
            pos += 10 + len;
            break;
        }
        case 0x20:
            if (pos + 2 > size || !emit_pause(t, U16(pos))) return;
            pos += 2;
            break;
        case 0x21: case 0x30: if (pos >= size) return; pos += 1 + d[pos]; break;
        case 0x22: break;
        case 0x31: if (pos + 1 >= size) return; pos += 2 + d[pos + 1]; break;
        case 0x32: if (pos + 2 > size) return; pos += 2 + U16(pos); break;
        case 0x33: if (pos >= size) return; pos += 1 + (uint32_t)d[pos] * 3; break;
        case 0x35:
            if (pos + 14 > size) return;
            pos += 14 + (U24(pos + 10) | ((uint32_t)d[pos + 13] << 24));
            break;
        case 0x5a: pos += 9; break;
        default: return;                   // unknown: keep what was decoded
        }
    }
#undef U16
#undef U24
}

const char *sam_tape_load(sam_tape *t, const uint8_t *data, uint32_t size)
{
    sam_tape_eject(t);
    if (size > 10 && memcmp(data, "ZXTape!\x1a", 8) == 0) parse_tzx(t, data, size);
    else parse_tap(t, data, size);
    if (!t->n) {
        sam_tape_eject(t);
        return "no usable blocks in the tape";
    }
    sam_tape_rewind(t);
    return NULL;
}

void sam_tape_tick(sam_tape *t, int tstates)
{
    if (!t->playing || t->index >= t->n) return;
    t->counter -= tstates;
    while (t->counter <= 0) {
        t->ear = !t->ear;
        if (++t->index >= t->n) {
            t->playing = false;
            return;
        }
        t->counter += (int64_t)to_sam(t, t->pulses[t->index]);
    }
}
