// macplus_pico.c - Macintosh Plus on the RP2350-PiZero (HDMI), with its 4 MB
// of RAM in the board's PSRAM; the ROM stays in the flash. The 512 x 342
// picture is drawn 320 x 240 in grey (each pixel the mean of four dots).
//
// USB keyboard = the Mac's (Alt = Option, Ctrl / GUI = Command); a USB
// mouse is its mouse. Host keys:
//   F9  media menu (microSD: a floppy, .dsk / .image of 400K, 800K or 1.44
//       MB, into the drive; a larger image is the SCSI hard disk)
//   F10 eject the floppy    F11 reset    F12 TURBO
// Floppies are read into the PSRAM; the hard disk is read from the card, and
// what the Mac writes on it stays in the PSRAM (lost at power off).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "macplus_core.h"
#include "mac_dsk.h"
#include "rom_data.h"
#include "../common/comp_blk.h"
#include "../common/comp_pico.h"
#include "../common/adb_hid.h"

static bool held_prev[128];
static uint16_t col0[PICO_W], col1[PICO_W];      // the two dots each pixel samples
static uint16_t row0[PICO_H], row1[PICO_H];
static const adb_hid_keys kKeys = { MK_LEFT, MK_RIGHT, MK_DOWN, MK_UP, ADB_NONE, MK_SHIFT, MK_OPTION, MK_COMMAND };

static inline int dot(const uint8_t *line, int x) { return (line[x >> 3] >> (7 - (x & 7))) & 1; }

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static const uint16_t grey[5] = { 0xffff, 0xbdf7, 0x8410, 0x4208, 0x0000 };
    const uint8_t *s = macplus_screen();
    for (int i = 0; i < n; i++) {
        const uint8_t *a = s + row0[y + i] * (MACPLUS_W / 8), *b = s + row1[y + i] * (MACPLUS_W / 8);
        uint16_t *o = out + i * PICO_W;
        for (int x = 0; x < PICO_W; x++)
            o[x] = grey[dot(a, col0[x]) + dot(a, col1[x]) + dot(b, col0[x]) + dot(b, col1[x])];
    }
}

static bool load(const char *name, const char **why)
{
    blkfile f;
    if (!comp_blk_open(name, &f, why)) return false;
    uint8_t hdr[0x54];
    const bool floppy = f.size <= 0x54 + 1474560 + 0x10000 && f.size >= sizeof hdr &&
                        f.read(f.ctx, 0, hdr, sizeof hdr) && mac_dsk_looks_like_header(hdr, sizeof hdr, f.size);
    if (!floppy) {
        const char *err = macplus_attach_scsi(&f);
        if (err) {
            blkfile_close(&f);
            *why = err;
            return false;
        }
        macplus_reset();                        // (the ROM looks for SCSI disks at power on)
        return true;
    }
    uint8_t *data = (uint8_t *)malloc(f.size);
    const uint32_t size = f.size;
    const bool ok = data && f.read(f.ctx, 0, data, size);
    blkfile_close(&f);
    if (!ok) {
        free(data);
        *why = data ? "SD read error" : "out of memory";
        return false;
    }
    const char *err = macplus_insert_floppy(data, size);
    if (err) {
        free(data);
        *why = err;
        return false;
    }
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "EJECT THE FLOPPY";
    case 1: return "DISCONNECT THE SCSI DISK";
    case 2: return "RESET";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) macplus_eject_floppy();
    else if (i == 1) macplus_detach_scsi();
    else macplus_reset();
    if (i < 2) comp_media[0] = 0;
}

static void frame(bool sound)
{
    const int16_t *audio;
    macplus_set_turbo(!sound);
    const int n = macplus_run_frame(&audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    macplus_keys_clear();
    memset(held_prev, 0, sizeof held_prev);
}

static const char *const kExts[] = MACPLUS_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "MACINTOSH PLUS", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x43) macplus_eject_floppy();                 // F10
            if (k == 0x44) macplus_reset();                        // F11
            if (k == 0x42) comp_media_menu(&kMenu);                // F9
            if (k == 0x45) comp_toggle_turbo();                    // F12
        }
        bool held[128] = { false };
        adb_hid_held(&kKeys, pico_port_key, held);
        for (int c = 0; c < MK_COUNT; c++) {
            if (held[c] == held_prev[c]) continue;
            held_prev[c] = held[c];
            macplus_key(c, held[c]);
        }
        int dx = 0, dy = 0;
        const uint32_t b = pico_port_mouse(&dx, &dy);
        macplus_mouse(dx, dy, (b & 3) != 0);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();                         // (the PSRAM: the RAM goes there)
    for (int x = 0; x < PICO_W; x++) {
        col0[x] = (uint16_t)(x * MACPLUS_W / PICO_W);
        col1[x] = (uint16_t)((x * MACPLUS_W + MACPLUS_W / 2) / PICO_W);
    }
    for (int y = 0; y < PICO_H; y++) {
        row0[y] = (uint16_t)(y * MACPLUS_H / PICO_H);
        row1[y] = (uint16_t)((y * MACPLUS_H + MACPLUS_H / 2) / PICO_H);
    }
    if (!macplus_rom_load()) pico_port_rom_error();
    if (!macplus_rom_even || !macplus_rom_odd || !macplus_init(macplus_rom_even, macplus_rom_odd)) {
        printf(macplus_rom_even ? "out of memory (4 MB of PSRAM)\n" : "no Macintosh Plus ROMs\n");
        for (;;) {}
    }
    pico_port_cfg cfg = { "Macintosh Plus", render, NULL, MACPLUS_AUDIO_RATE, MACPLUS_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
