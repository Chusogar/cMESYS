// ql_pico.c - Sinclair QL on the Waveshare RP2040-PiZero (HDMI). Same core as
// the CYD and the desktop. The 512x256 picture is shown whole (averaged to
// 320x240, as the CYD) or at 1:1 (F11 cycles: whole / left / right half).
//
// A USB keyboard is the QL's (Alt = ALT, Backspace = CTRL+LEFT, Delete =
// CTRL+RIGHT, ` = pound). Not QL keys:
//   F1   mount / unmount the current cartridge in mdv1
//   F9   cartridge menu (microSD .mdv list / firmware / eject)
//   F6   the QL's F1 (F2-F5 are the QL's)     F11  view     F12  reset
//
#include <stdio.h>
#include <string.h>
#include "pico_port.h"
#include "ql_core.h"
#include "ql_cart_host.h"
#include "ql_sd.h"
#include "ql_view.h"
#include "rom_data.h"

static uint8_t ram[QL_RAM_BLOCKS][0x8000];
static volatile int view = QL_VIEW_FIT;

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

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static int v;
    if (y == 0) {
        v = view;
        ql_view_begin(ql_screen(), ql_video_control(), ql_flash());
    }
    for (int i = 0; i < n; i++) ql_view_row(v, y + i, out + i * PICO_W);
}

// The cartridge menu on the screen (and on the UART log): the microSD's .mdv
// files, the firmware's cartridge, eject, close
static void cart_menu(void)
{
    static ql_sd_entry files[QL_SD_MAX_FILES];      // (2.5 KB: not on the 4 KB task stack)
    static const char *items[QL_SD_MAX_FILES + 3];
    static char fw[PICO_OSD_COLS + 1], title[PICO_OSD_COLS + 1];
    static const char help[] = "UP/DOWN  ENTER INSERT IN MDV1  ESC/F9 BACK";
    int nfiles = 0;
    ql_keys_clear();
    bool sd_ok = ql_sd_mount();
    if (sd_ok) nfiles = ql_sd_list_mdv(files, QL_SD_MAX_FILES);
    for (int i = 0; i < nfiles; i++) items[i] = files[i].name;
    snprintf(fw, sizeof fw, "FIRMWARE: %s", ql_mdv_image ? ql_mdv_name : "(NONE)");
    items[nfiles] = fw;
    items[nfiles + 1] = "EJECT";
    items[nfiles + 2] = "CLOSE";
    const int n = nfiles + 3;
    int sel = 0;
    snprintf(title, sizeof title, "MDV1: %s%s", ql_cart_loaded(0) ? ql_cart_host_name() : "(EMPTY)",
             sd_ok ? "" : "  - NO MICROSD CARD");
    printf("\n=== CARTRIDGE MENU (mdv1) ===\n");
    for (int i = 0; i < n; i++) printf("  %2d  %s\n", i, items[i]);
    pico_osd_list(title, items, n, sel, help);
    pico_osd_show(true);

    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x52 || k == 0x51 || k == 0x4b || k == 0x4e) {  // up / down / page up / page down
                const int step = (k == 0x4b || k == 0x4e) ? PICO_OSD_ROWS - 4 : 1;
                sel += (k == 0x52 || k == 0x4b) ? -step : step;
                if (sel < 0) sel = 0;
                if (sel > n - 1) sel = n - 1;
                pico_osd_list(title, items, n, sel, help);
            } else if (k == 0x29 || k == 0x42) {                                   // Esc / F9
                pico_osd_show(false);
                return;
            } else if (k == 0x28 || k == 0x58) {                                   // Enter
                if (sel < nfiles) {
                    pico_osd_text(PICO_OSD_ROWS - 1, "LOADING...", true);
                    if (ql_sd_open(files[sel].name) &&
                        ql_cart_host_load_reader(ql_sd_read_sector, NULL, files[sel].name)) {
                        printf("mdv1: %s\n", files[sel].name);
                    } else {
                        printf("load failed\n");
                        ql_sd_close();
                        pico_osd_text(PICO_OSD_ROWS - 1, "ERROR: NOT A CARTRIDGE (.MDV) OR SD ERROR", true);
                        continue;
                    }
                } else if (sel == nfiles) {
                    ql_sd_close();
                    if (ql_cart_host_firmware()) printf("mdv1: %s\n", ql_mdv_name);
                    else printf("no firmware cartridge\n");
                } else if (sel == nfiles + 1) {
                    ql_sd_close();
                    ql_cart_host_eject();
                    printf("mdv1: empty\n");
                }
                pico_osd_show(false);
                return;
            }
        }
        // keep HDMI/USB alive
        const int16_t *audio;
        int ns = ql_run_frame(&audio);
        pico_port_audio(audio, ns);
        pico_port_frame_done();
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) ql_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x3a) {                                         // F1
                ql_cart_host_toggle();
                printf("mdv1: %s\n", ql_cart_loaded(0) ? ql_cart_host_name() : "empty");
            }
            if (k == 0x42) cart_menu();                              // F9
            if (k == 0x44) view = (view + 1) % QL_VIEW_N;            // F11
            if (k == 0x45) ql_reset();                               // F12
        }
        ql_keys_clear();
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) ql_key(keymap[i].key, true);
        if (pico_port_key(0x2a)) { ql_key(QLK_CTRL, true); ql_key(QLK_LEFT, true); }
        if (pico_port_key(0x4c)) { ql_key(QLK_CTRL, true); ql_key(QLK_RIGHT, true); }
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
