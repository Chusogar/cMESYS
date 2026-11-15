// atari8_core.h - Atari 400/800 and 800XL / 130XE home computers: a C11
// migration of dsp-cpp's drivers/computers/atari8 with its video/antic,
// video/gtia (main/video) and sound/pokey (main/sound). The 6502 at
// 1.79 MHz (NTSC), ANTIC's display lists, GTIA's colours and players,
// POKEY's sound, keyboard and timers, the PIA's joysticks and (XL / XE)
// memory banking. Disks: .ATR images, served by an SIO patch (the OS's
// SIOV call is answered from the image, as other emulators do).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

enum { A8_MODEL_800 = 0, A8_MODEL_XL = 1, A8_MODEL_XE = 2 };

#define A8_W            384
#define A8_H            240
#define A8_CPU_CLOCK    1789790
#define A8_FPS          ((double)A8_CPU_CLOCK / (262 * 114))
#define A8_AUDIO_MAX    2048
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define A8_AUDIO_RATE   22050
#else
#define A8_AUDIO_RATE   44100
#endif

// Joystick bits (atari8_joy) and the console keys (atari8_console)
enum { A8_UP = 1, A8_DOWN = 2, A8_LEFT = 4, A8_RIGHT = 8, A8_FIRE = 16 };
enum { A8_START = 1, A8_SELECT = 2, A8_OPTION = 4 };

// The keyboard: POKEY's scan codes (KBCODE bits 0-5)
enum {
    A8K_L = 0x00, A8K_J = 0x01, A8K_SEMICOLON = 0x02, A8K_K = 0x05, A8K_PLUS = 0x06, A8K_ASTERISK = 0x07,
    A8K_O = 0x08, A8K_P = 0x0a, A8K_U = 0x0b, A8K_RETURN = 0x0c, A8K_I = 0x0d, A8K_MINUS = 0x0e, A8K_EQUALS = 0x0f,
    A8K_V = 0x10, A8K_C = 0x12, A8K_B = 0x15, A8K_X = 0x16, A8K_Z = 0x17,
    A8K_4 = 0x18, A8K_3 = 0x1a, A8K_6 = 0x1b, A8K_ESC = 0x1c, A8K_5 = 0x1d, A8K_2 = 0x1e, A8K_1 = 0x1f,
    A8K_COMMA = 0x20, A8K_SPACE = 0x21, A8K_PERIOD = 0x22, A8K_N = 0x23, A8K_M = 0x25, A8K_SLASH = 0x26,
    A8K_INVERSE = 0x27,
    A8K_R = 0x28, A8K_E = 0x2a, A8K_Y = 0x2b, A8K_TAB = 0x2c, A8K_T = 0x2d, A8K_W = 0x2e, A8K_Q = 0x2f,
    A8K_9 = 0x30, A8K_0 = 0x32, A8K_7 = 0x33, A8K_BACKSPACE = 0x34, A8K_8 = 0x35, A8K_LESS = 0x36, A8K_GREATER = 0x37,
    A8K_F = 0x38, A8K_H = 0x39, A8K_D = 0x3a, A8K_CAPS = 0x3c, A8K_G = 0x3d, A8K_S = 0x3e, A8K_A = 0x3f,
};

#define A8_MEDIA_EXTS { ".atr", NULL }
#define A8_MEDIA_LABEL "ATR"

typedef struct atari8 atari8;

// The OS ROM: 10 KB for the 800 ($D800-$FFFF: CO12399 + CO12499 + CO14599),
// 16 KB for the XL / XE ($C000-$FFFF); BASIC 8 KB (XL / XE, may be NULL).
// The ROMs are read in place (kept by the caller).
atari8 *atari8_create(int model, int audio_rate, const uint8_t *os, size_t os_size, const uint8_t *basic);
void atari8_destroy(atari8 *m);
void atari8_reset(atari8 *m);
int atari8_model(const atari8 *m);
const char *atari8_title(const atari8 *m);
// One frame; *audio gets its samples
int atari8_run_frame(atari8 *m, const int16_t **audio);

// Input for the next frames: the held key (-1: none) with SHIFT / CONTROL,
// as dsp-cpp's (one key at a time: the first of the keyboard's table held)
void atari8_set_key(atari8 *m, int code, bool shift, bool ctrl);
void atari8_break(atari8 *m);
void atari8_joy(atari8 *m, int port, unsigned bits);     // port 0 / 1
void atari8_console(atari8 *m, unsigned bits);
// The order dsp-cpp's keyboard table takes the keys in (a held set picks
// its first): atari8_key_order(i) for i = 0.. until -1
int atari8_key_order(int i);

void atari8_render_row(const atari8 *m, int y, uint32_t *out);      // A8_W pixels 0xRRGGBB
void atari8_render_row565(const atari8 *m, int y, int x0, int n, uint16_t *out);
void atari8_set_rgb565(atari8 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));
const uint8_t *atari8_frame(const atari8 *m);                        // A8_W x A8_H colour bytes

// A disk (.ATR): the pbuf is taken (freed on eject / the next disk)
bool atari8_load_pbuf(atari8 *m, zx_pbuf *pb, const char *name);
bool atari8_load_mem(atari8 *m, const uint8_t *data, size_t size, const char *name);
bool atari8_load_file(atari8 *m, const char *path);
bool atari8_has_disk(const atari8 *m);
void atari8_eject(atari8 *m);
const char *atari8_last_error(const atari8 *m);
void atari8_set_turbo(atari8 *m, bool on);

// The key (and SHIFT) that types an ASCII character (-1: none). For the
// typists of the boards and Android.
int atari8_char_key(char c, bool *shift);
