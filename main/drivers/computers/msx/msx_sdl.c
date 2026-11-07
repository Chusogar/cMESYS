// msx_sdl.c - MSX1 / MSX2 on desktop (SDL2) and web (Emscripten). Built as
// msx1 or msx2 (MSX_MODEL; the ROMs of that model built in).
//
//   F9  media menu (.rom .dsk .cas .tsx)    F10 reset    F11 eject the cartridge
//   F8  cursors + Space / M as joystick 1 (on / off)    F12 TURBO
//   F1-F5 the MSX's, F6 SELECT, End STOP, Left Alt GRAPH, Right Alt CODE,
//   Insert INS, Delete DEL, Home HOME. Keypad: 8 4 6 2 + 0 / 5 joystick 1.
// A tape loads through the BIOS and runs by itself (CLOAD / RUN"CAS:" /
// BLOAD"CAS:",R are typed for you). Drag-and-drop a file onto the window.
#include "msx_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

#ifndef MSX_MODEL
#define MSX_MODEL 1
#endif

static uint32_t frame[MSX_PIC_W * MSX_PIC_H];
static msx *m;
static comp_menu mm;
static bool joy_mode;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, MSXK_A }, { SDL_SCANCODE_B, MSXK_B }, { SDL_SCANCODE_C, MSXK_C }, { SDL_SCANCODE_D, MSXK_D },
    { SDL_SCANCODE_E, MSXK_E }, { SDL_SCANCODE_F, MSXK_F }, { SDL_SCANCODE_G, MSXK_G }, { SDL_SCANCODE_H, MSXK_H },
    { SDL_SCANCODE_I, MSXK_I }, { SDL_SCANCODE_J, MSXK_J }, { SDL_SCANCODE_K, MSXK_K }, { SDL_SCANCODE_L, MSXK_L },
    { SDL_SCANCODE_M, MSXK_M }, { SDL_SCANCODE_N, MSXK_N }, { SDL_SCANCODE_O, MSXK_O }, { SDL_SCANCODE_P, MSXK_P },
    { SDL_SCANCODE_Q, MSXK_Q }, { SDL_SCANCODE_R, MSXK_R }, { SDL_SCANCODE_S, MSXK_S }, { SDL_SCANCODE_T, MSXK_T },
    { SDL_SCANCODE_U, MSXK_U }, { SDL_SCANCODE_V, MSXK_V }, { SDL_SCANCODE_W, MSXK_W }, { SDL_SCANCODE_X, MSXK_X },
    { SDL_SCANCODE_Y, MSXK_Y }, { SDL_SCANCODE_Z, MSXK_Z },
    { SDL_SCANCODE_0, MSXK_0 }, { SDL_SCANCODE_1, MSXK_1 }, { SDL_SCANCODE_2, MSXK_2 }, { SDL_SCANCODE_3, MSXK_3 },
    { SDL_SCANCODE_4, MSXK_4 }, { SDL_SCANCODE_5, MSXK_5 }, { SDL_SCANCODE_6, MSXK_6 }, { SDL_SCANCODE_7, MSXK_7 },
    { SDL_SCANCODE_8, MSXK_8 }, { SDL_SCANCODE_9, MSXK_9 },
    { SDL_SCANCODE_MINUS, MSXK_MINUS }, { SDL_SCANCODE_EQUALS, MSXK_EQUALS }, { SDL_SCANCODE_BACKSLASH, MSXK_BACKSLASH },
    { SDL_SCANCODE_LEFTBRACKET, MSXK_LBRACKET }, { SDL_SCANCODE_RIGHTBRACKET, MSXK_RBRACKET },
    { SDL_SCANCODE_SEMICOLON, MSXK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, MSXK_QUOTE }, { SDL_SCANCODE_GRAVE, MSXK_GRAVE },
    { SDL_SCANCODE_COMMA, MSXK_COMMA }, { SDL_SCANCODE_PERIOD, MSXK_PERIOD }, { SDL_SCANCODE_SLASH, MSXK_SLASH },
    { SDL_SCANCODE_NONUSBACKSLASH, MSXK_DEAD },
    { SDL_SCANCODE_LSHIFT, MSXK_SHIFT }, { SDL_SCANCODE_RSHIFT, MSXK_SHIFT }, { SDL_SCANCODE_LCTRL, MSXK_CTRL },
    { SDL_SCANCODE_LALT, MSXK_GRAPH }, { SDL_SCANCODE_RALT, MSXK_CODE }, { SDL_SCANCODE_CAPSLOCK, MSXK_CAPS },
    { SDL_SCANCODE_F1, MSXK_F1 }, { SDL_SCANCODE_F2, MSXK_F2 }, { SDL_SCANCODE_F3, MSXK_F3 }, { SDL_SCANCODE_F4, MSXK_F4 },
    { SDL_SCANCODE_F5, MSXK_F5 }, { SDL_SCANCODE_F6, MSXK_SELECT }, { SDL_SCANCODE_END, MSXK_STOP },
    { SDL_SCANCODE_ESCAPE, MSXK_ESC }, { SDL_SCANCODE_TAB, MSXK_TAB }, { SDL_SCANCODE_BACKSPACE, MSXK_BS },
    { SDL_SCANCODE_RETURN, MSXK_RETURN }, { SDL_SCANCODE_KP_ENTER, MSXK_RETURN }, { SDL_SCANCODE_SPACE, MSXK_SPACE },
    { SDL_SCANCODE_HOME, MSXK_HOME }, { SDL_SCANCODE_INSERT, MSXK_INS }, { SDL_SCANCODE_DELETE, MSXK_DEL },
    { SDL_SCANCODE_LEFT, MSXK_LEFT }, { SDL_SCANCODE_UP, MSXK_UP }, { SDL_SCANCODE_DOWN, MSXK_DOWN },
    { SDL_SCANCODE_RIGHT, MSXK_RIGHT },
    { SDL_SCANCODE_KP_MULTIPLY, MSXK_KP_MUL }, { SDL_SCANCODE_KP_PLUS, MSXK_KP_PLUS },
    { SDL_SCANCODE_KP_DIVIDE, MSXK_KP_DIV }, { SDL_SCANCODE_KP_MINUS, MSXK_KP_MINUS },
    { SDL_SCANCODE_KP_1, MSXK_KP1 }, { SDL_SCANCODE_KP_3, MSXK_KP3 }, { SDL_SCANCODE_KP_7, MSXK_KP7 },
    { SDL_SCANCODE_KP_9, MSXK_KP9 }, { SDL_SCANCODE_KP_PERIOD, MSXK_KP_PERIOD },
};

