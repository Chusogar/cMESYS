// heap.c - RP2350-PiZero: newlib's heap in the internal RAM left after the
// program, then in the 8 MB PSRAM once pico_port_open() has set it up (from
// dsp-cpp's port). The PSRAM is slower: the big allocations a game makes
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

size_t dsp_heap_psram_used(void) { return (size_t)(psram_next - psram_base); }

void pico_heap_sram_only(bool on) { sram_only = on; }

// SRAM not yet given to the heap (what newlib has freed inside it is not counted)
size_t pico_heap_sram_free(void)
{
    if (!sram_next) sram_next = &end;
    return (size_t)(&__StackLimit - sram_next);
}

void *_sbrk(int incr)
{
    if (!sram_next) sram_next = &end;
    if (incr < 0) {             // newlib trims the top: only give back the area in use
        if (psram_next != psram_base && psram_next + incr >= psram_base) {
            psram_next += incr;
            return (void *)(psram_next - incr);
        }
        if (sram_next + incr >= &end) {
            sram_next += incr;
            return sram_next - incr;
        }
        errno = ENOMEM;
        return (void *)-1;
    }
    // SRAM until it is full (and before the PSRAM exists)
    if (psram_next == psram_base && sram_next + incr <= &__StackLimit) {
        char *prev = sram_next;
        sram_next += incr;
        return prev;
    }
    if (!sram_only && psram_base && psram_next + (uintptr_t)incr <= psram_end) {
        const uintptr_t prev = psram_next;
        psram_next += (uintptr_t)incr;
        return (void *)prev;
    }
    errno = ENOMEM;
    return (void *)-1;
}
