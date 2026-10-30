// zx_nec765.c - uPD765 + .DSK images, C11 port of dsp-cpp's
// machine/nec765.cpp. See zx_nec765.h.
#include "zx_nec765.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t k_bytes_in_cmd[32] = {
    1, 1, 9, 3, 2, 9, 9, 2,
    1, 9, 2, 1, 9, 6, 1, 3,
    1, 9, 1, 1, 1, 1, 1, 1,
    1, 9, 1, 1, 1, 1, 9, 1
};

static const nec_sector k_no_sector;
static const nec_track k_no_track;

#define B(o) zx_pb_byte(P, (uint32_t)(o))

static uint32_t sector_nominal_size(uint8_t n)
{
    if (n > 7) return 0x4000;
    return 1u << (n + 7u);
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static nec_disk *cur(zx_nec765 *f) { return &f->disks[f->current_drive]; }

static const nec_track *trk(const nec_disk *d)
{
    if (d->side_actual >= 2 || d->track_actual >= NEC_MAX_TRACKS) return &k_no_track;
    const nec_track *t = d->tracks[d->side_actual][d->track_actual];
    return t ? t : &k_no_track;
}

static const nec_sector *sec(const nec_track *t, unsigned i)
{
    return i < t->number_sector ? &t->sectors[i] : &k_no_sector;
}

// ---------------------------------------------------------------------------
// images
// ---------------------------------------------------------------------------
static void disk_clear(nec_disk *d)
{
    for (int s = 0; s < 2; s++)
        for (int t = 0; t < NEC_MAX_TRACKS; t++) {
            if (d->tracks[s][t]) free(d->tracks[s][t]->sectors);
            free(d->tracks[s][t]);
        }
    zx_pb_free(&d->pb);
    memset(d, 0, sizeof *d);
}

static void apply_protection_patches(nec_disk *d)
{
    const nec_track *t0 = d->tracks[0][0];
    if (!t0 || !t0->has_data || !t0->number_sector) return;
    const uint32_t len = t0->sectors[0].data_length;
    if (len == 0 || len > t0->track_length) return;
    uint8_t *s0 = zx_pb_flatten(&d->pb, t0->data, len);
    if (!s0) return;
    const uint32_t crc = crc32_of(s0, len);
    free(s0);
    nec_track *t;
    switch (crc) {
    case 0x8c817e25:
    case 0x4b616c83:
        if ((t = d->tracks[0][40]) && t->number_sector > 6) t->sectors[6].sector_size = 2;
        break;
    case 0x57a3276f:
        if ((t = d->tracks[0][39]) && t->number_sector > 10) t->sectors[10].sector_size = 0;
        break;
    case 0xf05fe06e:
        if ((t = d->tracks[0][39]) && t->number_sector > 0) t->sectors[0].sector_size = 2;
        break;
    default:
        break;
    }
}

bool nec_load_pbuf(zx_nec765 *f, int drive, zx_pbuf *file, const char **error)
{
    nec_disk fresh;
    memset(&fresh, 0, sizeof fresh);
    fresh.pb = *file;               // the disk owns it now (freed with it)
    zx_pb_init(file);
    const zx_pbuf *P = &fresh.pb;
    const size_t size = P->size;
    if (drive < 0 || drive >= NEC_DRIVES) {
        if (error) *error = "invalid drive number";
        goto fail;
    }
    if (size < 0x100) {
        if (error) *error = "file too small to be a .dsk image";
        goto fail;
    }
    bool standard;
    if (zx_pb_eq(P, 0, "MV - CPC", 8)) standard = true;
    else if (zx_pb_eq(P, 0, "EXTENDED", 8)) standard = false;
    else {
        if (error) *error = "not a .dsk/.edsk image (bad signature)";
        goto fail;
    }
    const uint8_t header_tracks = B(0x30);
    const uint8_t header_sides = B(0x31);
    const uint16_t header_track_size = zx_pb_rd16(P, 0x32);
    if (header_tracks == 0 || header_sides == 0 || header_sides > 2) {
        if (error) *error = "unsupported disk geometry";
        goto fail;
    }
    const uint32_t file_length = (uint32_t)size;
    fresh.tracks_count = header_tracks;
    fresh.heads_count = header_sides;

    uint32_t offset = 0x100;
    size_t map_index = 0;
    for (uint8_t lt = 0; lt < header_tracks; lt++) {
        for (uint8_t ls = 0; ls < header_sides; ls++, map_index++) {
            uint32_t full_track_size;
            if (standard) {
                full_track_size = header_track_size;
            } else {
                const uint8_t entry = (0x34 + map_index < size && lt < 132) ? B(0x34 + map_index) : 0;
                if (entry == 0) continue;
                full_track_size = (uint32_t)entry * 0x100u;
            }
            if (full_track_size < 0x100) continue;
            if (offset + 0x100 > file_length) goto done;
            const uint32_t th = offset;
            if (!zx_pb_eq(P, th, "Track-Info", 10)) goto done;
            const uint8_t track_num = B(th + 16);
            const uint8_t side_num = B(th + 17);
            if (side_num >= 2 || track_num >= NEC_MAX_TRACKS) {
                if (error) *error = "track header out of supported geometry";
                disk_clear(&fresh);
                return false;
            }
            nec_track *track = fresh.tracks[side_num][track_num];
            if (track) {
                free(track->sectors);
            } else if (!(track = fresh.tracks[side_num][track_num] = calloc(1, sizeof *track))) {
                goto oom;
            }
            memset(track, 0, sizeof *track);
            track->track_number = track_num;
            track->side_number = side_num;
            track->number_sector = B(th + 21) < 32 ? B(th + 21) : 32;
            if (track->number_sector && !(track->sectors = calloc(track->number_sector, sizeof(nec_sector))))
                goto oom;

            uint32_t position = 0;
            uint32_t sector_ptr = offset + 24;
            for (uint8_t i = 0; i < track->number_sector; i++) {
                if (sector_ptr + 8 > file_length) break;
                const uint32_t sd = sector_ptr;
                sector_ptr += 8;
                nec_sector *s = &track->sectors[i];
                s->track = B(sd);
                s->head = B(sd + 1);
                s->sector = B(sd + 2);
                s->sector_size = B(sd + 3);
                s->status1 = B(sd + 4);
                s->status2 = B(sd + 5);
                const uint32_t nominal = sector_nominal_size(s->sector_size);
                if (standard) {
                    s->data_length = (uint16_t)(nominal < 0xffff ? nominal : 0xffff);
                } else {
                    s->data_length = zx_pb_rd16(P, sd + 6);
                    if (s->data_length == 0) s->data_length = (uint16_t)(nominal < 0xffff ? nominal : 0xffff);
                }
                s->position = position;
                position += s->data_length;
                if (nominal != 0 && s->data_length > nominal) {
                    uint32_t multiplicity = s->data_length / nominal;
                    if (multiplicity > 1) {
                        if (multiplicity > 4) multiplicity = 4;
                        s->multi = true;
                        fresh.multi_counter = 0;
                        fresh.multi_max = (uint8_t)multiplicity;
                    }
                }
            }
            offset += 0x100;
            const uint32_t data_size = full_track_size - 0x100u;
            const uint32_t available = offset < file_length ? file_length - offset : 0;
            const uint32_t to_copy = data_size < available ? data_size : available;
            track->track_length = to_copy;
            track->data = offset;
            track->has_data = to_copy != 0;
            offset += data_size;
        }
    }
done:
    apply_protection_patches(&fresh);
    fresh.open = true;
    if (fresh.multi_max == 0) fresh.multi_max = 1;
    if (fresh.multi_counter >= fresh.multi_max) fresh.multi_counter = 0;
    disk_clear(&f->disks[drive]);
    f->disks[drive] = fresh;
    return true;
oom:
    if (error) *error = "out of memory for the disk";
fail:
    disk_clear(&fresh);
    return false;
}

bool nec_load_disk(zx_nec765 *f, int drive, const uint8_t *data, size_t size, const char **error)
{
    zx_pbuf pb;
    if (!zx_pb_from_mem(&pb, data, size)) {
        if (error) *error = "out of memory for the disk";
        return false;
    }
    return nec_load_pbuf(f, drive, &pb, error);
}

bool nec_load_disk_take(zx_nec765 *f, int drive, uint8_t *file, size_t size, const char **error)
{
    zx_pbuf pb;
    zx_pb_take_flat(&pb, file, size);
    return nec_load_pbuf(f, drive, &pb, error);
}

void nec_eject(zx_nec765 *f, int drive)
{
    if (drive >= 0 && drive < NEC_DRIVES) disk_clear(&f->disks[drive]);
}

// ---------------------------------------------------------------------------
// controller
// ---------------------------------------------------------------------------
void nec_init(zx_nec765 *f)
{
    memset(f, 0, sizeof *f);
    nec_reset(f);
}

void nec_free(zx_nec765 *f)
{
    for (int d = 0; d < NEC_DRIVES; d++) disk_clear(&f->disks[d]);
}

void nec_reset(zx_nec765 *f)
{
    f->floppy_motor = false;
    f->current_drive = 0;
    f->exec_cmd_phase = f->result_phase = false;
    f->status_register = 0x80;
    f->st0 = f->st1 = f->st2 = f->st3 = 0;
    f->command_pointer = f->result_pointer = f->result_counter = 0;
    f->data_pointer = f->data_length = f->counter = 0;
    f->seek_track_flag = f->tc = f->tc_done = false;
    memset(f->command, 0, sizeof f->command);
    memset(f->result, 0, sizeof f->result);
    for (int i = 0; i < NEC_DRIVES; i++) {
        nec_disk *d = &f->disks[i];
        d->track_actual = d->side_actual = d->sector_actual = d->sector_read_track = 0;
        if (d->multi_max == 0) d->multi_max = 1;
        if (d->multi_counter >= d->multi_max) d->multi_counter = 0;
    }
}

void nec_write_motor(zx_nec765 *f, uint8_t value) { f->floppy_motor = value != 0; }

static void get_result7(zx_nec765 *f)
{
    nec_disk *d = cur(f);
    const nec_sector *s = sec(trk(d), d->sector_actual);
    f->result[0] = f->st0;
    f->result[1] = f->st1;
    f->result[2] = f->st2;
    f->result[3] = s->track;
    f->result[4] = s->head;
    f->result[5] = s->sector;
    f->result[6] = s->sector_size;
    f->result_pointer = 0;
    f->result_counter = 7;
    f->exec_cmd_phase = false;
    f->result_phase = true;
    f->status_register = 0xd0;
    f->st0 = f->st1 = f->st2 = 0;
}

static void finish_transfer(zx_nec765 *f, bool abnormal)
{
    if (abnormal) {
        f->st0 |= 0x40;
        f->st1 |= 0x80;         // EN: end of cylinder / TC missing
    } else {
        f->st0 = (uint8_t)(f->st0 & ~0x40);
        f->st1 = (uint8_t)(f->st1 & ~0x80);
    }
    get_result7(f);
}

static bool find_sector(zx_nec765 *f)
{
    nec_disk *d = cur(f);
    if (!d->open || d->side_actual >= 2 || d->track_actual >= NEC_MAX_TRACKS) {
        f->st1 |= 0x04;
        f->st2 |= 0x10;
        return false;
    }
    const nec_track *t = trk(d);
    if (t->number_sector == 0) {
        f->st1 |= 0x04;
        f->st2 |= 0x10;
        return false;
    }
    if (d->sector_actual >= t->number_sector) d->sector_actual = 0;
    for (uint8_t tries = 0; tries < t->number_sector; tries++) {
        const nec_sector *s = &t->sectors[d->sector_actual];
        if (s->track == f->command[2] && s->head == f->command[3] && s->sector == f->command[4] &&
            s->sector_size == f->command[5]) {
            if (f->command[4] == f->command[6]) f->st1 |= 0x80;
            f->st1 |= (uint8_t)(s->status1 & 0x20);
            f->st2 |= (uint8_t)(s->status2 & 0x60);
            return true;
        }
        d->sector_actual++;
        if (d->sector_actual >= t->number_sector) d->sector_actual = 0;
    }
    f->st1 |= 0x04;
    f->st2 |= 0x10;
    return false;
}

static bool should_skip_sector(zx_nec765 *f)
{
    nec_disk *d = cur(f);
    const nec_sector *s = sec(trk(d), d->sector_actual);
    if (f->command[0] & 0x20) {
        if ((f->command[0] & 0x1f) == 0x06) { if (s->status2 & 0x40) return true; }
        else if ((f->command[0] & 0x1f) == 0x0c) { if (!(s->status2 & 0x40)) return true; }
    }
    return false;
}

static void clamp_transfer(zx_nec765 *f, const nec_track *t)
{
    if (f->data_pointer >= t->track_length) f->data_length = 0;
    else if (f->data_pointer + f->data_length > t->track_length) f->data_length = t->track_length - f->data_pointer;
}

static void start_read_sector(zx_nec765 *f)
{
    for (;;) {
        nec_disk *d = cur(f);
        if (!find_sector(f)) {
            f->st0 |= 0x40;
            f->st1 |= 0x04;
            get_result7(f);
            return;
        }
        if (should_skip_sector(f)) {
            if (f->command[4] == f->command[6]) {
                f->st1 &= 0x7f;
                get_result7(f);
                return;
            }
            f->command[4]++;
            continue;
        }
        const nec_track *t = trk(d);
        const nec_sector *s = &t->sectors[d->sector_actual];
        uint32_t nominal_length;
        if (f->command[5] == 0) {
            nominal_length = f->command[8];
            if (nominal_length > 0x80) nominal_length = 0x80;
        } else {
            nominal_length = sector_nominal_size(f->command[5]);
        }
        uint32_t real_length = s->data_length;
        if (real_length == 0) real_length = nominal_length;
        if (s->multi) {
            const uint32_t slice = sector_nominal_size(s->sector_size);
            if (d->multi_max == 0) d->multi_max = 1;
            d->multi_counter++;
            if (d->multi_counter >= d->multi_max) d->multi_counter = 0;
            f->data_pointer = s->position + (uint32_t)d->multi_counter * slice;
            f->data_length = nominal_length < slice ? nominal_length : slice;
        } else {
            f->data_pointer = s->position;
            f->data_length = nominal_length < real_length ? nominal_length : real_length;
        }
        clamp_transfer(f, t);
        f->counter = 0;
        f->exec_cmd_phase = true;
        f->result_phase = false;
        f->status_register = 0xf0;
        return;
    }
}

static void start_read_track(zx_nec765 *f)
{
    nec_disk *d = cur(f);
    const nec_track *t = trk(d);
    if (!d->open || t->number_sector == 0 || !t->has_data) {
        f->st0 = 0x40;
        f->st1 = 0x04;
        f->st2 = 0;
        get_result7(f);
        return;
    }
    if (d->sector_read_track >= t->number_sector) d->sector_read_track = 0;
    const nec_sector *s = &t->sectors[d->sector_read_track];
    f->data_pointer = s->position;
    f->data_length = s->data_length;
    if (f->data_length == 0) f->data_length = sector_nominal_size(s->sector_size);
    clamp_transfer(f, t);
    f->counter = 0;
    f->exec_cmd_phase = true;
    f->result_phase = false;
    f->status_register = 0xf0;
}

static void select_drive(zx_nec765 *f)
{
    f->current_drive = (uint8_t)(f->command[1] & 1);
    nec_disk *d = cur(f);
    d->side_actual = (uint8_t)((f->command[1] & 4) >> 2);
    if (d->heads_count != 0 && d->side_actual >= d->heads_count) d->side_actual = 0;
    f->st0 = (uint8_t)(f->st0 & 0xf8);
    f->st3 = (uint8_t)(f->st3 & 0xf8);
    f->st0 = (uint8_t)(f->st0 | (f->command[1] & 1) | (f->command[1] & 4));
    f->st3 = (uint8_t)(f->st3 | (f->command[1] & 1) | (f->command[1] & 4));
}

static bool seek_track(zx_nec765 *f, uint8_t track)
{
    nec_disk *d = cur(f);
    if (!d->open || d->tracks_count == 0) {
        d->track_actual = d->sector_actual = d->sector_read_track = 0;
        return false;
    }
    const bool ok = track < d->tracks_count;
    d->track_actual = ok ? track : (uint8_t)(d->tracks_count - 1);
    d->sector_actual = 0;
    d->sector_read_track = 0;
    return ok;
}

static void exec_write_command(zx_nec765 *f)
{
    switch (f->command[0] & 0x1f) {
    case 2:                     // read track
        f->st0 = f->st1 = f->st2 = 0;
        f->tc_done = false;
        select_drive(f);
        if (!cur(f)->open) {
            f->st0 |= 0x48;
            get_result7(f);
        } else {
            cur(f)->sector_read_track = 0;
            start_read_track(f);
        }
        break;
    case 3:                     // specify
        f->exec_cmd_phase = false;
        f->result_phase = false;
        f->status_register = 0x80;
        break;
    case 4: {                   // sense drive status
        f->current_drive = (uint8_t)(f->command[1] & 1);
        nec_disk *d = cur(f);
        f->st3 = (uint8_t)((f->command[1] & 1) | (f->command[1] & 4));
        if (d->open) f->st3 |= 0x20;
        if (d->write_protected) f->st3 |= 0x40;
        if (d->track_actual == 0) f->st3 |= 0x10;
        if (d->heads_count > 1) f->st3 |= 0x08;
        f->result[0] = f->st3;
        f->result_pointer = 0;
        f->result_counter = 1;
        f->exec_cmd_phase = false;
        f->result_phase = true;
        f->status_register = 0xd0;
        break;
    }
    case 5:
    case 9:                     // write data / deleted data: not writable
        f->st0 = f->st1 = f->st2 = 0;
        select_drive(f);
        if (!cur(f)->open) {
            f->st0 |= 0x48;
            get_result7(f);
            break;
        }
        f->st0 |= 0x40;
        f->st1 |= 0x02;
        get_result7(f);
        break;
    case 6:
    case 12:                    // read data / deleted data
        f->st0 = f->st1 = f->st2 = 0;
        f->tc_done = false;
        select_drive(f);
        if (!cur(f)->open) {
            f->st0 |= 0x48;
            get_result7(f);
        } else {
            start_read_sector(f);
        }
        break;
    case 7:                     // recalibrate
        f->st0 = 0x20;
        f->st1 = f->st2 = 0;
        select_drive(f);
        if (!cur(f)->open) f->st0 |= 0x48;
        else seek_track(f, 0);
        f->seek_track_flag = true;
        f->exec_cmd_phase = false;
        f->result_phase = false;
        f->status_register = 0x80;
        break;
    case 8:                     // sense interrupt status
        f->result_pointer = 0;
        if (f->seek_track_flag) {
            f->st0 = (uint8_t)((f->st0 & 0xf8) | 0x20 | (f->current_drive & 1));
            f->result[0] = f->st0;
            f->result[1] = cur(f)->track_actual;
            f->result_counter = 2;
            f->seek_track_flag = false;
        } else {
            f->result[0] = 0x80;
            f->result_counter = 1;
        }
        f->exec_cmd_phase = false;
        f->result_phase = true;
        f->status_register = 0xd0;
        break;
    case 10: {                  // read id
        f->st0 = f->st1 = f->st2 = 0;
        select_drive(f);
        nec_disk *d = cur(f);
        if (!d->open) {
            f->st0 = 0x48;
            get_result7(f);
            break;
        }
        const nec_track *t = trk(d);
        if (t->number_sector == 0) {
            f->st0 = 0x40;
            f->st1 = 0x01;
            f->result[0] = f->st0;
            f->result[1] = f->st1;
            f->result[2] = f->st2;
            f->result[3] = d->track_actual;
            f->result[4] = d->side_actual;
            f->result[5] = 0;
            f->result[6] = 0;
            f->result_pointer = 0;
            f->result_counter = 7;
            f->exec_cmd_phase = false;
            f->result_phase = true;
            f->status_register = 0xd0;
            break;
        }
        if (d->sector_actual >= t->number_sector) d->sector_actual = 0;
        get_result7(f);
        d->sector_actual++;
        if (d->sector_actual >= t->number_sector) d->sector_actual = 0;
        break;
    }
    case 15:                    // seek
        select_drive(f);
        f->st0 = 0x20;
        f->st1 = f->st2 = 0;
        if (!cur(f)->open) f->st0 |= 0x48;
        else seek_track(f, f->command[2]);
        f->seek_track_flag = true;
        f->exec_cmd_phase = false;
        f->result_phase = false;
        f->status_register = 0x80;
        break;
    default:                    // invalid
        f->result_pointer = 0;
        f->result_counter = 1;
        f->result[0] = 0x80;
        f->exec_cmd_phase = false;
        f->result_phase = true;
        f->seek_track_flag = false;
        f->status_register = 0xd0;
        break;
    }
}

static bool read_data_should_stop(zx_nec765 *f)
{
    nec_disk *d = cur(f);
    const nec_sector *s = sec(trk(d), d->sector_actual);
    bool stop = false;
    if ((f->command[0] & 0x20) == 0) {
        if ((f->command[0] & 0x1f) == 0x06) {
            if (s->status2 & 0x40) { f->st2 |= 0x40; stop = true; }
        } else if ((f->command[0] & 0x1f) == 0x0c) {
            if (!(s->status2 & 0x40)) { f->st2 |= 0x40; stop = true; }
        }
    }
    if (s->status1 & 0x20) {
        f->st1 |= 0x20;
        stop = true;
    }
    return stop;
}

static uint8_t exec_read_command(zx_nec765 *f)
{
    nec_disk *d = cur(f);
    const nec_track *t = trk(d);
    switch (f->command[0] & 0x1f) {
    case 2: {
        if (!t->has_data || f->data_length == 0) {
            f->st0 = 0x40;
            f->st1 = 0x20;
            f->st2 = 0x01;
            get_result7(f);
            return 0;
        }
        const uint8_t value = f->data_pointer < t->track_length ? zx_pb_byte(&d->pb, t->data + f->data_pointer) : 0xff;
        f->data_pointer++;
        f->counter++;
        if (f->counter >= f->data_length) {
            if (d->sector_read_track >= (uint8_t)(f->command[6] - 1) || d->sector_read_track + 1 >= t->number_sector) {
                f->st1 |= 0x80;
                get_result7(f);
            } else {
                d->sector_read_track++;
                start_read_track(f);
            }
        }
        return value;
    }
    case 6:
    case 12: {
        if (!t->has_data || f->data_length == 0) {
            f->st0 = 0x40;
            f->st1 = 0x20;
            f->st2 = 0x01;
            get_result7(f);
            return 0;
        }
        const nec_sector *s = sec(t, d->sector_actual);
        const uint8_t value = f->data_pointer < t->track_length ? zx_pb_byte(&d->pb, t->data + f->data_pointer) : 0xff;
        f->data_pointer++;
        f->counter++;
        if (f->counter >= f->data_length) {
            if (f->tc_done || f->command[4] == f->command[6] || read_data_should_stop(f)) {
                // MAME: R == EOT without TC -> abnormal + EN; with TC -> normal
                const bool abnormal = !f->tc_done;
                if (s->sector_size > 5) f->st1 |= 0x20;
                finish_transfer(f, abnormal);
            } else {
                f->command[4]++;
                d->sector_actual++;
                if (d->sector_actual >= t->number_sector) d->sector_actual = 0;
                start_read_sector(f);
            }
        }
        return value;
    }
    default:
        return 0xff;
    }
}

static uint8_t get_result(zx_nec765 *f)
{
    const uint8_t value = f->result[f->result_pointer];
    f->result_pointer++;
    if (f->result_pointer >= f->result_counter) {
        f->status_register = 0x80;
        f->result_phase = false;
        f->result_pointer = 0;
        f->result_counter = 0;
        memset(f->result, 0, sizeof f->result);
        memset(f->command, 0, sizeof f->command);
    }
    return value;
}

void nec_write_data(zx_nec765 *f, uint8_t value)
{
    if (f->result_phase) return;
    if (!f->exec_cmd_phase) {
        if (f->command_pointer == 0) {
            f->command[0] = value;
            f->command_pointer = 1;
            f->status_register = 0x90;
        } else if (f->command_pointer < sizeof f->command) {
            f->command[f->command_pointer++] = value;
        }
        if (f->command_pointer >= k_bytes_in_cmd[f->command[0] & 0x1f]) {
            f->command_pointer = 0;
            f->status_register = 0x80;
            exec_write_command(f);
        }
    } else {
        exec_write_command(f);
    }
}

uint8_t nec_read_data(zx_nec765 *f)
{
    if (f->exec_cmd_phase) return exec_read_command(f);
    if (f->result_phase) return get_result(f);
    return 0xff;
}

uint8_t nec_read_status(zx_nec765 *f)
{
    if (f->result_phase) f->status_register = 0xd0;
    else if (f->exec_cmd_phase) f->status_register = 0xf0;
    else f->status_register = 0x80;
    return f->status_register;
}
