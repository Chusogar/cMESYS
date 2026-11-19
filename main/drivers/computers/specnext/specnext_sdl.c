// specnext_sdl.c - the ZX Spectrum Next on desktop (SDL2) and web (Emscripten).
//
// It boots as the real one: the boot ROM (roms/specnext: MAME's tbblue set)
// loads TBBLUE.FW from the SD card image, the firmware NextZXOS. The first
// boot of a new card asks for the video mode (ENTER, then Y).
//
//   F9  media menu: an SD card image (.img .mmc .hdf: written in place, as a
//       real card) or a .nex program (loaded once NextZXOS is up)
//   F7  NMI button (the NextZXOS NMI menu)   F8  DRIVE button (DivMMC NMI)
//   F10 RESET button (soft reset)            F11 power off / on
//   F12 TURBO
// The PC keyboard: Shift = CAPS SHIFT, Ctrl = SYMBOL SHIFT, and the Next's
// extra keys: Backspace DELETE, ` EDIT, Esc BREAK, Tab EXTEND MODE, Caps Lock
// CAPS LOCK, Insert GRAPH, Home TRUE VIDEO, End INV VIDEO, the cursors,
// ; " , . (and - = + / * as SYMBOL SHIFT combinations). A gamepad or the
// keypad (8 4 6 2, 0 / 5 fire) is the joystick (port 1). Drag-and-drop works.
#include <stdlib.h>
#include "specnext_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

static uint32_t frame[SN_W * SN_H * 2];
static comp_menu mm;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, NK_A }, { SDL_SCANCODE_B, NK_B }, { SDL_SCANCODE_C, NK_C }, { SDL_SCANCODE_D, NK_D },
    { SDL_SCANCODE_E, NK_E }, { SDL_SCANCODE_F, NK_F }, { SDL_SCANCODE_G, NK_G }, { SDL_SCANCODE_H, NK_H },
    { SDL_SCANCODE_I, NK_I }, { SDL_SCANCODE_J, NK_J }, { SDL_SCANCODE_K, NK_K }, { SDL_SCANCODE_L, NK_L },
    { SDL_SCANCODE_M, NK_M }, { SDL_SCANCODE_N, NK_N }, { SDL_SCANCODE_O, NK_O }, { SDL_SCANCODE_P, NK_P },
    { SDL_SCANCODE_Q, NK_Q }, { SDL_SCANCODE_R, NK_R }, { SDL_SCANCODE_S, NK_S }, { SDL_SCANCODE_T, NK_T },
    { SDL_SCANCODE_U, NK_U }, { SDL_SCANCODE_V, NK_V }, { SDL_SCANCODE_W, NK_W }, { SDL_SCANCODE_X, NK_X },
    { SDL_SCANCODE_Y, NK_Y }, { SDL_SCANCODE_Z, NK_Z },
    { SDL_SCANCODE_0, NK_0 }, { SDL_SCANCODE_1, NK_1 }, { SDL_SCANCODE_2, NK_2 }, { SDL_SCANCODE_3, NK_3 },
    { SDL_SCANCODE_4, NK_4 }, { SDL_SCANCODE_5, NK_5 }, { SDL_SCANCODE_6, NK_6 }, { SDL_SCANCODE_7, NK_7 },
    { SDL_SCANCODE_8, NK_8 }, { SDL_SCANCODE_9, NK_9 },
    { SDL_SCANCODE_RETURN, NK_ENTER }, { SDL_SCANCODE_KP_ENTER, NK_ENTER }, { SDL_SCANCODE_SPACE, NK_SPACE },
    { SDL_SCANCODE_LSHIFT, NK_CAPS_SHIFT }, { SDL_SCANCODE_RSHIFT, NK_CAPS_SHIFT },
    { SDL_SCANCODE_LCTRL, NK_SYM_SHIFT }, { SDL_SCANCODE_RCTRL, NK_SYM_SHIFT },
    { SDL_SCANCODE_SEMICOLON, NK_SEMICOLON }, { SDL_SCANCODE_APOSTROPHE, NK_QUOTE },
    { SDL_SCANCODE_COMMA, NK_COMMA }, { SDL_SCANCODE_PERIOD, NK_PERIOD },
    { SDL_SCANCODE_UP, NK_UP }, { SDL_SCANCODE_DOWN, NK_DOWN }, { SDL_SCANCODE_LEFT, NK_LEFT },
    { SDL_SCANCODE_RIGHT, NK_RIGHT }, { SDL_SCANCODE_BACKSPACE, NK_DELETE }, { SDL_SCANCODE_DELETE, NK_DELETE },
    { SDL_SCANCODE_GRAVE, NK_EDIT }, { SDL_SCANCODE_ESCAPE, NK_BREAK }, { SDL_SCANCODE_TAB, NK_EXTEND },
    { SDL_SCANCODE_CAPSLOCK, NK_CAPS_LOCK }, { SDL_SCANCODE_INSERT, NK_GRAPH },
    { SDL_SCANCODE_HOME, NK_TRUE_VIDEO }, { SDL_SCANCODE_END, NK_INV_VIDEO },
    { SDL_SCANCODE_MINUS, NK_MINUS }, { SDL_SCANCODE_KP_MINUS, NK_MINUS }, { SDL_SCANCODE_EQUALS, NK_EQUALS },
    { SDL_SCANCODE_KP_PLUS, NK_PLUS }, { SDL_SCANCODE_SLASH, NK_SLASH }, { SDL_SCANCODE_KP_DIVIDE, NK_SLASH },
    { SDL_SCANCODE_KP_MULTIPLY, NK_ASTERISK },
};

