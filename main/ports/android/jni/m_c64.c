// m_c64.c - the Commodore 64 (drivers/computers/c64) for the Android port
// (core.h). ROMs: [0] KERNAL, [1] BASIC (8 KB each), [2] character ROM
// (4 KB), [3] the 1541's DOS (16 KB, optional: no drive without it). Media:
// .prg .t64 .d64 .g64 .tap. Key code 100 is RESTORE (the NMI, not in the
// matrix).
#include <stdlib.h>

#include "core.h"
#include "c64_core.h"

#define KEY_RESTORE 100

static c64 *m;
static uint8_t *rom[4];

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    static const int size[4] = { 0x2000, 0x2000, 0x1000, 0x4000 };
    bool ok = n >= 3;
    for (int i = 0; i < 3 && ok; i++) ok = roms[i] && lens[i] == size[i];
    if (!ok) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    if (m) c64_destroy(m);
    for (int i = 0; i < 4; i++) {
        free(rom[i]);
        rom[i] = i < n ? roms[i] : NULL;
    }
    if (rom[3] && lens[3] != size[3]) {
        free(rom[3]);
        rom[3] = NULL;
    }
    for (int i = 4; i < n; i++) free(roms[i]);
    m = c64_create(C64_AUDIO_RATE);
    if (!m) return false;
    c64_set_roms(m, rom[0], rom[1], rom[2], rom[3]);
    c64_reset(m);
    return true;
}

void mc_reset(void) { c64_reset(m); }
int mc_width(void) { return C64_PIC_W; }
int mc_height(void) { return C64_PIC_H; }
double mc_fps(void) { return C64_FPS; }
int mc_audio_rate(void) { return C64_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    c64_keys_clear(m);
    for (int c = 0; c < 64; c++)
        if (keys[c]) c64_key(m, c, true);
    c64_restore(m, keys[KEY_RESTORE] != 0);
    return c64_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < C64_PIC_H; y++) {
        uint32_t *o = argb + y * C64_PIC_W;
        c64_render_row(m, y, o);
        for (int x = 0; x < C64_PIC_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    c64_joy(m, port, (uint8_t)(bits & 31));      // the same bits as C64_JOY_*
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    return c64_load_file(m, path) ? NULL : c64_last_error(m);
}

void mc_eject(int slot) { (void)slot; }
void mc_turbo(bool on) { c64_set_turbo(m, on); }

int mc_status(void)
{
    return (c64_drive_led(m) ? MC_LED1 : 0) | (c64_tape_loaded(m) ? MC_TAPE_LOADED : 0) |
           (c64_tape_playing(m) ? MC_TAPE_PLAYING | MC_LED2 : 0) | (c64_has_drive(m) ? MC_HAS_DISK : 0);
}

void mc_command(int cmd)
{
    if (cmd == MC_CMD_TAPE_TOGGLE) c64_tape_toggle(m);
    else if (cmd == MC_CMD_FAST_LOAD_ON) c64_set_fast_load(m, true);
    else if (cmd == MC_CMD_FAST_LOAD_OFF) c64_set_fast_load(m, false);
}
