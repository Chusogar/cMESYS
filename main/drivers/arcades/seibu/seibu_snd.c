// seibu_snd.c - the Seibu sound system (see seibu_snd.h).
#include <string.h>
#include "seibu_snd.h"
#include "../common/arc.h"

static inline int bit(uint32_t v, int n) { return (v >> n) & 1; }

static uint8_t swap8(uint8_t v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0)
{
    return (uint8_t)(bit(v, b7) << 7 | bit(v, b6) << 6 | bit(v, b5) << 5 | bit(v, b4) << 4 | bit(v, b3) << 3 |
                     bit(v, b2) << 2 | bit(v, b1) << 1 | bit(v, b0));
}

static uint8_t decrypt_data(uint32_t a, uint8_t s)
{
    if (bit(a, 9) && bit(a, 8)) s ^= 0x80;
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) s ^= 0x40;
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) s ^= 0x04;
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) s ^= 0x02;
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) s ^= 0x01;
    if (bit(a, 13) && bit(a, 4)) s = swap8(s, 7, 6, 5, 4, 3, 2, 0, 1);
    if (bit(a, 8) && bit(a, 4)) s = swap8(s, 7, 6, 5, 4, 2, 3, 1, 0);
    return s;
}

static uint8_t decrypt_opcode(uint32_t a, uint8_t s)
{
    if (bit(a, 9) && bit(a, 8)) s ^= 0x80;
    if (bit(a, 11) && bit(a, 4) && bit(a, 1)) s ^= 0x40;
    if (!bit(a, 13) && bit(a, 12)) s ^= 0x20;
    if (!bit(a, 6) && bit(a, 1)) s ^= 0x10;
    if (!bit(a, 12) && bit(a, 2)) s ^= 0x08;
    if (bit(a, 11) && !bit(a, 8) && bit(a, 1)) s ^= 0x04;
    if (bit(a, 13) && !bit(a, 6) && bit(a, 4)) s ^= 0x02;
    if (!bit(a, 11) && bit(a, 9) && bit(a, 2)) s ^= 0x01;
    if (bit(a, 13) && bit(a, 4)) s = swap8(s, 7, 6, 5, 4, 3, 2, 0, 1);
    if (bit(a, 8) && bit(a, 4)) s = swap8(s, 7, 6, 5, 4, 2, 3, 1, 0);
    if (bit(a, 12) && bit(a, 9)) s = swap8(s, 7, 6, 4, 5, 3, 2, 1, 0);
    if (bit(a, 11) && !bit(a, 6)) s = swap8(s, 6, 7, 5, 4, 3, 2, 1, 0);
    return s;
}

void seibu_prepare(const uint8_t *rom, bool encrypted, uint8_t *out)
{
    for (uint32_t a = 0; a < 0x2000; a++) {
        out[a] = encrypted ? decrypt_data(a, rom[a]) : rom[a];
        out[0x2000 + a] = encrypted ? decrypt_opcode(a, rom[a]) : rom[a];
    }
    memcpy(out + 0x4000, rom + 0x8000, 0x8000);
    memcpy(out + 0xc000, rom, 0x8000);
}

enum { RST10_ASSERT, RST10_CLEAR, RST10_ACK, RST10_EOI, RST18_ASSERT, RST18_ACK, RST18_EOI };

static void update_irq(seibu_snd *s, int ev)
{
    switch (ev) {
    case RST10_ASSERT: s->rst10_irq = true; break;
    case RST10_CLEAR: s->rst10_irq = false; break;
    case RST10_ACK: s->rst10_service = true; break;
    case RST10_EOI: s->rst10_service = false; break;
    case RST18_ASSERT: s->rst18_irq = true; break;
    case RST18_ACK: s->rst18_irq = false; s->rst18_service = true; break;
    case RST18_EOI: s->rst18_service = false; break;
    default: break;
    }
    const bool pending = (s->rst10_irq && !s->rst10_service) || (s->rst18_irq && !s->rst18_service);
    zd_set_irq(&s->z80, pending ? ZD_ASSERT : ZD_CLEAR, s->rst18_irq && !s->rst18_service ? 0xdf : 0xd7);
}

