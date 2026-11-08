// lynx_core.c - Atari Lynx, see lynx_core.h (dsp-cpp's atari_lynx.cpp,
// lynx_mikey.cpp and lynx_suzy.cpp).
#include "lynx_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m65xx.h"

#define CPU_CLOCK 4000000
#define SCANLINES 105
#define CYCLES_PER_LINE 636

// ===========================================================================
// Mikey
// ===========================================================================
#define TIMERS 8
#define AUDIOS 4
#define UART_FRAME_BITS 11
#define UART_RX_NEXT_DELAY 44
#define UART_INACTIVE (-1)
#define UART_BREAK 0x8000
#define UART_QUEUE 32

typedef struct { uint8_t bakup, cntrl1, cntrl2, counter; int accum; } mk_timer;
typedef struct { int8_t volume; uint8_t feedback; int8_t output; uint16_t shifter; uint8_t bakup, control, counter, other; int accum; } mk_audio;

#define T_INT_EN(t) (((t)->cntrl1 & 0x80) != 0)
#define T_RELOAD(t) (((t)->cntrl1 & 0x10) != 0)
#define T_COUNT(t) (((t)->cntrl1 & 0x08) != 0)
#define T_LINKED(t) (((t)->cntrl1 & 0x07) == 0x07)
#define T_DONE(t) (((t)->cntrl2 & 0x08) != 0)
#define A_LINKED(a) (((a)->control & 0x07) == 0x07)
#define A_COUNT(a) (((a)->control & 0x08) != 0)
#define A_RELOAD(a) (((a)->control & 0x10) != 0)
#define A_DONE(a) (((a)->other & 0x08) != 0)

typedef struct {
    uint8_t data[0x100];
    mk_timer timers[TIMERS];
    mk_audio audio[AUDIOS];
    uint8_t interrupt;
    uint16_t disp_addr;
    bool vb_rest;
    int uart_div, uart_tx_countdown, uart_rx_countdown, uart_tx_data, uart_rx_data;
    bool uart_rx_ready, uart_tx_irq_en, uart_rx_irq_en, uart_parity_en, uart_parity_even, uart_send_break;
    bool uart_overrun, uart_framing;
    int uart_queue[UART_QUEUE], uart_queue_out, uart_queue_count;
} mikey;

// ===========================================================================
// Suzy
// ===========================================================================
typedef struct {
    uint8_t data[0x100];
    bool signed_math, accumulate, accumulate_overflow;
    int sign_ab, sign_cd;
    uint16_t screen, colbuf, colpos;
    int16_t xoff, yoff;
    uint8_t mode, spr_coll, spritenr;
    int16_t x_pos, y_pos;
    uint16_t width, height;
    int16_t tilt_acc;
    uint16_t height_acc, width_offset, height_offset;
    int16_t stretch, tilt;
    uint8_t color[16];
    uint16_t bitmap;
    bool use_rle;
    uint8_t line_color, spr_ctl0, spr_ctl1;
    uint16_t scb, scb_next;
    bool sprite_collide, everon;
    uint8_t fred;
    int memory_accesses;
    bool no_collide, vstretch, lefthanded, busy;
    int remaining;
    uint8_t joystick, switches;
} suzy;

struct lynx {
    m65xx cpu;
    mikey mk;
    suzy sz;
    uint8_t ram[0x10000];
    uint8_t boot_rom[0x200];
    bool bios_loaded;
    zx_pbuf cart;                       // the cartridge (its header skipped through cart_base)
    uint32_t cart_base, cart_size;
    uint16_t granularity;
    uint32_t audin_offset;
    uint8_t cart_block;
    uint16_t cart_counter;
    uint8_t last_sysctl, mapctl;
    int audio_rate;
    uint64_t audio_acc;
    int mk_pending, mk_next;            // Mikey's cycles not counted yet; the next one due
    uint32_t clock_mark;                // the CPU's clock up to which Mikey and Suzy have run
    int frame_debt;
    uint32_t audio_acc32;
    bool turbo;                         // no sound
    int16_t audio[LYNX_AUDIO_MAX];
    int naudio;
    uint8_t screen[LYNX_W / 2 * LYNX_H];  // the last frame's display, 4 bits a pixel
    uint32_t pal[16];
    uint16_t pal565[16];
    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);
    bool shown, flip;
    const char *error;
};

// ---------------------------------------------------------------------------
// Mikey
// ---------------------------------------------------------------------------
static void cpu_irq(lynx *m, bool on) { m65xx_set_irq(&m->cpu, on ? M65_ASSERT : M65_CLEAR); }
static void mk_wake(lynx *m) { m->cpu.halted = false; }
static void mk_update_irq(lynx *m) { cpu_irq(m, m->mk.interrupt != 0); }
static void cart_strobe(lynx *m, uint8_t sysctl, uint8_t iodat);

static void mk_reset(mikey *k)
{
    memset(k, 0, sizeof *k);
    k->data[0x88] = 0x01;
    for (int i = 0; i < AUDIOS; i++) k->audio[i].shifter = 1;
    k->uart_tx_countdown = k->uart_rx_countdown = UART_INACTIVE;
}

static void uart_loopback(mikey *k, int data)
{
    if (k->uart_queue_count >= UART_QUEUE) return;
    if (k->uart_queue_count == 0) k->uart_rx_countdown = UART_FRAME_BITS;
    k->uart_queue_out = (k->uart_queue_out + UART_QUEUE - 1) % UART_QUEUE;
    k->uart_queue[k->uart_queue_out] = data;
    k->uart_queue_count++;
}

static void uart_clock(mikey *k)
{
    if (k->uart_rx_countdown == 0) {
        if (k->uart_queue_count > 0) {
            k->uart_rx_data = k->uart_queue[k->uart_queue_out];
            k->uart_queue_out = (k->uart_queue_out + 1) % UART_QUEUE;
            k->uart_queue_count--;
        }
        k->uart_rx_countdown = k->uart_queue_count > 0 ? UART_FRAME_BITS + UART_RX_NEXT_DELAY : UART_INACTIVE;
        if (k->uart_rx_ready) k->uart_overrun = true;
        k->uart_rx_ready = true;
    } else if (k->uart_rx_countdown > 0) {
        k->uart_rx_countdown--;
    }
    if (k->uart_tx_countdown == 0) {
        if (k->uart_send_break) {
            k->uart_tx_data = UART_BREAK;
            k->uart_tx_countdown = UART_FRAME_BITS;
            uart_loopback(k, k->uart_tx_data);
        } else {
            k->uart_tx_countdown = UART_INACTIVE;
        }
    } else if (k->uart_tx_countdown > 0) {
        k->uart_tx_countdown--;
    }
}

static void uart_update_irq(lynx *m)
{
    mikey *k = &m->mk;
    const bool tx = k->uart_tx_irq_en && k->uart_tx_countdown == UART_INACTIVE;
    const bool rx = k->uart_rx_irq_en && k->uart_rx_ready;
    if ((tx || rx) && !(k->interrupt & 0x10)) {
        k->interrupt |= 0x10;
        mk_wake(m);
        mk_update_irq(m);
    }
}

