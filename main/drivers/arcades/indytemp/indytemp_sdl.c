// indytemp_sdl.c - Indiana Jones and the Temple of Doom (Atari System 1) on
// the desktop (SDL2), same core as the CYD.
//
//   ./indytemp                       ROMs compiled in (rom_data.c)
//   ./indytemp <indytemp> [atarisy1] ROMs from directories of unzipped files
//
// Keys: 5 coin, 1 start, cursors, Z/Ctrl left button (whip), X/Alt right
// button (jump), F3 reset, P pause, ESC quit. EEPROM kept in indytemp.eep.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "indytemp_core.h"
#include "indytemp_roms.h"
#include "rom_data.h"

#define AUDIO_RATE 44100
#define EEPROM_FILE "indytemp.eep"

static indy_core core;
static indy_mo_list mol;
static uint16_t lines[INDY_SCREEN_W * INDY_SCREEN_H];
static uint32_t pixels[INDY_SCREEN_W * INDY_SCREEN_H];

static void on_frame(void *user, indy_core *c)
{
    (void)user;
    indy_build_mo_list(&c->gfx, &c->v, &mol);
    for (int y = 0; y < INDY_SCREEN_H; y += 8)
        indy_compose(&c->gfx, &c->v, &mol, y, y + 8, lines + y * INDY_SCREEN_W);
    uint32_t pal[0x400];
    for (int i = 0; i < 0x400; i++) pal[i] = indy_palette_rgb(&c->v, i);
    for (int i = 0; i < INDY_SCREEN_W * INDY_SCREEN_H; i++) pixels[i] = pal[lines[i]];
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

int main(int argc, char **argv)
{
    indy_roms roms;
    if (argc > 1) {
        char err[512];
        if (!indy_load_roms(argv[1], argc > 2 ? argv[2] : NULL, &roms, err, sizeof err)) {
            fprintf(stderr, "[INDY] %s\n", err);
            return 1;
        }
    } else {
        indy_rom_data(&roms);
    }
    indy_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;             // same result, less host time
    core.on_frame = on_frame;
    static int16_t abuf[4096];
    core.audio = abuf;
    core.audio_cap = 4096;
    eeprom_io(false);

    sdl_port_cfg cfg = {"Indiana Jones and the Temple of Doom", INDY_SCREEN_W, INDY_SCREEN_H, 2, AUDIO_RATE, INDY_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) indy_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        uint32_t k = sdl_port_keys();
        indy_inputs in = {
            .coin1 = k & SP_COIN1, .coin2 = k & SP_COIN2, .start = k & (SP_START1 | SP_START2),
            .button1 = k & SP_B1, .button2 = k & SP_B2,
            .up = k & SP_UP, .down = k & SP_DOWN, .left = k & SP_LEFT, .right = k & SP_RIGHT,
        };
        indy_set_inputs(&core, &in);
        indy_run_frame(&core);
        sdl_port_audio(abuf, core.audio_n);
        if (core.eeprom_dirty) eeprom_io(true);
        sdl_port_present(pixels);
    }
    eeprom_io(true);
    sdl_port_close();
    return 0;
}
