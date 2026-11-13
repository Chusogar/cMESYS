// m_zx.c - the ZX Spectrum family (drivers/computers/zxspec) for the Android
// port (core.h). model: zx_model_t (48K, 128K, +3, Pentagon, Pentagon 1024,
// Scorpion). ROMs: [0] the 48K BASIC (16 KB), [1] the banked ROMs (32 or
// 64 KB; none on the 48K). Media by the name's extension: tapes, snapshots,
// RZX, TR-DOS and +3 disks, Interface 2 cartridges.
#include <stdlib.h>

#include "core.h"
#include "zxspec_core.h"

static zxspec *m;
static uint8_t *rom48, *rom128;

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    if (n < 1 || !roms[0] || lens[0] != 0x4000 || model < ZX_SPEC48 || model > ZX_SCORPION ||
        (model != ZX_SPEC48 && (n < 2 || !roms[1] || (lens[1] != 0x8000 && lens[1] != 0x10000)))) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    if (m) zxspec_destroy(m);
    free(rom48);
    free(rom128);
    rom48 = roms[0];
    rom128 = model != ZX_SPEC48 && n > 1 ? roms[1] : NULL;
    if (!rom128 && n > 1) free(roms[1]);
    for (int i = 2; i < n; i++) free(roms[i]);
    m = zxspec_create((zx_model_t)model);
    if (!m) return false;
    zxspec_set_rom(m, rom48, rom128, rom128 ? (size_t)lens[1] : 0);
    zxspec_reset(m);
    return true;
}

void mc_reset(void) { zxspec_reset(m); }
int mc_width(void) { return m ? zxspec_width(m) : ZX_PIC_W; }
int mc_height(void) { return m ? zxspec_height(m) : ZX_PIC_H; }
double mc_fps(void) { return m ? zxspec_fps(m) : ZX_FPS; }
int mc_audio_rate(void) { return ZX_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    zxspec_keys_clear(m);
    for (int c = 0; c < 64; c++)
        if (keys[c]) zxspec_key(m, c, true);
    return zxspec_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    zxspec_render(m, argb);
    int n = zxspec_width(m) * zxspec_height(m);
    for (int i = 0; i < n; i++) argb[i] |= 0xff000000u;
}

// Kempston: bits 0 right, 1 left, 2 down, 3 up, 4 fire
void mc_joy(int port, unsigned bits)
{
    if (port != 1) return;
    zxspec_joy(m, (uint8_t)(((bits & MC_JOY_RIGHT) ? 1 : 0) | ((bits & MC_JOY_LEFT) ? 2 : 0) |
                            ((bits & MC_JOY_DOWN) ? 4 : 0) | ((bits & MC_JOY_UP) ? 8 : 0) |
                            ((bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) ? 16 : 0)));
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    return zxspec_load_file(m, path) ? NULL : zxspec_last_error(m);
}

void mc_eject(int slot) { (void)slot; zxspec_tape_stop(m); }
void mc_turbo(bool on) { (void)on; }

int mc_status(void)
{
    return (zxspec_tape_loaded(m) ? MC_TAPE_LOADED : 0) | (zxspec_tape_playing(m) ? MC_TAPE_PLAYING | MC_LED1 : 0) |
           (zxspec_rzx_playing(m) ? MC_LED2 : 0);
}

void mc_command(int cmd)
{
    if (cmd == MC_CMD_TAPE_TOGGLE) zxspec_tape_toggle(m);
    else if (cmd == MC_CMD_MAGIC) zxspec_magic(m);
}
