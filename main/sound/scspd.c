// scspd.c - SCSP: C port of dsp-cpp sound/scsp.cpp (slot / envelope / LFO /
// DSP code from MAME scsp.cpp and scspdsp.cpp: BSD-3-Clause, ElSemi, R. Belmont).
#include <math.h>
#include <string.h>
#include "scspd.h"

enum { kAttack, kDecay1, kDecay2, kRelease };

#define SHIFT 12
#define LFO_SHIFT 8
#define EG_SHIFT 16

static inline uint32_t fix(float v) { return (uint32_t)((float)(1 << SHIFT) * v); }

static const double kARTimes[64] = {
    100000 /*infinity*/, 100000 /*infinity*/, 8100.0, 6900.0, 6000.0, 4800.0, 4000.0, 3400.0, 3000.0, 2400.0,
    2000.0, 1700.0, 1500.0, 1200.0, 1000.0, 860.0, 760.0, 600.0, 500.0, 430.0, 380.0, 300.0, 250.0, 220.0,
    190.0, 150.0, 130.0, 110.0, 95.0, 76.0, 63.0, 55.0, 47.0, 38.0, 31.0, 27.0, 24.0, 19.0, 15.0, 13.0, 12.0,
    9.4, 7.9, 6.8, 6.0, 4.7, 3.8, 3.4, 3.0, 2.4, 2.0, 1.8, 1.6, 1.3, 1.1, 0.93, 0.85, 0.65, 0.53, 0.44, 0.40,
    0.35, 0.0, 0.0};
static const double kDRTimes[64] = {
    100000 /*infinity*/, 100000 /*infinity*/, 118200.0, 101300.0, 88600.0, 70900.0, 59100.0, 50700.0,
    44300.0, 35500.0, 29600.0, 25300.0, 22200.0, 17700.0, 14800.0, 12700.0, 11100.0, 8900.0, 7400.0, 6300.0,
    5500.0, 4400.0, 3700.0, 3200.0, 2800.0, 2200.0, 1800.0, 1600.0, 1400.0, 1100.0, 920.0, 790.0, 690.0, 550.0,
    460.0, 390.0, 340.0, 270.0, 230.0, 200.0, 170.0, 140.0, 110.0, 98.0, 85.0, 68.0, 57.0, 49.0, 43.0, 34.0,
    28.0, 25.0, 22.0, 18.0, 14.0, 12.0, 11.0, 8.5, 7.1, 6.1, 5.4, 4.3, 3.6, 3.1};
static const float kSDLT[8] = {-1000000.0f, -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f};
static const float kLFOFreq[32] = {0.17f, 0.19f, 0.23f, 0.27f, 0.34f, 0.39f, 0.45f, 0.55f, 0.68f, 0.78f, 0.92f,
                            1.10f, 1.39f, 1.60f, 1.87f, 2.27f, 2.87f, 3.31f, 3.92f, 4.79f, 6.15f, 7.18f,
                            8.60f, 10.8f, 14.4f, 17.2f, 21.5f, 28.7f, 43.1f, 57.4f, 86.1f, 172.3f};
static const float kASCALE[8] = {0.0f, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f};
static const float kPSCALE[8] = {0.0f, 7.0f, 13.5f, 27.0f, 55.0f, 112.0f, 230.0f, 494.0f};

static inline int32_t sext(int32_t v, int bits) {
    const int s = 32 - bits;
    return (int32_t)((uint32_t)v << s) >> s;
}

static uint16_t pack(int32_t val) {
    const int sign = (val >> 23) & 1;
    uint32_t temp = (uint32_t)(val ^ (val << 1)) & 0xFFFFFF;
    int exponent = 0;
    for (int k = 0; k < 12; k++) {
        if (temp & 0x800000) break;
        temp <<= 1;
        exponent += 1;
    }
    if (exponent < 12) val = (val << exponent) & 0x3FFFFF;
    else val <<= 11;
    val >>= 11;
    val &= 0x7FF;
    val |= sign << 15;
    val |= exponent << 11;
    return (uint16_t)val;
}

static int32_t unpack(uint16_t val) {
    const int sign = (val >> 15) & 1;
    int exponent = (val >> 11) & 0xF;
    const int mantissa = val & 0x7FF;
    int32_t uval = mantissa << 11;
    if (exponent > 11) {
        exponent = 11;
        uval |= sign << 22;
    } else {
        uval |= (sign ^ 1) << 22;
    }
    uval |= sign << 23;
    uval = sext(uval, 24);
    uval >>= exponent;
    return uval;
}


