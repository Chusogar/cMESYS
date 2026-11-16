// cons_sdl.c - any console of main/drivers/consoles on desktop (SDL2: Linux,
// Windows) and web (Emscripten): the frontend all of them share (cons.h).
//
//   cons_<id> [cartridge]          (a .zip with the cartridge in it is fine)
//
//   Player 1: cursors, Z / Left Ctrl = button 1, X / Left Alt = button 2,
//             C = button 3, V = button 4, Enter = START, Backspace / Right
//             Shift = SELECT, 5 = COIN
//   Player 2: keypad 8 4 6 2, keypad 0 / . / + / - the buttons, keypad
//             Enter = START
//   The keys also go to the consoles with a keypad or keys (ColecoVision:
//   0-9, Q = *, W = #; PV-2000: its keyboard, buttons on Ctrl / Alt only).
//   F9 media menu, F3 reset, F12 TURBO. Drag-and-drop a file onto the window
//   (web: the Upload bar).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cons.h"
#include "../../computers/common/comp_sdl.h"

static uint32_t *frame;
static int FW, FH, FS;                  // the window's picture and its scale over the console's
static uint32_t pal565[65536];
static comp_menu mm;
static bool held_keys[CK_COUNT];

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_A, CK_A }, { SDL_SCANCODE_B, CK_B }, { SDL_SCANCODE_C, CK_C }, { SDL_SCANCODE_D, CK_D },
    { SDL_SCANCODE_E, CK_E }, { SDL_SCANCODE_F, CK_F }, { SDL_SCANCODE_G, CK_G }, { SDL_SCANCODE_H, CK_H },
    { SDL_SCANCODE_I, CK_I }, { SDL_SCANCODE_J, CK_J }, { SDL_SCANCODE_K, CK_K }, { SDL_SCANCODE_L, CK_L },
    { SDL_SCANCODE_M, CK_M }, { SDL_SCANCODE_N, CK_N }, { SDL_SCANCODE_O, CK_O }, { SDL_SCANCODE_P, CK_P },
    { SDL_SCANCODE_Q, CK_Q }, { SDL_SCANCODE_R, CK_R }, { SDL_SCANCODE_S, CK_S }, { SDL_SCANCODE_T, CK_T },
    { SDL_SCANCODE_U, CK_U }, { SDL_SCANCODE_V, CK_V }, { SDL_SCANCODE_W, CK_W }, { SDL_SCANCODE_X, CK_X },
    { SDL_SCANCODE_Y, CK_Y }, { SDL_SCANCODE_Z, CK_Z },
    { SDL_SCANCODE_0, CK_0 }, { SDL_SCANCODE_1, CK_1 }, { SDL_SCANCODE_2, CK_2 }, { SDL_SCANCODE_3, CK_3 },
    { SDL_SCANCODE_4, CK_4 }, { SDL_SCANCODE_5, CK_5 }, { SDL_SCANCODE_6, CK_6 }, { SDL_SCANCODE_7, CK_7 },
    { SDL_SCANCODE_8, CK_8 }, { SDL_SCANCODE_9, CK_9 },
    { SDL_SCANCODE_RETURN, CK_ENTER }, { SDL_SCANCODE_SPACE, CK_SPACE }, { SDL_SCANCODE_LSHIFT, CK_LSHIFT },
    { SDL_SCANCODE_RSHIFT, CK_RSHIFT }, { SDL_SCANCODE_LCTRL, CK_LCTRL }, { SDL_SCANCODE_RCTRL, CK_RCTRL },
    { SDL_SCANCODE_BACKSPACE, CK_BACKSPACE }, { SDL_SCANCODE_UP, CK_UP }, { SDL_SCANCODE_DOWN, CK_DOWN },
    { SDL_SCANCODE_LEFT, CK_LEFT }, { SDL_SCANCODE_RIGHT, CK_RIGHT }, { SDL_SCANCODE_COMMA, CK_COMMA },
    { SDL_SCANCODE_PERIOD, CK_PERIOD }, { SDL_SCANCODE_SEMICOLON, CK_SEMICOLON },
    { SDL_SCANCODE_APOSTROPHE, CK_QUOTE }, { SDL_SCANCODE_SLASH, CK_SLASH }, { SDL_SCANCODE_MINUS, CK_MINUS },
    { SDL_SCANCODE_ESCAPE, CK_ESCAPE }, { SDL_SCANCODE_TAB, CK_TAB }, { SDL_SCANCODE_CAPSLOCK, CK_CAPSLOCK },
    { SDL_SCANCODE_HOME, CK_HOME }, { SDL_SCANCODE_EQUALS, CK_EQUALS }, { SDL_SCANCODE_KP_PLUS, CK_PLUS },
    { SDL_SCANCODE_KP_MULTIPLY, CK_ASTERISK }, { SDL_SCANCODE_LEFTBRACKET, CK_AT },
    { SDL_SCANCODE_F1, CK_F1 }, { SDL_SCANCODE_F2, CK_F2 }, { SDL_SCANCODE_F4, CK_F4 },
    { SDL_SCANCODE_F5, CK_F5 }, { SDL_SCANCODE_F6, CK_F6 }, { SDL_SCANCODE_F7, CK_F7 }, { SDL_SCANCODE_F8, CK_F8 },
    { SDL_SCANCODE_F10, CK_F10 }, { SDL_SCANCODE_F11, CK_F11 },
    { SDL_SCANCODE_BACKSLASH, CK_BACKSLASH }, { SDL_SCANCODE_GRAVE, CK_BACKQUOTE },
    { SDL_SCANCODE_DELETE, CK_DELETE }, { SDL_SCANCODE_LGUI, CK_LGUI }, { SDL_SCANCODE_RGUI, CK_RGUI },
    { SDL_SCANCODE_RALT, CK_RALT },
};

