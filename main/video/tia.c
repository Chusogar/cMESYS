// tia.c - Atari TIA (NTSC): a C11 migration of dsp-cpp's video/tia (see tia.h).
#include "tia.h"

#include <string.h>

// Standard NTSC TIA palette (Stella), 16 hues x 8 luminances. Bit 0 of
// COLUxx is ignored.
const uint32_t tia_ntsc[128] = {
    0xFF000000, 0xFF4A4A4A, 0xFF6F6F6F, 0xFF8E8E8E, 0xFFAAAAAA, 0xFFC0C0C0, 0xFFD6D6D6, 0xFFECECEC,
    0xFF484800, 0xFF69690F, 0xFF86861D, 0xFFA2A22A, 0xFFBBBB35, 0xFFD2D240, 0xFFE8E84A, 0xFFFCFC54,
    0xFF7C2C00, 0xFF904811, 0xFFA26221, 0xFFB47A30, 0xFFC3903D, 0xFFD2A44A, 0xFFDFB755, 0xFFECC860,
    0xFF901C00, 0xFFA33915, 0xFFB55328, 0xFFC66C3A, 0xFFD5824A, 0xFFE39759, 0xFFF0AA67, 0xFFFCBC74,
    0xFF940000, 0xFFA71A1A, 0xFFB83232, 0xFFC84848, 0xFFD65C5C, 0xFFE46F6F, 0xFFF08080, 0xFFFC9090,
    0xFF840064, 0xFF97197A, 0xFFA8308F, 0xFFB846A2, 0xFFC659B3, 0xFFD46CC3, 0xFFE07CD2, 0xFFEC8CE0,
    0xFF500084, 0xFF68199A, 0xFF7D30AD, 0xFF9246C0, 0xFFA459D0, 0xFFB56CE0, 0xFFC57CEE, 0xFFD48CFC,
    0xFF140090, 0xFF331AA3, 0xFF4E32B5, 0xFF6848C6, 0xFF7F5CD5, 0xFF956FE3, 0xFFA980F0, 0xFFBC90FC,
    0xFF000094, 0xFF181AA7, 0xFF2D32B8, 0xFF4248C8, 0xFF545CD6, 0xFF656FE4, 0xFF7580F0, 0xFF8490FC,
    0xFF001C88, 0xFF183B9D, 0xFF2D57B0, 0xFF4272C2, 0xFF548AD2, 0xFF65A0E1, 0xFF75B5EF, 0xFF84C8FC,
    0xFF003064, 0xFF185080, 0xFF2D6D98, 0xFF4288B0, 0xFF54A0C5, 0xFF65B7D9, 0xFF75CCEB, 0xFF84E0FC,
    0xFF004030, 0xFF18624E, 0xFF2D8169, 0xFF429E82, 0xFF54B899, 0xFF65D1AE, 0xFF75E7C2, 0xFF84FCD4,
    0xFF004400, 0xFF1A661A, 0xFF328432, 0xFF48A048, 0xFF5CBA5C, 0xFF6FD26F, 0xFF80E880, 0xFF90FC90,
    0xFF143C00, 0xFF355F18, 0xFF527E2D, 0xFF6E9C42, 0xFF87B754, 0xFF9ED065, 0xFFB4E775, 0xFFC8FC84,
    0xFF303800, 0xFF505916, 0xFF6D762B, 0xFF88923E, 0xFFA0AB4F, 0xFFB7C25F, 0xFFCCD86E, 0xFFE0EC7C,
    0xFF482C00, 0xFF694D14, 0xFF866A26, 0xFFA28638, 0xFFBB9F47, 0xFFD2B656, 0xFFE8CC63, 0xFFFCE070,
};

enum { P0 = 0, P1 = 1, M0 = 2, M1 = 3, BL = 4 };

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

void tia_set_inpt4(tia *t, bool pressed)
{
    t->inpt4 = pressed;
    if (pressed && t->latch_inputs) t->inpt4_latched = true;
}

void tia_set_inpt5(tia *t, bool pressed)
{
    t->inpt5 = pressed;
    if (pressed && t->latch_inputs) t->inpt5_latched = true;
}

void tia_begin_line(tia *t)
{
    t->hclock = 0;
    t->extended_hblank = false;
}

static int player_scale(uint8_t nusiz)
{
    switch (nusiz & 7) {
    case 5: return 2;
    case 7: return 4;
    default: return 1;
    }
}

