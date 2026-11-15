// amiga_pico.c - Commodore Amiga 500 on the RP2350-PiZero (HDMI), with its
// chip and slow RAM (1 MB) and the disks (880 KB each) in the board's 8 MB
// PSRAM; the Kickstart stays in the flash. The picture is the PAL window's
// middle 240 lines (of 256).
//
// USB keyboard = the Amiga's, by position (as amiga_sdl.c). Host keys:
//   F11 menu (microSD .adf into DF0 / DF1, eject, reset, keypad joystick)
//   F12 TURBO.  F1-F10 are the Amiga's. Left GUI / Page Up = Left Amiga,
//   Right GUI / Menu / Page Down = Right Amiga, End = HELP, Insert = \ |.
// A USB mouse is the Amiga's mouse (port 0); a USB gamepad, or the keypad
// (8 4 6 2, fire 0 / 5 / Right Ctrl), is the joystick (port 1).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "amiga_core.h"
#include "rom_data.h"
#include "../ql/ql_sd.h"
#include "../common/comp_pico.h"

#define TOP 8                               // the first of the 256 lines shown

static bool ready, keypad_joy = true;
static int target;                          // where the next disk goes
static bool held_prev[0x68];

// HID usage -> Amiga raw key
static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x35, AK_GRAVE }, { 0x1e, AK_1 }, { 0x1f, AK_2 }, { 0x20, AK_3 }, { 0x21, AK_4 }, { 0x22, AK_5 },
    { 0x23, AK_6 }, { 0x24, AK_7 }, { 0x25, AK_8 }, { 0x26, AK_9 }, { 0x27, AK_0 }, { 0x2d, AK_MINUS },
    { 0x2e, AK_EQUALS }, { 0x49, AK_BACKSLASH }, { 0x2a, AK_BACKSPACE },
    { 0x2b, AK_TAB }, { 0x14, AK_Q }, { 0x1a, AK_W }, { 0x08, AK_E }, { 0x15, AK_R }, { 0x17, AK_T },
    { 0x1c, AK_Y }, { 0x18, AK_U }, { 0x0c, AK_I }, { 0x12, AK_O }, { 0x13, AK_P }, { 0x2f, AK_LBRACKET },
    { 0x30, AK_RBRACKET }, { 0x28, AK_RETURN },
    { 0xe0, AK_CTRL }, { 0x39, AK_CAPSLOCK }, { 0x04, AK_A }, { 0x16, AK_S }, { 0x07, AK_D }, { 0x09, AK_F },
    { 0x0a, AK_G }, { 0x0b, AK_H }, { 0x0d, AK_J }, { 0x0e, AK_K }, { 0x0f, AK_L }, { 0x33, AK_SEMICOLON },
    { 0x34, AK_QUOTE }, { 0x31, AK_HASH }, { 0x32, AK_HASH },
    { 0xe1, AK_LSHIFT }, { 0x64, AK_LESS }, { 0x1d, AK_Z }, { 0x1b, AK_X }, { 0x06, AK_C }, { 0x19, AK_V },
    { 0x05, AK_B }, { 0x11, AK_N }, { 0x10, AK_M }, { 0x36, AK_COMMA }, { 0x37, AK_PERIOD }, { 0x38, AK_SLASH },
    { 0xe5, AK_RSHIFT }, { 0xe2, AK_LALT }, { 0xe6, AK_RALT },
    { 0xe3, AK_LAMIGA }, { 0x4b, AK_LAMIGA }, { 0xe7, AK_RAMIGA }, { 0x65, AK_RAMIGA }, { 0x4e, AK_RAMIGA },
    { 0x2c, AK_SPACE }, { 0x29, AK_ESC }, { 0x4c, AK_DEL }, { 0x4d, AK_HELP },
    { 0x52, AK_UP }, { 0x51, AK_DOWN }, { 0x50, AK_LEFT }, { 0x4f, AK_RIGHT },
    { 0x3a, AK_F1 }, { 0x3b, AK_F2 }, { 0x3c, AK_F3 }, { 0x3d, AK_F4 }, { 0x3e, AK_F5 },
    { 0x3f, AK_F6 }, { 0x40, AK_F7 }, { 0x41, AK_F8 }, { 0x42, AK_F9 }, { 0x43, AK_F10 },
    { 0x53, AK_KPLPAREN }, { 0x47, AK_KPRPAREN }, { 0x54, AK_KPSLASH }, { 0x55, AK_KPSTAR },
    { 0x56, AK_KPMINUS }, { 0x57, AK_KPPLUS }, { 0x58, AK_KPENTER }, { 0x63, AK_KPDOT },
};
// keypad 1-9, 0 (HID 0x59-0x62)
static const uint8_t keypad[10] = { AK_KP1, AK_KP2, AK_KP3, AK_KP4, AK_KP5, AK_KP6, AK_KP7, AK_KP8, AK_KP9, AK_KP0 };

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    if (!ready) {
        memset(out, 0, (size_t)n * PICO_W * 2);
        return;
    }
    memcpy(out, amiga_frame() + (y + TOP) * AMIGA_W, (size_t)n * AMIGA_W * 2);
}

