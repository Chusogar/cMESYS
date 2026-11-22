// st_pico.c - Atari 1040ST on the RP2350-PiZero (HDMI), its 1 MB of RAM and
// the disks in the board's 8 MB PSRAM; the TOS stays in the flash. The
// picture is 320 x 200 in the 320 x 240 screen (medium and high resolution
// with their pixel pairs mixed: readable, if soft).
//
// USB keyboard = the ST's, by position (as st_sdl.c). Host keys:
//   F11 menu (microSD .st / .msa / .stx into A / B, eject, reset, keypad
//   joystick)   F12 TURBO.  F1-F10 are the ST's. Alt = Alternate, Home =
//   Clr Home, Page Up = Help, Page Down / End = Undo.
// A USB mouse is the ST's mouse; a USB gamepad, or the keypad (8 4 6 2,
// fire 0 / 5 / Right Ctrl), is the joystick (port 1).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "st_core.h"
#include "rom_data.h"
#include "../ql/ql_sd.h"
#include "../common/comp_pico.h"

#define TOP ((PICO_H - 200) / 2)

static bool ready, keypad_joy = true;
static int target;                          // where the next disk goes
static bool held_prev[128];

// HID usage -> ST scan code
static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x29, SK_ESC }, { 0x1e, SK_1 }, { 0x1f, SK_2 }, { 0x20, SK_3 }, { 0x21, SK_4 }, { 0x22, SK_5 },
    { 0x23, SK_6 }, { 0x24, SK_7 }, { 0x25, SK_8 }, { 0x26, SK_9 }, { 0x27, SK_0 }, { 0x2d, SK_MINUS },
    { 0x2e, SK_EQUALS }, { 0x35, SK_GRAVE }, { 0x2a, SK_BACKSPACE },
    { 0x2b, SK_TAB }, { 0x14, SK_Q }, { 0x1a, SK_W }, { 0x08, SK_E }, { 0x15, SK_R }, { 0x17, SK_T },
    { 0x1c, SK_Y }, { 0x18, SK_U }, { 0x0c, SK_I }, { 0x12, SK_O }, { 0x13, SK_P }, { 0x2f, SK_LBRACKET },
    { 0x30, SK_RBRACKET }, { 0x28, SK_RETURN }, { 0x4c, SK_DELETE },
    { 0xe0, SK_CTRL }, { 0x04, SK_A }, { 0x16, SK_S }, { 0x07, SK_D }, { 0x09, SK_F },
    { 0x0a, SK_G }, { 0x0b, SK_H }, { 0x0d, SK_J }, { 0x0e, SK_K }, { 0x0f, SK_L }, { 0x33, SK_SEMICOLON },
    { 0x34, SK_QUOTE }, { 0x31, SK_BACKSLASH }, { 0x32, SK_BACKSLASH },
    { 0xe1, SK_LSHIFT }, { 0x64, SK_ISO }, { 0x1d, SK_Z }, { 0x1b, SK_X }, { 0x06, SK_C }, { 0x19, SK_V },
    { 0x05, SK_B }, { 0x11, SK_N }, { 0x10, SK_M }, { 0x36, SK_COMMA }, { 0x37, SK_PERIOD }, { 0x38, SK_SLASH },
    { 0xe5, SK_RSHIFT }, { 0xe2, SK_ALT }, { 0xe6, SK_ALT }, { 0x2c, SK_SPACE }, { 0x39, SK_CAPSLOCK },
    { 0x3a, SK_F1 }, { 0x3b, SK_F2 }, { 0x3c, SK_F3 }, { 0x3d, SK_F4 }, { 0x3e, SK_F5 },
    { 0x3f, SK_F6 }, { 0x40, SK_F7 }, { 0x41, SK_F8 }, { 0x42, SK_F9 }, { 0x43, SK_F10 },
    { 0x4a, SK_CLRHOME }, { 0x49, SK_INSERT }, { 0x4b, SK_HELP }, { 0x4e, SK_UNDO }, { 0x4d, SK_UNDO },
    { 0x52, SK_UP }, { 0x51, SK_DOWN }, { 0x50, SK_LEFT }, { 0x4f, SK_RIGHT },
    { 0x53, SK_KPLPAREN }, { 0x47, SK_KPRPAREN }, { 0x54, SK_KPSLASH }, { 0x55, SK_KPSTAR },
    { 0x56, SK_KPMINUS }, { 0x57, SK_KPPLUS }, { 0x58, SK_KPENTER }, { 0x63, SK_KPDOT },
};
// keypad 1-9, 0 (HID 0x59-0x62)
static const uint8_t keypad[10] = { SK_KP1, SK_KP2, SK_KP3, SK_KP4, SK_KP5, SK_KP6, SK_KP7, SK_KP8, SK_KP9, SK_KP0 };

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int sy = y + i - TOP;
        if (!ready || sy < 0 || sy >= 200) memset(out, 0, PICO_W * 2);
        else memcpy(out, st_frame() + sy * 320, 320 * 2);
    }
}

