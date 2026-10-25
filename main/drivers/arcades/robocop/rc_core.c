// rc_core.c - RoboCop machine: 68000 bus, HuC6280 + mailbox, I/O, frame.
// See rc_core.h.
#include "rc_core.h"

#include <stdlib.h>
#include <string.h>

#include "m68k/m68k.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define RC_HOT IRAM_ATTR
#else
#define RC_HOT
#endif

static rc_core *g_core;
bool rc_ipl_dirty;

static inline void combine(uint16_t *p, uint16_t v, uint16_t mask) { *p = (uint16_t)((*p & ~mask) | (v & mask)); }

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
static inline int64_t now(const rc_core *c)
{
    return c->slice_start + (c->in_main ? m68k_cycles_run() + c->slice_skipped : 0);
}

// ---------------------------------------------------------------------------
// 68000 interrupts: IRQ6 vblank (held until acknowledged at 30C018), IRQ4
// from the mailbox (INTL)
// ---------------------------------------------------------------------------
static void apply_ipl(rc_core *c)
{
    m68k_set_irq(c->irq6 ? 6 : c->intl ? 4 : 0);
}

static void ipl_changed(rc_core *c)
{
    // Inside the 68000 (a memory handler) Musashi would take it in the middle
    // of the instruction: applied before the next one instead.
    if (c->in_main) rc_ipl_dirty = true;
    else apply_ipl(c);
}

int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }
void cpu_pulse_reset(void) {}
void cpu_set_fc(int fc) { (void)fc; }

static void idle_check(rc_core *c, uint32_t pc);

void RC_HOT cpu_instr_callback(unsigned int pc)
{
    rc_core *c = g_core;
    if (rc_ipl_dirty) {
        rc_ipl_dirty = false;
        apply_ipl(c);
        pc = m68k_get_reg(NULL, M68K_REG_PC);
    }
    if (c->idle_skip) idle_check(c, pc);
}

// ---------------------------------------------------------------------------
// HuC6280 and the MB8421 mailbox (left: 68000, right: HuC6280). A write to
// 7FF raises INTR (HuC6280 IRQ1), the right side reading 7FF drops it; a
// write to 7FE raises INTL (68000 IRQ4), the left side reading 7FE drops it.
// The HuC6280 only talks to the mailbox, so it is caught up with the 68000
// whenever the 68000 touches it (and at the end of every slice).
// ---------------------------------------------------------------------------
static void mcu_catch_up(rc_core *c, int64_t t)
{
    // 21.477272 MHz / 16 against 10 MHz
    const int64_t target = t * 21477272 / 160000000;
    const int64_t owed = target - (int64_t)c->mcu.total;
    if (owed > 0) h6280_run(&c->mcu, (int)owed);
}

static void set_intr(rc_core *c, bool on)
{
    c->intr = on;
    h6280_set_irq_line(&c->mcu, H6280_IRQ1, on ? H6280_ASSERT : H6280_CLEAR);
}

static void set_intl(rc_core *c, bool on)
{
    if (c->intl == on) return;
    c->intl = on;
    ipl_changed(c);
}

static uint8_t mcu_read(void *u, uint32_t a)
{
    rc_core *c = (rc_core *)u;
    if (a < 0x10000) return (a >= 0x1e00 && a < 0x2000) ? c->roms.mcu[a - 0x1e00] : 0;
    if (a >= 0x1f2000 && a < 0x1f2800) {
        const uint32_t o = a & 0x7ff;
        if (o == 0x7ff) set_intr(c, false);
        return c->dpram[o];
    }
    return 0;
}

static void mcu_write(void *u, uint32_t a, uint8_t v)
{
    rc_core *c = (rc_core *)u;
    if (a >= 0x1f2000 && a < 0x1f2800) {
        const uint32_t o = a & 0x7ff;
        c->dpram[o] = v;
        if (o == 0x7fe) set_intl(c, true);
    }
}

// ---------------------------------------------------------------------------
// 68000 bus (MAME dec0_map + robocop main_map; unmapped reads: 0)
// ---------------------------------------------------------------------------
static inline uint16_t rom16(const rc_core *c, uint32_t a)
{
    const uint32_t w = *(const volatile uint32_t *)(c->main_page[(a >> 10) & 0xff] + (a & 0x3fc));
    const uint32_t h = (a & 2) ? w >> 16 : w;
    return (uint16_t)(((h & 0xff) << 8) | ((h >> 8) & 0xff));
}

