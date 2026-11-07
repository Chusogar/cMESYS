// cpc_pico.c - Amstrad CPC 6128 on the RP2350-PiZero (HDMI).
//
// USB keyboard = the CPC's. Host keys:
//   F9  media menu (microSD .sna .dsk .edsk .cdt .tzx .csw)
//   F6  tape play / pause    F3  reset    F8  cursors + Space / Alt = joystick
//   F10 green monitor        F12 TURBO    keypad = the CPC's f0-f9, . and Enter
// A USB gamepad is the joystick.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "cpc_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

static cpc *cpu_m;
static bool joy_mode, green;

static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, CPCK_A }, { 0x05, CPCK_B }, { 0x06, CPCK_C }, { 0x07, CPCK_D }, { 0x08, CPCK_E }, { 0x09, CPCK_F },
    { 0x0a, CPCK_G }, { 0x0b, CPCK_H }, { 0x0c, CPCK_I }, { 0x0d, CPCK_J }, { 0x0e, CPCK_K }, { 0x0f, CPCK_L },
    { 0x10, CPCK_M }, { 0x11, CPCK_N }, { 0x12, CPCK_O }, { 0x13, CPCK_P }, { 0x14, CPCK_Q }, { 0x15, CPCK_R },
    { 0x16, CPCK_S }, { 0x17, CPCK_T }, { 0x18, CPCK_U }, { 0x19, CPCK_V }, { 0x1a, CPCK_W }, { 0x1b, CPCK_X },
    { 0x1c, CPCK_Y }, { 0x1d, CPCK_Z },
    { 0x1e, CPCK_1 }, { 0x1f, CPCK_2 }, { 0x20, CPCK_3 }, { 0x21, CPCK_4 }, { 0x22, CPCK_5 },
    { 0x23, CPCK_6 }, { 0x24, CPCK_7 }, { 0x25, CPCK_8 }, { 0x26, CPCK_9 }, { 0x27, CPCK_0 },
    { 0x28, CPCK_RETURN }, { 0x29, CPCK_ESC }, { 0x2a, CPCK_DEL }, { 0x2b, CPCK_TAB }, { 0x2c, CPCK_SPACE },
    { 0x2d, CPCK_MINUS }, { 0x2e, CPCK_CARET }, { 0x2f, CPCK_AT }, { 0x30, CPCK_LBRACKET }, { 0x31, CPCK_RBRACKET },
    { 0x33, CPCK_COLON }, { 0x34, CPCK_SEMICOLON }, { 0x35, CPCK_BACKSLASH }, { 0x36, CPCK_COMMA },
    { 0x37, CPCK_PERIOD }, { 0x38, CPCK_SLASH }, { 0x39, CPCK_CAPSLOCK }, { 0x64, CPCK_BACKSLASH },
    { 0x4c, CPCK_CLR }, { 0x52, CPCK_UP }, { 0x51, CPCK_DOWN }, { 0x50, CPCK_LEFT }, { 0x4f, CPCK_RIGHT },
    { 0xe0, CPCK_CTRL }, { 0xe4, CPCK_CTRL }, { 0xe1, CPCK_SHIFT }, { 0xe5, CPCK_SHIFT }, { 0xe2, CPCK_COPY },
    { 0x62, CPCK_F0 }, { 0x59, CPCK_F1 }, { 0x5a, CPCK_F2 }, { 0x5b, CPCK_F3 }, { 0x5c, CPCK_F4 },
    { 0x5d, CPCK_F5 }, { 0x5e, CPCK_F6 }, { 0x5f, CPCK_F7 }, { 0x60, CPCK_F8 }, { 0x61, CPCK_F9 },
    { 0x63, CPCK_KPDOT }, { 0x58, CPCK_KPENTER },
};

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++) cpc_render_row565(cpu_m, CPC_PIC_Y + y + i, CPC_PIC_X, PICO_W, out + i * PICO_W);
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".dsk", ".edsk", ".cdt", ".tzx", ".csw", NULL };
    zx_pbuf pb;
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = cpc_load_pbuf(cpu_m, &pb, name);
    *why = ok ? "" : cpc_last_error(cpu_m);
    return ok;
}

