// samcoupe_core.h - MGT SAM Coupe (512 KB): C port of dsp-cpp's
// drivers/computers/samcoupe.
//
// A 6 MHz Z80, 384 T-states a line and 312 lines a frame (50.08 Hz), the
// ASIC's paging (LMPR / HMPR / VMPR), the four screen modes, the 16-entry
// CLUT of 128 colours (changed line by line by the line interrupt), the
// border, the 9-column keyboard matrix, the SAA1099 sound, two WD1772 disk
// drives (MGT, SDF, DSK) and tapes (TAP / TZX on the EAR line).
//
// The picture is 768 x 312: 384 T-states a line at two dots each, the 512
// dot paper (256 x 192 at double width in modes 1, 2 and 4) at x 128..639
// and lines 68..259; it is shown with each line twice (4:3).
#ifndef SAMCOUPE_CORE_H
#define SAMCOUPE_CORE_H
#include <stdbool.h>
#include <stdint.h>

#define SAM_W 768
#define SAM_H 312
#define SAM_PAPER_X 128
#define SAM_PAPER_Y 68
#define SAM_CLOCK 6000000
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define SAM_AUDIO_RATE 22050
#else
#define SAM_AUDIO_RATE 44100
#endif
#define SAM_AUDIO_MAX 2048
#define SAM_DISK_EXTS { ".mgt", ".dsk", ".sad", ".sdf", ".img", NULL }
#define SAM_TAPE_EXTS { ".tap", ".tzx", NULL }
#define SAM_MEDIA_EXTS { ".mgt", ".dsk", ".sad", ".sdf", ".img", ".tap", ".tzx", NULL }
#define SAM_MEDIA_LABEL "MGT DSK SDF TAP TZX"

// Keys: their place in the matrix, column * 8 + bit (columns 0-7 are the
// address lines A8..A15 of ports 249 / 254, column 8 the extra row read
// with all of them high)
enum {
    SK_SHIFT = 0x00, SK_Z, SK_X, SK_C, SK_V, SK_F1, SK_F2, SK_F3,
    SK_A = 0x08, SK_S, SK_D, SK_F, SK_G, SK_F4, SK_F5, SK_F6,
    SK_Q = 0x10, SK_W, SK_E, SK_R, SK_T, SK_F7, SK_F8, SK_F9,
    SK_1 = 0x18, SK_2, SK_3, SK_4, SK_5, SK_ESC, SK_TAB, SK_CAPS,
    SK_0 = 0x20, SK_9, SK_8, SK_7, SK_6, SK_MINUS, SK_PLUS, SK_DELETE,
    SK_P = 0x28, SK_O, SK_I, SK_U, SK_Y, SK_EQUALS, SK_QUOTE, SK_F0,
    SK_RETURN = 0x30, SK_L, SK_K, SK_J, SK_H, SK_SEMICOLON, SK_COLON, SK_EDIT,
    SK_SPACE = 0x38, SK_SYMBOL, SK_M, SK_N, SK_B, SK_COMMA, SK_PERIOD, SK_INV,
    SK_CNTRL = 0x40, SK_UP, SK_DOWN, SK_LEFT, SK_RIGHT,
    SK_COUNT = 0x48
};
// Joystick bits: port 0 is read as the keys 6 7 8 9 0, port 1 as 1 2 3 4 5
enum { SAM_JOY_RIGHT = 1, SAM_JOY_LEFT = 2, SAM_JOY_DOWN = 4, SAM_JOY_UP = 8, SAM_JOY_FIRE = 16 };

// The ROM (32 KB, copied); false: out of memory (512 KB + the picture)
bool samcoupe_init(const uint8_t *rom);
void samcoupe_reset(void);
int samcoupe_run_frame(const int16_t **audio);
double samcoupe_fps(void);
void samcoupe_key(int code, bool down);
void samcoupe_keys_clear(void);
void samcoupe_joy(int port, uint8_t bits);
void samcoupe_render_row(int y, uint32_t *out);     // SAM_W dots, 0xRRGGBB
void samcoupe_render_row565(int y, uint16_t *out);
// Disks (drive 0 or 1; copied); NULL: in
const char *samcoupe_insert_disk(int drive, const uint8_t *data, uint32_t size);
void samcoupe_eject_disk(int drive);
bool samcoupe_disk_inserted(int drive);
bool samcoupe_disk_busy(void);                       // sectors read or written in the last frames
bool samcoupe_disk_dirty(int drive);                 // written since it went in
// The disk as an 819200-byte MGT (the plain layouts); false: not one
bool samcoupe_disk_image(int drive, uint8_t *out);
// Tapes (copied; stopped); NULL: in
const char *samcoupe_insert_tape(const uint8_t *data, uint32_t size);
void samcoupe_eject_tape(void);
bool samcoupe_tape_inserted(void);
void samcoupe_tape_play(bool on);                    // (play rewinds when it had ended)
bool samcoupe_tape_playing(void);
// BOOT: presses RETURN and F9 (the ROM's BOOT of drive 1); false: no disk
bool samcoupe_boot(void);
// A disk (drive) or a tape, by the name
const char *samcoupe_load_file(const char *path, int drive);
bool samcoupe_is_tape(const char *name);
#endif
