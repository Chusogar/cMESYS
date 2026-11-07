// pico_port.c - common code of the RP2040-PiZero and RP2350-PiZero ports.
// See pico_port.h. HDMI and sound follow dsp-cpp's ports/rp2040-pizero and
// rp2350-pizero (video_dvi.c, audio_pwm.c, psram.c); USB input its
// usb_input.cpp / hid.cpp, here in C.
#include "pico_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !PICO_RP2350
#include "dvi.h"                    // PicoDVI
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "tmds_encode.h"
#endif
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "tusb.h"

#if PICO_RP2350
// RP2350-PiZero: a 320x240 frame buffer (or two) in SRAM, PSRAM for the
// heap, keyboards on the PIO-USB port (GPIO28/29), picture and sound on the
// HDMI (rp2350/hdmi: pico_lib's DVI, with data islands for the sound)
#define PIZERO_FB 1
#include "hardware/pio.h"
#include "rp2350/psram.h"
#include "rp2350/hdmi/hdmi_glue.h"
#if CFG_TUH_RPI_PIO_USB
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "hardware/regs/sysinfo.h"
void pico_usb_frame_start(void);    // rp2350/usb_frame.c
#endif
#define HDMI_AUDIO_HZ 44100
#define PSRAM_CS_PIN 47
void pico_heap_sram_only(bool on);
size_t pico_heap_sram_free(void);
#else
#ifndef PICO_AUDIO_PIN
#define PICO_AUDIO_PIN 4
#endif
#if PICO_AUDIO_PIN >= 22
#error "GP22-29 are the HDMI pairs (D2 22/23, D1 24/25, D0 26/27, CLK 28/29)"
#endif
#endif
#define AUDIO_RING 2048u            // samples, power of two
#define AUDIO_DMA_SAMPLES 256u
#define AUDIO_DMA_IRQ 1             // DMA_IRQ_1 on core 0 (PicoDVI has DMA_IRQ_0 on core 1)
#define PWM_WRAP 1023u

static pico_port_cfg cfg;

// ---------------------------------------------------------------------------
// HDMI (core 1)
// ---------------------------------------------------------------------------
#ifndef PIZERO_FB
static struct dvi_inst dvi;
static const struct dvi_serialiser_cfg pizero_dvi = {
    .pio = pio1,
    .sm_tmds = {0, 1, 2},
    .pins_tmds = {26, 24, 22},       // D0 26/27, D1 24/25, D2 22/23
    .pins_clk = 28,                  // CLK 28/29
    .invert_diffpairs = false,
};
#endif
static uint16_t band_buf[PICO_BAND * PICO_W] __attribute__((aligned(4)));
static volatile bool video_on;

// ---------------------------------------------------------------------------
// On-screen text (menus): drawn by core 1 over the rows it sends
// ---------------------------------------------------------------------------
#include "../esp32-cyd/font5x7.h"        // ASCII 32..90, column-major, bit 0 = top
static uint8_t osd_font[59][5];         // (a copy in RAM: core 1 draws with it)
static char osd_chars[PICO_OSD_ROWS][PICO_OSD_COLS];
static bool osd_hl[PICO_OSD_ROWS];
static volatile bool osd_on;
#define OSD_BG  0x0008                   // dark blue
#define OSD_FG  0xffff
#define OSD_HBG 0xfd20                   // orange bar
#define OSD_HFG 0x0000

void pico_osd_show(bool on) { osd_on = on; }

void pico_osd_clear(void)
{
    if (!osd_font[1][2]) memcpy(osd_font, font5x7, sizeof osd_font);   // ('!' has a dot there)
    memset(osd_chars, ' ', sizeof osd_chars);
    memset(osd_hl, 0, sizeof osd_hl);
}

void pico_osd_text(int row, const char *text, bool highlight)
{
    if (row < 0 || row >= PICO_OSD_ROWS) return;
    int i = 0;
    for (; i < PICO_OSD_COLS && text[i]; i++) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c < 32 || c > 90) c = c == '_' ? '-' : '?';
        osd_chars[row][i] = c;
    }
    for (; i < PICO_OSD_COLS; i++) osd_chars[row][i] = ' ';
    osd_hl[row] = highlight;
}

void pico_osd_list(const char *title, const char *const *items, int n, int sel, const char *help)
{
    pico_osd_clear();
    pico_osd_text(0, title, false);
    const int view = PICO_OSD_ROWS - 4;          // rows 2 .. 27
    int top = sel - view / 2;
    if (top > n - view) top = n - view;
    if (top < 0) top = 0;
    for (int i = 0; i < view && top + i < n; i++) {
        char line[PICO_OSD_COLS + 1];
        snprintf(line, sizeof line, " %s", items[top + i]);
        pico_osd_text(2 + i, line, top + i == sel);
    }
    if (help) pico_osd_text(PICO_OSD_ROWS - 1, help, false);
}

