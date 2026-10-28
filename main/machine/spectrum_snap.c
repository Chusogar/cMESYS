// spectrum_snap.c - ZX Spectrum .sna loader. See spectrum_snap.h.
#include "spectrum_snap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNA_48_SIZE   (27 + SPECTRUM_SNAP_RAM48)   /* 49179 */
#define SNA_128_MIN   (SNA_48_SIZE + 4)

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

void spectrum_snap_init(spectrum_snap *s)
{
    memset(s, 0, sizeof *s);
    s->border = 7;
}

void spectrum_snap_free(spectrum_snap *s)
{
    free(s->owned);
    memset(s, 0, sizeof *s);
    s->border = 7;
}

static bool alloc_snap(spectrum_snap *s, bool is_128)
{
    /* One slab: ram48 (48K) + optionally 8 banks (128K). For 48K, banks
     * alias slices of ram48. For 128K, banks have their own pages and
     * ram48 is a convenience view of banks 5/2/paged. */
    size_t need = SPECTRUM_SNAP_RAM48;
    if (is_128) need += SPECTRUM_SNAP_BANKS * SPECTRUM_SNAP_BANK;
    uint8_t *mem = calloc(1, need);
    if (!mem) return false;
    s->owned = mem;
    s->owned_size = need;
    s->ram48 = mem;
    if (is_128) {
        uint8_t *base = mem + SPECTRUM_SNAP_RAM48;
        for (int i = 0; i < SPECTRUM_SNAP_BANKS; i++)
            s->banks[i] = base + (size_t)i * SPECTRUM_SNAP_BANK;
    } else {
        /* 48K machine layout mirrored into banks 5 / 2 / 0. */
        s->banks[5] = s->ram48;
        s->banks[2] = s->ram48 + 0x4000;
        s->banks[0] = s->ram48 + 0x8000;
        for (int i = 1; i < SPECTRUM_SNAP_BANKS; i++) {
            if (i == 2 || i == 5) continue;
            s->banks[i] = NULL;
        }
    }
    s->is_128 = is_128;
    return true;
}

bool spectrum_snap_load_mem(spectrum_snap *s, const uint8_t *data, size_t size)
{
    spectrum_snap_free(s);
    spectrum_snap_init(s);
    if (!data || size < SNA_48_SIZE) return false;

    const uint8_t *h = data;
    bool is_128 = size >= SNA_128_MIN;
    if (!alloc_snap(s, is_128)) return false;

    s->i = h[0];
    s->l2 = h[1];
    s->h2 = h[2];
    s->e2 = h[3];
    s->d2 = h[4];
    s->c2 = h[5];
    s->b2 = h[6];
    s->f2 = h[7];
    s->a2 = h[8];
    s->l = h[9];
    s->h = h[10];
    s->e = h[11];
    s->d = h[12];
    s->c = h[13];
    s->b = h[14];
    s->iy = rd16(h + 15);
    s->ix = rd16(h + 17);
    s->iff2 = (h[19] & 4) != 0;
    s->iff1 = s->iff2;
    s->r = h[20];
    s->f = h[21];
    s->a = h[22];
    s->sp = rd16(h + 23);
    s->im = (uint8_t)(h[25] & 3);
    s->border = (uint8_t)(h[26] & 7);

    if (is_128) {
        const uint8_t *tail = data + SNA_48_SIZE;
        s->pc = rd16(tail);
        s->port_7ffd = tail[2];
        s->trdos_paged = tail[3] != 0;
        int paged = (int)(s->port_7ffd & 7);

        memcpy(s->banks[5], data + 27, SPECTRUM_SNAP_BANK);
        memcpy(s->banks[2], data + 27 + SPECTRUM_SNAP_BANK, SPECTRUM_SNAP_BANK);
        memcpy(s->banks[paged], data + 27 + 2 * SPECTRUM_SNAP_BANK, SPECTRUM_SNAP_BANK);

        size_t off = SNA_128_MIN;
        for (int b = 0; b < SPECTRUM_SNAP_BANKS; b++) {
            if (b == 5 || b == 2 || b == paged) continue;
            if (off + SPECTRUM_SNAP_BANK > size) break;
            memcpy(s->banks[b], data + off, SPECTRUM_SNAP_BANK);
            off += SPECTRUM_SNAP_BANK;
        }

        memcpy(s->ram48, s->banks[5], SPECTRUM_SNAP_BANK);
        memcpy(s->ram48 + 0x4000, s->banks[2], SPECTRUM_SNAP_BANK);
        memcpy(s->ram48 + 0x8000, s->banks[paged], SPECTRUM_SNAP_BANK);
    } else {
        memcpy(s->ram48, data + 27, SPECTRUM_SNAP_RAM48);
        /* PC is on the stack at SP (Mirage Microdriver RETN). */
        uint16_t sp = s->sp;
        if (sp < 0x4000 || (uint32_t)sp + 1 > 0xffff) {
            spectrum_snap_free(s);
            return false;
        }
        s->pc = rd16(s->ram48 + (sp - 0x4000));
        s->sp = (uint16_t)(sp + 2);
    }
    return true;
}

bool spectrum_snap_load_file(spectrum_snap *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) { free(buf); return false; }
    bool ok = spectrum_snap_load_mem(s, buf, n);
    free(buf);
    return ok;
}
