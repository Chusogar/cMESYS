// a2600_pico.c - Atari 2600 / VCS on the RP2350-PiZero (HDMI): the 160x192
// picture doubled across to 320x192, in the middle of the 320x240 picture.
//
// USB keyboard: cursors joystick 1, Z / Left Ctrl / Space FIRE; W A S D
// joystick 2, Q its FIRE. F1 GAME RESET, F2 GAME SELECT, F5 colour / B&W,
// F6 / F7 left / right difficulty. F9 media menu (microSD .a26 .bin .rom),
// F3 power cycle, F12 TURBO, F4 what the emulation costs. A USB gamepad:
// d-pad and A / B FIRE, START = GAME RESET, SELECT = GAME SELECT.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "a2600_core.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

#define TOP ((PICO_H - A2600_H) / 2)

static a2600 *cpu_m;

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    uint16_t row[A2600_W];
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int ly = y + i - TOP;
        if (ly < 0 || ly >= A2600_H) {
            pico_fill(out, PICO_W, 0);
            continue;
        }
        a2600_render_row565(cpu_m, ly, row);
        for (int x = 0; x < A2600_W; x++) out[2 * x] = out[2 * x + 1] = row[x];
    }
}

static bool load(const char *name, const char **why)
{
    zx_pbuf pb;
    a2600_eject(cpu_m);
    if (!comp_sd_pbuf(name, NULL, &pb, why)) return false;
    const bool ok = a2600_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : a2600_last_error(cpu_m);
    return ok;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "POWER CYCLE";
    case 1: return "TV COLOUR / B&W";
    case 2: return "LEFT DIFFICULTY A/B";
    case 3: return "RIGHT DIFFICULTY A/B";
    default: return NULL;
    }
}

static void item_do(int i)
{
    const uint8_t sw = a2600_switches(cpu_m);
    if (i == 0) a2600_reset(cpu_m);
    if (i == 1) a2600_set_switches(cpu_m, sw ^ A2600_SW_COLOUR);
    if (i == 2) a2600_set_switches(cpu_m, sw ^ A2600_SW_DIFF_LEFT_A);
    if (i == 3) a2600_set_switches(cpu_m, sw ^ A2600_SW_DIFF_RIGHT_A);
}

static bool show_stats;
static uint64_t emu_us, stats_t0;
static int stats_frames;

static void stats(uint64_t frame_us)
{
    emu_us += frame_us;
    if (++stats_frames < 60) return;
    const uint64_t now = pico_port_us(), wall = now - stats_t0;
    if (stats_t0 && wall) {
        char line[48];
        snprintf(line, sizeof line, "EMU %u%%  %u.%u FPS", (unsigned)(emu_us * 100 / wall),
                 (unsigned)(stats_frames * 1000000ull / wall), (unsigned)(stats_frames * 10000000ull / wall % 10));
        if (show_stats) pico_osd_toast(line, 1200);
        printf("a2600: %s\n", line);
    }
    stats_t0 = now;
    stats_frames = 0;
    emu_us = 0;
}

static void frame(bool sound)
{
    const int16_t *audio;
    a2600_set_turbo(cpu_m, !sound);
    const uint64_t t0 = pico_port_us();
    const int n = a2600_run_frame(cpu_m, &audio);
    stats(pico_port_us() - t0);
    if (sound) pico_port_audio(audio, n);
}

static void release(void) { a2600_input(cpu_m, 0); }

static const char *const kExts[] = A2600_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "ATARI 2600", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) a2600_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            const uint8_t sw = a2600_switches(cpu_m);
            if (k == 0x3c) a2600_reset(cpu_m);                       // F3
            if (k == 0x3d) {                                         // F4
                show_stats = !show_stats;
                if (!show_stats) pico_osd_toast("", 1);
            }
            if (k == 0x3e) a2600_set_switches(cpu_m, sw ^ A2600_SW_COLOUR);        // F5
            if (k == 0x3f) a2600_set_switches(cpu_m, sw ^ A2600_SW_DIFF_LEFT_A);   // F6
            if (k == 0x40) a2600_set_switches(cpu_m, sw ^ A2600_SW_DIFF_RIGHT_A);  // F7
            if (k == 0x42) comp_media_menu(&kMenu);                  // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        const uint32_t p = pico_port_pad();
        unsigned b = 0;
        if (pico_port_key(0x52) || (p & SP_UP)) b |= A2600_UP;
        if (pico_port_key(0x51) || (p & SP_DOWN)) b |= A2600_DOWN;
        if (pico_port_key(0x50) || (p & SP_LEFT)) b |= A2600_LEFT;
        if (pico_port_key(0x4f) || (p & SP_RIGHT)) b |= A2600_RIGHT;
        if (pico_port_key(0x1d) || pico_port_key(0xe0) || pico_port_key(0x2c) || (p & (SP_B1 | SP_B2)))
            b |= A2600_FIRE;
        unsigned b2 = 0;
        if (pico_port_key(0x1a)) b2 |= A2600_UP;        // W
        if (pico_port_key(0x16)) b2 |= A2600_DOWN;      // S
        if (pico_port_key(0x04)) b2 |= A2600_LEFT;      // A
        if (pico_port_key(0x07)) b2 |= A2600_RIGHT;     // D
        if (pico_port_key(0x14)) b2 |= A2600_FIRE;      // Q
        b |= b2 << A2600_P2;
        if (pico_port_key(0x3a) || (p & SP_START1)) b |= A2600_RESET;             // F1
        if (pico_port_key(0x3b) || (p & (SP_COIN1 | SP_B3))) b |= A2600_SELECT;   // F2
        a2600_input(cpu_m, b);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = a2600_create(A2600_AUDIO_RATE);
    if (!cpu_m) {
        printf("out of memory\n");
        for (;;) {}
    }
    pico_port_cfg cfg = { "Atari 2600", render, NULL, A2600_AUDIO_RATE, A2600_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
