// pb_core.c - Puzzle Bobble (Taito B System) core. See pb_core.h.
// Memory maps, interrupts and the sound board as MAME's taito_b.cpp
// (pbobble_map, sound_map, machine config "pbobble").
#include "pb_core.h"
#include "rom_data.h"
#include "m68k/m68k.h"
#include "z80/z80.h"
#include "tc0140syt.h"
#include "eeprom93c46.h"
#include "ym2610.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define HOT IRAM_ATTR
#else
#define HOT
#endif

#define VBLANK_LINE   240
#define INTL_LINE     248         // INTH cleared / INTL raised 8 lines later
#define VBEND_LINE    16          // INTL cleared at the end of vblank

typedef struct {
    uint8_t ram[0x10000];          // 0x900000
    uint16_t pal[0x1000];          // 0x800000
    uint8_t sndram[0x2000];        // Z80 0xc000
    z80 cpu_snd;
    tc0140syt_t syt;
    eeprom93c46_t eep;
    ym2610_ctl_t ym;
    uint16_t eep_latch;
    uint16_t coin_word;
    uint8_t snd_bank;
    bool snd_reset_held;
    uint8_t irq_pending;           // bit 3 = IRQ3 (INTH), bit 5 = IRQ5 (INTL)
    // MB87078
    uint8_t mb_data, mb_control;
    uint16_t mb_latch[4];
    int gain;
    uint64_t zclock;               // Z80 cycles executed (before the current chunk)
    int32_t zchunk, zadv;          // cycles into the current chunk / YM side advanced to
    bool ym_touched;               // YM register written: its next event may have moved
    uint64_t mclock;               // 68000 cycles at the start of the current segment
    int64_t m68k_frac, z80_frac;   // fractional cycles (x 64)
    int32_t m68k_debt;             // 68000 cycles run past the previous segment
    pb_stats_t st;
} pb_t;

static pb_t *S;
static tc0180vcu_t *V;
vcu_frame_t pb_frame;
volatile pb_inputs_t pb_in = {0xff, 0xff, 0xff, 0xff, 0xff};
volatile uint32_t pb_palette_serial;
bool pb_idle_skip = true;

// 68000 program ROM through 1 KB pages (hot ones copied to RAM on the ESP32)
static const uint8_t *rom_page[512];
static const uint8_t *snd_rom = pb_snd_rom;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

// ---------------------------------------------------------------------------
// Interrupts (TC0180VCU INTH -> IRQ3, INTL -> IRQ5, HOLD_LINE)
// ---------------------------------------------------------------------------
static void irq_update(void)
{
    int lvl = (S->irq_pending & 0x20) ? 5 : (S->irq_pending & 0x08) ? 3 : 0;
    m68k_set_irq(lvl);
}

int cpu_irq_ack(int level)
{
    S->irq_pending &= (uint8_t)~(1 << level);
    irq_update();
    return M68K_INT_ACK_AUTOVECTOR;
}
void cpu_pulse_reset(void) {}
void cpu_set_fc(int fc) { (void)fc; }

// ---------------------------------------------------------------------------
// 68000 idle loop. Between interrupts the game spins at $1706:
//     $1706: JSR $2214 -> JSR $1326 ; BRA $1706
//     $1326: MOVE.W (-$5B16,A5),D0 ; BNE + ; MOVEQ #1,D0
//          + MULS.W #$B0,D0 ; DIVS.W #$7FED,D0 ; SWAP D0
//            MOVE.W D0,(-$5B16,A5) ; RTS
// i.e. it keeps stepping its random number generator, so the number of passes
// matters (it decides the bubble colours). Each pass costs the same number of
// cycles (learned from Musashi itself: two equal consecutive measurements),
// so whole passes that fit before the end of the slice are done natively with
// the exact result: seed, D0, flags and cycles as if executed.
// ---------------------------------------------------------------------------
#define IDLE_PC   0x1706
extern int m68ki_remaining_cycles;
static uint32_t exec_id, idle_exec;
static int32_t idle_last_run = -1, idle_cand, idle_cost;
static uint16_t idle_last_seed;

