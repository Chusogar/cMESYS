// tia.c - Atari TIA: a C11 port of dsp-cpp's video/tia; see tia.h.
#include <string.h>
#include "tia.h"

enum { P0 = 0, P1 = 1, M0 = 2, M1 = 3, BL = 4 };

// Stella's NTSC palette (RGB565), 16 hues x 8 luminances
static const uint16_t kNtsc565[128] = {
    0x0000, 0x4a49, 0x6b6d, 0x8c71, 0xad55, 0xc618, 0xd6ba, 0xef7d,
    0x4a40, 0x6b41, 0x8423, 0xa505, 0xbdc6, 0xd688, 0xef49, 0xffea,
    0x7960, 0x9242, 0xa304, 0xb3c6, 0xc487, 0xd529, 0xddaa, 0xee4c,
    0x90e0, 0xa1c2, 0xb285, 0xc367, 0xd409, 0xe4ab, 0xf54c, 0xfdee,
    0x9000, 0xa0c3, 0xb986, 0xca49, 0xd2eb, 0xe36d, 0xf410, 0xfc92,
    0x800c, 0x90cf, 0xa991, 0xba34, 0xc2d6, 0xd378, 0xe3fa, 0xec7c,
    0x5010, 0x68d3, 0x7995, 0x9238, 0xa2da, 0xb37c, 0xc3fd, 0xd47f,
    0x1012, 0x30d4, 0x4996, 0x6a58, 0x7afa, 0x937c, 0xac1e, 0xbc9f,
    0x0012, 0x18d4, 0x2997, 0x4259, 0x52fa, 0x637c, 0x741e, 0x849f,
    0x00f1, 0x19d3, 0x2ab6, 0x4398, 0x545a, 0x651c, 0x75bd, 0x865f,
    0x018c, 0x1a90, 0x2b73, 0x4456, 0x5518, 0x65bb, 0x767d, 0x871f,
    0x0206, 0x1b09, 0x2c0d, 0x44f0, 0x55d3, 0x6695, 0x7738, 0x87fa,
    0x0220, 0x1b23, 0x3426, 0x4d09, 0x5dcb, 0x6e8d, 0x8750, 0x97f2,
    0x11e0, 0x32e3, 0x53e5, 0x6ce8, 0x85aa, 0x9e8c, 0xb72e, 0xcff0,
    0x31c0, 0x52c2, 0x6ba5, 0x8c87, 0xa549, 0xb60b, 0xcecd, 0xe76f,
    0x4960, 0x6a62, 0x8344, 0xa427, 0xbce8, 0xd5aa, 0xee6c, 0xff0e,
};

static inline uint16_t ntsc(uint8_t colu) { return kNtsc565[(colu >> 1) & 0x7f]; }

void tia_reset(tia *t)
{
    memset(t, 0, sizeof *t);
    for (int i = 0; i < 5; i++) t->obj[i].hmm_clocks = 8;
    for (int i = 0; i < 2; i++) {
        t->ch[i].poly4 = 0x0f;
        t->ch[i].poly5 = 0x1f;
        t->ch[i].poly9 = 0x1ff;
    }
}

void tia_set_inpt4(tia *t, bool p)
{
    t->inpt4 = p;
    if (p && t->latch_inputs) t->inpt4_latched = true;
}

void tia_set_inpt5(tia *t, bool p)
{
    t->inpt5 = p;
    if (p && t->latch_inputs) t->inpt5_latched = true;
}

void tia_begin_line(tia *t)
{
    t->hclock = 0;
    t->extended_hblank = false;
}

static inline int player_scale(uint8_t nusiz)
{
    switch (nusiz & 7) {
    case 5: return 2;
    case 7: return 4;
    default: return 1;
    }
}

static inline bool decodes(const tia *t, int i, int counter)
{
    if (counter == 156) return true;
    if (i == BL) return false;
    switch (t->nusiz[i & 1] & 7) {
    case 1: return counter == 12;
    case 2: return counter == 28;
    case 3: return counter == 12 || counter == 28;
    case 4: return counter == 60;
    case 6: return counter == 28 || counter == 60;
    default: return false;
    }
}

