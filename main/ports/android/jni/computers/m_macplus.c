// m_macplus.c - the Macintosh Plus (drivers/computers/macplus) for the
// Android port (core.h). ROMs: [0] the even chip, [1] the odd one. Media: an
// 800K / 1.44 MB floppy or a SCSI hard disk (by its size). Key codes: the
// Mac's (MK_*). The mouse: the picture is its trackpad.
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "macplus_core.h"

static uint8_t *rom[2];
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 2) *n = 2;
    if (roms[0] && roms[1]) return;
    macplus_rom_load();
    mc_take(roms, lens, 0, &macplus_rom_even, 0x10000);
    mc_take(roms, lens, 1, &macplus_rom_odd, 0x10000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    const bool ok = n >= 2 && roms[0] && roms[1] && lens[0] == 0x10000 && lens[1] == 0x10000;
    for (int i = 2; i < n; i++) free(roms[i]);
    if (!ok) {
        for (int i = 0; i < 2 && i < n; i++) free(roms[i]);
        return false;
    }
    if (!macplus_init(roms[0], roms[1])) return false;
    for (int i = 0; i < 2; i++) {
        free(rom[i]);
        rom[i] = roms[i];
    }
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { macplus_reset(); }
int mc_width(void) { return MACPLUS_W; }
int mc_height(void) { return MACPLUS_H; }
double mc_fps(void) { return MACPLUS_FPS; }
int mc_audio_rate(void) { return MACPLUS_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < MK_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        macplus_key(c, k);
    }
    return macplus_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < MACPLUS_H; y++) {
        uint32_t *o = argb + y * MACPLUS_W;
        macplus_render_row(y, o);
        for (int x = 0; x < MACPLUS_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits) { (void)port, (void)bits; }
void mc_mouse(int dx, int dy, unsigned buttons) { macplus_mouse(dx, dy, (buttons & 3) != 0); }
const char *mc_load(const char *path, int slot) { (void)slot; return macplus_load_file(path); }
void mc_eject(int slot)
{
    if (slot == 1) macplus_detach_scsi();
    else macplus_eject_floppy();
}
void mc_turbo(bool on) { macplus_set_turbo(on); }
int mc_status(void) { return (macplus_drive_led() ? MC_LED1 : 0) | MC_HAS_DISK; }
void mc_command(int cmd) { (void)cmd; }