// Slot register fields.
#define KEYONEX(s) ((s)->data[0x0] & 0x1000)
#define KEYONB(s) ((s)->data[0x0] & 0x0800)
#define SBCTL(s) (((s)->data[0x0] >> 0x9) & 0x0003)
#define SSCTL(s) (((s)->data[0x0] >> 0x7) & 0x0003)
#define LPCTL(s) (((s)->data[0x0] >> 0x5) & 0x0003)
#define PCM8B(s) ((s)->data[0x0] & 0x0010)
#define SA(s) (((uint32_t)((s)->data[0x0] & 0xF) << 16) | (s)->data[0x1])
#define LSA(s) ((s)->data[0x2])
#define LEA(s) ((s)->data[0x3])
#define D2R(s) (((s)->data[0x4] >> 0xB) & 0x001F)
#define D1R(s) (((s)->data[0x4] >> 0x6) & 0x001F)
#define EGHOLD(s) ((s)->data[0x4] & 0x0020)
#define AR(s) ((s)->data[0x4] & 0x001F)
#define LPSLNK(s) ((s)->data[0x5] & 0x4000)
#define KRS(s) (((s)->data[0x5] >> 0xA) & 0x000F)
#define DL(s) (((s)->data[0x5] >> 0x5) & 0x001F)
#define RR(s) ((s)->data[0x5] & 0x001F)
#define STWINH(s) ((s)->data[0x6] & 0x0200)
#define SDIR(s) ((s)->data[0x6] & 0x0100)
#define TL(s) ((s)->data[0x6] & 0x00FF)
#define MDL(s) (((s)->data[0x7] >> 0xC) & 0x000F)
#define MDXSL(s) (((s)->data[0x7] >> 0x6) & 0x003F)
#define MDYSL(s) ((s)->data[0x7] & 0x003F)
#define OCT(s) (((s)->data[0x8] >> 0xB) & 0x000F)
#define FNS(s) ((s)->data[0x8] & 0x03FF)
#define LFORE(s) ((s)->data[0x9] & 0x8000)
#define LFOF(s) (((s)->data[0x9] >> 0xA) & 0x001F)
#define PLFOWS(s) (((s)->data[0x9] >> 0x8) & 0x0003)
#define PLFOS(s) (((s)->data[0x9] >> 0x5) & 0x0007)
#define ALFOWS(s) (((s)->data[0x9] >> 0x3) & 0x0003)
#define ALFOS(s) ((s)->data[0x9] & 0x0007)
#define ISEL(s) (((s)->data[0xA] >> 0x3) & 0x000F)
#define IMXL(s) ((s)->data[0xA] & 0x0007)
#define DISDL(s) (((s)->data[0xB] >> 0xD) & 0x0007)
#define DIPAN(s) (((s)->data[0xB] >> 0x8) & 0x001F)
#define EFSDL(s) (((s)->data[0xB] >> 0x5) & 0x0007)
#define EFPAN(s) ((s)->data[0xB] & 0x001F)


static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint8_t ram_byte(const scspd *c, uint32_t a) { return c->ram ? c->ram[a & c->ram_mask] : 0; }
static inline uint16_t ram_word(const scspd *c, uint32_t a)
{
    return c->ram ? (uint16_t)(c->ram[a & c->ram_mask] << 8 | c->ram[(a + 1) & c->ram_mask]) : 0;
}
static inline void ram_wword(scspd *c, uint32_t a, uint16_t v)
{
    if (!c->ram) return;
    c->ram[a & c->ram_mask] = (uint8_t)(v >> 8);
    c->ram[(a + 1) & c->ram_mask] = (uint8_t)v;
}

void scspd_init(scspd *c)
{
    memset(c, 0, sizeof *c);
    c->noise = 0x12345678;
    for (int i = 0; i < 0x400; ++i) {
        const float env_db = (float)(3 * (i - 0x3ff)) / 32.0f;
        c->eg_table[i] = (int32_t)(powf(10.0f, env_db / 20.0f) * (float)(1 << SHIFT));
    }
    for (int i = 0; i < 0x10000; ++i) {
        const int itl = i & 0xff, ipan = (i >> 8) & 0x1f, isdl = (i >> 13) & 7;
        float db = 0;
        if (itl & 0x01) db -= 0.4f;
        if (itl & 0x02) db -= 0.8f;
        if (itl & 0x04) db -= 1.5f;
        if (itl & 0x08) db -= 3.0f;
        if (itl & 0x10) db -= 6.0f;
        if (itl & 0x20) db -= 12.0f;
        if (itl & 0x40) db -= 24.0f;
        if (itl & 0x80) db -= 48.0f;
        const float tl = powf(10.0f, db / 20.0f);
        db = 0;
        if (ipan & 0x1) db -= 3.0f;
        if (ipan & 0x2) db -= 6.0f;
        if (ipan & 0x4) db -= 12.0f;
        if (ipan & 0x8) db -= 24.0f;
        const float pan = (ipan & 0xf) == 0xf ? 0.0f : powf(10.0f, db / 20.0f);
        float lpan, rpan;
        if (ipan < 0x10) { lpan = pan; rpan = 1.0f; }
        else { rpan = pan; lpan = 1.0f; }
        const float sdl = isdl ? powf(10.0f, kSDLT[isdl] / 20.0f) : 0.0f;
        c->lpan[i] = (int)fix(4.0f * lpan * tl * sdl);
        c->rpan[i] = (int)fix(4.0f * rpan * tl * sdl);
    }
    c->ar_table[0] = c->dr_table[0] = 0;
    c->ar_table[1] = c->dr_table[1] = 0;
    for (int i = 2; i < 64; ++i) {
        const double scale = (double)(1 << EG_SHIFT);
        double t = kARTimes[i];
        if (t != 0.0) c->ar_table[i] = (int)((1023 * 1000.0) / (44100.0 * t) * scale);
        else c->ar_table[i] = 1024 << EG_SHIFT;
        t = kDRTimes[i];
        c->dr_table[i] = (int)((1023 * 1000.0) / (44100.0 * t) * scale);
    }
    uint32_t rnd = 0x2545F491;
    for (int i = 0; i < 256; ++i) {
        int a, p;
        a = 255 - i;
        p = i < 128 ? i : i - 256;
        c->alfo_saw[i] = a;
        c->plfo_saw[i] = p;
        if (i < 128) { a = 255; p = 127; }
        else { a = 0; p = -128; }
        c->alfo_sqr[i] = a;
        c->plfo_sqr[i] = p;
        a = i < 128 ? 255 - i * 2 : i * 2 - 256;
        if (i < 64) p = i * 2;
        else if (i < 128) p = 255 - i * 2;
        else if (i < 192) p = 256 - i * 2;
        else p = i * 2 - 511;
        c->alfo_tri[i] = a;
        c->plfo_tri[i] = p;
        rnd = rnd * 1103515245u + 12345u;
        a = (int)((rnd >> 16) & 0xff);
        p = 128 - a;
        c->alfo_noi[i] = a;
        c->plfo_noi[i] = p;
    }
    for (int s = 0; s < 8; ++s) {
        float limit = kPSCALE[s];
        for (int i = -128; i < 128; ++i)
            c->pscales[s][i + 128] = (int)(uint32_t)((float)(1 << LFO_SHIFT) * powf(2.0f, (limit * (float)i) / 128.0f / 1200.0f));
        limit = -kASCALE[s];
        for (int i = 0; i < 256; ++i)
            c->ascales[s][i] = (int)(uint32_t)((float)(1 << LFO_SHIFT) * powf(10.0f, (limit * (float)i) / 256.0f / 20.0f));
    }
    scspd_reset(c);
}

