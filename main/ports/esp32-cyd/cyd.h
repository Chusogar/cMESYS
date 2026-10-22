// cyd.h - common code of the ESP32 "Cheap Yellow Display" ports
// (ESP32-2432S028, ST7789 240x320 over SPI, XPT2046 touch, speaker on GPIO26).
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ----------------------------------------------------------------------------
// Board pins
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
#define PIN_BOOT_BTN   0     // BOOT button: coin + start in the games
// Wii Nunchuk on I2C: SDA = GPIO22, SCL = GPIO27 (nunchuk.h)
#define PIN_LED_R      4
#define PIN_LED_G      16
#define PIN_LED_B      17

#define BAND           16    // lines per LCD transfer

// ----------------------------------------------------------------------------
// The game using it
// ----------------------------------------------------------------------------
typedef struct {
    const char *nvs_ns;      // NVS namespace of its settings (max 15 chars)
    const char *title;       // menu title (scale 2)
    const char *subtitle;    // optional second line (NULL = none)
    uint8_t title_rgb[3];
} cyd_game_t;

typedef struct {
    uint8_t invert, bgr, rot180, volume;
    uint8_t cal_ok, cal_swap, cal_rot, land_map;
    int16_t cx0, cx1, cy0, cy1;
    uint8_t land_ok, land_rot, pad[2];
} cyd_settings_t;

extern cyd_settings_t cyd_cfg;     // volume 0-4, colours, touch calibration
extern int scr_w, scr_h;

// Boot: NVS, settings, crash report, LEDs off, BOOT button input.
void cyd_boot(const cyd_game_t *g);
// LCD (unless with_lcd is false: QEMU tests), touch, black screen, backlight.
void cyd_display_start(bool with_lcd);
// Touch calibration when needed (or BOOT held) and the settings menu.
void cyd_setup_ui(void);
void cyd_settings_load(void);
void cyd_settings_save(void);

// ----------------------------------------------------------------------------
// LCD
// ----------------------------------------------------------------------------
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);   // byte-swapped, BGR/invert aware
void lcd_init(void);
uint16_t *lcd_get_buf(void);         // one of the two DMA band buffers (320 x BAND)
void lcd_blit(int x, int y, int w, int h, uint16_t *buf);
void lcd_flush(void);                // waits for the transfers in flight
void set_landscape(bool on);
void fill_rect(int x, int y, int w, int h, uint16_t c);
void draw_char(int x, int y, char ch, int sc, uint16_t fg, uint16_t bg);
void draw_text(int x, int y, const char *s, int sc, uint16_t fg, uint16_t bg);
void draw_text_c(int cx, int y, const char *s, int sc, uint16_t fg, uint16_t bg);
void draw_arrow(int x, int y, int w, int h, int dir, uint16_t fg, uint16_t bg);
void draw_button(int x, int y, int w, int h, const char *label, int sc, uint16_t fg, uint16_t bg);

#define C_BLACK  rgb565(0, 0, 0)
#define C_WHITE  rgb565(255, 255, 255)
#define C_RED    rgb565(255, 0, 0)
#define C_GREEN  rgb565(0, 255, 0)
#define C_BLUE   rgb565(0, 0, 255)
#define C_YELLOW rgb565(255, 220, 0)
#define C_GREY   rgb565(60, 60, 70)
#define C_DGREY  rgb565(30, 30, 36)
#define C_CYAN   rgb565(80, 200, 255)

// ----------------------------------------------------------------------------
// Touch (coordinates in the current orientation)
// ----------------------------------------------------------------------------
void xpt_init(void);
bool xpt_read_raw(int *ra, int *rb);
bool touch_get(int *x, int *y);
void calibrate(void);
void run_menu(bool wait_forever);
void crash_report_init(void);
