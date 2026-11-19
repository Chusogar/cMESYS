// specnext_pico.c - the ZX Spectrum Next on the RP2350-PiZero (HDMI), with
// its 2 MB of SRAM in the board's PSRAM; the boot ROM from the flash or the
// microSD (roms/specnext). It boots as the real one, from an SD card image
// on the microSD (read as it is used; what the Next writes on it stays in
// the PSRAM until power off). The 640 x 256 picture is drawn 320 x 240 (two
// dots a pixel, 8 lines of border off at the top and the bottom). It runs
// slower than the real one.
//
// USB keyboard = the Next's (Shift = CAPS SHIFT, Ctrl = SYMBOL SHIFT;
// Backspace DELETE, ` EDIT, Esc BREAK, Tab EXTEND MODE, Caps Lock, Insert
// GRAPH, Home TRUE VIDEO, End INV VIDEO, the cursors, ; " , .). Host keys:
//   F9  media menu (microSD: an SD card image .img .mmc .hdf, or a .nex)
//   F7  NMI button    F8  DRIVE button    F10 RESET    F11 power    F12 TURBO
// A USB gamepad, or the keypad (8 4 6 2, 0 / 5 fire), is the joystick.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "specnext_core.h"
#include "rom_data.h"
#include "../common/comp_blk.h"
#include "../common/comp_pico.h"

static bool held_prev[NK_COUNT];

// HID usage -> the Next's key
static const struct { uint8_t usage; uint8_t key; } keymap[] = {
    { 0x04, NK_A }, { 0x05, NK_B }, { 0x06, NK_C }, { 0x07, NK_D }, { 0x08, NK_E }, { 0x09, NK_F }, { 0x0a, NK_G },
    { 0x0b, NK_H }, { 0x0c, NK_I }, { 0x0d, NK_J }, { 0x0e, NK_K }, { 0x0f, NK_L }, { 0x10, NK_M }, { 0x11, NK_N },
    { 0x12, NK_O }, { 0x13, NK_P }, { 0x14, NK_Q }, { 0x15, NK_R }, { 0x16, NK_S }, { 0x17, NK_T }, { 0x18, NK_U },
    { 0x19, NK_V }, { 0x1a, NK_W }, { 0x1b, NK_X }, { 0x1c, NK_Y }, { 0x1d, NK_Z },
    { 0x1e, NK_1 }, { 0x1f, NK_2 }, { 0x20, NK_3 }, { 0x21, NK_4 }, { 0x22, NK_5 }, { 0x23, NK_6 }, { 0x24, NK_7 },
    { 0x25, NK_8 }, { 0x26, NK_9 }, { 0x27, NK_0 },
    { 0x28, NK_ENTER }, { 0x58, NK_ENTER }, { 0x29, NK_BREAK }, { 0x2a, NK_DELETE }, { 0x4c, NK_DELETE },
    { 0x2b, NK_EXTEND }, { 0x2c, NK_SPACE }, { 0x2d, NK_MINUS }, { 0x56, NK_MINUS }, { 0x2e, NK_EQUALS },
    { 0x57, NK_PLUS }, { 0x33, NK_SEMICOLON }, { 0x34, NK_QUOTE }, { 0x35, NK_EDIT }, { 0x36, NK_COMMA },
    { 0x37, NK_PERIOD }, { 0x38, NK_SLASH }, { 0x54, NK_SLASH }, { 0x55, NK_ASTERISK }, { 0x39, NK_CAPS_LOCK },
    { 0x49, NK_GRAPH }, { 0x4a, NK_TRUE_VIDEO }, { 0x4d, NK_INV_VIDEO },
    { 0x4f, NK_RIGHT }, { 0x50, NK_LEFT }, { 0x51, NK_DOWN }, { 0x52, NK_UP },
    { 0xe0, NK_SYM_SHIFT }, { 0xe4, NK_SYM_SHIFT }, { 0xe1, NK_CAPS_SHIFT }, { 0xe5, NK_CAPS_SHIFT },
};

static inline uint16_t avg565(uint16_t a, uint16_t b) { return (uint16_t)((((a ^ b) & 0xf7deu) >> 1) + (a & b)); }

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static uint16_t line[SN_W];
    for (int i = 0; i < n; i++) {
        specnext_render_row565(y + i + (SN_H - PICO_H) / 2, line);
        uint16_t *o = out + i * PICO_W;
        for (int x = 0; x < PICO_W; x++) o[x] = avg565(line[2 * x], line[2 * x + 1]);
    }
}

