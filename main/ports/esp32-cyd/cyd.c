// cyd.c - common code of the ESP32 "Cheap Yellow Display" (ESP32-2432S028,
// ST7789 240x320 SPI variant) ports: LCD over SPI/DMA, XPT2046 touch with
// calibration, settings menu in NVS, crash diagnostics, boot sequence.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cyd.h"
#include "font5x7.h"

static const char *TAG = "cyd";

// Runtime screen geometry (portrait for setup, landscape for the game)
int scr_w = 240, scr_h = 320;
static bool landscape;

// ----------------------------------------------------------------------------
// Settings (NVS)
// ----------------------------------------------------------------------------
cyd_settings_t cyd_cfg = { .invert = 1, .bgr = 0, .rot180 = 0, .volume = 2 };
static const cyd_game_t *game;

void cyd_settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(game->nvs_ns, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof cyd_cfg;
        cyd_settings_t t;
        if (nvs_get_blob(h, "cfg", &t, &sz) == ESP_OK && sz == sizeof cyd_cfg) cyd_cfg = t;
        nvs_close(h);
    }
}

void cyd_settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(game->nvs_ns, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", &cyd_cfg, sizeof cyd_cfg);
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

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    if (cyd_cfg.bgr) { uint8_t t = r; r = b; b = t; }
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void lcd_orientation(void)
{
    if (!lcd_ok) return;
    esp_lcd_panel_invert_color(panel, cyd_cfg.invert != 0);
    if (!landscape) {
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, cyd_cfg.rot180 != 0, cyd_cfg.rot180 != 0);
        scr_w = 240; scr_h = 320;
    } else {
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, cyd_cfg.rot180 == 0, cyd_cfg.rot180 != 0);
        scr_w = 320; scr_h = 240;
    }
}

void lcd_init(void)
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

uint16_t *lcd_get_buf(void)
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

void lcd_flush(void)
{
    if (!lcd_ok) return;
    int got = 0;
    for (int i = 0; i < 2; i++) if (xSemaphoreTake(lcd_done, pdMS_TO_TICKS(250)) == pdTRUE) got++;
    while (got--) xSemaphoreGive(lcd_done);
}

void lcd_blit(int x, int y, int w, int h, uint16_t *buf)
{
    if (!lcd_ok) return;
    esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, buf);
}

void fill_rect(int x, int y, int w, int h, uint16_t c)
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

void draw_char(int x, int y, char ch, int sc, uint16_t fg, uint16_t bg)
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

void draw_text(int x, int y, const char *s, int sc, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += 6 * sc) draw_char(x, y, *s, sc, fg, bg);
}

void draw_text_c(int cx, int y, const char *s, int sc, uint16_t fg, uint16_t bg)
{
    draw_text(cx - (int)strlen(s) * 3 * sc, y, s, sc, fg, bg);
}

void draw_arrow(int x, int y, int w, int h, int dir, uint16_t fg, uint16_t bg)
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


void draw_button(int x, int y, int w, int h, const char *label, int sc, uint16_t fg, uint16_t bg)
{
    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, w, 1, C_GREY);
    fill_rect(x, y + h - 1, w, 1, C_BLACK);
    if (label) draw_text_c(x + w / 2, y + (h - 8 * sc) / 2, label, sc, fg, bg);
}

// ----------------------------------------------------------------------------
// XPT2046 touch (bit-banged)
// ----------------------------------------------------------------------------
void xpt_init(void)
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

bool xpt_read_raw(int *ra, int *rb)
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
    if (!cyd_cfg.cal_ok || !xpt_read_raw(&a, &b)) return false;
    int rx = cyd_cfg.cal_swap ? b : a, ry = cyd_cfg.cal_swap ? a : b;
    int sx = 20 + (rx - cyd_cfg.cx0) * 200 / (cyd_cfg.cx1 - cyd_cfg.cx0);
    int sy = 20 + (ry - cyd_cfg.cy0) * 280 / (cyd_cfg.cy1 - cyd_cfg.cy0);
    if (cyd_cfg.cal_rot) { sx = 239 - sx; sy = 319 - sy; }
    *px = sx; *py = sy;
    return true;
}

