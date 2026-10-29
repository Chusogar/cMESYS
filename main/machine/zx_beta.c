// zx_beta.c - Beta 128 + WD1793 + TR-DOS disk, C11 port of dsp-cpp's
// machine/beta128.cpp, wd1793.cpp and trdos_disk.cpp. See zx_beta.h.
#include "zx_beta.h"
#include <stdlib.h>
#include <string.h>

#define TRACK_BYTES (TRD_SECTORS * TRD_SECTOR)

static const uint8_t k_zero_sector[TRD_SECTOR];

// ---------------------------------------------------------------------------
// TR-DOS disk (trdos_disk.cpp)
// ---------------------------------------------------------------------------
static void eject(zx_beta *b)
{
    for (int t = 0; t < TRD_MAX_TRACKS; t++)
        for (int h = 0; h < 2; h++) {
            free(b->track[t][h]);
            b->track[t][h] = NULL;
        }
    b->tracks = b->heads = 0;
    b->present = false;
}

static void format(zx_beta *b, int tracks, int heads)
{
    eject(b);
    b->tracks = tracks;
    b->heads = heads;
    b->present = true;
}

static bool in_geometry(const zx_beta *b, int track, int head, int sector)
{
    return b->present && track >= 0 && track < b->tracks && head >= 0 && head < b->heads && sector >= 1 &&
           sector <= TRD_SECTORS;
}

// The sector, allocating its track when `alloc` (NULL: off the disk or no memory).
static uint8_t *sector_w(zx_beta *b, int track, int head, int sector)
{
    if (!in_geometry(b, track, head, sector)) return NULL;
    uint8_t **t = &b->track[track][head];
    if (!*t && !(*t = calloc(1, TRACK_BYTES))) return NULL;
    return *t + (sector - 1) * TRD_SECTOR;
}

static const uint8_t *sector_r(const zx_beta *b, int track, int head, int sector)
{
    if (!in_geometry(b, track, head, sector)) return NULL;
    const uint8_t *t = b->track[track][head];
    return t ? t + (sector - 1) * TRD_SECTOR : k_zero_sector;
}

static bool load_trd(zx_beta *b, const uint8_t *data, size_t size, const char **error)
{
    // The disk info sector (track 0, side 0, sector 9) holds the geometry in
    // the type byte at +$E3; 320 KB images are ambiguous otherwise, and many
    // are cut short after the last used sector.
    const size_t type_off = 8u * TRD_SECTOR + 0xe3;
    if (size <= type_off) {
        if (error) *error = "TRD image is too small";
        return false;
    }
    int tracks = 0, heads = 0;
    switch (data[type_off]) {
    case 0x16: tracks = 80; heads = 2; break;
    case 0x17: tracks = 40; heads = 2; break;
    case 0x18: tracks = 80; heads = 1; break;
    case 0x19: tracks = 40; heads = 1; break;
    default: break;
    }
    if (tracks == 0) {
        if (size > 80 * TRACK_BYTES) { tracks = 80; heads = 2; }
        else if (size > 40 * TRACK_BYTES) { tracks = 40; heads = 2; }
        else { tracks = 40; heads = 1; }
    }
    format(b, tracks, heads);
    for (int i = 0; i < tracks * heads; i++) {
        const size_t off = (size_t)i * TRACK_BYTES;
        if (off >= size) break;
        const size_t n = size - off < TRACK_BYTES ? size - off : TRACK_BYTES;
        bool used = false;
        for (size_t k = 0; k < n && !used; k++) used = data[off + k] != 0;
        if (!used && i != 0) continue;
        uint8_t *t = calloc(1, TRACK_BYTES);
        if (!t) {
            eject(b);
            if (error) *error = "out of memory for the disk";
            return false;
        }
        memcpy(t, data + off, n);
        b->track[i / heads][i % heads] = t;
    }
    return true;
}

// ---------------------------------------------------------------------------
// .trd a track at a time
// ---------------------------------------------------------------------------
static int geometry(zx_beta *b, uint8_t type, size_t size)
{
    int tracks = 0, heads = 0;
    switch (type) {
    case 0x16: tracks = 80; heads = 2; break;
    case 0x17: tracks = 40; heads = 2; break;
    case 0x18: tracks = 80; heads = 1; break;
    case 0x19: tracks = 40; heads = 1; break;
    default: break;
    }
    if (tracks == 0) {
        if (size > 80 * TRACK_BYTES) { tracks = 80; heads = 2; }
        else if (size > 40 * TRACK_BYTES) { tracks = 40; heads = 2; }
        else { tracks = 40; heads = 1; }
    }
    format(b, tracks, heads);
    return tracks * heads;
}

