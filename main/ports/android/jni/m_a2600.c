// m_a2600.c - the Atari 2600 (drivers/computers/a2600) for the Android port
// (core.h). No ROMs (the cartridge is all). Media: .a26 / .bin / .rom
// cartridges. The "keys" are the joystick and the console's buttons: up 0,
// down 1, left 2, right 3, FIRE 4, GAME RESET 5, GAME SELECT 6, and 7..10
// the stick's diagonals (up-left, up-right, down-left, down-right); 16..20
// the second joystick (up, down, left, right, FIRE). The switches (colour /
// B&W, the difficulties) are commands.
#include <stdlib.h>

#include "core.h"
#include "a2600_core.h"

static a2600 *m;
static unsigned pad1, pad2;

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)lens, (void)unix_time;
    for (int i = 0; i < n; i++) free(roms[i]);
    if (m) a2600_destroy(m);
    m = a2600_create(A2600_AUDIO_RATE);
    return m != NULL;
}

void mc_reset(void) { a2600_reset(m); }
int mc_width(void) { return A2600_W; }
int mc_height(void) { return A2600_H; }
double mc_fps(void) { return A2600_FPS; }
int mc_audio_rate(void) { return A2600_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    static const unsigned kBits[11] = {
        A2600_UP, A2600_DOWN, A2600_LEFT, A2600_RIGHT, A2600_FIRE, A2600_RESET, A2600_SELECT,
        A2600_UP | A2600_LEFT, A2600_UP | A2600_RIGHT, A2600_DOWN | A2600_LEFT, A2600_DOWN | A2600_RIGHT,
    };
    unsigned b = pad1 | pad2 << A2600_P2;
    for (int c = 0; c < 11; c++)
        if (keys[c]) b |= kBits[c];
    for (int c = 0; c < 5; c++)
        if (keys[16 + c]) b |= kBits[c] << A2600_P2;
    a2600_input(m, b);
    return a2600_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < A2600_H; y++) {
        uint32_t *o = argb + y * A2600_W;
        a2600_render_row(m, y, o);
        for (int x = 0; x < A2600_W; x++) o[x] |= 0xff000000u;
    }
}

static unsigned joy_bits(unsigned bits)
{
    return ((bits & MC_JOY_UP) ? A2600_UP : 0) | ((bits & MC_JOY_DOWN) ? A2600_DOWN : 0) |
           ((bits & MC_JOY_LEFT) ? A2600_LEFT : 0) | ((bits & MC_JOY_RIGHT) ? A2600_RIGHT : 0) |
           ((bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) ? A2600_FIRE : 0);
}

void mc_joy(int port, unsigned bits)
{
    if (port == 1) pad1 = joy_bits(bits);
    else if (port == 2) pad2 = joy_bits(bits);
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    if (!a2600_load_file(m, path)) return a2600_last_error(m);
    return NULL;
}

void mc_eject(int slot) { (void)slot; a2600_eject(m); }
void mc_turbo(bool on) { a2600_set_turbo(m, on); }
int mc_status(void) { return 0; }

void mc_command(int cmd)
{
    const uint8_t sw = a2600_switches(m);
    if (cmd == MC_CMD_TV_TYPE) a2600_set_switches(m, sw ^ A2600_SW_COLOUR);
    if (cmd == MC_CMD_DIFF_LEFT) a2600_set_switches(m, sw ^ A2600_SW_DIFF_LEFT_A);
    if (cmd == MC_CMD_DIFF_RIGHT) a2600_set_switches(m, sw ^ A2600_SW_DIFF_RIGHT_A);
}
