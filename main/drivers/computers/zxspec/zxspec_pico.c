// zxspec_pico.c - ZX Spectrum family on the Waveshare RP2040-PiZero (HDMI).
// Build with -DZX_MODEL=ZX_SPEC48|ZX_SPEC128|ZX_PLUS3|ZX_PENTAGON|ZX_PENTAGON1024|ZX_SCORPION
//
// USB keyboard = Spectrum matrix. Host keys:
//   F9  media menu (microSD .tap .tzx .cdt .csw .pzx .sna .z80 .rzx .trd .scl .dsk .rom .if2)
//   F1 / F6  tape play / pause     F3  reset     F5  magic button (clones)
//   F12 TURBO (as dsp-cpp: no speed limit, no sound)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "zxspec_core.h"
#include "rom_data.h"
#include "zxspec_sd.h"

#ifndef ZX_MODEL
#define ZX_MODEL ZX_SPEC48
#endif

static zxspec *cpu_m;
static char media_name[48];
static bool turbo;

static const struct { uint8_t usage; int key; } keymap[] = {
    { 0xe1, ZXK_CAPS }, { 0xe5, ZXK_CAPS },
    { 0x1d, ZXK_Z }, { 0x1b, ZXK_X }, { 0x06, ZXK_C }, { 0x19, ZXK_V },
    { 0x04, ZXK_A }, { 0x16, ZXK_S }, { 0x07, ZXK_D }, { 0x09, ZXK_F }, { 0x0a, ZXK_G },
    { 0x14, ZXK_Q }, { 0x1a, ZXK_W }, { 0x08, ZXK_E }, { 0x15, ZXK_R }, { 0x17, ZXK_T },
    { 0x1e, ZXK_1 }, { 0x1f, ZXK_2 }, { 0x20, ZXK_3 }, { 0x21, ZXK_4 }, { 0x22, ZXK_5 },
    { 0x27, ZXK_0 }, { 0x26, ZXK_9 }, { 0x25, ZXK_8 }, { 0x24, ZXK_7 }, { 0x23, ZXK_6 },
    { 0x13, ZXK_P }, { 0x12, ZXK_O }, { 0x0c, ZXK_I }, { 0x18, ZXK_U }, { 0x1c, ZXK_Y },
    { 0x28, ZXK_ENTER }, { 0x58, ZXK_ENTER },
    { 0x0f, ZXK_L }, { 0x0e, ZXK_K }, { 0x0d, ZXK_J }, { 0x0b, ZXK_H },
    { 0x2c, ZXK_SPACE }, { 0xe0, ZXK_SYMB }, { 0xe4, ZXK_SYMB },
    { 0x10, ZXK_M }, { 0x11, ZXK_N }, { 0x05, ZXK_B },
};

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++)
        zxspec_render_row565(cpu_m, y + i, out + i * PICO_W);
}

static bool load_sd_name(const char *name)
{
    bool ok = zx_sd_load(cpu_m, name);
    if (ok) {
        snprintf(media_name, sizeof media_name, "%s", name);
        printf("media: %s\n", media_name);
    } else {
        printf("load failed: %s\n", zxspec_last_error(cpu_m));
    }
    return ok;
}

static void media_menu(void)
{
    static const char *const exts[] = ZX_MEDIA_EXTS;
    static ql_sd_entry files[QL_SD_MAX_FILES];      // (2.5 KB: not on the 4 KB task stack)
    int nfiles = 0;
    bool sd_ok = ql_sd_mount();
    if (sd_ok) nfiles = ql_sd_list_ext(files, QL_SD_MAX_FILES, exts);
    int n = nfiles + 2, sel = 0;
    printf("\n=== MEDIA (%s) ===\n", zxspec_title(ZX_MODEL));
    printf("Now: %s%s\n", media_name[0] ? media_name : "(none)",
           zxspec_tape_playing(cpu_m) ? " [PLAY]" : "");
    if (!sd_ok) printf("(no SD card)\n");
    for (int i = 0; i < nfiles; i++) printf("  %2d  %s\n", i, files[i].name);
    printf("  %2d  Tape play / pause\n", nfiles);
    printf("  %2d  Close\n", nfiles + 1);
    printf("Arrows select, Enter confirm, Esc/F9 closes.\n> %d\n", sel);

    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x52) { if (sel > 0) sel--; printf("> %d\n", sel); }
            else if (k == 0x51) { if (sel < n - 1) sel++; printf("> %d\n", sel); }
            else if (k == 0x29 || k == 0x42) return;
            else if (k == 0x28 || k == 0x58) {
                if (sel < nfiles) {
                    if (!load_sd_name(files[sel].name)) printf("load failed\n");
                } else if (sel == nfiles) {
                    zxspec_tape_toggle(cpu_m);
                    printf("tape: %s\n", zxspec_tape_playing(cpu_m) ? "play" :
                           (zxspec_tape_loaded(cpu_m) ? "pause" : "empty"));
                }
                return;
            }
        }
        const int16_t *audio;
        int ns = zxspec_run_frame(cpu_m, &audio);
        pico_port_audio(audio, ns);
        pico_port_frame_done();
    }
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) zxspec_reset(cpu_m);
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x3a || k == 0x3f) {                            // F1 / F6
                zxspec_tape_toggle(cpu_m);
                printf("tape: %s\n", zxspec_tape_playing(cpu_m) ? "play" :
                       (zxspec_tape_loaded(cpu_m) ? "pause" : "empty"));
            }
            if (k == 0x3c) zxspec_reset(cpu_m);                      // F3
            if (k == 0x3e) zxspec_magic(cpu_m);                      // F5
            if (k == 0x42) media_menu();                             // F9
            if (k == 0x45) {                                         // F12: TURBO
                turbo = !turbo;
                printf("TURBO %s\n", turbo ? "on" : "off");
            }
        }
        zxspec_keys_clear(cpu_m);
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) zxspec_key(cpu_m, keymap[i].key, true);
        if (pico_port_key(0x2a)) {                                   // Backspace = Caps+0
            zxspec_key(cpu_m, ZXK_CAPS, true);
            zxspec_key(cpu_m, ZXK_0, true);
        }
        if (pico_port_key(0x50)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_5, true); }
        if (pico_port_key(0x51)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_6, true); }
        if (pico_port_key(0x52)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_7, true); }
        if (pico_port_key(0x4f)) { zxspec_key(cpu_m, ZXK_CAPS, true); zxspec_key(cpu_m, ZXK_8, true); }
        uint8_t joy = 0;
        uint32_t p = pico_port_keys();
        if (p & SP_RIGHT) joy |= 1;
        if (p & SP_LEFT) joy |= 2;
        if (p & SP_DOWN) joy |= 4;
        if (p & SP_UP) joy |= 8;
        if (p & SP_B1) joy |= 16;
        zxspec_joy(cpu_m, joy);
        const int16_t *audio;
        int n = zxspec_run_frame(cpu_m, &audio);
        if (!turbo) pico_port_audio(audio, n);      // turbo: no sound, nothing paces
        pico_port_frame_done();
    }
}

int main(void)
{
    cpu_m = zxspec_create(ZX_MODEL);
    if (!cpu_m) {
        printf("out of memory\n");
        for (;;) {}
    }
    zxspec_set_rom(cpu_m, zx_rom48, zx_rom128, zx_rom128_bytes);
    zxspec_reset(cpu_m);
    pico_port_cfg cfg = { zxspec_title(ZX_MODEL), render, NULL, ZX_AUDIO_RATE, zxspec_fps(cpu_m), true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