static bool stream_commit(zx_beta *b, const char **error)
{
    const int i = (int)((b->spos - 1) / TRACK_BYTES);
    if (i == 0) geometry(b, b->sbuf[8u * TRD_SECTOR + 0xe3], b->ssize);
    if (i >= b->tracks * b->heads) return true;
    bool used = i == 0;
    for (int k = 0; k < TRACK_BYTES && !used; k++) used = b->sbuf[k] != 0;
    if (used) {
        b->track[i / b->heads][i % b->heads] = b->sbuf;
        if (!(b->sbuf = malloc(TRACK_BYTES))) {
            eject(b);
            if (error) *error = "out of memory for the disk";
            return false;
        }
    }
    memset(b->sbuf, 0, TRACK_BYTES);
    return true;
}

bool zx_beta_stream_begin(zx_beta *b, size_t size, const char **error)
{
    eject(b);
    free(b->sbuf);
    b->spos = 0;
    b->ssize = (uint32_t)size;
    if (size <= 8u * TRD_SECTOR + 0xe3) {
        b->sbuf = NULL;
        if (error) *error = "TRD image is too small";
        return false;
    }
    if (!(b->sbuf = calloc(1, TRACK_BYTES))) {
        if (error) *error = "out of memory for the disk";
        return false;
    }
    return true;
}

bool zx_beta_stream_data(zx_beta *b, const uint8_t *data, size_t n, const char **error)
{
    if (!b->sbuf) return false;
    while (n) {
        const uint32_t at = b->spos % TRACK_BYTES;
        size_t k = TRACK_BYTES - at;
        if (k > n) k = n;
        memcpy(b->sbuf + at, data, k);
        b->spos += (uint32_t)k;
        data += k;
        n -= k;
        if (b->spos % TRACK_BYTES == 0 && !stream_commit(b, error)) {
            free(b->sbuf);
            b->sbuf = NULL;
            return false;
        }
    }
    return true;
}

bool zx_beta_stream_end(zx_beta *b, const char **error)
{
    if (!b->sbuf) return false;
    bool ok = true;
    if (b->spos % TRACK_BYTES) ok = stream_commit(b, error);
    free(b->sbuf);
    b->sbuf = NULL;
    if (ok && !b->present) {
        if (error) *error = "TRD image is too small";
        ok = false;
    }
    return ok;
}

static bool load_scl(zx_beta *b, const uint8_t *data, size_t size, const char **error)
{
    if (size < 9 || memcmp(data, "SINCLAIR", 8) != 0) {
        if (error) *error = "not an SCL image";
        return false;
    }
    const int nfiles = data[8];
    if (nfiles > 128) {
        if (error) *error = "SCL catalogue is invalid";
        return false;
    }
    const size_t header = 9 + (size_t)nfiles * 14;
    if (size < header) {
        if (error) *error = "SCL image is truncated";
        return false;
    }
    format(b, 80, 2);
    // TR-DOS logical tracks interleave sides: 0 = C0H0, 1 = C0H1, 2 = C1H0...
    // Files start at logical track 1; track 0 side 0 is the catalogue.
    int log_track = 1, sec0 = 0, used = 0;
    size_t data_off = header;
    for (int i = 0; i < nfiles; i++) {
        const uint8_t *h = data + 9 + (size_t)i * 14;
        const int nsec = h[13];
        uint8_t *cat = sector_w(b, 0, 0, 1 + (i * 16) / TRD_SECTOR);
        if (!cat) goto oom;
        cat += (i * 16) % TRD_SECTOR;
        memcpy(cat, h, 14);
        cat[14] = (uint8_t)sec0;
        cat[15] = (uint8_t)log_track;
        for (int s = 0; s < nsec; s++) {
            const int cyl = log_track / b->heads, head = log_track % b->heads;
            if (!in_geometry(b, cyl, head, sec0 + 1)) {
                eject(b);
                if (error) *error = "SCL files do not fit on a DS/80 disk";
                return false;
            }
            if (data_off < size) {
                uint8_t *dest = sector_w(b, cyl, head, sec0 + 1);
                if (!dest) goto oom;
                const size_t chunk = size - data_off < TRD_SECTOR ? size - data_off : TRD_SECTOR;
                memcpy(dest, data + data_off, chunk);
                data_off += chunk;
            }
            ++used;
            if (++sec0 >= TRD_SECTORS) {
                sec0 = 0;
                ++log_track;
            }
        }
    }
    {
        uint8_t *info = sector_w(b, 0, 0, 9);
        if (!info) goto oom;
        info[0xe1] = (uint8_t)sec0;
        info[0xe2] = (uint8_t)log_track;
        info[0xe3] = 0x16;              // 80 tracks, double sided
        info[0xe4] = (uint8_t)nfiles;
        const int free_secs = 80 * 2 * 16 - 16 - used;
        info[0xe5] = (uint8_t)(free_secs & 0xff);
        info[0xe6] = (uint8_t)((free_secs >> 8) & 0xff);
        info[0xe7] = 0x10;
        info[0xe8] = 0;
        memset(info + 0xe9, 0x20, 10);
        info[0xf3] = 0;
        info[0xf4] = 0;
        memcpy(info + 0xf5, "SCLDISK ", 8);
    }
    return true;
oom:
    eject(b);
    if (error) *error = "out of memory for the disk";
    return false;
}

