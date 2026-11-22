// arc_util.c - the helpers of arc.h: ROMs (romfs), graphics decoding and
// resistor networks, as dsp-cpp's RomLoader, GfxSet and gfx.cpp.
#include "arc.h"
#include <stdlib.h>
#include <string.h>
#include "../../../rom/romfs.h"

bool arc_load(const arc_rom *r, int n, uint8_t *dst, size_t dst_size)
{
    for (int i = 0; i < n; i++) {
        if (!r[i].name) continue;
        if (r[i].offset + r[i].size > dst_size) {
            romfs_missing(arc_game.sets, r[i].name);
            return false;
        }
        bool ok = false;
        const char *s = r[i].name;
        while (!ok) {                   // the '|' alternatives, then by CRC (romfs)
            const char *bar = strchr(s, '|');
            char name[64];
            const size_t len = bar ? (size_t)(bar - s) : strlen(s);
            if (len >= sizeof name) break;
            memcpy(name, s, len);
            name[len] = 0;
            ok = romfs_get_into(arc_game.sets, name, r[i].crc, dst + r[i].offset, r[i].size);
            if (!bar) break;
            s = bar + 1;
        }
        if (!ok) return false;
    }
    return true;
}

static inline unsigned get_bit(const uint8_t *rom, size_t len, int b)
{
    const size_t i = (size_t)b >> 3;
    return i < len ? (rom[i] >> (7 - (b & 7))) & 1 : 0;
}

void arc_gfx_decode_into(const arc_layout *l, const uint8_t *rom, size_t rom_len, uint8_t *set, int first)
{
    const int sz = l->width * l->height;
    uint8_t *tmp = (l->rotate_cw || l->rotate_ccw) ? (uint8_t *)malloc((size_t)sz) : NULL;
    for (int n = 0; n < l->total; n++) {
        uint8_t *px = set + (size_t)(first + n) * (size_t)sz;
        const int base = n * l->char_increment;
        int k = 0;
        for (int y = 0; y < l->height; y++)
            for (int x = 0; x < l->width; x++) {
                unsigned v = 0;
                for (int p = 0; p < l->planes; p++) {
                    const unsigned bit = get_bit(rom, rom_len, l->plane_offsets[p] + l->y_offsets[y] + l->x_offsets[x] + base);
                    v |= bit << (l->lsb_first ? p : (l->planes - 1 - p));
                }
                px[k++] = (uint8_t)v;
            }
        if (tmp && l->rotate_cw) {
            memcpy(tmp, px, (size_t)sz);
            for (int row = 0; row < l->height; row++)
                for (int col = 0; col < l->width; col++)
                    px[row * l->width + col] = tmp[(l->height - 1 - col) * l->width + row];
        }
        if (tmp && l->rotate_ccw) {
            memcpy(tmp, px, (size_t)sz);
            for (int row = 0; row < l->height; row++)
                for (int col = 0; col < l->width; col++)
                    px[row * l->width + col] = tmp[col * l->width + (l->width - 1 - row)];
        }
    }
    free(tmp);
}

uint8_t *arc_gfx_decode(const arc_layout *l, const uint8_t *rom, size_t rom_len)
{
    uint8_t *set = (uint8_t *)calloc((size_t)l->total, (size_t)(l->width * l->height));
    if (set) arc_gfx_decode_into(l, rom, rom_len, set, 0);
    return set;
}

void arc_resistor_weights(double min_v, double max_v, double scaler, const arc_resnet *nets, int nnets,
                          double out[][8])
{
    double max_out[8] = { 0 };
    for (int net = 0; net < nnets; net++) {
        const arc_resnet *c = &nets[net];
        for (int n = 0; n < c->n; n++) {
            double r0 = c->pulldown == 0 ? 1.0 / 1e12 : 1.0 / c->pulldown;
            double r1 = c->pullup == 0 ? 1.0 / 1e12 : 1.0 / c->pullup;
            for (int j = 0; j < c->n; j++) {
                const double r = c->r[j];
                if (r == 0.0) continue;
                if (j == n) r1 += 1.0 / r;
                else r0 += 1.0 / r;
            }
            r0 = 1.0 / r0;
            r1 = 1.0 / r1;
            double v = (max_v - min_v) * r0 / (r1 + r0) + min_v;
            if (v < min_v) v = min_v;
            if (v > max_v) v = max_v;
            out[net][n] = v;
            max_out[net] += v;
        }
    }
    double scale = scaler;
    if (scaler < 0.0) {
        double m = 0.0;
        for (int net = 0; net < nnets; net++) if (max_out[net] > m) m = max_out[net];
        scale = m != 0.0 ? max_v / m : 0.0;
    }
    for (int net = 0; net < nnets; net++)
        for (int n = 0; n < nets[net].n; n++) out[net][n] *= scale;
}

int arc_combine(const double *w, int n, unsigned bits)
{
    double r = 0.5;
    for (int i = 0; i < n; i++) r += w[i] * ((bits >> i) & 1);
    return (int)(r < 255.0 ? r : 255.0);
}
