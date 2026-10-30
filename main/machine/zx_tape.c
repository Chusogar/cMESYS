// zx_tape.c - tape player, C11 port of dsp-cpp's machine/tape_tzx.cpp. See
// zx_tape.h.
#include "zx_tape.h"
#include "zx_inflate.h"
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ZT_HOT IRAM_ATTR
#else
#define ZT_HOT
#endif

// The file is read through its pages (zx_pbuf.h): every "pointer" into it
// is an offset, read with these, P being the pbuf in scope.
#define B(o) zx_pb_byte(P, (uint32_t)(o))
#define R16(o) zx_pb_rd16(P, (uint32_t)(o))
#define R24(o) zx_pb_rd24(P, (uint32_t)(o))
#define R32(o) zx_pb_rd32(P, (uint32_t)(o))

static uint32_t pause_tstates(uint16_t ms) { return ms ? (uint32_t)ms * 3500u : 0; }
static uint16_t alphabet_size(uint8_t raw) { return raw == 0 ? 256 : raw; }

static uint8_t bits_for_alphabet(uint16_t asd)
{
    if (asd <= 2) return 1;
    if (asd <= 4) return 2;
    if (asd <= 8) return 3;
    if (asd <= 16) return 4;
    if (asd <= 32) return 5;
    if (asd <= 64) return 6;
    if (asd <= 128) return 7;
    return 8;
}

// ---------------------------------------------------------------------------
// blocks
// ---------------------------------------------------------------------------
static zt_block *new_block(zx_tape *t, uint8_t type)
{
    if (t->nblocks == t->cap) {
        int cap = t->cap ? t->cap + t->cap / 2 : 32;
        zt_block *n = realloc(t->blocks, sizeof(zt_block) * (size_t)cap);
        if (!n) {
            t->oom = true;
            return NULL;
        }
        t->blocks = n;
        t->cap = cap;
    }
    zt_block *b = &t->blocks[t->nblocks++];
    memset(b, 0, sizeof *b);
    b->type = type;
    b->src = &t->pb;
    b->pilot = 2168; b->sync1 = 667; b->sync2 = 735;
    b->zero = 855; b->one = 1710; b->pilot_pulses = 8063;
    b->used_bits = 8;
    b->pause_ms = 1000;
    return b;
}

// Symbols of a 0x19 block (pilot or data table): number, flags and pulses.
// TZX pulse lists end at the first zero (as dsp-cpp's parser stores them).
static uint16_t sym_count(const zt_block *b, bool pilot)
{
    if (b->pzx) return pilot ? 0 : 2;
    if (pilot) return b->totp ? alphabet_size((uint8_t)b->asp) : 0;
    return b->totd ? alphabet_size((uint8_t)b->asd) : 0;
}

static uint32_t sym_def(const zt_block *b, bool pilot, uint32_t i)
{
    const uint8_t np = pilot ? b->npp : b->npd;
    return (pilot ? b->pilot_defs : b->data_defs) + i * (1 + 2u * np);
}

static uint32_t sym_size(const zt_block *b, bool pilot, uint32_t i)
{
    if (b->pzx) return b->pzx_n[i & 1];
    const zx_pbuf *P = b->src;
    const uint32_t d = sym_def(b, pilot, i) + 1;
    const uint8_t np = pilot ? b->npp : b->npd;
    uint32_t n = 0;
    while (n < np && R16(d + 2 * n)) n++;
    return n;
}

static uint16_t sym_pulse(const zt_block *b, bool pilot, uint32_t i, uint32_t k)
{
    const zx_pbuf *P = b->src;
    if (b->pzx) return R16(b->pzx_s[i & 1] + 2 * k);
    return R16(sym_def(b, pilot, i) + 1 + 2 * k);
}

static uint8_t sym_flags(const zt_block *b, bool pilot, uint32_t i)
{
    const zx_pbuf *P = b->src;
    return b->pzx ? 0 : B(sym_def(b, pilot, i));
}

// ---------------------------------------------------------------------------
// pulse lists
// ---------------------------------------------------------------------------
static void pl_reset(zx_tape *t)
{
    t->pl_pos = t->pl_rep = t->pl_sym_pulse = 0;
    t->pl_cur = 0;
}

