// amiga_core.c - Commodore Amiga 500 (PAL, OCS): the 68000 (Musashi), the
// memory map, the two 8520 CIAs, the floppy drives with their ADF images,
// the keyboard, mouse and joystick, the frame loop and the sound's filters.
// A C port of dsp-cpp's Amiga500 (src/drivers/computers/amiga.cpp, with
// src/machine/cia8520.cpp and amiga_adf.cpp); the chipset is in
// amiga_chipset.c. Added here: the keyboard (dsp-cpp has none), as the
// A500's keyboard processor sends it on CIA-A's serial port.
//
// Timing: as dsp-cpp, 313 lines of 454 CPU cycles; the CPU runs in slices
// of a quarter line and the CIAs (E clock: CPU / 10) and Paula (colour clock:
// CPU / 2) are brought up to the CPU's time before any access to them and
// after each slice. Interrupt changes are taken between instructions.
#include <stdlib.h>
#include <string.h>
#include "amiga_int.h"
#include "m68k.h"

#ifndef AMIGA_HOT
#define AMIGA_HOT
#endif

#define LINES 313
#define LINE_CYCLES 454
#define SLICE 114
#define SLOW_BASE 0xC00000u
#define SLOW_SIZE 0x80000u
#define MFM_WORDS 6334                  // one revolution (12668 MFM bytes)
#define MAX_COUNTS 60                   // mouse counts per frame at most

// ---------------------------------------------------------------------------
// 8520 CIA (dsp-cpp's Cia8520): TOD is a 24-bit binary counter; port reads
// are (latch & DDR) | (pins & ~DDR)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t pra, prb, ddra, ddrb;
    uint16_t ta, tb, ta_latch, tb_latch;
    uint32_t tod, alarm, tod_latch;
    uint8_t sdr, icr, imr, cra, crb;
    bool tod_run, tod_latched;
} cia8520;

typedef struct {
    uint8_t *img;
    uint32_t size;
    int tracks, sides;
    int cyl;
    bool motor, selected;
    bool changed;                       // /CHNG latch: set until a step with a disk in
} drive_t;

static struct {
    uint8_t *chip, *slow;
    const uint8_t *rom;
    uint32_t rom_size;
    uint16_t *fb;
    amiga_chips c;
    cia8520 ciaa, ciab;
    bool overlay;

    // CPU time and what has caught up with it
    uint32_t now;                       // CPU cycles run (before the current slice)
    uint64_t total;                     // all the CPU cycles run (the disks' rotation)
    uint32_t line_start;                // CPU time at the start of this line
    uint32_t synced;                    // cycles the CIAs / Paula have had
    bool in_cpu;
    int cur_ipl;
    bool ipl_pending;
    int debt;

    int cia_acc, cck_acc;
    int64_t audio_acc;
    float lp_fixed, dc, led_x1, led_x2, led_y1, led_y2;
    int16_t audio[AMIGA_AUDIO_MAX + 8];
    int naudio;

    drive_t drv[AMIGA_DRIVES];
    int side;
    uint8_t prev_prb;
    int index_div;
    int disk_gen;

    // keyboard
    uint8_t kq[64];
    int kq_head, kq_tail;
    bool kbd_ready;
    int kbd_wait, kbd_timeout;
    bool caps;
    uint8_t held[128];
    bool want_reset;

    // mouse / joystick
    uint8_t mouse_x, mouse_y;
    int pend_x, pend_y;
    bool lmb, fire1;
} A;

static void update_ipl(void);
static void cia_b_floppy(uint8_t prb);

static inline bool cia_is_a(const cia8520 *c) { return c == &A.ciaa; }

static void cia_reset(cia8520 *c)
{
    memset(c, 0, sizeof *c);
    c->ta = c->tb = c->ta_latch = c->tb_latch = 0xFFFF;
    c->tod_run = true;
}

static void cia_update_irq(cia8520 *c)
{
    if (c->icr & c->imr & 0x1F) c->icr |= 0x80;
    else c->icr &= 0x7F;
    update_ipl();
}

static void cia_pulse_flag(cia8520 *c)
{
    c->icr |= 0x10;
    cia_update_irq(c);
}

static void cia_tod_tick(cia8520 *c)
{
    if (!c->tod_run) return;
    c->tod = (c->tod + 1) & 0xFFFFFF;
    if (c->tod == c->alarm) {
        c->icr |= 0x04;
        cia_update_irq(c);
    }
}

// one E clock, as dsp-cpp's Cia8520::tick
static void cia_tick1(cia8520 *c)
{
    if (c->cra & 1) {
        if (c->ta == 0) {
            c->ta = c->ta_latch;
            c->icr |= 0x01;
            if (c->cra & 0x08) c->cra &= (uint8_t)~1;
            if ((c->crb & 0x41) == 0x41) {
                if (c->tb == 0) {
                    c->tb = c->tb_latch;
                    c->icr |= 0x02;
                    if (c->crb & 0x08) c->crb &= (uint8_t)~1;
                } else {
                    c->tb--;
                }
            }
            cia_update_irq(c);
        } else {
            c->ta--;
        }
    }
    if ((c->crb & 1) && (c->crb & 0x40) == 0) {
        if (c->tb == 0) {
            c->tb = c->tb_latch;
            c->icr |= 0x02;
            if (c->crb & 0x08) c->crb &= (uint8_t)~1;
            cia_update_irq(c);
        } else {
            c->tb--;
        }
    }
}

