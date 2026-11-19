// samcoupe_pico.c - the SAM Coupe on the RP2350-PiZero (HDMI), its 512 KB of
// RAM in the board's PSRAM; the ROM from the flash or the microSD
// (roms/samcoupe). The 768 x 312 picture is shown 320 x 240: the paper and
// 32 dots of border at the sides (two dots each), 24 lines above and below.
//
// USB keyboard = the SAM's (Shift = SHIFT, Left Ctrl = CNTRL, Alt / Right
// Ctrl = SYMBOL, Backspace DELETE, Home EDIT, End INV, F1-F10 = F1-F9 F0;
// F9 in BASIC boots the disk; BOOT in the menu presses RETURN and F9). Host keys:
//   F11 media menu (microSD: disks .mgt .dsk .sdf into drive 1, or 2; tapes
//       .tap .tzx; BOOT, the tape's play / stop, reset)   F12 TURBO
// A USB gamepad, or the keypad (8 4 6 2, 0 / 5 fire), is joystick 1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "samcoupe_core.h"
#include "rom_data.h"
#include "../common/comp_pico.h"

static bool held_prev[SK_COUNT];
static int target_drive;

static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, SK_A }, { 0x05, SK_B }, { 0x06, SK_C }, { 0x07, SK_D }, { 0x08, SK_E }, { 0x09, SK_F }, { 0x0a, SK_G },
    { 0x0b, SK_H }, { 0x0c, SK_I }, { 0x0d, SK_J }, { 0x0e, SK_K }, { 0x0f, SK_L }, { 0x10, SK_M }, { 0x11, SK_N },
    { 0x12, SK_O }, { 0x13, SK_P }, { 0x14, SK_Q }, { 0x15, SK_R }, { 0x16, SK_S }, { 0x17, SK_T }, { 0x18, SK_U },
    { 0x19, SK_V }, { 0x1a, SK_W }, { 0x1b, SK_X }, { 0x1c, SK_Y }, { 0x1d, SK_Z },
    { 0x1e, SK_1 }, { 0x1f, SK_2 }, { 0x20, SK_3 }, { 0x21, SK_4 }, { 0x22, SK_5 }, { 0x23, SK_6 }, { 0x24, SK_7 },
    { 0x25, SK_8 }, { 0x26, SK_9 }, { 0x27, SK_0 },
    { 0x3a, SK_F1 }, { 0x3b, SK_F2 }, { 0x3c, SK_F3 }, { 0x3d, SK_F4 }, { 0x3e, SK_F5 }, { 0x3f, SK_F6 },
    { 0x40, SK_F7 }, { 0x41, SK_F8 }, { 0x42, SK_F9 }, { 0x43, SK_F0 },
    { 0x28, SK_RETURN }, { 0x58, SK_RETURN }, { 0x29, SK_ESC }, { 0x2a, SK_DELETE }, { 0x4c, SK_DELETE },
    { 0x2b, SK_TAB }, { 0x2c, SK_SPACE }, { 0x2d, SK_MINUS }, { 0x56, SK_MINUS }, { 0x2e, SK_EQUALS },
    { 0x30, SK_PLUS }, { 0x57, SK_PLUS }, { 0x33, SK_SEMICOLON }, { 0x34, SK_QUOTE }, { 0x36, SK_COMMA },
    { 0x37, SK_PERIOD }, { 0x38, SK_COLON }, { 0x39, SK_CAPS }, { 0x4a, SK_EDIT }, { 0x4d, SK_INV },
    { 0x4f, SK_RIGHT }, { 0x50, SK_LEFT }, { 0x51, SK_DOWN }, { 0x52, SK_UP },
    { 0xe0, SK_CNTRL }, { 0xe2, SK_SYMBOL }, { 0xe4, SK_SYMBOL }, { 0xe6, SK_SYMBOL },
    { 0xe1, SK_SHIFT }, { 0xe5, SK_SHIFT },
};

