// pp_cyd.inc - Pole Position / Pole Position II on the ESP32 CYD. Included
// by polepos_cyd.c and polepos2_cyd.c (PP_GAME, PP_LINE1, PP_LINE2).
// Core: pp_core.c / pp_video.c (C port of dsp-cpp's polepos driver).
//
//   Core 1 : Z80, both Z8002s, the four Namco MCUs (51xx-54xx), WSG and
//            engine sound -> DAC GPIO26
//   Core 0 : road / tiles / sprites renderer, ST7789 SPI/DMA, XPT2046 touch,
//            Nunchuk, buttons, serial keys
//
// Landscape: the 256x224 picture centred in 320x240. The picture is drawn
// from the core's RAM as it is when core 0 gets to it (a frame is dropped
// while the screen is still busy with the previous one).
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
#include "pp_core.h"
#include "rom_data.h"

static const char *TAG = PP_GAME;

#define GAME_X     ((320 - PP_W) / 2)
#define GAME_Y     ((240 - PP_H) / 2)
#define AUDIO_RATE 22050

// ----------------------------------------------------------------------------
// Controls (landscape): the touch screen in 3x3 zones over the picture
// ----------------------------------------------------------------------------
//   COIN       | GEAR  | GAS
//   LEFT+GAS   | GAS   | RIGHT+GAS
//   LEFT       | BRAKE | RIGHT
// The race starts by itself after the coin (as on the cabinet).
enum { K_LEFT = 1, K_RIGHT = 2, K_GAS = 4, K_BRAKE = 8, K_GEAR = 16, K_COIN = 64 };

static const uint8_t zone_keys[3][3] = {
    { K_COIN,         K_GEAR,  K_GAS },
    { K_LEFT | K_GAS, K_GAS,   K_RIGHT | K_GAS },
    { K_LEFT,         K_BRAKE, K_RIGHT },
};
static const char *zone_label[3][3] = {
    { "COIN", "GEAR", "GAS" },
    { "LEFT+GAS", "GAS", "RIGHT+GAS" },
    { "LEFT", "BRAKE", "RIGHT" },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int x = c * 107, y = r * 80, w = c == 2 ? 106 : 107;
            uint16_t bg = (r == 0 && c == 0) ? rgb565(120, 30, 30) :
                          (zone_keys[r][c] & (K_GAS | K_BRAKE | K_GEAR)) ? rgb565(20, 60, 140) : C_DGREY;
            fill_rect(x + 1, y + 1, w - 2, 78, bg);
            draw_text_c(x + w / 2, y + 36, zone_label[r][c], 1, C_WHITE, bg);
        }
    draw_text_c(160, 4, "TOUCH ZONES", 1, C_YELLOW, C_DGREY);
    lcd_flush();
}

static volatile uint8_t in_keys;
static volatile uint8_t in_wheel;           // Nunchuk steering (0 = not in use)
static volatile int64_t ser_until[8];

static void input_task(void *arg)
{
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    gpio_config_t b = { .pin_bit_mask = (1ULL << PIN_BOOT_BTN),
                        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&b);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    int64_t coin_t0 = 0;
    for (;;) {
        uint8_t k = 0, wheel = 0;
        int x, y;
        int64_t now = esp_timer_get_time();
        nunchuk_t nc;
        nunchuk_read(&nc);
        bool nc_combo = nc.ok && nc.c && nc.z;
        bool coin = false;
        if (touch_get(&x, &y)) {
            int c = x / 107, r = y / 80;
            if (c > 2) c = 2;
            if (r > 2) r = 2;
            if (zone_keys[r][c] & K_COIN) coin = true;
            else k |= zone_keys[r][c];
        }
        // BOOT, Nunchuk C+Z or the COIN zone: one coin per press
        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo || coin) {
            if (!coin_t0) coin_t0 = now;
            if (now - coin_t0 < 150000) k |= K_COIN;
        } else coin_t0 = 0;
        // Nunchuk: stick X = steering wheel, stick up = gear, Z = gas, C = brake
        if (nc.ok && !nc_combo) {
            int dx = nc.jx - 128;
            if (dx > -8 && dx < 8) dx = 0;
            int s = 0x80 + dx * 0x70 / 100;
            wheel = (uint8_t)(s < 0x10 ? 0x10 : s > 0xf0 ? 0xf0 : s);
            if (nc.jy > 128 + 60) k |= K_GEAR;
            if (nc.z) k |= K_GAS;
            if (nc.c) k |= K_BRAKE;
        }
        // Serial keys: a/d steer, w gas, s brake, g gear, 5 coin
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            int i = -1;
            switch (ch) {
            case 'a': case 'A': i = 0; break;
            case 'd': case 'D': i = 1; break;
            case 'w': case 'W': i = 2; break;
            case 's': case 'S': i = 3; break;
            case 'g': case 'G': i = 4; break;
            case '5': i = 6; break;
            }
            if (i >= 0) ser_until[i] = now + 150000;
        }
        for (int i = 0; i < 8; i++) if (ser_until[i] > now) k |= (uint8_t)(1 << i);
