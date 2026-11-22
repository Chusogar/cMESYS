// arc.h - what every arcade core migrated with the common frontends gives
// them (arc_sdl.c: desktop, web and Android; arc_pico.c: RP2040 / RP2350;
// arc_cyd.c: ESP32 CYD and TTGO VGA32), as cons.h for the consoles. One game
// per binary: a core with several games builds one of them (ARC_VARIANT,
// arcades.mk) and defines arc_game for it.
//
// The cores are C11 migrations of dsp-cpp's drivers/arcade (see each core's
// header); their inputs follow dsp-cpp's MachineInputs.
#ifndef ARC_H
#define ARC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ARC_VARIANT
#define ARC_VARIANT 0
#endif

// A player's bits (arc_in.p[])
enum {
    AP_UP = 1 << 0, AP_DOWN = 1 << 1, AP_LEFT = 1 << 2, AP_RIGHT = 1 << 3,
    AP_B1 = 1 << 4, AP_B2 = 1 << 5, AP_B3 = 1 << 6, AP_B4 = 1 << 7, AP_START = 1 << 8,
};

typedef struct {
    unsigned p[2];                  // players 1 and 2 (AP_*)
    bool coin1, coin2, service;
} arc_in;

// The joystick (for the Android pad and the help)
enum { ARC_JOY_2WAY = 2, ARC_JOY_4WAY = 4, ARC_JOY_8WAY = 8 };

typedef struct {
    const char *id;                 // "bombjack" (the binary's name)
    const char *title;              // "Bomb Jack"
    const char *maker;              // "Tehkan"
    int year;
    int w, h;                       // the picture as the cabinet shows it (rotated)
    double fps;
    int audio_rate;                 // mono samples a second (0: no sound)
    const char *const *sets;        // MAME set names its ROMs are looked for in
    const char *button[4];          // the names of B1..B4 (NULL: not used)
    int joy;                        // ARC_JOY_*
} arc_info;

extern const arc_info arc_game;

// Loads the ROMs (romfs.h: built-in ones first) and sets the board up; false
// when a ROM is missing (romfs_error() says which) or memory is short.
bool arc_init(void);
void arc_reset(void);
// One frame; *audio: its samples (mono, arc_game.audio_rate).
int arc_run_frame(const int16_t **audio);
// The picture of the last frame: RGB565, arc_game.w x arc_game.h.
const uint16_t *arc_frame(void);
void arc_input(const arc_in *in);
void arc_dip(int bank, uint8_t value);

// ---------------------------------------------------------------------------
// Helpers for the cores (arc_util.c)

// A ROM of a set (dsp-cpp's RomEntry): `name` may list alternatives with
// '|'; a file not found by name is looked for by its CRC.
typedef struct { const char *name; uint32_t size, offset, crc; } arc_rom;
// Each ROM of the list into dst (at its offset; dst_size: dst's size). False
// when one is missing or of the wrong size (romfs_error()).
bool arc_load(const arc_rom *roms, int n, uint8_t *dst, size_t dst_size);
#define ARC_LOAD(list, dst, size) arc_load(list, (int)(sizeof list / sizeof list[0]), dst, size)

// MAME-style graphics layouts (dsp-cpp's GfxLayout), offsets in bits; the
// elements decoded one byte a pixel.
typedef struct {
    int width, height, total, planes;
    int char_increment;
    bool lsb_first, rotate_cw, rotate_ccw;
    int plane_offsets[8];
    const int *x_offsets, *y_offsets;
} arc_layout;
// `total` elements of width x height bytes (malloc'd), NULL: no memory
uint8_t *arc_gfx_decode(const arc_layout *l, const uint8_t *rom, size_t rom_len);
// into an existing set from element `first` on
void arc_gfx_decode_into(const arc_layout *l, const uint8_t *rom, size_t rom_len, uint8_t *set, int first);

// dsp-cpp's compute_resistor_weights / combine_weights
typedef struct { int n; int r[8]; int pulldown, pullup; } arc_resnet;
void arc_resistor_weights(double min_v, double max_v, double scaler, const arc_resnet *nets, int nnets,
                          double out[][8]);
int arc_combine(const double *w, int n, unsigned bits);

static inline uint16_t arc_rgb565(uint32_t argb)
{
    return (uint16_t)(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F));
}
static inline int16_t arc_clamp16(int32_t s) { return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }
#endif
