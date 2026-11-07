// c1541.c - Commodore 1541, see c1541.h (dsp-cpp's machine/c1541.cpp, gcr.cpp,
// d64_image.cpp and g64_image.cpp).
#include "c1541.h"

#include <stdlib.h>
#include <string.h>

#define D64_SIZE 174848

// ---------------------------------------------------------------------------
// .d64 geometry
// ---------------------------------------------------------------------------
int d64_sectors_per_track(int track)
{
    if (track < 1) return 0;
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    if (track <= 35) return 17;
    return 0;
}

static const int16_t kTrackFirst[36] = {
    0, 0, 21, 42, 63, 84, 105, 126, 147, 168, 189, 210, 231, 252, 273, 294, 315, 336,
    357, 376, 395, 414, 433, 452, 471, 490, 508, 526, 544, 562, 580, 598, 615, 632, 649, 666,
};

long d64_offset(int track, int sector)
{
    if (track < 1 || track > 35 || sector < 0 || sector >= d64_sectors_per_track(track)) return -1;
    return ((long)kTrackFirst[track] + sector) * 256;
}

// ---------------------------------------------------------------------------
// GCR (gcr.cpp): the track as bits, MSB first
// ---------------------------------------------------------------------------
static const uint8_t kGcrEncode[16] = { 0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
                                        0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15 };

typedef struct { uint8_t *buf; int bits, cap; } bitw;

static void put_bit(bitw *w, int bit)
{
    if (w->bits >= w->cap) return;
    uint8_t *b = &w->buf[w->bits >> 3];
    const uint8_t m = (uint8_t)(0x80 >> (w->bits & 7));
    if (bit) *b |= m;
    else *b &= (uint8_t)~m;
    w->bits++;
}

static void put_bits(bitw *w, uint8_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) put_bit(w, (v >> i) & 1);
}

static void put_sync(bitw *w, int ones)
{
    for (int i = 0; i < ones; i++) put_bit(w, 1);
}

static void put_gcr(bitw *w, const uint8_t *data, int len)     // 4 bytes -> 5 GCR bytes
{
    for (int i = 0; i < len; i += 4) {
        uint8_t in[4] = { 0, 0, 0, 0 };
        for (int j = 0; j < 4 && i + j < len; j++) in[j] = data[i + j];
        const uint8_t a = kGcrEncode[in[0] >> 4], b = kGcrEncode[in[0] & 15], c = kGcrEncode[in[1] >> 4],
                      d = kGcrEncode[in[1] & 15], e = kGcrEncode[in[2] >> 4], f = kGcrEncode[in[2] & 15],
                      g = kGcrEncode[in[3] >> 4], h = kGcrEncode[in[3] & 15];
        put_bits(w, (uint8_t)((a << 3) | (b >> 2)), 8);
        put_bits(w, (uint8_t)((b << 6) | (c << 1) | (d >> 4)), 8);
        put_bits(w, (uint8_t)((d << 4) | (e >> 1)), 8);
        put_bits(w, (uint8_t)((e << 7) | (f << 2) | (g >> 3)), 8);
        put_bits(w, (uint8_t)((g << 5) | h), 8);
    }
}

static int track_capacity(int track)
{
    if (track <= 17) return 7692;
    if (track <= 24) return 7142;
    if (track <= 30) return 6666;
    return 6250;
}

static void build_track(bitw *w, int track, int sectors, uint8_t (*sec)[256], uint8_t id1, uint8_t id2)
{
    const int capacity = track_capacity(track) * 8;
    for (int s = 0; s < sectors; s++) {
        put_sync(w, 40);
        const uint8_t hdr[8] = { 0x08, (uint8_t)(s ^ track ^ id2 ^ id1), (uint8_t)s, (uint8_t)track, id2, id1, 0x0F, 0x0F };
        put_gcr(w, hdr, 8);
        for (int i = 0; i < 9; i++) put_bits(w, 0x55, 8);
        put_sync(w, 40);
        uint8_t block[260];
        block[0] = 0x07;
        memcpy(block + 1, sec[s], 256);
        uint8_t chk = 0;
        for (int i = 1; i <= 256; i++) chk ^= block[i];
        block[257] = chk;
        block[258] = block[259] = 0;
        put_gcr(w, block, 260);
        for (int i = 0; i < 8; i++) put_bits(w, 0x55, 8);
    }
    while (w->bits + 8 <= capacity) put_bits(w, 0x55, 8);
    while (w->bits < capacity) put_bit(w, 0);
}

