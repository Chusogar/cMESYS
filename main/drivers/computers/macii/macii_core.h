// macii_core.h - the Macintosh II: a C11 migration of dsp-cpp's
// drivers/computers/macii (MAME's macii / nubus_48gc). 68020 at 15.6672 MHz
// (Musashi 4, cpu/musashi: no FPU, the 24-bit translation done by the
// HMMU), 8 MB of RAM, ROM rev. A or B, the Macintosh Display Card 8*24
// (JMFB) in NuBus slot 9 on a 640x480 monitor (1 to 24 bits), two VIAs
// (machine/via6522), the Apple Sound Chip (sound/asc), the NCR 5380 with a
// SCSI hard disk (machine/ncr5380, written in place), the IWM, the SCC,
// the 343-0042-B clock with 256 bytes of PRAM, and the ADB transceiver on
// VIA1's shift register (keyboard and mouse, at the protocol's level).
//
// One machine (Musashi is one CPU): the functions take no handle.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "blkfile.h"

#define MACII_W 640
#define MACII_H 480
#define MACII_CLOCK 15667200
#define MACII_FPS ((double)MACII_CLOCK / 235008.0)
#define MACII_AUDIO_RATE 22257
#define MACII_AUDIO_MAX 1024

#define MACII_MEDIA_EXTS { ".img", ".hda", ".hd", ".dsk", ".vhd", ".image", NULL }
#define MACII_MEDIA_LABEL "IMG HDA HD DSK"

// Keys: Apple Desktop Bus key codes
enum {
    ADBK_A = 0x00, ADBK_S = 0x01, ADBK_D = 0x02, ADBK_F = 0x03, ADBK_H = 0x04, ADBK_G = 0x05, ADBK_Z = 0x06,
    ADBK_X = 0x07, ADBK_C = 0x08, ADBK_V = 0x09, ADBK_B = 0x0b, ADBK_Q = 0x0c, ADBK_W = 0x0d, ADBK_E = 0x0e,
    ADBK_R = 0x0f, ADBK_Y = 0x10, ADBK_T = 0x11, ADBK_1 = 0x12, ADBK_2 = 0x13, ADBK_3 = 0x14, ADBK_4 = 0x15,
    ADBK_6 = 0x16, ADBK_5 = 0x17, ADBK_EQUALS = 0x18, ADBK_9 = 0x19, ADBK_7 = 0x1a, ADBK_MINUS = 0x1b,
    ADBK_8 = 0x1c, ADBK_0 = 0x1d, ADBK_RBRACKET = 0x1e, ADBK_O = 0x1f, ADBK_U = 0x20, ADBK_LBRACKET = 0x21,
    ADBK_I = 0x22, ADBK_P = 0x23, ADBK_RETURN = 0x24, ADBK_L = 0x25, ADBK_J = 0x26, ADBK_QUOTE = 0x27,
    ADBK_K = 0x28, ADBK_SEMICOLON = 0x29, ADBK_BACKSLASH = 0x2a, ADBK_COMMA = 0x2b, ADBK_SLASH = 0x2c,
    ADBK_N = 0x2d, ADBK_M = 0x2e, ADBK_PERIOD = 0x2f, ADBK_TAB = 0x30, ADBK_SPACE = 0x31, ADBK_GRAVE = 0x32,
    ADBK_DELETE = 0x33, ADBK_ESC = 0x35, ADBK_CONTROL = 0x36, ADBK_COMMAND = 0x37, ADBK_SHIFT = 0x38,
    ADBK_CAPS = 0x39, ADBK_OPTION = 0x3a, ADBK_LEFT = 0x3b, ADBK_RIGHT = 0x3c, ADBK_DOWN = 0x3d, ADBK_UP = 0x3e,
    ADBK_KP_PERIOD = 0x41, ADBK_KP_MUL = 0x43, ADBK_KP_PLUS = 0x45, ADBK_KP_CLEAR = 0x47, ADBK_KP_DIV = 0x4b,
    ADBK_KP_ENTER = 0x4c, ADBK_KP_MINUS = 0x4e, ADBK_KP_EQUALS = 0x51, ADBK_KP0 = 0x52, ADBK_KP1 = 0x53,
    ADBK_KP2 = 0x54, ADBK_KP3 = 0x55, ADBK_KP4 = 0x56, ADBK_KP5 = 0x57, ADBK_KP6 = 0x58, ADBK_KP7 = 0x59,
    ADBK_KP8 = 0x5b, ADBK_KP9 = 0x5c,
    ADBK_F5 = 0x60, ADBK_F6 = 0x61, ADBK_F7 = 0x62, ADBK_F3 = 0x63, ADBK_F8 = 0x64, ADBK_F9 = 0x65,
    ADBK_F11 = 0x67, ADBK_F13 = 0x69, ADBK_F14 = 0x6b, ADBK_F10 = 0x6d, ADBK_F12 = 0x6f, ADBK_F15 = 0x71,
    ADBK_HELP = 0x72, ADBK_HOME = 0x73, ADBK_PGUP = 0x74, ADBK_FWD_DEL = 0x75, ADBK_F4 = 0x76, ADBK_END = 0x77,
    ADBK_F2 = 0x78, ADBK_PGDN = 0x79, ADBK_F1 = 0x7a, ADBK_POWER = 0x7f,
    ADBK_COUNT = 0x80
};

// The ROMs (read in place): the Mac II's 256 KB and the Display Card's 32 KB.
// ram_mb: 8 (or 4).
bool macii_init(const uint8_t *rom, const uint8_t *card_rom, int ram_mb);
void macii_reset(void);
int macii_run_frame(const int16_t **audio);
void macii_set_turbo(bool on);

void macii_key(int adb_code, bool down);
void macii_keys_clear(void);
void macii_pointer(int x, int y, bool button);
void macii_mouse(int dx, int dy, bool button);

void macii_render_row(int y, uint32_t *out);     // 640 dots, 0xRRGGBB
void macii_render_row565(int y, uint16_t *out);

const char *macii_attach_scsi(blkfile *f);        // NULL: in
const char *macii_load_file(const char *path);
void macii_detach_scsi(void);
bool macii_scsi_loaded(void);
bool macii_scsi_busy(void);