static int prescale(int sel)
{
    static const int kScale[7] = { 4, 8, 16, 32, 64, 128, 256 };
    return sel >= 0 && sel <= 6 ? kScale[sel] : 4;
}

static void clock_audio(mikey *k, int channel);
static void count_down_linked(lynx *m, int which);

static void signal_irq(lynx *m, int which)
{
    mikey *k = &m->mk;
    if (T_INT_EN(&k->timers[which]) && which != 4) {
        k->interrupt |= (uint8_t)(1 << which);
        mk_wake(m);
        mk_update_irq(m);
    }
    switch (which) {
    case 0:
        if (k->timers[2].counter == 103) k->vb_rest = true;
        if (k->timers[2].counter == 102) k->disp_addr = (uint16_t)(k->data[0x94] | (k->data[0x95] << 8));
        if (k->timers[2].counter == 101) k->vb_rest = false;
        count_down_linked(m, 2);
        break;
    case 2: count_down_linked(m, 4); break;
    case 1: count_down_linked(m, 3); break;
    case 3: count_down_linked(m, 5); break;
    case 5: count_down_linked(m, 7); break;
    case 4:
        if (++k->uart_div >= 8) {           // timer 4: 8 borrows a UART bit
            k->uart_div = 0;
            uart_clock(k);
        }
        break;
    case 7: {
        mk_audio *a = &k->audio[0];
        if (A_LINKED(a) && A_COUNT(a) && !A_DONE(a)) {
            if (a->counter > 0) a->counter--;
            else clock_audio(k, 0);
        }
        break;
    }
    default: break;
    }
}

static void expire_timer(lynx *m, int which)
{
    signal_irq(m, which);
    mk_timer *t = &m->mk.timers[which];
    if (T_RELOAD(t)) t->counter = t->bakup;
    else t->cntrl2 |= 0x08;
}

static void count_down_linked(lynx *m, int which)
{
    mk_timer *t = &m->mk.timers[which];
    if (!T_COUNT(t) || !T_LINKED(t) || T_DONE(t)) return;
    if (t->counter > 0) t->counter--;
    else expire_timer(m, which);
}

static void clock_audio(mikey *k, int channel)
{
    mk_audio *a = &k->audio[channel];
    uint16_t shifter = a->shifter & 0x0fff;
    int taps = 0;
    const uint16_t mask = (uint16_t)(a->feedback | ((uint16_t)(a->other & 0x0f) << 8));
    for (int bit = 0; bit < 12; bit++)
        if ((mask & (1 << bit)) && (shifter & (1 << bit))) taps ^= 1;
    const int out = shifter & 1;
    a->shifter = (uint16_t)((shifter >> 1) | (taps ? 0x800 : 0));
    a->output = out ? a->volume : (int8_t)-a->volume;
    if (A_RELOAD(a)) a->counter = a->bakup;
    else a->other |= 0x08;
    if (channel + 1 < AUDIOS) {
        mk_audio *n = &k->audio[channel + 1];
        if (A_LINKED(n) && A_COUNT(n) && !A_DONE(n)) {
            if (n->counter > 0) n->counter--;
            else clock_audio(k, channel + 1);
        }
    }
}

// The free-running timers and audio channels counted in whole prescale
// ticks (no loop a tick); mk_sync brings them up to date only when one of
// them is due or the CPU touches Mikey: per instruction this was half of
// the Lynx's time on the boards.
static int ticks_to_expiry(int counter, int accum, int scale) { return (counter + 1) * scale - accum; }

static void mk_tick(lynx *m, int cycles)
{
    mikey *k = &m->mk;
    for (int w = 0; w < TIMERS; w++) {
        mk_timer *t = &k->timers[w];
        if (!T_COUNT(t) || T_LINKED(t) || T_DONE(t)) continue;
        const int scale = prescale(t->cntrl1 & 7);
        t->accum += cycles;
        int ticks = t->accum / scale;
        t->accum -= ticks * scale;
        while (ticks > 0) {
            if (ticks <= t->counter) {
                t->counter = (uint8_t)(t->counter - ticks);
                break;
            }
            ticks -= t->counter + 1;
            t->counter = 0;
            expire_timer(m, w);         // (reloads the counter, or stops the timer)
            if (T_DONE(t)) break;
        }
    }
    for (int c = 0; c < AUDIOS; c++) {
        mk_audio *a = &k->audio[c];
        if (!A_COUNT(a) || A_LINKED(a) || A_DONE(a)) continue;
        const int scale = prescale(a->control & 7);
        a->accum += cycles;
        int ticks = a->accum / scale;
        a->accum -= ticks * scale;
        while (ticks > 0) {
            if (ticks <= a->counter) {
                a->counter = (uint8_t)(a->counter - ticks);
                break;
            }
            ticks -= a->counter + 1;
            a->counter = 0;
            clock_audio(k, c);
            if (A_DONE(a)) break;
        }
    }
    uart_update_irq(m);
}

// Cycles until the next timer or audio channel underflows (at most 4096)
static int mk_next_event(const lynx *m)
{
    const mikey *k = &m->mk;
    int next = 4096;
    for (int w = 0; w < TIMERS; w++) {
        const mk_timer *t = &k->timers[w];
        if (!T_COUNT(t) || T_LINKED(t) || T_DONE(t)) continue;
        const int n = ticks_to_expiry(t->counter, t->accum, prescale(t->cntrl1 & 7));
        if (n < next) next = n;
    }
    for (int c = 0; c < AUDIOS; c++) {
        const mk_audio *a = &k->audio[c];
        if (!A_COUNT(a) || A_LINKED(a) || A_DONE(a)) continue;
        const int n = ticks_to_expiry(a->counter, a->accum, prescale(a->control & 7));
        if (n < next) next = n;
    }
    return next > 0 ? next : 1;
}

static void mk_sync(lynx *m)
{
    if (m->mk_pending) mk_tick(m, m->mk_pending);
    m->mk_pending = 0;
    m->mk_next = mk_next_event(m);
}

static uint8_t mk_read(lynx *m, uint8_t off)
{
    mikey *k = &m->mk;
    if (off < 0x20) {
        const mk_timer *t = &k->timers[off >> 2];
        switch (off & 3) {
        case 0: return t->bakup;
        case 1: return t->cntrl1;
        case 2: return t->counter;
        default: return t->cntrl2;
        }
    }
    if (off < 0x40) {
        const mk_audio *a = &k->audio[(off - 0x20) >> 3];
        switch (off & 7) {
        case 0: return (uint8_t)a->volume;
        case 1: return a->feedback;
        case 2: return (uint8_t)a->output;
        case 3: return (uint8_t)a->shifter;
        case 4: return a->bakup;
        case 5: return a->control;
        case 6: return a->counter;
        default: return (uint8_t)((a->other & 0xf0) | ((a->shifter >> 8) & 0x0f));
        }
    }
    switch (off) {
    case 0x80: case 0x81: return k->interrupt;
    case 0x84: case 0x85: return 0x00;
    case 0x86: return 0x80;
    case 0x88: return 0x01;
    case 0x8b: {
        const uint8_t dir = k->data[0x8a], dat = k->data[0x8b];
        uint8_t v = 0;
        v |= (dir & 0x01) ? (dat & 0x01) : 0x01;
        v |= (dir & 0x02) ? (dat & 0x02) : 0x00;
        v |= (dir & 0x04) ? (dat & 0x04) : 0x00;           // NOEXP: no ComLynx cable
        if (dir & 0x08) v |= ((dat & 0x08) && k->vb_rest) ? 0x00 : 0x08;
        v |= (dir & 0x10) ? (dat & 0x10) : 0x10;
        return v;
    }
    case 0x8c: {
        uint8_t v = 0;
        if (k->uart_tx_countdown == UART_INACTIVE) v |= 0xa0;
        if (k->uart_rx_ready) v |= 0x40;
        if (k->uart_overrun) v |= 0x08;
        if (k->uart_framing) v |= 0x04;
        if (k->uart_rx_data & UART_BREAK) v |= 0x02;
        if (k->uart_rx_data & 0x100) v |= 0x01;
        return v;
    }
    case 0x8d: k->uart_rx_ready = false; return (uint8_t)k->uart_rx_data;
    default: return k->data[off];
    }
}