// Counter values that start a copy: 156 is the main copy (seen 160 clocks
// after a reset, i.e. on the next line), 12/28/60 the close/medium/wide
// NUSIZ copies (16/32/64 pixels further right).
static bool decodes(const tia *t, int i, int counter)
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
    if (i == M0 || i == M1) {
        const int p = i - M0;
        if (t->resmp[p]) {
            // Locked to the centre of its player.
            const int scale = player_scale(t->nusiz[p]);
            const int lag = scale == 1 ? 4 : (scale == 2 ? 8 : 12);
            o->counter = (t->obj[p].counter + 160 - lag) % 160;
            o->rendering = false;
        }
    }
    if (decodes(t, i, o->counter)) {
        o->rendering = true;
        // Players show their first pixel 6 clocks after the decode, missiles
        // and the ball 5.
        o->render = i <= P1 ? -6 : -5;
    } else if (o->rendering) {
        o->render++;
        int width;
        if (i <= P1) width = 8 * player_scale(t->nusiz[i]) + (player_scale(t->nusiz[i]) > 1 ? 1 : 0);
        else if (i == BL) width = 1 << ((t->ctrlpf >> 4) & 3);
        else width = 1 << ((t->nusiz[i - M0] >> 4) & 3);
        if (o->render >= width) o->rendering = false;
    }
    o->counter = o->counter + 1 >= 160 ? 0 : o->counter + 1;
}

static bool player_on(const tia *t, int which)
{
    const tia_object *o = &t->obj[which];
    if (!o->rendering || o->render < 0) return false;
    const uint8_t grp = t->vdelp[which] ? t->grp_old[which] : t->grp[which];
    const int scale = player_scale(t->nusiz[which]);
    int bit;
    if (scale > 1) {
        if (o->render < 1) return false;
        bit = (o->render - 1) / scale;
    } else {
        bit = o->render;
    }
    if (bit > 7) return false;
    if (t->refp[which] & 0x08) bit = 7 - bit;
    return ((grp >> (7 - bit)) & 1) != 0;
}

static bool missile_on(const tia *t, int which)
{
    const tia_object *o = &t->obj[M0 + which];
    if (t->resmp[which] || !(t->enam[which] & 0x02)) return false;
    return o->rendering && o->render >= 0;
}

static bool ball_on(const tia *t)
{
    const tia_object *o = &t->obj[BL];
    const uint8_t en = t->vdelbl ? t->enabl_old : t->enabl;
    return (en & 0x02) && o->rendering && o->render >= 0;
}

static bool playfield_on(const tia *t, int x)
{
    int cell = x / 4;
    if (cell >= 20) cell = (t->ctrlpf & 0x01) ? (39 - cell) : (cell - 20);
    if (cell < 4) return ((t->pf0 >> (4 + cell)) & 1) != 0;
    if (cell < 12) return ((t->pf1 >> (11 - cell)) & 1) != 0;
    return ((t->pf2 >> (cell - 12)) & 1) != 0;
}

static uint8_t pixel(tia *t, int x)
{
    const bool pf = playfield_on(t, x);
    const bool p0 = player_on(t, 0);
    const bool p1 = player_on(t, 1);
    const bool m0 = missile_on(t, 0);
    const bool m1 = missile_on(t, 1);
    const bool bl = ball_on(t);

    if (m0 && p1) t->cx[0] |= 0x80;
    if (m0 && p0) t->cx[0] |= 0x40;
    if (m1 && p0) t->cx[1] |= 0x80;
    if (m1 && p1) t->cx[1] |= 0x40;
    if (p0 && pf) t->cx[2] |= 0x80;
    if (p0 && bl) t->cx[2] |= 0x40;
    if (p1 && pf) t->cx[3] |= 0x80;
    if (p1 && bl) t->cx[3] |= 0x40;
    if (m0 && pf) t->cx[4] |= 0x80;
    if (m0 && bl) t->cx[4] |= 0x40;
    if (m1 && pf) t->cx[5] |= 0x80;
    if (m1 && bl) t->cx[5] |= 0x40;
    if (bl && pf) t->cx[6] |= 0x80;
    if (p0 && p1) t->cx[7] |= 0x80;
    if (m0 && m1) t->cx[7] |= 0x40;

    if (tia_blanked(t)) return 0;

    // Score mode colours the playfield with the player colours, except when
    // the playfield has priority (the real chip ignores SCORE then).
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
    return TIA_COLOUR(colu);
}

static void apply(tia *t, uint8_t reg, uint8_t value);

