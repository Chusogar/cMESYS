// arc_flash_sim.h - the ESP32 partition calls the flash cache uses, over a
// file (ARC_FLASH_SIM builds on the desktop: tests the CYD's arccache)
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0xff
#define ESP_PARTITION_MMAP_DATA 0
typedef int esp_partition_mmap_handle_t;
typedef struct { uint32_t size; } esp_partition_t;
#define ESP_LOGE(t, ...) (fprintf(stderr, "E %s: ", t), fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define ESP_LOGI(t, ...) (fprintf(stderr, "I %s: ", t), fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
static esp_partition_t sim_part = { 0x200000 };
static uint8_t *sim_flash;
static int sim_erases;
static void sim_save(void)
{
    FILE *f = fopen(getenv("ARC_FLASH_FILE") ? getenv("ARC_FLASH_FILE") : "/tmp/arccache.bin", "wb");
    if (f) { fwrite(sim_flash, 1, sim_part.size, f); fclose(f); }
    fprintf(stderr, "flash sim: %d sectors erased\n", sim_erases);
}
static const esp_partition_t *esp_partition_find_first(int t, int s, const char *n)
{
    (void)t, (void)s, (void)n;
    sim_flash = (uint8_t *)malloc(sim_part.size);
    memset(sim_flash, 0xff, sim_part.size);
    FILE *f = fopen(getenv("ARC_FLASH_FILE") ? getenv("ARC_FLASH_FILE") : "/tmp/arccache.bin", "rb");
    if (f) { if (fread(sim_flash, 1, sim_part.size, f)) {} fclose(f); }
    atexit(sim_save);
    return &sim_part;
}
static esp_err_t esp_partition_read(const esp_partition_t *p, uint32_t o, void *d, size_t n)
{
    (void)p;
    memcpy(d, sim_flash + o, n);
    return ESP_OK;
}
static esp_err_t esp_partition_erase_range(const esp_partition_t *p, uint32_t o, size_t n)
{
    (void)p;
    if (o % 4096 || n % 4096 || o + n > sim_part.size) return 1;
    memset(sim_flash + o, 0xff, n);
    sim_erases += (int)(n / 4096);
    return ESP_OK;
}
static esp_err_t esp_partition_write(const esp_partition_t *p, uint32_t o, const void *s, size_t n)
{
    (void)p;
    if (o + n > sim_part.size) return 1;
    for (size_t i = 0; i < n; i++) sim_flash[o + i] &= ((const uint8_t *)s)[i];   // bits only go to 0
    return ESP_OK;
}
static esp_err_t esp_partition_mmap(const esp_partition_t *p, uint32_t o, size_t n, int m, const void **out,
                                    esp_partition_mmap_handle_t *h)
{
    (void)p, (void)n, (void)m;
    *out = sim_flash + o;
    *h = 1;
    return ESP_OK;
}
static void esp_partition_munmap(esp_partition_mmap_handle_t h) { (void)h; }
