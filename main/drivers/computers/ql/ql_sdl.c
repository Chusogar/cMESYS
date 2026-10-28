// ql_sdl.c - Sinclair QL on the desktop (SDL2) and web (Emscripten).
//
//   build/sdl/ql [cartridge.mdv]
//
// The PC keyboard is the QL's (Alt = ALT, Backspace = CTRL+LEFT, Delete =
// CTRL+RIGHT, ` = pound). Not QL keys:
//   F1   mount / unmount the current cartridge in mdv1 (reset + autoboot)
//   F9   cartridge menu (open from disk / upload / firmware / eject)
//   F6   the QL's F1 (F2-F5 are the QL's)
//   F12  reset            close the window to quit
// Drag-and-drop a .mdv onto the window (or upload on the web) to load it.
//
// Tests: SDL_PORT_FRAMES / SDL_PORT_SHOT (sdl_port.h), QL_MOUNT_AT=n presses
// F1 at frame n.
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "sdl_port.h"
#include "ql_core.h"
#include "ql_cart_host.h"
#include "zx8301.h"
#include "rom_data.h"
#include "../../../ports/esp32-cyd/font5x7.h"

static uint8_t ram[QL_RAM_BLOCKS][0x8000];
static uint32_t frame[QL_W * QL_H];

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_F6, QLK_F1 }, { SDL_SCANCODE_F2, QLK_F2 }, { SDL_SCANCODE_F3, QLK_F3 },
    { SDL_SCANCODE_F4, QLK_F4 }, { SDL_SCANCODE_F5, QLK_F5 },
    { SDL_SCANCODE_1, QLK_1 }, { SDL_SCANCODE_2, QLK_2 }, { SDL_SCANCODE_3, QLK_3 }, { SDL_SCANCODE_4, QLK_4 },
    { SDL_SCANCODE_5, QLK_5 }, { SDL_SCANCODE_6, QLK_6 }, { SDL_SCANCODE_7, QLK_7 }, { SDL_SCANCODE_8, QLK_8 },
    { SDL_SCANCODE_9, QLK_9 }, { SDL_SCANCODE_0, QLK_0 },
    { SDL_SCANCODE_A, QLK_A }, { SDL_SCANCODE_B, QLK_B }, { SDL_SCANCODE_C, QLK_C }, { SDL_SCANCODE_D, QLK_D },
    { SDL_SCANCODE_E, QLK_E }, { SDL_SCANCODE_F, QLK_F }, { SDL_SCANCODE_G, QLK_G }, { SDL_SCANCODE_H, QLK_H },
    { SDL_SCANCODE_I, QLK_I }, { SDL_SCANCODE_J, QLK_J }, { SDL_SCANCODE_K, QLK_K }, { SDL_SCANCODE_L, QLK_L },
    { SDL_SCANCODE_M, QLK_M }, { SDL_SCANCODE_N, QLK_N }, { SDL_SCANCODE_O, QLK_O }, { SDL_SCANCODE_P, QLK_P },
    { SDL_SCANCODE_Q, QLK_Q }, { SDL_SCANCODE_R, QLK_R }, { SDL_SCANCODE_S, QLK_S }, { SDL_SCANCODE_T, QLK_T },
    { SDL_SCANCODE_U, QLK_U }, { SDL_SCANCODE_V, QLK_V }, { SDL_SCANCODE_W, QLK_W }, { SDL_SCANCODE_X, QLK_X },
    { SDL_SCANCODE_Y, QLK_Y }, { SDL_SCANCODE_Z, QLK_Z },
    { SDL_SCANCODE_RETURN, QLK_ENTER }, { SDL_SCANCODE_KP_ENTER, QLK_ENTER }, { SDL_SCANCODE_SPACE, QLK_SPACE },
    { SDL_SCANCODE_ESCAPE, QLK_ESC }, { SDL_SCANCODE_TAB, QLK_TAB }, { SDL_SCANCODE_CAPSLOCK, QLK_CAPS },
    { SDL_SCANCODE_LEFT, QLK_LEFT }, { SDL_SCANCODE_RIGHT, QLK_RIGHT },
    { SDL_SCANCODE_UP, QLK_UP }, { SDL_SCANCODE_DOWN, QLK_DOWN },
    { SDL_SCANCODE_LSHIFT, QLK_SHIFT }, { SDL_SCANCODE_RSHIFT, QLK_SHIFT },
    { SDL_SCANCODE_LCTRL, QLK_CTRL }, { SDL_SCANCODE_RCTRL, QLK_CTRL },
    { SDL_SCANCODE_LALT, QLK_ALT }, { SDL_SCANCODE_RALT, QLK_ALT },
    { SDL_SCANCODE_MINUS, QLK_MINUS }, { SDL_SCANCODE_EQUALS, QLK_EQUALS },
    { SDL_SCANCODE_LEFTBRACKET, QLK_LBRACKET }, { SDL_SCANCODE_RIGHTBRACKET, QLK_RBRACKET },
    { SDL_SCANCODE_SEMICOLON, QLK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, QLK_QUOTE },
    { SDL_SCANCODE_BACKSLASH, QLK_BACKSLASH }, { SDL_SCANCODE_GRAVE, QLK_POUND },
    { SDL_SCANCODE_COMMA, QLK_COMMA }, { SDL_SCANCODE_PERIOD, QLK_PERIOD }, { SDL_SCANCODE_SLASH, QLK_SLASH },
};