static const char *title(void) { return MSX_MODEL == 2 ? "MSX2" : "MSX"; }

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "%s - %s%s%s  (F9 media, F10 reset, F8 joystick, F12 turbo)%s", title(),
             mm.now[0] ? mm.now : "(no media)", joy_mode ? " [JOY]" : "", sdl_port_turbo() ? " [TURBO]" : "",
             msx_has_disk_rom(m) ? "" : " - no disk ROM");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (msx_load_file(m, path)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", msx_last_error(m));
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
        msx_eject_cart(m);
        mm.now[0] = 0;
        comp_note(&mm, "Cartridge out, reset");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!msx_rom_bios || (MSX_MODEL == 2 && !msx_rom_sub)) {
        fprintf(stderr, "No %s ROMs built in: put them in roms/msx%d/ and run tools/regen.sh msx%d\n", title(),
                MSX_MODEL, MSX_MODEL);
        return 1;
    }
    m = msx_create(MSX_MODEL, 256, 128, MSX_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    msx_set_roms(m, msx_rom_bios, msx_rom_sub, msx_rom_disk);
    mm = (comp_menu){ frame, MSX_PIC_W, MSX_PIC_H, MSX_MODEL == 2 ? "MSX2 MEDIA  F9" : "MSX MEDIA  F9", MSX_MEDIA_LABEL,
                      "*.rom *.ROM *.mx1 *.mx2 *.dsk *.DSK *.cas *.CAS *.tsx *.TSX *.tzx *.TZX",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose media", "Eject cartridge", "Close menu" },
#else
                      { "Open media file", "Eject cartridge", "Close menu" },
#endif
                      0, 3, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { title(), MSX_PIC_W, MSX_PIC_H, 2, MSX_AUDIO_RATE, msx_fps(m), true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup(MSX_MODEL == 2 ? "MSX2 media (.rom .dsk .cas .tsx)" : "MSX media (.rom .dsk .cas .tsx)",
                   ".rom,.mx1,.mx2,.dsk,.cas,.tsx,.tzx");

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
            if (k == SDL_SCANCODE_F10) { msx_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F11) { msx_eject_cart(m); mm.now[0] = 0; set_title(); }
            if (k == SDL_SCANCODE_F8) { joy_mode = !joy_mode; set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        msx_keys_clear(m);
        uint8_t joy = 0;
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
                if (!sdl_port_key(keymap[i].sc)) continue;
                const int key = keymap[i].key;
                if (joy_mode && (key == MSXK_UP || key == MSXK_DOWN || key == MSXK_LEFT || key == MSXK_RIGHT ||
                                 key == MSXK_SPACE || key == MSXK_M)) continue;
                msx_key(m, key, true);
            }
            if (sdl_port_key(SDL_SCANCODE_KP_8) || (joy_mode && sdl_port_key(SDL_SCANCODE_UP))) joy |= MSX_JOY_UP;
            if (sdl_port_key(SDL_SCANCODE_KP_2) || (joy_mode && sdl_port_key(SDL_SCANCODE_DOWN))) joy |= MSX_JOY_DOWN;
            if (sdl_port_key(SDL_SCANCODE_KP_4) || (joy_mode && sdl_port_key(SDL_SCANCODE_LEFT))) joy |= MSX_JOY_LEFT;
            if (sdl_port_key(SDL_SCANCODE_KP_6) || (joy_mode && sdl_port_key(SDL_SCANCODE_RIGHT))) joy |= MSX_JOY_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || (joy_mode && sdl_port_key(SDL_SCANCODE_SPACE)))
                joy |= MSX_JOY_A;
            if (sdl_port_key(SDL_SCANCODE_RCTRL) || (joy_mode && sdl_port_key(SDL_SCANCODE_M))) joy |= MSX_JOY_B;
        }
        msx_joy(m, 1, joy);
        msx_joy(m, 2, 0);

        msx_set_turbo(m, sdl_port_turbo());
        const int16_t *audio;
        int n = msx_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < MSX_PIC_H; y++) msx_render_row(m, y, frame + y * MSX_PIC_W);
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    msx_destroy(m);
    return 0;
}
