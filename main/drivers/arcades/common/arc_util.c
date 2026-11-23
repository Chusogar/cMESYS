// arc_util.c - the helpers of arc.h: ROMs (romfs), graphics decoding and
// resistor networks, as dsp-cpp's RomLoader, GfxSet and gfx.cpp.
#include "arc.h"
#include <stdlib.h>
#include <string.h>
#include "../../../rom/romfs.h"
#if defined(ARC_FLASH_SIM)
#define ARC_FLASH_CACHE 1
#include "arc_flash_sim.h"
#elif defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if !CONFIG_SPIRAM
#define ARC_FLASH_CACHE 1
#include "esp_partition.h"
#include "esp_log.h"
#endif
#endif

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

// ---------------------------------------------------------------------------
// Blobs (arc.h)

const uint16_t *(*arc_frame_row_hook)(int y);

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t arc_rom_key(const arc_rom *r, int n, uint32_t seed)
{
    uint32_t h = seed ^ 2166136261u;
    for (int i = 0; i < n; i++) {
        h = fnv(h, &r[i].crc, 4);
        h = fnv(h, &r[i].size, 4);
        h = fnv(h, &r[i].offset, 4);
    }
    return h;
}

bool arc_blob_load(arc_blob *b, const arc_rom *r, int n)
{
    // in order of offset, as the flash is written forward
    int order[64];
    if (n > 64) return false;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && r[order[j]].offset < r[order[j - 1]].offset; j--) {
            const int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (int k = 0; k < n; k++) {
        const arc_rom *e = &r[order[k]];
        if (!e->name) continue;
        if (e->offset + e->size > b->size) {
            romfs_missing(arc_game.sets, e->name);
            return false;
        }
        uint8_t *tmp = (uint8_t *)malloc(e->size);
        if (!tmp) return false;
        const arc_rom one = *e;
        arc_rom at0 = one;
        at0.offset = 0;
        const bool ok = arc_load(&at0, 1, tmp, e->size);
        if (ok) arc_blob_write(b, e->offset, tmp, e->size);
        free(tmp);
        if (!ok) return false;
    }
    return true;
}

void arc_blob_gfx(arc_blob *b, const arc_layout *l, const uint8_t *rom, size_t rom_len, int first)
{
    const int sz = l->width * l->height;
    if (b->cached || b->err) return;
    if (b->ram && !b->err && b->cur < 0 && b->index < 0) {   // RAM: in place
        arc_gfx_decode_into(l, rom, rom_len, b->ram, first);
        return;
    }
    uint8_t *px = (uint8_t *)malloc((size_t)sz);
    if (!px) { b->err = true; return; }
    arc_layout one = *l;
    one.total = 1;
    for (int n = 0; n < l->total; n++) {
        for (int p = 0; p < l->planes; p++) one.plane_offsets[p] = l->plane_offsets[p] + n * l->char_increment;
        arc_gfx_decode_into(&one, rom, rom_len, px, 0);
        arc_blob_write(b, (uint32_t)(first + n) * (uint32_t)sz, px, (uint32_t)sz);
    }
    free(px);
}

const uint8_t *arc_rom_blob(const char *name, const arc_rom *roms, int n, uint32_t size)
{
    arc_blob b;
    if (arc_blob_begin(&b, name, size, arc_rom_key(roms, n, size)) && !arc_blob_load(&b, roms, n)) b.err = true;
    return arc_blob_end(&b);
}

bool arc_fb_init(arc_fb *f, int w, int h)
{
    f->w = w;
    f->h = h;
    f->part[1] = NULL;
    f->part[0] = (uint16_t *)calloc((size_t)w * h, 2);
    if (f->part[0]) {
        f->split = h;
        return true;
    }
    f->split = h / 2;
    f->part[0] = (uint16_t *)calloc((size_t)w * f->split, 2);
    f->part[1] = (uint16_t *)calloc((size_t)w * (h - f->split), 2);
    return f->part[0] && f->part[1];
}

// in the RAM (b->index < 0): everywhere but the CYD, or with no arccache partition
static bool ram_begin(arc_blob *b, uint32_t size)
{
    memset(b, 0, sizeof *b);
    b->size = size;
    b->index = b->cur = -1;
    b->ram = (uint8_t *)calloc(1, size ? size : 1);
    if (!b->ram) { b->err = true; return false; }
    return true;
}

static void ram_write(arc_blob *b, uint32_t off, const void *src, uint32_t n)
{
    if (b->err || !b->ram || off + n > b->size) { b->err = true; return; }
    memcpy(b->ram + off, src, n);
}

static const uint8_t *ram_end(arc_blob *b)
{
    if (b->err) {
        free(b->ram);
        return NULL;
    }
    return b->ram;
}

#if !ARC_FLASH_CACHE
bool arc_blob_begin(arc_blob *b, const char *name, uint32_t size, uint32_t key)
{
    (void)name, (void)key;
    return ram_begin(b, size);
}
void arc_blob_write(arc_blob *b, uint32_t off, const void *src, uint32_t n) { ram_write(b, off, src, n); }
const uint8_t *arc_blob_end(arc_blob *b) { return ram_end(b); }
void arc_blob_free(const uint8_t *p) { free((void *)p); }
#else
// The flash: a header sector (the blobs of the last game built: key, place,
// done), then the blobs one after another, each from a sector boundary.
#define SECT 4096u
#define MAGIC 0x41524331u               // "ARC1"
typedef struct { uint32_t key, off, size, done; } blob_entry;
static struct { uint32_t magic, n; blob_entry e[64]; } hdr;
static const esp_partition_t *part;
static int blob_count;
static uint32_t cursor = SECT;
static struct { const void *p; esp_partition_mmap_handle_t h; } maps[64];

