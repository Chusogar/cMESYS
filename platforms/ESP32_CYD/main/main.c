// main.c - Track & Field for ESP32 "Cheap Yellow Display" (ESP32-2432S028,
// ST7789 240x320 SPI variant). Port of zxtiny/zxm/trackfld.c.
//
//   Core 1 : Konami-1 6809 + sound Z80 + SN76489 / VLM5030 / DAC -> GPIO26
//   Core 0 : tile/sprite renderer, ST7789 SPI/DMA, XPT2046 touch, buttons
//
// Landscape 320x240: game 256x224 at (32,8), touch buttons on both sides.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/dac_continuous.h"
#include "driver/uart.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "tf_core.h"
#include "dac_audio.h"
#include "font5x7.h"
#include "nunchuk.h"

static const char *TAG = "trackfld";

// ----------------------------------------------------------------------------
// Board pins (ESP32-2432S028 / CYD, ST7789 variant)
// ----------------------------------------------------------------------------
#define LCD_HOST       SPI2_HOST
#define PIN_LCD_SCLK   14
#define PIN_LCD_MOSI   13
#define PIN_LCD_MISO   12
#define PIN_LCD_CS     15
#define PIN_LCD_DC     2
#define PIN_LCD_RST    -1
#define PIN_LCD_BL     21
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ     (40 * 1000 * 1000)
#endif

#define PIN_T_CLK      25
#define PIN_T_MOSI     32
#define PIN_T_MISO     39
#define PIN_T_CS       33
#define PIN_T_IRQ      36

#define PIN_BOOT_BTN   0     // BOOT button: coin + start
// Wii Nunchuk on I2C: SDA = GPIO22, SCL = GPIO27 (same wiring as Galagino)

#define PIN_LED_R      4
#define PIN_LED_G      16
#define PIN_LED_B      17

#define BAND           16

// Runtime screen geometry (portrait for setup, landscape for the game)
static int scr_w = 240, scr_h = 320;
static bool landscape;

#define GAME_X   32
#define GAME_Y   8

// ----------------------------------------------------------------------------
// Settings (NVS)
// ----------------------------------------------------------------------------
typedef struct {
    uint8_t invert, bgr, rot180, volume;
    uint8_t cal_ok, cal_swap, cal_rot, land_map;
    int16_t cx0, cx1, cy0, cy1;
    uint8_t land_ok, land_rot, pad[2];
} settings_t;

static settings_t S = { .invert = 1, .bgr = 0, .rot180 = 0, .volume = 2 };

static void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("trkfd", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof S;
        settings_t t;
        if (nvs_get_blob(h, "cfg", &t, &sz) == ESP_OK && sz == sizeof S) S = t;
        nvs_close(h);
    }
}

static void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("trkfd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", &S, sizeof S);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ----------------------------------------------------------------------------
// LCD
// ----------------------------------------------------------------------------
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t lcd_io;
static SemaphoreHandle_t lcd_done;
static bool lcd_ok;
static uint16_t *band_buf[2];
static int buf_idx;

static bool lcd_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(lcd_done, &hp);
    return hp == pdTRUE;
}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    if (S.bgr) { uint8_t t = r; r = b; b = t; }
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void lcd_orientation(void)
{
    if (!lcd_ok) return;
    esp_lcd_panel_invert_color(panel, S.invert != 0);
    if (!landscape) {
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, S.rot180 != 0, S.rot180 != 0);
        scr_w = 240; scr_h = 320;
    } else {
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, S.rot180 == 0, S.rot180 != 0);
        scr_w = 320; scr_h = 240;
    }
}

static void lcd_init(void)
{
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(PIN_LCD_BL, 0);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK, .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 320 * BAND * 2 + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    lcd_done = xSemaphoreCreateCounting(2, 2);
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = PIN_LCD_DC, .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_HZ, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 0, .trans_queue_depth = 4,
        .on_color_trans_done = lcd_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &lcd_io));
    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd_io, &pc, &panel));
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    for (int i = 0; i < 2; i++) {
        band_buf[i] = heap_caps_malloc(320 * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(band_buf[i]);
    }
    lcd_ok = true;
    lcd_orientation();
    esp_lcd_panel_disp_on_off(panel, true);
}

