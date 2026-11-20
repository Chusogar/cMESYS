// m_exelv.c - the Exelvision EXL-100 and EXELTEL (drivers/computers/exelv)
// for the Android port (core.h). model: 0 EXL-100, 1 EXELTEL. ROMs: [0] the
// main CPU's (2 / 4 KB), [1] the I/O CPU's (4 KB, optional), [2] the
// EXELTEL's system ROM (64 KB). Media: slot 0 a cartridge (.bin .rom: the
// machine restarts with it) or a cassette, slot 1 a cassette (.k7 .wav).
// Key codes: the infrared keyboard's channels (EK_* of exelv_core.h).
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "exelv_core.h"

static uint8_t *rom[3];
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    if (*n < 3) *n = 3;
    if (roms[0] && roms[1] && (model == 0 || roms[2])) return;
    exelv_rom_main = exelv_rom_sub = exelv_rom_sys = NULL;
    if (model == EXELV_EXELTEL) exeltel_rom_load();
    else exl100_rom_load();
    mc_take(roms, lens, 0, &exelv_rom_main, model == EXELV_EXELTEL ? 0x1000 : 0x800);
    mc_take(roms, lens, 1, &exelv_rom_sub, 0x1000);
    mc_take(roms, lens, 2, &exelv_rom_sys, 0x10000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    const int need = model == EXELV_EXELTEL ? 0x1000 : 0x800;
    if (n < 1 || !roms[0] || lens[0] < need) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    for (int i = 0; i < 3; i++) {
        free(rom[i]);
        rom[i] = i < n ? roms[i] : NULL;
    }
    for (int i = 3; i < n; i++) free(roms[i]);
    if (rom[1] && lens[1] < 0x1000) { free(rom[1]); rom[1] = NULL; }
    if (rom[2] && lens[2] < 0x10000) { free(rom[2]); rom[2] = NULL; }
    if (!exelv_init(model, rom[0], rom[1], rom[2])) return false;
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { exelv_reset(); }
int mc_width(void) { return EXELV_W; }
int mc_height(void) { return EXELV_H; }
double mc_fps(void) { return exelv_fps(); }
int mc_audio_rate(void) { return EXELV_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < EK_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        exelv_key(c, k);
    }
    const int n = exelv_run_frame(audio);
    size_t saved = 0;
    free(exelv_take_tape_save(&saved));      // (SAVE: not kept)
    return n;
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < EXELV_H; y++) exelv_render_row(y, argb + y * EXELV_W);
}

void mc_joy(int port, unsigned bits)
{
    (void)port;
    uint8_t j = 0;
    if (bits & MC_JOY_UP) j |= EXELV_JOY_UP;
    if (bits & MC_JOY_DOWN) j |= EXELV_JOY_DOWN;
    if (bits & MC_JOY_LEFT) j |= EXELV_JOY_LEFT;
    if (bits & MC_JOY_RIGHT) j |= EXELV_JOY_RIGHT;
    if (bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) j |= EXELV_JOY_FIRE;
    exelv_joy(j);
}

const char *mc_load(const char *path, int slot) { (void)slot; return exelv_load_file(path); }

void mc_eject(int slot)
{
    if (slot == 1) {
        exelv_eject_tape();
    } else {
        exelv_eject_cart();
        exelv_reset();
    }
}

void mc_turbo(bool on) { (void)on; }

int mc_status(void)
{
    return (exelv_tape_inserted() ? MC_TAPE_LOADED : 0) | (exelv_tape_playing() ? MC_TAPE_PLAYING : 0);
}

void mc_command(int cmd)
{
    if (cmd == MC_CMD_TAPE_TOGGLE) exelv_tape_play(!exelv_tape_playing());
}