static inline uint32_t rng_step(uint16_t seed, uint16_t *out)
{
    int32_t s = (int16_t)seed;
    if (!s) s = 1;
    int32_t prod = s * 0xb0;                      // MULS.W #$B0
    int32_t q = prod / 0x7fed, r = prod % 0x7fed; // DIVS.W #$7FED (no overflow: |q| <= 176)
    uint32_t d0 = ((uint32_t)(uint16_t)q << 16) | (uint16_t)r;   // after SWAP
    *out = (uint16_t)r;
    return d0;
}

void HOT cpu_instr_callback(unsigned int pc)
{
    if (pc != IDLE_PC) return;
    uint32_t a = (m68k_get_reg(NULL, M68K_REG_A5) - 0x5b16) & 0xffffff;
    if (a < 0x900000 || a > 0x90fffe) return;
    uint8_t *sp = &S->ram[a - 0x900000];
    uint16_t seed = rd16(sp), chk;
    int32_t run = m68k_cycles_run();
    if (idle_exec == exec_id && idle_last_run >= 0) {
        rng_step(idle_last_seed, &chk);
        if (chk == seed) {
            int32_t c = run - idle_last_run;
            if (c == idle_cand) idle_cost = c; else idle_cand = c;
        }
    }
    if (idle_cost > 0 && pb_idle_skip) {
        int32_t k = m68ki_remaining_cycles / idle_cost - 1;
        if (k > 0) {
            uint32_t d0 = 0;
            for (int32_t i = 0; i < k; i++) d0 = rng_step(seed, &seed);
            wr16(sp, seed);
            m68k_set_reg(M68K_REG_D0, d0);
            // CCR as left by MOVE.W D0,(d16,A5): N Z from the word, V = C = 0, X kept
            uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
            sr = (sr & ~0x0fu) | ((seed & 0x8000) ? 8 : 0) | (seed ? 0 : 4);
            m68k_set_reg(M68K_REG_SR, sr);
            m68ki_remaining_cycles -= k * idle_cost;
            run += k * idle_cost;
            S->st.m68k_cycles_skipped += (uint32_t)(k * idle_cost);
        }
    }
    idle_last_run = run;
    idle_last_seed = seed;
    idle_exec = exec_id;
}

// ---------------------------------------------------------------------------
// Sound CPU side
// ---------------------------------------------------------------------------
static void syt_nmi(void *u, bool st) { (void)u; if (st) z80_pulse_nmi(&S->cpu_snd); }
static void syt_reset(void *u, bool st)
{
    (void)u;
    if (st && !S->snd_reset_held) z80_reset(&S->cpu_snd);
    S->snd_reset_held = st;
}
static void ym_irq(void *u, bool st)
{
    (void)u;
    if (st) z80_assert_irq(&S->cpu_snd, 0xff); else z80_clr_irq(&S->cpu_snd);
}
static void ym_reg(void *u, int part, uint8_t reg, uint8_t val)
{
    (void)u;
    pb_ym_write((uint32_t)(S->zclock + (uint64_t)S->zchunk), part, reg, val);
}

// Brings the YM2610 timers up to the instruction being executed.
static inline void ym_catchup(pb_t *s)
{
    int32_t d = s->zchunk - s->zadv;
    if (d > 0) { ym2610_ctl_advance(&s->ym, d); s->zadv = s->zchunk; }
}

static uint8_t HOT snd_read(void *u, uint16_t a)
{
    pb_t *s = u;
    if (a < 0x4000) return snd_rom[a];
    if (a < 0x8000) return snd_rom[s->snd_bank * 0x4000 + (a - 0x4000)];
    if (a >= 0xc000 && a < 0xe000) return s->sndram[a - 0xc000];
    if (a >= 0xe000 && a <= 0xe003) { ym_catchup(s); return ym2610_ctl_read(&s->ym, a & 3); }
    if (a == 0xe201) return tc0140syt_slave_comm_r(&s->syt);
    return 0;
}

