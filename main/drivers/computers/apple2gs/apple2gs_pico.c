// apple2gs_pico.c - Apple IIGS (ROM 3) on the RP2350-PiZero (HDMI), with
// its 4 MB of RAM in the board's PSRAM; the ROMs stay in the flash. The 640
// x 200 picture is drawn 320 x 240 (two dots a pixel, every fifth line twice).
//
// USB keyboard = the IIGS's ADB keyboard (Alt = Option, GUI = Command); a
// USB mouse is its mouse. Host keys:
//   F9  media menu (microSD .po .2mg .hdv: SmartPort disks; the first one
//       boots, the others go into the next units)
//   F10 CTRL-RESET    F11 power off and on    F12 TURBO
// The disks are read from the card; what the IIGS writes stays in the PSRAM
// (lost at power off).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_port.h"
#include "apple2gs_core.h"
#include "rom_data.h"
#include "../common/comp_blk.h"
#include "../common/comp_pico.h"
#include "../common/adb_hid.h"

static apple2gs *cpu_m;
static bool held_prev[128];
static int units;                           // disks in

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    static uint16_t line[A2GS_W];
    for (int i = 0; i < n; i++) {
        apple2gs_render_row565(cpu_m, (y + i) * 5 / 6, line);  // 200 lines in 240
        uint16_t *o = out + i * PICO_W;
        for (int x = 0; x < PICO_W; x++) {
            const uint16_t a = line[2 * x], b = line[2 * x + 1];
            o[x] = (uint16_t)((((a ^ b) & 0xf7deu) >> 1) + (a & b));  // (the average of two RGB565)
        }
    }
}

static bool load(const char *name, const char **why)
{
    blkfile f;
    if (!comp_blk_open(name, &f, why)) return false;
    const int unit = units < A2GS_DISKS ? units : A2GS_DISKS - 1;
    if (units >= A2GS_DISKS) apple2gs_eject(cpu_m, unit);
    const char *err = apple2gs_insert(cpu_m, unit, &f, name);
    if (err) {
        blkfile_close(&f);
        *why = err;
        return false;
    }
    if (units < A2GS_DISKS) units++;
    if (unit == 0) apple2gs_reset(cpu_m);          // the boot disk: power on
    return true;
}

static const char *item(int i)
{
    switch (i) {
    case 0: return "EJECT ALL THE DISKS";
    case 1: return "CTRL-RESET";
    case 2: return "POWER OFF AND ON";
    default: return NULL;
    }
}

static void item_do(int i)
{
    if (i == 0) {
        for (int u = A2GS_DISKS - 1; u >= 0; u--) apple2gs_eject(cpu_m, u);
        units = 0;
        comp_media[0] = 0;
    } else if (i == 1) apple2gs_reset_key(cpu_m);
    else apple2gs_reset(cpu_m);
}

static void frame(bool sound)
{
    const int16_t *audio;
    apple2gs_set_turbo(cpu_m, !sound);
    const int n = apple2gs_run_frame(cpu_m, &audio);
    if (sound) pico_port_audio(audio, n);
}

static void release(void)
{
    apple2gs_keys_clear(cpu_m);
    memset(held_prev, 0, sizeof held_prev);
}

static const char *const kExts[] = A2GS_MEDIA_EXTS;
static const comp_pico_menu kMenu = { "APPLE IIGS", kExts, load, item, item_do, frame, release };

static void game(void)
{
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x43) apple2gs_reset_key(cpu_m);              // F10
            if (k == 0x44) apple2gs_reset(cpu_m);                  // F11
            if (k == 0x42) comp_media_menu(&kMenu);                // F9
            if (k == 0x45) comp_toggle_turbo();                    // F12
        }
        bool held[128] = { false };
        adb_hid_held(&adb_hid_adb, pico_port_key, held);
        if (pico_port_key(0x48)) held[GSK_RESET] = true;           // Pause = RESET
        for (int c = 0; c < 128; c++) {
            if (held[c] == held_prev[c]) continue;
            held_prev[c] = held[c];
            apple2gs_key(cpu_m, c, held[c]);
        }
        int dx = 0, dy = 0;
        const uint32_t b = pico_port_mouse(&dx, &dy);
        apple2gs_mouse(cpu_m, dx, dy, (b & 3) != 0);
        frame(!comp_turbo);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_early();                         // (the PSRAM: the RAM goes there)
    cpu_m = apple2gs_create(64, A2GS_AUDIO_RATE);
    if (!apple2gs_rom_load()) pico_port_rom_error();
    if (!cpu_m || !a2gs_rom_fc || !a2gs_rom_fe || !a2gs_rom_chr) {
        printf(cpu_m ? "no Apple IIGS ROMs\n" : "out of memory (4 MB of PSRAM)\n");
        for (;;) {}
    }
    apple2gs_set_roms(cpu_m, a2gs_rom_fc, a2gs_rom_fe, a2gs_rom_chr);
    apple2gs_set_time(cpu_m, 0);
    apple2gs_reset(cpu_m);
    pico_port_cfg cfg = { "Apple IIGS", render, NULL, A2GS_AUDIO_RATE, A2GS_FPS, true };
    pico_port_open(&cfg);
    pico_port_run(game);
}