// IM 0: RST 18h first; taking it marks it in service
static void irq_ack(void *u)
{
    seibu_snd *s = u;
    uint8_t vector = 0;
    if (s->rst18_irq && !s->rst18_service) {
        update_irq(s, RST18_ACK);
        vector = 0xdf;
    } else if (s->rst10_irq && !s->rst10_service) {
        update_irq(s, RST10_ACK);
        vector = 0xd7;
    }
    const bool pending = (s->rst10_irq && !s->rst10_service) || (s->rst18_irq && !s->rst18_service);
    zd_set_irq(&s->z80, pending ? ZD_ASSERT : ZD_CLEAR, vector);
}

static void fm_irq(void *u, bool on) { update_irq(u, on ? RST10_ASSERT : RST10_CLEAR); }

static uint8_t rd(void *u, uint16_t a)
{
    seibu_snd *s = u;
    if (a < 0x2000) return s->data[a];
    if (a < 0x2800) return s->ram[a & 0x7ff];
    if (a >= 0x8000) return s->banks[s->bank * 0x8000 + (a & 0x7fff)];
    switch (a) {
#ifdef SEIBU_ADPCM
    case 0x4008: case 0x4009: return ym2151_status(&s->ym);
#else
    case 0x4008: case 0x4009: return ym3812d_status(&s->ym);
#endif
    case 0x4010: return s->latch[0];
    case 0x4011: return s->latch[1];
    case 0x4012: return s->sub2main_pending ? 1 : 0;
    case 0x4013: return s->coins;
#ifndef SEIBU_ADPCM
    case 0x6000: return okid_read(&s->oki);
#endif
    default: return 0xff;
    }
}

static uint8_t op_rd(void *u, uint16_t a) { seibu_snd *s = u; return a < 0x2000 ? s->opcodes[a] : rd(u, a); }

#ifdef SEIBU_ADPCM
static void adpcm_w(msm5205d *m, uint16_t a, uint8_t v)
{
    switch (a & 0xff) {
    case 0x05: m->position = (uint32_t)v << 8; break;
    case 0x06: m->end = (uint32_t)v << 8; break;
    case 0x1a:
        if (v == 0) msm5205d_set_reset(m, true);
        else if (v == 1) msm5205d_set_reset(m, false);
        break;
    default: break;
    }
}
#endif

static void wr(void *u, uint16_t a, uint8_t v)
{
    seibu_snd *s = u;
    if (a >= 0x2000 && a < 0x2800) {
        s->ram[a & 0x7ff] = v;
        return;
    }
#ifdef SEIBU_ADPCM
    switch (a) {
    case 0x4005: case 0x4006: case 0x401a: adpcm_w(&s->adpcm[0], a, v); return;
    case 0x6005: case 0x6006: case 0x601a: adpcm_w(&s->adpcm[1], a, v); return;
    case 0x4007: return;
    case 0x4008: ym2151_select(&s->ym, v); return;
    case 0x4009: ym2151_write(&s->ym, v); return;
    case 0x6000: return;
    default: break;
    }
#endif
    switch (a) {
    case 0x4000:
        s->main2sub_pending = false;
        s->sub2main_pending = true;
        break;
    case 0x4001: update_irq(s, RST18_EOI); break;
    case 0x4002: update_irq(s, RST10_EOI); break;
    case 0x4003: update_irq(s, RST18_EOI); break;
    case 0x4007: s->bank = v & 1; break;
#ifndef SEIBU_ADPCM
    case 0x4008: ym3812d_control(&s->ym, v); break;
    case 0x4009: ym3812d_write(&s->ym, v); break;
    case 0x6000: okid_write(&s->oki, v); break;
#endif
    case 0x4018: s->sub2main[0] = v; break;
    case 0x4019: s->sub2main[1] = v; break;
    default: break;
    }
}

static uint8_t in_ff(void *u, uint16_t p) { (void)u, (void)p; return 0xff; }
static void out_nop(void *u, uint16_t p, uint8_t v) { (void)u, (void)p, (void)v; }

