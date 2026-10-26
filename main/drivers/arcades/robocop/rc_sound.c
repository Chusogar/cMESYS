// rc_sound.c - RoboCop sound board. See rc_sound.h.
// Memory map (MAME dec0_s_map):
//   0000-07FF RAM, 0800-0801 YM2203, 1000-1001 YM3812, 3000 sound latch,
//   3800 OKI M6295, 8000-FFFF ROM (unmapped reads: 0).
#include "rc_sound.h"
#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define SND_HOT IRAM_ATTR
static const char *SND_TAG = "rc_sound";
static inline void snd_lock(rc_sound *s)
{
    if (!s->lock) s->lock = xSemaphoreCreateMutex();
    if (s->lock) xSemaphoreTake((SemaphoreHandle_t)s->lock, portMAX_DELAY);
}
static inline void snd_unlock(rc_sound *s)
{
    if (s->lock) xSemaphoreGive((SemaphoreHandle_t)s->lock);
}
#else
#define SND_HOT
static inline void snd_lock(rc_sound *s) { (void)s; }
static inline void snd_unlock(rc_sound *s) { (void)s; }
#endif

#define OPL_CLOCK 3000000
#define OKI_CYCLES 198                  // 132 OKI clocks (1 MHz) in 6502 cycles

// ---------------------------------------------------------------------------
// Yamaha timers (ymfm fm_engine): periods in native samples, armed in clocks
// from the moment they are loaded; status bits set on expiry; IRQ from the
// status bits not masked.
// ---------------------------------------------------------------------------
enum { OPN_TA = 0x01, OPN_TB = 0x02, OPN_BUSY = 0x80, OPL_TA = 0x40, OPL_TB = 0x20, OPL_IRQ = 0x80 };

static inline uint32_t opn_period(const rc_fm_timers *t, int n)
{
    return n == 0 ? 1024u - (((uint32_t)t->regs[0x24] << 2) | (t->regs[0x25] & 3)) : 16u * (256u - t->regs[0x26]);
}
static inline uint32_t opl_period(const rc_fm_timers *t, int n)
{
    return n == 0 ? 4u * (256u - t->regs[0x02]) : 16u * (256u - t->regs[0x03]);
}

static void update_irq(rc_sound *s)
{
    // OPN: IRQ from TA / TB (status mask: none); OPL: TA / TB not masked by reg 4
    const bool opn = (s->opn_t.status & (OPN_TA | OPN_TB)) != 0;
    s->opn_t.irq = opn;
    const uint8_t mask = s->opl_t.regs[0x04] & 0x78;
    const bool opl = (s->opl_t.status & (OPL_TA | OPL_TB) & (uint8_t)~mask) != 0;
    if (opl) s->opl_t.status |= OPL_IRQ; else s->opl_t.status &= (uint8_t)~OPL_IRQ;
    s->opl_t.irq = opl;
    dsp6502_set_irq(&s->cpu, (opn || opl) ? LINE_ASSERT : LINE_CLEAR);
}

static void arm(rc_fm_timers *t, bool opl, int n, bool enable, int32_t delta)
{
    if (enable && !t->running[n]) {
        int32_t period = (int32_t)(opl ? opl_period(t, n) : opn_period(t, n)) + delta;
        t->left[n] = period * (int32_t)t->sample_cycles;
        t->running[n] = true;
    } else if (!enable) {
        t->running[n] = false;
    }
}

static void opn_mode_write(rc_sound *s, uint8_t v)
{
    rc_fm_timers *t = &s->opn_t;
    t->regs[0x27] = v;
    uint8_t reset = 0;
    if (v & 0x20) reset |= OPN_TB;
    if (v & 0x10) reset |= OPN_TA;
    t->status &= (uint8_t)~reset;
    arm(t, false, 1, v & 2, -(int32_t)(t->total & 15));
    arm(t, false, 0, v & 1, 0);
    update_irq(s);
}

static void opl_mode_write(rc_sound *s, uint8_t v)
{
    rc_fm_timers *t = &s->opl_t;
    if (v & 0x80) {
        t->regs[0x04] |= 0x80;
        t->status &= (uint8_t)~0x78;
    } else {
        t->regs[0x04] = v;
        uint8_t reset = 0;
        if (v & 0x20) reset |= OPL_TB;
        if (v & 0x40) reset |= OPL_TA;
        t->status &= (uint8_t)~reset;
        arm(t, true, 1, v & 2, -(int32_t)(t->total & 15));
        arm(t, true, 0, v & 1, 0);
    }
    update_irq(s);
}

