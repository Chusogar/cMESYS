// atari8_pico.c - Atari 800XL on the RP2350-PiZero (HDMI): the middle 320
// of the 384 columns (the playfield), all 240 lines. The ROMs built in
// (roms/atari800xl).
//
// USB keyboard: the Atari's keys (Right Ctrl CONTROL, Caps CAPS, F7 the
// inverse key, Esc ESC, Backspace DELETE); cursors + Left Ctrl / Left Alt
// joystick 1. F2 START, F3 SELECT, F4 OPTION, F6 BREAK, F9 media menu
// (microSD .atr), F10 cold start, F12 TURBO, F11 what the emulation costs.
// A USB gamepad: joystick 1, A / B FIRE, START = START, SELECT = OPTION.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "atari8_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

#define X0 ((A8_W - PICO_W) / 2)

static atari8 *cpu_m;

// USB HID usages -> the Atari's keys
static const struct { uint8_t usage; int8_t key; } kKeys[] = {
    { 0x04, A8K_A }, { 0x05, A8K_B }, { 0x06, A8K_C }, { 0x07, A8K_D }, { 0x08, A8K_E }, { 0x09, A8K_F },
    { 0x0a, A8K_G }, { 0x0b, A8K_H }, { 0x0c, A8K_I }, { 0x0d, A8K_J }, { 0x0e, A8K_K }, { 0x0f, A8K_L },
    { 0x10, A8K_M }, { 0x11, A8K_N }, { 0x12, A8K_O }, { 0x13, A8K_P }, { 0x14, A8K_Q }, { 0x15, A8K_R },
    { 0x16, A8K_S }, { 0x17, A8K_T }, { 0x18, A8K_U }, { 0x19, A8K_V }, { 0x1a, A8K_W }, { 0x1b, A8K_X },
    { 0x1c, A8K_Y }, { 0x1d, A8K_Z },
    { 0x1e, A8K_1 }, { 0x1f, A8K_2 }, { 0x20, A8K_3 }, { 0x21, A8K_4 }, { 0x22, A8K_5 }, { 0x23, A8K_6 },
    { 0x24, A8K_7 }, { 0x25, A8K_8 }, { 0x26, A8K_9 }, { 0x27, A8K_0 },
    { 0x28, A8K_RETURN }, { 0x58, A8K_RETURN }, { 0x29, A8K_ESC }, { 0x2a, A8K_BACKSPACE }, { 0x4c, A8K_BACKSPACE },
    { 0x2b, A8K_TAB }, { 0x2c, A8K_SPACE }, { 0x2d, A8K_MINUS }, { 0x2e, A8K_EQUALS }, { 0x2f, A8K_PLUS },
    { 0x30, A8K_ASTERISK }, { 0x31, A8K_GREATER }, { 0x64, A8K_LESS }, { 0x33, A8K_SEMICOLON },
    { 0x36, A8K_COMMA }, { 0x37, A8K_PERIOD }, { 0x38, A8K_SLASH }, { 0x39, A8K_CAPS }, { 0x40, A8K_INVERSE },
    { 0x57, A8K_PLUS }, { 0x55, A8K_ASTERISK }, { 0x56, A8K_MINUS }, { 0x54, A8K_SLASH },
};

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++, out += PICO_W) {
        if (y + i >= A8_H) {
            pico_fill(out, PICO_W, 0);
            continue;
        }
        atari8_render_row565(cpu_m, y + i, X0, PICO_W, out);
    }
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".atr", NULL };
    zx_pbuf pb;
    atari8_eject(cpu_m);
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = atari8_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : atari8_last_error(cpu_m);
    return ok;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "EJECT THE DISK (BASIC)";
    case 1: return "COLD START";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) atari8_eject(cpu_m);
    atari8_reset(cpu_m);
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
        printf("atari8: %s\n", line);
    }
    stats_t0 = now;
    stats_frames = 0;
    emu_us = 0;
}

static void frame(bool sound)
{
    const int16_t *audio;
    atari8_set_turbo(cpu_m, !sound);
    const uint64_t t0 = pico_port_us();
    const int n = atari8_run_frame(cpu_m, &audio);
    stats(pico_port_us() - t0);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    atari8_set_key(cpu_m, -1, false, false);
    atari8_joy(cpu_m, 0, 0);
    atari8_console(cpu_m, 0);
}

static const char *const kExts[] = A8_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "ATARI 800XL", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) atari8_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x43) atari8_reset(cpu_m);                      // F10
            if (k == 0x3f) atari8_break(cpu_m);                      // F6
            if (k == 0x44) {                                         // F11
                show_stats = !show_stats;
                if (!show_stats) pico_osd_toast("", 1);
            }
            if (k == 0x42) comp_media_menu(&kMenu);                  // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        int code = -1;
        for (int i = 0; code < 0 && atari8_key_order(i) >= 0; i++) {
            const int want = atari8_key_order(i);
            for (size_t j = 0; j < sizeof kKeys / sizeof kKeys[0]; j++)
                if (kKeys[j].key == want && pico_port_key(kKeys[j].usage)) { code = want; break; }
        }
        const bool shift = pico_port_key(0xe1) || pico_port_key(0xe5);
        const bool ctrl = pico_port_key(0xe4);
        const uint32_t p = pico_port_pad();
        unsigned joy = 0, con = 0;
        if (pico_port_key(0x52) || (p & SP_UP)) joy |= A8_UP;
        if (pico_port_key(0x51) || (p & SP_DOWN)) joy |= A8_DOWN;
        if (pico_port_key(0x50) || (p & SP_LEFT)) joy |= A8_LEFT;
        if (pico_port_key(0x4f) || (p & SP_RIGHT)) joy |= A8_RIGHT;
        if (pico_port_key(0xe0) || pico_port_key(0xe2) || (p & (SP_B1 | SP_B2))) joy |= A8_FIRE;
        if (pico_port_key(0x3b) || (p & SP_START1)) con |= A8_START;
        if (pico_port_key(0x3c)) con |= A8_SELECT;
        if (pico_port_key(0x3d) || (p & (SP_COIN1 | SP_B3))) con |= A8_OPTION;
        atari8_set_key(cpu_m, code, shift, ctrl);
        atari8_joy(cpu_m, 0, joy);
        atari8_console(cpu_m, con);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = atari8_rom_os ? atari8_create(A8_MODEL_XL, A8_AUDIO_RATE, atari8_rom_os, 0x4000, atari8_rom_basic) : NULL;
    if (!cpu_m) {
        printf("%s\n", atari8_rom_os ? "out of memory" : "no Atari OS ROM built in (roms/atari800xl/)");
        for (;;) {}
    }
    pico_port_cfg cfg = { "Atari 800XL", render, NULL, A8_AUDIO_RATE, A8_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