static bool load(const char *name, const char **why)
{
    size_t size = 0;
    uint8_t *img = ql_sd_load_file(name, &size);
    if (!img) {
        *why = "cannot read the file (out of memory?)";
        return false;
    }
    if (!amiga_insert(target, img, (uint32_t)size, why)) {
        free(img);
        return false;
    }
    printf("DF%d: %s\n", target, name);
    target = 0;
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return target ? "LOAD INTO: DF1 (THEN DF0)" : "LOAD INTO: DF0";
    case 1: return "EJECT DF0 AND DF1";
    case 2: return keypad_joy ? "KEYPAD: JOYSTICK" : "KEYPAD: AMIGA KEYS";
    case 3: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) target = 1 - target;
    else if (i == 1) { amiga_eject(0); amiga_eject(1); pico_osd_toast("DISKS EJECTED", 1500); }
    else if (i == 2) keypad_joy = !keypad_joy;
    else if (i == 3) amiga_reset();
}

static void frame(bool sound)
{
    const int16_t *audio;
    const int n = amiga_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    amiga_keys_release();
    memset(held_prev, 0, sizeof held_prev);
    amiga_joy(0);
    amiga_mouse_buttons(false, false);
}

static const char *const kExts[] = AMIGA_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "AMIGA 500", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) amiga_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x44) comp_media_menu(&kMenu);                  // F11
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        bool held[0x68] = { false };
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) held[keymap[i].key] = true;
        uint8_t joy = 0;
        if (keypad_joy) {
            if (pico_port_key(0x60) || pico_port_key(0x5f) || pico_port_key(0x61)) joy |= AMIGA_JOY_UP;
            if (pico_port_key(0x5a) || pico_port_key(0x59) || pico_port_key(0x5b)) joy |= AMIGA_JOY_DOWN;
            if (pico_port_key(0x5c) || pico_port_key(0x5f) || pico_port_key(0x59)) joy |= AMIGA_JOY_LEFT;
            if (pico_port_key(0x5e) || pico_port_key(0x61) || pico_port_key(0x5b)) joy |= AMIGA_JOY_RIGHT;
            if (pico_port_key(0x62) || pico_port_key(0x5d) || pico_port_key(0xe4)) joy |= AMIGA_JOY_FIRE;
        } else {
            for (int i = 0; i < 10; i++)
                if (pico_port_key(0x59 + i)) held[keypad[i]] = true;
            if (pico_port_key(0xe4)) joy |= AMIGA_JOY_FIRE;
        }
        for (int c = 0; c < 0x68; c++)
            if (held[c] != held_prev[c]) amiga_key(c, held[c]);
        memcpy(held_prev, held, sizeof held_prev);
        const uint32_t p = pico_port_pad();                          // a gamepad
        if (p & SP_UP) joy |= AMIGA_JOY_UP;
        if (p & SP_DOWN) joy |= AMIGA_JOY_DOWN;
        if (p & SP_LEFT) joy |= AMIGA_JOY_LEFT;
        if (p & SP_RIGHT) joy |= AMIGA_JOY_RIGHT;
        if (p & (SP_B1 | SP_B2)) joy |= AMIGA_JOY_FIRE;
        amiga_joy(joy);
        int dx, dy;
        const uint32_t b = pico_port_mouse(&dx, &dy);
        amiga_mouse(dx, dy);
        amiga_mouse_buttons(b & 1, (b & 2) != 0);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_cfg cfg = { "Amiga 500", render, NULL, AMIGA_AUDIO_RATE, AMIGA_FPS, true };
    pico_port_open(&cfg);                   // (the PSRAM's heap is there after this)
    if (!amiga_rom_kick) {
        printf("no Kickstart built in (roms/amiga/, tools/regen.sh amiga)\n");
        pico_osd_text(2, "NO KICKSTART BUILT IN: ROMS/AMIGA, TOOLS/REGEN.SH AMIGA", true);
        pico_osd_show(true);
        for (;;) pico_port_poll();
    }
    if (!amiga_init(amiga_rom_kick, 0x40000)) {
        char msg[PICO_OSD_COLS + 1];
        printf("out of memory for the Amiga's RAM\n");
        pico_osd_text(2, "OUT OF MEMORY FOR THE AMIGA'S 1 MB (NO PSRAM?)", true);
        pico_port_mem_info(msg, sizeof msg);
        pico_osd_text(4, msg, false);
        pico_osd_show(true);
        for (;;) pico_port_poll();
    }
    ready = true;
    pico_port_run(game);
}
