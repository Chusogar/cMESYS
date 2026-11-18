// macplus_core.h - the Macintosh Plus: a C11 migration of dsp-cpp's
// drivers/computers/macplus (MAME's mac128_state::macplus). 68000 at
// 7.8336 MHz (Musashi), the 128K ROM, 4 MB of RAM, 512x342 one bit, the VIA
// (machine/via6522), the SCC as the mouse needs it, the IWM with an 800K
// Sony drive (machine/iwm, mac_dsk; a 1.44 MB image is served by LBA
// through .Sony, as a SuperDrive), the 343-0042 clock (machine/mac_rtc),
// the NCR 5380 with a SCSI hard disk (machine/ncr5380), the sound buffer,
// and System 7's compressed resources expanded for the 128K ROM
// (machine/mac_dcmp).
//
// Beyond dsp-cpp: the whole M0110A keyboard (dsp-cpp sends only Return,
// Escape and Space): its transitions as Mini vMac sends them (key << 1,
// bit 7 when let go, the keypad and arrows after $79), and the sound buffer
// read on all 370 lines.
//
// One machine (Musashi is one CPU): the functions take no handle.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "blkfile.h"

#define MACPLUS_W 512
#define MACPLUS_H 342
#define MACPLUS_CLOCK 7833600
#define MACPLUS_FPS ((double)MACPLUS_CLOCK / (370.0 * 352.0))
#define MACPLUS_AUDIO_RATE 22254        // a sample a line
#define MACPLUS_RAM 0x400000u

#define MACPLUS_MEDIA_EXTS { ".dsk", ".img", ".image", ".dc42", ".hda", ".hd", ".vhd", NULL }
#define MACPLUS_MEDIA_LABEL "DSK IMG DC42 HDA"

// Keys: the Macintosh's key codes (Mini vMac's MKC_*; the M0110A's keypad
// and arrows have the ADB codes)
enum {
    MK_A = 0x00, MK_S = 0x01, MK_D = 0x02, MK_F = 0x03, MK_H = 0x04, MK_G = 0x05, MK_Z = 0x06, MK_X = 0x07,
    MK_C = 0x08, MK_V = 0x09, MK_B = 0x0B, MK_Q = 0x0C, MK_W = 0x0D, MK_E = 0x0E, MK_R = 0x0F,
    MK_Y = 0x10, MK_T = 0x11, MK_1 = 0x12, MK_2 = 0x13, MK_3 = 0x14, MK_4 = 0x15, MK_6 = 0x16, MK_5 = 0x17,
    MK_EQUALS = 0x18, MK_9 = 0x19, MK_7 = 0x1A, MK_MINUS = 0x1B, MK_8 = 0x1C, MK_0 = 0x1D, MK_RBRACKET = 0x1E,
    MK_O = 0x1F, MK_U = 0x20, MK_LBRACKET = 0x21, MK_I = 0x22, MK_P = 0x23, MK_RETURN = 0x24, MK_L = 0x25,
    MK_J = 0x26, MK_QUOTE = 0x27, MK_K = 0x28, MK_SEMICOLON = 0x29, MK_BACKSLASH = 0x2A, MK_COMMA = 0x2B,
    MK_SLASH = 0x2C, MK_N = 0x2D, MK_M = 0x2E, MK_PERIOD = 0x2F, MK_TAB = 0x30, MK_SPACE = 0x31,
    MK_GRAVE = 0x32, MK_BACKSPACE = 0x33, MK_ESC = 0x35, MK_COMMAND = 0x37, MK_SHIFT = 0x38,
    MK_CAPS = 0x39, MK_OPTION = 0x3A, MK_CONTROL = 0x3B,
    MK_KP_PERIOD = 0x41, MK_KP_MUL = 0x43, MK_KP_PLUS = 0x45, MK_KP_CLEAR = 0x47, MK_KP_DIV = 0x4B,
    MK_KP_ENTER = 0x4C, MK_KP_MINUS = 0x4E, MK_KP_EQUALS = 0x51, MK_KP0 = 0x52, MK_KP1 = 0x53, MK_KP2 = 0x54,
    MK_KP3 = 0x55, MK_KP4 = 0x56, MK_KP5 = 0x57, MK_KP6 = 0x58, MK_KP7 = 0x59, MK_KP8 = 0x5B, MK_KP9 = 0x5C,
    MK_LEFT = 0x7B, MK_RIGHT = 0x7C, MK_DOWN = 0x7D, MK_UP = 0x7E,
    MK_COUNT = 0x80
};

// The ROM: the even and odd byte chips (64 KB each; read in place)
bool macplus_init(const uint8_t *rom_even, const uint8_t *rom_odd);
void macplus_reset(void);               // power on (RAM cleared)
int macplus_run_frame(const int16_t **audio);
void macplus_set_turbo(bool on);

void macplus_key(int code, bool down);
void macplus_keys_clear(void);
// The mouse: the host pointer on the picture, absolute (or moved by dx, dy)
void macplus_pointer(int x, int y, bool button);
void macplus_mouse(int dx, int dy, bool button);

void macplus_render_row(int y, uint32_t *out);          // 512 dots, 0xRRGGBB
void macplus_render_row565(int y, uint16_t *out);
const uint8_t *macplus_screen(void);                    // the 1-bit picture (64 bytes a line)

// Media: a floppy image (malloc'd: taken) or a SCSI disk (taken); NULL: in
const char *macplus_insert_floppy(uint8_t *data, size_t size);
const char *macplus_attach_scsi(blkfile *f);
const char *macplus_load_file(const char *path);         // floppy or SCSI, by its size
void macplus_eject_floppy(void);
void macplus_detach_scsi(void);
bool macplus_floppy_loaded(void);
bool macplus_scsi_loaded(void);
bool macplus_drive_led(void);
