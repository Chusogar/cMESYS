// ql_sd_pico.c - microSD on the Waveshare RP2040-PiZero for QL cartridges.
// The board's own slot (as dsp-cpp). RP2040-PiZero: SPI0 SCK=GP18,
// MOSI=GP19, MISO=GP20, CS=GP21 (GP22-29 are the HDMI pairs; the PWM sound
// stays on GP4). RP2350-PiZero: below.
#include "ql_sd.h"
#include "ql_mdv.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#if PICO_RP2350
// RP2350-PiZero (as dsp-cpp): SPI1, SCK GP30, MOSI GP31, MISO GP40, CS GP43
#define PIN_SD_CS   43
#define PIN_SD_SCK  30
#define PIN_SD_MISO 40
#define PIN_SD_MOSI 31
#define SD_SPI      spi1
#else
#define PIN_SD_CS   21
#define PIN_SD_SCK  18
#define PIN_SD_MISO 20
#define PIN_SD_MOSI 19
#define SD_SPI      spi0
#endif

static bool mounted, is_sdhc;
static uint8_t sector_buf[512];
static uint32_t fat_start, data_start, root_secs, root_ents;
static uint16_t bps, spc;
static uint8_t fats, fat32;
static uint32_t root_cl;            // FAT32: first cluster of the root directory
static uint32_t file_start_cluster, file_size, file_pos;
static bool file_open;

static void cs(bool sel) { gpio_put(PIN_SD_CS, sel ? 0 : 1); }

static uint8_t spi_tx(uint8_t v)
{
    uint8_t r;
    spi_write_read_blocking(SD_SPI, &v, &r, 1);
    return r;
}

static void spi_read(uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) p[i] = spi_tx(0xff);
}

static int sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    cs(true);
    spi_tx(0xff);
    spi_tx(0x40 | cmd);
    spi_tx((uint8_t)(arg >> 24));
    spi_tx((uint8_t)(arg >> 16));
    spi_tx((uint8_t)(arg >> 8));
    spi_tx((uint8_t)arg);
    spi_tx(crc);
    for (int i = 0; i < 20; i++) {
        uint8_t r = spi_tx(0xff);
        if (!(r & 0x80)) return r;
    }
    return -1;
}

static int sd_read_block(uint32_t lba, uint8_t *dst)
{
    uint32_t a = is_sdhc ? lba : lba * 512;
    if (sd_cmd(17, a, 0xff) != 0) { cs(false); return -1; }
    for (int i = 0; i < 100000; i++) {
        uint8_t t = spi_tx(0xff);
        if (t == 0xfe) {
            spi_read(dst, 512);
            spi_tx(0xff); spi_tx(0xff);
            cs(false);
            spi_tx(0xff);
            return 0;
        }
    }
    cs(false);
    return -1;
}

static int sd_init_card(void)
{
    spi_init(SD_SPI, 200 * 1000);
    gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_SD_MISO);
    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    cs(false);
    for (int i = 0; i < 80; i++) spi_tx(0xff);
    if (sd_cmd(0, 0, 0x95) != 1) return -1;
    int r = sd_cmd(8, 0x1aa, 0x87);
    if (r == 1) {
        spi_tx(0xff); spi_tx(0xff); spi_tx(0xff); spi_tx(0xff);
    }
    for (int i = 0; i < 500; i++) {
        sd_cmd(55, 0, 0xff);
        r = sd_cmd(41, 0x40000000, 0xff);
        if (r == 0) break;
        sleep_ms(2);
    }
    if (r != 0) return -1;
    is_sdhc = false;
    if (sd_cmd(58, 0, 0xff) == 0) {
        uint8_t ocr[4];
        spi_read(ocr, 4);
        is_sdhc = (ocr[0] & 0x40) != 0;
    }
    cs(false);
    spi_tx(0xff);
    spi_init(SD_SPI, 10 * 1000 * 1000);
    return 0;
}

static uint32_t u16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | (u16(p + 2) << 16); }

