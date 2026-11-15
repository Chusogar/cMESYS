// amiga_chipset.c - OCS Agnus / Denise / Paula: copper, blitter, bitplanes,
// sprites, INTENA / DMACON, disk DMA, the four audio channels. A C port of
// dsp-cpp's src/machine/amiga_chipset.cpp (same behaviour; the bitplanes
// are turned into pixels a byte at a time, and the blitter's D writes lag
// one word as in the real one's pipeline instead of after the whole blit).
#include <string.h>
#include "amiga_int.h"

#ifndef AMIGA_HOT
#define AMIGA_HOT
#endif

enum { DMAEN = 0x0200, BPLEN = 0x0100, COPEN = 0x0080, BLTEN = 0x0040, SPREN = 0x0020, DSKEN = 0x0010 };

static inline bool dma(const amiga_chips *c, uint16_t bit) { return (c->dmacon & DMAEN) && (c->dmacon & bit); }

static inline uint16_t chip_rd(const amiga_chips *c, uint32_t a)
{
    a &= AMIGA_CHIP_SIZE - 1;
    return (uint16_t)((c->chip[a] << 8) | c->chip[(a + 1) & (AMIGA_CHIP_SIZE - 1)]);
}

static inline void chip_wr(amiga_chips *c, uint32_t a, uint16_t v)
{
    a &= AMIGA_CHIP_SIZE - 1;
    c->chip[a] = (uint8_t)(v >> 8);
    c->chip[(a + 1) & (AMIGA_CHIP_SIZE - 1)] = (uint8_t)v;
}

static int paula_ipl(uint16_t intena, uint16_t intreq)
{
    if (!(intena & 0x4000)) return 0;
    const uint16_t bits = (uint16_t)(intena & intreq & 0x3FFF);
    if (!bits) return 0;
    if (bits & 0x2000) return 6;
    if (bits & 0x1800) return 5;
    if (bits & 0x0780) return 4;
    if (bits & 0x0070) return 3;
    if (bits & 0x0008) return 2;
    if (bits & 0x0007) return 1;
    return 0;
}

static inline uint16_t minterm(uint16_t a, uint16_t b, uint16_t c, uint8_t mt)
{
    // the eight minterms of BLTCON0 as a sum of products, a whole word at once
    uint16_t d = 0;
    if (mt & 0x01) d |= ~a & ~b & ~c;
    if (mt & 0x02) d |= ~a & ~b & c;
    if (mt & 0x04) d |= ~a & b & ~c;
    if (mt & 0x08) d |= ~a & b & c;
    if (mt & 0x10) d |= a & ~b & ~c;
    if (mt & 0x20) d |= a & ~b & c;
    if (mt & 0x40) d |= a & b & ~c;
    if (mt & 0x80) d |= a & b & c;
    return d;
}

static void poke_ptr(uint32_t *p, bool high, uint16_t value)
{
    if (high) *p = ((uint32_t)value << 16) | (*p & 0xFFFF);
    else *p = (*p & 0xFFFF0000u) | (value & 0xFFFEu);     // DMA pointers address words
}

static void setclr(uint16_t *reg, uint16_t value, uint16_t mask)
{
    const uint16_t bits = (uint16_t)(value & mask);
    if (value & 0x8000) *reg = (uint16_t)(*reg | bits);
    else *reg = (uint16_t)(*reg & ~bits);
}

void ach_reset(amiga_chips *c)
{
    uint8_t *chip = c->chip;
    uint16_t *fb = c->fb;
    memset(c, 0, sizeof *c);
    c->chip = chip;
    c->fb = fb;
    c->dsksync = 0x4489;
    c->diwstrt = 0x2C81;
    c->diwstop = 0xF4C1;
    c->ddfstrt = 0x0038;
    c->ddfstop = 0x00D0;
    c->bltafwm = c->bltalwm = 0xFFFF;
    c->bzero = true;
    c->cop_stopped = true;
}

int ach_ipl(const amiga_chips *c)
{
    uint16_t req = c->intreq;
    if (c->ciaa_irq) req |= 0x0008;
    if (c->ciab_irq) req |= 0x2000;
    return paula_ipl(c->intena, req);
}

static void copper_step_until_wait(amiga_chips *c, int vpos);
static void blit(amiga_chips *c);
static void disk_dma(amiga_chips *c);
static void audio_dma_changed(amiga_chips *c, uint16_t old_dmacon);