static void tick_object(tia *t, int i)
{
    tia_object *o = &t->obj[i];
    if ((i == M0 || i == M1) && t->resmp[i - M0]) {
        const int p = i - M0;
        const int scale = player_scale(t->nusiz[p]);
        const int lag = scale == 1 ? 4 : scale == 2 ? 8 : 12;
        o->counter = (t->obj[p].counter + 160 - lag) % 160;
        o->rendering = false;
    }
    if (decodes(t, i, o->counter)) {
        o->rendering = true;
        o->render = i <= P1 ? -6 : -5;
    } else if (o->rendering) {
        o->render++;
        int width;
        if (i <= P1) {
            const int s = player_scale(t->nusiz[i]);
            width = 8 * s + (s > 1 ? 1 : 0);
        } else if (i == BL) {
            width = 1 << ((t->ctrlpf >> 4) & 3);
        } else {
            width = 1 << ((t->nusiz[i - M0] >> 4) & 3);
        }
        if (o->render >= width) o->rendering = false;
    }
    o->counter = o->counter + 1 >= 160 ? 0 : o->counter + 1;
}

static inline bool player_on(const tia *t, int w)
{
    const tia_object *o = &t->obj[w];
    if (!o->rendering || o->render < 0) return false;
    const uint8_t grp = t->vdelp[w] ? t->grp_old[w] : t->grp[w];
    const int scale = player_scale(t->nusiz[w]);
    int bit;
    if (scale > 1) {
        if (o->render < 1) return false;
        bit = (o->render - 1) / scale;
    } else {
        bit = o->render;
    }
    if (bit > 7) return false;
    if (t->refp[w] & 0x08) bit = 7 - bit;
    return (grp >> (7 - bit)) & 1;
}

static inline bool missile_on(const tia *t, int w)
{
    const tia_object *o = &t->obj[M0 + w];
    if (t->resmp[w] || !(t->enam[w] & 0x02)) return false;
    return o->rendering && o->render >= 0;
}

static inline bool ball_on(const tia *t)
{
    const tia_object *o = &t->obj[BL];
    const uint8_t en = t->vdelbl ? t->enabl_old : t->enabl;
    return (en & 0x02) && o->rendering && o->render >= 0;
}

static inline bool playfield_on(const tia *t, int x)
{
    int cell = x / 4;
    if (cell >= 20) cell = (t->ctrlpf & 0x01) ? 39 - cell : cell - 20;
    if (cell < 4) return (t->pf0 >> (4 + cell)) & 1;
    if (cell < 12) return (t->pf1 >> (11 - cell)) & 1;
    return (t->pf2 >> (cell - 12)) & 1;
}

static uint16_t pixel(tia *t, int x)
{
    const bool pf = playfield_on(t, x), p0 = player_on(t, 0), p1 = player_on(t, 1);
    const bool m0 = missile_on(t, 0), m1 = missile_on(t, 1), bl = ball_on(t);
    uint8_t *cx = t->cx;
    if (m0 && p1) cx[0] |= 0x80;
    if (m0 && p0) cx[0] |= 0x40;
    if (m1 && p0) cx[1] |= 0x80;
    if (m1 && p1) cx[1] |= 0x40;
    if (p0 && pf) cx[2] |= 0x80;
    if (p0 && bl) cx[2] |= 0x40;
    if (p1 && pf) cx[3] |= 0x80;
    if (p1 && bl) cx[3] |= 0x40;
    if (m0 && pf) cx[4] |= 0x80;
    if (m0 && bl) cx[4] |= 0x40;
    if (m1 && pf) cx[5] |= 0x80;
    if (m1 && bl) cx[5] |= 0x40;
    if (bl && pf) cx[6] |= 0x80;
    if (p0 && p1) cx[7] |= 0x80;
    if (m0 && m1) cx[7] |= 0x40;
    if (tia_blanked(t)) return 0;
    const bool score = (t->ctrlpf & 0x06) == 0x02;
    const uint8_t pf_colu = score ? t->colup[x < 80 ? 0 : 1] : t->colupf;
    uint8_t colu;
    if (t->ctrlpf & 0x04) {
        if (pf || bl) colu = pf ? pf_colu : t->colupf;
        else if (p0 || m0) colu = t->colup[0];
        else if (p1 || m1) colu = t->colup[1];
        else colu = t->colubk;
    } else {
        if (p0 || m0) colu = t->colup[0];
        else if (p1 || m1) colu = t->colup[1];
        else if (pf) colu = pf_colu;
        else if (bl) colu = t->colupf;
        else colu = t->colubk;
    }
    return ntsc(colu);
}

