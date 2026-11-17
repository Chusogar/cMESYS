#ifndef ANDROID_SHIM_DIRENT_H
#define ANDROID_SHIM_DIRENT_H
#include <stdint.h>
/* bionic's (LP64: arm64-v8a, x86_64) */
struct dirent {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};
typedef struct DIR DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#endif
