// diskii.c - Apple Disk II (slot 6): C11 migration of dsp-cpp's
// machine/diskii.cpp (see diskii.h).
#include "diskii.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// ~32.5 is authentic (4 us bit cells); 32 keeps the P5 PROM's 27-cycle data
// field loop from missing a nibble with ticks after each instruction.
#define CYCLES_PER_NIBBLE 32
#define VOLUME 0xFE
#define MOTOR_OFF_DELAY 1020484     // ~1 s at the Apple II clock
#define GAP 0xFF

enum { K_NONE, K_DOS, K_PRODOS, K_NIB };

static const uint8_t disk_byte[0x40] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};
// Physical sector -> logical sector of a DOS 3.3 / ProDOS-order image
static const uint8_t dos_skew[16] = {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04,
                                     0x0B, 0x03, 0x0A, 0x02, 0x09, 0x01, 0x08, 0x0F};
static const uint8_t prodos_skew[16] = {0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B,
                                        0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F};

// ---------------------------------------------------------------------------
// The image: bytes written into the pbuf (one block, or pages; a page that
// was all one byte gets its own memory when it changes)
static bool pb_write(zx_pbuf *pb, uint32_t off, const uint8_t *src, uint32_t n)
{
    if (off + n > pb->size) return false;
    if (pb->flat) {
        memcpy(pb->flat + off, src, n);
        return true;
    }
    if (pb->lazy) return false;
    while (n) {
        const uint32_t page = off >> ZX_PAGE_BITS, o = off & (ZX_PAGE - 1);
        uint32_t k = ZX_PAGE - o;
        if (k > n) k = n;
        if (!pb->pages[page]) {
            uint8_t *p = (uint8_t *)malloc(ZX_PAGE);
            if (!p) return false;
            memset(p, pb->fill[page], ZX_PAGE);
            pb->pages[page] = p;
        }
        memcpy(pb->pages[page] + o, src, k);
        off += k, src += k, n -= k;
    }
    return true;
}

static void encode_62(const uint8_t src[256], uint8_t dest[343])
{
    uint8_t nib[342];
    uint8_t offset = 0xAC;
    int idx = 0;
    while (offset != 0x02) {
        uint8_t value = 0;
#define ADD(a) value = (uint8_t)((value << 2) | (((a) & 0x01) << 1) | (((a) & 0x02) >> 1))
        ADD(src[offset]);
        offset = (uint8_t)(offset - 0x56);
        ADD(src[offset]);
        offset = (uint8_t)(offset - 0x56);
        ADD(src[offset]);
        offset = (uint8_t)(offset - 0x53);
#undef ADD
        nib[idx++] = (uint8_t)(value << 2);
    }
    nib[idx - 2] &= 0x3F;
    nib[idx - 1] &= 0x3F;
    for (int i = 0; i < 256; i++) nib[idx++] = src[i];
    uint8_t saved = 0, xored[343];
    for (int i = 0; i < 342; i++) {
        xored[i] = (uint8_t)(saved ^ nib[i]);
        saved = nib[i];
    }
    xored[342] = saved;
    for (int i = 0; i < 343; i++) dest[i] = disk_byte[xored[i] >> 2];
}

