// pico_port.h - common code of the Waveshare RP2040-PiZero ports (RP2040,
// 264 KB SRAM, 16 MB flash, HDMI, USB-C host): the counterpart of
// ports/sdl2/sdl_port.h and ports/esp32-cyd/cyd.h.
//
//   Core 0 : the emulation (the game's main loop), USB host (TinyUSB), sound
//   Core 1 : HDMI 640x480 60 Hz (PicoDVI) from a 320x240 picture. There is no
//            frame buffer (no room next to a game): core 1 asks the game for
//            its picture a band of rows at a time (render callback) while the
//            lines go out, and TMDS-encodes them. A band that is not ready in
//            time shows as a red line (the emulation is never held back).
//
// Sound: 10-bit PWM on GPIO4 (add an RC low-pass filter), mono.
// Input: a USB keyboard and/or a HID gamepad on the native USB-C port (power
//        the board and the device through an OTG / Y cable).
// Serial log: UART0, TX GPIO0, 115200 baud (fps every 5 seconds).
#ifndef PICO_PORT_H
#define PICO_PORT_H
#include <stdint.h>
#include <stdbool.h>

#define PICO_W 320
#define PICO_H 240
#ifndef PICO_BAND
#define PICO_BAND 4                 // rows core 1 asks for at a time (set by the build)
#endif

// Controls (bits of pico_port_keys()), as sdl_port.h. Keyboard:
//   5 / 6 coin 1/2, 1 / 2 start 1/2, cursors, Z or Left Ctrl = B1,
//   X or Left Alt = B2, C or Space = B3, F3 reset.
// Gamepad: d-pad / stick, A = B1, B = B2, X = B3, SELECT = coin 1,
//   START = start 1 (SELECT + START = reset).
enum {
    SP_COIN1 = 1 << 0, SP_COIN2 = 1 << 1, SP_START1 = 1 << 2, SP_START2 = 1 << 3,
    SP_UP = 1 << 4, SP_DOWN = 1 << 5, SP_LEFT = 1 << 6, SP_RIGHT = 1 << 7,
    SP_B1 = 1 << 8, SP_B2 = 1 << 9, SP_B3 = 1 << 10,
};

// Fills rows y .. y+n-1 of the 320x240 picture (RGB565, 320 pixels a row,
// rows one after another). Called on core 1, while the game runs on core 0:
// it must only read the game's state (a picture taken half-way through an
// update just tears, as on the CYD).
typedef void (*pico_render_fn)(void *user, int y, int n, uint16_t *rgb565);

typedef struct {
    const char *title;       // for the log
    pico_render_fn render;
    void *user;
    int audio_rate;          // mono int16 samples per second (0 = no sound)
    double fps;              // frame rate to pace at when there is no sound
    bool computer;           // keyboard machines: F3 is not reset
} pico_port_cfg;

// Clocks (252 MHz for HDMI), UART log, USB host, HDMI on core 1, sound.
void pico_port_open(const pico_port_cfg *cfg);
// Runs fn, the game's loop (it never returns).
void pico_port_run(void (*fn)(void)) __attribute__((noreturn));
// USB (call once a frame; the waits below call it too). Returns true when
// reset was asked for (F3, or SELECT + START).
bool pico_port_poll(void);
uint32_t pico_port_keys(void);              // SP_* bits held
// Keyboard state by HID usage (0x04 = A ... see the USB HID usage tables),
// modifiers as 0xE0-0xE7, and the next usage pressed (-1: none).
bool pico_port_key(int usage);
int pico_port_next_press(void);
// Queues the frame's sound; waits while the queue is full, which paces the
// emulation at the real speed (when there is no sound: at cfg.fps).
void pico_port_audio(const int16_t *samples, int n);
void pico_port_frame_done(void);            // counts frames, paces without sound, logs fps
// A colour in the picture's format (for palettes)
static inline uint16_t pico_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}
// Fills a row with one colour
static inline void pico_fill(uint16_t *row, int n, uint16_t c) { for (int i = 0; i < n; i++) row[i] = c; }

#endif