// ---------------------------------------------------------------------------
// WD1793 (wd1793.cpp)
// ---------------------------------------------------------------------------
enum { K_BUSY = 0x01, K_DRQ = 0x02, K_TRACK0 = 0x04, K_RNF = 0x10, K_WP = 0x40, K_NOTREADY = 0x80 };

static void wd_reset(zx_beta *b)
{
    b->status = 0;
    b->trk = 0;
    b->sec = 1;
    b->data = 0;
    b->command = 0;
    b->drq = b->intrq = b->busy = false;
    b->type1 = true;
    b->writing = false;
    b->index_pulse = false;
    b->buf_len = b->buf_pos = 0;
}

static bool ready(const zx_beta *b) { return b->present && b->drive == 0; }

static void finish_type1(zx_beta *b)
{
    b->type1 = true;
    b->busy = false;
    b->drq = false;
    b->intrq = true;
    b->status = 0;
}

static void complete_io(zx_beta *b, bool rnf)
{
    b->type1 = false;
    b->busy = false;
    b->drq = false;
    b->intrq = true;
    b->writing = false;
    b->status = rnf ? K_RNF : 0;
}

static void start_read_sector(zx_beta *b)
{
    b->type1 = false;
    b->writing = false;
    const uint8_t *src = ready(b) ? sector_r(b, b->trk, b->side, b->sec) : NULL;
    if (!src) {
        complete_io(b, true);
        return;
    }
    memcpy(b->buf, src, TRD_SECTOR);
    b->buf_len = TRD_SECTOR;
    b->buf_pos = 0;
    b->busy = true;
    b->drq = true;
    b->intrq = false;
    b->status = 0;
}

static void start_write_sector(zx_beta *b)
{
    b->type1 = false;
    b->writing = true;
    if (!ready(b) || !sector_r(b, b->trk, b->side, b->sec)) {
        complete_io(b, true);
        return;
    }
    memset(b->buf, 0, TRD_SECTOR);
    b->buf_len = TRD_SECTOR;
    b->buf_pos = 0;
    b->busy = true;
    b->drq = true;
    b->intrq = false;
    b->status = 0;
}

static void start_read_address(zx_beta *b)
{
    b->type1 = false;
    b->writing = false;
    if (!ready(b) || !sector_r(b, b->trk, b->side, 1)) {
        complete_io(b, true);
        return;
    }
    const uint8_t id[6] = { b->trk, (uint8_t)b->side, 1, 1, 0, 0 };
    memcpy(b->buf, id, 6);
    b->buf_len = 6;
    b->sec = b->trk;            // the controller copies the track address into the sector register
    b->buf_pos = 0;
    b->busy = true;
    b->drq = true;
    b->intrq = false;
    b->status = 0;
}

static uint8_t wd_status_r(zx_beta *b)
{
    uint8_t value = 0;
    if (!ready(b)) value |= K_NOTREADY;
    if (b->busy) value |= K_BUSY;
    if (b->type1) {
        if (b->trk == 0) value |= K_TRACK0;
        value |= 0x20;          // head loaded
        b->index_pulse = !b->index_pulse;
        if (b->index_pulse) value |= 0x02;
    } else {
        if (b->drq) value |= K_DRQ;
        if (b->status & K_RNF) value |= K_RNF;
        if (b->status & K_WP) value |= K_WP;
    }
    return value;
}