static void set_title(void)
{
    char t[256];
    const char *st = cons_status();
    snprintf(t, sizeof t, "%s - %s  (F9 media, F3 reset, F12 turbo)%s%s%s", cons_machine.title,
             mm.now[0] ? mm.now : "(nothing loaded)", sdl_port_turbo() ? " [TURBO]" : "",
             st ? " - " : "", st ? st : "");
    sdl_port_set_title(t);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return b;
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    const char *why = "cannot read the file";
    char inner[96] = "";
    if (data) {
        size_t un = 0;
        uint8_t *z = cons_unzip(data, size, cons_machine.exts, &un, inner, sizeof inner);
        if (z) {
            free(data);
            data = z;
            size = un;
        }
    }
    if (data && cons_load(data, size, inner[0] ? inner : comp_basename(path), &why)) {
        snprintf(mm.now, sizeof mm.now, "%s", comp_basename(path));
        comp_note(&mm, mm.now);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Load failed: %s", why ? why : "?");
        comp_note(&mm, msg);
        fprintf(stderr, "%s: %s\n", path, why ? why : "?");
    }
    set_title();
}

static void menu_activate(void)
{
    if (mm.sel == 0) {
        char *p = comp_pick_path(&mm);
        if (p) { apply_path(p); free(p); }
    } else if (mm.sel == 1) {
        cons_reset();
        comp_note(&mm, "Reset");
    } else if (mm.sel == 2) {
        cons_eject();
        mm.now[0] = 0;
        comp_note(&mm, "Ejected");
    }
    mm.open = false;
    set_title();
}

// Tests: CONS_INPUT="frame:p1bits[/p2bits],..." holds the pads from that frame
static const char *script;
static unsigned script_pad[2];

static void run_script(long f)
{
    while (script && *script) {
        char *end;
        long at = strtol(script, &end, 10);
        if (end == script || *end != ':' || at > f) return;
        script_pad[0] = (unsigned)strtoul(end + 1, &end, 0);
        if (*end == '/') script_pad[1] = (unsigned)strtoul(end + 1, &end, 0);
        script = *end == ',' ? end + 1 : end;
    }
}

