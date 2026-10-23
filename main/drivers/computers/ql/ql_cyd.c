// ql_cyd.c - Sinclair QL on the ESP32 CYD. Core: ql_core.c (C port of
// dsp-cpp's ql driver).
//
//   Core 1 : 68008 (Musashi) + 8749 IPC + ZX8302 + microdrives, speaker -> DAC GPIO26
//   Core 0 : ZX8301 picture -> ST7789 (only the lines written), XPT2046 touch,
//            Nunchuk, BOOT button, serial keyboard
//
// Landscape 320x240. The 512x256 picture is shown whole (scaled with
// averaging, VIEW = FIT) or at 1:1 (VIEW = left / right 320 columns).
//
// Controls: the touch screen in 4x3 invisible zones (shown at start; hold
// VIEW for a second to see them again):
//     MDV   | F1    | F2    | VIEW
//     LEFT  | UP    | DOWN  | RIGHT
//     ESC   | SPACE | ENTER | F3
// MDV (or the BOOT button) mounts / unmounts the cartridge built into the
// firmware in mdv1: the QL is reset and boots from it (the red LED shows the
// microdrive motor). Nunchuk: stick = cursor keys, Z = ENTER, C = SPACE,
// C+Z = ESC. Serial (115200): type on the QL keyboard (arrows, ESC, ESC O P..S
// = F1..F4, Backspace = CTRL+LEFT).
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
#include "ql_core.h"
#include "ql_view.h"
#include "rom_data.h"

static const char *TAG = "ql";

// ----------------------------------------------------------------------------
// Shared state
// ----------------------------------------------------------------------------
static volatile int view = QL_VIEW_FIT;
static volatile uint8_t in_keys[8];          // QL matrix from the input task
static volatile bool mount_req;               // MDV / BOOT pressed
static volatile bool help_req;                // show the zones
static volatile bool full_redraw;
static char toast[40];
static volatile int64_t toast_until;
static portMUX_TYPE dirty_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t dirty_rows[8];                // source rows written (256 bits)
static SemaphoreHandle_t frame_ready;
static volatile uint32_t frames_shown, video_busy_us;

static void show_toast(const char *s)
{
    strncpy(toast, s, sizeof toast - 1);
    toast_until = esp_timer_get_time() + 2000000;
}

// ----------------------------------------------------------------------------
// Input
// ----------------------------------------------------------------------------
enum { Z_MDV = -1, Z_VIEW = -2 };
static const int zone_key[3][4] = {
    { Z_MDV,   QLK_F1,    QLK_F2,    Z_VIEW },
    { QLK_LEFT, QLK_UP,   QLK_DOWN,  QLK_RIGHT },
    { QLK_ESC, QLK_SPACE, QLK_ENTER, QLK_F3 },
};
static const char *zone_label[3][4] = {
    { "MDV", "F1", "F2", "VIEW" },
    { "LEFT", "UP", "DOWN", "RIGHT" },
    { "ESC", "SPACE", "ENTER", "F3" },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) {
            int x = c * 80, y = r * 80;
            uint16_t bg = zone_key[r][c] == Z_MDV ? rgb565(140, 30, 30) :
                          zone_key[r][c] == Z_VIEW ? rgb565(30, 110, 40) :
                          r == 1 ? C_DGREY : rgb565(20, 60, 140);
            fill_rect(x + 1, y + 1, 78, 78, bg);
            draw_text_c(x + 40, y + 36, zone_label[r][c], 1, C_WHITE, bg);
        }
    draw_text_c(160, 4, "SINCLAIR QL - TOUCH ZONES", 1, C_YELLOW, C_DGREY);
    char s[48];
    snprintf(s, sizeof s, "MDV: %s", ql_mdv_image ? ql_mdv_name : "(none)");
    draw_text_c(160, 230, s, 1, C_YELLOW, C_DGREY);
    lcd_flush();
}

