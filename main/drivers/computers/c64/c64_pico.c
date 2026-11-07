// c64_pico.c - Commodore 64 on the RP2350-PiZero (HDMI).
//
// USB keyboard = the C64's (as c64_sdl.c). Host keys:
//   F9  media menu (microSD .prg .t64 .d64 .g64 .tap)    F11 tape play / pause
//   F10 reset    F12 TURBO    F8 joystick port 1 / 2 (keypad 8 4 6 2, 0 / 5 fire)
//   F1 F3 F5 F7 are the C64's (F2 F4 F6: shifted). Esc RUN/STOP, Tab CTRL,
//   Left Alt C=, Home CLR/HOME, Page Down RESTORE, ` <-, Page Up ^, \ pound.
// A USB gamepad is the joystick (in the port F8 chose).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "c64_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

static c64 *cpu_m;
static int joy_port = 2;

static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, C64K_A }, { 0x05, C64K_B }, { 0x06, C64K_C }, { 0x07, C64K_D }, { 0x08, C64K_E }, { 0x09, C64K_F },
    { 0x0a, C64K_G }, { 0x0b, C64K_H }, { 0x0c, C64K_I }, { 0x0d, C64K_J }, { 0x0e, C64K_K }, { 0x0f, C64K_L },
    { 0x10, C64K_M }, { 0x11, C64K_N }, { 0x12, C64K_O }, { 0x13, C64K_P }, { 0x14, C64K_Q }, { 0x15, C64K_R },
    { 0x16, C64K_S }, { 0x17, C64K_T }, { 0x18, C64K_U }, { 0x19, C64K_V }, { 0x1a, C64K_W }, { 0x1b, C64K_X },
    { 0x1c, C64K_Y }, { 0x1d, C64K_Z },
    { 0x1e, C64K_1 }, { 0x1f, C64K_2 }, { 0x20, C64K_3 }, { 0x21, C64K_4 }, { 0x22, C64K_5 },
    { 0x23, C64K_6 }, { 0x24, C64K_7 }, { 0x25, C64K_8 }, { 0x26, C64K_9 }, { 0x27, C64K_0 },
    { 0x28, C64K_RETURN }, { 0x58, C64K_RETURN }, { 0x29, C64K_RUNSTOP }, { 0x2a, C64K_DEL }, { 0x2b, C64K_CTRL },
    { 0x2c, C64K_SPACE }, { 0x2d, C64K_MINUS }, { 0x2e, C64K_EQUALS }, { 0x2f, C64K_AT }, { 0x30, C64K_ASTERISK },
    { 0x31, C64K_POUND }, { 0x33, C64K_COLON }, { 0x34, C64K_SEMICOLON }, { 0x35, C64K_LEFTARROW },
    { 0x36, C64K_COMMA }, { 0x37, C64K_PERIOD }, { 0x38, C64K_SLASH }, { 0x4a, C64K_HOME }, { 0x4b, C64K_UPARROW },
    { 0x3a, C64K_F1 }, { 0x3c, C64K_F3 }, { 0x3e, C64K_F5 }, { 0x40, C64K_F7 },
    { 0x57, C64K_PLUS }, { 0x55, C64K_ASTERISK }, { 0x56, C64K_MINUS }, { 0x54, C64K_SLASH },
    { 0xe1, C64K_LSHIFT }, { 0xe5, C64K_RSHIFT }, { 0xe0, C64K_CTRL }, { 0xe2, C64K_CBM },
};

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++) c64_render_row565(cpu_m, y + i, out + i * PICO_W);
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".d64", ".g64", ".tap", NULL };
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = c64_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : c64_last_error(cpu_m);
    return ok;
}

static void tape_toggle(void)
{
    c64_tape_toggle(cpu_m);
    printf("tape: %s\n", c64_tape_playing(cpu_m) ? "play" : (c64_tape_loaded(cpu_m) ? "pause" : "empty"));
    pico_osd_toast(c64_tape_playing(cpu_m) ? "TAPE PLAY" : (c64_tape_loaded(cpu_m) ? "TAPE PAUSE" : "NO TAPE"), 1500);
}

