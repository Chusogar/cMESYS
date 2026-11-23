// arc_pico.c - any arcade game of arc.h on the RP2040 / RP2350-PiZero (HDMI,
// 320x240): the frontend all of them share. A picture taller than 240 lines
// loses as many at the top and the bottom (a vertical game's 256: 8 each);
// one up to 360 wide, as many columns at each side (Tetris's 336: 8 each);
// a wider one, every other column. The ROMs come from the microSD
// (roms/<set>/ or <set>.zip) when they are not built in.
//
// Keys: 5 / 6 coin, 1 / 2 start, cursors, Z / X / C (or Ctrl / Alt) buttons,
// F3 reset, F2 what the emulation costs; a USB gamepad as its SP_* bits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "arc.h"

static bool ready, show_stats;
static uint64_t emu_us, stats_t0;
static int stats_frames;

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    const int w = arc_game.w, h = arc_game.h;
    const int xs = w > 360 ? 2 : 1;
    const int sw = w / xs, ox = (PICO_W - sw) / 2;  // negative: cropped
    const int oy = (PICO_H - h) / 2;                // negative: cropped
    const uint16_t *fb = ready ? arc_frame() : NULL;
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int sy = y + i - oy;
        if (!fb || sy < 0 || sy >= h) { pico_fill(out, PICO_W, 0); continue; }
        const uint16_t *src = fb + sy * w + (ox < 0 ? -ox * xs : 0);
        const int cw = sw < PICO_W ? sw : PICO_W;
        if (ox > 0) {
            pico_fill(out, ox, 0);
            pico_fill(out + ox + sw, PICO_W - ox - sw, 0);
        }
        uint16_t *d = out + (ox > 0 ? ox : 0);
        if (xs == 1) memcpy(d, src, (size_t)cw * 2);
        else for (int x = 0; x < cw; x++) d[x] = src[x * 2];
    }
}

static void stats(uint64_t us)
{
    emu_us += us;
    if (++stats_frames < 60) return;
    const uint64_t now = pico_port_us(), wall = now - stats_t0;
    if (stats_t0 && wall) {
        char line[48];
        snprintf(line, sizeof line, "EMU %u%%", (unsigned)(emu_us * 100 / wall));
        if (show_stats) pico_osd_toast(line, 1200);
        printf("%s: %s\n", arc_game.id, line);
    }
    stats_t0 = now;
    stats_frames = 0;
    emu_us = 0;
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) arc_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0)
            if (k == 0x3b) {                                     // F2
                show_stats = !show_stats;
                if (!show_stats) pico_osd_toast("", 1);
            }
        const uint32_t p = pico_port_keys();
        arc_in in;
        memset(&in, 0, sizeof in);
        in.coin1 = (p & SP_COIN1) != 0;
        in.coin2 = (p & SP_COIN2) != 0;
        if (p & SP_UP) in.p[0] |= AP_UP;
        if (p & SP_DOWN) in.p[0] |= AP_DOWN;
        if (p & SP_LEFT) in.p[0] |= AP_LEFT;
        if (p & SP_RIGHT) in.p[0] |= AP_RIGHT;
        if (p & SP_B1) in.p[0] |= AP_B1;
        if (p & SP_B2) in.p[0] |= AP_B2;
        if (p & SP_B3) in.p[0] |= AP_B3;
        if (p & SP_START1) in.p[0] |= AP_START;
        if (p & SP_START2) in.p[1] |= AP_START;
        arc_input(&in);
        const int16_t *audio;
        const uint64_t t0 = pico_port_us();
        const int n = arc_run_frame(&audio);
        stats(pico_port_us() - t0);
        if (arc_game.audio_rate) pico_port_audio(audio, n);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();                      // (the PSRAM heap for the ROMs)
    if (!arc_init()) pico_port_rom_error();
    pico_port_cfg cfg = { arc_game.title, render, NULL, arc_game.audio_rate, arc_game.fps, false };
    pico_port_open(&cfg);
    ready = true;
    pico_port_run(game);
}