// ---------------------------------------------------------------------------
// the drive
// ---------------------------------------------------------------------------
static void update_iec_outputs(c1541 *d)
{
    const uint8_t pb = (uint8_t)(via6522_out_b(&d->via1) & d->via1.ddr_b);
    const bool data_pull = (pb & 0x02) != 0, clk_pull = (pb & 0x08) != 0;
    // ATN acknowledge: DATA pulled low while ATNA disagrees with ATN
    const bool atna_pull = ((pb & 0x10) != 0) != !d->host_atn;
    d->drv_data = !(data_pull || atna_pull);
    d->drv_clk = !clk_pull;
}

static uint8_t read_mem(void *user, uint16_t a)
{
    c1541 *d = (c1541 *)user;
    if (a < 0x0800) return d->ram[a];
    if (a >= 0x1800 && a <= 0x180F) return via6522_read(&d->via1, (uint8_t)(a & 15));
    if (a >= 0x1C00 && a <= 0x1C0F) return via6522_read(&d->via2, (uint8_t)(a & 15));
    if (a >= 0xC000) return d->rom[d->rom_8k ? (a & 0x1FFF) : (a & 0x3FFF)];
    return 0xFF;
}

static void write_mem(void *user, uint16_t a, uint8_t v)
{
    c1541 *d = (c1541 *)user;
    if (a < 0x0800) d->ram[a] = v;
    else if (a >= 0x1800 && a <= 0x180F) {
        via6522_write(&d->via1, (uint8_t)(a & 15), v);
        update_iec_outputs(d);
    } else if (a >= 0x1C00 && a <= 0x1C0F) {
        via6522_write(&d->via2, (uint8_t)(a & 15), v);
    }
}

// VIA1 port B: PB0 DATA IN, PB2 CLK IN, PB7 ATN IN (set: the line is low)
static uint8_t via1_pb_in(void *user)
{
    c1541 *d = (c1541 *)user;
    uint8_t v = 0;
    if (!c1541_bus_data(d)) v |= 0x01;
    if (!c1541_bus_clk(d)) v |= 0x04;
    if (!d->host_atn) v |= 0x80;
    return v;
}

// VIA2 port B: PB4 write protect (0 = protected), PB7 SYNC (0 under a sync mark)
static uint8_t via2_pb_in(void *user)
{
    c1541 *d = (c1541 *)user;
    uint8_t v = 0;
    if (!d->write_protect) v |= 0x10;
    if (!d->sync) v |= 0x80;
    return v;
}

static void irq_out(c1541 *d)
{
    m65xx_set_irq(&d->cpu, (d->via1_irq || d->via2_irq) ? M65_ASSERT : M65_CLEAR);
}
static void via1_irq(void *user, bool on) { c1541 *d = (c1541 *)user; d->via1_irq = on; irq_out(d); }
static void via2_irq(void *user, bool on) { c1541 *d = (c1541 *)user; d->via2_irq = on; irq_out(d); }
static void via2_ca2(void *user, bool level) { ((c1541 *)user)->soe = level; }

static void rebuild_track(c1541 *d);

static void step_head(c1541 *d, int delta)
{
    const int old = d->half_track;
    d->half_track += delta;
    if (d->half_track < 2) d->half_track = 2;      // the head stop (keeps the parity)
    if (d->half_track > 84) d->half_track = 84;
    if (d->half_track == old) return;
    rebuild_track(d);
    d->bit_pos = 0;
}

static void via2_pb_out(void *user, uint8_t value)
{
    c1541 *d = (c1541 *)user;
    d->motor_on = (value & 0x04) != 0;
    d->led_on = (value & 0x08) != 0;
    const int phase = value & 3;                    // stepper phases
    if (phase == d->stepper_prev) return;
    const int diff = (phase - d->stepper_prev) & 3;
    if (diff == 1) step_head(d, +1);
    else if (diff == 3) step_head(d, -1);
    d->stepper_prev = phase;
}

