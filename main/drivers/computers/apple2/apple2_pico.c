// apple2_pico.c - Apple ][, ][+, //e and //e Enhanced on the RP2350-PiZero
// (HDMI). Built as apple2, apple2p, apple2e or apple2ee (A2_MODEL). The
// 560 x 192 picture is drawn 320 x 240: 7 dots in 4 pixels (40 columns
// sharp, 80 columns legible) and every fourth line twice.
//
// USB keyboard = the Apple's (as apple2_sdl.c): Backspace = <-, Delete =
// DELETE, Left Alt / GUI = Open Apple, Right Alt / GUI = Solid Apple. Host keys:
//   F9  media menu (microSD .dsk .do .po .nib into drive 1, which boots it)
//   F10 RESET (CTRL-RESET)    F11 power off and on    F12 TURBO
// A USB gamepad, or the keypad (8 4 6 2, 0 / 5 the buttons), is the joystick.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "apple2_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

#ifndef A2_MODEL
#define A2_MODEL A2_IIEE
#endif

static apple2 *cpu_m;
static bool held_prev[A2K_COUNT];

// HID usage -> the Apple's key
static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x1e, '1' }, { 0x1f, '2' }, { 0x20, '3' }, { 0x21, '4' }, { 0x22, '5' }, { 0x23, '6' }, { 0x24, '7' },
    { 0x25, '8' }, { 0x26, '9' }, { 0x27, '0' },
    { 0x28, A2K_RETURN }, { 0x58, A2K_RETURN }, { 0x29, A2K_ESC }, { 0x2a, A2K_LEFT }, { 0x4c, A2K_DELETE },
    { 0x2b, A2K_TAB }, { 0x2c, A2K_SPACE }, { 0x2d, A2K_MINUS }, { 0x2e, A2K_EQUALS }, { 0x2f, A2K_LBRACKET },
    { 0x30, A2K_RBRACKET }, { 0x31, A2K_BACKSLASH }, { 0x33, A2K_SEMICOLON }, { 0x34, A2K_QUOTE },
    { 0x35, A2K_GRAVE }, { 0x36, A2K_COMMA }, { 0x37, A2K_PERIOD }, { 0x38, A2K_SLASH }, { 0x39, A2K_CAPS },
    { 0x4f, A2K_RIGHT }, { 0x50, A2K_LEFT }, { 0x51, A2K_DOWN }, { 0x52, A2K_UP },
    { 0xe0, A2K_CTRL }, { 0xe4, A2K_CTRL }, { 0xe1, A2K_SHIFT }, { 0xe5, A2K_RSHIFT },
    { 0xe2, A2K_OPEN_APPLE }, { 0xe3, A2K_OPEN_APPLE }, { 0xe6, A2K_CLOSED_APPLE }, { 0xe7, A2K_CLOSED_APPLE },
};

// 560 dots -> 320 pixels: each 7 dots into 4 (weights in quarters of a dot)
static inline uint16_t mix565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r / 7) << 11) | ((g / 7) << 5) | (b / 7));
}

static void __not_in_flash_func(squeeze)(const uint16_t *in, uint16_t *out)
{
    static const uint8_t w[4][3][2] = {      // (source dot, weight in quarters) x 3 per output pixel
        { { 0, 4 }, { 1, 3 }, { 1, 0 } }, { { 1, 1 }, { 2, 4 }, { 3, 2 } },
        { { 3, 2 }, { 4, 4 }, { 5, 1 } }, { { 5, 3 }, { 6, 4 }, { 6, 0 } },
    };
    for (int x = 0; x < A2_W / 7; x++, in += 7, out += 4) {
        for (int o = 0; o < 4; o++) {
            uint32_t r = 0, g = 0, b = 0;
            for (int k = 0; k < 3; k++) {
                const uint16_t c = in[w[o][k][0]];
                const uint32_t q = w[o][k][1];
                r += ((c >> 11) & 31) * q;
                g += ((c >> 5) & 63) * q;
                b += (c & 31) * q;
            }
            out[o] = mix565(r, g, b);
        }
    }
}

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static uint16_t line[A2_W];
    for (int i = 0; i < n; i++) {
        apple2_render_row565(cpu_m, (y + i) * 4 / 5, line);     // 192 lines in 240
        squeeze(line, out + i * PICO_W);
    }
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { NULL };
    if (!apple2_has_disk_card(cpu_m)) {
        *why = "NO DISK II PROM (341-0027) IN THE ROMS";
        return false;
    }
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const char *err = apple2_load_pbuf(cpu_m, &pb, name);
    if (err) {
        *why = err;
        return false;
    }
    apple2_reset(cpu_m);                // (the ROM boots the disk at power on)
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "EJECT THE DISK";
    case 1: return "CTRL-RESET";
    case 2: return "POWER OFF AND ON";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        apple2_eject(cpu_m);
        comp_media[0] = 0;
    } else if (i == 1) apple2_reset_key(cpu_m);
    else apple2_reset(cpu_m);
}

