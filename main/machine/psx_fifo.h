// psx_fifo.h - a growable FIFO (std::deque's push_back / front / pop_front
// as dsp-cpp's PlayStation chips use it), one per element type.
#ifndef PSX_FIFO_H
#define PSX_FIFO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PSX_FIFO_DEFINE(NAME, T)                                                         \
    typedef struct { T *d; size_t head, tail, cap; } NAME;                               \
    static inline void NAME##_clear(NAME *f) { f->head = f->tail = 0; }                 \
    static inline bool NAME##_empty(const NAME *f) { return f->head == f->tail; }       \
    static inline size_t NAME##_size(const NAME *f) { return f->tail - f->head; }       \
    static inline T NAME##_front(const NAME *f) { return f->d[f->head]; }               \
    static inline T *NAME##_frontp(NAME *f) { return &f->d[f->head]; }                  \
    static inline T NAME##_at(const NAME *f, size_t i) { return f->d[f->head + i]; }    \
    static inline void NAME##_pop_front(NAME *f)                                         \
    {                                                                                    \
        if (++f->head == f->tail) f->head = f->tail = 0;                                 \
    }                                                                                    \
    static inline void NAME##_push_back(NAME *f, T v)                                    \
    {                                                                                    \
        if (f->tail == f->cap) {                                                         \
            if (f->head > 0) {                                                           \
                memmove(f->d, f->d + f->head, (f->tail - f->head) * sizeof(T));          \
                f->tail -= f->head;                                                      \
                f->head = 0;                                                             \
            }                                                                            \
            if (f->tail == f->cap) {                                                     \
                size_t nc = f->cap ? f->cap * 2 : 64;                                    \
                T *nd = (T *)realloc(f->d, nc * sizeof(T));                              \
                if (!nd) return;                                                         \
                f->d = nd;                                                               \
                f->cap = nc;                                                             \
            }                                                                            \
        }                                                                                \
        f->d[f->tail++] = v;                                                             \
    }

PSX_FIFO_DEFINE(fifo8, uint8_t)
PSX_FIFO_DEFINE(fifo16, uint16_t)
PSX_FIFO_DEFINE(fifos16, int16_t)
#endif
