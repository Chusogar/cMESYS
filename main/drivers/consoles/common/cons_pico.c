// cons_pico.c - any console of main/drivers/consoles on the RP2040 /
// RP2350-PiZero (HDMI, 320x240): the frontend all of them share (cons.h).
// A small picture is doubled when it fits, a big one is shown with every
// other pixel / line.
//
// USB keyboard: cursors the pad, Z / Left Ctrl button 1, X / Left Alt
// button 2, C button 3, V button 4, Enter START, Backspace SELECT, 5 COIN;
// for the consoles with keys, the keys too (buttons then on Ctrl / Alt).
// F9 media menu (microSD), F3 reset, F12 TURBO, F2 what the emulation costs.
// A USB gamepad: d-pad, its buttons 1-4, START, SELECT.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "cons.h"
#include "../../computers/common/comp_pico.h"

static bool ready;

// HID usage -> key (the keys' consoles)
static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, CK_A }, { 0x05, CK_B }, { 0x06, CK_C }, { 0x07, CK_D }, { 0x08, CK_E }, { 0x09, CK_F },
    { 0x0a, CK_G }, { 0x0b, CK_H }, { 0x0c, CK_I }, { 0x0d, CK_J }, { 0x0e, CK_K }, { 0x0f, CK_L },
    { 0x10, CK_M }, { 0x11, CK_N }, { 0x12, CK_O }, { 0x13, CK_P }, { 0x14, CK_Q }, { 0x15, CK_R },
    { 0x16, CK_S }, { 0x17, CK_T }, { 0x18, CK_U }, { 0x19, CK_V }, { 0x1a, CK_W }, { 0x1b, CK_X },
    { 0x1c, CK_Y }, { 0x1d, CK_Z }, { 0x27, CK_0 }, { 0x1e, CK_1 }, { 0x1f, CK_2 }, { 0x20, CK_3 },
    { 0x21, CK_4 }, { 0x22, CK_5 }, { 0x23, CK_6 }, { 0x24, CK_7 }, { 0x25, CK_8 }, { 0x26, CK_9 },
    { 0x28, CK_ENTER }, { 0x2c, CK_SPACE }, { 0xe1, CK_LSHIFT }, { 0xe5, CK_RSHIFT }, { 0xe0, CK_LCTRL },
    { 0xe4, CK_RCTRL }, { 0x2a, CK_BACKSPACE }, { 0x52, CK_UP }, { 0x51, CK_DOWN }, { 0x50, CK_LEFT },
    { 0x4f, CK_RIGHT }, { 0x36, CK_COMMA }, { 0x37, CK_PERIOD }, { 0x33, CK_SEMICOLON }, { 0x34, CK_QUOTE },
    { 0x38, CK_SLASH }, { 0x2d, CK_MINUS }, { 0x29, CK_ESCAPE }, { 0x2b, CK_TAB }, { 0x39, CK_CAPSLOCK },
    { 0x4a, CK_HOME }, { 0x2e, CK_EQUALS }, { 0x57, CK_PLUS }, { 0x55, CK_ASTERISK }, { 0x2f, CK_AT },
    { 0x31, CK_BACKSLASH }, { 0x35, CK_BACKQUOTE }, { 0x4c, CK_DELETE }, { 0xe3, CK_LGUI }, { 0xe7, CK_RGUI },
    { 0xe6, CK_RALT },
};
static bool held_keys[CK_COUNT];

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    if (!ready) {
        for (int i = 0; i < n; i++) pico_fill(out + i * PICO_W, PICO_W, 0);
        return;
    }
    int w, h, stride;
    const uint16_t *fb = cons_frame(&w, &h, &stride);
    const int s = (w * 2 <= PICO_W && h * 2 <= PICO_H) ? 2 : 1;      // doubled
    const int xs = w > PICO_W ? (w + PICO_W - 1) / PICO_W : 1;         // big: every other pixel
    const int ys = h > PICO_H ? (h + PICO_H - 1) / PICO_H : 1;
    const int sw = w * s / xs, sh = h * s / ys;
    const int ox = (PICO_W - sw) / 2, oy = (PICO_H - sh) / 2;
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int py = y + i - oy;
        if (py < 0 || py >= sh) {
            pico_fill(out, PICO_W, 0);
            continue;
        }
        const uint16_t *src = fb + (py / s) * ys * stride;
        if (ox > 0) {
            pico_fill(out, ox, 0);
            pico_fill(out + ox + sw, PICO_W - ox - sw, 0);
        }
        uint16_t *d = out + (ox > 0 ? ox : 0);
        if (s == 2) {
            for (int x = 0; x < w; x++) d[2 * x] = d[2 * x + 1] = src[x];
        } else {
            const int cw = sw < PICO_W ? sw : PICO_W;
            for (int x = 0; x < cw; x++) d[x] = src[x * xs];
        }
    }
}

