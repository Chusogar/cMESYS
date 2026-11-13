// m_lynx.c - the Atari Lynx (drivers/computers/lynx) for the Android port
// (core.h). ROMs: [0] the 512-byte boot ROM (optional: dsp-cpp's own loader
// without it). Media: .lnx / .lyx / .o cartridges. The "keys" are the
// console's buttons, by bit number of lynx_core.h (A 0, B 1, OPTION 2 2,
// OPTION 1 3, right 4, left 5, down 6, up 7, PAUSE 8), and 9..12 the
// cross's diagonals (up-left, up-right, down-left, down-right).
#include <stdlib.h>

#include "core.h"
#include "lynx_core.h"

static lynx *m;
static uint8_t *boot;
static unsigned pad;

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    if (m) lynx_destroy(m);
    free(boot);
    boot = n > 0 && roms[0] && lens[0] == 512 ? roms[0] : NULL;
    if (n > 0 && roms[0] && !boot) free(roms[0]);
    for (int i = 1; i < n; i++) free(roms[i]);
    m = lynx_create(LYNX_AUDIO_RATE);
    if (!m) return false;
    lynx_set_boot_rom(m, boot);
    lynx_reset(m);
    return true;
}

void mc_reset(void) { lynx_reset(m); }
int mc_width(void) { return LYNX_W; }
int mc_height(void) { return LYNX_H; }
double mc_fps(void) { return LYNX_FPS; }
int mc_audio_rate(void) { return LYNX_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    unsigned b = pad;
    for (int c = 0; c <= 8; c++)
        if (keys[c]) b |= 1u << c;
    if (keys[9]) b |= LYNX_UP | LYNX_LEFT;
    if (keys[10]) b |= LYNX_UP | LYNX_RIGHT;
    if (keys[11]) b |= LYNX_DOWN | LYNX_LEFT;
    if (keys[12]) b |= LYNX_DOWN | LYNX_RIGHT;
    lynx_buttons(m, b);
    return lynx_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < LYNX_H; y++) {
        uint32_t *o = argb + y * LYNX_W;
        lynx_render_row(m, y, o);
        for (int x = 0; x < LYNX_W; x++) o[x] |= 0xff000000u;
    }
}

// A pad's cross and A / B (FIRE / FIRE2) as the console's
void mc_joy(int port, unsigned bits)
{
    if (port != 1) return;
    pad = ((bits & MC_JOY_UP) ? LYNX_UP : 0) | ((bits & MC_JOY_DOWN) ? LYNX_DOWN : 0) |
          ((bits & MC_JOY_LEFT) ? LYNX_LEFT : 0) | ((bits & MC_JOY_RIGHT) ? LYNX_RIGHT : 0) |
          ((bits & MC_JOY_FIRE) ? LYNX_A : 0) | ((bits & MC_JOY_FIRE2) ? LYNX_B : 0);
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    if (!lynx_load_file(m, path)) return lynx_last_error(m);
    lynx_reset(m);
    return NULL;
}

void mc_eject(int slot) { (void)slot; lynx_eject(m); lynx_reset(m); }
void mc_turbo(bool on) { lynx_set_turbo(m, on); }
int mc_status(void) { return 0; }
void mc_command(int cmd) { (void)cmd; }
