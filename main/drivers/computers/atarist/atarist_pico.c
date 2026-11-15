// atarist_pico.c - Atari ST on the RP2350-PiZero (HDMI): 1 MB of RAM (in the
// PSRAM; 512 KB if that is all there is room for), the picture as 320x200
// (low resolution as it is, medium and high halved) in the middle of the
// 320x240 picture. TOS built in (roms/atarist).
//
// USB keyboard: the ST's keys (Alt ALTERNATE, Ctrl CONTROL, Page Up UNDO,
// Page Down HELP, Home CLR/HOME, the keypad the ST's, F1-F10 the ST's).
// USB mouse: the ST's mouse. F11 media menu (microSD .st .msa .stx into
// drive A), F12 TURBO. A USB gamepad: the joystick (port 1), A / B fire.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "atarist_core.h"
#include "rom_data.h"
#include "../common/comp_sd.h"
#include "../common/comp_pico.h"

#define TOP ((PICO_H - 200) / 2)

// USB HID usages -> the ST's scan codes
static const uint8_t kKeys[][2] = {
    { 0x29, STK_ESC }, { 0x1e, 0x02 }, { 0x1f, 0x03 }, { 0x20, 0x04 }, { 0x21, 0x05 }, { 0x22, 0x06 }, { 0x23, 0x07 },
    { 0x24, 0x08 }, { 0x25, 0x09 }, { 0x26, 0x0a }, { 0x27, 0x0b }, { 0x2d, 0x0c }, { 0x2e, 0x0d },
    { 0x35, STK_BACKQUOTE }, { 0x2a, 0x0e }, { 0x2b, 0x0f },
    { 0x14, 0x10 }, { 0x1a, 0x11 }, { 0x08, 0x12 }, { 0x15, 0x13 }, { 0x17, 0x14 }, { 0x1c, 0x15 }, { 0x18, 0x16 },
    { 0x0c, 0x17 }, { 0x12, 0x18 }, { 0x13, 0x19 }, { 0x2f, 0x1a }, { 0x30, 0x1b }, { 0x28, 0x1c }, { 0x4c, STK_DELETE },
    { 0xe0, STK_CONTROL }, { 0xe4, STK_CONTROL },
    { 0x04, 0x1e }, { 0x16, 0x1f }, { 0x07, 0x20 }, { 0x09, 0x21 }, { 0x0a, 0x22 }, { 0x0b, 0x23 }, { 0x0d, 0x24 },
    { 0x0e, 0x25 }, { 0x0f, 0x26 }, { 0x33, 0x27 }, { 0x34, 0x28 }, { 0x31, STK_BACKSLASH }, { 0x32, STK_BACKSLASH },
    { 0xe1, STK_LSHIFT }, { 0x64, STK_ISO },
    { 0x1d, 0x2c }, { 0x1b, 0x2d }, { 0x06, 0x2e }, { 0x19, 0x2f }, { 0x05, 0x30 }, { 0x11, 0x31 }, { 0x10, 0x32 },
    { 0x36, 0x33 }, { 0x37, 0x34 }, { 0x38, 0x35 }, { 0xe5, STK_RSHIFT }, { 0xe2, STK_ALTERNATE }, { 0xe6, STK_ALTERNATE },
    { 0x2c, STK_SPACE }, { 0x39, STK_CAPS },
    { 0x3a, 0x3b }, { 0x3b, 0x3c }, { 0x3c, 0x3d }, { 0x3d, 0x3e }, { 0x3e, 0x3f }, { 0x3f, 0x40 }, { 0x40, 0x41 },
    { 0x41, 0x42 }, { 0x42, 0x43 }, { 0x43, 0x44 },
    { 0x4a, STK_CLRHOME }, { 0x49, STK_INSERT }, { 0x4b, STK_UNDO }, { 0x4e, STK_HELP },
    { 0x52, STK_UP }, { 0x51, STK_DOWN }, { 0x50, STK_LEFT }, { 0x4f, STK_RIGHT },
    { 0x56, STK_KP_MINUS }, { 0x57, STK_KP_PLUS }, { 0x54, STK_KP_SLASH }, { 0x55, STK_KP_STAR },
    { 0x5f, STK_KP7 }, { 0x60, STK_KP8 }, { 0x61, STK_KP9 }, { 0x5c, STK_KP4 }, { 0x5d, STK_KP5 }, { 0x5e, STK_KP6 },
    { 0x59, STK_KP1 }, { 0x5a, STK_KP2 }, { 0x5b, STK_KP3 }, { 0x62, STK_KP0 }, { 0x63, STK_KP_PERIOD },
    { 0x58, STK_KP_ENTER }, { 0x53, STK_KP_LPAREN },
};

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int ly = y + i - TOP;
        if (ly < 0 || ly >= 200) {
            pico_fill(out, PICO_W, 0);
            continue;
        }
        atarist_render_row320(ly, out);
    }
}

static bool load(const char *name, const char **why)
{
    static const char *const lazy[] = { ".st", ".stx", NULL };
    zx_pbuf pb;
    atarist_eject(0);
    if (!comp_sd_pbuf(name, lazy, &pb, why)) return false;
    const bool ok = atarist_load_pbuf(0, &pb, name);
    *why = ok ? "" : atarist_last_error();
    return ok;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "EJECT THE DISK";
    case 1: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) atarist_eject(0);
    if (i == 1) atarist_reset();
}

static void frame(bool sound)
{
    const int16_t *audio;
    atarist_set_turbo(!sound);
    const int n = atarist_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    atarist_keys_clear();
    atarist_joy(1, 0);
}

static const char *const kExts[] = ST_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "ATARI ST", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) atarist_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x44) comp_media_menu(&kMenu);                  // F11
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        atarist_keys_clear();
        for (size_t i = 0; i < sizeof kKeys / sizeof kKeys[0]; i++)
            if (pico_port_key(kKeys[i][0])) atarist_key(kKeys[i][1], true);
        const uint32_t p = pico_port_pad();
        unsigned joy = 0;
        if (p & SP_UP) joy |= ST_UP;
        if (p & SP_DOWN) joy |= ST_DOWN;
        if (p & SP_LEFT) joy |= ST_LEFT;
        if (p & SP_RIGHT) joy |= ST_RIGHT;
        if (p & (SP_B1 | SP_B2)) joy |= ST_FIRE;
        atarist_joy(1, joy);
        int dx, dy;
        unsigned b;
        atarist_pointer ptr = { 0 };
        ptr.has_pointer = pico_port_mouse(&dx, &dy, &b);
        ptr.relative = true;
        ptr.dx = dx * 2;                // (the picture's pixels: low resolution's doubled)
        ptr.dy = dy * 2;
        ptr.left = (b & 1) != 0;
        ptr.right = (b & 2) != 0;
        atarist_pointer_input(&ptr);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    const bool ok = atarist_rom_tos && (atarist_init(atarist_rom_tos, ST_ROM_SIZE, 0x100000, ST_AUDIO_RATE) ||
                                        atarist_init(atarist_rom_tos, ST_ROM_SIZE, 0x80000, ST_AUDIO_RATE));
    if (!ok) {
        printf("%s\n", atarist_rom_tos ? "out of memory" : "no TOS built in (roms/atarist/)");
        for (;;) {}
    }
    pico_port_cfg cfg = { "Atari ST", render, NULL, ST_AUDIO_RATE, ST_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
