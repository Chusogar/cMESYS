// apple2gs_sdl.c - the Apple IIGS on desktop (SDL2) and web (Emscripten).
//
//   F9  disk menu (SmartPort: .po .2mg .hdv .dsk .do; up to four, the first boots)
//   F10 power off / on     F11 CTRL-RESET      F12 TURBO
//   Left Alt / Left Windows = Command (Open Apple), Right Alt / Right Windows = Option;
//   the keypad is the IIGS's. A click in the window captures the mouse, the
//   middle button lets it go. Drag-and-drop a disk image onto the window.
#include "apple2gs_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[A2GS_W * A2GS_H * 2];
static apple2gs *m;
static comp_menu mm;
static int ndisks;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, GSK_A }, { SDL_SCANCODE_B, GSK_B }, { SDL_SCANCODE_C, GSK_C }, { SDL_SCANCODE_D, GSK_D },
    { SDL_SCANCODE_E, GSK_E }, { SDL_SCANCODE_F, GSK_F }, { SDL_SCANCODE_G, GSK_G }, { SDL_SCANCODE_H, GSK_H },
    { SDL_SCANCODE_I, GSK_I }, { SDL_SCANCODE_J, GSK_J }, { SDL_SCANCODE_K, GSK_K }, { SDL_SCANCODE_L, GSK_L },
    { SDL_SCANCODE_M, GSK_M }, { SDL_SCANCODE_N, GSK_N }, { SDL_SCANCODE_O, GSK_O }, { SDL_SCANCODE_P, GSK_P },
    { SDL_SCANCODE_Q, GSK_Q }, { SDL_SCANCODE_R, GSK_R }, { SDL_SCANCODE_S, GSK_S }, { SDL_SCANCODE_T, GSK_T },
    { SDL_SCANCODE_U, GSK_U }, { SDL_SCANCODE_V, GSK_V }, { SDL_SCANCODE_W, GSK_W }, { SDL_SCANCODE_X, GSK_X },
    { SDL_SCANCODE_Y, GSK_Y }, { SDL_SCANCODE_Z, GSK_Z },
    { SDL_SCANCODE_0, GSK_0 }, { SDL_SCANCODE_1, GSK_1 }, { SDL_SCANCODE_2, GSK_2 }, { SDL_SCANCODE_3, GSK_3 },
    { SDL_SCANCODE_4, GSK_4 }, { SDL_SCANCODE_5, GSK_5 }, { SDL_SCANCODE_6, GSK_6 }, { SDL_SCANCODE_7, GSK_7 },
    { SDL_SCANCODE_8, GSK_8 }, { SDL_SCANCODE_9, GSK_9 },
    { SDL_SCANCODE_MINUS, GSK_MINUS }, { SDL_SCANCODE_EQUALS, GSK_EQUALS }, { SDL_SCANCODE_LEFTBRACKET, GSK_LBRACKET },
    { SDL_SCANCODE_RIGHTBRACKET, GSK_RBRACKET }, { SDL_SCANCODE_BACKSLASH, GSK_BACKSLASH },
    { SDL_SCANCODE_SEMICOLON, GSK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, GSK_QUOTE }, { SDL_SCANCODE_GRAVE, GSK_GRAVE },
    { SDL_SCANCODE_COMMA, GSK_COMMA }, { SDL_SCANCODE_PERIOD, GSK_PERIOD }, { SDL_SCANCODE_SLASH, GSK_SLASH },
    { SDL_SCANCODE_RETURN, GSK_RETURN }, { SDL_SCANCODE_SPACE, GSK_SPACE }, { SDL_SCANCODE_TAB, GSK_TAB },
    { SDL_SCANCODE_ESCAPE, GSK_ESC }, { SDL_SCANCODE_BACKSPACE, GSK_DELETE }, { SDL_SCANCODE_DELETE, GSK_DELETE },
    { SDL_SCANCODE_LCTRL, GSK_CTRL }, { SDL_SCANCODE_RCTRL, GSK_CTRL }, { SDL_SCANCODE_LSHIFT, GSK_SHIFT },
    { SDL_SCANCODE_RSHIFT, GSK_SHIFT }, { SDL_SCANCODE_CAPSLOCK, GSK_CAPS },
    { SDL_SCANCODE_LALT, GSK_COMMAND }, { SDL_SCANCODE_LGUI, GSK_COMMAND },
    { SDL_SCANCODE_RALT, GSK_OPTION }, { SDL_SCANCODE_RGUI, GSK_OPTION },
    { SDL_SCANCODE_LEFT, GSK_LEFT }, { SDL_SCANCODE_RIGHT, GSK_RIGHT }, { SDL_SCANCODE_UP, GSK_UP },
    { SDL_SCANCODE_DOWN, GSK_DOWN }, { SDL_SCANCODE_HOME, GSK_HOME },
    { SDL_SCANCODE_KP_0, GSK_KP0 }, { SDL_SCANCODE_KP_1, GSK_KP1 }, { SDL_SCANCODE_KP_2, GSK_KP2 },
    { SDL_SCANCODE_KP_3, GSK_KP3 }, { SDL_SCANCODE_KP_4, GSK_KP4 }, { SDL_SCANCODE_KP_5, GSK_KP5 },
    { SDL_SCANCODE_KP_6, GSK_KP6 }, { SDL_SCANCODE_KP_7, GSK_KP7 }, { SDL_SCANCODE_KP_8, GSK_KP8 },
    { SDL_SCANCODE_KP_9, GSK_KP9 }, { SDL_SCANCODE_KP_PERIOD, GSK_KP_PERIOD }, { SDL_SCANCODE_KP_ENTER, GSK_KP_ENTER },
    { SDL_SCANCODE_KP_PLUS, GSK_KP_PLUS }, { SDL_SCANCODE_KP_MINUS, GSK_KP_MINUS },
    { SDL_SCANCODE_KP_MULTIPLY, GSK_KP_MUL }, { SDL_SCANCODE_KP_DIVIDE, GSK_KP_DIV },
    { SDL_SCANCODE_NUMLOCKCLEAR, GSK_KP_CLEAR }, { SDL_SCANCODE_KP_EQUALS, GSK_KP_EQUALS },
};

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "Apple IIGS - %s%s%s%s  (F9 disks, F11 reset, F10 power, F12 turbo)",
             mm.now[0] ? mm.now : "(no disk)", ndisks > 1 ? " +" : "", apple2gs_disk_busy(m) ? " [DISK]" : "",
             sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path, bool boot)
{
    if (!path || !path[0]) return;
    int unit = ndisks < A2GS_DISKS ? ndisks : A2GS_DISKS - 1;
    if (boot) unit = 0;
    const char *err = apple2gs_load_file(m, unit, path);
    if (!err) {
        ndisks = 0;
        while (ndisks < A2GS_DISKS && apple2gs_has_disk(m, ndisks)) ndisks++;
        if (unit == 0) {
            snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
            apple2gs_reset(m);          // the first disk boots
        }
        comp_note(&mm, comp_basename(path));
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", err);
        comp_note(&mm, msg);
    }
    set_title();
}

