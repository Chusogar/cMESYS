// ql_sd_esp.c - microSD (VSPI) on the ESP32-2432S028 CYD for QL cartridges.
#include "ql_sd.h"
#include "ql_mdv.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

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
    esp_err_t e = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(e));
        return false;
    }
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = host.slot;
    e = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &cfg, &card);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "sd mount failed: %s", esp_err_to_name(e));
        return false;
    }
    mounted = true;
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT);
    return true;
}

void ql_sd_unmount(void)
{
    ql_sd_close();
    if (!mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    card = NULL;
    mounted = false;
}

static int add_dir(const char *dir, ql_sd_entry *out, int max, int n)
{
    DIR *d = opendir(dir);
    if (!d) return n;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        size_t len = strlen(nm);
        if (len < 5 || len >= QL_SD_NAME_MAX) continue;
        // case-insensitive .mdv
        const char *ext = nm + len - 4;
        if (!((ext[0] == '.' ) &&
              (ext[1] == 'm' || ext[1] == 'M') &&
              (ext[2] == 'd' || ext[2] == 'D') &&
              (ext[3] == 'v' || ext[3] == 'V'))) continue;
        snprintf(out[n].name, QL_SD_NAME_MAX, "%s", nm);
        n++;
    }
    closedir(d);
    return n;
}

int ql_sd_list_mdv(ql_sd_entry *out, int max)
{
    if (!mounted || !out || max <= 0) return 0;
    int n = add_dir(SD_MOUNT, out, max, 0);
    n = add_dir(SD_MOUNT "/mdv", out, max, n);
    return n;
}

bool ql_sd_open(const char *name)
{
    if (!mounted || !name || !name[0]) return false;
    ql_sd_close();
    char path[96];
    snprintf(path, sizeof path, SD_MOUNT "/%s", name);
    open_file = fopen(path, "rb");
    if (!open_file) {
        snprintf(path, sizeof path, SD_MOUNT "/mdv/%s", name);
        open_file = fopen(path, "rb");
    }
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