void scspd_reset(scspd *c)
{
    memset(c->regs, 0, sizeof c->regs);
    for (int i = 0; i < 32; ++i) {
        memset(&c->slots[i], 0, sizeof c->slots[i]);
        c->slots[i].slot = i;
        c->slots[i].eg.state = kRelease;
    }
    memset(c->ringbuf, 0, sizeof c->ringbuf);
    c->bufptr = 0;
    c->irq_tima = c->irq_timbc = c->irq_midi = c->irq_cpu = c->irq_dma = 0;
    c->latched_mslc = 0;
    c->latched_mslc_data = 0;
    c->midi_w = c->midi_r = 0;
    c->tim_cnt[0] = c->tim_cnt[1] = c->tim_cnt[2] = 0xffff;
    memset(&c->dma, 0, sizeof c->dma);
    memset(&c->dsp, 0, sizeof c->dsp);
    c->dsp.rbl = 8 * 1024;
    c->dsp.stopped = true;
}

static int get_ar(const scspd *c, int base, int r) { return c->ar_table[clampi(base + (r << 1), 0, 63)]; }
static int get_dr(const scspd *c, int base, int r) { return c->dr_table[clampi(base + (r << 1), 0, 63)]; }

static void compute_eg(scspd *c, scsp_slot *s)
{
    const int octave = ((int)OCT(s) ^ 8) - 8;
    int rate;
    if (KRS(s) != 0xf) rate = octave + 2 * (int)KRS(s) + ((FNS(s) >> 9) & 1);
    else rate = 0;
    s->eg.volume = 0x17F << EG_SHIFT;
    s->eg.ar = get_ar(c, rate, AR(s));
    s->eg.d1r = get_dr(c, rate, D1R(s));
    s->eg.d2r = get_dr(c, rate, D2R(s));
    s->eg.rr = get_dr(c, rate, RR(s));
    s->eg.dl = 0x1f - DL(s);
    s->eg.eghold = (uint8_t)(EGHOLD(s) ? 1 : 0);
}

static void stop_slot(scsp_slot *s, bool keyoff)
{
    if (keyoff) s->eg.state = kRelease;
    else s->active = 0;
    s->data[0] &= (uint16_t)~0x800;
}

static int eg_update(scsp_slot *s)
{
    switch (s->eg.state) {
    case kAttack:
        s->eg.volume += s->eg.ar;
        if (s->eg.volume >= (0x3ff << EG_SHIFT)) {
            if (!LPSLNK(s)) {
                s->eg.state = kDecay1;
                if (s->eg.d1r >= (1024 << EG_SHIFT)) s->eg.state = kDecay2;
            }
            s->eg.volume = 0x3ff << EG_SHIFT;
        }
        if (s->eg.eghold) return 0x3ff << (SHIFT - 10);
        break;
    case kDecay1:
        s->eg.volume -= s->eg.d1r;
        if (s->eg.volume <= 0) s->eg.volume = 0;
        if (s->eg.volume >> (EG_SHIFT + 5) <= s->eg.dl) s->eg.state = kDecay2;
        break;
    case kDecay2:
        if (D2R(s) == 0) return (s->eg.volume >> EG_SHIFT) << (SHIFT - 10);
        s->eg.volume -= s->eg.d2r;
        if (s->eg.volume <= 0) s->eg.volume = 0;
        break;
    case kRelease:
        s->eg.volume -= s->eg.rr;
        if (s->eg.volume <= 0) {
            s->eg.volume = 0;
            stop_slot(s, false);
        }
        break;
    }
    return (s->eg.volume >> EG_SHIFT) << (SHIFT - 10);
}

