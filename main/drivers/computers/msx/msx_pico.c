// msx_pico.c - MSX1 / MSX2 on the RP2350-PiZero (HDMI). Built as msx1 or
// msx2 (MSX_MODEL). The MSX2 has 64 KB of mapper RAM here and its 128 KB of
// VRAM (the board has no PSRAM: 280 KB of heap), and writes its rows straight
// into the HDMI's picture (no picture of its own).
//
// USB keyboard = the MSX's (as msx_sdl.c). Host keys:
//   F9  media menu (microSD .rom .dsk .cas .tsx)    F10 reset    F11 eject the cartridge
//   F8  cursors + Space / M as joystick 1           F12 TURBO
//   F1-F5 the MSX's, F6 SELECT, End STOP, Left Alt GRAPH, Right Alt CODE.
// A USB gamepad is joystick 1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "msx_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

#ifndef MSX_MODEL
#define MSX_MODEL 1
#endif

static msx *cpu_m;
static bool joy_mode;

static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, MSXK_A }, { 0x05, MSXK_B }, { 0x06, MSXK_C }, { 0x07, MSXK_D }, { 0x08, MSXK_E }, { 0x09, MSXK_F },
    { 0x0a, MSXK_G }, { 0x0b, MSXK_H }, { 0x0c, MSXK_I }, { 0x0d, MSXK_J }, { 0x0e, MSXK_K }, { 0x0f, MSXK_L },
    { 0x10, MSXK_M }, { 0x11, MSXK_N }, { 0x12, MSXK_O }, { 0x13, MSXK_P }, { 0x14, MSXK_Q }, { 0x15, MSXK_R },
    { 0x16, MSXK_S }, { 0x17, MSXK_T }, { 0x18, MSXK_U }, { 0x19, MSXK_V }, { 0x1a, MSXK_W }, { 0x1b, MSXK_X },
    { 0x1c, MSXK_Y }, { 0x1d, MSXK_Z },
    { 0x1e, MSXK_1 }, { 0x1f, MSXK_2 }, { 0x20, MSXK_3 }, { 0x21, MSXK_4 }, { 0x22, MSXK_5 },
    { 0x23, MSXK_6 }, { 0x24, MSXK_7 }, { 0x25, MSXK_8 }, { 0x26, MSXK_9 }, { 0x27, MSXK_0 },
    { 0x28, MSXK_RETURN }, { 0x58, MSXK_RETURN }, { 0x29, MSXK_ESC }, { 0x2a, MSXK_BS }, { 0x2b, MSXK_TAB },
    { 0x2c, MSXK_SPACE }, { 0x2d, MSXK_MINUS }, { 0x2e, MSXK_EQUALS }, { 0x2f, MSXK_LBRACKET },
    { 0x30, MSXK_RBRACKET }, { 0x31, MSXK_BACKSLASH }, { 0x33, MSXK_SEMICOLON }, { 0x34, MSXK_QUOTE },
    { 0x35, MSXK_GRAVE }, { 0x36, MSXK_COMMA }, { 0x37, MSXK_PERIOD }, { 0x38, MSXK_SLASH }, { 0x39, MSXK_CAPS },
    { 0x64, MSXK_DEAD },
    { 0x3a, MSXK_F1 }, { 0x3b, MSXK_F2 }, { 0x3c, MSXK_F3 }, { 0x3d, MSXK_F4 }, { 0x3e, MSXK_F5 },
    { 0x3f, MSXK_SELECT }, { 0x4d, MSXK_STOP }, { 0x4a, MSXK_HOME }, { 0x49, MSXK_INS }, { 0x4c, MSXK_DEL },
    { 0x4f, MSXK_RIGHT }, { 0x50, MSXK_LEFT }, { 0x51, MSXK_DOWN }, { 0x52, MSXK_UP },
    { 0xe0, MSXK_CTRL }, { 0xe1, MSXK_SHIFT }, { 0xe5, MSXK_SHIFT }, { 0xe2, MSXK_GRAPH }, { 0xe6, MSXK_CODE },
    { 0x55, MSXK_KP_MUL }, { 0x57, MSXK_KP_PLUS }, { 0x54, MSXK_KP_DIV }, { 0x56, MSXK_KP_MINUS },
    { 0x59, MSXK_KP1 }, { 0x5b, MSXK_KP3 }, { 0x5f, MSXK_KP7 }, { 0x61, MSXK_KP9 }, { 0x63, MSXK_KP_PERIOD },
};