static uint16_t *lcd_get_buf(void)
{
    if (!lcd_ok) { static uint16_t scratch[320 * BAND]; return scratch; }
    if (xSemaphoreTake(lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
        static int warned;                     // never freeze on a lost "transfer done"
        if (!warned++) ESP_LOGE(TAG, "LCD transfer timeout");
    }
    uint16_t *b = band_buf[buf_idx];
    buf_idx ^= 1;
    return b;
}

static void lcd_flush(void)
{
    if (!lcd_ok) return;
    int got = 0;
    for (int i = 0; i < 2; i++) if (xSemaphoreTake(lcd_done, pdMS_TO_TICKS(250)) == pdTRUE) got++;
    while (got--) xSemaphoreGive(lcd_done);
}

static void lcd_blit(int x, int y, int w, int h, uint16_t *buf)
{
    if (!lcd_ok) return;
    esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, buf);
}

static void fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    while (h > 0) {
        int rows = (320 * BAND) / w;
        if (rows > h) rows = h;
        uint16_t *b = lcd_get_buf();
        for (int i = 0; i < w * rows; i++) b[i] = c;
        lcd_blit(x, y, w, rows, b);
        y += rows; h -= rows;
    }
}

static void draw_char(int x, int y, char ch, int sc, uint16_t fg, uint16_t bg)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 'Z') ch = '?';
    const uint8_t *g = font5x7[ch - 32];
    int w = 6 * sc, h = 8 * sc;
    uint16_t *b = lcd_get_buf();
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            int cx = px / sc, cy = py / sc;
            bool on = cx < 5 && cy < 7 && ((g[cx] >> cy) & 1);
            b[py * w + px] = on ? fg : bg;
        }
    lcd_blit(x, y, w, h, b);
}

static void draw_text(int x, int y, const char *s, int sc, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += 6 * sc) draw_char(x, y, *s, sc, fg, bg);
}

static void draw_text_c(int cx, int y, const char *s, int sc, uint16_t fg, uint16_t bg)
{
    draw_text(cx - (int)strlen(s) * 3 * sc, y, s, sc, fg, bg);
}

static void draw_arrow(int x, int y, int w, int h, int dir, uint16_t fg, uint16_t bg)
{
    uint16_t *b = lcd_get_buf();
    int cx = w / 2, cy = h / 2, r = (w < h ? w : h) / 3;
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            int dx = px - cx, dy = py - cy, a, l;
            switch (dir) {
            case 0: a = -dy; l = dx; break;
            case 1: a = dy;  l = dx; break;
            case 2: a = -dx; l = dy; break;
            default: a = dx; l = dy; break;
            }
            bool on = a >= -r / 2 && a <= r && abs(l) * 3 <= (r - a) * 2;
            b[py * w + px] = on ? fg : bg;
        }
    lcd_blit(x, y, w, h, b);
}

#define C_BLACK  rgb565(0, 0, 0)
#define C_WHITE  rgb565(255, 255, 255)
#define C_RED    rgb565(255, 0, 0)
#define C_GREEN  rgb565(0, 255, 0)
#define C_BLUE   rgb565(0, 0, 255)
#define C_YELLOW rgb565(255, 220, 0)
#define C_GREY   rgb565(60, 60, 70)
#define C_DGREY  rgb565(30, 30, 36)
#define C_CYAN   rgb565(80, 200, 255)

static void draw_button(int x, int y, int w, int h, const char *label, int sc, uint16_t fg, uint16_t bg)
{
    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, w, 1, C_GREY);
    fill_rect(x, y + h - 1, w, 1, C_BLACK);
    if (label) draw_text_c(x + w / 2, y + (h - 8 * sc) / 2, label, sc, fg, bg);
}

