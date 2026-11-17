// psx_m.h - the Sony PlayStation machine (psx_core.c): a C11 migration of
// dsp-cpp's drivers/consoles/psx, its devices after ProjectPSX (MIT License,
// Copyright (c) 2019 Pedro Cortés). The R3000A with the GTE (cpu/psx), 2 MB
// of RAM, the scratchpad, the GPU, SPU, CD-ROM (XA-ADPCM), MDEC, DMA, root
// counters, interrupt controller and pad (machine/psx_*). The 512 KB BIOS
// (rom_data.c) and a disc image (.bin, raw 2352-byte sectors) are needed.
#ifndef PSX_M_H
#define PSX_M_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../cpu/psx/r3000a.h"
#include "../../../machine/psx_timers.h"
#include "../../../machine/psx_dma.h"
#include "../../../machine/psx_gpu.h"
#include "../../../machine/psx_cdrom.h"
#include "../../../machine/psx_joypad.h"
#include "../../../machine/psx_spu.h"
#include "../../../machine/psx_mdec.h"

enum { kCpuClock = 33868800, kCyclesPerFrame = 33868800 / 60, kSyncCycles = 100, kMipsUnderclock = 3,
       kMaxFbW = 640, kMaxFbH = 480 };

typedef struct {
    r3000a cpu_;
    psx_irq irq_;
    psx_dma dma_;
    psx_timers timers_;
    psx_gpu gpu_;
    psx_cdrom cdrom_;
    psx_joypad joypad_;
    psx_spu spu_;
    psx_mdec mdec_;
    uint8_t ram_[2 * 1024 * 1024];
    uint8_t scratch_[1024];
    uint8_t bios_[512 * 1024];
    uint8_t mem_ctrl1_[0x40], mem_ctrl2_[0x10], sio_[0x10];
    uint32_t cache_control_;
    uint32_t framebuffer_[kMaxFbW * kMaxFbH];
    int screen_w_, screen_h_;
    int16_t audio_[kSpuRate / 60 + 1];
    int naudio_;
} psx_m;

void psx_reset(psx_m *P);
void psx_run_frame(psx_m *P);
#endif