int main(int argc, char **argv)
{
    sdl_port_args(&argc, argv);
    if (!cons_init()) {
        fprintf(stderr, "%s: out of memory\n", cons_machine.title);
        return 1;
    }
    FS = cons_machine.max_w <= 200 ? 2 : 1;
    FW = cons_machine.max_w * FS;
    FH = cons_machine.max_h * FS;
    if (FW < 256) FW = 256;
    if (FH < 192) FH = 192;
    frame = (uint32_t *)calloc((size_t)FW * FH, sizeof *frame);
    for (int i = 0; i < 65536; i++) {
        const int r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
        pal565[i] = (uint32_t)(((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2)));
    }
    static char menu_title[48], filter[160];
    snprintf(menu_title, sizeof menu_title, "%s  F9", cons_machine.title);
    filter[0] = 0;
    for (int i = 0; cons_machine.exts[i]; i++) {
        char e[24], u[24];
        snprintf(e, sizeof e, "*%s ", cons_machine.exts[i]);
        snprintf(u, sizeof u, "%s", e);
        for (char *c = u; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        strncat(filter, e, sizeof filter - strlen(filter) - 1);
        strncat(filter, u, sizeof filter - strlen(filter) - 1);
    }
    strncat(filter, "*.zip *.ZIP", sizeof filter - strlen(filter) - 1);
    mm = (comp_menu){ frame, FW, FH, menu_title, cons_machine.media_label, filter,
#ifdef __EMSCRIPTEN__
                      { "Upload media", "Reset", "Eject", "Close menu" },
#else
                      { "Open media", "Reset", "Eject", "Close menu" },
#endif
                      0, 4, false, "", "" };
    if (argc > 1) apply_path(argv[1]);

    int scale = 1;
    while ((scale + 1) * FW <= 1100 && (scale + 1) * FH <= 860) scale++;
    sdl_port_cfg cfg = { cons_machine.title, FW, FH, scale, cons_machine.audio_rate, cons_machine.fps, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();
    static char accept[160];
    accept[0] = 0;
    for (int i = 0; cons_machine.exts[i]; i++) {
        strncat(accept, cons_machine.exts[i], sizeof accept - strlen(accept) - 2);
        strncat(accept, ",", sizeof accept - strlen(accept) - 1);
    }
    strncat(accept, ".zip", sizeof accept - strlen(accept) - 1);
    comp_web_setup(cons_machine.title, accept);
    script = getenv("CONS_INPUT");
    long frame_no = 0;
    const bool keyboard = cons_machine.keys == CONS_KEYBOARD;

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
            if (k == SDL_SCANCODE_F3) { cons_reset(); set_title(); }
            if (k == SDL_SCANCODE_F12) { sdl_port_set_turbo(!sdl_port_turbo()); set_title(); }
        }
        unsigned p1 = 0, p2 = 0;
        bool keys[CK_COUNT] = { false };
        if (!mm.open) {
            if (sdl_port_key(SDL_SCANCODE_UP)) p1 |= CP_UP;
            if (sdl_port_key(SDL_SCANCODE_DOWN)) p1 |= CP_DOWN;
            if (sdl_port_key(SDL_SCANCODE_LEFT)) p1 |= CP_LEFT;
            if (sdl_port_key(SDL_SCANCODE_RIGHT)) p1 |= CP_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_LCTRL) || (!keyboard && sdl_port_key(SDL_SCANCODE_Z))) p1 |= CP_B1;
            if (sdl_port_key(SDL_SCANCODE_LALT) || (!keyboard && sdl_port_key(SDL_SCANCODE_X))) p1 |= CP_B2;
            if (!keyboard && sdl_port_key(SDL_SCANCODE_C)) p1 |= CP_B3;
            if (!keyboard && sdl_port_key(SDL_SCANCODE_V)) p1 |= CP_B4;
            if (!keyboard && sdl_port_key(SDL_SCANCODE_RETURN)) p1 |= CP_START;
            if (!keyboard && (sdl_port_key(SDL_SCANCODE_BACKSPACE) || sdl_port_key(SDL_SCANCODE_RSHIFT)))
                p1 |= CP_SELECT;
            if (!keyboard && sdl_port_key(SDL_SCANCODE_5)) p1 |= CP_COIN;
            if (sdl_port_key(SDL_SCANCODE_KP_8)) p2 |= CP_UP;
            if (sdl_port_key(SDL_SCANCODE_KP_2)) p2 |= CP_DOWN;
            if (sdl_port_key(SDL_SCANCODE_KP_4)) p2 |= CP_LEFT;
            if (sdl_port_key(SDL_SCANCODE_KP_6)) p2 |= CP_RIGHT;
            if (sdl_port_key(SDL_SCANCODE_KP_0)) p2 |= CP_B1;
            if (sdl_port_key(SDL_SCANCODE_KP_PERIOD)) p2 |= CP_B2;
            if (sdl_port_key(SDL_SCANCODE_KP_PLUS)) p2 |= CP_B3;
            if (sdl_port_key(SDL_SCANCODE_KP_MINUS)) p2 |= CP_B4;
            if (sdl_port_key(SDL_SCANCODE_KP_ENTER)) p2 |= CP_START;
            if (cons_machine.keys != CONS_NO_KEYS)
                for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                    if (sdl_port_key(keymap[i].sc)) keys[keymap[i].key] = true;
        }
        for (int c = 0; c < CK_COUNT; c++)
            if (keys[c] != held_keys[c]) {
                held_keys[c] = keys[c];
                cons_key(c, keys[c]);
            }
        run_script(frame_no++);
        cons_pad(0, p1 | script_pad[0]);
        cons_pad(1, p2 | script_pad[1]);

        const int16_t *audio;
        int n = cons_run_frame(&audio);
        if (!sdl_port_turbo()) sdl_port_audio(audio, n);
        int w, h, stride;
        const uint16_t *fb = cons_frame(&w, &h, &stride);
        // the console's picture, scaled by FS, in the middle of the window's
        const int ox = (FW - w * FS) / 2 > 0 ? (FW - w * FS) / 2 : 0;
        const int oy = (FH - h * FS) / 2 > 0 ? (FH - h * FS) / 2 : 0;
        if (w * FS != FW || h * FS != FH) memset(frame, 0, (size_t)FW * FH * sizeof *frame);
        for (int y = 0; y < h && oy + y * FS < FH; y++) {
            uint32_t *d = frame + (oy + y * FS) * FW + ox;
            const uint16_t *s = fb + y * stride;
            const int ww = ox + w * FS <= FW ? w : (FW - ox) / FS;
            if (FS == 1) {
                for (int x = 0; x < ww; x++) d[x] = pal565[s[x]];
            } else {
                for (int x = 0; x < ww; x++) d[2 * x] = d[2 * x + 1] = pal565[s[x]];
                memcpy(d + FW, d, (size_t)ww * 2 * sizeof *d);
            }
        }
        if (mm.open) comp_draw_menu(&mm);
        sdl_port_present(frame);
    }
    sdl_port_close();
    return 0;
}
