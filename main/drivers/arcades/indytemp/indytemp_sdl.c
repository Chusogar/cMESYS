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
#include "atarisy1_core.h"
#include "atarisy1_roms.h"
#include "rom_data.h"

#define AUDIO_RATE 44100
#define EEPROM_FILE "indytemp.eep"

static as1_core core;
static as1_mo_list mol;
static uint16_t lines[AS1_SCREEN_W * AS1_SCREEN_H];
static uint32_t pixels[AS1_SCREEN_W * AS1_SCREEN_H];

static void on_frame(void *user, as1_core *c)
{
    (void)user;
    as1_build_mo_list(&c->gfx, &c->v, &mol);
    for (int y = 0; y < AS1_SCREEN_H; y += 8)
        as1_compose(&c->gfx, &c->v, &mol, y, y + 8, lines + y * AS1_SCREEN_W);
    uint32_t pal[0x400];
    for (int i = 0; i < 0x400; i++) pal[i] = as1_palette_rgb(&c->v, i);
    for (int i = 0; i < AS1_SCREEN_W * AS1_SCREEN_H; i++) pixels[i] = pal[lines[i]];
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
    as1_roms roms;
    if (argc > 1) {
        char err[512];
        if (!as1_load_roms(AS1_INDYTEMP, argv[1], argc > 2 ? argv[2] : NULL, &roms, err, sizeof err)) {
            fprintf(stderr, "[INDY] %s\n", err);
            return 1;
        }
    } else {
        as1_rom_data(&roms);
    }
    as1_init(&core, &roms, AUDIO_RATE);
    core.idle_skip = true;             // same result, less host time
    core.on_frame = on_frame;
    static int16_t abuf[4096];
    core.audio = abuf;
    core.audio_cap = 4096;
    eeprom_io(false);

    sdl_port_cfg cfg = {"Indiana Jones and the Temple of Doom", AS1_SCREEN_W, AS1_SCREEN_H, 2, AUDIO_RATE, AS1_FPS};
    if (!sdl_port_open(&cfg)) return 1;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) as1_reset(&core);
        if (sdl_port_paused()) { sdl_port_present(pixels); continue; }
        uint32_t k = sdl_port_keys();
        as1_inputs in = {
            .coin1 = k & SP_COIN1, .coin2 = k & SP_COIN2, .start = k & (SP_START1 | SP_START2),
            .button1 = k & SP_B1, .button2 = k & SP_B2,
            .up = k & SP_UP, .down = k & SP_DOWN, .left = k & SP_LEFT, .right = k & SP_RIGHT,
        };
        as1_set_inputs(&core, &in);
        as1_run_frame(&core);
        sdl_port_audio(abuf, core.audio_n);
        if (core.eeprom_dirty) eeprom_io(true);
        sdl_port_present(pixels);
    }
    eeprom_io(true);
    sdl_port_close();
    return 0;
}
