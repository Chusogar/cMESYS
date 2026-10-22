// gauntlet_sdl.c - Gauntlet (Atari, 1985) on the desktop (SDL2), same core as
// the CYD. ROMs compiled in (rom_data.c, made by tools/gen_roms_gauntlet.py).
//
// Keys (player 1): 5 coin, 6 coin 2, cursors, Z/Ctrl fire, X/Alt magic,
// 1 start (= magic, which joins the game), F3 reset, P pause, ESC quit.
// EEPROM (settings, high scores) in gauntlet.eep.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "gauntlet_core.h"
#include "rom_data.h"

#define AUDIO_RATE 44100
#define EEPROM_FILE "gauntlet.eep"

static gt_core core;
static gt_renderer rend;
static uint16_t lines[GT_SCREEN_W * GT_SCREEN_H];
static uint32_t pixels[GT_SCREEN_W * GT_SCREEN_H];

static void on_frame(void *user, gt_core *c)
{
    (void)user;
    for (int y = 0; y < GT_SCREEN_H; y += 16)
        gt_compose(&rend, &c->roms, &c->v, y, y + 16, lines + y * GT_SCREEN_W);
    uint32_t pal[0x400];
    for (int i = 0; i < 0x400; i++) pal[i] = gt_palette_rgb(&c->v, i);
    for (int i = 0; i < GT_SCREEN_W * GT_SCREEN_H; i++) pixels[i] = pal[lines[i]];
}

static void eeprom_io(bool save)
{
    FILE *f = fopen(EEPROM_FILE, save ? "wb" : "rb");
    if (!f) return;
    if (save) fwrite(core.eeprom, 1, sizeof core.eeprom, f);
    else if (fread(core.eeprom, 1, sizeof core.eeprom, f) != sizeof core.eeprom) memset(core.eeprom, 0xff, sizeof core.eeprom);
    fclose(f);
    core.eeprom_dirty = false;
}

int main(void)
{
    gt_roms roms;
    gt_rom_data(&roms);
    gt_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;             // same result, less host time
    core.on_frame = on_frame;
    static int16_t abuf[4096];
    core.audio = abuf;
    core.audio_cap = 4096;
    eeprom_io(false);

    sdl_port_cfg cfg = {"Gauntlet", GT_SCREEN_W, GT_SCREEN_H, 2, AUDIO_RATE, GT_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    int frames = 0;
    while (sdl_port_poll(&reset)) {
        if (reset) gt_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        uint32_t k = sdl_port_keys();
        gt_inputs in = {0};
        in.coin[0] = k & SP_COIN1;
        in.coin[1] = k & SP_COIN2;
        in.p[0].up = k & SP_UP;
        in.p[0].down = k & SP_DOWN;
        in.p[0].left = k & SP_LEFT;
        in.p[0].right = k & SP_RIGHT;
        in.p[0].fire = k & SP_B1;
        in.p[0].magic = k & (SP_B2 | SP_START1);
        gt_set_inputs(&core, &in);
        gt_run_frame(&core);
        sdl_port_audio(abuf, core.audio_n);
        if (core.eeprom_dirty && ++frames % 60 == 0) eeprom_io(true);
        sdl_port_present(pixels);
    }
    eeprom_io(true);
    sdl_port_close();
    return 0;
}
