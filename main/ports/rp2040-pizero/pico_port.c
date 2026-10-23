// pico_port.c - common code of the RP2040-PiZero ports. See pico_port.h.
// HDMI and sound follow dsp-cpp's ports/rp2040-pizero (video_dvi.c,
// audio_pwm.c); USB input its usb_input.cpp / hid.cpp, here in C.
#include "pico_port.h"

#include <stdio.h>
#include <string.h>

#include "dvi.h"
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "tmds_encode.h"
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

#ifndef PICO_AUDIO_PIN
#define PICO_AUDIO_PIN 4
#endif
#define AUDIO_RING 2048u            // samples, power of two
#define AUDIO_DMA_SAMPLES 256u
#define AUDIO_DMA_IRQ 1             // DMA_IRQ_1 on core 0 (PicoDVI has DMA_IRQ_0 on core 1)
#define PWM_WRAP 1023u

static pico_port_cfg cfg;

// ---------------------------------------------------------------------------
// HDMI (core 1)
// ---------------------------------------------------------------------------
static struct dvi_inst dvi;
static const struct dvi_serialiser_cfg pizero_dvi = {
    .pio = pio1,
    .sm_tmds = {0, 1, 2},
    .pins_tmds = {26, 24, 22},       // D0 26/27, D1 24/25, D2 22/23
    .pins_clk = 28,                  // CLK 28/29
    .invert_diffpairs = false,
};
static uint16_t band_buf[PICO_BAND * PICO_W] __attribute__((aligned(4)));
static volatile bool video_on;

static void __not_in_flash_func(core1_main)(void)
{
    dvi_register_irqs_this_core(&dvi, DMA_IRQ_0);
    dvi_start(&dvi);
    const uint words = dvi.timing->h_active_pixels / DVI_SYMBOLS_PER_WORD;
    for (;;) {
        for (int y = 0; y < PICO_H; y += PICO_BAND) {
            int n = PICO_H - y < PICO_BAND ? PICO_H - y : PICO_BAND;
            if (video_on && cfg.render) cfg.render(cfg.user, y, n, band_buf);
            else for (int i = 0; i < n * PICO_W; i++) band_buf[i] = 0;
            for (int i = 0; i < n; i++) {
                uint32_t *tmds;
                queue_remove_blocking_u32(&dvi.q_tmds_free, &tmds);
                const uint32_t *pix = (const uint32_t *)(band_buf + i * PICO_W);
                tmds_encode_data_channel_16bpp(pix, tmds + 0 * words, PICO_W, DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
                tmds_encode_data_channel_16bpp(pix, tmds + 1 * words, PICO_W, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
                tmds_encode_data_channel_16bpp(pix, tmds + 2 * words, PICO_W, DVI_16BPP_RED_MSB, DVI_16BPP_RED_LSB);
                queue_add_blocking_u32(&dvi.q_tmds_valid, &tmds);
            }
        }
    }
}

static void video_init(void)
{
    dvi.timing = &dvi_timing_640x480p_60hz;
    dvi.ser_cfg = pizero_dvi;
    dvi.scanline_callback = 0;
    dvi_init(&dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    // core 1 must not wait for the bus behind core 0
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);      // on its stack in SCRATCH_X (PICO_CORE1_STACK_SIZE)
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

void tuh_hid_mount_cb(uint8_t dev, uint8_t inst, uint8_t const *desc, uint16_t len)
{
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

#ifdef PICO_EMU_TEST
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
#ifdef PICO_EMU_TEST
    if (stat_frames % 10 == 0) printf("frame %u at %u ms\n", (unsigned)stat_frames, (unsigned)(now / 1000));
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
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(252000, true);
    stdio_init_all();
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