static uint16_t RC_HOT main_read(rc_core *c, uint32_t a, uint16_t mask)
{
    a &= 0xfffffe;
    if (a < 0x40000) return rom16(c, a);
    if (a >= 0xff8000) {
        if (a < 0xffc000) return c->ram[(a >> 1) & 0x1fff];
        if (a < 0xffc800) return c->spriteram[(a >> 1) & 0x3ff];
        return 0;
    }
    if (a < 0x180000) return 0;
    if (a < 0x181000) {
        if (!(mask & 0x00ff)) return 0;                     // only D0-D7 are wired
        mcu_catch_up(c, now(c));
        const uint32_t o = (a >> 1) & 0x7ff;
        if (o == 0x7fe) set_intl(c, false);
        return c->dpram[o];
    }
    if (a >= 0x240000 && a < 0x250000) {
        if (a >= 0x242000 && a < 0x242080) return c->pf[0].colscroll[(a >> 1) & 0x3f];
        if (a >= 0x242400 && a < 0x242800) return c->pf[0].rowscroll[(a >> 1) & 0x1ff];
        if (a >= 0x242800 && a < 0x244000) return c->ram1[(a - 0x242800) >> 1];
        if (a >= 0x244000 && a < 0x246000) return c->text_data[(a >> 1) & 0xfff];
        if (a >= 0x248000 && a < 0x248080) return c->pf[1].colscroll[(a >> 1) & 0x3f];
        if (a >= 0x248400 && a < 0x248800) return c->pf[1].rowscroll[(a >> 1) & 0x1ff];
        if (a >= 0x24a000 && a < 0x24a800) return c->pf1_data[(a >> 1) & 0x3ff];
        if (a >= 0x24c800 && a < 0x24c880) return c->pf[2].colscroll[(a >> 1) & 0x3f];
        if (a >= 0x24cc00 && a < 0x24d000) return c->pf[2].rowscroll[(a >> 1) & 0x1ff];
        if (a >= 0x24d000 && a < 0x24d800) return c->pf2_data[(a >> 1) & 0x3ff];
        return 0;                                           // (control registers: write only)
    }
    if (a == 0x300000 || a == 0x300008) return 0xffff;     // rotary inputs (none)
    if (a >= 0x30c000 && a < 0x30c00c) {
        switch (a & 0xe) {
        case 0: return c->in_players;
        case 2: return (uint16_t)(c->in_system | (c->vblank ? 0x80 : 0));
        case 4: return c->dsw;
        case 8: return 0;                                   // i8751 (not on this board)
        default: return 0xffff;
        }
    }
    if (a >= 0x310000 && a < 0x310800) return c->palette[(a >> 1) & 0x3ff];
    if (a >= 0x314000 && a < 0x314800) return c->palette_ext[(a >> 1) & 0x3ff];
    if (a >= 0x318000 && a < 0x31c000) return c->ram[(a >> 1) & 0x1fff];
    if (a >= 0x31c000 && a < 0x31c800) return c->spriteram[(a >> 1) & 0x3ff];
    return 0;
}

static void bac06_ctrl_w(rc_bac06 *p, uint32_t a, uint16_t v, uint16_t mask)
{
    if ((a & 0x18) == 0x00) combine(&p->ctrl0[(a >> 1) & 3], v, mask);
    else if ((a & 0x18) == 0x10) combine(&p->ctrl1[(a >> 1) & 3], v, mask);
}