static void rebuild_track(c1541 *d)
{
    d->track_bits = 0;
    d->zero_run = 0;
    d->bit_timer = 0;
    d->sync = d->byte_ready = false;
    d->shift_reg = 0;
    d->shift_count = 0;
    bitw w = { d->track, 0, C1541_TRACK_BYTES * 8 };
    if (d->disk && d->g64) {
        const int ht = d->half_track < 1 ? 1 : d->half_track > 84 ? 84 : d->half_track;
        const int len = d->g64_len[ht] > C1541_TRACK_BYTES ? C1541_TRACK_BYTES : d->g64_len[ht];
        if (len > 0) {
            zx_pb_copy(&d->img, d->g64_off[ht], d->track, (uint32_t)len);
            d->track_bits = len * 8;
            static const int kCyclesPerBit[4] = { 16, 15, 14, 13 };
            d->cycles_per_bit = kCyclesPerBit[d->g64_speed[ht] & 3];
        }
        d->bit_pos = d->track_bits ? d->bit_pos % d->track_bits : 0;
        return;
    }
    if (!d->disk) { d->bit_pos = 0; return; }
    const int track = d->half_track / 2;
    if (track < 1 || track > 35) { d->bit_pos = 0; return; }
    d->cycles_per_bit = track <= 17 ? 13 : track <= 24 ? 14 : track <= 30 ? 15 : 16;
    if (d->half_track & 1) {
        // .d64 has no half tracks: sparse flux so +0.5 seeks still see a signal
        put_sync(&w, 20);
        for (int i = 0; i < 2000; i++) put_bit(&w, i & 1);
        d->track_bits = w.bits;
        d->cycles_per_bit = 14;
    } else {
        const int spt = d64_sectors_per_track(track);
        static uint8_t sec[21][256];
        for (int s = 0; s < spt; s++) {
            const long off = d64_offset(track, s);
            if (off < 0 || (uint32_t)off + 256 > d->img.size) memset(sec[s], 0, 256);
            else zx_pb_copy(&d->img, (uint32_t)off, sec[s], 256);
            // the extended error map: 2/3 no header / sync, 4 bad data, 5/9 checksum
            const uint8_t err = d->d64_errors ? zx_pb_byte(&d->img, D64_SIZE + (uint32_t)(off / 256)) : 1;
            if (err == 2 || err == 3) memset(sec[s], 0xFF, 256);
            else if (err == 4) memset(sec[s], 0, 256);
            else if (err == 5 || err == 9) sec[s][0] ^= 0xFF;
        }
        build_track(&w, track, spt, sec, d->id1, d->id2);
        d->track_bits = w.bits;
    }
    if (!d->track_bits) {
        put_sync(&w, 80);
        for (int i = 0; i < 100; i++) put_bits(&w, 0x55, 8);
        d->track_bits = w.bits;
    }
    d->bit_pos = d->track_bits ? d->bit_pos % d->track_bits : 0;
}

static void tick_disk(c1541 *d, int cycles)
{
    if (!d->motor_on || !d->track_bits) {
        d->sync = d->byte_ready = false;
        d->ones = 0;
        return;
    }
    // cycles_per_bit counts quarter cycles
    d->bit_timer += cycles * 4;
    while (d->bit_timer >= d->cycles_per_bit) {
        d->bit_timer -= d->cycles_per_bit;
        bool bit = (d->track[d->bit_pos >> 3] >> (7 - (d->bit_pos & 7))) & 1;
        if (++d->bit_pos >= d->track_bits) d->bit_pos = 0;
        if (d->weak_bits) {                         // long zero runs: unstable flux
            if (!bit) {
                if (++d->zero_run >= 6) {
                    d->weak_rng = d->weak_rng * 1664525u + 1013904223u;
                    bit = d->weak_rng & 1;
                }
            } else {
                d->zero_run = 0;
            }
        }
        if (bit) {                                  // SYNC: ten ones or more
            if (++d->ones >= 10) d->sync = true;
        } else {
            d->ones = 0;
            d->sync = false;
        }
        d->shift_reg = (uint8_t)((d->shift_reg << 1) | bit);
        if (d->sync) {
            d->shift_count = 0;
            d->byte_ready = false;
            continue;
        }
        if (++d->shift_count >= 8) {
            d->shift_count = 0;
            d->last_byte = d->shift_reg;
            d->byte_ready = true;
            via6522_write_pa(&d->via2, d->last_byte);     // the GCR byte on PA
            via6522_write_ca1(&d->via2, false);           // BYTE READY on CA1
            via6522_write_ca1(&d->via2, true);
            if (d->soe) d->cpu.p.v = true;              // and on the 6502's SO pin
        }
    }
}

