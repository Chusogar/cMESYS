// cons.h - what every console core of main/drivers/consoles gives the common
// frontends (cons_sdl.c: desktop and web; cons_pico.c: RP2040 / RP2350;
// cons_cyd.c: ESP32 CYD; the Android port's m_cons.c). One console per
// binary: each core defines these functions and its cons_info.
//
// The consoles are C11 migrations of dsp-cpp's drivers/consoles (see each
// core's header). Inputs follow dsp-cpp's MachineInputs: two players with a
// pad, four buttons, START and SELECT, plus a keyboard (dsp-cpp's Key enum)
// for the consoles with a keypad or keys.
#ifndef CONS_H
#define CONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pad bits (cons_pad)
enum {
    CP_UP = 1 << 0, CP_DOWN = 1 << 1, CP_LEFT = 1 << 2, CP_RIGHT = 1 << 3,
    CP_B1 = 1 << 4, CP_B2 = 1 << 5, CP_B3 = 1 << 6, CP_B4 = 1 << 7,
    CP_START = 1 << 8, CP_SELECT = 1 << 9, CP_COIN = 1 << 10,
};

// Keys (cons_key): dsp-cpp's Key enum, in its order
enum {
    CK_A, CK_B, CK_C, CK_D, CK_E, CK_F, CK_G, CK_H, CK_I, CK_J, CK_K, CK_L, CK_M,
    CK_N, CK_O, CK_P, CK_Q, CK_R, CK_S, CK_T, CK_U, CK_V, CK_W, CK_X, CK_Y, CK_Z,
    CK_0, CK_1, CK_2, CK_3, CK_4, CK_5, CK_6, CK_7, CK_8, CK_9,
    CK_ENTER, CK_SPACE, CK_LSHIFT, CK_RSHIFT, CK_LCTRL, CK_RCTRL, CK_BACKSPACE,
    CK_UP, CK_DOWN, CK_LEFT, CK_RIGHT, CK_COMMA, CK_PERIOD, CK_SEMICOLON, CK_QUOTE, CK_SLASH, CK_MINUS,
    CK_ESCAPE, CK_TAB, CK_CAPSLOCK, CK_HOME, CK_CBM, CK_EQUALS, CK_PLUS, CK_ASTERISK, CK_AT,
    CK_F1, CK_F2, CK_F3, CK_F4, CK_F5, CK_F6, CK_F7, CK_F8, CK_F9, CK_F10, CK_F11, CK_F12,
    CK_BACKSLASH, CK_BACKQUOTE, CK_DELETE, CK_LGUI, CK_RGUI, CK_RALT,
    CK_COUNT
};

// cons_info.keys: what the console reads from the keyboard
enum { CONS_NO_KEYS = 0, CONS_KEYPAD = 1, CONS_KEYBOARD = 2 };

typedef struct {
    const char *id;                 // "sg1000"
    const char *title;              // "Sega SG-1000"
    const char *maker;              // "Sega, 1983"
    const char *const *exts;        // media extensions, NULL-terminated (".sg", ...)
    const char *media_label;        // "SG BIN"
    int max_w, max_h;               // the largest picture
    double fps;
    int audio_rate;                 // mono samples a second
    const char *button[4];          // the names of B1..B4 (NULL: not used)
    const char *start, *select;     // START / SELECT's names (NULL: not used)
    int keys;                       // CONS_*
    int players;                    // 1 or 2
    const char *keypad_help;        // what the keys do (NULL: nothing)
} cons_info;

extern const cons_info cons_machine;

// Sets the console up (allocates its memory: false when there is not enough).
bool cons_init(void);
void cons_reset(void);
// A cartridge / disc / tape: the whole file (zips already opened by the
// frontend). The core takes `data` over (malloc'd; freed on eject or the
// next load). `why` (may be NULL): the reason when it fails.
bool cons_load(uint8_t *data, size_t size, const char *name, const char **why);
void cons_eject(void);
bool cons_loaded(void);
// One frame; *audio: its samples (mono, cons_machine.audio_rate).
int cons_run_frame(const int16_t **audio);
// The picture of the last frame: RGB565, w x h, `stride` pixels a row.
const uint16_t *cons_frame(int *w, int *h, int *stride);
void cons_pad(int player, unsigned bits);
void cons_key(int key, bool down);
// Optional: the BIOS the core uses when none is built in (rom_data.c);
// default: none needed / built in.
const char *cons_status(void);      // a line for the title bar (NULL: none)

// Helpers for the cores (cons_util.c)
uint32_t cons_crc32(const uint8_t *p, size_t n);
static inline uint16_t cons_rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
// Opens a zip in memory: the first entry with one of `exts` (or the first
// entry). Returns a malloc'd buffer or NULL (then `data` is not a zip, or
// unreadable).
uint8_t *cons_unzip(const uint8_t *data, size_t size, const char *const *exts, size_t *out, char *name, size_t name_len);
#endif
