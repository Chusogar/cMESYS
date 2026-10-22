// indytemp_cyd.c - Indiana Jones and the Temple of Doom (Atari System 1) on
// the ESP32 CYD. Core: indytemp_core.c (C port of dsp-cpp's atari_system1).
//
//   Core 1 : 68010 (Musashi) + 6502 + YM2151 timers, POKEY, TMS5220, VIA
//   Core 0 : YM2151 synthesis -> DAC GPIO26, playfield/MO/alpha renderer,
//            ST7789 SPI/DMA, XPT2046 touch, Nunchuk, buttons, serial keys
//
// Landscape 320x240: the 336x240 picture with 8 columns cut on each side;
// the touch screen is split in 3x3 zones (see below).
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
#include "indytemp_core.h"
#include "rom_data.h"
#include "m68k/m68k.h"

static const char *TAG = "indytemp";

#define CROP_X   8        // 336 -> 320 columns

// ----------------------------------------------------------------------------
// Game controls (landscape): the touch screen in 3x3 zones over the picture
// ----------------------------------------------------------------------------
//   COIN+START |  UP   | RIGHT BTN (jump)
//   LEFT       | WHIP  | RIGHT
//   LEFT BTN   | DOWN  | RIGHT BTN
// (the game's "left button" is the whip, the "right button" jumps; at the
//  SELECT screen right button = 3 lives, left button = 7 lives)
enum { K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8, K_B1 = 16, K_B2 = 32, K_COIN = 64, K_START = 128 };

static const uint8_t zone_keys[3][3] = {
    { 0,     K_UP,   K_B2 },
    { K_LEFT, K_B1,  K_RIGHT },
    { K_B1,  K_DOWN, K_B2 },
};
static const char *zone_label[3][3] = {
    { "COIN+START", "UP", "JUMP (R)" },
    { "LEFT", "WHIP (L)", "RIGHT" },
    { "WHIP (L)", "DOWN", "JUMP (R)" },
};

static void draw_help(void)
{
    fill_rect(0, 0, 320, 240, C_BLACK);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int x = c * 107, y = r * 80, w = c == 2 ? 106 : 107;
            uint16_t bg = (r == 0 && c == 0) ? rgb565(120, 30, 30) :
                          (zone_keys[r][c] & (K_B1 | K_B2)) ? rgb565(20, 60, 140) : C_DGREY;
            fill_rect(x + 1, y + 1, w - 2, 78, bg);
            draw_text_c(x + w / 2, y + 36, zone_label[r][c], 1, C_WHITE, bg);
        }
    draw_text_c(160, 4, "TOUCH ZONES", 1, C_YELLOW, C_DGREY);
    lcd_flush();
}

static volatile uint8_t in_keys;
static volatile int64_t ser_until[8];
static volatile int64_t coin_seq_t0;

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
        // Nunchuk: stick = joystick, Z = left button (whip), C = right button (jump)
        if (nc.ok && !nc_combo) {
            if (nc_left(&nc))  k |= K_LEFT;
            if (nc_right(&nc)) k |= K_RIGHT;
            if (nc_up(&nc))    k |= K_UP;
            if (nc_down(&nc))  k |= K_DOWN;
            if (nc.z) k |= K_B1;
            if (nc.c) k |= K_B2;
        }
        // Serial keys: a/d/w/s move, j whip, k jump, 5 coin, 1 start
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
#define YMQ 4096
static ymw_t ymq[YMQ];
static volatile uint32_t ymq_head, ymq_tail;
#define AUXQ 4096
static int16_t auxq[AUXQ];
static volatile uint32_t emu_sample_clock;          // aux samples made by core 1
static volatile uint32_t synth_pos;                 // samples synthesized by core 0
static volatile uint32_t audio_underruns;
static volatile uint32_t audio_busy_us, video_busy_us;
static ym2151 ym_synth;

