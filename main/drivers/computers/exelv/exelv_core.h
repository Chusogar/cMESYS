// exelv_core.h - Exelvision EXL-100 (1984) and EXELTEL (1986): C port of
// dsp-cpp's drivers/computers/exelv (after MAME's exelv, Raphael Nabet /
// Robbbert).
//
// A custom TMS7020 (EXL-100) or TMS7040 (EXELTEL) at 2.4576 MHz, with LVDP in
// place of SWAP R; the TMS7041 / 7042 I/O CPU with the TMS5220 speech
// synthesizer and the infrared keyboard; the TMS3556 VDP (32 KB of VRAM); the
// cartridge at $0200-$7FFF (on the EXELTEL, paged with the 64 KB system ROM);
// 2 KB of RAM at $C000; the cassette on the main CPU's ports.
//
// The picture is 336 x 252 (320 x 250 and the border), 50 Hz.
#ifndef EXELV_CORE_H
#define EXELV_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXELV_W 336
#define EXELV_H 252
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define EXELV_AUDIO_RATE 22050
#else
#define EXELV_AUDIO_RATE 44100
#endif
#define EXELV_AUDIO_MAX 1024
#define EXELV_CART_EXTS { ".bin", ".rom", ".cart", NULL }
#define EXELV_MEDIA_EXTS { ".bin", ".rom", ".cart", ".k7", ".wav", NULL }
#define EXELV_MEDIA_LABEL "BIN ROM K7 WAV"

enum { EXELV_EXL100 = 0, EXELV_EXELTEL = 1 };

// The keys: the infrared keyboard's channel, row * 8 + column (MAME's X0-X7).
// The labels are the EXL-100's (AZERTY): "A" is the key that types A.
enum {
    EK_Z = 0, EK_UP, EK_RIGHT, EK_DOWN, EK_LEFT, EK_E, EK_SPACE, EK_UNDERSCORE,   // "_ -"
    EK_CTL = 8, EK_LOCK, EK_X, EK_SLASH, EK_ESC, EK_R, EK_QUESTION, EK_FCT,       // "/ :", "? ,"
    EK_TAB = 16, EK_HOME, EK_V, EK_PLUS, EK_1, EK_6, EK_8, EK_0,                  // "+ ="
    EK_A = 24, EK_BACKSPACE, EK_C, EK_PERIOD, EK_2, EK_3, EK_9, EK_HASH,          // ". ;", "# )"
    EK_SHIFT = 32, EK_O, EK_H, EK_LESS, EK_T, EK_M, EK_N, EK_G,                   // "< ["
    EK_S = 40, EK_U, EK_K, EK_GREATER, EK_Y, EK_NONE45, EK_B, EK_D,               // "> ]"
    EK_W = 48, EK_P, EK_J, EK_TILDE, EK_4, EK_7, EK_STAR, EK_F,                   // "~ ^", "* \"
    EK_Q = 56, EK_I, EK_L, EK_ENTER, EK_5, EK_PERCENT,                            // "% @"
    EK_COUNT = 64
};
// The joystick (as the arrows and SPACE)
enum { EXELV_JOY_UP = 1, EXELV_JOY_RIGHT = 2, EXELV_JOY_DOWN = 4, EXELV_JOY_LEFT = 8, EXELV_JOY_FIRE = 16 };

// The ROMs (copied): the main CPU's (2 KB EXL-100 / 4 KB EXELTEL), the I/O
// CPU's (4 KB, or NULL: its first answer is then simulated) and the
// EXELTEL's 64 KB system ROM (or NULL). false: no main ROM or no memory.
bool exelv_init(int model, const uint8_t *main_rom, const uint8_t *sub_rom, const uint8_t *sys_rom);
void exelv_reset(void);
int exelv_run_frame(const int16_t **audio);
double exelv_fps(void);
int exelv_model(void);
void exelv_key(int code, bool down);
void exelv_keys_clear(void);
void exelv_joy(uint8_t bits);
void exelv_render_row(int y, uint32_t *out);        // EXELV_W dots, 0xRRGGBB
void exelv_render_row565(int y, uint16_t *out);
// A cartridge (copied); takes effect at the next reset
const char *exelv_insert_cart(const uint8_t *data, size_t size);
void exelv_eject_cart(void);
bool exelv_cart_inserted(void);
// A cassette (.k7 or .wav, copied). It moves while the ROM reads it; loading
// is fast (the bytes go straight to the ROM's read routine).
const char *exelv_insert_tape(const uint8_t *data, size_t size);
void exelv_eject_tape(void);
bool exelv_tape_inserted(void);
void exelv_tape_play(bool on);
bool exelv_tape_playing(void);
// What a SAVE recorded, once the ROM has left its tape routine for a
// second (the bytes of a .k7, malloc'd: the caller frees; NULL if none)
uint8_t *exelv_take_tape_save(size_t *n);
// A cartridge or a cassette by the name (a .zip: its first file); a
// cartridge resets the machine
const char *exelv_load_file(const char *path);
bool exelv_is_tape(const char *name);
#endif
