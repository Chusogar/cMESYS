// heap.c - RP2350-PiZero: newlib's heap in the internal RAM left after the
// program, then a second heap in the 8 MB PSRAM once pico_port_open() has
// set it up. The PSRAM is slower: the big allocations a game makes
// before that stay in SRAM, and pico_heap_sram_only() keeps the frame
// buffers there too.
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "psram.h"

extern char end;           // linker: first free byte after .bss
extern char __StackLimit;  // linker: bottom of the stack area

static char *sram_next;
static uintptr_t psram_base, psram_next, psram_end;
static bool sram_only;

void dsp_heap_add_psram(uintptr_t base, size_t size)
{
    psram_base = psram_next = base;
    psram_end = base + size;
}

size_t dsp_heap_psram_used(void) { return (size_t)(psram_next - psram_base); }   // (high-water mark)

void pico_heap_sram_only(bool on) { sram_only = on; }

// SRAM not yet given to the heap (what newlib has freed inside it is not counted)
size_t pico_heap_sram_free(void)
{
    if (!sram_next) sram_next = &end;
    return (size_t)(&__StackLimit - sram_next);
}

// newlib's heap is the SRAM alone: its malloc gives up when sbrk returns
// memory below the heap it already has ("a foreign sbrk"), and the PSRAM
// (0x11000000) is below the SRAM (0x20000000). So a 640 KB TR-DOS disk got
// "out of memory" with 8 MB of PSRAM free. The PSRAM is a second heap
// (below), used when the SRAM's is full.
void *_sbrk(int incr)
{
    if (!sram_next) sram_next = &end;
    if (incr < 0) {             // newlib trims the top
        if (sram_next + incr >= &end) {
            sram_next += incr;
            return sram_next - incr;
        }
        errno = ENOMEM;
        return (void *)-1;
    }
    if (sram_next + incr <= &__StackLimit) {
        char *prev = sram_next;
        sram_next += incr;
        return prev;
    }
    errno = ENOMEM;
    return (void *)-1;
}

// ---------------------------------------------------------------------------
// The PSRAM heap: blocks with boundary tags (an 8-byte header: its size with
// bit 0 set when in use, and the previous block's size), next fit, merged
// with free neighbours when freed. Called with pico_malloc's mutex held.
// ---------------------------------------------------------------------------
typedef struct { uint32_t size, prev; } blk;
#define USED 1u
#define BSIZE(b) ((b)->size & ~USED)
static blk *ps_first, *ps_end, *ps_rover;

static inline blk *next_blk(blk *b) { return (blk *)((char *)b + BSIZE(b)); }

static void ps_init(void)
{
    uintptr_t base = (psram_base + 7) & ~(uintptr_t)7, top = psram_end & ~(uintptr_t)7;
    ps_first = (blk *)base;
    ps_end = (blk *)(top - sizeof(blk));        // a used block of size 0 ends the heap
    ps_first->size = (uint32_t)((uintptr_t)ps_end - base);
    ps_first->prev = 0;
    ps_end->size = USED;
    ps_end->prev = ps_first->size;
    ps_rover = ps_first;
}

static void *ps_alloc(size_t n)
{
    if (!psram_base || n > psram_end - psram_base) return NULL;
    if (!ps_first) ps_init();
    uint32_t need = (uint32_t)((n + 7) & ~(size_t)7) + sizeof(blk);
    if (need < 2 * sizeof(blk)) need = 2 * sizeof(blk);
    blk *b = ps_rover;
    do {
        if (!(b->size & USED) && b->size >= need) {
            const uint32_t left = b->size - need;
            if (left >= 2 * sizeof(blk)) {      // split: the rest stays free
                blk *r = (blk *)((char *)b + need);
                r->size = left;
                r->prev = need;
                next_blk(r)->prev = left;
                b->size = need;
            }
            b->size |= USED;
            ps_rover = next_blk(b);
            if (ps_rover == ps_end) ps_rover = ps_first;
            psram_next = (uintptr_t)next_blk(b) > psram_next ? (uintptr_t)next_blk(b) : psram_next;
            return b + 1;
        }
        b = next_blk(b);
        if (b == ps_end) b = ps_first;
    } while (b != ps_rover);
    return NULL;
}

static void ps_free(void *p)
{
    blk *b = (blk *)p - 1;
    b->size &= ~USED;
    blk *n = next_blk(b);
    if (!(n->size & USED)) {                    // merge the next one
        if (ps_rover == n) ps_rover = b;
        b->size += n->size;
        next_blk(b)->prev = b->size;
    }
    if (b != ps_first) {                        // and the previous one
        blk *pv = (blk *)((char *)b - b->prev);
        if (!(pv->size & USED)) {
            if (ps_rover == b) ps_rover = pv;
            pv->size += b->size;
            next_blk(pv)->prev = pv->size;
        }
    }
}

static inline bool in_psram(const void *p)
{
    return psram_base && (uintptr_t)p >= psram_base && (uintptr_t)p < psram_end;
}

static inline size_t ps_usable(const void *p) { return BSIZE((const blk *)p - 1) - sizeof(blk); }

// malloc & co. (what pico_malloc's wrappers call): the SRAM first, then the PSRAM
#include <reent.h>
#include <string.h>
extern void *_malloc_r(struct _reent *, size_t);
extern void _free_r(struct _reent *, void *);
extern void *_realloc_r(struct _reent *, void *, size_t);
extern size_t _malloc_usable_size_r(struct _reent *, void *);

void *malloc(size_t n)
{
    void *p = _malloc_r(_REENT, n);
    if (!p && !sram_only) p = ps_alloc(n);
    return p;
}

void free(void *p)
{
    if (!p) return;
    if (in_psram(p)) ps_free(p);
    else _free_r(_REENT, p);
}

void *calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size) return NULL;
    void *p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) {
        free(p);
        return NULL;
    }
    if (!in_psram(p)) {
        void *q = _realloc_r(_REENT, p, n);
        if (q) return q;
    }
    const size_t old = in_psram(p) ? ps_usable(p) : _malloc_usable_size_r(_REENT, p);
    if (in_psram(p) && old >= n) return p;
    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, old < n ? old : n);
    free(p);
    return q;
}