static void reset_object(tia *t, int i)
{
    tia_object *o = &t->obj[i];
    if (t->hclock < TIA_HBLANK) o->counter = 159;
    else if (t->hclock < TIA_HBLANK + (t->extended_hblank ? 8 : 0)) o->counter = 158;
    else o->counter = 157;
    if (i <= P1 && o->rendering && o->render <= -3) o->render = -6;
}

static void apply(tia *t, uint8_t reg, uint8_t v)
{
    switch (reg & 0x3f) {
    case 0x00: t->vsync = (v & 0x02) != 0; break;
    case 0x01:
        t->vblank = (v & 0x02) != 0;
        t->latch_inputs = (v & 0x40) != 0;
        t->dump_ports = (v & 0x80) != 0;
        if (!t->latch_inputs) {
            t->inpt4_latched = t->inpt5_latched = false;
        } else {
            if (t->inpt4) t->inpt4_latched = true;
            if (t->inpt5) t->inpt5_latched = true;
        }
        break;
    case 0x02: t->wsync = true; break;
    case 0x04: t->nusiz[0] = v; break;
    case 0x05: t->nusiz[1] = v; break;
    case 0x06: t->colup[0] = v; break;
    case 0x07: t->colup[1] = v; break;
    case 0x08: t->colupf = v; break;
    case 0x09: t->colubk = v; break;
    case 0x0a: t->ctrlpf = v; break;
    case 0x0b: t->refp[0] = v; break;
    case 0x0c: t->refp[1] = v; break;
    case 0x0d: t->pf0 = v; break;
    case 0x0e: t->pf1 = v; break;
    case 0x0f: t->pf2 = v; break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: reset_object(t, (reg & 0x3f) - 0x10); break;
    case 0x15: t->ch[0].audc = v & 0x0f; break;
    case 0x16: t->ch[1].audc = v & 0x0f; break;
    case 0x17: t->ch[0].audf = v & 0x1f; break;
    case 0x18: t->ch[1].audf = v & 0x1f; break;
    case 0x19: t->ch[0].audv = v & 0x0f; break;
    case 0x1a: t->ch[1].audv = v & 0x0f; break;
    case 0x1b:
        t->grp[0] = v;
        t->grp_old[1] = t->grp[1];
        break;
    case 0x1c:
        t->grp[1] = v;
        t->grp_old[0] = t->grp[0];
        t->enabl_old = t->enabl;
        break;
    case 0x1d: t->enam[0] = v; break;
    case 0x1e: t->enam[1] = v; break;
    case 0x1f: t->enabl = v; break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: {
        const int i = (reg & 0x3f) - 0x20;
        t->hm[i] = v;
        t->obj[i].hmm_clocks = ((v >> 4) ^ 0x08) & 0x0f;
        break;
    }
    case 0x25: t->vdelp[0] = v & 1; break;
    case 0x26: t->vdelp[1] = v & 1; break;
    case 0x27: t->vdelbl = v & 1; break;
    case 0x28: t->resmp[0] = (v & 0x02) != 0; break;
    case 0x29: t->resmp[1] = (v & 0x02) != 0; break;
    case 0x2a:
        t->movement = true;
        t->movement_clock = 0;
        for (int i = 0; i < 5; i++) t->obj[i].moving = true;
        if (t->hclock < TIA_HBLANK) t->extended_hblank = true;
        break;
    case 0x2b:
        memset(t->hm, 0, sizeof t->hm);
        for (int i = 0; i < 5; i++) t->obj[i].hmm_clocks = 8;
        break;
    case 0x2c: memset(t->cx, 0, sizeof t->cx); break;
    default: break;
    }
}

static void clock1(tia *t, int c)
{
    if (t->nqueue) {
        for (int i = 0; i < t->nqueue;) {
            if (--t->queue[i].clocks <= 0) {
                const uint8_t reg = t->queue[i].reg, v = t->queue[i].value;
                memmove(&t->queue[i], &t->queue[i + 1], (size_t)(t->nqueue - i - 1) * sizeof t->queue[0]);
                t->nqueue--;
                apply(t, reg, v);
            } else {
                i++;
            }
        }
    }
    const bool hblank = c < TIA_HBLANK + (t->extended_hblank ? 8 : 0);
    if (t->movement && (c & 3) == 0) {
        const int step = t->movement_clock > 15 ? 0 : t->movement_clock;
        bool any = false;
        for (int i = 0; i < 5; i++) {
            tia_object *o = &t->obj[i];
            if (step == o->hmm_clocks) o->moving = false;
            if (o->moving) {
                if (hblank) tick_object(t, i);
                any = true;
            }
        }
        t->movement = any;
        t->movement_clock++;
    }
    if (c < TIA_HBLANK) return;
    const int x = c - TIA_HBLANK;
    if (hblank) {
        t->line[x] = 0;
        return;
    }
    for (int i = 0; i < 5; i++) tick_object(t, i);
    t->line[x] = pixel(t, x);
}

