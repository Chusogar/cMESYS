// dspc_rom.h - dsp-cpp's RomLoader::find for the dspc cores: one ROM of a
// set by any of its CRCs, else by any of its names ("a|b|c"), exactly `size`
// bytes (malloc'd; NULL: missing, romfs_error() says why). Included once.
#pragma once
#include <stdlib.h>
#include <string.h>
#include "../../../rom/romfs.h"

static uint8_t *dspc_rom_find(const char *const *sets, const char *names, const uint32_t *crcs, int ncrc,
                              uint32_t size)
{
    char first[64];
    const char *bar = strchr(names, '|');
    const size_t n0 = bar ? (size_t)(bar - names) : strlen(names);
    snprintf(first, sizeof first, "%.*s", (int)n0, names);
    uint32_t len = 0;
    for (int i = 0; i < ncrc; i++) {
        uint8_t *p = romfs_get(sets, first, crcs[i], size, &len);
        if (p) return p;
    }
    for (const char *s = names; *s;) {
        const char *e = strchr(s, '|');
        const size_t n = e ? (size_t)(e - s) : strlen(s);
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)n, s);
        uint8_t *p = romfs_get(sets, name, 0, size, &len);
        if (p) return p;
        s = e ? e + 1 : s + n;
    }
    romfs_missing(sets, first);
    return NULL;
}
