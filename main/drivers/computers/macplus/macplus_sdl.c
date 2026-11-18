// macplus_sdl.c - the Macintosh Plus on desktop (SDL2) and web (Emscripten).
//
//   F9  disk menu (800K / 1.44 MB floppies .dsk .img .dc42; SCSI hard disks)
//   F10 power off / on      F12 TURBO
//   Left Alt / Windows = Command, Right Alt = Option. A click in the window
//   captures the mouse (the Mac's pointer follows it), the middle button lets
//   it go. Drag-and-drop a disk image onto the window.
#include "macplus_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[MACPLUS_W * MACPLUS_H];
static comp_menu mm;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, MK_A }, { SDL_SCANCODE_B, MK_B }, { SDL_SCANCODE_C, MK_C }, { SDL_SCANCODE_D, MK_D },
    { SDL_SCANCODE_E, MK_E }, { SDL_SCANCODE_F, MK_F }, { SDL_SCANCODE_G, MK_G }, { SDL_SCANCODE_H, MK_H },
    { SDL_SCANCODE_I, MK_I }, { SDL_SCANCODE_J, MK_J }, { SDL_SCANCODE_K, MK_K }, { SDL_SCANCODE_L, MK_L },
    { SDL_SCANCODE_M, MK_M }, { SDL_SCANCODE_N, MK_N }, { SDL_SCANCODE_O, MK_O }, { SDL_SCANCODE_P, MK_P },
    { SDL_SCANCODE_Q, MK_Q }, { SDL_SCANCODE_R, MK_R }, { SDL_SCANCODE_S, MK_S }, { SDL_SCANCODE_T, MK_T },
    { SDL_SCANCODE_U, MK_U }, { SDL_SCANCODE_V, MK_V }, { SDL_SCANCODE_W, MK_W }, { SDL_SCANCODE_X, MK_X },
    { SDL_SCANCODE_Y, MK_Y }, { SDL_SCANCODE_Z, MK_Z },
    { SDL_SCANCODE_0, MK_0 }, { SDL_SCANCODE_1, MK_1 }, { SDL_SCANCODE_2, MK_2 }, { SDL_SCANCODE_3, MK_3 },
    { SDL_SCANCODE_4, MK_4 }, { SDL_SCANCODE_5, MK_5 }, { SDL_SCANCODE_6, MK_6 }, { SDL_SCANCODE_7, MK_7 },
    { SDL_SCANCODE_8, MK_8 }, { SDL_SCANCODE_9, MK_9 },
    { SDL_SCANCODE_MINUS, MK_MINUS }, { SDL_SCANCODE_EQUALS, MK_EQUALS }, { SDL_SCANCODE_LEFTBRACKET, MK_LBRACKET },
    { SDL_SCANCODE_RIGHTBRACKET, MK_RBRACKET }, { SDL_SCANCODE_BACKSLASH, MK_BACKSLASH },
    { SDL_SCANCODE_SEMICOLON, MK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, MK_QUOTE }, { SDL_SCANCODE_GRAVE, MK_GRAVE },
    { SDL_SCANCODE_COMMA, MK_COMMA }, { SDL_SCANCODE_PERIOD, MK_PERIOD }, { SDL_SCANCODE_SLASH, MK_SLASH },
    { SDL_SCANCODE_RETURN, MK_RETURN }, { SDL_SCANCODE_SPACE, MK_SPACE }, { SDL_SCANCODE_TAB, MK_TAB },
    { SDL_SCANCODE_ESCAPE, MK_ESC }, { SDL_SCANCODE_BACKSPACE, MK_BACKSPACE }, { SDL_SCANCODE_DELETE, MK_BACKSPACE },
    { SDL_SCANCODE_LSHIFT, MK_SHIFT }, { SDL_SCANCODE_RSHIFT, MK_SHIFT }, { SDL_SCANCODE_CAPSLOCK, MK_CAPS },
    { SDL_SCANCODE_LALT, MK_COMMAND }, { SDL_SCANCODE_LGUI, MK_COMMAND }, { SDL_SCANCODE_RGUI, MK_COMMAND },
    { SDL_SCANCODE_RALT, MK_OPTION }, { SDL_SCANCODE_LCTRL, MK_CONTROL }, { SDL_SCANCODE_RCTRL, MK_CONTROL },
    { SDL_SCANCODE_LEFT, MK_LEFT }, { SDL_SCANCODE_RIGHT, MK_RIGHT }, { SDL_SCANCODE_UP, MK_UP },
    { SDL_SCANCODE_DOWN, MK_DOWN },
    { SDL_SCANCODE_KP_0, MK_KP0 }, { SDL_SCANCODE_KP_1, MK_KP1 }, { SDL_SCANCODE_KP_2, MK_KP2 },
    { SDL_SCANCODE_KP_3, MK_KP3 }, { SDL_SCANCODE_KP_4, MK_KP4 }, { SDL_SCANCODE_KP_5, MK_KP5 },
    { SDL_SCANCODE_KP_6, MK_KP6 }, { SDL_SCANCODE_KP_7, MK_KP7 }, { SDL_SCANCODE_KP_8, MK_KP8 },
    { SDL_SCANCODE_KP_9, MK_KP9 }, { SDL_SCANCODE_KP_PERIOD, MK_KP_PERIOD }, { SDL_SCANCODE_KP_ENTER, MK_KP_ENTER },
    { SDL_SCANCODE_KP_PLUS, MK_KP_PLUS }, { SDL_SCANCODE_KP_MINUS, MK_KP_MINUS },
    { SDL_SCANCODE_KP_MULTIPLY, MK_KP_MUL }, { SDL_SCANCODE_KP_DIVIDE, MK_KP_DIV },
    { SDL_SCANCODE_NUMLOCKCLEAR, MK_KP_CLEAR }, { SDL_SCANCODE_KP_EQUALS, MK_KP_EQUALS },
};

