// or_sound.c - OutRun sound board. See or_sound.h.
// Memory map (MAME's sound_map / sound_portmap):
//   0000-EFFF ROM (0x8000 bytes, zeros after), F000-F7FF PCM registers (F0xx
//   mirrored), F800-FFFF RAM; ports 00-3F YM2151 (even: address, odd: data /
//   status), 40-7F the main CPU's byte (reading it clears PBF).
#include "or_sound.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define SND_HOT IRAM_ATTR
#else
#define SND_HOT
#endif

#define SND_CLOCK 4000000

static inline void ym_recompute_due(or_sound *s)
{
    int32_t d = INT32_MAX;
    if (s->ym.timer_a_enabled && s->ym.timer_a_counter < d) d = s->ym.timer_a_counter;
    if (s->ym.timer_b_enabled && s->ym.timer_b_counter < d) d = s->ym.timer_b_counter;
    s->ym_due = d;
}

static inline void flush_ym(or_sound *s)
{
    if (s->ym_pending) {
        ym2151_run_timers(&s->ym, s->ym_pending);
        s->ym_pending = 0;
    }
    ym_recompute_due(s);
}

static uint8_t SND_HOT snd_read(void *u, uint16_t a)
{
    or_sound *s = (or_sound *)u;
    if (a < 0x8000) return s->rom[a];
    if (a < 0xf000) return 0x00;
    if (a < 0xf800) return segapcm_read(&s->pcm, (uint8_t)a);
    return s->ram[a & 0x7ff];
}

static void SND_HOT snd_write(void *u, uint16_t a, uint8_t v)
{
    or_sound *s = (or_sound *)u;
    if (a >= 0xf800) s->ram[a & 0x7ff] = v;
    else if (a >= 0xf000) segapcm_write(&s->pcm, (uint8_t)a, v);
}

static uint8_t snd_in(z80 *z, uint16_t port)
{
    or_sound *s = (or_sound *)z->userdata;
    port &= 0xff;
    if (port < 0x40) {
        if (!(port & 1)) return 0xff;
        flush_ym(s);
        return ym2151_status(&s->ym);
    }
    if (port < 0x80) {
        s->pbf = false;
        return s->latch;
    }
    return 0xff;
}

static void snd_out(z80 *z, uint16_t port, uint8_t v)
{
    or_sound *s = (or_sound *)z->userdata;
    port &= 0xff;
    if (port >= 0x40) return;
    if (!(port & 1)) { ym2151_select(&s->ym, v); return; }
    flush_ym(s);
    ym2151_write(&s->ym, v);
    ym_recompute_due(s);
}

static uint8_t pcm_read(void *u, uint32_t a)
{
    const or_sound *s = (const or_sound *)u;
    const uint32_t chip = (a >> 16) & 7;
    return chip < 6 ? s->pcm_rom[chip * 0x8000 + (a & 0x7fff)] : 0xff;
}

// Z80 time passes: YM2151 timers, PCM samples, output samples.
static void SND_HOT snd_cycles(or_sound *s, int cycles)
{
    s->ym_pending += cycles;                        // the YM2151 runs at the Z80 clock
    if (s->ym_pending >= s->ym_due) flush_ym(s);
    s->pcm_acc += cycles;
    while (s->pcm_acc >= 128) {
        s->pcm_acc -= 128;
        segapcm_tick(&s->pcm);
        s->pcm_sum_l += s->pcm.out_l;
        s->pcm_sum_r += s->pcm.out_r;
        s->pcm_n++;
    }
    s->audio_acc += cycles * (int32_t)s->sample_rate;
    while (s->audio_acc >= SND_CLOCK) {
        s->audio_acc -= SND_CLOCK;
        // PCM: average of its samples since the last output sample, mono
        // (or the last one again when the output rate is the higher)
        if (s->pcm_n) {
            s->pcm_last = (s->pcm_sum_l + s->pcm_sum_r) / (2 * s->pcm_n);
            s->pcm_sum_l = s->pcm_sum_r = 0;
            s->pcm_n = 0;
        }
        // MAME routes the YM2151 at 0.30 and the PCM at 0.70; measured against
        // MAME's output, this PCM sum comes out at its level as it is, and
        // this YM2151 at a quarter.
        int32_t v = s->pcm_last + (ym2151_update(&s->ym) >> 2);
        if (v < -32768) v = -32768; else if (v > 32767) v = 32767;
        if (s->audio && s->audio_n < s->audio_cap) s->audio[s->audio_n++] = (int16_t)v;
        s->sample_pos++;
    }
}

// The sound program waits for the YM2151's timer A flag:
//   0A61 IN A,($01) / BIT 0,A / JP Z,$0A61
// While the status read stays the same (and no NMI is pending: events are
// applied between runs), a pass changes nothing but the time and R. The
// passes are carried out here instruction by instruction (same cycles, same
// chips' work), so they can stop anywhere; the status is read where IN reads it.
static const uint8_t poll_cyc[3] = {11, 8, 10};
static const uint8_t poll_r[3] = {1, 2, 1};

