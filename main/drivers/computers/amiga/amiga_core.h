// amiga_core.h - Commodore Amiga 500 (PAL, OCS), ported from dsp-cpp
// (src/drivers/computers/amiga.cpp, src/machine/amiga_chipset.cpp,
// cia8520.cpp, amiga_adf.cpp) to C on Musashi.
//
// 68000 at 7.09 MHz, 512 KB chip RAM + 512 KB slow RAM ($C00000), Kickstart
// 1.2 / 1.3 (256 KB) or 2.04 / 3.1 (512 KB), Agnus / Denise / Paula (copper,
// blitter, bitplanes with HAM / EHB / dual playfield, sprites, four audio
// channels with the A500's filters), the two 8520 CIAs, two 3.5" drives
// (DF0, DF1) with ADF images, the keyboard on CIA-A's serial port (dsp-cpp
// has none: it is the A500's keyboard processor, with the handshake), a mouse
// in port 0 and a joystick in port 1.
#ifndef AMIGA_CORE_H
#define AMIGA_CORE_H
#include <stdbool.h>
#include <stdint.h>

#define AMIGA_W 320                 // picture: the PAL window, lores pixels
#define AMIGA_H 256
#define AMIGA_FPS 50.0
#define AMIGA_CLOCK 7093790
#define AMIGA_AUDIO_RATE 44100
#define AMIGA_AUDIO_MAX 1024        // samples per frame at most (~883)
#define AMIGA_DRIVES 2
#define AMIGA_MEDIA_EXTS { ".adf", NULL }

// Joystick in port 1
enum { AMIGA_JOY_UP = 1, AMIGA_JOY_DOWN = 2, AMIGA_JOY_LEFT = 4, AMIGA_JOY_RIGHT = 8, AMIGA_JOY_FIRE = 16 };

// Raw key codes of the A500 keyboard (by position; the names are those of
// the US keys, the Spanish keyboard prints others on some of them)
enum {
    AK_GRAVE = 0x00, AK_1 = 0x01, AK_2, AK_3, AK_4, AK_5, AK_6, AK_7, AK_8, AK_9, AK_0 = 0x0A,
    AK_MINUS = 0x0B, AK_EQUALS = 0x0C, AK_BACKSLASH = 0x0D, AK_KP0 = 0x0F,
    AK_Q = 0x10, AK_W, AK_E, AK_R, AK_T, AK_Y, AK_U, AK_I, AK_O, AK_P = 0x19,
    AK_LBRACKET = 0x1A, AK_RBRACKET = 0x1B, AK_KP1 = 0x1D, AK_KP2 = 0x1E, AK_KP3 = 0x1F,
    AK_A = 0x20, AK_S, AK_D, AK_F, AK_G, AK_H, AK_J, AK_K, AK_L = 0x28,
    AK_SEMICOLON = 0x29, AK_QUOTE = 0x2A, AK_HASH = 0x2B, AK_KP4 = 0x2D, AK_KP5 = 0x2E, AK_KP6 = 0x2F,
    AK_LESS = 0x30, AK_Z = 0x31, AK_X, AK_C, AK_V, AK_B, AK_N, AK_M = 0x37,
    AK_COMMA = 0x38, AK_PERIOD = 0x39, AK_SLASH = 0x3A, AK_KPDOT = 0x3C, AK_KP7 = 0x3D, AK_KP8 = 0x3E, AK_KP9 = 0x3F,
    AK_SPACE = 0x40, AK_BACKSPACE = 0x41, AK_TAB = 0x42, AK_KPENTER = 0x43, AK_RETURN = 0x44, AK_ESC = 0x45,
    AK_DEL = 0x46, AK_KPMINUS = 0x4A, AK_UP = 0x4C, AK_DOWN = 0x4D, AK_RIGHT = 0x4E, AK_LEFT = 0x4F,
    AK_F1 = 0x50, AK_F2, AK_F3, AK_F4, AK_F5, AK_F6, AK_F7, AK_F8, AK_F9, AK_F10 = 0x59,
    AK_KPLPAREN = 0x5A, AK_KPRPAREN = 0x5B, AK_KPSLASH = 0x5C, AK_KPSTAR = 0x5D, AK_KPPLUS = 0x5E, AK_HELP = 0x5F,
    AK_LSHIFT = 0x60, AK_RSHIFT = 0x61, AK_CAPSLOCK = 0x62, AK_CTRL = 0x63, AK_LALT = 0x64, AK_RALT = 0x65,
    AK_LAMIGA = 0x66, AK_RAMIGA = 0x67,
};

// Sets the machine up with a Kickstart (256 or 512 KB, used where it is: it
// must stay; the memory is allocated here, 1.2 MB); false: out of memory or
// not a Kickstart's size.
bool amiga_init(const uint8_t *kick, uint32_t size);
void amiga_reset(void);                 // power on again (the memory cleared, the disks stay)
// One frame (313 lines); *audio: its samples (mono, AMIGA_AUDIO_RATE).
int amiga_run_frame(const int16_t **audio);
// The picture, AMIGA_W x AMIGA_H RGB565.
const uint16_t *amiga_frame(void);

// ADF disks (880 KB or other 11-sector images). The image becomes the
// core's (freed on eject); a NULL `why` is fine.
bool amiga_insert(int drive, uint8_t *adf, uint32_t size, const char **why);
void amiga_eject(int drive);
bool amiga_disk_in(int drive);
int amiga_free_drive(void);             // the first empty drive (-1: none)
bool amiga_drive_led(void);             // a motor turning
bool amiga_power_led(void);             // bright (the audio filter on)

// Keyboard: a key goes down / up (raw code); queued and sent as the A500's
// keyboard does, one at a time with the computer's handshake. Caps Lock
// toggles on its down (as the real key, which has a lock). Ctrl + both Amiga
// keys resets.
void amiga_key(int code, bool down);
void amiga_keys_release(void);          // all held keys up
// Mouse in port 0 (counts: one per lores pixel) and its buttons.
void amiga_mouse(int dx, int dy);
void amiga_mouse_buttons(bool left, bool right);
// Joystick in port 1 (AMIGA_JOY_*).
void amiga_joy(uint8_t bits);

// Diagnostics
uint32_t amiga_pc(void);
uint8_t amiga_peek(uint32_t a);
#endif
