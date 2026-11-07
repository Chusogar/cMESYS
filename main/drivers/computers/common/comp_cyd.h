// comp_cyd.h - what the ESP32 CYD frontends of the CPC, the C64 and the Lynx
// share (as zxspec_cyd.c's, made generic): the tasks, the touch zones, the
// media menu from the SD card, the help screen, the serial console keyboard,
// TURBO and a typist (a line typed into the machine a key at a time, for
// RUN" and LOAD"*",8,1 on a board with no keyboard). Included once (static).
//
//   Core 1 : the machine -> DAC GPIO26
//   Core 0 : 320x240 RGB565 -> ST7789, XPT2046 touch, Nunchuk, BOOT, serial
//
// Touch: a 4 x 3 grid of zones. The top-left one is MEDIA (hold: the SD
// menu; tap: the machine's tap action, the tape), the top-right one HELP;
// the machine names the other ten. BOOT: tap = the tap action, hold = menu.
// Serial console: the characters typed are typed into the machine; F12
// (ESC [ 2 4 ~) or Ctrl-T toggles TURBO.
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cyd.h"
#include "dac_audio.h"
#include "nunchuk.h"
#include "../ql/ql_sd.h"

#ifndef CYD_HEADLESS_TYPE
#define CYD_HEADLESS_TYPE "PRINT 6*7\r"
#endif

enum { CZ_MEDIA = 0, CZ_HELP = 3 };     // zones: row * 4 + column

// What the emulation task gets each frame (from the input task)
typedef struct {
    uint16_t zones;                     // bit z: zone z held (not MEDIA / HELP)
    bool up, down, left, right, fire, fire2;    // the Nunchuk (C = fire2)
} comp_cyd_in;

typedef struct {
    const char *tag, *nvs_ns, *title, *subtitle;
    uint8_t title_rgb[3];
    const char *const *exts;            // media files listed (NULL-ended)
    const char *ext_label;              // "SNA DSK CDT TZX CSW"
    int audio_rate;
    bool (*load)(const char *name, const char **why);   // (the emulation held)
    const char *(*item)(int i);         // the machine's menu items (NULL: no more)
    void (*item_do)(int i);             //   (run on the emulation task)
    void (*tap)(void);                  // MEDIA tapped (on the emulation task)
    void (*input)(const comp_cyd_in *in);  // keys / joystick before a frame
    int (*frame)(const int16_t **audio);
    void (*render)(int y, uint16_t *row);  // row y (0..239) of 320 pixels
    const char *zone_label[12];
    // the typist: the machine's keys for a character (up to 3; 0: none)
    int (*char_keys)(char c, int *keys);
    void (*key)(int code, bool down);
    void (*keys_clear)(void);
    const char *now;                    // the media loaded (shown in the menu)
    void (*colours)(void);              // after the setup: the palette in the panel's format
} comp_cyd_machine;

static const comp_cyd_machine *cm;
static char cyd_toast[40];
static volatile int64_t cyd_toast_until;
static volatile bool cyd_menu_req, cyd_help_req, cyd_tap_req, cyd_full_redraw, cyd_turbo;
static volatile int cyd_item_req = -1;
static volatile comp_cyd_in cyd_in;
static SemaphoreHandle_t cyd_frame_ready;
static volatile bool cyd_hold, cyd_held;
static char cyd_menu_msg[64];

// The typist: a line queued by the menu or the serial console
#define CYD_TYPE_MAX 64
static char cyd_type_buf[CYD_TYPE_MAX];
static volatile int cyd_type_head, cyd_type_tail;
static int cyd_type_phase;

static void cyd_show_toast(const char *s)
{
    snprintf(cyd_toast, sizeof cyd_toast, "%s", s);
    cyd_toast_until = esp_timer_get_time() + 2000000;
}

static void cyd_type(const char *s)
{
    for (; *s; s++) {
        const int next = (cyd_type_head + 1) % CYD_TYPE_MAX;
        if (next == cyd_type_tail) break;
        cyd_type_buf[cyd_type_head] = *s;
        cyd_type_head = next;
    }
}

// A character takes 3 frames down and 3 up; returns true while typing
static bool cyd_type_frame(void)
{
    if (cyd_type_tail == cyd_type_head) return false;
    const char c = cyd_type_buf[cyd_type_tail];
    int keys[3] = { 0, 0, 0 };
    const int n = cm->char_keys ? cm->char_keys(c, keys) : 0;
    if (cyd_type_phase < 3)
        for (int i = 0; i < n; i++) cm->key(keys[i], true);
    if (++cyd_type_phase >= 6) {
        cyd_type_phase = 0;
        cyd_type_tail = (cyd_type_tail + 1) % CYD_TYPE_MAX;
    }
    return true;
}

