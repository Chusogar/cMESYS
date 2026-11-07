// comp_sdl.h - what the desktop (SDL2) and web frontends of the computers
// migrated after the Spectrum share (CPC, C64, Lynx): the F9 media menu drawn
// over the picture, a file dialog (zenity / kdialog), drag-and-drop and the
// web page's upload bar. Included once by each <machine>_sdl.c (static).
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
#include "sdl_port.h"
#include "../../../ports/esp32-cyd/font5x7.h"

typedef struct {
    uint32_t *rgb;              // the picture the menu is drawn over
    int w, h;
    const char *title;          // "MEDIA  F9"
    const char *exts;           // shown under the items ("SNA DSK CDT")
    const char *filter;         // zenity patterns ("*.sna *.SNA ...")
    const char *items[6];       // menu items (NULL ends)
    int sel, n;
    bool open;
    char msg[128];
    char now[96];
} comp_menu;

static void comp_note(comp_menu *mm, const char *s)
{
    snprintf(mm->msg, sizeof mm->msg, "%s", s ? s : "");
    printf("%s\n", mm->msg);
}

static void comp_blit_text(uint32_t *rgb, int W, int H, int x0, int y0, const char *s, uint32_t fg)
{
    for (int i = 0; s[i]; i++) {
        int ch = (unsigned char)s[i];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 90) ch = '?';
        const uint8_t *g = font5x7[ch - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++) {
                if (!(g[col] & (1u << row))) continue;
                int x = x0 + i * 6 + col, y = y0 + row;
                if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) rgb[y * W + x] = fg;
            }
    }
}

static void comp_draw_menu(comp_menu *mm)
{
    uint32_t *rgb = mm->rgb;
    const int W = mm->w, H = mm->h;
    for (int i = 0; i < W * H; i++) rgb[i] = ((rgb[i] >> 2) & 0x3f3f3f) | 0x101018;
    const int bw = 240, bh = 50 + mm->n * 28 + 30, bx = (W - bw) / 2, by = (H - bh) / 2;
    for (int y = by; y < by + bh; y++)
        for (int x = bx; x < bx + bw; x++)
            if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) rgb[y * W + x] = 0x1a2030;
    comp_blit_text(rgb, W, H, bx + 12, by + 8, mm->title, 0xffcc66);
    char cur[120];
    snprintf(cur, sizeof cur, "NOW: %.34s", mm->now[0] ? mm->now : "(NONE)");
    comp_blit_text(rgb, W, H, bx + 12, by + 20, cur, 0xa0a8b8);
    for (int i = 0; i < mm->n; i++) {
        const int y0 = by + 36 + i * 28;
        const uint32_t bg = i == mm->sel ? 0x3a6ea5 : 0x252b3a;
        for (int y = y0; y < y0 + 22; y++)
            for (int x = bx + 12; x < bx + bw - 12; x++)
                if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) rgb[y * W + x] = bg;
        comp_blit_text(rgb, W, H, bx + 20, y0 + 7, mm->items[i], 0xf0f0f0);
    }
    comp_blit_text(rgb, W, H, bx + 12, by + bh - 28, mm->exts, 0x808898);
    if (mm->msg[0]) comp_blit_text(rgb, W, H, bx + 12, by + bh - 14, mm->msg, 0xffcc66);
}

// A file from a dialog (malloc'd), or NULL. The web page opens its file input.
static char *comp_pick_path(comp_menu *mm)
{
#ifdef __EMSCRIPTEN__
    (void)mm;
    EM_ASM({ var el = document.getElementById('zx-media-file'); if (el) el.click(); });
    return NULL;
#elif defined(_WIN32)
    // the Windows open dialog (comdlg32), filtered as zenity's: "*.sna;*.dsk"
    char filter[600], file[MAX_PATH] = "";
    int n = snprintf(filter, sizeof filter, "%s", mm->exts) + 1;
    for (const char *f = mm->filter; *f && n < (int)sizeof filter - 8; f++) filter[n++] = *f == ' ' ? ';' : *f;
    filter[n++] = 0;
    memcpy(filter + n, "All\0*.*\0\0", 10);
    OPENFILENAMEA o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.lpstrFilter = filter;
    o.lpstrFile = file;
    o.nMaxFile = sizeof file;
    o.lpstrTitle = mm->title;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&o)) return NULL;
    return _strdup(file);
#else
    char z[1024], k[1024];
    snprintf(z, sizeof z, "zenity --file-selection --title='%s' --file-filter='%s|%s' --file-filter='*|*' 2>/dev/null",
             mm->title, mm->exts, mm->filter);
    snprintf(k, sizeof k, "kdialog --getopenfilename . '%s' 2>/dev/null", mm->filter);
    const char *cmds[] = { z, k, NULL };
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
            if (c) memcpy(c, buf, n + 1);
            return c;
        }
        pclose(p);
    }
    comp_note(mm, "No file dialog: drag-and-drop the file");
    return NULL;
#endif
}

static const char *comp_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}

#ifdef __EMSCRIPTEN__
static char comp_upload_path[96] = "/upload.bin";
EMSCRIPTEN_KEEPALIVE void zx_web_set_upload_path(const char *path)
{
    if (path && path[0]) snprintf(comp_upload_path, sizeof comp_upload_path, "%s", path);
}
EMSCRIPTEN_KEEPALIVE void zx_web_notify_upload(void) { sdl_port_queue_drop(comp_upload_path); }

// The page's upload bar (its text: `label`, the files offered: `accept`,
// ".sna,.dsk") and drag-and-drop (as the Spectrum's)
static void comp_web_setup(const char *label, const char *accept)
{
    EM_ASM({
        var bar = document.getElementById('zx-media-bar');
        if (bar) {
            bar.style.display = 'flex';
            var span = bar.querySelector('span');
            if (span) span.textContent = UTF8ToString($0);
            var inp = document.getElementById('zx-media-file');
            if (inp) inp.setAttribute('accept', UTF8ToString($1) + ',application/octet-stream');
        }
        function dropFile(f) {
            if (!f) return;
            var r = new FileReader();
            r.onload = function() {
                var u8 = new Uint8Array(r.result);
                if (typeof FS === 'undefined') return;
                var path = '/' + (f.name || 'upload.bin').replace(/[^\w.\-]+/g, '_');
                FS.writeFile(path, u8);
                if (Module.ccall) Module.ccall('zx_web_set_upload_path', null, ['string'], [path]);
                if (Module._zx_web_notify_upload) Module._zx_web_notify_upload();
            };
            r.readAsArrayBuffer(f);
        }
        document.body.addEventListener('dragover', function(e) { e.preventDefault(); });
        document.body.addEventListener('drop', function(e) {
            e.preventDefault();
            if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length) dropFile(e.dataTransfer.files[0]);
        });
    }, label, accept);
}
#else
static void comp_web_setup(const char *label, const char *accept) { (void)label; (void)accept; }
#endif
