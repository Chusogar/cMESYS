// vga.c - 320x240 VGA output of the LilyGO TTGO VGA32 (v1.4): the ESP32's I2S1
// in LCD (8-bit parallel) mode, fed by a looping DMA descriptor chain, as
// bitluni's ESP32Lib and FabGL do. No CPU and no interrupt once started.
//
// Timing: 640x480 @ 60 Hz (25.175 MHz) at half the pixel clock, each line of
// the framebuffer sent twice: 400 x 525 "pixels" at 12.5875 MHz, 59.94 Hz.
// A byte per pixel: bits 0-1 red, 2-3 green, 4-5 blue (two each, the board's
// resistor DACs), 6 HSync, 7 VSync (both active low).
// Pins: R 21 (LSB) 22, G 18 19, B 4 5, HSync 23, VSync 15.
#include "vga.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_rom_gpio.h"
#include "driver/gpio.h"
#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "soc/gpio_sig_map.h"
#include "esp_private/periph_ctrl.h"
#include "clk_ctrl_os.h"
#include "rom/lldesc.h"

static const char *TAG = "vga";

#define H_VIS   320
#define H_FP    8
#define H_SYNC  48
#define H_BP    24
#define H_BLANK (H_FP + H_SYNC + H_BP)
#define V_FP    10
#define V_SYNC  2
#define V_BP    33
#define PIXEL_HZ 12587500

#define HS 0x40
#define VS 0x80

uint8_t *vga_fb;                    // VGA_W x VGA_H, bytes swapped in pairs of 16 bits (VGA_FBI)

static const int pins[8] = {21, 22, 18, 19, 4, 5, 23, 15};
static lldesc_t *desc;
static uint8_t *hblank, *hblank_vs, *black, *black_vs;

static void set_desc(lldesc_t *d, const uint8_t *buf, int len, lldesc_t *next)
{
    d->size = (uint32_t)len;
    d->length = (uint32_t)len;
    d->offset = 0;
    d->sosf = 0;
    d->eof = 0;
    d->owner = 1;
    d->buf = buf;
    d->empty = (uint32_t)next;
}

bool vga_start(void)
{
    vga_fb = heap_caps_malloc(VGA_W * VGA_H, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    hblank = heap_caps_malloc(H_BLANK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    hblank_vs = heap_caps_malloc(H_BLANK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    black = heap_caps_malloc(H_VIS, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    black_vs = heap_caps_malloc(H_VIS, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const int lines = VGA_H * 2 + V_FP + V_SYNC + V_BP;
    desc = heap_caps_malloc(sizeof(lldesc_t) * lines * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!vga_fb || !hblank || !hblank_vs || !black || !black_vs || !desc) {
        ESP_LOGE(TAG, "no internal DMA memory for the framebuffer");
        return false;
    }
    memset(vga_fb, HS | VS, VGA_W * VGA_H);
    // (segments of uniform bytes, each a multiple of 4: the swap does not matter)
    memset(hblank, HS | VS, H_BLANK);
    memset(hblank + H_FP, VS, H_SYNC);
    memset(hblank_vs, HS, H_BLANK);
    memset(hblank_vs + H_FP, 0, H_SYNC);
    memset(black, HS | VS, H_VIS);
    memset(black_vs, HS, H_VIS);

    // visible lines (each twice), front porch, vsync, back porch; then again
    int n = 0;
    const int total = lines * 2;
    for (int l = 0; l < lines; l++) {
        const uint8_t *vis, *blank;
        if (l < VGA_H * 2) {
            vis = vga_fb + (l / 2) * VGA_W;
            blank = hblank;
        } else if (l < VGA_H * 2 + V_FP) {
            vis = black;
            blank = hblank;
        } else if (l < VGA_H * 2 + V_FP + V_SYNC) {
            vis = black_vs;
            blank = hblank_vs;
        } else {
            vis = black;
            blank = hblank;
        }
        set_desc(&desc[n], vis, H_VIS, &desc[(n + 1) % total]);
        n++;
        set_desc(&desc[n], blank, H_BLANK, &desc[(n + 1) % total]);
        n++;
    }

    // pins: the I2S1 data outputs 0-7
    for (int i = 0; i < 8; i++) {
        esp_rom_gpio_pad_select_gpio(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_drive_capability(pins[i], GPIO_DRIVE_CAP_3);
        esp_rom_gpio_connect_out_signal(pins[i], I2S1O_DATA_OUT0_IDX + i, false, false);
    }

    // the clock: the APLL at twice the pixel clock, divided by 2
    periph_rtc_apll_acquire();
    uint32_t real = 0;
    esp_err_t e = periph_rtc_apll_freq_set(PIXEL_HZ * 2, &real);
    ESP_LOGI(TAG, "APLL %u Hz (%s)", (unsigned)real, esp_err_to_name(e));

    periph_module_enable(PERIPH_I2S1_MODULE);
    i2s_dev_t *i2s = &I2S1;
    i2s->conf.tx_reset = 1;
    i2s->conf.tx_reset = 0;
    i2s->conf.rx_reset = 1;
    i2s->conf.rx_reset = 0;
    i2s->lc_conf.in_rst = 1;
    i2s->lc_conf.out_rst = 1;
    i2s->lc_conf.ahbm_rst = 1;
    i2s->lc_conf.ahbm_fifo_rst = 1;
    i2s->lc_conf.in_rst = 0;
    i2s->lc_conf.out_rst = 0;
    i2s->lc_conf.ahbm_rst = 0;
    i2s->lc_conf.ahbm_fifo_rst = 0;
    i2s->conf.tx_fifo_reset = 1;
    i2s->conf.tx_fifo_reset = 0;

    i2s->conf2.val = 0;
    i2s->conf2.lcd_en = 1;
    i2s->conf2.lcd_tx_wrx2_en = 1;
    i2s->conf2.lcd_tx_sdx2_en = 0;

    i2s->sample_rate_conf.val = 0;
    i2s->sample_rate_conf.tx_bits_mod = 8;
    i2s->sample_rate_conf.tx_bck_div_num = 1;
    i2s->clkm_conf.val = 0;
    i2s->clkm_conf.clkm_div_num = 2;
    i2s->clkm_conf.clkm_div_a = 1;
    i2s->clkm_conf.clkm_div_b = 0;
    i2s->clkm_conf.clka_en = 1;         // the APLL

    i2s->fifo_conf.val = 0;
    i2s->fifo_conf.tx_fifo_mod_force_en = 1;
    i2s->fifo_conf.tx_fifo_mod = 1;
    i2s->fifo_conf.tx_data_num = 32;
    i2s->fifo_conf.dscr_en = 1;

    i2s->conf1.val = 0;
    i2s->conf1.tx_stop_en = 0;
    i2s->conf1.tx_pcm_bypass = 1;
    i2s->conf_chan.val = 0;
    i2s->conf_chan.tx_chan_mod = 1;
    i2s->conf.tx_right_first = 1;
    i2s->timing.val = 0;
    i2s->conf.tx_msb_right = 0;
    i2s->conf.tx_msb_shift = 0;
    i2s->conf.tx_mono = 0;
    i2s->conf.tx_short_sync = 0;

    i2s->lc_conf.val = I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN;
    i2s->out_link.addr = ((uint32_t)&desc[0]) & 0xfffff;
    i2s->out_link.start = 1;
    i2s->int_clr.val = i2s->int_raw.val;
    i2s->int_ena.val = 0;
    i2s->conf.tx_start = 1;
    ESP_LOGI(TAG, "320x240 VGA (640x480 @ 60 Hz) started");
    return true;
}