static void put(seibu_snd *s, int32_t v)
{
    if (s->naudio < s->max_audio) s->audio[s->naudio++] = arc_clamp16(v);
}

static void cycles(void *u, int c)
{
    seibu_snd *s = u;
#ifdef SEIBU_ADPCM
    ym2151_run_timers(&s->ym, c);
    s->oki_acc += (int64_t)c * msm5205d_sample_frequency(&s->adpcm[0]);
    while (s->oki_acc >= s->clock) {
        s->oki_acc -= s->clock;
        msm5205d_vclk(&s->adpcm[0]);
        msm5205d_vclk(&s->adpcm[1]);
    }
    s->ym_acc += (int64_t)c * SEIBU_RATE;
    while (s->ym_acc >= s->clock) {
        s->ym_acc -= s->clock;
        put(s, (ym2151_update(&s->ym) + msm5205d_output(&s->adpcm[0]) + msm5205d_output(&s->adpcm[1])) / 2);
    }
#else
    s->oki_acc += (int64_t)c * okid_sample_frequency(&s->oki);
    while (s->oki_acc >= s->clock) {
        s->oki_acc -= s->clock;
        s->oki_level = okid_update(&s->oki);
    }
    s->ym_acc += (int64_t)c * SEIBU_RATE;
    while (s->ym_acc >= s->clock) {
        s->ym_acc -= s->clock;
        put(s, ym3812d_update(&s->ym) + s->oki_level * 2 / 5);
    }
#endif
}

void seibu_init(seibu_snd *s, uint32_t clock, const uint8_t *data, const uint8_t *opcodes, const uint8_t *banks)
{
    s->clock = clock;
    s->data = data;
    s->opcodes = opcodes;
    s->banks = banks;
    s->z80.read = rd;
    s->z80.write = wr;
    s->z80.opcode_read = op_rd;
    s->z80.in = in_ff;
    s->z80.out = out_nop;
    s->z80.cycles = cycles;
    s->z80.irq_ack = irq_ack;
    s->z80.user = s;
    zd_init(&s->z80);
#ifdef SEIBU_ADPCM
    ym2151_init(&s->ym, clock, SEIBU_RATE);
    s->ym.irq_handler = fm_irq;
    s->ym.user = s;
    for (int i = 0; i < 2; i++) msm5205d_init(&s->adpcm[i], 12000000 / 32, 48, 4);
#else
    ym3812d_init(&s->ym, clock, false, 1.0f);
    s->ym.irq = fm_irq;
    s->ym.user = s;
#endif
}

void seibu_reset(seibu_snd *s)
{
    s->rst10_irq = s->rst18_irq = s->rst10_service = s->rst18_service = false;
    s->main2sub_pending = s->sub2main_pending = false;
    memset(s->latch, 0, sizeof s->latch);
    memset(s->sub2main, 0, sizeof s->sub2main);
    memset(s->ram, 0, sizeof s->ram);
    s->bank = 0;
    s->ym_acc = s->oki_acc = 0;
    zd_reset(&s->z80);
    zd_set_irq(&s->z80, ZD_CLEAR, 0);
#ifdef SEIBU_ADPCM
    ym2151_reset(&s->ym);
    for (int i = 0; i < 2; i++) msm5205d_reset(&s->adpcm[i]);
#else
    s->oki_level = 0;
    ym3812d_reset(&s->ym);
    okid_reset(&s->oki);
#endif
}

uint8_t seibu_main_read(const seibu_snd *s, int offset)
{
    switch (offset) {
    case 2: return s->sub2main[0];
    case 3: return s->sub2main[1];
    case 5: return s->main2sub_pending ? 1 : 0;
    default: return 0xff;
    }
}

void seibu_main_write(seibu_snd *s, int offset, uint8_t v)
{
    switch (offset) {
    case 0: s->latch[0] = v; break;
    case 1: s->latch[1] = v; break;
    case 4: update_irq(s, RST18_ASSERT); break;
    case 2: case 6:
        s->sub2main_pending = false;
        s->main2sub_pending = true;
        break;
    default: break;
    }
}

int seibu_run(seibu_snd *s, int c) { return zd_run(&s->z80, c); }