static uint32_t step_of(const scsp_slot *s)
{
    const int octave = ((int)OCT(s) ^ 8) - 8 + SHIFT - 10;
    uint32_t fn = FNS(s) + (1u << 10);
    if (octave >= 0) fn <<= octave;
    else fn >>= -octave;
    return fn;
}

static void lfo_compute_step(scspd *c, scsp_lfo *lfo, uint32_t lfof, uint32_t lfows, uint32_t lfos, bool alfo)
{
    const float step = kLFOFreq[lfof] * 256.0f / 44100.0f;
    lfo->phase_step = (uint32_t)((float)(1 << LFO_SHIFT) * step);
    if (alfo) {
        switch (lfows) {
        case 0: lfo->table = c->alfo_saw; break;
        case 1: lfo->table = c->alfo_sqr; break;
        case 2: lfo->table = c->alfo_tri; break;
        default: lfo->table = c->alfo_noi; break;
        }
        lfo->scale = c->ascales[lfos];
    } else {
        switch (lfows) {
        case 0: lfo->table = c->plfo_saw; break;
        case 1: lfo->table = c->plfo_sqr; break;
        case 2: lfo->table = c->plfo_tri; break;
        default: lfo->table = c->plfo_noi; break;
        }
        lfo->scale = c->pscales[lfos];
    }
}

static void compute_lfo(scspd *c, scsp_slot *s)
{
    if (PLFOS(s) != 0) lfo_compute_step(c, &s->plfo, LFOF(s), PLFOWS(s), PLFOS(s), false);
    if (ALFOS(s) != 0) lfo_compute_step(c, &s->alfo, LFOF(s), ALFOWS(s), ALFOS(s), true);
}

static int32_t plfo_step(scsp_lfo *lfo)
{
    lfo->phase = (uint16_t)(lfo->phase + lfo->phase_step);
    int p = lfo->table ? lfo->table[(lfo->phase >> LFO_SHIFT) & 0xff] : 0;
    p = lfo->scale ? lfo->scale[p + 128] : (1 << LFO_SHIFT);
    return p << (SHIFT - LFO_SHIFT);
}

static int32_t alfo_step(scsp_lfo *lfo)
{
    lfo->phase = (uint16_t)(lfo->phase + lfo->phase_step);
    int p = lfo->table ? lfo->table[(lfo->phase >> LFO_SHIFT) & 0xff] : 0;
    p = lfo->scale ? lfo->scale[p] : (1 << LFO_SHIFT);
    return p << (SHIFT - LFO_SHIFT);
}

static void start_slot(scspd *c, scsp_slot *s)
{
    s->active = 1;
    s->cur_addr = 0;
    s->nxt_addr = 1 << SHIFT;
    s->step = step_of(s);
    compute_eg(c, s);
    s->eg.state = kAttack;
    s->eg.volume = 0x17F << EG_SHIFT;
    s->prev = 0;
    s->backwards = 0;
    compute_lfo(c, s);
}

static void update_slot_reg(scspd *c, int sl, int r)
{
    scsp_slot *s = &c->slots[sl];
    switch (r & 0x3f) {
    case 0: case 1:
        if (KEYONEX(s)) {
            for (int i = 0; i < 32; i++) {
                scsp_slot *s2 = &c->slots[i];
                if (KEYONB(s2) && s2->eg.state == kRelease) start_slot(c, s2);
                if (!KEYONB(s2)) stop_slot(s2, true);
            }
            s->data[0] &= (uint16_t)~0x1000;
        }
        break;
    case 0x10: case 0x11: s->step = step_of(s); break;
    case 0xA: case 0xB:
        s->eg.rr = get_dr(c, 0, RR(s));
        s->eg.dl = 0x1f - DL(s);
        break;
    case 0x12: case 0x13: compute_lfo(c, s); break;
    default: break;
    }
}

static uint8_t decode_sci(const scspd *c, int irq)
{
    uint8_t sci = 0;
    if (c->regs[0x24 / 2] & (1 << irq)) sci |= 1;
    if (c->regs[0x26 / 2] & (1 << irq)) sci |= 2;
    if (c->regs[0x28 / 2] & (1 << irq)) sci |= 4;
    return sci;
}

static void exec_dma(scspd *c);

static void update_reg(scspd *c, int r)
{
    switch (r & 0x3f) {
    case 0x2: case 0x3:
        c->dsp.rbl = (8 * 1024u) << ((c->regs[1] >> 7) & 3);
        c->dsp.rbp = c->regs[1] & 0x3f;
        break;
    case 0x8: case 0x9: c->latched_mslc = (uint8_t)((c->regs[0x8 / 2] & 0xf800) >> 11); break;
    case 0x12: case 0x13: c->dma.dmea = (c->regs[0x12 / 2] & 0xfffe) | (c->dma.dmea & 0xf0000); break;
    case 0x14: case 0x15:
        c->dma.dmea = ((uint32_t)(c->regs[0x14 / 2] & 0xf000) << 4) | (c->dma.dmea & 0xfffe);
        c->dma.drga = c->regs[0x14 / 2] & 0x0ffe;
        break;
    case 0x16: case 0x17:
        c->dma.dtlg = c->regs[0x16 / 2] & 0x0ffe;
        c->dma.ddir = (uint8_t)((c->regs[0x16 / 2] & 0x2000) >> 13);
        c->dma.dgate = (uint8_t)((c->regs[0x16 / 2] & 0x4000) >> 14);
        if (c->regs[0x16 / 2] & 0x1000) exec_dma(c);
        break;
    case 0x18: case 0x19: c->tim_cnt[0] = (c->regs[0x18 / 2] & 0xff) << 8; break;
    case 0x1a: case 0x1b: c->tim_cnt[1] = (c->regs[0x1a / 2] & 0xff) << 8; break;
    case 0x1c: case 0x1d: c->tim_cnt[2] = (c->regs[0x1c / 2] & 0xff) << 8; break;
    case 0x22: case 0x23: c->regs[0x20 / 2] &= (uint16_t)~c->regs[0x22 / 2]; break;
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
        c->irq_tima = decode_sci(c, 6);
        c->irq_timbc = decode_sci(c, 7);
        c->irq_midi = decode_sci(c, 3);
        c->irq_cpu = decode_sci(c, 5);
        c->irq_dma = decode_sci(c, 4);
        break;
    default: break;
    }
}

