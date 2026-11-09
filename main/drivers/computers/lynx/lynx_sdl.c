// lynx_sdl.c - Atari Lynx on desktop (SDL2) and web (Emscripten).
//
//   cursors: the pad    Z / Left Ctrl: A    X / Left Alt: B
//   1: OPTION 1    2: OPTION 2    Enter / P: PAUSE
//   F9  media menu (.lnx .lyx .o, or a 512-byte boot ROM)
//   F3  reset    F12 TURBO
// Drag-and-drop a file onto the window (web: Upload bar). The picture is
// shown doubled (320x204) so the menu fits over it.
#include "lynx_core.h"
#include "rom_data.h"
#include "../common/comp_sdl.h"

#define FW (LYNX_W * 2)
#define FH (LYNX_H * 2)

static uint32_t frame[FW * FH];
static lynx *m;
static comp_menu mm;

static void set_title(void)
{
    char t[200];
    snprintf(t, sizeof t, "Atari Lynx - %s  (F9 media, F3 reset, F12 turbo)%s%s",
             mm.now[0] ? mm.now : "(no cartridge)", sdl_port_turbo() ? " [TURBO]" : "",
             lynx_has_boot_rom(m) ? "" : " - built-in loader");
    sdl_port_set_title(t);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (lynx_load_file(m, path)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", lynx_last_error(m));
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
        lynx_reset(m);
        comp_note(&mm, "Reset");
    }
    mm.open = false;
    set_title();
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    m = lynx_create(LYNX_AUDIO_RATE);
    if (!m) { fprintf(stderr, "out of memory\n"); return 1; }
    if (lynx_rom_boot) lynx_set_boot_rom(m, lynx_rom_boot);
    else fprintf(stderr, "No Lynx boot ROM built in (roms/lynx/lynxboot.img): using a small loader\n");
    mm = (comp_menu){ frame, FW, FH, "LYNX MEDIA  F9", LYNX_MEDIA_LABEL,
                      "*.lnx *.LNX *.lyx *.LYX *.o *.O *.img *.bin",
#ifdef __EMSCRIPTEN__
                      { "Upload cartridge", "Reset", "Close menu" },
#else
                      { "Open cartridge", "Reset", "Close menu" },
#endif
                      0, 3, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { "Atari Lynx", FW, FH, 2, LYNX_AUDIO_RATE, LYNX_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    comp_web_setup("Lynx cartridges (.lnx .lyx .o)", ".lnx,.lyx,.o");

    uint32_t row[LYNX_W];
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
            if (k == SDL_SCANCODE_F3) { lynx_reset(m); set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        unsigned b = 0;
        if (!mm.open) {
            if (sdl_port_key(SDL_SCANCODE_UP)) b |= LYNX_UP;
            if (sdl_port_key(SDL_SCANCODE_DOWN)) b |= LYNX_DOWN;
            if (sdl_port_key(SDL_SCANCODE_LEFT)) b |= LYNX_LEFT;
            if (sdl_port_key(SDL_SCANCODE_RIGHT)) b |= LYNX_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_Z) || sdl_port_key(SDL_SCANCODE_LCTRL)) b |= LYNX_A;
            if (sdl_port_key(SDL_SCANCODE_X) || sdl_port_key(SDL_SCANCODE_LALT)) b |= LYNX_B;
            if (sdl_port_key(SDL_SCANCODE_1)) b |= LYNX_OPT1;
            if (sdl_port_key(SDL_SCANCODE_2)) b |= LYNX_OPT2;
            if (sdl_port_key(SDL_SCANCODE_RETURN) || sdl_port_key(SDL_SCANCODE_P)) b |= LYNX_PAUSE;
        }
        lynx_buttons(m, b);

        const int16_t *audio;
        lynx_set_turbo(m, sdl_port_turbo());
        int n = lynx_run_frame(m, &audio);
        sdl_port_audio(audio, n);
        for (int y = 0; y < LYNX_H; y++) {
            lynx_render_row(m, y, row);
            uint32_t *d = frame + y * 2 * FW;
            for (int x = 0; x < LYNX_W; x++) d[2 * x] = d[2 * x + 1] = row[x];
            memcpy(d + FW, d, FW * sizeof *d);
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    lynx_destroy(m);
    return 0;
}
