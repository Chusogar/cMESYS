// dspc_sdl.c - desktop (SDL2: Linux, Windows) and web (Emscripten) frontend
// of the dspc computers (dspc.h): ZX80 / ZX81, VIC-20, Plus/4 / C16, C128,
// Atari 800, PCW.
//
//   <machine> [file]       (a program, cartridge, tape or disk: dspc_load)
//
// The PC keyboard is dsp-cpp's for the machine (every key goes to it), F11
// the menu (load a file, reset), F12 TURBO; a gamepad or the keypad (8 4 6 2,
// 0 / 5 fire) is joystick 1. Drag-and-drop loads a file.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dspc.h"
#include "comp_sdl.h"
#include "../../../rom/romfs.h"

static uint32_t *frame;
static comp_menu mm;
static char media[256];

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, DK_A }, { SDL_SCANCODE_B, DK_B }, { SDL_SCANCODE_C, DK_C }, { SDL_SCANCODE_D, DK_D },
    { SDL_SCANCODE_E, DK_E }, { SDL_SCANCODE_F, DK_F }, { SDL_SCANCODE_G, DK_G }, { SDL_SCANCODE_H, DK_H },
    { SDL_SCANCODE_I, DK_I }, { SDL_SCANCODE_J, DK_J }, { SDL_SCANCODE_K, DK_K }, { SDL_SCANCODE_L, DK_L },
    { SDL_SCANCODE_M, DK_M }, { SDL_SCANCODE_N, DK_N }, { SDL_SCANCODE_O, DK_O }, { SDL_SCANCODE_P, DK_P },
    { SDL_SCANCODE_Q, DK_Q }, { SDL_SCANCODE_R, DK_R }, { SDL_SCANCODE_S, DK_S }, { SDL_SCANCODE_T, DK_T },
    { SDL_SCANCODE_U, DK_U }, { SDL_SCANCODE_V, DK_V }, { SDL_SCANCODE_W, DK_W }, { SDL_SCANCODE_X, DK_X },
    { SDL_SCANCODE_Y, DK_Y }, { SDL_SCANCODE_Z, DK_Z },
    { SDL_SCANCODE_0, DK_0 }, { SDL_SCANCODE_1, DK_1 }, { SDL_SCANCODE_2, DK_2 }, { SDL_SCANCODE_3, DK_3 },
    { SDL_SCANCODE_4, DK_4 }, { SDL_SCANCODE_5, DK_5 }, { SDL_SCANCODE_6, DK_6 }, { SDL_SCANCODE_7, DK_7 },
    { SDL_SCANCODE_8, DK_8 }, { SDL_SCANCODE_9, DK_9 },
    { SDL_SCANCODE_RETURN, DK_ENTER }, { SDL_SCANCODE_KP_ENTER, DK_ENTER }, { SDL_SCANCODE_SPACE, DK_SPACE },
    { SDL_SCANCODE_LSHIFT, DK_LSHIFT }, { SDL_SCANCODE_RSHIFT, DK_RSHIFT }, { SDL_SCANCODE_LCTRL, DK_LCTRL },
    { SDL_SCANCODE_RCTRL, DK_RCTRL }, { SDL_SCANCODE_BACKSPACE, DK_BACKSPACE },
    { SDL_SCANCODE_UP, DK_UP }, { SDL_SCANCODE_DOWN, DK_DOWN }, { SDL_SCANCODE_LEFT, DK_LEFT },
    { SDL_SCANCODE_RIGHT, DK_RIGHT }, { SDL_SCANCODE_COMMA, DK_COMMA }, { SDL_SCANCODE_PERIOD, DK_PERIOD },
    { SDL_SCANCODE_SEMICOLON, DK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, DK_QUOTE }, { SDL_SCANCODE_SLASH, DK_SLASH },
    { SDL_SCANCODE_MINUS, DK_MINUS }, { SDL_SCANCODE_KP_MINUS, DK_MINUS }, { SDL_SCANCODE_ESCAPE, DK_ESCAPE },
    { SDL_SCANCODE_TAB, DK_TAB }, { SDL_SCANCODE_CAPSLOCK, DK_CAPSLOCK }, { SDL_SCANCODE_HOME, DK_HOME },
    { SDL_SCANCODE_LALT, DK_CBM }, { SDL_SCANCODE_EQUALS, DK_EQUALS }, { SDL_SCANCODE_KP_PLUS, DK_PLUS },
    { SDL_SCANCODE_RIGHTBRACKET, DK_PLUS }, { SDL_SCANCODE_KP_MULTIPLY, DK_ASTERISK },
    { SDL_SCANCODE_LEFTBRACKET, DK_AT },
    { SDL_SCANCODE_F1, DK_F1 }, { SDL_SCANCODE_F2, DK_F2 }, { SDL_SCANCODE_F3, DK_F3 }, { SDL_SCANCODE_F4, DK_F4 },
    { SDL_SCANCODE_F5, DK_F5 }, { SDL_SCANCODE_F6, DK_F6 }, { SDL_SCANCODE_F7, DK_F7 }, { SDL_SCANCODE_F8, DK_F8 },
    { SDL_SCANCODE_F9, DK_F9 }, { SDL_SCANCODE_F10, DK_F10 },
    { SDL_SCANCODE_BACKSLASH, DK_BACKSLASH }, { SDL_SCANCODE_GRAVE, DK_BACKQUOTE },
    { SDL_SCANCODE_DELETE, DK_DELETE }, { SDL_SCANCODE_LGUI, DK_LGUI }, { SDL_SCANCODE_RGUI, DK_RGUI },
    { SDL_SCANCODE_RALT, DK_RALT },
};

