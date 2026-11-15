// m_atarist.c - the Atari ST (drivers/computers/atarist) for the Android port
// (core.h). ROMs: [0] TOS (192 KB: TOS 1.0x or EmuTOS). Model 0: 1 MB of
// RAM, 1: 512 KB. Media: .st / .msa / .stx disks, slot 0 / 1 = drive A / B.
// Keys: the ST's scan codes (1..127). The screen is a touchpad for the
// mouse (mc_mouse); a pad is the joystick (port 1 = the joystick port).
#include <stdlib.h>

#include "core.h"
#include "atarist_core.h"

static uint8_t *tos;

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    for (int i = 1; i < n; i++) free(roms[i]);
    if (n < 1 || !roms[0] || lens[0] < ST_ROM_SIZE) {
        if (n > 0) free(roms[0]);
        return false;
    }
    const uint8_t *old = tos;
    tos = roms[0];
    const bool ok = atarist_init(tos, ST_ROM_SIZE, model == 1 ? 0x80000 : 0x100000, ST_AUDIO_RATE);
    free((void *)old);
    return ok;
}

void mc_reset(void) { atarist_reset(); }
int mc_width(void) { return ST_W; }
int mc_height(void) { return ST_H; }
double mc_fps(void) { return ST_FPS; }
int mc_audio_rate(void) { return ST_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 1; c < 128; c++) atarist_key(c, keys[c] != 0);
    return atarist_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < ST_H; y++) {
        uint32_t *o = argb + y * ST_W;
        atarist_render_row(y, o);
        for (int x = 0; x < ST_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    const unsigned st = ((bits & MC_JOY_UP) ? ST_UP : 0) | ((bits & MC_JOY_DOWN) ? ST_DOWN : 0) |
                        ((bits & MC_JOY_LEFT) ? ST_LEFT : 0) | ((bits & MC_JOY_RIGHT) ? ST_RIGHT : 0) |
                        ((bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) ? ST_FIRE : 0);
    if (port == 1) atarist_joy(1, st);
    else if (port == 2) atarist_joy(0, st);
}

void mc_mouse(int dx, int dy, unsigned buttons)
{
    atarist_pointer p = { true, true, false, 0, 0, dx, dy, (buttons & 1) != 0, (buttons & 2) != 0 };
    atarist_pointer_input(&p);
}

const char *mc_load(const char *path, int slot)
{
    if (!atarist_load_file(slot == 1 ? 1 : 0, path)) return atarist_last_error();
    return NULL;
}

void mc_eject(int slot) { atarist_eject(slot == 1 ? 1 : 0); }
void mc_turbo(bool on) { atarist_set_turbo(on); }
int mc_status(void) { return (atarist_drive_light() ? MC_LED1 : 0) | MC_HAS_DISK; }
void mc_command(int cmd) { (void)cmd; }
