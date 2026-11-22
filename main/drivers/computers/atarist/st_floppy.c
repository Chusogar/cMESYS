// st_floppy.c - WD1772 + DMA + disk images (see st_floppy.h).
#include "st_floppy.h"
#include <stdlib.h>
#include <string.h>

#define SECTOR 512
#define CYCLES_PER_BYTE 256u
#define CYCLES_PER_REV 1600000u
#define GAP1 60
#define GAP2 12
#define RAW_SECTOR512 (12 + 3 + 1 + 4 + 2 + 22 + 12 + 3 + 1 + 512 + 2 + 40)
#define ID_TO_DATA (22 + 12 + 3 + 1)

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint16_t crc16_add(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)(b << 8);
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

static uint16_t id_crc(const uint8_t id[4])
{
    uint16_t crc = 0xffff;
    const uint8_t pre[4] = { 0xa1, 0xa1, 0xa1, 0xfe };
    for (int i = 0; i < 4; i++) crc = crc16_add(crc, pre[i]);
    for (int i = 0; i < 4; i++) crc = crc16_add(crc, id[i]);
    return crc;
}

static int sec_size(const stx_sector *s) { return 128 << (s->id[3] & 3); }

static void disk_free(stf_disk *d)
{
    if (d->image && d->image != d->file) free(d->image);
    free(d->file);
    free(d->stx_tracks);
    free(d->sectors);
    const int head = d->head;
    memset(d, 0, sizeof *d);
    d->head = head;
}

void stf_reset(st_floppy *f)
{
    f->dma_mode = 0;
    f->dma_addr = 0;
    f->dma_count = 0;
    f->fdc_track = 0;
    f->fdc_sector = 1;
    f->fdc_data = 0;
    f->fdc_status = 0;
    f->fdc_irq = f->fdc_busy = f->motor_on = f->dma_error = false;
    f->motor_idle = 0;
    f->irq_delay = 0;
    f->psg_a = 0xff;
    f->last_cmd = 0;
    for (int i = 0; i < STF_DRIVES; i++) f->drives[i].head = 0;
    f->step_dir = 1;
    f->dma_bytes = 0;
    if (!f->rng) f->rng = 0x12345678;
}

static bool decode_geometry(stf_disk *d, size_t bytes)
{
    const int k = (int)(bytes / SECTOR);
    static const int cands[][3] = {
        {80, 2, 9}, {80, 2, 10}, {80, 1, 9}, {80, 1, 10}, {81, 2, 9}, {81, 2, 10},
        {82, 2, 9}, {82, 2, 10}, {79, 2, 9}, {40, 2, 9}, {80, 2, 11}, {83, 2, 9},
    };
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++)
        if (cands[i][0] * cands[i][1] * cands[i][2] == k) {
            d->tracks = cands[i][0];
            d->sides = cands[i][1];
            d->spt = cands[i][2];
            return true;
        }
    if (k >= 9 * 80 && (k % 9) == 0) {
        d->tracks = 80;
        d->spt = 9;
        d->sides = k / (80 * 9);
        if (d->sides < 1) d->sides = 1;
        if (d->sides > 2) d->sides = 2;
        return true;
    }
    return false;
}

static uint8_t *disk_sector(const stf_disk *d, int track, int side, int sector)
{
    if (!d->loaded || d->stx || track < 0 || track >= d->tracks || side < 0 || side >= d->sides) return NULL;
    if (sector < 1 || sector > d->spt) return NULL;
    const size_t off = (size_t)((track * d->sides + side) * d->spt + (sector - 1)) * SECTOR;
    if (off + SECTOR > d->image_size) return NULL;
    return d->image + off;
}

static int selected_drive(const st_floppy *f)
{
    if ((f->psg_a & 2) == 0) return 0;
    if ((f->psg_a & 4) == 0) return 1;
    return -1;
}

static int selected_side(const st_floppy *f) { return (f->psg_a & 1) ? 0 : 1; }

