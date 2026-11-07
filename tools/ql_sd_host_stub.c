// Host stub for ql_sd_* when building *_pico.c with PORT=picohost: no microSD,
// unless PICO_SD=<dir> names a folder that stands in for the card's root
// (to test the media menus and their loading on the desktop).
#include "../main/drivers/computers/ql/ql_sd.h"
#include <ctype.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *dir(void) { return getenv("PICO_SD"); }

static FILE *open_name(const char *name)
{
    if (!dir()) return NULL;
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir(), name);
    return fopen(p, "rb");
}

bool ql_sd_mount(void) { return dir() != NULL; }
void ql_sd_unmount(void) {}
bool ql_sd_ready(void) { return dir() != NULL; }
const char *ql_sd_error(void) { return dir() ? "" : "no SD on the host (PICO_SD=dir)"; }

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)name[n - k + i]) != tolower((unsigned char)ext[i])) return false;
    return true;
}

int ql_sd_list_ext(ql_sd_entry *out, int max, const char *const *exts)
{
    if (!dir()) return 0;
    DIR *d = opendir(dir());
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        bool ok = false;
        for (int i = 0; exts && exts[i] && !ok; i++) ok = has_ext(e->d_name, exts[i]);
        if (!ok) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

bool ql_sd_open(const char *name) { (void)name; return false; }
void ql_sd_close(void) {}
int ql_sd_read_sector(void *user, unsigned sector, uint8_t dest[686])
{
    (void)user; (void)sector; (void)dest;
    return -1;
}

uint8_t *ql_sd_load_file(const char *name, size_t *out_size)
{
    if (out_size) *out_size = 0;
    FILE *f = open_name(name);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b && out_size) *out_size = (size_t)n;
    return b;
}

bool ql_sd_read_pieces(const char *name, ql_sd_piece_fn fn, void *user)
{
    FILE *f = open_name(name);
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t buf[512];
    size_t k;
    bool ok = true;
    while (ok && (k = fread(buf, 1, sizeof buf, f)) > 0) ok = fn(user, size, buf, k);
    fclose(f);
    return ok;
}

struct ql_sd_file { FILE *f; };

ql_sd_file *ql_sd_file_open(const char *name, size_t *size)
{
    FILE *f = open_name(name);
    if (!f) return NULL;
    ql_sd_file *q = malloc(sizeof *q);
    if (!q) { fclose(f); return NULL; }
    q->f = f;
    fseek(f, 0, SEEK_END);
    if (size) *size = (size_t)ftell(f);
    return q;
}

bool ql_sd_file_read(ql_sd_file *q, uint32_t off, uint8_t *dst, uint32_t n)
{
    return q && fseek(q->f, off, SEEK_SET) == 0 && fread(dst, 1, n, q->f) == n;
}

void ql_sd_file_close(ql_sd_file *q)
{
    if (!q) return;
    fclose(q->f);
    free(q);
}