static void HOT snd_write(void *u, uint16_t a, uint8_t v)
{
    pb_t *s = u;
    if (a >= 0xc000 && a < 0xe000) { s->sndram[a - 0xc000] = v; return; }
    if (a >= 0xe000 && a <= 0xe003) {
        ym_catchup(s);
        ym2610_ctl_write(&s->ym, a & 3, v);
        s->ym_touched = true;
        return;
    }
    if (a == 0xe200) { tc0140syt_slave_port_w(&s->syt, v); return; }
    if (a == 0xe201) { tc0140syt_slave_comm_w(&s->syt, v); return; }
    if (a == 0xf200) { s->snd_bank = v & 3; return; }
}

static uint8_t snd_in(z80 *z, uint16_t p) { (void)z; (void)p; return 0; }
static void snd_out(z80 *z, uint16_t p, uint8_t v) { (void)z; (void)p; (void)v; }

// Z80 main loop at $0152 (DI ; CALL $0068 ; CALL $0211 ; EI ; ... ; JP $0152):
// with no command from the 68000 (mailbox ports empty), its own queue empty
// ($D200 == $D201) and its last answer not yet read by the 68000 (status bit
// 2), a pass changes nothing, so whole passes are skipped while they fit
// before the next YM2610 timer event (the IRQ then arrives at the same
// instruction as without the skip). The pass length is measured once.
#define ZIDLE_PC 0x0152
static int32_t zidle_cost;

static bool z80_idle(pb_t *s)
{
    z80 *z = &s->cpu_snd;
    return z->pc == ZIDLE_PC && !z->irq_pending && !z->nmi_pending && !z->halted &&
           (s->syt.status & 0x07) == 0x04 && s->sndram[0x1200] == s->sndram[0x1201] &&
           s->snd_bank == 1;
}

static int32_t z80_measure_pass(pb_t *s)
{
    // one pass, instruction by instruction; valid if it comes back to the
    // start with the same state and without interrupts
    z80 *z = &s->cpu_snd;
    int32_t c0 = s->zchunk;
    do {
        s->zchunk += (int32_t)z80_step(z);
        if (s->zchunk - c0 > 2000 || z->irq_pending || z->nmi_pending || s->ym_touched) return -1;
    } while (z->pc != ZIDLE_PC);
    return z80_idle(s) ? s->zchunk - c0 : -1;
}

// Runs the Z80 until s->zchunk reaches n (or the YM2610 gets a nearer event
// after a register write).
static void HOT z80_run_to(pb_t *s, int32_t *n)
{
    z80 *z = &s->cpu_snd;
    while (s->zchunk < *n) {
        s->zchunk += (int32_t)z80_step(z);
        if (s->ym_touched) {
            s->ym_touched = false;
            int32_t e = s->zadv + ym2610_ctl_next_event(&s->ym);
            if (e < *n) *n = e;
        }
    }
}

static void HOT run_sound(pb_t *s, int32_t cycles)
{
    z80 *z = &s->cpu_snd;
    while (cycles > 0) {
        int32_t n = cycles;
        int32_t ev = ym2610_ctl_next_event(&s->ym);
        if (ev < n) n = ev;
        s->zchunk = s->zadv = 0;
        s->ym_touched = false;
        if (s->snd_reset_held) {
            s->zchunk = n;
        } else if (pb_idle_skip) {
            // run to the start of a pass, then skip the whole passes that fit
            while (z->pc != ZIDLE_PC && s->zchunk < n) {
                s->zchunk += (int32_t)z80_step(z);
                if (s->ym_touched) {
                    s->ym_touched = false;
                    int32_t e = s->zadv + ym2610_ctl_next_event(&s->ym);
                    if (e < n) n = e;
                }
            }
            if (s->zchunk < n && z80_idle(s)) {
                if (zidle_cost <= 0) {
                    int32_t c = z80_measure_pass(s);
                    if (c > 0) zidle_cost = c;
                } else {
                    int32_t k = (n - s->zchunk) / zidle_cost - 1;
                    if (k > 0) {
                        s->zchunk += k * zidle_cost;
                        s->st.z80_cycles_skipped += (uint32_t)(k * zidle_cost);
                    }
                }
            }
            if (s->zchunk < n) z80_run_to(s, &n);
        } else {
            z80_run_to(s, &n);
        }
        int32_t done = s->zchunk;
        ym2610_ctl_advance(&s->ym, done - s->zadv);
        s->zclock += (uint64_t)done;
        s->zchunk = s->zadv = 0;
        cycles -= done;
    }
}

