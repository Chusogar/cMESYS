// apple2_sdl.c - the Apple II family on desktop (SDL2) and web (Emscripten).
// Built as apple2, apple2p, apple2e or apple2ee (A2_MODEL; the ROMs of that
// model built in).
//
//   F9  disk menu (.dsk .do .po .nib)      F10 power off / on      F11 RESET (CTRL-RESET)
//   F12 TURBO    Left Alt / Left Windows = Open Apple, Right Alt / Right Windows =
//   Closed Apple (the joystick's buttons); keypad 8 4 6 2 joystick, 0 / 5 and . its
//   buttons. Caps Lock is the //e's CAPS LOCK; Page Up the ][s' REPT.
// The keys are the machine's by position (a US layout); the ][ and ][+ print
// upper case only. Drag-and-drop a disk image onto the window.
#include "apple2_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

#ifndef A2_MODEL
#define A2_MODEL A2_IIPLUS
#endif

static uint32_t frame[A2_W * A2_H * 2];
static apple2 *m;
static comp_menu mm;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, 'A' }, { SDL_SCANCODE_B, 'B' }, { SDL_SCANCODE_C, 'C' }, { SDL_SCANCODE_D, 'D' },
    { SDL_SCANCODE_E, 'E' }, { SDL_SCANCODE_F, 'F' }, { SDL_SCANCODE_G, 'G' }, { SDL_SCANCODE_H, 'H' },
    { SDL_SCANCODE_I, 'I' }, { SDL_SCANCODE_J, 'J' }, { SDL_SCANCODE_K, 'K' }, { SDL_SCANCODE_L, 'L' },
    { SDL_SCANCODE_M, 'M' }, { SDL_SCANCODE_N, 'N' }, { SDL_SCANCODE_O, 'O' }, { SDL_SCANCODE_P, 'P' },
    { SDL_SCANCODE_Q, 'Q' }, { SDL_SCANCODE_R, 'R' }, { SDL_SCANCODE_S, 'S' }, { SDL_SCANCODE_T, 'T' },
    { SDL_SCANCODE_U, 'U' }, { SDL_SCANCODE_V, 'V' }, { SDL_SCANCODE_W, 'W' }, { SDL_SCANCODE_X, 'X' },
    { SDL_SCANCODE_Y, 'Y' }, { SDL_SCANCODE_Z, 'Z' },
    { SDL_SCANCODE_0, '0' }, { SDL_SCANCODE_1, '1' }, { SDL_SCANCODE_2, '2' }, { SDL_SCANCODE_3, '3' },
    { SDL_SCANCODE_4, '4' }, { SDL_SCANCODE_5, '5' }, { SDL_SCANCODE_6, '6' }, { SDL_SCANCODE_7, '7' },
    { SDL_SCANCODE_8, '8' }, { SDL_SCANCODE_9, '9' },
    { SDL_SCANCODE_RETURN, A2K_RETURN }, { SDL_SCANCODE_KP_ENTER, A2K_RETURN }, { SDL_SCANCODE_SPACE, A2K_SPACE },
    { SDL_SCANCODE_ESCAPE, A2K_ESC }, { SDL_SCANCODE_TAB, A2K_TAB }, { SDL_SCANCODE_BACKSPACE, A2K_LEFT },
    { SDL_SCANCODE_DELETE, A2K_DELETE },
    { SDL_SCANCODE_LEFT, A2K_LEFT }, { SDL_SCANCODE_RIGHT, A2K_RIGHT }, { SDL_SCANCODE_UP, A2K_UP },
    { SDL_SCANCODE_DOWN, A2K_DOWN },
    { SDL_SCANCODE_LSHIFT, A2K_SHIFT }, { SDL_SCANCODE_RSHIFT, A2K_RSHIFT }, { SDL_SCANCODE_LCTRL, A2K_CTRL },
    { SDL_SCANCODE_RCTRL, A2K_CTRL }, { SDL_SCANCODE_CAPSLOCK, A2K_CAPS },
    { SDL_SCANCODE_LALT, A2K_OPEN_APPLE }, { SDL_SCANCODE_LGUI, A2K_OPEN_APPLE },
    { SDL_SCANCODE_RALT, A2K_CLOSED_APPLE }, { SDL_SCANCODE_RGUI, A2K_CLOSED_APPLE },
    { SDL_SCANCODE_PAGEUP, A2K_REPT },
    { SDL_SCANCODE_MINUS, A2K_MINUS }, { SDL_SCANCODE_EQUALS, A2K_EQUALS },
    { SDL_SCANCODE_LEFTBRACKET, A2K_LBRACKET }, { SDL_SCANCODE_RIGHTBRACKET, A2K_RBRACKET },
    { SDL_SCANCODE_BACKSLASH, A2K_BACKSLASH }, { SDL_SCANCODE_SEMICOLON, A2K_SEMICOLON },
    { SDL_SCANCODE_APOSTROPHE, A2K_QUOTE }, { SDL_SCANCODE_GRAVE, A2K_GRAVE },
    { SDL_SCANCODE_COMMA, A2K_COMMA }, { SDL_SCANCODE_PERIOD, A2K_PERIOD }, { SDL_SCANCODE_SLASH, A2K_SLASH },
};

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "%s - %s%s%s  (F9 disk, F11 reset, F10 power, F12 turbo)%s", apple2_title(A2_MODEL),
             mm.now[0] ? mm.now : "(no disk)", apple2_drive_led(m) ? " [DRIVE]" : "",
             sdl_port_turbo() ? " [TURBO]" : "", apple2_has_disk_card(m) ? "" : " - no Disk II PROM");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    const char *err = apple2_load_file(m, path);
    if (!err) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
        // a disk put in at power on boots (the ROM scans the slots then)
        apple2_reset(m);
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
        apple2_eject(m);
        mm.now[0] = 0;
        comp_note(&mm, "Disk ejected");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!a2_rom_load()) sdl_port_rom_error();
    if (!a2_rom_main) {
        fprintf(stderr, "No %s ROMs: put MAME's set in roms/ (see README)\n", apple2_title(A2_MODEL));
        return 1;
    }
    m = apple2_create(A2_MODEL, A2_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    apple2_set_roms(m, a2_rom_main, a2_main_size(A2_MODEL), a2_rom_chr, a2_chr_size(A2_MODEL), a2_rom_disk, a2_rom_aid);
    apple2_reset(m);
    mm = (comp_menu){ frame, A2_W, A2_H * 2, "APPLE II DISK  F9", A2_MEDIA_LABEL,
                      "*.dsk *.DSK *.do *.DO *.po *.PO *.nib *.NIB",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose a disk", "Eject the disk", "Close menu" },
#else
                      { "Open a disk image", "Eject the disk", "Close menu" },
#endif
                      0, 3, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { apple2_title(A2_MODEL), A2_W, A2_H * 2, 1, A2_AUDIO_RATE, A2_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("Apple II disks (.dsk .do .po .nib)", ".dsk,.do,.po,.nib");

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
            if (k == SDL_SCANCODE_F10) { apple2_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F11 || k == SDL_SCANCODE_PAUSE) apple2_reset_key(m);
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        uint8_t joy = 0;
        if (!mm.open) {
            bool held[A2K_COUNT] = { false };
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
            for (int c = 0; c < A2K_COUNT; c++) apple2_key(m, c, held[c]);
            if (sdl_port_key(SDL_SCANCODE_KP_8)) joy |= A2_JOY_UP;
            if (sdl_port_key(SDL_SCANCODE_KP_2)) joy |= A2_JOY_DOWN;
            if (sdl_port_key(SDL_SCANCODE_KP_4)) joy |= A2_JOY_LEFT;
            if (sdl_port_key(SDL_SCANCODE_KP_6)) joy |= A2_JOY_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5)) joy |= A2_JOY_B0;
            if (sdl_port_key(SDL_SCANCODE_KP_PERIOD)) joy |= A2_JOY_B1;
        } else {
            apple2_keys_clear(m);
        }
        apple2_joy(m, joy);

        const int16_t *audio;
        apple2_set_turbo(m, sdl_port_turbo());
        int n = apple2_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < A2_H; y++) {
            uint32_t *row = frame + y * 2 * A2_W;
            apple2_render_row(m, y, row);
            memcpy(row + A2_W, row, A2_W * sizeof *row);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
        if (apple2_drive_led(m) != led) {
            led = apple2_drive_led(m);
            set_title();
        }
    }
    sdl_port_close();
    apple2_destroy(m);
    return 0;
}