static void tape_toggle(void)
{
    cpc_tape_toggle(cpu_m);
    printf("tape: %s\n", cpc_tape_playing(cpu_m) ? "play" : (cpc_tape_loaded(cpu_m) ? "pause" : "empty"));
    pico_osd_toast(cpc_tape_playing(cpu_m) ? "TAPE PLAY" : (cpc_tape_loaded(cpu_m) ? "TAPE PAUSE" : "NO TAPE"), 1500);
}

static const char *item(int i)
{
    switch (i) {
    case 0: return cpc_tape_playing(cpu_m) ? "TAPE: PAUSE" : "TAPE: PLAY";
    case 1: return "RESET";
    case 2: return green ? "MONITOR: COLOUR" : "MONITOR: GREEN";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) tape_toggle();
    else if (i == 1) cpc_reset(cpu_m);
    else if (i == 2) cpc_set_color_monitor(cpu_m, !(green = !green));
}

static void frame(bool sound)
{
    const int16_t *audio;
    cpc_set_turbo(cpu_m, !sound);         // (no sound, less drawing: as fast as it goes)
    const int n = cpc_run_frame(cpu_m, &audio);
    if (sound) pico_port_audio(audio, n);       // turbo: no sound, nothing paces
}

static void release(void) { cpc_keys_clear(cpu_m); cpc_joy(cpu_m, 0); }

static const char *const kExts[] = CPC_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "AMSTRAD CPC 6128", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) cpc_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x3f) tape_toggle();                            // F6
            if (k == 0x3c) cpc_reset(cpu_m);                         // F3
            if (k == 0x41) {                                         // F8
                joy_mode = !joy_mode;
                pico_osd_toast(joy_mode ? "CURSORS = JOYSTICK" : "CURSORS = KEYS", 1500);
            }
            if (k == 0x43) cpc_set_color_monitor(cpu_m, !(green = !green));   // F10
            if (k == 0x42) comp_media_menu(&kMenu);                  // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        cpc_keys_clear(cpu_m);
        uint8_t joy = 0;
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
            if (!pico_port_key(keymap[i].usage)) continue;
            const int key = keymap[i].key;
            if (joy_mode && (key == CPCK_UP || key == CPCK_DOWN || key == CPCK_LEFT || key == CPCK_RIGHT ||
                             key == CPCK_SPACE || key == CPCK_COPY)) continue;
            cpc_key(cpu_m, key, true);
        }
        if (joy_mode) {
            if (pico_port_key(0x52)) joy |= CPC_JOY_UP;
            if (pico_port_key(0x51)) joy |= CPC_JOY_DOWN;
            if (pico_port_key(0x50)) joy |= CPC_JOY_LEFT;
            if (pico_port_key(0x4f)) joy |= CPC_JOY_RIGHT;
            if (pico_port_key(0x2c)) joy |= CPC_JOY_FIRE1;
            if (pico_port_key(0xe2)) joy |= CPC_JOY_FIRE2;
        }
        const uint32_t p = pico_port_pad();                          // a gamepad
        if (p & SP_UP) joy |= CPC_JOY_UP;
        if (p & SP_DOWN) joy |= CPC_JOY_DOWN;
        if (p & SP_LEFT) joy |= CPC_JOY_LEFT;
        if (p & SP_RIGHT) joy |= CPC_JOY_RIGHT;
        if (p & SP_B1) joy |= CPC_JOY_FIRE1;
        if (p & SP_B2) joy |= CPC_JOY_FIRE2;
        cpc_joy(cpu_m, joy);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = cpc_create(CPC_6128, CPC_AUDIO_RATE);
    if (!cpu_m || !cpc_rom_os) {
        printf(cpu_m ? "no CPC ROMs built in (roms/cpc6128/)\n" : "out of memory\n");
        for (;;) {}
    }
    cpc_set_roms(cpu_m, cpc_rom_os, cpc_rom_amsdos);
    cpc_reset(cpu_m);
    pico_port_cfg cfg = { "Amstrad CPC 6128", render, NULL, CPC_AUDIO_RATE, CPC_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
