// psx_dma.h - the PlayStation's DMA controller (7 channels, DPCR / DICR):
// a C11 port of dsp-cpp's machine/psx_dma (from ProjectPSX, MIT License,
// Copyright (c) 2019 Pedro Cortés). Block, request and linked-list modes; the
// devices are reached through callbacks.
#ifndef PSX_DMA_H
#define PSX_DMA_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t base, block_size, block_count, direction;
    int32_t memory_step;
    uint32_t chopping, sync_mode, chop_dma_window, chop_cpu_window;
    bool enable, trigger;
    uint32_t unknown29, unknown30, pending_blocks;
} psx_dma_ch;

typedef struct {
    uint32_t (*ram_read)(void *user, uint32_t addr);
    void (*ram_write)(void *user, uint32_t addr, uint32_t value);
    void (*gpu_from_ram)(void *user, const uint32_t *data, int words);
    void (*gpu_to_ram)(void *user, uint32_t addr, int words);
    void (*cdrom_to_ram)(void *user, uint32_t addr, int words);
    void (*spu_from_ram)(void *user, const uint32_t *data, int words);
    void (*spu_to_ram)(void *user, uint32_t addr, int words);
    void (*mdec_from_ram)(void *user, const uint32_t *data, int words);
    void (*mdec_to_ram)(void *user, uint32_t addr, int words);
    void *user;
    psx_dma_ch ch[7];
    uint32_t dpcr_;
    bool force_irq_;
    uint32_t irq_enable_;
    bool master_enable_;
    uint32_t irq_flag_;
    bool master_flag_, edge_irq_;
} psx_dma;

void psx_dma_reset(psx_dma *P);
uint32_t psx_dma_load(psx_dma *P, uint32_t addr);
void psx_dma_write(psx_dma *P, uint32_t addr, uint32_t value);
bool psx_dma_tick(psx_dma *P);
#endif
