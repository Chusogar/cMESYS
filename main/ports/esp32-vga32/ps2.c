// ps2.c - the TTGO VGA32's two PS/2 ports (keyboard: CLK 33 DATA 32, mouse:
// CLK 26 DATA 27) as the CYD frontends' inputs (see ps2.h).
//
// Each port: an interrupt on CLK's falling edge reads a bit (11 a byte:
// start, 8 data, parity, stop) or, while the host sends, writes one. A task
// decodes the keyboard's scan codes (set 2) and the mouse's 3-byte packets,
// and starts the mouse (reset, then "enable data reporting"), again when one
// is plugged in.
#include "ps2.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "cyd.h"
#include "nunchuk.h"
#include "vga.h"

static const char *TAG = "ps2";

// ----------------------------------------------------------------------------
// The ports
// ----------------------------------------------------------------------------
#define RXQ 64

typedef struct {
    int clk, data;
    // receiving
    int bits;
    uint16_t shift;
    int64_t last_us;
    volatile uint8_t q[RXQ];
    volatile uint32_t q_head, q_tail;
    // sending
    volatile bool sending, acked;
    uint8_t tx_byte, tx_parity;
    int tx_n;
} ps2_port;

static ps2_port kb = {.clk = PIN_KB_CLK, .data = PIN_KB_DATA};
static ps2_port ms = {.clk = PIN_MOUSE_CLK, .data = PIN_MOUSE_DATA};

static void IRAM_ATTR clk_isr(void *arg)
{
    ps2_port *p = (ps2_port *)arg;
    const int bit = (int)gpio_ll_get_level(&GPIO, p->data);
    if (p->sending) {
        const int n = ++p->tx_n;
        if (n <= 8) gpio_ll_set_level(&GPIO, p->data, (p->tx_byte >> (n - 1)) & 1);
        else if (n == 9) gpio_ll_set_level(&GPIO, p->data, p->tx_parity);
        else if (n == 10) gpio_ll_set_level(&GPIO, p->data, 1);    // stop: released
        else {
            p->acked = bit == 0;
            p->sending = false;
            p->bits = 0;
        }
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now - p->last_us > 1000) p->bits = 0;      // a lost bit: start again
    p->last_us = now;
    if (p->bits == 0) {
        if (bit != 0) return;                       // not a start bit
        p->shift = 0;
    } else if (p->bits <= 8) {
        p->shift |= (uint16_t)(bit << (p->bits - 1));
    } else if (p->bits == 9) {
        // parity (odd): not checked
    } else {
        if (bit == 1) {
            const uint32_t h = p->q_head;
            if (h - p->q_tail < RXQ) {
                p->q[h % RXQ] = (uint8_t)p->shift;
                p->q_head = h + 1;
            }
        }
        p->bits = 0;
        return;
    }
    p->bits++;
}

static int port_get(ps2_port *p)
{
    if (p->q_tail == p->q_head) return -1;
    const uint8_t b = p->q[p->q_tail % RXQ];
    p->q_tail++;
    return b;
}