static void swap_port(void)
{
    joy_port = 3 - joy_port;
    pico_osd_toast(joy_port == 1 ? "JOYSTICK PORT 1" : "JOYSTICK PORT 2", 1500);
}

static const char *item(int i)
{
    switch (i) {
    case 0: return c64_tape_playing(cpu_m) ? "TAPE: PAUSE" : "TAPE: PLAY";
    case 1: return joy_port == 1 ? "JOYSTICK: PORT 2" : "JOYSTICK: PORT 1";
    case 2: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) tape_toggle();
    else if (i == 1) swap_port();
    else if (i == 2) c64_reset(cpu_m);
}

static void frame(bool sound)
{
    const int16_t *audio;
    const int n = c64_run_frame(cpu_m, &audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    c64_keys_clear(cpu_m);
    c64_joy(cpu_m, 1, 0);
    c64_joy(cpu_m, 2, 0);
    c64_restore(cpu_m, false);
}

static const char *const kExts[] = C64_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "COMMODORE 64", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) c64_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x44) tape_toggle();                            // F11
            if (k == 0x43) c64_reset(cpu_m);                         // F10
            if (k == 0x41) swap_port();                              // F8
            if (k == 0x42) comp_media_menu(&kMenu);                  // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        c64_keys_clear(cpu_m);
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) c64_key(cpu_m, keymap[i].key, true);
        // cursors: right / down, Shift for left / up; F2 F4 F6 shifted
        if (pico_port_key(0x4f)) c64_key(cpu_m, C64K_CRSR_LR, true);
        if (pico_port_key(0x51)) c64_key(cpu_m, C64K_CRSR_UD, true);
        if (pico_port_key(0x50)) { c64_key(cpu_m, C64K_CRSR_LR, true); c64_key(cpu_m, C64K_LSHIFT, true); }
        if (pico_port_key(0x52)) { c64_key(cpu_m, C64K_CRSR_UD, true); c64_key(cpu_m, C64K_LSHIFT, true); }
        if (pico_port_key(0x3b)) { c64_key(cpu_m, C64K_F1, true); c64_key(cpu_m, C64K_LSHIFT, true); }
        if (pico_port_key(0x3d)) { c64_key(cpu_m, C64K_F3, true); c64_key(cpu_m, C64K_LSHIFT, true); }
        if (pico_port_key(0x3f)) { c64_key(cpu_m, C64K_F5, true); c64_key(cpu_m, C64K_LSHIFT, true); }
        uint8_t joy = 0;
        if (pico_port_key(0x60)) joy |= C64_JOY_UP;                  // keypad 8 2 4 6, 0 / 5 / Right Ctrl
        if (pico_port_key(0x5a)) joy |= C64_JOY_DOWN;
        if (pico_port_key(0x5c)) joy |= C64_JOY_LEFT;
        if (pico_port_key(0x5e)) joy |= C64_JOY_RIGHT;
        if (pico_port_key(0x62) || pico_port_key(0x5d) || pico_port_key(0xe4)) joy |= C64_JOY_FIRE;
        const uint32_t p = pico_port_pad();                          // a gamepad
        if (p & SP_UP) joy |= C64_JOY_UP;
        if (p & SP_DOWN) joy |= C64_JOY_DOWN;
        if (p & SP_LEFT) joy |= C64_JOY_LEFT;
        if (p & SP_RIGHT) joy |= C64_JOY_RIGHT;
        if (p & (SP_B1 | SP_B2)) joy |= C64_JOY_FIRE;
        c64_joy(cpu_m, joy_port, joy);
        c64_joy(cpu_m, 3 - joy_port, 0);
        c64_restore(cpu_m, pico_port_key(0x4e));                     // Page Down
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = c64_create(C64_AUDIO_RATE);
    if (!cpu_m || !c64_rom_kernal) {
        printf(cpu_m ? "no C64 ROMs built in (roms/c64/)\n" : "out of memory\n");
        for (;;) {}
    }
    c64_set_roms(cpu_m, c64_rom_kernal, c64_rom_basic, c64_rom_chargen, c64_rom_dos1541);
    c64_reset(cpu_m);
    pico_port_cfg cfg = { "Commodore 64", render, NULL, C64_AUDIO_RATE, C64_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
