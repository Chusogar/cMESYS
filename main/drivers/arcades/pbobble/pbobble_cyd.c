// pbobble_cyd.c - Puzzle Bobble (Taito B System, 1994) on the ESP32 CYD.
// Core: pb_core.c (68000 + Z80 + YM2610B timers, TC0180VCU, TC0140SYT,
// TC0640FIO, 93C46, MB87078), after MAME's taito_b.cpp.
//
//   Core 1 : 68000 (Musashi) + Z80, both with exact idle-loop fast-forward
//   Core 0 : YM2610B synthesis (FM + ADPCM-A) -> DAC GPIO26, TC0180VCU
//            renderer by 16-line bands -> ST7789 over SPI/DMA, touch, Nunchuk,
//            BOOT button, serial keys
//
// Landscape 320x240: the game's 320x224 picture at y = 8, touch zones over it.
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
#include "pb_core.h"
#include "ym2610.h"
#include "rom_data.h"

static const char *TAG = "pbobble";

#define GAME_Y        8
#define YM_PER_SAMPLE 288                       // 8 MHz / 288 = 27778 Hz
#define AUDIO_RATE    (PB_SND_CLOCK / YM_PER_SAMPLE)

// ----------------------------------------------------------------------------
// Game controls (landscape): the touch screen in 3x3 zones over the picture
// ----------------------------------------------------------------------------
//   COIN+START | FIRE | RIGHT
//   LEFT       | FIRE | RIGHT
//   LEFT       | FIRE | RIGHT
enum { K_LEFT = 1, K_RIGHT = 2, K_FIRE = 4, K_START2 = 8, K_COIN = 16, K_START = 32 };

static const uint8_t zone_keys[3][3] = {
    { 0,      K_FIRE, K_RIGHT },
    { K_LEFT, K_FIRE, K_RIGHT },
    { K_LEFT, K_FIRE, K_RIGHT },
};
static const char *zone_label[3][3] = {
    { "COIN+START", "FIRE", "RIGHT" },
    { "LEFT", "FIRE", "RIGHT" },
    { "LEFT", "FIRE", "RIGHT" },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int x = c * 107, y = r * 80, w = c == 2 ? 106 : 107;
            uint16_t bg = (r == 0 && c == 0) ? rgb565(120, 30, 30) :
                          (zone_keys[r][c] & K_FIRE) ? rgb565(20, 60, 140) : C_DGREY;
            fill_rect(x + 1, y + 1, w - 2, 78, bg);
            draw_text_c(x + w / 2, y + 36, zone_label[r][c], 1, C_WHITE, bg);
        }
    draw_text_c(160, 4, "TOUCH ZONES", 1, C_YELLOW, C_DGREY);
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
            int c = x / 107, r = y / 80;
            if (c > 2) c = 2;
            if (r > 2) r = 2;
            if (r == 0 && c == 0) seq = true;
            else k |= zone_keys[r][c];
        }
        // BOOT, Nunchuk C+Z or the top-left zone: a coin, then START
        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo || seq) {
            if (!boot_t0) boot_t0 = now;
            if (now - boot_t0 < 150000) k |= K_COIN;
            if (now - boot_t0 > 300000 && now - boot_t0 < 450000) k |= K_START;
        } else boot_t0 = 0;
        // Nunchuk: stick left / right = aim, Z or C = fire
        if (nc.ok && !nc_combo) {
            if (nc_left(&nc))  k |= K_LEFT;
            if (nc_right(&nc)) k |= K_RIGHT;
            if (nc.z || nc.c) k |= K_FIRE;
        }
        // Serial keys: a/d aim, j (or w / space) fire, 5 coin, 1 start, 2 start 2P
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            int i = -1;
            switch (ch) {
            case 'a': case 'A': i = 0; break;
            case 'd': case 'D': i = 1; break;
            case 'j': case 'J': case 'w': case 'W': case ' ': i = 2; break;
            case '2': i = 3; break;
            case '5': i = 4; break;
            case '1': i = 5; break;
            }
            if (i >= 0) ser_until[i] = now + 150000;
        }
        for (int i = 0; i < 6; i++) if (ser_until[i] > now) k |= (uint8_t)(1 << i);
#ifdef CYD_HEADLESS_TEST
        k = 0;                 // QEMU reads all GPIOs as 0 ("pressed")
#endif
        in_keys = k;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ----------------------------------------------------------------------------
