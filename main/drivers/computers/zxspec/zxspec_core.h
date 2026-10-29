// zxspec_core.h - ZX Spectrum 48K / 128K / +3 / Pentagon / Pentagon 1024 /
// Scorpion ZS-256: a C11 migration of dsp-cpp's drivers (spectrum,
// spectrum_128k, spectrum_3, zx_clone) with their Z80 core, ULA timing
// (contention, floating bus, per-T-state border, 4 T-state paper latches),
// ULA+, beeper and AY sound, and their media: tapes (.tap .tzx .cdt .csw
// .pzx), snapshots (.sna .z80), recordings (.rzx), TR-DOS disks (.trd .scl,
// Beta 128 on the clones), +3 disks (.dsk / extended .dsk) and Interface 2
// cartridges (.rom .bin .if2, 128K / +3).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define ZX_SMALL 1              /* the boards: 320x240 picture, 22 kHz sound */
#endif

/* The whole picture (dsp-cpp: 48 + 256 + 48 x 48 + 192 + 40) and the centre
 * of it the boards show (dsp-cpp set_picture_limit). */
#define ZX_FULL_W       352
#define ZX_FULL_H       280
#define ZX_W            320
#define ZX_H            240
#ifdef ZX_SMALL
#define ZX_PIC_W        ZX_W
#define ZX_PIC_H        ZX_H
#define ZX_AUDIO_RATE   22050
#else
#define ZX_PIC_W        ZX_FULL_W
#define ZX_PIC_H        ZX_FULL_H
#define ZX_AUDIO_RATE   44100   /* dsp-cpp's rate */
#endif
#define ZX_AUDIO_MAX    1024    /* samples a frame at most */
#define ZX_FPS          50.08   /* 48K; zxspec_fps() has each model's */

typedef enum {
    ZX_SPEC48 = 0,
    ZX_SPEC128,
    ZX_PLUS3,
    ZX_PENTAGON,
    ZX_PENTAGON1024,
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

/* The file types the media menus offer (every model; the core says which
 * ones a model cannot take). */
#define ZX_MEDIA_EXTS { ".tap", ".tzx", ".cdt", ".csw", ".pzx", ".sna", ".z80", ".rzx", \
                        ".trd", ".scl", ".dsk", ".rom", ".if2", NULL }
#define ZX_MEDIA_LABEL "TAP TZX CDT CSW PZX SNA Z80 RZX TRD SCL DSK ROM IF2"

typedef struct zxspec zxspec;

zxspec *zxspec_create(zx_model_t model);
void zxspec_destroy(zxspec *m);
/* rom48: 16 KB (48K). rom128: 32 KB (128K) or 64 KB (+3, Pentagon,
 * Pentagon 1024, Scorpion: 128K BASIC, 48K BASIC, service / Gluk, TR-DOS). */
void zxspec_set_rom(zxspec *m, const uint8_t *rom48, const uint8_t *rom128, size_t rom128_bytes);
void zxspec_reset(zxspec *m);
/* One frame; *audio gets its samples at ZX_AUDIO_RATE (mono). */
int zxspec_run_frame(zxspec *m, const int16_t **audio);

void zxspec_key(zxspec *m, int code, bool down);
void zxspec_keys_clear(zxspec *m);
void zxspec_joy(zxspec *m, uint8_t kempston);   /* bits: 0 right 1 left 2 down 3 up 4 fire */
void zxspec_magic(zxspec *m);                   /* Pentagon / Scorpion magic button (NMI) */

/* Media: by the name's extension, or by content when it has none. */
bool zxspec_load_file(zxspec *m, const char *path);
bool zxspec_load_mem(zxspec *m, const uint8_t *data, size_t n, const char *name_hint);
/* The same, keeping `data` (malloc'd) when it can instead of copying it
 * (tapes, +3 disks: saves the memory of a second copy on the boards). */
bool zxspec_load_take(zxspec *m, uint8_t *data, size_t n, const char *name_hint);
/* A .trd disk image a piece at a time (Pentagon / Scorpion), for the boards:
 * a 640 KB image does not fit their memory whole, its used tracks do. */
bool zxspec_trd_begin(zxspec *m, size_t size);
bool zxspec_trd_data(zxspec *m, const uint8_t *data, size_t n);
bool zxspec_trd_end(zxspec *m);
const char *zxspec_last_error(const zxspec *m);
void zxspec_tape_play(zxspec *m);
void zxspec_tape_stop(zxspec *m);
void zxspec_tape_toggle(zxspec *m);             /* play / pause */
bool zxspec_tape_loaded(const zxspec *m);
bool zxspec_tape_playing(const zxspec *m);
bool zxspec_rzx_playing(const zxspec *m);

/* The picture: zxspec_width() x zxspec_height() (ZX_PIC_W x ZX_PIC_H). */
int zxspec_width(const zxspec *m);
int zxspec_height(const zxspec *m);
void zxspec_render(const zxspec *m, uint32_t *rgb);             /* 0x00RRGGBB */
/* The display's RGB565 for zxspec_render_row565 (the CYD's rgb565(): byte
 * swapped and BGR / inverted as its settings say); NULL = plain RGB565. */
void zxspec_set_rgb565(zxspec *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));
/* One RGB565 row of the centre 320x240 (y in 0..ZX_H-1), for CYD / Pico. */
void zxspec_render_row565(const zxspec *m, int y, uint16_t *out320);

const char *zxspec_title(zx_model_t model);
zx_model_t zxspec_model(const zxspec *m);
double zxspec_fps(const zxspec *m);
int zxspec_ram_kb(const zxspec *m);
