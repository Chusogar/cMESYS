// st_floppy.c - Atari ST floppy (WD1772 + DMA, .ST / .MSA / .STX): a C11
// migration of dsp-cpp's machine/st_floppy (see st_floppy.h).
#include "st_floppy.h"

#include <stdlib.h>
#include <string.h>

#define CYCLES_PER_BYTE 256u            // 8 MHz, 250 kbit/s MFM
#define CYCLES_PER_REV  1600000u        // 300 rpm
#define GAP1 60
#define GAP2 12
#define RAW_SECTOR_512 (12 + 3 + 1 + 4 + 2 + 22 + 12 + 3 + 1 + 512 + 2 + 40)
#define ID_TO_DATA (22 + 12 + 3 + 1)
#define STX_TRACKS 86

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

static inline uint8_t ram_r(st_floppy *f, uint32_t a) { return f->ram_read ? f->ram_read(f->user, a) : 0; }
static inline void ram_w(st_floppy *f, uint32_t a, uint8_t v) { if (f->ram_write) f->ram_write(f->user, a, v); }

// --- the disks ----------------------------------------------------------------------
static void free_disk(st_disk *d)
{
    if (d->stx_tracks) {
        for (int t = 0; t < STX_TRACKS * 2; t++) {
            st_stx_track *tr = &d->stx_tracks[t];
            for (int i = 0; i < tr->nsectors; i++) free(tr->sectors[i].written);
            free(tr->sectors);
        }
        free(d->stx_tracks);
    }
    zx_pb_free(&d->img);
    const int head = d->head;
    memset(d, 0, sizeof *d);
    zx_pb_init(&d->img);
    d->tracks = 80;
    d->sides = 2;
    d->spt = 9;
    d->head = head;
}

void st_floppy_init(st_floppy *f)
{
    memset(f, 0, sizeof *f);
    for (int i = 0; i < ST_DRIVES; i++) free_disk(&f->drives[i]);
    f->rng = 0x12345678;
    st_floppy_reset(f);
}

void st_floppy_reset(st_floppy *f)
{
    f->dma_mode = 0;
    f->dma_addr = 0;
    f->dma_count = 0;
    f->fdc_track = 0;
    f->fdc_sector = 1;
    f->fdc_data = 0;
    f->fdc_status = 0;
    f->fdc_irq = false;
    f->fdc_busy = false;
    f->motor_on = false;
    f->motor_idle = 0;
    f->irq_delay = 0;
    f->dma_error = false;
    f->psg_a = 0xff;
    f->last_cmd = 0;
    for (int i = 0; i < ST_DRIVES; i++) f->drives[i].head = 0;
    f->step_dir = 1;
    f->dma_bytes = 0;
}

static bool decode_geometry(st_disk *d, uint32_t bytes)
{
    const int k = (int)(bytes / ST_SECTOR);
    static const struct { int tracks, sides, spt; } cands[] = {
        { 80, 2, 9 }, { 80, 2, 10 }, { 80, 1, 9 }, { 80, 1, 10 }, { 81, 2, 9 }, { 81, 2, 10 },
        { 82, 2, 9 }, { 82, 2, 10 }, { 79, 2, 9 }, { 40, 2, 9 }, { 80, 2, 11 }, { 83, 2, 9 },
    };
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++)
        if (cands[i].tracks * cands[i].sides * cands[i].spt == k) {
            d->tracks = cands[i].tracks;
            d->sides = cands[i].sides;
            d->spt = cands[i].spt;
            return true;
        }
    if (k >= 9 * 80 && k % 9 == 0) {
        d->tracks = 80;
        d->spt = 9;
        d->sides = k / (80 * 9);
        if (d->sides < 1) d->sides = 1;
        if (d->sides > 2) d->sides = 2;
        return true;
    }
    return false;
}

// The offset of a sector of a flat image (-1: none)
static int64_t sector_off(const st_disk *d, int track, int side, int sector)
{
    if (!d->loaded || d->stx || track < 0 || track >= d->tracks || side < 0 || side >= d->sides) return -1;
    if (sector < 1 || sector > d->spt) return -1;
    const int64_t off = (int64_t)((track * d->sides + side) * d->spt + (sector - 1)) * ST_SECTOR;
    if (off + ST_SECTOR > d->img.size) return -1;
    return off;
}

