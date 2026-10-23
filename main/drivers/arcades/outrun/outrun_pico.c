// outrun_pico.c - OutRun (Sega, 1986) on the Waveshare RP2040-PiZero (HDMI).
// Same core as the CYD and the desktop: both 68000s and the sound board run
// on core 0, core 1 draws the 320x224 picture from the core's RAM.
//
// Keys: 5 coin, 1 start, Left / Right steer, Up or Z gas, Down or X brake,
// C or Space gear, F3 reset; USB pad: A gas, B brake, X gear, SELECT coin,
// START start. (No high-score memory: the board has no storage in use.)
#include <string.h>
#include "pico_port.h"
#include "or_core.h"
#include "rom_data.h"

#define AUDIO_RATE 22050
#define Y0 ((PICO_H - OR_SCREEN_H) / 2)

static or_core core;
static or_video vframe;
static int16_t abuf[1024];
static uint16_t cband[OR_SCREEN_W * PICO_BAND], sband[OR_SCREEN_W * PICO_BAND];

static void on_frame(void *user, or_core *c)
{
    (void)user;
    or_get_video(c, &vframe);
}

// (no room for a colour table: made from the palette RAM pixel by pixel)
static inline uint16_t colour(int index)
{
    if (index >= OR_BLACK) return 0;
    uint32_t rgb = or_palette_rgb(core.paletteram, index);
    return pico_rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    int top = y - Y0, i = 0;
    for (; i < n && top + i < 0; i++) pico_fill(out + i * PICO_W, PICO_W, 0);
    int first = top + i, rows = n - i;
    if (first + rows > OR_SCREEN_H) rows = OR_SCREEN_H - first;
    if (rows > 0) {
        or_render(&vframe, &core.roms, first, first + rows, cband, sband);
        for (int r = 0; r < rows; r++, i++)
            for (int x = 0; x < OR_SCREEN_W; x++) out[i * PICO_W + x] = colour(cband[r * OR_SCREEN_W + x]);
    }
    for (; i < n; i++) pico_fill(out + i * PICO_W, PICO_W, 0);
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) or_reset(&core);
        uint32_t k = pico_port_keys();
        or_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & SP_COIN1;
        in.coin2 = k & SP_COIN2;
        in.start = k & SP_START1;
        in.steer = (k & SP_LEFT) ? 0x20 : (k & SP_RIGHT) ? 0xe0 : 0x80;
        in.gas = (k & (SP_UP | SP_B1)) ? 0xff : 0;
        in.brake = (k & (SP_DOWN | SP_B2)) ? 0xff : 0;
        in.gear_toggle = k & SP_B3;
        or_set_inputs(&core, &in);
        core.sound->audio_n = 0;
        or_run_frame(&core);
        pico_port_audio(abuf, core.sound->audio_n);
        pico_port_frame_done();
    }
}

int main(void)
{
    or_roms roms;
    or_rom_data(&roms);
    or_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;
    core.on_frame = on_frame;
    core.sound->audio = abuf;
    core.sound->audio_cap = 1024;
    or_get_video(&core, &vframe);
    pico_port_cfg cfg = { "OutRun", render, NULL, AUDIO_RATE, OR_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