static void timers_advance(rc_sound *s, rc_fm_timers *t, bool opl, int32_t n)
{
    t->phase += (uint32_t)n;
    if (t->phase >= t->sample_cycles) {
        t->total += t->phase / t->sample_cycles;
        t->phase %= t->sample_cycles;
    }
    bool changed = false;
    for (int k = 0; k < 2; k++) {
        if (!t->running[k]) continue;
        t->left[k] -= n;
        while (t->left[k] <= 0) {
            if (opl) t->status |= k ? OPL_TB : OPL_TA;
            else if (t->regs[0x27] & (k ? 0x08 : 0x04)) t->status |= k ? OPN_TB : OPN_TA;
            t->left[k] += (int32_t)((opl ? opl_period(t, k) : opn_period(t, k)) * t->sample_cycles);
            changed = true;
        }
    }
    if (changed) update_irq(s);
}

static inline int32_t timers_next(const rc_fm_timers *t)
{
    int32_t m = INT32_MAX;
    for (int k = 0; k < 2; k++) if (t->running[k] && t->left[k] < m) m = t->left[k];
    return m;
}

// ---------------------------------------------------------------------------
// Time: chips' timers, OKI, output samples
// ---------------------------------------------------------------------------
static inline void emit_sample(rc_sound *s)
{
    int16_t fm;
    ym2610_render(s->opn, &fm, 1, 256);
    int32_t opl = OPL_calc(s->opl);
    // MAME: YM3812 0.72 (emu8950's output is half of ymfm's), OKI 0.72
    int32_t v = fm + ((opl * 46) >> 5) + ((s->oki_out * 23) >> 5);
    if (v < -32768) v = -32768; else if (v > 32767) v = 32767;
    if (s->audio && s->audio_n < s->audio_cap) s->audio[s->audio_n++] = (int16_t)v;
    s->sample_pos++;
}

// The OKI samples and the output samples are taken in the order of their
// cycles (the result does not depend on how the time is cut up).
static void SND_HOT snd_cycles(rc_sound *s, int32_t n)
{
    timers_advance(s, &s->opn_t, false, n);
    timers_advance(s, &s->opl_t, true, n);
    const int32_t rate = (int32_t)s->sample_rate;
    while (n > 0) {
        const int32_t to_oki = OKI_CYCLES - s->oki_acc;
        const int32_t to_out = (RC_SND_CLOCK - s->audio_acc + rate - 1) / rate;
        int32_t step = to_oki < to_out ? to_oki : to_out;
        if (step > n) step = n;
        s->oki_acc += step;
        s->audio_acc += step * rate;
        n -= step;
        if (s->oki_acc >= OKI_CYCLES) {
            s->oki_acc -= OKI_CYCLES;
            s->oki_out = okim6295_tick(&s->oki);
        }
        if (s->audio_acc >= RC_SND_CLOCK) {
            s->audio_acc -= RC_SND_CLOCK;
            emit_sample(s);
        }
    }
}

// ---------------------------------------------------------------------------
// 6502 bus
// ---------------------------------------------------------------------------
static uint8_t SND_HOT snd_read(void *u, uint16_t a)
{
    rc_sound *s = (rc_sound *)u;
    if (a < 0x0800) return s->ram[a];
    if (a >= 0x8000) return s->rom[a - 0x8000];
    switch (a) {
    case 0x0800:                                            // YM2203 status
        return (uint8_t)((s->opn_t.status & (OPN_TA | OPN_TB)) |
                         ((int64_t)s->cycles < s->opn_busy_until ? OPN_BUSY : 0));
    case 0x0801:                                            // YM2203 data (SSG registers)
        return s->opn_addr < 0x10 ? s->opn_ssg[s->opn_addr] : 0;
    case 0x1000:                                            // YM3812 status
        return (uint8_t)((s->opl_t.status & ~(s->opl_t.regs[0x04] & 0x78)) | 0x06);
    case 0x1001: return 0xff;
    case 0x3000:                                            // sound latch (clears NMI)
        s->latch_pending = false;
        dsp6502_set_nmi(&s->cpu, LINE_CLEAR);
        return s->latch;
    case 0x3800: return okim6295_read(&s->oki);
    default: return 0;
    }
}

