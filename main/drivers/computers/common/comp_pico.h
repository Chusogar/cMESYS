// comp_pico.h - what the RP2350-PiZero frontends of the CPC, the C64 and the
// Lynx share (as zxspec_pico.c's): the F9 media menu over the picture (the
// microSD's files, the machine's own items, TURBO, close), with the machine
// kept running behind it. Included once by each <machine>_pico.c (static).
#pragma once
#include <stdio.h>
#include <string.h>
#include "pico_port.h"
#include "../ql/ql_sd.h"

typedef struct {
    const char *title;                          // "AMSTRAD CPC 6128"
    const char *const *exts;                    // the media files listed (NULL-ended)
    bool (*load)(const char *name, const char **why);
    const char *(*item)(int i);                 // the machine's own item i (NULL: no more)
    void (*item_do)(int i);
    void (*frame)(bool sound);                  // one frame of the machine (and its sound)
    void (*release)(void);                      // all keys up (before the menu)
} comp_pico_menu;

static bool comp_turbo;
static char comp_media[48];

static void comp_toggle_turbo(void)
{
    comp_turbo = !comp_turbo;
    printf("TURBO %s\n", comp_turbo ? "on" : "off");
    pico_osd_toast(comp_turbo ? "TURBO ON (F12)" : "TURBO OFF", 1500);
}

static void comp_media_menu(const comp_pico_menu *mm)
{
    static ql_sd_entry files[QL_SD_MAX_FILES];      // (not on the 4 KB stack)
    static const char *items[QL_SD_MAX_FILES + 12];
    static char title[PICO_OSD_COLS + 1];
    static const char help[] = "UP/DOWN  ENTER LOAD  ESC/F9 BACK";
    int nfiles = 0, nown = 0;
    if (mm->release) mm->release();
    bool sd_ok = ql_sd_mount();
    if (sd_ok) nfiles = ql_sd_list_ext(files, QL_SD_MAX_FILES, mm->exts);
    for (int i = 0; i < nfiles; i++) items[i] = files[i].name;
    while (mm->item && nown < 8 && mm->item(nown)) {
        items[nfiles + nown] = mm->item(nown);
        nown++;
    }
    items[nfiles + nown] = comp_turbo ? "TURBO: OFF" : "TURBO: ON";
    items[nfiles + nown + 1] = "CLOSE";
    const int n = nfiles + nown + 2;
    int sel = 0;
    snprintf(title, sizeof title, "%s - %s", mm->title,
             sd_ok ? (nfiles ? "MICROSD" : "NO MEDIA FILES ON THE MICROSD") : "NO MICROSD CARD");
    printf("\n=== MEDIA (%s) ===\nNow: %s\n", mm->title, comp_media[0] ? comp_media : "(none)");
    for (int i = 0; i < n; i++) printf("  %2d  %s\n", i, items[i]);
    pico_osd_list(title, items, n, sel, help);
    pico_osd_show(true);

    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            if (k == 0x52 || k == 0x51 || k == 0x4b || k == 0x4e) {  // up / down / page up / page down
                const int step = (k == 0x4b || k == 0x4e) ? PICO_OSD_ROWS - 4 : 1;
                sel += (k == 0x52 || k == 0x4b) ? -step : step;
                if (sel < 0) sel = 0;
                if (sel > n - 1) sel = n - 1;
                pico_osd_list(title, items, n, sel, help);
            } else if (k == 0x29 || k == 0x42) {                        // Esc / F9
                pico_osd_show(false);
                return;
            } else if (k == 0x28 || k == 0x58) {                        // Enter
                if (sel < nfiles) {
                    pico_osd_text(PICO_OSD_ROWS - 1, "LOADING...", true);
                    const char *why = "";
                    if (!mm->load(files[sel].name, &why)) {
                        char msg[PICO_OSD_COLS + 1];
                        printf("load failed: %s\n", why);
                        snprintf(msg, sizeof msg, "ERROR: %s", why && why[0] ? why : "CANNOT LOAD");
                        pico_osd_text(PICO_OSD_ROWS - 1, msg, true);
                        pico_port_mem_info(msg, sizeof msg);
                        pico_osd_text(PICO_OSD_ROWS - 2, msg, false);
                        continue;                                       // stay in the menu
                    }
                    snprintf(comp_media, sizeof comp_media, "%s", files[sel].name);
                    printf("media: %s\n", comp_media);
                } else if (sel < nfiles + nown) {
                    mm->item_do(sel - nfiles);
                } else if (sel == nfiles + nown) {
                    comp_toggle_turbo();
                }
                pico_osd_show(false);
                return;
            }
        }
        mm->frame(!comp_turbo);
        pico_port_frame_done();
    }
}
