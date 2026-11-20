// exelv_tape.c - the Exelvision cassette (see exelv_tape.h).
#include "exelv_tape.h"
#include <stdlib.h>
#include <string.h>

#define LEADER_BYTES 255
#define LEADER 0x55
#define SYNC 0x70
#define HALF_THRESHOLD ((EXL_HALF_BIT0 + EXL_HALF_BIT1) / 2)

typedef struct { uint8_t *p; size_t n, c; } bytes_t;

static bool bpush(bytes_t *b, uint8_t v)
{
    if (b->n == b->c) {
        const size_t c = b->c ? b->c * 2 : 1024;
        uint8_t *p = (uint8_t *)realloc(b->p, c);
        if (!p) return false;
        b->p = p;
        b->c = c;
    }
    b->p[b->n++] = v;
    return true;
}

// Groups of four half periods into bits (two or more short ones: a 1)
static void decode_halves(const int *h, size_t n, bytes_t *out)
{
    uint8_t value = 0;
    int bits = 0;
    for (size_t i = 0; i + 3 < n; i += 4) {
        int shorts = 0;
        for (size_t j = i; j < i + 4; j++)
            if (h[j] < HALF_THRESHOLD) shorts++;
        value = (uint8_t)((value << 1) | (shorts >= 2 ? 1 : 0));
        if (++bits == 8) {
            bpush(out, value);
            bits = 0;
            value = 0;
        }
    }
}

void exl_tape_eject(exl_tape *t)
{
    free(t->bytes);
    free(t->halves);
    free(t->rec);
    memset(t, 0, sizeof *t);
}

static void start_bit(exl_tape *t)
{
    const bool one = (t->bytes[t->pos] >> t->bit) & 1;
    t->remain = one ? EXL_HALF_BIT1 : EXL_HALF_BIT0;
    t->level = (t->half & 1) != 0;          // the low half first, then the high one
}

void exl_tape_rewind(exl_tape *t)
{
    t->pos = 0;
    t->bit = 7;
    t->half = 0;
    t->level = true;
    t->remain = 0;
    if (t->n) start_bit(t);
}

static const char *load_k7(exl_tape *t, const uint8_t *data, size_t size)
{
    if (!size) return "empty cassette";
    size_t leader = 0;
    while (leader < size && data[leader] == LEADER) leader++;
    bytes_t b = { 0 };
    if (leader < 16) {
        for (int i = 0; i < LEADER_BYTES; i++) bpush(&b, LEADER);
        // an image that starts with the name has no sync byte either
        if (data[leader < size ? leader : 0] != SYNC) bpush(&b, SYNC);
        for (size_t i = leader; i < size; i++) bpush(&b, data[i]);
    } else {
        for (size_t i = 0; i < size; i++) bpush(&b, data[i]);
    }
    if (!b.n) return "out of memory";
    free(t->bytes);
    t->bytes = b.p;
    t->n = b.n;
    exl_tape_rewind(t);
    return NULL;
}

static const char *load_wav(exl_tape *t, const uint8_t *f, size_t size)
{
#define U16(o) ((unsigned)(f[o] | (f[(o) + 1] << 8)))
#define U32(o) ((uint32_t)(U16(o) | ((uint32_t)U16((o) + 2) << 16)))
    if (size < 44 || memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4)) return "not a WAV file";
    unsigned channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t data = 0, data_size = 0;
    for (size_t o = 12; o + 8 <= size;) {
        const uint32_t sz = U32(o + 4);
        if (!memcmp(f + o, "fmt ", 4) && o + 24 <= size) {
            if (U16(o + 8) != 1) break;
            channels = U16(o + 10);
            rate = U32(o + 12);
            bits = U16(o + 22);
        } else if (!memcmp(f + o, "data", 4)) {
            data = o + 8;
            data_size = sz < size - data ? sz : size - data;
            break;
        }
        o += 8 + (size_t)sz + (sz & 1);
    }
    if (!data || !rate || !channels || (bits != 8 && bits != 16)) return "unsupported WAV (PCM 8/16-bit needed)";
    const size_t frame = channels * (bits / 8), frames = data_size / frame;
    int64_t sum = 0;
    for (size_t i = 0; i < frames; i++) {
        const size_t o = data + i * frame;
        sum += bits == 8 ? (int)f[o] - 128 : (int)(int16_t)U16(o) >> 8;
    }
    const int mid = frames ? (int)(sum / (int64_t)frames) : 0;
#define SAMPLE(i) (bits == 8 ? (int)f[data + (i) * frame] - 128 : (int)(int16_t)U16(data + (i) * frame) >> 8)
    // the half periods (CPU cycles at 2.4576 MHz), level with hysteresis
    int *halves = NULL;
    size_t nh = 0, ch = 0;
    bool high = frames && SAMPLE(0) > mid;
    size_t last = 0;
    for (size_t i = 0; i < frames; i++) {
        const int s = SAMPLE(i);
        const bool flip = high ? s < mid - 8 : s > mid + 8;
        if (!flip) continue;
        if (nh == ch) {
            ch = ch ? ch * 2 : 65536;
            int *p = (int *)realloc(halves, ch * sizeof *p);
            if (!p) { free(halves); return "out of memory"; }
            halves = p;
        }
        halves[nh++] = (int)((double)(i - last) * 2457600.0 / rate);
        last = i;
        high = !high;
    }