static int fat_mount(void)
{
    if (sd_read_block(0, sector_buf) != 0) return -1;
    uint32_t bpb = 0;
    if (sector_buf[0] != 0xeb && sector_buf[0] != 0xe9) {
        // MBR
        bpb = u32(sector_buf + 446 + 8);
        if (sd_read_block(bpb, sector_buf) != 0) return -1;
    }
    bps = (uint16_t)u16(sector_buf + 11);
    spc = sector_buf[13];
    uint16_t rsv = (uint16_t)u16(sector_buf + 14);
    fats = sector_buf[16];
    root_ents = u16(sector_buf + 17);
    uint32_t tot = u16(sector_buf + 19);
    if (!tot) tot = u32(sector_buf + 32);
    uint32_t fat_sz = u16(sector_buf + 22);
    if (!fat_sz) fat_sz = u32(sector_buf + 36);
    fat32 = root_ents == 0;
    root_cl = fat32 ? u32(sector_buf + 44) : 0;
    fat_start = bpb + rsv;
    root_secs = fat32 ? 0 : ((root_ents * 32) + (bps - 1)) / bps;
    data_start = fat_start + fats * fat_sz + root_secs;
    (void)tot;
    if (bps != 512 || spc == 0) return -1;
    return 0;
}

static uint32_t next_cluster(uint32_t c)
{
    if (fat32) {
        uint32_t off = fat_start + (c * 4) / 512;
        uint32_t ix = (c * 4) % 512;
        if (sd_read_block(off, sector_buf) != 0) return 0;
        return u32(sector_buf + ix) & 0x0fffffff;
    }
    uint32_t off = fat_start + (c * 2) / 512;
    uint32_t ix = (c * 2) % 512;
    if (sd_read_block(off, sector_buf) != 0) return 0;
    return u16(sector_buf + ix);
}

static int cluster_lba(uint32_t c)
{
    return (int)(data_start + (c - 2) * spc);
}

static void make83(const char *name, char out[11])
{
    memset(out, ' ', 11);
    int i = 0, j = 0;
    for (; name[i] && name[i] != '.' && j < 8; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[j++] = c;
    }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') i++;
    for (j = 8; name[i] && j < 11; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[j++] = c;
    }
}

bool ql_sd_ready(void) { return mounted; }
const char *ql_sd_error(void) { return mounted ? "" : "no card / not FAT"; }

bool ql_sd_mount(void)
{
    if (mounted) return true;
    if (sd_init_card() != 0 || fat_mount() != 0) {
        printf("ql_sd: mount failed\n");
        return false;
    }
    mounted = true;
    printf("ql_sd: mounted (FAT%s)\n", fat32 ? "32" : "16");
    return true;
}

void ql_sd_unmount(void)
{
    ql_sd_close();
    mounted = false;
}

// ---------------------------------------------------------------------------
// Directories: the root and its MDV and TAPE folders (as the CYD), with long
// file names. A listing remembers where each file it shows starts, so the
// menus open them by the name they show.
// ---------------------------------------------------------------------------
typedef bool (*dir_fn)(void *user, const uint8_t *e, const char *name);

static uint8_t lfn_sum(const uint8_t *e)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + e[i]);
    return sum;
}

