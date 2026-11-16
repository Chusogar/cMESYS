// cons_util.c - helpers for the console cores and frontends: CRC-32 and
// reading a cartridge out of a .zip in memory (stored or deflated entries;
// machine/zx_inflate.c).
#include <stdlib.h>
#include <string.h>
#include "cons.h"
#include "../../../machine/zx_inflate.h"

uint32_t cons_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

static bool ext_matches(const char *name, size_t len, const char *const *exts)
{
    if (!exts) return false;
    for (int i = 0; exts[i]; i++) {
        size_t el = strlen(exts[i]);
        if (len < el) continue;
        bool same = true;
        for (size_t k = 0; k < el && same; k++) {
            char a = name[len - el + k], b = exts[i][k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            same = a == b;
        }
        if (same) return true;
    }
    return false;
}

uint8_t *cons_unzip(const uint8_t *d, size_t n, const char *const *exts, size_t *out, char *name, size_t name_len)
{
    if (n < 22 || d[0] != 'P' || d[1] != 'K' || d[2] != 3 || d[3] != 4) return NULL;
    // the end of central directory record (the last 64 KB + 22)
    size_t eocd = 0;
    for (size_t i = n - 22 + 1; i-- > 0 && n - i < 65536 + 22;) {
        if (d[i] == 'P' && d[i + 1] == 'K' && d[i + 2] == 5 && d[i + 3] == 6) { eocd = i; break; }
    }
    if (!eocd) return NULL;
    const uint32_t entries = le16(d + eocd + 10);
    size_t cd = le32(d + eocd + 16);
    size_t pick = (size_t)-1, first = (size_t)-1;
    for (uint32_t e = 0; e < entries && cd + 46 <= n; e++) {
        if (le32(d + cd) != 0x02014b50u) break;
        const size_t nl = le16(d + cd + 28), xl = le16(d + cd + 30), cl = le16(d + cd + 32);
        const uint32_t usize = le32(d + cd + 24);
        if (usize > 0 && d[cd + 46 + nl - 1] != '/') {
            if (first == (size_t)-1) first = cd;
            if (pick == (size_t)-1 && ext_matches((const char *)d + cd + 46, nl, exts)) pick = cd;
        }
        cd += 46 + nl + xl + cl;
    }
    if (pick == (size_t)-1) pick = first;
    if (pick == (size_t)-1) return NULL;
    const uint32_t method = le16(d + pick + 10), csize = le32(d + pick + 20), usize = le32(d + pick + 24);
    const size_t nl = le16(d + pick + 28), lho = le32(d + pick + 42);
    if (name && name_len) {
        size_t k = nl < name_len - 1 ? nl : name_len - 1;
        memcpy(name, d + pick + 46, k);
        name[k] = 0;
    }
    if (lho + 30 > n || le32(d + lho) != 0x04034b50u) return NULL;
    const size_t data_at = lho + 30 + le16(d + lho + 26) + le16(d + lho + 28);
    if (data_at + csize > n) return NULL;
    if (method == 0) {
        uint8_t *b = (uint8_t *)malloc(usize ? usize : 1);
        if (!b) return NULL;
        memcpy(b, d + data_at, usize);
        *out = usize;
        return b;
    }
    if (method != 8) return NULL;
    size_t got = 0;
    uint8_t *b = zx_inflate(d + data_at, csize, usize, (size_t)usize + 1, &got);
    if (!b) return NULL;
    *out = got;
    return b;
}
