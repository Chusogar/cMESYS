#pragma once
#include <stdint.h>
#include <stdbool.h>

// Start 8-bit mono output on GPIO26 (built-in DAC, async DMA). Returns false if
// the DAC could not be started (push() then only paces by timer).
bool dac_audio_start(uint32_t rate);
// Queue n unsigned 8-bit samples; blocks while the ring buffer is full.
void dac_audio_push(const uint8_t *s, int n);
uint32_t dac_audio_queued(void);
bool dac_audio_ok(void);
