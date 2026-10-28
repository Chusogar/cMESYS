// sdl_port.h - common desktop (SDL2) frontend of the arcade drivers: window,
// audio queue, keyboard, frame pacing, screenshots for tests.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Controls (bits of sdl_port_keys()). Default keys:
//   5 / 6 coin 1/2, 1 / 2 start 1/2, cursors, Z or Left Ctrl = B1,
//   X or Left Alt = B2, C or Space = B3, F3 reset, P pause, ESC quit.
enum {
    SP_COIN1 = 1 << 0, SP_COIN2 = 1 << 1, SP_START1 = 1 << 2, SP_START2 = 1 << 3,
    SP_UP = 1 << 4, SP_DOWN = 1 << 5, SP_LEFT = 1 << 6, SP_RIGHT = 1 << 7,
    SP_B1 = 1 << 8, SP_B2 = 1 << 9, SP_B3 = 1 << 10,
};

typedef struct {
    const char *title;
    int width, height;        // picture size
    int scale;                // window = picture x scale
    int audio_rate;           // mono int16 samples per second (0 = no audio)
    double fps;               // frame rate to pace at
    bool computer;            // computers: every key goes to the machine (no ESC
                              // quit, P pause or F3 reset), see sdl_port_key()
} sdl_port_cfg;

bool sdl_port_open(const sdl_port_cfg *cfg);
void sdl_port_close(void);
// Handles the events; false when the user quits. Sets *reset on F3.
bool sdl_port_poll(bool *reset);
uint32_t sdl_port_keys(void);            // SP_* bits held
bool sdl_port_paused(void);
// Computer mode: SDL scancode held / next key pressed (-1: none) / title.
bool sdl_port_key(int scancode);
int sdl_port_next_press(void);
void sdl_port_set_title(const char *title);
void sdl_port_audio(const int16_t *samples, int n);
// Shows a width x height 0xRRGGBB picture and waits for the next frame time.
void sdl_port_present(const uint32_t *rgb);
// Drag-and-drop: next dropped file path (malloc'd; caller frees), or NULL.
char *sdl_port_next_drop(void);
void sdl_port_queue_drop(const char *path);

// Tests: SDL_PORT_FRAMES=n quits after n frames, SDL_PORT_SHOT=k saves frame
// k as shot.ppm (with SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy).