static stf_disk *cur(st_floppy *f)
{
    const int d = selected_drive(f);
    return &f->drives[d == 1 ? 1 : 0];
}

static stf_disk *media(st_floppy *f)
{
    const int d = selected_drive(f);
    if (d < 0 || !f->drives[d].loaded) return NULL;
    return &f->drives[d];
}

static void msa_rle(const uint8_t *src, int slen, uint8_t *dest, int dlen)
{
    int s = 0, d = 0;
    while (s < slen && d < dlen) {
        const uint8_t b = src[s++];
        if (b == 0xe5 && s + 2 <= slen) {
            const uint8_t fill = src[s++];
            int count = (src[s] << 8) | src[s + 1];
            s += 2;
            while (count-- > 0 && d < dlen) dest[d++] = fill;
        } else {
            dest[d++] = b;
        }
    }
}

static bool load_msa(stf_disk *d, const uint8_t *data, size_t size, const char **why)
{
    if (size < 10 || be16(data) != 0x0e0f) { *why = "not an MSA disk"; return false; }
    const int spt = be16(data + 2), sides = be16(data + 4) + 1;
    const int start = be16(data + 6), end = be16(data + 8);
    if (spt < 1 || spt > 11 || sides < 1 || sides > 2 || end < start || end > 85) {
        *why = "the MSA header is corrupt";
        return false;
    }
    d->tracks = end + 1;
    d->sides = sides;
    d->spt = spt;
    d->image_size = (size_t)d->tracks * d->sides * d->spt * SECTOR;
    d->image = calloc(1, d->image_size);
    if (!d->image) { *why = "out of memory"; return false; }
    d->loaded = true;                       // (for disk_sector)
    size_t pos = 10;
    for (int t = start; t <= end; t++)
        for (int s = 0; s < d->sides; s++) {
            if (pos + 2 > size) goto trunc;
            const int packed = be16(data + pos);
            pos += 2;
            if (pos + (size_t)packed > size) goto trunc;
            uint8_t *dest = disk_sector(d, t, s, 1);
            const int raw = d->spt * SECTOR;
            if (dest) {
                if (packed == raw) memcpy(dest, data + pos, (size_t)raw);
                else msa_rle(data + pos, packed, dest, raw);
            }
            pos += (size_t)packed;
        }
    return true;
trunc:
    free(d->image);
    d->image = NULL;
    d->loaded = false;
    *why = "the MSA track data is truncated";
    return false;
}

