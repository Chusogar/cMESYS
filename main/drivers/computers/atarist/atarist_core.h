// atarist_core.h - Atari ST (520 / 1040ST, PAL): a C11 migration of
// dsp-cpp's drivers/computers/atari_st with its machine/mc68901 and
// machine/st_floppy (main/machine). The 68000 is Musashi (cpu/m68k), as the
// QL's. TOS (1.00 / 1.02 / 1.04, or EmuTOS) at $FC0000, the shifter's three
// resolutions, the MFP's timers and interrupts, the YM2149 (sound and the
// drives' select lines), the WD1772 + DMA with .ST / .MSA / .STX disks, the
// IKBD (keyboard, mouse, joysticks, clock) behind the 6850 ACIA, and the
// Mega ST / STE blitter TOS 1.04's Line-A uses. One machine per program
// (Musashi's state is global).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

#define ST_W            640             // the picture: low / medium doubled
#define ST_H            400
#define ST_CPU_CLOCK    8010265
#define ST_FPS          50.053
#define ST_AUDIO_MAX    2048
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define ST_AUDIO_RATE   22050
#else
#define ST_AUDIO_RATE   44100
#endif
#define ST_ROM_SIZE     0x30000         // 192 KB TOS

#define ST_MEDIA_EXTS { ".st", ".msa", ".stx", NULL }
#define ST_MEDIA_LABEL "ST MSA STX"

// IKBD scan codes (the ST's keyboard)
enum {
    STK_ESC = 0x01, STK_1 = 0x02, STK_0 = 0x0b, STK_MINUS = 0x0c, STK_EQUALS = 0x0d, STK_BACKSPACE = 0x0e,
    STK_TAB = 0x0f, STK_Q = 0x10, STK_P = 0x19, STK_LBRACKET = 0x1a, STK_RBRACKET = 0x1b, STK_RETURN = 0x1c,
    STK_CONTROL = 0x1d, STK_A = 0x1e, STK_L = 0x26, STK_SEMICOLON = 0x27, STK_QUOTE = 0x28, STK_BACKQUOTE = 0x29,
    STK_LSHIFT = 0x2a, STK_BACKSLASH = 0x2b, STK_Z = 0x2c, STK_M = 0x32, STK_COMMA = 0x33, STK_PERIOD = 0x34,
    STK_SLASH = 0x35, STK_RSHIFT = 0x36, STK_ALTERNATE = 0x38, STK_SPACE = 0x39, STK_CAPS = 0x3a, STK_F1 = 0x3b,
    STK_F10 = 0x44, STK_CLRHOME = 0x47, STK_UP = 0x48, STK_KP_MINUS = 0x4a, STK_LEFT = 0x4b, STK_RIGHT = 0x4d,
    STK_KP_PLUS = 0x4e, STK_DOWN = 0x50, STK_INSERT = 0x52, STK_DELETE = 0x53, STK_ISO = 0x60, STK_UNDO = 0x61,
    STK_HELP = 0x62, STK_KP_LPAREN = 0x63, STK_KP_RPAREN = 0x64, STK_KP_SLASH = 0x65, STK_KP_STAR = 0x66,
    STK_KP7 = 0x67, STK_KP8 = 0x68, STK_KP9 = 0x69, STK_KP4 = 0x6a, STK_KP5 = 0x6b, STK_KP6 = 0x6c,
    STK_KP1 = 0x6d, STK_KP2 = 0x6e, STK_KP3 = 0x6f, STK_KP0 = 0x70, STK_KP_PERIOD = 0x71, STK_KP_ENTER = 0x72,
};

// Joystick bits (atarist_joy: port 1 the joystick port, 0 the mouse port)
enum { ST_UP = 1, ST_DOWN = 2, ST_LEFT = 4, ST_RIGHT = 8, ST_FIRE = 0x80 };

// The host's pointer (dsp-cpp's MachineInputs pointer fields): an absolute
// position over the 640x400 picture, or (relative) motion in its pixels
typedef struct {
    bool has_pointer;           // over the picture (absolute) / a mouse there (relative)
    bool relative;              // dx / dy instead of x / y
    bool resync;                // it just came back over the picture
    int x, y, dx, dy;
    bool left, right;
} atarist_pointer;

// tos: 192 KB, read in place (kept by the caller). ram_size: 256 KB, 512 KB
// or 1 MB (a multiple of 32 KB; the boards with little memory take less).
bool atarist_init(const uint8_t *tos, size_t tos_size, uint32_t ram_size, int audio_rate);
void atarist_reset(void);
int atarist_run_frame(const int16_t **audio);
uint32_t atarist_ram_size(void);

void atarist_key(int scancode, bool down);      // held keys, sent as the IKBD's make / break
void atarist_keys_clear(void);
void atarist_joy(int port, unsigned bits);
void atarist_pointer_input(const atarist_pointer *p);

// The last frame: row y (0..399) of 640 pixels 0xRRGGBB
void atarist_render_row(int y, uint32_t *out);
// The boards: 320x200, row y (0..199): low resolution as it is, medium and
// high halved (two pixels blended)
void atarist_render_row320(int y, uint16_t *out);
void atarist_set_rgb565(uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));
int atarist_resolution(void);                   // 0 low, 1 medium, 2 high

// A disk into drive 0 (A:) or 1 (B:): the pbuf is taken
bool atarist_load_pbuf(int drive, zx_pbuf *pb, const char *name);
bool atarist_load_file(int drive, const char *path);
bool atarist_has_disk(int drive);
void atarist_eject(int drive);
bool atarist_drive_light(void);
const char *atarist_last_error(void);
void atarist_set_turbo(bool on);
