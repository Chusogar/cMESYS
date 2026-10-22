// pokey.c - Atari POKEY. C port of dsp-cpp's sound/pokey.cpp.
#include "pokey.h"

#include <stdlib.h>
#include <string.h>

#ifndef POKEY_HOT
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define POKEY_HOT IRAM_ATTR   /* hot code out of the flash cache */
#else
#define POKEY_HOT
#endif
#endif

#define AUDCTL 0x08
#define STIMER 0x09
#define SKREST 0x0a
#define POTGO 0x0b
#define SEROUT 0x0d
#define IRQEN 0x0e
#define SKCTL 0x0f
#define ALLPOT 0x08
#define KBCODE 0x09
#define RANDOM 0x0a
#define SERIN 0x0d
#define IRQST 0x0e
#define SKSTAT 0x0f

#define NOT_POLY5 0x80
#define POLY4 0x40
#define PURE 0x20
#define VOLUME_ONLY 0x10
#define VOLUME_MASK 0x0f
#define POLY9 0x80
#define CH1_HICLK 0x40
#define CH3_HICLK 0x20
#define CH12_JOINED 0x10
#define CH34_JOINED 0x08
#define CH1_FILTER 0x04
#define CH2_FILTER 0x02
#define CLK_15KHZ 0x01
#define IRQ_TIMER4 0x04
#define IRQ_TIMER2 0x02
#define IRQ_TIMER1 0x01
#define SK_FRAME 0x80
#define SK_KBERR 0x40
#define SK_OVERRUN 0x20
#define SK_SEROUT 0x02
#define SK_PADDLE 0x04
#define SK_RESET 0x03

#define CLK1 0
#define CLK28 1
#define CLK114 2
static const int clock_divisors[3] = {1, 28, 114};
#define DEFAULT_GAIN (32767 / 11 / 4)
#define CHAN1 0
#define CHAN2 1
#define CHAN3 2
#define CHAN4 3

#define POLY17_LENGTH 0x1ffff

// Bit 16 of each state of the 17-bit LFSR (16 KB, in flash on the ESP32).
static const uint32_t poly17_bit16[(POLY17_LENGTH + 31) / 32] = {
#include "pokey_poly17.inc"
};

static inline uint32_t bit16_at(int i)
{
    if (i < 0) i += POLY17_LENGTH;
    return (poly17_bit16[(unsigned)i >> 5] >> (i & 31)) & 1;
}

static uint16_t poly4_tab[15], poly5_tab[31], poly9_tab[511];
static bool polys_made;

static void make_polys(void)
{
    if (polys_made) return;
    polys_made = true;
    uint32_t lfsr = 0;
    for (int i = 0; i < 15; ++i) {
        uint32_t in = (~(lfsr >> 0) & 1) ^ ((lfsr >> 1) & 1);
        lfsr >>= 1;
        lfsr = (in << 3) | lfsr;
        poly4_tab[i] = (uint16_t)lfsr;
    }
    lfsr = 0;
    for (int i = 0; i < 31; ++i) {
        uint32_t in = (~(lfsr >> 0) & 1) ^ ((lfsr >> 2) & 1);
        lfsr >>= 1;
        lfsr = (in << 4) | lfsr;
        poly5_tab[i] = (uint16_t)(lfsr ^ 1);
    }
    lfsr = 0x1ff;
    for (int i = 0; i < 511; ++i) {
        uint32_t in = ((lfsr >> 0) & 1) ^ ((lfsr >> 5) & 1);
        lfsr >>= 1;
        lfsr = (in << 8) | lfsr;
        poly9_tab[i] = (uint16_t)lfsr;
    }
}

static void reset_polys(pokey *p)
{
    p->i4 = p->i5 = p->i9 = p->i17 = 0;
    p->at4 = p->at5 = p->at9 = p->at17 = p->ticks;
}

static uint32_t poly4(pokey *p)
{
    uint32_t lag = p->ticks - p->at4;
    p->at4 = p->ticks;
    p->i4 += lag;
    if (p->i4 >= 15u) p->i4 %= 15u;
    return poly4_tab[p->i4];
}

static uint32_t poly5(pokey *p)
{
    uint32_t lag = p->ticks - p->at5;
    p->at5 = p->ticks;
    p->i5 += lag;
    if (p->i5 >= 31u) p->i5 %= 31u;
    return poly5_tab[p->i5];
}