// Row y of the text screen (6x8 cells, 2 pixels of margin left / right)
static void __not_in_flash_func(osd_row)(int y, uint16_t *out)
{
    const int r = y / 8, gy = y & 7;
    const bool hl = r < PICO_OSD_ROWS && osd_hl[r];
    const uint16_t bg = hl ? OSD_HBG : OSD_BG, fg = hl ? OSD_HFG : OSD_FG;
    for (int x = 0; x < PICO_W; x++) out[x] = bg;
    if (r >= PICO_OSD_ROWS || gy == 7) return;
    for (int c = 0; c < PICO_OSD_COLS; c++) {
        const uint8_t *g = osd_font[osd_chars[r][c] - 32];
        uint16_t *o = out + 4 + c * 6;
        for (int k = 0; k < 5; k++)
            if ((g[k] >> gy) & 1) o[k] = fg;
    }
}

#ifdef PIZERO_FB
// ---------------------------------------------------------------------------
// RP2350: the picture is kept whole. Core 1 TMDS-encodes the rows of the
// front picture as the HDMI needs them, and in the time left draws the game's
// next picture (the same render callback as the RP2040's, a band at a time)
// into the back one; the two swap at the top of the screen once the back one
// is whole. A game that draws slowly shows fewer pictures a second, never red
// lines or a tear. With no room for a second picture it draws into the one on
// screen just behind the rows being sent (a tear only when a picture takes
// longer than a scan to draw).
// ---------------------------------------------------------------------------
static uint16_t fb0[PICO_W * PICO_H] __attribute__((aligned(4)));
static uint16_t *volatile fb_front = fb0, *volatile fb_back;     // fb_back NULL: one picture
static volatile uint32_t frame_seq;            // frames the game has finished

#define FB_RENDER_ROWS 2                       // rows drawn at a time

// The rows go to the HDMI from an interrupt after every line: a
// game's drawing on core 1, however long a band takes, never makes a row
// late. One row each time (each row is shown on two lines, so they keep
// ahead), more before the start.
static volatile int out_y;                      // next row to the HDMI
static volatile uint32_t scan;                  // pictures sent so far
static volatile bool fb_ready;                  // fb_back is whole: show it at the top

static uint16_t osd_line[PICO_W] __attribute__((aligned(4)));

static bool __not_in_flash_func(feed_row)(void)
{
    int y = out_y;
    if (y == 0 && fb_ready) {                   // top of the screen: show the new picture
        uint16_t *t = fb_front;
        fb_front = fb_back;
        fb_back = t;
        fb_ready = false;
    }
    const uint16_t *src = fb_front + y * PICO_W;
    if (osd_on) {
        osd_row(y, osd_line);
        src = osd_line;
    }
    if (!pzhdmi_try_row(2 * y, 2, src, PICO_W)) return false;
    if (++y == PICO_H) {
        y = 0;
        scan++;
    }
    out_y = y;
    return true;
}

static void __not_in_flash_func(feed_hdmi)(void *user)
{
    (void)user;
    // rows that came too late are dropped by the HDMI: go on from the line
    // being shown, not from them
    const int line = pzhdmi_active_line();
    // (rows already queued for the next picture are ahead, not behind)
    if (line >= 0 && 2 * out_y <= line && line - 2 * out_y < 2 * PICO_H - 16 && line < 2 * PICO_H - 2)
        out_y = line / 2 + 1;
    // (two at most: this interrupt may have been held up a line)
    if (feed_row()) feed_row();
}

static void __not_in_flash_func(core1_main)(void)
{
    while (feed_row()) {            // the first rows before the start
    }
    pzhdmi_core1_start();           // (its DMA interrupt on this core)
    int draw_y = 0;                 // next row to draw
    uint32_t draw_scan = 0;         // (one picture: the scan the drawing started in)
    uint32_t last_scan = 0;
    bool drawing = false;
    uint32_t seq_drawn = 0;
    for (;;) {
        // the next picture, a band at a time. With one picture it is drawn
        // behind the rows already sent, from the top of a scan: whole on the
        // next one if it takes less than a scan (no tear line)
        const uint32_t sc = scan;
        const bool top = sc != last_scan;
        last_scan = sc;
        if (!drawing && !fb_ready && video_on && cfg.render && frame_seq != seq_drawn && (fb_back || top)) {
            seq_drawn = frame_seq;
            drawing = true;
            draw_y = 0;
            draw_scan = sc;
        }
        bool drew = false;
        if (drawing) {
            const int n = PICO_H - draw_y < FB_RENDER_ROWS ? PICO_H - draw_y : FB_RENDER_ROWS;
            if (fb_back || scan != draw_scan || draw_y + n <= out_y) {
                uint16_t *dst = fb_back ? fb_back : fb_front;
                cfg.render(cfg.user, draw_y, n, dst + draw_y * PICO_W);
                draw_y += n;
                drew = true;
                if (draw_y >= PICO_H) {
                    drawing = false;
                    if (fb_back) fb_ready = true;
                }
            }
        }
        if (!drew) __wfe();         // (the HDMI interrupt and frame_done are events)
    }
}
#endif