static void port_init(ps2_port *p)
{
    gpio_config_t c = {.pin_bit_mask = (1ULL << p->clk) | (1ULL << p->data),
                       .mode = GPIO_MODE_INPUT_OUTPUT_OD,
                       .pull_up_en = GPIO_PULLUP_ENABLE,
                       .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&c);
    gpio_set_level(p->clk, 1);
    gpio_set_level(p->data, 1);
    gpio_set_intr_type(p->clk, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(p->clk, clk_isr, p);
}

// Host -> device: one byte; true if the device acknowledged it
static bool port_send(ps2_port *p, uint8_t b)
{
    int ones = 0;
    for (int i = 0; i < 8; i++) ones += (b >> i) & 1;
    p->tx_byte = b;
    p->tx_parity = (uint8_t)((ones & 1) ? 0 : 1);
    p->tx_n = 0;
    p->acked = false;
    gpio_intr_disable(p->clk);
    gpio_set_level(p->clk, 0);              // inhibit
    esp_rom_delay_us(150);
    gpio_set_level(p->data, 0);             // request to send (the start bit)
    p->sending = true;
    gpio_set_level(p->clk, 1);
    gpio_intr_enable(p->clk);
    const int64_t until = esp_timer_get_time() + 25000;
    while (p->sending && esp_timer_get_time() < until) vTaskDelay(1);
    if (p->sending) {                       // no device
        p->sending = false;
        gpio_set_level(p->data, 1);
        p->bits = 0;
        return false;
    }
    return p->acked;
}

// ----------------------------------------------------------------------------
// Keyboard
// ----------------------------------------------------------------------------
static bool kb_seen;
static volatile bool k_up, k_down, k_left, k_right, k_ctrl, k_alt, k_f1;
static bool shift, ext, brk;

#define AQ 128
static volatile char aq[AQ];
static volatile uint32_t aq_head, aq_tail;

static void type(const char *s)
{
    for (; *s; s++) {
        const uint32_t h = aq_head;
        if (h - aq_tail >= AQ) return;
        aq[h % AQ] = *s;
        aq_head = h + 1;
    }
}

static const char plain[0x80] = {
    [0x0e] = '`', [0x16] = '1', [0x1e] = '2', [0x26] = '3', [0x25] = '4', [0x2e] = '5', [0x36] = '6',
    [0x3d] = '7', [0x3e] = '8', [0x46] = '9', [0x45] = '0', [0x4e] = '-', [0x55] = '=', [0x66] = 8,
    [0x0d] = '\t', [0x15] = 'q', [0x1d] = 'w', [0x24] = 'e', [0x2d] = 'r', [0x2c] = 't', [0x35] = 'y',
    [0x3c] = 'u', [0x43] = 'i', [0x44] = 'o', [0x4d] = 'p', [0x54] = '[', [0x5b] = ']', [0x5d] = '\\',
    [0x1c] = 'a', [0x1b] = 's', [0x23] = 'd', [0x2b] = 'f', [0x34] = 'g', [0x33] = 'h', [0x3b] = 'j',
    [0x42] = 'k', [0x4b] = 'l', [0x4c] = ';', [0x52] = '\'', [0x5a] = '\r', [0x1a] = 'z', [0x22] = 'x',
    [0x21] = 'c', [0x2a] = 'v', [0x32] = 'b', [0x31] = 'n', [0x3a] = 'm', [0x41] = ',', [0x49] = '.',
    [0x4a] = '/', [0x29] = ' ',
    // the keypad
    [0x70] = '0', [0x69] = '1', [0x72] = '2', [0x7a] = '3', [0x6b] = '4', [0x73] = '5', [0x74] = '6',
    [0x6c] = '7', [0x75] = '8', [0x7d] = '9', [0x71] = '.', [0x79] = '+', [0x7b] = '-', [0x7c] = '*',
};

static const char shifted[0x80] = {
    [0x0e] = '~', [0x16] = '!', [0x1e] = '@', [0x26] = '#', [0x25] = '$', [0x2e] = '%', [0x36] = '^',
    [0x3d] = '&', [0x3e] = '*', [0x46] = '(', [0x45] = ')', [0x4e] = '_', [0x55] = '+', [0x54] = '{',
    [0x5b] = '}', [0x5d] = '|', [0x4c] = ':', [0x52] = '"', [0x41] = '<', [0x49] = '>', [0x4a] = '?',
};

// F2..F12 as a terminal's ESC [ n ~ (F1 is the BOOT button)
static int fkey(uint8_t code)
{
    switch (code) {
    case 0x06: return 12;
    case 0x04: return 13;
    case 0x0c: return 14;
    case 0x03: return 15;
    case 0x0b: return 17;
    case 0x83: return 18;
    case 0x0a: return 19;
    case 0x01: return 20;
    case 0x09: return 21;
    case 0x78: return 23;
    case 0x07: return 24;
    default: return 0;
    }
}

static void kb_byte(uint8_t b)
{
    kb_seen = true;
    if (b == 0xe0) { ext = true; return; }
    if (b == 0xf0) { brk = true; return; }
    if (b == 0xaa || b == 0xfa || b == 0xee || b == 0xfe || b == 0x00 || b == 0xff) {
        ext = brk = false;
        return;
    }
    const bool down = !brk, e = ext;
    ext = brk = false;
    if (e) {
        switch (b) {
        case 0x75: k_up = down; return;
        case 0x72: k_down = down; return;
        case 0x6b: k_left = down; return;
        case 0x74: k_right = down; return;
        case 0x14: k_ctrl = down; return;
        case 0x11: k_alt = down; return;
        case 0x5a: if (down) type("\r"); return;
        case 0x4a: if (down) type("/"); return;
        default: return;
        }
    }
    switch (b) {
    case 0x12: case 0x59: shift = down; return;
    case 0x14: k_ctrl = down; return;
    case 0x11: k_alt = down; return;
    case 0x05: k_f1 = down; return;
    default: break;
    }
    if (!down || b >= 0x80 + 4) return;
    const int f = fkey(b);
    if (f) {
        char s[8] = {0x1b, '[', (char)('0' + f / 10), (char)('0' + f % 10), '~', 0};
        type(s);
        return;
    }
    if (b >= 0x80) return;
    char c = shift && shifted[b] ? shifted[b] : plain[b];
    if (!c) return;
    if (shift && c >= 'a' && c <= 'z') c = (char)(c - 32);
    const char s[2] = {c, 0};
    type(s);
}

int ps2_menu_char(void)
{
    if (aq_tail == aq_head) return -1;
    const char c = aq[aq_tail % AQ];
    aq_tail++;
    return (uint8_t)c;
}

void ps2_flush_keys(void) { aq_tail = aq_head; }

bool ps2_boot_held(void) { return k_f1; }

// The frontends read their serial port (UART0) for keys: the keyboard's
// characters come first
int __real_uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait);
int __wrap_uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    if (uart_num == UART_NUM_0 && length > 0 && aq_tail != aq_head) {
        uint32_t n = 0;
        uint8_t *d = (uint8_t *)buf;
        while (n < length && aq_tail != aq_head) {
            d[n++] = (uint8_t)aq[aq_tail % AQ];
            aq_tail++;
        }
        return (int)n;
    }
    return __real_uart_read_bytes(uart_num, buf, length, ticks_to_wait);
}

