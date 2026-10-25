// robocop_pico.c - RoboCop (Data East, 1988) on the Waveshare RP2040-PiZero
// (HDMI). Same core as the CYD and the desktop; the sound board runs with
// the main CPU on core 0, core 1 draws the 256x240 picture from the core's
// RAM band by band.
//
// Keys: 5 coin, 1 start, cursors (up / down aim), Z attack, X jump, F3 reset;
// USB pad: A attack, B jump, SELECT coin, START start.
#include <string.h>
#include "pico_port.h"
#include "rc_core.h"
#include "rom_data.h"

#define AUDIO_RATE 22050
#define X0 ((PICO_W - RC_SCREEN_W) / 2)

static rc_core core;
static rc_sound sound;
static rc_video vframe;
static rc_sprite_list sprites[2];
static rc_sprite_list *volatile shown_sprites = &sprites[0];
static uint16_t pal[0x400];
static int16_t abuf[2048];
static uint16_t cband[RC_SCREEN_W * 16];
static uint8_t pband[RC_SCREEN_W * 16];

static void on_frame(void *user, rc_core *c)
{
    (void)user;
    rc_get_video(c, &vframe);
    rc_sprite_list *back = shown_sprites == &sprites[0] ? &sprites[1] : &sprites[0];
    rc_build_sprites(&vframe, back);
    shown_sprites = back;
    if (c->palette_dirty) {
        c->palette_dirty = false;
        for (int i = 0; i < 0x400; i++) {
            uint32_t rgb = rc_palette_rgb(c->palette, c->palette_ext, i);
            pal[i] = pico_rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
        }
    }
}

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    rc_render(&vframe, shown_sprites, &core.roms, y, y + n, cband, pband);
    for (int i = 0; i < n; i++, out += PICO_W) {
        const uint16_t *src = cband + i * RC_SCREEN_W;
        pico_fill(out, X0, 0);
        for (int x = 0; x < RC_SCREEN_W; x++) out[X0 + x] = pal[src[x]];
        pico_fill(out + X0 + RC_SCREEN_W, PICO_W - X0 - RC_SCREEN_W, 0);
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) rc_reset(&core);
        const uint32_t k = pico_port_keys();
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
        pico_port_audio(abuf, core.sound->audio_n);
        pico_port_frame_done();
    }
}

int main(void)
{
    const rc_roms roms = { rc_main_rom, rc_sound_rom, rc_mcu_rom, rc_oki_rom,
                           rc_chars, rc_tiles1, rc_tiles2, rc_sprites };
    core.sound = &sound;
    rc_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;
    core.on_frame = on_frame;
    core.palette_dirty = true;
    core.sound->audio = abuf;
    core.sound->audio_cap = 2048;
    rc_get_video(&core, &vframe);
    pico_port_cfg cfg = { "RoboCop", render, NULL, AUDIO_RATE, RC_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
