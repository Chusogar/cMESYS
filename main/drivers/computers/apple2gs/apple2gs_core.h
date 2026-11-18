// apple2gs_core.h - the Apple IIGS (ROM 03): a C11 migration of dsp-cpp's
// drivers/computers/apple2gs. WDC 65C816 (cpu/w65c816) at 2.8 MHz (1 MHz
// when slowed down), the FPI and Mega II memory (fast RAM in banks $00-$3F,
// the slow banks $E0/$E1 the video reads, the shadowing of banks $00/$01
// into them), the VGC (the Apple II modes and Super Hi-Res, 320 / 640 dots,
// 16 palettes, fill mode, scan-line interrupts), the Ensoniq DOC
// (sound/es5503) and the speaker, the ADB GLU with keyboard and mouse,
// the clock chip and battery RAM, and the SmartPort card in slot 7 (high
// level, through WDM) with ProDOS-order images: .po, .2mg, .hdv, and 140K
// .dsk / .do turned into ProDOS order. The IWM ports are empty, as in dsp-cpp.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "blkfile.h"

#define A2GS_W 640
#define A2GS_H 200                      // lines (shown twice as high)
#define A2GS_FPS 59.923
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define A2GS_SMALL 1
#define A2GS_AUDIO_RATE 22050
#else
#define A2GS_AUDIO_RATE 44100
#endif
#define A2GS_AUDIO_MAX 2048
#define A2GS_DISKS 4                    // SmartPort units

#define A2GS_MEDIA_EXTS { ".po", ".2mg", ".hdv", ".dsk", ".do", ".img", NULL }
#define A2GS_MEDIA_LABEL "PO 2MG HDV DSK DO"

// Keys: Apple Desktop Bus key codes (the IIGS's keyboard)
enum {
    GSK_A = 0x00, GSK_S = 0x01, GSK_D = 0x02, GSK_F = 0x03, GSK_H = 0x04, GSK_G = 0x05, GSK_Z = 0x06, GSK_X = 0x07,
    GSK_C = 0x08, GSK_V = 0x09, GSK_B = 0x0b, GSK_Q = 0x0c, GSK_W = 0x0d, GSK_E = 0x0e, GSK_R = 0x0f,
    GSK_Y = 0x10, GSK_T = 0x11, GSK_1 = 0x12, GSK_2 = 0x13, GSK_3 = 0x14, GSK_4 = 0x15, GSK_6 = 0x16, GSK_5 = 0x17,
    GSK_EQUALS = 0x18, GSK_9 = 0x19, GSK_7 = 0x1a, GSK_MINUS = 0x1b, GSK_8 = 0x1c, GSK_0 = 0x1d,
    GSK_RBRACKET = 0x1e, GSK_O = 0x1f, GSK_U = 0x20, GSK_LBRACKET = 0x21, GSK_I = 0x22, GSK_P = 0x23,
    GSK_RETURN = 0x24, GSK_L = 0x25, GSK_J = 0x26, GSK_QUOTE = 0x27, GSK_K = 0x28, GSK_SEMICOLON = 0x29,
    GSK_BACKSLASH = 0x2a, GSK_COMMA = 0x2b, GSK_SLASH = 0x2c, GSK_N = 0x2d, GSK_M = 0x2e, GSK_PERIOD = 0x2f,
    GSK_TAB = 0x30, GSK_SPACE = 0x31, GSK_GRAVE = 0x32, GSK_DELETE = 0x33, GSK_ESC = 0x35, GSK_CTRL = 0x36,
    GSK_COMMAND = 0x37, GSK_SHIFT = 0x38, GSK_CAPS = 0x39, GSK_OPTION = 0x3a, GSK_LEFT = 0x3b, GSK_RIGHT = 0x3c,
    GSK_DOWN = 0x3d, GSK_UP = 0x3e,
    GSK_KP_PERIOD = 0x41, GSK_KP_MUL = 0x43, GSK_KP_PLUS = 0x45, GSK_KP_CLEAR = 0x47, GSK_KP_DIV = 0x4b,
    GSK_KP_ENTER = 0x4c, GSK_KP_MINUS = 0x4e, GSK_KP_EQUALS = 0x51, GSK_KP0 = 0x52, GSK_KP1 = 0x53,
    GSK_KP2 = 0x54, GSK_KP3 = 0x55, GSK_KP4 = 0x56, GSK_KP5 = 0x57, GSK_KP6 = 0x58, GSK_KP7 = 0x59,
    GSK_KP8 = 0x5b, GSK_KP9 = 0x5c,
    GSK_HOME = 0x73, GSK_RESET = 0x7f,
    GSK_COUNT = 0x80
};

// A disk: read and written in place (machine/blkfile.h)
typedef blkfile a2gs_file;

typedef struct apple2gs apple2gs;

// ram_banks: 64 KB banks of fast RAM ($00-...; 16 = 1 MB, 64 = 4 MB)
apple2gs *apple2gs_create(int ram_banks, int audio_rate);
void apple2gs_destroy(apple2gs *m);
// The ROMs (read in place): 341-0728 (banks $FC-$FD), 341-0748 ($FE-$FF,
// halves swapped as in the chip), the video ROM (4 KB)
void apple2gs_set_roms(apple2gs *m, const uint8_t *rom_fc, const uint8_t *rom_fe, const uint8_t *chr);
void apple2gs_set_time(apple2gs *m, int64_t unix_time);   // (0: the host's clock)
void apple2gs_reset(apple2gs *m);        // power on
void apple2gs_reset_key(apple2gs *m);    // CTRL-RESET
int apple2gs_run_frame(apple2gs *m, const int16_t **audio);
void apple2gs_set_turbo(apple2gs *m, bool on);

void apple2gs_key(apple2gs *m, int adb_code, bool down);
void apple2gs_keys_clear(apple2gs *m);
// The mouse: the pointer in the picture (0..639, 0..399 as shown) and its button
void apple2gs_pointer(apple2gs *m, int x, int y, bool button);
void apple2gs_mouse(apple2gs *m, int dx, int dy, bool button);   // (moves that pointer)

// Line y (0..199): 640 dots, 0xRRGGBB or RGB565
void apple2gs_render_row(const apple2gs *m, int y, uint32_t *out);
void apple2gs_render_row565(const apple2gs *m, int y, uint16_t *out);
void apple2gs_set_rgb565(apple2gs *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));

// Disks (SmartPort units 1..4; the first boots). NULL: in; otherwise why not
const char *apple2gs_insert(apple2gs *m, int unit, a2gs_file *f, const char *name);
const char *apple2gs_load_file(apple2gs *m, int unit, const char *path);
void apple2gs_eject(apple2gs *m, int unit);
bool apple2gs_has_disk(const apple2gs *m, int unit);
bool apple2gs_disk_busy(const apple2gs *m);    // a block read / written in the last frames