// ----------------------------------------------------------------------------
// Cartridge menu
// ----------------------------------------------------------------------------
enum { MI_OPEN = 0, MI_FIRMWARE, MI_EJECT, MI_CLOSE, MI_N };

static bool menu_open;
static int menu_sel;
static char menu_msg[64];

static void set_title(void)
{
    char t[160];
    if (ql_cart_loaded(0))
        snprintf(t, sizeof t, "Sinclair QL - mdv1: %s  (F1 eject, F9 menu)", ql_cart_host_name());
    else
        snprintf(t, sizeof t, "Sinclair QL - mdv1 empty  (F1 remount, F9 menu)");
    sdl_port_set_title(t);
}

static void menu_note(const char *s)
{
    snprintf(menu_msg, sizeof menu_msg, "%s", s ? s : "");
    printf("%s\n", menu_msg);
}

static char *pick_mdv_path(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var el = document.getElementById('ql-mdv-file');
        if (el) el.click();
    });
    return NULL;
#else
    // Prefer zenity, then kdialog; otherwise tell the user to drag-and-drop.
    const char *cmds[] = {
        "zenity --file-selection --title='QL cartridge (.mdv)' --file-filter='QLAY MDV|*.mdv *.MDV' 2>/dev/null",
        "kdialog --getopenfilename . '*.mdv' 2>/dev/null",
        NULL
    };
    for (int i = 0; cmds[i]; i++) {
        FILE *p = popen(cmds[i], "r");
        if (!p) continue;
        char buf[512];
        if (fgets(buf, sizeof buf, p)) {
            size_t n = strlen(buf);
            while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
            pclose(p);
            if (!n) return NULL;
            char *c = (char *)malloc(n + 1);
            if (!c) return NULL;
            memcpy(c, buf, n + 1);
            return c;
        }
        pclose(p);
    }
    menu_note("No file dialog (install zenity). Drag-and-drop a .mdv.");
    return NULL;
#endif
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (ql_cart_host_load_file(path)) {
        char s[80];
        snprintf(s, sizeof s, "Mounted %s", ql_cart_host_name());
        menu_note(s);
    } else {
        menu_note("Not a QLAY .mdv (174930 bytes)");
    }
    set_title();
}

static void menu_activate(void)
{
    switch (menu_sel) {
    case MI_OPEN: {
        char *p = pick_mdv_path();
        if (p) { apply_path(p); free(p); }
#ifdef __EMSCRIPTEN__
        else menu_note("Choose a .mdv in the file dialog…");
#endif
        menu_open = false;
        break;
    }
    case MI_FIRMWARE:
        if (ql_cart_host_firmware()) menu_note("Firmware cartridge mounted");
        else menu_note("No cartridge in firmware");
        set_title();
        menu_open = false;
        break;
    case MI_EJECT:
        ql_cart_host_eject();
        menu_note("mdv1 empty");
        set_title();
        menu_open = false;
        break;
    default:
        menu_open = false;
        break;
    }
}

static void blit_text(uint32_t *rgb, int x0, int y0, const char *s, uint32_t fg)
{
    for (int i = 0; s[i]; i++) {
        int ch = (unsigned char)s[i];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 90) ch = '?';
        const uint8_t *g = font5x7[ch - 32];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 7; row++) {
                if (!(bits & (1u << row))) continue;
                int x = x0 + i * 6 + col, y = y0 + row;
                if ((unsigned)x < QL_W && (unsigned)y < QL_H) rgb[y * QL_W + x] = fg;
            }
        }
    }
}

static void draw_menu(uint32_t *rgb)
{
    static const char *labels[MI_N] = {
#ifdef __EMSCRIPTEN__
        "Upload / choose .mdv",
#else
        "Open .mdv from disk",
#endif
        "Firmware cartridge",
        "Eject mdv1",
        "Close menu",
    };
    for (int i = 0; i < QL_W * QL_H; i++) {
        uint32_t c = rgb[i];
        rgb[i] = ((c >> 2) & 0x3f3f3f) | 0x101018;
    }
    int box_x = 72, box_y = 36, box_w = QL_W - 144, box_h = 168;
    for (int y = box_y; y < box_y + box_h; y++)
        for (int x = box_x; x < box_x + box_w; x++)
            rgb[y * QL_W + x] = 0x1a2030;
    for (int x = box_x; x < box_x + box_w; x++) {
        rgb[box_y * QL_W + x] = 0xc8a000;
        rgb[(box_y + box_h - 1) * QL_W + x] = 0xc8a000;
    }
    blit_text(rgb, box_x + 12, box_y + 8, "CARTRIDGE (mdv1)  F9", 0xffcc66);
    char cur[64];
    snprintf(cur, sizeof cur, "Now: %s", ql_cart_loaded(0) ? ql_cart_host_name() : "(empty)");
    blit_text(rgb, box_x + 12, box_y + 20, cur, 0xa0a8b8);
    for (int i = 0; i < MI_N; i++) {
        int y0 = box_y + 36 + i * 28;
        uint32_t bg = (i == menu_sel) ? 0x3a6ea5 : 0x252b3a;
        for (int y = y0; y < y0 + 22; y++)
            for (int x = box_x + 12; x < box_x + box_w - 12; x++)
                rgb[y * QL_W + x] = bg;
        blit_text(rgb, box_x + 20, y0 + 7, labels[i], 0xf0f0f0);
    }
    if (menu_msg[0]) blit_text(rgb, box_x + 12, box_y + box_h - 16, menu_msg, 0xffcc66);
}