static void mk_write(lynx *m, uint8_t off, uint8_t v)
{
    mikey *k = &m->mk;
    if (off < 0x20) {
        mk_timer *t = &k->timers[off >> 2];
        switch (off & 3) {
        case 0: t->bakup = v; break;
        case 1:
            t->cntrl1 = v;
            if (v & 0x40) t->cntrl2 &= (uint8_t)~0x08;
            if (T_COUNT(t) && t->counter == 0) t->counter = t->bakup;
            t->accum = 0;
            break;
        case 2: t->counter = v; break;
        default: t->cntrl2 = (uint8_t)((t->cntrl2 & ~0x08) | (v & 0x08)); break;
        }
        return;
    }
    if (off < 0x40) {
        mk_audio *a = &k->audio[(off - 0x20) >> 3];
        switch (off & 7) {
        case 0: a->volume = (int8_t)v; break;
        case 1: a->feedback = v; break;
        case 2: a->output = (int8_t)v; break;
        case 3: a->shifter = (uint16_t)((a->shifter & 0x0f00) | v); break;
        case 4: a->bakup = v; break;
        case 5:
            a->control = v;
            if (v & 0x40) a->other &= (uint8_t)~0x08;
            a->accum = 0;
            break;
        case 6: a->counter = v; break;
        default:
            a->other = v;
            a->shifter = (uint16_t)((a->shifter & 0x00ff) | ((uint16_t)(v & 0x0f) << 8));
            break;
        }
        k->data[off] = v;
        return;
    }
    switch (off) {
    case 0x80:
        k->interrupt &= (uint8_t)~v;
        mk_update_irq(m);
        uart_update_irq(m);
        break;
    case 0x81:
        k->interrupt |= v;
        if (v) mk_wake(m);
        mk_update_irq(m);
        break;
    case 0x87:
        k->data[off] = v;
        cart_strobe(m, v, k->data[0x8b]);
        break;
    case 0x8c:
        k->data[off] = v;
        k->uart_tx_irq_en = v & 0x80;
        k->uart_rx_irq_en = v & 0x40;
        k->uart_parity_en = v & 0x10;
        k->uart_send_break = v & 0x02;
        k->uart_parity_even = v & 0x01;
        if (v & 0x08) k->uart_overrun = k->uart_framing = false;
        if (k->uart_send_break) {
            k->uart_tx_countdown = UART_FRAME_BITS;
            uart_loopback(k, UART_BREAK);
        }
        uart_update_irq(m);
        break;
    case 0x8d:
        k->data[off] = v;
        k->uart_tx_data = v;
        k->uart_tx_countdown = UART_FRAME_BITS;
        uart_loopback(k, v);
        break;
    default: k->data[off] = v; break;
    }
}