static bool load_stx(stf_disk *d, uint8_t *data, size_t size, const char **why)
{
    if (size < 16 || memcmp(data, "RSY\0", 4) != 0) { *why = "not an STX disk"; return false; }
    if (le16(data + 4) != 3) { *why = "unsupported STX version"; return false; }
    const int blocks = data[10];
    // count the sectors first
    int total = 0;
    size_t pos = 16;
    for (int i = 0; i < blocks; i++) {
        if (pos + 16 > size) { *why = "the STX track header is truncated"; return false; }
        const uint32_t bs = le32(data + pos);
        if (bs < 16 || pos + bs > size) { *why = "the STX track block is truncated"; return false; }
        total += le16(data + pos + 8);
        pos += bs;
    }
    d->stx_tracks = calloc(STF_MAX_TRACKS * 2, sizeof *d->stx_tracks);
    d->sectors = calloc(total ? (size_t)total : 1, sizeof *d->sectors);
    if (!d->stx_tracks || !d->sectors) { *why = "out of memory"; goto fail; }
    int max_track = 0, used = 0;
    bool two_sides = false;
    pos = 16;
    for (int i = 0; i < blocks; i++) {
        uint8_t *h = data + pos;
        const uint32_t block_size = le32(h), fuzzy_size = le32(h + 4);
        const int count = le16(h + 8), flags = le16(h + 10);
        const uint32_t mfm_size = le16(h + 12);
        const int number = h[14];
        const uint8_t *end = data + pos + block_size;
        const int track = number & 0x7f, side = (number >> 7) & 1;
        pos += block_size;
        if (track >= STF_MAX_TRACKS) continue;
        stx_track *t = &d->stx_tracks[track * 2 + side];
        memset(t, 0, sizeof *t);
        t->present = true;
        t->bytes = mfm_size ? mfm_size : 6250;
        t->first = used;
        if (track > max_track) max_track = track;
        if (side) two_sides = true;
        uint8_t *p = h + 16;
        if (count > 0 && (flags & 1) == 0) {
            // a plain track: `count` 512-byte sectors, MFM size in bits
            t->bytes = mfm_size ? (mfm_size + 7) / 8 : 6250;
            if (t->bytes < 6000) t->bytes = 6250;
            int byte_pos = GAP1 + GAP2 + 4;
            for (int s = 0; s < count; s++) {
                if (p + (size_t)(s + 1) * 512 > end) { *why = "the STX sector data is truncated"; goto fail; }
                stx_sector *sec = &d->sectors[used++];
                sec->id[0] = (uint8_t)track;
                sec->id[1] = (uint8_t)side;
                sec->id[2] = (uint8_t)(s + 1);
                sec->id[3] = 2;
                sec->crc = id_crc(sec->id);
                sec->bit_pos = (uint32_t)byte_pos * 8;
                sec->data = p + s * 512;
                byte_pos += RAW_SECTOR512;
            }
            t->count = count;
            continue;
        }
        uint8_t *sector_blocks = p;
        const uint8_t *fuzzy = p + (size_t)count * 16;
        uint8_t *track_data = (uint8_t *)fuzzy + fuzzy_size;
        if (track_data > end) { *why = "the STX track block is corrupt"; goto fail; }
        if (flags & 0x40) {
            const uint8_t *img = track_data;
            if (flags & 0x80) img += 2;
            if (img + 2 > end) { *why = "the STX track image is truncated"; goto fail; }
            const uint32_t n = le16(img);
            img += 2;
            if (img + n > end) { *why = "the STX track image is truncated"; goto fail; }
            t->image = img;
            t->image_size = n;
        }
        const uint8_t *fz = fuzzy;
        for (int s = 0; s < count; s++) {
            const uint8_t *b = sector_blocks + s * 16;
            stx_sector *sec = &d->sectors[used++];
            const uint32_t off = le32(b);
            sec->bit_pos = le16(b + 4);
            sec->read_time = le16(b + 6);
            memcpy(sec->id, b + 8, 4);
            sec->crc = be16(b + 12);
            sec->status = b[14];
            if ((sec->status & 0x10) == 0) {
                const int n = sec_size(sec);
                uint8_t *src = track_data + off;
                if (src + n > end) { *why = "the STX sector data is truncated"; goto fail; }
                sec->data = src;
                if (sec->status & 0x80) {
                    if (fz + n > track_data) { *why = "the STX fuzzy mask is truncated"; goto fail; }
                    sec->fuzzy = fz;
                    fz += n;
                }
            }
        }
        t->count = count;
    }
    d->tracks = max_track + 1;
    d->sides = two_sides ? 2 : 1;
    d->spt = 9;
    if (d->stx_tracks[2].present && d->stx_tracks[2].count) d->spt = d->stx_tracks[2].count;
    d->stx = true;
    return true;
fail:
    free(d->stx_tracks);
    free(d->sectors);
    d->stx_tracks = NULL;
    d->sectors = NULL;
    return false;
}

static bool ext_is(const char *name, const char *ext)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    if (!dot) return false;
    for (int i = 0;; i++) {
        const char a = (char)(dot[i] | (dot[i] ? 0x20 : 0)), b = ext[i];
        if (a != b) return false;
        if (!a) return true;
    }
}