// Serial typing: a queue of keys, each held ~100 ms (modifiers first)
typedef struct { uint8_t key, shift, ctrl; } typed_t;
static typed_t tq[64];
static int tq_head, tq_tail;

static void type_key(int key, bool shift, bool ctrl)
{
    int n = (tq_tail + 1) & 63;
    if (n == tq_head) return;
    tq[tq_tail] = (typed_t){ (uint8_t)key, shift, ctrl };
    tq_tail = n;
}

static void type_char(uint8_t ch)
{
    static const char *const unshifted = "1234567890-=[];'\\,./`";
    static const char *const shifted   = "!@#$%^&*()_+{}:\"|<>?~";
    static const uint8_t sym_key[] = {
        QLK_1, QLK_2, QLK_3, QLK_4, QLK_5, QLK_6, QLK_7, QLK_8, QLK_9, QLK_0, QLK_MINUS, QLK_EQUALS,
        QLK_LBRACKET, QLK_RBRACKET, QLK_SEMICOLON, QLK_QUOTE, QLK_BACKSLASH, QLK_COMMA, QLK_PERIOD,
        QLK_SLASH, QLK_POUND,
    };
    static const uint8_t letter_key[26] = {
        QLK_A, QLK_B, QLK_C, QLK_D, QLK_E, QLK_F, QLK_G, QLK_H, QLK_I, QLK_J, QLK_K, QLK_L, QLK_M,
        QLK_N, QLK_O, QLK_P, QLK_Q, QLK_R, QLK_S, QLK_T, QLK_U, QLK_V, QLK_W, QLK_X, QLK_Y, QLK_Z,
    };
    if (ch >= 'a' && ch <= 'z') { type_key(letter_key[ch - 'a'], false, false); return; }
    if (ch >= 'A' && ch <= 'Z') { type_key(letter_key[ch - 'A'], true, false); return; }
    if (ch == '\r' || ch == '\n') { type_key(QLK_ENTER, false, false); return; }
    if (ch == ' ') { type_key(QLK_SPACE, false, false); return; }
    if (ch == '\t') { type_key(QLK_TAB, false, false); return; }
    if (ch == 8 || ch == 127) { type_key(QLK_LEFT, false, true); return; }
    const char *p = strchr(unshifted, ch);
    if (p && ch) { type_key(sym_key[p - unshifted], false, false); return; }
    p = strchr(shifted, ch);
    if (p && ch) type_key(sym_key[p - shifted], true, false);
}

