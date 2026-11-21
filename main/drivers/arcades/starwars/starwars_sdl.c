// starwars_sdl.c - Star Wars (Atari, 1983) and The Empire Strikes Back
// (Atari, 1985) on the desktop (SDL2), the web and Android: one program for
// both games, the same core (sw_core.c) as the boards.
//
//   starwars [esb]          (or --esb / --starwars: the game to start with)
//
// The flight yoke: the mouse (a click captures it; left / right button fire,
// the middle one lets it go), the cursors (as MAME's keys), or Android's
// yoke on the screen. Z / X / C fire (the yoke's triggers and thumb
// buttons), 5 coin, 1 start, F3 reset, F4 the other game.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "sw_core.h"
#include "rom_data.h"

#define F4 61               // SDL_SCANCODE_F4

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static bool start_game(int game)
{
    if (!sw_init(game, 0, 0)) return false;
    sdl_port_set_title(sw_title());
    return true;
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);         // --mute, --sound, --scale N
    int game = SW_GAME_STARWARS;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "esb") || !strcmp(argv[i], "--esb")) game = SW_GAME_ESB;
        else if (!strcmp(argv[i], "starwars") || !strcmp(argv[i], "--starwars")) game = SW_GAME_STARWARS;
    }
    static int16_t audio[SW_AUDIO_MAX];
    static uint32_t rgb[SW_W * SW_H];

    if (!sw_init(game, 0, 0)) sdl_port_rom_error();
    sdl_port_cfg cfg = {sw_title(), SW_W, SW_H, 2, SW_AUDIO_RATE, SW_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_mouse_enable(true);

    int mx = 0x80 << 4, my = 0x80 << 4;     // the mouse's yoke (12.4)
    bool f4 = false, was_captured = false;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) sw_reset();
        // F4: the other game (its ROMs too; if they are not there, this one goes on)
        bool f4_now = sdl_port_key(F4);
        if (f4_now && !f4) {
            int other = sw_game() == SW_GAME_ESB ? SW_GAME_STARWARS : SW_GAME_ESB;
            if (!start_game(other)) {
                fprintf(stderr, "starwars: no ROMs for %s\n", other == SW_GAME_ESB ? "esb" : "starwars");
                start_game(other == SW_GAME_ESB ? SW_GAME_STARWARS : SW_GAME_ESB);
            }
        }
        f4 = f4_now;
        if (sdl_port_paused()) { sdl_port_present(rgb); continue; }

        uint32_t k = sdl_port_keys();
        sw_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = (k & SP_COIN1) != 0;
        in.coin2 = (k & SP_COIN2) != 0;
        in.start = (k & (SP_START1 | SP_START2)) != 0;
        in.fire1 = (k & SP_B1) != 0;
        in.fire2 = (k & SP_B2) != 0;
        in.fire3 = (k & SP_B3) != 0;
        in.x = in.y = 0x80;
        int dx, dy;
        uint32_t mb = sdl_port_mouse(&dx, &dy);
        bool captured = sdl_port_mouse_captured();
        if (captured) {
            // the mouse moves the yoke and it stays where it is left
            mx += dx * 24;
            my += dy * 24;
            mx = mx < 0 ? 0 : mx > (255 << 4) ? 255 << 4 : mx;
            my = my < 0 ? 0 : my > (255 << 4) ? 255 << 4 : my;
            in.x = (uint8_t)(mx >> 4);
            in.y = (uint8_t)(my >> 4);
            if (mb & 1) in.fire1 = true;
            if (mb & 2) in.fire2 = true;
        } else if (was_captured) {
            mx = my = 0x80 << 4;                // let go: back to the centre
        }
        was_captured = captured;
        int ax, ay;
        if (sdl_port_analog(&ax, &ay)) {
            in.x = (uint8_t)clamp255(ax);
            in.y = (uint8_t)clamp255(ay);
        }
        if (k & SP_UP) in.y = 0x10;
        if (k & SP_DOWN) in.y = 0xf0;
        if (k & SP_LEFT) in.x = 0x10;
        if (k & SP_RIGHT) in.x = 0xf0;
        sw_set_inputs(&in);
        int n = sw_run_frame(audio);
        sdl_port_audio(audio, n);
        const uint32_t *fb = sw_framebuffer();
        for (int i = 0; i < SW_W * SW_H; i++) rgb[i] = fb[i] & 0xffffff;
        sdl_port_present(rgb);
    }
    sdl_port_close();
    return 0;
}