bool stf_insert(st_floppy *f, int drive, uint8_t *data, size_t size, const char *name, const char **why)
{
    stf_disk d;
    memset(&d, 0, sizeof d);
    bool ok;
    *why = NULL;
    if (ext_is(name, ".stx") || (size >= 4 && memcmp(data, "RSY\0", 4) == 0)) {
        ok = load_stx(&d, data, size, why);
    } else if (ext_is(name, ".msa") || (size >= 2 && data[0] == 0x0e && data[1] == 0x0f)) {
        ok = load_msa(&d, data, size, why);
        if (ok) {
            free(data);                     // the sectors are decoded
            data = NULL;
        }
    } else {
        ok = decode_geometry(&d, size);
        if (!ok) *why = "the image size is not a known floppy geometry";
        d.image = data;
        d.image_size = size;
    }
    if (!ok) return false;
    d.file = data;
    d.loaded = true;
    stf_disk *slot = &f->drives[drive & 1];
    d.head = slot->head;
    disk_free(slot);
    *slot = d;
    return true;
}

void stf_eject(st_floppy *f, int drive) { disk_free(&f->drives[drive & 1]); }

uint16_t stf_dma_status(const st_floppy *f)
{
    uint16_t v = 0;
    if (!f->dma_error) v |= 1;
    if (f->dma_count == 0) v |= 2;
    return v;
}

void stf_dma_mode_w(st_floppy *f, uint16_t value)
{
    if ((f->dma_mode ^ value) & 0x100) {
        f->dma_error = false;
        f->dma_bytes = 0;
    }
    f->dma_mode = value;
}

void stf_tick(st_floppy *f, int cycles)
{
    f->cycles += (uint64_t)cycles;
    if (f->motor_on && !f->fdc_busy) {
        f->motor_idle += (uint32_t)cycles;
        if (f->motor_idle >= 9 * CYCLES_PER_REV) f->motor_on = false;
    }
    if (f->irq_delay <= 0) return;
    f->irq_delay -= cycles;
    if (f->irq_delay <= 0) {
        f->irq_delay = 0;
        f->fdc_busy = false;
        f->fdc_irq = true;
    }
}

void stf_dma_addr_w(st_floppy *f, int which, uint8_t value)
{
    if (which == 0) f->dma_addr = (f->dma_addr & 0x00ffff) | ((uint32_t)value << 16);
    else if (which == 1) f->dma_addr = (f->dma_addr & 0xff00ff) | ((uint32_t)value << 8);
    else f->dma_addr = (f->dma_addr & 0xffff00) | value;
}

uint8_t stf_dma_addr_r(const st_floppy *f, int which)
{
    if (which == 0) return (uint8_t)(f->dma_addr >> 16);
    if (which == 1) return (uint8_t)(f->dma_addr >> 8);
    return (uint8_t)f->dma_addr;
}

static uint32_t rotation_byte(const st_floppy *f) { return (uint32_t)((f->cycles % CYCLES_PER_REV) / CYCLES_PER_BYTE); }

static stx_track *get_stx_track(stf_disk *d, int track, int side)
{
    if (!d->stx || track < 0 || track >= STF_MAX_TRACKS || side < 0 || side > 1) return NULL;
    stx_track *t = &d->stx_tracks[track * 2 + side];
    return t->present ? t : NULL;
}

static uint8_t fdc_status(st_floppy *f)
{
    uint8_t v = f->fdc_status;
    if (f->fdc_busy) v |= 0x01;
    const bool type1 = (f->last_cmd & 0x80) == 0;
    if (type1 && cur(f)->stx) {
        if (cur(f)->head == 0) v |= 0x04;
        if (f->motor_on && media(f) && rotation_byte(f) < 140) v |= 0x02;
        if (f->motor_on) v |= 0x20;
    } else if (type1) {
        if (f->fdc_track == 0) v |= 0x04;
        if (f->motor_on) v |= 0x20;
    }
    if (f->motor_on) v |= 0x80;
    f->fdc_irq = false;
    return v;
}

