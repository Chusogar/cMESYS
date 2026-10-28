// zxspec_core.h - ZX Spectrum 48K / 128K / +3 / Pentagon / Scorpion (C port
// of dsp-cpp spectrum / spectrum_128k / spectrum_3 / zx_clone). Shared ULA,
// tape (.tap/.tzx), SNA snapshots; 128K-class machines add AY + memory banking.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ZX_W            320     /* cropped 352x296 -> centre 320x240 */
#define ZX_H            240
#define ZX_CLOCK        3500000
#define ZX_FPS          50.08
#define ZX_AUDIO_RATE   22050
#define ZX_AUDIO_MAX    512
#define ZX_T_LINE       224
#define ZX_LINES        312
#define ZX_T_FRAME      (ZX_T_LINE * ZX_LINES)

typedef enum {
    ZX_SPEC48 = 0,
    ZX_SPEC128,
    ZX_PLUS3,
    ZX_PENTAGON,
    ZX_SCORPION
} zx_model_t;

#define ZX_KEY(row, bit) (((row) << 3) | (bit))
enum {
    ZXK_CAPS = ZX_KEY(0, 0), ZXK_Z = ZX_KEY(0, 1), ZXK_X = ZX_KEY(0, 2), ZXK_C = ZX_KEY(0, 3), ZXK_V = ZX_KEY(0, 4),
    ZXK_A = ZX_KEY(1, 0), ZXK_S = ZX_KEY(1, 1), ZXK_D = ZX_KEY(1, 2), ZXK_F = ZX_KEY(1, 3), ZXK_G = ZX_KEY(1, 4),
    ZXK_Q = ZX_KEY(2, 0), ZXK_W = ZX_KEY(2, 1), ZXK_E = ZX_KEY(2, 2), ZXK_R = ZX_KEY(2, 3), ZXK_T = ZX_KEY(2, 4),
    ZXK_1 = ZX_KEY(3, 0), ZXK_2 = ZX_KEY(3, 1), ZXK_3 = ZX_KEY(3, 2), ZXK_4 = ZX_KEY(3, 3), ZXK_5 = ZX_KEY(3, 4),
    ZXK_0 = ZX_KEY(4, 0), ZXK_9 = ZX_KEY(4, 1), ZXK_8 = ZX_KEY(4, 2), ZXK_7 = ZX_KEY(4, 3), ZXK_6 = ZX_KEY(4, 4),
    ZXK_P = ZX_KEY(5, 0), ZXK_O = ZX_KEY(5, 1), ZXK_I = ZX_KEY(5, 2), ZXK_U = ZX_KEY(5, 3), ZXK_Y = ZX_KEY(5, 4),
    ZXK_ENTER = ZX_KEY(6, 0), ZXK_L = ZX_KEY(6, 1), ZXK_K = ZX_KEY(6, 2), ZXK_J = ZX_KEY(6, 3), ZXK_H = ZX_KEY(6, 4),
    ZXK_SPACE = ZX_KEY(7, 0), ZXK_SYMB = ZX_KEY(7, 1), ZXK_M = ZX_KEY(7, 2), ZXK_N = ZX_KEY(7, 3), ZXK_B = ZX_KEY(7, 4),
};

typedef struct zxspec zxspec;

void zxspec_init(zxspec *m, zx_model_t model);
/* rom48: 16 KB (required). rom128: 32 KB (128K/Pentagon/Scorpion) or 64 KB (+3); may be NULL for 48K. */
void zxspec_set_rom(zxspec *m, const uint8_t *rom48, const uint8_t *rom128, size_t rom128_bytes);
void zxspec_reset(zxspec *m);
int zxspec_run_frame(zxspec *m, const int16_t **audio);

void zxspec_key(zxspec *m, int code, bool down);
void zxspec_keys_clear(zxspec *m);
void zxspec_joy(zxspec *m, uint8_t kempston);

bool zxspec_load_file(zxspec *m, const char *path);
bool zxspec_load_mem(zxspec *m, const uint8_t *data, size_t n, const char *name_hint);
void zxspec_tape_play(zxspec *m);
void zxspec_tape_stop(zxspec *m);
bool zxspec_tape_loaded(const zxspec *m);
bool zxspec_tape_playing(const zxspec *m);

/* Fill ZX_W*ZX_H ARGB8888 (0x00RRGGBB). */
void zxspec_render(const zxspec *m, uint32_t *rgb);
/* One RGB565 row for CYD / Pico (y in 0..ZX_H-1). */
void zxspec_render_row565(const zxspec *m, int y, uint16_t *out320);

const char *zxspec_title(zx_model_t model);
zx_model_t zxspec_model(const zxspec *m);

zxspec *zxspec_create(zx_model_t model);
void zxspec_destroy(zxspec *m);
