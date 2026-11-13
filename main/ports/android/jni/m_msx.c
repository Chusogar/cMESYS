// m_msx.c - the MSX1 and MSX2 (drivers/computers/msx) for the Android port
// (core.h). model: 1 or 2. ROMs: [0] BIOS + BASIC (32 KB), [1] the MSX2's
// sub-ROM (16 KB), [2] the disk ROM (16 KB, optional). Media: .rom / .mx1 /
// .mx2 cartridges, .dsk disks, .cas / .tsx / .tzx tapes (through the BIOS,
// CLOAD / RUN"CAS:" / BLOAD"CAS:",R typed by the core).
#include <stdlib.h>

#include "core.h"
#include "msx_core.h"

static msx *m;
static uint8_t *rom[3];

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    bool ok = n >= 1 && roms[0] && lens[0] == 0x8000 && (model == MSX_1 || model == MSX_2);
    if (ok && model == MSX_2) ok = n >= 2 && roms[1] && lens[1] == 0x4000;
    if (!ok) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    if (m) msx_destroy(m);
    for (int i = 0; i < 3; i++) {
        free(rom[i]);
        rom[i] = i < n ? roms[i] : NULL;
    }
    if (rom[2] && lens[2] != 0x4000) {
        free(rom[2]);
        rom[2] = NULL;
    }
    for (int i = 3; i < n; i++) free(roms[i]);
    m = msx_create(model, 256, 128, MSX_AUDIO_RATE);
    if (!m) return false;
    msx_set_roms(m, rom[0], model == MSX_2 ? rom[1] : NULL, rom[2]);
    msx_reset(m);
    return true;
}

void mc_reset(void) { msx_reset(m); }
int mc_width(void) { return MSX_PIC_W; }
int mc_height(void) { return MSX_PIC_H; }
double mc_fps(void) { return m ? msx_fps(m) : 50.0; }
int mc_audio_rate(void) { return MSX_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    msx_keys_clear(m);
    for (int c = 0; c < 88; c++)
        if (keys[c]) msx_key(m, c, true);
    return msx_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < MSX_PIC_H; y++) {
        uint32_t *o = argb + y * MSX_PIC_W;
        msx_render_row(m, y, o);
        for (int x = 0; x < MSX_PIC_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    msx_joy(m, port, (uint8_t)(bits & 63));      // the same bits as MSX_JOY_*
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    return msx_load_file(m, path) ? NULL : msx_last_error(m);
}

void mc_eject(int slot) { (void)slot; msx_eject_cart(m); }
void mc_turbo(bool on) { msx_set_turbo(m, on); }
int mc_status(void) { return msx_has_disk_rom(m) ? MC_HAS_DISK : 0; }
void mc_command(int cmd) { (void)cmd; }
