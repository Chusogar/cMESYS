// m_apple2gs.c - the Apple IIGS (drivers/computers/apple2gs) for the Android
// port (core.h). ROMs: [0] 341-0728, [1] 341-0748, [2] the video ROM.
// Media: ProDOS-order disks on the SmartPort (slot 0: the boot disk, and a
// power on; 1-3 the other units). Key codes: ADB codes (GSK_*). The mouse:
// the picture is its trackpad.
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "apple2gs_core.h"

static apple2gs *m;
static uint8_t *rom[3];
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 3) *n = 3;
    if (roms[0] && roms[1] && roms[2]) return;
    apple2gs_rom_load();
    mc_take(roms, lens, 0, &a2gs_rom_fc, 0x20000);
    mc_take(roms, lens, 1, &a2gs_rom_fe, 0x20000);
    mc_take(roms, lens, 2, &a2gs_rom_chr, 0x1000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model;
    bool ok = n >= 3;
    for (int i = 0; ok && i < 3; i++) ok = roms[i] && lens[i] == (i < 2 ? 0x20000 : 0x1000);
    if (!ok) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    apple2gs_destroy(m);
    m = NULL;
    for (int i = 0; i < 3; i++) {
        free(rom[i]);
        rom[i] = roms[i];
    }
    for (int i = 3; i < n; i++) free(roms[i]);
    m = apple2gs_create(64, A2GS_AUDIO_RATE);
    if (!m) return false;
    apple2gs_set_roms(m, rom[0], rom[1], rom[2]);
    apple2gs_set_time(m, unix_time);
    apple2gs_reset(m);
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { apple2gs_reset(m); }
int mc_width(void) { return A2GS_W; }
int mc_height(void) { return A2GS_H; }
double mc_fps(void) { return A2GS_FPS; }
int mc_audio_rate(void) { return A2GS_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < GSK_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        apple2gs_key(m, c, k);
    }
    return apple2gs_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < A2GS_H; y++) {
        uint32_t *o = argb + y * A2GS_W;
        apple2gs_render_row(m, y, o);
        for (int x = 0; x < A2GS_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits) { (void)port, (void)bits; }

void mc_mouse(int dx, int dy, unsigned buttons)
{
    apple2gs_mouse(m, dx, dy, (buttons & 3) != 0);
}

const char *mc_load(const char *path, int slot)
{
    if (slot < 0 || slot >= A2GS_DISKS) slot = 0;
    const char *err = apple2gs_load_file(m, slot, path);
    if (!err && slot == 0) apple2gs_reset(m);
    return err;
}

void mc_eject(int slot)
{
    if (slot < 0) for (int u = A2GS_DISKS - 1; u >= 0; u--) apple2gs_eject(m, u);
    else apple2gs_eject(m, slot);
}
void mc_turbo(bool on) { apple2gs_set_turbo(m, on); }
int mc_status(void) { return (apple2gs_disk_busy(m) ? MC_LED1 : 0) | MC_HAS_DISK; }
void mc_command(int cmd) { if (cmd == MC_CMD_RESET_KEY) apple2gs_reset_key(m); }