static void clock(tia *t, int c)
{
    // Delayed register writes (see tia_write), in the order written.
    if (t->nqueue) {
        for (int i = 0; i < t->nqueue;) {
            if (--t->queue[i].clocks <= 0) {
                const tia_delayed d = t->queue[i];
                memmove(&t->queue[i], &t->queue[i + 1], (size_t)(t->nqueue - i - 1) * sizeof d);
                t->nqueue--;
                apply(t, d.reg, d.value);
            } else {
                i++;
            }
        }
    }

    const bool hblank = c < TIA_HBLANK_CLOCKS + (t->extended_hblank ? 8 : 0);

    // HMOVE ripple counter: one step every 4 clocks; an object still moving
    // takes an extra counter clock, but only while HBLANK stops its normal
    // clock.
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

    if (c < TIA_HBLANK_CLOCKS) return;
    const int x = c - TIA_HBLANK_CLOCKS;
    if (hblank) {
        t->line[x] = 0;                 // HMOVE comb
        return;
    }
    for (int i = 0; i < 5; i++) tick_object(t, i);
    t->line[x] = pixel(t, x);
}

void tia_run(tia *t, int clocks)
{
    int end = t->hclock + clocks;
    if (end > TIA_CLOCKS_PER_LINE) end = TIA_CLOCKS_PER_LINE;
    for (int c = t->hclock; c < end; c++) {
        t->hclock = c;
        clock(t, c);
    }
    t->hclock = end;
}

static void reset_object(tia *t, int index)
{
    // The counter restarts so that the object shows up 5 (players) or 4
    // (missiles, ball) pixels later from the next line on, or at the left
    // edge (3 / 2) when strobed during HBLANK.
    tia_object *o = &t->obj[index];
    if (t->hclock < TIA_HBLANK_CLOCKS) o->counter = 159;
    else if (t->hclock < TIA_HBLANK_CLOCKS + (t->extended_hblank ? 8 : 0)) o->counter = 158;
    else o->counter = 157;
    // A reset while a player copy is still in its start-up delay restarts
    // that delay (Andrew Towers' TIA notes; Stella does the same).
    if (index <= P1 && o->rendering && o->render <= -3) o->render = -6;
}

// Stella's TIA write delays (colour clocks until the chip sees the value).
static int write_delay(uint8_t reg)
{
    switch (reg) {
    case 0x01: return 1;                        // VBLANK
    case 0x0b: case 0x0c: return 1;             // REFP
    case 0x0d: case 0x0e: case 0x0f: return 2;  // PF
    case 0x1b: case 0x1c: return 1;             // GRP
    case 0x1d: case 0x1e: case 0x1f: return 1;  // ENAM/ENABL
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: return 2;  // HMxx
    case 0x2b: return 2;                        // HMCLR
    default: return 0;
    }
}

void tia_write(tia *t, uint8_t reg, uint8_t value)
{
    reg &= 0x3f;
    const int delay = write_delay(reg);
    if (delay == 0) {
        apply(t, reg, value);
    } else if (t->nqueue < TIA_QUEUE) {
        t->queue[t->nqueue++] = (tia_delayed){ reg, value, delay + 1 };
    } else {
        apply(t, reg, value);
    }
}

static void apply(tia *t, uint8_t reg, uint8_t value)
{
    switch (reg & 0x3f) {
    case 0x00: t->vsync = (value & 0x02) != 0; break;
    case 0x01:
        t->vblank = (value & 0x02) != 0;
        t->latch_inputs = (value & 0x40) != 0;
        t->dump_ports = (value & 0x80) != 0;
        if (!t->latch_inputs) {
            t->inpt4_latched = t->inpt5_latched = false;
        } else {
            if (t->inpt4) t->inpt4_latched = true;
            if (t->inpt5) t->inpt5_latched = true;
        }
        break;
    case 0x02: t->wsync = true; break;
    case 0x03: break;  // RSYNC: handled by the driver
    case 0x04: t->nusiz[0] = value; break;
    case 0x05: t->nusiz[1] = value; break;
    case 0x06: t->colup[0] = value; break;
    case 0x07: t->colup[1] = value; break;
    case 0x08: t->colupf = value; break;
    case 0x09: t->colubk = value; break;
    case 0x0a: t->ctrlpf = value; break;
    case 0x0b: t->refp[0] = value; break;
    case 0x0c: t->refp[1] = value; break;
    case 0x0d: t->pf0 = value; break;
    case 0x0e: t->pf1 = value; break;
    case 0x0f: t->pf2 = value; break;
    case 0x10: reset_object(t, P0); break;
    case 0x11: reset_object(t, P1); break;
    case 0x12: reset_object(t, M0); break;
    case 0x13: reset_object(t, M1); break;
    case 0x14: reset_object(t, BL); break;
    case 0x15: t->ch[0].audc = value & 0x0f; break;
    case 0x16: t->ch[1].audc = value & 0x0f; break;
    case 0x17: t->ch[0].audf = value & 0x1f; break;
    case 0x18: t->ch[1].audf = value & 0x1f; break;
    case 0x19: t->ch[0].audv = value & 0x0f; break;
    case 0x1a: t->ch[1].audv = value & 0x0f; break;
    case 0x1b:
        t->grp[0] = value;
        t->grp_old[1] = t->grp[1];
        break;
    case 0x1c:
        t->grp[1] = value;
        t->grp_old[0] = t->grp[0];
        t->enabl_old = t->enabl;
        break;
    case 0x1d: t->enam[0] = value; break;
    case 0x1e: t->enam[1] = value; break;
    case 0x1f: t->enabl = value; break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: {
        const int i = (reg & 0x3f) - 0x20;
        t->hm[i] = value;
        t->obj[i].hmm_clocks = ((value >> 4) ^ 0x08) & 0x0f;
        break;
    }
    case 0x25: t->vdelp[0] = (value & 0x01) != 0; break;
    case 0x26: t->vdelp[1] = (value & 0x01) != 0; break;
    case 0x27: t->vdelbl = (value & 0x01) != 0; break;
    case 0x28: t->resmp[0] = (value & 0x02) != 0; break;
    case 0x29: t->resmp[1] = (value & 0x02) != 0; break;
    case 0x2a:
        // HMOVE: start the ripple counter; strobed during HBLANK it also
        // stretches this line's HBLANK by 8 clocks (the black comb).
        t->movement = true;
        t->movement_clock = 0;
        for (int i = 0; i < 5; i++) t->obj[i].moving = true;
        if (t->hclock < TIA_HBLANK_CLOCKS) t->extended_hblank = true;
        break;
    case 0x2b:
        memset(t->hm, 0, sizeof t->hm);
        for (int i = 0; i < 5; i++) t->obj[i].hmm_clocks = 8;
        break;
    case 0x2c: memset(t->cx, 0, sizeof t->cx); break;
    default: break;
    }
}