// ... and the BOOT button (GPIO0) is F1 too
int __real_gpio_get_level(gpio_num_t gpio_num);
int __wrap_gpio_get_level(gpio_num_t gpio_num)
{
    if (gpio_num == PIN_BOOT_BTN && k_f1) return 0;
    return __real_gpio_get_level(gpio_num);
}

// The "Nunchuk": cursor keys, Ctrl (Z) and Alt (C)
bool nunchuk_init(int sda, int scl)
{
    (void)sda, (void)scl;
    return true;
}

bool nunchuk_read(nunchuk_t *n)
{
    n->ok = kb_seen;
    n->jx = k_left ? 0 : k_right ? 255 : 128;
    n->jy = k_up ? 255 : k_down ? 0 : 128;
    n->z = k_ctrl;
    n->c = k_alt;
    return n->ok;
}

bool nunchuk_connected(void) { return kb_seen; }

// ----------------------------------------------------------------------------
// Mouse
// ----------------------------------------------------------------------------
static bool ms_on;
static int ms_x = VGA_W / 2, ms_y = VGA_H / 2;
static volatile bool ms_left;
static uint8_t pkt[3];
static int npkt;

bool ps2_mouse_touch(int *x, int *y)
{
    if (!ms_on || !ms_left) return false;
    *x = ms_x;
    *y = ms_y;
    return true;
}

static void ms_packet(void)
{
    const uint8_t b = pkt[0];
    int dx = pkt[1], dy = pkt[2];
    if (b & 0x10) dx -= 256;
    if (b & 0x20) dy -= 256;
    if (b & 0xc0) dx = dy = 0;              // overflow
    ms_x += dx;
    ms_y -= dy;
    if (ms_x < 0) ms_x = 0;
    if (ms_x > VGA_W - 1) ms_x = VGA_W - 1;
    if (ms_y < 0) ms_y = 0;
    if (ms_y > VGA_H - 1) ms_y = VGA_H - 1;
    ms_left = (b & 1) != 0;
    vga32_pointer(true, ms_x, ms_y);
}

static bool ms_enable(void)
{
    // reset (it answers FA AA 00), then data reporting on (FA)
    if (!port_send(&ms, 0xff)) return false;
    const int64_t until = esp_timer_get_time() + 1000000;
    while (esp_timer_get_time() < until) {
        int b = port_get(&ms);
        if (b == 0xaa) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    while (port_get(&ms) >= 0) {}
    if (!port_send(&ms, 0xf4)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    while (port_get(&ms) >= 0) {}
    npkt = 0;
    return true;
}

static void ps2_task(void *arg)
{
    (void)arg;
    int64_t next_try = 0;
    for (;;) {
        int b;
        while ((b = port_get(&kb)) >= 0) kb_byte((uint8_t)b);
        const int64_t now = esp_timer_get_time();
        if (!ms_on && now >= next_try) {
            ms_on = ms_enable();
            if (ms_on) {
                ESP_LOGI(TAG, "mouse on");
                vga32_pointer(true, ms_x, ms_y);
            }
            next_try = now + 3000000;
        }
        while ((b = port_get(&ms)) >= 0) {
            if (ms_on && npkt == 0 && b == 0xaa) {     // plugged in again
                ms_on = false;
                next_try = 0;
                break;
            }
            if (npkt == 0 && !(b & 0x08)) continue;    // not a packet's first byte
            pkt[npkt++] = (uint8_t)b;
            if (npkt == 3) {
                npkt = 0;
                ms_packet();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ps2_start(void)
{
    esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGE(TAG, "isr service: %s", esp_err_to_name(e));
    port_init(&kb);
    port_init(&ms);
    xTaskCreatePinnedToCore(ps2_task, "ps2", 3072, NULL, 11, NULL, 0);   // (above the frontends: it sleeps 5 ms)
}