static void finish_command(st_floppy *f)
{
    f->fdc_busy = true;
    f->fdc_irq = false;
    f->irq_delay = 8000;
}

static void stx_finish(st_floppy *f, uint32_t cycles)
{
    f->fdc_busy = true;
    f->fdc_irq = false;
    f->irq_delay = (int)(cycles ? cycles : 1);
}

static uint8_t random_byte(st_floppy *f)
{
    f->rng ^= f->rng << 13;
    f->rng ^= f->rng >> 17;
    f->rng ^= f->rng << 5;
    return (uint8_t)(f->rng >> 11);
}

static int stx_next_sector(st_floppy *f, const stf_disk *d, const stx_track *t, bool match, uint32_t *wait)
{
    const uint32_t len = t->bytes ? t->bytes : 6250;
    const uint32_t now = rotation_byte(f) % len;
    int best = -1;
    uint32_t best_dist = 0;
    for (int i = 0; i < t->count; i++) {
        const stx_sector *sec = &d->sectors[t->first + i];
        if (match && (sec->id[0] != f->fdc_track || sec->id[2] != f->fdc_sector)) continue;
        const uint32_t id_end = (sec->bit_pos / 8 + 6) % len;
        uint32_t dist = (id_end + len - now) % len;
        if (dist == 0) dist = len;
        if (best < 0 || dist < best_dist) {
            best = t->first + i;
            best_dist = dist;
        }
    }
    if (wait) *wait = best_dist;
    return best;
}

static void dma_push_byte(st_floppy *f, uint8_t b)
{
    if (f->dma_count == 0 || !f->ram) return;
    if (f->dma_addr < f->ram_size) f->ram[f->dma_addr] = b;
    f->dma_addr = (f->dma_addr + 1) & 0xffffff;
    if (++f->dma_bytes >= SECTOR) {
        f->dma_bytes = 0;
        f->dma_count--;
    }
}

static void dma_push(st_floppy *f, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n && f->dma_count; i++) dma_push_byte(f, data[i]);
}

static void stx_read_sector(st_floppy *f, uint8_t cmd)
{
    stf_disk *d = media(f);
    stx_track *t = d ? get_stx_track(d, d->head, selected_side(f)) : NULL;
    if (!t || !t->count) {
        f->fdc_status = 0x10;
        stx_finish(f, 5 * CYCLES_PER_REV);
        return;
    }
    uint64_t total = 0;
    const uint64_t saved = f->cycles;
    for (;;) {
        uint32_t wait = 0;
        const int idx = stx_next_sector(f, d, t, true, &wait);
        if (idx < 0 || (d->sectors[idx].status & 0x10)) {
            f->fdc_status |= 0x10;
            total += 5 * CYCLES_PER_REV;
            break;
        }
        const stx_sector *sec = &d->sectors[idx];
        const int n = sec_size(sec);
        uint64_t cycles = (uint64_t)(wait + ID_TO_DATA) * CYCLES_PER_BYTE;
        if (sec->read_time) cycles += (uint64_t)sec->read_time * 8;
        else cycles += (uint64_t)(n + 2) * CYCLES_PER_BYTE;
        total += cycles;
        f->cycles += cycles;
        if (sec->data) {
            for (int i = 0; i < n; i++) {
                uint8_t b = sec->data[i];
                if (sec->fuzzy) b = (uint8_t)((b & sec->fuzzy[i]) | (random_byte(f) & ~sec->fuzzy[i]));
                dma_push_byte(f, b);
                f->fdc_data = b;
            }
        }
        if (sec->status & 0x20) f->fdc_status |= 0x20;
        if (sec->status & 0x08) {
            f->fdc_status |= 0x08;
            break;
        }
        if ((cmd & 0x10) == 0) break;
        f->fdc_sector++;
    }
    f->cycles = saved;
    stx_finish(f, (uint32_t)total);
}