// Coordinates in the current screen orientation
bool touch_get(int *x, int *y)
{
    int px, py;
    if (!touch_phys(&px, &py)) return false;
    int lx, ly;
    if (!landscape) {
        lx = cyd_cfg.rot180 ? 239 - px : px;
        ly = cyd_cfg.rot180 ? 319 - py : py;
    } else {
        int m = cyd_cfg.land_map ^ (cyd_cfg.rot180 != cyd_cfg.land_rot);
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

void set_landscape(bool on)
{
    landscape = on;
    lcd_orientation();
}

void calibrate(void)
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
            cyd_cfg.cal_swap = swap; cyd_cfg.cx0 = x0; cyd_cfg.cx1 = x1; cyd_cfg.cy0 = y0; cyd_cfg.cy1 = y2;
            cyd_cfg.cal_rot = cyd_cfg.rot180; cyd_cfg.cal_ok = 1;
            // Landscape check: one more tap decides how the panel rotates.
            set_landscape(true);
            fill_rect(0, 0, 320, 240, C_BLACK);
            draw_text_c(160, 110, "ONE MORE TAP", 1, C_YELLOW, C_BLACK);
            int ra, rb, px, py;
            cal_point(40, 40, &ra, &rb);
            int rx = cyd_cfg.cal_swap ? rb : ra, ry = cyd_cfg.cal_swap ? ra : rb;
            px = 20 + (rx - cyd_cfg.cx0) * 200 / (cyd_cfg.cx1 - cyd_cfg.cx0);
            py = 20 + (ry - cyd_cfg.cy0) * 280 / (cyd_cfg.cy1 - cyd_cfg.cy0);
            if (cyd_cfg.cal_rot) { px = 239 - px; py = 319 - py; }
            int e0 = abs(py - 40) + abs((239 - px) - 40);         // map 0
            int e1 = abs((319 - py) - 40) + abs(px - 40);         // map 1
            cyd_cfg.land_map = e1 < e0;
            cyd_cfg.land_rot = cyd_cfg.rot180;
            cyd_cfg.land_ok = 1;
            set_landscape(false);
            cyd_settings_save();
            return;
        }
        draw_text_c(120, 190, "BAD CALIBRATION - RETRY", 1, C_RED, C_BLACK);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}


// ----------------------------------------------------------------------------
// Crash diagnostics: the panic handler stores the reason and a short backtrace
// in RTC memory (survives the reboot); the menu shows it on the next boot.
// ----------------------------------------------------------------------------
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_debug_helpers.h"
#include "xtensa_context.h"
#include "esp_cpu_utils.h"

#define CRASH_MAGIC 0x5B0C0DE5u
RTC_NOINIT_ATTR static uint32_t crash_magic, crash_core, crash_exc, crash_cause, crash_vaddr;
RTC_NOINIT_ATTR static uint32_t crash_bt[6];
static esp_reset_reason_t boot_reason;

void __real_esp_panic_handler(panic_info_t *info);
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
    crash_magic = CRASH_MAGIC;
    crash_core = (uint32_t)info->core;
    crash_exc = (uint32_t)info->exception;
    for (int i = 0; i < 6; i++) crash_bt[i] = 0;
    crash_bt[0] = (uint32_t)info->addr;
    const XtExcFrame *f = (const XtExcFrame *)info->frame;
    if (f) {
        crash_cause = (uint32_t)f->exccause;
        crash_vaddr = (uint32_t)f->excvaddr;
        esp_backtrace_frame_t bt = { .pc = (uint32_t)f->pc, .sp = (uint32_t)f->a1, .next_pc = (uint32_t)f->a0 };
        crash_bt[0] = esp_cpu_process_stack_pc(bt.pc);
        for (int i = 1; i < 6 && bt.next_pc; i++) {
            if (!esp_backtrace_get_next_frame(&bt)) break;
            crash_bt[i] = esp_cpu_process_stack_pc(bt.pc);
        }
    }
    __real_esp_panic_handler(info);
}

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT WDT";
    case ESP_RST_TASK_WDT: return "TASK WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_POWERON: return "POWER ON";
    case ESP_RST_EXT: return "EXT PIN";
    default: return "OTHER";
    }
}

void crash_report_init(void)
{
    boot_reason = esp_reset_reason();
    if (crash_magic == CRASH_MAGIC)
        ESP_LOGE(TAG, "last crash: core %u exc %u cause %u vaddr %08x bt %08x %08x %08x %08x %08x %08x",
                 (unsigned)crash_core, (unsigned)crash_exc, (unsigned)crash_cause, (unsigned)crash_vaddr,
                 (unsigned)crash_bt[0], (unsigned)crash_bt[1], (unsigned)crash_bt[2],
                 (unsigned)crash_bt[3], (unsigned)crash_bt[4], (unsigned)crash_bt[5]);
    ESP_LOGI(TAG, "reset reason: %s", reset_name(boot_reason));
}

