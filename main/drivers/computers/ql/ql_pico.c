// ql_pico.c - Sinclair QL on the Waveshare RP2040-PiZero (HDMI). Same core as
// the CYD and the desktop. The 512x256 picture is shown whole (averaged to
// 320x240, as the CYD) or at 1:1 (F11 cycles: whole / left / right half).
//
// A USB keyboard is the QL's (Alt = ALT, Backspace = CTRL+LEFT, Delete =
// CTRL+RIGHT, ` = pound). Not QL keys:
//   F1   mount / unmount the cartridge built into the firmware in mdv1: the
//        QL is reset and boots from it
//   F6   the QL's F1 (F2-F5 are the QL's)     F11  view     F12  reset
#include <stdio.h>
#include <string.h>
#include "pico_port.h"
#include "ql_core.h"
#include "ql_view.h"
#include "rom_data.h"

static uint8_t ram[QL_RAM_BLOCKS][0x8000];
static volatile int view = QL_VIEW_FIT;

// HID usage -> QL key
static const struct { uint8_t usage, key; } keymap[] = {
    { 0x3f, QLK_F1 }, { 0x3b, QLK_F2 }, { 0x3c, QLK_F3 }, { 0x3d, QLK_F4 }, { 0x3e, QLK_F5 },
    { 0x1e, QLK_1 }, { 0x1f, QLK_2 }, { 0x20, QLK_3 }, { 0x21, QLK_4 }, { 0x22, QLK_5 },
    { 0x23, QLK_6 }, { 0x24, QLK_7 }, { 0x25, QLK_8 }, { 0x26, QLK_9 }, { 0x27, QLK_0 },
    { 0x04, QLK_A }, { 0x05, QLK_B }, { 0x06, QLK_C }, { 0x07, QLK_D }, { 0x08, QLK_E }, { 0x09, QLK_F },
    { 0x0a, QLK_G }, { 0x0b, QLK_H }, { 0x0c, QLK_I }, { 0x0d, QLK_J }, { 0x0e, QLK_K }, { 0x0f, QLK_L },
    { 0x10, QLK_M }, { 0x11, QLK_N }, { 0x12, QLK_O }, { 0x13, QLK_P }, { 0x14, QLK_Q }, { 0x15, QLK_R },
    { 0x16, QLK_S }, { 0x17, QLK_T }, { 0x18, QLK_U }, { 0x19, QLK_V }, { 0x1a, QLK_W }, { 0x1b, QLK_X },
    { 0x1c, QLK_Y }, { 0x1d, QLK_Z },
    { 0x28, QLK_ENTER }, { 0x58, QLK_ENTER }, { 0x2c, QLK_SPACE }, { 0x29, QLK_ESC }, { 0x2b, QLK_TAB },
    { 0x39, QLK_CAPS }, { 0x50, QLK_LEFT }, { 0x4f, QLK_RIGHT }, { 0x52, QLK_UP }, { 0x51, QLK_DOWN },
    { 0xe1, QLK_SHIFT }, { 0xe5, QLK_SHIFT }, { 0xe0, QLK_CTRL }, { 0xe4, QLK_CTRL },
    { 0xe2, QLK_ALT }, { 0xe6, QLK_ALT },
    { 0x2d, QLK_MINUS }, { 0x2e, QLK_EQUALS }, { 0x2f, QLK_LBRACKET }, { 0x30, QLK_RBRACKET },
    { 0x33, QLK_SEMICOLON }, { 0x34, QLK_QUOTE }, { 0x31, QLK_BACKSLASH }, { 0x35, QLK_POUND },
    { 0x36, QLK_COMMA }, { 0x37, QLK_PERIOD }, { 0x38, QLK_SLASH },
};

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return pico_rgb565(r, g, b); }

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static int v;
    if (y == 0) {
        v = view;
        ql_view_begin(ql_screen(), ql_video_control(), ql_flash());
    }
    for (int i = 0; i < n; i++) ql_view_row(v, y + i, out + i * PICO_W);
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) ql_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x3a && ql_mdv_image) {                 // F1: the cartridge
                ql_mount(ql_cart_loaded(0) ? NULL : ql_mdv_image);
                printf("mdv1: %s\n", ql_cart_loaded(0) ? ql_mdv_name : "empty");
            }
            if (k == 0x44) view = (view + 1) % QL_VIEW_N;    // F11
            if (k == 0x45) ql_reset();                       // F12
        }
        ql_keys_clear();
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) ql_key(keymap[i].key, true);
        if (pico_port_key(0x2a)) { ql_key(QLK_CTRL, true); ql_key(QLK_LEFT, true); }    // Backspace
        if (pico_port_key(0x4c)) { ql_key(QLK_CTRL, true); ql_key(QLK_RIGHT, true); }   // Delete
        // a USB pad: the cursor keys, A = SPACE, B = ENTER
        uint32_t p = pico_port_keys();
        if (p & SP_UP) ql_key(QLK_UP, true);
        if (p & SP_DOWN) ql_key(QLK_DOWN, true);
        if (p & SP_LEFT) ql_key(QLK_LEFT, true);
        if (p & SP_RIGHT) ql_key(QLK_RIGHT, true);
        if (p & SP_B1) ql_key(QLK_SPACE, true);
        if (p & SP_B2) ql_key(QLK_ENTER, true);
        const int16_t *audio;
        int n = ql_run_frame(&audio);
        pico_port_audio(audio, n);
        pico_port_frame_done();
    }
}

int main(void)
{
    uint8_t *blocks[QL_RAM_BLOCKS];
    for (int i = 0; i < QL_RAM_BLOCKS; i++) blocks[i] = ram[i];
    ql_init(ql_rom, ql_ipc_rom, blocks, 0);
    ql_view_init(rgb);
    pico_port_cfg cfg = { "Sinclair QL", render, NULL, QL_AUDIO_RATE, QL_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
