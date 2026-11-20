// exelv_pico.c - the Exelvision EXL-100 / EXELTEL (EXELV_MODEL 0 / 1) on the
// RP2350-PiZero (HDMI); the ROMs from the flash or the microSD
// (roms/exl100, roms/exeltel). The 320 x 250 screen is shown 320 x 240 (every
// 25th line left out).
//
// USB keyboard: as the desktop's (MAME's keys for the machine; Shift = SHIFT,
// Ctrl = CTL, Left Alt = FCT, Caps Lock = LOCK, pressed before the key). Host keys:
//   F9 media menu (microSD: cartridges .bin .rom, cassettes .k7 .wav; play /
//      stop the cassette, take them out, reset)   F12 TURBO
// A USB gamepad is the arrows and SPACE.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "exelv_core.h"
#include "rom_data.h"
#include "../common/comp_pico.h"

#ifndef EXELV_MODEL
#define EXELV_MODEL 0
#endif

static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, EK_A }, { 0x05, EK_B }, { 0x06, EK_C }, { 0x07, EK_D }, { 0x08, EK_E }, { 0x09, EK_F }, { 0x0a, EK_G },
    { 0x0b, EK_H }, { 0x0c, EK_I }, { 0x0d, EK_J }, { 0x0e, EK_K }, { 0x0f, EK_L }, { 0x10, EK_M }, { 0x11, EK_N },
    { 0x12, EK_O }, { 0x13, EK_P }, { 0x14, EK_Q }, { 0x15, EK_R }, { 0x16, EK_S }, { 0x17, EK_T }, { 0x18, EK_U },
    { 0x19, EK_V }, { 0x1a, EK_W }, { 0x1b, EK_X }, { 0x1c, EK_Y }, { 0x1d, EK_Z },
    { 0x1e, EK_1 }, { 0x1f, EK_2 }, { 0x20, EK_3 }, { 0x21, EK_4 }, { 0x22, EK_5 }, { 0x23, EK_6 }, { 0x24, EK_7 },
    { 0x25, EK_8 }, { 0x26, EK_9 }, { 0x27, EK_0 },
    { 0x28, EK_ENTER }, { 0x58, EK_ENTER }, { 0x29, EK_ESC }, { 0x2a, EK_BACKSPACE }, { 0x2b, EK_TAB },
    { 0x2c, EK_SPACE }, { 0x2d, EK_HASH }, { 0x2e, EK_UNDERSCORE }, { 0x2f, EK_TILDE }, { 0x30, EK_LESS },
    { 0x31, EK_GREATER }, { 0x33, EK_PERCENT }, { 0x34, EK_PLUS }, { 0x36, EK_QUESTION }, { 0x37, EK_PERIOD },
    { 0x38, EK_SLASH }, { 0x39, EK_LOCK }, { 0x4a, EK_HOME },
    { 0x4f, EK_RIGHT }, { 0x50, EK_LEFT }, { 0x51, EK_DOWN }, { 0x52, EK_UP },
    { 0xe0, EK_CTL }, { 0xe4, EK_CTL }, { 0xe1, EK_SHIFT }, { 0xe5, EK_SHIFT }, { 0xe2, EK_FCT }, { 0xe6, EK_STAR },
};

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static uint16_t line[EXELV_W];
    for (int i = 0; i < n; i++) {
        exelv_render_row565(1 + (y + i) * 250 / PICO_H, line);
        memcpy(out + i * PICO_W, line + (EXELV_W - PICO_W) / 2, PICO_W * sizeof *out);
    }
}

static bool load(const char *name, const char **why)
{
    size_t size = 0;
    uint8_t *data = ql_sd_load_file(name, &size);
    if (!data) { *why = "SD read error or out of memory"; return false; }
    const bool tape = exelv_is_tape(name);
    const char *err = tape ? exelv_insert_tape(data, size) : exelv_insert_cart(data, size);
    free(data);
    if (!err && !tape) exelv_reset();
    *why = err;
    return !err;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return exelv_tape_playing() ? "CASSETTE: STOP" : "CASSETTE: PLAY";
    case 1: return "TAKE CARTRIDGE AND CASSETTE OUT";
    case 2: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        exelv_tape_play(!exelv_tape_playing());
    } else if (i == 1) {
        exelv_eject_cart();
        exelv_eject_tape();
        comp_media[0] = 0;
        exelv_reset();
    } else {
        exelv_reset();
    }
}

static void frame(bool sound)
{
    const int16_t *audio;
    const int n = exelv_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
    size_t saved = 0;
    free(exelv_take_tape_save(&saved));        // (no SAVE to the microSD)
}

static void release(void)
{
    exelv_keys_clear();
    exelv_joy(0);
}

static const char *const kExts[] = EXELV_MEDIA_EXTS;
static const comp_pico_menu kMenu = { EXELV_MODEL ? "EXELTEL" : "EXL-100", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x42) comp_media_menu(&kMenu);                // F9
            if (k == 0x45) comp_toggle_turbo();                    // F12
        }
        bool held[EK_COUNT] = { false };
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) held[keymap[i].key] = true;
        for (int c = 0; c < EK_COUNT; c++) exelv_key(c, held[c]);
        uint8_t joy = 0;
        const uint32_t p = pico_port_pad();
        if (p & SP_UP) joy |= EXELV_JOY_UP;
        if (p & SP_DOWN) joy |= EXELV_JOY_DOWN;
        if (p & SP_LEFT) joy |= EXELV_JOY_LEFT;
        if (p & SP_RIGHT) joy |= EXELV_JOY_RIGHT;
        if (p & (SP_B1 | SP_B2)) joy |= EXELV_JOY_FIRE;
        exelv_joy(joy);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();
    if (!exelv_rom_load()) pico_port_rom_error();
    if (!exelv_init(EXELV_MODEL, exelv_rom_main, exelv_rom_sub, exelv_rom_sys)) {
        printf("no Exelvision ROM, or out of memory\n");
        for (;;) {}
    }
    pico_port_cfg cfg = { EXELV_MODEL ? "EXELTEL" : "EXL-100", render, NULL, EXELV_AUDIO_RATE, exelv_fps(), true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