static void set_title(void)
{
    char t[220];
    snprintf(t, sizeof t, "Macintosh Plus - %s%s%s%s  (F9 disks, F10 power, F12 turbo; click: mouse)",
             mm.now[0] ? mm.now : "(no disk)", macplus_scsi_loaded() ? " [SCSI]" : "",
             macplus_drive_led() ? " [DRIVE]" : "", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    const char *err = macplus_load_file(path);
    if (!err) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", err);
        comp_note(&mm, msg);
    }
    set_title();
}

static void menu_activate(void)
{
    if (mm.sel == 0) {
        char *p = comp_pick_path(&mm);
        if (p) { apply_path(p); free(p); }
    } else if (mm.sel == 1) {
        macplus_eject_floppy();
        mm.now[0] = 0;
        comp_note(&mm, "Floppy ejected");
    } else if (mm.sel == 2) {
        macplus_reset();
        comp_note(&mm, "Restarted");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!macplus_rom_load()) sdl_port_rom_error();
    if (!macplus_init(macplus_rom_even, macplus_rom_odd)) { fprintf(stderr, "out of memory\n"); return 1; }
    mm = (comp_menu){ frame, MACPLUS_W, MACPLUS_H, "MACINTOSH DISKS  F9", MACPLUS_MEDIA_LABEL,
                      "*.dsk *.DSK *.img *.IMG *.image *.dc42 *.hda *.hd *.vhd",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose a disk", "Eject the floppy", "Restart", "Close menu" },
#else
                      { "Open a disk (floppy or SCSI)", "Eject the floppy", "Restart", "Close menu" },
#endif
                      0, 4, false, "", "" };
    for (int i = 1; i < argc; i++) apply_path(argv[i]);

    sdl_port_cfg cfg = { "Macintosh Plus", MACPLUS_W, MACPLUS_H, 1, MACPLUS_AUDIO_RATE, MACPLUS_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_mouse_enable(true);
    set_title();
    comp_web_setup("Macintosh disks (.dsk .img .dc42 .hda)", ".dsk,.img,.image,.dc42,.hda,.hd,.vhd");

    bool led = false;
    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            apply_path(drop);
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
            if (k == SDL_SCANCODE_F10) { macplus_reset(); set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[MK_COUNT] = { false };
        if (!mm.open)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
        for (int c = 0; c < MK_COUNT; c++) macplus_key(c, held[c]);
        int dx = 0, dy = 0;
        const uint32_t b = sdl_port_mouse(&dx, &dy);
        macplus_mouse(dx, dy, (b & 1) != 0);

        const int16_t *audio;
        macplus_set_turbo(sdl_port_turbo());
        int n = macplus_run_frame(&audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < MACPLUS_H; y++) macplus_render_row(y, frame + y * MACPLUS_W);
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
        if (macplus_drive_led() != led) {
            led = macplus_drive_led();
            set_title();
        }
    }
    sdl_port_close();
    return 0;
}
