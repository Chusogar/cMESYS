// arc_sdl.c - any arcade game of arc.h on desktop (SDL2: Linux, Windows),
// web (Emscripten) and Android (the arcade app's sdl_port): the frontend all
// of them share. The ROMs come from storage (romfs: roms/<set>/ or MAME's
// <set>.zip; --roms DIR) when they are not built in.
//
//   5 / 6 coin, 1 / 2 start, cursors the joystick, Z / X / C buttons 1-3,
//   F3 reset, P pause, Esc quit (sdl_port.h)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdl_port.h"
#include "arc.h"
#include "../../../rom/romfs.h"

// Tests: ARC_INPUT="frame:bits,..." holds player 1's AP_* bits (hex) from that
// frame on (c = coin, s = start: one frame)
static const char *script;
static unsigned script_bits;

static void run_script(long f, arc_in *in)
{
#ifdef __ANDROID__
    (void)f, (void)in;      // (no strtol in the Android port's libc)
#else
    while (script && *script) {
        char *end;
        const long at = strtol(script, &end, 10);
        if (end == script || *end != ':' || at > f) break;
        const char *a = end + 1;
        if (*a == 'c') { if (at == f) in->coin1 = true; end = (char *)a + 1; }
        else if (*a == 's') { if (at == f) in->p[0] |= AP_START; end = (char *)a + 1; }
        else script_bits = (unsigned)strtoul(a, &end, 16);
        if (at < f) { script = *end == ',' ? end + 1 : end; continue; }
        break;
    }
    in->p[0] |= script_bits;
#endif
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!arc_init()) sdl_port_rom_error();
    static uint32_t rgb[1024 * 1024];
    const int w = arc_game.w, h = arc_game.h;
    sdl_port_cfg cfg = { arc_game.title, w, h, w > 400 || h > 400 ? 2 : 3, arc_game.audio_rate, arc_game.fps, false };
    if (!sdl_port_open(&cfg)) return 1;
#ifndef __ANDROID__
    script = getenv("ARC_INPUT");
#endif
    long frame = 0;
    bool reset;
    while (sdl_port_poll(&reset)) {
        if (reset) arc_reset();
        if (sdl_port_paused()) { sdl_port_present(rgb); continue; }
        const uint32_t k = sdl_port_keys();
        arc_in in;
        memset(&in, 0, sizeof in);
        in.coin1 = (k & SP_COIN1) != 0;
        in.coin2 = (k & SP_COIN2) != 0;
        if (k & SP_UP) in.p[0] |= AP_UP;
        if (k & SP_DOWN) in.p[0] |= AP_DOWN;
        if (k & SP_LEFT) in.p[0] |= AP_LEFT;
        if (k & SP_RIGHT) in.p[0] |= AP_RIGHT;
        if (k & SP_B1) in.p[0] |= AP_B1;
        if (k & SP_B2) in.p[0] |= AP_B2;
        if (k & SP_B3) in.p[0] |= AP_B3;
        if (k & SP_START1) in.p[0] |= AP_START;
        if (k & SP_START2) in.p[1] |= AP_START;
        run_script(frame++, &in);
        arc_input(&in);
        const int16_t *audio;
        const int n = arc_run_frame(&audio);
        if (arc_game.audio_rate) sdl_port_audio(audio, n);
        const uint16_t *fb = arc_frame();
        for (int i = 0; i < w * h; i++) {
            const uint32_t p = fb[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
            rgb[i] = ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
        }
        sdl_port_present(rgb);
    }
    sdl_port_close();
    return 0;
}
