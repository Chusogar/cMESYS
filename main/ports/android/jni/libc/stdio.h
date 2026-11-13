#ifndef ANDROID_SHIM_STDIO_H
#define ANDROID_SHIM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct __sFILE FILE;
/* stdin / stdout / stderr: bionic has them as variables only since API 23
   (before, macros on __sF), so here they are null markers and what is
   written to them goes nowhere (jni/libc_extra.c). */
extern FILE *const cmesys_std[3];
#define stdin (cmesys_std[0])
#define stdout (cmesys_std[1])
#define stderr (cmesys_std[2])
#define printf cmesys_printf
#define puts cmesys_puts
#define putchar cmesys_putchar
#define fprintf cmesys_fprintf
#define vfprintf cmesys_vfprintf
#define fputs cmesys_fputs
#define fputc cmesys_fputc
#define fflush cmesys_fflush
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
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
int putchar(int c);
int fgetc(FILE *f);
char *fgets(char *s, int n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int feof(FILE *f);
int remove(const char *path);
int rename(const char *from, const char *to);
int sscanf(const char *s, const char *fmt, ...);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
#endif
