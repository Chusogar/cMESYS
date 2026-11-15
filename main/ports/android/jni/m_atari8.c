// m_atari8.c - the Atari 800XL / 800 (drivers/computers/atari8) for the
// Android port (core.h). Models: 0 the 800XL (ROMs [0] its 16 KB OS, [1]
// BASIC, optional), 1 the 400/800 ([0] its 10 KB OS). Media: .atr disks
// (the machine cold-starts with the disk). Keys: POKEY's scan codes 0-63,
// and 64 SHIFT, 65 CONTROL, 66 BREAK, 67 START, 68 SELECT, 69 OPTION.
#include <stdlib.h>

#include "core.h"
#include "atari8_core.h"

static atari8 *m;
static uint8_t *os, *basic;
static unsigned joy[2];
static bool break_was;

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    if (m) atari8_destroy(m);
    m = NULL;
    free(os);
    free(basic);
    os = n > 0 ? roms[0] : NULL;
    basic = n > 1 ? roms[1] : NULL;
    for (int i = 2; i < n; i++) free(roms[i]);
    if (!os) return false;
    const int mdl = model == 1 ? A8_MODEL_800 : A8_MODEL_XL;
    m = atari8_create(mdl, A8_AUDIO_RATE, os, (size_t)lens[0], basic);
    return m != NULL;
}

void mc_reset(void) { atari8_reset(m); }
int mc_width(void) { return A8_W; }
int mc_height(void) { return A8_H; }
double mc_fps(void) { return A8_FPS; }
int mc_audio_rate(void) { return A8_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    int code = -1;
    for (int i = 0; code < 0 && atari8_key_order(i) >= 0; i++)
        if (keys[atari8_key_order(i)]) code = atari8_key_order(i);
    atari8_set_key(m, code, keys[64] != 0, keys[65] != 0);
    if (keys[66] && !break_was) atari8_break(m);
    break_was = keys[66] != 0;
    atari8_console(m, (keys[67] ? A8_START : 0) | (keys[68] ? A8_SELECT : 0) | (keys[69] ? A8_OPTION : 0));
    atari8_joy(m, 0, joy[0]);
    atari8_joy(m, 1, joy[1]);
    return atari8_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < A8_H; y++) {
        uint32_t *o = argb + y * A8_W;
        atari8_render_row(m, y, o);
        for (int x = 0; x < A8_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    if (port < 1 || port > 2) return;
    joy[port - 1] = ((bits & MC_JOY_UP) ? A8_UP : 0) | ((bits & MC_JOY_DOWN) ? A8_DOWN : 0) |
                    ((bits & MC_JOY_LEFT) ? A8_LEFT : 0) | ((bits & MC_JOY_RIGHT) ? A8_RIGHT : 0) |
                    ((bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) ? A8_FIRE : 0);
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    if (!atari8_load_file(m, path)) return atari8_last_error(m);
    return NULL;
}

void mc_eject(int slot) { (void)slot; atari8_eject(m); atari8_reset(m); }
void mc_turbo(bool on) { atari8_set_turbo(m, on); }
int mc_status(void) { return atari8_has_disk(m) ? MC_HAS_DISK : 0; }
void mc_command(int cmd) { (void)cmd; }
