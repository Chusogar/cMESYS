// samcoupe_sdl.c - the SAM Coupe on desktop (SDL2: Linux, Windows) and web
// (Emscripten).
//
//   samcoupe [disk1 [disk2]] [tape.tap|.tzx]
//
//   F11 menu (a disk in drive 1 / 2 or a tape, BOOT, play / stop the tape,
//       eject, reset)   F12 TURBO   F1-F10 are the SAM's (F10 = F0); F9 in
//       BASIC boots the disk in drive 1 (BOOT presses RETURN and F9). A disk
//       given on the command line is booted.
// The PC keyboard: Shift = SHIFT, Left Ctrl = CNTRL, Right Ctrl / Alt Gr =
// SYMBOL, Esc, Tab, Caps Lock, Backspace = DELETE, Home = EDIT, End = INV,
// - + (keypad + too) = " ; : , . and the cursors. A gamepad or the keypad
// (8 4 6 2, 0 / 5 fire) is joystick 1 (the keys 6 7 8 9 0). Drag-and-drop:
// a disk goes to drive 1, a tape in. What is written on an MGT disk is saved
// to its file when it comes out (and at the end).
#include <stdlib.h>
#include "samcoupe_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[SAM_W * SAM_H * 2];
static comp_menu mm;
static char paths[2][512];
static int target_drive;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, SK_A }, { SDL_SCANCODE_B, SK_B }, { SDL_SCANCODE_C, SK_C }, { SDL_SCANCODE_D, SK_D },
    { SDL_SCANCODE_E, SK_E }, { SDL_SCANCODE_F, SK_F }, { SDL_SCANCODE_G, SK_G }, { SDL_SCANCODE_H, SK_H },
    { SDL_SCANCODE_I, SK_I }, { SDL_SCANCODE_J, SK_J }, { SDL_SCANCODE_K, SK_K }, { SDL_SCANCODE_L, SK_L },
    { SDL_SCANCODE_M, SK_M }, { SDL_SCANCODE_N, SK_N }, { SDL_SCANCODE_O, SK_O }, { SDL_SCANCODE_P, SK_P },
    { SDL_SCANCODE_Q, SK_Q }, { SDL_SCANCODE_R, SK_R }, { SDL_SCANCODE_S, SK_S }, { SDL_SCANCODE_T, SK_T },
    { SDL_SCANCODE_U, SK_U }, { SDL_SCANCODE_V, SK_V }, { SDL_SCANCODE_W, SK_W }, { SDL_SCANCODE_X, SK_X },
    { SDL_SCANCODE_Y, SK_Y }, { SDL_SCANCODE_Z, SK_Z },
    { SDL_SCANCODE_0, SK_0 }, { SDL_SCANCODE_1, SK_1 }, { SDL_SCANCODE_2, SK_2 }, { SDL_SCANCODE_3, SK_3 },
    { SDL_SCANCODE_4, SK_4 }, { SDL_SCANCODE_5, SK_5 }, { SDL_SCANCODE_6, SK_6 }, { SDL_SCANCODE_7, SK_7 },
    { SDL_SCANCODE_8, SK_8 }, { SDL_SCANCODE_9, SK_9 },
    { SDL_SCANCODE_F1, SK_F1 }, { SDL_SCANCODE_F2, SK_F2 }, { SDL_SCANCODE_F3, SK_F3 }, { SDL_SCANCODE_F4, SK_F4 },
    { SDL_SCANCODE_F5, SK_F5 }, { SDL_SCANCODE_F6, SK_F6 }, { SDL_SCANCODE_F7, SK_F7 }, { SDL_SCANCODE_F8, SK_F8 },
    { SDL_SCANCODE_F9, SK_F9 }, { SDL_SCANCODE_F10, SK_F0 },
    { SDL_SCANCODE_RETURN, SK_RETURN }, { SDL_SCANCODE_KP_ENTER, SK_RETURN }, { SDL_SCANCODE_SPACE, SK_SPACE },
    { SDL_SCANCODE_LSHIFT, SK_SHIFT }, { SDL_SCANCODE_RSHIFT, SK_SHIFT },
    { SDL_SCANCODE_LCTRL, SK_CNTRL }, { SDL_SCANCODE_RCTRL, SK_SYMBOL }, { SDL_SCANCODE_RALT, SK_SYMBOL },
    { SDL_SCANCODE_LALT, SK_SYMBOL },
    { SDL_SCANCODE_ESCAPE, SK_ESC }, { SDL_SCANCODE_TAB, SK_TAB }, { SDL_SCANCODE_CAPSLOCK, SK_CAPS },
    { SDL_SCANCODE_BACKSPACE, SK_DELETE }, { SDL_SCANCODE_DELETE, SK_DELETE },
    { SDL_SCANCODE_HOME, SK_EDIT }, { SDL_SCANCODE_END, SK_INV },
    { SDL_SCANCODE_MINUS, SK_MINUS }, { SDL_SCANCODE_KP_MINUS, SK_MINUS }, { SDL_SCANCODE_EQUALS, SK_EQUALS },
    { SDL_SCANCODE_KP_PLUS, SK_PLUS }, { SDL_SCANCODE_RIGHTBRACKET, SK_PLUS },
    { SDL_SCANCODE_APOSTROPHE, SK_QUOTE }, { SDL_SCANCODE_SEMICOLON, SK_SEMICOLON },
    { SDL_SCANCODE_SLASH, SK_COLON }, { SDL_SCANCODE_COMMA, SK_COMMA }, { SDL_SCANCODE_PERIOD, SK_PERIOD },
    { SDL_SCANCODE_UP, SK_UP }, { SDL_SCANCODE_DOWN, SK_DOWN }, { SDL_SCANCODE_LEFT, SK_LEFT },
    { SDL_SCANCODE_RIGHT, SK_RIGHT },
};