static void input_task(void *arg)
{
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    bool boot_was = false, touch_was = false;
    int touch_zone = 0;
    int64_t touch_t0 = 0, esc_t = 0;
    int esc = 0;                                  // 0, 1 after ESC, 2 after ESC [, 3 after ESC O
    int typing = 0;                               // ticks into the current typed key
    for (;;) {
        uint8_t k[8] = {0};
        int64_t now = esp_timer_get_time();
#define PRESS(code) (k[(code) >> 3] |= (uint8_t)(1u << ((code) & 7)))
        // touch
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
                if (z == Z_MDV) mount_req = true;
            }
            if (touch_zone >= 0) PRESS(touch_zone);
            if (touch_zone == Z_VIEW && now - touch_t0 > 800000 && !help_req) {
                help_req = true;
                touch_zone = -3;                  // used up
            }
        } else if (touch_was && touch_zone == Z_VIEW) {
            view = (view + 1) % QL_VIEW_N;
            full_redraw = true;
            show_toast(view == QL_VIEW_FIT ? "VIEW: FIT" : view == QL_VIEW_LEFT ? "VIEW: 1:1 LEFT" : "VIEW: 1:1 RIGHT");
        }
        touch_was = t;
        // BOOT button: MDV
        bool boot = gpio_get_level(PIN_BOOT_BTN) == 0;
        if (boot && !boot_was) mount_req = true;
        boot_was = boot;
        // Nunchuk
        nunchuk_t nc;
        if (nunchuk_read(&nc) && nc.ok) {
            if (nc_left(&nc)) PRESS(QLK_LEFT);
            if (nc_right(&nc)) PRESS(QLK_RIGHT);
            if (nc_up(&nc)) PRESS(QLK_UP);
            if (nc_down(&nc)) PRESS(QLK_DOWN);
            if (nc.c && nc.z) PRESS(QLK_ESC);
            else if (nc.z) PRESS(QLK_ENTER);
            else if (nc.c) PRESS(QLK_SPACE);
        }
        // serial
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            if (esc == 1) {
                if (ch == '[') { esc = 2; continue; }
                if (ch == 'O') { esc = 3; continue; }
                type_key(QLK_ESC, false, false);
                esc = 0;
            } else if (esc == 2 || esc == 3) {
                static const uint8_t arrows[4] = { QLK_UP, QLK_DOWN, QLK_RIGHT, QLK_LEFT };
                static const uint8_t fkeys[4] = { QLK_F1, QLK_F2, QLK_F3, QLK_F4 };
                if (esc == 2 && ch >= 'A' && ch <= 'D') type_key(arrows[ch - 'A'], false, false);
                if (esc == 3 && ch >= 'P' && ch <= 'S') type_key(fkeys[ch - 'P'], false, false);
                if (ch >= 0x40 && ch <= 0x7e) esc = 0;   // end of the sequence
                continue;
            }
            if (ch == 0x1b) { esc = 1; esc_t = now; continue; }
            type_char(ch);
        }
        if (esc == 1 && now - esc_t > 60000) { type_key(QLK_ESC, false, false); esc = 0; }
        if (tq_head != tq_tail) {
            const typed_t *e = &tq[tq_head];
            if (e->shift) PRESS(QLK_SHIFT);
            if (e->ctrl) PRESS(QLK_CTRL);
            if (typing >= 4 && typing < 14) PRESS(e->key);
            if (++typing >= 20) { typing = 0; tq_head = (tq_head + 1) & 63; }
        }
#undef PRESS
#ifdef CYD_HEADLESS_TEST
        memset(k, 0, sizeof k);                   // QEMU reads all GPIOs as 0 ("pressed")
#endif
        for (int i = 0; i < 8; i++) in_keys[i] = k[i];
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ----------------------------------------------------------------------------
// Video (core 0)
// ----------------------------------------------------------------------------
static void render_band(int y0, int v)
{
    uint16_t *buf = lcd_get_buf();
    for (int dy = 0; dy < BAND; dy++) ql_view_row(v, y0 + dy, buf + dy * 320);
    lcd_blit(0, y0, 320, BAND, buf);
}

