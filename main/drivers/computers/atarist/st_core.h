// st_core.h - Atari 1040ST (PAL, colour monitor), ported from dsp-cpp
// (src/drivers/computers/atari_st.cpp, machine/mc68901, machine/st_floppy)
// to C on Musashi.
//
// 68000 at 8 MHz, 1 MB RAM, TOS 1.00 / 1.02 / 1.04 (or a 192 KB EmuTOS) at
// $FC0000, the shifter's three resolutions, the MC68901 MFP, the YM2149 (its
// port A selects the drive and the side), the WD1772 + DMA with two drives
// (.ST, .MSA and Pasti .STX images), the IKBD (keyboard, mouse, joysticks,
// clock) on its 6850 ACIA, and the blitter that TOS 1.04 finds and uses.
#ifndef ST_CORE_H
#define ST_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST_FPS 50.053
#define ST_CLOCK 8010265
#define ST_AUDIO_RATE 44100
#define ST_AUDIO_MAX 1024           // samples per frame at most (~881)
#define ST_TOS_SIZE 0x30000         // 192 KB
#define ST_DRIVES 2
#define ST_MEDIA_EXTS { ".st", ".msa", ".stx", NULL }

// The picture: 640 x 400 (low and medium resolution doubled) or, with
// `half`, 320 x 200 (low as it is, medium and high with pixel pairs mixed)
#define ST_W 640
#define ST_H 400

// Joystick bits (IKBD's): up, down, left, right, fire
enum { ST_JOY_UP = 1, ST_JOY_DOWN = 2, ST_JOY_LEFT = 4, ST_JOY_RIGHT = 8, ST_JOY_FIRE = 0x80 };

// IKBD scan codes (by position on the ST's keyboard)
enum {
    SK_ESC = 0x01, SK_1, SK_2, SK_3, SK_4, SK_5, SK_6, SK_7, SK_8, SK_9, SK_0 = 0x0b,
    SK_MINUS = 0x0c, SK_EQUALS = 0x0d, SK_BACKSPACE = 0x0e, SK_TAB = 0x0f,
    SK_Q = 0x10, SK_W, SK_E, SK_R, SK_T, SK_Y, SK_U, SK_I, SK_O, SK_P = 0x19,
    SK_LBRACKET = 0x1a, SK_RBRACKET = 0x1b, SK_RETURN = 0x1c, SK_CTRL = 0x1d,
    SK_A = 0x1e, SK_S, SK_D, SK_F, SK_G, SK_H, SK_J, SK_K, SK_L = 0x26,
    SK_SEMICOLON = 0x27, SK_QUOTE = 0x28, SK_GRAVE = 0x29, SK_LSHIFT = 0x2a, SK_BACKSLASH = 0x2b,
    SK_Z = 0x2c, SK_X, SK_C, SK_V, SK_B, SK_N, SK_M = 0x32,
    SK_COMMA = 0x33, SK_PERIOD = 0x34, SK_SLASH = 0x35, SK_RSHIFT = 0x36, SK_ALT = 0x38,
    SK_SPACE = 0x39, SK_CAPSLOCK = 0x3a,
    SK_F1 = 0x3b, SK_F2, SK_F3, SK_F4, SK_F5, SK_F6, SK_F7, SK_F8, SK_F9, SK_F10 = 0x44,
    SK_CLRHOME = 0x47, SK_UP = 0x48, SK_KPMINUS = 0x4a, SK_LEFT = 0x4b, SK_RIGHT = 0x4d,
    SK_KPPLUS = 0x4e, SK_DOWN = 0x50, SK_INSERT = 0x52, SK_DELETE = 0x53,
    SK_ISO = 0x60, SK_UNDO = 0x61, SK_HELP = 0x62, SK_KPLPAREN = 0x63, SK_KPRPAREN = 0x64,
    SK_KPSLASH = 0x65, SK_KPSTAR = 0x66, SK_KP7 = 0x67, SK_KP8, SK_KP9, SK_KP4, SK_KP5, SK_KP6,
    SK_KP1 = 0x6d, SK_KP2, SK_KP3, SK_KP0 = 0x70, SK_KPDOT = 0x71, SK_KPENTER = 0x72,
};

// Sets the machine up with a TOS (192 KB, used where it is: it must stay;
// the RAM is allocated here); `half`: the 320 x 200 picture. False: out of
// memory or not a TOS's size.
bool st_init(const uint8_t *tos, uint32_t size, bool half);
void st_reset(void);                    // power on again (the RAM cleared, the disks stay)
// One frame (313 lines); *audio: its samples (mono, ST_AUDIO_RATE).
int st_run_frame(const int16_t **audio);
// The picture (RGB565) and its size
const uint16_t *st_frame(void);
int st_width(void);
int st_height(void);
int st_resolution(void);                // 0 low, 1 medium, 2 high

// Disks: .ST / .MSA / .STX (by the name, or the contents). The image becomes
// the core's (freed on eject); a NULL `why` is fine.
bool st_insert(int drive, uint8_t *data, uint32_t size, const char *name, const char **why);
void st_eject(int drive);
bool st_disk_in(int drive);
int st_free_drive(void);                // the first empty drive (-1: none)
bool st_drive_led(void);

// Keyboard: a key goes down / up (scan code SK_*)
void st_key(int code, bool down);
void st_keys_release(void);
// Mouse: motion in host pixels of the 640 x 400 picture (or of the 320 x 200
// one with `half`), turned into the IKBD's for the resolution; buttons.
void st_mouse(int dx, int dy);
void st_mouse_buttons(bool left, bool right);
// Joysticks: 1 is the game port, 0 the mouse port (ST_JOY_* bits)
void st_joy(int port, uint8_t bits);
// The IKBD's clock (the host's time when it has one)
void st_set_time(int year, int month, int day, int hour, int min, int sec);

// Diagnostics
uint32_t st_pc(void);
uint8_t st_peek(uint32_t a);
#endif
