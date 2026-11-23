// sw_core.h - Atari Star Wars (1983) and The Empire Strikes Back (1985), one
// core for both: C port of dsp-cpp's drivers/arcade/starwars (after MAME's).
//
// Two 6809s at 1.512 MHz (main and sound), the AVG vector generator, the
// mathbox (matrix processor and divider), four POKEYs, the TMS5220 speech
// chip and the 6532 RIOT; ESB adds a ROM bank behind an Atari slapstic.
// The flight yoke is two analog axes (0..255, 0x80 the centre).
//
// The picture: 8 bits a pixel (colour << 5 | brightness, sw_palette()), the
// vectors drawn as the AVG runs over the last list dimmed (a vector monitor's
// phosphor). The AVG's 250 x 280 screen fills it as the cabinet's 4:3 monitor
// does, the lines one pixel wide from the beam's exact positions (the vector
// font keeps its letters' shapes at any size): 800 x 600 by default, 320 x 240
// on the boards.
#ifndef SW_CORE_H
#define SW_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SW_W 800             // the default picture
#define SW_H 600
#define SW_FPS (12096000.0 / 4096.0 / 12.0 / 6.0)    // 41.0 Hz
#define SW_AUDIO_RATE 44100
#define SW_AUDIO_MAX 2048

enum { SW_GAME_STARWARS = 0, SW_GAME_ESB = 1 };

typedef struct {
    bool coin1, coin2, start;
    bool fire1, fire2, fire3;   // the yoke's buttons (both triggers / thumbs)
    uint8_t x, y;               // the yoke: 0 left / up .. 255, 0x80 the centre (the
                                // crosshair's), scaled to the cabinet's pots
} sw_inputs;

// The game's ROMs (rom_data.h: sw_roms_get) are read. pic_w / pic_h: the
// picture's size (0: SW_W x SW_H; 4:3 keeps the monitor's shape). false: no
// ROMs or no memory.
bool sw_init(int game, int pic_w, int pic_h);
void sw_reset(void);
int sw_game(void);
const char *sw_title(void);
// One frame (6 IRQs); the samples into audio (at most SW_AUDIO_MAX); their count
int sw_run_frame(int16_t *audio);
void sw_set_inputs(const sw_inputs *in);
// Below SW_AUDIO_RATE (the boards): the POKEYs run in batches (and frozen
// while muted), the sound is filtered in float, and the CPUs' idle loops are
// skipped (the same results as running them)
void sw_set_audio_rate(int rate);
int sw_audio_rate(void);
void sw_set_audio_enabled(bool on);
void sw_set_draw_enabled(bool on);
// Only every n-th vector list run and drawn (the boards: 2, half of the 41
// pictures a second, as many as their screens can show); 1: all
void sw_set_draw_every(int n);
int sw_width(void);
int sw_height(void);
const uint32_t *sw_framebuffer(void);     // the picture in 0xAARRGGBB
const uint8_t *sw_picture(void);          // the picture (8 bits a pixel)
const uint32_t *sw_palette(void);         // its 256 colours (0xAARRGGBB)
#endif
