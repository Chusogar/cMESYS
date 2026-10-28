// zxspec_sdl.c - ZX Spectrum family on desktop (SDL2) and web (Emscripten).
// Build with -DZX_MODEL=ZX_SPEC48|ZX_SPEC128|ZX_PLUS3|ZX_PENTAGON|ZX_SCORPION
//
//   F9  media menu (open .tap/.tzx/.sna, play/stop tape)
//   F1  tape play/stop     F12 reset
// Drag-and-drop a .tap/.tzx/.sna onto the window (web: Upload bar).
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "sdl_port.h"
#include "zxspec_core.h"
#include "rom_data.h"
#include "../../../ports/esp32-cyd/font5x7.h"

#ifndef ZX_MODEL
#define ZX_MODEL ZX_SPEC48
#endif

static uint32_t frame[ZX_W * ZX_H];
static zxspec *cpu_m;
static char media_name[80];

static const struct { int sc; int key; } keymap[] = {
    { SDL_SCANCODE_LSHIFT, ZXK_CAPS }, { SDL_SCANCODE_Z, ZXK_Z }, { SDL_SCANCODE_X, ZXK_X },
    { SDL_SCANCODE_C, ZXK_C }, { SDL_SCANCODE_V, ZXK_V },
    { SDL_SCANCODE_A, ZXK_A }, { SDL_SCANCODE_S, ZXK_S }, { SDL_SCANCODE_D, ZXK_D },
    { SDL_SCANCODE_F, ZXK_F }, { SDL_SCANCODE_G, ZXK_G },
    { SDL_SCANCODE_Q, ZXK_Q }, { SDL_SCANCODE_W, ZXK_W }, { SDL_SCANCODE_E, ZXK_E },
    { SDL_SCANCODE_R, ZXK_R }, { SDL_SCANCODE_T, ZXK_T },
    { SDL_SCANCODE_1, ZXK_1 }, { SDL_SCANCODE_2, ZXK_2 }, { SDL_SCANCODE_3, ZXK_3 },
    { SDL_SCANCODE_4, ZXK_4 }, { SDL_SCANCODE_5, ZXK_5 },
    { SDL_SCANCODE_0, ZXK_0 }, { SDL_SCANCODE_9, ZXK_9 }, { SDL_SCANCODE_8, ZXK_8 },
    { SDL_SCANCODE_7, ZXK_7 }, { SDL_SCANCODE_6, ZXK_6 },
    { SDL_SCANCODE_P, ZXK_P }, { SDL_SCANCODE_O, ZXK_O }, { SDL_SCANCODE_I, ZXK_I },
    { SDL_SCANCODE_U, ZXK_U }, { SDL_SCANCODE_Y, ZXK_Y },
    { SDL_SCANCODE_RETURN, ZXK_ENTER }, { SDL_SCANCODE_L, ZXK_L }, { SDL_SCANCODE_K, ZXK_K },
    { SDL_SCANCODE_J, ZXK_J }, { SDL_SCANCODE_H, ZXK_H },
    { SDL_SCANCODE_SPACE, ZXK_SPACE }, { SDL_SCANCODE_LCTRL, ZXK_SYMB }, { SDL_SCANCODE_RCTRL, ZXK_SYMB },
    { SDL_SCANCODE_M, ZXK_M }, { SDL_SCANCODE_N, ZXK_N }, { SDL_SCANCODE_B, ZXK_B },
};

enum { MI_OPEN, MI_TAPE_TOGGLE, MI_CLOSE, MI_N };
static bool menu_open;
static int menu_sel;
static char menu_msg[64];

static void set_title(void)
{
    char t[160];
    snprintf(t, sizeof t, "%s - %s%s  (F9 media, F1 tape)", zxspec_title(ZX_MODEL),
             media_name[0] ? media_name : "(no media)",
             zxspec_tape_playing(cpu_m) ? " [PLAY]" : "");
    sdl_port_set_title(t);
}

static void note(const char *s)
{
    snprintf(menu_msg, sizeof menu_msg, "%s", s ? s : "");
    printf("%s\n", menu_msg);
}