static inline uint16_t avg565(uint16_t a, uint16_t b) { return (uint16_t)((((a ^ b) & 0xf7deu) >> 1) + (a & b)); }

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static uint16_t line[SAM_W];
    const int x0 = SAM_PAPER_X - (2 * PICO_W - 512) / 2;            // 64
    const int y0 = SAM_PAPER_Y - (PICO_H - 192) / 2;                // 44
    for (int i = 0; i < n; i++) {
        samcoupe_render_row565(y + i + y0, line);
        uint16_t *o = out + i * PICO_W;
        for (int x = 0; x < PICO_W; x++) o[x] = avg565(line[x0 + 2 * x], line[x0 + 2 * x + 1]);
    }
}

static bool load(const char *name, const char **why)
{
    size_t size = 0;
    uint8_t *data = ql_sd_load_file(name, &size);
    if (!data) { *why = "SD read error or out of memory"; return false; }
    const bool tape = samcoupe_is_tape(name);
    const char *err = tape ? samcoupe_insert_tape(data, (uint32_t)size)
                           : samcoupe_insert_disk(target_drive, data, (uint32_t)size);
    free(data);
    *why = err;
    if (!err && !tape) target_drive = 0;
    return !err;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "BOOT (THE DISK IN DRIVE 1)";
    case 1: return samcoupe_tape_playing() ? "TAPE: STOP" : "TAPE: PLAY";
    case 2: return target_drive ? "NEXT DISK INTO: DRIVE 2" : "NEXT DISK INTO: DRIVE 1";
    case 3: return "TAKE DISKS AND TAPE OUT";
    case 4: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        pico_osd_toast(samcoupe_boot() ? "BOOT" : "NO DISK IN DRIVE 1", 1500);
    } else if (i == 1) {
        samcoupe_tape_play(!samcoupe_tape_playing());
        pico_osd_toast(!samcoupe_tape_inserted() ? "NO TAPE" : samcoupe_tape_playing() ? "TAPE PLAY" : "TAPE STOP", 1500);
    } else if (i == 2) {
        target_drive ^= 1;
    } else if (i == 3) {
        samcoupe_eject_disk(0);
        samcoupe_eject_disk(1);
        samcoupe_eject_tape();
        comp_media[0] = 0;
    } else {
        samcoupe_reset();
    }
}

static void frame(bool sound)
{
    const int16_t *audio;
    const int n = samcoupe_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    samcoupe_keys_clear();
    samcoupe_joy(0, 0);
    memset(held_prev, 0, sizeof held_prev);
}

static const char *const kExts[] = SAM_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "SAM COUPE", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x44) comp_media_menu(&kMenu);                // F11
            if (k == 0x45) comp_toggle_turbo();                    // F12
        }
        bool held[SK_COUNT] = { false };
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) held[keymap[i].key] = true;
        for (int c = 0; c < SK_COUNT; c++) {
            if (held[c] == held_prev[c]) continue;
            held_prev[c] = held[c];
            samcoupe_key(c, held[c]);
        }
        uint8_t joy = 0;
        if (pico_port_key(0x60)) joy |= SAM_JOY_UP;          // keypad 8
        if (pico_port_key(0x5a)) joy |= SAM_JOY_DOWN;        // keypad 2
        if (pico_port_key(0x5c)) joy |= SAM_JOY_LEFT;        // keypad 4
        if (pico_port_key(0x5e)) joy |= SAM_JOY_RIGHT;       // keypad 6
        if (pico_port_key(0x62) || pico_port_key(0x5d)) joy |= SAM_JOY_FIRE;   // keypad 0 / 5
        const uint32_t p = pico_port_pad();
        if (p & SP_UP) joy |= SAM_JOY_UP;
        if (p & SP_DOWN) joy |= SAM_JOY_DOWN;
        if (p & SP_LEFT) joy |= SAM_JOY_LEFT;
        if (p & SP_RIGHT) joy |= SAM_JOY_RIGHT;
        if (p & (SP_B1 | SP_B2)) joy |= SAM_JOY_FIRE;
        samcoupe_joy(0, joy);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();                         // (the PSRAM: the RAM goes there)
    if (!samcoupe_rom_load()) pico_port_rom_error();
    if (!samcoupe_rom || !samcoupe_init(samcoupe_rom)) {
        printf(samcoupe_rom ? "out of memory (760 KB of PSRAM)\n" : "no SAM Coupe ROM\n");
        for (;;) {}
    }
    pico_port_cfg cfg = { "SAM Coupe", render, NULL, SAM_AUDIO_RATE, samcoupe_fps(), true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