static uint32_t poly9(pokey *p)
{
    uint32_t lag = p->ticks - p->at9;
    p->at9 = p->ticks;
    p->i9 += lag;
    if (p->i9 >= 511u) p->i9 %= 511u;
    return poly9_tab[p->i9];
}

static void sync_poly17(pokey *p)
{
    uint32_t lag = p->ticks - p->at17;
    p->at17 = p->ticks;
    p->i17 += lag;
    if (p->i17 >= (uint32_t)POLY17_LENGTH) p->i17 %= (uint32_t)POLY17_LENGTH;
}

static uint32_t poly17_bit0(pokey *p)
{
    sync_poly17(p);
    int i = (int)p->i17;
    return bit16_at(i - 16) ^ bit16_at(i - 11);
}

static uint32_t poly17_byte(pokey *p)
{
    sync_poly17(p);
    uint32_t v = 0;
    for (int k = 0; k < 8; ++k) v |= bit16_at((int)p->i17 - 8 + k) << k;
    return v;
}

void pokey_init(pokey *p)
{
    memset(p, 0, sizeof *p);
    make_polys();
    p->fast_forward_on = true;
    pokey_reset(p);
}

void pokey_reset(pokey *p)
{
    p->output = 0;
    p->output_stale = false;
    p->borrowing = 0;
    p->deferred = 0;
    memset(p->ch, 0, sizeof p->ch);
    p->ch[CHAN1].int_mask = IRQ_TIMER1;
    p->ch[CHAN2].int_mask = IRQ_TIMER2;
    p->ch[CHAN4].int_mask = IRQ_TIMER4;
    p->kbcode = 0x09;
    p->skctl = SK_RESET;
    p->skstat = 0;
    p->irqst = 0;
    p->irqen = 0;
    p->audctl = 0;
    p->serin = 0;
    reset_polys(p);
    p->allpot = 0;
    p->pot_counter = 0;
    p->clock_cnt[0] = p->clock_cnt[1] = p->clock_cnt[2] = 0;
    memset(p->potx, 0, sizeof p->potx);
}

static inline bool check_borrow(pokey_chan *c)
{
    if (c->borrow_cnt > 0) {
        c->borrow_cnt--;
        return c->borrow_cnt == 0;
    }
    return false;
}

static inline void reset_counter(pokey_chan *c) { c->counter = c->audf ^ 0xff; }

static inline void inc_chan(pokey *p, pokey_chan *c)
{
    c->counter = (c->counter + 1) & 0xff;
    if (c->counter == 0 && c->borrow_cnt == 0) {
        c->borrow_cnt = 3;
        if (p->irqen & c->int_mask) p->irqst |= c->int_mask;
    }
}

static void catch_up(pokey *p);

uint8_t pokey_read(pokey *p, uint16_t offset)
{
    catch_up(p);
    uint8_t data = 0;
    switch (offset & 0x0f) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: {
        uint8_t pot = offset & 7;
        data = (p->allpot & (1 << pot)) ? p->potx[pot] : p->pot_counter;
        break;
    }
    case ALLPOT:
        data = ((p->skctl & SK_RESET) == 0) ? 0 : (uint8_t)(p->allpot ^ 0xff);
        break;
    case KBCODE: data = p->kbcode; break;
    case RANDOM:
        data = (p->audctl & POLY9) ? (uint8_t)(poly9(p) & 0xff) : (uint8_t)poly17_byte(p);
        break;
    case SERIN: data = p->serin; break;
    case IRQST: data = (uint8_t)(p->irqst ^ 0xff); break;
    case SKSTAT: data = (uint8_t)(p->skstat ^ 0xff); break;
    default: break;
    }
    return data;
}

static void potgo(pokey *p)
{
    p->allpot = 0;
    p->pot_counter = 0;
    for (int pot = 0; pot < 8; ++pot) p->potx[pot] = 228;  // no pots wired
}

static uint32_t compute_output(const pokey *p)
{
    uint32_t sum = 0;
    for (int c = 0; c < 4; ++c)
        if ((p->ch[c].output ^ p->ch[c].filter_sample) || (p->ch[c].audc & VOLUME_ONLY))
            sum |= (uint32_t)(p->ch[c].audc & VOLUME_MASK) << (c * 4);
    return sum;
}

