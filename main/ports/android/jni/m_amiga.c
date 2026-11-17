// m_amiga.c - the Commodore Amiga 500 (drivers/computers/amiga) for the
// Android port (core.h). ROMs: [0] the Kickstart (256 or 512 KB: the model
// setting picks which). Media: .adf into DF0 (slot 0) or DF1 (slot 1). Key
// codes: the A500's raw codes (amiga_core.h). Caps Lock: 0x62 is the
// on-screen key, which locks (each change toggles it); 0x70 is a physical
// keyboard's (each press toggles it).
#include <stdio.h>
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "amiga_core.h"

#define KEY_CAPS_PHYS 0x70

static uint8_t *kick;
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 1) *n = 1;
    if (roms[0]) return;
    amiga_rom_load();
    mc_take(roms, lens, 0, &amiga_rom_kick, 0x40000);
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)unix_time;
    bool ok = n >= 1 && roms[0] && (lens[0] == 0x40000 || lens[0] == 0x80000);
    for (int i = 1; i < n; i++) free(roms[i]);
    if (!ok) {
        if (n >= 1) free(roms[0]);
        return false;
    }
    // (the old Kickstart is not freed: the core may still point at it until amiga_init)
    if (!amiga_init(roms[0], (uint32_t)lens[0])) return false;
    free(kick);
    kick = roms[0];
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { amiga_reset(); }
int mc_width(void) { return AMIGA_W; }
int mc_height(void) { return AMIGA_H; }
double mc_fps(void) { return AMIGA_FPS; }
int mc_audio_rate(void) { return AMIGA_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        if (c == AK_CAPSLOCK) amiga_key(AK_CAPSLOCK, true);                 // locked / let go: toggles
        else if (c == KEY_CAPS_PHYS) { if (k) amiga_key(AK_CAPSLOCK, true); }
        else if (c <= AK_RAMIGA) amiga_key(c, k);
    }
    return amiga_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    const uint16_t *fb = amiga_frame();
    for (int i = 0; i < AMIGA_W * AMIGA_H; i++) {
        const uint32_t p = fb[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        argb[i] = 0xff000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
    }
}

void mc_joy(int port, unsigned bits)
{
    (void)port;                                         // (the joystick port: the mouse has the other)
    uint8_t j = 0;
    if (bits & MC_JOY_UP) j |= AMIGA_JOY_UP;
    if (bits & MC_JOY_DOWN) j |= AMIGA_JOY_DOWN;
    if (bits & MC_JOY_LEFT) j |= AMIGA_JOY_LEFT;
    if (bits & MC_JOY_RIGHT) j |= AMIGA_JOY_RIGHT;
    if (bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) j |= AMIGA_JOY_FIRE;
    amiga_joy(j);
}

void mc_mouse(int dx, int dy, unsigned buttons)
{
    amiga_mouse(dx, dy);
    amiga_mouse_buttons(buttons & 1, (buttons & 2) != 0);
}

const char *mc_load(const char *path, int slot)
{
    static char why[96];
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *img = n > 0 && n < (4 << 20) ? (uint8_t *)malloc((size_t)n) : NULL;
    if (!img || fread(img, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(img);
        return "cannot read the file";
    }
    fclose(f);
    const char *err = NULL;
    if (!amiga_insert(slot == 1 ? 1 : 0, img, (uint32_t)n, &err)) {
        free(img);
        snprintf(why, sizeof why, "%s", err && err[0] ? err : "not an ADF");
        return why;
    }
    return NULL;
}

void mc_eject(int slot) { amiga_eject(slot == 1 ? 1 : 0); }
void mc_turbo(bool on) { (void)on; }
int mc_status(void) { return (amiga_drive_led() ? MC_LED1 : 0) | MC_HAS_DISK; }
void mc_command(int cmd) { (void)cmd; }
