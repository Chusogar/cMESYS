// rv_core.c - Return of the Invaders machine. See rv_core.h.
//
// Main Z80:  0000-7FFF ROM (6000-7FFF empty socket), 8000-87FF fg RAM,
//            8800-9FFF shared RAM, A000-A7FF bg RAM, B800-B802 flip / banks,
//            C000 P1, C001 P2, C003 MCU flags, C004 system, C005-C007 DSW,
//            C800-C807 LS259, D000 watchdog, D800 sound latch,
//            E000 / E800 MCU data, F800 sound CPU's byte
// Sub Z80:   0000-7FFF ROM, 8000-A7FF the same RAM, C800-C807 LS259
// Sound Z80: 0000-1FFF ROM, 2000-27FF RAM, 4000 sound latch, 6000 byte to
//            the main CPU, 8000 / A000 SN76489A
#include "rv_core.h"
#include "rom_data.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define RV_HOT IRAM_ATTR
#else
#define RV_HOT
#endif

#define NMI_PERIOD (RV_CLOCK / 120)     // 25600 cycles

// ---------------------------------------------------------------------------
// LS259 (outputs: 0 main IRQ mask, 1 coin lockout, 2 /sound reset,
// 3 /MCU reset, 4 sub IRQ mask, 5 /sub reset)
// ---------------------------------------------------------------------------
static void sub_line(rv_core *c)
{
    if (c->sub_irq) z80_assert_irq(&c->sub, 0xff); else z80_clr_irq(&c->sub);
}

static void q_changed(rv_core *c, int bit, bool v, int64_t now)
{
    switch (bit) {
    case 0:
        if (!v) { c->main_irq = false; z80_clr_irq(&c->main); }
        break;
    case 2:     // sound CPU reset (held while 0, reset on release)
        if (v) {
            z80_reset(&c->snd);
            c->snd_poll_ok = false;
            if (c->snd_cyc < now) c->snd_cyc = now;
        }
        break;
    case 3:
        taito68705_reset_w(&c->mcu, !v);
        if (v && c->mcu_cyc4 < now) c->mcu_cyc4 = now;
        break;
    case 4:
        if (!v) { c->sub_irq = false; sub_line(c); }
        break;
    case 5:
        if (v) {
            z80_reset(&c->sub);
            if (c->sub_cyc < now) c->sub_cyc = now;
        }
        break;
    }
}

static void latch_w(rv_core *c, uint16_t a, uint8_t v, int64_t now)
{
    const int bit = a & 7;
    const bool d = v & 1;
    if (((c->latch259 >> bit) & 1) == d) return;
    c->latch259 = (uint8_t)((c->latch259 & ~(1 << bit)) | (d << bit));
    q_changed(c, bit, d, now);
}

static inline bool snd_running(const rv_core *c) { return c->latch259 & 0x04; }
static inline bool mcu_running(const rv_core *c) { return c->latch259 & 0x08; }
static inline bool sub_running(const rv_core *c) { return c->latch259 & 0x20; }

// ---------------------------------------------------------------------------
// Shared RAM 8000-A7FF
// ---------------------------------------------------------------------------
static inline uint8_t *ram_ptr(rv_core *c, uint16_t a)
{
    if (a < 0x8800) return &c->fg_vram[a - 0x8000];
    if (a < 0xa000) return &c->shared[a - 0x8800];
    return &c->bg_vram[a - 0xa000];
}

// ---------------------------------------------------------------------------
// Main CPU
// ---------------------------------------------------------------------------
static uint8_t RV_HOT main_read(void *u, uint16_t a)
{
    rv_core *c = (rv_core *)u;
    if (a < 0x6000) return rv_main_rom[a];
    if (a < 0x8000) return 0x00;
    if (a < 0xa800) return *ram_ptr(c, a);
    switch (a) {
    case 0xc000: return c->in_p1;
    case 0xc001: return c->in_p2;
    case 0xc003:
        return (uint8_t)((taito68705_host_semaphore(&c->mcu) ? 0x00 : 0x10) |
                         (taito68705_mcu_semaphore(&c->mcu) ? 0x20 : 0x00));
    case 0xc004: return c->in_sys;
    case 0xc005: return c->dsw[0];
    case 0xc006: return c->dsw[1];
    case 0xc007: return c->dsw[2];
    case 0xe000: return taito68705_data_r(&c->mcu);
    case 0xf800: return c->snd_m6000;
    default: return 0x00;
    }
}

