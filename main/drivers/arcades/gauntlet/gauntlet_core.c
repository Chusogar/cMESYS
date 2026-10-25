// gauntlet_core.c - Gauntlet (Atari, 1985). See gauntlet_core.h.
// C port of dsp-cpp's drivers/arcade/gauntlet.cpp (gauntlet_hw.pas), with
// MAME's atari/gauntlet.cpp as the reference for the details dsp-cpp
// simplifies: address mirrors, the motion-object shadow pen (pen 1 darkens
// the playfield), the playfield flip bit, SLIP motion objects, the 6502 IRQ
// every 64 lines (32, 96, 160, 224) cleared by its acknowledge, the two LS259
// latches (sound CPU reset, YM2151 reset, TMS5220 strobes) and the mixer.
#include "gauntlet_core.h"

#include <stdlib.h>
#include <string.h>

#include "m68k/m68k.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define GT_HOT IRAM_ATTR
#else
#define GT_HOT
#endif

#ifdef GT_TRACE
extern void gt_trace(gt_core *c, char cpu, uint32_t a, uint16_t v);
#define TRACE(c, cpu, a, v) gt_trace(c, cpu, a, v)
#else
#define TRACE(c, cpu, a, v) ((void)0)
#endif

static gt_core *g_core;
// the sound program's polling loop (see sound_idle)
static const uint16_t poll_addr[12] = {0x4104, 0x4106, 0x4108, 0x410a, 0x410d, 0x410f,
                                       0x4112, 0x4115, 0x4127, 0x412a, 0x412d, 0x413f};
bool gt_ipl_dirty;

// ---------------------------------------------------------------------------
// 68010 interrupts (IRQ4 vblank, IRQ6 sound response)
// ---------------------------------------------------------------------------
static void apply_ipl(gt_core *c)
{
    int level = 0;
    for (int l = 7; l >= 1; --l)
        if (c->irq[l] != LINE_CLEAR) { level = l; break; }
    m68k_set_irq((unsigned)level);
}

static void main_irq(gt_core *c, int level, uint8_t state)
{
    if (c->irq[level] == state) return;
    c->irq[level] = state;
    // Inside the 68010 (a memory handler) Musashi would take it in the middle
    // of the instruction: applied before the next one instead.
    if (c->in_main) gt_ipl_dirty = true;
    else apply_ipl(c);
}

int cpu_irq_ack(int level)
{
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}

// Busy loop of the program, fast-forwarded exactly (see idle_check).
static void idle_check(gt_core *c, uint32_t pc);

void GT_HOT cpu_instr_callback(unsigned int pc)
{
    gt_core *c = g_core;
    if (gt_ipl_dirty) {
        gt_ipl_dirty = false;
        apply_ipl(c);
        pc = m68k_get_reg(NULL, M68K_REG_PC);
    }
    if (c->idle_skip) idle_check(c, pc);
}

void cpu_pulse_reset(void) {}
void cpu_set_fc(int fc) { (void)fc; }

// ---------------------------------------------------------------------------
// Sound board
// ---------------------------------------------------------------------------
static void sync_pokey(gt_core *c)
{
    if (c->pokey_pending > 0) {
        pokey_run(&c->pk, c->pokey_pending);
        c->pokey_pending = 0;
    }
}

static inline void flush_tms(gt_core *c)
{
    if (c->tms_pending) {
        tms5220_tick(&c->tms, c->tms_pending);
        c->tms_pending = 0;
    }
}

static inline void ym_recompute_due(gt_core *c)
{
    int32_t d = INT32_MAX;
    if (c->ym.timer_a_enabled && c->ym.timer_a_counter < d) d = c->ym.timer_a_counter;
    if (c->ym.timer_b_enabled && c->ym.timer_b_counter < d) d = c->ym.timer_b_counter;
    c->ym_due = d;
}

static inline void flush_ym(gt_core *c)
{
    if (c->ym_pending) {
        ym2151_run_timers(&c->ym, c->ym_pending);
        c->ym_pending = 0;
    }
    ym_recompute_due(c);
}

static void ym_reset(gt_core *c)
{
    flush_ym(c);
    ym2151_reset(&c->ym);
    ym_recompute_due(c);
    if (c->ym_reset_cb) c->ym_reset_cb(c->user);
}

// 6502 LS259 (write_d7): Q0 YM2151 reset (low = reset), Q1 TMS5220 /WS,
// Q2 /RS, Q3 speech squeak, Q4/Q5 coin counters.
static void soundctl_w(gt_core *c, int bit, bool state)
{
    uint8_t old = c->soundctl;
    c->soundctl = (uint8_t)(state ? (old | (1 << bit)) : (old & ~(1 << bit)));
    if (old == c->soundctl) return;
    switch (bit) {
    case 0: if (!state) ym_reset(c); break;
    case 1: flush_tms(c); tms5220_set_wsq(&c->tms, state); break;
    case 2: flush_tms(c); tms5220_set_rsq(&c->tms, state); break;
    case 3: flush_tms(c); tms5220_set_clock(&c->tms, GT_ATARI_CLOCK / 2 / (uint32_t)(16 - (5 | (state ? 2 : 0)))); break;
    default: break;
    }
}