static void RC_HOT main_write(rc_core *c, uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xfffffe;
    if (a < 0x60000) return;
    if (a >= 0xff8000) {
        if (a < 0xffc000) combine(&c->ram[(a >> 1) & 0x1fff], v, mask);
        else if (a < 0xffc800) combine(&c->spriteram[(a >> 1) & 0x3ff], v, mask);
        return;
    }
    if (a >= 0x180000 && a < 0x181000) {
        if (!(mask & 0x00ff)) return;
        mcu_catch_up(c, now(c));
        const uint32_t o = (a >> 1) & 0x7ff;
        c->dpram[o] = (uint8_t)v;
        if (o == 0x7ff) set_intr(c, true);
        return;
    }
    if (a >= 0x240000 && a < 0x250000) {
        if (a < 0x240020) { bac06_ctrl_w(&c->pf[0], a, v, mask); return; }
        if (a >= 0x242000 && a < 0x242080) { combine(&c->pf[0].colscroll[(a >> 1) & 0x3f], v, mask); return; }
        if (a >= 0x242400 && a < 0x242800) { combine(&c->pf[0].rowscroll[(a >> 1) & 0x1ff], v, mask); return; }
        if (a >= 0x242800 && a < 0x244000) { combine(&c->ram1[(a - 0x242800) >> 1], v, mask); return; }
        if (a >= 0x244000 && a < 0x246000) { combine(&c->text_data[(a >> 1) & 0xfff], v, mask); return; }
        if (a >= 0x246000 && a < 0x246020) { bac06_ctrl_w(&c->pf[1], a, v, mask); return; }
        if (a >= 0x248000 && a < 0x248080) { combine(&c->pf[1].colscroll[(a >> 1) & 0x3f], v, mask); return; }
        if (a >= 0x248400 && a < 0x248800) { combine(&c->pf[1].rowscroll[(a >> 1) & 0x1ff], v, mask); return; }
        if (a >= 0x24a000 && a < 0x24a800) { combine(&c->pf1_data[(a >> 1) & 0x3ff], v, mask); return; }
        if (a >= 0x24c000 && a < 0x24c020) { bac06_ctrl_w(&c->pf[2], a, v, mask); return; }
        if (a >= 0x24c800 && a < 0x24c880) { combine(&c->pf[2].colscroll[(a >> 1) & 0x3f], v, mask); return; }
        if (a >= 0x24cc00 && a < 0x24d000) { combine(&c->pf[2].rowscroll[(a >> 1) & 0x1ff], v, mask); return; }
        if (a >= 0x24d000 && a < 0x24d800) { combine(&c->pf2_data[(a >> 1) & 0x3ff], v, mask); return; }
        return;
    }
    if (a >= 0x30c010 && a < 0x30c020) {
        switch (a & 0xe) {
        case 0x0: combine(&c->pri, v, mask); break;                         // priority
        case 0x2: memcpy(c->spritebuf, c->spriteram, sizeof c->spritebuf); break;   // sprite DMA
        case 0x4:                                                           // sound latch
            if (mask & 0x00ff) {
                // With sound_async the board runs on another core. A full queue
                // must not busy-spin: that freezes the 68000 before the first
                // vblank callback and leaves the CYD on a black screen after
                // the setup menu. Yield so the audio task can drain events.
                while (!rc_sound_latch(c->sound, now(c), (uint8_t)v)) {
                    if (!c->sound_async) rc_sound_run(c->sound, c->time);
#ifdef ESP_PLATFORM
                    else vTaskDelay(1);
#endif
                }
            }
            break;
        case 0x8: if (c->irq6) { c->irq6 = false; ipl_changed(c); } break;   // vblank IRQ ack
        default: break;
        }
        return;
    }
    if (a >= 0x310000 && a < 0x310800) { combine(&c->palette[(a >> 1) & 0x3ff], v, mask); c->palette_dirty = true; return; }
    if (a >= 0x314000 && a < 0x314800) { combine(&c->palette_ext[(a >> 1) & 0x3ff], v, mask); c->palette_dirty = true; return; }
    if (a >= 0x318000 && a < 0x31c000) { combine(&c->ram[(a >> 1) & 0x1fff], v, mask); return; }
    if (a >= 0x31c000 && a < 0x31c800) { combine(&c->spriteram[(a >> 1) & 0x3ff], v, mask); return; }
}

