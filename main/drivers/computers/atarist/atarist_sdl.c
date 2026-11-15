// atarist_sdl.c - Atari ST on desktop (SDL2) and web (Emscripten): TOS
// built in (roms/atarist: TOS 1.04 / 1.02 / 1.00, or EmuTOS), 1 MB of RAM.
//
//   the PC's keys are the ST's (Alt ALTERNATE, Ctrl CONTROL, Page Up UNDO,
//   Page Down HELP, Home CLR/HOME, the keypad the ST's); F1-F10 the ST's
//   the mouse is the ST's mouse (the host's arrow hidden over the picture)
//   F11 media menu (.st .msa .stx: drive A, then B; reset; joystick)
//   F12 TURBO
//   joystick mode (menu): cursors = joystick 1, Right Ctrl its fire
// Drag-and-drop a disk onto the window (web: Upload bar): drive A.
#include "atarist_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[ST_W * ST_H];
static comp_menu mm;
static bool joy_mode;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_ESCAPE, STK_ESC }, { SDL_SCANCODE_1, 0x02 }, { SDL_SCANCODE_2, 0x03 }, { SDL_SCANCODE_3, 0x04 },
    { SDL_SCANCODE_4, 0x05 }, { SDL_SCANCODE_5, 0x06 }, { SDL_SCANCODE_6, 0x07 }, { SDL_SCANCODE_7, 0x08 },
    { SDL_SCANCODE_8, 0x09 }, { SDL_SCANCODE_9, 0x0a }, { SDL_SCANCODE_0, 0x0b }, { SDL_SCANCODE_MINUS, 0x0c },
    { SDL_SCANCODE_EQUALS, 0x0d }, { SDL_SCANCODE_GRAVE, STK_BACKQUOTE }, { SDL_SCANCODE_BACKSPACE, 0x0e },
    { SDL_SCANCODE_TAB, 0x0f }, { SDL_SCANCODE_Q, 0x10 }, { SDL_SCANCODE_W, 0x11 }, { SDL_SCANCODE_E, 0x12 },
    { SDL_SCANCODE_R, 0x13 }, { SDL_SCANCODE_T, 0x14 }, { SDL_SCANCODE_Y, 0x15 }, { SDL_SCANCODE_U, 0x16 },
    { SDL_SCANCODE_I, 0x17 }, { SDL_SCANCODE_O, 0x18 }, { SDL_SCANCODE_P, 0x19 }, { SDL_SCANCODE_LEFTBRACKET, 0x1a },
    { SDL_SCANCODE_RIGHTBRACKET, 0x1b }, { SDL_SCANCODE_RETURN, 0x1c }, { SDL_SCANCODE_DELETE, STK_DELETE },
    { SDL_SCANCODE_LCTRL, STK_CONTROL }, { SDL_SCANCODE_A, 0x1e }, { SDL_SCANCODE_S, 0x1f }, { SDL_SCANCODE_D, 0x20 },
    { SDL_SCANCODE_F, 0x21 }, { SDL_SCANCODE_G, 0x22 }, { SDL_SCANCODE_H, 0x23 }, { SDL_SCANCODE_J, 0x24 },
    { SDL_SCANCODE_K, 0x25 }, { SDL_SCANCODE_L, 0x26 }, { SDL_SCANCODE_SEMICOLON, 0x27 },
    { SDL_SCANCODE_APOSTROPHE, 0x28 }, { SDL_SCANCODE_BACKSLASH, STK_BACKSLASH }, { SDL_SCANCODE_LSHIFT, STK_LSHIFT },
    { SDL_SCANCODE_NONUSBACKSLASH, STK_ISO }, { SDL_SCANCODE_Z, 0x2c }, { SDL_SCANCODE_X, 0x2d },
    { SDL_SCANCODE_C, 0x2e }, { SDL_SCANCODE_V, 0x2f }, { SDL_SCANCODE_B, 0x30 }, { SDL_SCANCODE_N, 0x31 },
    { SDL_SCANCODE_M, 0x32 }, { SDL_SCANCODE_COMMA, 0x33 }, { SDL_SCANCODE_PERIOD, 0x34 }, { SDL_SCANCODE_SLASH, 0x35 },
    { SDL_SCANCODE_RSHIFT, STK_RSHIFT }, { SDL_SCANCODE_LALT, STK_ALTERNATE }, { SDL_SCANCODE_RALT, STK_ALTERNATE },
    { SDL_SCANCODE_SPACE, STK_SPACE }, { SDL_SCANCODE_CAPSLOCK, STK_CAPS },
    { SDL_SCANCODE_F1, 0x3b }, { SDL_SCANCODE_F2, 0x3c }, { SDL_SCANCODE_F3, 0x3d }, { SDL_SCANCODE_F4, 0x3e },
    { SDL_SCANCODE_F5, 0x3f }, { SDL_SCANCODE_F6, 0x40 }, { SDL_SCANCODE_F7, 0x41 }, { SDL_SCANCODE_F8, 0x42 },
    { SDL_SCANCODE_F9, 0x43 }, { SDL_SCANCODE_F10, 0x44 },
    { SDL_SCANCODE_HOME, STK_CLRHOME }, { SDL_SCANCODE_INSERT, STK_INSERT }, { SDL_SCANCODE_PAGEUP, STK_UNDO },
    { SDL_SCANCODE_PAGEDOWN, STK_HELP },
    { SDL_SCANCODE_KP_MINUS, STK_KP_MINUS }, { SDL_SCANCODE_KP_PLUS, STK_KP_PLUS }, { SDL_SCANCODE_KP_DIVIDE, STK_KP_SLASH },
    { SDL_SCANCODE_KP_MULTIPLY, STK_KP_STAR }, { SDL_SCANCODE_KP_7, STK_KP7 }, { SDL_SCANCODE_KP_8, STK_KP8 },
    { SDL_SCANCODE_KP_9, STK_KP9 }, { SDL_SCANCODE_KP_4, STK_KP4 }, { SDL_SCANCODE_KP_5, STK_KP5 },
    { SDL_SCANCODE_KP_6, STK_KP6 }, { SDL_SCANCODE_KP_1, STK_KP1 }, { SDL_SCANCODE_KP_2, STK_KP2 },
    { SDL_SCANCODE_KP_3, STK_KP3 }, { SDL_SCANCODE_KP_0, STK_KP0 }, { SDL_SCANCODE_KP_PERIOD, STK_KP_PERIOD },
    { SDL_SCANCODE_KP_ENTER, STK_KP_ENTER }, { SDL_SCANCODE_NUMLOCKCLEAR, STK_KP_LPAREN },
};
static const int kCursors[4][2] = {
    { SDL_SCANCODE_UP, STK_UP }, { SDL_SCANCODE_DOWN, STK_DOWN }, { SDL_SCANCODE_LEFT, STK_LEFT }, { SDL_SCANCODE_RIGHT, STK_RIGHT },
};

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "Atari ST - %s%s  (F11 media, F12 turbo)%s%s", mm.now[0] ? mm.now : "(no disk)",
             atarist_has_disk(1) ? " + B:" : "", joy_mode ? " [JOY]" : "", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path, int drive)
{
    if (!path || !path[0]) return;
    if (atarist_load_file(drive, path)) {
        if (drive == 0) snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, comp_basename(path));
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", atarist_last_error());
        comp_note(&mm, msg);
    }
    set_title();
}