static void SND_HOT z80_poll(or_sound *s, int64_t target)
{
    z80 *z = &s->z;
    int i = (z->pc - 0x0a61) / 2;
    const int64_t start = s->z80_cycles;
    while (s->z80_cycles < target && !z->nmi_pending) {
        if (i == 0) {
            if (s->ym_pending) flush_ym(s);
            if (ym2151_status(&s->ym) != z->a) break;       // IN would read something new
        }
        z->r = (uint8_t)((z->r & 0x80) | ((z->r + poll_r[i]) & 0x7f));
        snd_cycles(s, poll_cyc[i]);
        s->z80_cycles += poll_cyc[i];
        i = i == 2 ? 0 : i + 1;
    }
    z->pc = (uint16_t)(0x0a61 + 2 * i);
    s->skipped += (uint32_t)(s->z80_cycles - start);
}

// Runs the Z80 (or just time, in reset) up to main CPU time t.
static void SND_HOT run_to(or_sound *s, int64_t t)
{
    const int64_t target = t * 2 / 5;              // 4 MHz vs 10 MHz
    if (s->reset) {
        if (target > s->z80_cycles) {
            int64_t n = target - s->z80_cycles;
            while (n > 0) {
                const int k = n > 4096 ? 4096 : (int)n;
                snd_cycles(s, k);
                n -= k;
            }
            s->z80_cycles = target;
        }
        return;
    }
    z80 *z = &s->z;
    while (s->z80_cycles < target) {
        if (s->idle_skip && s->poll_ok && (z->pc == 0x0a61 || z->pc == 0x0a63 || z->pc == 0x0a65)) {
            z80_poll(s, target);
            if (s->z80_cycles >= target) break;
        }
#ifdef OR_PROF_Z80
        { extern void prof_z80(uint16_t pc); prof_z80(z->pc); }
#endif
        const uint16_t pc = z->pc;
        const int n = (int)z80_step(z);
        s->z80_cycles += n;
        snd_cycles(s, n);
        // a real pass ended (JP back to the IN, no interrupt taken)
        s->poll_ok = pc == 0x0a65 && z->pc == 0x0a61;
    }
}

static void apply(or_sound *s, const or_snd_event *e)
{
    switch (e->type) {
    case OR_EV_LATCH:
        if (!s->pbf) z80_pulse_nmi(&s->z);     // PBF -> NMI (edge; kept through a reset hold, as MAME)
        s->pbf = true;
        s->latch = e->value;
        break;
    case OR_EV_RESET:
        if (e->value && !s->reset) {
            z80_reset(&s->z);
            s->poll_ok = false;
        }
        s->reset = e->value != 0;
        break;
    }
}

void or_sound_run(or_sound *s, int64_t until)
{
    while (s->ev_tail != s->ev_head) {
        const or_snd_event *e = &s->ev[s->ev_tail % OR_SND_EVENTS];
        if (e->time > until) break;
        run_to(s, e->time);
        apply(s, e);
        __sync_synchronize();
        s->ev_tail++;
    }
    run_to(s, until);
    if (until > s->time) s->time = until;
}

bool or_sound_event(or_sound *s, int64_t time, uint8_t type, uint8_t value)
{
    if (s->ev_head - s->ev_tail >= OR_SND_EVENTS) return false;
    or_snd_event *e = &s->ev[s->ev_head % OR_SND_EVENTS];
    e->time = time;
    e->type = type;
    e->value = value;
    __sync_synchronize();
    s->ev_head++;
    return true;
}

void or_sound_init(or_sound *s, const uint8_t *rom, const uint8_t *pcm_rom, uint32_t sample_rate)
{
    memset(s, 0, sizeof *s);
    s->rom = rom;
    s->pcm_rom = pcm_rom;
    s->sample_rate = sample_rate ? sample_rate : 44100;
    z80_init(&s->z);
    s->z.read_byte = snd_read;
    s->z.write_byte = snd_write;
    s->z.port_in = snd_in;
    s->z.port_out = snd_out;
    s->z.userdata = s;
    ym2151_init(&s->ym, SND_CLOCK, s->sample_rate);
    s->pcm.read_rom = pcm_read;
    s->pcm.user = s;
    segapcm_init(&s->pcm);
    or_sound_reset(s);
}

void or_sound_reset(or_sound *s)
{
    z80_reset(&s->z);
    ym2151_reset(&s->ym);
    s->ym_pending = 0;
    ym_recompute_due(s);
    s->pbf = false;
    s->latch = 0;
    s->reset = false;
    s->time = 0;
    s->z80_cycles = 0;
    s->pcm_acc = s->audio_acc = 0;
    s->pcm_sum_l = s->pcm_sum_r = s->pcm_n = s->pcm_last = 0;
    s->sample_pos = 0;
    s->poll_ok = false;
    s->ev_head = s->ev_tail = 0;
}