static bool decode_62(const uint8_t src[343], uint8_t dest[256])
{
    static uint8_t sixbit[0x80];
    static bool ready;
    if (!ready) {
        memset(sixbit, 0, sizeof sixbit);
        for (int i = 0; i < 0x40; i++) sixbit[disk_byte[i] - 0x80] = (uint8_t)(i << 2);
        ready = true;
    }
    uint8_t raw[343];
    for (int i = 0; i < 343; i++) {
        if (!(src[i] & 0x80)) return false;
        raw[i] = sixbit[src[i] & 0x7F];
    }
    uint8_t saved = 0, nib[342];
    for (int i = 0; i < 342; i++) {
        nib[i] = (uint8_t)(saved ^ raw[i]);
        saved = nib[i];
    }
    if (saved != raw[342]) return false;
    memset(dest, 0, 256);
    const uint8_t *low = nib, *sector = nib + 0x56;
    uint8_t offset = 0xAC;
    while (offset != 0x02) {
        if (offset >= 0xAC)
            dest[offset] = (uint8_t)((sector[offset] & 0xFC) | ((low[0] & 0x80) >> 7) | ((low[0] & 0x40) >> 5));
        offset = (uint8_t)(offset - 0x56);
        dest[offset] = (uint8_t)((sector[offset] & 0xFC) | ((low[0] & 0x20) >> 5) | ((low[0] & 0x10) >> 3));
        offset = (uint8_t)(offset - 0x56);
        dest[offset] = (uint8_t)((sector[offset] & 0xFC) | ((low[0] & 0x08) >> 3) | ((low[0] & 0x04) >> 1));
        offset = (uint8_t)(offset - 0x53);
        ++low;
    }
    return true;
}

static int logical_sector(const diskii *d, int physical)
{
    physical &= 0x0F;
    return d->kind == K_PRODOS ? prodos_skew[physical] : dos_skew[physical];
}

static uint32_t nib_stride(const diskii *d) { return d->img.size / DISKII_TRACKS; }

static void put(diskii *d, uint8_t v)
{
    if (d->nib_len < DISKII_NIBBLES) d->nib[d->nib_len++] = v;
}

static void put44(diskii *d, uint8_t v)
{
    put(d, (uint8_t)(((v >> 1) & 0x55) | 0xAA));
    put(d, (uint8_t)((v & 0x55) | 0xAA));
}

static void encode_track(diskii *d, int track)
{
    d->nib_len = 0;
    d->encoded_track = track;
    d->nibble_pos = 0;
    d->latch = 0;
    d->cycles_until_nibble = CYCLES_PER_NIBBLE;
    d->dirty = false;
    if (!d->loaded || track < 0 || track >= DISKII_TRACKS) return;
    if (d->kind == K_NIB) {
        const uint32_t stride = nib_stride(d);
        const uint32_t n = stride < DISKII_NIBBLES ? stride : DISKII_NIBBLES;
        zx_pb_copy(&d->img, (uint32_t)track * stride, d->nib, n);
        for (uint32_t i = n; i < DISKII_NIBBLES; i++) d->nib[i] = GAP;
        d->nib_len = DISKII_NIBBLES;
        return;
    }
    for (int i = 0; i < 48; i++) put(d, GAP);
    for (int physical = 0; physical < DISKII_SECTORS; physical++) {
        uint8_t sector[256], enc[343];
        zx_pb_copy(&d->img, (uint32_t)((track * DISKII_SECTORS + logical_sector(d, physical)) * 256), sector, 256);
        put(d, 0xD5), put(d, 0xAA), put(d, 0x96);
        put44(d, VOLUME);
        put44(d, (uint8_t)track);
        put44(d, (uint8_t)physical);
        put44(d, (uint8_t)(VOLUME ^ track ^ physical));
        put(d, 0xDE), put(d, 0xAA), put(d, 0xEB);
        for (int i = 0; i < 6; i++) put(d, GAP);
        put(d, 0xD5), put(d, 0xAA), put(d, 0xAD);
        encode_62(sector, enc);
        for (int i = 0; i < 343; i++) put(d, enc[i]);
        put(d, 0xDE), put(d, 0xAA), put(d, 0xEB);
        for (int i = 0; i < 27; i++) put(d, GAP);
    }
    while (d->nib_len < DISKII_NIBBLES) put(d, GAP);
}

