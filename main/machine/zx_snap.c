// zx_snap.c - ZX Spectrum snapshots, C11 port of dsp-cpp's
// machine/spectrum_snap.cpp. See zx_snap.h.
#include "zx_snap.h"
#include <string.h>
#include <ctype.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

#define SNA48 (27 + 0xc000)         // 49179
#define SNA128_MIN (SNA48 + 4)      // PC / 7FFD / TR-DOS tail

static void zero_banks(uint8_t *const banks[8])
{
    for (int b = 0; b < 8; b++)
        if (banks[b]) memset(banks[b], 0, 0x4000);
}

static void put(uint8_t *dst, const uint8_t *src)
{
    if (dst) memcpy(dst, src, 0x4000);
}

// Classic .Z80 RLE: ED ED xx yy => yy repeated xx times. Optional end marker
// 00 ED ED 00. dst NULL: only checks the block.
static bool decompress_z80_block(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len,
                                 bool expect_end_marker, const char **error)
{
    size_t si = 0, di = 0;
    while (si < src_len && di < dst_len) {
        if (expect_end_marker && si + 4 <= src_len && src[si] == 0x00 && src[si + 1] == 0xed &&
            src[si + 2] == 0xed && src[si + 3] == 0x00) {
            si += 4;
            break;
        }
        if (si + 4 <= src_len && src[si] == 0xed && src[si + 1] == 0xed) {
            const uint8_t count = src[si + 2];
            const uint8_t value = src[si + 3];
            si += 4;
            for (uint8_t n = 0; n < count && di < dst_len; ++n) {
                if (dst) dst[di] = value;
                di++;
            }
            continue;
        }
        if (dst) dst[di] = src[si];
        di++;
        si++;
    }
    if (di != dst_len) {
        if (error) *error = "Z80 block decompressed to wrong size";
        return false;
    }
    return true;
}

static void regs_from_z80_header(zx_snap *out, const uint8_t *h)
{
    out->a = h[0];
    out->f = h[1];
    out->c = h[2];
    out->b = h[3];
    out->l = h[4];
    out->h = h[5];
    // PC at 6-7 for v1; zero for v2/v3
    out->sp = rd16(h + 8);
    out->i = h[10];
    out->r = (uint8_t)((h[11] & 0x7f) | ((h[12] & 1) << 7));
    uint8_t flags = h[12];
    if (flags == 0xff) flags = 1;
    out->border = (uint8_t)((flags >> 1) & 7);
    out->e = h[13];
    out->d = h[14];
    out->c2 = h[15];
    out->b2 = h[16];
    out->e2 = h[17];
    out->d2 = h[18];
    out->l2 = h[19];
    out->h2 = h[20];
    out->a2 = h[21];
    out->f2 = h[22];
    out->iy = rd16(h + 23);
    out->ix = rd16(h + 25);
    out->iff1 = h[27] != 0;
    out->iff2 = h[28] != 0;
    out->im = (uint8_t)(h[29] & 3);
}