void tia_run(tia *t, int clocks)
{
    int end = t->hclock + clocks;
    if (end > TIA_CLOCKS) end = TIA_CLOCKS;
    for (int c = t->hclock; c < end; c++) {
        t->hclock = c;
        clock1(t, c);
    }
    t->hclock = end;
}

static int write_delay(uint8_t reg)
{
    switch (reg) {
    case 0x01: case 0x0b: case 0x0c: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: return 1;
    case 0x0d: case 0x0e: case 0x0f: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x2b: return 2;
    default: return 0;
    }
}

void tia_write(tia *t, uint8_t reg, uint8_t v)
{
    reg &= 0x3f;
    const int d = write_delay(reg);
    if (d == 0 || t->nqueue >= 16) {
        apply(t, reg, v);
        return;
    }
    t->queue[t->nqueue].reg = reg;
    t->queue[t->nqueue].value = v;
    t->queue[t->nqueue].clocks = d + 1;
    t->nqueue++;
}

uint8_t tia_read(const tia *t, uint8_t reg)
{
    reg &= 0x0f;
    if (reg <= 0x07) return t->cx[reg];
    if (reg == 0x0c) return (t->inpt4 || t->inpt4_latched) ? 0x00 : 0x80;
    if (reg == 0x0d) return (t->inpt5 || t->inpt5_latched) ? 0x00 : 0x80;
    if (reg >= 0x08 && reg <= 0x0b) return t->dump_ports ? 0x00 : 0x80;
    return 0;
}

static void clock_channel(tia_channel *ch)
{
    const int max = (ch->audf + 1) * ((ch->audc & 0x0c) == 0x0c ? 3 : 1);
    if (++ch->divider < max) return;
    ch->divider = 0;
    const uint8_t fb5 = (uint8_t)((ch->poly5 ^ (ch->poly5 >> 2)) & 1);
    ch->poly5 = (uint8_t)((ch->poly5 >> 1) | (fb5 << 4));
    const bool bit5 = ch->poly5 & 1, div31 = ch->poly5 == 0x1f;
    const uint8_t c = ch->audc;
    if (c == 0x00 || c == 0x0b) {
        ch->bit = 1;
        return;
    }
    const bool tick = !(c & 0x02) || (!(c & 0x01) && div31) || ((c & 0x01) && bit5);
    if (!tick) return;
    if (c & 0x04) {
        ch->bit ^= 1;
    } else if (c & 0x08) {
        if (c == 0x08) {
            const uint16_t fb9 = (uint16_t)((ch->poly9 ^ (ch->poly9 >> 4)) & 1);
            ch->poly9 = (uint16_t)((ch->poly9 >> 1) | (fb9 << 8));
            ch->bit = ch->poly9 & 1;
        } else {
            ch->bit = bit5 ? 1 : 0;
        }
    } else {
        const uint8_t fb4 = (uint8_t)((ch->poly4 ^ (ch->poly4 >> 1)) & 1);
        ch->poly4 = (uint8_t)((ch->poly4 >> 1) | (fb4 << 3));
        ch->bit = ch->poly4 & 1;
    }
}

void tia_clock_audio(tia *t)
{
    clock_channel(&t->ch[0]);
    clock_channel(&t->ch[1]);
    const int mix = t->ch[0].bit * t->ch[0].audv + t->ch[1].bit * t->ch[1].audv;
    t->sample = (int16_t)(mix * 800);
}

void tia_emit_audio(tia *t, int cpu_cycles, uint32_t cpu_clock, int16_t *out, int *n, int max)
{
    if (!cpu_clock || cpu_cycles <= 0) return;
    t->audio_phase += (double)cpu_cycles * TIA_RATE / (double)cpu_clock;
    while (t->audio_phase >= 1.0) {
        t->audio_phase -= 1.0;
        if (*n < max) out[(*n)++] = t->sample;
    }
}
