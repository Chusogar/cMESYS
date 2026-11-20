// exelv_sdl.c - the Exelvision EXL-100 / EXELTEL (EXELV_MODEL 0 / 1) on
// desktop (SDL2: Linux, Windows) and web (Emscripten).
//
//   exl100 [cartridge.bin | cassette.k7 | .wav | .zip]     (exeltel the same)
//
//   F9 menu (a cartridge or a cassette, play / stop the cassette, take them
//   out, reset)   F12 TURBO
// The keys are MAME's for the machine: the letters and digits, Enter, Space,
// Backspace, Esc, Tab, Home, the cursors; Shift = SHIFT, Ctrl = CTL, Left Alt =
// FCT, Caps Lock = LOCK; - = "# )", = "_ -", ' "+ =", ; "% @", , "? ,", .
// ". ;", / "/ :", [ "~ ^", ] "< [", \ "> ]", Right Alt "* \". As on the real
// one, SHIFT, CTL and FCT are pressed before the key, not held (a chord such
// as Shift+3 is sent that way). A gamepad is the arrows and SPACE.
// What a SAVE writes is added to <cassette>-save.k7 (or exelvision-save.k7).
#include <stdlib.h>
#include "exelv_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

#ifndef EXELV_MODEL
#define EXELV_MODEL 0
#endif

#define FW (EXELV_W * 2)
#define FH (EXELV_H * 2)

static uint32_t frame[FW * FH];
static comp_menu mm;
static char tape_path[512], save_path[600];

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, EK_A }, { SDL_SCANCODE_B, EK_B }, { SDL_SCANCODE_C, EK_C }, { SDL_SCANCODE_D, EK_D },
    { SDL_SCANCODE_E, EK_E }, { SDL_SCANCODE_F, EK_F }, { SDL_SCANCODE_G, EK_G }, { SDL_SCANCODE_H, EK_H },
    { SDL_SCANCODE_I, EK_I }, { SDL_SCANCODE_J, EK_J }, { SDL_SCANCODE_K, EK_K }, { SDL_SCANCODE_L, EK_L },
    { SDL_SCANCODE_M, EK_M }, { SDL_SCANCODE_N, EK_N }, { SDL_SCANCODE_O, EK_O }, { SDL_SCANCODE_P, EK_P },
    { SDL_SCANCODE_Q, EK_Q }, { SDL_SCANCODE_R, EK_R }, { SDL_SCANCODE_S, EK_S }, { SDL_SCANCODE_T, EK_T },
    { SDL_SCANCODE_U, EK_U }, { SDL_SCANCODE_V, EK_V }, { SDL_SCANCODE_W, EK_W }, { SDL_SCANCODE_X, EK_X },
    { SDL_SCANCODE_Y, EK_Y }, { SDL_SCANCODE_Z, EK_Z },
    { SDL_SCANCODE_0, EK_0 }, { SDL_SCANCODE_1, EK_1 }, { SDL_SCANCODE_2, EK_2 }, { SDL_SCANCODE_3, EK_3 },
    { SDL_SCANCODE_4, EK_4 }, { SDL_SCANCODE_5, EK_5 }, { SDL_SCANCODE_6, EK_6 }, { SDL_SCANCODE_7, EK_7 },
    { SDL_SCANCODE_8, EK_8 }, { SDL_SCANCODE_9, EK_9 },
    { SDL_SCANCODE_RETURN, EK_ENTER }, { SDL_SCANCODE_KP_ENTER, EK_ENTER }, { SDL_SCANCODE_SPACE, EK_SPACE },
    { SDL_SCANCODE_BACKSPACE, EK_BACKSPACE }, { SDL_SCANCODE_ESCAPE, EK_ESC }, { SDL_SCANCODE_TAB, EK_TAB },
    { SDL_SCANCODE_HOME, EK_HOME }, { SDL_SCANCODE_CAPSLOCK, EK_LOCK },
    { SDL_SCANCODE_LSHIFT, EK_SHIFT }, { SDL_SCANCODE_RSHIFT, EK_SHIFT },
    { SDL_SCANCODE_LCTRL, EK_CTL }, { SDL_SCANCODE_RCTRL, EK_CTL }, { SDL_SCANCODE_LALT, EK_FCT },
    { SDL_SCANCODE_RALT, EK_STAR },
    { SDL_SCANCODE_UP, EK_UP }, { SDL_SCANCODE_DOWN, EK_DOWN }, { SDL_SCANCODE_LEFT, EK_LEFT },
    { SDL_SCANCODE_RIGHT, EK_RIGHT },
    { SDL_SCANCODE_MINUS, EK_HASH }, { SDL_SCANCODE_EQUALS, EK_UNDERSCORE }, { SDL_SCANCODE_APOSTROPHE, EK_PLUS },
    { SDL_SCANCODE_SEMICOLON, EK_PERCENT }, { SDL_SCANCODE_COMMA, EK_QUESTION }, { SDL_SCANCODE_PERIOD, EK_PERIOD },
    { SDL_SCANCODE_SLASH, EK_SLASH }, { SDL_SCANCODE_LEFTBRACKET, EK_TILDE }, { SDL_SCANCODE_RIGHTBRACKET, EK_LESS },
    { SDL_SCANCODE_BACKSLASH, EK_GREATER },
};