// Calls fn for every file and folder of the directory starting at cluster
// cl (0: the root), with its long name when it has one; false: read error.
static bool walk_dir(uint32_t cl, dir_fn fn, void *user)
{
    static const uint8_t lfn_at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    char lfn[QL_SD_NAME_MAX], name[QL_SD_NAME_MAX];
    int lfn_ok = 0;
    uint8_t lfn_ck = 0;
    const bool fixed_root = !fat32 && cl == 0;
    if (!fixed_root && cl == 0) cl = root_cl;
    uint32_t root_lba = data_start - root_secs, sec = 0;
    for (;;) {
        uint32_t lba, count;
        if (fixed_root) {
            if (sec >= root_secs) return true;
            lba = root_lba + sec;
            count = 1;
            sec++;
        } else {
            if (cl < 2 || cl >= (fat32 ? 0x0ffffff8u : 0xfff8u)) return true;
            lba = (uint32_t)cluster_lba(cl);
            count = spc;
        }
        for (uint32_t s = 0; s < count; s++) {
            if (sd_read_block(lba + s, sector_buf) != 0) return false;
            for (int i = 0; i < 16; i++) {
                const uint8_t *e = sector_buf + i * 32;
                if (e[0] == 0x00) return true;
                if (e[0] == 0xe5) { lfn_ok = 0; continue; }
                if ((e[11] & 0x3f) == 0x0f) {                  // a piece of a long name
                    const int seq = e[0] & 0x1f;
                    if (e[0] & 0x40) { memset(lfn, 0, sizeof lfn); lfn_ok = 1; lfn_ck = e[13]; }
                    if (!lfn_ok || e[13] != lfn_ck || seq < 1) { lfn_ok = 0; continue; }
                    for (int k = 0; k < 13; k++) {
                        const int at = (seq - 1) * 13 + k;
                        const uint8_t lo = e[lfn_at[k]], hi = e[lfn_at[k] + 1];
                        if ((lo == 0 && hi == 0) || (lo == 0xff && hi == 0xff)) break;
                        if (at < QL_SD_NAME_MAX - 1) lfn[at] = hi ? '?' : (char)lo;
                    }
                    continue;
                }
                if (e[11] & 0x08) { lfn_ok = 0; continue; }          // volume label
                if (e[0] == '.') { lfn_ok = 0; continue; }           // . and ..
                if (lfn_ok && lfn_sum(e) == lfn_ck && lfn[0]) {
                    snprintf(name, sizeof name, "%s", lfn);
                } else {
                    int k = 0;
                    for (int j = 0; j < 8 && e[j] != ' '; j++) name[k++] = (char)e[j];
                    if (e[8] != ' ') {
                        name[k++] = '.';
                        for (int j = 8; j < 11 && e[j] != ' '; j++) name[k++] = (char)e[j];
                    }
                    name[k] = 0;
                }
                lfn_ok = 0;
                uint8_t keep[32];
                memcpy(keep, e, 32);                 // (fn may read other sectors)
                if (!fn(user, keep, name)) return true;
                if (sd_read_block(lba + s, sector_buf) != 0) return false;
            }
        }
        if (fixed_root) continue;
        cl = next_cluster(cl);
    }
}

static uint32_t entry_cluster(const uint8_t *e)
{
    return fat32 ? ((uint32_t)u16(e + 20) << 16) | u16(e + 26) : u16(e + 26);
}

static bool name_has_ext(const char *name, const char *const *exts)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !exts) return false;
    for (int i = 0; exts[i]; i++) {
        const char *a = dot, *b = exts[i];
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return true;
    }
    return false;
}

// The last listing: where its files are
static struct { char name[QL_SD_NAME_MAX]; uint32_t cl, size; } listed[QL_SD_MAX_FILES];
static int nlisted;

typedef struct { ql_sd_entry *out; int max, n; const char *const *exts; } list_ctx;

static bool list_one(void *user, const uint8_t *e, const char *name)
{
    list_ctx *c = (list_ctx *)user;
    if (c->n >= c->max) return false;
    if (e[11] & 0x10) return true;                          // folders: not here
    if (!name_has_ext(name, c->exts)) return true;
    for (int i = 0; i < c->n; i++)                          // (the same name in two folders: the first)
        if (strcmp(c->out[i].name, name) == 0) return true;
    snprintf(c->out[c->n].name, QL_SD_NAME_MAX, "%s", name);
    if (c->n < QL_SD_MAX_FILES) {
        snprintf(listed[c->n].name, QL_SD_NAME_MAX, "%s", name);
        listed[c->n].cl = entry_cluster(e);
        listed[c->n].size = u32(e + 28);
    }
    c->n++;
    return true;
}

typedef struct { const char *want; uint32_t cl; } folder_ctx;

static bool find_folder(void *user, const uint8_t *e, const char *name)
{
    folder_ctx *c = (folder_ctx *)user;
    if (!(e[11] & 0x10)) return true;
    for (int i = 0;; i++) {
        if (toupper((unsigned char)name[i]) != c->want[i]) return true;
        if (!name[i]) break;
    }
    c->cl = entry_cluster(e);
    return false;
}