static bool load_msa(st_disk *d, zx_pbuf *src, const char **error)
{
    if (src->size < 10 || zx_pb_byte(src, 0) != 0x0e || zx_pb_byte(src, 1) != 0x0f) {
        *error = "not an MSA disk";
        return false;
    }
    const int spt = (zx_pb_byte(src, 2) << 8) | zx_pb_byte(src, 3);
    const int sides = ((zx_pb_byte(src, 4) << 8) | zx_pb_byte(src, 5)) + 1;
    const int start = (zx_pb_byte(src, 6) << 8) | zx_pb_byte(src, 7);
    const int end = (zx_pb_byte(src, 8) << 8) | zx_pb_byte(src, 9);
    if (spt < 1 || spt > 11 || sides < 1 || sides > 2 || end < start) {
        *error = "MSA header is corrupt";
        return false;
    }
    d->tracks = end + 1;
    d->sides = sides;
    d->spt = spt;
    const int raw = spt * ST_SECTOR;
    uint8_t *trk = malloc((size_t)raw);
    if (!trk) {
        *error = "out of memory for the MSA disk";
        return false;
    }
    zx_pbuf out;
    zx_pb_init(&out);
    // the tracks before `start` are empty
    memset(trk, 0, (size_t)raw);
    for (int t = 0; t < start * sides; t++)
        if (!zx_pb_append(&out, trk, (size_t)raw)) goto nomem;
    uint32_t pos = 10;
    for (int t = start; t <= end; t++)
        for (int s = 0; s < sides; s++) {
            if (pos + 2 > src->size) goto truncated;
            const int packed = (zx_pb_byte(src, pos) << 8) | zx_pb_byte(src, pos + 1);
            pos += 2;
            if (pos + (uint32_t)packed > src->size) goto truncated;
            memset(trk, 0, (size_t)raw);
            if (packed == raw) {
                zx_pb_copy(src, pos, trk, (uint32_t)raw);
            } else {
                uint32_t i = pos;
                const uint32_t e = pos + (uint32_t)packed;
                int o = 0;
                while (i < e && o < raw) {
                    const uint8_t b = zx_pb_byte(src, i++);
                    if (b == 0xe5 && i + 2 <= e) {
                        const uint8_t fill = zx_pb_byte(src, i++);
                        int count = (zx_pb_byte(src, i) << 8) | zx_pb_byte(src, i + 1);
                        i += 2;
                        while (count-- > 0 && o < raw) trk[o++] = fill;
                    } else {
                        trk[o++] = b;
                    }
                }
            }
            pos += (uint32_t)packed;
            if (!zx_pb_append(&out, trk, (size_t)raw)) goto nomem;
        }
    free(trk);
    if (!zx_pb_finish(&out)) {
        zx_pb_free(&out);
        *error = "out of memory for the MSA disk";
        return false;
    }
    d->img = out;
    d->loaded = true;
    return true;
truncated:
    free(trk);
    zx_pb_free(&out);
    *error = "MSA track data is truncated";
    return false;
nomem:
    free(trk);
    zx_pb_free(&out);
    *error = "out of memory for the MSA disk";
    return false;
}

static st_stx_track *stx_track(st_disk *d, int track, int side)
{
    if (!d->stx || track < 0 || track >= STX_TRACKS || side < 0 || side > 1) return NULL;
    st_stx_track *t = &d->stx_tracks[track * 2 + side];
    return t->present ? t : NULL;
}

static uint16_t le16(const zx_pbuf *pb, uint32_t o) { return (uint16_t)(zx_pb_byte(pb, o) | (zx_pb_byte(pb, o + 1) << 8)); }
static uint32_t le32(const zx_pbuf *pb, uint32_t o) { return (uint32_t)le16(pb, o) | ((uint32_t)le16(pb, o + 2) << 16); }

