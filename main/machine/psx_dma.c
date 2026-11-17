// psx_dma.c - the PlayStation's DMA controller (see psx_dma.h).
#include "psx_dma.h"
#include <stdlib.h>
#include <string.h>

static void handle_channel(psx_dma *P, int channel);

void psx_dma_reset(psx_dma *P)
{
    memset(P->ch, 0, sizeof P->ch);
    for (int i = 0; i < 7; i++) P->ch[i].memory_step = 4;
    P->dpcr_ = 0x07654321u;
    P->force_irq_ = false;
    P->irq_enable_ = 0;
    P->master_enable_ = false;
    P->irq_flag_ = 0;
    P->master_flag_ = false;
    P->edge_irq_ = false;
}

static uint32_t load_control(const psx_dma_ch *c, int channel)
{
    uint32_t ctrl = 0;
    ctrl |= c->direction;
    ctrl |= (c->memory_step == 4 ? 0u : 1u) << 1;
    ctrl |= c->chopping << 8;
    ctrl |= c->sync_mode << 9;
    ctrl |= c->chop_dma_window << 16;
    ctrl |= c->chop_cpu_window << 20;
    ctrl |= (c->enable ? 1u : 0u) << 24;
    ctrl |= (c->trigger ? 1u : 0u) << 28;
    ctrl |= c->unknown29 << 29;
    ctrl |= c->unknown30 << 30;
    if (channel == 6) return (ctrl & 0x50000002u) | 0x2u;
    return ctrl;
}

static void write_control(psx_dma *P, int channel, uint32_t value)
{
    psx_dma_ch *c = &P->ch[channel];
    c->direction = value & 1u;
    c->memory_step = ((value >> 1) & 1u) == 0 ? 4 : -4;
    c->chopping = (value >> 8) & 1u;
    c->sync_mode = (value >> 9) & 3u;
    c->chop_dma_window = (value >> 16) & 7u;
    c->chop_cpu_window = (value >> 20) & 7u;
    c->enable = ((value >> 24) & 1u) != 0;
    c->trigger = ((value >> 28) & 1u) != 0;
    c->unknown29 = (value >> 29) & 1u;
    c->unknown30 = (value >> 30) & 1u;
    if (!c->enable) c->pending_blocks = 0;
    handle_channel(P, channel);
}

static bool update_master_flag(const psx_dma *P)
{
    return P->force_irq_ || (P->master_enable_ && (P->irq_enable_ & P->irq_flag_) != 0);
}

uint32_t psx_dma_load(psx_dma *P, uint32_t addr)
{
    const int channel = (int)((addr & 0x70) >> 4);
    const uint32_t reg = addr & 0xF;
    if (channel < 7) {
        const psx_dma_ch *c = &P->ch[channel];
        switch (reg) {
        case 0: return c->base;
        case 4: return (c->block_count << 16) | c->block_size;
        case 8: return load_control(c, channel);
        default: return 0;
        }
    }
    if (reg == 0) return P->dpcr_;
    if (reg == 4 || reg == 6) {
        uint32_t irq = 0;
        irq |= (P->force_irq_ ? 1u : 0u) << 15;
        irq |= P->irq_enable_ << 16;
        irq |= (P->master_enable_ ? 1u : 0u) << 23;
        irq |= P->irq_flag_ << 24;
        irq |= (P->master_flag_ ? 1u : 0u) << 31;
        return reg == 6 ? (irq >> 16) : irq;
    }
    return 0xFFFFFFFFu;
}

void psx_dma_write(psx_dma *P, uint32_t addr, uint32_t value)
{
    const int channel = (int)((addr & 0x70) >> 4);
    const uint32_t reg = addr & 0xF;
    if (channel < 7) {
        psx_dma_ch *c = &P->ch[channel];
        switch (reg) {
        case 0: c->base = value & 0xFFFFFFu; break;
        case 4:
            c->block_count = value >> 16;
            c->block_size = value & 0xFFFFu;
            break;
        case 8: write_control(P, channel, value); break;
        default: break;
        }
        return;
    }
    if (reg == 0) {
        P->dpcr_ = value;
    } else if (reg == 4 || reg == 6) {
        uint32_t v = value;
        if (reg == 6) v = (value << 16) | ((P->force_irq_ ? 1u : 0u) << 15);
        P->force_irq_ = ((v >> 15) & 1u) != 0;
        P->irq_enable_ = (v >> 16) & 0x7Fu;
        P->master_enable_ = ((v >> 23) & 1u) != 0;
        P->irq_flag_ &= ~((v >> 24) & 0x7Fu);
        P->master_flag_ = update_master_flag(P);
    }
}

static bool master_enabled(const psx_dma *P, int channel) { return ((P->dpcr_ >> 3 >> (4 * channel)) & 1u) != 0; }

static bool channel_active(const psx_dma *P, int channel)
{
    const psx_dma_ch *c = &P->ch[channel];
    return c->sync_mode == 0 ? (c->enable && c->trigger) : c->enable;
}

