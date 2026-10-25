// retofinv_cyd.c - Return of the Invaders (Taito, 1985) on the ESP32 CYD.
// Core: rv_core.c / rv_video.c (C port of dsp-cpp's retofinv driver).
//
//   Core 1 : main, sub and sound Z80s, MC68705 MCU, 2 x SN76489 -> DAC GPIO26
//   Core 0 : tilemap/sprite renderer, ST7789 SPI/DMA, XPT2046 touch, Nunchuk,
//            buttons, serial keys
//
// Portrait 240x320: the game (224x288, as on the ROT90 cabinet) at (8, 0),
// touch buttons in the 32 lines below it.
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
#include "rv_core.h"

static const char *TAG = "retofinv";

#define GAME_X     8
#define GAME_Y     0
#define BAR_Y      288
#define AUDIO_RATE 44100

// ----------------------------------------------------------------------------
// Controls (portrait)
// ----------------------------------------------------------------------------
//   picture: left third = LEFT, middle = FIRE, right third = RIGHT
//   bar:     COIN+START | LEFT+FIRE | FIRE | RIGHT+FIRE
enum { K_LEFT = 1, K_RIGHT = 2, K_FIRE = 4, K_COIN = 64, K_START = 128 };

typedef struct { int x, w; uint8_t keys; const char *label; } bar_btn_t;
static const bar_btn_t bar[] = {
    {   0, 60, 0,                "COIN" },      // coin then start
    {  60, 60, K_LEFT | K_FIRE,  "<+F" },
    { 120, 60, K_FIRE,           "FIRE" },
    { 180, 60, K_RIGHT | K_FIRE, "F+>" },
};
#define NBAR (int)(sizeof bar / sizeof bar[0])

static void draw_bar(void)
{
    fill_rect(0, 0, 240, 320, C_BLACK);
    for (int i = 0; i < NBAR; i++) {
        uint16_t bg = i == 0 ? rgb565(120, 30, 30) : rgb565(20, 60, 140);
        fill_rect(bar[i].x + 1, BAR_Y + 1, bar[i].w - 2, 30, bg);
        draw_text_c(bar[i].x + bar[i].w / 2, BAR_Y + 12, bar[i].label, 1, C_WHITE, bg);
    }
    lcd_flush();
}

static volatile uint8_t in_keys;
static volatile int64_t ser_until[8];

static void input_task(void *arg)
{
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    gpio_config_t b = { .pin_bit_mask = (1ULL << PIN_BOOT_BTN),
                        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&b);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    int64_t boot_t0 = 0;
    for (;;) {
        uint8_t k = 0;
        int x, y;
        int64_t now = esp_timer_get_time();
        nunchuk_t nc;
        nunchuk_read(&nc);
        bool nc_combo = nc.ok && nc.c && nc.z;
        bool seq = false;
        if (touch_get(&x, &y)) {
            if (y >= BAR_Y) {
                for (int i = 0; i < NBAR; i++)
                    if (x >= bar[i].x && x < bar[i].x + bar[i].w) {
                        if (i == 0) seq = true; else k |= bar[i].keys;
                    }
            } else {
                k |= x < 80 ? K_LEFT : x >= 160 ? K_RIGHT : K_FIRE;
            }
        }
        // BOOT, Nunchuk C+Z or the COIN button: a coin, then START
        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo || seq) {
            if (!boot_t0) boot_t0 = now;
            if (now - boot_t0 < 150000) k |= K_COIN;
            if (now - boot_t0 > 300000 && now - boot_t0 < 450000) k |= K_START;
        } else boot_t0 = 0;
        // Nunchuk: stick left / right, Z or C = fire
        if (nc.ok && !nc_combo) {
            if (nc_left(&nc)) k |= K_LEFT;
            if (nc_right(&nc)) k |= K_RIGHT;
            if (nc.z || nc.c) k |= K_FIRE;
        }
        // Serial keys: a / d move, w or space fire, 5 coin, 1 start
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            int i = -1;
            switch (ch) {
            case 'a': case 'A': i = 0; break;
            case 'd': case 'D': i = 1; break;
            case 'w': case 'W': case ' ': i = 2; break;
            case '5': i = 6; break;
            case '1': i = 7; break;
            }
            if (i >= 0) ser_until[i] = now + 150000;
        }
        for (int i = 0; i < 8; i++) if (ser_until[i] > now) k |= (uint8_t)(1 << i);
#ifdef CYD_HEADLESS_TEST
        k = 0;                 // QEMU reads all GPIOs as 0 ("pressed")
#endif
        in_keys = k;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): draws a copy of the video RAM taken at vblank
