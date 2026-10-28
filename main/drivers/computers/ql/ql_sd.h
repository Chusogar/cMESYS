// ql_sd.h - list/open QLAY .mdv cartridges on a microSD card (CYD / RP2040).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define QL_SD_MAX_FILES 48
#define QL_SD_NAME_MAX  48

typedef struct {
    char name[QL_SD_NAME_MAX];
} ql_sd_entry;

// Mount the board's microSD (FAT). false if no card / init failed.
bool ql_sd_mount(void);
void ql_sd_unmount(void);
bool ql_sd_ready(void);

// Fills out[0..max) with *.mdv names in the card root (and /mdv if present).
// Returns the number of entries.
int ql_sd_list_mdv(ql_sd_entry *out, int max);

// Open a listed file for ql_cart_host_load_reader(ql_sd_read_sector, ...).
bool ql_sd_open(const char *name);
void ql_sd_close(void);
// Sector reader for ql_mount_reader / ql_cart_host_load_reader.
int ql_sd_read_sector(void *user, unsigned sector, uint8_t dest[686]);