static void ym_write_cb(void *user, uint8_t reg, uint8_t val, uint32_t when)
{
    (void)user;
    while (ymq_head - ymq_tail >= YMQ) vTaskDelay(1);   // full: let core 0 drain
    ymw_t *w = &ymq[ymq_head % YMQ];
    w->when = when; w->reg = reg; w->val = val;
    __sync_synchronize();
    ymq_head++;
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
        int64_t ta = esp_timer_get_time();
        for (int i = 0; i < n; i++) {
            while (ymq_tail != ymq_head && (int32_t)(ymq[ymq_tail % YMQ].when - synth_pos) <= 0) {
                ymw_t *w = &ymq[ymq_tail % YMQ];
                ym2151_write_reg(&ym_synth, w->reg, w->val);
                __sync_synchronize();
                ymq_tail++;
            }
            if ((int32_t)(emu_sample_clock - synth_pos) > 0) last_aux = auxq[synth_pos % AUXQ];
            int32_t s = ym2151_update(&ym_synth) + last_aux;
            int v = sh ? 128 + (s >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            synth_pos++;
        }
        audio_busy_us += (uint32_t)(esp_timer_get_time() - ta);
        dac_audio_push(a8, n);                        // blocks while the ring is full
    }
}

// ----------------------------------------------------------------------------
// Video task (core 0): composes the snapshot taken at line 239
// ----------------------------------------------------------------------------
static indy_core *core;
static indy_video *vsnap;
static indy_mo_list *mol;
static uint16_t *cband;                  // INDY_SCREEN_W x BAND palette entries
static SemaphoreHandle_t frame_ready;
static volatile bool video_busy;
static volatile uint32_t frames_shown;