// Next pulse of the block's list; false at the end.
static bool pl_next(zx_tape *t, const zt_block *b, uint32_t *out)
{
    const zx_pbuf *P = b->src;
    switch (b->plist) {
    case ZP_RAW16:
        if (t->pl_pos >= b->pulses_len) return false;
        *out = R16(b->pulses + 2 * t->pl_pos++);
        return true;
    case ZP_PZX:
        for (;;) {
            if (t->pl_rep) {
                t->pl_rep--;
                *out = t->pl_cur;
                return true;
            }
            uint32_t i = t->pl_pos;
            if (i + 2 > b->pulses_len) return false;
            uint32_t count = 1, duration = R16(b->pulses + i);
            i += 2;
            if (duration > 0x8000) {
                count = duration & 0x7fff;
                if (i + 2 > b->pulses_len) return false;
                duration = R16(b->pulses + i);
                i += 2;
            }
            if (duration & 0x8000) {
                if (i + 2 > b->pulses_len) return false;
                duration = ((duration & 0x7fff) << 16) | R16(b->pulses + i);
                i += 2;
            }
            t->pl_pos = i;
            if (duration == 0) continue;
            t->pl_cur = duration;
            t->pl_rep = count;
        }
    case ZP_GENPILOT:
        for (;;) {
            if (t->pl_rep) {
                const uint8_t si = B(b->pulses + 3 * (t->pl_pos - 1));
                const uint32_t n = sym_size(b, true, si);
                if (t->pl_sym_pulse < n) {
                    *out = sym_pulse(b, true, si, t->pl_sym_pulse++);
                    return true;
                }
                t->pl_sym_pulse = 0;
                t->pl_rep--;
                continue;
            }
            if (t->pl_pos >= b->pulses_len) return false;
            const uint32_t e = b->pulses + 3 * t->pl_pos++;
            t->pl_rep = (B(e) < sym_count(b, true)) ? R16(e + 1) : 0;
            t->pl_sym_pulse = 0;
        }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// parsers
// ---------------------------------------------------------------------------
// payload: [off, off+len) of P
static bool parse_csw_payload(zx_tape *t, const zx_pbuf *P, uint32_t sample_rate, uint8_t compression,
                              uint8_t polarity, uint32_t off, uint32_t len, const char **error)
{
    if (sample_rate == 0) sample_rate = 44100;
    const zx_pbuf *RP = P;
    uint32_t rle = off, rle_len = len;
    if (compression == 2) {         // zlib-wrapped RLE: inflated into a buffer of its own
        uint8_t *src = zx_pb_flatten(P, off, len);
        size_t got = 0;
        uint8_t *dec = src ? zx_inflate(src, len, 0, 0, &got) : NULL;
        free(src);
        if (!dec) {
            if (error) *error = src ? "CSW zlib decompress failed" : "out of memory for the CSW";
            return false;
        }
        zx_pbuf **n = realloc(t->extra, sizeof(zx_pbuf *) * (size_t)(t->nextra + 1));
        zx_pbuf *x = malloc(sizeof *x);
        if (n) t->extra = n;
        if (!n || !x) { free(dec); free(x); return false; }
        zx_pb_take_flat(x, dec, got);
        t->extra[t->nextra++] = x;
        RP = x;
        rle = 0;
        rle_len = (uint32_t)got;
    }
    // Count the samples (descomprimir_csw in tap_tzx.pas: runs of `count`
    // samples at the current polarity, flipped after each run).
    uint64_t samples = 0;
    {
        const zx_pbuf *P = RP;
        uint32_t i = 0;
        while (i < rle_len) {
            uint32_t c = B(rle + i++);
            if (c == 0) {
                if (i + 4 > rle_len) break;
                c = R32(rle + i);
                i += 4;
            }
            samples += c;
        }
    }
    if (samples == 0) {
        if (error) *error = "CSW produced no samples";
        return false;
    }
    zt_block *b = new_block(t, ZT_CSW);
    if (!b) return false;
    b->src = RP;
    // T-states per sample at 3.5 MHz (Direct Recording, $15)
    b->sample_tstates = (uint32_t)((3500000.0 + sample_rate / 2) / sample_rate);
    if (b->sample_tstates == 0) b->sample_tstates = 1;
    b->one = (uint16_t)(b->sample_tstates < 65535 ? b->sample_tstates : 65535);
    b->used_bits = 8;
    b->pause_ms = 0;
    b->rle = rle;
    b->rle_len = rle_len;
    b->csw_polarity = polarity & 1;
    // the expanded bit stream ends on a whole byte (padded with 0 samples)
    b->csw_samples = (uint32_t)((samples + 7) & ~(uint64_t)7);
    return true;
}

static bool parse_csw(zx_tape *t, const zx_pbuf *P, size_t size, const char **error)
{
    // Header: "Compressed Square Wave" (22) + 0x1A + major + minor => 25 bytes
    if (size < 25 || !zx_pb_eq(P, 0, "Compressed Square Wave", 22)) {
        if (error) *error = "not a CSW file";
        return false;
    }
    const uint8_t major = B(23);
    uint32_t sample_rate = 44100;
    uint8_t polarity = 0, compression = 1;
    uint32_t payload;
    uint32_t payload_len;
    if (major == 1) {
        if (size < 32) { if (error) *error = "CSW v1 truncated header"; return false; }
        sample_rate = R16(25);
        compression = 1;        // v1 is always RLE in practice
        polarity = B(28) & 1;
        payload = 32;
        payload_len = (uint32_t)size - 32;
    } else if (major == 2) {
        if (size < 25 + 27) { if (error) *error = "CSW v2 truncated header"; return false; }
        const uint32_t p = 25;
        sample_rate = R32(p);
        compression = B(p + 8);
        polarity = B(p + 9) & 1;
        const size_t hdr_skip = 25 + 27 + B(p + 10);
        if (size < hdr_skip) { if (error) *error = "CSW v2 header extension past EOF"; return false; }
        payload = (uint32_t)hdr_skip;
        payload_len = (uint32_t)(size - hdr_skip);
    } else {
        if (error) *error = "unsupported CSW version";
        return false;
    }
    return parse_csw_payload(t, P, sample_rate, compression, polarity, payload, payload_len, error);
}

static bool parse_tzx(zx_tape *t, const zx_pbuf *P, size_t size, const char **error)
{
    if (size < 8) {
        if (error) *error = "file too small";
        return false;
    }
    if (!zx_pb_eq(P, 0, "ZXTape!", 7)) {      // plain TAP
        size_t pos = 0;
        while (pos + 2 <= size) {
            const uint16_t len = R16(pos);
            pos += 2;
            if (len == 0 || pos + len > size) break;
            zt_block *b = new_block(t, ZT_STANDARD);
            if (!b) return false;
            b->src = P;
            b->pilot_pulses = (B(pos) & 0x80) ? 3223 : 8063;
            b->pause_ms = 1000;
            b->data = pos;
            b->data_len = len;
            pos += len;
        }
        return t->nblocks > 0;
    }

    size_t pos = 10;
#define NEED(n) do { if (pos + (size_t)(n) > size) { if (error) *error = "truncated TZX block"; return false; } } while (0)
    while (pos < size) {
        const uint8_t id = B(pos++);
        zt_block *b;
        switch (id) {
        case 0x10: {
            NEED(4);
            uint16_t pause = R16(pos), len = R16(pos + 2);
            pos += 4;
            NEED(len);
            if (!(b = new_block(t, id))) return false;
            b->pause_ms = pause;
            b->pilot_pulses = (len && (B(pos) & 0x80)) ? 3223 : 8063;
            b->data = pos;
            b->data_len = len;
            pos += len;
            break;
        }
        case 0x11: {
            NEED(18);
            if (!(b = new_block(t, id))) return false;
            b->pilot = R16(pos); b->sync1 = R16(pos + 2); b->sync2 = R16(pos + 4);
            b->zero = R16(pos + 6); b->one = R16(pos + 8); b->pilot_pulses = R16(pos + 10);
            b->used_bits = B(pos + 12);
            b->pause_ms = R16(pos + 13);
            const uint32_t len = R24(pos + 15);
            pos += 18;
            NEED(len);
            b->data = pos;
            b->data_len = len;
            pos += len;
            break;
        }
        case 0x12:
            NEED(4);
            if (!(b = new_block(t, id))) return false;
            b->pilot = R16(pos);
            b->pilot_pulses = R16(pos + 2);
            b->pause_ms = 0;            // Pure Tone has no pause field
            pos += 4;
            break;
        case 0x13: {
            NEED(1);
            const uint8_t n = B(pos++);
            NEED((size_t)n * 2);
            if (!(b = new_block(t, id))) return false;
            b->plist = ZP_RAW16;
            b->pulses = pos;
            b->pulses_len = b->npulses = n;
            b->pause_ms = 0;
            pos += (size_t)n * 2;
            break;
        }
        case 0x14: {
            NEED(10);
            if (!(b = new_block(t, id))) return false;
            b->zero = R16(pos); b->one = R16(pos + 2);
            b->used_bits = B(pos + 4);
            b->pause_ms = R16(pos + 5);
            const uint32_t len = R24(pos + 7);
            pos += 10;
            NEED(len);
            b->pilot_pulses = 0;
            b->data = pos;
            b->data_len = len;
            pos += len;
            break;
        }
        case 0x15: {
            NEED(8);
            if (!(b = new_block(t, id))) return false;
            b->sample_tstates = R16(pos);
            b->pause_ms = R16(pos + 2);
            b->used_bits = B(pos + 4);
            const uint32_t len = R24(pos + 5);
            pos += 8;
            NEED(len);
            b->one = (uint16_t)b->sample_tstates;
            b->data = pos;
            b->data_len = len;
            pos += len;
            break;
        }
        case 0x18: {
            NEED(10);
            const uint32_t block_len = R32(pos);
            const uint32_t sample_rate = R24(pos + 6);
            const uint8_t compression = B(pos + 9);
            pos += 10;
            const size_t payload = block_len >= 10 ? block_len - 10 : 0;
            NEED(payload);
            // dsp-cpp rebuilds a CSW v2 header (polarity 0) and parses it
            if (!parse_csw_payload(t, P, sample_rate, compression, 0, (uint32_t)pos, (uint32_t)payload, error)) return false;
            pos += payload;
            break;
        }
        case 0x19: {
            NEED(18);
            if (!(b = new_block(t, id))) return false;
            b->pause_ms = R16(pos + 4);
            b->totp = R32(pos + 6);
            b->npp = B(pos + 10);
            b->asp = B(pos + 11);
            b->totd = R32(pos + 12);
            b->npd = B(pos + 16);
            b->asd = B(pos + 17);
            pos += 18;
            if (b->totp != 0) {
                const uint16_t nsym = alphabet_size((uint8_t)b->asp);
                const size_t defs = (size_t)nsym * (1 + 2u * b->npp);
                NEED(defs);
                b->pilot_defs = pos;
                pos += defs;
                NEED((size_t)b->totp * 3);
                b->plist = ZP_GENPILOT;
                b->pulses = pos;
                b->pulses_len = b->totp;
                pos += (size_t)b->totp * 3;
                // the flat pilot pulse train dsp-cpp builds: count it
                uint64_t n = 0;
                for (uint32_t i = 0; i < b->totp; i++) {
                    const uint8_t si = B(b->pulses + 3 * i);
                    if (si < nsym) {
                        if (i == 0) b->initial_level = sym_flags(b, true, si);
                        n += (uint64_t)R16(b->pulses + 3 * i + 1) * sym_size(b, true, si);
                    }
                }
                b->npulses = n > 0xffffffffu ? 0xffffffffu : (uint32_t)n;
            }
            if (b->totd != 0) {
                const uint16_t nsym = alphabet_size((uint8_t)b->asd);
                const size_t defs = (size_t)nsym * (1 + 2u * b->npd);
                NEED(defs);
                b->data_defs = pos;
                pos += defs;
                const uint8_t bps = bits_for_alphabet(nsym);
                const uint32_t data_bytes = (uint32_t)(((uint64_t)b->totd * bps + 7) / 8);
                NEED(data_bytes);
                b->data = pos;
                b->data_len = data_bytes;
                pos += data_bytes;
            }
            break;
        }
        case 0x20:
            NEED(2);
            if (!(b = new_block(t, id))) return false;
            b->pause_ms = R16(pos);
            pos += 2;
            break;
        case 0x21: case 0x30: {
            NEED(1);
            const uint8_t n = B(pos++);
            NEED(n);
            if (!new_block(t, id)) return false;
            pos += n;
            break;
        }
        case 0x22: case 0x25: case 0x27:
            if (!new_block(t, id)) return false;
            break;
        case 0x23:
            NEED(2);
            if (!(b = new_block(t, id))) return false;
            b->jump = (int16_t)R16(pos);
            pos += 2;
            break;
        case 0x24:
            NEED(2);
            if (!(b = new_block(t, id))) return false;
            b->loop_count = R16(pos);
            pos += 2;
            break;
        case 0x26: {
            NEED(2);
            const uint16_t count = R16(pos);
            pos += 2;
            NEED((size_t)count * 2);
            if (!(b = new_block(t, id))) return false;
            b->calls = pos;
            b->ncalls = count;
            pos += (size_t)count * 2;
            break;
        }
        case 0x28: {     // Select - skipped (UI only)
            NEED(2);
            const uint16_t n = R16(pos);
            pos += 2;
            NEED(n);
            pos += n;
            break;
        }
        case 0x2a:
            NEED(4);
            pos += 4;
            if (!new_block(t, id)) return false;
            break;
        case 0x2b: {
            NEED(5);
            const uint32_t n = R32(pos);
            pos += 4;
            NEED(1);
            if (!(b = new_block(t, id))) return false;
            b->signal_level = B(pos++) ? 0x40 : 0x00;
            if (n > 1) {
                NEED(n - 1);
                pos += n - 1;
            }
            break;
        }
        case 0x31: {
            NEED(2);
            pos += 1;
            const uint8_t n = B(pos++);
            NEED(n);
            pos += n;
            break;
        }
        case 0x32: {
            NEED(2);
            const uint16_t n = R16(pos);
            pos += 2;
            NEED(n);
            pos += n;
            break;
        }
        case 0x33: {
            NEED(1);
            const uint8_t n = B(pos++);
            NEED((size_t)n * 3);
            pos += (size_t)n * 3;
            break;
        }
        case 0x35: {
            NEED(20);
            pos += 16;
            const uint32_t n = R32(pos);
            pos += 4;
            NEED(n);
            pos += n;
            break;
        }
        case 0x4b: {    // MSX Kansas City: played like a turbo block
            NEED(16);
            const uint32_t blen = R32(pos);
            if (!(b = new_block(t, ZT_TURBO))) return false;
            b->pause_ms = R16(pos + 4);
            b->pilot = R16(pos + 6);
            b->pilot_pulses = R16(pos + 8);
            b->zero = R16(pos + 10);
            b->one = R16(pos + 12);
            pos += 14;
            const size_t rest = blen > 12 ? blen - 12 : 0;
            NEED(rest);
            if (rest >= 2) {
                b->used_bits = 8;
                b->data = pos + 2;
                b->data_len = (uint32_t)(rest - 2);
            }
            pos += rest;
            break;
        }
        case 0x5a:
            NEED(9);
            pos += 9;
            break;
        default:
            if (error) *error = "unsupported TZX block: stopping parse";
            return t->nblocks > 0;
        }
    }
#undef NEED
    return t->nblocks > 0;
}

static bool parse_pzx(zx_tape *t, const zx_pbuf *P, size_t size, const char **error)
{
    if (size < 8 || !zx_pb_eq(P, 0, "PZXT", 4)) {
        if (error) *error = "not a PZX file";
        return false;
    }
    size_t pos = 8;
    uint32_t tag_size = R32(4);
    if (pos + tag_size > size) {
        if (error) *error = "truncated PZX header";
        return false;
    }
    pos += tag_size;
    while (pos + 8 <= size) {
        char tag[5];
        zx_pb_copy(P, (uint32_t)pos, (uint8_t *)tag, 4);
        tag[4] = 0;
        tag_size = R32(pos + 4);
        pos += 8;
        if (pos + tag_size > size) break;
        const uint32_t body = (uint32_t)pos;
        pos += tag_size;
        zt_block *b;
        if (!strcmp(tag, "PULS")) {
            // count the pulses and find the initial level (a zero duration)
            uint32_t n = 0;
            uint8_t init_ear = 0;
            size_t i = 0;
            while (i + 2 <= tag_size) {
                uint32_t count = 1, duration = R16(body + i);
                i += 2;
                if (duration > 0x8000) {
                    count = duration & 0x7fff;
                    if (i + 2 > tag_size) break;
                    duration = R16(body + i);
                    i += 2;
                }
                if (duration & 0x8000) {
                    if (i + 2 > tag_size) break;
                    duration = ((duration & 0x7fff) << 16) | R16(body + i);
                    i += 2;
                }
                if (duration == 0) { init_ear = 1; continue; }
                n += count;
            }
            if (n) {
                if (!(b = new_block(t, ZT_PULSE_SEQ))) return false;
                b->plist = ZP_PZX;
                b->pulses = body;
                b->pulses_len = tag_size;
                b->npulses = n;
                if (init_ear) b->initial_level = 0x40;
            }
        } else if (!strcmp(tag, "PAUS")) {
            if (tag_size < 4) continue;
            const uint32_t raw = R32(body);
            const uint32_t tstates = raw & 0x7fffffffu;
            if (raw >> 31) {
                if (!(b = new_block(t, ZT_SET_LEVEL))) return false;
                b->signal_level = 0x40;
            }
            if (!(b = new_block(t, ZT_PAUSE))) return false;
            b->pause_ms = tstates / 3500u;
            if (b->pause_ms == 0 && tstates > 0) b->pause_ms = 1;
            b->sample_tstates = tstates;
        } else if (!strcmp(tag, "DATA")) {
            if (tag_size < 8) continue;
            uint32_t bit_count = R32(body);
            const uint8_t initial = (uint8_t)((bit_count >> 31) & 1);
            bit_count &= 0x7fffffffu;
            const uint16_t tail = R16(body + 4);
            uint8_t p0 = B(body + 6), p1 = B(body + 7);
            size_t i = 8;
            if (!(b = new_block(t, ZT_GENERALIZED))) return false;
            b->pzx = true;
            b->asd = 2;
            b->npd = p0 > p1 ? p0 : p1;
            b->totd = bit_count;
            b->used_bits = (bit_count % 8) ? (uint8_t)(bit_count % 8) : 8;
            if (initial) b->initial_level = 0x40;
            // (as dsp-cpp: a list cut short by the end of the tag keeps what fits)
            uint8_t k0 = 0, k1 = 0;
            b->pzx_s[0] = body + i;
            while (k0 < p0 && i + 2 <= tag_size) { k0++; i += 2; }
            b->pzx_s[1] = body + i;
            while (k1 < p1 && i + 2 <= tag_size) { k1++; i += 2; }
            b->pzx_n[0] = k0;
            b->pzx_n[1] = k1;
            const uint32_t data_bytes = (bit_count + 7) / 8;
            if (i + data_bytes <= tag_size) {
                b->data = body + i;
                b->data_len = data_bytes;
            }
            if (tail) {
                // a one-pulse sequence: the tail, as LE16 in the tag header
                if (!(b = new_block(t, ZT_PULSE_SEQ))) return false;
                b->plist = ZP_RAW16;
                b->pulses = body + 4;
                b->pulses_len = b->npulses = 1;
            }
        } else if (!strcmp(tag, "BRWS")) {
            if (!new_block(t, ZT_TEXT)) return false;
        } else if (!strcmp(tag, "STOP")) {
            uint16_t flags = tag_size >= 2 ? R16(body) : 0;
            if (flags == 1) {
                if (!new_block(t, ZT_STOP48K)) return false;
            } else {
                if (!(b = new_block(t, ZT_PAUSE))) return false;
                b->pause_ms = 0;            // stop tape
            }
        }
    }
    if (t->nblocks == 0) {
        if (error) *error = "PZX contained no playable blocks";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// player
// ---------------------------------------------------------------------------
void zt_init(zx_tape *t) { memset(t, 0, sizeof *t); }

static void clear(zx_tape *t)
{
    free(t->blocks);
    zx_pb_free(&t->pb);
    for (int i = 0; i < t->nextra; i++) {
        zx_pb_free(t->extra[i]);
        free(t->extra[i]);
    }
    free(t->extra);
    memset(t, 0, sizeof *t);
}

void zt_free(zx_tape *t) { clear(t); }

bool zt_load_pbuf(zx_tape *t, zx_pbuf *file, const char **error)
{
    clear(t);
    t->pb = *file;                  // (the tape owns it now)
    zx_pb_init(file);
    const zx_pbuf *P = &t->pb;
    const size_t size = P->size;
    bool ok = false;
    if (size == 0) ok = false;
    else if (size >= 22 && zx_pb_eq(P, 0, "Compressed Square Wave", 22)) ok = parse_csw(t, P, size, error);
    else if (size >= 4 && zx_pb_eq(P, 0, "PZXT", 4)) ok = parse_pzx(t, P, size, error);
    else ok = parse_tzx(t, P, size, error);
    t->loaded = ok && t->nblocks > 0;
    if (!t->loaded) {
        if (error && t->oom) *error = "out of memory for the tape blocks";
        if (error && !*error) *error = "no usable tape blocks";
        clear(t);
    }
    return t->loaded;
}

bool zt_load_take(zx_tape *t, uint8_t *file, size_t size, const char **error)
{
    if (!file || !size) {
        free(file);
        return false;
    }
    zx_pbuf pb;
    zx_pb_take_flat(&pb, file, size);
    return zt_load_pbuf(t, &pb, error);
}

bool zt_load(zx_tape *t, const uint8_t *data, size_t size, const char **error)
{
    if (!data || !size) return false;
    zx_pbuf pb;
    if (!zx_pb_from_mem(&pb, data, size)) {
        if (error) *error = "out of memory for the tape";
        return false;
    }
    return zt_load_pbuf(t, &pb, error);
}

static void start_block(zx_tape *t);

void zt_stop(zx_tape *t)
{
    t->playing = false;
    t->paused = false;
    t->estado = 0;
    t->estados_left = 0;
    t->index = 0;
}

static void next_block(zx_tape *t)
{
    ++t->index;
    if (t->index >= t->nblocks) {
        zt_stop(t);
        return;
    }
    start_block(t);
}

void zt_play(zx_tape *t, bool restart)
{
    if (!t->loaded || t->nblocks == 0) return;
    if (restart || !t->playing) {
        t->index = 0;
        t->level = 0;
        t->estado = 0;
        t->estados_left = 0;
        t->playing = true;
        t->paused = false;
        start_block(t);
    } else {
        t->paused = false;
        t->playing = true;
    }
}

void zt_pause(zx_tape *t)
{
    if (t->playing) t->paused = true;
}

static void start_gen_symbol(zx_tape *t, const zt_block *b, bool pilot, uint32_t si, bool apply_flags)
{
    // dsp-emulator: flags applied at block/byte start, not every intra-byte symbol.
    //   0 = keep, 1 = toggle, 2 = force low, 3 = force high
    if (apply_flags) {
        switch (sym_flags(b, pilot, si) & 3) {
        case 0: break;
        case 1: t->level ^= 0x40; break;
        case 2: t->level = 0; break;
        case 3: t->level = 0x40; break;
        }
    }
    t->sym_pulse_i = 0;
    if (sym_size(b, pilot, si) == 0) {
        t->estados_left = 1;
        return;
    }
    const uint16_t p = sym_pulse(b, pilot, si, 0);
    t->estados_left = p ? p : 1;
}

static uint32_t read_symbol_index(const zt_block *b, uint32_t symbol_i)
{
    const zx_pbuf *P = b->src;
    const uint16_t asd = alphabet_size((uint8_t)b->asd);
    const uint8_t bps = bits_for_alphabet(asd);
    if (bps == 8 && asd == 256) return symbol_i < b->data_len ? B(b->data + (symbol_i)) : 0;
    // MSB-first bit stream across data[]
    const uint32_t bit_pos = symbol_i * bps;
    uint32_t value = 0;
    for (uint8_t k = 0; k < bps; ++k) {
        const uint32_t abs_bit = bit_pos + k;
        const uint32_t byte_i = abs_bit / 8;
        const int bit = 7 - (int)(abs_bit % 8);
        uint8_t bitv = 0;
        if (byte_i < b->data_len) bitv = (uint8_t)((B(b->data + (byte_i)) >> bit) & 1);
        value = (value << 1) | bitv;
    }
    return value;
}

static void advance_gen_data(zx_tape *t, bool apply_flags)
{
    const zt_block *b = &t->blocks[t->index];
    if (sym_count(b, false) == 0 || t->data_pos >= b->totd) {
        t->estado = 5;
        const uint32_t p = pause_tstates((uint16_t)b->pause_ms);
        t->estados_left = p ? (int)p : 1;
        return;
    }
    uint32_t si = read_symbol_index(b, t->data_pos);
    if (si >= sym_count(b, false)) si = 0;
    t->current_symbol = (uint8_t)(si & 0xff);
    start_gen_symbol(t, b, false, si, apply_flags);
    t->estado = 20;
}

static int clamp_pulse(uint32_t d) { return (int)(d > 0x7fffffffu ? 0x7fffffffu : d); }

static uint8_t csw_sample_level(zx_tape *t, const zt_block *b)
{
    const zx_pbuf *P = b->src;
    // next sample of the run-length stream (0 once past its end: padding)
    while (t->csw_run == 0) {
        if (t->csw_pos >= b->rle_len) return 0;
        uint32_t c = B(b->rle + t->csw_pos++);
        if (c == 0) {
            if (t->csw_pos + 4 > b->rle_len) { t->csw_pos = b->rle_len; return 0; }
            c = R32(b->rle + t->csw_pos);
            t->csw_pos += 4;
        }
        if (t->csw_done) t->csw_level ^= 1;      // the polarity flips after every run but the first
        t->csw_done = 1;
        t->csw_run = c;
    }
    t->csw_run--;
    return t->csw_level ? 0x40 : 0;
}

static void start_block(zx_tape *t)
{
    if (t->index >= t->nblocks) {
        zt_stop(t);
        return;
    }
    const zt_block *b = &t->blocks[t->index];
    const zx_pbuf *P = b->src;
    t->data_pos = 0;
    t->pulse_count = 0;
    t->pulse_index = 0;
    t->bit_mask = 0x80;
    // used_bits applies only to the last byte
    t->last_bit_mask = 1;
    if (b->data_len == 1) {
        uint8_t ub = b->used_bits ? b->used_bits : 8;
        if (ub < 8) t->last_bit_mask = (uint8_t)(1u << (8 - ub));
    }
    t->sym_pulse_i = 0;
    t->pilot_stream_i = 0;
    t->pilot_repeat_left = 0;

    switch (b->type) {
    case ZT_STANDARD:
    case ZT_TURBO:
        t->estado = 0;
        t->estados_left = b->pilot;
        t->pulse_count = b->pilot_pulses;
        break;
    case ZT_PURE_TONE:
        t->estado = 11;
        t->estados_left = b->pilot ? b->pilot : 1;
        t->pulse_count = b->pilot_pulses ? b->pilot_pulses : 1;
        break;
    case ZT_PULSE_SEQ: {
        t->estado = 10;
        if (b->initial_level) t->level = b->initial_level;
        uint32_t d;
        pl_reset(t);
        if (b->npulses && pl_next(t, b, &d)) {
            t->estados_left = clamp_pulse(d);
            t->pulse_index = 0;
        } else {
            next_block(t);
        }
        break;
    }
    case ZT_PURE_DATA:
        t->estado = 3;
        t->data_pos = 0;
        if (b->data_len) {
            t->bit_mask = 0x80;
            if (b->data_len == 1) {
                const uint8_t ub = b->used_bits ? b->used_bits : 8;
                t->last_bit_mask = (uint8_t)(1u << (8 - ub));
            } else {
                t->last_bit_mask = 1;
            }
            t->estados_left = (B(b->data + (0)) & 0x80) ? b->one : b->zero;
        } else {
            next_block(t);
        }
        break;
    case ZT_DIRECT:
        t->estado = 15;
        if (b->data_len) {
            t->bit_mask = 0x80;
            if (b->data_len == 1) t->last_bit_mask = (uint8_t)(1u << (8 - b->used_bits));
            t->level = (B(b->data + (0)) & 0x80) ? 0x40 : 0;
            t->estados_left = (int)(b->sample_tstates ? b->sample_tstates : 1);
        } else {
            next_block(t);
        }
        break;
    case ZT_CSW:
        // a Direct Recording of the expanded samples: one level per sample
        t->estado = 16;
        t->csw_pos = 0;
        t->csw_run = 0;
        t->csw_done = 0;
        t->csw_level = b->csw_polarity;
        t->data_pos = 1;               // samples played (this one included)
        t->level = csw_sample_level(t, b);
        t->estados_left = (int)(b->sample_tstates ? b->sample_tstates : 1);
        break;
    case ZT_GENERALIZED:
        if (b->npulses) {
            // the expanded pilot pulse train: first symbol flags once
            switch (b->initial_level & 3) {
            case 0: break;
            case 1: t->level ^= 0x40; break;
            case 2: t->level = 0; break;
            case 3: t->level = 0x40; break;
            }
            uint32_t d = 1;
            pl_reset(t);
            pl_next(t, b, &d);
            t->pulse_index = 0;
            t->estado = 10;
            t->estados_left = (int)(d ? d : 1);
        } else if (b->totp && b->plist == ZP_GENPILOT && sym_count(b, true)) {
            const uint8_t si = B(b->pulses);
            t->pilot_stream_i = 0;
            t->pilot_repeat_left = b->totp >= 1 ? R16(b->pulses + 1) : 1;
            if (si < sym_count(b, true)) {
                t->current_symbol = si;
                start_gen_symbol(t, b, true, si, true);
                t->estado = 19;
            } else {
                advance_gen_data(t, true);
            }
        } else if (b->totd) {
            advance_gen_data(t, true);
        } else {
            next_block(t);
        }
        break;
    case ZT_PAUSE:
        if (b->pause_ms == 0 && b->sample_tstates == 0) {
            // "Stop the tape": dsp-cpp stops and rewinds; here the tape is
            // paused on the next block, so the next LOAD carries on from it
            // (multi-load games) instead of reading the first block again.
            if (t->index + 1 >= t->nblocks) {
                zt_stop(t);
                return;
            }
            ++t->index;
            start_block(t);
            t->paused = true;
            return;
        }
        t->estado = 5;
        if (b->sample_tstates) t->estados_left = clamp_pulse(b->sample_tstates);
        else t->estados_left = (int)pause_tstates((uint16_t)b->pause_ms);
        break;
    case ZT_SET_LEVEL:
        t->level = b->signal_level;
        next_block(t);
        break;
    case ZT_JUMP: {
        const int target = t->index + 1 + b->jump;
        if (target >= 0 && target < t->nblocks) {
            t->index = target;
            start_block(t);
        } else {
            next_block(t);
        }
        break;
    }
    case ZT_LOOP_START:
        t->loop_start = t->index + 1;
        t->loop_remain = b->loop_count;
        next_block(t);
        break;
    case ZT_LOOP_END:
        if (t->loop_remain > 1) {
            --t->loop_remain;
            t->index = t->loop_start;
            start_block(t);
        } else {
            next_block(t);
        }
        break;
    case ZT_CALL:
        if (!b->ncalls) {
            next_block(t);
            break;
        }
        // return points: pushed last first
        for (int i = b->ncalls - 1; i >= 0; --i) {
            if (t->call_sp < (int)(sizeof t->call_stack / sizeof t->call_stack[0]))
                t->call_stack[t->call_sp++] = t->index + 1 + (int16_t)R16(b->calls + 2 * i);
        }
        if (t->call_sp == 0) { next_block(t); break; }
        t->index = t->call_stack[--t->call_sp];
        start_block(t);
        break;
    case ZT_RETURN:
        if (t->call_sp == 0) {
            next_block(t);
            break;
        }
        t->index = t->call_stack[--t->call_sp];
        start_block(t);
        break;
    default:            // group start / end, text, stop-in-48K, ...
        next_block(t);
        break;
    }
}

int ZT_HOT zt_advance(zx_tape *t, int tstates)
{
    if (!t->playing || t->paused || tstates <= 0) return (t->level & 0x40) ? 1 : 0;

    while (tstates > 0 && t->playing && !t->paused) {
        if (t->estados_left > tstates) {
            t->estados_left -= tstates;
            tstates = 0;
            break;
        }
        tstates -= t->estados_left;
        t->estados_left = 0;

        if (t->index >= t->nblocks) {
            zt_stop(t);
            break;
        }
        const zt_block *b = &t->blocks[t->index];
    const zx_pbuf *P = b->src;

        switch (t->estado) {
        case 0:  // pilot
            t->level ^= 0x40;
            if (t->pulse_count > 1) {
                --t->pulse_count;
                t->estados_left = b->pilot;
            } else {
                t->estado = 1;
                t->estados_left = b->sync1 ? b->sync1 : 1;
            }
            break;
        case 1:
            t->level ^= 0x40;
            t->estado = 2;
            t->estados_left = b->sync2 ? b->sync2 : 1;
            break;
        case 2:
            t->level ^= 0x40;
            t->estado = 3;
            t->data_pos = 0;
            t->bit_mask = 0x80;
            if (!b->data_len) {
                t->estado = 5;
                t->estados_left = (int)pause_tstates((uint16_t)b->pause_ms);
                break;
            }
            if (b->data_len == 1) t->last_bit_mask = (uint8_t)(1u << (8 - b->used_bits));
            t->estados_left = (B(b->data + (0)) & 0x80) ? b->one : b->zero;
            break;
        case 3:
            t->level ^= 0x40;
            t->estado = 4;
            t->estados_left = (B(b->data + (t->data_pos)) & t->bit_mask) ? b->one : b->zero;
            break;
        case 4: {
            t->level ^= 0x40;
            if (t->bit_mask > t->last_bit_mask) {
                t->bit_mask >>= 1;
                if (t->bit_mask != 0) {
                    t->estado = 3;
                    t->estados_left = (B(b->data + (t->data_pos)) & t->bit_mask) ? b->one : b->zero;
                    break;
                }
            }
            ++t->data_pos;
            if (t->data_pos < b->data_len) {
                t->bit_mask = 0x80;
                t->last_bit_mask = (t->data_pos + 1 == b->data_len)
                                       ? (uint8_t)(1u << (8 - (b->used_bits ? b->used_bits : 8)))
                                       : (uint8_t)1;
                t->estado = 3;
                t->estados_left = (B(b->data + (t->data_pos)) & 0x80) ? b->one : b->zero;
            } else {
                t->estado = 5;
                const uint32_t p = pause_tstates((uint16_t)b->pause_ms);
                t->estados_left = p ? (int)p : 1;
            }
            break;
        }
        case 5:
            next_block(t);
            break;
        case 11:  // pure tone ($12)
            t->level ^= 0x40;
            if (t->pulse_count > 1) {
                --t->pulse_count;
                t->estados_left = b->pilot ? b->pilot : 1;
            } else {
                t->estado = 5;
                const uint32_t p = pause_tstates((uint16_t)b->pause_ms);
                t->estados_left = p ? (int)p : 1;
            }
            break;
        case 10: {  // pulse sequence (also the expanded $19 pilot)
            t->level ^= 0x40;
            ++t->pulse_index;
            uint32_t d;
            if (t->pulse_index < b->npulses && pl_next(t, b, &d)) {
                if (d == 0) d = 1;
                t->estados_left = clamp_pulse(d);
            } else if (b->type == ZT_GENERALIZED && b->totd) {
                t->data_pos = 0;
                advance_gen_data(t, true);
            } else {
                next_block(t);
            }
            break;
        }
        case 15:  // direct recording
            if (t->bit_mask > t->last_bit_mask) {
                t->bit_mask >>= 1;
                t->level = (B(b->data + (t->data_pos)) & t->bit_mask) ? 0x40 : 0;
                t->estados_left = (int)(b->sample_tstates ? b->sample_tstates : 1);
            } else {
                ++t->data_pos;
                if (t->data_pos < b->data_len) {
                    t->bit_mask = 0x80;
                    t->last_bit_mask = (t->data_pos + 1 == b->data_len)
                                           ? (uint8_t)(1u << (8 - (b->used_bits ? b->used_bits : 8)))
                                           : (uint8_t)1;
                    t->level = (B(b->data + (t->data_pos)) & 0x80) ? 0x40 : 0;
                    t->estados_left = (int)(b->sample_tstates ? b->sample_tstates : 1);
                } else {
                    t->estado = 5;
                    const uint32_t p = pause_tstates((uint16_t)b->pause_ms);
                    t->estados_left = p ? (int)p : 1;
                }
            }
            break;
        case 16:  // CSW samples
            if (t->data_pos < b->csw_samples) {
                t->data_pos++;
                t->level = csw_sample_level(t, b);
                t->estados_left = (int)(b->sample_tstates ? b->sample_tstates : 1);
            } else {
                t->estado = 5;
                const uint32_t p = pause_tstates((uint16_t)b->pause_ms);
                t->estados_left = p ? (int)p : 1;
            }
            break;
        case 19: {  // generalized pilot symbol pulses
            const bool have = t->current_symbol < sym_count(b, true);
            const uint32_t n = have ? sym_size(b, true, t->current_symbol) : 0;
            if (n == 0) {
                t->pilot_stream_i += 1;
                if (t->pilot_stream_i < b->totp) {
                    const uint8_t si = B(b->pulses + (3 * t->pilot_stream_i));
                    t->pilot_repeat_left = R16(b->pulses + 3 * t->pilot_stream_i + 1);
                    if (si < sym_count(b, true)) {
                        t->current_symbol = si;
                        start_gen_symbol(t, b, true, si, true);
                    } else {
                        t->data_pos = 0;
                        advance_gen_data(t, true);
                    }
                } else {
                    t->data_pos = 0;
                    advance_gen_data(t, true);
                }
                break;
            }
            ++t->sym_pulse_i;
            if (t->sym_pulse_i < n && sym_pulse(b, true, t->current_symbol, t->sym_pulse_i) != 0) {
                t->level ^= 0x40;
                const uint16_t p = sym_pulse(b, true, t->current_symbol, t->sym_pulse_i);
                t->estados_left = p ? p : 1;
            } else if (t->pilot_repeat_left > 1) {
                --t->pilot_repeat_left;
                t->sym_pulse_i = 0;
                t->level ^= 0x40;
                const uint16_t p = sym_pulse(b, true, t->current_symbol, 0);
                t->estados_left = p ? p : 1;
            } else {
                t->pilot_stream_i += 1;
                if (t->pilot_stream_i < b->totp) {
                    const uint8_t si = B(b->pulses + (3 * t->pilot_stream_i));
                    t->pilot_repeat_left = R16(b->pulses + 3 * t->pilot_stream_i + 1);
                    if (si < sym_count(b, true)) {
                        t->current_symbol = si;
                        start_gen_symbol(t, b, true, si, true);
                    } else {
                        t->data_pos = 0;
                        advance_gen_data(t, true);
                    }
                } else {
                    t->data_pos = 0;
                    advance_gen_data(t, true);
                }
            }
            break;
        }
        case 20: {  // generalized data
            bool need_next = false;
            const bool have = t->current_symbol < sym_count(b, false);
            const uint32_t n = have ? sym_size(b, false, t->current_symbol) : 0;
            if (n == 0) {
                need_next = true;
            } else {
                ++t->sym_pulse_i;
                if (t->sym_pulse_i < n && sym_pulse(b, false, t->current_symbol, t->sym_pulse_i) != 0) {
                    t->level ^= 0x40;
                    const uint16_t p = sym_pulse(b, false, t->current_symbol, t->sym_pulse_i);
                    t->estados_left = p ? p : 1;
                    break;
                }
                need_next = true;
            }
            if (need_next) {
                t->level ^= 0x40;      // end of symbol: always xor
                ++t->data_pos;
                if (t->data_pos >= b->totd || sym_count(b, false) == 0) {
                    advance_gen_data(t, true);
                    break;
                }
                const uint16_t asd = alphabet_size((uint8_t)b->asd);
                bool apply = true;
                if (asd <= 2) {
                    // flags only when entering a new byte
                    if (t->bit_mask > 1) {
                        t->bit_mask >>= 1;
                        apply = false;
                    } else {
                        t->bit_mask = 0x80;
                        apply = true;
                    }
                }
                advance_gen_data(t, apply);
            }
            break;
        }
        default:
            next_block(t);
            break;
        }
        if (t->estados_left <= 0) t->estados_left = 1;   // guard: zero would spin forever
    }
    return (t->level & 0x40) ? 1 : 0;
}
