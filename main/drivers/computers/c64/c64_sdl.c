// c64_sdl.c - Commodore 64 on desktop (SDL2) and web (Emscripten).
//
//   F9  media menu (.prg .t64 .d64 .g64 .tap)      F11 tape play / pause
//   F10 reset    F12 TURBO    F8 joystick port 1 / 2 (keypad 8 4 6 2, 0 / 5 fire)
//   F1 F3 F5 F7 are the C64's (Shift: F2 F4 F6 F8). Esc RUN/STOP, Tab CTRL,
//   Left Alt C=, Home CLR/HOME, Page Down RESTORE, ` <-, Page Up ^, \ pound,
//   [ @, ] *, ; :, ' ;, = =, keypad + and * the C64's + and *.
// Drag-and-drop a file onto the window (web: Upload bar).
#include "c64_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[C64_PIC_W * C64_PIC_H];
static c64 *m;
static comp_menu mm;
static int joy_port = 2;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, C64K_A }, { SDL_SCANCODE_B, C64K_B }, { SDL_SCANCODE_C, C64K_C }, { SDL_SCANCODE_D, C64K_D },
    { SDL_SCANCODE_E, C64K_E }, { SDL_SCANCODE_F, C64K_F }, { SDL_SCANCODE_G, C64K_G }, { SDL_SCANCODE_H, C64K_H },
    { SDL_SCANCODE_I, C64K_I }, { SDL_SCANCODE_J, C64K_J }, { SDL_SCANCODE_K, C64K_K }, { SDL_SCANCODE_L, C64K_L },
    { SDL_SCANCODE_M, C64K_M }, { SDL_SCANCODE_N, C64K_N }, { SDL_SCANCODE_O, C64K_O }, { SDL_SCANCODE_P, C64K_P },
    { SDL_SCANCODE_Q, C64K_Q }, { SDL_SCANCODE_R, C64K_R }, { SDL_SCANCODE_S, C64K_S }, { SDL_SCANCODE_T, C64K_T },
    { SDL_SCANCODE_U, C64K_U }, { SDL_SCANCODE_V, C64K_V }, { SDL_SCANCODE_W, C64K_W }, { SDL_SCANCODE_X, C64K_X },
    { SDL_SCANCODE_Y, C64K_Y }, { SDL_SCANCODE_Z, C64K_Z },
    { SDL_SCANCODE_0, C64K_0 }, { SDL_SCANCODE_1, C64K_1 }, { SDL_SCANCODE_2, C64K_2 }, { SDL_SCANCODE_3, C64K_3 },
    { SDL_SCANCODE_4, C64K_4 }, { SDL_SCANCODE_5, C64K_5 }, { SDL_SCANCODE_6, C64K_6 }, { SDL_SCANCODE_7, C64K_7 },
    { SDL_SCANCODE_8, C64K_8 }, { SDL_SCANCODE_9, C64K_9 },
    { SDL_SCANCODE_RETURN, C64K_RETURN }, { SDL_SCANCODE_KP_ENTER, C64K_RETURN }, { SDL_SCANCODE_SPACE, C64K_SPACE },
    { SDL_SCANCODE_BACKSPACE, C64K_DEL }, { SDL_SCANCODE_LSHIFT, C64K_LSHIFT }, { SDL_SCANCODE_RSHIFT, C64K_RSHIFT },
    { SDL_SCANCODE_TAB, C64K_CTRL }, { SDL_SCANCODE_LCTRL, C64K_CTRL }, { SDL_SCANCODE_LALT, C64K_CBM },
    { SDL_SCANCODE_ESCAPE, C64K_RUNSTOP }, { SDL_SCANCODE_HOME, C64K_HOME },
    { SDL_SCANCODE_F1, C64K_F1 }, { SDL_SCANCODE_F3, C64K_F3 }, { SDL_SCANCODE_F5, C64K_F5 }, { SDL_SCANCODE_F7, C64K_F7 },
    { SDL_SCANCODE_MINUS, C64K_MINUS }, { SDL_SCANCODE_EQUALS, C64K_EQUALS }, { SDL_SCANCODE_LEFTBRACKET, C64K_AT },
    { SDL_SCANCODE_RIGHTBRACKET, C64K_ASTERISK }, { SDL_SCANCODE_BACKSLASH, C64K_POUND },
    { SDL_SCANCODE_SEMICOLON, C64K_COLON }, { SDL_SCANCODE_APOSTROPHE, C64K_SEMICOLON },
    { SDL_SCANCODE_COMMA, C64K_COMMA }, { SDL_SCANCODE_PERIOD, C64K_PERIOD }, { SDL_SCANCODE_SLASH, C64K_SLASH },
    { SDL_SCANCODE_GRAVE, C64K_LEFTARROW }, { SDL_SCANCODE_PAGEUP, C64K_UPARROW },
    { SDL_SCANCODE_KP_PLUS, C64K_PLUS }, { SDL_SCANCODE_KP_MULTIPLY, C64K_ASTERISK },
    { SDL_SCANCODE_KP_MINUS, C64K_MINUS }, { SDL_SCANCODE_KP_DIVIDE, C64K_SLASH },
};

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "Commodore 64 - %s%s%s  (F9 media, F11 tape, F8 joy %d, F12 turbo)%s",
             mm.now[0] ? mm.now : "(no media)", c64_tape_playing(m) ? " [PLAY]" : "",
             sdl_port_turbo() ? " [TURBO]" : "", joy_port, c64_has_drive(m) ? "" : " - no 1541 ROM");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (c64_load_file(m, path)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", c64_last_error(m));
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
        if (!c64_tape_loaded(m)) comp_note(&mm, "No tape loaded");
        else {
            c64_tape_toggle(m);
            comp_note(&mm, c64_tape_playing(m) ? "Tape playing" : "Tape paused");
        }
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!c64_rom_kernal) {
        fprintf(stderr, "No C64 ROMs built in: put them in roms/c64/ and run tools/regen.sh c64\n");
        return 1;
    }
    m = c64_create(C64_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    c64_set_roms(m, c64_rom_kernal, c64_rom_basic, c64_rom_chargen, c64_rom_dos1541);
    mm = (comp_menu){ frame, C64_PIC_W, C64_PIC_H, "C64 MEDIA  F9", C64_MEDIA_LABEL,
                      "*.prg *.PRG *.t64 *.T64 *.d64 *.D64 *.g64 *.G64 *.tap *.TAP",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose media", "Tape play / stop", "Close menu" },
#else
                      { "Open media file", "Tape play / stop", "Close menu" },
#endif
                      0, 3, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { "Commodore 64", C64_PIC_W, C64_PIC_H, 2, C64_AUDIO_RATE, C64_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("C64 media (.prg .t64 .d64 .g64 .tap)", ".prg,.t64,.d64,.g64,.tap");

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
            if (k == SDL_SCANCODE_F11) { c64_tape_toggle(m); set_title(); }
            if (k == SDL_SCANCODE_F10) { c64_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F8) { joy_port = 3 - joy_port; set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        c64_keys_clear(m);
        uint8_t joy = 0;
        bool restore = false;
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) c64_key(m, keymap[i].key, true);
            // cursors: right / down, Shift for left / up
            if (sdl_port_key(SDL_SCANCODE_RIGHT)) c64_key(m, C64K_CRSR_LR, true);
            if (sdl_port_key(SDL_SCANCODE_DOWN)) c64_key(m, C64K_CRSR_UD, true);
            if (sdl_port_key(SDL_SCANCODE_LEFT)) { c64_key(m, C64K_CRSR_LR, true); c64_key(m, C64K_LSHIFT, true); }
            if (sdl_port_key(SDL_SCANCODE_UP)) { c64_key(m, C64K_CRSR_UD, true); c64_key(m, C64K_LSHIFT, true); }
            if (sdl_port_key(SDL_SCANCODE_F2)) { c64_key(m, C64K_F1, true); c64_key(m, C64K_LSHIFT, true); }
            if (sdl_port_key(SDL_SCANCODE_F4)) { c64_key(m, C64K_F3, true); c64_key(m, C64K_LSHIFT, true); }
            if (sdl_port_key(SDL_SCANCODE_F6)) { c64_key(m, C64K_F5, true); c64_key(m, C64K_LSHIFT, true); }
            if (sdl_port_key(SDL_SCANCODE_KP_8)) joy |= C64_JOY_UP;
            if (sdl_port_key(SDL_SCANCODE_KP_2)) joy |= C64_JOY_DOWN;
            if (sdl_port_key(SDL_SCANCODE_KP_4)) joy |= C64_JOY_LEFT;
            if (sdl_port_key(SDL_SCANCODE_KP_6)) joy |= C64_JOY_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || sdl_port_key(SDL_SCANCODE_RCTRL))
                joy |= C64_JOY_FIRE;
            restore = sdl_port_key(SDL_SCANCODE_PAGEDOWN);
        }
        c64_joy(m, joy_port, joy);
        c64_joy(m, 3 - joy_port, 0);
        c64_restore(m, restore);

        const int16_t *audio;
        int n = c64_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < C64_PIC_H; y++) c64_render_row(m, y, frame + y * C64_PIC_W);
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    c64_destroy(m);
    return 0;
}
