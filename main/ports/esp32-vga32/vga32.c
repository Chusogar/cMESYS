// vga32.c - the CYD frontends' port API (cyd.h) on the LilyGO TTGO VGA32:
// the screen calls draw into the VGA framebuffer (vga.c), with the PS/2
// mouse's pointer laid over it; settings in NVS; the settings menu on the
// keyboard; crash diagnostics; the boot sequence.
//
// The CYD's portrait screen (240x320) is shown turned: its top on the
// monitor's left.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cyd.h"
#include "vga.h"
#include "ps2.h"
#include "nunchuk.h"
#include "../esp32-cyd/font5x7.h"
#include "../../rom/romfs.h"

static const char *TAG = "vga32";

int scr_w = 320, scr_h = 240;
static bool landscape = true;

// ----------------------------------------------------------------------------
// Settings (NVS)
// ----------------------------------------------------------------------------
cyd_settings_t cyd_cfg = { .invert = 0, .bgr = 0, .rot180 = 0, .volume = 2, .cal_ok = 1, .land_ok = 1 };
static const cyd_game_t *game;

void cyd_settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(game->nvs_ns, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof cyd_cfg;
        cyd_settings_t t;
        if (nvs_get_blob(h, "cfg", &t, &sz) == ESP_OK && sz == sizeof cyd_cfg) cyd_cfg.volume = t.volume % 5;
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
// Screen
// ----------------------------------------------------------------------------
static uint16_t band_buf[2][320 * BAND];
static int buf_idx;
static uint8_t lo_tab[256], hi_tab[256];    // RGB565 (byte-swapped) -> VGA byte
static SemaphoreHandle_t fb_lock;

// The pointer: drawn over the framebuffer, what it covers kept in under[]
#define CUR_W 8
#define CUR_H 12
static const uint8_t cur_shape[CUR_H] = {   // 1: white (an outline in black)
    0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xf8, 0xd8, 0x8c, 0x0c};
static bool cur_on;
static int cur_x, cur_y;
static uint8_t under[CUR_H][CUR_W];

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void make_tables(void)
{
    // byte-swapped RGB565: low byte RRRRRGGG, high byte GGGBBBBB
    for (int v = 0; v < 256; v++) {
        lo_tab[v] = (uint8_t)(VGA_SYNC_OFF | (v >> 6) | (((v >> 1) & 3) << 2));
        hi_tab[v] = (uint8_t)(((v >> 3) & 3) << 4);
    }
}

static inline uint8_t vga_px(uint16_t v) { return (uint8_t)(lo_tab[v & 0xff] | hi_tab[v >> 8]); }

// The pointer's pixels: in[] (what is under it) <-> the framebuffer
static void cursor_hide(void)
{
    if (!cur_on) return;
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            const int x = cur_x + i, y = cur_y + j;
            if (x < VGA_W && y < VGA_H && (cur_shape[j] & (0x80 >> i))) vga_fb[VGA_FBI(x, y)] = under[j][i];
        }
    cur_on = false;
}

static void cursor_draw(void)
{
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            const int x = cur_x + i, y = cur_y + j;
            if (x >= VGA_W || y >= VGA_H || !(cur_shape[j] & (0x80 >> i))) continue;
            uint8_t *p = &vga_fb[VGA_FBI(x, y)];
            under[j][i] = *p;
            const bool edge = i == 0 || j == CUR_H - 1 || !(cur_shape[j] & (0x80 >> (i + 1)));
            *p = edge ? VGA_SYNC_OFF : (VGA_SYNC_OFF | 0x3f);
        }
    cur_on = true;
}

void vga32_pointer(bool show, int x, int y)
{
    if (!vga_fb) return;
    xSemaphoreTake(fb_lock, portMAX_DELAY);
    cursor_hide();
    cur_x = x;
    cur_y = y;
    if (show) cursor_draw();
    xSemaphoreGive(fb_lock);
}

// (x, y) of the current orientation -> the framebuffer's
static inline void to_fb(int x, int y, int *fx, int *fy)
{
    if (landscape) *fx = x, *fy = y;
    else *fx = y, *fy = VGA_H - 1 - x;
}

void lcd_init(void) {}

uint16_t *lcd_get_buf(void)
{
    uint16_t *b = band_buf[buf_idx];
    buf_idx ^= 1;
    return b;
}