static void apply_path(const char *path)
{
    if (!path || !path[0]) return;
    if (zxspec_load_file(cpu_m, path)) {
        const char *base = path, *p = path;
        for (; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
        snprintf(media_name, sizeof media_name, "%s", base);
        note(media_name);
    } else {
        note("Load failed (need .tap/.tzx/.sna)");
    }
    set_title();
}

static char *pick_path(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ var el = document.getElementById('zx-media-file'); if (el) el.click(); });
    return NULL;
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Spectrum media' --file-filter='TAP TZX SNA|*.tap *.TAP *.tzx *.TZX *.sna *.SNA' 2>/dev/null",
        "kdialog --getopenfilename . '*.tap *.tzx *.sna' 2>/dev/null",
        NULL
    };
    for (int i = 0; cmds[i]; i++) {
        FILE *p = popen(cmds[i], "r");
        if (!p) continue;
        char buf[512];
        if (fgets(buf, sizeof buf, p)) {
            size_t n = strlen(buf);
            while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
            pclose(p);
            if (!n) return NULL;
            char *c = (char *)malloc(n + 1);
            if (!c) return NULL;
            memcpy(c, buf, n + 1);
            return c;
        }
        pclose(p);
    }
    note("No file dialog — drag-and-drop a .tap/.tzx/.sna");
    return NULL;
#endif
}

static void menu_activate(void)
{
    switch (menu_sel) {
    case MI_OPEN: {
        char *p = pick_path();
        if (p) { apply_path(p); free(p); }
        menu_open = false;
        break;
    }
    case MI_TAPE_TOGGLE:
        if (zxspec_tape_playing(cpu_m)) { zxspec_tape_stop(cpu_m); note("Tape stopped"); }
        else if (zxspec_tape_loaded(cpu_m)) { zxspec_tape_play(cpu_m); note("Tape playing"); }
        else note("No tape loaded");
        set_title();
        menu_open = false;
        break;
    default:
        menu_open = false;
        break;
    }
}

static void blit_text(uint32_t *rgb, int x0, int y0, const char *s, uint32_t fg)
{
    for (int i = 0; s[i]; i++) {
        int ch = (unsigned char)s[i];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 90) ch = '?';
        const uint8_t *g = font5x7[ch - 32];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 7; row++) {
                if (!(bits & (1u << row))) continue;
                int x = x0 + i * 6 + col, y = y0 + row;
                if ((unsigned)x < ZX_W && (unsigned)y < ZX_H) rgb[y * ZX_W + x] = fg;
            }
        }
    }
}

static void draw_menu(uint32_t *rgb)
{
    static const char *labels[MI_N] = {
#ifdef __EMSCRIPTEN__
        "Upload / choose media",
#else
        "Open .tap/.tzx/.sna",
#endif
        "Tape play / stop",
        "Close menu",
    };
    for (int i = 0; i < ZX_W * ZX_H; i++) rgb[i] = ((rgb[i] >> 2) & 0x3f3f3f) | 0x101018;
    int bx = 40, by = 40, bw = ZX_W - 80, bh = 140;
    for (int y = by; y < by + bh; y++)
        for (int x = bx; x < bx + bw; x++) rgb[y * ZX_W + x] = 0x1a2030;
    blit_text(rgb, bx + 12, by + 8, "MEDIA  F9", 0xffcc66);
    char cur[64];
    snprintf(cur, sizeof cur, "Now: %s", media_name[0] ? media_name : "(none)");
    blit_text(rgb, bx + 12, by + 20, cur, 0xa0a8b8);
    for (int i = 0; i < MI_N; i++) {
        int y0 = by + 40 + i * 28;
        uint32_t bg = (i == menu_sel) ? 0x3a6ea5 : 0x252b3a;
        for (int y = y0; y < y0 + 22; y++)
            for (int x = bx + 12; x < bx + bw - 12; x++) rgb[y * ZX_W + x] = bg;
        blit_text(rgb, bx + 20, y0 + 7, labels[i], 0xf0f0f0);
    }
    if (menu_msg[0]) blit_text(rgb, bx + 12, by + bh - 16, menu_msg, 0xffcc66);
}

#ifdef __EMSCRIPTEN__
static char zx_upload_path[96] = "/zx_upload.bin";

EMSCRIPTEN_KEEPALIVE void zx_web_set_upload_path(const char *path)
{
    if (path && path[0])
        snprintf(zx_upload_path, sizeof zx_upload_path, "%s", path);
}

EMSCRIPTEN_KEEPALIVE void zx_web_notify_upload(void)
{
    sdl_port_queue_drop(zx_upload_path);
}
#endif