static void stx_write_sector(st_floppy *f, uint8_t cmd)
{
    stf_disk *d = media(f);
    stx_track *t = d ? get_stx_track(d, d->head, selected_side(f)) : NULL;
    if (!t || !t->count) {
        f->fdc_status = 0x10;
        stx_finish(f, 5 * CYCLES_PER_REV);
        return;
    }
    uint64_t total = 0;
    const uint64_t saved = f->cycles;
    for (;;) {
        uint32_t wait = 0;
        const int idx = stx_next_sector(f, d, t, true, &wait);
        if (idx < 0) {
            f->fdc_status |= 0x10;
            total += 5 * CYCLES_PER_REV;
            break;
        }
        stx_sector *sec = &d->sectors[idx];
        const int n = sec_size(sec);
        for (int i = 0; i < n; i++) {
            if (f->dma_count == 0 || !f->ram) break;
            const uint8_t b = f->dma_addr < f->ram_size ? f->ram[f->dma_addr] : 0;
            if (sec->data) sec->data[i] = b;     // (a sector without data stays so)
            f->dma_addr = (f->dma_addr + 1) & 0xffffff;
            if (++f->dma_bytes >= SECTOR) {
                f->dma_bytes = 0;
                f->dma_count--;
            }
        }
        if (sec->data) {
            sec->status &= (uint8_t)~(0x08 | 0x10 | 0x20 | 0x80);
            sec->fuzzy = NULL;
        }
        const uint64_t cycles = (uint64_t)(wait + ID_TO_DATA + n + 2) * CYCLES_PER_BYTE;
        total += cycles;
        f->cycles += cycles;
        if ((cmd & 0x10) == 0) break;
        f->fdc_sector++;
    }
    f->cycles = saved;
    stx_finish(f, (uint32_t)total);
}

static void stx_read_address(st_floppy *f)
{
    stf_disk *d = media(f);
    stx_track *t = d ? get_stx_track(d, d->head, selected_side(f)) : NULL;
    uint32_t wait = 0;
    const int idx = t ? stx_next_sector(f, d, t, false, &wait) : -1;
    if (idx < 0) {
        f->fdc_status = 0x10;
        stx_finish(f, 5 * CYCLES_PER_REV);
        return;
    }
    const stx_sector *sec = &d->sectors[idx];
    const uint8_t id[6] = { sec->id[0], sec->id[1], sec->id[2], sec->id[3], (uint8_t)(sec->crc >> 8), (uint8_t)sec->crc };
    dma_push(f, id, 6);
    f->fdc_data = id[5];
    f->fdc_sector = sec->id[0];
    if ((sec->status & 0x08) && id_crc(sec->id) != sec->crc) f->fdc_status |= 0x08;
    stx_finish(f, wait * CYCLES_PER_BYTE);
}

static void fill(st_floppy *f, uint8_t b, int n, uint32_t *len)
{
    for (int i = 0; i < n; i++) dma_push_byte(f, b);
    *len += (uint32_t)n;
}

