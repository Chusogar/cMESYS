// ql_sd_pico.c - microSD on the Waveshare RP2040-PiZero for QL cartridges.
// SPI0: CS=GP5, SCK=GP6, MISO=GP4, MOSI=GP7 (PWM audio must not use GP4;
// the QL build sets PICO_AUDIO_PIN=22).
#include "ql_sd.h"
#include "ql_mdv.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define PIN_SD_CS   5
#define PIN_SD_SCK  6
#define PIN_SD_MISO 4
#define PIN_SD_MOSI 7
#define SD_SPI      spi0

static bool mounted, is_sdhc;
static uint8_t sector_buf[512];
static uint32_t fat_start, data_start, root_secs, root_ents;
static uint16_t bps, spc;
static uint8_t fats, fat32;
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

static int scan_dir_sector(uint32_t lba, ql_sd_entry *out, int max, int n)
{
    if (sd_read_block(lba, sector_buf) != 0) return n;
    for (int i = 0; i < 16 && n < max; i++) {
        const uint8_t *e = sector_buf + i * 32;
        if (e[0] == 0x00) return n;
        if (e[0] == 0xe5 || (e[11] & 0x08) || (e[11] & 0x10)) continue;
        if (e[8] != 'M' || e[9] != 'D' || e[10] != 'V') continue;
        char nm[13];
        int k = 0;
        for (int j = 0; j < 8 && e[j] != ' '; j++) nm[k++] = (char)e[j];
        nm[k++] = '.';
        for (int j = 8; j < 11 && e[j] != ' '; j++) nm[k++] = (char)e[j];
        nm[k] = 0;
        snprintf(out[n].name, QL_SD_NAME_MAX, "%s", nm);
        n++;
    }
    return n;
}

int ql_sd_list_mdv(ql_sd_entry *out, int max)
{
    if (!mounted || !out || max <= 0) return 0;
    int n = 0;
    if (!fat32) {
        uint32_t root = data_start - root_secs;
        for (uint32_t s = 0; s < root_secs && n < max; s++)
            n = scan_dir_sector(root + s, out, max, n);
        return n;
    }
    // FAT32 root cluster at BPB offset 44
    if (sd_read_block(0, sector_buf) != 0) return 0;
    if (sector_buf[0] != 0xeb && sector_buf[0] != 0xe9) {
        uint32_t bpb = u32(sector_buf + 446 + 8);
        if (sd_read_block(bpb, sector_buf) != 0) return 0;
    }
    uint32_t cl = u32(sector_buf + 44);
    while (cl >= 2 && cl < 0x0ffffff8 && n < max) {
        for (uint8_t s = 0; s < spc && n < max; s++)
            n = scan_dir_sector((uint32_t)cluster_lba(cl) + s, out, max, n);
        cl = next_cluster(cl);
    }
    return n;
}

bool ql_sd_open(const char *name)
{
    if (!mounted || !name) return false;
    ql_sd_close();
    char want[11];
    make83(name, want);
    uint32_t root = data_start - root_secs;
    uint32_t nsec = root_secs ? root_secs : 0;
    uint32_t cl = 0;
    if (fat32) {
        if (sd_read_block(0, sector_buf) != 0) return false;
        uint32_t bpb = 0;
        if (sector_buf[0] != 0xeb && sector_buf[0] != 0xe9) {
            bpb = u32(sector_buf + 446 + 8);
            if (sd_read_block(bpb, sector_buf) != 0) return false;
        } else (void)bpb;
        cl = u32(sector_buf + 44);
    }
    for (;;) {
        uint32_t lba;
        uint32_t count;
        if (!fat32) {
            lba = root;
            count = nsec;
        } else {
            if (cl < 2 || cl >= 0x0ffffff8) return false;
            lba = (uint32_t)cluster_lba(cl);
            count = spc;
        }
        for (uint32_t s = 0; s < count; s++) {
            if (sd_read_block(lba + s, sector_buf) != 0) return false;
            for (int i = 0; i < 16; i++) {
                const uint8_t *e = sector_buf + i * 32;
                if (e[0] == 0x00) return false;
                if (e[0] == 0xe5 || (e[11] & 0x18)) continue;
                if (memcmp(e, want, 11) != 0) continue;
                file_size = u32(e + 28);
                file_start_cluster = fat32 ? (u16(e + 20) << 16) | u16(e + 26) : u16(e + 26);
                if (file_size != QL_MDV_IMAGE_BYTES) {
                    printf("ql_sd: %s size %u, expected %d\n", name, (unsigned)file_size, QL_MDV_IMAGE_BYTES);
                    return false;
                }
                file_pos = 0;
                file_open = true;
                return true;
            }
        }
        if (!fat32) return false;
        cl = next_cluster(cl);
    }
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
