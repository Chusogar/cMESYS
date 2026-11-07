// hdmi_glue.h - HDMI picture and sound of the RP2350-PiZero (see README.md)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// 640x480 at 60 Hz on GPIO32-39 (PIO1), sound at audio_hz (32000, 44100 or
// 48000; 0: none) in HDMI data islands. line_cb(user) runs after every line,
// in a low priority interrupt (the DMA's raises it) on the core that calls
// pzhdmi_core1_start().
void pzhdmi_init(int audio_hz, void (*line_cb)(void *), void *user);
void pzhdmi_core1_start(void);
// A row of `width` RGB565 pixels (a multiple of 4, 4-byte aligned), pixel
// doubled and centred, for the `count` output lines from `first`; false
// when no TMDS buffer is free.
bool pzhdmi_try_row(int first, int count, const uint16_t *src, int width);
// Sound: stereo samples the ring can still take; one more (dropped if full)
unsigned pzhdmi_audio_free(void);
void pzhdmi_audio_put(int16_t left, int16_t right);
unsigned pzhdmi_missed_lines(void);
// The output line (0-479) the HDMI is setting up, -1 in the blanking
int pzhdmi_active_line(void);

#ifdef __cplusplus
}
#endif
