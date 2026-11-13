// libc_extra.c - what the minimal libc headers (jni/libc) route here: the
// standard streams are null markers (bionic has stdin / stdout / stderr as
// variables only since API 23), so what the cores print to them is dropped
// and writes to real files go to bionic.
#include <stdarg.h>
#include <stddef.h>

typedef struct __sFILE FILE;
FILE *const cmesys_std[3] = {NULL, NULL, NULL};

// bionic's own
extern int vfprintf(FILE *f, const char *fmt, va_list ap);
extern int fputs(const char *s, FILE *f);
extern int fputc(int c, FILE *f);
extern int fflush(FILE *f);

int cmesys_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    return f ? vfprintf(f, fmt, ap) : 0;
}

int cmesys_fprintf(FILE *f, const char *fmt, ...)
{
    if (!f) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int cmesys_printf(const char *fmt, ...) { (void)fmt; return 0; }
int cmesys_puts(const char *s) { (void)s; return 0; }
int cmesys_putchar(int c) { return c; }
int cmesys_fputs(const char *s, FILE *f) { return f ? fputs(s, f) : 0; }
int cmesys_fputc(int c, FILE *f) { return f ? fputc(c, f) : c; }
int cmesys_fflush(FILE *f) { return f ? fflush(f) : 0; }