void c1541_init(c1541 *d, const uint8_t *rom, size_t rom_size)
{
    memset(d, 0, sizeof *d);
    d->rom = rom;
    d->rom_8k = rom_size == 0x2000;
    d->weak_bits = true;
    d->cpu.read = read_mem;
    d->cpu.write = write_mem;
    d->cpu.user = d;
    d->via1.user = d->via2.user = d;
    d->via1.pb_in = via1_pb_in;
    d->via1.irq = via1_irq;
    d->via2.pb_in = via2_pb_in;
    d->via2.pb_out = via2_pb_out;
    d->via2.ca2_out = via2_ca2;
    d->via2.irq = via2_irq;
    zx_pb_init(&d->img);
    if (rom) c1541_reset(d);
}

void c1541_free(c1541 *d) { zx_pb_free(&d->img); }

void c1541_reset(c1541 *d)
{
    if (!d->rom) return;
    via6522_reset(&d->via1);
    via6522_reset(&d->via2);
    memset(d->ram, 0, sizeof d->ram);
    d->motor_on = d->led_on = false;
    d->half_track = 18 * 2;
    d->stepper_prev = 0;
    d->cycle_debt = 0;
    d->bit_pos = d->bit_timer = 0;
    d->cycles_per_bit = 13;
    d->shift_reg = 0;
    d->shift_count = d->ones = 0;
    d->sync = d->byte_ready = d->soe = false;
    d->last_byte = 0;
    d->drv_data = d->drv_clk = d->host_atn = d->host_clk = d->host_data = true;
    d->via1_irq = d->via2_irq = false;
    d->weak_rng = 0xACE1u;
    d->zero_run = 0;
    m65xx_reset(&d->cpu);
    rebuild_track(d);
}

void c1541_eject(c1541 *d)
{
    zx_pb_free(&d->img);
    d->disk = d->g64 = false;
    rebuild_track(d);
}

static uint32_t le32(const zx_pbuf *pb, uint32_t o) { return zx_pb_rd32(pb, o); }

bool c1541_insert(c1541 *d, zx_pbuf *pb, const char **error)
{
    const char *err = NULL;
    bool g64 = false;
    if (pb->size >= 12 && zx_pb_eq(pb, 0, "GCR-1541", 8)) {
        g64 = true;
    } else if (pb->size < D64_SIZE) {
        err = "D64 too small";
    }
    if (err) {
        if (error) *error = err;
        zx_pb_free(pb);
        return false;
    }
    zx_pb_free(&d->img);
    d->img = *pb;
    zx_pb_init(pb);
    d->disk = true;
    d->g64 = g64;
    if (g64) {
        memset(d->g64_off, 0, sizeof d->g64_off);
        memset(d->g64_len, 0, sizeof d->g64_len);
        int n = zx_pb_byte(&d->img, 9);
        if (n <= 0 || n > 84) n = 84;
        const uint32_t table = 12, speed = table + (uint32_t)n * 4;
        const bool has_speed = d->img.size >= speed + (uint32_t)n * 4;
        for (int ht = 1; ht <= n; ht++) {
            const uint32_t off = le32(&d->img, table + (uint32_t)(ht - 1) * 4);
            if (off == 0 || off + 2 > d->img.size) continue;
            const uint16_t len = zx_pb_rd16(&d->img, off);
            if (!len || off + 2 + len > d->img.size) continue;
            d->g64_off[ht] = off + 2;
            d->g64_len[ht] = len;
            uint32_t sp = has_speed ? le32(&d->img, speed + (uint32_t)(ht - 1) * 4) : 99;
            if (sp > 3) {
                const int full = (ht + 1) / 2;
                sp = full <= 17 ? 3 : full <= 24 ? 2 : full <= 30 ? 1 : 0;
            }
            d->g64_speed[ht] = (uint8_t)sp;
        }
        d->write_protect = true;
    } else {
        d->d64_errors = d->img.size >= D64_SIZE + 683;
        const long bam = d64_offset(18, 0);
        d->id1 = zx_pb_byte(&d->img, (uint32_t)bam + 0xA2);
        d->id2 = zx_pb_byte(&d->img, (uint32_t)bam + 0xA3);
        d->write_protect = false;
    }
    rebuild_track(d);
    return true;
}

void c1541_set_host_atn(c1541 *d, bool high)
{
    const bool old = d->host_atn;
    d->host_atn = high;
    if (old != high) {
        via6522_write_ca1(&d->via1, !high);     // CA1: the DOS's ATN interrupt
        update_iec_outputs(d);
    }
}

void c1541_set_host_clk(c1541 *d, bool high) { d->host_clk = high; update_iec_outputs(d); }
void c1541_set_host_data(c1541 *d, bool high) { d->host_data = high; update_iec_outputs(d); }