static void stx_read_track(st_floppy *f)
{
    stf_disk *d = media(f);
    stx_track *t = d ? get_stx_track(d, d->head, selected_side(f)) : NULL;
    const uint32_t len = t && t->bytes ? t->bytes : 6250;
    if (t && t->image) {
        dma_push(f, t->image, t->image_size);
        if (t->image_size) f->fdc_data = t->image[t->image_size - 1];
    } else if (t && t->count) {
        // a standard track rebuilt around the recorded sectors
        uint32_t raw = 0;
        fill(f, 0x4e, GAP1, &raw);
        for (int i = 0; i < t->count; i++) {
            const stx_sector *sec = &d->sectors[t->first + i];
            const int n = sec_size(sec);
            if (raw + (uint32_t)(RAW_SECTOR512 - 512 + n) > len) break;
            fill(f, 0x00, 12, &raw);
            fill(f, 0xa1, 3, &raw);
            fill(f, 0xfe, 1, &raw);
            dma_push(f, sec->id, 4);
            fill(f, (uint8_t)(sec->crc >> 8), 1, &raw);
            fill(f, (uint8_t)sec->crc, 1, &raw);
            raw += 4;
            fill(f, 0x4e, 22, &raw);
            fill(f, 0x00, 12, &raw);
            fill(f, 0xa1, 3, &raw);
            const uint8_t dam = (sec->status & 0x20) ? 0xf8 : 0xfb;
            fill(f, dam, 1, &raw);
            uint16_t crc = 0xffff;
            crc = crc16_add(crc, 0xa1);
            crc = crc16_add(crc, 0xa1);
            crc = crc16_add(crc, 0xa1);
            crc = crc16_add(crc, dam);
            for (int k = 0; k < n; k++) {
                const uint8_t b = sec->data ? sec->data[k] : 0;
                dma_push_byte(f, b);
                crc = crc16_add(crc, b);
            }
            raw += (uint32_t)n;
            if (sec->status & 0x08) crc = (uint16_t)~crc;
            fill(f, (uint8_t)(crc >> 8), 1, &raw);
            fill(f, (uint8_t)crc, 1, &raw);
            fill(f, 0x4e, 40, &raw);
        }
        if (raw < len) fill(f, 0x4e, (int)(len - raw), &raw);
        f->fdc_data = 0x4e;
    } else {
        for (uint32_t i = 0; i < len; i++) {
            f->fdc_data = random_byte(f);
            dma_push_byte(f, f->fdc_data);
        }
    }
    const uint32_t to_index = (len - rotation_byte(f) % len) % len;
    stx_finish(f, (to_index + len) * CYCLES_PER_BYTE);
}

static void do_read_address(st_floppy *f)
{
    if (!media(f)) {
        f->fdc_status = 0x10;
        finish_command(f);
        return;
    }
    const uint8_t id[6] = { f->fdc_track, (uint8_t)selected_side(f), f->fdc_sector, 2, 0, 0 };
    f->fdc_data = id[0];
    if (f->dma_count && f->ram && f->dma_addr + 6 <= f->ram_size) {
        memcpy(f->ram + f->dma_addr, id, 6);
        f->dma_addr += 6;
        f->dma_count--;
    }
    finish_command(f);
}

static void do_dma(st_floppy *f, bool write)
{
    const int side = selected_side(f);
    stf_disk *d = media(f);
    if (!d) {
        f->dma_error = true;
        f->fdc_status = 0x10;
        finish_command(f);
        return;
    }
    while (f->dma_count) {
        uint8_t *sec = disk_sector(d, f->fdc_track, side, f->fdc_sector);
        if (!sec || !f->ram || f->dma_addr + SECTOR > f->ram_size) {
            f->dma_error = true;
            f->fdc_status = 0x10;
            finish_command(f);
            return;
        }
        if (write) memcpy(sec, f->ram + f->dma_addr, SECTOR);
        else memcpy(f->ram + f->dma_addr, sec, SECTOR);
        f->dma_addr += SECTOR;
        f->dma_count--;
        f->fdc_sector++;
        if (f->fdc_sector > d->spt) {
            f->fdc_sector = 1;
            break;
        }
    }
    finish_command(f);
}