// 68010 LS259 Q7: sound CPU reset line (0 = held in reset)
static void sound_reset_w(gt_core *c, bool state)
{
    if (state == !c->sound_halted) return;
    if (state) {
        c->sound_halted = false;
        dsp6502_reset(&c->snd);
        c->snd.idle_pc = poll_addr[0];
        c->poll_i = 0;
    } else {
        c->sound_halted = true;
        for (int b = 0; b < 8; b++) soundctl_w(c, b, false);   // LS259 clear
        c->mainlatch_full = false;
        main_irq(c, 6, LINE_CLEAR);
        c->mixer = 0;
    }
}

static inline uint8_t GT_HOT sound_read(void *u, uint16_t a)
{
    gt_core *c = (gt_core *)u;
    if (a >= 0x4000) return c->roms.sound[a - 0x4000];
    a &= (uint16_t)~0x2000;
    if (a < 0x1000) return c->sound_ram[a];
    const uint16_t r = (uint16_t)((a & 0x1800) | (a & 0x3f));
    if (r < 0x1800) {
        switch (r & 0x30) {
        case 0x10:                                  // command from the 68010
            c->soundlatch_full = false;
            dsp6502_set_nmi(&c->snd, LINE_CLEAR);
            return c->soundlatch;
        case 0x20: return c->coins;
        case 0x30: {
            uint8_t v = 0x30;
            if (c->soundlatch_full) v ^= 0x80;
            if (c->mainlatch_full) v ^= 0x40;
            flush_tms(c);
            if (!tms5220_readyq(&c->tms)) v ^= 0x20;
            if (c->service) v ^= 0x10;
            return v;
        }
        default: return 0xff;
        }
    }
    switch (r & 0x30) {
    case 0x00: sync_pokey(c); return pokey_read(&c->pk, a & 0x0f);
    case 0x10: return ym2151_status(&c->ym);
    case 0x30: dsp6502_set_irq(&c->snd, LINE_CLEAR); return 0xff;   // IRQ acknowledge
    default: return 0xff;
    }
}

static inline void GT_HOT sound_write(void *u, uint16_t a, uint8_t v)
{
    gt_core *c = (gt_core *)u;
    if (a >= 0x4000) return;
    a &= (uint16_t)~0x2000;
    if (a < 0x1000) { c->sound_ram[a] = v; return; }
    const uint16_t r = (uint16_t)((a & 0x1800) | (a & 0x3f));
    if (r < 0x1800) {
        switch (r & 0x30) {
        case 0x00:                                  // response to the 68010
            TRACE(c, 'S', a, v);
            c->mainlatch = v;
            c->mainlatch_full = true;
            main_irq(c, 6, LINE_ASSERT);
            return;
        case 0x20: c->mixer = v; return;
        case 0x30: soundctl_w(c, a & 7, (v & 0x80) != 0); return;
        default: return;
        }
    }
    switch (r & 0x30) {
    case 0x00: sync_pokey(c); pokey_write(&c->pk, a & 0x0f, v); return;
    case 0x10:
        if (!(a & 1)) { ym2151_select(&c->ym, v); return; }
        if (c->ym_write) c->ym_write(c->user, c->ym.last_register, v, c->sample_pos);
        flush_ym(c);
        ym2151_write(&c->ym, v);
        ym_recompute_due(c);
        return;
    case 0x20:                                      // TMS5220 data
        flush_tms(c);
        tms5220_set_data_latch(&c->tms, v);
        tms5220_write_data(&c->tms, v);
        return;
    case 0x30: dsp6502_set_irq(&c->snd, LINE_CLEAR); return;
    }
}

int gt_ym_gain(const gt_core *c)
{
    // MAME: mixer (d & 7) / 7 on a 0.48 route; POKEY 0.32, speech 0.80. The
    // chips here come out about twice as loud as MAME's: 3/4 at full volume
    // keeps the speech from clipping (about 1.5 x MAME's level).
    return (c->mixer & 7) * 192 / 7;
}

static inline void GT_HOT on_sound_cycles(void *u, int cycles)
{
    gt_core *c = (gt_core *)u;
    c->ym_pending += cycles * 2;                   // the YM2151 runs at twice the 6502 clock
    if (c->ym_pending >= c->ym_due) flush_ym(c);
    c->pokey_pending += cycles;
    if (c->tms.clock != c->tms_table_clock) {
        c->tms_table_clock = c->tms.clock;
        for (int n = 0; n < 16; ++n)
            c->tms_clocks[n] = (uint16_t)(((uint64_t)n * c->tms_table_clock + (GT_SOUND_CLOCK / 2)) / GT_SOUND_CLOCK);
    }
    c->tms_pending += cycles < 16 ? c->tms_clocks[cycles]
        : (int)(((int64_t)cycles * (int64_t)c->tms.clock + (GT_SOUND_CLOCK / 2)) / GT_SOUND_CLOCK);
    if (c->tms.io_ready_delay > 0) flush_tms(c);
    c->audio_acc += cycles * (int32_t)c->sample_rate;
    if (c->audio_acc < (int32_t)GT_SOUND_CLOCK) return;
    sync_pokey(c);
    flush_tms(c);
    // route gains relative to the YM2151's 0.48: POKEY 0.32 -> 2/3; speech
    // 0.80 would be 5/3, but this TMS5220 comes out hotter than MAME's (it
    // would clip): 1 puts it at MAME's level against the other two.
    const int pg = ((c->mixer >> 3) & 3) * 2 * 192 / (3 * 3);
    const int tg = ((c->mixer >> 5) & 7) * 192 / 7;
    while (c->audio_acc >= (int32_t)GT_SOUND_CLOCK) {
        c->audio_acc -= GT_SOUND_CLOCK;
        int32_t s = (pokey_update(&c->pk) * pg + tms5220_last_sample(&c->tms) * tg) >> 8;
        if (!c->ym_external) s += (ym2151_update(&c->ym) * gt_ym_gain(c)) >> 8;
        if (s < -32768) s = -32768; else if (s > 32767) s = 32767;
        if (c->audio && c->audio_n < c->audio_cap) c->audio[c->audio_n++] = (int16_t)s;
        c->sample_pos++;
    }
}