uint16_t ach_read(amiga_chips *c, uint16_t reg)
{
    switch (reg & 0x1FE) {
    case 0x002: {
        uint16_t v = (uint16_t)(c->dmacon & 0x07FF);
        if (!c->bzero) v |= 0x2000;
        return v;
    }
    case 0x004: return (uint16_t)((c->lof ? 0x8000 : 0) | ((c->vpos >> 8) & 1));
    // the low byte is HPOS, from the CPU's time in the line (dsp-cpp gives
    // a fixed $80, and Kickstart 3.1, waiting for HPOS < $30 with the
    // interrupts off, never leaves)
    case 0x006: return (uint16_t)((uint16_t)(c->vpos & 0xFF) << 8) | (uint16_t)amiga_hpos();
    case 0x00A: return c->joy0dat;
    case 0x00C: return c->joy1dat;
    case 0x010: return c->adkcon;
    case 0x016: return c->rmb ? (uint16_t)(0xFF00 & ~0x0400) : (uint16_t)0xFF00;   // POTGOR (DATLY: right button)
    case 0x018: return 0x3000;
    case 0x01A: return (c->dsklen & 0x8000) ? 0x8000 : 0;
    case 0x01C: return c->intena;
    case 0x01E: {
        uint16_t v = c->intreq;
        if (c->ciaa_irq) v |= 0x0008;
        if (c->ciab_irq) v |= 0x2000;
        return v;
    }
    default:
        if ((reg & 0x1FE) >= 0x180 && (reg & 0x1FE) <= 0x1BE) return c->color[((reg & 0x1FE) - 0x180) >> 1];
        return 0;
    }
}

