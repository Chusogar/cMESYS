// cyd.h - the CYD frontends' port API on the LilyGO TTGO VGA32 (v1.4,
// ESP32-WROVER with 4 MB of PSRAM): every <machine>_cyd.c builds for it
// unchanged (idf.py -DBOARD=vga32). The 320x240 screen is a VGA output (vga.c),
// the touch is the PS/2 mouse (a click is a touch), the Nunchuk and the
// serial terminal are the PS/2 keyboard, the BOOT button is F1 (ps2.c), the
// sound is the DAC on GPIO25 (the 3.5 mm jack), and the microSD is on
// MISO 2, MOSI 12, CLK 14, CS 13.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ----------------------------------------------------------------------------
// Board pins
// ----------------------------------------------------------------------------
#define PIN_BOOT_BTN   0     // (no button: F1 on the PS/2 keyboard reads as it)
#define PIN_LED_R      0     // (no LEDs: writes to GPIO0, an input, do nothing)
#define PIN_LED_G      0
#define PIN_LED_B      0
#define PIN_KB_CLK     33
#define PIN_KB_DATA    32
#define PIN_MOUSE_CLK  26
#define PIN_MOUSE_DATA 27

#define BAND           16    // lines per "LCD transfer" (lcd_get_buf)

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
// The VGA output and the PS/2 ports, black screen.
void cyd_display_start(bool with_lcd);
// The machine's ROMs are not built in and not on the microSD (romfs.h): shows
// what is missing and where it was looked for, and stops.
void cyd_rom_error(void) __attribute__((noreturn));
// The settings menu (a few seconds; always with F1 held at power-up).
void cyd_setup_ui(void);
void cyd_settings_load(void);
void cyd_settings_save(void);

// ----------------------------------------------------------------------------
// Screen (the CYD's LCD API, drawn into the VGA framebuffer)
// ----------------------------------------------------------------------------
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);   // byte-swapped, as the CYD's
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
// Touch (coordinates in the current orientation): the PS/2 mouse's pointer
// while its left button is held
// ----------------------------------------------------------------------------
void xpt_init(void);
bool xpt_read_raw(int *ra, int *rb);
bool touch_get(int *x, int *y);
void calibrate(void);
void run_menu(bool wait_forever);
void crash_report_init(void);