// ----------------------------------------------------------------------------
// XPT2046 touch (bit-banged)
// ----------------------------------------------------------------------------
static void xpt_init(void)
{
    gpio_config_t o = { .pin_bit_mask = (1ULL << PIN_T_CLK) | (1ULL << PIN_T_MOSI) | (1ULL << PIN_T_CS),
                        .mode = GPIO_MODE_OUTPUT };
    gpio_config(&o);
    gpio_config_t i = { .pin_bit_mask = (1ULL << PIN_T_MISO) | (1ULL << PIN_T_IRQ), .mode = GPIO_MODE_INPUT };
    gpio_config(&i);
    gpio_set_level(PIN_T_CS, 1);
    gpio_set_level(PIN_T_CLK, 0);
}

static uint16_t xpt_cmd(uint8_t cmd)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_T_MOSI, (cmd >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_T_CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_T_CLK, 0);
    }
    gpio_set_level(PIN_T_MOSI, 0);
    uint16_t r = 0;
    for (int i = 0; i < 16; i++) {
        gpio_set_level(PIN_T_CLK, 1);
        esp_rom_delay_us(1);
        r = (uint16_t)((r << 1) | gpio_get_level(PIN_T_MISO));
        gpio_set_level(PIN_T_CLK, 0);
        esp_rom_delay_us(1);
    }
    return (r >> 3) & 0x0FFF;
}

static bool xpt_read_raw(int *ra, int *rb)
{
    gpio_set_level(PIN_T_CS, 0);
    int z1 = xpt_cmd(0xB1);
    int z2 = xpt_cmd(0xC1);
    int z = z1 + 4095 - z2;
    bool down = z > 400 && z1 > 50;
    int sa = 0, sb = 0;
    if (down) {
        xpt_cmd(0xD1);
        int a[4], b[4];
        for (int i = 0; i < 4; i++) { a[i] = xpt_cmd(0xD1); b[i] = xpt_cmd(0x91); }
        for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) {
            if (a[j] < a[i]) { int t = a[i]; a[i] = a[j]; a[j] = t; }
            if (b[j] < b[i]) { int t = b[i]; b[i] = b[j]; b[j] = t; }
        }
        sa = (a[1] + a[2]) / 2; sb = (b[1] + b[2]) / 2;
    }
    xpt_cmd(0xD0);
    gpio_set_level(PIN_T_CS, 1);
    if (down) { *ra = sa; *rb = sb; }
    return down;
}

// Physical portrait coordinates (as with MADCTL = 0)
static bool touch_phys(int *px, int *py)
{
    int a, b;
    if (!S.cal_ok || !xpt_read_raw(&a, &b)) return false;
    int rx = S.cal_swap ? b : a, ry = S.cal_swap ? a : b;
    int sx = 20 + (rx - S.cx0) * 200 / (S.cx1 - S.cx0);
    int sy = 20 + (ry - S.cy0) * 280 / (S.cy1 - S.cy0);
    if (S.cal_rot) { sx = 239 - sx; sy = 319 - sy; }
    *px = sx; *py = sy;
    return true;
}

// Coordinates in the current screen orientation
static bool touch_get(int *x, int *y)
{
    int px, py;
    if (!touch_phys(&px, &py)) return false;
    int lx, ly;
    if (!landscape) {
        lx = S.rot180 ? 239 - px : px;
        ly = S.rot180 ? 319 - py : py;
    } else {
        int m = S.land_map ^ (S.rot180 != S.land_rot);
        if (!m) { lx = py; ly = 239 - px; }
        else    { lx = 319 - py; ly = px; }
    }
    if (lx < 0) lx = 0;
    if (lx >= scr_w) lx = scr_w - 1;
    if (ly < 0) ly = 0;
    if (ly >= scr_h) ly = scr_h - 1;
    *x = lx; *y = ly;
    return true;
}

static void crosshair(int x, int y, uint16_t c)
{
    fill_rect(x - 10, y, 21, 1, c);
    fill_rect(x, y - 10, 1, 21, c);
    fill_rect(x - 2, y - 2, 5, 5, c);
}

static void wait_release(void)
{
    int a, b;
    while (xpt_read_raw(&a, &b)) vTaskDelay(pdMS_TO_TICKS(20));
}