void AMIGA_HOT ach_write(amiga_chips *c, uint16_t reg, uint16_t value)
{
    const uint16_t r = (uint16_t)(reg & 0x1FE);
    switch (r) {
    case 0x036: amiga_joytest(value); break;                // JOYTEST
    case 0x020: poke_ptr(&c->dskpt, true, value); break;
    case 0x022: poke_ptr(&c->dskpt, false, value); break;
    case 0x024: {
        const uint16_t prev = c->dsklen;
        c->dsklen = value;
        if ((value & 0x8000) && (prev & 0x8000) && dma(c, DSKEN)) disk_dma(c);
        break;
    }
    case 0x07E: c->dsksync = value; break;
    case 0x080: poke_ptr(&c->cop1lc, true, value); break;
    case 0x082: poke_ptr(&c->cop1lc, false, value); break;
    case 0x084: poke_ptr(&c->cop2lc, true, value); break;
    case 0x086: poke_ptr(&c->cop2lc, false, value); break;
    case 0x088:
        c->coppc = c->cop1lc;
        c->cop_stopped = false;
        if (!c->copper_active) copper_step_until_wait(c, c->vpos);
        break;
    case 0x08A:
        c->coppc = c->cop2lc;
        c->cop_stopped = false;
        if (!c->copper_active) copper_step_until_wait(c, c->vpos);
        break;
    case 0x08E: c->diwstrt = value; break;
    case 0x090: c->diwstop = value; break;
    case 0x092: c->ddfstrt = value; break;
    case 0x094: c->ddfstop = value; break;
    case 0x096: {
        const uint16_t old = c->dmacon;
        setclr(&c->dmacon, value, 0x07FF);
        audio_dma_changed(c, old);
        break;
    }
    case 0x09A: setclr(&c->intena, value, 0x7FFF); break;
    case 0x09C: setclr(&c->intreq, value, 0x7FFF); break;
    case 0x09E: setclr(&c->adkcon, value, 0x7FFF); break;
    case 0x040: c->bltcon0 = value; break;
    case 0x042: c->bltcon1 = value; break;
    case 0x044: c->bltafwm = value; break;
    case 0x046: c->bltalwm = value; break;
    case 0x048: poke_ptr(&c->bltcpt, true, value); break;
    case 0x04A: poke_ptr(&c->bltcpt, false, value); break;
    case 0x04C: poke_ptr(&c->bltbpt, true, value); break;
    case 0x04E: poke_ptr(&c->bltbpt, false, value); break;
    case 0x050: poke_ptr(&c->bltapt, true, value); break;
    case 0x052: poke_ptr(&c->bltapt, false, value); break;
    case 0x054: poke_ptr(&c->bltdpt, true, value); break;
    case 0x056: poke_ptr(&c->bltdpt, false, value); break;
    case 0x058: c->bltsize = value; blit(c); break;
    case 0x060: c->bltcmod = (int16_t)(value & 0xFFFE); break;   // (bit 0 is not implemented)
    case 0x062: c->bltbmod = (int16_t)(value & 0xFFFE); break;
    case 0x064: c->bltamod = (int16_t)(value & 0xFFFE); break;
    case 0x066: c->bltdmod = (int16_t)(value & 0xFFFE); break;
    case 0x070: c->bltcdat = value; break;
    case 0x072: c->bltbdat = value; break;
    case 0x074: c->bltadat = value; break;
    case 0x100: c->bplcon0 = value; break;
    case 0x102: c->bplcon1 = value; break;
    case 0x104: c->bplcon2 = value; break;
    case 0x108: c->bpl1mod = (int16_t)(value & 0xFFFE); break;
    case 0x10A: c->bpl2mod = (int16_t)(value & 0xFFFE); break;
    default:
        if (r >= 0x0E0 && r <= 0x0F6) {
            poke_ptr(&c->bplpt[(r - 0x0E0) >> 2], (r & 2) == 0, value);
        } else if (r >= 0x120 && r <= 0x13E) {
            poke_ptr(&c->sprpt[(r - 0x120) >> 2], (r & 2) == 0, value);
        } else if (r >= 0x140 && r <= 0x17E) {
            const int s = (r - 0x140) >> 3;
            switch (r & 6) {
            case 0: c->sprpos[s] = value; break;
            case 2: c->sprctl[s] = value; break;
            case 4: c->sprdata[s] = value; break;
            default: c->sprdatb[s] = value; break;
            }
        } else if (r >= 0x180 && r <= 0x1BE) {
            c->color[(r - 0x180) >> 1] = (uint16_t)(value & 0x0FFF);
        } else if (r >= 0x0A0 && r < 0x0E0) {
            amiga_aud *a = &c->aud[(r - 0x0A0) >> 4];
            switch (r & 0x0E) {
            case 0x0: a->lc = ((uint32_t)value << 16) | (a->lc & 0xFFFF); break;
            case 0x2: a->lc = (a->lc & 0xFFFF0000u) | (value & 0xFFFEu); break;
            case 0x4: a->len = value; break;
            case 0x6: a->per = value; break;
            case 0x8: a->vol = (uint16_t)((value & 0x7F) > 64 ? 64 : (value & 0x7F)); break;
            case 0xA:
                // AUDxDAT with DMA off feeds the DAC directly; the channel
                // interrupt asks for the next word
                if (!a->dma_on) {
                    if (a->manual) {
                        a->manual_dat = value;
                        a->manual_next = true;
                    } else {
                        a->manual = true;
                        a->dat = value;
                        a->byte = 0;
                        a->out = (int8_t)(value >> 8);
                        a->counter = a->per ? a->per : 65536;
                    }
                } else {
                    a->dat = value;
                }
                break;
            default: break;
            }
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Frame, copper, sprites
// ---------------------------------------------------------------------------
void ach_begin_frame(amiga_chips *c)
{
    c->vpos = 0;
    c->lof = !c->lof;
    c->intreq |= 0x0020;                                    // VERTB
    if (c->ciaa_irq) c->intreq |= 0x0008;
    if (c->ciab_irq) c->intreq |= 0x2000;
    if (dma(c, COPEN)) {                                    // the copper restarts at COP1LC
        c->coppc = c->cop1lc;
        c->cop_stopped = false;
    }
}

static void sprite_dma_line(amiga_chips *c, int vpos)
{
    for (int s = 0; s < 8; s++) {
        uint16_t pos = c->sprpos[s], ctl = c->sprctl[s];
        const int vstart = (int)((pos >> 8) & 0xFF) | ((ctl & 4) ? 0x100 : 0);
        const int vstop = (int)((ctl >> 8) & 0xFF) | ((ctl & 2) ? 0x100 : 0);
        if (dma(c, SPREN)) {
            // copinit writes SPRxPT around line 12 (still vblank): POS / CTL
            // are fetched after that, so the dummy $FE00 / $FF00 terminator is
            // not walked into the following chip RAM
            uint32_t *pt = &c->sprpt[s];
            if (vpos == 20) {
                pos = chip_rd(c, *pt);
                ctl = chip_rd(c, *pt + 2);
                *pt += 4;
            } else if (vstop != 0 && vpos == vstop) {
                pos = chip_rd(c, *pt);
                ctl = chip_rd(c, *pt + 2);
                *pt += 4;
                c->sprdata[s] = 0;
                c->sprdatb[s] = 0;
            } else if (vpos > 20 && vpos >= vstart && vpos < vstop) {
                c->sprdata[s] = chip_rd(c, *pt);
                c->sprdatb[s] = chip_rd(c, *pt + 2);
                *pt += 4;
            }
            c->sprpos[s] = pos;
            c->sprctl[s] = ctl;
        }
        const int vs = (int)((pos >> 8) & 0xFF) | ((ctl & 4) ? 0x100 : 0);
        const int ve = (int)((ctl >> 8) & 0xFF) | ((ctl & 2) ? 0x100 : 0);
        c->sl_data[s] = c->sprdata[s];
        c->sl_datb[s] = c->sprdatb[s];
        c->sl_pos[s] = pos;
        c->sl_ctl[s] = ctl;
        c->sl_on[s] = vpos >= vs && vpos < ve && (c->sprdata[s] || c->sprdatb[s]);
    }
}

void ach_copper_line(amiga_chips *c, int vpos)
{
    c->vpos = vpos;
    sprite_dma_line(c, vpos);
    if (!dma(c, COPEN) || c->cop_stopped) return;
    copper_step_until_wait(c, vpos);
}

static void copper_step_until_wait(amiga_chips *c, int vpos)
{
    if (c->copper_active) return;
    c->copper_active = true;
    int nops = 0;
    // ~113 copper slots fit in an OCS line (32 MOVEs cut LoadView's palettes)
    for (int n = 0; n < 80; n++) {
        const uint32_t pc = c->coppc & 0x000FFFFEu;
        const uint16_t w1 = chip_rd(c, pc);
        const uint16_t w2 = chip_rd(c, pc + 2);
        c->coppc = pc + 4;
        if (w1 == 0xFFFF && (w2 & 0xFFFE) == 0xFFFE) {
            c->cop_stopped = true;
            break;
        }
        if ((w1 & 1) == 0) {                                // MOVE
            const uint16_t dest = (uint16_t)(w1 & 0x1FE);
            if (dest < 0x040) {
                if (++nops >= 4) {
                    c->cop_stopped = true;
                    break;
                }
                continue;
            }
            nops = 0;
            if (dest == 0x088) { c->coppc = c->cop1lc; continue; }
            if (dest == 0x08A) { c->coppc = c->cop2lc; continue; }
            ach_write(c, dest, w2);
            continue;
        }
        nops = 0;
        if (w2 & 1) continue;                               // SKIP
        const int wait_v = (w1 >> 8) & 0xFF;                // WAIT
        const int ve = ((w2 >> 8) & 0x7F) | 0x80;
        if ((vpos & ve) < (wait_v & ve)) {
            c->coppc -= 4;
            break;
        }
        if ((w1 & 0xFE) != 0) break;                        // horizontal WAIT: the rest of the line
    }
    c->copper_active = false;
}

// ---------------------------------------------------------------------------
// Blitter (done at once)
// ---------------------------------------------------------------------------
static void blit(amiga_chips *c)
{
    int height = c->bltsize >> 6;
    int width = c->bltsize & 0x3F;
    if (height == 0) height = 1024;
    if (width == 0) width = 64;

    const bool desc = (c->bltcon1 & 2) != 0;
    const int delta = desc ? -2 : 2;
    const int ashift = (c->bltcon0 >> 12) & 0xF;
    const int bshift = (c->bltcon1 >> 12) & 0xF;
    const bool usea = (c->bltcon0 & 0x0800) != 0;
    const bool useb = (c->bltcon0 & 0x0400) != 0;
    const bool usec = (c->bltcon0 & 0x0200) != 0;
    const bool used = (c->bltcon0 & 0x0100) != 0;
    const uint8_t mt = (uint8_t)c->bltcon0;

    // Agnus' blit pointers are word addresses (bit 0 ignored)
    uint32_t apt = c->bltapt & ~1u, bpt = c->bltbpt & ~1u, cpt = c->bltcpt & ~1u, dpt = c->bltdpt & ~1u;
    c->bzero = true;

    if (c->bltcon1 & 1) {
        // Line mode (HRM / WinUAE blitter_line): BLTAPT's low word is the
        // Bresenham accumulator (2*dy-dx, sign in BLTCON1 bit 6), BLTBMOD =
        // 4*dy added while it is negative, BLTAMOD = 4*(dy-dx) otherwise. The
        // pixel is BLTADAT >> ASH in the word at BLTCPT; BLTBDAT the texture
        // (rotated by BSH). The octant bits pick the major axis (SUD) and the
        // directions (SUL, AUL); SING draws one dot per line (fill outlines).
        int16_t acc = (int16_t)(c->bltapt & 0xFFFF);
        bool sign = (c->bltcon1 & 0x40) != 0;
        const bool sing = (c->bltcon1 & 0x02) != 0;
        const bool sud = (c->bltcon1 & 0x10) != 0;
        const bool sul = (c->bltcon1 & 0x08) != 0;
        const bool aul = (c->bltcon1 & 0x04) != 0;
        int ash = ashift, bsh = bshift;
        uint32_t cur = cpt, dout = dpt;
        bool onedot = false;
#define INCX() do { if (++ash == 16) { ash = 0; cur += 2; } } while (0)
#define DECX() do { if (ash-- == 0) { ash = 15; cur -= 2; } } while (0)
#define INCY() do { cur = (uint32_t)((int32_t)cur + c->bltcmod); onedot = false; } while (0)
#define DECY() do { cur = (uint32_t)((int32_t)cur - c->bltcmod); onedot = false; } while (0)
        for (int i = 0; i < height; i++) {
            const uint16_t a = (uint16_t)((c->bltadat & c->bltafwm) >> ash);
            const uint16_t b = ((c->bltbdat >> bsh) & 1) ? 0xFFFF : 0x0000;
            const uint16_t cc = usec ? chip_rd(c, cur & ~1u) : c->bltcdat;
            const uint16_t d = minterm(a, b, cc, mt);
            if (d) c->bzero = false;
            if (used && (!sing || !onedot)) chip_wr(c, dout & ~1u, d);
            onedot = true;
            if (!sign) acc = (int16_t)(acc + c->bltamod);
            else acc = (int16_t)(acc + c->bltbmod);
            if (!sign) {
                if (sud) { if (sul) DECY(); else INCY(); }
                else { if (sul) DECX(); else INCX(); }
            }
            if (sud) { if (aul) DECX(); else INCX(); }
            else { if (aul) DECY(); else INCY(); }
            sign = acc < 0;
            bsh = (bsh - 1) & 15;
            dout = cur;
        }
#undef INCX
#undef DECX
#undef INCY
#undef DECY
        c->bltapt = (c->bltapt & 0xFFFF0000u) | (uint16_t)acc;
        c->bltcpt = cur;
        c->bltdpt = cur;
        c->bltcon0 = (uint16_t)((c->bltcon0 & 0x0FFF) | (ash << 12));
        c->bltcon1 = (uint16_t)((c->bltcon1 & 0x0FBF) | (bsh << 12) | (sign ? 0x40 : 0));
        return;
    }

    // Area fill (descending only): BLTCON1 IFE (bit 3) / EFE (bit 4), FCI
    // (bit 2) the fill carry at the start of each line; bits from bit 0 up,
    // words from right to left.
    const bool ife = (c->bltcon1 & 0x08) != 0;
    const bool efe = (c->bltcon1 & 0x10) != 0;
    const bool fill = desc && (ife || efe);
    const bool fci = (c->bltcon1 & 0x04) != 0;
    // D is written a word late (after the next word's sources are read)
    bool pending = false;
    uint32_t pend_a = 0;
    uint16_t pend_d = 0;

    for (int y = 0; y < height; y++) {
        uint32_t a_hold = 0, b_hold = 0;
        bool carry = fci;
        for (int x = 0; x < width; x++) {
            uint16_t a_in = c->bltadat;
            if (usea) {
                a_in = chip_rd(c, apt);
                apt = (uint32_t)((int32_t)apt + delta);
            }
            uint16_t mask = 0xFFFF;
            if (x == 0) mask &= c->bltafwm;
            if (x == width - 1) mask &= c->bltalwm;
            a_in &= mask;
            uint16_t a_sh;
            if (desc) a_sh = (uint16_t)((((uint32_t)a_in << 16) | a_hold) >> (16 - ashift));
            else a_sh = (uint16_t)(((a_hold << 16) | a_in) >> ashift);
            a_hold = a_in;

            uint16_t b_in = c->bltbdat;
            if (useb) {
                b_in = chip_rd(c, bpt);
                bpt = (uint32_t)((int32_t)bpt + delta);
            }
            uint16_t b_sh;
            if (desc) b_sh = (uint16_t)((((uint32_t)b_in << 16) | b_hold) >> (16 - bshift));
            else b_sh = (uint16_t)(((b_hold << 16) | b_in) >> bshift);
            b_hold = b_in;
            if (!useb) b_sh = c->bltbdat;

            uint16_t cc = c->bltcdat;
            if (usec) {
                cc = chip_rd(c, cpt);
                cpt = (uint32_t)((int32_t)cpt + delta);
            }
            if (pending) {
                chip_wr(c, pend_a, pend_d);
                pending = false;
            }

            uint16_t d = minterm(a_sh, b_sh, cc, mt);
            if (fill) {
                uint16_t out = 0;
                for (int bit = 0; bit < 16; bit++) {
                    const bool in = (d >> bit) & 1;
                    carry = carry != in;
                    if (efe ? carry : (carry || in)) out |= (uint16_t)(1u << bit);
                }
                d = out;
            }
            if (d) c->bzero = false;
            if (used) {
                pending = true;
                pend_a = dpt;
                pend_d = d;
                dpt = (uint32_t)((int32_t)dpt + delta);
            }
        }
        // descending: the modulos are subtracted, like the word step
        const int msign = desc ? -1 : 1;
        apt = (uint32_t)((int32_t)apt + msign * c->bltamod);
        bpt = (uint32_t)((int32_t)bpt + msign * c->bltbmod);
        cpt = (uint32_t)((int32_t)cpt + msign * c->bltcmod);
        dpt = (uint32_t)((int32_t)dpt + msign * c->bltdmod);
    }
    if (pending) chip_wr(c, pend_a, pend_d);
    c->bltapt = apt;
    c->bltbpt = bpt;
    c->bltcpt = cpt;
    c->bltdpt = dpt;
}

// ---------------------------------------------------------------------------
// Disk DMA (the whole track at once; reads only)
// ---------------------------------------------------------------------------
static void disk_dma(amiga_chips *c)
{
    int n = 0;
    const uint16_t *mfm = (c->dsklen & 0x4000) ? NULL : amiga_track_mfm(&n);
    if (mfm && n > 0) {
        // the data starts where the disk is now; with WORDSYNC, after the
        // next sync word to pass under the head (DSKSYNC), as the real DMA
        // (dsp-cpp starts at the track's beginning: custom loaders that
        // check for the second $4489 at the buffer's start never get it)
        int start = amiga_disk_pos(n);
        if (c->adkcon & 0x0400) {
            for (int i = 0; i < n; i++) {
                const int at = (start + i) % n;
                if (mfm[at] == c->dsksync) {
                    c->intreq |= 0x1000;
                    start = (at + 1) % n;
                    break;
                }
            }
        }
        int words = c->dsklen & 0x3FFF;
        if (words == 0) words = 0x4000;
        uint32_t pt = c->dskpt;
        for (int i = 0; i < words; i++) {
            chip_wr(c, pt, mfm[(start + i) % n]);
            pt += 2;
        }
        c->dskpt = pt;
    }
    c->intreq |= 0x0002;                                    // DSKBLK
    c->dsklen &= 0x7FFF;
}

// ---------------------------------------------------------------------------
// Paula's audio
// ---------------------------------------------------------------------------
static void audio_fetch(amiga_chips *c, int ch)
{
    amiga_aud *a = &c->aud[ch];
    a->dat = chip_rd(c, a->pt);
    a->pt += 2;
    if (--a->words_left == 0) {
        // block done: the location / length latches reload, and the channel
        // interrupt says the program may queue the next block
        a->pt = a->lc;
        a->words_left = a->len ? a->len : 0x10000u;
        c->intreq |= (uint16_t)(0x0080 << ch);
        c->aud_irq = true;
    }
}

static void audio_dma_changed(amiga_chips *c, uint16_t old_dmacon)
{
    for (int ch = 0; ch < 4; ch++) {
        const uint16_t bit = (uint16_t)(1 << ch);
        const bool was = (old_dmacon & DMAEN) && (old_dmacon & bit);
        const bool now = (c->dmacon & DMAEN) && (c->dmacon & bit);
        amiga_aud *a = &c->aud[ch];
        if (!was && now) {
            a->dma_on = true;
            a->manual = a->manual_next = false;
            a->pt = a->lc;
            a->words_left = a->len ? a->len : 0x10000u;
            c->intreq |= (uint16_t)(0x0080 << ch);          // (the first word fetched)
            c->aud_irq = true;
            audio_fetch(c, ch);
            a->byte = 0;
            a->out = (int8_t)(a->dat >> 8);
            a->counter = a->per ? a->per : 65536;
        } else if (was && !now) {
            a->dma_on = false;
            a->out = 0;
        }
    }
}

bool AMIGA_HOT ach_audio_run(amiga_chips *c, int cck)
{
    int level = 0;
    for (int ch = 0; ch < 4; ch++) level += (int)c->aud[ch].out * (int)c->aud[ch].vol;
    c->aud_sum += level * cck;
    c->aud_cck += cck;
    c->aud_irq = false;
    for (int ch = 0; ch < 4; ch++) {
        amiga_aud *a = &c->aud[ch];
        if (!a->dma_on && !a->manual) continue;
        a->counter -= cck;
        while (a->counter <= 0) {
            int per = a->per ? a->per : 65536;
            if (a->dma_on && per < 124) per = 124;          // a DMA channel cannot fetch faster than its slot
            a->counter += per;
            if (a->byte == 0) {
                a->byte = 1;
                a->out = (int8_t)(a->dat & 0xFF);
                continue;
            }
            a->byte = 0;
            if (a->dma_on) {
                audio_fetch(c, ch);
            } else {
                c->intreq |= (uint16_t)(0x0080 << ch);
                c->aud_irq = true;
                if (!a->manual_next) {
                    a->manual = false;                      // the DAC holds: nothing more to play
                    a->out = 0;
                    break;
                }
                a->dat = a->manual_dat;
                a->manual_next = false;
            }
            a->out = (int8_t)(a->dat >> 8);
        }
    }
    return c->aud_irq;
}

int ach_audio_take_sample(amiga_chips *c)
{
    if (c->aud_cck == 0) {
        int level = 0;
        for (int ch = 0; ch < 4; ch++) level += (int)c->aud[ch].out * (int)c->aud[ch].vol;
        return level;
    }
    const int v = c->aud_sum / c->aud_cck;
    c->aud_sum = c->aud_cck = 0;
    return v;
}

// ---------------------------------------------------------------------------
// Denise: one line of the picture
// ---------------------------------------------------------------------------
static uint64_t expand8[256];       // a byte -> its 8 pixels, one byte each (0 / 1)

static inline uint16_t rgb565(int r8, int g8, int b8)
{
    return (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
}

static inline uint16_t col565(uint16_t c)
{
    return rgb565(((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17);
}

#define PIX_PAD 32

void AMIGA_HOT ach_render_line(amiga_chips *c, int vpos)
{
    if (!expand8[1]) {
        for (int b = 0; b < 256; b++) {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++)
                if (b & (0x80 >> i)) v |= (uint64_t)1 << (8 * i);
            expand8[b] = v;
        }
    }
    // display window: DIWSTOP's vertical V8 is the inverse of its V7
    const int vstart = c->diwstrt >> 8;
    const int vstop = (c->diwstop >> 8) | ((c->diwstop & 0x8000) ? 0 : 0x100);
    const int hstart = c->diwstrt & 0xFF;
    const int hstop = (c->diwstop & 0xFF) | 0x100;
    const bool vwin = vpos >= vstart && vpos < vstop;
    const bool hires = (c->bplcon0 & 0x8000) != 0;
    const bool ham = (c->bplcon0 & 0x0800) != 0;
    const bool dpf = (c->bplcon0 & 0x0400) != 0;
    int bpu = (c->bplcon0 >> 12) & 7;
    if (bpu > 6) bpu = 4;                                   // OCS: seven planes fetch four
    if (hires && bpu > 4) bpu = 4;

    // bitplane DMA: DDFSTRT..DDFSTOP words per plane, then the modulo (the
    // pointers move on every fetched line, also outside the picture)
    int ddfs = c->ddfstrt & 0xFC;
    if (ddfs < 0x18) ddfs = 0x18;
    int ddfe = c->ddfstop & 0xFC;
    if (ddfe > 0xD8) ddfe = 0xD8;
    int nwords = 0;
    // fetches go in 8-cycle units: one word per plane in lores, two in hires
    if (ddfe >= ddfs) nwords = (((ddfe - ddfs + 7) >> 3) + 1) * (hires ? 2 : 1);
    if (nwords > AMIGA_MAX_FETCH) nwords = AMIGA_MAX_FETCH;
    const bool fetch = vwin && bpu > 0 && dma(c, BPLEN) && nwords > 0;
    if (fetch) {
        for (int p = 0; p < bpu; p++) {
            for (int i = 0; i < nwords; i++) c->line_words[p][i] = chip_rd(c, c->bplpt[p] + (uint32_t)(i * 2));
            const int16_t mod = (p & 1) ? c->bpl2mod : c->bpl1mod;
            c->bplpt[p] = (uint32_t)((int32_t)c->bplpt[p] + nwords * 2 + mod);
        }
    }

    const int y = vpos - AMIGA_FIRST_LINE;
    if (y < 0 || y >= AMIGA_H) return;
    uint16_t *row = c->fb + y * AMIGA_W;
    const uint16_t c0 = c->color[0];
    const uint16_t c0p = col565(c0);
    if (!vwin) {
        for (int x = 0; x < AMIGA_W; x++) row[x] = c0p;
        return;
    }

    // playfield colour indices at hires resolution (two per lores pixel);
    // the first fetched pixel reaches the screen at 2*DDFSTRT+17 (lores) or
    // +9 (hires) in DIWSTRT units; BPLCON1 delays the odd planes (PF1H) and
    // the even ones (PF2H) by up to 15 lores pixels
    uint8_t pixbuf[AMIGA_W * 2 + 2 * PIX_PAD];
    uint8_t *pix = pixbuf + PIX_PAD;
    const int sub = hires ? 2 : 1;
    const int npix = AMIGA_W * sub;
    memset(pixbuf, 0, PIX_PAD + npix + PIX_PAD);
    if (fetch) {
        const int first = 2 * ddfs + (hires ? 9 : 17);
        for (int p = 0; p < bpu; p++) {
            const int delay = (p & 1) ? ((c->bplcon1 >> 4) & 15) : (c->bplcon1 & 15);
            const int origin = (AMIGA_FIRST_HPOS - (first + delay)) * sub;   // pixel offset of x = 0
            const uint16_t *words = c->line_words[p];
            for (int w = 0; w < nwords; w++) {
                const uint16_t v = words[w];
                if (!v) continue;
                const int at = w * 16 - origin;             // where its first pixel goes
                if (at <= -16 || at >= npix) continue;
                uint64_t hi, lo, cur;
                hi = expand8[v >> 8] << p;
                lo = expand8[v & 0xFF] << p;
                memcpy(&cur, pix + at, 8);
                cur |= hi;
                memcpy(pix + at, &cur, 8);
                memcpy(&cur, pix + at + 8, 8);
                cur |= lo;
                memcpy(pix + at + 8, &cur, 8);
            }
        }
    }

    // sprites on this line: the lowest-numbered one wins
    uint8_t spr_col[AMIGA_W];                               // colour register (0: none)
    uint8_t spr_pair[AMIGA_W];
    bool any_spr = false;
    for (int s = 7; s >= 0; s--) {
        if (!c->sl_on[s]) continue;
        if ((s & 1) && (c->sl_ctl[s] & 0x80) && c->sl_on[s - 1]) continue;   // attached odd one: drawn with its pair
        if (!any_spr) {
            memset(spr_col, 0, sizeof spr_col);
            any_spr = true;
        }
        const uint16_t pos = c->sl_pos[s], ctl = c->sl_ctl[s];
        const bool attached = !(s & 1) && (c->sl_ctl[s + 1] & 0x80) && c->sl_on[s + 1];
        const int x0 = (((pos & 0xFF) << 1) | (ctl & 1)) - AMIGA_FIRST_HPOS;
        const uint16_t da = c->sl_data[s], db = c->sl_datb[s];
        const uint16_t oa = attached ? c->sl_data[s + 1] : 0;
        const uint16_t ob = attached ? c->sl_datb[s + 1] : 0;
        for (int i = 0; i < 16; i++) {
            const int x = x0 + i;
            if (x < 0 || x >= AMIGA_W) continue;
            const uint16_t m = (uint16_t)(0x8000u >> i);
            int idx = ((da & m) ? 1 : 0) | ((db & m) ? 2 : 0);
            if (attached) idx |= ((oa & m) ? 4 : 0) | ((ob & m) ? 8 : 0);
            if (!idx) continue;
            spr_col[x] = (uint8_t)(attached ? 16 + idx : 16 + (s & ~1) * 2 + idx);
            spr_pair[x] = (uint8_t)(s >> 1);
        }
    }

    // BPLCON2: a playfield with priority code P is in front of sprite pairs
    // P..3 (single playfield: PF2P's code)
    const int pf1p = c->bplcon2 & 7;
    const int pf2p = (c->bplcon2 >> 3) & 7;
    const bool pf2pri = (c->bplcon2 & 0x40) != 0;
    uint16_t ham_col = c0;
    for (int x = 0; x < AMIGA_W; x++) {
        const int h = AMIGA_FIRST_HPOS + x;
        if (h < hstart || h >= hstop) {
            row[x] = c0p;
            ham_col = c0;
            continue;
        }
        int rsum = 0, gsum = 0, bsum = 0;
        bool front1 = false, front2 = false;                // non-transparent playfield pixels
        uint16_t col = c0;
        for (int k = 0; k < sub; k++) {
            const int idx = pix[x * sub + k];
            uint16_t cc;
            if (dpf) {
                const int p1 = (idx & 1) | ((idx >> 1) & 2) | ((idx >> 2) & 4);
                const int p2 = ((idx >> 1) & 1) | ((idx >> 2) & 2) | ((idx >> 3) & 4);
                if (p1) front1 = true;
                if (p2) front2 = true;
                if (pf2pri) cc = p2 ? c->color[8 + p2] : p1 ? c->color[p1] : c0;
                else cc = p1 ? c->color[p1] : p2 ? c->color[8 + p2] : c0;
            } else if (ham && bpu >= 5) {
                const int v = idx & 15;
                switch ((idx >> 4) & 3) {
                case 0: ham_col = c->color[v]; break;
                case 1: ham_col = (uint16_t)((ham_col & 0xFF0) | v); break;
                case 2: ham_col = (uint16_t)((ham_col & 0x0FF) | (v << 8)); break;
                default: ham_col = (uint16_t)((ham_col & 0xF0F) | (v << 4)); break;
                }
                cc = ham_col;
                if (idx) front2 = true;
            } else {
                if (idx) front2 = true;
                if (idx >= 32) cc = (uint16_t)((c->color[idx - 32] >> 1) & 0x777);   // EHB
                else cc = c->color[idx];
            }
            col = cc;
            rsum += (cc >> 8) & 15;
            gsum += (cc >> 4) & 15;
            bsum += cc & 15;
        }
        if (any_spr && spr_col[x]) {
            const int pair = spr_pair[x];
            bool hidden;
            if (dpf) hidden = (front1 && pair >= pf1p) || (front2 && pair >= pf2p);
            else hidden = front2 && pair >= pf2p;
            if (!hidden) {
                row[x] = col565(c->color[spr_col[x]]);
                continue;
            }
        }
        if (sub == 1) row[x] = col565(col);
        else row[x] = rgb565(rsum * 17 / 2, gsum * 17 / 2, bsum * 17 / 2);
    }
}
