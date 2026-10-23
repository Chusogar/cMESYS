// robocop_cyd.c - RoboCop (Data East, 1988) on the ESP32 CYD.
// Core: rc_core.c / rc_video.c / rc_sound.c (C port of dsp-cpp's dec0 driver).
//
//   Core 1 : 68000 (Musashi) + HuC6280 (protection CPU)
//   Core 0 : sound board (6502, YM2203, YM3812, OKI M6295) -> DAC GPIO26,
//            playfields/sprites renderer, ST7789 SPI/DMA, XPT2046 touch,
//            Nunchuk, buttons, serial keys
//
// Landscape 320x240: the 256x240 picture in the middle. The picture is drawn
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
#include "rc_core.h"
#include "rom_data.h"
#include "m68k/m68k.h"

static volatile uint32_t diag_emu_frames, diag_vid_frames;
static const char *TAG = "robocop";

#define GAME_X ((320 - RC_SCREEN_W) / 2)

// ----------------------------------------------------------------------------
// Controls (landscape): the touch screen in 3x3 zones over the picture
// ----------------------------------------------------------------------------
//   COIN+START | UP     | JUMP
//   LEFT       | ATTACK | RIGHT
//   LEFT+ATTACK| DOWN   | RIGHT+ATTACK
enum { K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8, K_ATTACK = 16, K_JUMP = 32, K_COIN = 64, K_START = 128 };

