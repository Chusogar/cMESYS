// trackfld_cyd.c - Track & Field (Konami 1983) on the ESP32 CYD.
// Core: tf_core.c (port of zxtiny/zxm/trackfld.c + trackfld_audio.c).
//
//   Core 1 : Konami-1 6809 + sound Z80 + SN76489 / VLM5030 / DAC -> GPIO26
//   Core 0 : tile/sprite renderer, ST7789 SPI/DMA, XPT2046 touch, Nunchuk, buttons
//
// Landscape 320x240: game 256x224 at (32,8), touch buttons on both sides.
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
#include "nvs_flash.h"
#include "nvs.h"
#include "cyd.h"
#include "dac_audio.h"
#include "nunchuk.h"
#include "tf_core.h"

static const char *TAG = "trackfld";

#define GAME_X   32
#define GAME_Y   8

// ----------------------------------------------------------------------------
// Game controls (landscape)
// ----------------------------------------------------------------------------
//  left strip (x 0..31)          right strip (x 288..319)
//    COIN  y 0..49                 JUMP  y 0..239
//    START y 50..99
//    RUN   y 100..239
//  game picture: left half = RUN, right half = JUMP
//
// RUN zones: every new tap presses the *other* run button (RUN1 / RUN2), so
// tapping one zone repeatedly gives the alternating presses the game needs
// (a resistive panel cannot read two fingers at once).
#define ZONE_NONE 0
#define ZONE_RUN  1
typedef struct { int x, y, w, h; uint8_t in0, in1; int zone; const char *label; } pad_t;
static const pad_t pads[] = {
    {   0,   0, 32,  50, TF_IN0_COIN1,  0,            ZONE_NONE, "CN" },
    {   0,  50, 32,  50, TF_IN0_START1, 0,            ZONE_NONE, "ST" },
    {   0, 100, 32, 140, 0,             0,            ZONE_RUN,  "RUN" },
    { 288,   0, 32, 240, 0,             TF_IN1_JUMP,  ZONE_NONE, "JMP" },
};
#define NPADS (int)(sizeof pads / sizeof pads[0])

static void draw_pad(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int i = 0; i < NPADS; i++) {
        const pad_t *p = &pads[i];
        uint16_t bg = p->zone == ZONE_RUN ? rgb565(20, 110, 40) :
                      (p->in1 ? rgb565(20, 60, 140) : rgb565(120, 30, 30));
        fill_rect(p->x + 1, p->y + 1, p->w - 2, p->h - 2, bg);
        const char *l = p->label;
        int n = (int)strlen(l), y0 = p->y + p->h / 2 - n * 5;      // vertical text
        for (int k = 0; k < n; k++) draw_char(p->x + p->w / 2 - 3, y0 + k * 10, l[k], 1, C_WHITE, bg);
    }
    lcd_flush();
}

static volatile uint8_t in_sys, in_p1;
static volatile int64_t ser_until[8];

static void input_task(void *arg)
{
    nunchuk_init(NUNCHUK_SDA, NUNCHUK_SCL);
    gpio_config_t b = { .pin_bit_mask = (1ULL << PIN_BOOT_BTN),
                        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&b);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    int64_t boot_t0 = 0;
    bool run_down = false;
    int run_which = 0;
    for (;;) {
        uint8_t s = 0, p = 0;
        int x, y;
        int64_t now = esp_timer_get_time();
        bool run_now = false;
        nunchuk_t nc;
        nunchuk_read(&nc);
        bool nc_combo = nc.ok && nc.c && nc.z;
        if (nc.ok && nc.z && !nc_combo) run_now = true;   // Z: each press = the other run button
        if (touch_get(&x, &y)) {
            bool in_pad = false;
            for (int i = 0; i < NPADS; i++) {
                const pad_t *q = &pads[i];
                if (x >= q->x && x < q->x + q->w && y >= q->y && y < q->y + q->h) {
                    s |= q->in0; p |= q->in1; in_pad = true;
                    if (q->zone == ZONE_RUN) run_now = true;
                }
            }
            if (!in_pad) {
                if (x < 160) run_now = true;
                else p |= TF_IN1_JUMP;
            }
        }
        if (run_now && !run_down) run_which ^= 1;      // new tap: switch run button
        run_down = run_now;
        if (run_now) p |= run_which ? TF_IN1_RUN2 : TF_IN1_RUN1;

        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo) {        // BOOT or Nunchuk C+Z
            if (!boot_t0) boot_t0 = now;
            if (now - boot_t0 < 150000) s |= TF_IN0_COIN1;
            if (now - boot_t0 > 300000 && now - boot_t0 < 450000) s |= TF_IN0_START1;
        } else boot_t0 = 0;
        // Nunchuk: stick left / right = RUN 1 / RUN 2 (waggle the stick to run),
        // C = JUMP (hold to set the angle), C+Z = coin + start
        if (nc.ok && !nc_combo) {
            if (nc_left(&nc))  p |= TF_IN1_RUN1;
            if (nc_right(&nc)) p |= TF_IN1_RUN2;
            if (nc.c) p |= TF_IN1_JUMP;
        }

        // Serial keys as on the desktop: c = run 1, z = run 2, x = jump, 5 coin, 1 start
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            int k = -1;
            switch (ch) {
            case 'c': case 'C': k = 0; break;
            case 'z': case 'Z': k = 1; break;
            case 'x': case 'X': k = 2; break;
            case '5': k = 3; break;
            case '1': k = 4; break;
            }
            if (k >= 0) ser_until[k] = now + 80000;
        }
        if (ser_until[0] > now) p |= TF_IN1_RUN1;
        if (ser_until[1] > now) p |= TF_IN1_RUN2;
        if (ser_until[2] > now) p |= TF_IN1_JUMP;
        if (ser_until[3] > now) s |= TF_IN0_COIN1;
        if (ser_until[4] > now) s |= TF_IN0_START1;
