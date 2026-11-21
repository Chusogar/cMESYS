// m_cons.c - any console of main/drivers/consoles (cons.h) for the Android
// port (core.h). No ROMs from the app (a console's BIOS is built into its
// core: rom_data.c). Media: the console's cartridges / discs (a .zip too).
// Key codes: 0..CK_COUNT-1 the console's keys (cons.h); 100.. player 1's
// pad: 100 up, 101 down, 102 left, 103 right, 104-107 buttons 1-4, 108
// START, 109 SELECT, 110 COIN, and 111-114 the cross's diagonals (up-left,
// up-right, down-left, down-right). A game pad: port 1 player 1, port 2
// player 2 (FIRE / FIRE2: buttons 1 / 2).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "cons.h"

#define PAD0 100
static unsigned joy[2];
static uint8_t prev[MC_KEYS];
static bool ready;

// (the consoles' cores load their BIOS themselves: romfs)
void mc_rom_fill(int model, uint8_t **roms, int *lens, int *n) { (void)model, (void)roms, (void)lens, (void)n; }

bool mc_init(int model, uint8_t **roms, const int *lens, int n, int64_t unix_time)
{
    (void)model, (void)lens, (void)unix_time;
    for (int i = 0; i < n; i++) free(roms[i]);
    if (!ready && !cons_init()) return false;
    ready = true;
    memset(prev, 0, sizeof prev);
    cons_reset();
    return true;
}

void mc_reset(void) { cons_reset(); }
int mc_width(void) { return cons_machine.max_w; }
int mc_height(void) { return cons_machine.max_h; }
double mc_fps(void) { return cons_machine.fps; }
int mc_audio_rate(void) { return cons_machine.audio_rate; }

int mc_run_frame(const uint8_t keys[MC_KEYS], const int16_t **audio)
{
    for (int c = 0; c < CK_COUNT && c < MC_KEYS; c++)
        if ((keys[c] != 0) != (prev[c] != 0)) {
            prev[c] = keys[c];
            cons_key(c, keys[c] != 0);
        }
    unsigned b = joy[0];
    static const unsigned bits[] = { CP_UP, CP_DOWN, CP_LEFT, CP_RIGHT, CP_B1, CP_B2, CP_B3, CP_B4,
                                     CP_START, CP_SELECT, CP_COIN };
    for (int i = 0; i < 11; i++)
        if (keys[PAD0 + i]) b |= bits[i];
    if (keys[111]) b |= CP_UP | CP_LEFT;
    if (keys[112]) b |= CP_UP | CP_RIGHT;
    if (keys[113]) b |= CP_DOWN | CP_LEFT;
    if (keys[114]) b |= CP_DOWN | CP_RIGHT;
    cons_pad(0, b);
    cons_pad(1, joy[1]);
    return cons_run_frame(audio);
}

// The picture at the console's largest size (a smaller one in the middle)
void mc_render(uint32_t *argb)
{
    static uint32_t pal[65536];
    static bool pal_ok;
    if (!pal_ok) {
        for (int i = 0; i < 65536; i++) {
            const uint32_t r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
            pal[i] = 0xff000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
        }
        pal_ok = true;
    }
    const int W = cons_machine.max_w, H = cons_machine.max_h;
    int w, h, stride;
    const uint16_t *fb = cons_frame(&w, &h, &stride);
    if (w > W) w = W;
    if (h > H) h = H;
    const int ox = (W - w) / 2, oy = (H - h) / 2;
    if (w != W || h != H)
        for (int i = 0; i < W * H; i++) argb[i] = 0xff000000u;
    for (int y = 0; y < h; y++) {
        uint32_t *o = argb + (oy + y) * W + ox;
        const uint16_t *s = fb + y * stride;
        for (int x = 0; x < w; x++) o[x] = pal[s[x]];
    }
}

void mc_joy(int port, unsigned bits)
{
    if (port < 1 || port > 2) return;
    joy[port - 1] = ((bits & MC_JOY_UP) ? CP_UP : 0) | ((bits & MC_JOY_DOWN) ? CP_DOWN : 0) |
                    ((bits & MC_JOY_LEFT) ? CP_LEFT : 0) | ((bits & MC_JOY_RIGHT) ? CP_RIGHT : 0) |
                    ((bits & MC_JOY_FIRE) ? CP_B1 : 0) | ((bits & MC_JOY_FIRE2) ? CP_B2 : 0);
}

const char *mc_load(const char *path, int slot)
{
    (void)slot;
    static char why[128];
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open the file";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(data);
        return "cannot read the file";
    }
    fclose(f);
    size_t size = (size_t)n, un = 0;
    char inner[96];
    uint8_t *z = cons_unzip(data, size, cons_machine.exts, &un, inner, sizeof inner);
    if (z) {
        free(data);
        data = z;
        size = un;
    }
    const char *err = NULL;
    if (!cons_load(data, size, path, &err)) {
        snprintf(why, sizeof why, "%s", err && err[0] ? err : "cannot load it");
        return why;
    }
    return NULL;
}

void mc_eject(int slot) { (void)slot; cons_eject(); }
void mc_turbo(bool on) { (void)on; }
int mc_status(void) { return 0; }
void mc_command(int cmd) { (void)cmd; }