static void frame(bool sound)
{
    const int16_t *audio;
    apple2_set_turbo(cpu_m, !sound);
    const int n = apple2_run_frame(cpu_m, &audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    apple2_keys_clear(cpu_m);
    apple2_joy(cpu_m, 0);
    memset(held_prev, 0, sizeof held_prev);
}

static const char *const kExts[] = A2_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "APPLE II", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x43) apple2_reset_key(cpu_m);                // F10: RESET (CTRL-RESET)
            if (k == 0x44) apple2_reset(cpu_m);                     // F11
            if (k == 0x42) comp_media_menu(&kMenu);                 // F9
            if (k == 0x45) comp_toggle_turbo();                     // F12
        }
        bool held[A2K_COUNT] = { false };
        for (int u = 0x04; u <= 0x1d; u++) held['A' + u - 0x04] = pico_port_key(u);
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) held[keymap[i].key] = true;
        for (int c = 0; c < A2K_COUNT; c++) {
            if (held[c] == held_prev[c]) continue;
            held_prev[c] = held[c];
            apple2_key(cpu_m, c, held[c]);
        }
        uint8_t joy = 0;
        if (pico_port_key(0x60)) joy |= A2_JOY_UP;          // keypad 8
        if (pico_port_key(0x5a)) joy |= A2_JOY_DOWN;        // keypad 2
        if (pico_port_key(0x5c)) joy |= A2_JOY_LEFT;        // keypad 4
        if (pico_port_key(0x5e)) joy |= A2_JOY_RIGHT;       // keypad 6
        if (pico_port_key(0x62)) joy |= A2_JOY_B0;          // keypad 0
        if (pico_port_key(0x5d) || pico_port_key(0x63)) joy |= A2_JOY_B1;   // keypad 5 / .
        const uint32_t p = pico_port_pad();
        if (p & SP_UP) joy |= A2_JOY_UP;
        if (p & SP_DOWN) joy |= A2_JOY_DOWN;
        if (p & SP_LEFT) joy |= A2_JOY_LEFT;
        if (p & SP_RIGHT) joy |= A2_JOY_RIGHT;
        if (p & SP_B1) joy |= A2_JOY_B0;
        if (p & SP_B2) joy |= A2_JOY_B1;
        apple2_joy(cpu_m, joy);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();
    cpu_m = apple2_create(A2_MODEL, A2_AUDIO_RATE);
    if (!a2_rom_load()) pico_port_rom_error();
    if (!cpu_m || !a2_rom_main) {
        printf(cpu_m ? "no Apple II ROMs\n" : "out of memory\n");
        for (;;) {}
    }
    apple2_set_roms(cpu_m, a2_rom_main, a2_main_size(A2_MODEL), a2_rom_chr, a2_rom_chr ? a2_chr_size(A2_MODEL) : 0,
                    a2_rom_disk, a2_rom_aid);
    apple2_reset(cpu_m);
    pico_port_cfg cfg = { apple2_title(A2_MODEL), render, NULL, A2_AUDIO_RATE, A2_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
