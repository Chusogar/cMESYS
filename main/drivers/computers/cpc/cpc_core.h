// cpc_core.h - Amstrad CPC 464 / 664 / 6128: a C11 migration of dsp-cpp's
// drivers/computers/amstrad_cpc (itself from amstrad_cpc.pas of the DSP
// emulator). Z80 at 4 MHz with the Gate Array's wait states (z80dsp with the
// CPC timing tables), the 6845 CRTC wired the Amstrad way, the Gate Array
// (pens, modes 0/1/2, ROM enables, the 300 Hz interrupt), the 8255 PPI
// (AY bus, VSYNC / cassette, keyboard row), the AY-3-8912 scanning the
// keyboard, the uPD765 with .dsk / extended .dsk (664 / 6128, AMSDOS) and
// cassettes (.cdt / .tzx / .csw ... through the Spectrum tape player, the
// 4 MHz clock scaled to its 3.5 MHz timings), and .sna snapshots.
//
// The picture: the CRTC beam records each line (its first video address, the
// bytes it shows, the mode, the pens and the border) and the frontends draw
// the rows from the RAM when they show them (as the Spectrum's), so the core
// keeps no frame buffer: the boards have no room for a 400 x 312 one.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "zx_pbuf.h"

#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define CPC_SMALL 1             /* the boards: 320x240 picture, 22 kHz sound */
#endif

#define CPC_FULL_W      400     /* dsp-cpp's picture */
#define CPC_FULL_H      312
#ifdef CPC_SMALL
#define CPC_PIC_W       320     /* the centre of it */
#define CPC_PIC_H       240
#define CPC_AUDIO_RATE  22050
#else
#define CPC_PIC_W       CPC_FULL_W
#define CPC_PIC_H       CPC_FULL_H
#define CPC_AUDIO_RATE  44100
#endif
#define CPC_PIC_X       ((CPC_FULL_W - CPC_PIC_W) / 2)
#ifdef CPC_SMALL
#define CPC_PIC_Y       40      /* (the picture sits a little low in the 312 lines) */
#else
#define CPC_PIC_Y       0
#endif
#define CPC_AUDIO_MAX   1024
#define CPC_FPS         (4000000.0 / 79872.0)

typedef enum { CPC_464 = 0, CPC_664, CPC_6128 } cpc_model_t;