static bool cache_open(void)
{
    if (part) return true;
    static bool looked;
    if (looked) return false;
    looked = true;
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "arccache");
    if (!part) {
        ESP_LOGI("arc", "no arccache partition: the ROMs in the RAM");
        return false;
    }
    if (esp_partition_read(part, 0, &hdr, sizeof hdr) != ESP_OK || hdr.magic != MAGIC || hdr.n > 64) {
        memset(&hdr, 0, sizeof hdr);
        hdr.magic = MAGIC;
    }
    return true;
}

static bool hdr_write(void)
{
    return esp_partition_erase_range(part, 0, SECT) == ESP_OK && esp_partition_write(part, 0, &hdr, sizeof hdr) == ESP_OK;
}

static void flush(arc_blob *b, int sector)
{
    if (b->err) return;
    const uint32_t at = b->off + (uint32_t)sector * SECT;
    if (esp_partition_erase_range(part, at, SECT) != ESP_OK || esp_partition_write(part, at, b->ram, SECT) != ESP_OK)
        b->err = true;
}

bool arc_blob_begin(arc_blob *b, const char *name, uint32_t size, uint32_t key)
{
    if (!cache_open()) return ram_begin(b, size);
    memset(b, 0, sizeof *b);
    b->cur = -1;
    b->size = size;
    b->index = blob_count++;
    if (b->index >= 64) { b->err = true; return false; }
    uint32_t k = fnv(key ^ 2166136261u, arc_game.id, strlen(arc_game.id));
    k = fnv(k, name, strlen(name));
    k = fnv(k, &size, 4);
    b->off = cursor;
    cursor += (size + SECT - 1) / SECT * SECT;
    if (cursor > part->size) {
        ESP_LOGE("arc", "arccache: %s does not fit", name);
        b->err = true;
        return false;
    }
    const blob_entry *e = &hdr.e[b->index];
    if ((uint32_t)b->index < hdr.n && e->key == k && e->off == b->off && e->size == size && e->done) {
        b->cached = true;
        return false;
    }
    ESP_LOGI("arc", "arccache: writing %s (%u bytes)", name, (unsigned)size);
    hdr.n = (uint32_t)b->index;          // what follows has to be redone
    hdr.e[b->index] = (blob_entry){k, b->off, size, 0};
    b->ram = (uint8_t *)calloc(1, SECT);
    if (!b->ram || !hdr_write()) { b->err = true; return false; }
    return true;
}

void arc_blob_write(arc_blob *b, uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    if (b->index < 0) { ram_write(b, off, src, n); return; }
    if (b->cached || b->err) return;
    if (off + n > b->size) { b->err = true; return; }
    while (n) {
        const int sec = (int)(off / SECT);
        if (sec != b->cur) {
            if (sec < b->cur) {         // backwards: not possible in the flash
                ESP_LOGE("arc", "arccache: a write goes back");
                b->err = true;
                return;
            }
            if (b->cur >= 0) flush(b, b->cur);
            memset(b->ram, 0, SECT);
            for (int g = b->cur + 1; g < sec; g++) flush(b, g);   // the gaps: zeros
            b->cur = sec;
        }
        const uint32_t in = off % SECT, k = SECT - in < n ? SECT - in : n;
        memcpy(b->ram + in, s, k);
        s += k;
        off += k;
        n -= k;
    }
}

const uint8_t *arc_blob_end(arc_blob *b)
{
    if (b->index < 0) return ram_end(b);
    if (!b->err && !b->cached) {
        const int last = (int)((b->size + SECT - 1) / SECT) - 1;
        if (b->cur >= 0) flush(b, b->cur);
        memset(b->ram, 0, SECT);
        for (int g = b->cur + 1; g <= last; g++) flush(b, g);
        if (!b->err) {
            hdr.e[b->index].done = 1;
            hdr.n = (uint32_t)b->index + 1;
            if (!hdr_write()) b->err = true;
        }
    }
    free(b->ram);
    b->ram = NULL;
    if (b->err) return NULL;
    const void *p = NULL;
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(part, b->off, b->size ? b->size : 1, ESP_PARTITION_MMAP_DATA, &p, &h) != ESP_OK) {
        ESP_LOGE("arc", "arccache: mmap failed");
        return NULL;
    }
    for (int i = 0; i < 64; i++)
        if (!maps[i].p) {
            maps[i].p = p;
            maps[i].h = h;
            break;
        }
    return (const uint8_t *)p;
}

void arc_blob_free(const uint8_t *p)
{
    for (int i = 0; i < 64; i++)
        if (p && maps[i].p == p) {
            esp_partition_munmap(maps[i].h);
            maps[i].p = NULL;
            return;
        }
    free((void *)p);                    // (in the RAM)
}
#endif