static void write_internal(pokey *p, uint16_t offset, uint8_t data)
{
    switch (offset & 0x0f) {
    case 0x00: p->ch[CHAN1].audf = data; break;
    case 0x01: p->ch[CHAN1].audc = data; break;
    case 0x02: p->ch[CHAN2].audf = data; break;
    case 0x03: p->ch[CHAN2].audc = data; break;
    case 0x04: p->ch[CHAN3].audf = data; break;
    case 0x05: p->ch[CHAN3].audc = data; break;
    case 0x06: p->ch[CHAN4].audf = data; break;
    case 0x07: p->ch[CHAN4].audc = data; break;
    case AUDCTL: p->audctl = data; break;
    case STIMER:
        for (int i = 0; i < 4; ++i) {
            reset_counter(&p->ch[i]);
            p->ch[i].output = 0;
            p->ch[i].filter_sample = (uint8_t)(i < 2 ? 1 : 0);
        }
        break;
    case SKREST: p->skstat &= (uint8_t)~(SK_FRAME | SK_OVERRUN | SK_KBERR); break;
    case POTGO: potgo(p); break;
    case SEROUT: p->skstat |= SK_SEROUT; break;
    case IRQEN:
        if (p->irqst & ~data) p->irqst &= data;
        p->irqen = data;
        break;
    case SKCTL:
        p->skctl = data;
        if ((data & SK_RESET) == 0) {
            write_internal(p, IRQEN, 0);
            write_internal(p, SKREST, 0);
            reset_polys(p);
            p->clock_cnt[0] = p->clock_cnt[1] = p->clock_cnt[2] = 0;
        }
        break;
    default: break;
    }
}

void pokey_write(pokey *p, uint16_t offset, uint8_t data)
{
    catch_up(p);
    if (p->output_stale) {
        p->output = compute_output(p);
        p->output_stale = false;
    }
    write_internal(p, offset, data);
}

static void process_channel(pokey *p, int c)
{
    if ((p->ch[c].audc & NOT_POLY5) || (poly5(p) & 1)) {
        if (p->ch[c].audc & PURE) p->ch[c].output ^= 1;
        else if (p->ch[c].audc & POLY4) p->ch[c].output = (uint8_t)(poly4(p) & 1);
        else if (p->audctl & POLY9) p->ch[c].output = (uint8_t)(poly9(p) & 1);
        else p->ch[c].output = (uint8_t)poly17_bit0(p);
    }
}

static void step_pot(pokey *p)
{
    uint8_t upd = 0;
    p->pot_counter++;
    for (int pot = 0; pot < 8; ++pot)
        if (p->potx[pot] < p->pot_counter || p->pot_counter == 228) upd |= (uint8_t)(1 << pot);
    p->allpot |= upd;
}

static inline uint8_t borrow_bits(const pokey *p)
{
    return (uint8_t)((p->ch[0].borrow_cnt > 0 ? 1 : 0) | (p->ch[1].borrow_cnt > 0 ? 2 : 0) |
                     (p->ch[2].borrow_cnt > 0 ? 4 : 0) | (p->ch[3].borrow_cnt > 0 ? 8 : 0));
}