static bool load(const char *name, const char **why)
{
    size_t size = 0;
    uint8_t *img = ql_sd_load_file(name, &size);
    if (!img) {
        *why = "cannot read the file (out of memory?)";
        return false;
    }
    if (!st_insert(target, img, (uint32_t)size, name, why)) {
        free(img);
        return false;
    }
    printf("%c: %s\n", 'A' + target, name);
    target = 0;
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return target ? "LOAD INTO: B (THEN A)" : "LOAD INTO: A";
    case 1: return "EJECT A AND B";
    case 2: return keypad_joy ? "KEYPAD: JOYSTICK" : "KEYPAD: ST KEYS";
    case 3: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) target = 1 - target;
    else if (i == 1) { st_eject(0); st_eject(1); pico_osd_toast("DISKS EJECTED", 1500); }
    else if (i == 2) keypad_joy = !keypad_joy;
    else if (i == 3) st_reset();
}

static void frame(bool sound)
{
    const int16_t *audio;
    const int n = st_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    st_keys_release();
    memset(held_prev, 0, sizeof held_prev);
    st_joy(1, 0);
    st_mouse_buttons(false, false);
}

static const char *const kExts[] = ST_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "ATARI ST", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) st_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x44) comp_media_menu(&kMenu);                  // F11
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        bool held[128] = { false };
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) held[keymap[i].key] = true;
        uint8_t joy = 0;
        if (keypad_joy) {
            if (pico_port_key(0x60) || pico_port_key(0x5f) || pico_port_key(0x61)) joy |= ST_JOY_UP;
            if (pico_port_key(0x5a) || pico_port_key(0x59) || pico_port_key(0x5b)) joy |= ST_JOY_DOWN;
            if (pico_port_key(0x5c) || pico_port_key(0x5f) || pico_port_key(0x59)) joy |= ST_JOY_LEFT;
            if (pico_port_key(0x5e) || pico_port_key(0x61) || pico_port_key(0x5b)) joy |= ST_JOY_RIGHT;
            if (pico_port_key(0x62) || pico_port_key(0x5d) || pico_port_key(0xe4)) joy |= ST_JOY_FIRE;
        } else {
            for (int i = 0; i < 10; i++)
                if (pico_port_key(0x59 + i)) held[keypad[i]] = true;
            if (pico_port_key(0xe4)) joy |= ST_JOY_FIRE;
        }
        for (int c = 0; c < 128; c++)
            if (held[c] != held_prev[c]) st_key(c, held[c]);
        memcpy(held_prev, held, sizeof held_prev);
        const uint32_t p = pico_port_pad();                          // a gamepad
        if (p & SP_UP) joy |= ST_JOY_UP;
        if (p & SP_DOWN) joy |= ST_JOY_DOWN;
        if (p & SP_LEFT) joy |= ST_JOY_LEFT;
        if (p & SP_RIGHT) joy |= ST_JOY_RIGHT;
        if (p & (SP_B1 | SP_B2)) joy |= ST_JOY_FIRE;
        st_joy(1, joy);
        int dx, dy;
        const uint32_t b = pico_port_mouse(&dx, &dy);
        st_mouse(dx, dy);
        st_mouse_buttons(b & 1, (b & 2) != 0);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_cfg cfg = { "Atari ST", render, NULL, ST_AUDIO_RATE, ST_FPS, true };
    pico_port_open(&cfg);                   // (the PSRAM's heap is there after this)
    if (!st_rom_load()) pico_port_rom_error();
    if (!st_rom_tos) {
        printf("no TOS built in (roms/atarist/, tools/regen.sh atarist)\n");
        pico_osd_text(2, "NO TOS BUILT IN: ROMS/ATARIST, TOOLS/REGEN.SH ATARIST", true);
        pico_osd_show(true);
        for (;;) pico_port_poll();
    }
    if (!st_init(st_rom_tos, ST_TOS_SIZE, true)) {
        char msg[PICO_OSD_COLS + 1];
        printf("out of memory for the ST's RAM\n");
        pico_osd_text(2, "OUT OF MEMORY FOR THE ST'S 1 MB (NO PSRAM?)", true);
        pico_port_mem_info(msg, sizeof msg);
        pico_osd_text(4, msg, false);
        pico_osd_show(true);
        for (;;) pico_port_poll();
    }
    ready = true;
    pico_port_run(game);
}
