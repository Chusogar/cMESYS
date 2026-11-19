// m_specnext.c - the ZX Spectrum Next (drivers/computers/specnext) for the
// Android port (core.h). ROMs: [0] the TBBlue boot ROM (8 KB). Media: an SD
// card image (.img .mmc .hdf: the app's copy, written as a real card) that
// the boot ROM boots (TBBLUE.FW, NextZXOS), or a .nex program (loaded once
// NextZXOS has started). Key codes: the Next's keys (NK_* of
// specnext_core.h). The picture: 640 x 256, each line shown twice.
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "specnext_core.h"

static uint8_t *rom;
static uint8_t prev[MC_KEYS];
static int nmi_frames, drive_frames;

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 1) *n = 1;
    if (roms[0]) return;
    specnext_rom_load();
    mc_take(roms, lens, 0, &specnext_rom_boot, 0x2000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    const bool ok = n >= 1 && roms[0] && lens[0] >= 0x2000;
    for (int i = 1; i < n; i++) free(roms[i]);
    if (!ok) {
        if (n >= 1) free(roms[0]);
        return false;
    }
    free(rom);
    rom = roms[0];
    if (!specnext_init(rom)) return false;
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    nmi_frames = drive_frames = 0;
    return true;
}

void mc_reset(void) { specnext_hard_reset(); }
int mc_width(void) { return SN_W; }
int mc_height(void) { return SN_H; }
double mc_fps(void) { return specnext_fps(); }
int mc_audio_rate(void) { return SN_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < NK_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        specnext_key(c, k);
    }
    if (nmi_frames) nmi_frames--;
    if (drive_frames) drive_frames--;
    specnext_nmi_buttons(nmi_frames > 0, drive_frames > 0);
    return specnext_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < SN_H; y++) {
        uint32_t *o = argb + y * SN_W;
        specnext_render_row(y, o);
        for (int x = 0; x < SN_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    uint8_t j = 0;
    if (bits & MC_JOY_UP) j |= SN_JOY_UP;
    if (bits & MC_JOY_DOWN) j |= SN_JOY_DOWN;
    if (bits & MC_JOY_LEFT) j |= SN_JOY_LEFT;
    if (bits & MC_JOY_RIGHT) j |= SN_JOY_RIGHT;
    if (bits & MC_JOY_FIRE) j |= SN_JOY_FIRE;
    if (bits & MC_JOY_FIRE2) j |= SN_JOY_FIRE2;
    specnext_joy(port == 2 ? 1 : 0, j);
}

const char *mc_load(const char *path, int slot) { (void)slot; return specnext_load_file(path); }
void mc_eject(int slot) { (void)slot; specnext_eject_sd(); }
void mc_turbo(bool on) { specnext_set_turbo(on); }
int mc_status(void) { return (specnext_sd_busy() ? MC_LED1 : 0) | MC_HAS_DISK; }

void mc_command(int cmd)
{
    if (cmd == MC_CMD_MAGIC) nmi_frames = 5;
    else if (cmd == MC_CMD_DRIVE) drive_frames = 5;
    else if (cmd == MC_CMD_RESET_KEY) specnext_soft_reset();
}
