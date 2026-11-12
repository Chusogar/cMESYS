#ifndef ANDROID_SHIM_STDLIB_H
#define ANDROID_SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
int abs(int x);
long labs(long x);
int atoi(const char *s);
long atol(const char *s);
char *getenv(const char *name);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
#endif
