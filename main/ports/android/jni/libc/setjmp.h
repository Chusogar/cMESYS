#ifndef ANDROID_SHIM_SETJMP_H
#define ANDROID_SHIM_SETJMP_H
/* bionic: long[32] on arm64, long[12] on x86_64; this is at least as big */
typedef long jmp_buf[32];
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
#endif