// ---------------------------------------------------------------------------
// MB87078 (only channel 1 drives the YM2610B volume)
// ---------------------------------------------------------------------------
static void mb87078_recalc(pb_t *s)
{
    uint16_t l = s->mb_latch[1];
    int idx;
    if (!(l & 0x40)) idx = 65;
    else if (l & 0x100) idx = 64;
    else if (l & 0x80) idx = 0;
    else idx = ~l & 0x3f;
    // MAME: gain_percent = 100 * 10^(-0.5 idx / 20), passed on as data / 100
    int pct = idx >= 65 ? 0 : (int)(100.0 * pow(10.0, -0.5 * idx / 20.0));
    s->gain = pct * 256 / 100;
}

static void mb87078_w(pb_t *s, int offset, uint8_t data)
{
    if (offset & 1) { s->mb_control = data & 0x1f; return; }
    s->mb_data = data & 0x3f;
    s->mb_latch[s->mb_control & 3] = (uint16_t)(((s->mb_control << 4) & 0x1c0) | s->mb_data);
    mb87078_recalc(s);
}

// ---------------------------------------------------------------------------
// 68000 bus
// ---------------------------------------------------------------------------
static uint8_t fio_read(int off)
{
    switch (off) {
    case 0: return pb_in.service;
    case 1:
        S->eep.now = S->mclock + (uint64_t)m68k_cycles_run();
        return (uint8_t)((pb_in.coin & 0xfe) | eeprom93c46_do_read(&S->eep));
    case 2: return pb_in.start;
    case 3: return pb_in.p12a;
    case 7: return pb_in.p12b;
    default: return 0xff;
    }
}

static void eeprom_w(pb_t *s, uint8_t d)
{
    s->eep.now = s->mclock + (uint64_t)m68k_cycles_run();
    eeprom93c46_di_write(&s->eep, (d >> 2) & 1);
    eeprom93c46_clk_write(&s->eep, (d >> 3) & 1);
    eeprom93c46_cs_write(&s->eep, (d >> 4) & 1);
}

static void sync_sound(void);

static uint16_t HOT bus_read16(uint32_t a)
{
    pb_t *s = S;
    a &= 0xffffff;
    if (a < 0x80000) return rd16(&rom_page[a >> 10][a & 1023]);
    if (a >= 0x900000 && a < 0x910000) return rd16(&s->ram[a - 0x900000]);
    if (a >= 0x400000 && a < 0x480000) return tc0180vcu_read(V, a - 0x400000);
    if (a >= 0x800000 && a < 0x802000) return s->pal[(a - 0x800000) >> 1];
    if (a >= 0x500000 && a < 0x500010) return (uint16_t)(fio_read((a - 0x500000) >> 1) << 8);
    if (a == 0x500024 || a == 0x50002e) return 0x00ff;
    if (a == 0x500026) return s->eep_latch;
    if (a == 0x700002) { sync_sound(); return (uint16_t)(tc0140syt_master_comm_r(&s->syt) << 8); }
    return 0;
}

static void HOT bus_write16(uint32_t a, uint16_t v, uint16_t mask)
{
    pb_t *s = S;
    a &= 0xffffff;
    if (a >= 0x900000 && a < 0x910000) {
        uint8_t *p = &s->ram[a - 0x900000];
        if (mask & 0xff00) p[0] = (uint8_t)(v >> 8);
        if (mask & 0x00ff) p[1] = (uint8_t)v;
        return;
    }
    if (a >= 0x400000 && a < 0x480000) { tc0180vcu_write(V, a - 0x400000, v, mask); return; }
    if (a >= 0x800000 && a < 0x802000) {
        uint16_t *p = &s->pal[(a - 0x800000) >> 1];
        *p = (uint16_t)((*p & ~mask) | (v & mask));
        pb_palette_serial++;
        return;
    }
    if (a >= 0x500000 && a < 0x500010) {
        // TC0640FIO halfword_byteswap_w: the high byte goes to the chip
        // (register 0 = watchdog, 4 = coin counters / lockout)
        return;
    }
    if (a == 0x500026) {
        s->eep_latch = (uint16_t)((s->eep_latch & ~mask) | (v & mask));
        if (mask & 0xff00) eeprom_w(s, (uint8_t)(v >> 8));
        return;
    }
    if (a == 0x500028) { s->coin_word = (uint16_t)((s->coin_word & ~mask) | (v & mask)); return; }
    if (a == 0x600000 || a == 0x600002) {
        if (mask & 0xff00) mb87078_w(s, (a >> 1) & 1, (uint8_t)(v >> 8));
        return;
    }
    if (a == 0x700000) {
        if (mask & 0xff00) tc0140syt_master_port_w(&s->syt, (uint8_t)(v >> 8));
        return;
    }
    if (a == 0x700002) {
        if (mask & 0xff00) { sync_sound(); tc0140syt_master_comm_w(&s->syt, (uint8_t)(v >> 8)); }
        return;
    }
}

