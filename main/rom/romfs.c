// romfs.c - the ROMs of a machine from storage (see romfs.h).
#include "romfs.h"
#include "../machine/zx_inflate.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <sys/stat.h>
#endif
#if !defined(ROMFS_SD) && !defined(ROMFS_NO_DIRENT)
#include <dirent.h>
#define ROMFS_HAVE_DIRENT 1
#endif

#ifdef ROMFS_SD
#define PATH_MAX_ 128            // (the boards: little RAM; "roms/<set>/<file>" on the card)
#define NDIRS 4
#define ZIPS_IN_DIR 4
#else
#define PATH_MAX_ 320
#define NDIRS ROMFS_MAX_DIRS
#define ZIPS_IN_DIR 8
#endif

static char dirs[NDIRS][PATH_MAX_];
static int ndirs;
static bool defaults_done;
static char err[256];
static const romfs_io *io;

// ---------------------------------------------------------------- CRC-32

uint32_t romfs_crc32(uint32_t crc, const void *data, size_t n)
{
    static const uint32_t t[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

// ---------------------------------------------------------------- stdio

static void *sio_open(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
#if defined(__EMSCRIPTEN__)
    // the page's server: roms/... next to the .html (each URL tried once)
    if (!f && strncmp(path, "roms/", 5) == 0) {
        static char tried[64][96];
        static int ntried;
        for (int i = 0; i < ntried; i++)
            if (strcmp(tried[i], path) == 0) return NULL;
        if (ntried < 64) snprintf(tried[ntried++], sizeof tried[0], "%s", path);
        char dir[PATH_MAX_];
        snprintf(dir, sizeof dir, "%s", path);
        for (char *s = dir + 1; *s; s++)
            if (*s == '/') { *s = 0; mkdir(dir, 0777); *s = '/'; }
        emscripten_wget(path, path);       // (ASYNCIFY: waits for it; a missing URL leaves no file)
        f = fopen(path, "rb");
    }
#endif
    if (!f) return NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    *size = (uint32_t)n;
    return f;
}

static bool sio_read(void *h, uint32_t off, void *dst, uint32_t n)
{
    FILE *f = (FILE *)h;
    return fseek(f, (long)off, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
}

static void sio_close(void *h) { fclose((FILE *)h); }

static bool sio_list(const char *dir, void (*fn)(void *user, const char *name), void *user)
{
#ifdef ROMFS_HAVE_DIRENT
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.') fn(user, e->d_name);
    closedir(d);
    return true;
#else
    (void)dir; (void)fn; (void)user;
    return false;
#endif
}

static const romfs_io stdio_io = { sio_open, sio_read, sio_close, sio_list };

const romfs_io *romfs_stdio(void) { return &stdio_io; }
void romfs_set_io(const romfs_io *i) { io = i; romfs_release(); }
static const romfs_io *the_io(void) { return io ? io : &stdio_io; }

// ---------------------------------------------------------------- directories

void romfs_add_dir(const char *dir)
{
    if (!dir) return;
    char d[PATH_MAX_];
    snprintf(d, sizeof d, "%s", dir);
    size_t n = strlen(d);
    while (n > 1 && (d[n - 1] == '/' || d[n - 1] == '\\')) d[--n] = 0;
    for (int i = 0; i < ndirs; i++)
        if (strcmp(dirs[i], d) == 0) return;
    if (ndirs < NDIRS) snprintf(dirs[ndirs++], PATH_MAX_, "%s", d);
}

void romfs_clear_dirs(void) { ndirs = 0; defaults_done = false; }
int romfs_dir_count(void) { return ndirs; }
const char *romfs_dir(int i) { return i >= 0 && i < ndirs ? dirs[i] : NULL; }

void romfs_default_dirs(const char *argv0)
{
    if (defaults_done) return;
    defaults_done = true;
    const char *env = getenv("CMESYS_ROMS");
    if (env && *env) {
        char buf[PATH_MAX_ * 2];
        snprintf(buf, sizeof buf, "%s", env);
        char *s = buf;
        while (s && *s) {
            char *sep = s;
            // ';' always separates; ':' too, except in "C:\..."
            while (*sep && *sep != ';' && !(*sep == ':' && !(sep == s + 1 && isalpha((unsigned char)*s)))) sep++;
            char c = *sep;
            *sep = 0;
            if (*s) romfs_add_dir(s);
            s = c ? sep + 1 : NULL;
        }
    }
    romfs_add_dir("roms");
    if (argv0) {
        const char *a = strrchr(argv0, '/'), *b = strrchr(argv0, '\\');
        if (b && (!a || b > a)) a = b;
        if (a) {
            char d[PATH_MAX_];
            snprintf(d, sizeof d, "%.*s/roms", (int)(a - argv0), argv0);
            romfs_add_dir(d);
            snprintf(d, sizeof d, "%.*s/../roms", (int)(a - argv0), argv0);
            romfs_add_dir(d);
        }
    }
}

const char *romfs_error(void) { return err; }

void romfs_missing(const char *const *sets, const char *what)
{
    snprintf(err, sizeof err, "%s: missing %s", sets && sets[0] ? sets[0] : "", what);
}

static void defaults(void);

// Appends `text` to buf, wrapped at `cols` characters.
static void add_wrapped(char *buf, size_t n, int cols, const char *text)
{
    size_t at = strlen(buf);
    int col = 0;
    for (const char *p = text; *p && at + 2 < n; p++) {
        if (*p == '\n') col = -1;
        else if (col >= cols) { buf[at++] = '\n'; col = 0; }
        buf[at++] = *p;
        col++;
    }
    buf[at] = 0;
}

char *romfs_describe(char *buf, size_t n, int cols)
{
    if (!n) return buf;
    buf[0] = 0;
    if (cols < 16) cols = 16;
    add_wrapped(buf, n, cols, "ROMS NOT FOUND\n\n");
    add_wrapped(buf, n, cols, err[0] ? err : "(unknown)");
    add_wrapped(buf, n, cols, "\n\nLooked for <set>/<file>, <set>.zip\nand <set>/*.zip in:");
    defaults();
    for (int i = 0; i < ndirs; i++) {
        add_wrapped(buf, n, cols, "\n ");
        add_wrapped(buf, n, cols, dirs[i][0] ? dirs[i] : "(root)");
        add_wrapped(buf, n, cols, "/");
    }
    return buf;
}

// ---------------------------------------------------------------- .zip

typedef struct {
    char *name;                 // base name
    uint32_t crc, csize, usize, local;
    uint16_t method;
} zent;

static struct {
    char path[PATH_MAX_];
    zent *e;
    int n;
    bool valid;                 // (false: not a zip / absent)
} zc;

void romfs_release(void)
{
    for (int i = 0; i < zc.n; i++) free(zc.e[i].name);
    free(zc.e);
    memset(&zc, 0, sizeof zc);
}

static uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static bool zip_dir(const char *path)
{
    if (zc.path[0] && strcmp(zc.path, path) == 0) return zc.valid;
    romfs_release();
    snprintf(zc.path, sizeof zc.path, "%s", path);
    const romfs_io *f = the_io();
    uint32_t size = 0;
    void *h = f->open(path, &size);
    if (!h) return false;
    bool ok = false;
    uint32_t tail = size < 65557 ? size : 65557;
    uint8_t *t = tail >= 22 ? (uint8_t *)malloc(tail) : NULL;
    uint8_t *cd = NULL;
    if (t && f->read(h, size - tail, t, tail)) {
        for (int i = (int)tail - 22; i >= 0; i--) {
            if (rd32(t + i) != 0x06054b50) continue;
            uint32_t count = rd16(t + i + 10), cd_size = rd32(t + i + 12), cd_off = rd32(t + i + 16);
            if (cd_off > size || cd_size > size - cd_off) break;
            cd = (uint8_t *)malloc(cd_size ? cd_size : 1);
            zc.e = (zent *)calloc(count ? count : 1, sizeof *zc.e);
            if (!cd || !zc.e || !f->read(h, cd_off, cd, cd_size)) break;
            uint32_t at = 0;
            for (uint32_t k = 0; k < count && at + 46 <= cd_size; k++) {
                const uint8_t *e = cd + at;
                if (rd32(e) != 0x02014b50) break;
                uint32_t nl = rd16(e + 28), xl = rd16(e + 30), cl = rd16(e + 32);
                if (at + 46 + nl > cd_size) break;
                const char *nm = (const char *)e + 46;
                uint32_t b = 0;
                for (uint32_t j = 0; j < nl; j++)
                    if (nm[j] == '/' || nm[j] == '\\') b = j + 1;
                zent *z = &zc.e[zc.n];
                z->name = (char *)malloc(nl - b + 1);
                if (!z->name) break;
                memcpy(z->name, nm + b, nl - b);
                z->name[nl - b] = 0;
                z->method = (uint16_t)rd16(e + 10);
                z->crc = rd32(e + 16);
                z->csize = rd32(e + 20);
                z->usize = rd32(e + 24);
                z->local = rd32(e + 42);
                zc.n++;
                at += 46 + nl + xl + cl;
            }
            ok = true;
            break;
        }
    }
    free(t);
    free(cd);
    f->close(h);
    zc.valid = ok;
    return ok;
}

static bool same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return !*a && !*b;
}

static uint8_t *zip_read(const zent *z, uint32_t *len)
{
    const romfs_io *f = the_io();
    uint32_t size = 0;
    void *h = f->open(zc.path, &size);
    if (!h) return NULL;
    uint8_t lh[30], *comp = NULL, *out = NULL;
    if (f->read(h, z->local, lh, 30) && rd32(lh) == 0x04034b50) {
        uint32_t data = z->local + 30 + rd16(lh + 26) + rd16(lh + 28);
        comp = (uint8_t *)malloc(z->csize ? z->csize : 1);
        if (comp && data <= size && z->csize <= size - data && f->read(h, data, comp, z->csize)) {
            if (z->method == 0 && z->csize == z->usize) {
                out = comp;
                comp = NULL;
            } else if (z->method == 8) {
                size_t n = 0;
                out = zx_inflate_raw(comp, z->csize, z->usize, (size_t)z->usize + 1, &n);
                if (out && n != z->usize) { free(out); out = NULL; }
            }
        }
    }
    free(comp);
    f->close(h);
    if (out && romfs_crc32(0, out, z->usize) != z->crc) {
        fprintf(stderr, "romfs: %s in %s: bad CRC (damaged .zip?)\n", z->name, zc.path);
        free(out);
        out = NULL;
    }
    if (out) *len = z->usize;
    return out;
}

// An entry of the cached .zip: by CRC first, then by name.
static uint8_t *zip_find(const char *name, uint32_t crc, uint32_t *len)
{
    if (crc)
        for (int i = 0; i < zc.n; i++)
            if (zc.e[i].crc == crc) return zip_read(&zc.e[i], len);
    for (int i = 0; i < zc.n; i++)
        if (same_name(zc.e[i].name, name)) {
            if (crc) fprintf(stderr, "romfs: %s in %s has CRC %08x, expected %08x\n", name, zc.path,
                            (unsigned)zc.e[i].crc, (unsigned)crc);
            return zip_read(&zc.e[i], len);
        }
    return NULL;
}

// ---------------------------------------------------------------- search

static uint8_t *read_file(const char *path, uint32_t *len)
{
    const romfs_io *f = the_io();
    uint32_t size = 0;
    void *h = f->open(path, &size);
    if (!h) return NULL;
    uint8_t *buf = (uint8_t *)malloc(size ? size : 1);
    if (buf && !f->read(h, 0, buf, size)) { free(buf); buf = NULL; }
    f->close(h);
    if (buf) *len = size;
    return buf;
}

static void join(char *out, const char *dir, const char *set, const char *name)
{
    if (!dir[0]) snprintf(out, PATH_MAX_, "%s%s%s", set, set[0] ? "/" : "", name);
    else snprintf(out, PATH_MAX_, "%s/%s%s%s", dir, set, set[0] ? "/" : "", name);
}

typedef struct { char names[ZIPS_IN_DIR][96]; int n; } zlist;

static void add_zip(void *user, const char *name)
{
    zlist *z = (zlist *)user;
    size_t l = strlen(name);
    if (z->n < ZIPS_IN_DIR && l > 4 && l < sizeof z->names[0] && same_name(name + l - 4, ".zip"))
        snprintf(z->names[z->n++], sizeof z->names[0], "%s", name);
}

// One file, any size: unzipped, in S.zip, in a .zip in S/.
static uint8_t *find_one(const char *dir, const char *set, const char *name, uint32_t crc, uint32_t *len)
{
    char path[PATH_MAX_];
    // unzipped: the name as it is, then lower / upper case
    for (int v = 0; v < 3; v++) {
        char nm[128];
        size_t k = 0;
        for (; name[k] && k < sizeof nm - 1; k++)
            nm[k] = v == 0 ? name[k] : v == 1 ? (char)tolower((unsigned char)name[k]) : (char)toupper((unsigned char)name[k]);
        nm[k] = 0;
        if (v && strcmp(nm, name) == 0) continue;
#if defined(__EMSCRIPTEN__)
        if (v) {                            // (no case variants over HTTP)
            join(path, dir, set, nm);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            fclose(f);
        }
#endif
        join(path, dir, set, nm);
        uint8_t *b = read_file(path, len);
        if (b) {
            if (crc && romfs_crc32(0, b, *len) != crc)
                fprintf(stderr, "romfs: %s has CRC %08x, expected %08x\n", path, (unsigned)romfs_crc32(0, b, *len), (unsigned)crc);
            return b;
        }
    }
    // the set's .zip
    char zname[PATH_MAX_];
    snprintf(zname, sizeof zname, "%s.zip", set);
    join(path, dir, "", zname);
    if (set[0] && zip_dir(path)) {
        uint8_t *b = zip_find(name, crc, len);
        if (b) return b;
    }
    // a .zip in the set's folder
    static zlist z;                     // (static: the boards' stacks are small)
    z.n = 0;
    join(path, dir, set, "");
    if (path[0] && path[strlen(path) - 1] == '/') path[strlen(path) - 1] = 0;
    if (the_io()->list && the_io()->list(path, add_zip, &z)) {
        for (int i = 0; i < z.n; i++) {
            char zp[PATH_MAX_];
            join(zp, dir, set, z.names[i]);
            if (!zip_dir(zp)) continue;
            uint8_t *b = zip_find(name, crc, len);
            if (b) return b;
        }
    }
    return NULL;
}

#ifdef ROMFS_SD
#include "romfs_sd.h"
#endif

static void defaults(void)
{
#ifdef ROMFS_SD
    if (!io) romfs_use_sd();            // (the boards: the microSD)
#endif
    if (!ndirs) romfs_default_dirs(NULL);
}

static uint8_t *search(const char *const *sets, const char *name, uint32_t crc, uint32_t size, uint32_t *len,
                       bool *wrong_size)
{
    defaults();
    for (int d = 0; d < ndirs; d++)
        for (int s = 0; sets && sets[s]; s++) {
            uint32_t n = 0;
            uint8_t *b = find_one(dirs[d], sets[s], name, crc, &n);
            if (!b) continue;
            if (size && n != size) {
                fprintf(stderr, "romfs: %s/%s/%s is %u bytes, expected %u\n", dirs[d], sets[s], name, (unsigned)n,
                       (unsigned)size);
                free(b);
                *wrong_size = true;
                continue;
            }
            *len = n;
            return b;
        }
    return NULL;
}

static void set_missing(const char *const *sets, const char *name, uint32_t size, bool wrong_size)
{
    const char *set = sets && sets[0] ? sets[0] : "";
    if (size)
        snprintf(err, sizeof err, "%s: %s %s (%u bytes)", set, wrong_size ? "wrong size" : "missing", name,
                 (unsigned)size);
    else
        snprintf(err, sizeof err, "%s: missing %s", set, name);
}

uint8_t *romfs_get(const char *const *sets, const char *name, uint32_t crc, uint32_t size, uint32_t *len)
{
    uint32_t n = 0;
    bool wrong = false;
    uint8_t *b = search(sets, name, crc, size, &n, &wrong);
    if (!b) {
        set_missing(sets, name, size, wrong);
        return NULL;
    }
    if (len) *len = n;
    return b;
}

bool romfs_get_into(const char *const *sets, const char *name, uint32_t crc, uint8_t *dst, uint32_t size)
{
    uint8_t *b = romfs_get(sets, name, crc, size, NULL);
    if (!b) return false;
    memcpy(dst, b, size);
    free(b);
    return true;
}

uint8_t *romfs_get_any(const char *const *sets, const romfs_cand *c, uint32_t *len, const char **found)
{
    const romfs_cand *c0 = c;
    for (; c && c->name; c++) {
        if (!strchr(c->name, '+')) {
            uint32_t n = 0;
            bool wrong = false;
            uint8_t *b = search(sets, c->name, c->crc, 0, &n, &wrong);
            if (b) {
                if (len) *len = n;
                if (found) *found = c->name;
                return b;
            }
            continue;
        }
        // "a+b": the parts joined
        uint8_t *all = NULL;
        uint32_t total = 0;
        bool ok = true;
        const char *p = c->name;
        while (ok && *p) {
            const char *q = strchr(p, '+');
            char part[128];
            snprintf(part, sizeof part, "%.*s", (int)(q ? q - p : (long)strlen(p)), p);
            uint32_t n = 0;
            bool wrong = false;
            uint8_t *b = search(sets, part, 0, 0, &n, &wrong);
            uint8_t *grown = b ? (uint8_t *)realloc(all, total + n) : NULL;
            if (!grown) {
                ok = false;
            } else {
                all = grown;
                memcpy(all + total, b, n);
                total += n;
            }
            free(b);
            p = q ? q + 1 : p + strlen(p);
        }
        if (ok && all) {
            if (len) *len = total;
            if (found) *found = c->name;
            return all;
        }
        free(all);
    }
    if (c0 && c0->name) romfs_missing(sets, c0->name);
    return NULL;
}

bool romfs_fill(const char *const *sets, const romfs_entry *e, int n)
{
    bool ok = true;
    char first[sizeof err] = "";
    err[0] = 0;
    for (int i = 0; i < n; i++) {
        if (*e[i].ptr) continue;                 // compiled in
        uint32_t len = 0;
        const char *found = NULL;
        uint8_t *b = romfs_get_any(sets, e[i].cands, &len, &found);
        if (b && len != e[i].size) {
            fprintf(stderr, "romfs: %s is %u bytes (expected %u)\n", found, (unsigned)len, (unsigned)e[i].size);
            uint8_t *r = (uint8_t *)realloc(b, e[i].size);
            if (!r) { free(b); b = NULL; }
            else {
                if (len < e[i].size) memset(r + len, 0xff, e[i].size - len);
                b = r;
            }
        }
        if (b) {
            fprintf(stderr, "romfs: %s <- %s\n", sets && sets[0] ? sets[0] : "", found);
            *e[i].ptr = b;
            continue;
        }
        if (e[i].required && ok) {
            ok = false;
            const romfs_cand *c = e[i].cands;
            snprintf(first, sizeof first, "%s: missing %s (%u bytes)", sets && sets[0] ? sets[0] : "",
                     c && c->name ? c->name : "?", (unsigned)e[i].size);
        }
    }
    romfs_release();
    snprintf(err, sizeof err, "%s", ok ? "" : first);     // (the first required one missing)
    return ok;
}