static const char *machine_name(void) { return EXELV_MODEL ? "EXELTEL" : "EXL-100"; }

static void set_title(void)
{
    char t[300];
    snprintf(t, sizeof t, "Exelvision %s - %s%s%s  (F9 menu, F12 turbo)", machine_name(),
             mm.now[0] ? mm.now : (exelv_cart_inserted() ? "cartridge" : "no cartridge"),
             exelv_tape_inserted() ? (exelv_tape_playing() ? " [cassette]" : " [cassette stopped]") : "",
             sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

// What a SAVE wrote, added to the save file
static void flush_save(void)
{
    size_t n = 0;
    uint8_t *b = exelv_take_tape_save(&n);
    if (!b) return;
#ifndef __EMSCRIPTEN__
    const char *path = save_path[0] ? save_path : "exelvision-save.k7";
    FILE *f = fopen(path, "ab");
    if (f) {
        if (fwrite(b, 1, n, f) != n) fprintf(stderr, "could not write %s\n", path);
        fclose(f);
        char msg[128];
        snprintf(msg, sizeof msg, "SAVE: %.100s", comp_basename(path));
        comp_note(&mm, msg);
    }
#endif
    free(b);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    const char *err = exelv_load_file(path);
    char msg[128];
    if (!err) {
        if (exelv_tape_inserted() && exelv_is_tape(path)) {
            snprintf(tape_path, sizeof tape_path, "%s", path);
            snprintf(save_path, sizeof save_path, "%.*s-save.k7", (int)(strrchr(path, '.') - path), path);
        }
        snprintf(msg, sizeof msg, "%s: %.100s", exelv_is_tape(path) ? "Cassette" : "Cartridge", comp_basename(path));
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
    } else {
        snprintf(msg, sizeof msg, "Load failed: %.100s", err);
    }
    comp_note(&mm, msg);
    set_title();
}

static void menu_activate(void)
{
    if (mm.sel == 0) {
        char *p = comp_pick_path(&mm);
        if (p) { apply_path(p); free(p); }
    } else if (mm.sel == 1) {
        exelv_tape_play(!exelv_tape_playing());
        comp_note(&mm, !exelv_tape_inserted() ? "No cassette" : exelv_tape_playing() ? "Cassette playing" : "Cassette stopped");
    } else if (mm.sel == 2) {
        exelv_eject_cart();
        exelv_eject_tape();
        mm.now[0] = 0;
        exelv_reset();
        comp_note(&mm, "Cartridge and cassette out");
    } else if (mm.sel == 3) {
        exelv_reset();
        comp_note(&mm, "Reset");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!exelv_rom_load()) sdl_port_rom_error();
    if (!exelv_init(EXELV_MODEL, exelv_rom_main, exelv_rom_sub, exelv_rom_sys)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    mm = (comp_menu){ frame, FW, FH, EXELV_MODEL ? "EXELTEL  F9" : "EXL-100  F9", EXELV_MEDIA_LABEL,
                      "*.bin *.BIN *.rom *.ROM *.k7 *.K7 *.wav *.WAV *.zip *.ZIP",
                      { "Cartridge or cassette", "Play / stop the cassette", "Take them out", "Reset" },
                      0, 4, false, "", "" };
    for (int i = 1; i < argc; i++) apply_path(argv[i]);
    sdl_port_cfg cfg = { EXELV_MODEL ? "EXELTEL" : "EXL-100", FW, FH, 1, EXELV_AUDIO_RATE, exelv_fps(), true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("Exelvision cartridges (.bin .rom) or cassettes (.k7 .wav)", ".bin,.rom,.k7,.wav,.zip");

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
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[EK_COUNT] = { false };
        if (!mm.open)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
        for (int c = 0; c < EK_COUNT; c++) exelv_key(c, held[c]);
        uint8_t joy = 0;
        const uint32_t pad = mm.open ? 0 : sdl_port_keys();
        if (pad & SP_UP) joy |= EXELV_JOY_UP;
        if (pad & SP_DOWN) joy |= EXELV_JOY_DOWN;
        if (pad & SP_LEFT) joy |= EXELV_JOY_LEFT;
        if (pad & SP_RIGHT) joy |= EXELV_JOY_RIGHT;
        if (pad & (SP_B1 | SP_B2)) joy |= EXELV_JOY_FIRE;
        exelv_joy(joy);

        const int16_t *audio;
        const int n = exelv_run_frame(&audio);
        sdl_port_audio(audio, n);
        flush_save();
        static uint32_t row[EXELV_W];
        for (int y = 0; y < EXELV_H; y++) {
            exelv_render_row(y, row);
            uint32_t *o = frame + (size_t)y * 2 * FW;
            for (int x = 0; x < EXELV_W; x++) o[2 * x] = o[2 * x + 1] = row[x];
            memcpy(o + FW, o, FW * sizeof *o);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    flush_save();
    sdl_port_close();
    return 0;
}
