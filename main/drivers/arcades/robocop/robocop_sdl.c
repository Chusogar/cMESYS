// robocop_sdl.c - RoboCop (Data East, 1988) on the desktop (SDL2), same core
// as the CYD. ROMs compiled in (rom_data.c, made by tools/gen_roms_robocop.py).
//
// Keys: 5 / 6 coin, 1 / 2 start, cursors move, Z or Left Ctrl attack,
// X or Left Alt jump, F3 reset, P pause, ESC quit.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "rc_core.h"
#include "rom_data.h"

#define AUDIO_RATE 44100

static rc_core core;
static rc_sprite_list sprites;
static uint16_t lines[RC_SCREEN_W * RC_SCREEN_H];
static uint32_t pixels[RC_SCREEN_W * RC_SCREEN_H];

static void on_frame(void *user, rc_core *c)
{
    (void)user;
    rc_video v;
    rc_get_video(c, &v);
    rc_build_sprites(&v, &sprites);
    for (int y = 0; y < RC_SCREEN_H; y += 16)
        rc_render(&v, &sprites, &c->roms, y, y + 16, lines + y * RC_SCREEN_W, NULL);
    static uint32_t pal[0x400];
    if (c->palette_dirty) {
        c->palette_dirty = false;
        for (int i = 0; i < 0x400; i++) pal[i] = rc_palette_rgb(c->palette, c->palette_ext, i);
    }
    for (int i = 0; i < RC_SCREEN_W * RC_SCREEN_H; i++) pixels[i] = pal[lines[i]];
}

int main(void)
{
    const rc_roms roms = { rc_main_rom, rc_sound_rom, rc_mcu_rom, rc_oki_rom,
                           rc_chars, rc_tiles1, rc_tiles2, rc_sprites };
    rc_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;             // same result, less host time
    core.on_frame = on_frame;
    static int16_t abuf[4096];
    core.sound->audio = abuf;
    core.sound->audio_cap = 4096;

    sdl_port_cfg cfg = {"RoboCop", RC_SCREEN_W, RC_SCREEN_H, 3, AUDIO_RATE, RC_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) rc_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        const uint32_t k = sdl_port_keys();
        rc_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & SP_COIN1;
        in.coin2 = k & SP_COIN2;
        in.start1 = k & SP_START1;
        in.start2 = k & SP_START2;
        in.p[0].up = k & SP_UP;
        in.p[0].down = k & SP_DOWN;
        in.p[0].left = k & SP_LEFT;
        in.p[0].right = k & SP_RIGHT;
        in.p[0].attack = k & SP_B1;
        in.p[0].jump = k & SP_B2;
        rc_set_inputs(&core, &in);
        core.sound->audio_n = 0;
        rc_run_frame(&core);
        sdl_port_audio(abuf, core.sound->audio_n);
        sdl_port_present(pixels);
    }
    sdl_port_close();
    return 0;
}