static void SND_HOT snd_write(void *u, uint16_t a, uint8_t v)
{
    rc_sound *s = (rc_sound *)u;
    if (a < 0x0800) { s->ram[a] = v; return; }
    switch (a) {
    case 0x0800:
        s->opn_addr = v;
        if (v == 0x2d) s->opn_prescale = 6;
        else if (v == 0x2e && s->opn_prescale == 6) s->opn_prescale = 3;
        else if (v == 0x2f) s->opn_prescale = 2;
        else break;
        s->opn_t.sample_cycles = (uint32_t)(s->opn_prescale * 12);
        ym2203_set_prescale(s->opn, s->opn_prescale);
        break;
    case 0x0801: {
        const uint8_t r = s->opn_addr;
        if (r < 0x10) s->opn_ssg[r] = v;
        if (r == 0x27) opn_mode_write(s, v);
        else if (r >= 0x24 && r <= 0x26) s->opn_t.regs[r] = v;
        ym2610_write(s->opn, 0, r, v);
        s->opn_busy_until = (int64_t)s->cycles + 32 * s->opn_prescale;
        break;
    }
    case 0x1000: s->opl_addr = v; break;
    case 0x1001: {
        const uint8_t r = s->opl_addr;
        if (r == 0x04) opl_mode_write(s, v);
        else {
            if (r == 0x02 || r == 0x03) s->opl_t.regs[r] = v;
            OPL_writeReg(s->opl, r, v);
        }
        break;
    }
    case 0x3800: okim6295_write(&s->oki, v); break;
    default: break;
    }
}

static void snd_tick(void *u, int cycles)
{
    rc_sound *s = (rc_sound *)u;
    s->cycles += cycles;
    snd_cycles(s, cycles);
}

// The sound program waits for its interrupts in "8091 JMP 8091" (3 cycles
// a pass). Interrupts are checked before each pass and only the timers (or a
// latch write, between runs) raise them: the passes up to the one in which
// the next timer expires (or the run ends) only take time.
static int SND_HOT snd_idle(void *u, int budget)
{
    rc_sound *s = (rc_sound *)u;
    if (!s->idle_skip) return 0;
    int32_t span = budget;
    // an interrupt already waiting (the pass right after CLI): one pass, then it is taken
    if (s->cpu.nmi_request != LINE_CLEAR || (s->cpu.irq_request != LINE_CLEAR && !s->cpu.fi)) span = 1;
    int32_t ev = timers_next(&s->opn_t), ev2 = timers_next(&s->opl_t);
    if (ev2 < ev) ev = ev2;
    if (ev < span) span = ev;
    if (span < 1) span = 1;
    // all the passes but the last one: the CPU runs that one itself (and
    // checks the interrupts after it)
    const int32_t cyc = ((span + 2) / 3 - 1) * 3;
    if (cyc <= 0) return 0;
    s->cycles += cyc;
    snd_cycles(s, cyc);
    s->skipped += (uint32_t)cyc;
    return cyc;
}

// After an OKI command the program waits in "8F5F SEC / SBC #1 / BNE 8F5F"
// (7 cycles a pass, 6 the last one). A pass only changes A (the flags are
// made again by the next one): the whole passes that end before the next
// timer expiry, the last pass and the end of the run are counted in one go.
static inline int snd_hook(rc_sound *s, dsp6502 *c, int budget)
{
    if (c->pc != 0x8f5f || !s->idle_skip || c->a < 2) return 0;
    if (c->nmi_request != LINE_CLEAR || (c->irq_request != LINE_CLEAR && !c->fi)) return 0;
    int32_t k = c->a - 1;                                   // taken passes left
    int32_t ev = timers_next(&s->opn_t), ev2 = timers_next(&s->opl_t);
    if (ev2 < ev) ev = ev2;
    if ((ev - 1) / 7 < k) k = (ev - 1) / 7;
    if ((budget - 1) / 7 < k) k = (budget - 1) / 7;
    if (k <= 0) return 0;
    c->a = (uint8_t)(c->a - k);
    const int32_t cyc = 7 * k;
    s->cycles += cyc;
    snd_cycles(s, cyc);
    s->skipped += (uint32_t)cyc;
    return cyc;
}

// The 6502 with the bus handlers above called directly
#define DSP6502_READ(u, a) snd_read(u, a)
#define DSP6502_WRITE(u, a, v) snd_write(u, a, v)
#define DSP6502_CYCLES(u, n) snd_tick(u, n)
#define DSP6502_HOOK(u, c, budget) snd_hook((rc_sound *)(u), c, budget)
#include "m6502/dsp6502.c"

// Runs the 6502 up to main CPU time t (6502 = main clock * 3 / 20).
static void SND_HOT run_to(rc_sound *s, int64_t t)
{
    const int64_t target = t * 3 / 20;
    while (s->cycles < target) {
        int64_t n = target - s->cycles;
        // stop at the next timer event so the IRQ comes on its cycle
        int32_t ev = timers_next(&s->opn_t);
        int32_t ev2 = timers_next(&s->opl_t);
        if (ev2 < ev) ev = ev2;
        if (ev > 0 && ev < n) n = ev;
        if (n > 256) n = 256;
        dsp6502_run(&s->cpu, (int)n);
    }
}

