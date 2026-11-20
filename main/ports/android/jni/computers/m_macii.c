// m_macii.c - the Macintosh II (drivers/computers/macii) for the Android
// port (core.h). ROMs: [0] the Mac II's (256 KB), [1] the Display Card
// 8*24's (32 KB). Media: a SCSI hard disk image (written in place: the
// app's copy). Key codes: ADB codes (ADBK_*). The mouse: the picture is its
// trackpad.
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "macii_core.h"

static uint8_t *rom[2];
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 2) *n = 2;
    if (roms[0] && roms[1]) return;
    macii_rom_load();
    mc_take(roms, lens, 0, &macii_rom, 0x40000);
    mc_take(roms, lens, 1, &macii_rom_card, 0x8000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    const bool ok = n >= 2 && roms[0] && roms[1] && lens[0] == 0x40000 && lens[1] == 0x8000;
    for (int i = 2; i < n; i++) free(roms[i]);
    if (!ok) {
        for (int i = 0; i < 2 && i < n; i++) free(roms[i]);
        return false;
    }
    if (!macii_init(roms[0], roms[1], 8)) return false;
    for (int i = 0; i < 2; i++) {
        free(rom[i]);
        rom[i] = roms[i];
    }
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { macii_reset(); }
int mc_width(void) { return MACII_W; }
int mc_height(void) { return MACII_H; }
double mc_fps(void) { return MACII_FPS; }
int mc_audio_rate(void) { return MACII_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < ADBK_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        macii_key(c, k);
    }
    return macii_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < MACII_H; y++) {
        uint32_t *o = argb + y * MACII_W;
        macii_render_row(y, o);
        for (int x = 0; x < MACII_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits) { (void)port, (void)bits; }
void mc_mouse(int dx, int dy, unsigned buttons) { macii_mouse(dx, dy, (buttons & 3) != 0); }
const char *mc_load(const char *path, int slot)
{
    (void)slot;
    const char *err = macii_load_file(path);
    if (!err) macii_reset();            // (the ROM looks for SCSI disks at power on)
    return err;
}
void mc_eject(int slot) { (void)slot; macii_detach_scsi(); }
void mc_turbo(bool on) { macii_set_turbo(on); }
int mc_status(void) { return (macii_scsi_busy() ? MC_LED1 : 0) | MC_HAS_DISK; }
void mc_command(int cmd) { (void)cmd; }
