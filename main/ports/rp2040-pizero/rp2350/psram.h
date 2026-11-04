#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PSRAM is mapped (cached, writable) here once dsp_psram_setup() succeeds.
#define DSP_PSRAM_BASE 0x11000000u

// Detects and configures the PSRAM on `cs_pin`; returns its size (0: none).
// Call after the system clock is set (or call dsp_psram_update_timing()).
size_t dsp_psram_setup(uint32_t cs_pin);
void dsp_psram_update_timing(void);

// The heap (heap.c) grows into PSRAM once the internal RAM is used up.
void dsp_heap_add_psram(uintptr_t base, size_t size);
size_t dsp_heap_psram_used(void);

#ifdef __cplusplus
}
#endif
