// macii_sdl.c - the Macintosh II on desktop (SDL2) and web (Emscripten).
//
//   F9  disk menu (a SCSI hard disk: .img .hda .hd .dsk; written in place)
//   F10 power off / on      F12 TURBO
//   Left Alt / Windows = Command, Right Alt = Option. A click in the window
//   captures the mouse (the Mac's pointer follows it), the middle button lets
//   it go. Drag-and-drop a disk image onto the window.
#include "macii_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[MACII_W * MACII_H];
static comp_menu mm;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, ADBK_A }, { SDL_SCANCODE_B, ADBK_B }, { SDL_SCANCODE_C, ADBK_C }, { SDL_SCANCODE_D, ADBK_D },
    { SDL_SCANCODE_E, ADBK_E }, { SDL_SCANCODE_F, ADBK_F }, { SDL_SCANCODE_G, ADBK_G }, { SDL_SCANCODE_H, ADBK_H },
    { SDL_SCANCODE_I, ADBK_I }, { SDL_SCANCODE_J, ADBK_J }, { SDL_SCANCODE_K, ADBK_K }, { SDL_SCANCODE_L, ADBK_L },
    { SDL_SCANCODE_M, ADBK_M }, { SDL_SCANCODE_N, ADBK_N }, { SDL_SCANCODE_O, ADBK_O }, { SDL_SCANCODE_P, ADBK_P },
    { SDL_SCANCODE_Q, ADBK_Q }, { SDL_SCANCODE_R, ADBK_R }, { SDL_SCANCODE_S, ADBK_S }, { SDL_SCANCODE_T, ADBK_T },
    { SDL_SCANCODE_U, ADBK_U }, { SDL_SCANCODE_V, ADBK_V }, { SDL_SCANCODE_W, ADBK_W }, { SDL_SCANCODE_X, ADBK_X },
    { SDL_SCANCODE_Y, ADBK_Y }, { SDL_SCANCODE_Z, ADBK_Z },
    { SDL_SCANCODE_0, ADBK_0 }, { SDL_SCANCODE_1, ADBK_1 }, { SDL_SCANCODE_2, ADBK_2 }, { SDL_SCANCODE_3, ADBK_3 },
    { SDL_SCANCODE_4, ADBK_4 }, { SDL_SCANCODE_5, ADBK_5 }, { SDL_SCANCODE_6, ADBK_6 }, { SDL_SCANCODE_7, ADBK_7 },
    { SDL_SCANCODE_8, ADBK_8 }, { SDL_SCANCODE_9, ADBK_9 },
    { SDL_SCANCODE_MINUS, ADBK_MINUS }, { SDL_SCANCODE_EQUALS, ADBK_EQUALS }, { SDL_SCANCODE_LEFTBRACKET, ADBK_LBRACKET },
    { SDL_SCANCODE_RIGHTBRACKET, ADBK_RBRACKET }, { SDL_SCANCODE_BACKSLASH, ADBK_BACKSLASH },
    { SDL_SCANCODE_SEMICOLON, ADBK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, ADBK_QUOTE }, { SDL_SCANCODE_GRAVE, ADBK_GRAVE },
    { SDL_SCANCODE_COMMA, ADBK_COMMA }, { SDL_SCANCODE_PERIOD, ADBK_PERIOD }, { SDL_SCANCODE_SLASH, ADBK_SLASH },
    { SDL_SCANCODE_RETURN, ADBK_RETURN }, { SDL_SCANCODE_SPACE, ADBK_SPACE }, { SDL_SCANCODE_TAB, ADBK_TAB },
    { SDL_SCANCODE_ESCAPE, ADBK_ESC }, { SDL_SCANCODE_BACKSPACE, ADBK_DELETE }, { SDL_SCANCODE_DELETE, ADBK_FWD_DEL },
    { SDL_SCANCODE_LSHIFT, ADBK_SHIFT }, { SDL_SCANCODE_RSHIFT, ADBK_SHIFT }, { SDL_SCANCODE_CAPSLOCK, ADBK_CAPS },
    { SDL_SCANCODE_LALT, ADBK_COMMAND }, { SDL_SCANCODE_LGUI, ADBK_COMMAND }, { SDL_SCANCODE_RGUI, ADBK_COMMAND },
    { SDL_SCANCODE_RALT, ADBK_OPTION }, { SDL_SCANCODE_LCTRL, ADBK_CONTROL }, { SDL_SCANCODE_RCTRL, ADBK_CONTROL },
    { SDL_SCANCODE_HOME, ADBK_HOME }, { SDL_SCANCODE_END, ADBK_END }, { SDL_SCANCODE_PAGEUP, ADBK_PGUP },
    { SDL_SCANCODE_PAGEDOWN, ADBK_PGDN }, { SDL_SCANCODE_INSERT, ADBK_HELP }, { SDL_SCANCODE_F1, ADBK_F1 },
    { SDL_SCANCODE_F2, ADBK_F2 }, { SDL_SCANCODE_F3, ADBK_F3 }, { SDL_SCANCODE_F4, ADBK_F4 }, { SDL_SCANCODE_F5, ADBK_F5 },
    { SDL_SCANCODE_F6, ADBK_F6 }, { SDL_SCANCODE_F7, ADBK_F7 }, { SDL_SCANCODE_F8, ADBK_F8 },
    { SDL_SCANCODE_LEFT, ADBK_LEFT }, { SDL_SCANCODE_RIGHT, ADBK_RIGHT }, { SDL_SCANCODE_UP, ADBK_UP },
    { SDL_SCANCODE_DOWN, ADBK_DOWN },
    { SDL_SCANCODE_KP_0, ADBK_KP0 }, { SDL_SCANCODE_KP_1, ADBK_KP1 }, { SDL_SCANCODE_KP_2, ADBK_KP2 },
    { SDL_SCANCODE_KP_3, ADBK_KP3 }, { SDL_SCANCODE_KP_4, ADBK_KP4 }, { SDL_SCANCODE_KP_5, ADBK_KP5 },
    { SDL_SCANCODE_KP_6, ADBK_KP6 }, { SDL_SCANCODE_KP_7, ADBK_KP7 }, { SDL_SCANCODE_KP_8, ADBK_KP8 },
    { SDL_SCANCODE_KP_9, ADBK_KP9 }, { SDL_SCANCODE_KP_PERIOD, ADBK_KP_PERIOD }, { SDL_SCANCODE_KP_ENTER, ADBK_KP_ENTER },
    { SDL_SCANCODE_KP_PLUS, ADBK_KP_PLUS }, { SDL_SCANCODE_KP_MINUS, ADBK_KP_MINUS },
    { SDL_SCANCODE_KP_MULTIPLY, ADBK_KP_MUL }, { SDL_SCANCODE_KP_DIVIDE, ADBK_KP_DIV },
    { SDL_SCANCODE_NUMLOCKCLEAR, ADBK_KP_CLEAR }, { SDL_SCANCODE_KP_EQUALS, ADBK_KP_EQUALS },
};

