// retofinv_sdl.c - Return of the Invaders (Taito, 1985) on the desktop (SDL2),
// same core as the CYD. ROMs compiled in (rom_data.c, made by
// tools/gen_roms_retofinv.py). Shown upright (224x288, the cabinet's ROT90).
//
// Keys: 5 / 6 coin, 1 / 2 start, Left / Right move, Z or Left Ctrl fire,
// F3 reset, P pause, ESC quit.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "rv_core.h"

#define AUDIO_RATE 44100

static rv_core core;
static uint8_t fb[RV_W * RV_H];
static uint32_t pixels[RV_W * RV_H];
static uint32_t pal[256];

static void on_frame(void *user, rv_core *c)
{
    (void)user;
    rv_video v;
    rv_get_video(c, &v);
    for (int y = 0; y < RV_H; y += 16) rv_render(&v, y, y + 16, fb + y * RV_W);
    for (int i = 0; i < RV_W * RV_H; i++) pixels[i] = pal[fb[i]];
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);         // --mute, --sound, --scale N
    uint8_t rgb[256][3];
    rv_palette_rgb(rgb);
    for (int i = 0; i < 256; i++) pal[i] = (uint32_t)(rgb[i][0] << 16 | rgb[i][1] << 8 | rgb[i][2]);
    rv_init(&core, AUDIO_RATE);
    core.idle_skip = true;             // same result, less host time
    core.on_frame = on_frame;
    static int16_t abuf[4096];
    core.audio = abuf;
    core.audio_cap = 4096;

    sdl_port_cfg cfg = {"Return of the Invaders", RV_W, RV_H, 3, AUDIO_RATE, RV_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) rv_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        uint32_t k = sdl_port_keys();
        rv_inputs in = {0};
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
        sdl_port_audio(abuf, core.audio_n);
        sdl_port_present(pixels);
    }
    sdl_port_close();
    return 0;
}