static void cyd_toggle_turbo(void)
{
    cyd_turbo = !cyd_turbo;
    cyd_show_toast(cyd_turbo ? "TURBO on" : "TURBO off");
}

static void cyd_draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int z = 0; z < 12; z++) {
        const int x = (z & 3) * 80, y = (z >> 2) * 80;
        const uint16_t bg = z == CZ_MEDIA ? rgb565(140, 30, 30) : z == CZ_HELP ? rgb565(30, 110, 40) :
                            (z >> 2) == 1 ? C_DGREY : rgb565(20, 60, 140);
        fill_rect(x + 1, y + 1, 78, 78, bg);
        const char *l = z == CZ_MEDIA ? "MEDIA" : z == CZ_HELP ? "HELP" : cm->zone_label[z];
        if (l) draw_text_c(x + 40, y + 36, l, 1, C_WHITE, bg);
    }
    draw_text_c(160, 4, cm->title, 1, C_YELLOW, C_DGREY);
    char s[48];
    snprintf(s, sizeof s, "%.24s  now: %.16s", cm->subtitle, cm->now && cm->now[0] ? cm->now : "(none)");
    draw_text_c(160, 230, s, 1, C_YELLOW, C_DGREY);
    lcd_flush();
}

// The emulation stops while the menu is up: nothing may run the machine
// while a file is loaded into it.
static void cyd_hold_emulation(bool on)
{
    cyd_hold = on;
    if (!on) return;
    for (int i = 0; i < 50 && !cyd_held; i++) vTaskDelay(pdMS_TO_TICKS(5));
}

static void cyd_wait_release(void)
{
    int x, y;
    while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
}

