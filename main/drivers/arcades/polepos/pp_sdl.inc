// pp_sdl.inc - Pole Position / Pole Position II on the desktop (SDL2), the
// same core as the CYD and the RP2040. Included by polepos_sdl.c and
// polepos2_sdl.c (PP_TITLE). ROMs compiled in (rom_data.c).
//
// Keys: 5 coin, Left / Right steer, Up or Z gas, Down or X brake, C or Space
// gear (low / high), F3 reset, P pause, ESC quit. The race starts on its own
// after the coin (as on the cabinet).
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "pp_core.h"
#include "rom_data.h"

#define AUDIO_RATE 44100

static pp_core core;
static uint8_t lines[PP_W * PP_H];
static uint32_t pixels[PP_W * PP_H];

static void on_frame(void *user, pp_core *c)
{
    (void)user;
    pp_render(c, 0, PP_H, lines);
    for (int i = 0; i < PP_W * PP_H; i++) pixels[i] = c->rgb[lines[i] & 127];
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);         // --mute, --sound, --scale N
    pp_roms roms;
    pp_rom_data(&roms);
    static int16_t abuf[4096];
    core.audio = abuf;
    core.audio_cap = 4096;
    pp_init(&core, &roms, AUDIO_RATE);
    core.on_frame = on_frame;

    sdl_port_cfg cfg = {PP_TITLE, PP_W, PP_H, 3, AUDIO_RATE, PP_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) pp_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        uint32_t k = sdl_port_keys();
        pp_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & SP_COIN1;
        in.coin2 = k & SP_COIN2;
        in.left = k & SP_LEFT;
        in.right = k & SP_RIGHT;
        in.gas = k & (SP_UP | SP_B1);
        in.brake = k & (SP_DOWN | SP_B2);
        in.gear = k & SP_B3;
        pp_set_inputs(&core, &in);
        core.audio_n = 0;
        pp_run_frame(&core);
        sdl_port_audio(abuf, core.audio_n);
        sdl_port_present(pixels);
    }
    sdl_port_close();
    return 0;
}