static bool from_sna(const uint8_t *data, size_t size, bool host48, uint8_t *const banks[8], zx_snap *out,
                     const char **error)
{
    if (!data || size < SNA48) {
        if (error) *error = "SNA too small";
        return false;
    }
    memset(out, 0, sizeof *out);
    const uint8_t *h = data;
    out->i = h[0];
    out->l2 = h[1]; out->h2 = h[2]; out->e2 = h[3]; out->d2 = h[4];
    out->c2 = h[5]; out->b2 = h[6]; out->f2 = h[7]; out->a2 = h[8];
    out->l = h[9]; out->h = h[10]; out->e = h[11]; out->d = h[12];
    out->c = h[13]; out->b = h[14];
    out->iy = rd16(h + 15);
    out->ix = rd16(h + 17);
    // Byte 19 bit 2 = IFF2 (EI/DI). RETN semantics: IFF1 <- IFF2.
    out->iff2 = (h[19] & 4) != 0;
    out->iff1 = out->iff2;
    out->r = h[20];
    out->f = h[21];
    out->a = h[22];
    out->sp = rd16(h + 23);
    out->im = (uint8_t)(h[25] & 3);
    out->border = (uint8_t)(h[26] & 7);

    if (size >= SNA128_MIN) {
        // 128K: banks 5, 2, then the paged one (7FFD bits 0-2, even when it
        // is 5 or 2), then the remaining banks in ascending order.
        out->is_128 = true;
        const uint8_t *tail = data + SNA48;
        out->pc = rd16(tail);
        out->port_7ffd = tail[2];
        out->trdos_paged = tail[3] != 0;
        const int paged = out->port_7ffd & 7;
        zero_banks(banks);
        if (host48) {
            put(banks[5], data + 27);
            put(banks[2], data + 27 + 0x4000);
            put(banks[0], data + 27 + 0x8000);
            return true;
        }
        put(banks[5], data + 27);
        put(banks[2], data + 27 + 0x4000);
        put(banks[paged], data + 27 + 0x8000);
        size_t off = SNA128_MIN;
        for (int b = 0; b < 8; ++b) {
            if (b == 5 || b == 2 || b == paged) continue;
            if (off + 0x4000 > size) break;
            put(banks[b], data + off);
            off += 0x4000;
        }
        return true;
    }

    // PC is on the stack at SP (Mirage Microdriver RETN).
    const uint16_t sp = out->sp;
    if (sp < 0x4000 || sp == 0xffff) {
        if (error) *error = "SNA SP out of range";
        return false;
    }
    const uint8_t *ram = data + 27;
    out->pc = (uint16_t)(ram[sp - 0x4000] | (ram[sp - 0x4000 + 1] << 8));
    out->sp = (uint16_t)(sp + 2);
    zero_banks(banks);
    put(banks[5], ram);
    put(banks[2], ram + 0x4000);
    put(banks[0], ram + 0x8000);
    return true;
}

// pass 0 checks everything, pass 1 writes the banks
static bool z80_pages(const uint8_t *data, size_t size, size_t off, bool host48, uint8_t *const banks[8],
                      zx_snap *out, bool write, const char **error)
{
    while (off + 3 <= size) {
        const uint16_t blk_len = rd16(data + off);
        const uint8_t page = data[off + 2];
        off += 3;
        uint8_t *dst = NULL;
        if (write) {
            if (out->is_128) {
                // Pages 3..10 => banks 0..7 (a 48K host keeps 5 / 2 / 0)
                if (page >= 3 && page <= 10) {
                    const int b = page - 3;
                    dst = (!host48 || b == 5 || b == 2 || b == 0) ? banks[b] : NULL;
                }
            } else {
                switch (page) {
                case 8: dst = banks[5]; break;
                case 4: dst = banks[2]; break;
                case 5: dst = banks[0]; break;
                default: break;
                }
            }
        }
        if (blk_len == 0xffff) {
            if (off + 0x4000 > size) {
                if (error) *error = "Z80 uncompressed page truncated";
                return false;
            }
            if (dst) memcpy(dst, data + off, 0x4000);
            off += 0x4000;
        } else {
            if (off + blk_len > size) {
                if (error) *error = "Z80 compressed page truncated";
                return false;
            }
            if (!decompress_z80_block(data + off, blk_len, dst, 0x4000, false, error)) return false;
            off += blk_len;
        }
    }
    return true;
}