/* Keys: the keyboard matrix, line * 8 + bit (the AY reads line `keyb_line`). */
#define CPC_KEY(line, bit) (((line) << 3) | (bit))
enum {
    CPCK_UP = CPC_KEY(0, 0), CPCK_RIGHT = CPC_KEY(0, 1), CPCK_DOWN = CPC_KEY(0, 2), CPCK_F9 = CPC_KEY(0, 3),
    CPCK_F6 = CPC_KEY(0, 4), CPCK_F3 = CPC_KEY(0, 5), CPCK_KPENTER = CPC_KEY(0, 6), CPCK_KPDOT = CPC_KEY(0, 7),
    CPCK_LEFT = CPC_KEY(1, 0), CPCK_COPY = CPC_KEY(1, 1), CPCK_F7 = CPC_KEY(1, 2), CPCK_F8 = CPC_KEY(1, 3),
    CPCK_F5 = CPC_KEY(1, 4), CPCK_F1 = CPC_KEY(1, 5), CPCK_F2 = CPC_KEY(1, 6), CPCK_F0 = CPC_KEY(1, 7),
    CPCK_CLR = CPC_KEY(2, 0), CPCK_LBRACKET = CPC_KEY(2, 1), CPCK_RETURN = CPC_KEY(2, 2), CPCK_RBRACKET = CPC_KEY(2, 3),
    CPCK_F4 = CPC_KEY(2, 4), CPCK_SHIFT = CPC_KEY(2, 5), CPCK_BACKSLASH = CPC_KEY(2, 6), CPCK_CTRL = CPC_KEY(2, 7),
    CPCK_CARET = CPC_KEY(3, 0), CPCK_MINUS = CPC_KEY(3, 1), CPCK_AT = CPC_KEY(3, 2), CPCK_P = CPC_KEY(3, 3),
    CPCK_SEMICOLON = CPC_KEY(3, 4), CPCK_COLON = CPC_KEY(3, 5), CPCK_SLASH = CPC_KEY(3, 6), CPCK_PERIOD = CPC_KEY(3, 7),
    CPCK_0 = CPC_KEY(4, 0), CPCK_9 = CPC_KEY(4, 1), CPCK_O = CPC_KEY(4, 2), CPCK_I = CPC_KEY(4, 3),
    CPCK_L = CPC_KEY(4, 4), CPCK_K = CPC_KEY(4, 5), CPCK_M = CPC_KEY(4, 6), CPCK_COMMA = CPC_KEY(4, 7),
    CPCK_8 = CPC_KEY(5, 0), CPCK_7 = CPC_KEY(5, 1), CPCK_U = CPC_KEY(5, 2), CPCK_Y = CPC_KEY(5, 3),
    CPCK_H = CPC_KEY(5, 4), CPCK_J = CPC_KEY(5, 5), CPCK_N = CPC_KEY(5, 6), CPCK_SPACE = CPC_KEY(5, 7),
    CPCK_6 = CPC_KEY(6, 0), CPCK_5 = CPC_KEY(6, 1), CPCK_R = CPC_KEY(6, 2), CPCK_T = CPC_KEY(6, 3),
    CPCK_G = CPC_KEY(6, 4), CPCK_F = CPC_KEY(6, 5), CPCK_B = CPC_KEY(6, 6), CPCK_V = CPC_KEY(6, 7),
    CPCK_4 = CPC_KEY(7, 0), CPCK_3 = CPC_KEY(7, 1), CPCK_E = CPC_KEY(7, 2), CPCK_W = CPC_KEY(7, 3),
    CPCK_S = CPC_KEY(7, 4), CPCK_D = CPC_KEY(7, 5), CPCK_C = CPC_KEY(7, 6), CPCK_X = CPC_KEY(7, 7),
    CPCK_1 = CPC_KEY(8, 0), CPCK_2 = CPC_KEY(8, 1), CPCK_ESC = CPC_KEY(8, 2), CPCK_Q = CPC_KEY(8, 3),
    CPCK_TAB = CPC_KEY(8, 4), CPCK_A = CPC_KEY(8, 5), CPCK_CAPSLOCK = CPC_KEY(8, 6), CPCK_Z = CPC_KEY(8, 7),
    CPCK_DEL = CPC_KEY(9, 7),
};
/* Joystick bits (cpc_joy): matrix line 9 */
enum { CPC_JOY_UP = 1, CPC_JOY_DOWN = 2, CPC_JOY_LEFT = 4, CPC_JOY_RIGHT = 8, CPC_JOY_FIRE2 = 16, CPC_JOY_FIRE1 = 32 };

#define CPC_MEDIA_EXTS { ".sna", ".dsk", ".edsk", ".cdt", ".tzx", ".csw", NULL }
#define CPC_MEDIA_LABEL "SNA DSK CDT TZX CSW"

typedef struct cpc cpc;

cpc *cpc_create(cpc_model_t model, int audio_rate);
void cpc_destroy(cpc *m);
/* os: 32 KB (the lower ROM, then BASIC); amsdos: 16 KB or NULL (664 / 6128) */
void cpc_set_roms(cpc *m, const uint8_t *os, const uint8_t *amsdos);
void cpc_reset(cpc *m);
/* One frame; *audio gets its samples (mono, the audio rate). */
int cpc_run_frame(cpc *m, const int16_t **audio);

void cpc_keys_clear(cpc *m);
void cpc_key(cpc *m, int code, bool down);
void cpc_joy(cpc *m, uint8_t bits);
void cpc_set_color_monitor(cpc *m, bool color);   /* false: the green monitor */
/* The RGB565 colours in a panel's own format (the CYD's byte order / BGR) */
void cpc_set_rgb565(cpc *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));

/* The picture of the last frame: row y (0 .. CPC_FULL_H-1) of `w` pixels from
 * column x0, as 0xRRGGBB or RGB565. */
void cpc_render_row(const cpc *m, int y, int x0, int w, uint32_t *out);
void cpc_render_row565(const cpc *m, int y, int x0, int w, uint16_t *out);

/* Media, by the name's extension: .sna, .dsk / .edsk (664 / 6128), tapes. */
bool cpc_load_pbuf(cpc *m, zx_pbuf *pb, const char *name);   /* takes the pages over */
bool cpc_load_mem(cpc *m, const uint8_t *data, size_t n, const char *name);
bool cpc_load_file(cpc *m, const char *path);
const char *cpc_last_error(const cpc *m);
void cpc_tape_toggle(cpc *m);
bool cpc_tape_loaded(const cpc *m);
bool cpc_tape_playing(const cpc *m);
cpc_model_t cpc_model(const cpc *m);
const char *cpc_title(cpc_model_t model);
