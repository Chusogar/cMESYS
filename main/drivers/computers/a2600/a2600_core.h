// a2600_core.h - Atari 2600 / VCS (1977): a C11 migration of dsp-cpp's
// drivers/consoles/a2600 with its video/tia (main/video/tia) and
// machine/mos6532 (main/machine/mos6532). The 6507 (cpu/m6502/m65xx, NMOS)
// at 1.19 MHz, the TIA (picture and the two sound channels), the RIOT (128
// bytes of RAM, the switches, the timer). No BIOS: the cartridge maps into
// $1000-$1FFF, with the usual bank-switching schemes (Atari F8 / F6 / F4 and
// their Superchip RAM, Parker Bros E0, M Network E7, Activision FE,
// Tigervision 3F, Megaboy F0), found as Stella's CartDetector does.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

#define A2600_W          160
#define A2600_H          192
#define A2600_CPU_CLOCK  (3579545 / 3)
#define A2600_FPS        ((double)A2600_CPU_CLOCK / 76 / 262)
#define A2600_AUDIO_MAX  2048
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define A2600_AUDIO_RATE 22050
#else
#define A2600_AUDIO_RATE 44100
#endif
#define A2600_MAX_CART   (64 * 1024 + 256)

// Joystick bits (a2600_input: player 1 in bits 0-4, player 2 in 8-12) and
// the console's buttons and switches
enum {
    A2600_UP = 0x01, A2600_DOWN = 0x02, A2600_LEFT = 0x04, A2600_RIGHT = 0x08, A2600_FIRE = 0x10,
    A2600_P2 = 8,                                   // shift for player 2
    A2600_RESET = 0x10000, A2600_SELECT = 0x20000,  // the console's buttons
};
// a2600_set_switches: SWCHB's bits as dsp-cpp's dip switch (default 0x08)
enum { A2600_SW_COLOUR = 0x08, A2600_SW_DIFF_LEFT_A = 0x40, A2600_SW_DIFF_RIGHT_A = 0x80 };

#define A2600_MEDIA_EXTS { ".a26", ".bin", ".rom", NULL }
#define A2600_MEDIA_LABEL "A26 BIN ROM"

typedef struct a2600 a2600;

a2600 *a2600_create(int audio_rate);
void a2600_destroy(a2600 *m);
void a2600_reset(a2600 *m);
// One frame (up to the next VSYNC); *audio gets its samples
int a2600_run_frame(a2600 *m, const int16_t **audio);
void a2600_input(a2600 *m, unsigned bits);
void a2600_set_switches(a2600 *m, uint8_t switches);
uint8_t a2600_switches(const a2600 *m);

// The picture of the last frame, row y of A2600_W pixels
void a2600_render_row(const a2600 *m, int y, uint32_t *out);
void a2600_render_row565(const a2600 *m, int y, uint16_t *out);
void a2600_set_rgb565(a2600 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));

// A cartridge (the name: a "f8sc"... in it says Superchip). The pbuf is freed.
bool a2600_load_pbuf(a2600 *m, zx_pbuf *pb, const char *name);
bool a2600_load_mem(a2600 *m, const uint8_t *data, size_t size, const char *name);
bool a2600_load_file(a2600 *m, const char *path);
bool a2600_has_cart(const a2600 *m);
void a2600_eject(a2600 *m);
const char *a2600_last_error(const a2600 *m);
const char *a2600_mapper_name(const a2600 *m);
// TURBO: no sound (the frames give no samples)
void a2600_set_turbo(a2600 *m, bool on);
