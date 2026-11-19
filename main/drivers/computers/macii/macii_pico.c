// macii_pico.c - Macintosh II on the RP2350-PiZero (HDMI): the 68020, 4 MB
// of RAM and the Display Card's 1 MB of VRAM in the board's PSRAM (the ROMs
// stay in the flash). The 640 x 480 picture is drawn 320 x 240 (each pixel
// the mean of four). It runs slower than the real one.
//
// USB keyboard = the Apple Extended Keyboard (Alt = Option, GUI = Command,
// F1-F10, Insert = help, Delete = forward delete, Home / End / Page Up /
// Page Down); a USB mouse is its mouse. Host keys:
//   F11 media menu (microSD: the SCSI hard disk image)    F12 TURBO
// The disk is read from the card; what the Mac writes on it stays in the
// PSRAM (lost at power off).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "macii_core.h"
#include "rom_data.h"
#include "../common/comp_blk.h"
#include "../common/comp_pico.h"
#include "../common/adb_hid.h"

static bool held_prev[128];
static const uint8_t kFkeys[10] = { ADBK_F1, ADBK_F2, ADBK_F3, ADBK_F4, ADBK_F5, ADBK_F6, ADBK_F7, ADBK_F8,
                                    ADBK_F9, ADBK_F10 };

static inline uint16_t avg565(uint16_t a, uint16_t b) { return (uint16_t)((((a ^ b) & 0xf7deu) >> 1) + (a & b)); }

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static uint16_t l0[MACII_W], l1[MACII_W];
    for (int i = 0; i < n; i++) {
        macii_render_row565(2 * (y + i), l0);
        macii_render_row565(2 * (y + i) + 1, l1);
        uint16_t *o = out + i * PICO_W;
        for (int x = 0; x < PICO_W; x++)
            o[x] = avg565(avg565(l0[2 * x], l0[2 * x + 1]), avg565(l1[2 * x], l1[2 * x + 1]));
    }
}

static bool load(const char *name, const char **why)
{
    blkfile f;
    if (!comp_blk_open(name, &f, why)) return false;
    macii_detach_scsi();
    const char *err = macii_attach_scsi(&f);
    if (err) {
        blkfile_close(&f);
        *why = err;
        return false;
    }
    macii_reset();                              // (the ROM looks for SCSI disks at power on)
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "DISCONNECT THE SCSI DISK";
    case 1: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        macii_detach_scsi();
        comp_media[0] = 0;
    } else macii_reset();
}

static void frame(bool sound)
{
    const int16_t *audio;
    macii_set_turbo(!sound);
    const int n = macii_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    macii_keys_clear();
    memset(held_prev, 0, sizeof held_prev);
}

static const char *const kExts[] = MACII_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "MACINTOSH II", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x44) comp_media_menu(&kMenu);                // F11
            if (k == 0x45) comp_toggle_turbo();                    // F12
        }
        bool held[128] = { false };
        adb_hid_held(&adb_hid_adb, pico_port_key, held);
        for (int i = 0; i < 10; i++)
            if (pico_port_key(0x3a + i)) held[kFkeys[i]] = true;
        if (pico_port_key(0x49)) held[ADBK_HELP] = true;
        if (pico_port_key(0x4a)) held[ADBK_HOME] = true;
        if (pico_port_key(0x4b)) held[ADBK_PGUP] = true;
        if (pico_port_key(0x4c)) held[ADBK_FWD_DEL] = true;
        if (pico_port_key(0x4d)) held[ADBK_END] = true;
        if (pico_port_key(0x4e)) held[ADBK_PGDN] = true;
        for (int c = 0; c < ADBK_COUNT; c++) {
            if (held[c] == held_prev[c]) continue;
            held_prev[c] = held[c];
            macii_key(c, held[c]);
        }
        int dx = 0, dy = 0;
        const uint32_t b = pico_port_mouse(&dx, &dy);
        macii_mouse(dx, dy, (b & 3) != 0);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();                         // (the PSRAM: the RAM goes there)
    if (!macii_rom_load()) pico_port_rom_error();
    if (!macii_rom || !macii_rom_card || !macii_init(macii_rom, macii_rom_card, 4)) {
        printf(macii_rom ? "out of memory (5 MB of PSRAM)\n" : "no Macintosh II ROMs\n");
        for (;;) {}
    }
    pico_port_cfg cfg = { "Macintosh II", render, NULL, MACII_AUDIO_RATE, MACII_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