static void update_reg_read(scspd *c, int r)
{
    switch (r & 0x3f) {
    case 4: case 5: {
        uint16_t v = c->regs[0x4 / 2] & 0xff00;
        v |= c->midi_stack[c->midi_r];
        if (c->midi_r != c->midi_w) ++c->midi_r;
        if (c->midi_r == c->midi_w) c->regs[0x20 / 2] &= (uint16_t)~8;
        c->regs[0x4 / 2] = v;
        break;
    }
    case 8: case 9: c->regs[0x8 / 2] = c->latched_mslc_data; break;
    default: break;
    }
}

static void dsp_start(scspd *c)
{
    c->dsp.stopped = false;
    int i;
    for (i = 127; i >= 0; --i) {
        const uint16_t *p = c->dsp.mpro + i * 4;
        if (p[0] || p[1] || p[2] || p[3]) break;
    }
    c->dsp.last_step = i + 1;
}

static void w16(scspd *c, uint32_t addr, uint16_t val)
{
    addr &= 0xffff;
    if (addr < 0x400) {
        const int sl = (int)(addr / 0x20);
        addr &= 0x1f;
        c->slots[sl].data[addr / 2] = val;
        update_slot_reg(c, sl, (int)addr);
    } else if (addr < 0x600) {
        if (addr < 0x430) {
            if (addr == 0x420 || addr == 0x42e) c->regs[(addr & 0x3f) / 2] |= val & 0x20;
            else c->regs[(addr & 0x3f) / 2] = val;
            update_reg(c, (int)(addr & 0x3f));
        }
    } else if (addr < 0x700) c->ringbuf[(addr - 0x600) / 2] = (int16_t)val;
    else if (addr < 0x780) c->dsp.coef[(addr - 0x700) / 2] = (int16_t)val;
    else if (addr < 0x7c0) c->dsp.madrs[(addr - 0x780) / 2] = val;
    else if (addr < 0x800) c->dsp.madrs[(addr - 0x7c0) / 2] = val;
    else if (addr < 0xC00) {
        c->dsp.mpro[(addr - 0x800) / 2] = val;
        if (addr == 0xBF0) dsp_start(c);
    }
}

static uint16_t r16(scspd *c, uint32_t addr)
{
    addr &= 0xffff;
    if (addr < 0x400) return c->slots[addr / 0x20].data[(addr & 0x1f) / 2];
    if (addr < 0x600) {
        if (addr < 0x430) {
            update_reg_read(c, (int)(addr & 0x3f));
            return c->regs[(addr & 0x3f) / 2];
        }
        return 0;
    }
    if (addr < 0x700) return (uint16_t)c->ringbuf[(addr - 0x600) / 2];
    if (addr < 0x780) return (uint16_t)c->dsp.coef[(addr - 0x700) / 2];
    if (addr < 0x7c0) return c->dsp.madrs[(addr - 0x780) / 2];
    if (addr < 0x800) return c->dsp.madrs[(addr - 0x7c0) / 2];
    if (addr < 0xC00) return c->dsp.mpro[(addr - 0x800) / 2];
    if (addr < 0xE00) {
        const int32_t t = c->dsp.temp[(addr >> 2) & 0x7f];
        return (addr & 2) ? (uint16_t)(t & 0xffff) : (uint16_t)((uint32_t)t >> 16);
    }
    if (addr < 0xE80) {
        const int32_t t = c->dsp.mems[(addr >> 2) & 0x1f];
        return (addr & 2) ? (uint16_t)(t & 0xffff) : (uint16_t)((uint32_t)t >> 16);
    }
    if (addr < 0xEC0) {
        const int32_t t = c->dsp.mixs[(addr >> 2) & 0xf];
        return (addr & 2) ? (uint16_t)(t & 0xffff) : (uint16_t)((uint32_t)t >> 16);
    }
    if (addr < 0xEE0) return (uint16_t)c->dsp.efreg[(addr - 0xec0) / 2];
    if (addr < 0xEE4) return (uint16_t)c->dsp.exts[(addr - 0xee0) / 2];
    return 0;
}

uint16_t scspd_read16(scspd *c, uint32_t offset) { return r16(c, offset); }

