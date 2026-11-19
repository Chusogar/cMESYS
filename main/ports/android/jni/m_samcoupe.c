// m_samcoupe.c - the SAM Coupe (drivers/computers/samcoupe) for the Android
// port (core.h). ROMs: [0] the 32 KB ROM. Media: slot 0 a disk (.mgt .dsk
// .sad .sdf: drive 1) or a tape (.tap .tzx), slot 1 a disk in drive 2; what
// the SAM writes on a disk stays in memory. Key codes: the SAM's matrix
// (SK_* of samcoupe_core.h). The picture: 768 x 312, each line shown twice.
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "samcoupe_core.h"

static uint8_t *rom;
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 1) *n = 1;
    if (roms[0]) return;
    samcoupe_rom_load();
    mc_take(roms, lens, 0, &samcoupe_rom, 0x8000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    const bool ok = n >= 1 && roms[0] && lens[0] >= 0x8000;
    for (int i = 1; i < n; i++) free(roms[i]);
    if (!ok) {
        if (n >= 1) free(roms[0]);
        return false;
    }
    free(rom);
    rom = roms[0];
    if (!samcoupe_init(rom)) return false;
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { samcoupe_reset(); }
int mc_width(void) { return SAM_W; }
int mc_height(void) { return SAM_H; }
double mc_fps(void) { return samcoupe_fps(); }
int mc_audio_rate(void) { return SAM_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < SK_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        samcoupe_key(c, k);
    }
    return samcoupe_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < SAM_H; y++) samcoupe_render_row(y, argb + y * SAM_W);
}

void mc_joy(int port, unsigned bits)
{
    uint8_t j = 0;
    if (bits & MC_JOY_UP) j |= SAM_JOY_UP;
    if (bits & MC_JOY_DOWN) j |= SAM_JOY_DOWN;
    if (bits & MC_JOY_LEFT) j |= SAM_JOY_LEFT;
    if (bits & MC_JOY_RIGHT) j |= SAM_JOY_RIGHT;
    if (bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) j |= SAM_JOY_FIRE;
    samcoupe_joy(port == 2 ? 1 : 0, j);
}

const char *mc_load(const char *path, int slot) { return samcoupe_load_file(path, slot == 1 ? 1 : 0); }

void mc_eject(int slot)
{
    if (slot == 2) samcoupe_eject_tape();
    else samcoupe_eject_disk(slot == 1 ? 1 : 0);
}

void mc_turbo(bool on) { (void)on; }

int mc_status(void)
{
    return (samcoupe_disk_busy() ? MC_LED1 : 0) | MC_HAS_DISK | (samcoupe_tape_inserted() ? MC_TAPE_LOADED : 0) |
           (samcoupe_tape_playing() ? MC_TAPE_PLAYING : 0);
}

void mc_command(int cmd)
{
    if (cmd == MC_CMD_TAPE_TOGGLE) samcoupe_tape_play(!samcoupe_tape_playing());
    else if (cmd == MC_CMD_AUTOBOOT) samcoupe_boot();
}
