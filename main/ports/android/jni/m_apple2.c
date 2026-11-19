// m_apple2.c - the Apple ][, ][+, //e and //e Enhanced (drivers/computers/apple2)
// for the Android port (core.h). model: 0 ][, 1 ][+, 2 //e, 3 //e Enhanced.
// ROMs: [0] the system ROM, [1] the character ROM, [2] the Disk II's P5
// PROM, [3] the ]['s Programmer's Aid (the last three optional). Media:
// .dsk / .do / .po / .nib in drive 1 (a power-on boots it). Key codes: the
// machine's keys (A2K_* of apple2_core.h).
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "apple2_core.h"

static apple2 *m;
static uint8_t *rom[4];
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    if (*n < 4) *n = 4;
    if (roms[0] && roms[1] && roms[2]) return;
    a2_rom_main = a2_rom_chr = a2_rom_disk = a2_rom_aid = NULL;
    switch (model) {
    case A2_II: apple2_rom_load(); break;
    case A2_IIPLUS: apple2p_rom_load(); break;
    case A2_IIE: apple2e_rom_load(); break;
    default: apple2ee_rom_load(); break;
    }
    mc_take(roms, lens, 0, &a2_rom_main, (int)a2_main_size(model));
    mc_take(roms, lens, 1, &a2_rom_chr, (int)a2_chr_size(model));
    mc_take(roms, lens, 2, &a2_rom_disk, 0x100);
    mc_take(roms, lens, 3, &a2_rom_aid, 0x800);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)unix_time;
    const bool ok = model >= A2_II && model <= A2_IIEE && n >= 1 && roms[0] && lens[0] >= 0x2000;
    if (!ok) {
        for (int i = 0; i < n; i++) free(roms[i]);
        return false;
    }
    apple2_destroy(m);
    m = NULL;
    for (int i = 0; i < 4; i++) {
        free(rom[i]);
        rom[i] = i < n ? roms[i] : NULL;
    }
    for (int i = 4; i < n; i++) free(roms[i]);
    if (rom[2] && lens[2] < 0x100) { free(rom[2]); rom[2] = NULL; }
    if (rom[3] && lens[3] < 0x800) { free(rom[3]); rom[3] = NULL; }
    m = apple2_create(model, A2_AUDIO_RATE);
    if (!m) return false;
    apple2_set_roms(m, rom[0], (uint32_t)lens[0], rom[1], rom[1] ? (uint32_t)lens[1] : 0, rom[2], rom[3]);
    apple2_reset(m);
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { apple2_reset(m); }
int mc_width(void) { return A2_W; }
int mc_height(void) { return A2_H; }
double mc_fps(void) { return A2_FPS; }
int mc_audio_rate(void) { return A2_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < A2K_COUNT && c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        apple2_key(m, c, k);
    }
    return apple2_run_frame(m, audio);
}

void mc_render(uint32_t *argb)
{
    for (int y = 0; y < A2_H; y++) {
        uint32_t *o = argb + y * A2_W;
        apple2_render_row(m, y, o);
        for (int x = 0; x < A2_W; x++) o[x] |= 0xff000000u;
    }
}

void mc_joy(int port, unsigned bits)
{
    (void)port;
    uint8_t j = 0;
    if (bits & MC_JOY_UP) j |= A2_JOY_UP;
    if (bits & MC_JOY_DOWN) j |= A2_JOY_DOWN;
    if (bits & MC_JOY_LEFT) j |= A2_JOY_LEFT;
    if (bits & MC_JOY_RIGHT) j |= A2_JOY_RIGHT;
    if (bits & MC_JOY_FIRE) j |= A2_JOY_B0;
    if (bits & MC_JOY_FIRE2) j |= A2_JOY_B1;
    apple2_joy(m, j);
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    const char *err = apple2_load_file(m, path);
    if (!err) apple2_reset(m);          // the ROM boots the disk at power on
    return err;
}

void mc_eject(int slot) { (void)slot; apple2_eject(m); }
void mc_turbo(bool on) { apple2_set_turbo(m, on); }
int mc_status(void) { return (apple2_drive_led(m) ? MC_LED1 : 0) | (apple2_has_disk_card(m) ? MC_HAS_DISK : 0); }
void mc_command(int cmd) { if (cmd == MC_CMD_RESET_KEY) apple2_reset_key(m); }
