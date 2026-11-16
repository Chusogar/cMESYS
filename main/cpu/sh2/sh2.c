#include <string.h>
#include "sh2.h"
#define DMA_STEP(addr, mode, size) do { if ((mode) == 1) (addr) += (size); else if ((mode) == 2) (addr) -= (size); } while (0)



enum { kT = 0x001 };
enum { kS = 0x002 };
enum { kQ = 0x100 };
enum { kM = 0x200 };
enum { kSrMask = 0x3f3 };

static inline uint32_t sext8(uint32_t v) { return (uint32_t)((int32_t)((int8_t)(v))); }
static inline uint32_t sext16(uint32_t v) { return (uint32_t)((int32_t)((int16_t)(v))); }
static inline uint32_t sext12(uint32_t v) { return (v & 0x800) ? (v | 0xfffff000u) : (v & 0xfff); }

// On-chip register offsets from $FFFFFE00.
enum { kTier = 0x10, kFtcsr = 0x11, kFrcH = 0x12, kFrcL = 0x13, kOcrH = 0x14, kOcrL = 0x15, kTcr = 0x16, kTocr = 0x17 };
enum { kIprb = 0x60, kVcrc = 0x66, kVcrd = 0x68 };
enum { kIpra = 0xe2, kVcrwdt = 0xe4 };
static uint8_t read8(sh2 *P, uint32_t a);
static uint16_t read16(sh2 *P, uint32_t a);
static uint32_t read32(sh2 *P, uint32_t a);
static void write8(sh2 *P, uint32_t a, uint8_t v);
static void write16(sh2 *P, uint32_t a, uint16_t v);
static void write32(sh2 *P, uint32_t a, uint32_t v);
static uint16_t fetch(sh2 *P, uint32_t a);
static uint8_t onchip_read8(sh2 *P, uint32_t a);
static uint16_t onchip_read16(sh2 *P, uint32_t a);
static uint32_t onchip_read32(sh2 *P, uint32_t a);
static void onchip_write8(sh2 *P, uint32_t a, uint8_t v);
static void onchip_write16(sh2 *P, uint32_t a, uint16_t v);
static void onchip_write32(sh2 *P, uint32_t a, uint32_t v);
static void divu_32(sh2 *P);
static void divu_64(sh2 *P);
static void dma_check(sh2 *P, int channel);
static void dma_run(sh2 *P, int channel, bool external);
static int peripheral_level(sh2 *P, int* vector);
static void tick_peripherals(sh2 *P, int cycles);
static void exception(sh2 *P, uint32_t vector);
static bool check_interrupts(sh2 *P);
static void delay_slot(sh2 *P, uint32_t target);
static void execute(sh2 *P, uint16_t op);

void sh2_map_memory(sh2 *P, int page, uint8_t* data, uint32_t size, bool writable) {
    sh2_page *p = &P->pages_[(size_t)(page) & 31];
    p->data = data;
    p->mask = size - 1;
    p->writable = writable;
}
#if 0
{
}
#endif
void sh2_reset(sh2 *P) {
    memset(P->r_, 0, sizeof P->r_);
    P->sr_ = 0xf0;
    P->vbr_ = 0;
    P->gbr_ = 0;
    P->pr_ = 0;
    P->mach_ = P->macl_ = 0;
    P->sleeping_ = false;
    P->in_slot_ = false;
    memset(P->regs_, 0, sizeof P->regs_);
    P->frc_ = 0;
    P->ocra_ = P->ocrb_ = 0xffff;
    P->ftcsr_ = 0;
    P->frt_div_ = 0;
    P->wtcsr_ = 0x18;
    P->wtcnt_ = 0;
    P->wdt_div_ = 0;
    P->dvsr_ = P->dvdnth_ = P->dvdntl_ = P->dvcr_ = 0;
    memset(P->dma_, 0, sizeof P->dma_);
    P->dmaor_ = 0;
    P->periph_dirty_ = true;
    P->pc_ = read32(P, 0);
    P->r_[15] = read32(P, 4);
}

