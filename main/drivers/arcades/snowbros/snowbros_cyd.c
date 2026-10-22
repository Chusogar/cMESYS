// snowbros_cyd.c - Snow Bros (Toaplan 1990) on the ESP32 CYD.
// Core: sb_core.c (port of zxtiny/zxm/snowbros.c).
//
//   Core 1 : 68000 (Musashi) + Z80 + YM3812 timers
//   Core 0 : YM3812 synthesis (emu8950) -> DAC GPIO26, sprite renderer,
//            ST7789 SPI/DMA, XPT2046 touch, Nunchuk, buttons, serial keys
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
#include "sb_core.h"
#include "emu8950/emu8950.h"

static const char *TAG = "snowbros";

#define GAME_X   32
#define GAME_Y   8

// ----------------------------------------------------------------------------
// Game controls (landscape)
// ----------------------------------------------------------------------------
//  left strip (x 0..31)            right strip (x 288..319)
//    COIN   y 0..59                  UP     y 0..59
//    START  y 60..119                DOWN   y 60..119
//    FIRE   y 120..239               JUMP   y 120..239
//  game picture: left / right half = walk left / right,
//                top quarter = walk + jump
typedef struct { int x, y, w, h; uint16_t sys, p1; const char *label; int arrow; } pad_t;
static const pad_t pads[] = {
    {   0,   0, 32,  60, SB_SYS_COIN1,  0,           "CN",  -1 },
    {   0,  60, 32,  60, SB_SYS_START1, 0,           "ST",  -1 },
    {   0, 120, 32, 120, 0,             SB_IN_B1,    "SH",  -1 },
    { 288,   0, 32,  60, 0,             SB_IN_UP,    NULL,   0 },
    { 288,  60, 32,  60, 0,             SB_IN_DOWN,  NULL,   1 },
    { 288, 120, 32, 120, 0,             SB_IN_B2,    "JP",  -1 },
};
#define NPADS (int)(sizeof pads / sizeof pads[0])

static void draw_pad(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int i = 0; i < NPADS; i++) {
        const pad_t *p = &pads[i];
        uint16_t bg = (p->p1 & (SB_IN_B1 | SB_IN_B2)) ? rgb565(20, 60, 140) :
                      (p->sys ? rgb565(120, 30, 30) : C_DGREY);
        fill_rect(p->x + 1, p->y + 1, p->w - 2, p->h - 2, bg);
        if (p->arrow >= 0) draw_arrow(p->x + 1, p->y + 1, p->w - 2, p->h - 2, p->arrow, C_WHITE, bg);
        else draw_text_c(p->x + p->w / 2, p->y + p->h / 2 - 4, p->label, 1, C_WHITE, bg);
    }
    lcd_flush();
}

static volatile uint16_t in_sys, in_p1;
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
        uint16_t s = 0, p = 0;
        int x, y;
        int64_t now = esp_timer_get_time();
        nunchuk_t nc;
        nunchuk_read(&nc);
        bool nc_combo = nc.ok && nc.c && nc.z;
        if (touch_get(&x, &y)) {
            bool in_pad = false;
            for (int i = 0; i < NPADS; i++) {
                const pad_t *q = &pads[i];
                if (x >= q->x && x < q->x + q->w && y >= q->y && y < q->y + q->h) { s |= q->sys; p |= q->p1; in_pad = true; }
            }
            if (!in_pad) {
                p |= (x < 160) ? SB_IN_LEFT : SB_IN_RIGHT;
                if (y < 64) p |= SB_IN_B2;
            }
        }
        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo) {        // BOOT or Nunchuk C+Z
            if (!boot_t0) boot_t0 = now;
            if (now - boot_t0 < 150000) s |= SB_SYS_COIN1;
            if (now - boot_t0 > 300000 && now - boot_t0 < 450000) s |= SB_SYS_START1;
        } else boot_t0 = 0;
        // Nunchuk: stick = 4 directions, Z = SHOOT, C = JUMP, C+Z = coin + start
        if (nc.ok && !nc_combo) {
            if (nc_left(&nc))  p |= SB_IN_LEFT;
            if (nc_right(&nc)) p |= SB_IN_RIGHT;
            if (nc_up(&nc))    p |= SB_IN_UP;
            if (nc_down(&nc))  p |= SB_IN_DOWN;
            if (nc.z) p |= SB_IN_B1;
            if (nc.c) p |= SB_IN_B2;
        }

        // Serial keys: a/d/w/s move, j fire, k jump, 5 coin, 1 start
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            int k = -1;
            switch (ch) {
            case 'a': case 'A': k = 0; break;
            case 'd': case 'D': k = 1; break;
            case 'w': case 'W': k = 2; break;
            case 's': case 'S': k = 3; break;
            case 'j': case 'J': k = 4; break;
            case 'k': case 'K': k = 5; break;
            case '5': k = 6; break;
            case '1': k = 7; break;
            }
            if (k >= 0) ser_until[k] = now + 150000;
        }
        static const uint16_t pm[6] = { SB_IN_LEFT, SB_IN_RIGHT, SB_IN_UP, SB_IN_DOWN, SB_IN_B1, SB_IN_B2 };
        for (int i = 0; i < 6; i++) if (ser_until[i] > now) p |= pm[i];
        if (ser_until[6] > now) s |= SB_SYS_COIN1;
        if (ser_until[7] > now) s |= SB_SYS_START1;
