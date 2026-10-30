// ql_sd.h - list/open media on a microSD card (CYD / RP2040).
// Used by the QL (.mdv) and ZX Spectrum (.tap/.tzx/.sna) frontends.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define QL_SD_MAX_FILES 48
#define QL_SD_NAME_MAX  64

typedef struct {
    char name[QL_SD_NAME_MAX];
} ql_sd_entry;

// Mount the board's microSD (FAT). false if no card / init failed.
bool ql_sd_mount(void);
void ql_sd_unmount(void);
bool ql_sd_ready(void);

// Fills out[0..max) with names matching a NULL-terminated list of extensions
// (e.g. {".mdv", NULL} or {".tap",".tzx",".sna", NULL}) in the card root
// (and /mdv if present). Case-insensitive. Returns the number of entries.
// Why the last mount failed ("" if it did not / not known), for the menus.
const char *ql_sd_error(void);
int ql_sd_list_ext(ql_sd_entry *out, int max, const char *const *exts);
static inline int ql_sd_list_mdv(ql_sd_entry *out, int max)
{
    static const char *const mdv[] = { ".mdv", NULL };
    return ql_sd_list_ext(out, max, mdv);
}

// Open a listed file for ql_cart_host_load_reader(ql_sd_read_sector, ...).
// Requires exact QLAY size (174930). For arbitrary files use ql_sd_load_file.
bool ql_sd_open(const char *name);
void ql_sd_close(void);
// Sector reader for ql_mount_reader / ql_cart_host_load_reader.
int ql_sd_read_sector(void *user, unsigned sector, uint8_t dest[686]);
// Load an entire file into a malloc'd buffer (*out_size set). Caller frees.
uint8_t *ql_sd_load_file(const char *name, size_t *out_size);
// Read a file a piece at a time: fn gets the file size and each piece in
// order (false from fn stops). False if the file cannot be read to the end.
// A file kept open and read at any offset (the Spectrum's tapes and +3
// disks are read from the card as they play). NULL if it cannot be opened.
typedef struct ql_sd_file ql_sd_file;
ql_sd_file *ql_sd_file_open(const char *name, size_t *size);
bool ql_sd_file_read(ql_sd_file *f, uint32_t off, uint8_t *dst, uint32_t n);
void ql_sd_file_close(ql_sd_file *f);
typedef bool (*ql_sd_piece_fn)(void *user, size_t size, const uint8_t *data, size_t n);
bool ql_sd_read_pieces(const char *name, ql_sd_piece_fn fn, void *user);
