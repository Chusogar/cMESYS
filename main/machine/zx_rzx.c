// zx_rzx.c - RZX playback, C11 port of dsp-cpp's machine/spectrum_rzx.cpp.
#include "zx_rzx.h"
#include "zx_inflate.h"
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

void zx_rzx_init(zx_rzx *r) { memset(r, 0, sizeof *r); }

void zx_rzx_free(zx_rzx *r)
{
    free(r->frames);
    free(r->snap);
    memset(r, 0, sizeof *r);
}

void zx_rzx_drop_snap(zx_rzx *r)
{
    free(r->snap);
    r->snap = NULL;
    r->snap_len = 0;
}

static uint8_t *dup_bytes(const uint8_t *p, size_t n)
{
    uint8_t *d = malloc(n ? n : 1);
    if (d && n) memcpy(d, p, n);
    return d;
}

bool zx_rzx_load(zx_rzx *r, const uint8_t *data, size_t size, const char **error)
{
    zx_rzx_free(r);
    if (!data || size < 10 || memcmp(data, "RZX!", 4) != 0) {
        if (error) *error = "not an RZX file";
        return false;
    }
    if (rd32(data + 6) & 1) {
        if (error) *error = "encrypted RZX not supported";
        return false;
    }
    size_t o = 10;
    bool got_input = false;
    while (o + 5 <= size) {
        const uint8_t id = data[o];
        const uint32_t blen = rd32(data + o + 1);
        if (blen < 5 || o + blen > size) {
            if (error) *error = "corrupt RZX block length";
            goto fail;
        }
        const uint8_t *block = data + o;
        if (id == 0x30) {           // snapshot
            if (blen < 17) {
                if (error) *error = "RZX snapshot block too small";
                goto fail;
            }
            const uint32_t flags = rd32(block + 5);
            if (flags & 1) {
                if (error) *error = "RZX external snapshot not supported";
                goto fail;
            }
            memcpy(r->snap_ext, block + 9, 4);
            r->snap_ext[4] = 0;
            const uint32_t usl = rd32(block + 13);
            free(r->snap);
            if (flags & 2) {
                r->snap = zx_inflate(block + 17, blen - 17, usl, (size_t)usl + 1024, &r->snap_len);
                if (!r->snap) {
                    if (error) *error = "RZX snapshot decompress failed";
                    goto fail;
                }
            } else {
                r->snap_len = blen - 17;
                r->snap = dup_bytes(block + 17, r->snap_len);
                if (!r->snap) goto oom;
            }
        } else if (id == 0x80) {    // input recording
            if (blen < 18) {
                if (error) *error = "RZX input block too small";
                goto fail;
            }
            const uint32_t nframes = rd32(block + 5);
            r->start_tstates = rd32(block + 10);
            const uint32_t flags = rd32(block + 14);
            if (flags & 1) {
                if (error) *error = "protected/encrypted RZX input not supported";
                goto fail;
            }
            free(r->frames);
            if (flags & 2) {
                r->frames = zx_inflate(block + 18, blen - 18, 0, 0, &r->frames_len);
                if (!r->frames) {
                    if (error) *error = "RZX decompress failed";
                    goto fail;
                }
            } else {
                r->frames_len = blen - 18;
                r->frames = dup_bytes(block + 18, r->frames_len);
                if (!r->frames) goto oom;
            }
            // check the frames now (dsp-cpp parses them all at load)
            size_t p = 0;
            for (uint32_t i = 0; i < nframes; ++i) {
                if (p + 4 > r->frames_len) {
                    if (error) *error = "RZX frame data truncated";
                    goto fail;
                }
                const uint16_t in_count = rd16(r->frames + p + 2);
                p += 4;
                if (in_count != 0xffff) {
                    if (p + in_count > r->frames_len) {
                        if (error) *error = "RZX IN bytes truncated";
                        goto fail;
                    }
                    p += in_count;
                }
            }
            r->nframes = nframes;
            got_input = true;
        }
        o += blen;
    }
    if (!r->snap) {
        if (error) *error = "RZX has no embedded snapshot";
        goto fail;
    }
    if (!got_input || r->nframes == 0) {
        if (error) *error = "RZX has no input frames";
        goto fail;
    }
    return true;
oom:
    if (error) *error = "out of memory for the RZX";
fail:
    zx_rzx_free(r);
    return false;
}

void zx_rzx_start(zx_rzx *r)
{
    if (!r->frames || !r->nframes) return;
    r->playing = true;
    r->finished = false;
    r->frame_index = 0;
    r->pos = 0;
    r->fetches_left = 0;
    r->in_pos = 0;
    r->cur_ins = r->last_ins = NULL;
    r->cur_n = r->last_n = 0;
}

void zx_rzx_stop(zx_rzx *r)
{
    r->playing = false;
    r->finished = false;
    r->fetches_left = 0;
    r->in_pos = 0;
}

bool zx_rzx_begin_frame(zx_rzx *r)
{
    if (!r->playing || r->finished) return false;
    if (r->frame_index >= r->nframes || r->pos + 4 > r->frames_len) {
        r->playing = false;
        r->finished = true;
        return false;
    }
    const uint8_t *f = r->frames + r->pos;
    r->fetches_left = rd16(f);
    const uint16_t in_count = rd16(f + 2);
    r->pos += 4;
    if (in_count == 0xffff) {   // repeat the last frame's INs
        r->cur_ins = r->last_ins;
        r->cur_n = r->last_n;
    } else {
        r->cur_ins = r->last_ins = r->frames + r->pos;
        r->cur_n = r->last_n = in_count;
        r->pos += in_count;
    }
    r->in_pos = 0;
    ++r->frame_index;
    return true;
}