// The sound program's main loop polls for work 12 instructions at a time:
//   4104 LDA $02 / AND #$FE / STA $02 / BIT $1030 / BVS (68010 owes a read)
//   / LDY $224 / CPY $225 / BEQ 4127 / ... 4127 LDX $210 / CPX $211 / BEQ 413F
//   / ... 413F JMP $4104
// Most of the 6502's time goes round it. The instructions of that path are
// carried out here without decoding them: the same reads, writes, registers,
// flags and cycles, in the same order, so they can stop anywhere (the 6502
// runs a quarter of a line at a time). They stop before an instruction that
// would leave the path (a command to send, a sound to start); the 6502 core
// takes over from there. Interrupts are taken by the core between calls.
static const uint8_t poll_cycles[12] = {3, 2, 3, 4, 2, 4, 4, 3, 4, 4, 3, 3};

static int GT_HOT sound_idle(void *u, int budget)
{
    gt_core *c = (gt_core *)u;
    dsp6502 *p = &c->snd;
    int i = c->poll_i;
    // entered only where the previous call stopped, or from the real path
    if (!c->idle_skip || p->pc != poll_addr[i] || p->ppc != poll_addr[(i + 11) % 12]) {
        p->idle_pc = poll_addr[0];
        c->poll_i = 0;
        return 0;
    }
    uint8_t *ram = c->sound_ram;
    int done = 0;
    while (done < budget) {
        switch (i) {
        case 0: p->a = ram[0x02]; p->fz = p->a == 0; p->fn = (p->a & 0x80) != 0; break;
        case 1: p->a &= 0xfe; p->fz = p->a == 0; p->fn = (p->a & 0x80) != 0; break;
        case 2: ram[0x02] = p->a; break;
        case 3: {
            const uint8_t v = sound_read(c, 0x1030);
            p->fn = (v & 0x80) != 0; p->fv = (v & 0x40) != 0; p->fz = (p->a & v) == 0;
            break;
        }
        case 4: if (p->fv) goto leave; break;                       // BVS taken
        case 5: p->y = ram[0x224]; p->fz = p->y == 0; p->fn = (p->y & 0x80) != 0; break;
        case 6: case 9: {
            const uint8_t r = i == 6 ? p->y : p->x, m = ram[i == 6 ? 0x225 : 0x211];
            const uint8_t d = (uint8_t)(r - m);
            p->fc = r >= m; p->fz = d == 0; p->fn = (d & 0x80) != 0;
            break;
        }
        case 7: case 10: if (!p->fz) goto leave; break;            // BEQ not taken
        case 8: p->x = ram[0x210]; p->fz = p->x == 0; p->fn = (p->x & 0x80) != 0; break;
        default: break;                                            // JMP $4104
        }
        on_sound_cycles(c, poll_cycles[i]);
        done += poll_cycles[i];
        i = i == 11 ? 0 : i + 1;
    }
leave:
    p->pc = poll_addr[i];
    p->ppc = poll_addr[(i + 11) % 12];
    p->idle_pc = poll_addr[i];
    c->poll_i = (uint8_t)i;
    c->snd_skipped += (uint32_t)done;
    return done;
}

#define DSP6502_READ(u, a) sound_read(u, a)
#define DSP6502_WRITE(u, a, v) sound_write(u, a, v)
#define DSP6502_CYCLES(u, n) on_sound_cycles(u, n)
#include "dsp6502.c"

static void ym_irq(void *u, bool on)
{
    gt_core *c = (gt_core *)u;
    (void)c; (void)on;           // the YM2151 IRQ is not connected on Gauntlet
}