static void fdc_command(st_floppy *f, uint8_t cmd)
{
    f->last_cmd = cmd;
    f->motor_idle = 0;
    f->fdc_irq = false;
    f->irq_delay = 0;
    f->dma_error = false;
    f->fdc_status = 0;
    if ((cmd & 0xf0) != 0xd0) f->motor_on = true;
    const uint8_t type = (uint8_t)(cmd & 0xf0);
    stf_disk *d = cur(f);
    if (d->stx && type < 0x80) {
        int steps = 0;
        if (type == 0x00 || type == 0x10) {
            if (type == 0x00) {
                f->step_dir = -1;
                steps = d->head;
                d->head = 0;
                f->fdc_track = 0;
            } else {
                const int delta = (int)f->fdc_data - (int)f->fdc_track;
                if (delta != 0) f->step_dir = delta > 0 ? 1 : -1;
                d->head += delta;
                steps = delta < 0 ? -delta : delta;
                f->fdc_track = f->fdc_data;
            }
        } else {
            if (type == 0x40 || type == 0x50) f->step_dir = 1;
            else if (type == 0x60 || type == 0x70) f->step_dir = -1;
            if (cmd & 0x10) f->fdc_track = (uint8_t)(f->fdc_track + f->step_dir);
            if (!(f->step_dir < 0 && d->head == 0)) d->head += f->step_dir;
            steps = 1;
        }
        if (d->head > 85) d->head = 85;
        if (d->head < 0) d->head = 0;
        static const int kStepMs[4] = { 6, 12, 2, 3 };
        uint32_t cycles = 8000 + (uint32_t)steps * (uint32_t)kStepMs[cmd & 3] * 8000;
        if (cmd & 0x04) {
            const stx_track *t = media(f) ? get_stx_track(d, d->head, selected_side(f)) : NULL;
            bool ok = false;
            if (t)
                for (int i = 0; i < t->count; i++)
                    if (d->sectors[t->first + i].id[0] == f->fdc_track) ok = true;
            if (ok) cycles += 15000 * 8;
            else {
                f->fdc_status = 0x10;
                cycles += 5 * CYCLES_PER_REV;
            }
        }
        stx_finish(f, cycles);
        return;
    }
    if (type < 0x80) {
        if (type == 0x00) f->fdc_track = 0;
        else if (type == 0x10) f->fdc_track = f->fdc_data;
        else if (type == 0x40 || type == 0x60) {
            if (f->fdc_track < 90) f->fdc_track++;
        } else if (type == 0x20 || type == 0x50 || type == 0x70) {
            if (f->fdc_track > 0) f->fdc_track--;
        }
        d->head = f->fdc_track;
        finish_command(f);
        return;
    }
    if (type == 0xd0) {
        f->fdc_busy = false;
        f->irq_delay = 0;
        f->fdc_irq = (cmd & 8) != 0;
        return;
    }
    if (d->stx) {
        if ((cmd & 0xe0) == 0x80) stx_read_sector(f, cmd);
        else if ((cmd & 0xe0) == 0xa0) stx_write_sector(f, cmd);
        else if (type == 0xc0) stx_read_address(f);
        else if (type == 0xe0) stx_read_track(f);
        else stx_finish(f, 8000);
        return;
    }
    if ((cmd & 0xe0) == 0x80) { do_dma(f, false); return; }
    if ((cmd & 0xe0) == 0xa0) { do_dma(f, true); return; }
    if (type == 0xc0) { do_read_address(f); return; }
    if (type == 0xe0) { finish_command(f); return; }
    f->fdc_status = 0x10;
    finish_command(f);
}

uint16_t stf_dma_data_r(st_floppy *f)
{
    if (f->dma_mode & 0x10) return f->dma_count;
    if (f->dma_mode & 0x08) return 0xff;
    const int reg = (f->dma_mode >> 1) & 3;
    if (reg == 0) return fdc_status(f);
    if (reg == 1) return f->fdc_track;
    if (reg == 2) return f->fdc_sector;
    return f->fdc_data;
}

void stf_dma_data_w(st_floppy *f, uint16_t value)
{
    if (f->dma_mode & 0x10) {
        f->dma_count = (uint8_t)value;
        return;
    }
    if (f->dma_mode & 0x08) return;
    const int reg = (f->dma_mode >> 1) & 3;
    if (reg == 0) fdc_command(f, (uint8_t)value);
    else if (reg == 1) f->fdc_track = (uint8_t)value;
    else if (reg == 2) f->fdc_sector = (uint8_t)value;
    else f->fdc_data = (uint8_t)value;
}
