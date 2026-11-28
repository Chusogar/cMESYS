// dspc.h - the common interface of the computers migrated from dsp-cpp with
// its own Machine API (ZX80 / ZX81, VIC-20, Plus/4 / C16, C128, Atari 800,
// PCW): their cores take dsp-cpp's inputs (a PC keyboard of DK_* keys, held
// or not, and two pads), give an ARGB picture and mono sound, and load their
// ROMs themselves from storage (main/rom/romfs.h). One frontend per platform
// serves them all: dspc_sdl.c (desktop, web), dspc_pico.c (RP2350) and the
// Android port's m_dspc.c + Dspc.java. DSPC_MODEL picks the model of a core
// that has several (the GAME id of each in the Makefile).
#ifndef DSPC_H
#define DSPC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// dsp-cpp's Key, in its order
enum {
    DK_A, DK_B, DK_C, DK_D, DK_E, DK_F, DK_G, DK_H, DK_I, DK_J, DK_K, DK_L, DK_M,
    DK_N, DK_O, DK_P, DK_Q, DK_R, DK_S, DK_T, DK_U, DK_V, DK_W, DK_X, DK_Y, DK_Z,
    DK_0, DK_1, DK_2, DK_3, DK_4, DK_5, DK_6, DK_7, DK_8, DK_9,
    DK_ENTER, DK_SPACE, DK_LSHIFT, DK_RSHIFT, DK_LCTRL, DK_RCTRL, DK_BACKSPACE,
    DK_UP, DK_DOWN, DK_LEFT, DK_RIGHT, DK_COMMA, DK_PERIOD, DK_SEMICOLON, DK_QUOTE, DK_SLASH, DK_MINUS,
    DK_ESCAPE, DK_TAB, DK_CAPSLOCK, DK_HOME, DK_CBM, DK_EQUALS, DK_PLUS, DK_ASTERISK, DK_AT,
    DK_F1, DK_F2, DK_F3, DK_F4, DK_F5, DK_F6, DK_F7, DK_F8, DK_F9, DK_F10, DK_F11, DK_F12,
    DK_BACKSLASH, DK_BACKQUOTE, DK_DELETE, DK_LGUI, DK_RGUI, DK_RALT,
    DK_COUNT
};

typedef struct {
    bool up, down, left, right, b1, b2, b3, b4, start, select;
} dspc_pad;

typedef struct {
    dspc_pad p1, p2;
    bool keys[DK_COUNT];
} dspc_inputs;

typedef struct {
    const char *id;             // the GAME id ("zx81")
    const char *title;          // "Sinclair ZX81"
    int w, h;                   // the picture
    int line_scale;             // rows per picture line shown (2: lines twice)
    double fps;
    int rate;                   // sound, mono
    const char *media_label;    // "P" (the extensions, for the menus)
    const char *const *exts;    // the media files' extensions (".p", NULL-ended)
    const char *filter;         // zenity patterns ("*.p *.P")
    const char *accept;         // the web page's (".p")
} dspc_info;

extern const dspc_info dspc_machine;

// Loads the ROMs (romfs) and starts the machine; false: missing (romfs_error())
// or out of memory.
bool dspc_init(void);
void dspc_reset(void);
void dspc_set_inputs(const dspc_inputs *in);
int dspc_run_frame(const int16_t **audio);      // mono samples of the frame
const uint32_t *dspc_frame(void);               // w x h, 0xAARRGGBB
// A media file (copied); NULL: in, else why not.
const char *dspc_load(const char *name, const uint8_t *data, size_t size);

#endif