static bool ends_with(const char *s, const char *ext)
{
    const size_t n = strlen(s), m = strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

static bool load(const char *name, const char **why)
{
    blkfile f;
    if (!comp_blk_open(name, &f, why)) return false;
    if (ends_with(name, ".nex")) {
        uint8_t *data = (uint8_t *)malloc(f.size ? f.size : 1);
        const bool ok = data && f.read(f.ctx, 0, data, f.size);
        const uint32_t size = f.size;
        blkfile_close(&f);
        const char *err = ok ? specnext_load_nex(data, size) : (data ? "SD read error" : "out of memory");
        free(data);
        *why = err;
        return !err;
    }
    const char *err = specnext_insert_sd(&f);
    if (err) {
        blkfile_close(&f);
        *why = err;
        return false;
    }
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "TAKE THE SD CARD OUT";
    case 1: return "NMI BUTTON (NEXTZXOS MENU)";
    case 2: return "RESET BUTTON";
    case 3: return "POWER OFF AND ON";
    default: return NULL;
    }
}

static int nmi_frames;

static void item_do(int i)
{
    if (i == 0) {
        specnext_eject_sd();
        comp_media[0] = 0;
    } else if (i == 1) nmi_frames = 5;
    else if (i == 2) specnext_soft_reset();
    else specnext_hard_reset();
}

static void frame(bool sound)
{
    const int16_t *audio;
    specnext_set_turbo(!sound);
    const int n = specnext_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    specnext_keys_clear();
    specnext_joy(0, 0);
    memset(held_prev, 0, sizeof held_prev);
}

static const char *const kExts[] = SN_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "ZX SPECTRUM NEXT", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x42) comp_media_menu(&kMenu);                // F9
            if (k == 0x43) specnext_soft_reset();                  // F10
            if (k == 0x44) specnext_hard_reset();                  // F11
            if (k == 0x45) comp_toggle_turbo();                    // F12
        }
        bool held[NK_COUNT] = { false };
        for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
            if (pico_port_key(keymap[i].usage)) held[keymap[i].key] = true;
        for (int c = 0; c < NK_COUNT; c++) {
            if (held[c] == held_prev[c]) continue;
            held_prev[c] = held[c];
            specnext_key(c, held[c]);
        }
        if (nmi_frames) nmi_frames--;
        specnext_nmi_buttons(pico_port_key(0x40) || nmi_frames > 0, pico_port_key(0x41));   // F7 / F8
        uint8_t joy = 0;
        if (pico_port_key(0x60)) joy |= SN_JOY_UP;          // keypad 8
        if (pico_port_key(0x5a)) joy |= SN_JOY_DOWN;        // keypad 2
        if (pico_port_key(0x5c)) joy |= SN_JOY_LEFT;        // keypad 4
        if (pico_port_key(0x5e)) joy |= SN_JOY_RIGHT;       // keypad 6
        if (pico_port_key(0x62) || pico_port_key(0x5d)) joy |= SN_JOY_FIRE;   // keypad 0 / 5
        const uint32_t p = pico_port_pad();
        if (p & SP_UP) joy |= SN_JOY_UP;
        if (p & SP_DOWN) joy |= SN_JOY_DOWN;
        if (p & SP_LEFT) joy |= SN_JOY_LEFT;
        if (p & SP_RIGHT) joy |= SN_JOY_RIGHT;
        if (p & SP_B1) joy |= SN_JOY_FIRE;
        if (p & SP_B2) joy |= SN_JOY_FIRE2;
        specnext_joy(0, joy);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();                         // (the PSRAM: the SRAM goes there)
    if (!specnext_rom_load()) pico_port_rom_error();
    if (!specnext_rom_boot || !specnext_init(specnext_rom_boot)) {
        printf(specnext_rom_boot ? "out of memory (2.5 MB of PSRAM)\n" : "no ZX Spectrum Next boot ROM\n");
        for (;;) {}
    }
    pico_port_cfg cfg = { "ZX Spectrum Next", render, NULL, SN_AUDIO_RATE, specnext_fps(), true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