void c1541_run(c1541 *d, int cycles)
{
    if (!d->rom || cycles <= 0) return;
    update_iec_outputs(d);
    // one instruction at a time, the VIAs and the head with the cycles it took
    d->cycle_debt += cycles;
    while (d->cycle_debt > 0) {
        const int ran = m65xx_run(&d->cpu, 1);
        const int used = ran > 0 ? ran : 1;
        tick_disk(d, used);
        via6522_tick(&d->via1, used);
        via6522_tick(&d->via2, used);
        d->cycle_debt -= used;
    }
}

// ---------------------------------------------------------------------------
// a .d64 holding PRG files (build_d64)
// ---------------------------------------------------------------------------
static void petscii_pad(uint8_t *dst, size_t n, const char *name)
{
    memset(dst, 0xA0, n);
    for (size_t i = 0; i < n && name && name[i]; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[i] = (uint8_t)c;
    }
}

static void bam_allocate(uint8_t *bam, int track, int sector)
{
    uint8_t *e = bam + 4 + (track - 1) * 4;
    if (e[0] > 0) e[0]--;
    e[1 + sector / 8] &= (uint8_t)~(1 << (sector % 8));
}

uint8_t *d64_build(const d64_file *files, int n, const char *disk_name)
{
    uint8_t *img = (uint8_t *)calloc(1, D64_SIZE);
    if (!img) return NULL;
#define SEC(t, s) (img + d64_offset((t), (s)))
    uint8_t *bam = SEC(18, 0);
    bam[0] = 18; bam[1] = 1; bam[2] = 0x41;
    for (int t = 1; t <= 35; t++) {
        uint8_t *e = bam + 4 + (t - 1) * 4;
        const int k = d64_sectors_per_track(t);
        e[0] = (uint8_t)k;
        for (int s = 0; s < k; s++) e[1 + s / 8] |= (uint8_t)(1 << (s % 8));
    }
    petscii_pad(bam + 0x90, 16, disk_name);
    bam[0xA0] = bam[0xA1] = 0xA0;
    bam[0xA2] = 0x41; bam[0xA3] = 0x42; bam[0xA4] = 0xA0;
    bam[0xA5] = '2'; bam[0xA6] = 'A';
    bam[0xA7] = bam[0xA8] = bam[0xA9] = bam[0xAA] = 0xA0;
    bam_allocate(bam, 18, 0);
    int dir_sector = 1, dir_index = 0;
    uint8_t *dir = SEC(18, dir_sector);
    dir[0] = 0; dir[1] = 0xFF;
    bam_allocate(bam, 18, dir_sector);
    int track = 1, sector = 0;
    for (int f = 0; f < n; f++) {
        if (files[f].size < 2) continue;
        if (dir_index == 8) {
            const int next = dir_sector + 3;
            if (next >= d64_sectors_per_track(18)) { free(img); return NULL; }
            dir[0] = 18; dir[1] = (uint8_t)next;
            dir_sector = next;
            dir_index = 0;
            dir = SEC(18, dir_sector);
            dir[0] = 0; dir[1] = 0xFF;
            bam_allocate(bam, 18, dir_sector);
        }
        const int first_track = track, first_sector = sector;
        size_t written = 0;
        int blocks = 0;
        while (written < files[f].size) {
            uint8_t *sec = SEC(track, sector);
            bam_allocate(bam, track, sector);
            blocks++;
            const size_t chunk = files[f].size - written < 254 ? files[f].size - written : 254;
            memcpy(sec + 2, files[f].prg + written, chunk);
            written += chunk;
            if (written >= files[f].size) {
                sec[0] = 0;
                sec[1] = (uint8_t)(chunk + 1);
                break;
            }
            if (++sector >= d64_sectors_per_track(track)) {
                sector = 0;
                if (++track == 18) track = 19;
                if (track > 35) { free(img); return NULL; }
            }
            sec[0] = (uint8_t)track;
            sec[1] = (uint8_t)sector;
        }
        if (++sector >= d64_sectors_per_track(track)) {
            sector = 0;
            if (++track == 18) track = 19;
            if (track > 35) { free(img); return NULL; }
        }
        uint8_t *e = dir + dir_index * 32;
        e[2] = 0x82;
        e[3] = (uint8_t)first_track;
        e[4] = (uint8_t)first_sector;
        petscii_pad(e + 5, 16, files[f].name);
        e[0x1E] = (uint8_t)(blocks & 0xFF);
        e[0x1F] = (uint8_t)(blocks >> 8);
        dir_index++;
    }
#undef SEC
    return img;
}