#ifndef PIZERO_FB
// Core 1 has to hand the HDMI each row in time (a 320-pixel row every
// 63.5 us), and the games draw from their ROMs in flash. Flash is one bus
// through a 16 KB cache shared by both cores, and core 0 can keep it busy
// (a 68000 reads its program, its opcode tables and most of its handlers
// from it): then core 1 is late and PicoDVI sends red lines. So when few
// encoded rows are left in the queue, core 1 holds core 0 (an interrupt
// that waits from RAM, not touching flash) until it has caught up. A game
// that draws in time never holds it.
#define HOLD_BELOW 2                // queued rows: hold core 0 below this...
#define RELEASE_AT (DVI_N_TMDS_BUFFERS > 5 ? DVI_N_TMDS_BUFFERS - 3 : 2)   // ...and let it go at this many
                                    // (one buffer is on screen, one being freed)
static volatile bool core0_hold;

static void __not_in_flash_func(core0_hold_irq)(void)
{
    while (multicore_fifo_rvalid()) (void)sio_hw->fifo_rd;
    multicore_fifo_clear_irq();
    while (core0_hold) __wfe();
}

static inline void __not_in_flash_func(hold_core0)(void)
{
    if (core0_hold) return;
    core0_hold = true;
    __dmb();
    if (multicore_fifo_wready()) sio_hw->fifo_wr = 1;
}

static inline void __not_in_flash_func(release_core0)(void)
{
    if (!core0_hold) return;
    core0_hold = false;
    __dmb();
    __sev();
}