// Draws up to 4 lines at y; returns true if there is something to report
static bool crash_report_draw(int y)
{
    char l[48];
    bool crashed = crash_magic == CRASH_MAGIC &&
                   (boot_reason == ESP_RST_PANIC || boot_reason == ESP_RST_INT_WDT ||
                    boot_reason == ESP_RST_TASK_WDT || boot_reason == ESP_RST_WDT);
    if (!crashed && boot_reason != ESP_RST_BROWNOUT) return false;
    fill_rect(0, y, 240, 34, C_BLACK);
    snprintf(l, sizeof l, "LAST RESET: %s", reset_name(boot_reason));
    draw_text_c(120, y, l, 1, C_RED, C_BLACK);
    if (crashed) {
        snprintf(l, sizeof l, "C%u E%u X%u V%08X", (unsigned)crash_core, (unsigned)crash_exc,
                 (unsigned)crash_cause, (unsigned)crash_vaddr);
        draw_text_c(120, y + 9, l, 1, C_YELLOW, C_BLACK);
        snprintf(l, sizeof l, "%08X %08X %08X", (unsigned)crash_bt[0], (unsigned)crash_bt[1], (unsigned)crash_bt[2]);
        draw_text_c(120, y + 18, l, 1, C_YELLOW, C_BLACK);
        snprintf(l, sizeof l, "%08X %08X %08X", (unsigned)crash_bt[3], (unsigned)crash_bt[4], (unsigned)crash_bt[5]);
        draw_text_c(120, y + 27, l, 1, C_YELLOW, C_BLACK);
    }
    return true;
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
    case 0: snprintf(t, sizeof t, "INVERT COLORS: %s", cyd_cfg.invert ? "ON" : "OFF"); break;
    case 1: snprintf(t, sizeof t, "COLOR ORDER: %s", cyd_cfg.bgr ? "BGR" : "RGB"); break;
    case 2: snprintf(t, sizeof t, "ROTATE 180: %s", cyd_cfg.rot180 ? "ON" : "OFF"); break;
    case 3: snprintf(t, sizeof t, "VOLUME: %d", cyd_cfg.volume); break;
    case 4: snprintf(t, sizeof t, "CALIBRATE TOUCH"); break;
    default: snprintf(t, sizeof t, "PLAY"); bg = rgb565(0, 90, 160); break;
    }
    int h = (i == MENU_N - 1) ? 38 : 32;
    draw_button(12, menu_y[i], 216, h, t, (i == MENU_N - 1) ? 2 : 1, C_WHITE, bg);
}

static void menu_draw(void)
{
    fill_rect(0, 0, 240, 320, C_BLACK);
    if (game->subtitle) {
        draw_text_c(120, 6, game->title, 2, rgb565(game->title_rgb[0], game->title_rgb[1], game->title_rgb[2]), C_BLACK);
        draw_text_c(120, 24, game->subtitle, 1, C_CYAN, C_BLACK);
        draw_text_c(120, 34, "ESP32 CYD ST7789", 1, C_WHITE, C_BLACK);
    } else {
        draw_text_c(120, 8, game->title, 2, rgb565(game->title_rgb[0], game->title_rgb[1], game->title_rgb[2]), C_BLACK);
        draw_text_c(120, 30, "ESP32 CYD ST7789", 1, C_WHITE, C_BLACK);
    }
    fill_rect(12, 46, 54, 22, C_RED);
    fill_rect(66, 46, 54, 22, C_GREEN);
    fill_rect(120, 46, 54, 22, C_BLUE);
    fill_rect(174, 46, 54, 22, C_WHITE);
    draw_text_c(120, 74, "R  G  B  W ON BLACK?", 1, C_WHITE, C_BLACK);
    for (int i = 0; i < MENU_N; i++) menu_item(i);
    crash_report_draw(48);       // over the colour bars, only after a crash
}

void run_menu(bool wait_forever)
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
        case 0: cyd_cfg.invert ^= 1; lcd_orientation(); break;
        case 1: cyd_cfg.bgr ^= 1; menu_draw(); break;
        case 2: cyd_cfg.rot180 ^= 1; lcd_orientation(); menu_draw(); break;
        case 3: cyd_cfg.volume = (cyd_cfg.volume + 1) % 5; break;
        case 4: calibrate(); menu_draw(); break;
        }
        if (hit != 4) { cyd_settings_save(); menu_item(hit); }
    }
    cyd_settings_save();
}


// ----------------------------------------------------------------------------
// Boot sequence
// ----------------------------------------------------------------------------
void cyd_boot(const cyd_game_t *g)
{
    game = g;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    cyd_settings_load();
    crash_report_init();
    gpio_config_t led = { .pin_bit_mask = (1ULL << PIN_LED_R) | (1ULL << PIN_LED_G) | (1ULL << PIN_LED_B),
                          .mode = GPIO_MODE_OUTPUT };
    gpio_config(&led);
    gpio_set_level(PIN_LED_R, 1); gpio_set_level(PIN_LED_G, 1); gpio_set_level(PIN_LED_B, 1);
    gpio_config_t bb = { .pin_bit_mask = 1ULL << PIN_BOOT_BTN, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&bb);
}

void cyd_display_start(bool with_lcd)
{
    if (with_lcd) lcd_init();
    xpt_init();
    fill_rect(0, 0, 240, 320, C_BLACK);
    lcd_flush();
    gpio_set_level(PIN_LCD_BL, 1);
}

void cyd_setup_ui(void)
{
    bool force = gpio_get_level(PIN_BOOT_BTN) == 0;
    if (!cyd_cfg.cal_ok || !cyd_cfg.land_ok || force) calibrate();
    run_menu(force);
}