static void set_title(void)
{
    char t[220];
    snprintf(t, sizeof t, "Macintosh II - %s%s%s%s  (F9 disk, F10 power, F12 turbo; click: mouse)",
             mm.now[0] ? mm.now : "(no disk)", macii_scsi_loaded() ? " [SCSI]" : "",
             macii_scsi_busy() ? " [BUSY]" : "", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    const char *err = macii_load_file(path);
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
        macii_detach_scsi();
        mm.now[0] = 0;
        comp_note(&mm, "Disk detached (restart to boot without it)");
    } else if (mm.sel == 2) {
        macii_reset();
        comp_note(&mm, "Restarted");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!macii_rom_load()) sdl_port_rom_error();
    if (!macii_init(macii_rom, macii_rom_card, 8)) { fprintf(stderr, "out of memory\n"); return 1; }
    mm = (comp_menu){ frame, MACII_W, MACII_H, "MACINTOSH II DISK  F9", MACII_MEDIA_LABEL,
                      "*.img *.IMG *.hda *.HDA *.hd *.dsk *.DSK *.vhd *.image",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose a disk", "Detach the disk", "Restart", "Close menu" },
#else
                      { "Open a SCSI disk image", "Detach the disk", "Restart", "Close menu" },
#endif
                      0, 4, false, "", "" };
    for (int i = 1; i < argc; i++) apply_path(argv[i]);

    sdl_port_cfg cfg = { "Macintosh II", MACII_W, MACII_H, 1, MACII_AUDIO_RATE, MACII_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_mouse_enable(true);
    set_title();
    comp_web_setup("Macintosh II SCSI disks (.img .hda .hd)", ".img,.hda,.hd,.dsk,.vhd,.image");

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
            if (k == SDL_SCANCODE_F10) { macii_reset(); set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[ADBK_COUNT] = { false };
        if (!mm.open)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
        for (int c = 0; c < ADBK_COUNT; c++) macii_key(c, held[c]);
        int dx = 0, dy = 0;
        const uint32_t b = sdl_port_mouse(&dx, &dy);
        macii_mouse(dx, dy, (b & 1) != 0);

        const int16_t *audio;
        macii_set_turbo(sdl_port_turbo());
        int n = macii_run_frame(&audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < MACII_H; y++) macii_render_row(y, frame + y * MACII_W);
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
        if (macii_scsi_busy() != led) {
            led = macii_scsi_busy();
            set_title();
        }
    }
    sdl_port_close();
    return 0;
}
