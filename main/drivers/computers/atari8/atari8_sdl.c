// atari8_sdl.c - Atari 800XL on desktop (SDL2) and web (Emscripten): the
// ROMs built in (roms/atari800xl: the XL's OS and BASIC, or AltirraOS and
// Altirra BASIC).
//
//   the PC's keys are the Atari's (Right Ctrl CONTROL, Caps CAPS, F7 the
//   inverse key, Esc ESC, Backspace DELETE); cursors + Left Ctrl joystick 1
//   F2 START   F3 SELECT   F4 OPTION   F6 BREAK
//   F9  media menu (.atr disks: the machine boots them)
//   F10 cold start    F12 TURBO
// Drag-and-drop a file onto the window (web: Upload bar). The picture is
// shown doubled (768x480).
#include "atari8_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

#define FW (A8_W * 2)
#define FH (A8_H * 2)

static uint32_t frame[FW * FH];
static atari8 *m;
static comp_menu mm;

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, A8K_A }, { SDL_SCANCODE_B, A8K_B }, { SDL_SCANCODE_C, A8K_C }, { SDL_SCANCODE_D, A8K_D },
    { SDL_SCANCODE_E, A8K_E }, { SDL_SCANCODE_F, A8K_F }, { SDL_SCANCODE_G, A8K_G }, { SDL_SCANCODE_H, A8K_H },
    { SDL_SCANCODE_I, A8K_I }, { SDL_SCANCODE_J, A8K_J }, { SDL_SCANCODE_K, A8K_K }, { SDL_SCANCODE_L, A8K_L },
    { SDL_SCANCODE_M, A8K_M }, { SDL_SCANCODE_N, A8K_N }, { SDL_SCANCODE_O, A8K_O }, { SDL_SCANCODE_P, A8K_P },
    { SDL_SCANCODE_Q, A8K_Q }, { SDL_SCANCODE_R, A8K_R }, { SDL_SCANCODE_S, A8K_S }, { SDL_SCANCODE_T, A8K_T },
    { SDL_SCANCODE_U, A8K_U }, { SDL_SCANCODE_V, A8K_V }, { SDL_SCANCODE_W, A8K_W }, { SDL_SCANCODE_X, A8K_X },
    { SDL_SCANCODE_Y, A8K_Y }, { SDL_SCANCODE_Z, A8K_Z },
    { SDL_SCANCODE_0, A8K_0 }, { SDL_SCANCODE_1, A8K_1 }, { SDL_SCANCODE_2, A8K_2 }, { SDL_SCANCODE_3, A8K_3 },
    { SDL_SCANCODE_4, A8K_4 }, { SDL_SCANCODE_5, A8K_5 }, { SDL_SCANCODE_6, A8K_6 }, { SDL_SCANCODE_7, A8K_7 },
    { SDL_SCANCODE_8, A8K_8 }, { SDL_SCANCODE_9, A8K_9 },
    { SDL_SCANCODE_RETURN, A8K_RETURN }, { SDL_SCANCODE_KP_ENTER, A8K_RETURN }, { SDL_SCANCODE_SPACE, A8K_SPACE },
    { SDL_SCANCODE_SEMICOLON, A8K_SEMICOLON }, { SDL_SCANCODE_EQUALS, A8K_EQUALS }, { SDL_SCANCODE_MINUS, A8K_MINUS },
    { SDL_SCANCODE_COMMA, A8K_COMMA }, { SDL_SCANCODE_PERIOD, A8K_PERIOD }, { SDL_SCANCODE_SLASH, A8K_SLASH },
    { SDL_SCANCODE_KP_PLUS, A8K_PLUS }, { SDL_SCANCODE_LEFTBRACKET, A8K_PLUS }, { SDL_SCANCODE_KP_MULTIPLY, A8K_ASTERISK },
    { SDL_SCANCODE_RIGHTBRACKET, A8K_ASTERISK }, { SDL_SCANCODE_NONUSBACKSLASH, A8K_LESS },
    { SDL_SCANCODE_BACKSLASH, A8K_GREATER }, { SDL_SCANCODE_KP_MINUS, A8K_MINUS }, { SDL_SCANCODE_KP_DIVIDE, A8K_SLASH },
    { SDL_SCANCODE_ESCAPE, A8K_ESC }, { SDL_SCANCODE_TAB, A8K_TAB }, { SDL_SCANCODE_BACKSPACE, A8K_BACKSPACE },
    { SDL_SCANCODE_DELETE, A8K_BACKSPACE }, { SDL_SCANCODE_CAPSLOCK, A8K_CAPS }, { SDL_SCANCODE_F7, A8K_INVERSE },
};

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "%s - %s  (F2 START, F3 SELECT, F4 OPTION, F9 media, F10 reset, F12 turbo)%s",
             atari8_title(m), mm.now[0] ? mm.now : "(no disk)", sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (atari8_load_file(m, path)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", atari8_last_error(m));
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
        atari8_eject(m);
        mm.now[0] = 0;
        atari8_reset(m);
        comp_note(&mm, "Disk out: BASIC");
    } else if (mm.sel == 2) {
        atari8_reset(m);
        comp_note(&mm, "Cold start");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!atari8_rom_os) {
        fprintf(stderr, "No Atari OS ROM built in: put co61598b.rom (or AltirraOS as ATARIXL.ROM) and "
                        "BASIC in roms/atari800xl/ and run tools/regen.sh atari800xl\n");
        return 1;
    }
    m = atari8_create(A8_MODEL_XL, A8_AUDIO_RATE, atari8_rom_os, 0x4000, atari8_rom_basic);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    mm = (comp_menu){ frame, FW, FH, "ATARI 800XL MEDIA  F9", A8_MEDIA_LABEL, "*.atr *.ATR",
#ifdef __EMSCRIPTEN__
                      { "Upload disk (.atr)", "Eject disk", "Cold start", "Close menu" },
#else
                      { "Open disk (.atr)", "Eject disk", "Cold start", "Close menu" },
#endif
                      0, 4, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { "Atari 800XL", FW, FH, 1, A8_AUDIO_RATE, A8_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("Atari disks (.atr)", ".atr");

    uint32_t row[A8_W];
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
            if (k == SDL_SCANCODE_F10) { atari8_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F6) atari8_break(m);
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        int code = -1;
        unsigned joy = 0, con = 0;
        bool shift = false, ctrl = false;
        if (!mm.open) {
            // one key at a time, the first of the keyboard's order held (as dsp-cpp)
            for (int i = 0; code < 0 && atari8_key_order(i) >= 0; i++) {
                const int want = atari8_key_order(i);
                for (size_t j = 0; j < sizeof keymap / sizeof keymap[0]; j++)
                    if (keymap[j].key == want && sdl_port_key(keymap[j].sc)) { code = want; break; }
            }
            shift = sdl_port_key(SDL_SCANCODE_LSHIFT) || sdl_port_key(SDL_SCANCODE_RSHIFT);
            ctrl = sdl_port_key(SDL_SCANCODE_RCTRL);
            if (sdl_port_key(SDL_SCANCODE_UP)) joy |= A8_UP;
            if (sdl_port_key(SDL_SCANCODE_DOWN)) joy |= A8_DOWN;
            if (sdl_port_key(SDL_SCANCODE_LEFT)) joy |= A8_LEFT;
            if (sdl_port_key(SDL_SCANCODE_RIGHT)) joy |= A8_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_LCTRL) || sdl_port_key(SDL_SCANCODE_LALT)) joy |= A8_FIRE;
            if (sdl_port_key(SDL_SCANCODE_F2)) con |= A8_START;
            if (sdl_port_key(SDL_SCANCODE_F3)) con |= A8_SELECT;
            if (sdl_port_key(SDL_SCANCODE_F4)) con |= A8_OPTION;
        }
        atari8_set_key(m, code, shift, ctrl);
        atari8_joy(m, 0, joy);
        atari8_console(m, con);

        const int16_t *audio;
        atari8_set_turbo(m, sdl_port_turbo());
        int n = atari8_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < A8_H; y++) {
            atari8_render_row(m, y, row);
            uint32_t *d = frame + y * 2 * FW;
            for (int x = 0; x < A8_W; x++) d[2 * x] = d[2 * x + 1] = row[x];
            memcpy(d + FW, d, FW * sizeof *d);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    atari8_destroy(m);
    return 0;
}