static void wd_command_w(zx_beta *b, uint8_t value)
{
    b->command = value;
    b->intrq = false;
    const uint8_t type = (uint8_t)(value & 0xf0);
    if (type == 0xd0) {         // force interrupt
        b->busy = false;
        b->drq = false;
        b->writing = false;
        b->type1 = true;
        if (value & 0x08) b->intrq = true;
        return;
    }
    b->busy = true;
    b->drq = false;
    if (type <= 0x70) {
        if (type == 0x00) b->trk = 0;
        else if (type == 0x10) b->trk = b->data;
        else if (type == 0x40 || type == 0x50) { if (b->trk < 255) b->trk++; }
        else if (type == 0x60 || type == 0x70) { if (b->trk > 0) b->trk--; }
        finish_type1(b);
        return;
    }
    if (type == 0x80 || type == 0x90) { start_read_sector(b); return; }
    if (type == 0xa0 || type == 0xb0) { start_write_sector(b); return; }
    if (type == 0xc0) { start_read_address(b); return; }
    if (type == 0xe0 || type == 0xf0) {     // read/write track (FORMAT): instant
        complete_io(b, false);
        return;
    }
    complete_io(b, true);
}

static uint8_t wd_data_r(zx_beta *b)
{
    if (!b->drq || b->buf_pos >= b->buf_len) return b->data;
    b->data = b->buf[b->buf_pos++];
    if (b->buf_pos >= b->buf_len) {
        const bool multi = (b->command & 0x10) != 0;
        if (multi && b->sec < TRD_SECTORS) {
            b->sec++;
            start_read_sector(b);
        } else {
            complete_io(b, false);
        }
    }
    return b->data;
}

static void wd_data_w(zx_beta *b, uint8_t value)
{
    b->data = value;
    if (!b->writing || !b->drq) return;
    if (b->buf_pos < b->buf_len) b->buf[b->buf_pos++] = value;
    if (b->buf_pos >= b->buf_len) {
        uint8_t *dest = ready(b) ? sector_w(b, b->trk, b->side, b->sec) : NULL;
        if (dest) memcpy(dest, b->buf, (size_t)b->buf_len);
        complete_io(b, false);
    }
}

// ---------------------------------------------------------------------------
// Beta 128 (beta128.cpp)
// ---------------------------------------------------------------------------
void zx_beta_init(zx_beta *b)
{
    memset(b, 0, sizeof *b);
    zx_beta_reset(b);
}

void zx_beta_free(zx_beta *b)
{
    eject(b);
    free(b->sbuf);
    b->sbuf = NULL;
}

void zx_beta_reset(zx_beta *b)
{
    wd_reset(b);
    b->active = false;
    b->control = 0x3c;
    b->drive = 0;
    b->side = 0;
}

bool zx_beta_load(zx_beta *b, const uint8_t *data, size_t size, const char **error)
{
    if (!data || size < 8) {
        if (error) *error = "disk image is empty";
        return false;
    }
    if (memcmp(data, "SINCLAIR", 8) == 0) return load_scl(b, data, size, error);
    return load_trd(b, data, size, error);
}

uint8_t zx_beta_status_r(zx_beta *b) { return b->active ? wd_status_r(b) : 0xff; }
uint8_t zx_beta_track_r(zx_beta *b) { return b->active ? b->trk : 0xff; }
uint8_t zx_beta_sector_r(zx_beta *b) { return b->active ? b->sec : 0xff; }
uint8_t zx_beta_data_r(zx_beta *b) { return b->active ? wd_data_r(b) : 0xff; }

uint8_t zx_beta_state_r(const zx_beta *b)
{
    if (!b->active) return 0xff;
    uint8_t value = 0x3f;
    if (b->drq) value |= 0x40;
    if (b->intrq) value |= 0x80;
    return value;
}

void zx_beta_command_w(zx_beta *b, uint8_t v) { if (b->active) wd_command_w(b, v); }
void zx_beta_track_w(zx_beta *b, uint8_t v) { if (b->active) b->trk = v; }
void zx_beta_sector_w(zx_beta *b, uint8_t v) { if (b->active) b->sec = v; }
void zx_beta_data_w(zx_beta *b, uint8_t v) { if (b->active) wd_data_w(b, v); }

void zx_beta_param_w(zx_beta *b, uint8_t value)
{
    if (!b->active) return;
    b->control = value;
    b->drive = value & 3;
    b->side = (value & 0x10) ? 0 : 1;      // bit 4 selects the head, inverted
    if ((value & 0x04) == 0) wd_reset(b);
}