#ifdef CYD_HEADLESS_TEST
        s = 0; p = 0;          // QEMU reads all GPIOs as 0 ("pressed")
#endif
        in_sys = s; in_p1 = p;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ----------------------------------------------------------------------------
// YM3812: register-write ring (core 1 -> core 0) and synthesis task
// ----------------------------------------------------------------------------
typedef struct { uint32_t when; uint8_t reg, val; uint16_t pad; } oplw_t;
#define OPLQ 2048
static oplw_t oplq[OPLQ];
static volatile uint32_t oplq_head, oplq_tail;      // head: producer, tail: consumer
static volatile uint32_t emu_sample_clock;          // published by core 1 after each frame
static volatile uint32_t synth_pos;                 // samples synthesized by core 0
static volatile uint32_t audio_underruns;
static volatile uint32_t audio_busy_us, video_busy_us;

void sb_opl_write(uint32_t when, uint8_t reg, uint8_t val)
{
    while (oplq_head - oplq_tail >= OPLQ) vTaskDelay(1);   // full: let core 0 drain
    oplw_t *w = &oplq[oplq_head % OPLQ];
    w->when = when; w->reg = reg; w->val = val;
    __sync_synchronize();
    oplq_head++;
}

static OPL *opl;

static void audio_task(void *arg)
{
    static int16_t pcm[256];
    static uint8_t a8[256];
    static const int gain_shift[5] = { 0, 7, 6, 5, 4 };     // 16-bit -> 8-bit with gain
    for (;;) {
        // How many samples to produce now: follow the emulation clock, but keep
        // the DAC fed even if the emulation is late (then synth runs ahead).
        int32_t ahead = (int32_t)(emu_sample_clock - synth_pos);
        int n = ahead > 256 ? 256 : (ahead > 64 ? ahead : 128);
        if (ahead < 64) audio_underruns++;
        int64_t ta = esp_timer_get_time();
        for (int i = 0; i < n; i++) {
            while (oplq_tail != oplq_head && (int32_t)(oplq[oplq_tail % OPLQ].when - synth_pos) <= 0) {
                oplw_t *w = &oplq[oplq_tail % OPLQ];
                OPL_writeReg(opl, w->reg, w->val);
                __sync_synchronize();
                oplq_tail++;
            }
#ifdef CYD_QEMU_SOLO
            pcm[i] = 0;
#else
            pcm[i] = OPL_calc(opl);
#endif
            synth_pos++;
        }
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < n; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        audio_busy_us += (uint32_t)(esp_timer_get_time() - ta);
        dac_audio_push(a8, n);                        // blocks while the ring is full (41.67 kHz)
    }
}

