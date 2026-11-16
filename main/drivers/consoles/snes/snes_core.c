#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "snes_m.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static uint32_t map_rom(snes_m *P, uint32_t addr);
static void account_access(snes_m *P, uint32_t addr);
static uint8_t cpu_read(snes_m *P, uint32_t addr);
static void cpu_write(snes_m *P, uint32_t addr, uint8_t value);
static uint8_t apu_read(snes_m *P, int port);
static void apu_write(snes_m *P, int port, uint8_t value);
static uint8_t aram_read(snes_m *P, uint16_t addr);
static void aram_write(snes_m *P, uint16_t addr, uint8_t value);
static void tick_apu_timers(snes_m *P, int cycles);
static void run_apu(snes_m *P, int main_cycles);
static int current_hdot(snes_m *P);
static uint8_t read_io(snes_m *P, uint16_t addr);
static bool vram_open_after(snes_m *P, int master_clocks);
static void write_io(snes_m *P, uint16_t addr, uint8_t value);
static void run_dma(snes_m *P, uint8_t channels);
static void run_hdma_init(snes_m *P);
static void run_hdma_line(snes_m *P);
static void raise_timer_irq(snes_m *P);
static void run_cpu_line(snes_m *P);
void snes_reset(snes_m *P) {
    FILL(P->wram_, 0);
    snes_ppu_reset(&P->ppu_);
    memset(P->dma_, 0, sizeof P->dma_);
    P->nmitimen_ = 0;
    P->rdnmi_ = 0;
    P->hdmaen_ = 0;
    P->memsel_ = 0;
    P->wrio_ = 0xff;
    P->htime_ = P->vtime_ = 0x1ff;
    P->in_vblank_ = false;
    P->irq_pending_ = false;
    P->nmi_pending_ = false;
    P->line_ = 0;
    P->line_cycle_ = 0;
    P->wrmpya_ = 0xff;
    P->wrdiv_ = 0xffff;
    P->rddiv_ = P->rdmpy_ = 0;
    P->ophct_ = P->opvct_ = 0;
    P->ophct_high_ = P->opvct_high_ = false;
    P->counter_latched_ = false;
    P->wram_addr_ = 0;
    P->extra_clocks_ = 0;
    P->in_dma_ = false;
    P->pad1_ = P->pad1_shift_ = 0;
    P->joy_latch_ = 0;
    FILL(P->aram_, 0);
    memset(P->apu_out_, 0, sizeof P->apu_out_);
    memset(P->apu_in_, 0, sizeof P->apu_in_);
    P->apu_control_ = 0;
    memset(P->timer_target_, 0, sizeof P->timer_target_);
    memset(P->timer_stage_, 0, sizeof P->timer_stage_);
    memset(P->timer_out_, 0, sizeof P->timer_out_);
    memset(P->timer_div_, 0, sizeof P->timer_div_);
    FILL(P->dsp_, 0);
    P->dsp_addr_ = 0;
    P->apu_cycles_ = 0;
    P->ipl_visible_ = true;
    spc700_reset(&P->apu_);
    if (P->framebuffer_) for (int i_ = 0; i_ < kWidth * kHeight; i_++) P->framebuffer_[i_] = 0xff000000u;
    P->open_bus_ = 0;
    w65c816_reset(&P->cpu_);
}
static uint32_t map_rom(snes_m *P, uint32_t addr) {
    const uint8_t bank = (uint8_t)(addr >> 16);
    const uint16_t off = (uint16_t)(addr);
    if ((P->rom_size_ == 0)) return 0xffffffffu;
    if (P->hirom_) {
        // HiROM: banks $C0-$FF map linearly; $00-$3F mirror the upper half.
        const uint32_t a = ((bank & 0x3f) << 16) | off;
        return a % P->rom_size_;
    }
    // LoROM: the top 32 KB of every bank, packed back to back.
    const uint32_t a = ((uint32_t)(bank & 0x7f) << 15) | (off & 0x7fff);
    return a % P->rom_size_;
}
static void account_access(snes_m *P, uint32_t addr) {
    if (P->in_dma_) return;
    const uint8_t bank = (uint8_t)(addr >> 16);
    const uint16_t off = (uint16_t)(addr);
    const bool fast_rom = (bank & 0x80) && (P->memsel_ & 1);
    int clocks;
    if (!(bank & 0x40)) {
        if (off < 0x2000) clocks = 8;
        else if (off < 0x4000) clocks = 6;
        else if (off < 0x4200) clocks = 12;
        else if (off < 0x6000) clocks = 6;
        else if (off < 0x8000) clocks = 8;
        else clocks = fast_rom ? 6 : 8;
    } else {
        clocks = (bank == 0x7e || bank == 0x7f) ? 8 : (fast_rom ? 6 : 8);
    }
    P->extra_clocks_ += clocks - 6;
}
static uint8_t cpu_read(snes_m *P, uint32_t addr) {
    account_access(P, addr);
    const uint8_t bank = (uint8_t)(addr >> 16);
    const uint16_t off = (uint16_t)(addr);

    if (bank == 0x7e || bank == 0x7f) {
        if (((off & 0xfffe) == 0x7000)) return 0x80;
        return P->wram_[((bank - 0x7e) << 16) | off];
    }
    if ((bank <= 0x3f) || (bank >= 0x80 && bank <= 0xbf)) {
        if (off < 0x2000) {
            if (((off & 0xfffe) == 0x7000)) return 0x80;
            return P->wram_[off];
        }
        if (off < 0x6000) return read_io(P, off);
        if (off < 0x8000) {
            if (((off & 0xfffe) == 0x7000)) return 0x80;
            if ((P->sram_size_ != 0)) {
                const uint32_t s = ((uint32_t)(bank & 0x0f) << 13) | (off & 0x1fff);
                return P->sram_[s % P->sram_size_];
            }
            return P->open_bus_;
        }
        const uint32_t a = map_rom(P, addr);
        return a < P->rom_size_ ? P->rom_[a] : P->open_bus_;
    }
    if ((P->sram_size_ != 0) && bank >= 0x70 && bank <= 0x77) {
        if (((off & 0xfffe) == 0x7000)) return 0x80;
        const uint32_t s = ((uint32_t)(bank - 0x70) << 15) | off;
        return P->sram_[s % P->sram_size_];
    }
    const uint32_t a = map_rom(P, addr);
    return a < P->rom_size_ ? P->rom_[a] : P->open_bus_;
}
static void cpu_write(snes_m *P, uint32_t addr, uint8_t value) {
    account_access(P, addr);
    const uint8_t bank = (uint8_t)(addr >> 16);
    const uint16_t off = (uint16_t)(addr);
    P->open_bus_ = value;

    if (bank == 0x7e || bank == 0x7f) {
        P->wram_[((bank - 0x7e) << 16) | off] = value;
        return;
    }
    if ((bank <= 0x3f) || (bank >= 0x80 && bank <= 0xbf)) {
        if (off < 0x2000) { P->wram_[off] = value; return; }
        if (off < 0x6000) { write_io(P, off, value); return; }
        if (off < 0x8000 && (P->sram_size_ != 0)) {
            const uint32_t s = ((uint32_t)(bank & 0x0f) << 13) | (off & 0x1fff);
            P->sram_[s % P->sram_size_] = value;
            return;
        }
    }
    if ((P->sram_size_ != 0) && bank >= 0x70 && bank <= 0x77) {
        const uint32_t s = ((uint32_t)(bank - 0x70) << 15) | off;
        P->sram_[s % P->sram_size_] = value;
        return;
    }
}
static uint8_t apu_read(snes_m *P, int port) { return P->apu_out_[(size_t)(port & 3)]; }
static void apu_write(snes_m *P, int port, uint8_t value) { P->apu_in_[(size_t)(port & 3)] = value; }
static uint8_t aram_read(snes_m *P, uint16_t addr) {
    if (addr >= 0xffc0 && P->ipl_visible_) return P->ipl_[addr - 0xffc0];
    if (addr >= 0x00f0 && addr <= 0x00ff) {
        switch (addr) {
            case 0xf2: return P->dsp_addr_;
            case 0xf3: return P->dsp_[P->dsp_addr_ & 0x7f];
            case 0xf4: case 0xf5: case 0xf6: case 0xf7:
                return P->apu_in_[addr - 0xf4];
            case 0xfd: case 0xfe: case 0xff: {
                // Reading a counter returns it and clears it.
                const size_t t = addr - 0xfd;
                const uint8_t v = P->timer_out_[t];
                P->timer_out_[t] = 0;
                return v;
            }
            default: return P->aram_[addr];
        }
    }
    return P->aram_[addr];
}
static void aram_write(snes_m *P, uint16_t addr, uint8_t value) {
    if (addr >= 0x00f0 && addr <= 0x00ff) {
        switch (addr) {
            case 0xf1:
                P->apu_control_ = value;
                // Bits 4 and 5 clear the mailbox in each direction; bit 7
                // keeps the boot ROM mapped.
                if (value & 0x10) { P->apu_in_[0] = P->apu_in_[1] = 0; }
                if (value & 0x20) { P->apu_in_[2] = P->apu_in_[3] = 0; }
                P->ipl_visible_ = (value & 0x80) != 0;
                for (int t = 0; t < 3; t++)
                    if (value & (1 << t)) { P->timer_stage_[(size_t)(t)] = 0; P->timer_out_[(size_t)(t)] = 0; }
                return;
            case 0xf2: P->dsp_addr_ = value; return;
            case 0xf3: P->dsp_[P->dsp_addr_ & 0x7f] = value; return;
            case 0xf4: case 0xf5: case 0xf6: case 0xf7:
                P->apu_out_[addr - 0xf4] = value; return;
            case 0xfa: case 0xfb: case 0xfc:
                P->timer_target_[addr - 0xfa] = value; return;
            default: break;
        }
    }
    P->aram_[addr] = value;
}
static void tick_apu_timers(snes_m *P, int cycles) {
    // Timers 0 and 1 tick at 8 kHz, timer 2 at 64 kHz, derived from the
    // sound CPU's 1.024 MHz clock.
    static const int kDivider[3] = {128, 128, 16};
    for (int t = 0; t < 3; t++) {
        if (!(P->apu_control_ & (1 << t))) continue;
        P->timer_div_[(size_t)(t)] += cycles;
        while (P->timer_div_[(size_t)(t)] >= kDivider[t]) {
            P->timer_div_[(size_t)(t)] -= kDivider[t];
            const uint8_t target = P->timer_target_[(size_t)(t)];
            if (++P->timer_stage_[(size_t)(t)] == (target ? target : 0)) {
                P->timer_stage_[(size_t)(t)] = 0;
                P->timer_out_[(size_t)(t)] = (uint8_t)((P->timer_out_[(size_t)(t)] + 1) & 0x0f);
            }
        }
    }
}
static void run_apu(snes_m *P, int main_cycles) {
    // The sound CPU runs at 1.024 MHz against the main CPU's 3.58 MHz.
    P->apu_cycles_ += main_cycles * 1024;
    while (P->apu_cycles_ >= 3580) {
        const int used = spc700_step(&P->apu_);
        P->apu_cycles_ -= used * 3580;
        tick_apu_timers(P, used);
    }
}
static int current_hdot(snes_m *P) {
    // 1364 master clocks per line, 4 per dot; the CPU cycle count assumes
    // 6 master clocks per cycle.
    return MIN(339, P->line_cycle_ * 6 / 4);
}
static uint8_t read_io(snes_m *P, uint16_t addr) {
    switch (addr) {
        case 0x2137:   // SLHV: latch the H/V counters
            P->ophct_ = (uint16_t)(current_hdot(P));
            P->opvct_ = (uint16_t)(P->line_);
            P->counter_latched_ = true;
            return P->open_bus_;
        case 0x213c: {
            const uint8_t v = P->ophct_high_ ? (uint8_t)((P->ophct_ >> 8) & 1) : (uint8_t)(P->ophct_);
            P->ophct_high_ = !P->ophct_high_;
            return v;
        }
        case 0x213d: {
            const uint8_t v = P->opvct_high_ ? (uint8_t)((P->opvct_ >> 8) & 1) : (uint8_t)(P->opvct_);
            P->opvct_high_ = !P->opvct_high_;
            return v;
        }
        case 0x213f: {   // STAT78: reading resets the counter flip-flops
            const uint8_t v = (uint8_t)((snes_ppu_read(&P->ppu_, addr) & 0xbf) | (P->counter_latched_ ? 0x40 : 0));
            P->ophct_high_ = P->opvct_high_ = false;
            P->counter_latched_ = false;
            return v;
        }
        default: break;
    }
    if (addr >= 0x2100 && addr <= 0x213f) return snes_ppu_read(&P->ppu_, addr);
    if (addr >= 0x2140 && addr <= 0x217f) return apu_read(P, addr & 3);
    switch (addr) {
        case 0x2180: {
            const uint8_t v = P->wram_[P->wram_addr_ & 0x1ffff];
            P->wram_addr_ = (P->wram_addr_ + 1) & 0x1ffff;
            return v;
        }
        case 0x4210: {   // RDNMI: reading the flag clears it
            const uint8_t v = (uint8_t)(P->rdnmi_ | 0x02);
            P->rdnmi_ &= (uint8_t)(~0x80);
            return v;
        }
        case 0x4211: {   // TIMEUP: reading acknowledges the timer interrupt
            const uint8_t v = (uint8_t)(P->irq_pending_ ? 0x80 : 0);
            P->irq_pending_ = false;
            w65c816_set_irq(&P->cpu_, IRQ_CLEAR);
            return v;
        }
        case 0x4212: {   // HVBJOY: vblank, hblank and the auto-joypad busy bit
            const int dot = current_hdot(P);
            const bool hblank = dot < 22 || dot >= 274;
            const bool joy_busy = (P->nmitimen_ & 1) && P->line_ >= 225 && P->line_ <= 227;
            return (uint8_t)((P->in_vblank_ ? 0x80 : 0) | (hblank ? 0x40 : 0) | (joy_busy ? 0x01 : 0));
        }
        case 0x4214: return (uint8_t)(P->rddiv_);
        case 0x4215: return (uint8_t)(P->rddiv_ >> 8);
        case 0x4216: return (uint8_t)(P->rdmpy_);
        case 0x4217: return (uint8_t)(P->rdmpy_ >> 8);
        case 0x4213: return P->wrio_;                  // RDIO echoes back WRIO
        case 0x4218: return (uint8_t)(P->pad1_);         // JOY1L
        case 0x4219: return (uint8_t)(P->pad1_ >> 8);    // JOY1H
        case 0x421a: case 0x421b: case 0x421c:
        case 0x421d: case 0x421e: case 0x421f: return 0;
        case 0x4016: case 0x4017: {
            const uint8_t bit = (uint8_t)(P->pad1_shift_ & 1);
            P->pad1_shift_ >>= 1;
            return bit;
        }
        default:
            if (addr >= 0x4300 && addr <= 0x437f) {
                snes_dma *c = &P->dma_[(addr >> 4) & 7];
                switch (addr & 0x0f) {
                    case 0x0: return c->control;
                    case 0x1: return c->dest;
                    case 0x2: return (uint8_t)(c->src);
                    case 0x3: return (uint8_t)(c->src >> 8);
                    case 0x4: return (uint8_t)(c->src >> 16);
                    case 0x5: return (uint8_t)(c->count);
                    case 0x6: return (uint8_t)(c->count >> 8);
                    default: return P->open_bus_;
                }
            }
            return P->open_bus_;
    }
}
static bool vram_open_after(snes_m *P, int master_clocks) {
    // Where the beam will be `master_clocks` from now (1364 per line).
    const int clocks = P->line_cycle_ * 6 + P->extra_clocks_ + master_clocks;
    const int line = (P->line_ + clocks / 1364) % kLinesTotal;
    return line > kVisibleLines;
}
static void write_io(snes_m *P, uint16_t addr, uint8_t value) {
    if (addr == 0x2118 || addr == 0x2119) snes_ppu_set_vram_open(&P->ppu_, P->in_dma_ ? P->dma_vram_open_ : P->in_vblank_);
    if (addr >= 0x2100 && addr <= 0x213f) { snes_ppu_write(&P->ppu_, addr, value); return; }
    if (addr >= 0x2140 && addr <= 0x217f) { apu_write(P, addr & 3, value); return; }
    switch (addr) {
        case 0x2180:
            P->wram_[P->wram_addr_ & 0x1ffff] = value;
            P->wram_addr_ = (P->wram_addr_ + 1) & 0x1ffff;
            return;
        case 0x2181: P->wram_addr_ = (P->wram_addr_ & 0x1ff00) | value; return;
        case 0x2182: P->wram_addr_ = (P->wram_addr_ & 0x100ff) | ((uint32_t)(value) << 8); return;
        case 0x2183: P->wram_addr_ = (P->wram_addr_ & 0x0ffff) | ((uint32_t)(value & 1) << 16); return;
        case 0x4016:
            // Writing bit 0 latches the pad; reads then shift it out.
            if ((P->joy_latch_ & 1) && !(value & 1)) P->pad1_shift_ = P->pad1_;
            P->joy_latch_ = value;
            return;
        case 0x4200:
            // Disabling both timer sources also drops a pending timer IRQ.
            if ((value & 0x30) == 0) {
                P->irq_pending_ = false;
                w65c816_set_irq(&P->cpu_, IRQ_CLEAR);
            }
            // Enabling NMI during vblank with the flag still set fires it.
            if (!(P->nmitimen_ & 0x80) && (value & 0x80) && (P->rdnmi_ & 0x80)) w65c816_set_nmi(&P->cpu_, IRQ_PULSE);
            P->nmitimen_ = value;
            return;
        case 0x4201:
            // A 1->0 transition on bit 7 latches the H/V counters.
            if ((P->wrio_ & 0x80) && !(value & 0x80)) {
                P->ophct_ = (uint16_t)(current_hdot(P));
                P->opvct_ = (uint16_t)(P->line_);
                P->counter_latched_ = true;
            }
            P->wrio_ = value;
            return;
        case 0x4202: P->wrmpya_ = value; return;
        case 0x4203:
            P->rdmpy_ = (uint16_t)(P->wrmpya_ * value);
            P->rddiv_ = value;   // the multiplier also lands in RDDIV
            return;
        case 0x4204: P->wrdiv_ = (uint16_t)((P->wrdiv_ & 0xff00) | value); return;
        case 0x4205: P->wrdiv_ = (uint16_t)((P->wrdiv_ & 0x00ff) | (value << 8)); return;
        case 0x4206:
            if (value == 0) {
                P->rddiv_ = 0xffff;
                P->rdmpy_ = P->wrdiv_;
            } else {
                P->rddiv_ = (uint16_t)(P->wrdiv_ / value);
                P->rdmpy_ = (uint16_t)(P->wrdiv_ % value);
            }
            return;
        case 0x4207: P->htime_ = (uint16_t)((P->htime_ & 0x100) | value); return;
        case 0x4208: P->htime_ = (uint16_t)((P->htime_ & 0x0ff) | ((value & 1) << 8)); return;
        case 0x4209: P->vtime_ = (uint16_t)((P->vtime_ & 0x100) | value); return;
        case 0x420a: P->vtime_ = (uint16_t)((P->vtime_ & 0x0ff) | ((value & 1) << 8)); return;
        case 0x420b: run_dma(P, value); return;
        case 0x420c:
            // Turning a channel on part-way through a frame starts it on the
            // next line rather than waiting for the next frame.
            if (value != P->hdmaen_) {
                P->hdmaen_ = value;
                run_hdma_init(P);
                return;
            }
            P->hdmaen_ = value;
            return;
        case 0x420d: P->memsel_ = value; return;
        default:
            if (addr >= 0x4300 && addr <= 0x437f) {
                snes_dma *c = &P->dma_[(addr >> 4) & 7];
                switch (addr & 0x0f) {
                    case 0x0: c->control = value; return;
                    case 0x1: c->dest = value; return;
                    case 0x2: c->src = (c->src & 0xffff00) | value; return;
                    case 0x3: c->src = (c->src & 0xff00ff) | ((uint32_t)(value) << 8); return;
                    case 0x4: c->src = (c->src & 0x00ffff) | ((uint32_t)(value) << 16); return;
                    case 0x5: c->count = (uint16_t)((c->count & 0xff00) | value); return;
                    case 0x6: c->count = (uint16_t)((c->count & 0x00ff) | (value << 8)); return;
                    case 0x8: c->table = (uint16_t)((c->table & 0xff00) | value); return;
                    case 0x9: c->table = (uint16_t)((c->table & 0x00ff) | (value << 8)); return;
                    case 0x7: c->indirect_bank = value; return;
                    case 0xa: c->line_count = value; return;
                    default: return;
                }
            }
            return;
    }
}
static void run_dma(snes_m *P, uint8_t channels) {
    // The transfer patterns say how many $21xx registers a unit covers and in
    // what order, which is what lets one channel fill VRAM through the pair
    // of data ports.
    static const uint8_t kPattern[8][4] = {
        {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
        {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    };
    static const int kPatternLen[8] = {1, 2, 2, 4, 4, 4, 2, 4};

    // DMA halts the CPU: 8 master clocks a byte plus 8 per channel.
    P->in_dma_ = true;
    for (int ch = 0; ch < 8; ch++) {
        if (!(channels & (1 << ch))) continue;
        snes_dma *c = &P->dma_[(size_t)(ch)];
        const int mode = c->control & 7;
        const bool to_cpu = (c->control & 0x80) != 0;
        const int bytes = (int)(c->count ? c->count : 0x10000);
        const int step = (c->control & 0x08) ? 0 : ((c->control & 0x10) ? -1 : 1);
        uint32_t count = c->count ? c->count : 0x10000;
        int unit = 0;
        int done = 0;
        while (count--) {
            const uint16_t reg = (uint16_t)(0x2100 + c->dest + kPattern[mode][unit]);
            // Long transfers run on into the picture, where VRAM drops writes.
            P->dma_vram_open_ = vram_open_after(P, 8 * done++);
            if (to_cpu) {
                cpu_write(P, c->src, read_io(P, reg));
            } else {
                write_io(P, reg, cpu_read(P, c->src));
            }
            c->src = (c->src & 0xff0000) | (uint16_t)((uint16_t)(c->src) + step);
            unit = (unit + 1) % kPatternLen[mode];
        }
        P->extra_clocks_ += 8 + 8 * bytes;
        c->count = 0;
    }
    P->in_dma_ = false;
}
static void run_hdma_init(snes_m *P) {
    for (int ch = 0; ch < 8; ch++) {
        if (!(P->hdmaen_ & (1 << ch))) continue;
        snes_dma *c = &P->dma_[(size_t)(ch)];
        c->table = (uint16_t)(c->src);
        c->line_count = 0;
        c->done = false;
        c->pending = false;
    }
}
static void run_hdma_line(snes_m *P) {
    if (P->hdmaen_) P->extra_clocks_ += 18;
    P->in_dma_ = true;
    static const uint8_t kPattern[8][4] = {
        {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
        {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    };
    static const int kPatternLen[8] = {1, 2, 2, 4, 4, 4, 2, 4};

    for (int ch = 0; ch < 8; ch++) {
        if (!(P->hdmaen_ & (1 << ch))) continue;
        snes_dma *c = &P->dma_[(size_t)(ch)];
        if (c->done) continue;
        const uint32_t bank = c->src & 0xff0000;
        const int mode = c->control & 7;
        const bool indirect = (c->control & 0x40) != 0;

        if (c->line_count == 0) {
            // A header of zero ends the channel for this frame; otherwise the
            // low seven bits are a repeat count and bit 7 says whether the
            // transfer happens on every line or only the first.
            const uint8_t hdr = cpu_read(P, bank | c->table++);
            if (hdr == 0) { c->done = true; continue; }
            c->repeat = (hdr & 0x80) != 0;
            c->line_count = (uint8_t)(hdr & 0x7f);
            if (indirect) {
                const uint8_t lo = cpu_read(P, bank | c->table++);
                const uint8_t hi = cpu_read(P, bank | c->table++);
                c->indirect = (uint16_t)(lo | (hi << 8));
            }
            c->pending = true;   // always transfer on the first line of a block
        }

        if (c->pending || c->repeat) {
            const uint32_t addr = indirect ? (((uint32_t)(c->indirect_bank) << 16) | c->indirect)
                                           : (bank | c->table);
            for (int i = 0; i < kPatternLen[mode]; i++) {
                write_io(P, (uint16_t)(0x2100 + c->dest + kPattern[mode][i]),
                         cpu_read(P, addr + (uint32_t)(i)));
            }
            P->extra_clocks_ += 8 + 8 * kPatternLen[mode];
            if (indirect) c->indirect = (uint16_t)(c->indirect + kPatternLen[mode]);
            else c->table = (uint16_t)(c->table + kPatternLen[mode]);
        }
        c->pending = false;
        c->line_count--;
    }
    P->in_dma_ = false;
}
static void raise_timer_irq(snes_m *P) {
    P->irq_pending_ = true;
    w65c816_set_irq(&P->cpu_, IRQ_ASSERT);   // held until $4211 is read
}
static void run_cpu_line(snes_m *P) {
    // H/V timer IRQ: $4200 bits 4-5 select H (every line at HTIME), V (at
    // VTIME, dot 0) or both. The line is split at the trigger point.
    const int mode = (P->nmitimen_ >> 4) & 3;
    int trigger = -1;
    if (mode == 1) trigger = P->htime_ <= 339 ? P->htime_ : -1;
    else if (mode == 2) trigger = P->line_ == P->vtime_ ? 0 : -1;
    else if (mode == 3) trigger = (P->line_ == P->vtime_ && P->htime_ <= 339) ? P->htime_ : -1;
    const int trigger_cycle = trigger < 0 ? -1 : trigger * 4 / 6;

    // P->line_cycle_ carries the previous line's overshoot.
    bool fired = false;
    while (P->line_cycle_ < kCyclesPerLine) {
        if (!fired && trigger_cycle >= 0 && P->line_cycle_ >= trigger_cycle) {
            raise_timer_irq(P);
            fired = true;
        }
        int slice = kCyclesPerLine - P->line_cycle_;
        if (!fired && trigger_cycle > P->line_cycle_) slice = MIN(slice, trigger_cycle - P->line_cycle_);
        int ran = w65c816_run(&P->cpu_, MIN(slice, 32));
        if (ran <= 0) break;
        ran += P->extra_clocks_ / 6;
        P->extra_clocks_ %= 6;
        // A long DMA can stall the CPU across whole frames; the stall is
        // paid off line by line rather than skipping NMIs.
        P->line_cycle_ += ran;
        run_apu(P, ran);
    }
    P->line_cycle_ -= kCyclesPerLine;
    if (P->line_cycle_ < 0) P->line_cycle_ = 0;
}
void snes_run_frame(snes_m *P) {
    // V=0 is not displayed; lines 1-224 are the picture (rows 0-223) and
    // vertical blank starts at V=225. HDMA is set up at the top of the frame
    // and transfers during the horizontal blank of lines 0-224, so what it
    // writes at the end of one line shows on the next.
    for (P->line_ = 0; P->line_ < kLinesTotal; P->line_++) {
        if (P->line_ == 0) {
            P->in_vblank_ = false;
            P->rdnmi_ &= (uint8_t)(~0x80);
            run_hdma_init(P);
        }
        if (P->line_ == kVisibleLines + 1) {
            P->in_vblank_ = true;
            snes_ppu_start_vblank(&P->ppu_);
            P->rdnmi_ |= 0x80;
            if (P->nmitimen_ & 0x80) w65c816_set_nmi(&P->cpu_, IRQ_PULSE);
            if (P->nmitimen_ & 0x01) P->pad1_shift_ = P->pad1_;   // auto joypad read
        }
        if (P->line_ >= 1 && P->line_ <= kVisibleLines) {
            snes_ppu_render_line(&P->ppu_, P->line_, &P->framebuffer_[(size_t)(P->line_ - 1) * kWidth]);
        }
        run_cpu_line(P);
        if (P->line_ <= kVisibleLines) run_hdma_line(P);
    }
}

// ---- the console API ----
#include <stdlib.h>
#include "../common/cons.h"

static const char *const kExts[] = { ".sfc", ".smc", ".swc", ".fig", NULL };
const cons_info cons_machine = {
    "snes", "Super Nintendo", "Nintendo, 1990", kExts, "SFC SMC", kWidth, kHeight,
    kFps, kSampleRate, { "B", "Y", "A", "X" }, "START", "SELECT", CONS_NO_KEYS, 1, NULL,
};

static snes_m S;
static uint16_t fb565[kWidth * kHeight];
static int16_t silence[kSampleRate / kFps + 1];

static uint8_t bus_read(void *u, uint32_t a) { return cpu_read((snes_m *)u, a); }
static void bus_write(void *u, uint32_t a, uint8_t v) { cpu_write((snes_m *)u, a, v); }
static uint8_t apu_bus_read(void *u, uint16_t a) { return aram_read((snes_m *)u, a); }
static void apu_bus_write(void *u, uint16_t a, uint8_t v) { aram_write((snes_m *)u, a, v); }

// a candidate header: a printable title and a checksum that agrees with its complement
static int score_header(const uint8_t *rom, size_t size, size_t base)
{
    if (base + 32 > size) return -1;
    int score = 0;
    for (size_t i = 0; i < 21; i++)
        if (rom[base + i] >= 0x20 && rom[base + i] < 0x7f) score++;
    const uint16_t chk = (uint16_t)(rom[base + 28] | (rom[base + 29] << 8));
    const uint16_t inv = (uint16_t)(rom[base + 30] | (rom[base + 31] << 8));
    if ((chk ^ inv) == 0xffff) score += 16;
    return score;
}

bool cons_init(void)
{
    static const uint8_t kDefaultIpl[64] = {
        0xcd,0xef,0xbd,0xe8,0x00,0xc6,0x1d,0xd0,0xfc,0x8f,0xaa,0xf4,0x8f,0xbb,0xf5,0x78,
        0xcc,0xf4,0xd0,0xfb,0x2f,0x19,0xeb,0xf4,0xd0,0xfc,0x7e,0xf4,0xd0,0x0b,0xe4,0xf5,
        0xcb,0xf4,0xd7,0x00,0xfc,0xd0,0xf3,0xab,0x01,0x10,0xef,0x7e,0xf4,0x10,0xeb,0xba,
        0xf6,0xda,0x00,0xba,0xf4,0xc4,0xf4,0xdd,0x5d,0xd0,0xdb,0x1f,0x00,0x00,0xc0,0xff
    };
    S.framebuffer_ = (uint32_t *)malloc(kWidth * kHeight * sizeof(uint32_t));
    if (!S.framebuffer_) return false;
    memcpy(S.ipl_, kDefaultIpl, sizeof S.ipl_);
    w65c816_init(&S.cpu_, kCpuClock);
    S.cpu_.read = bus_read;
    S.cpu_.write = bus_write;
    S.cpu_.user = &S;
    S.apu_.read_ = apu_bus_read;
    S.apu_.write_ = apu_bus_write;
    S.apu_.user_ = &S;
    S.apu_.sp = 0xef;
    S.apu_.pc_ = 0xffc0;
    snes_ppu_init(&S.ppu_);
    S.dma_vram_open_ = true;
    snes_reset(&S);
    return true;
}

void cons_reset(void) { snes_reset(&S); }

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (data && size % 0x8000 == 512) {        // a copier header
        memmove(data, data + 512, size - 512);
        size -= 512;
    }
    if (!data || size < 0x8000) {
        if (why) *why = "cartridge too small";
        free(data);
        return false;
    }
    const bool hirom = score_header(data, size, 0xffc0) > score_header(data, size, 0x7fc0);
    const size_t hdr = hirom ? 0xffc0u : 0x7fc0u;
    size_t sram_bytes = 0;
    if (hdr + 24 < size) {
        const uint8_t sh = data[hdr + 24];
        if (sh > 0 && sh < 16) sram_bytes = (size_t)1 << (sh + 10);
    }
    if (hdr + 22 < size && (data[hdr + 22] & 0x0f) >= 2 && sram_bytes == 0) sram_bytes = 2048;
    if (sram_bytes < 0x800) sram_bytes = 0x800;
    uint8_t *sram = (uint8_t *)calloc(sram_bytes, 1);
    if (!sram) {
        if (why) *why = "out of memory";
        free(data);
        return false;
    }
    free(S.rom_);
    free(S.sram_);
    S.rom_ = data;
    S.rom_size_ = size;
    S.hirom_ = hirom;
    S.sram_ = sram;
    S.sram_size_ = sram_bytes;
    snes_reset(&S);
    return true;
}

void cons_eject(void)
{
    free(S.rom_);
    free(S.sram_);
    S.rom_ = S.sram_ = NULL;
    S.rom_size_ = S.sram_size_ = 0;
    snes_reset(&S);
}

bool cons_loaded(void) { return S.rom_size_ != 0; }

int cons_run_frame(const int16_t **audio)
{
    *audio = silence;                           // (dsp-cpp's SNES makes no sound yet)
    if (S.rom_size_) snes_run_frame(&S);
    for (int i = 0; i < kWidth * kHeight; i++) fb565[i] = cons_rgb565(S.framebuffer_[i]);
    return kSampleRate / kFps;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = kWidth;
    *h = kHeight;
    *stride = kWidth;
    return fb565;
}

void cons_pad(int player, unsigned b)
{
    if (player) return;
    // the standard pad, MSB first: B Y Select Start Up Down Left Right A X L R
    uint16_t v = 0;
    if (b & CP_B1) v |= 0x8000;
    if (b & CP_B2) v |= 0x4000;
    if (b & CP_SELECT) v |= 0x2000;
    if (b & CP_START) v |= 0x1000;
    if (b & CP_UP) v |= 0x0800;
    if (b & CP_DOWN) v |= 0x0400;
    if (b & CP_LEFT) v |= 0x0200;
    if (b & CP_RIGHT) v |= 0x0100;
    if (b & CP_B3) v |= 0x0080;
    if (b & CP_B4) v |= 0x0040;
    S.pad1_ = v;
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return S.hirom_ ? "HiROM" : "LoROM"; }