static void step_one_clock(pokey *p)
{
    int trig[3] = {0, 0, 0};
    int base_clock = (p->audctl & CLK_15KHZ) ? CLK114 : CLK28;
    if (p->skctl & SK_RESET) {
        for (int clk = 0; clk < 3; ++clk) {
            p->clock_cnt[clk]++;
            if (p->clock_cnt[clk] >= clock_divisors[clk]) {
                p->clock_cnt[clk] = 0;
                trig[clk] = 1;
            }
        }
        ++p->ticks;
        int clk = (p->audctl & CH1_HICLK) ? CLK1 : base_clock;
        if (trig[clk]) inc_chan(p, &p->ch[CHAN1]);
        clk = (p->audctl & CH3_HICLK) ? CLK1 : base_clock;
        if (trig[clk]) inc_chan(p, &p->ch[CHAN3]);
        if (trig[base_clock]) {
            if ((p->audctl & CH12_JOINED) == 0) inc_chan(p, &p->ch[CHAN2]);
            if ((p->audctl & CH34_JOINED) == 0) inc_chan(p, &p->ch[CHAN4]);
        }
        if ((trig[CLK114] || (p->skctl & SK_PADDLE)) && p->pot_counter < 228) step_pot(p);
    }
    if (check_borrow(&p->ch[CHAN2])) {
        if (p->audctl & CH12_JOINED) reset_counter(&p->ch[CHAN1]);
        reset_counter(&p->ch[CHAN2]);
        process_channel(p, CHAN2);
    }
    if (check_borrow(&p->ch[CHAN1])) {
        if (p->audctl & CH12_JOINED) inc_chan(p, &p->ch[CHAN2]);
        else reset_counter(&p->ch[CHAN1]);
        process_channel(p, CHAN1);
    }
    if (check_borrow(&p->ch[CHAN4])) {
        if (p->audctl & CH34_JOINED) reset_counter(&p->ch[CHAN3]);
        reset_counter(&p->ch[CHAN4]);
        process_channel(p, CHAN4);
        if (p->audctl & CH2_FILTER) p->ch[CHAN2].filter_sample = p->ch[CHAN2].output;
        else p->ch[CHAN2].filter_sample = 1;
    }
    if (check_borrow(&p->ch[CHAN3])) {
        if (p->audctl & CH34_JOINED) inc_chan(p, &p->ch[CHAN4]);
        else reset_counter(&p->ch[CHAN3]);
        process_channel(p, CHAN3);
        if (p->audctl & CH1_FILTER) p->ch[CHAN1].filter_sample = p->ch[CHAN1].output;
        else p->ch[CHAN1].filter_sample = 1;
    }
    p->borrowing = borrow_bits(p);
}

static bool silent(const pokey *p)
{
    if (!p->fast_forward_on) return false;
    const uint8_t all_fast = CH1_HICLK | CH3_HICLK | CH12_JOINED | CH34_JOINED;
    if ((p->skctl & SK_RESET) == 0 || (p->audctl & all_fast) != all_fast || (p->irqen & 0x07) != 0) return false;
    if (p->pot_counter < 228) return false;
    for (int c = 0; c < 4; ++c)
        if (p->ch[c].audc & (VOLUME_MASK | VOLUME_ONLY | PURE)) return false;
    return true;
}

static void POKEY_HOT run_clocks(pokey *p, int cycles)
{
    const uint32_t stale = 0x40000000u;
    if (p->ticks - p->at4 > stale) poly4(p);
    if (p->ticks - p->at5 > stale) poly5(p);
    if (p->ticks - p->at9 > stale) poly9(p);
    if (p->ticks - p->at17 > stale) sync_poly17(p);
    if (cycles <= 0) return;
    p->output_stale = true;
    p->borrowing = borrow_bits(p);
    const bool counting = (p->skctl & SK_RESET) != 0;
    const bool hi1 = (p->audctl & CH1_HICLK) != 0, hi3 = (p->audctl & CH3_HICLK) != 0;
    const bool base_used = !hi1 || !hi3 || (p->audctl & CH12_JOINED) == 0 || (p->audctl & CH34_JOINED) == 0;
    const int base = (p->audctl & CLK_15KHZ) ? CLK114 : CLK28;
    const bool paddle = (p->skctl & SK_PADDLE) != 0;
    while (cycles > 0) {
        int quiet = cycles;
        if (p->borrowing)
            for (int c = 0; c < 4; ++c)
                if (p->ch[c].borrow_cnt > 0 && p->ch[c].borrow_cnt - 1 < quiet) quiet = p->ch[c].borrow_cnt - 1;
        if (counting && quiet > 0) {
            if (base_used) {
                // Base clock ticks only count the channels on it up: run to
                // just before the tick on which one of them would wrap to 0.
                int kmax = 255;
                if (!hi1 && 255 - p->ch[CHAN1].counter < kmax) kmax = 255 - p->ch[CHAN1].counter;
                if (!hi3 && 255 - p->ch[CHAN3].counter < kmax) kmax = 255 - p->ch[CHAN3].counter;
                if (!(p->audctl & CH12_JOINED) && 255 - p->ch[CHAN2].counter < kmax) kmax = 255 - p->ch[CHAN2].counter;
                if (!(p->audctl & CH34_JOINED) && 255 - p->ch[CHAN4].counter < kmax) kmax = 255 - p->ch[CHAN4].counter;
                int to = clock_divisors[base] - p->clock_cnt[base] - 1 + kmax * clock_divisors[base];
                if (to < quiet) quiet = to;
            }
            if (!paddle && p->pot_counter < 228) {
                int to = clock_divisors[CLK114] - p->clock_cnt[CLK114] - 1;
                if (to < quiet) quiet = to;
            }
            if (hi1 && 255 - p->ch[CHAN1].counter < quiet) quiet = 255 - p->ch[CHAN1].counter;
            if (hi3 && 255 - p->ch[CHAN3].counter < quiet) quiet = 255 - p->ch[CHAN3].counter;
        }
        if (quiet > 0) {
            if (counting) {
                if (base_used) {
                    int ticks = (p->clock_cnt[base] + quiet) / clock_divisors[base];
                    if (ticks) {
                        if (!hi1) p->ch[CHAN1].counter += ticks;
                        if (!hi3) p->ch[CHAN3].counter += ticks;
                        if (!(p->audctl & CH12_JOINED)) p->ch[CHAN2].counter += ticks;
                        if (!(p->audctl & CH34_JOINED)) p->ch[CHAN4].counter += ticks;
                    }
                }
                p->clock_cnt[CLK28] = (p->clock_cnt[CLK28] + quiet) % clock_divisors[CLK28];
                p->clock_cnt[CLK114] = (p->clock_cnt[CLK114] + quiet) % clock_divisors[CLK114];
                p->ticks += (uint32_t)quiet;
                if (hi1) p->ch[CHAN1].counter += quiet;
                if (hi3) p->ch[CHAN3].counter += quiet;
                if (paddle && p->pot_counter < 228) {
                    int steps = quiet < 228 - (int)p->pot_counter ? quiet : 228 - (int)p->pot_counter;
                    p->pot_counter = (uint8_t)(p->pot_counter + steps);
                    uint8_t upd = p->pot_counter == 228 ? 0xff : 0;
                    for (int pot = 0; pot < 8; ++pot)
                        if (p->potx[pot] < p->pot_counter) upd |= (uint8_t)(1 << pot);
                    p->allpot |= upd;
                }
            }
            if (p->borrowing)
                for (int c = 0; c < 4; ++c)
                    if (p->ch[c].borrow_cnt > 0) p->ch[c].borrow_cnt -= quiet;
            cycles -= quiet;
            continue;
        }
        step_one_clock(p);
        --cycles;
    }
}

