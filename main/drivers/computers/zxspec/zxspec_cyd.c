// zxspec_cyd.c - ZX Spectrum family on the ESP32 CYD.
// Build with -DZX_MODEL=ZX_SPEC48|ZX_SPEC128|ZX_PLUS3|ZX_PENTAGON|ZX_SCORPION
//
//   Core 1 : Z80 + ULA + tape/AY -> DAC GPIO26
//   Core 0 : 320x240 RGB565 -> ST7789, XPT2046 touch, Nunchuk, BOOT, serial
//
// Touch zones (hold MEDIA for the SD menu; short tap = tape play/stop):
//     MEDIA | 1    | 2    | HELP
//     LEFT  | UP   | DOWN | RIGHT
//     CAPS  | SPACE| ENTER| FIRE
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
#include "zxspec_core.h"
#include "rom_data.h"
#include "../ql/ql_sd.h"

#ifndef ZX_MODEL
#define ZX_MODEL ZX_SPEC48
#endif

static const char *TAG = "zxspec";
static zxspec *cpu_m;
static char media_name[40];
static char toast[40];
static volatile int64_t toast_until;
static volatile bool media_menu_req, help_req, tape_toggle_req, full_redraw;
static volatile uint8_t in_keys[8];
static volatile uint8_t in_joy;
static SemaphoreHandle_t frame_ready;
static volatile uint32_t frames_shown, video_busy_us;
static volatile uint32_t diag_emu_frames, diag_vid_frames;

static void show_toast(const char *s)
{
    strncpy(toast, s, sizeof toast - 1);
    toast[sizeof toast - 1] = 0;
    toast_until = esp_timer_get_time() + 2000000;
}

enum { Z_MEDIA = -1, Z_HELP = -2, Z_FIRE = -3 };
static const int zone_key[3][4] = {
    { Z_MEDIA, ZXK_1,     ZXK_2,      Z_HELP },
    { ZXK_5,   ZXK_7,     ZXK_6,      ZXK_8 },   /* left/up/down/right via Caps in emu */
    { ZXK_CAPS, ZXK_SPACE, ZXK_ENTER, Z_FIRE },
};
static const char *zone_label[3][4] = {
    { "MEDIA", "1", "2", "HELP" },
    { "LEFT", "UP", "DOWN", "RIGHT" },
    { "CAPS", "SPACE", "ENTER", "FIRE" },
};
/* Cursor rows use Caps+5/6/7/8; zone_key mid-row stores the digit; Caps is OR'd. */
static const bool zone_need_caps[3][4] = {
    { false, false, false, false },
    { true, true, true, true },
    { false, false, false, false },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) {
            int x = c * 80, y = r * 80;
            uint16_t bg = zone_key[r][c] == Z_MEDIA ? rgb565(140, 30, 30) :
                          zone_key[r][c] == Z_HELP ? rgb565(30, 110, 40) :
                          zone_key[r][c] == Z_FIRE ? rgb565(20, 60, 140) :
                          r == 1 ? C_DGREY : rgb565(20, 60, 140);
            fill_rect(x + 1, y + 1, 78, 78, bg);
            draw_text_c(x + 40, y + 36, zone_label[r][c], 1, C_WHITE, bg);
        }
    draw_text_c(160, 4, zxspec_title(ZX_MODEL), 1, C_YELLOW, C_DGREY);
    char s[48];
    snprintf(s, sizeof s, "MEDIA hold=SD  tap=tape  now: %.16s",
             media_name[0] ? media_name : "(none)");
    draw_text_c(160, 230, s, 1, C_YELLOW, C_DGREY);
    lcd_flush();
}

static bool load_sd_name(const char *name)
{
    size_t sz = 0;
    uint8_t *buf = ql_sd_load_file(name, &sz);
    if (!buf) return false;
    bool ok = zxspec_load_mem(cpu_m, buf, sz, name);
    free(buf);
    if (ok) {
        snprintf(media_name, sizeof media_name, "%s", name);
        show_toast(media_name);
    }
    return ok;
}