static void set_title(void)
{
    char t[400];
    snprintf(t, sizeof t, "%s%s%s  (F11 menu, F12 turbo)", dspc_machine.title, media[0] ? " - " : "",
             media[0] ? comp_basename(media) : "");
    if (sdl_port_turbo()) strncat(t, " [TURBO]", sizeof t - strlen(t) - 1);
    sdl_port_set_title(t);
}

static void load_path(const char *path)
{
    if (!path || !path[0]) return;
    char msg[128];
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n = 0;
    if (f && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (data = (uint8_t *)malloc((size_t)n)) != NULL && fread(data, 1, (size_t)n, f) == (size_t)n) {
        const char *err = dspc_load(path, data, (size_t)n);
        if (!err) {
            snprintf(media, sizeof media, "%s", path);
            snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
            snprintf(msg, sizeof msg, "Loaded: %s", comp_basename(path));
        } else {
            snprintf(msg, sizeof msg, "Load failed: %s", err);
        }
    } else {
        snprintf(msg, sizeof msg, "Cannot read %s", path);
    }
    if (f) fclose(f);
    free(data);
    comp_note(&mm, msg);
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    romfs_default_dirs(argc > 0 ? argv[0] : NULL);
    if (!dspc_init()) sdl_port_rom_error();
    const int W = dspc_machine.w, H = dspc_machine.h * dspc_machine.line_scale;
    frame = (uint32_t *)calloc((size_t)W * H, sizeof *frame);
    if (!frame) return 1;
    const bool has_media = dspc_machine.exts[0] != NULL;
    char title[64];
    snprintf(title, sizeof title, "%s  F11", dspc_machine.title);
    mm = (comp_menu){ frame, W, H, title, dspc_machine.media_label, dspc_machine.filter,
                      { has_media ? "Load a file" : "Reset", has_media ? "Reset" : NULL }, 0, has_media ? 2 : 1,
                      false, "", "" };
    sdl_port_cfg cfg = { dspc_machine.title, W, H, dspc_machine.w < 400 ? 2 : 1, dspc_machine.rate, dspc_machine.fps,
                         true };
    if (!sdl_port_open(&cfg)) return 1;
    if (argc > 1) load_path(argv[1]);
    set_title();
    if (has_media) comp_web_setup(dspc_machine.title, dspc_machine.accept);

    dspc_inputs in;
    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            load_path(drop);
            free(drop);
            mm.open = false;
        }
        int k;
        while ((k = sdl_port_next_press()) >= 0) {
            if (mm.open) {
                if (k == SDL_SCANCODE_UP) { if (mm.sel > 0) mm.sel--; }
                else if (k == SDL_SCANCODE_DOWN) { if (mm.sel < mm.n - 1) mm.sel++; }
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_SPACE) {
                    if (has_media && mm.sel == 0) {
                        char *p = comp_pick_path(&mm);
                        if (p) { load_path(p); free(p); }
                    } else {
                        dspc_reset();
                        comp_note(&mm, "Reset");
                    }
                    mm.open = false;
                } else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F11) mm.open = false;
                continue;
            }
            if (k == SDL_SCANCODE_F11) { mm.open = true; mm.sel = 0; mm.msg[0] = 0; }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        memset(&in, 0, sizeof in);
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) in.keys[keymap[i].key] = true;
            const uint32_t pad = sdl_port_keys();
            in.p1.up = sdl_port_key(SDL_SCANCODE_KP_8) || (pad & SP_UP);
            in.p1.down = sdl_port_key(SDL_SCANCODE_KP_2) || (pad & SP_DOWN);
            in.p1.left = sdl_port_key(SDL_SCANCODE_KP_4) || (pad & SP_LEFT);
            in.p1.right = sdl_port_key(SDL_SCANCODE_KP_6) || (pad & SP_RIGHT);
            in.p1.b1 = sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || (pad & SP_B1);
            in.p1.b2 = (pad & SP_B2) != 0;
            in.p1.start = (pad & SP_START1) != 0;
        }
        dspc_set_inputs(&in);
        const int16_t *audio;
        const int n = dspc_run_frame(&audio);
        sdl_port_audio(audio, n);
        const uint32_t *fb = dspc_frame();
        for (int y = 0; y < dspc_machine.h; y++)
            for (int r = 0; r < dspc_machine.line_scale; r++) {
                uint32_t *row = frame + ((size_t)y * dspc_machine.line_scale + r) * W;
                for (int x = 0; x < W; x++) row[x] = fb[(size_t)y * W + x] & 0xffffff;
            }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