// ---------------------------------------------------------------------------
// 68010 memory map (MAME main_map, with its mirrors)
// ---------------------------------------------------------------------------
static inline uint16_t image_word(const gt_core *c, uint32_t off)
{
    const uint8_t *p = c->main_page[off >> 10] + (off & 0x3fe);
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint16_t slap_word(const gt_core *c, uint32_t r)
{
    return image_word(c, 0x38000 + (uint32_t)(c->rom_bank & 3) * 0x2000 + (r & 0x1ffe));
}

static uint16_t GT_HOT main_read(gt_core *c, uint32_t a)
{
    a &= 0xfffffe;
    if (a < 0x800000) {
        uint32_t r = a & ~0x280000u;
        if (r < 0x38000 || (r >= 0x40000 && r < 0x80000)) return image_word(c, r);
        if (r < 0x40000) {                          // SLAPSTIC
            uint16_t v = slap_word(c, r);
            c->rom_bank = slapstic_tweak(&c->slap, (uint16_t)((r & 0x7fff) >> 1));
            return v;
        }
        return 0xffff;
    }
    if (!(a & 0x100000)) {
        uint32_t r = a & ~0x2fc000u;                // $800000-$803FFF
        if (r < 0x802000) return c->ram[(r & 0x1fff) >> 1];
        if (r < 0x803000) return (uint16_t)(0xff00 | c->eeprom[(r & 0x3ff) >> 1]);
        uint32_t io = r & 0x10f;
        switch (io) {
        case 0x000: case 0x002: case 0x004: case 0x006: return c->in[io >> 1];
        case 0x008: {
            uint16_t v = c->service ? 0 : 0x08;
            if (c->mainlatch_full) v |= 0x10;
            if (c->soundlatch_full) v |= 0x20;
            if (c->line < GT_SCREEN_H) v |= 0x40;   // vblank, active low
            return v;
        }
        case 0x00e:                                 // ($80300F) response from the 6502
            c->mainlatch_full = false;
            main_irq(c, 6, LINE_CLEAR);
            return (uint16_t)(0xff00 | c->mainlatch);
        default: return 0xffff;
        }
    }
    uint32_t r = a & ~0x2c8000u;                    // $900000-$937FFF
    if (r >= 0x900000 && r < 0x902000) return c->v.pf[(r & 0x1fff) >> 1];
    if (r >= 0x902000 && r < 0x904000) return c->v.mo[(r & 0x1fff) >> 1];
    if (r >= 0x904000 && r < 0x905000) return c->ram2[(r & 0xfff) >> 1];
    if (r >= 0x905000 && r < 0x906000) return c->v.alpha[(r & 0xfff) >> 1];
    if ((a & ~0x2cf800u) >= 0x910000 && (a & ~0x2cf800u) < 0x910800) return c->v.palette[(a & 0x7ff) >> 1];
    return 0xffff;
}

static inline void combine(uint16_t *p, uint16_t v, uint16_t mask) { *p = (uint16_t)((*p & ~mask) | (v & mask)); }

static void GT_HOT main_write(gt_core *c, uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xfffffe;
    if (a >= 0x800000 && !(a & 0x100000)) TRACE(c, 'M', a, v);
    if (a < 0x800000) {
        uint32_t r = a & ~0x280000u;
        if (r >= 0x38000 && r < 0x40000) c->rom_bank = slapstic_tweak(&c->slap, (uint16_t)((r & 0x7fff) >> 1));
        return;
    }
    if (!(a & 0x100000)) {
        uint32_t r = a & ~0x2fc000u;
        if (r < 0x802000) { combine(&c->ram[(r & 0x1fff) >> 1], v, mask); return; }
        if (r < 0x803000) {                         // 2804 EEPROM, low byte, one write per unlock
            if ((mask & 0x00ff) && c->eeprom_unlocked) {
                uint8_t *e = &c->eeprom[(r & 0x3ff) >> 1];
                if (*e != (uint8_t)v) c->eeprom_dirty = true;
                *e = (uint8_t)v;
                c->eeprom_unlocked = false;
            }
            return;
        }
        uint32_t io = r & 0x17f;
        if (io >= 0x100 && io < 0x110) return;      // watchdog
        if (io >= 0x120 && io < 0x130) {            // LS259, data bit 0 of the low byte
            if (!(mask & 0x00ff)) return;
            int bit = (int)((io & 0xf) >> 1);
            bool st = (v & 1) != 0;
            c->outlatch = (uint8_t)(st ? (c->outlatch | (1 << bit)) : (c->outlatch & ~(1 << bit)));
            if (bit == 7) sound_reset_w(c, st);
            return;
        }
        if (io >= 0x140 && io < 0x150) { main_irq(c, 4, LINE_CLEAR); return; }
        if (io >= 0x150 && io < 0x160) { c->eeprom_unlocked = true; return; }
        if (io >= 0x170 && io < 0x180) {            // command to the 6502 ($803171)
            if (!(mask & 0x00ff)) return;
            c->soundlatch = (uint8_t)v;
            c->soundlatch_full = true;
            dsp6502_set_nmi(&c->snd, LINE_ASSERT);
            return;
        }
        return;
    }
    uint32_t r = a & ~0x2c8000u;
    if (r >= 0x900000 && r < 0x902000) { combine(&c->v.pf[(r & 0x1fff) >> 1], v, mask); return; }
    if (r >= 0x902000 && r < 0x904000) { combine(&c->v.mo[(r & 0x1fff) >> 1], v, mask); return; }
    if (r >= 0x904000 && r < 0x905000) { combine(&c->ram2[(r & 0xfff) >> 1], v, mask); return; }
    if (r >= 0x905000 && r < 0x906000) { combine(&c->v.alpha[(r & 0xfff) >> 1], v, mask); return; }
    uint32_t p = a & ~0x2cf800u;
    if (p >= 0x910000 && p < 0x910800) { combine(&c->v.palette[(a & 0x7ff) >> 1], v, mask); return; }
    if ((a & ~0x2cfffeu) == 0x930000) { combine(&c->xscroll, v, mask); return; }
}

unsigned int GT_HOT cpu_read_word(unsigned int a) { return main_read(g_core, a); }
unsigned int GT_HOT cpu_read_byte(unsigned int a)
{
    uint16_t w = main_read(g_core, a);
    return (a & 1) ? (w & 0xff) : (w >> 8);
}
unsigned int GT_HOT cpu_read_long(unsigned int a)
{
    uint32_t hi = main_read(g_core, a);
    return (hi << 16) | main_read(g_core, a + 2);
}
void GT_HOT cpu_write_word(unsigned int a, unsigned int d) { main_write(g_core, a, (uint16_t)d, 0xffff); }
void GT_HOT cpu_write_byte(unsigned int a, unsigned int d)
{
    if (a & 1) main_write(g_core, a, (uint16_t)(d & 0xff), 0x00ff);
    else main_write(g_core, a, (uint16_t)((d & 0xff) << 8), 0xff00);
}
void GT_HOT cpu_write_long(unsigned int a, unsigned int d)
{
    main_write(g_core, a, (uint16_t)(d >> 16), 0xffff);
    main_write(g_core, a + 2, (uint16_t)d, 0xffff);
}
void cpu_write_long_pd(unsigned int a, unsigned int d)
{
    main_write(g_core, a + 2, (uint16_t)d, 0xffff);
    main_write(g_core, a, (uint16_t)(d >> 16), 0xffff);
}

// Opcode / extension fetches: the SLAPSTIC kludge needs the fetch address.
static inline bool plain_rom(uint32_t a)
{
    uint32_t r = a & 0xffffff;
    if (r >= 0x800000) return false;
    r &= ~0x280000u;
    return r < 0x38000 || (r >= 0x40000 && r < 0x80000);
}
unsigned int GT_HOT m68k_read_immediate_16(unsigned int a)
{
    gt_core *c = g_core;
    if (plain_rom(a)) return image_word(c, (a & ~0x280000u) & 0x7fffe);
    c->fetching = true;
    c->fetch_pc = a;
    unsigned int v = main_read(c, a);
    c->fetching = false;
    return v;
}
unsigned int GT_HOT m68k_read_immediate_32(unsigned int a)
{
    return (m68k_read_immediate_16(a) << 16) | m68k_read_immediate_16(a + 2);
}
unsigned int m68k_read_pcrelative_8(unsigned int a) { return cpu_read_byte(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return cpu_read_word(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return cpu_read_long(a); }

static uint16_t peek_word(const gt_core *c, uint32_t a)
{
    a &= 0xfffffe;
    if (a < 0x800000) {
        uint32_t r = a & ~0x280000u;
        if (r < 0x38000 || (r >= 0x40000 && r < 0x80000)) return image_word(c, r);
        if (r < 0x40000) return slap_word(c, r);
        return 0xffff;
    }
    uint32_t r = a & ~0x2fc000u;
    if (!(a & 0x100000) && r < 0x802000) return c->ram[(r & 0x1fff) >> 1];
    return 0xffff;
}
unsigned int cpu_read_word_dasm(unsigned int a) { return peek_word(g_core, a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return ((uint32_t)peek_word(g_core, a) << 16) | peek_word(g_core, a + 2); }

static uint32_t s_pc(void *u)
{
    gt_core *c = (gt_core *)u;
    return c->fetching ? c->fetch_pc : m68k_get_reg(NULL, M68K_REG_PC);
}
static uint32_t s_ppc(void *u) { (void)u; return m68k_get_reg(NULL, M68K_REG_PPC); }
static uint16_t s_peek(void *u, uint32_t a) { return peek_word((gt_core *)u, a); }
static uint32_t s_areg(void *u, int n) { (void)u; return m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + n)); }

// ---------------------------------------------------------------------------
// Idle loop
// ---------------------------------------------------------------------------
// Busy loops of the program, fast-forwarded exactly. Interrupts only arrive
// between m68k_execute() calls (a quarter of a line), so a whole number of
// iterations that ends inside the slice changes nothing but the loop's own
// state:
//   $0047F0  subq.l #1,D0 / bne $47F0              (delay, EEPROM set-up)  D0 -= n
//   $040C22  tst.w (A2) / bne $40C2A / clr.w (A3) / bra $40C22
//            (A2 = $904002: waits for the vblank interrupt; nothing changes)
// The cycles of one iteration are measured from two hits in one slice.
static void GT_HOT idle_check(gt_core *c, uint32_t pc)
{
    int k;
    if (pc == 0x40c22) k = 1;
    else if (pc == 0x47f0) k = 0;
    else return;
    int rem = m68k_cycles_remaining();
    if (c->site[k].slice == c->slice && c->site[k].cost == 0) c->site[k].cost = c->site[k].rem - rem;
    c->site[k].slice = c->slice;
    c->site[k].rem = rem;
    const int cost = c->site[k].cost;
    if (cost <= 0) return;
    uint32_t n = (uint32_t)((rem - 1) / cost);
    if (n == 0) return;
    if (k == 0) {
        uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
        if (d0 < 2) return;
        if (n > d0 - 1) n = d0 - 1;
        m68k_set_reg(M68K_REG_D0, d0 - n);
    } else {
        if (m68k_get_reg(NULL, M68K_REG_A2) != 0x904002 || c->ram2[1] != 0) return;
        // only after one real pass (back from the BRA): the stack word is cleared
        if (m68k_get_reg(NULL, M68K_REG_PPC) != 0x40c28) return;
    }
    m68k_modify_timeslice(-(int)(n * (uint32_t)cost));
    c->site[k].rem = rem - (int)(n * (uint32_t)cost);
    c->skipped += n * (uint32_t)cost;
    c->slice_skipped += (int32_t)(n * (uint32_t)cost);
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------
void gt_init(gt_core *c, const gt_roms *roms, uint32_t sample_rate)
{
    memset(c, 0, sizeof *c);
    g_core = c;
    c->roms = *roms;
    for (int i = 0; i < 0x80000 / 1024; i++) c->main_page[i] = roms->main + (size_t)i * 1024;
    c->sample_rate = sample_rate ? sample_rate : 44100;

    dsp6502_init(&c->snd);
    c->snd.read = sound_read;
    c->snd.write = sound_write;
    c->snd.cycles = on_sound_cycles;
    c->snd.user = c;
    c->snd.idle = sound_idle;                // the polling loop of the sound program
    c->snd.idle_pc = poll_addr[0];
    for (int page = 0x40; page < 0x100; ++page) dsp6502_set_page(&c->snd, (uint8_t)page, roms->sound + (page - 0x40) * 0x100, NULL);
    for (int page = 0; page < 0x10; ++page) {
        dsp6502_set_page(&c->snd, (uint8_t)page, c->sound_ram + page * 0x100, c->sound_ram + page * 0x100);
        dsp6502_set_page(&c->snd, (uint8_t)(0x20 + page), c->sound_ram + page * 0x100, c->sound_ram + page * 0x100);
    }
    c->ym.irq_handler = ym_irq;
    c->ym.user = c;
    ym2151_init(&c->ym, GT_YM_CLOCK, c->sample_rate);
    pokey_init(&c->pk);
    tms5220_init(&c->tms, GT_ATARI_CLOCK / 2 / 11, true);

    slapstic_cpu sc = {s_pc, s_ppc, s_peek, s_areg, c};
    slapstic_init(&c->slap, 104, &sc);
    memset(c->eeprom, 0xff, sizeof c->eeprom);

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68010);
    gt_reset(c);
}

void gt_reset(gt_core *c)
{
    g_core = c;
    slapstic_reset(&c->slap);
    c->rom_bank = c->slap.current_bank;
    memset(c->irq, 0, sizeof c->irq);
    gt_ipl_dirty = false;
    c->in_main = false;
    m68k_pulse_reset();
    m68k_set_irq(0);
    dsp6502_reset(&c->snd);
    c->snd.idle_pc = poll_addr[0];
    c->poll_i = 0;
    ym2151_reset(&c->ym);
    pokey_reset(&c->pk);
    tms5220_reset(&c->tms);
    c->pokey_pending = c->tms_pending = 0;
    c->ym_pending = 0;
    ym_recompute_due(c);
    for (int i = 0; i < 4; i++) c->in[i] = 0xffff;
    c->coins = 0xff;
    c->xscroll = 0;
    c->outlatch = 0;
    c->soundctl = 0;
    c->mixer = 0;
    c->sound_halted = true;                 // LS259 Q7 low: the 6502 waits for the 68010
    c->soundlatch_full = c->mainlatch_full = false;
    c->eeprom_unlocked = false;
    c->line = 0;
    c->m68k_debt = 0;
    c->audio_acc = 0;
    c->sample_pos = 0;
    tms5220_set_wsq(&c->tms, false);
    tms5220_set_rsq(&c->tms, false);
}

void gt_set_inputs(gt_core *c, const gt_inputs *in)
{
    for (int i = 0; i < 4; i++) {
        uint16_t v = 0xffff;
        if (in->p[i].magic) v &= (uint16_t)~0x0001;
        if (in->p[i].fire) v &= (uint16_t)~0x0002;
        if (in->p[i].right) v &= (uint16_t)~0x0010;
        if (in->p[i].left) v &= (uint16_t)~0x0020;
        if (in->p[i].down) v &= (uint16_t)~0x0040;
        if (in->p[i].up) v &= (uint16_t)~0x0080;
        c->in[i] = v;
    }
    uint8_t coins = 0xff;
    if (in->coin[0]) coins &= (uint8_t)~0x08;
    if (in->coin[1]) coins &= (uint8_t)~0x04;
    if (in->coin[2]) coins &= (uint8_t)~0x02;
    if (in->coin[3]) coins &= (uint8_t)~0x01;
    c->coins = coins;
    c->service = in->service;
}

void gt_run_frame(gt_core *c)
{
    // 456 68010 cycles a line (pixel clock = CPU clock), 114 of the 6502;
    // the line is run in 4 slices of each, as dsp-cpp.
    enum { SYNC = 4 };
    const int main_cycles = GT_HTOTAL / SYNC;
    int snd_acc = 0;
    g_core = c;
    c->audio_n = 0;
    for (c->line = 0; c->line < GT_SCANLINES; c->line++) {
        const int line = c->line;
        if ((line & 63) == 32) dsp6502_set_irq(&c->snd, LINE_ASSERT);   // MAME scanline timer (param & 32)
        if (line == GT_SCREEN_H) {
            if (c->on_frame) c->on_frame(c->user, c);
            main_irq(c, 4, LINE_ASSERT);
        }
        for (int s = 0; s < SYNC; s++) {
            c->in_main = true;
            c->slice++;
            // Musashi ends on an instruction boundary: the overshoot is owed by
            // the next slice. (Fast-forwarded cycles shrink the timeslice
            // instead of counting as run: added back.)
            const int run = main_cycles - c->m68k_debt;
            c->slice_skipped = 0;
            c->m68k_debt = run > 0 ? m68k_execute(run) + c->slice_skipped - run : -run;
            c->in_main = false;
            if (gt_ipl_dirty) {
                gt_ipl_dirty = false;
                apply_ipl(c);
            }
            snd_acc += GT_HTOTAL;                   // 6502 = CPU clock / 4
            int sc = snd_acc / (4 * SYNC);
            snd_acc -= sc * 4 * SYNC;
            if (!c->sound_halted) dsp6502_run(&c->snd, sc);
            else on_sound_cycles(c, sc);            // (time and audio go on)
        }
        if (line < GT_SCREEN_H) {
            c->v.xscroll[line] = c->xscroll;
            c->v.yscroll[line] = c->v.alpha[0x7b7];
        }
    }
    c->line = 0;
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------
void gt_pack_tiles(const uint8_t *rom, uint32_t *out)
{
    // gfx_8x8x4_planar on the inverted ROMs: planes at 3/4, 2/4, 1/4, 0 of the
    // region (MSB first), 8 bytes a tile
    for (unsigned at = 0; at < GT_TILES * 8; at++) {
        uint32_t row = 0;
        for (int k = 0; k < 4; k++) {
            uint8_t b = (uint8_t)~rom[(3 - k) * 0x10000 + at];
            for (int x = 0; x < 8; x++) row |= (uint32_t)((b >> (7 - x)) & 1) << (4 * x + (3 - k));
        }
        out[at] = row;
    }
}

static uint32_t char_pens[256];
static bool char_pens_ready;

static void make_char_pens(void)
{
    for (unsigned b = 0; b < 256; ++b) {
        uint32_t v = 0;
        for (int xx = 0; xx < 4; ++xx)
            v |= (uint32_t)((((b >> (7 - xx)) & 1) << 1) | ((b >> (3 - xx)) & 1)) << (8 * xx);
        char_pens[b] = v;
    }
    char_pens_ready = true;
}

static const atari_mo_config mo_config = {
    .tile_width = 8, .tile_height = 8, .bankcount = 1, .linked = true, .split = true,
    .maxperline = 0, .palettebase = 0x100,
    .link_entry = {0, 0, 0, 0x03ff},
    .code_entry = {{0x7fff, 0, 0, 0}, {0, 0, 0, 0}},
    .color_entry = {{0, 0x000f, 0, 0}, {0, 0, 0, 0}},
    .xpos_entry = {0, 0xff80, 0, 0},
    .ypos_entry = {0, 0, 0xff80, 0},
    .width_entry = {0, 0, 0x0038, 0},
    .height_entry = {0, 0, 0x0007, 0},
    .hflip_entry = {0, 0, 0x0040, 0},
    .slipheight = 8, .slipoffset = 1,
};

static int mo_code(void *u, int raw) { (void)u; return raw ^ 0x800; }   // codes inverted vs the tiles
static int mo_color(void *u, int index) { (void)u; return index; }

static void GT_HOT mo_tile(void *u, int code, int color, bool hflip, bool vflip, int x, int y, int gfx,
                           int priority, int cy0, int cy1)
{
    (void)gfx; (void)priority;
    gt_renderer *r = (gt_renderer *)u;
    const uint32_t *t = r->roms->tiles + (size_t)(code & (GT_TILES - 1)) * 8;
    int ya = y > cy0 ? y : cy0, yb = y + 7 < cy1 ? y + 7 : cy1;
    if (ya < r->band_y0) ya = r->band_y0;
    if (yb >= r->band_y1) yb = r->band_y1 - 1;
    for (int ty = ya; ty <= yb; ty++) {
        uint32_t row = t[vflip ? 7 - (ty - y) : (ty - y)];
        if (!row) continue;
        uint16_t *o = r->mobuf + (ty - r->band_y0) * GT_SCREEN_W;
        for (int col = 0; col < 8; col++) {
            const int tx = x + col;
            if ((unsigned)tx >= GT_SCREEN_W) continue;
            const int pen = (row >> (4 * (hflip ? 7 - col : col))) & 15;
            if (pen) o[tx] = (uint16_t)(color + pen);
        }
    }
}

void gt_renderer_init(gt_renderer *r)
{
    if (r->ready) return;
    if (!char_pens_ready) make_char_pens();
    atari_mo_init(&r->mo, &mo_config, GT_HTOTAL, GT_SCANLINES);
    if (!r->mobuf) r->mobuf = (uint16_t *)malloc(GT_SCREEN_W * 16 * sizeof(uint16_t));
    r->ready = true;
}

void GT_HOT gt_compose(gt_renderer *r, const gt_roms *roms, const gt_video *v, int y0, int y1, uint16_t *out)
{
    const int W = GT_SCREEN_W;
    if (!r->ready) gt_renderer_init(r);
    r->roms = roms;
    // Playfield: 64x64 tiles of 8x8 (512x512, column major), flip X = bit 15,
    // colour bank 1: palette $280 + colour * 16
    for (int y = y0; y < y1; y++) {
        uint16_t *dst = out + (size_t)(y - y0) * W;
        const uint16_t ysr = v->yscroll[y];
        const int bank = ysr & 3;
        const int py = (y + (ysr >> 7)) & 511;
        int px = v->xscroll[y] & 511;
        int x = 0;
        while (x < W) {
            const uint16_t d = v->pf[((px >> 3) & 63) * 64 + (py >> 3)];
            const uint32_t code = ((uint32_t)(bank * 0x1000 + (d & 0xfff)) ^ 0x800) & (GT_TILES - 1);
            uint32_t row = roms->tiles[code * 8 + (py & 7)];
            if (d & 0x8000) {   // flip X: reverse the 8 nibbles
                row = ((row >> 4) & 0x0f0f0f0fu) | ((row & 0x0f0f0f0fu) << 4);
                row = ((row >> 8) & 0x00ff00ffu) | ((row & 0x00ff00ffu) << 8);
                row = (row >> 16) | (row << 16);
            }
            const unsigned base = 0x280 + ((d >> 12) & 7) * 16;
            const int first = px & 7;
            int count = 8 - first;
            if (count > W - x) count = W - x;
            row >>= 4 * first;
            for (int col = 0; col < count; col++, row >>= 4) dst[x + col] = (uint16_t)(base + (row & 15));
            x += count; px += count;
        }
    }

    // Motion objects into their own band buffer (later ones on top), runs of
    // lines with the same scroll drawn as MAME's partial updates would.
    memset(r->mobuf, 0xff, (size_t)(y1 - y0) * W * sizeof(uint16_t));
    r->band_y0 = y0;
    r->band_y1 = y1;
    const atari_mo_lookup lk = {mo_code, mo_color, NULL};
    for (int ya = y0; ya < y1;) {
        int yb = ya;
        while (yb + 1 < y1 && v->xscroll[yb + 1] == v->xscroll[ya] && v->yscroll[yb + 1] == v->yscroll[ya]) yb++;
        atari_mo_draw_slip(&r->mo, v->mo, &v->alpha[0x7c0], v->xscroll[ya] & 0x1ff, (v->yscroll[ya] >> 7) & 0x1ff,
                           ya, yb, &lk, mo_tile, r);
        ya = yb + 1;
    }
    // MO pen 1 clears playfield colour bit 0x80 (shadows), other pens replace it
    for (int i = 0; i < (y1 - y0) * W; i++) {
        const uint16_t m = r->mobuf[i];
        if (m == 0xffff) continue;
        if ((m & 0x0f) == 1) out[i] ^= 0x80;
        else out[i] = m;
    }

    // Alphanumerics on top: 64x31, opaque when bit 15 is set
    const uint8_t *chars = roms->chars;
    for (int y = y0; y < y1; y++) {
        uint16_t *line = out + (size_t)(y - y0) * W;
        const uint16_t *arow = v->alpha + (y >> 3) * 64;
        for (int cx = 0; cx < W / 8; cx++) {
            const uint16_t d = arow[cx];
            const bool opaque = (d & 0x8000) != 0;
            const uint8_t *rp = chars + (size_t)(d & 0x3ff) * 16 + (size_t)(y & 7) * 2;
            if (!opaque && rp[0] == 0 && rp[1] == 0) continue;
            const int base = (((d >> 10) & 0x0f) | ((d >> 9) & 0x20)) << 2;
            const uint32_t lo = char_pens[rp[0]], hi = char_pens[rp[1]];
            uint16_t *cell = line + cx * 8;
            for (int col = 0; col < 8; col++) {
                const uint8_t pen = (uint8_t)((col < 4 ? lo >> (8 * col) : hi >> (8 * (col - 4))) & 0xff);
                if (opaque || pen) cell[col] = (uint16_t)(base + pen);
            }
        }
    }
}

uint32_t gt_palette_rgb(const gt_video *v, int index)
{
    // IRGB 4444 as MAME's palette_device::IRGB_4444
    uint16_t val = v->palette[index & 0x3ff];
    uint32_t i = (uint32_t)(val >> 12) * 17;
    uint32_t r = (i * ((val >> 8) & 15) * 17) >> 8, g = (i * ((val >> 4) & 15) * 17) >> 8, b = (i * (val & 15) * 17) >> 8;
    return (r << 16) | (g << 8) | b;
}