// ---- Jumping a silent chip forward (see dsp-cpp pokey.cpp) ----

static inline bool pair_eq(const pokey_pair_state *a, const pokey_pair_state *b)
{
    return a->lead_counter == b->lead_counter && a->slave_counter == b->slave_counter &&
           a->lead_borrow == b->lead_borrow && a->slave_borrow == b->slave_borrow;
}

static inline int pair_clock(pokey_pair_state *s, int lead_reload, int slave_reload)
{
    int events = 0;
    s->lead_counter = (s->lead_counter + 1) & 0xff;
    if (s->lead_counter == 0 && s->lead_borrow == 0) s->lead_borrow = 3;
    if (s->slave_borrow > 0 && --s->slave_borrow == 0) {
        s->lead_counter = lead_reload;
        s->slave_counter = slave_reload;
        events |= 1;
    }
    if (s->lead_borrow > 0 && --s->lead_borrow == 0) {
        s->slave_counter = (s->slave_counter + 1) & 0xff;
        if (s->slave_counter == 0 && s->slave_borrow == 0) s->slave_borrow = 3;
        events |= 2;
    }
    return events;
}

static bool plan_pair(const pokey *p, int lead, int slave, uint32_t clocks, pokey_plan *plan)
{
    const int lr = p->ch[lead].audf ^ 0xff, sr = p->ch[slave].audf ^ 0xff;
    const pokey_pair_state start = {p->ch[lead].counter & 0xff, p->ch[slave].counter & 0xff,
                                    p->ch[lead].borrow_cnt, p->ch[slave].borrow_cnt};
    pokey_pair_state tortoise = start, hare = start;
    uint32_t power = 1, lambda = 1;
    pair_clock(&hare, lr, sr);
    while (!pair_eq(&tortoise, &hare)) {
        if (power == lambda) {
            tortoise = hare;
            power *= 2;
            lambda = 0;
        }
        pair_clock(&hare, lr, sr);
        ++lambda;
        if (power > POKEY_PLAN_CLOCKS) return false;
    }
    pokey_pair_state a = start, b = start;
    for (uint32_t i = 0; i < lambda; ++i) pair_clock(&b, lr, sr);
    uint32_t mu = 0;
    while (!pair_eq(&a, &b)) {
        pair_clock(&a, lr, sr);
        pair_clock(&b, lr, sr);
        ++mu;
        if (mu + lambda > POKEY_PLAN_CLOCKS) return false;
    }
    plan->prefix = mu;
    plan->period = lambda;
    plan->clocks = clocks;
    pokey_pair_state s = start;
    plan->count = 0;
    for (uint32_t k = 1; k <= mu + lambda; ++k) {
        if (k == mu + 1) plan->count_prefix = plan->count;
        int ev = pair_clock(&s, lr, sr);
        if (ev & 1) { plan->at[plan->count] = (uint16_t)k; plan->who[plan->count++] = 1; }
        if (ev & 2) { plan->at[plan->count] = (uint16_t)k; plan->who[plan->count++] = 2; }
    }
    if (mu + lambda == 0) plan->count_prefix = 0;
    pokey_pair_state e = start;
    uint32_t steps = clocks <= mu ? clocks : mu + (clocks - mu) % lambda;
    for (uint32_t k = 0; k < steps; ++k) pair_clock(&e, lr, sr);
    plan->end = e;
    return true;
}