// n E clocks: the ones that underflow nothing in one go, the others one by one
static void AMIGA_HOT cia_tick(cia8520 *c, int n)
{
    while (n > 0) {
        const bool a_run = (c->cra & 1) != 0;
        const bool b_run = (c->crb & 1) && !(c->crb & 0x40);
        int step = n;
        if (a_run && c->ta + 1 < step) step = c->ta + 1;
        if (b_run && c->tb + 1 < step) step = c->tb + 1;
        const int plain = step - 1;
        if (a_run) c->ta = (uint16_t)(c->ta - plain);
        if (b_run) c->tb = (uint16_t)(c->tb - plain);
        cia_tick1(c);
        n -= step;
    }
}

static uint8_t cia_a_pra_in(void);

static uint8_t cia_port_in(cia8520 *c, int port)
{
    if (cia_is_a(c) && port == 0) return cia_a_pra_in();
    return 0xFF;
}

static void cia_port_out(cia8520 *c, int port)
{
    if (cia_is_a(c)) {
        if (port == 0) A.overlay = !((c->ddra & 1) && !(c->pra & 1));
    } else if (port == 1) {
        cia_b_floppy((uint8_t)(c->prb | (uint8_t)~c->ddrb));
    }
}

static uint8_t cia_read(cia8520 *c, int reg)
{
    switch (reg & 15) {
    case 0: return (uint8_t)((c->pra & c->ddra) | (cia_port_in(c, 0) & (uint8_t)~c->ddra));
    case 1: return (uint8_t)((c->prb & c->ddrb) | (cia_port_in(c, 1) & (uint8_t)~c->ddrb));
    case 2: return c->ddra;
    case 3: return c->ddrb;
    case 4: return (uint8_t)c->ta;
    case 5: return (uint8_t)(c->ta >> 8);
    case 6: return (uint8_t)c->tb;
    case 7: return (uint8_t)(c->tb >> 8);
    case 8: {
        const uint8_t v = (uint8_t)(c->tod_latched ? c->tod_latch : c->tod);
        c->tod_latched = false;
        return v;
    }
    case 9: return (uint8_t)((c->tod_latched ? c->tod_latch : c->tod) >> 8);
    case 10:
        c->tod_latch = c->tod;
        c->tod_latched = true;
        return (uint8_t)(c->tod_latch >> 16);
    case 11: return 0;
    case 12: return c->sdr;
    case 13: {
        const uint8_t v = c->icr;
        c->icr = 0;
        update_ipl();
        return v;
    }
    case 14: return c->cra;
    default: return c->crb;
    }
}

static void tod_alarm_check(cia8520 *c)
{
    if (c->tod == c->alarm) {
        c->icr |= 0x04;
        cia_update_irq(c);
    }
}

static void kbd_handshake(void);