static void RV_HOT main_write(void *u, uint16_t a, uint8_t v)
{
    rv_core *c = (rv_core *)u;
    if (a >= 0x8000 && a < 0xa800) { *ram_ptr(c, a) = v; return; }
    switch (a) {
    case 0xb800: c->flip = v & 1; break;
    case 0xb801: c->fg_bank = v & 1; break;
    case 0xb802: c->bg_bank = v & 1; break;
    case 0xc800: case 0xc801: case 0xc802: case 0xc803:
    case 0xc804: case 0xc805: case 0xc806: case 0xc807:
        latch_w(c, a, v, c->main_cyc);
        break;
    case 0xd800:
        c->soundlatch = v;
        c->soundlatch_pending = true;
        z80_assert_irq(&c->snd, 0xff);
        break;
    case 0xe800: taito68705_data_w(&c->mcu, v); break;
    default: break;     // 7FFF coin counter, D000 watchdog
    }
}

// ---------------------------------------------------------------------------
// Sub CPU
// ---------------------------------------------------------------------------
static uint8_t RV_HOT sub_read(void *u, uint16_t a)
{
    rv_core *c = (rv_core *)u;
    if (a < 0x2000) return rv_sub_rom[a];
    if (a >= 0x8000 && a < 0xa800) return *ram_ptr(c, a);
    return 0x00;
}

static void RV_HOT sub_write(void *u, uint16_t a, uint8_t v)
{
    rv_core *c = (rv_core *)u;
    if (a >= 0x8000 && a < 0xa800) *ram_ptr(c, a) = v;
    else if ((a & 0xfff8) == 0xc800) latch_w(c, a, v, c->sub_cyc);
}

// ---------------------------------------------------------------------------
// Sound CPU
// ---------------------------------------------------------------------------
static uint8_t RV_HOT snd_read(void *u, uint16_t a)
{
    rv_core *c = (rv_core *)u;
    if (a < 0x2000) return rv_sound_rom[a];
    if (a < 0x2800) return c->snd_ram[a - 0x2000];
    if (a == 0x4000) {
        c->soundlatch_pending = false;
        z80_clr_irq(&c->snd);
        return c->soundlatch;
    }
    return 0x00;
}

static void RV_HOT snd_write(void *u, uint16_t a, uint8_t v)
{
    rv_core *c = (rv_core *)u;
    if (a >= 0x2000 && a < 0x2800) c->snd_ram[a - 0x2000] = v;
    else if (a == 0x6000) c->snd_m6000 = v;
    else if (a == 0x8000) sn76489_write(&c->sn[0], v);
    else if (a == 0xa000) sn76489_write(&c->sn[1], v);
}

static uint8_t port_in(z80 *z, uint16_t p) { (void)z; (void)p; return 0x00; }
static void port_out(z80 *z, uint16_t p, uint8_t v) { (void)z; (void)p; (void)v; }