static void cyd_media_menu(void)
{
    static ql_sd_entry files[QL_SD_MAX_FILES];
    int nfiles = 0, nown = 0;
    const bool sd_ok = ql_sd_mount();
    if (sd_ok) nfiles = ql_sd_list_ext(files, QL_SD_MAX_FILES, cm->exts);
    while (cm->item && nown < 8 && cm->item(nown)) nown++;
    const int n = nfiles + nown + 2;
    int sel = 0, scroll = 0;
    const int visible = 8;
    cyd_hold_emulation(true);
    cyd_menu_msg[0] = 0;
    for (;;) {
        fill_rect(0, 0, 320, 240, C_BLACK);
        char head[64];
        snprintf(head, sizeof head, "MEDIA: %s", cm->ext_label);
        draw_text_c(160, 4, head, 1, C_YELLOW, C_BLACK);
        char cur[96];
        snprintf(cur, sizeof cur, "Now: %.24s  mem %uK/%uK", cm->now && cm->now[0] ? cm->now : "(none)",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
        draw_text_c(160, 16, cur, 1, C_CYAN, C_BLACK);
        if (!sd_ok) {
            char e[64];
            snprintf(e, sizeof e, "No SD card (FAT32): %.36s", ql_sd_error());
            draw_text_c(160, 28, e, 1, C_RED, C_BLACK);
        } else if (cyd_menu_msg[0]) draw_text_c(160, 28, cyd_menu_msg, 1, C_RED, C_BLACK);
        else if (!nfiles) draw_text_c(160, 28, "No media files on the SD card", 1, C_GREY, C_BLACK);
        else draw_text_c(160, 28, "SD: tap a file to load", 1, C_GREY, C_BLACK);

        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;
        for (int i = 0; i < visible; i++) {
            const int idx = scroll + i;
            if (idx >= n) break;
            char label[40];
            if (idx < nfiles) snprintf(label, sizeof label, "%.36s", files[idx].name);
            else if (idx < nfiles + nown) snprintf(label, sizeof label, "%.36s", cm->item(idx - nfiles));
            else if (idx == nfiles + nown) snprintf(label, sizeof label, "TURBO %s", cyd_turbo ? "off" : "on");
            else snprintf(label, sizeof label, "Close");
            const uint16_t bg = idx == sel ? rgb565(40, 90, 160) : C_DGREY;
            fill_rect(20, 44 + i * 22, 280, 20, bg);
            draw_text(28, 48 + i * 22, label, 1, C_WHITE, bg);
        }
        // scroll arrows when the list is longer than the screen
        if (n > visible) {
            draw_text(304, 48, "^", 1, C_YELLOW, C_BLACK);
            draw_text(304, 48 + (visible - 1) * 22, "v", 1, C_YELLOW, C_BLACK);
        }
        draw_text_c(160, 228, "tap row to load  bottom=close", 1, C_YELLOW, C_BLACK);
        lcd_flush();

        int x = 0, y = 0;
        while (!touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
        const int row = (y - 44) / 22;
        if (x >= 300 && y >= 44 && n > visible) {           // the arrows: a page up / down
            cyd_wait_release();
            if (row < visible / 2) scroll = scroll > visible ? scroll - visible : 0;
            else if (scroll + visible < n) scroll += visible;
            if (scroll > n - visible) scroll = n - visible;
            sel = scroll;
            continue;
        }
        if (y >= 44 && row >= 0 && row < visible) {
            const int idx = scroll + row;
            cyd_wait_release();
            if (idx >= n) continue;
            sel = idx;
            if (sel < nfiles) {
                fill_rect(20, 44 + (sel - scroll) * 22, 280, 20, rgb565(140, 110, 20));
                draw_text(28, 48 + (sel - scroll) * 22, "Loading...", 1, C_WHITE, rgb565(140, 110, 20));
                lcd_flush();
                const char *why = "";
                if (!cm->load(files[sel].name, &why)) {
                    snprintf(cyd_menu_msg, sizeof cyd_menu_msg, "%.40s (%uK/%uK)", why && why[0] ? why : "SD read error",
                             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
                    ESP_LOGW(cm->tag, "%s: %s", files[sel].name, cyd_menu_msg);
                    continue;
                }
                cyd_show_toast(files[sel].name);
            } else if (sel < nfiles + nown) {
                cyd_item_req = sel - nfiles;
            } else if (sel == nfiles + nown) {
                cyd_toggle_turbo();
            }
            break;
        } else if (y > 220) {
            cyd_wait_release();
            break;
        }
        cyd_wait_release();
    }
    cyd_full_redraw = true;
    cyd_hold_emulation(false);
}

static void cyd_input_task(void *arg)
{
    (void)arg;
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    bool boot_was = false, touch_was = false;
    int touch_zone = -1;
    int64_t touch_t0 = 0, boot_t0 = 0;
    for (;;) {
        comp_cyd_in in;
        memset(&in, 0, sizeof in);
        const int64_t now = esp_timer_get_time();
#ifdef CYD_HEADLESS_TEST
        // (QEMU: no touch, the BOOT pin reads low) a line typed now and then
        if (now > 6000000 && now < 6100000 && cyd_type_head == cyd_type_tail) cyd_type(CYD_HEADLESS_TYPE);
        cyd_in = in;
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
#endif
        int x, y;
        const bool t = touch_get(&x, &y);
        if (t) {
            int c = x / 80, r = y / 80;
            c = c < 0 ? 0 : c > 3 ? 3 : c;
            r = r < 0 ? 0 : r > 2 ? 2 : r;
            const int z = r * 4 + c;
            if (!touch_was) {
                touch_zone = z;
                touch_t0 = now;
            }
            if (touch_zone > 0 && touch_zone != CZ_HELP) in.zones |= (uint16_t)(1u << touch_zone);
            if (touch_zone == CZ_HELP && now - touch_t0 > 100000 && !cyd_help_req) {
                cyd_help_req = true;
                touch_zone = -9;
            }
            if (touch_zone == CZ_MEDIA && now - touch_t0 > 800000 && !cyd_menu_req) {
                cyd_menu_req = true;
                touch_zone = -9;
            }
        } else if (touch_was && touch_zone == CZ_MEDIA) {
            cyd_tap_req = true;
        }
        touch_was = t;

        const bool boot = gpio_get_level(PIN_BOOT_BTN) == 0;
        if (boot && !boot_was) boot_t0 = now;
        if (boot && boot_was && now - boot_t0 > 800000 && !cyd_menu_req) {
            cyd_menu_req = true;
            boot_t0 = now + 10000000;
        }
        if (!boot && boot_was && now - boot_t0 < 800000) cyd_tap_req = true;
        boot_was = boot;

        nunchuk_t nc;
        if (nunchuk_read(&nc) && nc.ok) {
            in.left = nc_left(&nc);
            in.right = nc_right(&nc);
            in.up = nc_up(&nc);
            in.down = nc_down(&nc);
            in.fire = nc.z;
            in.fire2 = nc.c;
        }

        uint8_t ch;
        static char esc[8];
        static int nesc;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            if (ch == 0x1b) { nesc = 1; esc[0] = 0x1b; continue; }
            if (nesc) {
                if (nesc < (int)sizeof esc - 1) esc[nesc++] = (char)ch;
                esc[nesc] = 0;
                if (ch == '~' || nesc >= (int)sizeof esc - 1) {
                    if (!strcmp(esc, "\x1b[24~")) cyd_toggle_turbo();
                    nesc = 0;
                }
                continue;
            }
            if (ch == 0x14) { cyd_toggle_turbo(); continue; }
            const char s[2] = { ch == '\n' ? '\r' : (char)ch, 0 };
            cyd_type(s);
        }
        cyd_in = in;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void cyd_video_task(void *arg)
{
    (void)arg;
    bool toast_shown = false;
    for (;;) {
        xSemaphoreTake(cyd_frame_ready, portMAX_DELAY);
        if (cyd_help_req) {
            cyd_draw_help();
            int x, y;
            cyd_wait_release();
            const int64_t until = esp_timer_get_time() + 8000000;
            while (!touch_get(&x, &y) && esp_timer_get_time() < until) vTaskDelay(pdMS_TO_TICKS(20));
            cyd_wait_release();
            cyd_help_req = false;
            cyd_full_redraw = true;
        }
        if (cyd_menu_req) {
            cyd_media_menu();
            cyd_menu_req = false;
        }
        cyd_full_redraw = false;
        for (int y0 = 0; y0 < 240; y0 += BAND) {
            uint16_t *buf = lcd_get_buf();
            for (int dy = 0; dy < BAND; dy++) cm->render(y0 + dy, buf + dy * 320);
            lcd_blit(0, y0, 320, BAND, buf);
        }
        const bool toast_on = esp_timer_get_time() < cyd_toast_until;
        if (toast_on) {
            const int w = (int)strlen(cyd_toast) * 6 + 12;
            fill_rect(160 - w / 2, 4, w, 16, rgb565(140, 30, 30));
            draw_text_c(160, 8, cyd_toast, 1, C_WHITE, rgb565(140, 30, 30));
            lcd_flush();
        }
        toast_shown = toast_on;
        (void)toast_shown;
    }
}

static void cyd_emu_task(void *arg)
{
    (void)arg;
    static uint8_t a8[2048];
    static const int gain_shift[5] = { 0, 7, 6, 5, 4 };
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0;
    for (;;) {
        if (cyd_hold) {                     // the menu is up
            cyd_held = true;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        cyd_held = false;
        if (cyd_tap_req) {
            cyd_tap_req = false;
            if (cm->tap) cm->tap();
        }
        if (cyd_item_req >= 0) {
            const int i = cyd_item_req;
            cyd_item_req = -1;
            cm->item_do(i);
        }
        comp_cyd_in in = cyd_in;
        cm->input(&in);
        cyd_type_frame();                   // (after the input: its keys add to them)

        const int64_t t0 = esp_timer_get_time();
        const int16_t *pcm;
        int n = cm->frame(&pcm);
        if (n > (int)sizeof a8) n = sizeof a8;
        const int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < n; i++) {
            const int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
        if (cyd_turbo) {
            static unsigned tc;
            if ((++tc & 3) == 0) xSemaphoreGive(cyd_frame_ready);
            if ((tc & 15) == 0) vTaskDelay(1);
        } else {
            xSemaphoreGive(cyd_frame_ready);
            dac_audio_push(a8, n);
        }
        const int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(cm->tag, "emu %.1f fps (cpu1 %.0f%%), media %s, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t), 100.0 * stat_busy / (double)(now - stat_t),
                     cm->now && cm->now[0] ? cm->now : "(none)",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now;
            stat_frames = stat_busy = 0;
        }
    }
}

// After the machine is made: the display, the touch setup, the help screen,
// the sound and the tasks
static void comp_cyd_start(const comp_cyd_machine *machine)
{
    cm = machine;
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_FULL)
    cyd_display_start(true);
#else
    cyd_display_start(false);
#endif
#ifndef CYD_HEADLESS_TEST
    cyd_setup_ui();
#endif
    set_landscape(true);
    if (cm->colours) cm->colours();
#ifndef CYD_HEADLESS_TEST
    cyd_draw_help();
    {
        int x, y;
        const int64_t until = esp_timer_get_time() + 5000000;
        while (!touch_get(&x, &y) && esp_timer_get_time() < until) vTaskDelay(pdMS_TO_TICKS(20));
        cyd_wait_release();
    }
#endif
    fill_rect(0, 0, 320, 240, C_BLACK);
    lcd_flush();
    dac_audio_set_rate((uint32_t)cm->audio_rate);
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start((uint32_t)cm->audio_rate);
#endif
    cyd_frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(cm->tag, "%s: free heap %u, largest block %u", cm->title,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (xTaskCreatePinnedToCore(cyd_input_task, "input", 4096, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(cm->tag, "input task failed");
    if (xTaskCreatePinnedToCore(cyd_video_task, "video", 6144, NULL, 6, NULL, 0) != pdPASS)
        ESP_LOGE(cm->tag, "video task failed");
    if (xTaskCreatePinnedToCore(cyd_emu_task, "emu", 8192, NULL, 10, NULL, 1) != pdPASS)
        ESP_LOGE(cm->tag, "emu task failed");
}
