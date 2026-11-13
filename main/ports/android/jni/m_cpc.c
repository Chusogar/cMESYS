// m_cpc.c - the Amstrad CPC (drivers/computers/cpc) for the Android port
// (core.h). model: cpc_model_t. ROMs: [0] OS + BASIC (32 KB), [1] AMSDOS
// (16 KB, 664 / 6128). Media: .sna, .dsk / .edsk, tapes (.cdt .tzx .csw).
#include <stdlib.h>

#include "core.h"
#include "cpc_core.h"

static cpc *m;
static uint8_t *os_rom, *dos_rom;
static bool green;

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    if (n < 1 || !roms[0] || lens[0] != 0x8000 || model < CPC_464 || model > CPC_6128) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    if (m) cpc_destroy(m);
    free(os_rom);
    free(dos_rom);
    os_rom = roms[0];
    dos_rom = n > 1 && roms[1] && lens[1] == 0x4000 ? roms[1] : NULL;
    if (n > 1 && roms[1] && !dos_rom) free(roms[1]);
    for (int i = 2; i < n; i++) free(roms[i]);
    m = cpc_create((cpc_model_t)model, CPC_AUDIO_RATE);
    if (!m) return false;
    cpc_set_roms(m, os_rom, dos_rom);
    cpc_set_color_monitor(m, !green);
    cpc_reset(m);
    return true;
}

void mc_reset(void) { cpc_reset(m); }
int mc_width(void) { return CPC_FULL_W; }
int mc_height(void) { return CPC_FULL_H; }
double mc_fps(void) { return CPC_FPS; }
int mc_audio_rate(void) { return CPC_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    cpc_keys_clear(m);
    for (int c = 0; c < 80; c++)
        if (keys[c]) cpc_key(m, c, true);
    return cpc_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < CPC_FULL_H; y++) {
        uint32_t *o = argb + y * CPC_FULL_W;
        cpc_render_row(m, y, 0, CPC_FULL_W, o);
        for (int x = 0; x < CPC_FULL_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    if (port != 1) return;
    cpc_joy(m, (uint8_t)(((bits & MC_JOY_UP) ? CPC_JOY_UP : 0) | ((bits & MC_JOY_DOWN) ? CPC_JOY_DOWN : 0) |
                         ((bits & MC_JOY_LEFT) ? CPC_JOY_LEFT : 0) | ((bits & MC_JOY_RIGHT) ? CPC_JOY_RIGHT : 0) |
                         ((bits & MC_JOY_FIRE) ? CPC_JOY_FIRE1 : 0) | ((bits & MC_JOY_FIRE2) ? CPC_JOY_FIRE2 : 0)));
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    return cpc_load_file(m, path) ? NULL : cpc_last_error(m);
}

void mc_eject(int slot) { (void)slot; }
void mc_turbo(bool on) { cpc_set_turbo(m, on); }

int mc_status(void)
{
    return (cpc_tape_loaded(m) ? MC_TAPE_LOADED : 0) | (cpc_tape_playing(m) ? MC_TAPE_PLAYING | MC_LED1 : 0) |
           (cpc_model(m) != CPC_464 ? MC_HAS_DISK : 0);
}

void mc_command(int cmd)
{
    if (cmd == MC_CMD_TAPE_TOGGLE) cpc_tape_toggle(m);
    else if (cmd == MC_CMD_MONITOR) {
        green = !green;
        cpc_set_color_monitor(m, !green);
    }
}