typedef struct {
    const pokey *p;
    uint32_t t0;
} ff_ctx;

static inline uint32_t poly5_at(const ff_ctx *x, uint32_t k)
{
    const pokey *p = x->p;
    return poly5_tab[(p->i5 + (x->t0 + k - p->at5) % 31u) % 31u] & 1;
}

static uint8_t sample_at(const ff_ctx *x, int ch, uint32_t k)
{
    const pokey *p = x->p;
    if (p->ch[ch].audc & POLY4) return (uint8_t)(poly4_tab[(p->i4 + (x->t0 + k - p->at4) % 15u) % 15u] & 1);
    if (p->audctl & POLY9) return (uint8_t)(poly9_tab[(p->i9 + (x->t0 + k - p->at9) % 511u) % 511u] & 1);
    const uint32_t n = (uint32_t)POLY17_LENGTH;
    int i = (int)((p->i17 + (x->t0 + k - p->at17) % n) % n);
    return (uint8_t)(bit16_at(i - 16) ^ bit16_at(i - 11));
}

// take: 0 = every event, 1 = events where poly5 is 1 (gated channel)
static uint32_t last_event(const ff_ctx *x, const pokey_plan *plan, int who, uint32_t limit, int gated)
{
    if (limit > plan->clocks) limit = plan->clocks;
    if (plan->period > 0 && limit > plan->prefix && plan->count > plan->count_prefix) {
        uint32_t n_last = (limit - plan->prefix - 1) / plan->period;
        for (uint32_t back = 0; back <= 32 && back <= n_last; ++back) {
            uint32_t n = n_last - back;
            uint32_t base = plan->prefix + n * plan->period - plan->prefix;
            for (int i = (int)plan->count - 1; i >= (int)plan->count_prefix; --i) {
                if (plan->who[i] != who) continue;
                uint32_t k = base + plan->at[i];
                if (k > limit) continue;
                if (!gated || poly5_at(x, k)) return k;
            }
        }
    }
    for (int i = (int)plan->count_prefix - 1; i >= 0; --i) {
        if (plan->who[i] != who || plan->at[i] > limit) continue;
        if (!gated || poly5_at(x, plan->at[i])) return plan->at[i];
    }
    return 0;
}

static uint8_t output_at(const ff_ctx *x, int ch, const pokey_plan *plan, int who, uint32_t limit)
{
    int gated = (x->p->ch[ch].audc & NOT_POLY5) == 0;
    uint32_t k = last_event(x, plan, who, limit, gated);
    return k ? sample_at(x, ch, k) : x->p->ch[ch].output;
}