void scspd_write16(scspd *c, uint32_t offset, uint16_t data, uint16_t mem_mask)
{
    uint16_t tmp = r16(c, offset & ~1u);
    tmp = (uint16_t)((tmp & ~mem_mask) | (data & mem_mask));
    w16(c, offset & ~1u, tmp);
}

void scspd_midi_in(scspd *c, uint8_t data)
{
    c->midi_stack[c->midi_w++] = data;
    if (c->midi_w == c->midi_r) ++c->midi_r;
}

static void exec_dma(scspd *c)
{
    uint16_t saved[3] = {0, 0, 0};
    if (!c->dma.ddir)
        for (int i = 0; i < 3; i++) saved[i] = c->regs[(0x12 + i * 2) / 2];
    if (c->dma.ddir) {
        for (int i = 0; i < c->dma.dtlg; i += 2) {
            ram_wword(c, c->dma.dmea, c->dma.dgate ? 0 : r16(c, c->dma.drga));
            c->dma.dmea += 2;
            if (!c->dma.dgate) c->dma.drga = (uint16_t)(c->dma.drga + 2);
        }
    } else {
        for (int i = 0; i < c->dma.dtlg; i += 2) {
            w16(c, c->dma.drga, c->dma.dgate ? 0 : ram_word(c, c->dma.dmea));
            c->dma.drga = (uint16_t)(c->dma.drga + 2);
            if (!c->dma.dgate) c->dma.dmea += 2;
        }
    }
    if (!c->dma.ddir)
        for (int i = 0; i < 3; i++) c->regs[(0x12 + i * 2) / 2] = saved[i];
    c->regs[0x16 / 2] &= (uint16_t)~0x1000;
    c->regs[0x20 / 2] |= 0x10;
}

static int32_t update_slot(scspd *c, scsp_slot *s)
{
    if (SSCTL(s) == 3) return 0;
    int32_t sample = 0;
    int step = (int)s->step;
    uint32_t addr1, addr2;
    if (PLFOS(s) != 0) {
        step = step * plfo_step(&s->plfo);
        step >>= SHIFT;
    }
    if (PCM8B(s)) {
        addr1 = s->cur_addr >> SHIFT;
        addr2 = s->nxt_addr >> SHIFT;
    } else {
        addr1 = (s->cur_addr >> (SHIFT - 1)) & ~1u;
        addr2 = (s->nxt_addr >> (SHIFT - 1)) & ~1u;
    }
    if (MDL(s) != 0 || MDXSL(s) != 0 || MDYSL(s) != 0) {
        int32_t smp = (c->ringbuf[(c->bufptr + MDXSL(s)) & 63] + c->ringbuf[(c->bufptr + MDYSL(s)) & 63]) / 2;
        smp <<= 0xA;
        smp >>= 0x1A - MDL(s);
        if (!PCM8B(s)) smp <<= 1;
        addr1 += (uint32_t)smp;
        addr2 += (uint32_t)smp;
    }
    if (SSCTL(s) == 0) {
        const int32_t fpart = (int32_t)(s->cur_addr & ((1 << SHIFT) - 1));
        if (PCM8B(s)) {
            const int8_t p1 = (int8_t)ram_byte(c, SA(s) + addr1);
            const int8_t p2 = (int8_t)ram_byte(c, SA(s) + addr2);
            const int32_t v = (int32_t)(p1 << 8) * ((1 << SHIFT) - fpart) + (int32_t)(p2 << 8) * fpart;
            sample = v >> SHIFT;
        } else {
            const int16_t p1 = (int16_t)ram_word(c, SA(s) + addr1);
            const int16_t p2 = (int16_t)ram_word(c, SA(s) + addr2);
            const int32_t v = (int32_t)p1 * ((1 << SHIFT) - fpart) + (int32_t)p2 * fpart;
            sample = v >> SHIFT;
        }
    } else if (SSCTL(s) == 1) {
        c->noise = c->noise * 1664525u + 1013904223u;
        sample = (int16_t)(c->noise >> 16);
    } else sample = 0;
    if (SBCTL(s) & 1) sample ^= 0x7FFF;
    if (SBCTL(s) & 2) sample = (int16_t)(sample ^ 0x8000);

    if (s->backwards) s->cur_addr -= (uint32_t)step;
    else s->cur_addr += (uint32_t)step;
    s->nxt_addr = s->cur_addr + (1 << SHIFT);

    addr1 = s->cur_addr >> SHIFT;
    addr2 = s->nxt_addr >> SHIFT;

    if (addr1 >= LSA(s) && !s->backwards)
        if (LPSLNK(s) && s->eg.state == kAttack) s->eg.state = kDecay1;

    uint32_t *addr[2] = {&addr1, &addr2};
    uint32_t *slot_addr[2] = {&s->cur_addr, &s->nxt_addr};
    for (int sel = 0; sel < 2; sel++) {
        int32_t rem;
        switch (LPCTL(s)) {
        case 0:
            if (*addr[sel] >= LSA(s) && *addr[sel] >= LEA(s)) stop_slot(s, false);
            break;
        case 1:
            if (*addr[sel] >= LEA(s)) {
                rem = (int32_t)(*slot_addr[sel] - ((uint32_t)LEA(s) << SHIFT));
                *slot_addr[sel] = ((uint32_t)LSA(s) << SHIFT) + (uint32_t)rem;
            }
            break;
        case 2:
            if (*addr[sel] >= LSA(s) && !s->backwards) {
                rem = (int32_t)(*slot_addr[sel] - ((uint32_t)LSA(s) << SHIFT));
                *slot_addr[sel] = ((uint32_t)LEA(s) << SHIFT) - (uint32_t)rem;
                s->backwards = 1;
            } else if ((*addr[sel] < LSA(s) || (*slot_addr[sel] & 0x80000000u)) && s->backwards) {
                rem = (int32_t)(((uint32_t)LSA(s) << SHIFT) - *slot_addr[sel]);
                *slot_addr[sel] = ((uint32_t)LEA(s) << SHIFT) - (uint32_t)rem;
            }
            break;
        case 3:
            if (*addr[sel] >= LEA(s)) {
                rem = (int32_t)(*slot_addr[sel] - ((uint32_t)LEA(s) << SHIFT));
                *slot_addr[sel] = ((uint32_t)LEA(s) << SHIFT) - (uint32_t)rem;
                s->backwards = 1;
            } else if ((*addr[sel] < LSA(s) || (*slot_addr[sel] & 0x80000000u)) && s->backwards) {
                rem = (int32_t)(((uint32_t)LSA(s) << SHIFT) - *slot_addr[sel]);
                *slot_addr[sel] = ((uint32_t)LSA(s) << SHIFT) + (uint32_t)rem;
                s->backwards = 0;
            }
            break;
        }
    }

    if (!SDIR(s)) {
        if (ALFOS(s) != 0) {
            sample = sample * alfo_step(&s->alfo);
            sample >>= SHIFT;
        }
        if (s->eg.state == kAttack) sample = (sample * eg_update(s)) >> SHIFT;
        else sample = (sample * c->eg_table[(eg_update(s) >> (SHIFT - 10)) & 0x3ff]) >> SHIFT;
    }

    if (!STWINH(s)) {
        const uint16_t enc = SDIR(s) ? (uint16_t)(0x7 << 0xd) : (uint16_t)(TL(s) | (0x7 << 0xd));
        *c->rbufdst = (int16_t)((sample * c->lpan[enc]) >> (SHIFT + 1));
    }
    return sample;
}