static void __attribute__((unused)) __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++) msx_render_row565(cpu_m, y + i, out + i * PICO_W);
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".dsk", NULL };
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) {
        // a cartridge bigger than the free memory: read from the card as it
        // is used, through 32 KB of cache
        static const char *const all[] = { ".rom", ".mx1", ".mx2", NULL };
        comp_sd_lazy_slots = 128;
        const bool ok = comp_sd_pbuf(name, all, &pb, why);
        comp_sd_lazy_slots = 8;
        if (!ok) return false;
    }
    const bool ok = msx_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : msx_last_error(cpu_m);
    return ok;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "EJECT CARTRIDGE";
    case 1: return joy_mode ? "CURSORS: KEYS" : "CURSORS: JOYSTICK";
    case 2: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        msx_eject_cart(cpu_m);
        comp_media[0] = 0;
    } else if (i == 1) joy_mode = !joy_mode;
    else msx_reset(cpu_m);
}

static void frame(bool sound)
{
    const int16_t *audio;
    msx_set_turbo(cpu_m, !sound);
    const int n = msx_run_frame(cpu_m, &audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void) { msx_keys_clear(cpu_m); msx_joy(cpu_m, 1, 0); }

static const char *const kExts[] = MSX_MEDIA_EXTS;
static const comp_pico_menu kMenu = { MSX_MODEL == 2 ? "MSX2" : "MSX", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) msx_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x43) msx_reset(cpu_m);                         // F10
            if (k == 0x44) { msx_eject_cart(cpu_m); comp_media[0] = 0; }    // F11
            if (k == 0x41) {                                         // F8
                joy_mode = !joy_mode;
                pico_osd_toast(joy_mode ? "CURSORS = JOYSTICK" : "CURSORS = KEYS", 1500);
            }
            if (k == 0x42) comp_media_menu(&kMenu);                  // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        msx_keys_clear(cpu_m);
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
            if (!pico_port_key(keymap[i].usage)) continue;
            const int key = keymap[i].key;
            if (joy_mode && (key == MSXK_UP || key == MSXK_DOWN || key == MSXK_LEFT || key == MSXK_RIGHT ||
                             key == MSXK_SPACE || key == MSXK_M)) continue;
            msx_key(cpu_m, key, true);
        }
        uint8_t joy = 0;
        if (pico_port_key(0x60) || (joy_mode && pico_port_key(0x52))) joy |= MSX_JOY_UP;      // keypad 8
        if (pico_port_key(0x5a) || (joy_mode && pico_port_key(0x51))) joy |= MSX_JOY_DOWN;    // keypad 2
        if (pico_port_key(0x5c) || (joy_mode && pico_port_key(0x50))) joy |= MSX_JOY_LEFT;    // keypad 4
        if (pico_port_key(0x5e) || (joy_mode && pico_port_key(0x4f))) joy |= MSX_JOY_RIGHT;   // keypad 6
        if (pico_port_key(0x62) || pico_port_key(0x5d) || (joy_mode && pico_port_key(0x2c))) joy |= MSX_JOY_A;
        if (pico_port_key(0xe4) || (joy_mode && pico_port_key(0x10))) joy |= MSX_JOY_B;
        const uint32_t p = pico_port_pad();
        if (p & SP_UP) joy |= MSX_JOY_UP;
        if (p & SP_DOWN) joy |= MSX_JOY_DOWN;
        if (p & SP_LEFT) joy |= MSX_JOY_LEFT;
        if (p & SP_RIGHT) joy |= MSX_JOY_RIGHT;
        if (p & SP_B1) joy |= MSX_JOY_A;
        if (p & SP_B2) joy |= MSX_JOY_B;
        if (p & SP_START1) msx_key(cpu_m, MSXK_SPACE, true);
        msx_joy(cpu_m, 1, joy);
        msx_joy(cpu_m, 2, 0);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = msx_create(MSX_MODEL, 64, 128, MSX_AUDIO_RATE);
    if (!cpu_m || !msx_rom_bios || (MSX_MODEL == 2 && !msx_rom_sub)) {
        printf(cpu_m ? "no MSX ROMs built in (roms/msx%d/)\n" : "out of memory\n", MSX_MODEL);
        for (;;) {}
    }
    msx_set_roms(cpu_m, msx_rom_bios, msx_rom_sub, msx_rom_disk);
#ifdef PIZERO_FB
    msx_set_row_out(cpu_m, pico_port_fb_row);
    pico_port_cfg cfg = { MSX_MODEL == 2 ? "MSX2" : "MSX", NULL, NULL, MSX_AUDIO_RATE, msx_fps(cpu_m), true };
#else
    pico_port_cfg cfg = { MSX_MODEL == 2 ? "MSX2" : "MSX", render, NULL, MSX_AUDIO_RATE, msx_fps(cpu_m), true };
#endif
    pico_port_open(&cfg);
    pico_port_run(game);
}