int main(int argc, char **argv)
{
    cpu_m = zxspec_create(ZX_MODEL);
    if (!cpu_m) { fprintf(stderr, "out of memory\n"); return 1; }
    zxspec_set_rom(cpu_m, zx_rom48, zx_rom128, zx_rom128_bytes);
    zxspec_reset(cpu_m);

    if (argc > 1) apply_path(argv[1]);

    sdl_port_cfg cfg = { zxspec_title(ZX_MODEL), ZX_W, ZX_H, 2, ZX_AUDIO_RATE, ZX_FPS, true };
    if (!sdl_port_open(&cfg)) return 1;
    set_title();

#ifdef __EMSCRIPTEN__
    EM_ASM({
        var bar = document.getElementById('zx-media-bar');
        if (bar) bar.style.display = 'flex';
        function zxDropFile(f) {
            if (!f) return;
            var r = new FileReader();
            r.onload = function(){
                var u8 = new Uint8Array(r.result);
                if (typeof FS === 'undefined') return;
                var name = (f.name || 'upload.bin').replace(/[^\w.\-]+/g, '_');
                var path = '/' + name;
                FS.writeFile(path, u8);
                if (Module.ccall) Module.ccall('zx_web_set_upload_path', null, ['string'], [path]);
                else if (Module._zx_web_set_upload_path) Module._zx_web_set_upload_path(path);
                if (Module._zx_web_notify_upload) Module._zx_web_notify_upload();
            };
            r.readAsArrayBuffer(f);
        }
        document.body.addEventListener('dragover', function(e){ e.preventDefault(); });
        document.body.addEventListener('drop', function(e){
            e.preventDefault();
            if (!e.dataTransfer || !e.dataTransfer.files || !e.dataTransfer.files.length) return;
            zxDropFile(e.dataTransfer.files[0]);
        });
    });
#endif

    while (sdl_port_poll(NULL)) {
        char *drop;
        while ((drop = sdl_port_next_drop()) != NULL) {
            apply_path(drop);
            free(drop);
            menu_open = false;
        }
        int k;
        while ((k = sdl_port_next_press()) >= 0) {
            if (menu_open) {
                if (k == SDL_SCANCODE_UP) { if (menu_sel > 0) menu_sel--; }
                else if (k == SDL_SCANCODE_DOWN) { if (menu_sel < MI_N - 1) menu_sel++; }
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_SPACE) menu_activate();
                else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F9) menu_open = false;
                continue;
            }
            if (k == SDL_SCANCODE_F9) { menu_open = true; menu_sel = 0; }
            if (k == SDL_SCANCODE_F1) {
                if (zxspec_tape_playing(cpu_m)) zxspec_tape_stop(cpu_m);
                else if (zxspec_tape_loaded(cpu_m)) zxspec_tape_play(cpu_m);
                set_title();
            }
            if (k == SDL_SCANCODE_F12) zxspec_reset(cpu_m);
        }

        if (!menu_open) {
            zxspec_keys_clear(cpu_m);
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
                if (sdl_port_key(keymap[i].sc)) zxspec_key(cpu_m, keymap[i].key, true);
            if (sdl_port_key(SDL_SCANCODE_RSHIFT)) zxspec_key(cpu_m, ZXK_SYMB, true);
            if (sdl_port_key(SDL_SCANCODE_BACKSPACE)) {
                zxspec_key(cpu_m, ZXK_CAPS, true);
                zxspec_key(cpu_m, ZXK_0, true);
            }
            if (sdl_port_key(SDL_SCANCODE_LEFT)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_5, true); }
            if (sdl_port_key(SDL_SCANCODE_DOWN)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_6, true); }
            if (sdl_port_key(SDL_SCANCODE_UP)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_7, true); }
            if (sdl_port_key(SDL_SCANCODE_RIGHT)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_8, true); }
            uint8_t joy = 0;
            if (sdl_port_key(SDL_SCANCODE_KP_6)) joy |= 1;
            if (sdl_port_key(SDL_SCANCODE_KP_4)) joy |= 2;
            if (sdl_port_key(SDL_SCANCODE_KP_2)) joy |= 4;
            if (sdl_port_key(SDL_SCANCODE_KP_8)) joy |= 8;
            if (sdl_port_key(SDL_SCANCODE_KP_0)) joy |= 16;
            zxspec_joy(cpu_m, joy);
        } else {
            zxspec_keys_clear(cpu_m);
        }

        const int16_t *audio;
        int n = zxspec_run_frame(cpu_m, &audio);
        sdl_port_audio(audio, n);
        zxspec_render(cpu_m, frame);
        if (menu_open) draw_menu(frame);
        sdl_port_present(frame);
    }
    sdl_port_close();
    zxspec_destroy(cpu_m);
    return 0;
}
