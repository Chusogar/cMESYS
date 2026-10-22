// gauntlet_cyd.c - Gauntlet (Atari, 1985) on the ESP32 CYD.
// Core: gauntlet_core.c (C port of dsp-cpp's gauntlet driver).
//
//   Core 1 : 68010 (Musashi) + 6502 + YM2151 timers, POKEY, TMS5220
//   Core 0 : YM2151 synthesis -> DAC GPIO26, playfield/MO/alpha renderer,
//            ST7789 SPI/DMA, XPT2046 touch, Nunchuk, buttons, serial keys
//
// Landscape 320x240: the 336x240 picture with 8 columns cut on each side
// (the playfield keeps its 232 columns minus 8, the status panel loses 8).
// One player (player 1, the Warrior); the touch screen is split in 3x3 zones.
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
#include "gauntlet_core.h"
#include "rom_data.h"
#include "m68k/m68k.h"

static const char *TAG = "gauntlet";

#define CROP_X   8        // 336 -> 320 columns

// ----------------------------------------------------------------------------
// Game controls (landscape): the touch screen in 3x3 zones over the picture
// ----------------------------------------------------------------------------
//   COIN+START |  UP   | MAGIC
//   LEFT       | FIRE  | RIGHT
//   FIRE       | DOWN  | MAGIC
// (hold FIRE to shoot, release it to move; MAGIC also joins the game)
enum { K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8, K_FIRE = 16, K_MAGIC = 32, K_COIN = 64, K_START = 128 };