static void set_title(void)
{
    char t[240];
    snprintf(t, sizeof t, "ZX Spectrum Next - %s%s%s  (F9 media, F7 NMI, F10 reset, F11 power, F12 turbo)",
             mm.now[0] ? mm.now : (specnext_sd_inserted() ? "SD card" : "no SD card"),
             specnext_sd_busy() ? " [SD]" : "", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    const char *err = specnext_load_file(path);
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
        specnext_eject_sd();
        mm.now[0] = 0;
        comp_note(&mm, "SD card out");
    } else if (mm.sel == 2) {
        specnext_hard_reset();
        comp_note(&mm, "Power off and on");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!specnext_rom_load()) sdl_port_rom_error();
    if (!specnext_init(specnext_rom_boot)) { fprintf(stderr, "out of memory\n"); return 1; }
    mm = (comp_menu){ frame, SN_W, SN_H * 2, "ZX SPECTRUM NEXT  F9", SN_MEDIA_LABEL,
                      "*.img *.IMG *.mmc *.hdf *.sd *.nex *.NEX",
#ifdef __EMSCRIPTEN__
                      { "Upload / choose an SD image or .nex", "Take the SD card out", "Power off and on", "Close menu" },
#else
                      { "Open an SD card image or a .nex", "Take the SD card out", "Power off and on", "Close menu" },
#endif
                      0, 4, false, "", "" };
    for (int i = 1; i < argc; i++) apply_path(argv[i]);

    sdl_port_cfg cfg = { "ZX Spectrum Next", SN_W, SN_H * 2, 1, SN_AUDIO_RATE, specnext_fps(), true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("Spectrum Next SD card images (.img .mmc) or .nex", ".img,.mmc,.hdf,.sd,.nex");

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
            if (k == SDL_SCANCODE_F10) specnext_soft_reset();
            if (k == SDL_SCANCODE_F11) specnext_hard_reset();
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        bool held[NK_COUNT] = { false };
        if (!mm.open)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) held[keymap[i].key] = true;
        for (int c = 0; c < NK_COUNT; c++) specnext_key(c, held[c]);
        specnext_nmi_buttons(!mm.open && sdl_port_key(SDL_SCANCODE_F7), !mm.open && sdl_port_key(SDL_SCANCODE_F8));
        uint8_t joy = 0;
        const uint32_t pad = sdl_port_keys();
        if (sdl_port_key(SDL_SCANCODE_KP_8) || (pad & SP_UP)) joy |= SN_JOY_UP;
        if (sdl_port_key(SDL_SCANCODE_KP_2) || (pad & SP_DOWN)) joy |= SN_JOY_DOWN;
        if (sdl_port_key(SDL_SCANCODE_KP_4) || (pad & SP_LEFT)) joy |= SN_JOY_LEFT;
        if (sdl_port_key(SDL_SCANCODE_KP_6) || (pad & SP_RIGHT)) joy |= SN_JOY_RIGHT;
        if (sdl_port_key(SDL_SCANCODE_KP_0) || sdl_port_key(SDL_SCANCODE_KP_5) || (pad & SP_B1)) joy |= SN_JOY_FIRE;
        if (pad & SP_B2) joy |= SN_JOY_FIRE2;
        specnext_joy(0, mm.open ? 0 : joy);

        const int16_t *audio;
        specnext_set_turbo(sdl_port_turbo());
        const int n = specnext_run_frame(&audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < SN_H; y++) {
            uint32_t *row = frame + (size_t)y * 2 * SN_W;
            specnext_render_row(y, row);
            memcpy(row + SN_W, row, SN_W * sizeof *row);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
        if (specnext_sd_busy() != led) {
            led = specnext_sd_busy();
            set_title();
        }
    }
    sdl_port_close();
    return 0;
}