static int16_t mk_mix(const mikey *k)
{
    int mix = 0;
    for (int i = 0; i < AUDIOS; i++) mix += k->audio[i].output * 64;
    return (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
}

// ---------------------------------------------------------------------------
// Suzy
// ---------------------------------------------------------------------------
enum { kBackground = 0, kBackgroundNoColl, kBoundaryShadow, kBoundary, kNormalSprite, kNoColl, kXorSprite, kShadow };
enum {
    kTiltAcumL = 0x02, kHoffL = 0x04, kVoffL = 0x06, kVidBasL = 0x08, kCollBasL = 0x0a, kScbNextL = 0x10,
    kSprDLineL = 0x12, kHPosStrtL = 0x14, kVPosStrtL = 0x16, kSprHSizL = 0x18, kSprVSizL = 0x1a, kStretchL = 0x1c,
    kTiltL = 0x1e, kCollOffL = 0x24, kVSizAcumL = 0x26, kHSizOffL = 0x28, kVSizOffL = 0x2a, kScbAdrL = 0x2c,
    kMathD = 0x52, kMathC = 0x53, kMathB = 0x54, kMathA = 0x55, kMathP = 0x56, kMathN = 0x57,
    kMathH = 0x60, kMathG = 0x61, kMathF = 0x62, kMathE = 0x63, kMathM = 0x6c, kMathL = 0x6d, kMathK = 0x6e, kMathJ = 0x6f,
    kSprCtl0 = 0x80, kSprCtl1 = 0x81, kSprColl = 0x82, kSuzyHRev = 0x88, kSuzyBusEn = 0x90, kSprGo = 0x91,
    kSprSys = 0x92, kJoystick = 0xb0, kSwitches = 0xb1, kRCart0 = 0xb2, kRCart1 = 0xb3,
};
static const uint8_t kColorMasks[4] = { 0x01, 0x03, 0x07, 0x0f };
static const uint8_t kColorCounts[4] = { 2, 4, 8, 16 };

static void sz_reset(suzy *s)
{
    memset(s, 0, sizeof *s);
    s->data[kSuzyHRev] = 0x01;
    s->sign_ab = s->sign_cd = 1;
    s->width = s->height = 0x100;
    s->width_offset = s->height_offset = 0x80;
}

#define RAM(a) (m->ram[(uint16_t)(a)])
static inline void ram_write(lynx *m, uint16_t a, uint8_t v) { if ((a & 0xfffe) != 0xfff8) m->ram[a] = v; }
static inline uint16_t ram_word(lynx *m, uint16_t a) { return (uint16_t)(RAM(a) | (RAM(a + 1) << 8)); }
static inline void write_nibble(lynx *m, uint16_t a, uint8_t nib, bool high)
{
    uint8_t v = RAM(a);
    v = high ? (uint8_t)((v & 0x0f) | (nib << 4)) : (uint8_t)((v & 0xf0) | (nib & 0x0f));
    ram_write(m, a, v);
}
static inline uint8_t read_nibble(lynx *m, uint16_t a, bool high) { return high ? (uint8_t)(RAM(a) >> 4) : (uint8_t)(RAM(a) & 0x0f); }

static void multiply(suzy *s)
{
    const uint16_t l = (uint16_t)(s->data[kMathB] | (s->data[kMathA] << 8)), r = (uint16_t)(s->data[kMathD] | (s->data[kMathC] << 8));
    uint32_t result = (uint32_t)l * r;
    s->accumulate_overflow = false;
    if (s->signed_math && s->sign_ab + s->sign_cd == 0) result = ~result + 1;
    s->data[kMathH] = (uint8_t)result;
    s->data[kMathG] = (uint8_t)(result >> 8);
    s->data[kMathF] = (uint8_t)(result >> 16);
    s->data[kMathE] = (uint8_t)(result >> 24);
    if (s->accumulate) {
        const uint32_t acc = (uint32_t)(s->data[kMathM] | (s->data[kMathL] << 8) | (s->data[kMathK] << 16) | ((uint32_t)s->data[kMathJ] << 24));
        const uint32_t sum = acc + result;
        if (sum < result) s->accumulate_overflow = true;
        s->data[kMathM] = (uint8_t)sum;
        s->data[kMathL] = (uint8_t)(sum >> 8);
        s->data[kMathK] = (uint8_t)(sum >> 16);
        s->data[kMathJ] = (uint8_t)(sum >> 24);
    }
}

static void divide(suzy *s)
{
    const uint32_t l = (uint32_t)(s->data[kMathH] | (s->data[kMathG] << 8) | (s->data[kMathF] << 16) | ((uint32_t)s->data[kMathE] << 24));
    const uint16_t r = (uint16_t)(s->data[kMathP] | (s->data[kMathN] << 8));
    s->accumulate_overflow = false;
    uint32_t result = 0xffffffff, mod = 0;
    if (r == 0) s->accumulate_overflow = true;
    else { result = l / r; mod = l % r; }
    s->data[kMathD] = (uint8_t)result;
    s->data[kMathC] = (uint8_t)(result >> 8);
    s->data[kMathB] = (uint8_t)(result >> 16);
    s->data[kMathA] = (uint8_t)(result >> 24);
    s->data[kMathM] = (uint8_t)mod;
    s->data[kMathL] = (uint8_t)(mod >> 8);
    s->data[kMathK] = s->data[kMathJ] = 0;
}

static void plot(lynx *m, int16_t x, int16_t y, uint8_t color)
{
    suzy *s = &m->sz;
    s->everon = true;
    const uint16_t pixel = (uint16_t)(s->screen + y * 80 + x / 2), coll = (uint16_t)(s->colbuf + y * 80 + x / 2);
    const bool high = (x & 1) == 0, collide = s->sprite_collide && !s->no_collide;
#define COLLIDE_NIBBLE(allow)                                       \
    do {                                                            \
        if (collide && (allow)) {                                   \
            const uint8_t back = read_nibble(m, coll, high);        \
            if (back > s->fred) s->fred = back;                     \
            write_nibble(m, coll, s->spritenr, high);               \
            s->memory_accesses += 2;                                \
        }                                                           \
    } while (0)
    switch (s->mode & 7) {
    case kNormalSprite:
        if (color == 0) break;
        write_nibble(m, pixel, color, high);
        s->memory_accesses++;
        COLLIDE_NIBBLE(true);
        break;
    case kBoundary:
        if (color == 0) break;
        if (color != 0x0f) { write_nibble(m, pixel, color, high); s->memory_accesses++; }
        COLLIDE_NIBBLE(true);
        break;
    case kShadow:
        if (color == 0) break;
        write_nibble(m, pixel, color, high);
        s->memory_accesses++;
        COLLIDE_NIBBLE(color != 0x0e);
        break;
    case kBoundaryShadow:
        if (color == 0) break;
        if (color != 0x0f) { write_nibble(m, pixel, color, high); s->memory_accesses++; }
        COLLIDE_NIBBLE(color != 0x0e);
        break;
    case kBackground:
        write_nibble(m, pixel, color, high);
        s->memory_accesses++;
        if (collide && color != 0x0e) { write_nibble(m, coll, s->spritenr, high); s->memory_accesses++; }
        break;
    case kBackgroundNoColl:
        write_nibble(m, pixel, color, high);
        s->memory_accesses++;
        break;
    case kNoColl:
        if (color == 0) break;
        write_nibble(m, pixel, color, high);
        s->memory_accesses++;
        break;
    default: {      // kXorSprite
        if (color == 0) break;
        uint8_t v = RAM(pixel);
        v = high ? (uint8_t)(v ^ (color << 4)) : (uint8_t)(v ^ color);
        ram_write(m, pixel, v);
        s->memory_accesses += 2;
        COLLIDE_NIBBLE(color != 0x0e);
        break;
    }
    }
#undef COLLIDE_NIBBLE
}

static void put_run(lynx *m, int16_t *xi, int xdir, int16_t y, uint8_t color, uint16_t *width_acc)
{
    suzy *s = &m->sz;
    *width_acc = (uint16_t)(*width_acc + s->width);
    for (int i = 0; i < (*width_acc >> 8); i++, *xi = (int16_t)(*xi + xdir))
        if (*xi >= 0 && *xi < LYNX_W) plot(m, *xi, y, color);
    *width_acc &= 0xff;
}

static void blit_packed(lynx *m, int16_t y, int xdir, int bpp, uint8_t mask)
{
    suzy *s = &m->sz;
    const int next_line = RAM(s->bitmap);
    s->memory_accesses++;
    uint16_t width_acc = xdir == 1 ? s->width_offset : 0;
    int bits = 0;
    uint16_t buffer = 0;
    int16_t xi = (int16_t)(s->x_pos - s->xoff);
    for (int j = 1; j < next_line; j++) {
        buffer = (uint16_t)((buffer << 8) | RAM(s->bitmap + j));
        bits += 8;
        s->memory_accesses++;
        while (bits > bpp) {
            const uint8_t color = s->color[(buffer >> (bits - bpp)) & mask];
            bits -= bpp;
            put_run(m, &xi, xdir, y, color, &width_acc);
        }
    }
}

static void blit_rle(lynx *m, int16_t y, int xdir, int bpp, uint8_t mask)
{
    suzy *s = &m->sz;
    const int next_line = RAM(s->bitmap);
    uint16_t width_acc = xdir == 1 ? s->width_offset : 0;
    int bits = 0, j = 0;
    uint32_t buffer = 0;
    int16_t xi = (int16_t)(s->x_pos - s->xoff);
#define NEED(n)                                                     \
    if (bits < (n)) {                                               \
        if (++j >= next_line) return;                               \
        bits += 8;                                                  \
        buffer = (buffer << 8) | RAM(s->bitmap + j);                \
        s->memory_accesses++;                                       \
    }
    for (;;) {
        NEED(5 + bpp)
        const bool literal = (buffer >> (bits - 1)) & 1;
        bits -= 1;
        int count = (int)((buffer >> (bits - 4)) & 0x0f);
        bits -= 4;
        if (literal) {
            for (; count >= 0; count--) {
                NEED(bpp)
                const uint8_t color = s->color[(buffer >> (bits - bpp)) & mask];
                bits -= bpp;
                put_run(m, &xi, xdir, y, color, &width_acc);
            }
        } else {
            if (count == 0) return;
            NEED(bpp)
            const uint8_t color = s->color[(buffer >> (bits - bpp)) & mask];
            bits -= bpp;
            for (; count >= 0; count--) put_run(m, &xi, xdir, y, color, &width_acc);
        }
    }
#undef NEED
}

static void blit_lines(lynx *m)
{
    suzy *s = &m->sz;
    s->everon = false;
    int xdir = 1, ydir = 1, flip = 0;
    switch (s->spr_ctl1 & 3) {
    case 0: xdir = 1; ydir = 1; flip = 0; break;
    case 1: xdir = -1; ydir = 1; flip = 3; break;
    case 2: xdir = 1; ydir = -1; flip = 1; break;
    default: xdir = -1; ydir = -1; flip = 2; break;
    }
    if (s->spr_ctl0 & 0x20) xdir = -xdir;          // HFLIP
    if (s->spr_ctl0 & 0x10) ydir = -ydir;          // VFLIP
    s->height_acc = ydir == 1 ? s->height_offset : 0;
    for (int16_t y = (int16_t)(s->y_pos - s->yoff);;) {
        const int offset = RAM(s->bitmap);
        s->memory_accesses++;
        if (offset == 0) break;
        if (offset == 1) {
            switch (flip & 3) {
            case 0: case 2: ydir = -ydir; s->y_pos = (int16_t)(s->y_pos + ydir); break;
            default: xdir = -xdir; s->x_pos = (int16_t)(s->x_pos + xdir); break;
            }
            flip++;
            y = (int16_t)(s->y_pos - s->yoff);
            s->height_acc = ydir == 1 ? s->height_offset : 0;
            s->bitmap = (uint16_t)(s->bitmap + offset);
            continue;
        }
        s->height_acc = (uint16_t)(s->height_acc + s->height);
        for (int j = 0; j < (s->height_acc >> 8); j++, y = (int16_t)(y + ydir)) {
            if (y >= 0 && y < LYNX_H) {
                if (s->use_rle) blit_rle(m, y, xdir, s->line_color + 1, kColorMasks[s->line_color]);
                else blit_packed(m, y, xdir, s->line_color + 1, kColorMasks[s->line_color]);
            }
            s->width = (uint16_t)((int16_t)s->width + s->stretch);
            if (s->vstretch) s->height = (uint16_t)((int16_t)s->height + s->stretch);
            s->tilt_acc = (int16_t)(s->tilt_acc + s->tilt);
            s->x_pos = (int16_t)(s->x_pos + (s->tilt_acc >> 8));
            s->tilt_acc = (int16_t)(s->tilt_acc & 0xff);
        }
        s->height_acc &= 0xff;
        s->bitmap = (uint16_t)(s->bitmap + offset);
    }
}

static void blitter(lynx *m)
{
    suzy *s = &m->sz;
    s->busy = true;
    s->memory_accesses = 0;
    while (s->scb_next & 0xff00) {
        s->stretch = s->tilt = s->tilt_acc = 0;
        s->scb = s->scb_next;
        s->scb_next = ram_word(m, (uint16_t)(s->scb + 3));
        s->spr_ctl0 = RAM(s->scb);
        s->spr_ctl1 = RAM(s->scb + 1);
        s->spr_coll = RAM(s->scb + 2);
        s->memory_accesses += 5;
        const bool skip = (s->spr_ctl1 & 0x04) != 0;
        if (skip) continue;
        s->bitmap = ram_word(m, (uint16_t)(s->scb + 5));
        s->x_pos = (int16_t)ram_word(m, (uint16_t)(s->scb + 7));
        s->y_pos = (int16_t)ram_word(m, (uint16_t)(s->scb + 9));
        s->memory_accesses += 6;
        const int reload = (s->spr_ctl1 >> 4) & 3;
        switch (reload) {
        case 3:
            s->tilt = (int16_t)ram_word(m, (uint16_t)(s->scb + 0x11));
            s->memory_accesses += 2;
            /* fall through */
        case 2:
            s->stretch = (int16_t)ram_word(m, (uint16_t)(s->scb + 0x0f));
            s->memory_accesses += 2;
            /* fall through */
        case 1:
            s->width = ram_word(m, (uint16_t)(s->scb + 0x0b));
            s->height = ram_word(m, (uint16_t)(s->scb + 0x0d));
            s->memory_accesses += 4;
            break;
        default: break;
        }
        if ((s->spr_ctl1 & 0x08) == 0) {        // a new palette
            const uint8_t po = reload ? (uint8_t)(0x0b + 2 * (reload + 1)) : 0x0b;
            const uint8_t colors = kColorCounts[(s->spr_ctl0 >> 6) & 3];
            for (int i = 0; i < colors / 2; i++) {
                const uint8_t packed = RAM(s->scb + po + i);
                s->color[i * 2] = packed >> 4;
                s->color[i * 2 + 1] = packed & 0x0f;
                s->memory_accesses++;
            }
        }
        const uint16_t coll_off = (uint16_t)(s->data[kCollOffL] | (s->data[kCollOffL + 1] << 8));
        s->colpos = (uint16_t)(s->scb + coll_off);
        s->mode = s->spr_ctl0 & 7;
        s->use_rle = (s->spr_ctl1 & 0x80) == 0;
        s->line_color = (s->spr_ctl0 >> 6) & 3;
        s->sprite_collide = (s->spr_coll & 0x20) == 0;
        s->spritenr = s->spr_coll & 0x0f;
        s->fred = 0;
        blit_lines(m);
        if (s->sprite_collide && !s->no_collide) {
            switch (s->mode) {
            case kBoundaryShadow: case kBoundary: case kNormalSprite: case kXorSprite: case kShadow:
                ram_write(m, s->colpos, s->fred);
                break;
            default: break;
            }
        }
        if (s->data[kSprGo] & 0x04) {
            uint8_t v = RAM(s->colpos);
            v = s->everon ? (uint8_t)(v & 0x7f) : (uint8_t)(v | 0x80);
            ram_write(m, s->colpos, v);
        }
    }
    s->remaining = s->memory_accesses > 1 ? s->memory_accesses : 1;
}

static void sz_tick(suzy *s, int cycles)
{
    if (!s->busy) return;
    s->remaining -= cycles;
    if (s->remaining <= 0) {
        s->remaining = 0;
        s->busy = false;
    }
}

static uint8_t cart_read(lynx *m);

static uint8_t sz_read(lynx *m, uint8_t off)
{
    suzy *s = &m->sz;
#define W16(v) case_lo: return (uint8_t)(v); case_hi: return (uint8_t)((uint16_t)(v) >> 8)
    switch (off) {
    case kTiltAcumL: return (uint8_t)s->tilt_acc;
    case kTiltAcumL + 1: return (uint8_t)((uint16_t)s->tilt_acc >> 8);
    case kHoffL: return (uint8_t)s->xoff;
    case kHoffL + 1: return (uint8_t)((uint16_t)s->xoff >> 8);
    case kVoffL: return (uint8_t)s->yoff;
    case kVoffL + 1: return (uint8_t)((uint16_t)s->yoff >> 8);
    case kVidBasL: return (uint8_t)s->screen;
    case kVidBasL + 1: return (uint8_t)(s->screen >> 8);
    case kCollBasL: return (uint8_t)s->colbuf;
    case kCollBasL + 1: return (uint8_t)(s->colbuf >> 8);
    case kScbNextL: return (uint8_t)s->scb_next;
    case kScbNextL + 1: return (uint8_t)(s->scb_next >> 8);
    case kSprDLineL: return (uint8_t)s->bitmap;
    case kSprDLineL + 1: return (uint8_t)(s->bitmap >> 8);
    case kHPosStrtL: return (uint8_t)s->x_pos;
    case kHPosStrtL + 1: return (uint8_t)((uint16_t)s->x_pos >> 8);
    case kVPosStrtL: return (uint8_t)s->y_pos;
    case kVPosStrtL + 1: return (uint8_t)((uint16_t)s->y_pos >> 8);
    case kSprHSizL: return (uint8_t)s->width;
    case kSprHSizL + 1: return (uint8_t)(s->width >> 8);
    case kSprVSizL: return (uint8_t)s->height;
    case kSprVSizL + 1: return (uint8_t)(s->height >> 8);
    case kStretchL: return (uint8_t)s->stretch;
    case kStretchL + 1: return (uint8_t)((uint16_t)s->stretch >> 8);
    case kTiltL: return (uint8_t)s->tilt;
    case kTiltL + 1: return (uint8_t)((uint16_t)s->tilt >> 8);
    case kVSizAcumL: return (uint8_t)s->height_acc;
    case kVSizAcumL + 1: return (uint8_t)(s->height_acc >> 8);
    case kHSizOffL: return (uint8_t)s->width_offset;
    case kHSizOffL + 1: return (uint8_t)(s->width_offset >> 8);
    case kVSizOffL: return (uint8_t)s->height_offset;
    case kVSizOffL + 1: return (uint8_t)(s->height_offset >> 8);
    case kScbAdrL: return (uint8_t)s->scb;
    case kScbAdrL + 1: return (uint8_t)(s->scb >> 8);
    case kSuzyHRev: return 0x01;
    case kSprSys: {
        uint8_t v = 0;
        if (s->accumulate_overflow) v |= 0x40;
        if (s->vstretch) v |= 0x10;
        if (s->lefthanded) v |= 0x08;
        if (s->busy) v |= 0x01;
        return v;
    }
    case kJoystick: {
        const uint8_t in = s->joystick;
        if (s->lefthanded) {
            uint8_t v = in & 0x0f;
            if (in & 0x80) v |= 0x40;
            if (in & 0x40) v |= 0x80;
            if (in & 0x20) v |= 0x10;
            if (in & 0x10) v |= 0x20;
            return v;
        }
        return in;
    }
    case kSwitches: return s->switches;
    case kRCart0: return cart_read(m);
    case kRCart1: return 0;
    case kSprCtl0: case kSprCtl1: case kSprColl: case kSprGo: case kSuzyBusEn: return 0;
    default: return s->data[off];
    }
#undef W16
}

static void set_hi(uint16_t *w, uint8_t v) { *w = (uint16_t)((*w & 0x00ff) | ((uint16_t)v << 8)); }
static void set_shi(int16_t *w, uint8_t v) { *w = (int16_t)(((uint16_t)*w & 0x00ff) | ((uint16_t)v << 8)); }

static void sz_write(lynx *m, uint8_t off, uint8_t v)
{
    suzy *s = &m->sz;
    s->data[off] = v;
    if (off < 0x80 && (off & 1) == 0) s->data[off + 1] = 0;
    switch (off) {
    case kTiltAcumL: s->tilt_acc = (int16_t)v; break;
    case kTiltAcumL + 1: set_shi(&s->tilt_acc, v); break;
    case kHoffL: s->xoff = (int16_t)v; break;
    case kHoffL + 1: set_shi(&s->xoff, v); break;
    case kVoffL: s->yoff = (int16_t)v; break;
    case kVoffL + 1: set_shi(&s->yoff, v); break;
    case kVidBasL: s->screen = v; break;
    case kVidBasL + 1: set_hi(&s->screen, v); break;
    case kCollBasL: s->colbuf = v; break;
    case kCollBasL + 1: set_hi(&s->colbuf, v); break;
    case kScbNextL: s->scb_next = v; break;
    case kScbNextL + 1: set_hi(&s->scb_next, v); break;
    case kSprDLineL: s->bitmap = v; break;
    case kSprDLineL + 1: set_hi(&s->bitmap, v); break;
    case kHPosStrtL: s->x_pos = (int16_t)v; break;
    case kHPosStrtL + 1: set_shi(&s->x_pos, v); break;
    case kVPosStrtL: s->y_pos = (int16_t)v; break;
    case kVPosStrtL + 1: set_shi(&s->y_pos, v); break;
    case kSprHSizL: s->width = v; break;
    case kSprHSizL + 1: set_hi(&s->width, v); break;
    case kSprVSizL: s->height = v; break;
    case kSprVSizL + 1: set_hi(&s->height, v); break;
    case kStretchL: s->stretch = (int16_t)v; break;
    case kStretchL + 1: set_shi(&s->stretch, v); break;
    case kTiltL: s->tilt = (int16_t)v; break;
    case kTiltL + 1: set_shi(&s->tilt, v); break;
    case kVSizAcumL: s->height_acc = v; break;
    case kVSizAcumL + 1: set_hi(&s->height_acc, v); break;
    case kHSizOffL: s->width_offset = v; break;
    case kHSizOffL + 1: set_hi(&s->width_offset, v); break;
    case kVSizOffL: s->height_offset = v; break;
    case kVSizOffL + 1: set_hi(&s->height_offset, v); break;
    case kScbAdrL: s->scb = v; break;
    case kScbAdrL + 1: set_hi(&s->scb, v); break;
    case kMathM: s->accumulate_overflow = false; break;
    case kMathC:
        if (s->signed_math) {
            const uint16_t f = (uint16_t)(s->data[kMathD] | (s->data[kMathC] << 8));
            if ((uint16_t)(f - 1) & 0x8000) {
                const uint16_t t = (uint16_t)((f ^ 0xffff) + 1);
                s->sign_cd = -1;
                s->data[kMathD] = (uint8_t)t;
                s->data[kMathC] = (uint8_t)(t >> 8);
            } else {
                s->sign_cd = 1;
            }
        }
        break;
    case kMathD: if (v) s->sign_cd = 1; break;
    case kMathA:
        if (s->signed_math) {
            const uint16_t f = (uint16_t)(s->data[kMathB] | (s->data[kMathA] << 8));
            if ((uint16_t)(f - 1) & 0x8000) {
                const uint16_t t = (uint16_t)((f ^ 0xffff) + 1);
                s->sign_ab = -1;
                s->data[kMathB] = (uint8_t)t;
                s->data[kMathA] = (uint8_t)(t >> 8);
            } else {
                s->sign_ab = 1;
            }
        }
        multiply(s);
        break;
    case kMathE: divide(s); break;
    case kSprCtl0: s->spr_ctl0 = v; break;
    case kSprCtl1: s->spr_ctl1 = v; break;
    case kSprColl: s->spr_coll = v; break;
    case kSprSys:
        s->signed_math = v & 0x80;
        s->accumulate = v & 0x40;
        s->no_collide = v & 0x20;
        s->vstretch = v & 0x10;
        s->lefthanded = v & 0x08;
        break;
    case kSprGo: if ((v & 0x01) && s->data[kSuzyBusEn]) blitter(m); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// the machine
// ---------------------------------------------------------------------------
// dsp-cpp's own 512-byte loader when there is no lynxboot.img: copies the
// first cartridge page to $0200 and jumps there (not Atari's ROM)
static void fallback_boot(uint8_t *rom)
{
    static const uint8_t code[] = {
        0xa2, 0xff, 0x9a, 0xd8, 0x78, 0xa9, 0x02, 0x8d, 0x8a, 0xfd, 0xa9, 0x00, 0x8d, 0x8b, 0xfd,
        0xa9, 0x02, 0x8d, 0x87, 0xfd, 0xa2, 0x08, 0xa9, 0x03, 0x8d, 0x87, 0xfd, 0xa9, 0x02, 0x8d,
        0x87, 0xfd, 0xca, 0xd0, 0xf3, 0xa0, 0x00, 0xad, 0xb2, 0xfc, 0x99, 0x00, 0x02, 0xc8, 0xd0,
        0xf7, 0x4c, 0x00, 0x02, 0x40,
    };
    memset(rom, 0, 0x200);
    memcpy(rom, code, sizeof code);
    rom[0x1fa] = 0x31; rom[0x1fb] = 0xfe;   // NMI
    rom[0x1fc] = 0x00; rom[0x1fd] = 0xfe;   // RESET
    rom[0x1fe] = 0x31; rom[0x1ff] = 0xfe;   // IRQ
}

static uint8_t cart_read(lynx *m)
{
    const uint16_t mask = (uint16_t)(m->granularity - 1);
    if (!m->cart_size) {
        m->cart_counter = (uint16_t)((m->cart_counter + 1) & mask);
        return 0xff;
    }
    uint32_t a = (uint32_t)m->cart_block * m->granularity + m->cart_counter;
    if (m->mk.data[0x8b] & 0x10) a += m->audin_offset;
    const uint8_t v = a < m->cart_size ? zx_pb_byte(&m->cart, m->cart_base + a) : 0xff;
    m->cart_counter = (uint16_t)((m->cart_counter + 1) & mask);
    return v;
}

static void cart_strobe(lynx *m, uint8_t sysctl, uint8_t iodat)
{
    if (sysctl & 0x02) {
        if ((sysctl & 0x01) && !(m->last_sysctl & 0x01)) {
            m->cart_block = (uint8_t)(((m->cart_block << 1) & 0xfe) | ((iodat >> 1) & 1));
            m->cart_counter = 0;
        }
    } else {
        m->cart_block = 0;
        m->cart_counter = 0;
    }
    m->last_sysctl = sysctl;
}

static void catch_up(lynx *m);

static uint8_t read_byte(void *user, uint16_t a)
{
    lynx *m = (lynx *)user;
    if (a == 0xfff9) return m->mapctl;
    if (a == 0xfff8) return m->ram[0xfff8];
    const bool rom_on = (m->mapctl & 0x04) == 0, vectors_rom = (m->mapctl & 0x08) == 0;
    if (a >= 0xfe00) {
        if (a >= 0xfffa && vectors_rom && rom_on) return m->boot_rom[a - 0xfe00];
        if (a <= 0xfff7 && rom_on) return m->boot_rom[a - 0xfe00];
    }
    if (a >= 0xfc00 && a <= 0xfcff && !(m->mapctl & 0x01)) {
        catch_up(m);
        return sz_read(m, (uint8_t)a);
    }
    if (a >= 0xfd00 && a <= 0xfdff && !(m->mapctl & 0x02)) {
        catch_up(m);
        mk_sync(m);
        return mk_read(m, (uint8_t)a);
    }
    return m->ram[a];
}

static void write_byte(void *user, uint16_t a, uint8_t v)
{
    lynx *m = (lynx *)user;
    if (a == 0xfff9) { m->mapctl = v; return; }
    if (a >= 0xfc00 && a <= 0xfcff && !(m->mapctl & 0x01)) {
        catch_up(m);
        sz_write(m, (uint8_t)a, v);
        return;
    }
    if (a >= 0xfd00 && a <= 0xfdff && !(m->mapctl & 0x02)) {
        catch_up(m);
        mk_sync(m);
        mk_write(m, (uint8_t)a, v);
        m->mk_next = mk_next_event(m);
        if (a == 0xfd91 && m->sz.busy) m->cpu.halted = true;    // CPUSLEEP while Suzy draws
        return;
    }
    if ((a & 0xfffe) == 0xfff8) {
        if (a == 0xfff8) m->ram[0xfff8] = v;
        return;
    }
    m->ram[a] = v;
}

// Mikey's timers, Suzy and the sound for the cycles the CPU ran since the
// last time: after each slice of CPU (up to the next timer due) and before
// any access to Mikey or Suzy. No call per instruction.
static void catch_up(lynx *m)
{
    const int cycles = (int)(m->cpu.clock - m->clock_mark);
    if (cycles <= 0) return;
    m->clock_mark = m->cpu.clock;
    if (!m->turbo) {                    // (the sound of these cycles: before what is due at their end)
        m->audio_acc32 += (uint32_t)cycles * (uint32_t)m->audio_rate;
        while (m->audio_acc32 >= CPU_CLOCK) {
            m->audio_acc32 -= CPU_CLOCK;
            if (m->naudio < LYNX_AUDIO_MAX) m->audio[m->naudio++] = mk_mix(&m->mk);
        }
    }
    m->mk_pending += cycles;
    if (m->mk_pending >= m->mk_next) mk_sync(m);
    sz_tick(&m->sz, cycles);
    if (m->cpu.halted && !m->sz.busy) m->cpu.halted = false;
}

static void present_frame(lynx *m)
{
    const mikey *k = &m->mk;
    m->shown = (k->data[0x92] & 0x01) != 0;
    m->flip = (k->data[0x92] & 0x02) != 0;
    for (int i = 0; i < 16; i++) {
        const uint32_t g = k->data[0xa0 + i] & 0x0f, r = k->data[0xb0 + i] & 0x0f, b = k->data[0xb0 + i] >> 4;
        const uint32_t rgb = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
        m->pal[i] = rgb;
        m->pal565[i] = m->rgb565 ? m->rgb565((uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17))
                                 : (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x1f));
    }
    if (!m->shown) return;
    const uint16_t addr = (uint16_t)((k->data[0x94] | (k->data[0x95] << 8)) & 0xfffc);
    for (int i = 0; i < LYNX_W / 2 * LYNX_H; i++) m->screen[i] = m->ram[(uint16_t)(addr + i)];
}

lynx *lynx_create(int audio_rate)
{
    lynx *m = (lynx *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->audio_rate = audio_rate > 0 ? audio_rate : LYNX_AUDIO_RATE;
    fallback_boot(m->boot_rom);
    m->cpu.cmos = true;
    m->cpu.read = read_byte;
    m->cpu.write = write_byte;
    m->cpu.cycles = NULL;               // (catch_up: after slices, not instructions)
    m->cpu.user = m;
    for (int p = 0; p < 0xfc; p++) {
        m->cpu.page_r[p] = m->ram + p * 256;
        m->cpu.page_w[p] = m->ram + p * 256;
    }
    m->granularity = 0x400;
    zx_pb_init(&m->cart);
    lynx_reset(m);
    return m;
}

void lynx_destroy(lynx *m)
{
    if (!m) return;
    zx_pb_free(&m->cart);
    free(m);
}

void lynx_set_boot_rom(lynx *m, const uint8_t *rom)
{
    if (rom) {
        memcpy(m->boot_rom, rom, 0x200);
        m->bios_loaded = true;
    } else {
        fallback_boot(m->boot_rom);
        m->bios_loaded = false;
    }
    lynx_reset(m);
}

bool lynx_has_boot_rom(const lynx *m) { return m->bios_loaded; }
void lynx_set_turbo(lynx *m, bool on) { m->turbo = on; }

void lynx_set_rgb565(lynx *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b)) { m->rgb565 = fn; }

void lynx_reset(lynx *m)
{
    memset(m->ram, 0, sizeof m->ram);
    sz_reset(&m->sz);
    mk_reset(&m->mk);
    mk_update_irq(m);
    m->mk_pending = 0;
    m->mk_next = 1;
    m->mapctl = 0;
    m->cart_block = 0;
    m->cart_counter = 0;
    m->last_sysctl = 0;
    m->audio_acc = 0;
    m->audio_acc32 = 0;
    m->clock_mark = m->cpu.clock;
    m->frame_debt = 0;
    m->naudio = 0;
    memset(m->screen, 0, sizeof m->screen);
    m->shown = false;
    m->cpu.halted = false;
    m65xx_reset(&m->cpu);
}

int lynx_run_frame(lynx *m, const int16_t **audio)
{
    m->naudio = 0;
    // the CPU in slices that end where a timer is due (or Suzy is done)
    const uint32_t end = m->clock_mark + SCANLINES * CYCLES_PER_LINE - m->frame_debt;
    while ((int32_t)(end - m->cpu.clock) > 0) {
        int slice = (int)(end - m->cpu.clock);
        const int due = m->mk_next - m->mk_pending;
        if (due < slice) slice = due;
        if (m->sz.busy && m->sz.remaining < slice) slice = m->sz.remaining;
        if (slice < 1) slice = 1;
        m65xx_run(&m->cpu, slice);
        catch_up(m);
    }
    m->frame_debt = (int)(m->cpu.clock - end);      // (the last instruction ran past the frame)
    present_frame(m);
    if (audio) *audio = m->audio;
    return m->naudio;
}

void lynx_buttons(lynx *m, unsigned bits)
{
    m->sz.joystick = (uint8_t)bits;
    m->sz.switches = (bits & LYNX_PAUSE) ? 0x01 : 0x00;
}

#define RENDER(NAME, T, PAL)                                                            \
void NAME(const lynx *m, int y, T *out)                                                 \
{                                                                                       \
    if (!m->shown || y < 0 || y >= LYNX_H) {                                            \
        for (int x = 0; x < LYNX_W; x++) out[x] = 0;                                    \
        return;                                                                         \
    }                                                                                   \
    if (m->flip) {                                                                      \
        const uint8_t *src = m->screen + (LYNX_H - 1 - y) * (LYNX_W / 2);               \
        for (int x = LYNX_W - 2, i = 0; x >= 0; i++, x -= 2) {                          \
            out[x + 1] = m->PAL[src[i] >> 4];                                           \
            out[x] = m->PAL[src[i] & 0x0f];                                             \
        }                                                                               \
    } else {                                                                            \
        const uint8_t *src = m->screen + y * (LYNX_W / 2);                              \
        for (int x = 0, i = 0; x < LYNX_W; i++, x += 2) {                               \
            out[x] = m->PAL[src[i] >> 4];                                               \
            out[x + 1] = m->PAL[src[i] & 0x0f];                                         \
        }                                                                               \
    }                                                                                   \
}
RENDER(lynx_render_row, uint32_t, pal)
RENDER(lynx_render_row565, uint16_t, pal565)

// ---------------------------------------------------------------------------
// cartridges
// ---------------------------------------------------------------------------
static uint16_t guess_granularity(size_t size)
{
    if (size == 0x20000) return 0x200;
    if (size == 0x80000 || size == 0x100000) return 0x800;
    return 0x400;
}

bool lynx_load_pbuf(lynx *m, zx_pbuf *pb, const char *name)
{
    (void)name;
    m->error = NULL;
    if (!pb->size) {
        m->error = "empty Lynx cartridge";
        zx_pb_free(pb);
        return false;
    }
    if (pb->size == 0x200 && zx_pb_rd16(pb, 0x1fc) == 0xff80) {       // a boot ROM
        zx_pb_copy(pb, 0, m->boot_rom, 0x200);
        m->bios_loaded = true;
        zx_pb_free(pb);
        lynx_reset(m);
        return true;
    }
    if (pb->size >= 10 && zx_pb_eq(pb, 6, "BS93", 4)) {                // BLL / Handy quickload
        const uint16_t start = (uint16_t)(zx_pb_byte(pb, 3) | (zx_pb_byte(pb, 2) << 8));
        uint16_t length = (uint16_t)(zx_pb_byte(pb, 5) | (zx_pb_byte(pb, 4) << 8));
        if (length >= 10) length -= 10;
        memset(m->ram, 0, sizeof m->ram);
        sz_reset(&m->sz);
        mk_reset(&m->mk);
        m->mk_pending = 0;
        m->mk_next = 1;
        m->mapctl = 0x0c;
        const size_t copy = length < pb->size - 10 ? length : pb->size - 10;
        for (size_t i = 0; i < copy; i++) m->ram[(uint16_t)(start + i)] = zx_pb_byte(pb, (uint32_t)(10 + i));
        m->ram[0xfffc] = (uint8_t)start;
        m->ram[0xfffd] = (uint8_t)(start >> 8);
        zx_pb_free(&m->cart);
        m->cart_size = 0;
        zx_pb_free(pb);
        m65xx_reset(&m->cpu);
        return true;
    }
    m->granularity = 0x400;
    m->audin_offset = 0;
    m->cart_base = 0;
    if (pb->size >= 64 && zx_pb_eq(pb, 0, "LYNX", 4)) {
        uint16_t g = zx_pb_rd16(pb, 4);
        if (g != 256 && g != 512 && g != 1024 && g != 2048) g = 0x400;
        m->granularity = g;
        if (zx_pb_rd16(pb, 6) != 0) m->audin_offset = 256u * g;
        m->cart_base = 64;
    } else {
        m->granularity = guess_granularity(pb->size);
    }
    zx_pb_free(&m->cart);
    m->cart = *pb;
    zx_pb_init(pb);
    m->cart_size = m->cart.size - m->cart_base;
    lynx_reset(m);
    return true;
}

bool lynx_load_file(lynx *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { m->error = "cannot open the file"; return false; }
    zx_pbuf pb;
    zx_pb_init(&pb);
    uint8_t buf[4096];
    size_t k;
    bool ok = true;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0)
        if (!(ok = zx_pb_append(&pb, buf, k))) break;
    fclose(f);
    if (!ok || !zx_pb_finish(&pb) || pb.size == 0) {
        zx_pb_free(&pb);
        m->error = ok ? "empty file" : "out of memory";
        return false;
    }
    return lynx_load_pbuf(m, &pb, path);
}

const char *lynx_last_error(const lynx *m) { return m->error ? m->error : ""; }
