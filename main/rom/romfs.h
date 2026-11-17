// romfs.h - the ROMs of a machine from storage, for the builds whose
// rom_data.c has no ROMs in it (a stub, or ROMS=external).
//
// Each machine first uses the ROMs compiled into its rom_data.c; what is not
// there is looked for, by name or (inside a .zip) by CRC, in each search
// directory D, for each set name S the machine gives (its MAME set and the
// sets it borrows from, such as a BIOS):
//
//   D/S/<file>          the files unzipped (exact name, lower or upper case)
//   D/S.zip             the MAME set as it is
//   D/S/*.zip           a .zip inside the set's folder
//
// The directories: desktop and web: --roms DIR, $CMESYS_ROMS (':' or ';'
// separated), ./roms and <program folder>/roms; the web also downloads
// roms/S.zip and roms/S/<file> from next to the page. Android: the app's
// roms folder (Android/data/<app>/files/roms) and the ROMs imported from the
// menu. CYD and RP2040/RP2350: roms/ on the microSD card (and its root).
#ifndef ROMFS_H
#define ROMFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMFS_MAX_DIRS 8

// One way to find a ROM: a file name ("a+b": two files joined), and its
// CRC-32 (0: any).
typedef struct { const char *name; uint32_t crc; } romfs_cand;

// A ROM of a simple set (the computers and the consoles' BIOS): *ptr is set
// to a malloc'd image of `size` bytes from the first candidate found (padded
// with 0xff or cut, as tools/gen_roms_computers.py does), and left as it is
// when it is already set (the ROM compiled in).
typedef struct {
    const uint8_t **ptr;
    uint32_t size;
    const romfs_cand *cands;    // ends with { NULL, 0 }
    bool required;
} romfs_entry;

// Storage: open/read/close (and, optionally, the names in a folder). The
// default is the C library (fopen); the boards install their microSD.
typedef struct {
    void *(*open)(const char *path, uint32_t *size);
    bool (*read)(void *h, uint32_t off, void *dst, uint32_t n);
    void (*close)(void *h);
    // calls fn for each file of the folder; false if there is no such folder
    bool (*list)(const char *dir, void (*fn)(void *user, const char *name), void *user);
} romfs_io;

void romfs_set_io(const romfs_io *io);
const romfs_io *romfs_stdio(void);

// Search directories, in order (copied; "" is the storage's root).
void romfs_add_dir(const char *dir);
void romfs_clear_dirs(void);
int romfs_dir_count(void);
const char *romfs_dir(int i);
// The desktop's defaults: $CMESYS_ROMS, ./roms and the program's folder/roms
// (argv0: the program's path, may be NULL). Added only once.
void romfs_default_dirs(const char *argv0);

// One ROM: a malloc'd image of exactly `size` bytes (0: any size, *len set).
// NULL if it is not found or is of another size (romfs_error says why).
// sets: NULL-terminated set names, looked for in this order.
uint8_t *romfs_get(const char *const *sets, const char *name, uint32_t crc, uint32_t size, uint32_t *len);
// The same, read into dst (exactly `size` bytes).
bool romfs_get_into(const char *const *sets, const char *name, uint32_t crc, uint8_t *dst, uint32_t size);
// The first of several candidates (any size, *len set).
uint8_t *romfs_get_any(const char *const *sets, const romfs_cand *cands, uint32_t *len, const char **found);

// Fills the entries not compiled in; false if a required one is missing.
bool romfs_fill(const char *const *sets, const romfs_entry *e, int n);

// Why the last search failed ("" if it did not).
const char *romfs_error(void);
// For the loaders: says what is missing ("<set>: missing <what>").
void romfs_missing(const char *const *sets, const char *what);
// What to tell the user when a machine's ROMs are missing: lines (each at
// most `cols` characters, '\n' between them) with romfs_error() and where
// they were looked for. Returns buf.
char *romfs_describe(char *buf, size_t n, int cols);
// Frees the cached .zip directory (after a machine's ROMs are loaded).
void romfs_release(void);

uint32_t romfs_crc32(uint32_t crc, const void *data, size_t n);

#ifdef __cplusplus
}
#endif
#endif