static void timers_tick(scspd *c)
{
    static const int kReg[3] = {0x18 / 2, 0x1a / 2, 0x1c / 2};
    static const uint16_t kBit[3] = {0x40, 0x80, 0x100};
    for (int t = 0; t < 3; ++t) {
        if (c->tim_cnt[t] <= 0xff00) {
            c->tim_cnt[t] += 1 << (8 - ((c->regs[kReg[t]] >> 8) & 7));
            if (c->tim_cnt[t] > 0xff00) {
                c->tim_cnt[t] = 0xffff;
                c->regs[0x20 / 2] |= kBit[t];
            }
            c->regs[kReg[t]] = (uint16_t)((c->regs[kReg[t]] & 0xff00) | (c->tim_cnt[t] >> 8));
        }
    }
}

int scspd_irq_level(scspd *c)
{
    if (c->midi_w != c->midi_r) c->regs[0x20 / 2] |= 8;
    const uint32_t pend = c->regs[0x20 / 2], en = c->regs[0x1e / 2];
    int level = 0;
#define MAXL(x) do { if ((x) > level) level = (x); } while (0)
    if (pend & en & 0x40) MAXL(c->irq_tima);
    if (pend & en & 0x80) MAXL(c->irq_timbc);
    if (pend & en & 0x100) MAXL(c->irq_timbc);
    if (pend & en & 0x08) MAXL(c->irq_midi);
    if (pend & en & 0x20) MAXL(c->irq_cpu);
    if (pend & en & 0x10) MAXL(c->irq_dma);
#undef MAXL
    return level;
}

static inline int32_t clamp24(int32_t v) { return v < -0x00800000 ? -0x00800000 : v > 0x007fffff ? 0x007fffff : v; }

