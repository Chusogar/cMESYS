// lynx_pico.c - Atari Lynx on the RP2350-PiZero (HDMI): the 160x102 LCD
// doubled to 320x204 in the middle of the 320x240 picture.
//
// USB keyboard: cursors the pad, Z / Left Ctrl A, X / Left Alt B, 1 / 2
// OPTION 1 / 2, Enter / P PAUSE. F9 media menu (microSD .lnx .lyx .o),
// F3 reset, F12 TURBO. A USB gamepad: d-pad, A, B, SELECT = OPTION 1,
// START = PAUSE.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "lynx_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

#define TOP ((PICO_H - LYNX_H * 2) / 2)

static lynx *cpu_m;

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    uint16_t row[LYNX_W];
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int ly = (y + i - TOP) >> 1;
        if (y + i < TOP || ly >= LYNX_H) {
            pico_fill(out, PICO_W, 0);
            continue;
        }
        lynx_render_row565(cpu_m, ly, row);
        for (int x = 0; x < LYNX_W; x++) out[2 * x] = out[2 * x + 1] = row[x];
    }
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".lnx", ".lyx", NULL };
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = lynx_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : lynx_last_error(cpu_m);
    return ok;
}

static const char *item(int i) { return i == 0 ? "RESET" : NULL; }
static void item_do(int i) { if (i == 0) lynx_reset(cpu_m); }

static void frame(bool sound)
{
    const int16_t *audio;
    const int n = lynx_run_frame(cpu_m, &audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void) { lynx_buttons(cpu_m, 0); }

static const char *const kExts[] = LYNX_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "ATARI LYNX", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) lynx_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x3c) lynx_reset(cpu_m);                        // F3
            if (k == 0x42) comp_media_menu(&kMenu);                  // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        const uint32_t p = pico_port_pad();
        unsigned b = 0;
        if (pico_port_key(0x52)) b |= LYNX_UP;
        if (pico_port_key(0x51)) b |= LYNX_DOWN;
        if (pico_port_key(0x50)) b |= LYNX_LEFT;
        if (pico_port_key(0x4f)) b |= LYNX_RIGHT;
        if (pico_port_key(0x1d) || pico_port_key(0xe0)) b |= LYNX_A;
        if (pico_port_key(0x1b) || pico_port_key(0xe2)) b |= LYNX_B;
        if (p & SP_UP) b |= LYNX_UP;
        if (p & SP_DOWN) b |= LYNX_DOWN;
        if (p & SP_LEFT) b |= LYNX_LEFT;
        if (p & SP_RIGHT) b |= LYNX_RIGHT;
        if (p & SP_B1) b |= LYNX_A;
        if (p & SP_B2) b |= LYNX_B;
        if (p & (SP_COIN1 | SP_B3)) b |= LYNX_OPT1;
        if (p & SP_START1) b |= LYNX_PAUSE;
        if (pico_port_key(0x1e)) b |= LYNX_OPT1;
        if (pico_port_key(0x1f)) b |= LYNX_OPT2;
        if (pico_port_key(0x28) || pico_port_key(0x13)) b |= LYNX_PAUSE;
        lynx_buttons(cpu_m, b);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = lynx_create(LYNX_AUDIO_RATE);
    if (!cpu_m) {
        printf("out of memory\n");
        for (;;) {}
    }
    if (lynx_rom_boot) lynx_set_boot_rom(cpu_m, lynx_rom_boot);
    else printf("no Lynx boot ROM built in (roms/lynx/): the built-in loader\n");
    pico_port_cfg cfg = { "Atari Lynx", render, NULL, LYNX_AUDIO_RATE, LYNX_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