// ----------------------------------------------------------------------------
static rv_core *core;
static rv_video vframe;
static uint8_t *iband;                   // RV_W x BAND colour indices
static uint16_t pal565[256];
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown, video_busy_us;

static void on_frame(void *user, rv_core *c)
{
    (void)user;
    if (video_busy) return;              // the screen is behind: skip this frame
    rv_get_video(c, &vframe);
    video_busy = true;
    xSemaphoreGive(frame_ready);
}

static void video_task(void *arg)
{
    uint8_t rgb[256][3];
    rv_palette_rgb(rgb);
    for (int i = 0; i < 256; i++) pal565[i] = rgb565(rgb[i][0], rgb[i][1], rgb[i][2]);
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        for (int y0 = 0; y0 < RV_H; y0 += BAND) {
            uint16_t *b = lcd_get_buf();
            rv_render(&vframe, y0, y0 + BAND, iband);
            for (int i = 0; i < RV_W * BAND; i++) b[i] = pal565[iband[i]];
            lcd_blit(GAME_X, GAME_Y + y0, RV_W, BAND, b);
        }
        video_busy = false;
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

#ifdef CYD_HEADLESS_TEST
// Progress check against the host build: hash of the picture.
static uint32_t frame_hash(void)
{
    static uint8_t *hband;
    if (!hband) hband = heap_caps_malloc(RV_W * 16, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!hband) return 0;
    static rv_video v;
    rv_get_video(core, &v);
    uint32_t h = 2166136261u;
    for (int y0 = 0; y0 < RV_H; y0 += 16) {
        rv_render(&v, y0, y0 + 16, hband);
        for (int i = 0; i < RV_W * 16; i++) h = (h ^ hband[i]) * 16777619u;
    }
    return h;
}
#endif

// ----------------------------------------------------------------------------
// Emulation + audio task (core 1)
// ----------------------------------------------------------------------------
static void emu_task(void *arg)
{
    static int16_t pcm[1024];
    static uint8_t a8[1024];
    static const int gain_shift[5] = { 0, 9, 8, 7, 6 };
    core->audio = pcm;
    core->audio_cap = 1024;
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0, sk0 = 0, mk0 = 0;

    for (;;) {
        uint8_t k = in_keys;
        rv_inputs in = {0};
        in.coin1 = k & K_COIN;
        in.start1 = k & K_START;
        in.left = k & K_LEFT;
        in.right = k & K_RIGHT;
        in.fire = k & K_FIRE;
        rv_set_inputs(core, &in);

        int64_t t0 = esp_timer_get_time();
        core->audio_n = 0;
        rv_run_frame(core);
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < core->audio_n; i++) {
            int v = sh ? 128 + (pcm[i] >> (sh - 1)) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        {   // (the renderer's work strip is shared with the video task: wait for it)
            static uint32_t fno;
            if (++fno % 300 == 0) {
                while (video_busy) vTaskDelay(1);
                video_busy = true;
                ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)frame_hash());
                video_busy = false;
            }
        }
#endif
        dac_audio_push(a8, core->audio_n);      // blocks while the ring is full: paces at the audio rate

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            const double cyc = stat_frames * (double)RV_CLOCK / RV_FPS + 1;
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%, Z80 cycles skipped %.0f%%, MCU %.0f%%), video %.0f%%, shown %.1f fps, %s q=%u, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * (core->skipped - sk0) / (3 * cyc),
                     100.0 * (core->mcu.cpu.idle_skipped - mk0) / (cyc / 4),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     dac_audio_ok() ? "dac ok" : "dac OFF", (unsigned)dac_audio_queued(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; video_busy_us = 0;
            sk0 = core->skipped; mk0 = core->mcu.cpu.idle_skipped;
        }
    }
}

static void *must_alloc(size_t n, const char *what)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGE(TAG, "no memory for %s (%u bytes)", what, (unsigned)n);
        abort();
    }
    return p;
}

void app_main(void)
{
    static const cyd_game_t info = { "retofinv", "RETURN OF THE", "INVADERS  TAITO 1985", {80, 200, 255} };
    cyd_boot(&info);

    core = must_alloc(sizeof *core, "core");
    iband = must_alloc(RV_W * BAND, "band");
    rv_init(core, AUDIO_RATE);
    core->idle_skip = true;
    core->on_frame = on_frame;

#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_FULL)
    cyd_display_start(true);
#else
    cyd_display_start(false);
#endif
#ifndef CYD_HEADLESS_TEST
    cyd_setup_ui();
#endif
    set_landscape(false);
    draw_bar();

    dac_audio_set_rate(AUDIO_RATE);
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(AUDIO_RATE);
#endif
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "core %u bytes, free internal heap: %u", (unsigned)sizeof *core,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
