// lynx_core.h - Atari Lynx: a C11 migration of dsp-cpp's drivers/consoles/
// atari_lynx with its machine/lynx_mikey and lynx_suzy. The 65C02 in Mikey
// (cpu/m6502/m65xx, CMOS) at an effective 4 MHz, 64 KB of RAM, Suzy (the
// sprite engine: packed / RLE, stretch, tilt, flip, collisions; the maths
// unit; joystick, switches and the cartridge port), Mikey (eight timers, four
// audio channels, the palette, the LCD DMA, the cartridge's address shift
// register, the ComLynx UART looped back), MAPCTL and the 512-byte boot ROM
// (lynxboot.img; without it dsp-cpp's own small loader). Cartridges: .lnx
// (header) and headerless .lyx / .o (BS93 quickload).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

#define LYNX_W          160
#define LYNX_H          102
#define LYNX_FPS        (4000000.0 / 636 / 105)
#define LYNX_AUDIO_MAX  1024
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define LYNX_AUDIO_RATE 22050
#else
#define LYNX_AUDIO_RATE 44100
#endif

/* Buttons (lynx_buttons) */
enum {
    LYNX_A = 0x01, LYNX_B = 0x02, LYNX_OPT2 = 0x04, LYNX_OPT1 = 0x08,
    LYNX_RIGHT = 0x10, LYNX_LEFT = 0x20, LYNX_DOWN = 0x40, LYNX_UP = 0x80,
    LYNX_PAUSE = 0x100,
};

#define LYNX_MEDIA_EXTS { ".lnx", ".lyx", ".o", NULL }
#define LYNX_MEDIA_LABEL "LNX LYX O"

typedef struct lynx lynx;

lynx *lynx_create(int audio_rate);
void lynx_destroy(lynx *m);
void lynx_set_boot_rom(lynx *m, const uint8_t *rom512);     /* NULL: the built-in loader */
void lynx_reset(lynx *m);
int lynx_run_frame(lynx *m, const int16_t **audio);
void lynx_buttons(lynx *m, unsigned bits);

/* The picture of the last frame, row y of LYNX_W pixels */
void lynx_render_row(const lynx *m, int y, uint32_t *out);
void lynx_render_row565(const lynx *m, int y, uint16_t *out);
/* The RGB565 colours in a panel's own format (the CYD's byte order / BGR) */
void lynx_set_rgb565(lynx *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));

bool lynx_load_pbuf(lynx *m, zx_pbuf *pb, const char *name);  /* takes the pages over */
bool lynx_load_file(lynx *m, const char *path);
const char *lynx_last_error(const lynx *m);
bool lynx_has_boot_rom(const lynx *m);
/* TURBO: no sound (the frames give no samples) */
void lynx_set_turbo(lynx *m, bool on);