static void sound_run_unlocked(rc_sound *s, int64_t until)
{
    while (s->ev_tail != s->ev_head) {
        const rc_snd_event *e = &s->ev[s->ev_tail % RC_SND_EVENTS];
        if (e->time > until) break;
        run_to(s, e->time);
        s->latch = e->value;
        if (!s->latch_pending) dsp6502_set_nmi(&s->cpu, LINE_ASSERT);
        s->latch_pending = true;
        __sync_synchronize();
        s->ev_tail++;
    }
    run_to(s, until);
    if (until > s->time) s->time = until;
}

void rc_sound_run(rc_sound *s, int64_t until)
{
    snd_lock(s);
    sound_run_unlocked(s, until);
    snd_unlock(s);
}

void rc_sound_catch_up(rc_sound *s, int64_t until)
{
    snd_lock(s);
    int16_t *au = s->audio;
    int cap = s->audio_cap, n = s->audio_n;
    s->audio = NULL;
    s->audio_cap = 0;
    sound_run_unlocked(s, until);
    s->audio = au;
    s->audio_cap = cap;
    s->audio_n = n;
    snd_unlock(s);
}

bool rc_sound_latch(rc_sound *s, int64_t time, uint8_t value)
{
    if (s->ev_head - s->ev_tail >= RC_SND_EVENTS) return false;
    rc_snd_event *e = &s->ev[s->ev_head % RC_SND_EVENTS];
    e->time = time;
    e->value = value;
    __sync_synchronize();
    s->ev_head++;
    return true;
}

void rc_sound_init(rc_sound *s, const uint8_t *rom, const uint8_t *oki_rom, uint32_t oki_size, uint32_t sample_rate)
{
    memset(s, 0, sizeof *s);
    s->rom = rom;
    s->sample_rate = sample_rate ? sample_rate : 44100;
    dsp6502_init(&s->cpu);
    s->cpu.read = snd_read;
    s->cpu.write = snd_write;
    s->cpu.cycles = snd_tick;
    s->cpu.user = s;
    for (int p = 0; p < 8; p++) dsp6502_set_page(&s->cpu, (uint8_t)p, s->ram + p * 256, s->ram + p * 256);
    for (int p = 0x80; p < 0x100; p++) dsp6502_set_page(&s->cpu, (uint8_t)p, rom + (p - 0x80) * 256, NULL);
    if (rom[0x0091] == 0x4c && rom[0x0092] == 0x91 && rom[0x0093] == 0x80) {
        s->cpu.idle = snd_idle;
        s->cpu.idle_pc = 0x8091;
    }
    okim6295_init(&s->oki, oki_rom, oki_size);
    s->opn = ym2203_create(RC_SND_CLOCK, (int)s->sample_rate);
    s->opl = OPL_new(OPL_CLOCK, s->sample_rate);
#ifdef ESP_PLATFORM
    if (!s->opn || !s->opl) {
        ESP_LOGE(SND_TAG, "YM2203/YM3812 alloc failed (opn=%p opl=%p)", (void *)s->opn, (void *)s->opl);
        abort();
    }
#endif
    // MAME: SSG A/B/C 0.81 each, FM 0.32 (ssg_output averages the 3 channels)
    ym2610_set_gains(s->opn, 82, 622);
    OPL_setChipType(s->opl, 2);         // YM3812
    rc_sound_reset(s);
}

void rc_sound_reset(rc_sound *s)
{
    memset(s->ram, 0, sizeof s->ram);
    s->latch = 0;
    s->latch_pending = false;
    s->opn_addr = 0;
    memset(s->opn_ssg, 0, sizeof s->opn_ssg);
    s->opn_prescale = 6;
    s->opn_busy_until = 0;
    memset(&s->opn_t, 0, sizeof s->opn_t);
    s->opn_t.sample_cycles = 72;        // 6 x 12 clocks, YM2203 clock = 6502 clock
    s->opl_addr = 0;
    memset(&s->opl_t, 0, sizeof s->opl_t);
    s->opl_t.sample_cycles = 36;        // 72 clocks at 3 MHz
    okim6295_reset(&s->oki);
    s->oki_acc = 0;
    s->oki_out = 0;
    ym2610_reset(s->opn);
    ym2203_set_prescale(s->opn, 6);
    OPL_reset(s->opl);
    s->time = 0;
    s->cycles = 0;
    s->audio_acc = 0;
    s->sample_pos = 0;
    s->ev_head = s->ev_tail = 0;
    dsp6502_reset(&s->cpu);
    update_irq(s);
}