// ---------------------------------------------------------------------------
// Memory
static uint8_t read8(sh2 *P, uint32_t a) {
    switch (a >> 29) {
        case 2:
        case 3: return 0;
        case 6: return P->cache_ram_[a & 0xfff];
        case 7: return (a >= 0xfffffe00u) ? onchip_read8(P, a) : 0;
        default: break;
    }
    a &= 0x1fffffff;
    const sh2_page *p = &P->pages_[a >> 24];
    if (p->data) return p->data[a & p->mask];
    return P->bus.read8(P->bus.user, a);
}
static uint16_t read16(sh2 *P, uint32_t a) {
    a &= ~1u;
    switch (a >> 29) {
        case 2:
        case 3: return 0;
        case 6: return (uint16_t)((P->cache_ram_[a & 0xfff] << 8) | P->cache_ram_[(a + 1) & 0xfff]);
        case 7: return (a >= 0xfffffe00u) ? onchip_read16(P, a) : 0;
        default: break;
    }
    a &= 0x1fffffff;
    const sh2_page *p = &P->pages_[a >> 24];
    if (p->data) {
        const uint8_t* d = p->data + (a & p->mask);
        return (uint16_t)((d[0] << 8) | d[1]);
    }
    return P->bus.read16(P->bus.user, a);
}
static uint32_t read32(sh2 *P, uint32_t a) {
    a &= ~3u;
    switch (a >> 29) {
        case 2:
        case 3: return 0;
        case 6: {
            const uint8_t* d = &P->cache_ram_[a & 0xffc];
            return ((uint32_t)(d[0]) << 24) | ((uint32_t)(d[1]) << 16) | ((uint32_t)(d[2]) << 8) | d[3];
        }
        case 7: return (a >= 0xfffffe00u) ? onchip_read32(P, a) : 0;
        default: break;
    }
    a &= 0x1fffffff;
    const sh2_page *p = &P->pages_[a >> 24];
    if (p->data) {
        const uint8_t* d = p->data + (a & p->mask);
        return ((uint32_t)(d[0]) << 24) | ((uint32_t)(d[1]) << 16) | ((uint32_t)(d[2]) << 8) | d[3];
    }
    return P->bus.read32(P->bus.user, a);
}
static void write8(sh2 *P, uint32_t a, uint8_t v) {
        switch (a >> 29) {
        case 2:
        case 3: return;
        case 6: P->cache_ram_[a & 0xfff] = v; return;
        case 7:
            if (a >= 0xfffffe00u) onchip_write8(P, a, v);
            return;
        default: break;
    }
    a &= 0x1fffffff;
    const sh2_page *p = &P->pages_[a >> 24];
    if (p->data && p->writable) {
        p->data[a & p->mask] = v;
        return;
    }
    P->bus.write8(P->bus.user, a, v);
}
static void write16(sh2 *P, uint32_t a, uint16_t v) {
        a &= ~1u;
    switch (a >> 29) {
        case 2:
        case 3: return;
        case 6:
            P->cache_ram_[a & 0xfff] = (uint8_t)(v >> 8);
            P->cache_ram_[(a + 1) & 0xfff] = (uint8_t)(v);
            return;
        case 7:
            if (a >= 0xfffffe00u) onchip_write16(P, a, v);
            return;
        default: break;
    }
    a &= 0x1fffffff;
    const sh2_page *p = &P->pages_[a >> 24];
    if (p->data && p->writable) {
        uint8_t* d = p->data + (a & p->mask);
        d[0] = (uint8_t)(v >> 8);
        d[1] = (uint8_t)(v);
        return;
    }
    P->bus.write16(P->bus.user, a, v);
}
static void write32(sh2 *P, uint32_t a, uint32_t v) {
        a &= ~3u;
    switch (a >> 29) {
        case 2:
        case 3: return;
        case 6: {
            uint8_t* d = &P->cache_ram_[a & 0xffc];
            d[0] = (uint8_t)(v >> 24);
            d[1] = (uint8_t)(v >> 16);
            d[2] = (uint8_t)(v >> 8);
            d[3] = (uint8_t)(v);
            return;
        }
        case 7:
            if (a >= 0xfffffe00u) onchip_write32(P, a, v);
            return;
        default: break;
    }
    a &= 0x1fffffff;
    const sh2_page *p = &P->pages_[a >> 24];
    if (p->data && p->writable) {
        uint8_t* d = p->data + (a & p->mask);
        d[0] = (uint8_t)(v >> 24);
        d[1] = (uint8_t)(v >> 16);
        d[2] = (uint8_t)(v >> 8);
        d[3] = (uint8_t)(v);
        return;
    }
    P->bus.write32(P->bus.user, a, v);
}
static uint16_t fetch(sh2 *P, uint32_t a) {
    if ((a >> 29) <= 1) {
        const uint32_t e = a & 0x1ffffffe;
        const sh2_page *p = &P->pages_[e >> 24];
        if (p->data) {
            const uint8_t* d = p->data + (e & p->mask);
            return (uint16_t)((d[0] << 8) | d[1]);
        }
    }
    return read16(P, a);
}

