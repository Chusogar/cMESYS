// c64_core.h - Commodore 64 (PAL): a C11 migration of dsp-cpp's
// drivers/computers/c64 (from commodore64.pas of the DSP emulator). 6510 with
// its port at $00/$01 (cpu/m6502/m65xx), the PLA's banking, VIC-II (6569,
// a line at a time), SID (6581), two CIAs (keyboard / joysticks, IEC bus /
// VIC bank), the 1541 on the IEC bus when its DOS ROM is there
// (machine/c1541: .d64 / .g64, and .t64 tapes turned into a disk), .tap
// cassettes, and .prg / .t64 files put straight into the RAM (and RUN) when
// there is no drive. The on-screen keyboard of dsp-cpp is left out.
//
// The picture is kept 4 bits a pixel, only the part the frontend shows.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define C64_SMALL 1
#endif

#define C64_FULL_W      384             /* the VIC's picture */
#define C64_FULL_H      270
#ifdef C64_SMALL
#define C64_PIC_W       320
#define C64_PIC_H       240
#define C64_AUDIO_RATE  22050
#else
#define C64_PIC_W       C64_FULL_W
#define C64_PIC_H       C64_FULL_H
#define C64_AUDIO_RATE  44100
#endif
#define C64_AUDIO_MAX   1024
#define C64_FPS         (985248.0 / (312 * 63))

/* Keys: the matrix, column (CIA1 PA bit) * 8 + row (PB bit) */
#define C64_KEY(col, row) (((col) << 3) | (row))
enum {
    C64K_DEL = C64_KEY(0, 0), C64K_RETURN = C64_KEY(0, 1), C64K_CRSR_LR = C64_KEY(0, 2), C64K_F7 = C64_KEY(0, 3),
    C64K_F1 = C64_KEY(0, 4), C64K_F3 = C64_KEY(0, 5), C64K_F5 = C64_KEY(0, 6), C64K_CRSR_UD = C64_KEY(0, 7),
    C64K_3 = C64_KEY(1, 0), C64K_W = C64_KEY(1, 1), C64K_A = C64_KEY(1, 2), C64K_4 = C64_KEY(1, 3),
    C64K_Z = C64_KEY(1, 4), C64K_S = C64_KEY(1, 5), C64K_E = C64_KEY(1, 6), C64K_LSHIFT = C64_KEY(1, 7),
    C64K_5 = C64_KEY(2, 0), C64K_R = C64_KEY(2, 1), C64K_D = C64_KEY(2, 2), C64K_6 = C64_KEY(2, 3),
    C64K_C = C64_KEY(2, 4), C64K_F = C64_KEY(2, 5), C64K_T = C64_KEY(2, 6), C64K_X = C64_KEY(2, 7),
    C64K_7 = C64_KEY(3, 0), C64K_Y = C64_KEY(3, 1), C64K_G = C64_KEY(3, 2), C64K_8 = C64_KEY(3, 3),
    C64K_B = C64_KEY(3, 4), C64K_H = C64_KEY(3, 5), C64K_U = C64_KEY(3, 6), C64K_V = C64_KEY(3, 7),
    C64K_9 = C64_KEY(4, 0), C64K_I = C64_KEY(4, 1), C64K_J = C64_KEY(4, 2), C64K_0 = C64_KEY(4, 3),
    C64K_M = C64_KEY(4, 4), C64K_K = C64_KEY(4, 5), C64K_O = C64_KEY(4, 6), C64K_N = C64_KEY(4, 7),
    C64K_PLUS = C64_KEY(5, 0), C64K_P = C64_KEY(5, 1), C64K_L = C64_KEY(5, 2), C64K_MINUS = C64_KEY(5, 3),
    C64K_PERIOD = C64_KEY(5, 4), C64K_COLON = C64_KEY(5, 5), C64K_AT = C64_KEY(5, 6), C64K_COMMA = C64_KEY(5, 7),
    C64K_POUND = C64_KEY(6, 0), C64K_ASTERISK = C64_KEY(6, 1), C64K_SEMICOLON = C64_KEY(6, 2), C64K_HOME = C64_KEY(6, 3),
    C64K_RSHIFT = C64_KEY(6, 4), C64K_EQUALS = C64_KEY(6, 5), C64K_UPARROW = C64_KEY(6, 6), C64K_SLASH = C64_KEY(6, 7),
    C64K_1 = C64_KEY(7, 0), C64K_LEFTARROW = C64_KEY(7, 1), C64K_CTRL = C64_KEY(7, 2), C64K_2 = C64_KEY(7, 3),
    C64K_SPACE = C64_KEY(7, 4), C64K_CBM = C64_KEY(7, 5), C64K_Q = C64_KEY(7, 6), C64K_RUNSTOP = C64_KEY(7, 7),
};
/* Joystick bits (active high here): up, down, left, right, fire */
enum { C64_JOY_UP = 1, C64_JOY_DOWN = 2, C64_JOY_LEFT = 4, C64_JOY_RIGHT = 8, C64_JOY_FIRE = 16 };

#define C64_MEDIA_EXTS { ".prg", ".t64", ".d64", ".g64", ".tap", NULL }
#define C64_MEDIA_LABEL "PRG T64 D64 G64 TAP"

typedef struct c64 c64;

c64 *c64_create(int audio_rate);
void c64_destroy(c64 *m);
/* kernal 8 KB, basic 8 KB, chargen 4 KB; dos1541 16 KB or NULL (no drive) */
void c64_set_roms(c64 *m, const uint8_t *kernal, const uint8_t *basic, const uint8_t *chargen,
                  const uint8_t *dos1541);
void c64_reset(c64 *m);
int c64_run_frame(c64 *m, const int16_t **audio);

void c64_keys_clear(c64 *m);
void c64_key(c64 *m, int code, bool down);
void c64_joy(c64 *m, int port, uint8_t bits);  /* port 1 or 2 */
void c64_restore(c64 *m, bool down);            /* RESTORE: the NMI */
/* Text (upper case PETSCII, \r = RETURN) into the KERNAL's keyboard buffer
 * (10 characters): read once BASIC waits for a line, even after a LOAD */
void c64_kbuf(c64 *m, const char *text);

/* Row y (0 .. C64_PIC_H-1) of the shown picture, 0xRRGGBB or RGB565 */
void c64_render_row(const c64 *m, int y, uint32_t *out);
void c64_render_row565(const c64 *m, int y, uint16_t *out);
/* The RGB565 colours in a panel's own format (the CYD's byte order / BGR) */
void c64_set_rgb565(c64 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));

bool c64_load_pbuf(c64 *m, zx_pbuf *pb, const char *name);    /* takes the pages over */
bool c64_load_file(c64 *m, const char *path);
const char *c64_last_error(const c64 *m);
bool c64_has_drive(const c64 *m);
bool c64_drive_led(const c64 *m);
void c64_tape_toggle(c64 *m);
bool c64_tape_loaded(const c64 *m);
bool c64_tape_playing(const c64 *m);