static bool load_stx(st_disk *d, const char **error)
{
    const zx_pbuf *pb = &d->img;
    if (pb->size < 16 || !zx_pb_eq(pb, 0, "RSY\0", 4)) { *error = "not an STX disk"; return false; }
    if (le16(pb, 4) != 3) { *error = "unsupported STX version"; return false; }
    const int track_blocks = zx_pb_byte(pb, 10);
    d->stx_tracks = calloc(STX_TRACKS * 2, sizeof *d->stx_tracks);
    if (!d->stx_tracks) { *error = "out of memory for the STX disk"; return false; }
    d->stx = true;                      // (free_disk frees the tracks from here on)
    int max_track = 0;
    bool two_sides = false;
    uint32_t pos = 16;
    for (int i = 0; i < track_blocks; i++) {
        if (pos + 16 > pb->size) { *error = "STX track header is truncated"; return false; }
        const uint32_t h = pos;
        const uint32_t block_size = le32(pb, h);
        const uint32_t fuzzy_size = le32(pb, h + 4);
        const int count = le16(pb, h + 8);
        const int flags = le16(pb, h + 10);
        const uint32_t mfm_size = le16(pb, h + 12);
        const int number = zx_pb_byte(pb, h + 14);
        if (block_size < 16 || pos + block_size > pb->size) { *error = "STX track block is truncated"; return false; }
        const uint32_t end = pos + block_size;
        const int track = number & 0x7f, side = (number >> 7) & 1;
        if (track >= STX_TRACKS) {
            pos += block_size;
            continue;
        }
        st_stx_track *t = &d->stx_tracks[track * 2 + side];
        for (int k = 0; k < t->nsectors; k++) free(t->sectors[k].written);
        free(t->sectors);
        memset(t, 0, sizeof *t);
        t->present = true;
        t->bytes = mfm_size ? mfm_size : 6250;
        if (track > max_track) max_track = track;
        if (side) two_sides = true;
        const uint32_t p = h + 16;
        if (count > 0) {
            t->sectors = calloc((size_t)count, sizeof *t->sectors);
            if (!t->sectors) { *error = "out of memory for the STX disk"; return false; }
        }
        if (count > 0 && (flags & 0x01) == 0) {
            // plain track: `count` 512-byte sectors in the standard layout
            t->bytes = mfm_size ? (mfm_size + 7) / 8 : 6250;
            if (t->bytes < 6000) t->bytes = 6250;
            int byte_pos = GAP1 + GAP2 + 4;
            for (int sct = 0; sct < count; sct++) {
                if (p + (uint32_t)(sct + 1) * 512 > end) { *error = "STX sector data is truncated"; return false; }
                st_stx_sector *sec = &t->sectors[sct];
                sec->id[0] = (uint8_t)track;
                sec->id[1] = (uint8_t)side;
                sec->id[2] = (uint8_t)(sct + 1);
                sec->id[3] = 2;
                sec->crc = id_crc(sec->id);
                sec->bit_pos = (uint32_t)byte_pos * 8;
                sec->has_data = true;
                sec->data_off = p + (uint32_t)sct * 512;
                t->nsectors++;
                byte_pos += RAW_SECTOR_512;
            }
            pos += block_size;
            continue;
        }
        const uint32_t sector_blocks = p;
        const uint32_t fuzzy = p + (uint32_t)count * 16;
        const uint32_t track_data = fuzzy + fuzzy_size;
        if (track_data > end) { *error = "STX track block is corrupt"; return false; }
        if (flags & 0x40) {
            uint32_t img = track_data;
            if (flags & 0x80) img += 2;
            if (img + 2 > end) { *error = "STX track image is truncated"; return false; }
            const uint32_t img_size = le16(pb, img);
            img += 2;
            if (img + img_size > end) { *error = "STX track image is truncated"; return false; }
            t->image_off = img;
            t->image_len = img_size;
        }
        uint32_t fz = fuzzy;
        for (int sct = 0; sct < count; sct++) {
            const uint32_t b = sector_blocks + (uint32_t)sct * 16;
            st_stx_sector *sec = &t->sectors[sct];
            const uint32_t data_offset = le32(pb, b);
            sec->bit_pos = le16(pb, b + 4);
            sec->read_time = le16(pb, b + 6);
            for (int k = 0; k < 4; k++) sec->id[k] = zx_pb_byte(pb, b + 8 + k);
            sec->crc = (uint16_t)((zx_pb_byte(pb, b + 12) << 8) | zx_pb_byte(pb, b + 13));
            sec->status = zx_pb_byte(pb, b + 14);
            if ((sec->status & 0x10) == 0) {
                const uint32_t n = 128u << (sec->id[3] & 3);
                const uint32_t src = track_data + data_offset;
                if (src + n > end) { *error = "STX sector data is truncated"; return false; }
                sec->has_data = true;
                sec->data_off = src;
                if (sec->status & 0x80) {
                    if (fz + n > track_data) { *error = "STX fuzzy mask is truncated"; return false; }
                    sec->has_fuzzy = true;
                    sec->fuzzy_off = fz;
                    fz += n;
                }
            }
            t->nsectors++;
        }
        pos += block_size;
    }
    d->tracks = max_track + 1;
    d->sides = two_sides ? 2 : 1;
    d->spt = 9;
    const st_stx_track *t1 = stx_track(d, 1, 0);
    if (t1 && t1->nsectors) d->spt = t1->nsectors;
    d->loaded = true;
    return true;
}