static void audio_init(void)
{
    opl = OPL_new(SB_YM_CLOCK, SB_OPL_RATE);
    OPL_setChipType(opl, 2);           // YM3812
    OPL_reset(opl);
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(SB_OPL_RATE);
#endif
}

// ----------------------------------------------------------------------------
// Video task (core 0)
// ----------------------------------------------------------------------------
static sb_video_t vsnap;
static uint16_t vpal[256];
static uint8_t *fb;
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown;

static void video_task(void *arg)
{
    static uint16_t pal565[256];
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
#ifdef CYD_QEMU_SOLO
        video_busy = false; continue;
#endif
        sb_render(&vsnap, fb);
        for (int i = 0; i < 256; i++) {
            uint16_t v = vpal[i];
            unsigned r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
            pal565[i] = rgb565((uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 3) | (g >> 2)), (uint8_t)((b << 3) | (b >> 2)));
        }
        video_busy = false;
        for (int y0 = 0; y0 < SB_SCREEN_H; y0 += BAND) {
            uint16_t *b = lcd_get_buf();
            const uint8_t *src = fb + y0 * SB_SCREEN_W;
            for (int i = 0; i < SB_SCREEN_W * BAND; i++) b[i] = pal565[src[i]];
            lcd_blit(GAME_X, GAME_Y + y0, SB_SCREEN_W, BAND, b);
        }
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

// ----------------------------------------------------------------------------
// Emulation task (core 1)
// ----------------------------------------------------------------------------
static void emu_task(void *arg)
{
    const int32_t spf = SB_OPL_RATE / SB_FPS;            // ~694 samples per frame
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0, stat_under0 = 0;

    for (;;) {
        sb_input[0] = (uint16_t)(0x7fff & ~in_p1);
        sb_input[2] = (uint16_t)(0xffff & ~in_sys);

        int64_t t0 = esp_timer_get_time();
        sb_run_frame();
        if (!video_busy) {
            sb_snapshot(&vsnap);
            memcpy(vpal, sb_palette(), sizeof vpal);
            video_busy = true;
            xSemaphoreGive(frame_ready);
        }
        emu_sample_clock = sb_sample_clock();
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        {   // progress check: hash of the rendered screen every 300 frames
            static uint32_t fno; static uint8_t *hfb; static sb_video_t hv;
            static uint8_t hfb_mem[SB_SCREEN_W * SB_SCREEN_H];   // (a 57 KB malloc fails here)
            hfb = hfb_mem;
            if (++fno % 300 == 0 && hfb) {
                sb_snapshot(&hv); sb_render(&hv, hfb);
                uint32_t h = 2166136261u;
                for (int i = 0; i < SB_SCREEN_W * SB_SCREEN_H; i++) h = (h ^ hfb[i]) * 16777619u;
                ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)h);
            }
        }
#endif

        // Pace on the audio clock: stay at most ~2.5 frames ahead of the DAC.
        while ((int32_t)(sb_sample_clock() - synth_pos) > spf * 5 / 2) vTaskDelay(1);

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%), synth %.0f%%, video %.0f%%, shown %.1f fps, audio underruns %u (%s), heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * audio_busy_us / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     (unsigned)(audio_underruns - stat_under0), dac_audio_ok() ? "dac ok" : "dac OFF",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; stat_under0 = audio_underruns;
            audio_busy_us = 0; video_busy_us = 0;
        }
    }
}

// ----------------------------------------------------------------------------
void app_main(void)
{
    static const cyd_game_t info = { "snowb", "SNOW BROS", NULL, {80, 200, 255} };
    cyd_boot(&info);

    // Big allocations first, while the heap is unfragmented
    fb = heap_caps_malloc(SB_SCREEN_W * SB_SCREEN_H, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(fb);
    memset(fb, 0, SB_SCREEN_W * SB_SCREEN_H);
    sb_init(SB_OPL_RATE);

#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_FULL)
    cyd_display_start(true);
#else
    cyd_display_start(false);
#endif
#ifndef CYD_HEADLESS_TEST
    cyd_setup_ui();
#endif
    set_landscape(true);
    draw_pad();

    audio_init();
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "free internal heap: %u (largest %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 8, NULL, 0);
    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
