// zx_inflate.c - small DEFLATE decoder (RFC 1951), puff-style canonical
// Huffman decoding. See zx_inflate.h.
#include "zx_inflate.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t out_len, out_cap, out_max;
    bool fail;
} zstate;

typedef struct {
    short count[16];
    short symbol[288];
} huff;

static int bits(zstate *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) { s->fail = true; return 0; }
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static bool put(zstate *s, uint8_t b)
{
    if (s->out_len >= s->out_cap) {
        if (s->out_cap >= s->out_max) { s->fail = true; return false; }
        size_t cap = s->out_cap ? s->out_cap * 2 : 4096;
        if (cap > s->out_max) cap = s->out_max;
        uint8_t *n = realloc(s->out, cap);
        if (!n) { s->fail = true; return false; }
        s->out = n;
        s->out_cap = cap;
    }
    s->out[s->out_len++] = b;
    return true;
}

static int decode(zstate *s, const huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1);
        if (s->fail) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->fail = true;
    return -1;
}

static int construct(huff *h, const short *length, int n)
{
    short offs[16];
    for (int len = 0; len < 16; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static const short kLbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short kLext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short kDbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short kDext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool codes(zstate *s, const huff *lencode, const huff *distcode)
{
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) return false;
        if (sym < 256) {
            if (!put(s, (uint8_t)sym)) return false;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int len = kLbase[sym] + bits(s, kLext[sym]);
            int dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return false;
            size_t dist = (size_t)(kDbase[dsym] + bits(s, kDext[dsym]));
            if (s->fail || dist > s->out_len) return false;
            while (len--) {
                if (!put(s, s->out[s->out_len - dist])) return false;
            }
        }
    }
}

static bool stored(zstate *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->in_pos + 4 > s->in_len) return false;
    unsigned len = s->in[s->in_pos] | (s->in[s->in_pos + 1] << 8);
    unsigned nlen = s->in[s->in_pos + 2] | (s->in[s->in_pos + 3] << 8);
    s->in_pos += 4;
    if (len != (~nlen & 0xffff) || s->in_pos + len > s->in_len) return false;
    while (len--) if (!put(s, s->in[s->in_pos++])) return false;
    return true;
}

static bool fixed(zstate *s)
{
    static huff lencode, distcode;
    static bool built;
    if (!built) {
        short lengths[288];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < 288; sym++) lengths[sym] = 8;
        construct(&lencode, lengths, 288);
        for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
        construct(&distcode, lengths, 30);
        built = true;
    }
    return codes(s, &lencode, &distcode);
}

static bool dynamic(zstate *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    huff lencode, distcode;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->fail || nlen > 286 || ndist > 30) return false;
    int index;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                symbol = 3 + bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + bits(s, 3);
            } else {
                symbol = 11 + bits(s, 7);
            }
            if (index + symbol > nlen + ndist) return false;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return false;
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
    return codes(s, &lencode, &distcode);
}

static bool run(zstate *s)
{
    int last;
    do {
        last = bits(s, 1);
        int type = bits(s, 2);
        if (s->fail) return false;
        bool ok = type == 0 ? stored(s) : type == 1 ? fixed(s) : type == 2 ? dynamic(s) : false;
        if (!ok || s->fail) return false;
    } while (!last);
    return true;
}

uint8_t *zx_inflate(const uint8_t *src, size_t src_len, size_t expected, size_t max, size_t *out_len)
{
    if (!max) max = (size_t)64 << 20;
    for (int pass = 0; pass < 2; pass++) {
        size_t skip = 0;
        // zlib header: CMF/FLG with deflate method and a valid check
        if (pass == 0) {
            if (src_len < 2 || (src[0] & 0x0f) != 8 || ((src[0] << 8) | src[1]) % 31 != 0) continue;
            skip = 2;
        }
        zstate s;
        memset(&s, 0, sizeof s);
        s.in = src + skip;
        s.in_len = src_len - skip;
        s.out_max = max;
        if (expected) {
            s.out = malloc(expected ? expected : 1);
            if (!s.out) return NULL;
            s.out_cap = expected;
        }
        if (run(&s)) {
            if (out_len) *out_len = s.out_len;
            if (!s.out) s.out = malloc(1);
            return s.out;
        }
        free(s.out);
    }
    return NULL;
}