static const uint8_t zone_keys[3][3] = {
    { 0,      K_UP,   K_MAGIC },
    { K_LEFT, K_FIRE, K_RIGHT },
    { K_FIRE, K_DOWN, K_MAGIC },
};
static const char *zone_label[3][3] = {
    { "COIN+START", "UP", "MAGIC" },
    { "LEFT", "FIRE", "RIGHT" },
    { "FIRE", "DOWN", "MAGIC" },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int x = c * 107, y = r * 80, w = c == 2 ? 106 : 107;
            uint16_t bg = (r == 0 && c == 0) ? rgb565(120, 30, 30) :
                          (zone_keys[r][c] & (K_FIRE | K_MAGIC)) ? rgb565(20, 60, 140) : C_DGREY;
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
        // BOOT, Nunchuk C+Z or the top-left zone: a coin, then START (magic)
        if (gpio_get_level(PIN_BOOT_BTN) == 0 || nc_combo || seq) {
            if (!boot_t0) boot_t0 = now;
            if (now - boot_t0 < 150000) k |= K_COIN;
            if (now - boot_t0 > 300000 && now - boot_t0 < 450000) k |= K_START;
        } else boot_t0 = 0;
        // Nunchuk: stick = 8-way joystick, Z = fire, C = magic
        if (nc.ok && !nc_combo) {
            int dx = nc.jx - 128, dy = nc.jy - 128;
            if (dx < -40) k |= K_LEFT;
            if (dx > 40) k |= K_RIGHT;
            if (dy > 40) k |= K_UP;
            if (dy < -40) k |= K_DOWN;
            if (nc.z) k |= K_FIRE;
            if (nc.c) k |= K_MAGIC;
        }
        // Serial keys: a/d/w/s move, j fire, k magic, 5 coin, 1 start
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
// Sound: the YM2151 is synthesized on core 0 from the register writes core 1
// makes (time-stamped in samples); POKEY + TMS5220 come as a sample stream.
// ----------------------------------------------------------------------------
#define AUDIO_RATE 22050
typedef struct { uint32_t when; uint8_t reg, val; uint16_t pad; } ymw_t;
#define YMQ 2048
static ymw_t ymq[YMQ];
static volatile uint32_t ymq_head, ymq_tail;
#define AUXQ 4096
static int16_t auxq[AUXQ];
static volatile uint32_t emu_sample_clock;          // aux samples made by core 1
static volatile uint32_t synth_pos;                 // samples synthesized by core 0
static volatile uint32_t audio_underruns;
static volatile uint32_t audio_busy_us, video_busy_us;
static ym2151 *ym_synth;
static gt_core *core;

static void ym_write_cb(void *user, uint8_t reg, uint8_t val, uint32_t when)
{
    (void)user;
    while (ymq_head - ymq_tail >= YMQ) vTaskDelay(1);   // full: let core 0 drain
    ymw_t *w = &ymq[ymq_head % YMQ];
    w->when = when; w->reg = reg; w->val = val;
    __sync_synchronize();
    ymq_head++;
}

// YM2151 reset by the sound program (LS259 Q0): the synthesizer follows when
// it gets there (a write to register 0 cannot mean it: queued as reg 0xff).
static void ym_reset_cb(void *user)
{
    ym_write_cb(user, 0xff, 0, core->sample_pos);
}

static void audio_task(void *arg)
{
    static uint8_t a8[256];
    static const int gain_shift[5] = { 0, 9, 8, 7, 6 };    // ~17-bit mix -> 8 bits, with gain
    int16_t last_aux = 0;
    for (;;) {
        int32_t ahead = (int32_t)(emu_sample_clock - synth_pos);
        int n = ahead > 256 ? 256 : (ahead > 64 ? ahead : 128);
        if (ahead <= 0) audio_underruns++;           // the emulation is late: samples made up
        int sh = gain_shift[cyd_cfg.volume % 5];
        const int ym_gain = gt_ym_gain(core);       // the sound program's mixer
        int64_t ta = esp_timer_get_time();
        for (int i = 0; i < n; i++) {
            while (ymq_tail != ymq_head && (int32_t)(ymq[ymq_tail % YMQ].when - synth_pos) <= 0) {
                ymw_t *w = &ymq[ymq_tail % YMQ];
                if (w->reg == 0xff) ym2151_reset(ym_synth);
                else ym2151_write_reg(ym_synth, w->reg, w->val);
                __sync_synchronize();
                ymq_tail++;
            }
            if ((int32_t)(emu_sample_clock - synth_pos) > 0) last_aux = auxq[synth_pos % AUXQ];
            int32_t s = ((ym2151_update(ym_synth) * ym_gain) >> 8) + last_aux;
            int v = sh ? 128 + (s >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            synth_pos++;
        }
        audio_busy_us += (uint32_t)(esp_timer_get_time() - ta);
        dac_audio_push(a8, n);                        // blocks while the ring is full
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): composes the snapshot taken at the start of vblank
// ----------------------------------------------------------------------------
static gt_video *vsnap;
static gt_renderer rend;
static uint16_t *cband;                  // GT_SCREEN_W x BAND palette entries
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown;

static void on_frame(void *user, gt_core *c)
{
    (void)user;
    if (video_busy) return;              // the screen is behind: skip this frame
    memcpy(vsnap, &c->v, sizeof *vsnap);
    video_busy = true;
    xSemaphoreGive(frame_ready);
}

static void video_task(void *arg)
{
    static uint16_t pal565[0x400];
    for (;;) {
        xSemaphoreTake(frame_ready, portMAX_DELAY);
        int64_t tv = esp_timer_get_time();
        for (int i = 0; i < 0x400; i++) {
            uint32_t c = gt_palette_rgb(vsnap, i);
            pal565[i] = rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
        }
        for (int y0 = 0; y0 < GT_SCREEN_H; y0 += BAND) {
            gt_compose(&rend, &core->roms, vsnap, y0, y0 + BAND, cband);
            uint16_t *b = lcd_get_buf();
            for (int y = 0; y < BAND; y++) {
                const uint16_t *src = cband + y * GT_SCREEN_W + CROP_X;
                uint16_t *dst = b + y * 320;
                for (int x = 0; x < 320; x++) dst[x] = pal565[src[x]];
            }
            lcd_blit(0, y0, 320, BAND, b);
        }
        video_busy = false;
        frames_shown++;
        video_busy_us += (uint32_t)(esp_timer_get_time() - tv);
    }
}

#ifdef CYD_HEADLESS_TEST
// Progress check against the host build: hash of the composed picture.
// (Own renderer and buffers: the video task uses the others at the same time.)
static uint32_t frame_hash(void)
{
    static gt_renderer hr;
    static uint16_t *hband, *hmo;
    if (!hband) {
        hband = heap_caps_malloc(GT_SCREEN_W * BAND * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        hmo = heap_caps_malloc(GT_SCREEN_W * BAND * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        hr.mobuf = hmo;
        if (hmo) gt_renderer_init(&hr);
    }
    if (!hband || !hmo) return 0;
    uint32_t h = 2166136261u;
    for (int y0 = 0; y0 < GT_SCREEN_H; y0 += BAND) {
        gt_compose(&hr, &core->roms, &core->v, y0, y0 + BAND, hband);
        for (int i = 0; i < GT_SCREEN_W * BAND; i++) h = (h ^ hband[i]) * 16777619u;
    }
    return h;
}
#endif

// ----------------------------------------------------------------------------
// EEPROM (settings, high scores) in NVS
// ----------------------------------------------------------------------------
static void eeprom_load(void)
{
    nvs_handle_t h;
    if (nvs_open("gauntlet", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof core->eeprom;
        if (nvs_get_blob(h, "eeprom", core->eeprom, &sz) != ESP_OK || sz != sizeof core->eeprom)
            memset(core->eeprom, 0xff, sizeof core->eeprom);
        nvs_close(h);
    }
}

static void eeprom_save(void)
{
    nvs_handle_t h;
    if (nvs_open("gauntlet", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "eeprom", core->eeprom, sizeof core->eeprom);
        nvs_commit(h);
        nvs_close(h);
    }
    core->eeprom_dirty = false;
}

// ----------------------------------------------------------------------------
// Emulation task (core 1)
// ----------------------------------------------------------------------------
static void emu_task(void *arg)
{
    const int32_t spf = (int32_t)(AUDIO_RATE / GT_FPS);   // ~368 samples per frame
    static int16_t aux[1024];
    core->audio = aux;
    core->audio_cap = 1024;
    int64_t stat_t = esp_timer_get_time(), eeprom_t = 0;
    uint32_t stat_frames = 0, stat_busy = 0, stat_under0 = 0, skip0 = 0, sskip0 = 0;

    for (;;) {
        uint8_t k = in_keys;
        gt_inputs in = {0};
        in.coin[0] = k & K_COIN;
        in.p[0].up = k & K_UP;
        in.p[0].down = k & K_DOWN;
        in.p[0].left = k & K_LEFT;
        in.p[0].right = k & K_RIGHT;
        in.p[0].fire = k & K_FIRE;
        in.p[0].magic = k & (K_MAGIC | K_START);
        gt_set_inputs(core, &in);

        int64_t t0 = esp_timer_get_time();
        uint32_t pos0 = core->sample_pos;
        gt_run_frame(core);
        for (int i = 0; i < core->audio_n; i++) auxq[(pos0 + (uint32_t)i) % AUXQ] = aux[i];
        __sync_synchronize();
        emu_sample_clock = core->sample_pos;
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        { static uint32_t fno; if (++fno % 300 == 0) ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)frame_hash()); }
#endif
        // Settings / high scores: written to NVS a while after the game changes them.
        if (core->eeprom_dirty) {
            if (!eeprom_t) eeprom_t = t0;
            else if (t0 - eeprom_t > 3000000) { eeprom_save(); eeprom_t = 0; }
        }

        // Pace on the audio clock: stay at most ~2.5 frames ahead of the DAC.
        while ((int32_t)(core->sample_pos - synth_pos) > spf * 5 / 2) vTaskDelay(1);

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%, idle skipped 68k %.0f%% 6502 %.0f%%), synth %.0f%%, video %.0f%%, shown %.1f fps, underruns %u (%s), heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * (double)(core->skipped - skip0) / (stat_frames * (double)(GT_HTOTAL * GT_SCANLINES) + 1),
                     100.0 * (double)(core->snd_skipped - sskip0) / (stat_frames * (double)(GT_HTOTAL / 4 * GT_SCANLINES) + 1),
                     100.0 * audio_busy_us / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     (unsigned)(audio_underruns - stat_under0), dac_audio_ok() ? "dac ok" : "dac OFF",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; stat_under0 = audio_underruns;
            audio_busy_us = 0; video_busy_us = 0; skip0 = core->skipped; sskip0 = core->snd_skipped;
        }
    }
}

// The busiest 1 KB pages of the 68010 program (measured on the host, attract
// mode and play: 99.8% of the instructions in the 48 first), copied from
// flash to RAM, as many as the heap allows.
static const uint16_t hot_rom_pages[] = {259, 278, 17, 32, 276, 289, 277, 265, 279, 274, 270, 18, 319, 256, 257, 16,
                                         290, 2, 281, 1, 266, 33, 288, 283, 0, 313, 35, 264, 269, 37, 38, 36,
                                         267, 304, 10, 280, 11, 268, 14, 260, 272, 302, 261, 273, 301, 306, 275, 34};

static void copy_hot_pages(void)
{
    // 6502: $4000-$59FF and $8000-$81FF (all but 0.1% of its instructions)
    uint8_t *s = heap_caps_malloc(0x1a00 + 0x200, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s) {
        memcpy(s, gt_sound_rom, 0x1a00);
        memcpy(s + 0x1a00, gt_sound_rom + 0x4000, 0x200);
        for (int p = 0x40; p < 0x5a; p++) core->snd.page_r[p] = s + (p - 0x40) * 0x100;
        for (int p = 0x80; p <= 0x81; p++) core->snd.page_r[p] = s + 0x1a00 + (p - 0x80) * 0x100;
    }
    int n = sizeof hot_rom_pages / sizeof hot_rom_pages[0];
    uint8_t *ram = NULL;
    for (; n >= 8 && !ram; n -= 8) {
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < (size_t)n * 1024 + 24 * 1024) continue;
        ram = heap_caps_malloc((size_t)n * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (ram) break;
    }
    if (!ram) { ESP_LOGW(TAG, "no RAM for hot ROM pages"); return; }
    ESP_LOGI(TAG, "%d hot 68010 ROM pages in RAM", n);
    for (int i = 0; i < n; i++) {
        memcpy(ram + i * 1024, gt_main_rom + hot_rom_pages[i] * 1024, 1024);
        core->main_page[hot_rom_pages[i]] = ram + i * 1024;
    }
}

// ----------------------------------------------------------------------------
void app_main(void)
{
    static const cyd_game_t info = { "gauntlet", "GAUNTLET", "ATARI 1985", {255, 90, 40} };
    cyd_boot(&info);

    // Big allocations first, while the heap is unfragmented
    core = heap_caps_malloc(sizeof *core, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    vsnap = heap_caps_malloc(sizeof *vsnap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    cband = heap_caps_malloc(GT_SCREEN_W * BAND * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    rend.mobuf = heap_caps_malloc(GT_SCREEN_W * BAND * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ym_synth = heap_caps_malloc(sizeof *ym_synth, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(core && vsnap && cband && rend.mobuf && ym_synth);
    gt_renderer_init(&rend);
    ESP_LOGI(TAG, "core %u bytes, video snapshot %u", (unsigned)sizeof *core, (unsigned)sizeof *vsnap);
    gt_roms roms;
    gt_rom_data(&roms);
    gt_init(core, &roms, AUDIO_RATE);
    core->idle_skip = true;
    core->pk.approx_mute = true;         // silent POKEY: counters frozen (see pokey.h)
    core->ym_external = true;
    core->ym_write = ym_write_cb;
    core->ym_reset_cb = ym_reset_cb;
    core->on_frame = on_frame;
    eeprom_load();
    memset(ym_synth, 0, sizeof *ym_synth);
    ym2151_init(ym_synth, GT_YM_CLOCK, AUDIO_RATE);

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

    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 8, NULL, 0);
    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