uint8_t tia_read(const tia *t, uint8_t reg)
{
    reg &= 0x0f;
    if (reg <= 0x07) return t->cx[reg];
    if (reg == 0x0c) return (t->inpt4 || t->inpt4_latched) ? 0x00 : 0x80;
    if (reg == 0x0d) return (t->inpt5 || t->inpt5_latched) ? 0x00 : 0x80;
    // INPT0-3: paddle capacitors; grounded while dumped, charged otherwise.
    if (reg >= 0x08 && reg <= 0x0b) return t->dump_ports ? 0x00 : 0x80;
    return 0x00;
}

// TIA audio (after Ron Fries' TIASound): AUDF divides the 31.4 kHz clock;
// AUDC selects the clock modifier (always / div31 / 5-bit poly) and the
// output (pure toggle / 4-bit / 5-bit / 9-bit poly), with C-F dividing by 3.
static void clock_channel(tia_channel *ch)
{
    const int max = (ch->audf + 1) * ((ch->audc & 0x0c) == 0x0c ? 3 : 1);
    if (++ch->divider < max) return;
    ch->divider = 0;

    const uint8_t fb5 = (uint8_t)(((ch->poly5 >> 0) ^ (ch->poly5 >> 2)) & 1);
    ch->poly5 = (uint8_t)((ch->poly5 >> 1) | (fb5 << 4));
    const bool bit5 = (ch->poly5 & 1) != 0;
    const bool div31 = ch->poly5 == 0x1f;

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
            const uint16_t fb9 = (uint16_t)(((ch->poly9 >> 0) ^ (ch->poly9 >> 4)) & 1);
            ch->poly9 = (uint16_t)((ch->poly9 >> 1) | (fb9 << 8));
            ch->bit = (uint8_t)(ch->poly9 & 1);
        } else {
            ch->bit = bit5 ? 1 : 0;
        }
    } else {
        const uint8_t fb4 = (uint8_t)(((ch->poly4 >> 0) ^ (ch->poly4 >> 1)) & 1);
        ch->poly4 = (uint8_t)((ch->poly4 >> 1) | (fb4 << 3));
        ch->bit = (uint8_t)(ch->poly4 & 1);
    }
}

void tia_clock_audio(tia *t)
{
    clock_channel(&t->ch[0]);
    clock_channel(&t->ch[1]);
    const int mix = (int)t->ch[0].bit * t->ch[0].audv + (int)t->ch[1].bit * t->ch[1].audv;
    t->sample = (int16_t)(mix * 800);
}

int tia_emit_audio(tia *t, int cpu_cycles, uint32_t cpu_clock, int rate, int16_t *dst, int room)
{
    if (cpu_clock == 0 || cpu_cycles <= 0) return 0;
    int n = 0;
    t->audio_acc += (uint32_t)cpu_cycles * (uint32_t)rate;
    while (t->audio_acc >= cpu_clock) {
        t->audio_acc -= cpu_clock;
        if (n < room) dst[n++] = t->sample;
    }
    return n;
}
