// starwars_cyd.c - Star Wars (Atari, 1983) and The Empire Strikes Back
// (Atari, 1985) on the ESP32 CYD: one firmware for both, the same core
// (sw_core.c) as the desktop.
//
//   Core 1 : the two 6809s, the mathbox, the AVG (drawing the vectors into an
//            8-bit 320x240 picture), 4 POKEYs + TMS5220 -> DAC GPIO26
//   Core 0 : the picture to the ST7789 (SPI/DMA), touch, Nunchuk, BOOT
//
// The whole screen is the game. The yoke: where the screen is touched (the
// crosshair goes there, and fires while touched), or the Nunchuk's stick
// (Z fire, C the thumb button). BOOT (or Nunchuk C+Z): coin, then start;
// BOOT held 2 s: the other game. Serial: 5 coin, 1 start, space fire,
// w/a/s/d the yoke, g the other game.
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
#include "sw_core.h"
#include "rom_data.h"

static const char *TAG = "starwars";

#define AUDIO_RATE 22050

// ----------------------------------------------------------------------------
// Input task (core 0)
// ----------------------------------------------------------------------------
static volatile bool in_coin, in_start, in_fire1, in_fire2, in_fire3;
static volatile uint8_t in_x = 0x80, in_y = 0x80;
static volatile bool switch_req;
static volatile int64_t ser_until[8];

static int scale255(int v, int span)
{
    v = v * 255 / (span - 1);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static void input_task(void *arg)
{
    (void)arg;
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    gpio_config_t b = { .pin_bit_mask = (1ULL << PIN_BOOT_BTN),
                        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&b);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    int64_t boot_t0 = 0;
    bool switched = false;
    int sx = 0x80, sy = 0x80;               // the serial keys' yoke
    for (;;) {
        bool coin = false, start = false, f1 = false, f2 = false, f3 = false;
        int x = in_x, y = in_y;
        int64_t now = esp_timer_get_time();
        nunchuk_t nc;
        nunchuk_read(&nc);
        bool nc_combo = nc.ok && nc.c && nc.z;
        int tx, ty;
        if (touch_get(&tx, &ty)) {
            x = scale255(tx, 320);
            y = scale255(ty, 240);
            f1 = true;
        } else if (nc.ok) {
            x = nc.jx;
            y = 255 - nc.jy;
            if (!nc_combo) {
                f1 = nc.z;
                f3 = nc.c;
            }
        } else {
            x = sx;
            y = sy;
        }
        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo) {       // BOOT or Nunchuk C+Z
            if (!boot_t0) boot_t0 = now, switched = false;
            int64_t t = now - boot_t0;
            if (t < 150000) coin = true;
            if (t > 300000 && t < 450000) start = true;
            if (t > 2000000 && !switched) switch_req = true, switched = true;
        } else boot_t0 = 0;

        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            switch (ch) {
            case '5': ser_until[0] = now + 100000; break;
            case '1': ser_until[1] = now + 100000; break;
            case ' ': ser_until[2] = now + 100000; break;
            case 'g': case 'G': switch_req = true; break;
            case 'w': case 'W': sy = sy > 0x20 ? sy - 0x20 : 0; break;
            case 's': case 'S': sy = sy < 0xdf ? sy + 0x20 : 0xff; break;
            case 'a': case 'A': sx = sx > 0x20 ? sx - 0x20 : 0; break;
            case 'd': case 'D': sx = sx < 0xdf ? sx + 0x20 : 0xff; break;
            }
        }
        if (ser_until[0] > now) coin = true;
        if (ser_until[1] > now) start = true;
        if (ser_until[2] > now) f1 = true;
#ifdef CYD_HEADLESS_TEST
        coin = start = f1 = f2 = f3 = false;    // QEMU reads all GPIOs as 0 ("pressed")
        x = y = 0x80;
        switch_req = false;
#endif
        in_coin = coin; in_start = start; in_fire1 = f1; in_fire2 = f2; in_fire3 = f3;
        in_x = (uint8_t)x; in_y = (uint8_t)y;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): the 8-bit picture through the palette to the LCD
// ----------------------------------------------------------------------------
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown, video_busy_us;
static uint16_t pal565[256];

static void video_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        const uint8_t *pic = sw_picture();
        const int w = sw_width(), h = sw_height(), x0 = (320 - w) / 2, y0 = (240 - h) / 2;
        for (int y = 0; pic && y < h; y += BAND) {
            int n = h - y < BAND ? h - y : BAND;
            uint16_t *b = lcd_get_buf();
            const uint8_t *src = pic + y * w;
            for (int i = 0; i < w * n; i++) b[i] = pal565[src[i]];
            lcd_blit(x0, y0 + y, w, n, b);
        }
        video_busy = false;
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

// ----------------------------------------------------------------------------
// Emulation + audio task (core 1)
// ----------------------------------------------------------------------------
static bool start_game(int game)
{
    if (!sw_init(game, 320, 240)) return false;
    sw_set_audio_rate(AUDIO_RATE);
    const uint32_t *c = sw_palette();
    for (int i = 0; i < 256; i++) pal565[i] = rgb565((uint8_t)(c[i] >> 16), (uint8_t)(c[i] >> 8), (uint8_t)c[i]);
    ESP_LOGI(TAG, "%s, picture %dx%d", sw_title(), sw_width(), sw_height());
    return true;
}

static void emu_task(void *arg)
{
    (void)arg;
    static int16_t pcm[SW_AUDIO_MAX];
    static uint8_t a8[SW_AUDIO_MAX];
    static const int gain_shift[5] = { 0, 9, 8, 7, 6 };
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0;

    for (;;) {
        if (switch_req) {
            switch_req = false;
            while (video_busy) vTaskDelay(1);
            int other = sw_game() == SW_GAME_ESB ? SW_GAME_STARWARS : SW_GAME_ESB;
            if (!start_game(other)) start_game(other == SW_GAME_ESB ? SW_GAME_STARWARS : SW_GAME_ESB);
        }
        sw_inputs in = { in_coin, false, in_start, in_fire1, in_fire2, in_fire3, in_x, in_y };
        sw_set_inputs(&in);

        int64_t t0 = esp_timer_get_time();
        int n = sw_run_frame(pcm);
        if (!video_busy) {
            video_busy = true;
            xSemaphoreGive(frame_ready);
        }
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < n; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        {   // progress check: hash of the picture every 300 frames
            static uint32_t fno;
            if (++fno % 300 == 0) {
                const uint8_t *p = sw_picture();
                uint32_t h = 2166136261u;
                for (int i = 0; p && i < sw_width() * sw_height(); i++) h = (h ^ p[i]) * 16777619u;
                ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)h);
            }
        }
#endif
        dac_audio_push(a8, n);          // blocks while the ring is full: paces at the sound's rate

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%), video %.0f%%, shown %.1f fps, audio %s q=%u, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     dac_audio_ok() ? "ok" : "OFF", (unsigned)dac_audio_queued(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; video_busy_us = 0;
        }
    }
}

// ----------------------------------------------------------------------------
void app_main(void)
{
    static const cyd_game_t info = { "starw", "STAR WARS", NULL, {255, 220, 0} };
    cyd_boot(&info);

    if (!start_game(SW_GAME_STARWARS) && !start_game(SW_GAME_ESB)) cyd_rom_error();

#ifndef CYD_HEADLESS_TEST
    cyd_display_start(true);
    cyd_setup_ui();
#else
    cyd_display_start(false);
#endif
    set_landscape(true);
    fill_rect(0, 0, 320, 240, C_BLACK);
    lcd_flush();

#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(AUDIO_RATE);
#endif
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "free internal heap: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
