// m_atarist.c - the Atari 1040ST (drivers/computers/atarist) for the Android
// port (core.h). ROMs: [0] the TOS (192 KB: 1.04, 1.02, 1.00 or EmuTOS; the
// "tos" setting picks which). Media: .st / .msa / .stx into A (slot 0) or B
// (slot 1). Key codes: the IKBD's scan codes (st_core.h).
#include <stdio.h>
#include <stdlib.h>

#include "core.h"
#include "rom_data.h"
#include "st_core.h"

static uint8_t *tos;
static uint8_t prev[MC_KEYS];

void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n)
{
    (void)model;
    if (*n < 1) *n = 1;
    if (roms[0]) return;
    st_rom_load();
    mc_take(roms, lens, 0, &st_rom_tos, ST_TOS_SIZE);
}

// the IKBD's clock from the phone's (UTC) time
static void set_clock(int64_t t)
{
    if (t <= 0) return;
    const int64_t days = t / 86400, secs = t % 86400;
    // civil from days (Howard Hinnant's algorithm)
    const int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
    const int y = (int)(yoe + era * 400 + (m <= 2));
    st_set_time(y, m, d, (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60));
}

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model;
    const bool ok = n >= 1 && roms[0] && lens[0] == ST_TOS_SIZE;
    for (int i = 1; i < n; i++) free(roms[i]);
    if (!ok) {
        if (n >= 1) free(roms[0]);
        return false;
    }
    // (the old TOS is not freed: the core may still point at it until st_init)
    if (!st_init(roms[0], ST_TOS_SIZE, false)) return false;
    free(tos);
    tos = roms[0];
    set_clock(unix_time);
    for (int i = 0; i < MC_KEYS; i++) prev[i] = 0;
    return true;
}

void mc_reset(void) { st_reset(); }
int mc_width(void) { return ST_W; }
int mc_height(void) { return ST_H; }
double mc_fps(void) { return ST_FPS; }
int mc_audio_rate(void) { return ST_AUDIO_RATE; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 1; c < MC_KEYS; c++) {
        const uint8_t k = keys[c] != 0;
        if (k == prev[c]) continue;
        prev[c] = k;
        st_key(c, k);
    }
    return st_run_frame(audio);
}

void mc_render(uint32_t *argb)
{
    const uint16_t *fb = st_frame();
    for (int i = 0; i < ST_W * ST_H; i++) {
        const uint32_t p = fb[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        argb[i] = 0xff000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
    }
}

void mc_joy(int port, unsigned bits)
{
    uint8_t j = 0;
    if (bits & MC_JOY_UP) j |= ST_JOY_UP;
    if (bits & MC_JOY_DOWN) j |= ST_JOY_DOWN;
    if (bits & MC_JOY_LEFT) j |= ST_JOY_LEFT;
    if (bits & MC_JOY_RIGHT) j |= ST_JOY_RIGHT;
    if (bits & (MC_JOY_FIRE | MC_JOY_FIRE2)) j |= ST_JOY_FIRE;
    st_joy(port == 2 ? 0 : 1, j);       // port 1: the joystick port; 2: the mouse's
}

void mc_mouse(int dx, int dy, unsigned buttons)
{
    st_mouse(dx, dy);
    st_mouse_buttons(buttons & 1, (buttons & 2) != 0);
}

const char *mc_load(const char *path, int slot)
{
    static char why[96];
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *img = n > 0 && n < (8 << 20) ? (uint8_t *)malloc((size_t)n) : NULL;
    if (!img || fread(img, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(img);
        return "cannot read the file";
    }
    fclose(f);
    const char *err = NULL;
    if (!st_insert(slot == 1 ? 1 : 0, img, (uint32_t)n, path, &err)) {
        free(img);
        snprintf(why, sizeof why, "%s", err && err[0] ? err : "not an ST disk");
        return why;
    }
    return NULL;
}

void mc_eject(int slot) { st_eject(slot == 1 ? 1 : 0); }
void mc_turbo(bool on) { (void)on; }
int mc_status(void) { return (st_drive_led() ? MC_LED1 : 0) | MC_HAS_DISK; }
void mc_command(int cmd) { (void)cmd; }