#undef SAMPLE
    // split at the silences; each burst starts on an unknown half, so keep
    // the alignment with the most leader bytes
    bytes_t out = { 0 };
    size_t start = 0;
    for (size_t i = 0; i <= nh; i++) {
        const bool end = i == nh || halves[i] > EXL_HALF_BIT0 * 8;
        if (!end) continue;
        const size_t len = i - start;
        if (len > 64) {
            bytes_t best = { 0 };
            size_t best_leader = 0;
            for (size_t skip = 0; skip < 4 && skip < len; skip++) {
                bytes_t b = { 0 };
                decode_halves(halves + start + skip, len - skip, &b);
                size_t leader = 0;
                for (size_t k = 0; k < b.n; k++) leader += b.p[k] == LEADER;
                if (leader > best_leader) {
                    best_leader = leader;
                    free(best.p);
                    best = b;
                } else {
                    free(b.p);
                }
            }
            size_t first = 0;
            while (first < best.n && best.p[first] != LEADER) first++;
            for (size_t k = first; k < best.n; k++) bpush(&out, best.p[k]);
            free(best.p);
        }
        start = i + 1;
    }
    free(halves);
#undef U16
#undef U32
    if (!out.n) { free(out.p); return "no Exelvision data found in the WAV"; }
    const char *err = load_k7(t, out.p, out.n);
    free(out.p);
    return err;
}

const char *exl_tape_load(exl_tape *t, const uint8_t *data, size_t size)
{
    if (size >= 4 && !memcmp(data, "RIFF", 4)) return load_wav(t, data, size);
    return load_k7(t, data, size);
}

bool exl_tape_seek_sync(exl_tape *t)
{
    for (size_t k = t->pos + (t->bit == 7 && t->half == 0 ? 0 : 1); k < t->n; k++) {
        if (t->bytes[k] == SYNC && k > 0 && t->bytes[k - 1] == LEADER) {
            t->pos = k + 1;
            t->bit = 7;
            t->half = 0;
            if (!exl_tape_at_end(t)) start_bit(t);
            return true;
        }
    }
    return false;
}

int exl_tape_next_byte(exl_tape *t)
{
    if (t->bit != 7 || t->half != 0) t->pos++;          // drop a partly played byte
    if (exl_tape_at_end(t)) return -1;
    const int v = t->bytes[t->pos++];
    t->bit = 7;
    t->half = 0;
    if (!exl_tape_at_end(t)) start_bit(t);
    return v;
}

bool exl_tape_advance(exl_tape *t, int cycles)
{
    if (exl_tape_at_end(t)) {
        t->level = true;
        return true;
    }
    t->remain -= cycles;
    while (t->remain <= 0 && !exl_tape_at_end(t)) {
        const int carry = t->remain;
        if (++t->half == 4) {
            t->half = 0;
            if (--t->bit < 0) {
                t->bit = 7;
                if (++t->pos >= t->n) {
                    t->level = true;
                    return true;
                }
            }
        }
        start_bit(t);
        t->remain += carry;
    }
    return t->level;
}

static void flush_halves(exl_tape *t)
{
    bytes_t b = { t->rec, t->nrec, t->crec };
    decode_halves(t->halves, t->nhalves, &b);
    t->rec = b.p;
    t->nrec = b.n;
    t->crec = b.c;
    t->nhalves = 0;
}

void exl_tape_record_edge(exl_tape *t, uint64_t cycle)
{
    if (t->have_edge) {
        const uint64_t half = cycle - t->last_edge;
        if (half > (uint64_t)EXL_HALF_BIT0 * 8) {       // a long silence ends a recording
            flush_halves(t);
        } else {
            if (t->nhalves == t->chalves) {
                const size_t c = t->chalves ? t->chalves * 2 : 4096;
                int *p = (int *)realloc(t->halves, c * sizeof *p);
                if (!p) return;
                t->halves = p;
                t->chalves = c;
            }
            t->halves[t->nhalves++] = (int)half;
        }
    }
    t->last_edge = cycle;
    t->have_edge = true;
}

uint8_t *exl_tape_take_recording(exl_tape *t, size_t *n)
{
    if (t->nhalves) {
        // the last half has no closing edge: as the one before it
        exl_tape_record_edge(t, t->last_edge + (uint64_t)t->halves[t->nhalves - 1]);
        flush_halves(t);
    }
    t->have_edge = false;
    uint8_t *out = t->rec;
    *n = t->nrec;
    t->rec = NULL;
    t->nrec = t->crec = 0;
    if (!*n) { free(out); return NULL; }
    return out;
}