#ifdef __EMSCRIPTEN__
// Called from shell.html after writing /ql_upload.mdv into the MEMFS.
EMSCRIPTEN_KEEPALIVE void ql_web_notify_upload(void)
{
    sdl_port_queue_drop("/ql_upload.mdv");
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!ql_cart_host_load_file(argv[1])) {
            fprintf(stderr, "%s: not a QLAY .mdv (174930 bytes)\n", argv[1]);
            return 1;
        }
    } else if (ql_mdv_image) {
        // Do not auto-boot; user mounts via F1 / menu. Remember firmware as default.
        // kind stays H_NONE until firmware()/toggle — seed via a silent kind set:
        // mounting only on demand keeps the cold boot prompt visible.
    } else {
        fprintf(stderr, "no cartridge: put a .mdv in roms/ql and run tools/regen.sh ql,"
                        " pass one, or F9 / drag-and-drop\n");
    }

    uint8_t *blocks[QL_RAM_BLOCKS];
    for (int i = 0; i < QL_RAM_BLOCKS; i++) blocks[i] = ram[i];
    ql_init(ql_rom, ql_ipc_rom, blocks, (uint32_t)time(NULL));

    sdl_port_cfg cfg = { "Sinclair QL", QL_W, QL_H, 2, QL_AUDIO_RATE, QL_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();

#ifdef __EMSCRIPTEN__
    EM_ASM({
        var bar = document.getElementById('ql-cart-bar');
        if (bar) bar.style.display = 'flex';
        document.body.addEventListener('dragover', function(e){ e.preventDefault(); });
        document.body.addEventListener('drop', function(e){
            e.preventDefault();
            if (!e.dataTransfer || !e.dataTransfer.files || !e.dataTransfer.files.length) return;
            var f = e.dataTransfer.files[0];
            var r = new FileReader();
            r.onload = function(){
                var u8 = new Uint8Array(r.result);
                if (typeof FS !== 'undefined') {
                    FS.writeFile('/ql_upload.mdv', u8);
                    if (Module._ql_web_notify_upload) Module._ql_web_notify_upload();
                }
            };
            r.readAsArrayBuffer(f);
        });
    });
#endif

    const char *e = getenv("QL_MOUNT_AT");
    long mount_at = e ? atol(e) : -1;

    uint8_t row[QL_W];
    uint32_t pal[8];
    for (int i = 0; i < 8; i++) pal[i] = zx8301_rgb(i);
    for (long fno = 0; sdl_port_poll(NULL); fno++) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            apply_path(drop);
            free(drop);
            menu_open = false;
        }

        int k;
        bool mount = fno == mount_at;
        while ((k = sdl_port_next_press()) >= 0) {
            if (menu_open) {
                if (k == SDL_SCANCODE_UP) { if (menu_sel > 0) menu_sel--; }
                else if (k == SDL_SCANCODE_DOWN) { if (menu_sel < MI_N - 1) menu_sel++; }
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_SPACE) menu_activate();
                else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F9) menu_open = false;
                continue;
            }
            if (k == SDL_SCANCODE_F1) mount = true;
            if (k == SDL_SCANCODE_F9) { menu_open = true; menu_sel = 0; }
            if (k == SDL_SCANCODE_F12) ql_reset();
        }
        if (mount) {
            ql_cart_host_toggle();
            printf("mdv1: %s\n", ql_cart_loaded(0) ? ql_cart_host_name() : "empty");
            set_title();
        }

        if (!menu_open) {
            ql_keys_clear();
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) ql_key(keymap[i].key, true);
            if (sdl_port_key(SDL_SCANCODE_BACKSPACE)) { ql_key(QLK_CTRL, true); ql_key(QLK_LEFT, true); }
            if (sdl_port_key(SDL_SCANCODE_DELETE)) { ql_key(QLK_CTRL, true); ql_key(QLK_RIGHT, true); }
        } else {
            ql_keys_clear();
        }

        const int16_t *audio;
        int n = ql_run_frame(&audio);
        sdl_port_audio(audio, n);

        const uint8_t *scr = ql_screen();
        uint8_t ctrl = ql_video_control();
        bool flash = ql_flash();
        for (int y = 0; y < QL_H; y++) {
            zx8301_render_row(scr, ctrl, flash, y, row);
            for (int x = 0; x < QL_W; x++) frame[y * QL_W + x] = pal[row[x]];
        }
        if (menu_open) draw_menu(frame);
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