static void on_frame(void *user, indy_core *c)
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
        indy_build_mo_list(&core->gfx, vsnap, mol);
        for (int i = 0; i < 0x400; i++) {
            uint32_t c = indy_palette_rgb(vsnap, i);
            pal565[i] = rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
        }
        for (int y0 = 0; y0 < INDY_SCREEN_H; y0 += BAND) {
            indy_compose(&core->gfx, vsnap, mol, y0, y0 + BAND, cband);
            uint16_t *b = lcd_get_buf();
            for (int y = 0; y < BAND; y++) {
                const uint16_t *src = cband + y * INDY_SCREEN_W + CROP_X;
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
// (Own buffers: the video task uses the others at the same time.)
static uint32_t frame_hash(void)
{
    static indy_video hv;
    static indy_mo_list hmo;
    static uint16_t hband[INDY_SCREEN_W * BAND];
    memcpy(&hv, &core->v, sizeof hv);
    indy_build_mo_list(&core->gfx, &hv, &hmo);
    uint32_t h = 2166136261u;
    for (int y0 = 0; y0 < INDY_SCREEN_H; y0 += BAND) {
        indy_compose(&core->gfx, &hv, &hmo, y0, y0 + BAND, hband);
        for (int i = 0; i < INDY_SCREEN_W * BAND; i++) h = (h ^ hband[i]) * 16777619u;
    }
    return h;
}
#endif

// ----------------------------------------------------------------------------
// EEPROM (high scores) in NVS
// ----------------------------------------------------------------------------
static void eeprom_load(void)
{
    nvs_handle_t h;
    if (nvs_open("indyt", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof core->eeprom;
        if (nvs_get_blob(h, "eeprom", core->eeprom, &sz) != ESP_OK || sz != sizeof core->eeprom)
            memset(core->eeprom, 0xff, sizeof core->eeprom);
        nvs_close(h);
    }
}

static void eeprom_save(void)
{
    nvs_handle_t h;
    if (nvs_open("indyt", NVS_READWRITE, &h) == ESP_OK) {
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
    const int32_t spf = (int32_t)(AUDIO_RATE / INDY_FPS);   // ~368 samples per frame
    static int16_t aux[1024];
    core->audio = aux;
    core->audio_cap = 1024;
    int64_t stat_t = esp_timer_get_time(), eeprom_t = 0;
    uint32_t stat_frames = 0, stat_busy = 0, stat_under0 = 0, skip0 = 0;

    for (;;) {
        uint8_t k = in_keys;
        indy_inputs in = {
            .coin1 = k & K_COIN, .start = k & K_START, .button1 = k & K_B1, .button2 = k & K_B2,
            .up = k & K_UP, .down = k & K_DOWN, .left = k & K_LEFT, .right = k & K_RIGHT,
        };
        indy_set_inputs(core, &in);

        int64_t t0 = esp_timer_get_time();
        uint32_t pos0 = core->sample_pos;
        indy_run_frame(core);
        for (int i = 0; i < core->audio_n; i++) auxq[(pos0 + (uint32_t)i) % AUXQ] = aux[i];
        __sync_synchronize();
        emu_sample_clock = core->sample_pos;
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef CYD_HEADLESS_TEST
        { static uint32_t fno; if (++fno % 300 == 0) ESP_LOGI(TAG, "HASH frame %u %08x", (unsigned)fno, (unsigned)frame_hash()); }
#endif
        // High scores: written to NVS a while after the game changes them.
        if (core->eeprom_dirty) {
            if (!eeprom_t) eeprom_t = t0;
            else if (t0 - eeprom_t > 3000000) { eeprom_save(); eeprom_t = 0; }
        }

        // Pace on the audio clock: stay at most ~2.5 frames ahead of the DAC.
        while ((int32_t)(core->sample_pos - synth_pos) > spf * 5 / 2) vTaskDelay(1);

        int64_t now = esp_timer_get_time();
        if (now - stat_t >= 5000000) {
            ESP_LOGI(TAG, "emu %.1f fps (cpu1 %.0f%%, 68k idle skipped %.0f%%), synth %.0f%%, video %.0f%%, shown %.1f fps, underruns %u (%s), heap %u",
                     stat_frames * 1e6 / (double)(now - stat_t),
                     100.0 * stat_busy / (double)(now - stat_t),
                     100.0 * (double)(core->skipped - skip0) / (stat_frames * 119500.0 + 1),
                     100.0 * audio_busy_us / (double)(now - stat_t),
                     100.0 * video_busy_us / (double)(now - stat_t),
                     frames_shown * 1e6 / (double)(now - stat_t),
                     (unsigned)(audio_underruns - stat_under0), dac_audio_ok() ? "dac ok" : "dac OFF",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            stat_t = now; stat_frames = 0; stat_busy = 0; frames_shown = 0; stat_under0 = audio_underruns;
            audio_busy_us = 0; video_busy_us = 0; skip0 = core->skipped;
        }
    }
}

// The busiest 1 KB pages of the 68000 program (measured on the host: 99.8%
// of the instruction fetches), copied from flash to RAM.
static const uint16_t hot_rom_pages[] = {66,182,9,70,69,125,83,89,19,73,1,88,15,84,72,183,0,67,12,92,
                                         14,11,94,82,20,91,90,124,108,80,76,167,178,64,85,97,2,107,106,109};

static void copy_hot_pages(void)
{
    // 6502: $4000-$57FF and $FE00-$FFFF (99.7% of its instructions)
    uint8_t *s = heap_caps_malloc(0x1800 + 0x200, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s) {
        memcpy(s, indy_sound_rom, 0x1800);
        memcpy(s + 0x1800, indy_sound_rom + 0xbe00, 0x200);
        for (int p = 0x40; p < 0x58; p++) core->snd.page_r[p] = s + (p - 0x40) * 0x100;
        for (int p = 0xfe; p <= 0xff; p++) core->snd.page_r[p] = s + 0x1800 + (p - 0xfe) * 0x100;
    }
    const int n = sizeof hot_rom_pages / sizeof hot_rom_pages[0];
    uint8_t *ram = heap_caps_malloc((size_t)n * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ram) { ESP_LOGW(TAG, "no RAM for hot ROM pages"); return; }
    for (int i = 0; i < n; i++) {
        memcpy(ram + i * 1024, indy_main_rom + hot_rom_pages[i] * 1024, 1024);
        core->main_page[hot_rom_pages[i]] = ram + i * 1024;
    }
}

// ----------------------------------------------------------------------------
void app_main(void)
{
    static const cyd_game_t info = { "indyt", "INDIANA JONES", "AND THE TEMPLE OF DOOM", {255, 220, 0} };
    cyd_boot(&info);

    // Big allocations first, while the heap is unfragmented
    core = heap_caps_malloc(sizeof *core, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    vsnap = heap_caps_malloc(sizeof *vsnap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    mol = heap_caps_calloc(1, sizeof *mol, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    cband = heap_caps_malloc(INDY_SCREEN_W * BAND * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(core && vsnap && mol && cband);
    ESP_LOGI(TAG, "core %u bytes, video snapshot %u, MO list %u", (unsigned)sizeof *core,
             (unsigned)sizeof *vsnap, (unsigned)sizeof *mol);
    indy_roms roms;
    indy_rom_data(&roms);
    indy_init(core, &roms, AUDIO_RATE);
    core->idle_skip = true;
    core->pk.approx_mute = true;         // muted POKEY: counters frozen (see pokey.h)
    core->ym_external = true;
    core->ym_write = ym_write_cb;
    core->on_frame = on_frame;
    eeprom_load();
    ym2151_init(&ym_synth, INDY_YM_CLOCK, AUDIO_RATE);

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
