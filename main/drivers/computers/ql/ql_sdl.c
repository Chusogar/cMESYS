// ql_sdl.c - Sinclair QL on the desktop (SDL2). Core: ql_core.c.
//
//   build/sdl/ql [cartridge.mdv]
//
// The PC keyboard is the QL's (Alt = ALT, Backspace = CTRL+LEFT, Delete =
// CTRL+RIGHT, ` = pound). Not QL keys:
//   F1   mount / unmount the cartridge in mdv1 (the one built in, or the one
//        on the command line): the QL is reset and boots from it
//   F6   the QL's F1 (F2-F5 are the QL's)
//   F12  reset            close the window to quit
//
// Tests: SDL_PORT_FRAMES / SDL_PORT_SHOT (sdl_port.h), QL_MOUNT_AT=n presses
// F1 at frame n.
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sdl_port.h"
#include "ql_core.h"
#include "zx8301.h"
#include "rom_data.h"

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

static const uint8_t *load_mdv(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    uint8_t *b = malloc(255 * 686 + 1);
    size_t n = fread(b, 1, 255 * 686 + 1, f);
    fclose(f);
    if (n != 255 * 686) {
        fprintf(stderr, "%s: not a QLAY .mdv (%u bytes, expected 174930)\n", path, (unsigned)n);
        free(b);
        return NULL;
    }
    return b;
}

static void set_title(const char *name)
{
    char t[160];
    if (ql_cart_loaded(0)) snprintf(t, sizeof t, "Sinclair QL - mdv1: %s  (F1 desmontar)", name);
    else snprintf(t, sizeof t, "Sinclair QL - mdv1 vacio  (F1 montar %s)", name);
    sdl_port_set_title(t);
}

int main(int argc, char **argv)
{
    const uint8_t *image = ql_mdv_image;
    const char *name = ql_mdv_name;
    if (argc > 1) {
        image = load_mdv(argv[1]);
        name = argv[1];
        if (!image) return 1;
    }
    if (!image) fprintf(stderr, "no cartridge: put a .mdv in roms/ql and run tools/regen.sh ql, or pass one\n");

    uint8_t *blocks[QL_RAM_BLOCKS];
    for (int i = 0; i < QL_RAM_BLOCKS; i++) blocks[i] = ram[i];
    ql_init(ql_rom, ql_ipc_rom, blocks, (uint32_t)time(NULL));

    sdl_port_cfg cfg = { "Sinclair QL", QL_W, QL_H, 2, QL_AUDIO_RATE, QL_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title(name);
    const char *e = getenv("QL_MOUNT_AT");
    long mount_at = e ? atol(e) : -1;

    uint8_t row[QL_W];
    uint32_t pal[8];
    for (int i = 0; i < 8; i++) pal[i] = zx8301_rgb(i);
    for (long fno = 0; sdl_port_poll(NULL); fno++) {
        int k;
        bool mount = fno == mount_at;
        while ((k = sdl_port_next_press()) >= 0) {
            if (k == SDL_SCANCODE_F1) mount = true;
            if (k == SDL_SCANCODE_F12) ql_reset();
        }
        if (mount && image) {
            ql_mount(ql_cart_loaded(0) ? NULL : image);
            printf("mdv1: %s\n", ql_cart_loaded(0) ? "cartucho montado, arrancando" : "cartucho desmontado");
            set_title(name);
        }
        ql_keys_clear();
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (sdl_port_key(keymap[i].sc)) ql_key(keymap[i].key, true);
        if (sdl_port_key(SDL_SCANCODE_BACKSPACE)) { ql_key(QLK_CTRL, true); ql_key(QLK_LEFT, true); }
        if (sdl_port_key(SDL_SCANCODE_DELETE)) { ql_key(QLK_CTRL, true); ql_key(QLK_RIGHT, true); }

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
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