static void cal_point(int x, int y, int *ra, int *rb)
{
    crosshair(x, y, C_WHITE);
    lcd_flush();
    wait_release();
    int n = 0, a, b; long sa = 0, sb = 0;
    while (n < 16) {
        if (xpt_read_raw(&a, &b)) { if (n >= 4) { sa += a; sb += b; } n++; }
        else n = 0, sa = sb = 0;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    *ra = (int)(sa / 12); *rb = (int)(sb / 12);
    crosshair(x, y, C_GREEN);
    lcd_flush();
    wait_release();
    vTaskDelay(pdMS_TO_TICKS(150));
    crosshair(x, y, C_BLACK);
}

static void set_landscape(bool on)
{
    landscape = on;
    lcd_orientation();
}

static void calibrate(void)
{
    for (;;) {
        set_landscape(false);
        fill_rect(0, 0, 240, 320, C_BLACK);
        draw_text_c(120, 140, "TOUCH CALIBRATION", 1, C_YELLOW, C_BLACK);
        draw_text_c(120, 160, "TAP THE CROSSES", 1, C_WHITE, C_BLACK);
        int a0, b0, a1, b1, a2, b2;
        cal_point(20, 20, &a0, &b0);
        cal_point(220, 20, &a1, &b1);
        cal_point(20, 300, &a2, &b2);
        bool swap = abs(b1 - b0) > abs(a1 - a0);
        int x0 = swap ? b0 : a0, x1 = swap ? b1 : a1;
        int y0 = swap ? a0 : b0, y2 = swap ? a2 : b2;
        ESP_LOGI(TAG, "cal raw: (%d,%d) (%d,%d) (%d,%d) swap=%d", a0, b0, a1, b1, a2, b2, swap);
        if (abs(x1 - x0) > 300 && abs(y2 - y0) > 300) {
            S.cal_swap = swap; S.cx0 = x0; S.cx1 = x1; S.cy0 = y0; S.cy1 = y2;
            S.cal_rot = S.rot180; S.cal_ok = 1;
            // Landscape check: one more tap decides how the panel rotates.
            set_landscape(true);
            fill_rect(0, 0, 320, 240, C_BLACK);
            draw_text_c(160, 110, "ONE MORE TAP", 1, C_YELLOW, C_BLACK);
            int ra, rb, px, py;
            cal_point(40, 40, &ra, &rb);
            int rx = S.cal_swap ? rb : ra, ry = S.cal_swap ? ra : rb;
            px = 20 + (rx - S.cx0) * 200 / (S.cx1 - S.cx0);
            py = 20 + (ry - S.cy0) * 280 / (S.cy1 - S.cy0);
            if (S.cal_rot) { px = 239 - px; py = 319 - py; }
            int e0 = abs(py - 40) + abs((239 - px) - 40);         // map 0
            int e1 = abs((319 - py) - 40) + abs(px - 40);         // map 1
            S.land_map = e1 < e0;
            S.land_rot = S.rot180;
            S.land_ok = 1;
            set_landscape(false);
            settings_save();
            return;
        }
        draw_text_c(120, 190, "BAD CALIBRATION - RETRY", 1, C_RED, C_BLACK);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

// ----------------------------------------------------------------------------
// Settings menu (portrait)
// ----------------------------------------------------------------------------
#define MENU_N 6
static const int menu_y[MENU_N] = { 92, 128, 164, 200, 236, 276 };

static void menu_item(int i)
{
    char t[32];
    uint16_t bg = C_DGREY;
    switch (i) {
    case 0: snprintf(t, sizeof t, "INVERT COLORS: %s", S.invert ? "ON" : "OFF"); break;
    case 1: snprintf(t, sizeof t, "COLOR ORDER: %s", S.bgr ? "BGR" : "RGB"); break;
    case 2: snprintf(t, sizeof t, "ROTATE 180: %s", S.rot180 ? "ON" : "OFF"); break;
    case 3: snprintf(t, sizeof t, "VOLUME: %d", S.volume); break;
    case 4: snprintf(t, sizeof t, "CALIBRATE TOUCH"); break;
    default: snprintf(t, sizeof t, "PLAY"); bg = rgb565(0, 90, 160); break;
    }
    int h = (i == MENU_N - 1) ? 38 : 32;
    draw_button(12, menu_y[i], 216, h, t, (i == MENU_N - 1) ? 2 : 1, C_WHITE, bg);
}

static void menu_draw(void)
{
    fill_rect(0, 0, 240, 320, C_BLACK);
    draw_text_c(120, 8, "TRACK & FIELD", 2, C_YELLOW, C_BLACK);
    draw_text_c(120, 30, "ESP32 CYD ST7789", 1, C_WHITE, C_BLACK);
    fill_rect(12, 46, 54, 22, C_RED);
    fill_rect(66, 46, 54, 22, C_GREEN);
    fill_rect(120, 46, 54, 22, C_BLUE);
    fill_rect(174, 46, 54, 22, C_WHITE);
    draw_text_c(120, 74, "R  G  B  W ON BLACK?", 1, C_WHITE, C_BLACK);
    for (int i = 0; i < MENU_N; i++) menu_item(i);
}

static void run_menu(bool wait_forever)
{
    set_landscape(false);
    menu_draw();
    int64_t deadline = esp_timer_get_time() + 6000000;
    int x, y;
    for (;;) {
        if (!wait_forever && esp_timer_get_time() > deadline) break;
        if (!touch_get(&x, &y)) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        deadline = esp_timer_get_time() + 15000000;
        int hit = -1;
        for (int i = 0; i < MENU_N; i++) {
            int h = (i == MENU_N - 1) ? 38 : 32;
            if (y >= menu_y[i] && y < menu_y[i] + h) hit = i;
        }
        while (touch_get(&x, &y)) vTaskDelay(pdMS_TO_TICKS(20));
        if (hit < 0) continue;
        if (hit == MENU_N - 1) break;
        switch (hit) {
        case 0: S.invert ^= 1; lcd_orientation(); break;
        case 1: S.bgr ^= 1; menu_draw(); break;
        case 2: S.rot180 ^= 1; lcd_orientation(); menu_draw(); break;
        case 3: S.volume = (S.volume + 1) % 5; break;
        case 4: calibrate(); menu_draw(); break;
        }
        if (hit != 4) { settings_save(); menu_item(hit); }
    }
    settings_save();
}

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
#ifdef TF_HEADLESS_TEST
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
#if !defined(TF_HEADLESS_TEST) || defined(TF_QEMU_DAC)
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
        int sh = gain_shift[S.volume % 5];
        for (int i = 0; i < TF_SAMPLES_FRAME; i++) {
            int v = sh ? 128 + (pcm[i] >> sh) : 128;
            a8[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        stat_busy += (uint32_t)(esp_timer_get_time() - t0);
        stat_frames++;
#ifdef TF_HEADLESS_TEST
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
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_load();

    gpio_config_t led = { .pin_bit_mask = (1ULL << PIN_LED_R) | (1ULL << PIN_LED_G) | (1ULL << PIN_LED_B),
                          .mode = GPIO_MODE_OUTPUT };
    gpio_config(&led);
    gpio_set_level(PIN_LED_R, 1); gpio_set_level(PIN_LED_G, 1); gpio_set_level(PIN_LED_B, 1);
    gpio_config_t bb = { .pin_bit_mask = 1ULL << PIN_BOOT_BTN, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&bb);

    fb = heap_caps_malloc(TF_W * TF_H, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(fb);
    memset(fb, 0, TF_W * TF_H);
    tf_init();

#ifndef TF_HEADLESS_TEST
    lcd_init();
#endif
    xpt_init();
    fill_rect(0, 0, 240, 320, C_BLACK);
    lcd_flush();
    gpio_set_level(PIN_LCD_BL, 1);

#ifndef TF_HEADLESS_TEST
    bool force = gpio_get_level(PIN_BOOT_BTN) == 0;
    if (!S.cal_ok || !S.land_ok || force) calibrate();
    run_menu(force);
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