unsigned int RC_HOT cpu_read_byte(unsigned int a)
{
    if (a & 1) return main_read(g_core, a, 0x00ff) & 0xff;
    return main_read(g_core, a, 0xff00) >> 8;
}
unsigned int RC_HOT cpu_read_word(unsigned int a) { return main_read(g_core, a, 0xffff); }
// Long reads (the 68000's prefetch among them): one access for aligned program ROM
unsigned int RC_HOT cpu_read_long(unsigned int a)
{
    rc_core *c = g_core;
    const uint32_t m = a & 0xffffff;
    if (!(m & 3) && m < 0x40000)
        return __builtin_bswap32(*(const volatile uint32_t *)(c->main_page[m >> 10] + (m & 0x3fc)));
    return ((uint32_t)main_read(c, a, 0xffff) << 16) | main_read(c, a + 2, 0xffff);
}
unsigned int cpu_read_word_dasm(unsigned int a) { return cpu_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void RC_HOT cpu_write_byte(unsigned int a, unsigned int v)
{
    if (a & 1) main_write(g_core, a, (uint16_t)(v & 255), 0x00ff);
    else main_write(g_core, a, (uint16_t)((v & 255) << 8), 0xff00);
}
void RC_HOT cpu_write_word(unsigned int a, unsigned int v) { main_write(g_core, a, (uint16_t)v, 0xffff); }
void RC_HOT cpu_write_long(unsigned int a, unsigned int v)
{
    main_write(g_core, a, (uint16_t)(v >> 16), 0xffff);
    main_write(g_core, a + 2, (uint16_t)v, 0xffff);
}
void RC_HOT cpu_write_long_pd(unsigned int a, unsigned int v)
{
    main_write(g_core, a + 2, (uint16_t)v, 0xffff);
    main_write(g_core, a, (uint16_t)(v >> 16), 0xffff);
}

// ---------------------------------------------------------------------------
// Busy loops of the 68000, fast-forwarded exactly. Both wait for the vblank
// interrupt to count $FF8004 up, counting their own passes in $FF8008:
//   17BA  addq.w #1,$ff8008 / cmpi.w #1,$ff8004 / bcs 17BA
//   17E8  addq.w #1,$ff8008 / cmpi.w #2,$ff8004 / bcs 17E8
// Interrupt levels only change between slices (or at a mailbox access, which
// the loop does not make), so until the slice ends $FF8004 cannot change:
// the remaining whole passes only take time and count. The cycles of a pass
// are measured from two hits in one slice.
// ---------------------------------------------------------------------------
static void RC_HOT idle_check(rc_core *c, uint32_t pc)
{
    int k;
    uint16_t limit;
    if (pc == 0x17ba) { k = 0; limit = 1; }
    else if (pc == 0x17e8) { k = 1; limit = 2; }
    else return;
    if (c->ram[0x0004 >> 1] >= limit) return;          // the loop ends on this pass
    const int rem = m68k_cycles_remaining();
    struct rc_idle_site *s = &c->site[k];
    if (s->slice == c->slice_no && s->cost == 0) s->cost = s->rem - rem;
    s->slice = c->slice_no;
    s->rem = rem;
    if (s->cost <= 0) return;
    const uint32_t n = (uint32_t)((rem - 1) / s->cost);
    if (!n) return;
    const int32_t cyc = (int32_t)(n * (uint32_t)s->cost);
    c->ram[0x0008 >> 1] = (uint16_t)(c->ram[0x0008 >> 1] + n);
    m68k_modify_timeslice(-cyc);
    s->rem = rem - cyc;
    c->slice_skipped += cyc;
    c->skipped += (uint32_t)cyc;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
static void run_slice(rc_core *c, int cycles)
{
    c->slice_start = c->time;
    c->in_main = true;
    c->slice_no++;
    c->slice_skipped = 0;
    const int run = cycles - c->debt;
    // Musashi ends on an instruction boundary: the overshoot is owed by the
    // next slice (fast-forwarded cycles shrink the timeslice: added back)
    c->debt = run > 0 ? m68k_execute(run) + c->slice_skipped - run : -run;
    c->in_main = false;
    if (rc_ipl_dirty) {
        rc_ipl_dirty = false;
        apply_ipl(c);
    }
    c->time += cycles;
    mcu_catch_up(c, c->time);
    if (!c->sound_async) rc_sound_run(c->sound, c->time);
}

void rc_run_frame(rc_core *c)
{
    g_core = c;
    c->sound->idle_skip = c->idle_skip;
    for (c->line = 0; c->line < RC_LINES; c->line++) {
        if (c->line == RC_FIRST_LINE) c->vblank = false;
        if (c->line == RC_FIRST_LINE + RC_SCREEN_H) {
            if (c->on_frame) c->on_frame(c->user, c);
            c->frame++;
            c->vblank = true;
            c->irq6 = true;
            apply_ipl(c);
        }
        run_slice(c, RC_LINE_CYCLES);
    }
    c->line = 0;
}

void rc_get_video(const rc_core *c, rc_video *v)
{
    for (int i = 0; i < 3; i++) v->pf[i] = &c->pf[i];
    v->sprites = c->spritebuf;
    v->pri = c->pri;
    v->odd_frame = c->frame & 1;
}

void rc_set_inputs(rc_core *c, const rc_inputs *in)
{
    uint16_t p = 0xffff;
    for (int i = 0; i < 2; i++) {
        const rc_player *pl = &in->p[i];
        const int s = i * 8;
        if (pl->up) p &= (uint16_t)~(0x01 << s);
        if (pl->down) p &= (uint16_t)~(0x02 << s);
        if (pl->left) p &= (uint16_t)~(0x04 << s);
        if (pl->right) p &= (uint16_t)~(0x08 << s);
        if (pl->attack) p &= (uint16_t)~(0x10 << s);
        if (pl->jump) p &= (uint16_t)~(0x20 << s);
    }
    c->in_players = p;
    uint16_t s = 0x7f;                      // bits 0-1 unused (high), 7 vblank (added on read)
    if (in->start1) s &= (uint16_t)~0x04;
    if (in->start2) s &= (uint16_t)~0x08;
    if (in->coin1) s &= (uint16_t)~0x10;
    if (in->coin2) s &= (uint16_t)~0x20;
    if (in->service) s &= (uint16_t)~0x40;
    c->in_system = s;
}

void rc_init(rc_core *c, const rc_roms *roms, uint32_t sample_rate)
{
    rc_sound *snd = c->sound;
    memset(c, 0, sizeof *c);
    c->sound = snd;
    c->roms = *roms;
    c->sample_rate = sample_rate ? sample_rate : 44100;
    for (int i = 0; i < 256; i++) c->main_page[i] = roms->main + i * 1024;
    c->pf[0].data = c->text_data; c->pf[0].size = 0x1000;
    c->pf[1].data = c->pf1_data;  c->pf[1].size = 0x400;
    c->pf[2].data = c->pf2_data;  c->pf[2].size = 0x400;
    if (!c->sound) c->sound = (rc_sound *)malloc(sizeof(rc_sound));
    rc_sound_init(c->sound, roms->sound, roms->oki, 0x10000, c->sample_rate);
    c->sound->idle_skip = true;

    h6280_init(&c->mcu);
    c->mcu.read = mcu_read;
    c->mcu.write = mcu_write;
    c->mcu.user = c;
    h6280_set_bank(&c->mcu, 0xf8, c->mcu_ram, c->mcu_ram);     // 1F0000-1F1FFF

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    c->dsw = 0xff7f;
    rc_reset(c);
}

void rc_reset(rc_core *c)
{
    g_core = c;
    memset(c->ram, 0, sizeof c->ram);
    memset(c->ram1, 0, sizeof c->ram1);
    memset(c->spriteram, 0, sizeof c->spriteram);
    memset(c->spritebuf, 0, sizeof c->spritebuf);
    memset(c->palette, 0, sizeof c->palette);
    memset(c->palette_ext, 0, sizeof c->palette_ext);
    c->palette_dirty = true;
    for (int i = 0; i < 3; i++) {
        rc_bac06 *p = &c->pf[i];
        memset(p->ctrl0, 0, sizeof p->ctrl0);
        memset(p->ctrl1, 0, sizeof p->ctrl1);
        memset(p->colscroll, 0, sizeof p->colscroll);
        memset(p->rowscroll, 0, sizeof p->rowscroll);
        memset(p->data, 0, p->size * 2);
    }
    c->pri = 0;
    memset(c->mcu_ram, 0, sizeof c->mcu_ram);
    memset(c->dpram, 0, sizeof c->dpram);
    c->intl = c->intr = false;
    c->irq6 = false;
    c->debt = 0;
    c->vblank = false;
    c->time = c->slice_start = 0;
    c->frame = 0;
    c->in_players = 0xffff;
    c->in_system = 0x7f;
    rc_ipl_dirty = false;

    h6280_reset(&c->mcu);
    c->mcu.icount = 0;
    h6280_set_irq_line(&c->mcu, H6280_IRQ1, H6280_CLEAR);
    rc_sound_reset(c->sound);
    m68k_pulse_reset();
    apply_ipl(c);
}