static bool ext_is(const char *name, const char *ext)
{
    if (!name) return false;
    const size_t n = strlen(name), e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++)
        if ((name[n - e + i] | 0x20) != ext[i]) return false;
    return true;
}

bool st_floppy_load(st_floppy *f, int drive, zx_pbuf *pb, const char *name, const char **error)
{
    static const char *dummy;
    if (!error) error = &dummy;
    *error = "";
    st_disk d;
    memset(&d, 0, sizeof d);
    zx_pb_init(&d.img);
    d.tracks = 80;
    d.sides = 2;
    d.spt = 9;
    st_disk *slot = &f->drives[drive & 1];
    d.head = slot->head;
    bool ok;
    if (ext_is(name, ".stx") || zx_pb_eq(pb, 0, "RSY\0", 4)) {
        d.img = *pb;
        ok = load_stx(&d, error);
    } else if (ext_is(name, ".msa") || (pb->size >= 2 && zx_pb_byte(pb, 0) == 0x0e && zx_pb_byte(pb, 1) == 0x0f)) {
        ok = load_msa(&d, pb, error);
        zx_pb_free(pb);
    } else {
        d.img = *pb;
        ok = decode_geometry(&d, pb->size);
        if (ok) d.loaded = true;
        else *error = "ST image size is not a known floppy geometry";
    }
    zx_pb_init(pb);
    if (!ok) {
        free_disk(&d);
        return false;
    }
    free_disk(slot);
    *slot = d;
    return true;
}

void st_floppy_eject(st_floppy *f, int drive) { free_disk(&f->drives[drive & 1]); }

// --- the DMA chip and the WD1772 ------------------------------------------------------
static int selected_drive(const st_floppy *f)
{
    if ((f->psg_a & 2) == 0) return 0;
    if ((f->psg_a & 4) == 0) return 1;
    return -1;
}

static int selected_side(const st_floppy *f) { return (f->psg_a & 1) ? 0 : 1; }

static st_disk *cur(st_floppy *f)
{
    const int d = selected_drive(f);
    return &f->drives[d == 1 ? 1 : 0];
}

static st_disk *media(st_floppy *f)
{
    const int d = selected_drive(f);
    if (d < 0 || !f->drives[d].loaded) return NULL;
    return &f->drives[d];
}

uint16_t st_floppy_dma_status(const st_floppy *f)
{
    uint16_t v = 0;
    if (!f->dma_error) v |= 1;
    if (f->dma_count == 0) v |= 2;
    return v;
}

void st_floppy_dma_mode_w(st_floppy *f, uint16_t v)
{
    if ((f->dma_mode ^ v) & 0x100) {
        f->dma_error = false;
        f->dma_bytes = 0;
    }
    f->dma_mode = v;
}

void st_floppy_tick(st_floppy *f, int cycles)
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

void st_floppy_dma_addr_w(st_floppy *f, int which, uint8_t v)
{
    if (which == 0) f->dma_addr = (f->dma_addr & 0x00ffff) | ((uint32_t)v << 16);
    else if (which == 1) f->dma_addr = (f->dma_addr & 0xff00ff) | ((uint32_t)v << 8);
    else f->dma_addr = (f->dma_addr & 0xffff00) | v;
}

uint8_t st_floppy_dma_addr_r(const st_floppy *f, int which)
{
    if (which == 0) return (uint8_t)(f->dma_addr >> 16);
    if (which == 1) return (uint8_t)(f->dma_addr >> 8);
    return (uint8_t)f->dma_addr;
}

static uint32_t rotation_byte(const st_floppy *f) { return (uint32_t)((f->cycles % CYCLES_PER_REV) / CYCLES_PER_BYTE); }