#ifdef CYD_HEADLESS_TEST
        k = 0; wheel = 0;      // QEMU reads all GPIOs as 0 ("pressed")
#endif
        in_wheel = wheel;
        in_keys = k;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): draws the frame from the core's RAM
// ----------------------------------------------------------------------------
static pp_core *core;
static uint8_t *iband;                   // PP_W x BAND colour numbers
static uint16_t pal565[128];
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown, video_busy_us;

static void on_frame(void *user, pp_core *c)
{
    (void)user; (void)c;
    if (video_busy) return;              // the screen is behind: skip this frame
    video_busy = true;
    xSemaphoreGive(frame_ready);
}

static void video_task(void *arg)
{
    for (int i = 0; i < 128; i++) {
        uint32_t c = core->rgb[i];
        pal565[i] = rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        for (int y0 = 0; y0 < PP_H; y0 += BAND) {
            uint16_t *b = lcd_get_buf();
            pp_render(core, y0, y0 + BAND, iband);
            for (int i = 0; i < PP_W * BAND; i++) b[i] = pal565[iband[i] & 127];
            lcd_blit(GAME_X, GAME_Y + y0, PP_W, BAND, b);
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
    uint32_t h = 2166136261u;
    for (int y0 = 0; y0 < PP_H; y0 += BAND) {
        pp_render(core, y0, y0 + BAND, iband);
        for (int i = 0; i < PP_W * BAND; i++) h = (h ^ iband[i]) * 16777619u;
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
    uint32_t stat_frames = 0, stat_busy = 0;

    for (;;) {
        uint8_t k = in_keys;
        pp_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & K_COIN;
        in.left = k & K_LEFT;
        in.right = k & K_RIGHT;
        in.gas = k & K_GAS;
        in.brake = k & K_BRAKE;
        in.gear = k & K_GEAR;
        in.wheel = in_wheel;
        pp_set_inputs(core, &in);

        int64_t t0 = esp_timer_get_time();
        core->audio_n = 0;
        pp_run_frame(core);
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < core->audio_n; i++) {
            int v = sh ? 128 + (pcm[i] >> (sh - 1)) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        {   // (the renderer's band is shared with the video task: wait for it)
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
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%), video %.0f%%, shown %.1f fps, %s q=%u, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     dac_audio_ok() ? "dac ok" : "dac OFF", (unsigned)dac_audio_queued(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; video_busy_us = 0;
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
    static const cyd_game_t info = { PP_GAME, PP_LINE1, PP_LINE2, {255, 200, 40} };
    cyd_boot(&info);

    core = must_alloc(sizeof *core, "core");
    memset(core, 0, sizeof *core);
    iband = must_alloc(PP_W * BAND, "band");
    pp_roms roms;
    pp_rom_data(&roms);
    // the three CPUs' programs in RAM (out of the flash cache), if they fit
    const uint8_t **prog[3] = { &roms.sub1, &roms.sub2, &roms.z80 };
    const size_t psize[3] = { 0x8000, 0x8000, roms.z80_size };
    for (int i = 0; i < 3; i++) {
        uint8_t *p = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > psize[i] + 96 * 1024 ?
                     heap_caps_malloc(psize[i], MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : NULL;
        if (!p) break;
        memcpy(p, *prog[i], psize[i]);
        *prog[i] = p;
    }
    pp_init(core, &roms, AUDIO_RATE);
    core->on_frame = on_frame;

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
    vTaskDelay(pdMS_TO_TICKS(2500));
#endif
    fill_rect(0, 0, 320, 240, C_BLACK);
    lcd_flush();

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