static void handle_interrupt(psx_dma *P, int channel)
{
    if ((P->irq_enable_ & (1u << channel)) != 0) P->irq_flag_ |= 1u << channel;
    P->master_flag_ = update_master_flag(P);
    P->edge_irq_ = P->edge_irq_ || P->master_flag_;
}

static void finish_channel(psx_dma *P, int channel)
{
    P->ch[channel].enable = false;
    P->ch[channel].trigger = false;
    handle_interrupt(P, channel);
}

static void block_copy(psx_dma *P, int channel, uint32_t size)
{
    psx_dma_ch *c = &P->ch[channel];
    if (c->direction == 0) {                     // to RAM
        switch (channel) {
        case 1: if (P->mdec_to_ram) P->mdec_to_ram(P->user, c->base, (int)size); break;
        case 2: if (P->gpu_to_ram) P->gpu_to_ram(P->user, c->base, (int)size); break;
        case 3: if (P->cdrom_to_ram) P->cdrom_to_ram(P->user, c->base, (int)size); break;
        case 4: if (P->spu_to_ram) P->spu_to_ram(P->user, c->base, (int)size); break;
        case 6: {                                // OTC: a linked list of decreasing addresses
            uint32_t addr = c->base;
            for (uint32_t i = 0; i + 1 < size; i++) {
                if (P->ram_write) P->ram_write(P->user, addr & 0x1FFFFCu, (addr - 4) & 0xFFFFFFu);
                addr -= 4;
            }
            if (P->ram_write) P->ram_write(P->user, addr & 0x1FFFFCu, 0x00FFFFFFu);
            break;
        }
        default: break;
        }
        c->base += (uint32_t)c->memory_step * size;
    } else {                                     // from RAM
        uint32_t *words = (uint32_t *)malloc((size ? size : 1) * sizeof(uint32_t));
        if (!words) return;
        uint32_t addr = c->base & 0x1FFFFCu;
        for (uint32_t i = 0; i < size; i++) {
            words[i] = P->ram_read ? P->ram_read(P->user, addr) : 0;
            addr = (addr + 4) & 0x1FFFFFu;
        }
        switch (channel) {
        case 0: if (P->mdec_from_ram) P->mdec_from_ram(P->user, words, (int)size); break;
        case 2: if (P->gpu_from_ram) P->gpu_from_ram(P->user, words, (int)size); break;
        case 4: if (P->spu_from_ram) P->spu_from_ram(P->user, words, (int)size); break;
        default: break;
        }
        free(words);
        c->base += (uint32_t)c->memory_step * size;
    }
}

static void linked_list(psx_dma *P, int channel)
{
    psx_dma_ch *g = &P->ch[channel];
    uint32_t header = 0, hard_stop = 0xFFFFu;
    uint32_t words[256];
    while ((header & 0x800000u) == 0 && hard_stop-- != 0) {
        header = P->ram_read ? P->ram_read(P->user, g->base & 0x1FFFFCu) : 0;
        const uint32_t size = header >> 24;
        if (size > 0) {
            g->base = (g->base + 4) & 0x1FFFFCu;
            uint32_t addr = g->base;
            for (uint32_t i = 0; i < size; i++) {
                words[i] = P->ram_read ? P->ram_read(P->user, addr) : 0;
                addr = (addr + 4) & 0x1FFFFFu;
            }
            if (P->gpu_from_ram) P->gpu_from_ram(P->user, words, (int)size);
        }
        if (g->base == (header & 0x1FFFFCu)) break;
        g->base = header & 0x1FFFFCu;
    }
}

static void handle_channel(psx_dma *P, int channel)
{
    if (!channel_active(P, channel) || !master_enabled(P, channel)) return;
    psx_dma_ch *c = &P->ch[channel];
    if (c->sync_mode == 0) {
        block_copy(P, channel, c->block_size == 0 ? 0x10000u : c->block_size);
        finish_channel(P, channel);
    } else if (c->sync_mode == 1) {
        if ((channel == 2 && c->direction == 1) || channel == 0) {   // GPU-in / MDEC-in at once
            block_copy(P, channel, c->block_size * c->block_count);
            finish_channel(P, channel);
            return;
        }
        c->trigger = false;
        c->pending_blocks = c->block_count;
    } else if (c->sync_mode == 2) {
        linked_list(P, channel);
        finish_channel(P, channel);
    }
}

bool psx_dma_tick(psx_dma *P)
{
    for (int i = 0; i < 7; i++) {
        psx_dma_ch *c = &P->ch[i];
        if (c->pending_blocks > 0) {
            c->pending_blocks--;
            block_copy(P, i, c->block_size);
            if (c->pending_blocks == 0) finish_channel(P, i);
        }
    }
    if (P->edge_irq_) {
        P->edge_irq_ = false;
        return true;
    }
    return false;
}