// ---------------------------------------------------------------------------
// On-chip modules
static uint8_t onchip_read8(sh2 *P, uint32_t a) {
    const uint32_t o = a & 0x1ff;
    switch (o) {
        case kFtcsr: return P->ftcsr_;
        case kFrcH: return (uint8_t)(P->frc_ >> 8);
        case kFrcL: return (uint8_t)(P->frc_);
        case kOcrH: return (uint8_t)(((P->regs_[kTocr] & 0x10) ? P->ocrb_ : P->ocra_) >> 8);
        case kOcrL: return (uint8_t)((P->regs_[kTocr] & 0x10) ? P->ocrb_ : P->ocra_);
        case 0x80: return (uint8_t)(P->wtcsr_ | 0x18);
        case 0x81: return P->wtcnt_;
        case 0x82: return (uint8_t)(P->regs_[0x83] | 0x1f);
        default: break;
    }
    if (o >= 0x100) {
        const uint32_t v = onchip_read32(P, a & ~3u);
        return (uint8_t)(v >> (8 * (3 - (o & 3))));
    }
    return P->regs_[o];
}
static uint16_t onchip_read16(sh2 *P, uint32_t a) {
    const uint32_t o = a & 0x1fe;
    if (o >= 0x100) {
        const uint32_t v = onchip_read32(P, a & ~3u);
        return (uint16_t)((o & 2) ? v : (v >> 16));
    }
    if ((o >= 0x60 && o < 0x6a) || (o >= 0xe0 && o < 0xe6)) {
        return (uint16_t)((P->regs_[o] << 8) | P->regs_[o + 1]);
    }
    return (uint16_t)((onchip_read8(P, a) << 8) | onchip_read8(P, a + 1));
}
static uint32_t onchip_read32(sh2 *P, uint32_t a) {
    const uint32_t o = a & 0x1fc;
    if (o < 0x100) return ((uint32_t)(onchip_read16(P, a)) << 16) | onchip_read16(P, a + 2);
    switch (o) {
        case 0x100: return P->dvsr_;
        case 0x104: return P->dvdntl_;
        case 0x108: return P->dvcr_;
        case 0x10c: return ((uint32_t)(P->regs_[0x10e]) << 8) | P->regs_[0x10f];
        case 0x110:
        case 0x118: return P->dvdnth_;
        case 0x114:
        case 0x11c: return P->dvdntl_;
        case 0x180: return P->dma_[0].sar;
        case 0x184: return P->dma_[0].dar;
        case 0x188: return P->dma_[0].tcr;
        case 0x18c: return P->dma_[0].chcr;
        case 0x190: return P->dma_[1].sar;
        case 0x194: return P->dma_[1].dar;
        case 0x198: return P->dma_[1].tcr;
        case 0x19c: return P->dma_[1].chcr;
        case 0x1a0: return P->dma_[0].vcr;
        case 0x1a8: return P->dma_[1].vcr;
        case 0x1b0: return P->dmaor_;
        default: break;
    }
    return ((uint32_t)(P->regs_[o]) << 24) | ((uint32_t)(P->regs_[o + 1]) << 16) | ((uint32_t)(P->regs_[o + 2]) << 8) |
           P->regs_[o + 3];
}
static void onchip_write8(sh2 *P, uint32_t a, uint8_t v) {
    P->periph_dirty_ = true;
    const uint32_t o = a & 0x1ff;
    if (o >= 0x100) {
        // Byte writes to the 32-bit modules: merge into the long.
        const uint32_t base = a & ~3u;
        const int shift = 8 * (int)(3 - (o & 3));
        uint32_t cur = onchip_read32(P, base);
        cur = (cur & ~(0xffu << shift)) | ((uint32_t)(v) << shift);
        onchip_write32(P, base, cur);
        return;
    }
    switch (o) {
        case kFtcsr:
            // Flags clear by writing 0 after reading 1; CCLRA is plain.
            P->ftcsr_ = (uint8_t)((P->ftcsr_ & v & 0x8e) | (v & 0x01));
            return;
        case kFrcH: P->frc_ = (uint16_t)((P->frc_ & 0x00ff) | (v << 8)); return;
        case kFrcL: P->frc_ = (uint16_t)((P->frc_ & 0xff00) | v); return;
        case kOcrH:
            if (P->regs_[kTocr] & 0x10) P->ocrb_ = (uint16_t)((P->ocrb_ & 0x00ff) | (v << 8));
            else P->ocra_ = (uint16_t)((P->ocra_ & 0x00ff) | (v << 8));
            return;
        case kOcrL:
            if (P->regs_[kTocr] & 0x10) P->ocrb_ = (uint16_t)((P->ocrb_ & 0xff00) | v);
            else P->ocra_ = (uint16_t)((P->ocra_ & 0xff00) | v);
            return;
        case 0x71:
        case 0x72: P->regs_[o] = v; return;
        case 0x92:
            // CCR: CP (bit 4) purges the cache and reads back 0.
            P->regs_[o] = (uint8_t)(v & ~0x10);
            return;
        default: break;
    }
    P->regs_[o] = v;
}
static void onchip_write16(sh2 *P, uint32_t a, uint16_t v) {
    P->periph_dirty_ = true;
    const uint32_t o = a & 0x1fe;
    if (o >= 0x100) {
        const uint32_t base = a & ~3u;
        uint32_t cur = onchip_read32(P, base);
        if (o & 2) cur = (cur & 0xffff0000u) | v;
        else cur = (cur & 0x0000ffffu) | ((uint32_t)(v) << 16);
        onchip_write32(P, base, cur);
        return;
    }
    if (o == 0x80) {
        // Watchdog: $A5xx writes WTCSR, $5Axx writes WTCNT.
        if ((v >> 8) == 0xa5) {
            const uint8_t nv = (uint8_t)(v);
            P->wtcsr_ = (uint8_t)((nv & 0x7f) | (P->wtcsr_ & nv & 0x80));
        } else if ((v >> 8) == 0x5a) {
            P->wtcnt_ = (uint8_t)(v);
        }
        return;
    }
    if (o == 0x82) {
        if ((v >> 8) == 0x5a) P->regs_[0x83] = (uint8_t)(v & 0x60);
        return;
    }
    if ((o >= 0x60 && o < 0x6a) || (o >= 0xe0 && o < 0xe6)) {
        P->regs_[o] = (uint8_t)(v >> 8);
        P->regs_[o + 1] = (uint8_t)(v);
        return;
    }
    onchip_write8(P, a, (uint8_t)(v >> 8));
    onchip_write8(P, a + 1, (uint8_t)(v));
}
static void onchip_write32(sh2 *P, uint32_t a, uint32_t v) {
    P->periph_dirty_ = true;
    const uint32_t o = a & 0x1fc;
    if (o < 0x100) {
        onchip_write16(P, a, (uint16_t)(v >> 16));
        onchip_write16(P, a + 2, (uint16_t)(v));
        return;
    }
    switch (o) {
        case 0x100: P->dvsr_ = v; return;
        case 0x104: P->dvdntl_ = v; divu_32(P); return;
        case 0x108: P->dvcr_ = v & 3; return;
        case 0x110:
        case 0x118: P->dvdnth_ = v; return;
        case 0x114:
        case 0x11c: P->dvdntl_ = v; divu_64(P); return;
        case 0x180: P->dma_[0].sar = v; return;
        case 0x184: P->dma_[0].dar = v; return;
        case 0x188: P->dma_[0].tcr = v & 0xffffff; return;
        case 0x18c:
            // TE is cleared by writing 0 after reading 1.
            P->dma_[0].chcr = (v & ~2u) | (P->dma_[0].chcr & v & 2u);
            dma_check(P, 0);
            return;
        case 0x190: P->dma_[1].sar = v; return;
        case 0x194: P->dma_[1].dar = v; return;
        case 0x198: P->dma_[1].tcr = v & 0xffffff; return;
        case 0x19c:
            P->dma_[1].chcr = (v & ~2u) | (P->dma_[1].chcr & v & 2u);
            dma_check(P, 1);
            return;
        case 0x1a0: P->dma_[0].vcr = v & 0x7f; return;
        case 0x1a8: P->dma_[1].vcr = v & 0x7f; return;
        case 0x1b0:
            P->dmaor_ = (v & 0x9) | (P->dmaor_ & v & 0x6);
            dma_check(P, 0);
            dma_check(P, 1);
            return;
        default: break;
    }
    P->regs_[o] = (uint8_t)(v >> 24);
    P->regs_[o + 1] = (uint8_t)(v >> 16);
    P->regs_[o + 2] = (uint8_t)(v >> 8);
    P->regs_[o + 3] = (uint8_t)(v);
}
static void divu_32(sh2 *P) {
    const int32_t d = (int32_t)(P->dvdntl_);
    const int32_t s = (int32_t)(P->dvsr_);
    if (s == 0 || (d == INT32_MIN && s == -1)) {
        P->dvcr_ |= 1;
        P->dvdnth_ = (uint32_t)(d);
        P->dvdntl_ = (d < 0) ? 0x80000000u : 0x7fffffffu;
        return;
    }
    P->dvdntl_ = (uint32_t)(d / s);
    P->dvdnth_ = (uint32_t)(d % s);
}
static void divu_64(sh2 *P) {
    const int64_t d = (int64_t)(((uint64_t)(P->dvdnth_) << 32) | P->dvdntl_);
    const int32_t s = (int32_t)(P->dvsr_);
    if (s == 0) {
        P->dvcr_ |= 1;
        P->dvdntl_ = (d < 0) ? 0x80000000u : 0x7fffffffu;
        return;
    }
    const int64_t q = d / s;
    const int64_t rem = d % s;
    if (q > INT32_MAX || q < INT32_MIN) {
        P->dvcr_ |= 1;
        P->dvdntl_ = (q < 0) ? 0x80000000u : 0x7fffffffu;
        return;
    }
    P->dvdntl_ = (uint32_t)((int32_t)(q));
    P->dvdnth_ = (uint32_t)((int32_t)(rem));
}
static void dma_check(sh2 *P, int channel) {
    const sh2_dma *c = &P->dma_[(size_t)(channel)];
    // Auto request runs the whole block as soon as it is enabled.
    if ((c->chcr & 0x200) != 0) dma_run(P, channel, false);
    else dma_run(P, channel, true);
}
void sh2_dma_request(sh2 *P, int channel) { dma_run(P, channel, true); }
static void dma_run(sh2 *P, int channel, bool external) {
    if (P->dma_busy_) return;
    sh2_dma *c = &P->dma_[(size_t)(channel)];
    if ((P->dmaor_ & 1) == 0 || (P->dmaor_ & 6) != 0) return;
    if ((c->chcr & 1) == 0 || (c->chcr & 2) != 0) return;
    if (external && (c->chcr & 0x200) != 0) return;
    if (!external && (c->chcr & 0x200) == 0) return;
    P->dma_busy_ = true;
    const int ts = (int)((c->chcr >> 10) & 3);
    const uint32_t unit = ts == 0 ? 1 : ts == 1 ? 2 : 4;
    const int sm = (int)((c->chcr >> 12) & 3);
    const int dm = (int)((c->chcr >> 14) & 3);
    uint32_t count = c->tcr ? c->tcr : 0x1000000;
    while (count > 0) {
        if (external && !P->bus.dreq(P->bus.user, channel)) break;
        if (ts == 3) {
            // 16-byte unit: four longs, the count drops by 4.
            for (int i = 0; i < 4; i++) {
                write32(P, c->dar, read32(P, c->sar));
                if (sm) c->sar += 4;
                DMA_STEP(c->dar, dm, 4);
            }
            count = count > 4 ? count - 4 : 0;
        } else {
            if (unit == 1) write8(P, c->dar, read8(P, c->sar));
            else if (unit == 2) write16(P, c->dar, read16(P, c->sar));
            else write32(P, c->dar, read32(P, c->sar));
            DMA_STEP(c->sar, sm, unit);
            DMA_STEP(c->dar, dm, unit);
            count--;
        }
    }
    c->tcr = count & 0xffffff;
    if (count == 0) c->chcr |= 2;  // TE
    P->periph_dirty_ = true;
    P->dma_busy_ = false;
}
static int peripheral_level(sh2 *P, int* vector) {
    int best = 0;
#define offer(level, vec) do { int l_ = (level), v_ = (vec); if (l_ > best) { best = l_; *vector = v_; } } while (0)
    const uint16_t ipra = (uint16_t)((P->regs_[kIpra] << 8) | P->regs_[kIpra + 1]);
    const uint16_t iprb = (uint16_t)((P->regs_[kIprb] << 8) | P->regs_[kIprb + 1]);
    // DMA transfer end.
    for (int ch = 0; ch < 2; ch++) {
        const sh2_dma *c = &P->dma_[(size_t)(ch)];
        if ((c->chcr & 0x6) == 0x6) offer((ipra >> 8) & 15, (int)(c->vcr & 0x7f));
    }
    // Division overflow.
    if ((P->dvcr_ & 3) == 3) offer((ipra >> 12) & 15, P->regs_[0x10f] & 0x7f);
    // Watchdog interval.
    if ((P->wtcsr_ & 0xc0) == 0x80) offer((ipra >> 4) & 15, P->regs_[kVcrwdt] & 0x7f);
    // Free-running timer.
    const uint8_t tier = P->regs_[kTier];
    const int frt = (iprb >> 8) & 15;
    if ((P->ftcsr_ & tier & 0x80) != 0) offer(frt, P->regs_[kVcrc] & 0x7f);
    else if ((P->ftcsr_ & tier & 0x0c) != 0) offer(frt, P->regs_[kVcrc + 1] & 0x7f);
    else if ((P->ftcsr_ & tier & 0x02) != 0) offer(frt, P->regs_[kVcrd] & 0x7f);
    return best;
#undef offer
}
static void tick_peripherals(sh2 *P, int cycles) {
    // FRT: Pφ / 8, 32 or 128.
    static const int kFrtDiv[4] = {8, 32, 128, 128};
    P->frt_div_ += cycles;
    const int fdiv = kFrtDiv[P->regs_[kTcr] & 3];
    if (P->frt_div_ < fdiv) goto wdt;
    {
        // Jump straight to just before the next compare / overflow.
        const int steps = P->frt_div_ / fdiv;
        int safe = (uint16_t)(P->ocra_ - P->frc_);
        if ((uint16_t)(P->ocrb_ - P->frc_) < safe) safe = (uint16_t)(P->ocrb_ - P->frc_);
        if ((uint16_t)(0 - P->frc_) < safe) safe = (uint16_t)(0 - P->frc_);
        safe -= 1;
        if (safe > 0) {
            const int skip = safe < steps ? safe : steps;
            P->frc_ = (uint16_t)(P->frc_ + skip);
            P->frt_div_ -= skip * fdiv;
        }
    }
    while (P->frt_div_ >= fdiv) {
        P->frt_div_ -= fdiv;
        P->frc_++;
        if (P->frc_ == P->ocra_) {
            P->ftcsr_ |= 0x08;
            P->periph_dirty_ = true;
            if (P->ftcsr_ & 0x01) P->frc_ = 0;
        }
        if (P->frc_ == P->ocrb_) {
            P->ftcsr_ |= 0x04;
            P->periph_dirty_ = true;
        }
        if (P->frc_ == 0) {
            P->ftcsr_ |= 0x02;
            P->periph_dirty_ = true;
        }
    }
wdt:
    // Watchdog interval timer.
    if (P->wtcsr_ & 0x20) {
        static const int kWdtDiv[8] = {2, 64, 128, 256, 512, 1024, 4096, 8192};
        P->wdt_div_ += cycles;
        const int wdiv = kWdtDiv[P->wtcsr_ & 7];
        while (P->wdt_div_ >= wdiv) {
            P->wdt_div_ -= wdiv;
            if (++P->wtcnt_ == 0) {
                P->wtcsr_ |= 0x80;
                P->periph_dirty_ = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Execution
static void exception(sh2 *P, uint32_t vector) {
        P->r_[15] -= 4;
    write32(P, P->r_[15], P->sr_);
    P->r_[15] -= 4;
    write32(P, P->r_[15], P->pc_);
    P->pc_ = read32(P, P->vbr_ + vector * 4);
}
static bool check_interrupts(sh2 *P) {
    const int mask = (int)((P->sr_ >> 4) & 15);
    if (P->periph_dirty_) {
        P->periph_dirty_ = false;
        P->periph_level_ = peripheral_level(P, &P->periph_vector_);
    }
    if (P->irl_ <= mask && P->periph_level_ <= mask) return false;
    int vector = P->periph_vector_;
    int level = P->periph_level_;
    if (P->irl_ > level) {
        level = P->irl_;
        vector = 64 + (P->irl_ >> 1);
    }
    if (level <= mask) return false;
    P->sleeping_ = false;
    exception(P, (uint32_t)(vector));
    P->sr_ = (P->sr_ & ~0xf0u) | ((uint32_t)(level) << 4);
    P->cycles_ += 13;
    return true;
}
static void delay_slot(sh2 *P, uint32_t target) {
    const uint16_t op = fetch(P, P->pc_);
    // The pipeline is already fetching from the branch target, so a
    // PC-relative load or MOVA in the slot is relative to the target
    // (After Burner Complete's palette code depends on it).
    P->pc_ = target;
    P->in_slot_ = true;
    execute(P, op);
    P->in_slot_ = false;
    P->pc_ = target;
}
int sh2_run(sh2 *P, int cycles) {
    P->cycles_ = 0;
    int ticked = 0;
    while (P->cycles_ < cycles) {
        if (!check_interrupts(P) && P->sleeping_) {
            P->cycles_ = cycles;
            break;
        }
                const uint16_t op = fetch(P, P->pc_);
        P->pc_ += 2;
        P->cycles_++;
        execute(P, op);
        // Timers advance in small batches: their flags (and interrupts)
        // show up within 32 cycles.
        if (P->cycles_ - ticked >= 32) {
            tick_peripherals(P, P->cycles_ - ticked);
            ticked = P->cycles_;
        }
    }
    tick_peripherals(P, P->cycles_ - ticked);
    P->total_cycles_ += (uint64_t)(P->cycles_);
    return P->cycles_;
}
static void execute(sh2 *P, uint16_t op) {
    const int n = (op >> 8) & 15;
    const int m = (op >> 4) & 15;
#define rn (P->r_[n])
#define rm (P->r_[m])
#define set_t(v) (P->sr_ = (v) ? (P->sr_ | kT) : (P->sr_ & ~kT))
#define t() ((P->sr_ & kT) != 0)

    switch (op >> 12) {
        case 0x0:
            switch (op & 0xf) {
                case 0x2:
                    if (m == 0) rn = P->sr_;
                    else if (m == 1) rn = P->gbr_;
                    else if (m == 2) rn = P->vbr_;
                    return;
                case 0x3:
                    if (m == 0) {  // BSRF Rn
                        P->pr_ = P->pc_ + 2;
                        P->cycles_++;
                        delay_slot(P, P->pc_ + 2 + rn);
                    } else if (m == 2) {  // BRAF Rn
                        P->cycles_++;
                        delay_slot(P, P->pc_ + 2 + rn);
                    }
                    return;
                case 0x4: write8(P, rn + P->r_[0], (uint8_t)(rm)); P->cycles_++; return;
                case 0x5: write16(P, rn + P->r_[0], (uint16_t)(rm)); P->cycles_++; return;
                case 0x6: write32(P, rn + P->r_[0], rm); P->cycles_++; return;
                case 0x7: P->macl_ = rn * rm; P->cycles_ += 2; return;
                case 0x8:
                    if (m == 0) P->sr_ &= ~kT;
                    else if (m == 1) P->sr_ |= kT;
                    else if (m == 2) P->mach_ = P->macl_ = 0;
                    return;
                case 0x9:
                    if (m == 0) return;  // NOP
                    if (m == 1) {  // DIV0U
                        P->sr_ &= ~(kM | kQ | kT);
                    } else if (m == 2) {  // MOVT
                        rn = P->sr_ & kT;
                    }
                    return;
                case 0xa:
                    if (m == 0) rn = P->mach_;
                    else if (m == 1) rn = P->macl_;
                    else if (m == 2) rn = P->pr_;
                    return;
                case 0xb:
                    if (m == 0) {  // RTS
                        P->cycles_++;
                        delay_slot(P, P->pr_);
                    } else if (m == 1) {  // SLEEP
                        P->sleeping_ = true;
                        P->cycles_ += 2;
                    } else if (m == 2) {  // RTE
                        const uint32_t target = read32(P, P->r_[15]);
                        P->r_[15] += 4;
                        P->sr_ = read32(P, P->r_[15]) & kSrMask;
                        P->r_[15] += 4;
                        P->cycles_ += 3;
                        delay_slot(P, target);
                    }
                    return;
                case 0xc: rn = sext8(read8(P, rm + P->r_[0])); P->cycles_++; return;
                case 0xd: rn = sext16(read16(P, rm + P->r_[0])); P->cycles_++; return;
                case 0xe: rn = read32(P, rm + P->r_[0]); P->cycles_++; return;
                case 0xf: {  // MAC.L @Rm+,@Rn+
                    const int64_t a = (int32_t)(read32(P, rn));
                    rn += 4;
                    const int64_t b = (int32_t)(read32(P, rm));
                    rm += 4;
                    int64_t acc = (int64_t)(((uint64_t)(P->mach_) << 32) | P->macl_);
                    acc += a * b;
                    if (P->sr_ & kS) {
                        const int64_t lo = -((int64_t)(1) << 47);
                        const int64_t hi = ((int64_t)(1) << 47) - 1;
                        if (acc < lo) acc = lo;
                        if (acc > hi) acc = hi;
                    }
                    P->mach_ = (uint32_t)((uint64_t)(acc) >> 32);
                    P->macl_ = (uint32_t)(acc);
                    P->cycles_ += 3;
                    return;
                }
                default: return;
            }
        case 0x1: write32(P, rn + (uint32_t)(op & 0xf) * 4, rm); P->cycles_++; return;
        case 0x2:
            switch (op & 0xf) {
                case 0x0: write8(P, rn, (uint8_t)(rm)); P->cycles_++; return;
                case 0x1: write16(P, rn, (uint16_t)(rm)); P->cycles_++; return;
                case 0x2: write32(P, rn, rm); P->cycles_++; return;
                case 0x4: write8(P, rn - 1, (uint8_t)(rm)); rn -= 1; P->cycles_++; return;
                case 0x5: write16(P, rn - 2, (uint16_t)(rm)); rn -= 2; P->cycles_++; return;
                case 0x6: write32(P, rn - 4, rm); rn -= 4; P->cycles_++; return;
                case 0x7:  // DIV0S
                    P->sr_ = (rn & 0x80000000u) ? (P->sr_ | kQ) : (P->sr_ & ~kQ);
                    P->sr_ = (rm & 0x80000000u) ? (P->sr_ | kM) : (P->sr_ & ~kM);
                    set_t(((P->sr_ & kQ) != 0) != ((P->sr_ & kM) != 0));
                    return;
                case 0x8: set_t((rn & rm) == 0); return;
                case 0x9: rn &= rm; return;
                case 0xa: rn ^= rm; return;
                case 0xb: rn |= rm; return;
                case 0xc: {  // CMP/STR
                    const uint32_t x = rn ^ rm;
                    set_t((x & 0xff000000u) == 0 || (x & 0x00ff0000u) == 0 || (x & 0x0000ff00u) == 0 ||
                          (x & 0x000000ffu) == 0);
                    return;
                }
                case 0xd: rn = (rn >> 16) | (rm << 16); return;  // XTRCT
                case 0xe: P->macl_ = (rn & 0xffff) * (rm & 0xffff); return;  // MULU.W
                case 0xf: P->macl_ = (uint32_t)((int32_t)((int16_t)(rn)) * (int32_t)((int16_t)(rm))); return;
                default: return;
            }
        case 0x3:
            switch (op & 0xf) {
                case 0x0: set_t(rn == rm); return;
                case 0x2: set_t(rn >= rm); return;
                case 0x3: set_t((int32_t)(rn) >= (int32_t)(rm)); return;
                case 0x4: {  // DIV1
                    const bool old_q = (P->sr_ & kQ) != 0;
                    const bool mq = (P->sr_ & kM) != 0;
                    bool q = (rn & 0x80000000u) != 0;
                    const uint32_t tmp2 = rm;
                    rn = (rn << 1) | (P->sr_ & kT);
                    const uint32_t tmp0 = rn;
                    bool tmp1;
                    if (old_q == mq) {
                        rn -= tmp2;
                        tmp1 = rn > tmp0;
                    } else {
                        rn += tmp2;
                        tmp1 = rn < tmp0;
                    }
                    if (!old_q) {
                        q = (!mq) ? (q ? !tmp1 : tmp1) : (q ? tmp1 : !tmp1);
                    } else {
                        q = (!mq) ? (q ? !tmp1 : tmp1) : (q ? tmp1 : !tmp1);
                    }
                    P->sr_ = q ? (P->sr_ | kQ) : (P->sr_ & ~kQ);
                    set_t(q == mq);
                    return;
                }
                case 0x5: {
                    const uint64_t p = (uint64_t)(rn) * (uint64_t)(rm);
                    P->mach_ = (uint32_t)(p >> 32);
                    P->macl_ = (uint32_t)(p);
                    P->cycles_ += 2;
                    return;
                }
                case 0x6: set_t(rn > rm); return;
                case 0x7: set_t((int32_t)(rn) > (int32_t)(rm)); return;
                case 0x8: rn -= rm; return;
                case 0xa: {  // SUBC
                    const uint32_t tmp1 = rn - rm;
                    const uint32_t tmp0 = rn;
                    rn = tmp1 - (P->sr_ & kT);
                    set_t(tmp0 < tmp1 || tmp1 < rn);
                    return;
                }
                case 0xb: {  // SUBV
                    const uint32_t res = rn - rm;
                    set_t((((rn ^ rm) & (rn ^ res)) >> 31) != 0);
                    rn = res;
                    return;
                }
                case 0xc: rn += rm; return;
                case 0xd: {
                    const int64_t p = (int64_t)((int32_t)(rn)) * (int64_t)((int32_t)(rm));
                    P->mach_ = (uint32_t)((uint64_t)(p) >> 32);
                    P->macl_ = (uint32_t)(p);
                    P->cycles_ += 2;
                    return;
                }
                case 0xe: {  // ADDC
                    const uint32_t tmp1 = rn + rm;
                    const uint32_t tmp0 = rn;
                    rn = tmp1 + (P->sr_ & kT);
                    set_t(tmp0 > tmp1 || tmp1 > rn);
                    return;
                }
                case 0xf: {  // ADDV
                    const uint32_t res = rn + rm;
                    set_t(((~(rn ^ rm) & (rn ^ res)) >> 31) != 0);
                    rn = res;
                    return;
                }
                default: return;
            }
        case 0x4:
            if ((op & 0xf) == 0xf) {  // MAC.W @Rm+,@Rn+
                const int32_t a = (int16_t)(read16(P, rn));
                rn += 2;
                const int32_t b = (int16_t)(read16(P, rm));
                rm += 2;
                const int64_t prod = (int64_t)(a) * b;
                if (P->sr_ & kS) {
                    int64_t res = (int64_t)((int32_t)(P->macl_)) + prod;
                    if (res > INT32_MAX) {
                        res = INT32_MAX;
                        P->mach_ |= 1;
                    } else if (res < INT32_MIN) {
                        res = INT32_MIN;
                        P->mach_ |= 1;
                    }
                    P->macl_ = (uint32_t)((int32_t)(res));
                } else {
                    const int64_t acc = (int64_t)(((uint64_t)(P->mach_) << 32) | P->macl_) + prod;
                    P->mach_ = (uint32_t)((uint64_t)(acc) >> 32);
                    P->macl_ = (uint32_t)(acc);
                }
                P->cycles_ += 2;
                return;
            }
            switch (op & 0xff) {
                case 0x00: set_t((rn >> 31) != 0); rn <<= 1; return;           // SHLL
                case 0x01: set_t((rn & 1) != 0); rn >>= 1; return;             // SHLR
                case 0x02: rn -= 4; write32(P, rn, P->mach_); P->cycles_++; return;     // STS.L MACH
                case 0x03: rn -= 4; write32(P, rn, P->sr_); P->cycles_++; return;       // STC.L SR
                case 0x04: set_t((rn >> 31) != 0); rn = (rn << 1) | (rn >> 31); return;  // ROTL
                case 0x05: set_t((rn & 1) != 0); rn = (rn >> 1) | (rn << 31); return;    // ROTR
                case 0x06: P->mach_ = read32(P, rn); rn += 4; P->cycles_++; return;
                case 0x07: P->sr_ = read32(P, rn) & kSrMask; rn += 4; P->cycles_ += 2; return;
                case 0x08: rn <<= 2; return;
                case 0x09: rn >>= 2; return;
                case 0x0a: P->mach_ = rn; return;
                case 0x0b: {  // JSR @Rn
                    const uint32_t target = rn;
                    P->pr_ = P->pc_ + 2;
                    P->cycles_++;
                    delay_slot(P, target);
                    return;
                }
                case 0x0e: P->sr_ = rn & kSrMask; return;
                case 0x10: rn--; set_t(rn == 0); return;  // DT
                case 0x11: set_t((int32_t)(rn) >= 0); return;
                case 0x12: rn -= 4; write32(P, rn, P->macl_); P->cycles_++; return;
                case 0x13: rn -= 4; write32(P, rn, P->gbr_); P->cycles_++; return;
                case 0x15: set_t((int32_t)(rn) > 0); return;
                case 0x16: P->macl_ = read32(P, rn); rn += 4; P->cycles_++; return;
                case 0x17: P->gbr_ = read32(P, rn); rn += 4; P->cycles_ += 2; return;
                case 0x18: rn <<= 8; return;
                case 0x19: rn >>= 8; return;
                case 0x1a: P->macl_ = rn; return;
                case 0x1b: {  // TAS.B
                    const uint8_t v = read8(P, rn);
                    set_t(v == 0);
                    write8(P, rn, (uint8_t)(v | 0x80));
                    P->cycles_ += 3;
                    return;
                }
                case 0x1e: P->gbr_ = rn; return;
                case 0x20: set_t((rn >> 31) != 0); rn <<= 1; return;  // SHAL
                case 0x21: set_t((rn & 1) != 0); rn = (uint32_t)((int32_t)(rn) >> 1); return;  // SHAR
                case 0x22: rn -= 4; write32(P, rn, P->pr_); P->cycles_++; return;
                case 0x23: rn -= 4; write32(P, rn, P->vbr_); P->cycles_++; return;
                case 0x24: {  // ROTCL
                    const bool c = (rn >> 31) != 0;
                    rn = (rn << 1) | (P->sr_ & kT);
                    set_t(c);
                    return;
                }
                case 0x25: {  // ROTCR
                    const bool c = (rn & 1) != 0;
                    rn = (rn >> 1) | ((P->sr_ & kT) << 31);
                    set_t(c);
                    return;
                }
                case 0x26: P->pr_ = read32(P, rn); rn += 4; P->cycles_++; return;
                case 0x27: P->vbr_ = read32(P, rn); rn += 4; P->cycles_ += 2; return;
                case 0x28: rn <<= 16; return;
                case 0x29: rn >>= 16; return;
                case 0x2a: P->pr_ = rn; return;
                case 0x2b: P->cycles_++; delay_slot(P, rn); return;  // JMP
                case 0x2e: P->vbr_ = rn; return;
                default: return;
            }
        case 0x5: rn = read32(P, rm + (uint32_t)(op & 0xf) * 4); P->cycles_++; return;
        case 0x6:
            switch (op & 0xf) {
                case 0x0: rn = sext8(read8(P, rm)); P->cycles_++; return;
                case 0x1: rn = sext16(read16(P, rm)); P->cycles_++; return;
                case 0x2: rn = read32(P, rm); P->cycles_++; return;
                case 0x3: rn = rm; return;
                case 0x4: {
                    const uint32_t v = sext8(read8(P, rm));
                    if (n != m) rm += 1;
                    rn = v;
                    P->cycles_++;
                    return;
                }
                case 0x5: {
                    const uint32_t v = sext16(read16(P, rm));
                    if (n != m) rm += 2;
                    rn = v;
                    P->cycles_++;
                    return;
                }
                case 0x6: {
                    const uint32_t v = read32(P, rm);
                    if (n != m) rm += 4;
                    rn = v;
                    P->cycles_++;
                    return;
                }
                case 0x7: rn = ~rm; return;
                case 0x8: rn = (rm & 0xffff0000u) | ((rm & 0xff) << 8) | ((rm >> 8) & 0xff); return;
                case 0x9: rn = (rm << 16) | (rm >> 16); return;
                case 0xa: {  // NEGC
                    const uint32_t tmp = 0u - rm;
                    rn = tmp - (P->sr_ & kT);
                    set_t(tmp != 0 || tmp < rn);
                    return;
                }
                case 0xb: rn = 0u - rm; return;
                case 0xc: rn = rm & 0xff; return;
                case 0xd: rn = rm & 0xffff; return;
                case 0xe: rn = sext8(rm); return;
                case 0xf: rn = sext16(rm); return;
                default: return;
            }
        case 0x7: rn += sext8(op & 0xff); return;
        case 0x8: {
            const uint32_t disp = op & 0xf;
            const uint32_t d8 = sext8(op & 0xff);
            switch (n) {
                case 0x0: write8(P, rm + disp, (uint8_t)(P->r_[0])); P->cycles_++; return;
                case 0x1: write16(P, rm + disp * 2, (uint16_t)(P->r_[0])); P->cycles_++; return;
                case 0x4: P->r_[0] = sext8(read8(P, rm + disp)); P->cycles_++; return;
                case 0x5: P->r_[0] = sext16(read16(P, rm + disp * 2)); P->cycles_++; return;
                case 0x8: set_t(P->r_[0] == d8); return;
                case 0x9:  // BT
                    if (t()) {
                        P->pc_ = P->pc_ + 2 + d8 * 2;
                        P->cycles_ += 2;
                    }
                    return;
                case 0xb:  // BF
                    if (!t()) {
                        P->pc_ = P->pc_ + 2 + d8 * 2;
                        P->cycles_ += 2;
                    }
                    return;
                case 0xd:  // BT/S
                    if (t()) {
                        P->cycles_++;
                        delay_slot(P, P->pc_ + 2 + d8 * 2);
                    }
                    return;
                case 0xf:  // BF/S
                    if (!t()) {
                        P->cycles_++;
                        delay_slot(P, P->pc_ + 2 + d8 * 2);
                    }
                    return;
                default: return;
            }
        }
        case 0x9: rn = sext16(read16(P, P->pc_ + 2 + (uint32_t)(op & 0xff) * 2)); P->cycles_++; return;
        case 0xa: P->cycles_++; delay_slot(P, P->pc_ + 2 + sext12(op & 0xfff) * 2); return;  // BRA
        case 0xb: {  // BSR
            P->pr_ = P->pc_ + 2;
            P->cycles_++;
            delay_slot(P, P->pc_ + 2 + sext12(op & 0xfff) * 2);
            return;
        }
        case 0xc: {
            const uint32_t imm = op & 0xff;
            switch (n) {
                case 0x0: write8(P, P->gbr_ + imm, (uint8_t)(P->r_[0])); P->cycles_++; return;
                case 0x1: write16(P, P->gbr_ + imm * 2, (uint16_t)(P->r_[0])); P->cycles_++; return;
                case 0x2: write32(P, P->gbr_ + imm * 4, P->r_[0]); P->cycles_++; return;
                case 0x3:  // TRAPA
                    exception(P, imm);
                    P->cycles_ += 7;
                    return;
                case 0x4: P->r_[0] = sext8(read8(P, P->gbr_ + imm)); P->cycles_++; return;
                case 0x5: P->r_[0] = sext16(read16(P, P->gbr_ + imm * 2)); P->cycles_++; return;
                case 0x6: P->r_[0] = read32(P, P->gbr_ + imm * 4); P->cycles_++; return;
                case 0x7: P->r_[0] = ((P->pc_ + 2) & ~3u) + imm * 4; return;  // MOVA
                case 0x8: set_t((P->r_[0] & imm) == 0); return;
                case 0x9: P->r_[0] &= imm; return;
                case 0xa: P->r_[0] ^= imm; return;
                case 0xb: P->r_[0] |= imm; return;
                case 0xc: set_t((read8(P, P->gbr_ + P->r_[0]) & imm) == 0); P->cycles_ += 2; return;
                case 0xd: {
                    const uint32_t a = P->gbr_ + P->r_[0];
                    write8(P, a, (uint8_t)(read8(P, a) & imm));
                    P->cycles_ += 2;
                    return;
                }
                case 0xe: {
                    const uint32_t a = P->gbr_ + P->r_[0];
                    write8(P, a, (uint8_t)(read8(P, a) ^ imm));
                    P->cycles_ += 2;
                    return;
                }
                case 0xf: {
                    const uint32_t a = P->gbr_ + P->r_[0];
                    write8(P, a, (uint8_t)(read8(P, a) | imm));
                    P->cycles_ += 2;
                    return;
                }
                default: return;
            }
        }
        case 0xd: rn = read32(P, ((P->pc_ + 2) & ~3u) + (uint32_t)(op & 0xff) * 4); P->cycles_++; return;
        case 0xe: rn = sext8(op & 0xff); return;
        default: return;
    }
}
#undef rn
#undef rm
#undef set_t
#undef t

