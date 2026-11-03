// retofinv_pico.c - Return of the Invaders (Taito, 1985) on the Waveshare
// RP2040-PiZero (HDMI). Same core as the CYD and the desktop.
//
// The upright 224x288 picture goes in the middle of the 320x240 screen with
// its rows thinned 6 -> 5. Keys: 5 coin, 1 start, Left / Right, Z or Left
// Ctrl fire, F3 reset; or a USB pad (SELECT coin, START start, A fire).
#include <string.h>
#include "pico_port.h"
#include "rv_core.h"

#define AUDIO_RATE 22050
#define X0 ((PICO_W - RV_W) / 2)

static rv_core core;
static rv_video shown;                   // what core 1 draws (copied at each frame's end)
static uint16_t pal[256];
static int16_t abuf[1024];

static void on_frame(void *user, rv_core *c)
{
    (void)user;
    rv_get_video(c, &shown);
}

// The band's native columns in one call (a call draws every tile and sprite
// clipped to its columns: once a row was 4-8 times the work)
#define RV_BAND_ROWS (PICO_BAND * RV_H / PICO_H + 2)
static uint8_t rows[RV_BAND_ROWS][RV_W];

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    const int s0 = y * RV_H / PICO_H, s1 = (y + n - 1) * RV_H / PICO_H + 1;
    rv_render(&shown, s0, s1, rows[0]);
    for (int i = 0; i < n; i++, out += PICO_W) {
        const uint8_t *row = rows[(y + i) * RV_H / PICO_H - s0];
        pico_fill(out, X0, 0);
        for (int x = 0; x < RV_W; x++) out[X0 + x] = pal[row[x]];
        pico_fill(out + X0 + RV_W, PICO_W - X0 - RV_W, 0);
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) rv_reset(&core);
        uint32_t k = pico_port_keys();
        rv_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & SP_COIN1;
        in.coin2 = k & SP_COIN2;
        in.start1 = k & SP_START1;
        in.start2 = k & SP_START2;
        in.left = k & SP_LEFT;
        in.right = k & SP_RIGHT;
        in.fire = k & SP_B1;
        rv_set_inputs(&core, &in);
        core.audio_n = 0;
        rv_run_frame(&core);
        pico_port_audio(abuf, core.audio_n);
        pico_port_frame_done();
    }
}

int main(void)
{
    uint8_t rgb[256][3];
    rv_palette_rgb(rgb);
    for (int i = 0; i < 256; i++) pal[i] = pico_rgb565(rgb[i][0], rgb[i][1], rgb[i][2]);
    rv_init(&core, AUDIO_RATE);
    core.idle_skip = true;
    core.on_frame = on_frame;
    core.audio = abuf;
    core.audio_cap = 1024;
    rv_get_video(&core, &shown);
    pico_port_cfg cfg = { "Return of the Invaders", render, NULL, AUDIO_RATE, RV_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