static bool fast_forward(pokey *p, uint32_t clocks)
{
    if (!silent(p)) return false;
    if (!p->plan_a) p->plan_a = (pokey_plan *)malloc(sizeof(pokey_plan));
    if (!p->plan_b) p->plan_b = (pokey_plan *)malloc(sizeof(pokey_plan));
    if (!p->plan_a || !p->plan_b) return false;
    pokey_plan *a = p->plan_a, *b = p->plan_b;
    if (!plan_pair(p, CHAN1, CHAN2, clocks, a) || !plan_pair(p, CHAN3, CHAN4, clocks, b)) return false;

    ff_ctx x = {p, p->ticks};
    uint8_t out1 = output_at(&x, CHAN1, a, 2, clocks);
    uint8_t out2 = output_at(&x, CHAN2, a, 1, clocks);
    uint8_t out3 = output_at(&x, CHAN3, b, 2, clocks);
    uint8_t out4 = output_at(&x, CHAN4, b, 1, clocks);
    uint32_t k3 = last_event(&x, b, 2, clocks, 0);
    uint32_t k4 = last_event(&x, b, 1, clocks, 0);
    if (k3) p->ch[CHAN1].filter_sample = (p->audctl & CH1_FILTER) ? output_at(&x, CHAN1, a, 2, k3) : 1;
    if (k4) p->ch[CHAN2].filter_sample = (p->audctl & CH2_FILTER) ? output_at(&x, CHAN2, a, 1, k4) : 1;
    p->ch[CHAN1].output = out1;
    p->ch[CHAN2].output = out2;
    p->ch[CHAN3].output = out3;
    p->ch[CHAN4].output = out4;
    p->ch[CHAN1].counter = a->end.lead_counter;
    p->ch[CHAN1].borrow_cnt = a->end.lead_borrow;
    p->ch[CHAN2].counter = a->end.slave_counter;
    p->ch[CHAN2].borrow_cnt = a->end.slave_borrow;
    p->ch[CHAN3].counter = b->end.lead_counter;
    p->ch[CHAN3].borrow_cnt = b->end.lead_borrow;
    p->ch[CHAN4].counter = b->end.slave_counter;
    p->ch[CHAN4].borrow_cnt = b->end.slave_borrow;
    p->clock_cnt[CLK28] = (int)(((uint32_t)p->clock_cnt[CLK28] + clocks) % (uint32_t)clock_divisors[CLK28]);
    p->clock_cnt[CLK114] = (int)(((uint32_t)p->clock_cnt[CLK114] + clocks) % (uint32_t)clock_divisors[CLK114]);
    p->ticks += clocks;
    poly4(p);
    poly5(p);
    poly9(p);
    sync_poly17(p);
    p->output_stale = true;
    return true;
}

static bool muted(const pokey *p)
{
    if (!p->approx_mute || (p->irqen & 0x07) != 0 || p->pot_counter < 228) return false;
    for (int c = 0; c < 4; ++c)
        if (p->ch[c].audc & (VOLUME_MASK | VOLUME_ONLY | PURE)) return false;
    return true;
}

static void catch_up(pokey *p)
{
    if (p->deferred == 0) return;
    uint32_t clocks = p->deferred;
    p->deferred = 0;
    if (!silent(p) && muted(p)) {
        if (p->skctl & SK_RESET) {
            p->clock_cnt[CLK28] = (int)(((uint32_t)p->clock_cnt[CLK28] + clocks) % (uint32_t)clock_divisors[CLK28]);
            p->clock_cnt[CLK114] = (int)(((uint32_t)p->clock_cnt[CLK114] + clocks) % (uint32_t)clock_divisors[CLK114]);
            p->ticks += clocks;
            poly4(p);
            poly5(p);
            poly9(p);
            sync_poly17(p);
        }
        p->output_stale = true;
        return;
    }
    if (!fast_forward(p, clocks)) {
        for (uint32_t done = 0; done < clocks;) {
            int n = (int)(clocks - done < 0x100000 ? clocks - done : 0x100000);
            run_clocks(p, n);
            done += (uint32_t)n;
        }
    }
}

void POKEY_HOT pokey_run(pokey *p, int cycles)
{
    if (cycles <= 0) return;
    if (silent(p) || muted(p)) {
        p->output_stale = true;
        p->deferred += (uint32_t)cycles;
        if (p->deferred >= 0x10000000u) catch_up(p);
        return;
    }
    catch_up(p);
    run_clocks(p, cycles);
}

int32_t POKEY_HOT pokey_update(pokey *p)
{
    if (p->output_stale) {
        p->output = compute_output(p);
        p->output_stale = false;
    }
    int32_t out = 0;
    for (int i = 0; i < 4; ++i) out += (int32_t)((p->output >> (4 * i)) & 0x0f);
    out *= DEFAULT_GAIN;
    return out < 0x7fff ? out : 0x7fff;
}