static bool load(const char *name, const char **why)
{
    size_t size = 0;
    uint8_t *data = ql_sd_load_file(name, &size);
    if (!data) {
        *why = "cannot read the file (out of memory?)";
        return false;
    }
    size_t un = 0;
    char inner[64];
    uint8_t *z = cons_unzip(data, size, cons_machine.exts, &un, inner, sizeof inner);
    if (z) {
        free(data);
        data = z;
        size = un;
    }
    return cons_load(data, size, name, why);
}

static const char *item(int i) { return i == 0 ? "RESET" : i == 1 ? "EJECT" : NULL; }
static void item_do(int i)
{
    if (i == 0) cons_reset();
    else cons_eject();
}

static bool show_stats;
static uint64_t emu_us, stats_t0;
static int stats_frames;

static void stats(uint64_t frame_us)
{
    emu_us += frame_us;
    if (++stats_frames < 60) return;
    const uint64_t now = pico_port_us(), wall = now - stats_t0;
    if (stats_t0 && wall) {
        char line[48];
        snprintf(line, sizeof line, "EMU %u%%  %u.%u FPS", (unsigned)(emu_us * 100 / wall),
                 (unsigned)(stats_frames * 1000000ull / wall), (unsigned)(stats_frames * 10000000ull / wall % 10));
        if (show_stats) pico_osd_toast(line, 1200);
        printf("%s: %s\n", cons_machine.id, line);
    }
    stats_t0 = now;
    stats_frames = 0;
    emu_us = 0;
}

static void frame(bool sound)
{
    const int16_t *audio;
    const uint64_t t0 = pico_port_us();
    const int n = cons_run_frame(&audio);
    stats(pico_port_us() - t0);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    cons_pad(0, 0);
    cons_pad(1, 0);
    for (int k = 0; k < CK_COUNT; k++)
        if (held_keys[k]) {
            held_keys[k] = false;
            cons_key(k, false);
        }
}

static const comp_pico_menu kMenu = { "", NULL, load, item, item_do, frame, release };

static void game(void)
{
    static comp_pico_menu menu;
    static char title[40];
    menu = kMenu;
    snprintf(title, sizeof title, "%s", cons_machine.title);
    for (char *c = title; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    menu.title = title;
    menu.exts = cons_machine.exts;
    const bool keyboard = cons_machine.keys == CONS_KEYBOARD;
    for (;;) {
        if (pico_port_poll()) cons_reset();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x3c) cons_reset();                             // F3
            if (k == 0x3b) {                                         // F2
                show_stats = !show_stats;
                if (!show_stats) pico_osd_toast("", 1);
            }
            if (k == 0x42) comp_media_menu(&menu);                   // F9
            if (k == 0x45) comp_toggle_turbo();                      // F12
        }
        const uint32_t p = pico_port_pad();
        unsigned b = 0;
        if (pico_port_key(0x52) || (p & SP_UP)) b |= CP_UP;
        if (pico_port_key(0x51) || (p & SP_DOWN)) b |= CP_DOWN;
        if (pico_port_key(0x50) || (p & SP_LEFT)) b |= CP_LEFT;
        if (pico_port_key(0x4f) || (p & SP_RIGHT)) b |= CP_RIGHT;
        if (pico_port_key(0xe0) || (!keyboard && pico_port_key(0x1d)) || (p & SP_B1)) b |= CP_B1;
        if (pico_port_key(0xe2) || (!keyboard && pico_port_key(0x1b)) || (p & SP_B2)) b |= CP_B2;
        if ((!keyboard && pico_port_key(0x06)) || (p & SP_B3)) b |= CP_B3;
        if (!keyboard && pico_port_key(0x19)) b |= CP_B4;
        if ((!keyboard && pico_port_key(0x28)) || (p & SP_START1)) b |= CP_START;
        if ((!keyboard && pico_port_key(0x2a)) || (p & SP_COIN1)) b |= CP_SELECT;
        if (!keyboard && pico_port_key(0x22)) b |= CP_COIN;
        cons_pad(0, b);
        if (cons_machine.keys != CONS_NO_KEYS)
            for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
                const bool d = pico_port_key(keymap[i].usage);
                if (d != held_keys[keymap[i].key]) {
                    held_keys[keymap[i].key] = d;
                    cons_key(keymap[i].key, d);
                }
            }
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_cfg cfg = { cons_machine.title, render, NULL, cons_machine.audio_rate, cons_machine.fps, true };
    pico_port_open(&cfg);                   // (the RP2350's PSRAM heap is there after this)
    if (!cons_init()) {
        char msg[PICO_OSD_COLS + 1];
        printf("%s: out of memory\n", cons_machine.id);
        pico_osd_text(2, "OUT OF MEMORY FOR THIS CONSOLE", true);
        pico_port_mem_info(msg, sizeof msg);
        pico_osd_text(4, msg, false);
        pico_osd_show(true);
        for (;;) pico_port_poll();
    }
    ready = true;
    pico_port_run(game);
}