// The sectors written on the nibble stream, back into the image
static void decode_track(diskii *d, int track)
{
    if (!d->loaded || d->kind == K_NIB || track < 0 || track >= DISKII_TRACKS || !d->nib_len) return;
    const int n = d->nib_len;
    int offset = 0, parts = DISKII_SECTORS * 2 + 1, physical = -1;
    while (parts-- > 0) {
        uint8_t byteval[3] = {0, 0, 0};
        int bytenum = 0, loop = n;
        while (loop-- && bytenum < 3) {
            const uint8_t v = d->nib[offset++];
            if (offset >= n) offset = 0;
            if (bytenum) byteval[bytenum++] = v;
            else if (v == 0xD5) bytenum = 1;
        }
        if (bytenum != 3 || byteval[1] != 0xAA) continue;
        if (byteval[2] == 0x96) {
            uint8_t field[8];
            for (int i = 0; i < 8; i++) {
                field[i] = d->nib[offset++];
                if (offset >= n) offset = 0;
            }
            physical = ((field[4] & 0x55) << 1) | (field[5] & 0x55);
        } else if (byteval[2] == 0xAD && physical >= 0 && physical < DISKII_SECTORS) {
            uint8_t enc[343], dec[256];
            for (int i = 0; i < 343; i++) {
                enc[i] = d->nib[offset++];
                if (offset >= n) offset = 0;
            }
            if (decode_62(enc, dec))
                pb_write(&d->img, (uint32_t)((track * DISKII_SECTORS + logical_sector(d, physical)) * 256), dec, 256);
            physical = -1;
        }
    }
}

static void flush_write(diskii *d)
{
    if (d->dirty) {
        decode_track(d, d->encoded_track);
        d->dirty = false;
    }
}

static uint8_t next_nibble(diskii *d)
{
    if (!d->nib_len) return 0;
    if (d->nibble_pos < 0 || d->nibble_pos >= d->nib_len) d->nibble_pos = 0;
    const uint8_t v = d->nib[d->nibble_pos];
    d->nibble_pos = (d->nibble_pos + 1) % d->nib_len;
    return v;
}

static void write_nibble(diskii *d, uint8_t value)
{
    if (!d->nib_len) return;
    if (d->nibble_pos < 0 || d->nibble_pos >= d->nib_len) d->nibble_pos = 0;
    d->nib[d->nibble_pos] = (uint8_t)(value | 0x80);
    if (d->kind == K_NIB && d->encoded_track >= 0 && d->encoded_track < DISKII_TRACKS) {
        const uint32_t stride = nib_stride(d);
        if ((uint32_t)d->nibble_pos < stride)
            pb_write(&d->img, (uint32_t)d->encoded_track * stride + (uint32_t)d->nibble_pos, &d->nib[d->nibble_pos], 1);
    }
    d->nibble_pos = (d->nibble_pos + 1) % d->nib_len;
    d->dirty = true;
}

static void step_phase(diskii *d, int phase, bool on)
{
    if (!on) {
        d->phases &= (uint8_t)~(1u << phase);
        return;
    }
    d->phases |= (uint8_t)(1u << phase);
    const int current = ((d->half_track % 4) + 4) % 4;
    const int delta = (phase - current + 4) % 4;
    if (delta == 1) d->half_track++;
    else if (delta == 3) d->half_track--;
    if (d->half_track < 0) d->half_track = 0;
    if (d->half_track > (DISKII_TRACKS - 1) * 2) d->half_track = (DISKII_TRACKS - 1) * 2;
    const int track = d->half_track / 2;
    if (track != d->encoded_track) {
        flush_write(d);
        encode_track(d, track);
    }
}

// ---------------------------------------------------------------------------
void diskii_init(diskii *d)
{
    memset(d, 0, sizeof *d);
    zx_pb_init(&d->img);
    d->encoded_track = -1;
}

void diskii_free(diskii *d)
{
    zx_pb_free(&d->img);
    d->loaded = false;
    d->kind = K_NONE;
    d->nib_len = 0;
}

