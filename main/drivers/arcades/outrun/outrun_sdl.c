// outrun_sdl.c - OutRun (Sega, 1986) on the desktop (SDL2), same core as the
// CYD. ROMs compiled in (rom_data.c, made by tools/gen_roms_outrun.py).
//
// Keys: 5 coin, 1 start, Left / Right steer, Up or Z gas, Down or X brake,
// C or Space change gear, F3 reset, P pause, ESC quit. The main CPU's RAM
// (battery backed on the board: high scores) is kept in outrun.nv.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "or_core.h"
#include "rom_data.h"

#define AUDIO_RATE 44100
#define NVRAM_FILE "outrun.nv"

static or_core core;
static uint16_t lines[OR_SCREEN_W * OR_SCREEN_H];
static uint32_t pixels[OR_SCREEN_W * OR_SCREEN_H];

static void on_frame(void *user, or_core *c)
{
    (void)user;
    or_video v;
    or_get_video(c, &v);
    for (int y = 0; y < OR_SCREEN_H; y += 16)
        or_render(&v, &c->roms, y, y + 16, lines + y * OR_SCREEN_W, NULL);
    static uint32_t pal[0x2001];
    static bool pal_ok;
    if (c->palette_dirty || !pal_ok) {
        c->palette_dirty = false;
        pal_ok = true;
        for (int i = 0; i <= OR_BLACK; i++) pal[i] = or_palette_rgb(c->paletteram, i);
    }
    for (int i = 0; i < OR_SCREEN_W * OR_SCREEN_H; i++) pixels[i] = pal[lines[i]];
}

static void nvram_io(bool save)
{
    FILE *f = fopen(NVRAM_FILE, save ? "wb" : "rb");
    if (!f) return;
    if (save) fwrite(core.workram, 1, 0x8000, f);
    else if (fread(core.workram, 1, 0x8000, f) != 0x8000) memset(core.workram, 0, 0x8000);
    fclose(f);
}

int main(void)
{
    or_roms roms;
    or_rom_data(&roms);
    or_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;             // same result, less host time
    core.on_frame = on_frame;
    static int16_t abuf[4096];
    core.sound->audio = abuf;
    core.sound->audio_cap = 4096;
    nvram_io(false);

    sdl_port_cfg cfg = {"OutRun", OR_SCREEN_W, OR_SCREEN_H, 3, AUDIO_RATE, OR_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) or_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        uint32_t k = sdl_port_keys();
        or_inputs in = {0};
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
        sdl_port_audio(abuf, core.sound->audio_n);
        sdl_port_present(pixels);
    }
    nvram_io(true);
    sdl_port_close();
    return 0;
}