static void set_title(void)
{
    char t[300];
    snprintf(t, sizeof t, "SAM Coupe - %s%s%s%s  (F11 menu, F12 turbo)",
             paths[0][0] ? comp_basename(paths[0]) : "no disk",
             samcoupe_tape_inserted() ? (samcoupe_tape_playing() ? " [tape playing]" : " [tape]") : "",
             samcoupe_disk_busy() ? " [disk]" : "", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

// What the SAM wrote on an MGT disk, back to its file
static void save_disk(int drive)
{
#ifndef __EMSCRIPTEN__
    if (!paths[drive][0] || !samcoupe_disk_dirty(drive)) return;
    uint8_t *img = (uint8_t *)malloc(819200);
    if (img && samcoupe_disk_image(drive, img)) {
        FILE *f = fopen(paths[drive], "r+b");
        if (f) {
            if (fwrite(img, 1, 819200, f) != 819200) fprintf(stderr, "could not save %s\n", paths[drive]);
            fclose(f);
        }
    }
    free(img);
#else
    (void)drive;
#endif
}

static void apply_path(const char *path, int drive)
{
    if (!path || !path[0]) return;
    const bool tape = samcoupe_is_tape(path);
    if (!tape) save_disk(drive);
    const char *err = samcoupe_load_file(path, drive);
    char msg[200];
    if (!err) {
        if (!tape) snprintf(paths[drive], sizeof paths[drive], "%s", path);
        if (tape) snprintf(msg, sizeof msg, "Tape: %s (play it from the menu)", comp_basename(path));
        else snprintf(msg, sizeof msg, "Drive %d: %s", drive + 1, comp_basename(path));
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
    } else {
        snprintf(msg, sizeof msg, "Load failed: %s", err);
    }
    comp_note(&mm, msg);
    set_title();
}

static void menu_activate(void)
{
    if (mm.sel == 0 || mm.sel == 1) {
        char *p = comp_pick_path(&mm);
        if (p) { apply_path(p, mm.sel); free(p); }
    } else if (mm.sel == 2) {
        comp_note(&mm, samcoupe_boot() ? "BOOT" : "No disk in drive 1");
    } else if (mm.sel == 3) {
        samcoupe_tape_play(!samcoupe_tape_playing());
        comp_note(&mm, !samcoupe_tape_inserted() ? "No tape" : samcoupe_tape_playing() ? "Tape playing" : "Tape stopped");
    } else if (mm.sel == 4) {
        for (int d = 0; d < 2; d++) { save_disk(d); samcoupe_eject_disk(d); paths[d][0] = 0; }
        samcoupe_eject_tape();
        mm.now[0] = 0;
        comp_note(&mm, "Disks and tape out");
    } else if (mm.sel == 5) {
        samcoupe_reset();
        comp_note(&mm, "Reset");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!samcoupe_rom_load()) sdl_port_rom_error();
    if (!samcoupe_rom || !samcoupe_init(samcoupe_rom)) { fprintf(stderr, "out of memory\n"); return 1; }
    mm = (comp_menu){ frame, SAM_W, SAM_H * 2, "SAM COUPE  F11", SAM_MEDIA_LABEL,
                      "*.mgt *.MGT *.dsk *.DSK *.sad *.sdf *.img *.tap *.TAP *.tzx *.TZX",
                      { "Disk (drive 1) or tape", "Disk in drive 2", "BOOT drive 1", "Play / stop the tape",
                        "Take disks and tape out", "Reset" },
                      0, 6, false, "", "" };
    int drive = 0;
    for (int i = 1; i < argc; i++) {
        apply_path(argv[i], drive);
        if (!samcoupe_is_tape(argv[i]) && drive == 0) drive = 1;
    }
    if (samcoupe_disk_inserted(0)) samcoupe_boot();
    sdl_port_cfg cfg = { "SAM Coupe", SAM_W, SAM_H * 2, 1, SAM_AUDIO_RATE, samcoupe_fps(), true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("SAM disks (.mgt .dsk .sdf) or tapes (.tap .tzx)", ".mgt,.dsk,.sad,.sdf,.img,.tap,.tzx");
    // SAM_BOOT=n: BOOT at frame n (tests)
    const char *boot_env = getenv("SAM_BOOT");
    const long boot_at = boot_env ? atol(boot_env) : -1;
    long frame_no = 0;

    bool busy = false, playing = false;
    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            apply_path(drop, target_drive);
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
                continue;
            }
            if (k == SDL_SCANCODE_F11) { mm.open = true; mm.sel = 0; mm.msg[0] = 0; }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[SK_COUNT] = { false };
        if (!mm.open)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
        for (int c = 0; c < SK_COUNT; c++) samcoupe_key(c, held[c]);
        uint8_t joy = 0;
        const uint32_t pad = sdl_port_keys();
        if (sdl_port_key(SDL_SCANCODE_KP_8) || (pad & SP_UP)) joy |= SAM_JOY_UP;
        if (sdl_port_key(SDL_SCANCODE_KP_2) || (pad & SP_DOWN)) joy |= SAM_JOY_DOWN;
        if (sdl_port_key(SDL_SCANCODE_KP_4) || (pad & SP_LEFT)) joy |= SAM_JOY_LEFT;
        if (sdl_port_key(SDL_SCANCODE_KP_6) || (pad & SP_RIGHT)) joy |= SAM_JOY_RIGHT;
        if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || (pad & (SP_B1 | SP_B2)))
            joy |= SAM_JOY_FIRE;
        samcoupe_joy(0, mm.open ? 0 : joy);
        if (frame_no++ == boot_at) samcoupe_boot();

        const int16_t *audio;
        const int n = samcoupe_run_frame(&audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < SAM_H; y++) {
            uint32_t *row = frame + (size_t)y * 2 * SAM_W;
            samcoupe_render_row(y, row);
            memcpy(row + SAM_W, row, SAM_W * sizeof *row);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
        if (samcoupe_disk_busy() != busy || samcoupe_tape_playing() != playing) {
            busy = samcoupe_disk_busy();
            playing = samcoupe_tape_playing();
            set_title();
        }
    }
    save_disk(0);
    save_disk(1);
    sdl_port_close();
    return 0;
}