static uint8_t fdc_status(st_floppy *f)
{
    uint8_t v = f->fdc_status;
    if (f->fdc_busy) v |= 0x01;
    const bool type1 = (f->last_cmd & 0x80) == 0;
    st_disk *c = cur(f);
    if (type1 && c->stx) {
        if (c->head == 0) v |= 0x04;
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

uint16_t st_floppy_dma_data_r(st_floppy *f)
{
    if (f->dma_mode & 0x10) return f->dma_count;
    if (f->dma_mode & 0x08) return 0xff;            // ACSI: nothing there
    const int reg = (f->dma_mode >> 1) & 3;
    if (reg == 0) return fdc_status(f);
    if (reg == 1) return f->fdc_track;
    if (reg == 2) return f->fdc_sector;
    return f->fdc_data;
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

static void do_read_address(st_floppy *f)
{
    if (!media(f)) {
        f->fdc_status = 0x10;
        finish_command(f);
        return;
    }
    const uint8_t id[6] = { f->fdc_track, (uint8_t)selected_side(f), f->fdc_sector, 2, 0, 0 };
    f->fdc_data = id[0];
    if (f->dma_count && f->dma_addr + 6 <= f->ram_size) {
        for (int i = 0; i < 6; i++) ram_w(f, f->dma_addr + (uint32_t)i, id[i]);
        f->dma_addr += 6;
        f->dma_count--;
    }
    finish_command(f);
}

static void do_dma_rw(st_floppy *f, bool write)
{
    const int side = selected_side(f);
    st_disk *d = media(f);
    if (!d) {
        f->dma_error = true;
        f->fdc_status = 0x10;
        finish_command(f);
        return;
    }
    while (f->dma_count) {
        const int64_t off = sector_off(d, f->fdc_track, side, f->fdc_sector);
        if (off < 0 || !f->ram_read || f->dma_addr + ST_SECTOR > f->ram_size) {
            f->dma_error = true;
            f->fdc_status = 0x10;
            finish_command(f);
            return;
        }
        if (write) {
            for (int i = 0; i < ST_SECTOR; i++)
                if (!zx_pb_poke(&d->img, (uint32_t)off + (uint32_t)i, ram_r(f, f->dma_addr + (uint32_t)i))) {
                    f->fdc_status = 0x40;           // write protected (left on the card)
                    finish_command(f);
                    return;
                }
        } else {
            for (int i = 0; i < ST_SECTOR; i++) ram_w(f, f->dma_addr + (uint32_t)i, zx_pb_byte(&d->img, (uint32_t)off + (uint32_t)i));
        }
        f->dma_addr += ST_SECTOR;
        f->dma_count--;
        f->fdc_sector++;
        if (f->fdc_sector > d->spt) {
            f->fdc_sector = 1;
            break;
        }
    }
    finish_command(f);
}

static int sec_size(const st_stx_sector *s) { return 128 << (s->id[3] & 3); }

static uint8_t sec_byte(const st_disk *d, const st_stx_sector *s, int i)
{
    if (s->written) return s->written[i];
    return s->has_data ? zx_pb_byte(&d->img, s->data_off + (uint32_t)i) : 0;
}

static int stx_next_sector(st_floppy *f, const st_stx_track *t, bool match_sector, uint32_t *wait_bytes)
{
    const uint32_t len = t->bytes ? t->bytes : 6250;
    const uint32_t now = rotation_byte(f) % len;
    int best = -1;
    uint32_t best_dist = 0;
    for (int i = 0; i < t->nsectors; i++) {
        const st_stx_sector *sec = &t->sectors[i];
        if (match_sector && (sec->id[0] != f->fdc_track || sec->id[2] != f->fdc_sector)) continue;
        const uint32_t id_end = (sec->bit_pos / 8 + 6) % len;
        uint32_t dist = (id_end + len - now) % len;
        if (dist == 0) dist = len;
        if (best < 0 || dist < best_dist) {
            best = i;
            best_dist = dist;
        }
    }
    if (wait_bytes) *wait_bytes = best_dist;
    return best;
}

static void dma_push_byte(st_floppy *f, uint8_t b)
{
    if (f->dma_count == 0 || !f->ram_write) return;
    if (f->dma_addr < f->ram_size) ram_w(f, f->dma_addr, b);
    f->dma_addr = (f->dma_addr + 1) & 0xffffff;
    if (++f->dma_bytes >= ST_SECTOR) {
        f->dma_bytes = 0;
        f->dma_count--;
    }
}

static st_stx_track *cur_stx_track(st_floppy *f)
{
    st_disk *m = media(f);
    return m ? stx_track(m, m->head, selected_side(f)) : NULL;
}

static void stx_read_sector(st_floppy *f, uint8_t cmd)
{
    st_stx_track *t = cur_stx_track(f);
    if (!t || !t->nsectors) {
        f->fdc_status = 0x10;
        stx_finish(f, 5 * CYCLES_PER_REV);
        return;
    }
    const st_disk *d = media(f);
    uint64_t total = 0;
    const uint64_t saved = f->cycles;
    for (;;) {
        uint32_t wait = 0;
        const int idx = stx_next_sector(f, t, true, &wait);
        if (idx < 0 || (t->sectors[idx].status & 0x10)) {
            f->fdc_status |= 0x10;
            total += 5 * CYCLES_PER_REV;
            break;
        }
        const st_stx_sector *sec = &t->sectors[idx];
        const int n = sec_size(sec);
        uint64_t cycles = (uint64_t)(wait + ID_TO_DATA) * CYCLES_PER_BYTE;
        if (sec->read_time) cycles += (uint64_t)sec->read_time * 8;
        else cycles += (uint64_t)(n + 2) * CYCLES_PER_BYTE;
        total += cycles;
        f->cycles += cycles;
        // (a sector without data reads as nothing, as dsp-cpp's empty buffer)
        const int have = (sec->has_data || sec->written) ? n : 0;
        uint8_t last = 0;
        for (int i = 0; i < have; i++) {
            uint8_t b = sec_byte(d, sec, i);
            if (sec->has_fuzzy && !sec->written) {
                const uint8_t fz = zx_pb_byte(&d->img, sec->fuzzy_off + (uint32_t)i);
                b = (uint8_t)((b & fz) | (random_byte(f) & ~fz));
            }
            dma_push_byte(f, b);
            last = b;
        }
        if (have) f->fdc_data = last;
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
    st_stx_track *t = cur_stx_track(f);
    if (!t || !t->nsectors) {
        f->fdc_status = 0x10;
        stx_finish(f, 5 * CYCLES_PER_REV);
        return;
    }
    const st_disk *d = media(f);
    uint64_t total = 0;
    const uint64_t saved = f->cycles;
    for (;;) {
        uint32_t wait = 0;
        const int idx = stx_next_sector(f, t, true, &wait);
        if (idx < 0) {
            f->fdc_status |= 0x10;
            total += 5 * CYCLES_PER_REV;
            break;
        }
        st_stx_sector *sec = &t->sectors[idx];
        const int n = sec_size(sec);
        if (!sec->written) {
            uint8_t *w = malloc((size_t)n);
            if (!w) {
                f->fdc_status |= 0x40;
                break;
            }
            for (int i = 0; i < n; i++) w[i] = sec_byte(d, sec, i);
            sec->written = w;
        }
        for (int i = 0; i < n; i++) {
            if (f->dma_count == 0 || !f->ram_read) break;
            if (f->dma_addr < f->ram_size) sec->written[i] = ram_r(f, f->dma_addr);
            f->dma_addr = (f->dma_addr + 1) & 0xffffff;
            if (++f->dma_bytes >= ST_SECTOR) {
                f->dma_bytes = 0;
                f->dma_count--;
            }
        }
        sec->status &= (uint8_t)~(0x08 | 0x10 | 0x20 | 0x80);
        sec->has_fuzzy = false;
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
    st_stx_track *t = cur_stx_track(f);
    uint32_t wait = 0;
    const int idx = t ? stx_next_sector(f, t, false, &wait) : -1;
    if (idx < 0) {
        f->fdc_status = 0x10;
        stx_finish(f, 5 * CYCLES_PER_REV);
        return;
    }
    const st_stx_sector *sec = &t->sectors[idx];
    const uint8_t id[6] = { sec->id[0], sec->id[1], sec->id[2], sec->id[3], (uint8_t)(sec->crc >> 8), (uint8_t)sec->crc };
    for (int i = 0; i < 6; i++) dma_push_byte(f, id[i]);
    f->fdc_data = id[5];
    f->fdc_sector = sec->id[0];
    if ((sec->status & 0x08) && id_crc(sec->id) != sec->crc) f->fdc_status |= 0x08;
    stx_finish(f, wait * CYCLES_PER_BYTE);
}

// Read Track: the raw track (a dumped image, or a standard one rebuilt
// around the sectors, or noise) straight into the DMA
static void stx_read_track(st_floppy *f)
{
    st_stx_track *t = cur_stx_track(f);
    const st_disk *d = media(f);
    const uint32_t len = t && t->bytes ? t->bytes : 6250;
    uint32_t emitted = 0;
    uint8_t last = 0;
#define PUT(b) do { const uint8_t _b = (uint8_t)(b); dma_push_byte(f, _b); last = _b; emitted++; } while (0)
    if (t && t->image_len) {
        for (uint32_t i = 0; i < t->image_len; i++) PUT(zx_pb_byte(&d->img, t->image_off + i));
    } else if (t && t->nsectors) {
        for (int i = 0; i < GAP1; i++) PUT(0x4e);
        for (int s = 0; s < t->nsectors; s++) {
            const st_stx_sector *sec = &t->sectors[s];
            const int n = sec_size(sec);
            if (emitted + (uint32_t)(RAW_SECTOR_512 - 512 + n) > len) break;
            for (int i = 0; i < 12; i++) PUT(0x00);
            for (int i = 0; i < 3; i++) PUT(0xa1);
            PUT(0xfe);
            for (int i = 0; i < 4; i++) PUT(sec->id[i]);
            PUT(sec->crc >> 8);
            PUT(sec->crc);
            for (int i = 0; i < 22; i++) PUT(0x4e);
            for (int i = 0; i < 12; i++) PUT(0x00);
            for (int i = 0; i < 3; i++) PUT(0xa1);
            const uint8_t dam = (sec->status & 0x20) ? 0xf8 : 0xfb;
            PUT(dam);
            uint16_t crc = 0xffff;
            crc = crc16_add(crc, 0xa1);
            crc = crc16_add(crc, 0xa1);
            crc = crc16_add(crc, 0xa1);
            crc = crc16_add(crc, dam);
            const int have = (sec->has_data || sec->written) ? n : 0;
            for (int i = 0; i < n; i++) {
                const uint8_t b = i < have ? sec_byte(d, sec, i) : 0;
                PUT(b);
                crc = crc16_add(crc, b);
            }
            if (sec->status & 0x08) crc = (uint16_t)~crc;
            PUT(crc >> 8);
            PUT(crc);
            for (int i = 0; i < 40; i++) PUT(0x4e);
        }
        while (emitted < len) PUT(0x4e);
    } else {
        for (uint32_t i = 0; i < len; i++) PUT(random_byte(f));
    }
#undef PUT
    if (emitted) f->fdc_data = last;
    const uint32_t to_index = (len - rotation_byte(f) % len) % len;
    stx_finish(f, (to_index + len) * CYCLES_PER_BYTE);
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
    st_disk *d = cur(f);
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
            const st_stx_track *t = media(f) ? stx_track(d, d->head, selected_side(f)) : NULL;
            bool ok = false;
            if (t)
                for (int i = 0; i < t->nsectors; i++)
                    if (t->sectors[i].id[0] == f->fdc_track) ok = true;
            if (ok) {
                cycles += 15000 * 8;
            } else {
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
    if ((cmd & 0xe0) == 0x80) {
        do_dma_rw(f, false);
        return;
    }
    if ((cmd & 0xe0) == 0xa0) {
        do_dma_rw(f, true);
        return;
    }
    if (type == 0xc0) {
        do_read_address(f);
        return;
    }
    if (type == 0xe0) {
        finish_command(f);
        return;
    }
    f->fdc_status = 0x10;
    finish_command(f);
}

void st_floppy_dma_data_w(st_floppy *f, uint16_t v)
{
    if (f->dma_mode & 0x10) {
        f->dma_count = (uint8_t)v;
        return;
    }
    if (f->dma_mode & 0x08) return;                 // ACSI
    const int reg = (f->dma_mode >> 1) & 3;
    if (reg == 0) fdc_command(f, (uint8_t)v);
    else if (reg == 1) f->fdc_track = (uint8_t)v;
    else if (reg == 2) f->fdc_sector = (uint8_t)v;
    else f->fdc_data = (uint8_t)v;
}