unsigned int HOT cpu_read_byte(unsigned int a) { uint16_t v = bus_read16(a & ~1u); return (a & 1) ? v & 255 : v >> 8; }
unsigned int HOT cpu_read_word(unsigned int a) { return bus_read16(a & ~1u); }
unsigned int HOT cpu_read_long(unsigned int a) { return ((uint32_t)bus_read16(a & ~1u) << 16) | bus_read16((a + 2) & ~1u); }
unsigned int cpu_read_word_dasm(unsigned int a) { return cpu_read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void HOT cpu_write_byte(unsigned int a, unsigned int v)
{
    if (a & 1) bus_write16(a & ~1u, (uint16_t)(v & 255), 0x00ff);
    else bus_write16(a, (uint16_t)((v & 255) << 8), 0xff00);
}
void HOT cpu_write_word(unsigned int a, unsigned int v) { bus_write16(a & ~1u, (uint16_t)v, 0xffff); }
void HOT cpu_write_long(unsigned int a, unsigned int v) { bus_write16(a & ~1u, (uint16_t)(v >> 16), 0xffff); bus_write16((a + 2) & ~1u, (uint16_t)v, 0xffff); }
void HOT cpu_write_long_pd(unsigned int a, unsigned int v) { bus_write16((a + 2) & ~1u, (uint16_t)v, 0xffff); bus_write16(a & ~1u, (uint16_t)(v >> 16), 0xffff); }

// ---------------------------------------------------------------------------
// Scheduling: the frame is run line by line (68000, then the Z80 up to the
// same time). A mailbox access by the 68000 first brings the Z80 up to the
// 68000's time within the line (MAME synchronizes there too).
// ---------------------------------------------------------------------------
// The frame is run in segments between the interrupt events (lines 16, 240,
// 248): the 68000 runs a whole segment, then the Z80 up to the same time. A
// mailbox access by the 68000 first brings the Z80 up to the 68000's time
// (MAME synchronizes there too).
#define M68K_PER_LINE_X64  ((int64_t)PB_CPU_CLOCK * 64 / (PB_FPS * PB_LINES))
#define Z80_PER_LINE_X64   ((int64_t)PB_SND_CLOCK * 64 / (PB_FPS * PB_LINES))

static int32_t seg_m68k_budget;       // 68000 cycles of the current segment
static int32_t seg_z80_target;        // Z80 cycles of the current segment
static int32_t seg_z80_done;

static void sync_sound(void)
{
    if (seg_m68k_budget <= 0) return;
    int32_t used = m68k_cycles_run();
    int32_t want = (int32_t)((int64_t)seg_z80_target * used / seg_m68k_budget);
    if (want > seg_z80_target) want = seg_z80_target;
    if (want > seg_z80_done) { run_sound(S, want - seg_z80_done); seg_z80_done = want; }
}

int pb_max_seg = 256;
static void HOT run_lines1(pb_t *s, int lines);
static void HOT run_lines(pb_t *s, int lines)
{
    while (lines > 0) { int n = lines > pb_max_seg ? pb_max_seg : lines; run_lines1(s, n); lines -= n; }
}
static void HOT run_lines1(pb_t *s, int lines)
{
    s->m68k_frac += M68K_PER_LINE_X64 * lines;
    int32_t mc = (int32_t)(s->m68k_frac >> 6);
    s->m68k_frac &= 63;
    s->z80_frac += Z80_PER_LINE_X64 * lines;
    int32_t zc = (int32_t)(s->z80_frac >> 6);
    s->z80_frac &= 63;
    mc -= s->m68k_debt;              // cycles the last segment ran over
    seg_m68k_budget = mc;
    seg_z80_target = zc;
    seg_z80_done = 0;
    exec_id++;
    int32_t used = mc > 0 ? m68k_execute(mc) : 0;
    s->m68k_debt = used - mc;
    s->mclock += (uint64_t)used;
    seg_m68k_budget = 0;
    if (zc > seg_z80_done) run_sound(s, zc - seg_z80_done);
}

void HOT pb_run_frame(void)
{
    pb_t *s = S;
    // line 0..15: vblank (INTL pending until 16)
    run_lines(s, VBEND_LINE);
    s->irq_pending &= (uint8_t)~0x20;                 // INTL cleared at vblank end
    irq_update();
    run_lines(s, VBLANK_LINE - VBEND_LINE);           // 16..239: visible
    tc0180vcu_capture(V, &pb_frame);                  // what the screen shows
    tc0180vcu_vblank(V);                              // sprites for the next frame
    s->irq_pending |= 0x08;                           // INTH
    irq_update();
    run_lines(s, INTL_LINE - VBLANK_LINE);            // 240..247
    s->irq_pending = (uint8_t)((s->irq_pending & ~0x08) | 0x20);   // INTH off, INTL on
    irq_update();
    run_lines(s, PB_LINES - INTL_LINE);               // 248..255
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void pb_init(void)
{
    if (!S) {
        S = calloc(1, sizeof(pb_t));
        V = calloc(1, sizeof(tc0180vcu_t));
        for (int i = 0; i < 512; i++) rom_page[i] = &pb_prog_rom[i * 1024];
        eeprom93c46_init(&S->eep, 16);
        eeprom93c46_set_timing(&S->eep, PB_CPU_CLOCK);
    }
    tc0180vcu_set_gfx(pb_gfx_rom, sizeof pb_gfx_rom / 32);
    pb_t *s = S;
    s->syt.nmi_cb = syt_nmi;
    s->syt.reset_cb = syt_reset;
    s->ym.irq_cb = ym_irq;
    s->ym.reg_cb = ym_reg;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    z80_init(&s->cpu_snd);
    s->cpu_snd.read_byte = snd_read;
    s->cpu_snd.write_byte = snd_write;
    s->cpu_snd.port_in = snd_in;
    s->cpu_snd.port_out = snd_out;
    s->cpu_snd.userdata = s;
    pb_reset();
}

void pb_reset(void)
{
    pb_t *s = S;
    memset(s->ram, 0, sizeof s->ram);
    memset(s->sndram, 0, sizeof s->sndram);
    s->eep_latch = 0;
    s->coin_word = 0;
    s->snd_bank = 0;
    s->snd_reset_held = false;
    s->irq_pending = 0;
    s->mb_data = s->mb_control = 0;
    for (int i = 0; i < 4; i++) s->mb_latch[i] = 0x7f;
    mb87078_recalc(s);
    tc0180vcu_reset(V);
    tc0140syt_reset(&s->syt);
    eeprom93c46_reset(&s->eep);
    ym2610_ctl_reset(&s->ym);
    m68k_pulse_reset();
    z80_reset(&s->cpu_snd);
    irq_update();
}

const uint16_t *pb_vram(void) { return V->vram; }
const uint16_t *pb_palette_ram(void) { return S->pal; }
uint64_t pb_snd_clock(void) { return S->zclock; }
int pb_sound_gain(void) { return S->gain; }
uint8_t *pb_eeprom(bool **dirty) { if (dirty) *dirty = &S->eep.dirty; return S->eep.data; }

void pb_get_stats(pb_stats_t *st)
{
    *st = S->st;
    st->fb_cpu_accesses = V->fb_cpu_accesses;
    st->no_erase_frames = V->no_erase_frames;
}
