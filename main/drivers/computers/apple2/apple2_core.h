// apple2_core.h - the Apple II family: the original Apple ][ (Integer BASIC
// and the Autostart monitor), the ][+, the //e and the //e Enhanced (65C02).
// A C11 migration of dsp-cpp's drivers/computers/apple2 (and its
// video/apple2_video): 6502 (cpu/m6502/m65xx; the 65C02 on the Enhanced),
// 48 KB of RAM with the 16 KB language card (on the //e, 64 KB more of
// auxiliary RAM: 80 columns and double hi-res), the //e's MMU and IOU soft
// switches, text 40/80 columns, lo-res, hi-res and double hi-res, the
// speaker, paddles / joystick and buttons, and a Disk II card in slot 6
// (machine/diskii) when its P5 boot PROM is there.
//
// Beyond dsp-cpp: the keyboard is the machine's own (its keys and an encoder
// as the AY-5-3600's: SHIFT and CTRL, the ][+'s upper case only and its
// symbols on the shifted keys, the //e's CAPS LOCK and auto-repeat, the ][s'
// REPT key, RESET / CTRL-RESET), and the speaker is filtered.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define A2_SMALL 1
#endif

enum { A2_II = 0, A2_IIPLUS = 1, A2_IIE = 2, A2_IIEE = 3 };

#define A2_W 560                        // a line: 7 dots per byte, 2 per hi-res dot
#define A2_H 192
#define A2_CLOCK 1020484
#define A2_FPS ((double)A2_CLOCK / (262.0 * 65.0))
#ifdef A2_SMALL
#define A2_AUDIO_RATE 22050
#else
#define A2_AUDIO_RATE 44100
#endif
#define A2_AUDIO_MAX 1024

// Keys: one code per key of the machine's keyboards (the ][ / ][+ and the
// //e); the core turns them into characters as the keyboard's encoder did.
enum {
    A2K_CTRL = 0x01, A2K_SHIFT = 0x02, A2K_RSHIFT = 0x03, A2K_CAPS = 0x04,
    A2K_OPEN_APPLE = 0x05, A2K_CLOSED_APPLE = 0x06, A2K_RESET = 0x07,
    A2K_LEFT = 0x08, A2K_TAB = 0x09, A2K_DOWN = 0x0A, A2K_UP = 0x0B,
    A2K_RETURN = 0x0D, A2K_REPT = 0x0E, A2K_RIGHT = 0x15, A2K_ESC = 0x1B,
    A2K_SPACE = 0x20, A2K_QUOTE = 0x27, A2K_COMMA = 0x2C, A2K_MINUS = 0x2D,
    A2K_PERIOD = 0x2E, A2K_SLASH = 0x2F,
    A2K_0 = 0x30,                       // .. A2K_9 = 0x39
    A2K_COLON = 0x3A,                   // (the ][s' : * key)
    A2K_SEMICOLON = 0x3B, A2K_EQUALS = 0x3D,
    A2K_A = 0x41,                       // .. A2K_Z = 0x5A
    A2K_LBRACKET = 0x5B, A2K_BACKSLASH = 0x5C, A2K_RBRACKET = 0x5D,
    A2K_GRAVE = 0x60, A2K_DELETE = 0x7F,
    A2K_COUNT = 0x80
};
// Joystick bits (active high): the stick at the ends of paddles 0 / 1
enum { A2_JOY_UP = 1, A2_JOY_DOWN = 2, A2_JOY_LEFT = 4, A2_JOY_RIGHT = 8, A2_JOY_B0 = 16, A2_JOY_B1 = 32 };

#define A2_MEDIA_EXTS { ".dsk", ".do", ".po", ".nib", NULL }
#define A2_MEDIA_LABEL "DSK DO PO NIB"

typedef struct apple2 apple2;

apple2 *apple2_create(int model, int audio_rate);
void apple2_destroy(apple2 *m);
int apple2_model(const apple2 *m);
const char *apple2_title(int model);
// The ROMs (kept, read in place): main = the system ROM from $D000 (][ and
// ][+: 12 KB, $D000-$FFFF; the ]['s may be 8 KB, $E000-$FFFF) or from $C000
// (//e: 16 KB); chr = the character ROM (2 KB or 4 KB, NULL: a block font);
// disk = the Disk II's P5 PROM (256 bytes, NULL: no disk card); aid = the
// ]['s Programmer's Aid #1 at $D000 (2 KB, or NULL).
void apple2_set_roms(apple2 *m, const uint8_t *main, uint32_t main_len, const uint8_t *chr, uint32_t chr_len,
                     const uint8_t *disk, const uint8_t *aid);
void apple2_reset(apple2 *m);           // power on (RAM cleared)
void apple2_reset_key(apple2 *m);       // RESET (CTRL-RESET): the 6502's RESET line
int apple2_run_frame(apple2 *m, const int16_t **audio);

void apple2_keys_clear(apple2 *m);
void apple2_key(apple2 *m, int code, bool down);
// A character typed straight into the keyboard latch (the desktop: the PC's
// own layout), and held until apple2_char_up / apple2_keys_clear
void apple2_char(apple2 *m, uint8_t ascii);
void apple2_char_up(apple2 *m);
bool apple2_caps_lock(const apple2 *m);
void apple2_joy(apple2 *m, uint8_t bits);
void apple2_paddle(apple2 *m, int n, uint8_t value);
void apple2_set_turbo(apple2 *m, bool on);

// Line y (0..191) of the picture: 560 dots, 0xRRGGBB or RGB565
void apple2_render_row(const apple2 *m, int y, uint32_t *out);
void apple2_render_row565(const apple2 *m, int y, uint16_t *out);
// The panels' RGB565 (byte order / BGR)
void apple2_set_rgb565(apple2 *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));
bool apple2_is_80col(const apple2 *m);  // the picture has single dots (80 columns, double hi-res)

const char *apple2_load_pbuf(apple2 *m, zx_pbuf *pb, const char *name);   // takes the pages
const char *apple2_load_file(apple2 *m, const char *path);                // NULL: loaded
void apple2_eject(apple2 *m);
bool apple2_has_disk_card(const apple2 *m);
bool apple2_disk_loaded(const apple2 *m);
bool apple2_drive_led(const apple2 *m);
