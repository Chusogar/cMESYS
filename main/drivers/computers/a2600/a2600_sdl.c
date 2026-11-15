// a2600_sdl.c - Atari 2600 / VCS on desktop (SDL2) and web (Emscripten).
//
//   cursors: joystick 1    Z / Left Ctrl / Space: FIRE
//   W A S D: joystick 2    Q: its FIRE
//   F1 GAME RESET   F2 GAME SELECT
//   F5 colour / B&W   F6 left difficulty A/B   F7 right difficulty A/B
//   F9  media menu (.a26 .bin .rom)
//   F3  power cycle    F12 TURBO
// Drag-and-drop a file onto the window (web: Upload bar). The picture is
// shown 4x across and 2x down (640x384), near the TV's shape.
#include "a2600_core.h"
#include "../common/comp_sdl.h"

#define SX 4
#define SY 2
#define FW (A2600_W * SX)
#define FH (A2600_H * SY)

static uint32_t frame[FW * FH];
static a2600 *m;
static comp_menu mm;

static void set_title(void)
{
    char t[240];
    const uint8_t sw = a2600_switches(m);
    snprintf(t, sizeof t, "Atari 2600 - %s [%s]  %s L:%c R:%c  (F1 reset, F2 select, F9 media, F12 turbo)%s",
             mm.now[0] ? mm.now : "(no cartridge)", a2600_has_cart(m) ? a2600_mapper_name(m) : "-",
             (sw & A2600_SW_COLOUR) ? "colour" : "B&W", (sw & A2600_SW_DIFF_LEFT_A) ? 'A' : 'B',
             (sw & A2600_SW_DIFF_RIGHT_A) ? 'A' : 'B', sdl_port_turbo() ? " [TURBO]" : "");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (a2600_load_file(m, path)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", a2600_last_error(m));
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
        a2600_reset(m);
        comp_note(&mm, "Power cycle");
    } else if (mm.sel == 2) {
        a2600_eject(m);
        mm.now[0] = 0;
        comp_note(&mm, "Ejected");
    }
    mm.open = false;
    set_title();
}

static unsigned joy(int up, int down, int left, int right, int fire1, int fire2, int fire3)
{
    unsigned b = 0;
    if (sdl_port_key(up)) b |= A2600_UP;
    if (sdl_port_key(down)) b |= A2600_DOWN;
    if (sdl_port_key(left)) b |= A2600_LEFT;
    if (sdl_port_key(right)) b |= A2600_RIGHT;
    if (sdl_port_key(fire1) || (fire2 && sdl_port_key(fire2)) || (fire3 && sdl_port_key(fire3))) b |= A2600_FIRE;
    return b;
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    m = a2600_create(A2600_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    mm = (comp_menu){ frame, FW, FH, "ATARI 2600 MEDIA  F9", A2600_MEDIA_LABEL,
                      "*.a26 *.A26 *.bin *.BIN *.rom *.ROM",
#ifdef __EMSCRIPTEN__
                      { "Upload cartridge", "Power cycle", "Eject", "Close menu" },
#else
                      { "Open cartridge", "Power cycle", "Eject", "Close menu" },
#endif
                      0, 4, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { "Atari 2600", FW, FH, 1, A2600_AUDIO_RATE, A2600_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("2600 cartridges (.a26 .bin .rom)", ".a26,.bin,.rom");

    uint32_t row[A2600_W];
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
            uint8_t sw = a2600_switches(m);
            if (k == SDL_SCANCODE_F9) { mm.open = true; mm.sel = 0; mm.msg[0] = 0; }
            if (k == SDL_SCANCODE_F3) { a2600_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
            if (k == SDL_SCANCODE_F5) { a2600_set_switches(m, sw ^ A2600_SW_COLOUR); set_title(); }
            if (k == SDL_SCANCODE_F6) { a2600_set_switches(m, sw ^ A2600_SW_DIFF_LEFT_A); set_title(); }
            if (k == SDL_SCANCODE_F7) { a2600_set_switches(m, sw ^ A2600_SW_DIFF_RIGHT_A); set_title(); }
        }
        unsigned b = 0;
        if (!mm.open) {
            b = joy(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                    SDL_SCANCODE_Z, SDL_SCANCODE_LCTRL, SDL_SCANCODE_SPACE);
            b |= joy(SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D,
                     SDL_SCANCODE_Q, 0, 0) << A2600_P2;
            if (sdl_port_key(SDL_SCANCODE_F1)) b |= A2600_RESET;
            if (sdl_port_key(SDL_SCANCODE_F2)) b |= A2600_SELECT;
        }
        a2600_input(m, b);

        const int16_t *audio;
        a2600_set_turbo(m, sdl_port_turbo());
        int n = a2600_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < A2600_H; y++) {
            a2600_render_row(m, y, row);
            uint32_t *d = frame + y * SY * FW;
            for (int x = 0; x < A2600_W; x++)
                for (int i = 0; i < SX; i++) d[SX * x + i] = row[x];
            for (int j = 1; j < SY; j++) memcpy(d + j * FW, d, FW * sizeof *d);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    a2600_destroy(m);
    return 0;
}