// Sound: the YM2610B is synthesized on core 0 from the register writes of
// core 1, time-stamped in output samples.
// ----------------------------------------------------------------------------
typedef struct { uint32_t when; uint8_t part, reg, val, pad; } ymw_t;
#define YMQ 2048
static ymw_t ymq[YMQ];
static volatile uint32_t ymq_head, ymq_tail;        // head: producer, tail: consumer
static volatile uint32_t emu_sample_clock;          // published by core 1 after each frame
static volatile uint32_t synth_pos;                 // samples synthesized by core 0
static volatile uint32_t audio_underruns;
static volatile uint32_t audio_busy_us, video_busy_us;
static ym2610_t *ym;

void pb_ym_write(uint32_t when, int part, uint8_t reg, uint8_t val)
{
    while (ymq_head - ymq_tail >= YMQ) vTaskDelay(1);   // full: let core 0 drain
    ymw_t *w = &ymq[ymq_head % YMQ];
    w->when = when / YM_PER_SAMPLE;
    w->part = (uint8_t)part; w->reg = reg; w->val = val;
    __sync_synchronize();
    ymq_head++;
}

static void audio_task(void *arg)
{
    static int16_t pcm[256];
    static uint8_t a8[256];
    static const int gain_shift[5] = { 0, 8, 7, 6, 5 };    // 16-bit -> 8 bits, with gain
    for (;;) {
        int32_t ahead = (int32_t)(emu_sample_clock - synth_pos);
        int n = ahead > 256 ? 256 : (ahead > 64 ? ahead : 128);
        if (ahead <= 0) audio_underruns++;           // the emulation is late: samples made up
        int64_t ta = esp_timer_get_time();
        int gain = pb_sound_gain();
        for (int i = 0; i < n; i++) {
            while (ymq_tail != ymq_head && (int32_t)(ymq[ymq_tail % YMQ].when - synth_pos) <= 0) {
                ymw_t *w = &ymq[ymq_tail % YMQ];
                ym2610_write(ym, w->part, w->reg, w->val);
                __sync_synchronize();
                ymq_tail++;
            }
#ifdef CYD_QEMU_SOLO
            pcm[i] = 0;
#else
            ym2610_render(ym, &pcm[i], 1, gain);
#endif
            synth_pos++;
        }
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < n; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        audio_busy_us += (uint32_t)(esp_timer_get_time() - ta);
        dac_audio_push(a8, n);                        // blocks while the ring is full
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): composes the state captured at vblank, by bands.
// The tile RAM is read live (it may change while a frame is drawn).
// ----------------------------------------------------------------------------
static vcu_frame_t *vsnap;
static uint16_t *pal565;                 // 4096 palette entries, LCD order
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown;

static void video_task(void *arg)
{
    uint32_t pal_serial = ~0u;
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
#ifdef CYD_QEMU_SOLO
        video_busy = false; continue;
#endif
        uint32_t ps = pb_palette_serial;
        if (ps != pal_serial) {
            pal_serial = ps;
            const uint16_t *p = pb_palette_ram();
            for (int i = 0; i < 4096; i++) {
                uint32_t c = pb_pal_rgb(p[i]);
                pal565[i] = rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
            }
        }
        for (int y0 = 0; y0 < PB_SCREEN_H; y0 += BAND) {
            uint16_t *b = lcd_get_buf();
            tc0180vcu_render(vsnap, pb_vram(), y0, BAND, b, NULL);
            for (int i = 0; i < PB_SCREEN_W * BAND; i++) b[i] = pal565[b[i] & 0xfff];
            lcd_blit(0, GAME_Y + y0, PB_SCREEN_W, BAND, b);
        }
        video_busy = false;
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

// ----------------------------------------------------------------------------
// EEPROM (settings, high scores) in NVS
// ----------------------------------------------------------------------------
static void eeprom_load(void)
{
    nvs_handle_t h;
    uint8_t *e = pb_eeprom(NULL);
    if (nvs_open("pbobble", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = 128;
        if (nvs_get_blob(h, "eeprom", e, &sz) != ESP_OK || sz != 128) memset(e, 0xff, 128);
        nvs_close(h);
    }
}

static void eeprom_save(void)
{
    nvs_handle_t h;
    bool *dirty;
    uint8_t *e = pb_eeprom(&dirty);
    *dirty = false;
    if (nvs_open("pbobble", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "eeprom", e, 128);
        nvs_commit(h);
        nvs_close(h);
    }
}

#ifdef CYD_HEADLESS_TEST
// Hash of the rendered picture (palette indices), as tools/pb_hash on the PC
static uint32_t frame_hash(void)
{
    // line by line, with its own buffers (the video task renders at the same time)
    static uint16_t line[PB_SCREEN_W], scratch[PB_SCREEN_W];
    uint32_t h = 2166136261u;
    for (int y = 0; y < PB_SCREEN_H; y++) {
        tc0180vcu_render(&pb_frame, pb_vram(), y, 1, line, scratch);
        for (int i = 0; i < PB_SCREEN_W; i++) h = (h ^ line[i]) * 16777619u;
    }
    return h;
}
#endif

// ----------------------------------------------------------------------------
// Emulation task (core 1)
// ----------------------------------------------------------------------------
static void emu_task(void *arg)
{
    const int32_t spf = AUDIO_RATE / PB_FPS;              // ~463 samples per frame
    int64_t stat_t = esp_timer_get_time(), eeprom_t = 0;
    uint32_t stat_frames = 0, stat_busy = 0, stat_under0 = 0;
    pb_stats_t st0 = {0}, st;
    bool *eep_dirty;
    pb_eeprom(&eep_dirty);

    for (;;) {
        uint8_t k = in_keys;
        pb_in.coin = (k & K_COIN) ? (uint8_t)~PB_COIN1 : 0xff;
        pb_in.start = (uint8_t)(0xff & ~((k & K_START) ? PB_START1 : 0) & ~((k & K_START2) ? PB_START2 : 0));
        pb_in.p12a = (k & K_FIRE) ? (uint8_t)~PB_B1 : 0xff;
        pb_in.p12b = (uint8_t)(0xff & ~((k & K_LEFT) ? PB_LEFT : 0) & ~((k & K_RIGHT) ? PB_RIGHT : 0));

        int64_t t0 = esp_timer_get_time();
        pb_run_frame();
        if (!video_busy) {
            memcpy(vsnap, &pb_frame, sizeof *vsnap);
            video_busy = true;
            xSemaphoreGive(frame_ready);
        }
        __sync_synchronize();
        emu_sample_clock = (uint32_t)(pb_snd_clock() / YM_PER_SAMPLE);
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        { static uint32_t fno; if (++fno % 300 == 0) ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)frame_hash()); }
#endif
        // Settings / high scores: written to NVS a while after the game changes them.
        if (*eep_dirty) {
            if (!eeprom_t) eeprom_t = t0;
            else if (t0 - eeprom_t > 3000000) { eeprom_save(); eeprom_t = 0; }
        }

        // Pace on the audio clock: stay at most ~2.5 frames ahead of the DAC.
        while ((int32_t)(emu_sample_clock - synth_pos) > spf * 5 / 2) vTaskDelay(1);

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            pb_get_stats(&st);
            double el = (double)(now - stat_t);
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%, idle skipped 68k %.0f%% z80 %.0f%%), synth %.0f%%, video %.0f%%, shown %.1f fps, underruns %u (%s), heap %u",
                     stat_frames * 1e6 / el, 100.0 * stat_busy / el,
                     100.0 * (double)(st.m68k_cycles_skipped - st0.m68k_cycles_skipped) / (stat_frames * (PB_CPU_CLOCK / PB_FPS) + 1.0),
                     100.0 * (double)(st.z80_cycles_skipped - st0.z80_cycles_skipped) / (stat_frames * (PB_SND_CLOCK / PB_FPS) + 1.0),
                     100.0 * audio_busy_us / el, 100.0 * video_busy_us / el, frames_shown * 1e6 / el,
                     (unsigned)(audio_underruns - stat_under0), dac_audio_ok() ? "dac ok" : "dac OFF",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; stat_under0 = audio_underruns;
            audio_busy_us = 0; video_busy_us = 0; st0 = st;
        }
    }
}

// ----------------------------------------------------------------------------
void app_main(void)
{
    static const cyd_game_t info = { "pbobble", "PUZZLE BOBBLE", "TAITO 1994", {120, 220, 80} };
    cyd_boot(&info);

    // Big allocations first, while the heap is unfragmented
    pb_init();
    vsnap = heap_caps_malloc(sizeof *vsnap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    pal565 = heap_caps_calloc(4096, sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(vsnap && pal565);
    eeprom_load();
    ym = ym2610_create(PB_SND_CLOCK, AUDIO_RATE, 6, pb_adpcma_rom, sizeof pb_adpcma_rom,
                       pb_adpcma_rom, sizeof pb_adpcma_rom);
    assert(ym);

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
    ESP_LOGI(TAG, "free internal heap: %u (largest %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 8, NULL, 0);
    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
