#ifndef ANDROID_SHIM_TIME_H
#define ANDROID_SHIM_TIME_H
typedef long time_t;
// bionic's layout (the real libc fills it)
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
time_t time(time_t *t);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
#endif