static void __not_in_flash_func(core1_main)(void)
{
    dvi_register_irqs_this_core(&dvi, DMA_IRQ_0);
    dvi_start(&dvi);
    const uint words = dvi.timing->h_active_pixels / DVI_SYMBOLS_PER_WORD;
    for (;;) {
        for (int y = 0; y < PICO_H; y += PICO_BAND) {
            int n = PICO_H - y < PICO_BAND ? PICO_H - y : PICO_BAND;
            if (queue_get_level(&dvi.q_tmds_valid) < HOLD_BELOW) hold_core0();
            if (osd_on) for (int i = 0; i < n; i++) osd_row(y + i, band_buf + i * PICO_W);
            else if (video_on && cfg.render) cfg.render(cfg.user, y, n, band_buf);
            else for (int i = 0; i < n * PICO_W; i++) band_buf[i] = 0;
            for (int i = 0; i < n; i++) {
                uint32_t *tmds;
                if (queue_get_level(&dvi.q_tmds_valid) < HOLD_BELOW) hold_core0();
                queue_remove_blocking_u32(&dvi.q_tmds_free, &tmds);
                const uint32_t *pix = (const uint32_t *)(band_buf + i * PICO_W);
                tmds_encode_data_channel_16bpp(pix, tmds + 0 * words, PICO_W, DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
                tmds_encode_data_channel_16bpp(pix, tmds + 1 * words, PICO_W, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
                tmds_encode_data_channel_16bpp(pix, tmds + 2 * words, PICO_W, DVI_16BPP_RED_MSB, DVI_16BPP_RED_LSB);
                queue_add_blocking_u32(&dvi.q_tmds_valid, &tmds);
            }
            if (queue_get_level(&dvi.q_tmds_valid) >= RELEASE_AT) release_core0();
        }
    }
}
#endif

static void video_init(void)
{
#ifdef PIZERO_FB
    // the TMDS rows and the sound ring in SRAM (the DMA and the interrupt
    // read them all the time: not the PSRAM)
    pico_heap_sram_only(true);
    pzhdmi_init(HDMI_AUDIO_HZ, feed_hdmi, NULL);
    // then a second picture, if it fits in SRAM with 48 KB left over for the
    // game's later needs (the rest of its heap goes on in the PSRAM)
    if (pico_heap_sram_free() >= PICO_W * PICO_H * 2 + 48 * 1024)
        fb_back = calloc(PICO_W * PICO_H, sizeof *fb_back);
    pico_heap_sram_only(false);
#else
    dvi.timing = &dvi_timing_640x480p_60hz;
    dvi.ser_cfg = pizero_dvi;
    dvi.scanline_callback = 0;
    dvi_init(&dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
#endif
    // core 1 must not wait for the bus behind core 0
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);
#ifndef PIZERO_FB
    // core 1 holds core 0 through the FIFO (see core0_hold_irq): lowest
    // priority, so the sound's DMA interrupt still runs while it waits
    multicore_fifo_drain();
    multicore_fifo_clear_irq();
    irq_set_exclusive_handler(SIO_IRQ_PROC0, core0_hold_irq);
    irq_set_priority(SIO_IRQ_PROC0, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(SIO_IRQ_PROC0, true);      // on its stack in SCRATCH_X (PICO_CORE1_STACK_SIZE)
#endif
}

// ---------------------------------------------------------------------------
// Sound: PWM level from a ring, two DMA buffers paced by a DMA timer
// ---------------------------------------------------------------------------
static int16_t ring[AUDIO_RING];
static volatile uint32_t r_read, r_write;
static uint32_t dma_buf[2][AUDIO_DMA_SAMPLES] __attribute__((aligned(4)));
static int dma_ch[2] = {-1, -1};
static uint pwm_slice, pwm_chan;
static int16_t last_sample;
static bool audio_ok;

static void __not_in_flash_func(audio_fill)(uint32_t *buf)
{
    uint32_t r = r_read;
    const uint32_t w = r_write;
    for (unsigned i = 0; i < AUDIO_DMA_SAMPLES; i++) {
        if (r != w) { last_sample = ring[r & (AUDIO_RING - 1)]; r++; }
        uint32_t level = ((uint32_t)(last_sample + 32768) * (PWM_WRAP + 1)) >> 16;
        buf[i] = pwm_chan == PWM_CHAN_A ? level : level << 16;
    }
    r_read = r;
}

static void __not_in_flash_func(audio_irq)(void)
{
    for (int i = 0; i < 2; i++)
        if (dma_irqn_get_channel_status(AUDIO_DMA_IRQ, (uint)dma_ch[i])) {
            dma_irqn_acknowledge_channel(AUDIO_DMA_IRQ, (uint)dma_ch[i]);
            audio_fill(dma_buf[i]);
            dma_channel_set_read_addr((uint)dma_ch[i], dma_buf[i], false);
        }
}

#ifdef PIZERO_FB
// RP2350: the sound goes to the TV in the HDMI's data islands, at 44.1 kHz:
// the game's samples resampled (linear), the same on both channels. The
// HDMI's ring (46 ms) paces the game, as the PWM's ring does on the RP2040.
static uint32_t rs_step, rs_pos;                // input samples an output sample, 16.16
static int16_t rs_prev;

static void audio_init(int rate)
{
    rs_step = (uint32_t)(((uint64_t)rate << 16) / HDMI_AUDIO_HZ);
    rs_pos = 0;
    audio_ok = true;
    printf("sound: HDMI, %d Hz resampled to %d Hz\n", rate, HDMI_AUDIO_HZ);
}

static void hdmi_audio(const int16_t *s, int n)
{
    for (int i = 0; i < n; i++) {
        const int32_t cur = s[i], prev = rs_prev;
        while (rs_pos < 0x10000u) {
            const int16_t v = (int16_t)(prev + (((cur - prev) * (int32_t)(rs_pos >> 2)) >> 14));
            while (!pzhdmi_audio_free()) {      // full: real time has caught up with us
                tuh_task();
                sleep_us(200);
            }
            pzhdmi_audio_put(v, v);
            rs_pos += rs_step;
        }
        rs_pos -= 0x10000u;
        rs_prev = (int16_t)cur;
    }
}
#else
static void audio_init(int rate)
{
    gpio_set_function(PICO_AUDIO_PIN, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(PICO_AUDIO_PIN);
    pwm_chan = pwm_gpio_to_channel(PICO_AUDIO_PIN);
    pwm_config pc = pwm_get_default_config();
    pwm_config_set_wrap(&pc, PWM_WRAP);
    pwm_init(pwm_slice, &pc, true);
    pwm_set_chan_level(pwm_slice, pwm_chan, (PWM_WRAP + 1) / 2);
    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t den = (sys + (uint32_t)rate / 2) / (uint32_t)rate;
    if (den > 65535) den = 65535;
    int timer = dma_claim_unused_timer(true);
    dma_timer_set_fraction((uint)timer, 1, (uint16_t)den);
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) {
        audio_fill(dma_buf[i]);
        dma_channel_config c = dma_channel_get_default_config((uint)dma_ch[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, dma_get_timer_dreq((uint)timer));
        channel_config_set_chain_to(&c, (uint)dma_ch[i ^ 1]);
        dma_channel_configure((uint)dma_ch[i], &c, &pwm_hw->slice[pwm_slice].cc, dma_buf[i], AUDIO_DMA_SAMPLES, false);
        dma_irqn_set_channel_enabled(AUDIO_DMA_IRQ, (uint)dma_ch[i], true);
    }
    irq_add_shared_handler(dma_get_irq_num(AUDIO_DMA_IRQ), audio_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(dma_get_irq_num(AUDIO_DMA_IRQ), true);
    dma_channel_start((uint)dma_ch[0]);
    audio_ok = true;
    printf("sound: PWM on GPIO%d, %u Hz\n", PICO_AUDIO_PIN, (unsigned)(sys / den));
}
#endif

// ---------------------------------------------------------------------------
// USB input (TinyUSB host): boot keyboards and HID gamepads
// ---------------------------------------------------------------------------
static volatile uint8_t kb_state[256];      // by HID usage; 0xE0-0xE7 modifiers
static uint8_t presses[32];
static int npresses;
static bool reset_req;

typedef struct { uint8_t id, size; uint16_t usage; uint32_t bit; int32_t min, max; uint16_t page; } hid_field;
#define MAX_FIELDS 24
typedef struct {
    bool used, is_pad, ids;
    uint8_t dev, inst, proto;
    int nf, buttons;
    hid_field f[MAX_FIELDS];
    bool up, down, left, right, b[17];
} hid_dev;
static hid_dev devs[CFG_TUH_HID];

static bool pad_parse(hid_dev *d, const uint8_t *p, uint32_t len)
{
    struct { uint16_t page; int32_t min, max; uint32_t size, count; uint8_t id; } g = {0}, stack[4];
    int depth = 0;
    uint32_t usages[16];
    int nu = 0;
    uint32_t umin = 0, umax = 0;
    bool range = false;
    static uint32_t offs[256];
    memset(offs, 0, sizeof offs);
    d->nf = 0;
    d->buttons = 0;
    d->ids = false;
    uint32_t i = 0;
    while (i < len) {
        uint8_t prefix = p[i++];
        if (prefix == 0xfe) { if (i + 1 >= len) break; i += 2u + p[i]; continue; }
        uint32_t size = (prefix & 3) == 3 ? 4 : (prefix & 3);
        if (i + size > len) break;
        uint32_t v = 0;
        for (uint32_t b = 0; b < size; b++) v |= (uint32_t)p[i + b] << (8 * b);
        int32_t sv = (int32_t)v;
        if (size == 1) sv = (int8_t)v; else if (size == 2) sv = (int16_t)v;
        i += size;
        uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {
            switch (tag) {
            case 0: g.page = (uint16_t)v; break;
            case 1: g.min = sv; break;
            case 2: g.max = (g.min >= 0 && sv < 0) ? (int32_t)v : sv; break;
            case 7: g.size = v; break;
            case 8: g.id = (uint8_t)v; d->ids = true; break;
            case 9: g.count = v; break;
            case 10: if (depth < 4) stack[depth++] = g; break;
            case 11: if (depth > 0) g = stack[--depth]; break;
            default: break;
            }
        } else if (type == 2) {
            uint32_t full = size == 4 ? v : ((uint32_t)g.page << 16) | v;
            if (tag == 0 && nu < 16) usages[nu++] = full;
            else if (tag == 1) { umin = full; range = true; }
            else if (tag == 2) { umax = full; range = true; }
        } else if (type == 0) {
            if (tag == 8) {
                bool constant = v & 1, variable = v & 2;
                for (uint32_t n = 0; n < g.count; n++) {
                    if (!constant && variable && g.size > 0 && g.size <= 32) {
                        uint32_t u = 0;
                        if (range) u = umin + n <= umax ? umin + n : umax;
                        else if ((int)n < nu) u = usages[n];
                        else if (nu) u = usages[nu - 1];
                        if ((u >> 16) == 0) u |= (uint32_t)g.page << 16;
                        uint16_t page = (uint16_t)(u >> 16), usage = (uint16_t)u;
                        bool wanted = (page == 1 && (usage == 0x30 || usage == 0x31 || usage == 0x39)) ||
                                      (page == 9 && usage >= 1 && usage <= 16);
                        if (wanted && d->nf < MAX_FIELDS) {
                            hid_field *f = &d->f[d->nf++];
                            f->id = g.id; f->bit = offs[g.id]; f->size = (uint8_t)g.size;
                            f->min = g.min; f->max = g.max; f->page = page; f->usage = usage;
                            if (page == 9 && usage > d->buttons) d->buttons = usage;
                        }
                    }
                    offs[g.id] += g.size;
                }
            }
            nu = 0;
            range = false;
            umin = umax = 0;
        }
    }
    return d->nf > 0;
}

static int32_t field_read(const uint8_t *data, uint32_t len, const hid_field *f)
{
    uint32_t v = 0;
    for (uint32_t b = 0; b < f->size; b++) {
        uint32_t bit = f->bit + b;
        if (bit / 8 >= len) break;
        v |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 1) << b;
    }
    if (f->min < 0 && f->size < 32 && (v & (1u << (f->size - 1)))) v |= ~0u << f->size;
    return (int32_t)v;
}

static void pad_report(hid_dev *d, const uint8_t *r, uint32_t len)
{
    uint8_t id = 0;
    if (d->ids) { if (!len) return; id = *r++; len--; }
    bool any = false, b[17] = {0}, up = false, down = false, left = false, right = false;
    for (int i = 0; i < d->nf; i++) {
        const hid_field *f = &d->f[i];
        if (f->id != id) continue;
        any = true;
        int32_t v = field_read(r, len, f);
        if (f->page == 9) b[f->usage] = v != 0;
        else if (f->usage == 0x39) {
            int32_t h = v - f->min, steps = f->max - f->min + 1;
            if (h >= 0 && h < steps && steps >= 4) {
                int dir = steps == 4 ? h * 2 : h;
                up |= dir == 7 || dir == 0 || dir == 1;
                right |= dir >= 1 && dir <= 3;
                down |= dir >= 3 && dir <= 5;
                left |= dir >= 5 && dir <= 7;
            }
        } else {
            int64_t range = (int64_t)f->max - f->min;
            if (range <= 0) continue;
            bool low = (int64_t)v - f->min < range / 4, high = (int64_t)f->max - v < range / 4;
            if (f->usage == 0x30) { left |= low; right |= high; }
            else { up |= low; down |= high; }
        }
    }
    if (!any) return;
    d->up = up; d->down = down; d->left = left; d->right = right;
    memcpy(d->b, b, sizeof b);
}

static void kb_report(const uint8_t *r, uint32_t len)
{
    if (len < 3) return;
    uint8_t old[256];
    memcpy(old, (const void *)kb_state, sizeof old);
    for (int i = 0; i < 256; i++) kb_state[i] = 0;
    for (int m = 0; m < 8; m++) kb_state[0xe0 + m] = (r[0] >> m) & 1;
    for (uint32_t i = 2; i < len && i < 8; i++)
        if (r[i] >= 4) {
            kb_state[r[i]] = 1;
            if (!old[r[i]] && npresses < 32) presses[npresses++] = r[i];
        }
    // F3: reset (arcades)
    if (!cfg.computer && kb_state[0x3c] && !old[0x3c]) reset_req = true;
}

// USB diagnostics (pico_port_usb_info)
static volatile uint32_t usb_mounts, usb_hid_mounts, usb_reports;
static volatile uint8_t usb_last_vid_pid_ok;
static volatile uint16_t usb_vid, usb_pid;

void tuh_mount_cb(uint8_t dev)
{
    usb_mounts++;
    uint16_t vid = 0, pid = 0;
    if (tuh_vid_pid_get(dev, &vid, &pid)) { usb_vid = vid; usb_pid = pid; usb_last_vid_pid_ok = 1; }
    printf("usb: device %u mounted (%04x:%04x)\n", dev, vid, pid);
}

void tuh_umount_cb(uint8_t dev)
{
    printf("usb: device %u unmounted\n", dev);
}

void tuh_hid_mount_cb(uint8_t dev, uint8_t inst, uint8_t const *desc, uint16_t len)
{
    usb_hid_mounts++;
    hid_dev *d = NULL;
    for (int i = 0; i < CFG_TUH_HID; i++) if (!devs[i].used) { d = &devs[i]; break; }
    if (!d) return;
    memset(d, 0, sizeof *d);
    d->used = true;
    d->dev = dev;
    d->inst = inst;
    d->proto = tuh_hid_interface_protocol(dev, inst);
    if (d->proto == HID_ITF_PROTOCOL_NONE) d->is_pad = pad_parse(d, desc, len);
    printf("usb: %s connected\n", d->proto == HID_ITF_PROTOCOL_KEYBOARD ? "keyboard" : d->is_pad ? "gamepad" : "HID device");
    tuh_hid_receive_report(dev, inst);
}

void tuh_hid_umount_cb(uint8_t dev, uint8_t inst)
{
    for (int i = 0; i < CFG_TUH_HID; i++)
        if (devs[i].used && devs[i].dev == dev && devs[i].inst == inst) {
            if (devs[i].proto == HID_ITF_PROTOCOL_KEYBOARD) for (int k = 0; k < 256; k++) kb_state[k] = 0;
            devs[i].used = false;
        }
}

void tuh_hid_report_received_cb(uint8_t dev, uint8_t inst, uint8_t const *r, uint16_t len)
{
    usb_reports++;
    for (int i = 0; i < CFG_TUH_HID; i++) {
        hid_dev *d = &devs[i];
        if (!d->used || d->dev != dev || d->inst != inst) continue;
        if (d->proto == HID_ITF_PROTOCOL_KEYBOARD) kb_report(r, len);
        else if (d->is_pad) {
            bool sel_start = d->b[d->buttons >= 10 ? 9 : 7] && d->b[d->buttons >= 10 ? 10 : 8];
            pad_report(d, r, len);
            bool now = d->b[d->buttons >= 10 ? 9 : 7] && d->b[d->buttons >= 10 ? 10 : 8];
            if (now && !sel_start) reset_req = true;
        }
    }
    tuh_hid_receive_report(dev, inst);
}

void pico_port_usb_info(char *buf, int n)
{
#if CFG_TUH_RPI_PIO_USB
    const root_port_t *root = PIO_USB_ROOT_PORT(0);
    const uint32_t id = *(io_ro_32 *)(SYSINFO_BASE + SYSINFO_CHIP_ID_OFFSET);
    snprintf(buf, (size_t)n, "REV %u D+%d D-%d %s%s DEV %u HID %u REP %u ID %04X:%04X",
             (unsigned)((id & SYSINFO_CHIP_ID_REVISION_BITS) >> SYSINFO_CHIP_ID_REVISION_LSB),
             gpio_get(root->pin_dp) ? 0 : 1, gpio_get(root->pin_dm) ? 0 : 1,
             root->connected ? "CON" : "NC", root->is_fullspeed ? " FS" : " LS",
             (unsigned)usb_mounts, (unsigned)usb_hid_mounts, (unsigned)usb_reports, usb_vid, usb_pid);
#else
    snprintf(buf, (size_t)n, "DEV %u HID %u REP %u ID %04X:%04X", (unsigned)usb_mounts, (unsigned)usb_hid_mounts,
             (unsigned)usb_reports, usb_vid, usb_pid);
#endif
}

bool pico_port_key(int usage) { return usage >= 0 && usage < 256 && kb_state[usage]; }

int pico_port_next_press(void)
{
    if (!npresses) return -1;
    int k = presses[0];
    for (int i = 1; i < npresses; i++) presses[i - 1] = presses[i];
    npresses--;
    return k;
}

uint32_t pico_port_keys(void)
{
    uint32_t k = 0;
#define KB(u) kb_state[u]
    if (KB(0x22)) k |= SP_COIN1;                    // 5
    if (KB(0x23)) k |= SP_COIN2;                    // 6
    if (KB(0x1e)) k |= SP_START1;                   // 1
    if (KB(0x1f)) k |= SP_START2;                   // 2
    if (KB(0x52)) k |= SP_UP;
    if (KB(0x51)) k |= SP_DOWN;
    if (KB(0x50)) k |= SP_LEFT;
    if (KB(0x4f)) k |= SP_RIGHT;
    if (KB(0x1d) || KB(0xe0)) k |= SP_B1;           // Z, left Ctrl
    if (KB(0x1b) || KB(0xe2)) k |= SP_B2;           // X, left Alt
    if (KB(0x06) || KB(0x2c)) k |= SP_B3;           // C, space
#undef KB
    for (int i = 0; i < CFG_TUH_HID; i++) {
        const hid_dev *d = &devs[i];
        if (!d->used || !d->is_pad) continue;
        if (d->up) k |= SP_UP;
        if (d->down) k |= SP_DOWN;
        if (d->left) k |= SP_LEFT;
        if (d->right) k |= SP_RIGHT;
        if (d->b[1]) k |= SP_B1;
        if (d->b[2]) k |= SP_B2;
        if (d->b[3]) k |= SP_B3;
        if (d->b[d->buttons >= 10 ? 9 : 7]) k |= SP_COIN1;
        if (d->b[d->buttons >= 10 ? 10 : 8]) k |= SP_START1;
    }
    return k;
}

#if defined(PICO_EMU_TEST)
#define tuh_task() do { } while (0)
#endif

bool pico_port_poll(void)
{
    tuh_task();
    bool r = reset_req;
    reset_req = false;
    return r;
}

// ---------------------------------------------------------------------------
// Pacing and the log
// ---------------------------------------------------------------------------
static uint64_t next_frame_us, stat_t;
static uint32_t stat_frames;

void pico_port_audio(const int16_t *s, int n)
{
#ifdef PICO_EMU_TEST
    (void)s; (void)n;
    return;
#endif
    if (!audio_ok || !cfg.audio_rate) return;
#ifdef PIZERO_FB
    hdmi_audio(s, n);
    return;
#endif
    while (n > 0) {
        uint32_t w = r_write, room = AUDIO_RING - (w - r_read);
        if (room == 0) {                     // full: real time has caught up with us
            tuh_task();
            sleep_us(500);
            continue;
        }
        int k = (int)room < n ? (int)room : n;
        for (int i = 0; i < k; i++) ring[(w + (uint32_t)i) & (AUDIO_RING - 1)] = s[i];
        __dmb();
        r_write = w + (uint32_t)k;
        s += k;
        n -= k;
    }
}

void pico_port_frame_done(void)
{
    uint64_t now = time_us_64();
    if (!cfg.audio_rate) {
        uint64_t frame_us = (uint64_t)(1000000.0 / cfg.fps);
        if (!next_frame_us || next_frame_us + 100000 < now) next_frame_us = now;
        next_frame_us += frame_us;
        while (time_us_64() < next_frame_us) { tuh_task(); sleep_us(500); }
    }
    stat_frames++;
#ifdef PIZERO_FB
    frame_seq++;                    // core 1 draws this frame's picture
    __sev();
#endif
#ifdef PICO_EMU_TEST
    if (stat_frames % 10 == 0) printf("frame %u at %u ms\n", (unsigned)stat_frames, (unsigned)(now / 1000));
    if (stat_frames % 50 == 0 && cfg.render) {
        // what core 1 would spend drawing one picture (the HDMI's budget:
        // 240 rows in ~15 ms at 252 MHz, ~45 us a row with the TMDS encode)
        uint64_t t0 = time_us_64();
        for (int y = 0; y < PICO_H; y += PICO_BAND) cfg.render(cfg.user, y, PICO_BAND, band_buf);
        uint64_t t1 = time_us_64();
        uint32_t *tmds = (uint32_t *)dma_buf;    // (scratch: 2 x 1 KB, enough for one channel)
        for (int y = 0; y < PICO_H; y++)
            tmds_encode_data_channel_16bpp((const uint32_t *)band_buf, tmds, PICO_W / 2, DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
        printf("render: %u us a picture, TMDS (1 channel, half row): %u us\n", (unsigned)(t1 - t0),
               (unsigned)(time_us_64() - t1));
    }
    if (0) {
#else
    if (now - stat_t >= 5000000) {
#endif
        printf("%s: %.1f fps (target %.1f), sound queue %u\n", cfg.title,
               stat_frames * 1e6 / (double)(now - stat_t), cfg.fps, (unsigned)(r_write - r_read));
        stat_t = now;
        stat_frames = 0;
    }
}

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------
void pico_port_open(const pico_port_cfg *c)
{
    cfg = *c;
    // 640x480 DVI needs a 252 MHz system clock (the TMDS bit rate)
#ifdef PIZERO_FB
    vreg_set_voltage(VREG_VOLTAGE_1_25);     // (the RP2350 at 252 MHz: some headroom, as XRoar's port)
#else
    vreg_set_voltage(VREG_VOLTAGE_1_20);
#endif
    sleep_ms(10);
    set_sys_clock_khz(252000, true);
    stdio_init_all();
#ifdef PIZERO_FB
    // PSRAM and PIO-USB before core 1 starts: TinyUSB's alarm pool and the
    // PSRAM set-up must not run while core 1 sends the picture (as in
    // XRoar's port for this board). Then the picture, with what the start-up
    // did on it (and on the UART)
    printf("\ncMESYS %s on the RP2350-PiZero, %u MHz\n", cfg.title, (unsigned)(clock_get_hz(clk_sys) / 1000000));
    // the 8 MB PSRAM: the heap goes on there once the SRAM is full
#ifdef PICO_VIDEO_ONLY
    const size_t psram = 0;                  // (hdmitest: the picture and the sound only)
#else
    const size_t psram = dsp_psram_setup(PSRAM_CS_PIN);
#endif
    if (psram) dsp_heap_add_psram(DSP_PSRAM_BASE, psram);
    printf("PSRAM %u KB\n", (unsigned)(psram / 1024));
#if CFG_TUH_RPI_PIO_USB && !defined(PICO_EMU_TEST)
    // PIO0 and DMA channel 0, before the HDMI claims its own. Its 1 ms frame
    // runs from rp2350/usb_frame.c (the RP2350-E9 workaround), not from
    // Pico-PIO-USB's own timer
    pio_usb_configuration_t pcfg = PIO_USB_DEFAULT_CONFIG;
    pcfg.pin_dp = 28;
    pcfg.pio_tx_num = 0;
    pcfg.pio_rx_num = 0;
    pcfg.skip_alarm_pool = true;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pcfg);
#endif
#if !defined(PICO_EMU_TEST)
    tuh_init(BOARD_TUH_RHPORT);
#if CFG_TUH_RPI_PIO_USB
    pico_usb_frame_start();
#endif
    printf("USB host on PIO-USB (GPIO28/29)\n");
    const char *usb_line = "USB (PIO-USB, GPIO28/29): OK";
#else
    const char *usb_line = "USB: NOT STARTED (TEST)";
#endif
    char line[PICO_OSD_COLS + 1];
    int row = 0;
    pico_osd_clear();
    snprintf(line, sizeof line, "CMESYS %s - RP2350-PIZERO %u MHZ", cfg.title, (unsigned)(clock_get_hz(clk_sys) / 1000000));
    pico_osd_text(row++, line, true);
    snprintf(line, sizeof line, "PSRAM: %u KB", (unsigned)(psram / 1024));
    pico_osd_text(row++, line, false);
    pico_osd_text(row++, usb_line, false);
    pico_osd_show(true);
#ifndef PICO_EMU_TEST
    video_init();
#endif
    printf("frame buffers: %s; SRAM heap left %u KB\n", fb_back ? "2" : "1", (unsigned)(pico_heap_sram_free() / 1024));
    snprintf(line, sizeof line, "HDMI 640X480 60 HZ: OK  PICTURES: %d  SRAM FREE: %u KB", fb_back ? 2 : 1,
             (unsigned)(pico_heap_sram_free() / 1024));
    pico_osd_text(row++, line, false);
    pico_osd_text(row, "SOUND...", false);
#ifndef PICO_EMU_TEST
    if (cfg.audio_rate) audio_init(cfg.audio_rate);
#endif
    snprintf(line, sizeof line, "SOUND: %s", cfg.audio_rate ? "HDMI 44.1 KHZ" : "NONE");
    pico_osd_text(row++, line, false);
    pico_osd_text(row++, "STARTING...", false);
    printf("start-up done\n");
    const uint64_t t_end = time_us_64() + 1500000;
    while (time_us_64() < t_end) {
        tuh_task();                          // (a keyboard is found meanwhile)
        sleep_ms(1);
    }
    pico_osd_show(false);
#else
    printf("\ncMESYS %s on the RP2040-PiZero, %u MHz\n", cfg.title, (unsigned)(clock_get_hz(clk_sys) / 1000000));
#ifndef PICO_EMU_TEST
    tuh_init(BOARD_TUH_RHPORT);
    video_init();
#else
    // rp2040js (one core, no HDMI): the emulation alone, fps on the UART
    printf("PICO_EMU_TEST: no USB, no HDMI\n");
#endif
#ifndef PICO_EMU_TEST
    if (cfg.audio_rate) audio_init(cfg.audio_rate);
#endif
#endif
    video_on = true;
    stat_t = time_us_64();
}

// The SDK's stacks are in the 4 KB SCRATCH banks (core 0: PICO_STACK_SIZE,
// core 1: PICO_CORE1_STACK_SIZE): the 256 KB of main RAM stay for the game.
void pico_port_run(void (*fn)(void))
{
    fn();
    for (;;) tight_loop_contents();
}