void diskii_reset(diskii *d)
{
    d->phases = 0;
    d->half_track = 0;
    d->motor_on = false;
    d->motor_off_delay = 0;
    d->drive = 0;
    d->q6 = d->q7 = false;
    d->latch = 0;
    d->nibble_pos = 0;
    d->cycles_until_nibble = CYCLES_PER_NIBBLE;
    d->write_mode = false;
    d->encoded_track = -1;
    d->dirty = false;
    if (d->loaded) encode_track(d, 0);
    else d->nib_len = 0;
}

void diskii_tick(diskii *d, int cycles)
{
    if (d->motor_on && d->motor_off_delay > 0) {
        // The card keeps the drive enabled about a second after $C0E8 (a 556
        // one-shot); RWTS stops the motor after every call and expects the
        // disk still spinning on the next one.
        d->motor_off_delay -= cycles;
        if (d->motor_off_delay <= 0) {
            d->motor_off_delay = 0;
            d->motor_on = false;
            flush_write(d);
        }
    }
    if (!d->motor_on || !d->loaded) return;
    d->cycles_until_nibble -= cycles;
    while (d->cycles_until_nibble <= 0) {
        d->cycles_until_nibble += CYCLES_PER_NIBBLE;
        if (!d->q7) d->latch = next_nibble(d);
    }
}

uint8_t diskii_read(diskii *d, uint8_t offset)
{
    const uint8_t n = offset & 0x0F;
    switch (n) {
    case 0x0: case 0x2: case 0x4: case 0x6: step_phase(d, n / 2, false); break;
    case 0x1: case 0x3: case 0x5: case 0x7: step_phase(d, n / 2, true); break;
    case 0x8:
        if (d->motor_on && d->motor_off_delay == 0) d->motor_off_delay = MOTOR_OFF_DELAY;
        break;
    case 0x9: d->motor_on = true; d->motor_off_delay = 0; break;
    case 0xA: d->drive = 0; break;
    case 0xB: d->drive = 1; break;
    case 0xC:
        d->q6 = false;
        if (!d->q7) {
            const uint8_t v = d->latch;
            d->latch &= 0x7F;
            return v;
        }
        if (d->write_mode) write_nibble(d, d->latch);
        return d->latch;
    case 0xD: d->q6 = true; return 0x00;         // not write-protected
    case 0xE: d->q7 = false; d->write_mode = false; break;
    case 0xF: d->q7 = true; d->write_mode = true; break;
    default: break;
    }
    return d->latch;
}

void diskii_write(diskii *d, uint8_t offset, uint8_t value)
{
    const uint8_t n = offset & 0x0F;
    if (n == 0x0F || (n == 0x0D && d->q7)) d->latch = (uint8_t)(value | 0x80);
    diskii_read(d, offset);
    if (n == 0x0F) {
        d->latch = (uint8_t)(value | 0x80);
        d->write_mode = true;
        d->q7 = true;
    }
}

static bool ends_with_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)s[n - k + i]) != tolower((unsigned char)ext[i])) return false;
    return true;
}

void diskii_eject(diskii *d)
{
    flush_write(d);
    diskii_free(d);
    diskii_reset(d);
}

const char *diskii_load_pbuf(diskii *d, zx_pbuf *pb, const char *name)
{
    const char *why = NULL;
    const uint32_t size = pb->size;
    const bool nib = (name && ends_with_ci(name, ".nib")) || size == (uint32_t)(DISKII_TRACKS * DISKII_NIBBLES);
    if (!size) why = "empty disk image";
    else if (nib && size < (uint32_t)(DISKII_TRACKS * 6000)) why = "nibble image is too small";
    else if (!nib && size < DISKII_DOS_SIZE) why = "disk image is too small (need 140K DOS 3.3 / ProDOS)";
    if (why) {
        zx_pb_free(pb);
        return why;
    }
    diskii_free(d);
    d->img = *pb;
    zx_pb_init(pb);
    d->kind = nib ? K_NIB : (name && ends_with_ci(name, ".po")) ? K_PRODOS : K_DOS;
    d->loaded = true;
    diskii_reset(d);
    return NULL;
}