int ql_sd_list_ext(ql_sd_entry *out, int max, const char *const *exts)
{
    if (!mounted || !out || max <= 0 || !exts) return 0;
    list_ctx c = { out, max, 0, exts };
    nlisted = 0;
    walk_dir(0, list_one, &c);
    static const char *const folders[] = { "MDV", "TAPE", "ZX", "GAMES" };
    for (size_t f = 0; f < sizeof folders / sizeof folders[0] && c.n < max; f++) {
        folder_ctx fc = { folders[f], 0 };
        walk_dir(0, find_folder, &fc);
        if (fc.cl >= 2) walk_dir(fc.cl, list_one, &c);
    }
    nlisted = c.n < QL_SD_MAX_FILES ? c.n : QL_SD_MAX_FILES;
    return c.n;
}

typedef struct { const char *name; char want83[11]; bool found; uint32_t cl, size; } find_ctx;

static bool find_one(void *user, const uint8_t *e, const char *name)
{
    find_ctx *c = (find_ctx *)user;
    if (e[11] & 0x10) return true;
    bool same = memcmp(e, c->want83, 11) == 0;
    if (!same) {
        same = true;
        for (int i = 0; same; i++) {
            if (tolower((unsigned char)name[i]) != tolower((unsigned char)c->name[i])) same = false;
            else if (!name[i]) break;
        }
    }
    if (!same) return true;
    c->found = true;
    c->cl = entry_cluster(e);
    c->size = u32(e + 28);
    return false;
}

// Finds a file by the name a listing showed (or in the root by its name)
static bool locate(const char *name, uint32_t *cl, uint32_t *size)
{
    for (int i = 0; i < nlisted; i++)
        if (strcmp(listed[i].name, name) == 0) {
            *cl = listed[i].cl;
            *size = listed[i].size;
            return true;
        }
    find_ctx c;
    memset(&c, 0, sizeof c);
    c.name = name;
    make83(name, c.want83);
    walk_dir(0, find_one, &c);
    if (!c.found) return false;
    *cl = c.cl;
    *size = c.size;
    return true;
}

bool ql_sd_open(const char *name)
{
    if (!mounted || !name) return false;
    ql_sd_close();
    uint32_t cl, size;
    if (!locate(name, &cl, &size)) return false;
    if (size != QL_MDV_IMAGE_BYTES) {
        printf("ql_sd: %s size %u, expected %d\n", name, (unsigned)size, QL_MDV_IMAGE_BYTES);
        return false;
    }
    file_start_cluster = cl;
    file_size = size;
    file_pos = 0;
    file_open = true;
    return true;
}

void ql_sd_close(void) { file_open = false; file_pos = 0; }

int ql_sd_read_sector(void *user, unsigned sector, uint8_t dest[686])
{
    (void)user;
    if (!file_open || sector >= QL_MDV_SECTORS) return -1;
    uint32_t want = sector * QL_MDV_SECTOR_BYTES;
    // Walk clusters from the start (images are small / infrequent seeks).
    uint32_t cl = file_start_cluster;
    uint32_t skip = want;
    while (skip >= (uint32_t)spc * 512) {
        cl = next_cluster(cl);
        if (cl < 2) return -1;
        skip -= (uint32_t)spc * 512;
    }
    uint32_t left = QL_MDV_SECTOR_BYTES;
    uint8_t *out = dest;
    while (left) {
        uint32_t lba = (uint32_t)cluster_lba(cl) + (skip / 512);
        if (sd_read_block(lba, sector_buf) != 0) return -1;
        uint32_t off = skip % 512;
        uint32_t n = 512 - off;
        if (n > left) n = left;
        memcpy(out, sector_buf + off, n);
        out += n;
        left -= n;
        skip += n;
        if (skip >= (uint32_t)spc * 512) {
            skip = 0;
            cl = next_cluster(cl);
            if (cl < 2 && left) return -1;
        }
    }
    file_pos = want + QL_MDV_SECTOR_BYTES;
    return 0;
}

