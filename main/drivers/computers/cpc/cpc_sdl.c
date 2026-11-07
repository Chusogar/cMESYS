// cpc_sdl.c - Amstrad CPC 6128 on desktop (SDL2) and web (Emscripten).
//
//   F9  media menu (.sna .dsk .cdt .tzx .csw)      F6  tape play / pause
//   F3  reset    F8  cursors + Space / Alt as joystick (on / off)
//   F10 colour / green monitor    F12 TURBO
// The PC keyboard is the CPC's by position; the keypad is the CPC's (F0-F9,
// ".", Enter). Drag-and-drop a file onto the window (web: Upload bar).
// Command line: cpc6128 [file] [--464 | --664]
#include "cpc_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[CPC_PIC_W * CPC_PIC_H];
static cpc *m;
static comp_menu mm;
static bool joy_mode;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_UP, CPCK_UP }, { SDL_SCANCODE_DOWN, CPCK_DOWN }, { SDL_SCANCODE_LEFT, CPCK_LEFT },
    { SDL_SCANCODE_RIGHT, CPCK_RIGHT }, { SDL_SCANCODE_RETURN, CPCK_RETURN }, { SDL_SCANCODE_SPACE, CPCK_SPACE },
    { SDL_SCANCODE_LSHIFT, CPCK_SHIFT }, { SDL_SCANCODE_RSHIFT, CPCK_SHIFT }, { SDL_SCANCODE_LCTRL, CPCK_CTRL },
    { SDL_SCANCODE_RCTRL, CPCK_CTRL }, { SDL_SCANCODE_LALT, CPCK_COPY }, { SDL_SCANCODE_ESCAPE, CPCK_ESC },
    { SDL_SCANCODE_TAB, CPCK_TAB }, { SDL_SCANCODE_CAPSLOCK, CPCK_CAPSLOCK }, { SDL_SCANCODE_BACKSPACE, CPCK_DEL },
    { SDL_SCANCODE_DELETE, CPCK_CLR },
    { SDL_SCANCODE_A, CPCK_A }, { SDL_SCANCODE_B, CPCK_B }, { SDL_SCANCODE_C, CPCK_C }, { SDL_SCANCODE_D, CPCK_D },
    { SDL_SCANCODE_E, CPCK_E }, { SDL_SCANCODE_F, CPCK_F }, { SDL_SCANCODE_G, CPCK_G }, { SDL_SCANCODE_H, CPCK_H },
    { SDL_SCANCODE_I, CPCK_I }, { SDL_SCANCODE_J, CPCK_J }, { SDL_SCANCODE_K, CPCK_K }, { SDL_SCANCODE_L, CPCK_L },
    { SDL_SCANCODE_M, CPCK_M }, { SDL_SCANCODE_N, CPCK_N }, { SDL_SCANCODE_O, CPCK_O }, { SDL_SCANCODE_P, CPCK_P },
    { SDL_SCANCODE_Q, CPCK_Q }, { SDL_SCANCODE_R, CPCK_R }, { SDL_SCANCODE_S, CPCK_S }, { SDL_SCANCODE_T, CPCK_T },
    { SDL_SCANCODE_U, CPCK_U }, { SDL_SCANCODE_V, CPCK_V }, { SDL_SCANCODE_W, CPCK_W }, { SDL_SCANCODE_X, CPCK_X },
    { SDL_SCANCODE_Y, CPCK_Y }, { SDL_SCANCODE_Z, CPCK_Z },
    { SDL_SCANCODE_0, CPCK_0 }, { SDL_SCANCODE_1, CPCK_1 }, { SDL_SCANCODE_2, CPCK_2 }, { SDL_SCANCODE_3, CPCK_3 },
    { SDL_SCANCODE_4, CPCK_4 }, { SDL_SCANCODE_5, CPCK_5 }, { SDL_SCANCODE_6, CPCK_6 }, { SDL_SCANCODE_7, CPCK_7 },
    { SDL_SCANCODE_8, CPCK_8 }, { SDL_SCANCODE_9, CPCK_9 },
    { SDL_SCANCODE_MINUS, CPCK_MINUS }, { SDL_SCANCODE_EQUALS, CPCK_CARET }, { SDL_SCANCODE_LEFTBRACKET, CPCK_AT },
    { SDL_SCANCODE_RIGHTBRACKET, CPCK_LBRACKET }, { SDL_SCANCODE_BACKSLASH, CPCK_RBRACKET },
    { SDL_SCANCODE_SEMICOLON, CPCK_COLON }, { SDL_SCANCODE_APOSTROPHE, CPCK_SEMICOLON },
    { SDL_SCANCODE_COMMA, CPCK_COMMA }, { SDL_SCANCODE_PERIOD, CPCK_PERIOD }, { SDL_SCANCODE_SLASH, CPCK_SLASH },
    { SDL_SCANCODE_NONUSBACKSLASH, CPCK_BACKSLASH }, { SDL_SCANCODE_GRAVE, CPCK_BACKSLASH },
    { SDL_SCANCODE_KP_0, CPCK_F0 }, { SDL_SCANCODE_KP_1, CPCK_F1 }, { SDL_SCANCODE_KP_2, CPCK_F2 },
    { SDL_SCANCODE_KP_3, CPCK_F3 }, { SDL_SCANCODE_KP_4, CPCK_F4 }, { SDL_SCANCODE_KP_5, CPCK_F5 },
    { SDL_SCANCODE_KP_6, CPCK_F6 }, { SDL_SCANCODE_KP_7, CPCK_F7 }, { SDL_SCANCODE_KP_8, CPCK_F8 },
    { SDL_SCANCODE_KP_9, CPCK_F9 }, { SDL_SCANCODE_KP_PERIOD, CPCK_KPDOT }, { SDL_SCANCODE_KP_ENTER, CPCK_KPENTER },
};

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "%s - %s%s%s%s  (F9 media, F6 tape, F8 joystick, F12 turbo)", cpc_title(cpc_model(m)),
             mm.now[0] ? mm.now : "(no media)", cpc_tape_playing(m) ? " [PLAY]" : "",
             joy_mode ? " [JOY]" : "", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (cpc_load_file(m, path)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", cpc_last_error(m));
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
        if (!cpc_tape_loaded(m)) comp_note(&mm, "No tape loaded");
        else {
            cpc_tape_toggle(m);
            comp_note(&mm, cpc_tape_playing(m) ? "Tape playing" : "Tape paused");
        }
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    cpc_model_t model = CPC_6128;
    const char *file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--464")) model = CPC_464;
        else if (!strcmp(argv[i], "--664")) model = CPC_664;
        else file = argv[i];
    }
    if (!cpc_rom_os) {
        fprintf(stderr, "No CPC ROMs built in: put cpc6128.rom and amsdos.rom in roms/cpc6128/ and run tools/regen.sh cpc6128\n");
        return 1;
    }
    m = cpc_create(model, CPC_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    cpc_set_roms(m, cpc_rom_os, cpc_rom_amsdos);
    mm = (comp_menu){ frame, CPC_PIC_W, CPC_PIC_H, "CPC MEDIA  F9", CPC_MEDIA_LABEL,
                      "*.sna *.SNA *.dsk *.DSK *.edsk *.EDSK *.cdt *.CDT *.tzx *.TZX *.csw *.CSW",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose media", "Tape play / stop", "Close menu" },
#else
                      { "Open media file", "Tape play / stop", "Close menu" },
#endif
                      0, 3, false, "", "" };
    if (file) apply_path(file);

    sdl_port_cfg cfg = { cpc_title(model), CPC_PIC_W, CPC_PIC_H, 2, CPC_AUDIO_RATE, CPC_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("CPC media (.sna .dsk .cdt .tzx .csw)", ".sna,.dsk,.edsk,.cdt,.tzx,.csw");

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
            if (k == SDL_SCANCODE_F6) { cpc_tape_toggle(m); set_title(); }
            if (k == SDL_SCANCODE_F3) { cpc_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F8) { joy_mode = !joy_mode; set_title(); }
            if (k == SDL_SCANCODE_F10) {
                static bool green;
                green = !green;
                cpc_set_color_monitor(m, !green);
            }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        cpc_keys_clear(m);
        uint8_t joy = 0;
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
                if (!sdl_port_key(keymap[i].sc)) continue;
                const int key = keymap[i].key;
                if (joy_mode && (key == CPCK_UP || key == CPCK_DOWN || key == CPCK_LEFT || key == CPCK_RIGHT ||
                                 key == CPCK_SPACE || key == CPCK_COPY))
                    continue;
                cpc_key(m, key, true);
            }
            if (joy_mode) {
                if (sdl_port_key(SDL_SCANCODE_UP)) joy |= CPC_JOY_UP;
                if (sdl_port_key(SDL_SCANCODE_DOWN)) joy |= CPC_JOY_DOWN;
                if (sdl_port_key(SDL_SCANCODE_LEFT)) joy |= CPC_JOY_LEFT;
                if (sdl_port_key(SDL_SCANCODE_RIGHT)) joy |= CPC_JOY_RIGHT;
                if (sdl_port_key(SDL_SCANCODE_SPACE)) joy |= CPC_JOY_FIRE1;
                if (sdl_port_key(SDL_SCANCODE_LALT)) joy |= CPC_JOY_FIRE2;
            }
        }
        cpc_joy(m, joy);

        const int16_t *audio;
        int n = cpc_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        {
            static int last = -1;
            if (cpc_tape_playing(m) != last) { last = cpc_tape_playing(m); set_title(); }
        }
        for (int y = 0; y < CPC_PIC_H; y++)
            cpc_render_row(m, CPC_PIC_Y + y, CPC_PIC_X, CPC_PIC_W, frame + y * CPC_PIC_W);
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    cpc_destroy(m);
    return 0;
}
