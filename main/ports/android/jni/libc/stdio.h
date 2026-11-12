#ifndef ANDROID_SHIM_STDIO_H
#define ANDROID_SHIM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct __sFILE FILE;
extern FILE *stdin, *stdout, *stderr;   /* (bionic: variables since API 23) */
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *s, const char *fmt, ...);
int snprintf(char *s, size_t n, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int puts(const char *s);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int fflush(FILE *f);
#define EOF (-1)
#endif