static bool ql_sd_find(const char *name, size_t *out_size)
{
    if (!mounted || !name) return false;
    ql_sd_close();
    uint32_t cl, size;
    if (!locate(name, &cl, &size) || size == 0 || size > 8 * 1024 * 1024) return false;
    file_size = size;
    file_start_cluster = cl;
    file_open = true;
    *out_size = file_size;
    return true;
}

uint8_t *ql_sd_load_file(const char *name, size_t *out_size)
{
    if (out_size) *out_size = 0;
    size_t fsz = 0;
    if (!ql_sd_find(name, &fsz)) return NULL;
    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf) { ql_sd_close(); return NULL; }
    /* Read whole file via 686-sized path is wrong; read by 512. */
    uint32_t left = file_size;
    uint32_t cl = file_start_cluster;
    uint8_t *out = buf;
    uint32_t skip = 0;
    while (left) {
        uint32_t lba = (uint32_t)cluster_lba(cl) + (skip / 512);
        if (sd_read_block(lba, sector_buf) != 0) { free(buf); ql_sd_close(); return NULL; }
        uint32_t off = skip % 512;
        uint32_t n = 512 - off;
        if (n > left) n = left;
        memcpy(out, sector_buf + off, n);
        out += n; left -= n; skip += n;
        if (skip >= (uint32_t)spc * 512) { skip = 0; cl = next_cluster(cl); if (cl < 2 && left) { free(buf); ql_sd_close(); return NULL; } }
    }
    ql_sd_close();
    if (out_size) *out_size = file_size;
    return buf;
}

bool ql_sd_read_pieces(const char *name, ql_sd_piece_fn fn, void *user)
{
    // (finds the file as ql_sd_load_file does, then reads it 512 bytes at a time)
    size_t size = 0;
    if (!mounted || !name || !fn || !ql_sd_find(name, &size)) return false;
    uint32_t left = (uint32_t)size, skip = 0, cl = file_start_cluster;
    bool ok = true;
    while (ok && left) {
        uint32_t lba = (uint32_t)cluster_lba(cl) + (skip / 512);
        if (sd_read_block(lba, sector_buf) != 0) { ok = false; break; }
        uint32_t n = 512;
        if (n > left) n = left;
        ok = fn(user, size, sector_buf, n);
        left -= n;
        skip += n;
        if (skip >= (uint32_t)spc * 512) {
            skip = 0;
            cl = next_cluster(cl);
            if (cl < 2 && left) ok = false;
        }
    }
    ql_sd_close();
    return ok;
}

// A file of its own (start cluster and size), read at any offset by
// following its cluster chain (remembering the last cluster reached).
struct ql_sd_file { uint32_t start, size, at_index, at_cluster; };

ql_sd_file *ql_sd_file_open(const char *name, size_t *size)
{
    size_t sz = 0;
    if (!ql_sd_find(name, &sz)) return NULL;
    ql_sd_file *h = (ql_sd_file *)malloc(sizeof *h);
    if (h) {
        h->start = h->at_cluster = file_start_cluster;
        h->size = (uint32_t)sz;
        h->at_index = 0;
        *size = sz;
    }
    ql_sd_close();
    return h;
}

bool ql_sd_file_read(ql_sd_file *h, uint32_t off, uint8_t *dst, uint32_t n)
{
    if (!h || !mounted || off > h->size || n > h->size - off) return false;
    const uint32_t csize = (uint32_t)spc * 512;
    while (n) {
        const uint32_t ci = off / csize;
        if (ci < h->at_index) { h->at_index = 0; h->at_cluster = h->start; }
        while (h->at_index < ci) {
            h->at_cluster = next_cluster(h->at_cluster);
            if (h->at_cluster < 2) { h->at_index = 0; h->at_cluster = h->start; return false; }
            h->at_index++;
        }
        const uint32_t in = off % csize;
        if (sd_read_block((uint32_t)cluster_lba(h->at_cluster) + in / 512, sector_buf) != 0) return false;
        uint32_t k = 512 - in % 512;
        if (k > n) k = n;
        memcpy(dst, sector_buf + in % 512, k);
        dst += k;
        off += k;
        n -= k;
    }
    return true;
}

void ql_sd_file_close(ql_sd_file *h) { free(h); }