static void run_media_menu(void)
{
    static const char *const exts[] = { ".tap", ".tzx", ".sna", NULL };
    ql_sd_entry files[QL_SD_MAX_FILES];
    int nfiles = 0;
    bool sd_ok = ql_sd_mount();
    if (sd_ok) nfiles = ql_sd_list_ext(files, QL_SD_MAX_FILES, exts);
    int n = nfiles + 2;
    int sel = 0, scroll = 0;
    const int visible = 8;
    for (;;) {
        fill_rect(0, 0, 320, 240, C_BLACK);
        draw_text_c(160, 4, "MEDIA (.tap/.tzx/.sna)", 1, C_YELLOW, C_BLACK);
        char cur[48];
        snprintf(cur, sizeof cur, "Now: %s%s", media_name[0] ? media_name : "(none)",
                 zxspec_tape_playing(cpu_m) ? " [PLAY]" : "");
        draw_text_c(160, 16, cur, 1, C_CYAN, C_BLACK);
        if (!sd_ok) draw_text_c(160, 28, "No SD card (FAT32)", 1, C_RED, C_BLACK);
        else if (!nfiles) draw_text_c(160, 28, "No files in SD root /tape", 1, C_GREY, C_BLACK);
        else draw_text_c(160, 28, "SD: tap a file to load", 1, C_GREY, C_BLACK);

        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;
        for (int i = 0; i < visible; i++) {
            int idx = scroll + i;
            if (idx >= n) break;
            char label[40];
            if (idx < nfiles) snprintf(label, sizeof label, "%.36s", files[idx].name);
            else if (idx == nfiles) snprintf(label, sizeof label, "Tape play / stop");
            else snprintf(label, sizeof label, "Close");
            uint16_t bg = (idx == sel) ? rgb565(40, 90, 160) : C_DGREY;
            fill_rect(20, 44 + i * 22, 280, 20, bg);
            draw_text(28, 48 + i * 22, label, 1, C_WHITE, bg);
        }
        draw_text_c(160, 228, "tap row to load  bottom=close", 1, C_YELLOW, C_BLACK);
        lcd_flush();

        int x = 0, y = 0;
        while (!touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
        int row = (y - 44) / 22;
        if (y >= 44 && row >= 0 && row < visible) {
            int idx = scroll + row;
            if (idx >= n) {
                while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            sel = idx;
            while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
            if (sel < nfiles) {
                if (!load_sd_name(files[sel].name)) {
                    show_toast("SD load failed");
                    continue;
                }
            } else if (sel == nfiles) {
                if (zxspec_tape_playing(cpu_m)) {
                    zxspec_tape_stop(cpu_m);
                    show_toast("Tape stopped");
                } else if (zxspec_tape_loaded(cpu_m)) {
                    zxspec_tape_play(cpu_m);
                    show_toast("Tape playing");
                } else {
                    show_toast("No tape loaded");
                    continue;
                }
            }
            full_redraw = true;
            return;
        } else if (y > 220) {
            while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
            full_redraw = true;
            return;
        } else {
            while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void input_task(void *arg)
{
    (void)arg;
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    bool boot_was = false, touch_was = false;
    int touch_zone = 0;
    int64_t touch_t0 = 0;
    for (;;) {
        uint8_t k[8];
        memset(k, 0xff, sizeof k);
        uint8_t joy = 0;
        int64_t now = esp_timer_get_time();
#define PRESS(code) do { \
            int _r = (code) >> 3, _b = (code) & 7; \
            k[_r] = (uint8_t)(k[_r] & ~(1u << _b)); \
        } while (0)
        int x, y;
        bool t = touch_get(&x, &y);
        if (t) {
            int c = x / 80, r = y / 80;
            if (c > 3) c = 3;
            if (r > 2) r = 2;
            if (c < 0) c = 0;
            if (r < 0) r = 0;
            int z = zone_key[r][c];
            if (!touch_was) {
                touch_zone = z;
                touch_t0 = now;
            }
            if (touch_zone >= 0) {
                PRESS(touch_zone);
                if (zone_need_caps[r][c]) PRESS(ZXK_CAPS);
            }
            if (touch_zone == Z_FIRE) joy |= 16;
            if (touch_zone == Z_HELP && now - touch_t0 > 100000 && !help_req) {
                help_req = true;
                touch_zone = -9;
            }
            if (touch_zone == Z_MEDIA && now - touch_t0 > 800000 && !media_menu_req) {
                media_menu_req = true;
                touch_zone = -9;
            }
        } else if (touch_was && touch_zone == Z_MEDIA) {
            tape_toggle_req = true;
        }
        touch_was = t;

        bool boot = gpio_get_level(PIN_BOOT_BTN) == 0;
        static int64_t boot_t0;
        if (boot && !boot_was) boot_t0 = now;
        if (boot && boot_was && now - boot_t0 > 800000 && !media_menu_req) {
            media_menu_req = true;
            boot_t0 = now + 10000000;
        }
        if (!boot && boot_was && now - boot_t0 < 800000) tape_toggle_req = true;
        boot_was = boot;

        nunchuk_t nc;
        if (nunchuk_read(&nc) && nc.ok) {
            if (nc_left(&nc)) { PRESS(ZXK_CAPS); PRESS(ZXK_5); }
            if (nc_right(&nc)) { PRESS(ZXK_CAPS); PRESS(ZXK_8); }
            if (nc_up(&nc)) { PRESS(ZXK_CAPS); PRESS(ZXK_7); }
            if (nc_down(&nc)) { PRESS(ZXK_CAPS); PRESS(ZXK_6); }
            if (nc.z) joy |= 16;
            if (nc.c) PRESS(ZXK_SPACE);
            if (nc.c && nc.z) PRESS(ZXK_ENTER);
        }

        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 'A');
            static const struct { char c; int k; } map[] = {
                { 'Z', ZXK_Z }, { 'X', ZXK_X }, { 'C', ZXK_C }, { 'V', ZXK_V },
                { 'A', ZXK_A }, { 'S', ZXK_S }, { 'D', ZXK_D }, { 'F', ZXK_F }, { 'G', ZXK_G },
                { 'Q', ZXK_Q }, { 'W', ZXK_W }, { 'E', ZXK_E }, { 'R', ZXK_R }, { 'T', ZXK_T },
                { '1', ZXK_1 }, { '2', ZXK_2 }, { '3', ZXK_3 }, { '4', ZXK_4 }, { '5', ZXK_5 },
                { '0', ZXK_0 }, { '9', ZXK_9 }, { '8', ZXK_8 }, { '7', ZXK_7 }, { '6', ZXK_6 },
                { 'P', ZXK_P }, { 'O', ZXK_O }, { 'I', ZXK_I }, { 'U', ZXK_U }, { 'Y', ZXK_Y },
                { 'L', ZXK_L }, { 'K', ZXK_K }, { 'J', ZXK_J }, { 'H', ZXK_H },
                { 'M', ZXK_M }, { 'N', ZXK_N }, { 'B', ZXK_B },
                { ' ', ZXK_SPACE }, { '\r', ZXK_ENTER }, { '\n', ZXK_ENTER },
            };
            for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
                if (map[i].c == (char)ch) { PRESS(map[i].k); break; }
        }
#undef PRESS
#ifdef CYD_HEADLESS_TEST
        memset(k, 0xff, sizeof k);
        joy = 0;
#endif
        for (int i = 0; i < 8; i++) in_keys[i] = k[i];
        in_joy = joy;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void render_band(int y0)
{
    uint16_t *buf = lcd_get_buf();
    for (int dy = 0; dy < BAND; dy++)
        zxspec_render_row565(cpu_m, y0 + dy, buf + dy * 320);
    lcd_blit(0, y0, 320, BAND, buf);
}

static void video_task(void *arg)
{
    (void)arg;
    bool toast_shown = false;
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        if (help_req) {
            draw_help();
            int x, y;
            while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
            int64_t until = esp_timer_get_time() + 8000000;
            while (!touch_get(&x, &y) && esp_timer_get_time() < until) vTaskDelay(pdMS_TO_TICKS(20));
            while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
            help_req = false;
            full_redraw = true;
        }
        if (media_menu_req) {
            run_media_menu();
            media_menu_req = false;
            full_redraw = true;
        }
        bool all = full_redraw;
        full_redraw = false;
        int64_t now = esp_timer_get_time();
        bool toast_on = now < toast_until;
        if (toast_shown && !toast_on) all = true;
        if (all) {
            for (int y0 = 0; y0 < 240; y0 += BAND) render_band(y0);
        } else {
            /* Spectrum ULA redraws the whole frame each tick; always refresh. */
            for (int y0 = 0; y0 < 240; y0 += BAND) render_band(y0);
        }
        if (toast_on) {
            int w = (int)strlen(toast) * 6 + 12;
            fill_rect(160 - w / 2, 4, w, 16, rgb565(140, 30, 30));
            draw_text_c(160, 8, toast, 1, C_WHITE, rgb565(140, 30, 30));
            lcd_flush();
        }
        toast_shown = toast_on;
        frames_shown++;
        diag_vid_frames++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

static void emu_task(void *arg)
{
    (void)arg;
    static uint8_t a8[ZX_AUDIO_MAX];
    static const int gain_shift[5] = { 0, 7, 6, 5, 4 };
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0;

    for (;;) {
        if (tape_toggle_req) {
            tape_toggle_req = false;
            if (zxspec_tape_playing(cpu_m)) {
                zxspec_tape_stop(cpu_m);
                show_toast("Tape stopped");
            } else if (zxspec_tape_loaded(cpu_m)) {
                zxspec_tape_play(cpu_m);
                show_toast("Tape playing");
            } else {
                show_toast("No tape");
            }
        }
        /* Copy host matrix (active-low bits) into core. */
        zxspec_keys_clear(cpu_m);
        for (int r = 0; r < 8; r++) {
            uint8_t bits = in_keys[r];
            for (int b = 0; b < 5; b++)
                if (!(bits & (1u << b))) zxspec_key(cpu_m, ZX_KEY(r, b), true);
        }
        zxspec_joy(cpu_m, in_joy);

        int64_t t0 = esp_timer_get_time();
        const int16_t *pcm;
        int n = zxspec_run_frame(cpu_m, &pcm);
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < n; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
        diag_emu_frames++;
        xSemaphoreGive(frame_ready);
        dac_audio_push(a8, n);

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%), video %.0f%%, shown %.1f fps, media %s, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     media_name[0] ? media_name : "(none)",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; video_busy_us = 0;
        }
    }
}

void app_main(void)
{
    static cyd_game_t info = {
        "zxspec", NULL, "MEDIA: TAPE / HOLD=SD", {255, 180, 40}
    };
    info.title = zxspec_title(ZX_MODEL);
    cyd_boot(&info);

    cpu_m = zxspec_create(ZX_MODEL);
    if (!cpu_m) {
        ESP_LOGE(TAG, "out of memory for Spectrum");
        abort();
    }
    zxspec_set_rom(cpu_m, zx_rom48, zx_rom128, zx_rom128_bytes);
    zxspec_reset(cpu_m);

#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_FULL)
    cyd_display_start(true);
#else
    cyd_display_start(false);
#endif
#ifndef CYD_HEADLESS_TEST
    cyd_setup_ui();
#endif
    set_landscape(true);
#ifndef CYD_HEADLESS_TEST
    draw_help();
    {
        int x, y;
        int64_t until = esp_timer_get_time() + 5000000;
        while (!touch_get(&x, &y) && esp_timer_get_time() < until) vTaskDelay(pdMS_TO_TICKS(20));
        while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif
    fill_rect(0, 0, 320, 240, C_BLACK);
    lcd_flush();
    full_redraw = true;

    dac_audio_set_rate(ZX_AUDIO_RATE);
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(ZX_AUDIO_RATE);
#endif
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "%s rom=%s free heap=%u", zxspec_title(ZX_MODEL), zx_rom_name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    if (xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "input task failed");
    if (xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "video task failed");
    if (xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1) != pdPASS)
        ESP_LOGE(TAG, "emu task failed");
}