// sound time passes: output samples
static inline void snd_cycles(rv_core *c, int n)
{
    c->audio_acc += (uint32_t)n * c->sample_rate;
    while (c->audio_acc >= RV_CLOCK) {
        c->audio_acc -= RV_CLOCK;
        int16_t s = 0;
        sn76489_update(&c->sn[0], &s, 1);
        sn76489_update(&c->sn[1], &s, 1);
        // x3.5: MAME's level (2 x SN76489A at 0.80), measured on its output
        int32_t v = (int32_t)s * 7 / 2;
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        if (c->audio && c->audio_n < c->audio_cap) c->audio[c->audio_n++] = (int16_t)v;
    }
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------
// Idle skipping: a halted Z80 that cannot take an interrupt before t only
// repeats its NOP cycle (4 cycles, R + 1): those cycles are counted in one go.
static inline bool halt_idle(const z80 *z)
{
    return z->halted && !z->nmi_pending && !(z->irq_pending && z->iff1) && !z->iff_delay;
}

static inline int64_t halt_skip(rv_core *c, z80 *z, int64_t cyc, int64_t t)
{
    const int64_t n = (t - cyc + 3) / 4;
    z->r = (uint8_t)((z->r & 0x80) | ((z->r + n) & 0x7f));
    c->skipped += (uint32_t)(n * 4);
    return cyc + n * 4;
}

static void RV_HOT run_main(rv_core *c, int64_t t)
{
    while (c->main_cyc < t) {
        if (c->idle_skip && halt_idle(&c->main)) { c->main_cyc = halt_skip(c, &c->main, c->main_cyc, t); break; }
#ifdef RV_PROF
        { extern void rv_prof(int cpu, uint16_t pc); rv_prof(0, c->main.pc); }
#endif
        c->main_cyc += z80_step(&c->main);
    }
}

static void RV_HOT run_sub(rv_core *c, int64_t t)
{
    if (!sub_running(c)) { if (c->sub_cyc < t) c->sub_cyc = t; return; }
    while (c->sub_cyc < t) {
        if (c->idle_skip && halt_idle(&c->sub)) { c->sub_cyc = halt_skip(c, &c->sub, c->sub_cyc, t); break; }
#ifdef RV_PROF
        { extern void rv_prof(int cpu, uint16_t pc); rv_prof(1, c->sub.pc); }
#endif
        c->sub_cyc += z80_step(&c->sub);
    }
}

// The sound program waits for its NMI / IRQ handlers to set a flag:
//   012C LD A,($2714) / AND A / JR Z,$012C      (13 + 4 + 12 cycles, R + 3)
// After one real pass (A = 0, flags set), while the flag stays 0 and no
// interrupt can be taken (none arrives inside a run_snd step), each pass
// changes only R and the time: whole passes are counted in one go.
#define SND_POLL_PC   0x012c
#define SND_POLL_CYC  29

static inline bool snd_poll_idle(const rv_core *c)
{
    const z80 *z = &c->snd;
    return z->pc == SND_POLL_PC && c->snd_poll_ok && z->a == 0 && c->snd_ram[0x714] == 0 &&
           !z->nmi_pending && !(z->irq_pending && z->iff1) && !z->iff_delay;
}

static void RV_HOT run_snd(rv_core *c, int64_t t)
{
    while (c->snd_cyc < t) {
        if (c->snd_cyc >= c->next_nmi) {
            if (snd_running(c)) z80_pulse_nmi(&c->snd);
            c->next_nmi += NMI_PERIOD;
        }
        int64_t stop = t < c->next_nmi ? t : c->next_nmi;
        if (!snd_running(c)) {
            snd_cycles(c, (int)(stop - c->snd_cyc));
            c->snd_cyc = stop;
            continue;
        }
        z80 *z = &c->snd;
        while (c->snd_cyc < stop) {
            if (c->idle_skip) {
                int64_t n = 0;
                if (halt_idle(z)) {
                    n = (stop - c->snd_cyc + 3) / 4;
                    z->r = (uint8_t)((z->r & 0x80) | ((z->r + n) & 0x7f));
                    n *= 4;
                } else if (snd_poll_idle(c)) {
                    const int64_t k = (stop - c->snd_cyc) / SND_POLL_CYC;
                    z->r = (uint8_t)((z->r & 0x80) | ((z->r + 3 * k) & 0x7f));
                    n = k * SND_POLL_CYC;
                }
                if (n > 0) {
                    c->snd_cyc += n;
                    c->skipped += (uint32_t)n;
                    while (n > 0) { const int k = n > 4096 ? 4096 : (int)n; snd_cycles(c, k); n -= k; }
                    continue;
                }
            }
#ifdef RV_PROF
            { extern void rv_prof(int cpu, uint16_t pc); rv_prof(2, z->pc); }
#endif
            const uint16_t pc = z->pc;
            const int n = (int)z80_step(z);
            c->snd_cyc += n;
            snd_cycles(c, n);
            c->snd_poll_ok = pc == 0x0130 && z->pc == SND_POLL_PC;   // a real pass ended
        }
    }
}

static void RV_HOT run_mcu(rv_core *c, int64_t t)
{
    // MCU cycles = CPU cycles / 4: run whole MCU cycles up to t
    const int64_t want = (t - c->mcu_cyc4) / 4;
    if (want <= 0) return;
    if (mcu_running(c)) taito68705_run(&c->mcu, (int)want);
    c->mcu_cyc4 += want * 4;
}

// The MCU's main loop waits for its /INT handler to set RAM $18:
//   057D NOP / TST $18 / BEQ $057D      (2 + 6 + 4 cycles)
static bool mcu_idle(void *u, const m6805 *m)
{
    const taito68705 *t = (const taito68705 *)u;
    return t->ram[0x18 - 0x10] == 0 && (m->cc & 0x06) == 0x02;     // Z set, N clear: as after TST
}

static int64_t frame_time(uint64_t f)
{
    return (int64_t)((f * (uint64_t)RV_CLOCK * 100) / 6058);
}

void RV_HOT rv_run_frame(rv_core *c)
{
    const int64_t start = c->frame_end;
    const int64_t end = frame_time(c->frame + 1);
    c->mcu.cpu.idle_ok = c->idle_skip ? mcu_idle : NULL;
    for (int s = 1; s <= RV_SLICES; s++) {
        const int64_t t = start + (end - start) * s / RV_SLICES;
        run_main(c, t);
        run_sub(c, t);
        run_snd(c, t);
        run_mcu(c, t);
    }
    c->frame++;
    c->frame_end = end;
    // vblank: the screen is drawn, then the IRQs (if enabled)
    if (c->on_frame) c->on_frame(c->user, c);
    if (c->latch259 & 0x01) { c->main_irq = true; z80_assert_irq(&c->main, 0xff); }
    if ((c->latch259 & 0x10) && sub_running(c)) { c->sub_irq = true; sub_line(c); }
}

void rv_set_inputs(rv_core *c, const rv_inputs *in)
{
    uint8_t s = 0xcf;
    if (in->start1) s &= (uint8_t)~0x01;
    if (in->start2) s &= (uint8_t)~0x02;
    if (in->tilt) s &= (uint8_t)~0x04;
    if (in->service) s &= (uint8_t)~0x08;
    if (in->coin1) s |= 0x10;
    if (in->coin2) s |= 0x20;
    c->in_sys = s;
    uint8_t p = 0xff;
    if (in->left) p &= (uint8_t)~0x02;
    if (in->right) p &= (uint8_t)~0x08;
    if (in->fire) p &= (uint8_t)~0x80;
    c->in_p1 = p;
    p = 0xff;
    if (in->left2) p &= (uint8_t)~0x02;
    if (in->right2) p &= (uint8_t)~0x08;
    if (in->fire2) p &= (uint8_t)~0x80;
    c->in_p2 = p;
}

void rv_get_video(const rv_core *c, rv_video *v)
{
    memcpy(v->fg, c->fg_vram, sizeof v->fg);
    memcpy(v->bg, c->bg_vram, sizeof v->bg);
    memcpy(v->spr[0], c->shared + 0x0780, 0x80);
    memcpy(v->spr[1], c->shared + 0x0f80, 0x80);
    memcpy(v->spr[2], c->shared + 0x1780, 0x80);
    v->fg_bank = c->fg_bank;
    v->bg_bank = c->bg_bank;
    v->flip = c->flip;
}

static void setup_z80(z80 *z, rv_core *c, uint8_t (*rd)(void *, uint16_t), void (*wr)(void *, uint16_t, uint8_t))
{
    z80_init(z);
    z->read_byte = rd;
    z->write_byte = wr;
    z->port_in = port_in;
    z->port_out = port_out;
    z->userdata = c;
}

void rv_init(rv_core *c, uint32_t sample_rate)
{
    memset(c, 0, sizeof *c);
    c->sample_rate = sample_rate ? sample_rate : 44100;
    setup_z80(&c->main, c, main_read, main_write);
    setup_z80(&c->sub, c, sub_read, sub_write);
    setup_z80(&c->snd, c, snd_read, snd_write);
    taito68705_init(&c->mcu, rv_mcu_rom);
    c->mcu.cpu.idle_pc = 0x057d;
    c->mcu.cpu.idle_pass = 12;
    c->dsw[0] = 0x6f;       // MAME defaults: bonus 30k/80k/80k, 3 lives, upright
    c->dsw[1] = 0x00;       // 1 coin 1 credit
    c->dsw[2] = 0xff;
    rv_reset(c);
}

void rv_reset(rv_core *c)
{
    z80_reset(&c->main);
    z80_reset(&c->sub);
    z80_reset(&c->snd);
    z80_clr_irq(&c->main);
    z80_clr_irq(&c->sub);
    z80_clr_irq(&c->snd);
    taito68705_reset(&c->mcu);
    sn76489_init(&c->sn[0], RV_CLOCK, (int)c->sample_rate);
    sn76489_init(&c->sn[1], RV_CLOCK, (int)c->sample_rate);
    memset(c->fg_vram, 0, sizeof c->fg_vram);
    memset(c->shared, 0, sizeof c->shared);
    memset(c->bg_vram, 0, sizeof c->bg_vram);
    memset(c->snd_ram, 0, sizeof c->snd_ram);
    // LS259 cleared: sub CPU, sound CPU and MCU held in reset, IRQs masked
    c->latch259 = 0;
    taito68705_reset_w(&c->mcu, true);
    c->soundlatch = c->snd_m6000 = 0;
    c->soundlatch_pending = false;
    c->snd_poll_ok = false;
    c->main_irq = c->sub_irq = false;
    c->fg_bank = c->bg_bank = 0;
    c->flip = false;
    c->main_cyc = c->sub_cyc = c->snd_cyc = c->mcu_cyc4 = 0;
    c->frame = 0;
    c->frame_end = 0;
    c->next_nmi = NMI_PERIOD;
    c->audio_acc = 0;
    rv_inputs none = {0};
    rv_set_inputs(c, &none);
}