static bool from_z80(const uint8_t *data, size_t size, bool host48, uint8_t *const banks[8], zx_snap *out,
                     const char **error)
{
    if (!data || size < 30) {
        if (error) *error = "Z80 snapshot too small";
        return false;
    }
    memset(out, 0, sizeof *out);
    regs_from_z80_header(out, data);
    const uint16_t pc_v1 = rd16(data + 6);
    const bool compressed_v1 = (data[12] != 0xff) && ((data[12] & 0x20) != 0);

    if (pc_v1 != 0) {
        // Version 1: 48K, one block
        out->pc = pc_v1;
        out->is_128 = false;
        const uint8_t *payload = data + 30;
        const size_t plen = size - 30;
        if (!compressed_v1) {
            if (plen < 0xc000) {
                if (error) *error = "Z80 v1 uncompressed RAM short";
                return false;
            }
            zero_banks(banks);
            put(banks[5], payload);
            put(banks[2], payload + 0x4000);
            put(banks[0], payload + 0x8000);
            return true;
        }
        if (!decompress_z80_block(payload, plen, NULL, 0xc000, true, error)) return false;
        // decompress the three 16 KB thirds into their banks (a 48K stream)
        zero_banks(banks);
        uint8_t *dst[3] = { banks[5], banks[2], banks[0] };
        size_t si = 0, di = 0;
        while (si < plen && di < 0xc000) {
            if (si + 4 <= plen && payload[si] == 0x00 && payload[si + 1] == 0xed && payload[si + 2] == 0xed &&
                payload[si + 3] == 0x00)
                break;
            uint8_t value, count = 1;
            if (si + 4 <= plen && payload[si] == 0xed && payload[si + 1] == 0xed) {
                count = payload[si + 2];
                value = payload[si + 3];
                si += 4;
            } else {
                value = payload[si++];
            }
            for (uint8_t n = 0; n < count && di < 0xc000; ++n, ++di) {
                uint8_t *p = dst[di >> 14];
                if (p) p[di & 0x3fff] = value;
            }
        }
        return true;
    }

    // Version 2 / 3
    if (size < 32) {
        if (error) *error = "Z80 v2/v3 truncated header";
        return false;
    }
    const size_t ext_len = rd16(data + 30);
    if (size < 32 + ext_len || ext_len < 3) {
        if (error) *error = "Z80 extended header truncated";
        return false;
    }
    const uint8_t *ext = data + 32;
    out->pc = rd16(ext);
    const uint8_t hw = ext[2];
    // v2: 0/1 = 48K, 3/4 = 128K. v3: 0/1/3 = 48K, 4/5/6 = 128K; and the
    // later ones (+3, Pentagon, Scorpion, +2, +2A), which dsp-cpp reads as 48K.
    const bool v3 = ext_len >= 54;
    if (v3) out->is_128 = hw >= 4 && hw != 14 && hw != 15 && hw < 128;
    else out->is_128 = hw == 3 || hw == 4;
    if (ext_len >= 6) out->port_7ffd = ext[3];
    if (ext_len >= 8) out->ay_used = (ext[5] & 0x04) != 0;
    if (ext_len >= 9) out->ay_latch = ext[6];
    if (ext_len >= 25) memcpy(out->ay_regs, ext + 7, 16);
    if (ext_len >= 55) out->port_1ffd = ext[54];

    const size_t off = 32 + ext_len;
    if (!z80_pages(data, size, off, host48, banks, out, false, error)) return false;
    zero_banks(banks);
    return z80_pages(data, size, off, host48, banks, out, true, error);
}

static bool has_ext(const char *name, const char *ext)
{
    if (!name) return false;
    const size_t n = strlen(name), m = strlen(ext);
    for (size_t i = 0; i + m <= n; i++) {
        size_t k = 0;
        while (k < m && tolower((unsigned char)name[i + k]) == ext[k]) k++;
        if (k == m) return true;
    }
    return false;
}

bool zx_snap_load(const uint8_t *data, size_t size, const char *ext, bool host_48k,
                  uint8_t *const banks[8], zx_snap *out, const char **error)
{
    if (has_ext(ext, "sna")) return from_sna(data, size, host_48k, banks, out, error);
    if (has_ext(ext, "z80")) return from_z80(data, size, host_48k, banks, out, error);
    // Autodetect classic SNA sizes (48K = 49179, 128K = 131103 / 147487).
    if (size == 49179 || size == 131103 || size == 147487)
        if (from_sna(data, size, host_48k, banks, out, error)) return true;
    return from_z80(data, size, host_48k, banks, out, error);
}