static void cia_write(cia8520 *c, int reg, uint8_t v)
{
    uint32_t *tod = (c->crb & 0x80) ? &c->alarm : &c->tod;
    switch (reg & 15) {
    case 0: c->pra = v; cia_port_out(c, 0); break;
    case 1: c->prb = v; cia_port_out(c, 1); break;
    case 2: c->ddra = v; cia_port_out(c, 0); break;
    case 3: c->ddrb = v; cia_port_out(c, 1); break;
    case 4: c->ta_latch = (uint16_t)((c->ta_latch & 0xFF00) | v); break;
    case 5:
        c->ta_latch = (uint16_t)((c->ta_latch & 0x00FF) | (v << 8));
        // 8520: writing timer A high loads it; in one-shot it also starts
        if (!(c->cra & 1) || (c->cra & 0x08)) c->ta = c->ta_latch;
        if (c->cra & 0x08) c->cra |= 1;
        break;
    case 6: c->tb_latch = (uint16_t)((c->tb_latch & 0xFF00) | v); break;
    case 7:
        c->tb_latch = (uint16_t)((c->tb_latch & 0x00FF) | (v << 8));
        if (!(c->crb & 1) || (c->crb & 0x08)) c->tb = c->tb_latch;
        if (c->crb & 0x08) c->crb |= 1;
        break;
    case 8:
        *tod = (*tod & 0xFFFF00) | v;
        if (!(c->crb & 0x80)) {
            c->tod_run = true;
            c->tod_latched = false;
        }
        tod_alarm_check(c);
        break;
    case 9:
        *tod = (*tod & 0xFF00FF) | ((uint32_t)v << 8);
        tod_alarm_check(c);
        break;
    case 10:
        *tod = (*tod & 0x00FFFF) | ((uint32_t)v << 16);
        if (!(c->crb & 0x80)) c->tod_run = false;
        tod_alarm_check(c);
        break;
    case 12:
        c->sdr = v;
        c->icr |= 0x08;
        cia_update_irq(c);
        break;
    case 13:
        if (v & 0x80) c->imr |= (uint8_t)(v & 0x1F);
        else c->imr &= (uint8_t)~(v & 0x1F);
        cia_update_irq(c);
        break;
    case 14: {
        const uint8_t old = c->cra;
        c->cra = (uint8_t)(v & ~0x10);
        if (v & 0x10) c->ta = c->ta_latch;
        // the keyboard's handshake: SP pulled low (output) and let go
        if (cia_is_a(c) && (old & 0x40) && !(v & 0x40)) kbd_handshake();
        break;
    }
    case 15: {
        const uint8_t old = c->crb;
        c->crb = (uint8_t)(v & ~0x10);
        if (v & 0x10) c->tb = c->tb_latch;
        // Kickstart 1.3's cia.resource writes the timeout into TOD, then
        // sets CRB ALARM with the alarm still 0: that is alarm = timeout
        if ((c->crb & 0x80) && !(old & 0x80) && c->tod != 0 && c->tod < 0x10000) {
            c->alarm = c->tod;
            c->tod = 0;
            c->tod_run = true;
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Time: the CIAs and Paula brought up to the CPU, the sound's filters
// ---------------------------------------------------------------------------
static int16_t filter_sample(int in)
{
    // A500 output stage: a fixed ~4.9 kHz RC low-pass, the "LED" 3.3 kHz
    // two-pole Butterworth (on while CIA-A PRA bit 1 lights the power LED)
    // and the AC coupling of the line output (dsp-cpp's, in float)
    const float kFixed = 0.5025f;
    const float b0 = 0.04109f, b1 = 0.08218f, b2 = 0.04109f, a1 = -1.35039f, a2 = 0.51475f;
    const float kDc = 0.000712f;
    float x = (float)in;
    x = A.lp_fixed += kFixed * (x - A.lp_fixed);
    if (amiga_power_led()) {
        const float y = b0 * x + b1 * A.led_x1 + b2 * A.led_x2 - a1 * A.led_y1 - a2 * A.led_y2;
        A.led_x2 = A.led_x1;
        A.led_x1 = x;
        A.led_y2 = A.led_y1;
        A.led_y1 = y;
        x = y;
    } else {
        A.led_x2 = A.led_x1 = x;
        A.led_y2 = A.led_y1 = x;
    }
    A.dc += kDc * (x - A.dc);
    x -= A.dc;
    if (x > 32767.f) x = 32767.f;
    if (x < -32768.f) x = -32768.f;
    return (int16_t)x;
}

static void AMIGA_HOT sync_to(uint32_t t)
{
    int n = (int)(t - A.synced);
    if (n <= 0) return;
    A.synced = t;
    A.cia_acc += n;
    if (A.cia_acc >= 10) {
        const int e = A.cia_acc / 10;
        A.cia_acc -= e * 10;
        cia_tick(&A.ciaa, e);
        cia_tick(&A.ciab, e);
    }
    while (n > 0) {
        // up to the next sample
        const int k = (int)((AMIGA_CLOCK - A.audio_acc + AMIGA_AUDIO_RATE - 1) / AMIGA_AUDIO_RATE);
        const int step = n < k ? n : k;
        A.cck_acc += step;
        const int cck = A.cck_acc >> 1;
        A.cck_acc &= 1;
        if (cck > 0 && ach_audio_run(&A.c, cck)) update_ipl();
        A.audio_acc += (int64_t)step * AMIGA_AUDIO_RATE;
        if (A.audio_acc >= AMIGA_CLOCK) {
            A.audio_acc -= AMIGA_CLOCK;
            const int16_t s = filter_sample(ach_audio_take_sample(&A.c));
            if (A.naudio < AMIGA_AUDIO_MAX) A.audio[A.naudio++] = s;
        }
        n -= step;
    }
}

// the CPU's time now (inside a slice: what it has run of it)
static inline uint32_t cpu_now(void) { return A.now + (A.in_cpu ? (uint32_t)m68k_cycles_run() : 0); }
static inline void sync_cpu(void) { sync_to(cpu_now()); }

static void end_slice(void)
{
    // (m68k_end_timeslice would return the wrong count of cycles run)
    m68k_modify_timeslice(-m68k_cycles_remaining());
}

static void update_ipl(void)
{
    A.c.ciaa_irq = (A.ciaa.icr & 0x80) != 0;
    A.c.ciab_irq = (A.ciab.icr & 0x80) != 0;
    const int ipl = ach_ipl(&A.c);
    if (ipl == A.cur_ipl) return;
    if (A.in_cpu) {                     // between instructions: after this one
        A.ipl_pending = true;
        end_slice();
        return;
    }
    A.cur_ipl = ipl;
    m68k_set_irq((unsigned)ipl);
}

// ---------------------------------------------------------------------------
// Floppy drives (dsp-cpp: one 3.5" DD drive each; /STEP, DIR, /SIDE shared,
// /SELx each, /MTR latched on /SELx's falling edge; DF1 is only connected
// with a disk in it)
// ---------------------------------------------------------------------------
static inline bool drive_connected(int i) { return i == 0 || A.drv[i].img != NULL; }

static uint8_t cia_a_pra_in(void)
{
    // bit 2 /CHNG, 3 /WPRO, 4 /TK0, 5 /RDY (active low, wired-OR over the
    // selected drives), 6 /FIR0 (left mouse button), 7 /FIR1 (joystick fire)
    uint8_t v = 0xFF;
    if (A.lmb) v &= (uint8_t)~0x40;
    if (A.fire1) v &= (uint8_t)~0x80;
    bool any_selected = false;
    for (int i = 0; i < AMIGA_DRIVES; i++) any_selected = any_selected || (A.drv[i].selected && drive_connected(i));
    for (int i = 0; i < AMIGA_DRIVES; i++) {
        const drive_t *d = &A.drv[i];
        if (!drive_connected(i)) continue;
        // with nothing selected DF0 still drives /CHNG and /TK0
        const bool talking = d->selected || (!any_selected && i == 0);
        if (!talking) continue;
        if (!d->img || d->changed) v &= (uint8_t)~0x04;
        if (d->cyl == 0) v &= (uint8_t)~0x10;
        // /RDY only while selected. With /MTR latched off Kickstart 1.3 reads
        // a 32-bit drive ID on it (a 3.5" DD drive shifts zeros: RDY low; an
        // unconnected DF1 leaves it high). With the motor on, RDY is "disk
        // turning".
        if (d->selected) {
            const bool ready = d->motor ? d->img != NULL : true;
            if (ready) v &= (uint8_t)~0x20;
        }
    }
    return v;
}

static void cia_b_floppy(uint8_t prb)
{
    // bit 0 /STEP, 1 DIR, 2 /SIDE, 3 /SEL0, 4 /SEL1, 7 /MTR
    const bool mtr_line = (prb & 0x80) == 0;
    A.side = (prb & 0x04) ? 0 : 1;
    const bool step = (A.prev_prb & 1) && !(prb & 1);
    for (int i = 0; i < AMIGA_DRIVES; i++) {
        drive_t *d = &A.drv[i];
        const bool sel = (prb & (0x08 << i)) == 0;
        if (sel && !d->selected) d->motor = mtr_line;
        d->selected = sel;
        if (!d->selected || !step) continue;
        // DIR 0: towards the spindle (higher cylinders); 1: towards track 0
        if (prb & 2) {
            if (d->cyl > 0) d->cyl--;
        } else {
            if (d->cyl < 82) d->cyl++;
        }
        if (d->img) d->changed = false;
    }
    A.prev_prb = prb;
}

// --- ADF -> MFM (dsp-cpp's AmigaAdf::encode_track) ---
static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// the MFM clock bits put into a 0x55555555-masked data long; `prev` is the
// last output bit of the previous long
static uint32_t encode_clocks(uint32_t data, uint32_t *prev)
{
    data &= 0x55555555u;
    uint32_t out = data, p = *prev;
    for (int i = 31; i >= 0; i--) {
        if (i & 1) {
            const uint32_t next = (data >> (i - 1)) & 1u;
            const uint32_t clock = (p == 0 && next == 0) ? 1u : 0u;
            out |= clock << i;
            p = clock;
        } else {
            p = (data >> i) & 1u;
        }
    }
    *prev = p;
    return out;
}

static uint16_t mfm_buf[MFM_WORDS];
static int mfm_len, mfm_key = -1;

static inline void emit_long(uint32_t w)
{
    if (mfm_len + 2 > MFM_WORDS) return;
    mfm_buf[mfm_len++] = (uint16_t)(w >> 16);
    mfm_buf[mfm_len++] = (uint16_t)w;
}

// Kickstart 1.3's trackdisk decodes the header longs as odd / even pairs,
// and blits the 512-byte payload as all its odd bits, then all its even ones
static void encode_pairs(const uint32_t *data, int n, uint32_t *prev)
{
    for (int i = 0; i < n; i++) {
        emit_long(encode_clocks((data[i] >> 1) & 0x55555555u, prev));
        emit_long(encode_clocks(data[i] & 0x55555555u, prev));
    }
}

static void encode_planes(const uint32_t *data, int n, uint32_t *prev)
{
    for (int i = 0; i < n; i++) emit_long(encode_clocks((data[i] >> 1) & 0x55555555u, prev));
    for (int i = 0; i < n; i++) emit_long(encode_clocks(data[i] & 0x55555555u, prev));
}

// trackdisk XORs the masked MFM longs (odd then even data bits)
static uint32_t mfm_data_xor(const uint32_t *data, int n)
{
    uint32_t s = 0;
    for (int i = 0; i < n; i++) {
        s ^= (data[i] >> 1) & 0x55555555u;
        s ^= data[i] & 0x55555555u;
    }
    return s;
}

static void encode_track(const drive_t *d, int cyl, int side)
{
    mfm_len = 0;
    for (int i = 0; i < 165; i++) mfm_buf[mfm_len++] = 0xAAAA;   // leading gap
    const int amiga_track = cyl * 2 + side;
    static uint32_t data[128];
    for (int sec = 0; sec < 11; sec++) {
        mfm_buf[mfm_len++] = 0x4489;
        mfm_buf[mfm_len++] = 0x4489;
        uint32_t prev = 0x4489 & 1;
        uint32_t header[5] = { 0xFF000000u | ((uint32_t)amiga_track << 16) | ((uint32_t)sec << 8) | (uint32_t)(11 - sec),
                               0, 0, 0, 0 };
        const uint32_t hsum = mfm_data_xor(header, 5);
        const bool ok = cyl < d->tracks && side < d->sides;
        const uint8_t *src = ok ? d->img + (uint32_t)(((cyl * d->sides + side) * 11 + sec) * 512) : NULL;
        for (int i = 0; i < 128; i++) data[i] = src ? be32(src + i * 4) : 0;
        const uint32_t dsum = mfm_data_xor(data, 128);
        encode_pairs(header, 5, &prev);
        encode_pairs(&hsum, 1, &prev);
        encode_pairs(&dsum, 1, &prev);
        encode_planes(data, 128, &prev);
        if (mfm_len + 2 <= MFM_WORDS) {
            mfm_buf[mfm_len++] = 0xAAAA;
            mfm_buf[mfm_len++] = 0xAAAA;
        }
    }
    while (mfm_len < MFM_WORDS) mfm_buf[mfm_len++] = 0xAAAA;
}

const uint16_t *amiga_track_mfm(int *words)
{
    // the data comes from the selected drive whose disk is turning
    for (int i = 0; i < AMIGA_DRIVES; i++) {
        const drive_t *d = &A.drv[i];
        if (!d->img || !d->selected || !d->motor) continue;
        int cyl = d->cyl;
        if (cyl < 0) cyl = 0;
        if (cyl >= d->tracks) cyl = d->tracks - 1;
        const int key = ((A.disk_gen * 2 + i) * 128 + cyl) * 2 + A.side;
        if (key != mfm_key) {
            encode_track(d, cyl, A.side);
            mfm_key = key;
        }
        *words = MFM_WORDS;
        return mfm_buf;
    }
    *words = 0;
    return NULL;
}

int amiga_disk_pos(int words)
{
    // 300 rpm: 5 turns a second
    const uint64_t t = A.total + (A.in_cpu ? (uint64_t)m68k_cycles_run() : 0);
    return (int)((t * 5 % AMIGA_CLOCK) * (uint64_t)words / AMIGA_CLOCK);
}

bool amiga_insert(int drive, uint8_t *adf, uint32_t size, const char **why)
{
    const char *dummy;
    if (!why) why = &dummy;
    *why = "";
    if (drive < 0 || drive >= AMIGA_DRIVES || !adf) { *why = "no such drive"; return false; }
    if (size < 11 * 512) { *why = "ADF is too small"; return false; }
    if (size % 512) { *why = "ADF is not a multiple of 512 bytes"; return false; }
    const int sectors = (int)(size / 512);
    if (sectors % 11) { *why = "not an 11-sector AmigaDOS image"; return false; }
    const int track_total = sectors / 11;
    int sides = 1, tracks = track_total;
    if (track_total >= 160 && track_total % 2 == 0) {
        sides = 2;
        tracks = track_total / 2;
    }
    if (tracks < 40 || tracks > 84) { *why = "ADF cylinder count is out of range"; return false; }
    amiga_eject(drive);
    drive_t *d = &A.drv[drive];
    d->img = adf;
    d->size = size;
    d->tracks = tracks;
    d->sides = sides;
    d->changed = true;
    A.disk_gen++;
    return true;
}

void amiga_eject(int drive)
{
    if (drive < 0 || drive >= AMIGA_DRIVES) return;
    drive_t *d = &A.drv[drive];
    if (d->img) free(d->img);
    d->img = NULL;
    d->size = 0;
    d->changed = true;
    if (drive > 0) {                    // (DF1 goes away with its disk)
        d->motor = false;
        d->selected = false;
    }
    A.disk_gen++;
}

bool amiga_disk_in(int drive) { return drive >= 0 && drive < AMIGA_DRIVES && A.drv[drive].img != NULL; }

int amiga_free_drive(void)
{
    for (int i = 0; i < AMIGA_DRIVES; i++)
        if (!A.drv[i].img) return i;
    return -1;
}

bool amiga_drive_led(void)
{
    for (int i = 0; i < AMIGA_DRIVES; i++)
        if (A.drv[i].motor && drive_connected(i)) return true;
    return false;
}

bool amiga_power_led(void) { return (A.ciaa.ddra & 0x02) && !(A.ciaa.pra & 0x02); }

// ---------------------------------------------------------------------------
// Keyboard: the A500's keyboard processor. A key code goes out on CIA-A's
// serial port rotated left and inverted (~(code << 1 | code >> 7), bit 7 of
// the code: key up) and raises its SP interrupt; the computer answers with a
// handshake (CRA's SPMODE set and cleared) before the next one is sent.
// Without an answer in 143 ms the keyboard sends the next one anyway.
// ---------------------------------------------------------------------------
static void kq_push(uint8_t code)
{
    const int next = (A.kq_tail + 1) & 63;
    if (next == A.kq_head) return;      // full: dropped
    A.kq[A.kq_tail] = code;
    A.kq_tail = next;
}

static void kbd_handshake(void)
{
    A.kbd_ready = true;
    A.kbd_wait = 2;                     // lines
}

static void kbd_line(void)
{
    if (A.kbd_wait > 0) {
        A.kbd_wait--;
        return;
    }
    if (!A.kbd_ready) {
        if (--A.kbd_timeout <= 0) A.kbd_ready = true;
        return;
    }
    if (A.kq_head == A.kq_tail || (A.ciaa.cra & 0x40)) return;
    const uint8_t code = A.kq[A.kq_head];
    A.kq_head = (A.kq_head + 1) & 63;
    sync_cpu();
    A.ciaa.sdr = (uint8_t)~((code << 1) | (code >> 7));
    A.ciaa.icr |= 0x08;
    cia_update_irq(&A.ciaa);
    A.kbd_ready = false;
    A.kbd_timeout = 2236;               // 143 ms in lines
}

void amiga_key(int code, bool down)
{
    if (code < 0 || code > 0x67) return;
    if (code == AK_CAPSLOCK) {          // the key locks: down when its LED goes on, up when off
        if (!down) return;
        A.caps = !A.caps;
        kq_push((uint8_t)(AK_CAPSLOCK | (A.caps ? 0 : 0x80)));
        return;
    }
    if ((A.held[code] != 0) == down) return;
    A.held[code] = down;
    kq_push((uint8_t)(code | (down ? 0 : 0x80)));
    if (down && A.held[AK_CTRL] && A.held[AK_LAMIGA] && A.held[AK_RAMIGA]) A.want_reset = true;
}

void amiga_keys_release(void)
{
    for (int k = 0; k < 128; k++)
        if (A.held[k]) amiga_key(k, false);
}

// ---------------------------------------------------------------------------
// Mouse (port 0) and joystick (port 1)
// ---------------------------------------------------------------------------
static void update_joy0(void) { A.c.joy0dat = (uint16_t)((A.mouse_y << 8) | A.mouse_x); }

void amiga_joytest(uint16_t v)
{
    A.mouse_x = (uint8_t)((A.mouse_x & 0x03) | (v & 0xfc));
    A.mouse_y = (uint8_t)((A.mouse_y & 0x03) | ((v >> 8) & 0xfc));
    update_joy0();
}

void amiga_mouse(int dx, int dy)
{
    A.pend_x += dx;
    A.pend_y += dy;
    if (A.pend_x > 4000) A.pend_x = 4000;
    if (A.pend_x < -4000) A.pend_x = -4000;
    if (A.pend_y > 4000) A.pend_y = 4000;
    if (A.pend_y < -4000) A.pend_y = -4000;
}

void amiga_mouse_buttons(bool left, bool right)
{
    A.lmb = left;
    A.c.rmb = right;
}

// the counters are 8 bits: more than 127 counts between two reads wrap the
// other way, and games often read them every other frame; so at most 60
// counts a frame, and a fast flick is spread over the next frames
static void mouse_frame(void)
{
    int sx = A.pend_x, sy = A.pend_y;
    if (sx > MAX_COUNTS) sx = MAX_COUNTS;
    if (sx < -MAX_COUNTS) sx = -MAX_COUNTS;
    if (sy > MAX_COUNTS) sy = MAX_COUNTS;
    if (sy < -MAX_COUNTS) sy = -MAX_COUNTS;
    A.pend_x -= sx;
    A.pend_y -= sy;
    A.mouse_x = (uint8_t)(A.mouse_x + sx);
    A.mouse_y = (uint8_t)(A.mouse_y + sy);
    update_joy0();
}

void amiga_joy(uint8_t bits)
{
    // JOY1DAT: right = bit 1, left = bit 9; up / down XORed with them into 8 / 0
    const bool r = bits & AMIGA_JOY_RIGHT, l = bits & AMIGA_JOY_LEFT;
    const bool u = bits & AMIGA_JOY_UP, d = bits & AMIGA_JOY_DOWN;
    uint16_t j = 0;
    if (r) j |= 0x0002;
    if (l) j |= 0x0200;
    if (d != r) j |= 0x0001;
    if (u != l) j |= 0x0100;
    A.c.joy1dat = j;
    A.fire1 = (bits & AMIGA_JOY_FIRE) != 0;
}

// ---------------------------------------------------------------------------
// The 68000's memory map
// ---------------------------------------------------------------------------
static inline uint8_t rom_byte(uint32_t a) { return A.rom[(a - 0xF80000u) & (A.rom_size - 1)]; }

static uint8_t AMIGA_HOT read_byte(uint32_t a)
{
    a &= 0x00FFFFFFu;
    if (a < 0x200000u) {
        if (A.overlay && a < A.rom_size) return A.rom[a];
        return A.chip[a & (AMIGA_CHIP_SIZE - 1)];
    }
    if (a - SLOW_BASE < SLOW_SIZE) return A.slow[a - SLOW_BASE];
    if (a >= 0xBF0000u && a <= 0xBFFFFFu) {
        // CIA-A: A12 low, odd bytes; CIA-B: A13 low, even bytes
        sync_cpu();
        const int reg = (a >> 8) & 0x0F;
        if (a & 1) return (a & 0x1000) ? 0xFF : cia_read(&A.ciaa, reg);
        return (a & 0x2000) ? 0xFF : cia_read(&A.ciab, reg);
    }
    if (a >= 0xDFF000u && a <= 0xDFFFFFu) {
        sync_cpu();
        const uint16_t w = ach_read(&A.c, (uint16_t)(a & 0x1FE));
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (a >= 0xF80000u) return rom_byte(a);
    return 0xFF;                        // no Zorro board / empty slot
}

static void AMIGA_HOT write_byte(uint32_t a, uint8_t v)
{
    a &= 0x00FFFFFFu;
    if (a < 0x200000u) {
        A.chip[a & (AMIGA_CHIP_SIZE - 1)] = v;
        return;
    }
    if (a - SLOW_BASE < SLOW_SIZE) {
        A.slow[a - SLOW_BASE] = v;
        return;
    }
    if (a >= 0xBF0000u && a <= 0xBFFFFFu) {
        sync_cpu();
        const int reg = (a >> 8) & 0x0F;
        if (a & 1) {
            if (!(a & 0x1000)) cia_write(&A.ciaa, reg, v);
        } else if (!(a & 0x2000)) {
            cia_write(&A.ciab, reg, v);
        }
        return;
    }
    if (a >= 0xDFF000u && a <= 0xDFFFFFu) {
        sync_cpu();
        const uint16_t r = (uint16_t)(a & 0x1FE);
        const uint16_t old = ach_read(&A.c, r);
        if (a & 1) ach_write(&A.c, r, (uint16_t)((old & 0xFF00) | v));
        else ach_write(&A.c, r, (uint16_t)((v << 8) | (old & 0x00FF)));
        update_ipl();
    }
}

static uint16_t AMIGA_HOT read_word(uint32_t a)
{
    a &= 0x00FFFFFEu;
    if (a < 0x200000u && !(A.overlay && a < A.rom_size)) {
        const uint8_t *p = A.chip + (a & (AMIGA_CHIP_SIZE - 1));
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    if (a - SLOW_BASE < SLOW_SIZE) {
        const uint8_t *p = A.slow + (a - SLOW_BASE);
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    if (a >= 0xF80000u) {
        const uint8_t *p = A.rom + ((a - 0xF80000u) & (A.rom_size - 1));
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    if (a >= 0xDFF000u && a <= 0xDFFFFFu) {
        sync_cpu();
        return ach_read(&A.c, (uint16_t)(a & 0x1FE));
    }
    return (uint16_t)((read_byte(a) << 8) | read_byte(a + 1));
}

static void AMIGA_HOT write_word(uint32_t a, uint16_t v)
{
    a &= 0x00FFFFFEu;
    if (a < 0x200000u) {
        uint8_t *p = A.chip + (a & (AMIGA_CHIP_SIZE - 1));
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
        return;
    }
    if (a - SLOW_BASE < SLOW_SIZE) {
        uint8_t *p = A.slow + (a - SLOW_BASE);
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
        return;
    }
    if (a >= 0xDFF000u && a <= 0xDFFFFFu) {
        sync_cpu();
        ach_write(&A.c, (uint16_t)(a & 0x1FE), v);
        update_ipl();
        return;
    }
    write_byte(a, (uint8_t)(v >> 8));
    write_byte(a + 1, (uint8_t)v);
}

unsigned int cpu_read_byte(unsigned int a) { return read_byte(a); }
unsigned int cpu_read_word(unsigned int a) { return read_word(a); }
unsigned int cpu_read_long(unsigned int a) { return ((uint32_t)read_word(a) << 16) | read_word(a + 2); }
unsigned int cpu_read_word_dasm(unsigned int a) { return read_word(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return cpu_read_long(a); }
void cpu_write_byte(unsigned int a, unsigned int v) { write_byte(a, (uint8_t)v); }
void cpu_write_word(unsigned int a, unsigned int v) { write_word(a, (uint16_t)v); }
void cpu_write_long(unsigned int a, unsigned int v)
{
    write_word(a, (uint16_t)(v >> 16));
    write_word(a + 2, (uint16_t)v);
}
void cpu_write_long_pd(unsigned int a, unsigned int v)
{
    write_word(a + 2, (uint16_t)v);
    write_word(a, (uint16_t)(v >> 16));
}
int cpu_irq_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }
void cpu_set_fc(int fc) { (void)fc; }
void cpu_instr_callback(unsigned int pc) { (void)pc; }

// the RESET instruction: the chips and the CIAs (the overlay comes back)
void cpu_pulse_reset(void)
{
    sync_cpu();
    cia_reset(&A.ciaa);
    cia_reset(&A.ciab);
    ach_reset(&A.c);
    A.overlay = true;
    A.prev_prb = 0xFF;
    for (int i = 0; i < AMIGA_DRIVES; i++) {
        A.drv[i].motor = false;
        A.drv[i].selected = false;
    }
    update_joy0();
    update_ipl();
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------
bool amiga_init(const uint8_t *kick, uint32_t size)
{
    if (!kick || (size != 0x40000 && size != 0x80000)) return false;
    if (!A.chip) A.chip = (uint8_t *)malloc(AMIGA_CHIP_SIZE);
    if (!A.slow) A.slow = (uint8_t *)malloc(SLOW_SIZE);
    if (!A.fb) A.fb = (uint16_t *)malloc(AMIGA_W * AMIGA_H * sizeof(uint16_t));
    if (!A.chip || !A.slow || !A.fb) return false;
    A.rom = kick;
    A.rom_size = size;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    amiga_reset();
    return true;
}

void amiga_reset(void)
{
    memset(A.chip, 0, AMIGA_CHIP_SIZE);
    memset(A.slow, 0, SLOW_SIZE);
    memset(A.fb, 0, AMIGA_W * AMIGA_H * sizeof(uint16_t));
    A.c.chip = A.chip;
    A.c.fb = A.fb;
    ach_reset(&A.c);
    cia_reset(&A.ciaa);
    cia_reset(&A.ciab);
    A.overlay = true;
    A.side = 0;
    for (int i = 0; i < AMIGA_DRIVES; i++) {
        A.drv[i].cyl = 0;
        A.drv[i].motor = false;
        A.drv[i].selected = false;
        A.drv[i].changed = true;
    }
    A.prev_prb = 0xFF;
    A.index_div = 0;
    A.cia_acc = A.cck_acc = 0;
    A.audio_acc = 0;
    A.lp_fixed = A.dc = A.led_x1 = A.led_x2 = A.led_y1 = A.led_y2 = 0;
    A.naudio = 0;
    A.now = A.synced = 0;
    A.in_cpu = false;
    A.ipl_pending = false;
    A.debt = 0;
    A.kq_head = A.kq_tail = 0;
    A.kbd_ready = true;
    A.kbd_wait = 0;
    A.caps = false;
    memset(A.held, 0, sizeof A.held);
    A.want_reset = false;
    A.mouse_x = A.mouse_y = 0;
    A.pend_x = A.pend_y = 0;
    A.c.joy1dat = 0;
    A.c.rmb = false;
    A.lmb = A.fire1 = false;
    update_joy0();
    A.cur_ipl = 0;
    m68k_set_irq(0);
    m68k_pulse_reset();
}

int amiga_hpos(void)
{
    const int h = (int)(cpu_now() - A.line_start) >> 1;
    return h < 0 ? 0 : h > 226 ? 226 : h;
}

static void run_cpu(int cycles)
{
    A.line_start = A.now - (uint32_t)A.debt;
    int left = cycles - A.debt;
    while (left > 0) {
        const int run = left < SLICE ? left : SLICE;
        A.in_cpu = true;
        const int used = m68k_execute(run);
        A.in_cpu = false;
        A.now += (uint32_t)used;
        A.total += (uint64_t)used;
        left -= used;
        sync_to(A.now);
        if (A.ipl_pending) {
            A.ipl_pending = false;
            A.cur_ipl = -1;
            update_ipl();
        }
    }
    A.debt = -left;
}

int amiga_run_frame(const int16_t **audio)
{
    A.naudio = 0;
    if (A.want_reset) {                 // Ctrl + Amiga + Amiga
        A.want_reset = false;
        amiga_reset();                  // (the disks stay in)
    }
    mouse_frame();
    ach_begin_frame(&A.c);
    update_ipl();
    cia_tod_tick(&A.ciaa);              // CIA-A's TOD: the vertical blank
    // the index pulse of a turning drive (5 a second), even while not selected
    bool spinning = false;
    for (int i = 0; i < AMIGA_DRIVES; i++) spinning = spinning || (A.drv[i].motor && drive_connected(i));
    if (spinning && ++A.index_div >= 10) {
        A.index_div = 0;
        cia_pulse_flag(&A.ciaa);
    }
    for (int line = 0; line < LINES; line++) {
        A.c.vpos = line;
        ach_copper_line(&A.c, line);
        ach_render_line(&A.c, line);
        cia_tod_tick(&A.ciab);          // CIA-B's TOD: HSYNC
        kbd_line();
        update_ipl();
        run_cpu(LINE_CYCLES);
        update_ipl();
    }
    if (audio) *audio = A.audio;
    return A.naudio;
}

const uint16_t *amiga_frame(void) { return A.fb; }
uint32_t amiga_pc(void) { return m68k_get_reg(NULL, M68K_REG_PC); }
uint8_t amiga_peek(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x200000u) return (A.overlay && a < A.rom_size) ? A.rom[a] : A.chip[a & (AMIGA_CHIP_SIZE - 1)];
    if (a - SLOW_BASE < SLOW_SIZE) return A.slow[a - SLOW_BASE];
    if (a >= 0xF80000u) return rom_byte(a);
    return 0xFF;
}