#ifdef CYD_HEADLESS_TEST
        s = 0; p = 0;          // QEMU reads all GPIOs as 0 ("pressed")
#endif
        in_sys = s; in_p1 = p;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0)
// ----------------------------------------------------------------------------
static tf_video_t vsnap;
static uint8_t *fb;
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown, video_busy_us;

static void video_task(void *arg)
{
    static uint16_t pal565[32];
    uint8_t rgb[32][3];
    tf_palette_rgb(rgb);
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        tf_render(&vsnap, fb);
        video_busy = false;
        for (int i = 0; i < 32; i++) pal565[i] = rgb565(rgb[i][0], rgb[i][1], rgb[i][2]);
        for (int y0 = 0; y0 < TF_H; y0 += BAND) {
            uint16_t *b = lcd_get_buf();
            const uint8_t *src = fb + y0 * TF_W;
            for (int i = 0; i < TF_W * BAND; i++) b[i] = pal565[src[i] & 31];
            lcd_blit(GAME_X, GAME_Y + y0, TF_W, BAND, b);
        }
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

// ----------------------------------------------------------------------------
// Emulation + audio task (core 1)
// ----------------------------------------------------------------------------
static void audio_init(void)
{
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(TF_AUDIO_RATE);
#endif
}

static void emu_task(void *arg)
{
    static int16_t pcm[TF_SAMPLES_FRAME];
    static uint8_t a8[TF_SAMPLES_FRAME];
    static const int gain_shift[5] = { 0, 9, 8, 7, 6 };
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0, faults = 0;

    for (;;) {
        tf_in.in0 = (uint8_t)~in_sys;
        tf_in.in1 = (uint8_t)~in_p1;

        int64_t t0 = esp_timer_get_time();
        if (!tf_run_frame(pcm)) faults++;
        if (!video_busy) {
            tf_snapshot(&vsnap);
            video_busy = true;
            xSemaphoreGive(frame_ready);
        }
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < TF_SAMPLES_FRAME; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        {   // progress check: hash of the rendered screen every 300 frames
            static uint32_t fno; static uint8_t *hfb; static tf_video_t hv;
            if (!hfb) hfb = malloc(TF_W * TF_H);
            if ((++fno % 300 == 0 || fno < 300) && hfb) {
                tf_snapshot(&hv); tf_render(&hv, hfb);
                uint32_t h = 2166136261u;
                for (int i = 0; i < TF_W * TF_H; i++) h = (h ^ hfb[i]) * 16777619u;
                extern uint16_t tf_dbg_pc(void); extern unsigned long tf_dbg_cycles(void); extern uint16_t tf_dbg_zpc(void);
                ESP_LOGI(TAG, "HASH frame %u %08x pc=%04x cyc=%lu zpc=%04x", (unsigned)fno, (unsigned)h, tf_dbg_pc(), tf_dbg_cycles(), tf_dbg_zpc());
            }
        }
#endif

        dac_audio_push(a8, TF_SAMPLES_FRAME);      // blocks while the ring is full: paces at 44.1 kHz

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%), video %.0f%%, shown %.1f fps, faults %u, audio %s q=%u, heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     (unsigned)faults, dac_audio_ok() ? "ok" : "OFF", (unsigned)dac_audio_queued(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; video_busy_us = 0;
        }
    }
}

// ----------------------------------------------------------------------------
void app_main(void)
{
    static const cyd_game_t info = { "trkfd", "TRACK & FIELD", NULL, {255, 220, 0} };
    cyd_boot(&info);

    fb = heap_caps_malloc(TF_W * TF_H, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(fb);
    memset(fb, 0, TF_W * TF_H);
    tf_init();

#ifndef CYD_HEADLESS_TEST
    cyd_display_start(true);
    cyd_setup_ui();
#else
    cyd_display_start(false);
#endif
    set_landscape(true);
    draw_pad();

    audio_init();
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "free internal heap: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