void lcd_flush(void) {}

void lcd_blit(int x, int y, int w, int h, uint16_t *buf)
{
    if (!vga_fb || w <= 0 || h <= 0) return;
    xSemaphoreTake(fb_lock, portMAX_DELAY);
    const bool had = cur_on;
    cursor_hide();
    if (landscape) {
        for (int j = 0; j < h; j++) {
            const int yy = y + j;
            if ((unsigned)yy >= VGA_H) continue;
            const uint16_t *s = buf + j * w;
            uint8_t *row = vga_fb + yy * VGA_W;
            int i = 0, xx = x;
            if (xx < 0) i = -xx, xx = 0;
            for (; i < w && xx < VGA_W; i++, xx++) row[xx ^ 2] = vga_px(s[i]);
        }
    } else {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int fx, fy;
                to_fb(x + i, y + j, &fx, &fy);
                if ((unsigned)fx < VGA_W && (unsigned)fy < VGA_H) vga_fb[VGA_FBI(fx, fy)] = vga_px(buf[j * w + i]);
            }
    }
    if (had) cursor_draw();
    xSemaphoreGive(fb_lock);
}

void set_landscape(bool on)
{
    landscape = on;
    scr_w = on ? 320 : 240;
    scr_h = on ? 240 : 320;
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
        y += rows;
        h -= rows;
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
// "Touch": the mouse's pointer while its left button is held
// ----------------------------------------------------------------------------
void xpt_init(void) {}

bool xpt_read_raw(int *ra, int *rb)
{
    int x, y;
    if (!ps2_mouse_touch(&x, &y)) return false;
    *ra = x;
    *rb = y;
    return true;
}

bool touch_get(int *x, int *y)
{
    int mx, my;
    if (!ps2_mouse_touch(&mx, &my)) return false;
    // the framebuffer's (mx, my) -> the current orientation
    int lx = mx, ly = my;
    if (!landscape) lx = VGA_H - 1 - my, ly = mx;
    if (lx < 0) lx = 0;
    if (lx >= scr_w) lx = scr_w - 1;
    if (ly < 0) ly = 0;
    if (ly >= scr_h) ly = scr_h - 1;
    *x = lx;
    *y = ly;
    return true;
}

void calibrate(void) {}

// ----------------------------------------------------------------------------
// Crash diagnostics (as the CYD's): the panic handler keeps the reason and a
// short backtrace in RTC memory; the menu shows them on the next boot.
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

static void crash_report_draw(int y)
{
    char l[48];
    bool crashed = crash_magic == CRASH_MAGIC &&
                   (boot_reason == ESP_RST_PANIC || boot_reason == ESP_RST_INT_WDT ||
                    boot_reason == ESP_RST_TASK_WDT || boot_reason == ESP_RST_WDT);
    if (!crashed && boot_reason != ESP_RST_BROWNOUT) return;
    snprintf(l, sizeof l, "LAST RESET: %s", reset_name(boot_reason));
    draw_text_c(160, y, l, 1, C_RED, C_BLACK);
    if (crashed) {
        snprintf(l, sizeof l, "C%u E%u X%u V%08X", (unsigned)crash_core, (unsigned)crash_exc,
                 (unsigned)crash_cause, (unsigned)crash_vaddr);
        draw_text_c(160, y + 9, l, 1, C_YELLOW, C_BLACK);
        snprintf(l, sizeof l, "%08X %08X %08X", (unsigned)crash_bt[0], (unsigned)crash_bt[1], (unsigned)crash_bt[2]);
        draw_text_c(160, y + 18, l, 1, C_YELLOW, C_BLACK);
        snprintf(l, sizeof l, "%08X %08X %08X", (unsigned)crash_bt[3], (unsigned)crash_bt[4], (unsigned)crash_bt[5]);
        draw_text_c(160, y + 27, l, 1, C_YELLOW, C_BLACK);
    }
}

// ----------------------------------------------------------------------------
// Settings menu (keyboard: left / right the volume, Enter / Ctrl to play;
// or the mouse)
// ----------------------------------------------------------------------------
static void menu_volume(void)
{
    char t[32];
    snprintf(t, sizeof t, "VOLUME: %d   (LEFT / RIGHT)", cyd_cfg.volume);
    draw_button(40, 120, 240, 28, t, 1, C_WHITE, C_DGREY);
}

static void menu_draw(void)
{
    set_landscape(true);
    fill_rect(0, 0, 320, 240, C_BLACK);
    const uint16_t tc = rgb565(game->title_rgb[0], game->title_rgb[1], game->title_rgb[2]);
    draw_text_c(160, 14, game->title, 2, tc, C_BLACK);
    if (game->subtitle) draw_text_c(160, 34, game->subtitle, 1, C_CYAN, C_BLACK);
    draw_text_c(160, 46, "LILYGO TTGO VGA32", 1, C_WHITE, C_BLACK);
    fill_rect(52, 62, 54, 18, C_RED);
    fill_rect(106, 62, 54, 18, C_GREEN);
    fill_rect(160, 62, 54, 18, C_BLUE);
    fill_rect(214, 62, 54, 18, C_WHITE);
    menu_volume();
    draw_button(40, 160, 240, 34, "PLAY  (ENTER)", 2, C_WHITE, rgb565(0, 90, 160));
    draw_text_c(160, 204, "F1 BOOT  CTRL / ALT / CURSORS THE NUNCHUK", 1, C_GREY, C_BLACK);
    draw_text_c(160, 214, "MOUSE CLICK = TOUCH", 1, C_GREY, C_BLACK);
    crash_report_draw(84);
}

void run_menu(bool wait_forever)
{
    menu_draw();
    ps2_flush_keys();
    int64_t deadline = esp_timer_get_time() + 5000000;
    bool lr_was = false, touch_was = false;
    for (;;) {
        if (!wait_forever && esp_timer_get_time() > deadline) break;
        nunchuk_t n;
        nunchuk_read(&n);
        const bool left = nc_left(&n), right = nc_right(&n);
        if ((left || right) && !lr_was) {
            cyd_cfg.volume = (uint8_t)((cyd_cfg.volume + (right ? 1 : 4)) % 5);
            menu_volume();
            deadline = esp_timer_get_time() + 15000000;
        }
        lr_was = left || right;
        int ch = ps2_menu_char();
        if (ch == '\r' || ch == ' ' || n.z) break;
        if (ch >= 0) deadline = esp_timer_get_time() + 15000000;
        int x, y;
        const bool t = touch_get(&x, &y);
        if (!t && touch_was) {
            if (y >= 160 && y < 194) break;
            if (y >= 120 && y < 148) {
                cyd_cfg.volume = (uint8_t)((cyd_cfg.volume + 1) % 5);
                menu_volume();
                deadline = esp_timer_get_time() + 15000000;
            }
        }
        touch_was = t;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    cyd_settings_save();
    // (the keys of the menu are not the game's)
    for (int i = 0; i < 25; i++) {
        nunchuk_t n;
        nunchuk_read(&n);
        if (!n.z) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ps2_flush_keys();
    fill_rect(0, 0, 320, 240, C_BLACK);
}

// ----------------------------------------------------------------------------
// Boot sequence
// ----------------------------------------------------------------------------
static bool display_started;

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
    fb_lock = xSemaphoreCreateMutex();
    make_tables();
    // the framebuffer first: internal DMA memory, before the machine takes it
    if (!vga_start()) ESP_LOGE(TAG, "no VGA output");
    ps2_start();
    ESP_LOGI(TAG, "free: internal %u, PSRAM %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void cyd_display_start(bool with_lcd)
{
    (void)with_lcd;
    display_started = true;
    set_landscape(false);
    fill_rect(0, 0, 240, 320, C_BLACK);
}

void cyd_rom_error(void)
{
    char msg[768];
    romfs_describe(msg, sizeof msg, 52);
    ESP_LOGE(TAG, "%s", msg);
    if (!display_started) cyd_display_start(true);
    set_landscape(true);
    fill_rect(0, 0, 320, 240, C_BLACK);
    int y = 8;
    for (char *line = msg; line && y < 232; y += 10) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        draw_text(4, y, line, 1, y == 8 ? C_YELLOW : C_WHITE, C_BLACK);
        line = nl ? nl + 1 : NULL;
    }
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

void cyd_setup_ui(void)
{
    const bool force = ps2_boot_held();
    const bool was_landscape = landscape;
    run_menu(force);
    set_landscape(was_landscape);
}
