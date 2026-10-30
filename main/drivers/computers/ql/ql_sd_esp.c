// ql_sd_esp.c - microSD (VSPI) on the ESP32-2432S028 CYD for QL cartridges.
#include "ql_sd.h"
#include "ql_mdv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"

// CYD onboard microSD (VSPI), separate from the TFT SPI bus.
#define PIN_SD_CS   5
#define PIN_SD_SCK  18
#define PIN_SD_MOSI 23
#define PIN_SD_MISO 19
#define SD_MOUNT    "/sd"

static const char *TAG = "ql_sd";
static sdmmc_card_t *card;
static FILE *open_file;
static bool mounted;
static const char *last_error = "";

bool ql_sd_ready(void) { return mounted; }

bool ql_sd_mount(void)
{
    if (mounted) return true;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    // SPI_DMA_CH_AUTO: on the ESP32 SDSPI_DEFAULT_DMA asks for DMA channel 1,
    // which the LCD bus (SPI2, CH_AUTO) already has -> ESP_ERR_INVALID_STATE,
    // the bus was never set up and the card never answered.
    static bool bus_ok;
    if (!bus_ok) {
        esp_err_t e = spi_bus_initialize(host.slot, &bus, SPI_DMA_CH_AUTO);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(e));
            return false;
        }
        bus_ok = true;
    }
    // the card's lines idle high (some CYD batches have no pull-ups fitted)
    gpio_set_pull_mode(PIN_SD_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SD_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SD_CS, GPIO_PULLUP_ONLY);
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = host.slot;
    // 20 MHz, then slower for cards / wiring that do not cope (Arduino's SD uses 4 MHz)
    static const int khz[] = { SDMMC_FREQ_DEFAULT, 10000, 4000 };
    esp_err_t e = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        host.max_freq_khz = khz[i];
        e = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &cfg, &card);
        if (e == ESP_OK) break;
        ESP_LOGW(TAG, "sd mount at %d kHz failed: %s", khz[i], esp_err_to_name(e));
    }
    if (e != ESP_OK) {
        last_error = esp_err_to_name(e);
        return false;
    }
    mounted = true;
    last_error = "";
    ESP_LOGI(TAG, "SD mounted at %s (%d kHz)", SD_MOUNT, host.max_freq_khz);
    return true;
}

const char *ql_sd_error(void) { return last_error; }

void ql_sd_unmount(void)
{
    ql_sd_close();
    if (!mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    card = NULL;
    mounted = false;
}

// A listed file: in the root, mdv/ or tape/ (as ql_sd_list_ext looks)
static FILE *open_listed(const char *name)
{
    static const char *const dirs[] = { "", "/mdv", "/tape" };
    char path[96];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, SD_MOUNT "%s/%s", dirs[i], name);
        FILE *f = fopen(path, "rb");
        if (f) return f;
    }
    return NULL;
}

static int ext_match(const char *nm, size_t len, const char *const *exts)
{
    if (!exts) return 0;
    for (int i = 0; exts[i]; i++) {
        size_t el = strlen(exts[i]);
        if (len < el) continue;
        const char *a = nm + len - el, *b = exts[i];
        int ok = 1;
        for (size_t k = 0; k < el; k++) {
            char ca = a[k], cb = b[k];
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static int add_dir(const char *dir, ql_sd_entry *out, int max, int n, const char *const *exts)
{
    DIR *d = opendir(dir);
    if (!d) return n;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        size_t len = strlen(nm);
        if (len < 2 || len >= QL_SD_NAME_MAX) continue;
        if (!ext_match(nm, len, exts)) continue;
        snprintf(out[n].name, QL_SD_NAME_MAX, "%s", nm);
        n++;
    }
    closedir(d);
    return n;
}

int ql_sd_list_ext(ql_sd_entry *out, int max, const char *const *exts)
{
    if (!mounted || !out || max <= 0 || !exts) return 0;
    int n = add_dir(SD_MOUNT, out, max, 0, exts);
    n = add_dir(SD_MOUNT "/mdv", out, max, n, exts);
    n = add_dir(SD_MOUNT "/tape", out, max, n, exts);
    return n;
}

bool ql_sd_open(const char *name)
{
    if (!mounted || !name || !name[0]) return false;
    ql_sd_close();
    open_file = open_listed(name);
    if (!open_file) return false;
    if (fseek(open_file, 0, SEEK_END) != 0) { ql_sd_close(); return false; }
    long sz = ftell(open_file);
    if (sz != (long)QL_MDV_IMAGE_BYTES) {
        ESP_LOGW(TAG, "%s: size %ld, expected %d", name, sz, QL_MDV_IMAGE_BYTES);
        ql_sd_close();
        return false;
    }
    rewind(open_file);
    return true;
}

void ql_sd_close(void)
{
    if (open_file) { fclose(open_file); open_file = NULL; }
}

int ql_sd_read_sector(void *user, unsigned sector, uint8_t dest[686])
{
    (void)user;
    if (!open_file || sector >= QL_MDV_SECTORS) return -1;
    if (fseek(open_file, (long)sector * QL_MDV_SECTOR_BYTES, SEEK_SET) != 0) return -1;
    return fread(dest, 1, QL_MDV_SECTOR_BYTES, open_file) == QL_MDV_SECTOR_BYTES ? 0 : -1;
}

uint8_t *ql_sd_load_file(const char *name, size_t *out_size)
{
    if (out_size) *out_size = 0;
    if (!mounted || !name || !name[0]) return NULL;
    char path[96];
    FILE *f = open_listed(name);
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_size) *out_size = (size_t)sz;
    return buf;
}

bool ql_sd_read_pieces(const char *name, ql_sd_piece_fn fn, void *user)
{
    if (!mounted || !name || !name[0] || !fn) return false;
    char path[96];
    FILE *f = open_listed(name);
    if (!f) return false;
    long sz = -1;
    if (fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc(4096);
    bool ok = sz > 0 && buf;
    size_t left = ok ? (size_t)sz : 0;
    while (ok && left) {
        size_t k = left < 4096 ? left : 4096;
        ok = fread(buf, 1, k, f) == k && fn(user, (size_t)sz, buf, k);
        left -= k;
    }
    free(buf);
    fclose(f);
    return ok;
}