static void menu_activate(void)
{
    if (mm.sel == 0 || mm.sel == 1) {
        char *p = comp_pick_path(&mm);
        if (p) { apply_path(p, mm.sel == 0); free(p); }
    } else if (mm.sel == 2) {
        for (int u = A2GS_DISKS - 1; u >= 0; u--) apple2gs_eject(m, u);
        ndisks = 0;
        mm.now[0] = 0;
        comp_note(&mm, "Disks ejected");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!apple2gs_rom_load()) sdl_port_rom_error();
    m = apple2gs_create(64, A2GS_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    apple2gs_set_roms(m, a2gs_rom_fc, a2gs_rom_fe, a2gs_rom_chr);
    apple2gs_reset(m);
    mm = (comp_menu){ frame, A2GS_W, A2GS_H * 2, "APPLE IIGS DISKS  F9", A2GS_MEDIA_LABEL,
                      "*.po *.PO *.2mg *.2MG *.hdv *.HDV *.dsk *.DSK *.do *.DO *.img *.IMG",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose the boot disk", "Upload / add a disk", "Eject all", "Close menu" },
#else
                      { "Open the boot disk", "Add a disk (next unit)", "Eject all", "Close menu" },
#endif
                      0, 4, false, "", "" };
    for (int i = 1; i < argc; i++) apply_path(argv[i], i == 1);

    sdl_port_cfg cfg = { "Apple IIGS", A2GS_W, A2GS_H * 2, 1, A2GS_AUDIO_RATE, A2GS_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_mouse_enable(true);
    set_title();
    comp_web_setup("Apple IIGS disks (.po .2mg .hdv .dsk)", ".po,.2mg,.hdv,.dsk,.do,.img");

    bool busy = false;
    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            apply_path(drop, ndisks == 0);
            free(drop);
            mm.open = false;
        }
        int k;
        while ((k = sdl_port_next_press()) >= 0) {
            if (mm.open) {
                if (k == SDL_SCANCODE_UP) { if (mm.sel > 0) mm.sel--; }
                else if (k == SDL_SCANCODE_DOWN) { if (mm.sel < mm.n - 1) mm.sel++; }
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_SPACE) menu_activate();
                else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F9) mm.open = false;
                continue;
            }
            if (k == SDL_SCANCODE_F9) { mm.open = true; mm.sel = 0; mm.msg[0] = 0; }
            if (k == SDL_SCANCODE_F10) { apple2gs_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F11 || k == SDL_SCANCODE_PAUSE) apple2gs_reset_key(m);
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[GSK_COUNT] = { false };
        if (!mm.open)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
        for (int c = 0; c < GSK_COUNT; c++) apple2gs_key(m, c, held[c]);
        int dx = 0, dy = 0;
        const uint32_t b = sdl_port_mouse(&dx, &dy);
        apple2gs_mouse(m, dx, dy, (b & 1) != 0);

        const int16_t *audio;
        apple2gs_set_turbo(m, sdl_port_turbo());
        int n = apple2gs_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < A2GS_H; y++) {
            uint32_t *row = frame + y * 2 * A2GS_W;
            apple2gs_render_row(m, y, row);
            memcpy(row + A2GS_W, row, A2GS_W * sizeof *row);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
        if (apple2gs_disk_busy(m) != busy) {
            busy = apple2gs_disk_busy(m);
            set_title();
        }
    }
    sdl_port_close();
    apple2gs_destroy(m);
    return 0;
}