static void menu_activate(void)
{
    if (mm.sel == 0 || mm.sel == 1) {
        char *p = comp_pick_path(&mm);
        if (p) { apply_path(p, mm.sel); free(p); }
    } else if (mm.sel == 2) {
        atarist_eject(0);
        atarist_eject(1);
        mm.now[0] = 0;
        comp_note(&mm, "Disks out");
    } else if (mm.sel == 3) {
        atarist_reset();
        comp_note(&mm, "Reset");
    } else if (mm.sel == 4) {
        joy_mode = !joy_mode;
        comp_note(&mm, joy_mode ? "Cursors + Right Ctrl: joystick 1" : "Cursors: the ST's cursor keys");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!atarist_rom_tos) {
        fprintf(stderr, "No TOS built in: put tos104.bin (or EmuTOS's etos192uk.img) in roms/atarist/ "
                        "and run tools/regen.sh atarist\n");
        return 1;
    }
    if (!atarist_init(atarist_rom_tos, ST_ROM_SIZE, 0x100000, ST_AUDIO_RATE)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    mm = (comp_menu){ frame, ST_W, ST_H, "ATARI ST MEDIA  F11", ST_MEDIA_LABEL, "*.st *.ST *.msa *.MSA *.stx *.STX",
#ifdef __EMSCRIPTEN__
                      { "Upload disk A:", "Upload disk B:", "Eject the disks", "Reset", "Joystick on / off", "Close menu" },
#else
                      { "Open disk A:", "Open disk B:", "Eject the disks", "Reset", "Joystick on / off", "Close menu" },
#endif
                      0, 6, false, "", "" };
    if (argc > 1) apply_path(argv[1], 0);
    if (argc > 2) apply_path(argv[2], 1);

    sdl_port_cfg cfg = { "Atari ST", ST_W, ST_H, 1, ST_AUDIO_RATE, ST_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    sdl_port_show_cursor(false);
    set_title();
    comp_web_setup("Atari ST disks (.st .msa .stx)", ".st,.msa,.stx");

    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            apply_path(drop, 0);
            free(drop);
            mm.open = false;
        }
        int k;
        while ((k = sdl_port_next_press()) >= 0) {
            if (mm.open) {
                if (k == SDL_SCANCODE_UP) { if (mm.sel > 0) mm.sel--; }
                else if (k == SDL_SCANCODE_DOWN) { if (mm.sel < mm.n - 1) mm.sel++; }
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_SPACE) menu_activate();
                else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F11) mm.open = false;
                sdl_port_show_cursor(mm.open);
                continue;
            }
            if (k == SDL_SCANCODE_F11) { mm.open = true; mm.sel = 0; mm.msg[0] = 0; sdl_port_show_cursor(true); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        atarist_keys_clear();
        unsigned joy = 0;
        atarist_pointer ptr = { 0 };
        if (!mm.open) {
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) atarist_key(keymap[i].key, true);
            if (joy_mode) {
                if (sdl_port_key(SDL_SCANCODE_UP)) joy |= ST_UP;
                if (sdl_port_key(SDL_SCANCODE_DOWN)) joy |= ST_DOWN;
                if (sdl_port_key(SDL_SCANCODE_LEFT)) joy |= ST_LEFT;
                if (sdl_port_key(SDL_SCANCODE_RIGHT)) joy |= ST_RIGHT;
                if (sdl_port_key(SDL_SCANCODE_RCTRL)) joy |= ST_FIRE;
            } else {
                for (int i = 0; i < 4; i++)
                    if (sdl_port_key(kCursors[i][0])) atarist_key(kCursors[i][1], true);
                if (sdl_port_key(SDL_SCANCODE_RCTRL)) atarist_key(STK_CONTROL, true);
            }
            unsigned b = 0;
            bool entered = false;
            ptr.has_pointer = sdl_port_mouse(&ptr.x, &ptr.y, &b, &entered);
            ptr.resync = entered;
            ptr.left = (b & 1) != 0;
            ptr.right = (b & 2) != 0;
        }
        atarist_joy(1, joy);
        atarist_pointer_input(&ptr);

        const int16_t *audio;
        atarist_set_turbo(sdl_port_turbo());
        const int n = atarist_run_frame(&audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < ST_H; y++) atarist_render_row(y, frame + y * ST_W);
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