static void dsp_step(scspd *c)
{
    scsp_dsp *d = &c->dsp;
    if (d->stopped) {
        memset(d->mixs, 0, sizeof d->mixs);
        return;
    }
    memset(d->efreg, 0, sizeof d->efreg);
    int32_t acc = 0, memval = 0, frc_reg = 0, y_reg = 0;
    uint32_t adrs_reg = 0;
    for (int step = 0; step < d->last_step; ++step) {
        const uint16_t *ip = d->mpro + step * 4;
        const uint32_t tra = (ip[0] >> 8) & 0x7f, twt = (ip[0] >> 7) & 1, twa = ip[0] & 0x7f;
        const uint32_t xsel = (ip[1] >> 15) & 1, ysel = (ip[1] >> 13) & 3, ira = (ip[1] >> 6) & 0x3f;
        const uint32_t iwt = (ip[1] >> 5) & 1, iwa = ip[1] & 0x1f;
        const uint32_t table = (ip[2] >> 15) & 1, mwt = (ip[2] >> 14) & 1, mrd = (ip[2] >> 13) & 1;
        const uint32_t ewt = (ip[2] >> 12) & 1, ewa = (ip[2] >> 8) & 0xf, adrl = (ip[2] >> 7) & 1;
        const uint32_t frcl = (ip[2] >> 6) & 1, shift = (ip[2] >> 4) & 3, yrl = (ip[2] >> 3) & 1;
        const uint32_t negb = (ip[2] >> 2) & 1, zero = (ip[2] >> 1) & 1, bsel = ip[2] & 1;
        const uint32_t nofl = (ip[3] >> 15) & 1, coef = (ip[3] >> 9) & 0x3f, masa = (ip[3] >> 2) & 0x1f;
        const uint32_t adreb = (ip[3] >> 1) & 1, nxadr = ip[3] & 1;

        int32_t inputs;
        if (ira <= 0x1f) inputs = d->mems[ira];
        else if (ira <= 0x2f) inputs = d->mixs[ira - 0x20] << 4;
        else if (ira <= 0x31) inputs = d->exts[ira - 0x30] << 8;
        else break;
        inputs = sext(inputs, 24);
        if (iwt) {
            d->mems[iwa] = memval;
            if (ira == iwa) inputs = memval;
        }
        int32_t b;
        if (!zero) {
            b = bsel ? acc : sext(d->temp[(tra + d->dec) & 0x7f], 24);
            if (negb) b = 0 - b;
        } else b = 0;
        const int32_t x = xsel ? inputs : sext(d->temp[(tra + d->dec) & 0x7f], 24);
        int32_t y = 0;
        if (ysel == 0) y = frc_reg;
        else if (ysel == 1) y = d->coef[coef] >> 3;
        else if (ysel == 2) y = (y_reg >> 11) & 0x1fff;
        else y = (y_reg >> 4) & 0x0fff;
        if (yrl) y_reg = inputs;
        int32_t shifted;
        if (shift == 0) shifted = clamp24(acc);
        else if (shift == 1) shifted = clamp24(acc * 2);
        else if (shift == 2) shifted = sext(acc * 2, 24);
        else shifted = sext(acc, 24);
        y = sext(y, 13);
        const int64_t v = ((int64_t)x * (int64_t)y) >> 12;
        acc = (int32_t)(v + b);
        if (twt) d->temp[(twa + d->dec) & 0x7f] = shifted;
        if (frcl) frc_reg = shift == 3 ? (shifted & 0x0fff) : ((shifted >> 11) & 0x1fff);
        if (mrd || mwt) {
            uint32_t a = d->madrs[masa];
            if (!table) a += d->dec;
            if (adreb) a += adrs_reg & 0x0fff;
            if (nxadr) a++;
            if (!table) a &= d->rbl - 1;
            else a &= 0xffff;
            a += d->rbp << 12;
            a <<= 1;
            if (mrd && (step & 1)) memval = nofl ? (int32_t)ram_word(c, a) << 8 : unpack(ram_word(c, a));
            if (mwt && (step & 1)) ram_wword(c, a, nofl ? (uint16_t)(shifted >> 8) : pack(shifted));
        }
        if (adrl) adrs_reg = shift == 3 ? (uint32_t)((shifted >> 12) & 0xfff) : (uint32_t)(inputs >> 16);
        if (ewt) d->efreg[ewa] = (int16_t)(d->efreg[ewa] + (shifted >> 8));
    }
    --d->dec;
    memset(d->mixs, 0, sizeof d->mixs);
}

void scspd_sample(scspd *c, int32_t *left, int32_t *right)
{
    int32_t smpl = 0, smpr = 0;
    for (int sl = 0; sl < 32; ++sl) {
        c->rbufdst = c->ringbuf + c->bufptr;
        scsp_slot *s = &c->slots[sl];
        if (s->active) {
            const int32_t sample = update_slot(c, s);
            const uint16_t eff_tl = SDIR(s) ? 0 : TL(s);
            uint16_t enc = (uint16_t)(eff_tl | (IMXL(s) << 0xd));
            const int sel = ISEL(s);
            c->dsp.mixs[sel] += (sample * c->lpan[enc]) >> (SHIFT - 2);
            enc = (uint16_t)(eff_tl | (DIPAN(s) << 0x8) | (DISDL(s) << 0xd));
            smpl += (sample * c->lpan[enc]) >> SHIFT;
            smpr += (sample * c->rpan[enc]) >> SHIFT;
        }
        c->bufptr = (uint8_t)((c->bufptr + 1) & 63);
    }
    dsp_step(c);
    for (int i = 0; i < 16; ++i) {
        const scsp_slot *s = &c->slots[i];
        if (EFSDL(s)) {
            const uint16_t enc = (uint16_t)((EFPAN(s) << 0x8) | (EFSDL(s) << 0xd));
            smpl += (c->dsp.efreg[i] * c->lpan[enc]) >> SHIFT;
            smpr += (c->dsp.efreg[i] * c->rpan[enc]) >> SHIFT;
        }
    }
    const int mvol = c->regs[0] & 0xf;
    *left = (smpl >> 2) * mvol / 15;
    *right = (smpr >> 2) * mvol / 15;

    const scsp_slot *m = &c->slots[c->latched_mslc & 31];
    const uint32_t sgc = (uint32_t)m->eg.state & 3;
    const uint32_t ca = (m->cur_addr >> (SHIFT + 12)) & 0xf;
    const uint32_t eg = (0x1f - (m->eg.volume >> (EG_SHIFT + 5))) & 0x1f;
    c->latched_mslc_data = (uint16_t)((ca << 7) | (sgc << 5) | eg);
    timers_tick(c);
}