static const uint8_t zone_keys[3][3] = {
    { 0,                 K_UP,     K_JUMP },
    { K_LEFT,            K_ATTACK, K_RIGHT },
    { K_LEFT | K_ATTACK, K_DOWN,   K_RIGHT | K_ATTACK },
};
static const char *zone_label[3][3] = {
    { "COIN+START", "UP", "JUMP" },
    { "LEFT", "ATTACK", "RIGHT" },
    { "LEFT+ATTACK", "DOWN", "RIGHT+ATTACK" },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int x = c * 107, y = r * 80, w = c == 2 ? 106 : 107;
            uint16_t bg = (r == 0 && c == 0) ? rgb565(120, 30, 30) :
                          (zone_keys[r][c] & (K_ATTACK | K_JUMP)) ? rgb565(20, 60, 140) : C_DGREY;
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
        // Nunchuk: stick = 8-way joystick, Z = attack, C = jump
        if (nc.ok && !nc_combo) {
            int dx = nc.jx - 128, dy = nc.jy - 128;
            if (dx < -40) k |= K_LEFT;
            if (dx > 40) k |= K_RIGHT;
            if (dy > 40) k |= K_UP;
            if (dy < -40) k |= K_DOWN;
            if (nc.z) k |= K_ATTACK;
            if (nc.c) k |= K_JUMP;
        }
        // Serial keys: a/d/w/s move, j attack, k jump, 5 coin, 1 start
        uint8_t ch;
        while (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            int i = -1;
            switch (ch) {
            case 'a': case 'A': i = 0; break;
            case 'd': case 'D': i = 1; break;
            case 'w': case 'W': i = 2; break;
            case 's': case 'S': i = 3; break;
            case 'j': case 'J': i = 4; break;
            case 'k': case 'K': i = 5; break;
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
// Sound: the whole sound board runs on core 0, from the time-stamped latch
// writes core 1 queues (see rc_sound.h), as the DAC takes the samples.
// Core 1 stays at most ~2.5 frames ahead.
// ----------------------------------------------------------------------------
#define AUDIO_RATE 22050
#define CHUNK 256
static rc_core *core;
static volatile int64_t emu_time;                  // main CPU cycles emulated (end of the last frame)
static volatile uint32_t audio_underruns;
static volatile uint32_t audio_busy_us, video_busy_us;

static void audio_task(void *arg)
{
    static int16_t pcm[CHUNK + 8];
    static uint8_t a8[CHUNK + 8];
    static const int gain_shift[5] = { 0, 9, 8, 7, 6 };    // ~17-bit mix -> 8 bits, with gain
    rc_sound *s = core->sound;
    s->audio = pcm;
    s->audio_cap = CHUNK + 8;
    const int64_t chunk_time = (int64_t)RC_MAIN_CLOCK * CHUNK / AUDIO_RATE;
    for (;;) {
        // as far as core 1 has got: its last frame, or its latest event
        int64_t avail = emu_time;
        const uint32_t head = s->ev_head;
        if (head != s->ev_tail) {
            const int64_t t = s->ev[(head - 1) % RC_SND_EVENTS].time;
            if (t > avail) avail = t;
        }
        if (avail <= s->time) {                     // caught up with the emulation
            if (dac_audio_queued() < 64) audio_underruns++;     // and the DAC runs dry
            vTaskDelay(1);
            continue;
        }
        int64_t ta = esp_timer_get_time();
        const int64_t until = avail - s->time > chunk_time ? s->time + chunk_time : avail;
        s->audio_n = 0;
        rc_sound_run(s, until);
        int sh = gain_shift[cyd_cfg.volume % 5];
        for (int i = 0; i < s->audio_n; i++) {
            int v = sh ? 128 + (pcm[i] >> (sh - 1)) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        audio_busy_us += (uint32_t)(esp_timer_get_time() - ta);
        if (s->audio_n) dac_audio_push(a8, s->audio_n);      // blocks while the ring is full
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): draws the frame from the core's RAM
// ----------------------------------------------------------------------------
#define RBAND 8                          // lines rendered at a time (two per LCD band)
static uint16_t *cband;                  // RC_SCREEN_W x RBAND palette indices
static uint8_t *pband;                   // priority work
static uint32_t *pal565;                 // the 0x400 colours, RGB565 (IRAM if any: 32-bit accesses only)
static rc_sprite_list *sprites;
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown;
static rc_video vframe;

static void on_frame(void *user, rc_core *c)
{
    (void)user;
    if (video_busy) return;              // the screen is behind: skip this frame
    rc_get_video(c, &vframe);
    rc_build_sprites(&vframe, sprites);  // (the sprite buffer only changes on a DMA write)
    video_busy = true;
    xSemaphoreGive(frame_ready);
}

static void video_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        if (core->palette_dirty) {
            core->palette_dirty = false;
            for (int i = 0; i < 0x400; i++) {
                uint32_t c = rc_palette_rgb(core->palette, core->palette_ext, i);
                ((volatile uint32_t *)pal565)[i] = rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
            }
        }
        for (int y0 = 0; y0 < RC_SCREEN_H; y0 += BAND) {
            uint16_t *b = lcd_get_buf();
            for (int h = 0; h < BAND; h += RBAND) {
                rc_render(&vframe, sprites, &core->roms, y0 + h, y0 + h + RBAND, cband, pband);
                for (int i = 0; i < RC_SCREEN_W * RBAND; i++)
                    b[h * RC_SCREEN_W + i] = (uint16_t)((const volatile uint32_t *)pal565)[cband[i]];   // 32-bit loads (IRAM)
            }
            lcd_blit(GAME_X, y0, RC_SCREEN_W, BAND, b);
        }
        video_busy = false;
        frames_shown++;
        diag_vid_frames++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

#ifdef CYD_HEADLESS_TEST
// Progress check against the host build: hash of the composed picture.
static uint32_t frame_hash(void)
{
    static uint16_t *hband;
    static uint8_t *hpri;
    static rc_sprite_list *hspr;
    if (!hband) {
        hband = heap_caps_malloc(RC_SCREEN_W * RBAND * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        hpri = heap_caps_malloc(RC_SCREEN_W * RBAND, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        hspr = heap_caps_malloc(sizeof *hspr, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!hband || !hpri || !hspr) return 0;
    rc_video v;
    rc_get_video(core, &v);
    rc_build_sprites(&v, hspr);
    uint32_t h = 2166136261u;
    for (int y0 = 0; y0 < RC_SCREEN_H; y0 += RBAND) {
        rc_render(&v, hspr, &core->roms, y0, y0 + RBAND, hband, hpri);
        for (int i = 0; i < RC_SCREEN_W * RBAND; i++) h = (h ^ hband[i]) * 16777619u;
    }
    return h;
}
#endif

// ----------------------------------------------------------------------------
// Emulation task (core 1)
// ----------------------------------------------------------------------------
static void emu_task(void *arg)
{
    const int64_t frame_time = (int64_t)(RC_MAIN_CLOCK / RC_FPS);
    int64_t stat_t = esp_timer_get_time();
    uint32_t stat_frames = 0, stat_busy = 0, stat_under0 = 0;
    uint32_t sk0[2] = {0, 0};

    for (;;) {
        const uint8_t k = in_keys;
        rc_inputs in;
        memset(&in, 0, sizeof in);
        in.coin1 = k & K_COIN;
        in.start1 = k & K_START;
        in.p[0].left = k & K_LEFT;
        in.p[0].right = k & K_RIGHT;
        in.p[0].up = k & K_UP;
        in.p[0].down = k & K_DOWN;
        in.p[0].attack = k & K_ATTACK;
        in.p[0].jump = k & K_JUMP;
        rc_set_inputs(core, &in);

        int64_t t0 = esp_timer_get_time();
        rc_run_frame(core);
        __sync_synchronize();
        emu_time = core->time;
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
        diag_emu_frames++;
#ifdef CYD_HEADLESS_TEST
        { static uint32_t fno; if (++fno % 300 == 0) ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)frame_hash()); }
#endif
        // Pace on the audio clock: stay at most ~2.5 frames ahead of the sound board.
        while (core->time - core->sound->time > frame_time * 5 / 2) vTaskDelay(1);

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            const double cyc = stat_frames * (double)RC_MAIN_CLOCK / RC_FPS + 1;
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%, 68000 skipped %.0f%%, 6502 skipped %.0f%%), sound %.0f%%, video %.0f%%, shown %.1f fps, underruns %u (%s), heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * (core->skipped - sk0[0]) / cyc,
                     100.0 * (core->sound->skipped - sk0[1]) / (cyc * 0.15),
                     100.0 * audio_busy_us / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     (unsigned)(audio_underruns - stat_under0), dac_audio_ok() ? "dac ok" : "dac OFF",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; stat_under0 = audio_underruns;
            audio_busy_us = 0; video_busy_us = 0;
            sk0[0] = core->skipped; sk0[1] = core->sound->skipped;
        }
    }
}

// ----------------------------------------------------------------------------
// The busiest 1 KB pages of the 68000 program (measured on the host, attract
// mode and play: these 32 run 98.5% of the instructions). Copied from flash
// to RAM (IRAM first: only read 32 bits at a time), as many as fit.
static const uint8_t main_hot[] = {29, 4, 5, 15, 30, 6, 17, 7, 8, 100, 112, 111, 84, 2, 31, 16,
                                   98, 96, 11, 104, 18, 99, 22, 97, 68, 0, 23, 21, 103, 94, 10, 82};

static uint8_t *page_alloc(void)
{
    uint8_t *p = heap_caps_malloc(1024, MALLOC_CAP_32BIT | MALLOC_CAP_EXEC);
    if (!p && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > 40 * 1024)
        p = heap_caps_malloc(1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

static void copy_hot_pages(void)
{
    int n = 0;
    for (int i = 0; i < (int)sizeof main_hot; i++, n++) {
        uint8_t *p = page_alloc();
        if (!p) break;
        for (int k = 0; k < 1024; k += 4) *(volatile uint32_t *)(p + k) = *(const uint32_t *)(rc_main_rom + main_hot[i] * 1024 + k);
        core->main_page[main_hot[i]] = p;
    }
    ESP_LOGI(TAG, "%d hot program pages in RAM", n);
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
    static const cyd_game_t info = { "robocop", "ROBOCOP", "DATA EAST 1988", {120, 160, 220} };
    cyd_boot(&info);

    // Big allocations first, while the heap is unfragmented
    core = must_alloc(sizeof *core, "core");
    memset(core, 0, sizeof *core);
    core->sound = must_alloc(sizeof(rc_sound), "sound board");
    sprites = must_alloc(sizeof *sprites, "sprite list");
    cband = must_alloc(RC_SCREEN_W * RBAND * 2, "band");
    pband = must_alloc(RC_SCREEN_W * RBAND, "priority band");
    // the palette as 32-bit words: fits the IRAM heap, which the rest cannot use
    pal565 = heap_caps_malloc(0x400 * 4, MALLOC_CAP_32BIT | MALLOC_CAP_EXEC);
    if (!pal565) pal565 = must_alloc(0x400 * 4, "palette");
    ESP_LOGI(TAG, "core %u bytes, sound %u, free %u (largest %u)", (unsigned)sizeof *core, (unsigned)sizeof(rc_sound),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const rc_roms roms = { rc_main_rom, rc_sound_rom, rc_mcu_rom, rc_oki_rom,
                           rc_chars, rc_tiles1, rc_tiles2, rc_sprites };
    rc_init(core, &roms, AUDIO_RATE);
    core->idle_skip = true;
    core->sound_async = true;           // run by the audio task on core 0
    core->on_frame = on_frame;
    core->palette_dirty = true;

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

    copy_hot_pages();
    dac_audio_set_rate(AUDIO_RATE);
#if !defined(CYD_HEADLESS_TEST) || defined(CYD_QEMU_DAC)
    dac_audio_start(AUDIO_RATE);
#endif
    frame_ready = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "free internal heap: %u (largest %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    if (xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 8, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "task audio not created (heap %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "task input not created (heap %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "task video not created (heap %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1) != pdPASS)
        ESP_LOGE(TAG, "task emu not created (heap %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#ifdef CYD_DIAG
    // Diagnostics on the screen for the first 60 s: frames emulated / shown, 68000 PC
    for (int i = 0; i < 120; i++) {
        char l[64];
        snprintf(l, sizeof l, "E%u V%u PC%06X H%u", (unsigned)diag_emu_frames, (unsigned)diag_vid_frames,
                 (unsigned)m68k_get_reg(NULL, M68K_REG_PC), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        draw_text(0, 0, l, 1, C_YELLOW, C_BLACK);
        ESP_LOGI(TAG, "diag %s", l);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
#endif
}