static void video_task(void *arg)
{
    bool toast_shown = false;
    uint8_t last_ctrl = 0xff;
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
        uint32_t rows[8];
        portENTER_CRITICAL(&dirty_mux);
        memcpy(rows, dirty_rows, sizeof rows);
        memset(dirty_rows, 0, sizeof dirty_rows);
        portEXIT_CRITICAL(&dirty_mux);
        const uint8_t *scr = ql_screen();
        uint8_t ctrl = ql_video_control();
        bool flash = ql_flash();
        bool all = full_redraw || ctrl != last_ctrl;
        full_redraw = false;
        last_ctrl = ctrl;
        int64_t now = esp_timer_get_time();
        bool toast_on = now < toast_until;
        if (toast_shown && !toast_on) all = true;      // take the toast off
        int v = view;
        ql_view_begin(scr, ctrl, flash);
        for (int y0 = 0; y0 < 240; y0 += BAND) {
            int lo, hi;
            ql_view_src_rows(v, y0, y0 + BAND - 1, &lo, &hi);
            bool d = all;
            for (int s = lo; s <= hi && !d; s++) d = (rows[s >> 5] >> (s & 31)) & 1;
            if (d) render_band(y0, v);
        }
        if (toast_on) {
            int w = (int)strlen(toast) * 6 + 12;
            fill_rect(160 - w / 2, 4, w, 16, rgb565(140, 30, 30));
            draw_text_c(160, 8, toast, 1, C_WHITE, rgb565(140, 30, 30));
            lcd_flush();
        }
        toast_shown = toast_on;
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

// ----------------------------------------------------------------------------
// Emulation + audio (core 1)
// ----------------------------------------------------------------------------
#ifdef CYD_HEADLESS_TEST
static uint32_t screen_hash(void)
{
    const uint8_t *s = ql_screen();
    uint32_t h = 2166136261u;
    for (int i = 0; i < 0x8000; i++) h = (h ^ s[i]) * 16777619u;
    return h;
}
#endif

static void emu_task(void *arg)
{
    static uint8_t a8[QL_AUDIO_MAX];
    static const int gain_shift[5] = { 0, 7, 6, 5, 4 };
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0, fno = 0;
    bool motor_led = false;

    for (;;) {
        fno++;
        bool req = mount_req;
        mount_req = false;
#ifdef CYD_HEADLESS_TEST
        req = fno == 5;                  // (the BOOT button reads as pressed in QEMU)
#endif
        if (req) {
            if (ql_mdv_image) {
                ql_mount(ql_cart_loaded(0) ? NULL : ql_mdv_image);
                char s[40];
                if (ql_cart_loaded(0)) snprintf(s, sizeof s, "MDV1: %.24s", ql_mdv_name);
                else snprintf(s, sizeof s, "MDV1: EJECTED");
                show_toast(s);
                ESP_LOGI(TAG, "%s", s);
            } else {
                show_toast("NO CARTRIDGE IN FIRMWARE");
            }
        }
        ql_keys_clear();
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if (in_keys[r] & (1 << b)) ql_key(QL_KEY(r, b), true);

        int64_t t0 = esp_timer_get_time();
        const int16_t *pcm;
        int n = ql_run_frame(&pcm);
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < n; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;

        uint32_t rows[8];
        ql_take_dirty_rows(rows);
        portENTER_CRITICAL(&dirty_mux);
        for (int i = 0; i < 8; i++) dirty_rows[i] |= rows[i];
        portEXIT_CRITICAL(&dirty_mux);
        xSemaphoreGive(frame_ready);

        bool m = ql_cart_motor(0) || ql_cart_motor(1);
        if (m != motor_led) {
            motor_led = m;
            gpio_set_level(PIN_LED_R, m ? 0 : 1);
        }
#ifdef CYD_HEADLESS_TEST
        if (fno % 300 == 0) ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)screen_hash());
#endif
        dac_audio_push(a8, n);           // blocks while the ring is full: paces at the audio rate

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%), video %.0f%%, shown %.1f fps, %s q=%u, mdv %s, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t), 100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t), frames_shown * 1e6 / (double)(now - stat_t),
                     dac_audio_ok() ? "dac ok" : "dac OFF", (unsigned)dac_audio_queued(),
                     ql_cart_loaded(0) ? (motor_led ? "run" : "in") : "out",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; video_busy_us = 0;
        }
    }
}

static void *must_alloc(size_t n, const char *what)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGE(TAG, "no memory for %s (%u bytes, largest block %u)", what, (unsigned)n,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        abort();
    }
    return p;
}

void app_main(void)
{
    static const cyd_game_t info = { "ql", "SINCLAIR QL", "F1/MDV: MICRODRIVE", {255, 60, 60} };
    cyd_boot(&info);

    uint8_t *ram[QL_RAM_BLOCKS];
    for (int i = 0; i < QL_RAM_BLOCKS; i++) {
        ram[i] = must_alloc(0x8000, "RAM");
        memset(ram[i], 0, 0x8000);
    }
    ql_init(ql_rom, ql_ipc_rom, ram, 0);
    ql_view_init(rgb565);

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

    dac_audio_set_rate(QL_AUDIO_RATE);
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(QL_AUDIO_RATE);
#endif
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "cartridge: %s, free internal heap: %u (largest %u)", ql_mdv_image ? ql_mdv_name : "none",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
