#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "psx_m.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static uint32_t physical_addr(psx_m *P, uint32_t address);
static void tick_devices(psx_m *P, int cycles);
static uint8_t read8(psx_m *P, uint32_t address);
static uint16_t read16(psx_m *P, uint32_t address);
static uint32_t read32(psx_m *P, uint32_t address);
static void write8(psx_m *P, uint32_t address, uint8_t value);
static void write16(psx_m *P, uint32_t address, uint16_t value);
static void write32(psx_m *P, uint32_t address, uint32_t value);
static uint32_t io_read32(psx_m *P, uint32_t addr);
static void io_write32(psx_m *P, uint32_t addr, uint32_t value);
static uint16_t io_read16(psx_m *P, uint32_t addr);
static void io_write16(psx_m *P, uint32_t addr, uint16_t value);
static uint8_t io_read8(psx_m *P, uint32_t addr);
static void io_write8(psx_m *P, uint32_t addr, uint8_t value);
static const uint32_t kRegionMask[8] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x1FFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
static uint32_t physical_addr(psx_m *P, uint32_t address) {
    return address & kRegionMask[address >> 29];
}
void psx_reset(psx_m *P) {
    memset(P->ram_, 0, sizeof P->ram_);
    FILL(P->scratch_, 0);
    FILL(P->mem_ctrl1_, 0);
    FILL(P->mem_ctrl2_, 0);
    FILL(P->sio_, 0);
    P->cache_control_ = 0;
    psx_irq_reset(&P->irq_);
    psx_dma_reset(&P->dma_);
    psx_timers_reset(&P->timers_);
    psx_gpu_reset(&P->gpu_);
    psx_cdrom_reset(&P->cdrom_);
    psx_joypad_reset(&P->joypad_);
    psx_spu_reset(&P->spu_);
    psx_mdec_reset(&P->mdec_);
    r3000a_reset(&P->cpu_);
    P->screen_w_ = 320;
    P->screen_h_ = 240;
    for (int i = 0; i < kMaxFbW * kMaxFbH; i++) P->framebuffer_[i] = 0xFF000000u;
}
static void tick_devices(psx_m *P, int cycles) {
    if (psx_gpu_tick(&P->gpu_, cycles)) psx_irq_raise(&P->irq_, kPsxIrqVblank);
    if (psx_cdrom_tick(&P->cdrom_, cycles)) psx_irq_raise(&P->irq_, kPsxIrqCdrom);
    if (psx_dma_tick(&P->dma_)) psx_irq_raise(&P->irq_, kPsxIrqDma);
    { const psx_gpu_sync gsync = psx_gpu_blanks_and_dot(&P->gpu_); psx_timers_sync_gpu(&P->timers_, &gsync); }
    if (psx_timers_tick(&P->timers_, 0, cycles)) psx_irq_raise(&P->irq_, kPsxIrqTmr0);
    if (psx_timers_tick(&P->timers_, 1, cycles)) psx_irq_raise(&P->irq_, kPsxIrqTmr1);
    if (psx_timers_tick(&P->timers_, 2, cycles)) psx_irq_raise(&P->irq_, kPsxIrqTmr2);
    if (psx_joypad_tick(&P->joypad_)) psx_irq_raise(&P->irq_, kPsxIrqController);
    if (psx_spu_tick(&P->spu_, cycles)) psx_irq_raise(&P->irq_, kPsxIrqSpu);
}
void psx_run_frame(psx_m *P) {
    // Match ProjectPSX RunFrame underclock scheme.
    const int kSyncLoops = (kCyclesPerFrame / (kSyncCycles * kMipsUnderclock)) + 1;
    int sync = 0;
    for (int i = 0; i < kSyncLoops; i++) {
        while (sync < kSyncCycles) {
            sync += r3000a_run(&P->cpu_, 1);
        }
        sync -= kSyncCycles;
        tick_devices(P, kSyncCycles * kMipsUnderclock + 1);
        r3000a_handle_interrupts(&P->cpu_);
    }

    P->screen_w_ = psx_gpu_display_width(&P->gpu_);
    P->screen_h_ = psx_gpu_display_height(&P->gpu_);
    if (P->screen_w_ <= 0) P->screen_w_ = 320;
    if (P->screen_h_ <= 0) P->screen_h_ = 240;
    P->screen_w_ = MIN(P->screen_w_, kMaxFbW);
    P->screen_h_ = MIN(P->screen_h_, kMaxFbH);
    psx_gpu_blit_display(&P->gpu_, P->framebuffer_, P->screen_w_, P->screen_h_);

    const int samples = kSpuRate / 60;
    P->naudio_ = psx_spu_drain_samples(&P->spu_, P->audio_, samples);

}
static uint8_t read8(psx_m *P, uint32_t address) {
    const uint32_t addr = physical_addr(P, address);
    if (addr < 0x1F000000u) {
        return P->ram_[addr & 0x1FFFFFu];
    }
    if (addr < 0x1F800000u) {
        return 0xFF;  // expansion
    }
    if (addr < 0x1F800400u) {
        return P->scratch_[addr & 0x3FFu];
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        return io_read8(P, addr);
    }
    if (addr >= 0x1FC00000u && addr < 0x1FC80000u) {
        return P->bios_[addr & 0x7FFFFu];
    }
    return 0xFF;
}
static uint16_t read16(psx_m *P, uint32_t address) {
    const uint32_t addr = physical_addr(P, address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFEu;
        return (uint16_t)(P->ram_[a] | (P->ram_[a + 1] << 8));
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FEu;
        return (uint16_t)(P->scratch_[a] | (P->scratch_[a + 1] << 8));
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        return io_read16(P, addr);
    }
    if (addr >= 0x1FC00000u && addr < 0x1FC80000u) {
        const uint32_t a = addr & 0x7FFFEu;
        return (uint16_t)(P->bios_[a] | (P->bios_[a + 1] << 8));
    }
    return 0xFFFF;
}
static uint32_t read32(psx_m *P, uint32_t address) {
    const uint32_t addr = physical_addr(P, address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFCu;
        return (uint32_t)(P->ram_[a]) | ((uint32_t)(P->ram_[a + 1]) << 8) |
               ((uint32_t)(P->ram_[a + 2]) << 16) | ((uint32_t)(P->ram_[a + 3]) << 24);
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FCu;
        return (uint32_t)(P->scratch_[a]) | ((uint32_t)(P->scratch_[a + 1]) << 8) |
               ((uint32_t)(P->scratch_[a + 2]) << 16) | ((uint32_t)(P->scratch_[a + 3]) << 24);
    }
    if (addr >= 0x1F801000u && addr < 0x1F804000u) {
        return io_read32(P, addr);
    }
    if (addr >= 0x1FC00000u && addr < 0x1FC80000u) {
        const uint32_t a = addr & 0x7FFFCu;
        return (uint32_t)(P->bios_[a]) | ((uint32_t)(P->bios_[a + 1]) << 8) |
               ((uint32_t)(P->bios_[a + 2]) << 16) | ((uint32_t)(P->bios_[a + 3]) << 24);
    }
    if (addr == 0xFFFE0130u) return P->cache_control_;
    return 0xFFFFFFFFu;
}
static void write8(psx_m *P, uint32_t address, uint8_t value) {
    const uint32_t addr = physical_addr(P, address);
    if (addr < 0x1F000000u) {
        P->ram_[addr & 0x1FFFFFu] = value;
        return;
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        P->scratch_[addr & 0x3FFu] = value;
        return;
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        io_write8(P, addr, value);
        return;
    }
}
static void write16(psx_m *P, uint32_t address, uint16_t value) {
    const uint32_t addr = physical_addr(P, address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFEu;
        P->ram_[a] = (uint8_t)(value);
        P->ram_[a + 1] = (uint8_t)(value >> 8);
        return;
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FEu;
        P->scratch_[a] = (uint8_t)(value);
        P->scratch_[a + 1] = (uint8_t)(value >> 8);
        return;
    }
    if (addr >= 0x1F801000u && addr < 0x1F802000u) {
        io_write16(P, addr, value);
        return;
    }
    if (addr == 0xFFFE0130u) {
        P->cache_control_ = value;
        return;
    }
}
static void write32(psx_m *P, uint32_t address, uint32_t value) {
    const uint32_t addr = physical_addr(P, address);
    if (addr < 0x1F000000u) {
        const uint32_t a = addr & 0x1FFFFCu;
        P->ram_[a] = (uint8_t)(value);
        P->ram_[a + 1] = (uint8_t)(value >> 8);
        P->ram_[a + 2] = (uint8_t)(value >> 16);
        P->ram_[a + 3] = (uint8_t)(value >> 24);
        return;
    }
    if (addr < 0x1F800400u && addr >= 0x1F800000u) {
        const uint32_t a = addr & 0x3FCu;
        P->scratch_[a] = (uint8_t)(value);
        P->scratch_[a + 1] = (uint8_t)(value >> 8);
        P->scratch_[a + 2] = (uint8_t)(value >> 16);
        P->scratch_[a + 3] = (uint8_t)(value >> 24);
        return;
    }
    if (addr >= 0x1F801000u && addr < 0x1F804000u) {
        io_write32(P, addr, value);
        return;
    }
    if (addr == 0xFFFE0130u) {
        P->cache_control_ = value;
        return;
    }
}
static uint32_t io_read32(psx_m *P, uint32_t addr) {
    if (addr < 0x1F801040u) {
        const uint32_t o = addr & 0x3Cu;
        return (uint32_t)(P->mem_ctrl1_[o]) | ((uint32_t)(P->mem_ctrl1_[o + 1]) << 8) |
               ((uint32_t)(P->mem_ctrl1_[o + 2]) << 16) | ((uint32_t)(P->mem_ctrl1_[o + 3]) << 24);
    }
    if (addr < 0x1F801050u) return psx_joypad_load(&P->joypad_, addr);
    if (addr < 0x1F801060u) {
        if (addr == 0x1F801054u) return 0x00000805u;  // SIO_STAT stub
        const uint32_t o = addr & 0xCu;
        return (uint32_t)(P->sio_[o]) | ((uint32_t)(P->sio_[o + 1]) << 8) |
               ((uint32_t)(P->sio_[o + 2]) << 16) | ((uint32_t)(P->sio_[o + 3]) << 24);
    }
    if (addr < 0x1F801070u) {
        const uint32_t o = addr & 0xCu;
        return (uint32_t)(P->mem_ctrl2_[o]) | ((uint32_t)(P->mem_ctrl2_[o + 1]) << 8) |
               ((uint32_t)(P->mem_ctrl2_[o + 2]) << 16) | ((uint32_t)(P->mem_ctrl2_[o + 3]) << 24);
    }
    if (addr < 0x1F801080u) return psx_irq_load(&P->irq_, addr);
    if (addr < 0x1F801100u) return psx_dma_load(&P->dma_, addr);
    if (addr < 0x1F801140u) return psx_timers_load(&P->timers_, addr);
    if (addr <= 0x1F801803u) return psx_cdrom_load(&P->cdrom_, addr);
    if (addr == 0x1F801810u) return psx_gpu_load_gpuread(&P->gpu_);
    if (addr == 0x1F801814u) return psx_gpu_load_gpustat(&P->gpu_);
    if (addr == 0x1F801820u) return psx_mdec_read_data(&P->mdec_);
    if (addr == 0x1F801824u) return psx_mdec_read_status(&P->mdec_);
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) return psx_spu_load32(&P->spu_, addr);
    return 0xFFFFFFFFu;
}
static void io_write32(psx_m *P, uint32_t addr, uint32_t value) {
    if (addr < 0x1F801040u) {
        const uint32_t o = addr & 0x3Cu;
        P->mem_ctrl1_[o] = (uint8_t)(value);
        P->mem_ctrl1_[o + 1] = (uint8_t)(value >> 8);
        P->mem_ctrl1_[o + 2] = (uint8_t)(value >> 16);
        P->mem_ctrl1_[o + 3] = (uint8_t)(value >> 24);
        return;
    }
    if (addr < 0x1F801050u) {
        psx_joypad_write(&P->joypad_, addr, value);
        return;
    }
    if (addr < 0x1F801060u) {
        const uint32_t o = addr & 0xCu;
        P->sio_[o] = (uint8_t)(value);
        P->sio_[o + 1] = (uint8_t)(value >> 8);
        P->sio_[o + 2] = (uint8_t)(value >> 16);
        P->sio_[o + 3] = (uint8_t)(value >> 24);
        return;
    }
    if (addr < 0x1F801070u) {
        const uint32_t o = addr & 0xCu;
        P->mem_ctrl2_[o] = (uint8_t)(value);
        P->mem_ctrl2_[o + 1] = (uint8_t)(value >> 8);
        P->mem_ctrl2_[o + 2] = (uint8_t)(value >> 16);
        P->mem_ctrl2_[o + 3] = (uint8_t)(value >> 24);
        return;
    }
    if (addr < 0x1F801080u) {
        psx_irq_write(&P->irq_, addr, value);
        return;
    }
    if (addr < 0x1F801100u) {
        psx_dma_write(&P->dma_, addr, value);
        return;
    }
    if (addr < 0x1F801140u) {
        psx_timers_write(&P->timers_, addr, value);
        return;
    }
    if (addr < 0x1F801810u) {
        psx_cdrom_write(&P->cdrom_, addr, value);
        return;
    }
    if (addr < 0x1F801820u) {
        psx_gpu_write(&P->gpu_, addr, value);
        return;
    }
    if (addr < 0x1F801830u) {
        // MDEC stub (accept writes so games do not trap on missing regs).
        psx_mdec_write(&P->mdec_, addr, value);
        return;
    }
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) {
        psx_spu_write32(&P->spu_, addr, value);
        return;
    }
}
static uint16_t io_read16(psx_m *P, uint32_t addr) {
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) return psx_spu_read16(&P->spu_, addr);
    if (addr < 0x1F801050u && addr >= 0x1F801040u) return (uint16_t)(psx_joypad_load(&P->joypad_, addr));
    if (addr < 0x1F801080u && addr >= 0x1F801070u) return (uint16_t)(psx_irq_load(&P->irq_, addr));
    if (addr < 0x1F801100u && addr >= 0x1F801080u) return (uint16_t)(psx_dma_load(&P->dma_, addr));
    if (addr < 0x1F801140u && addr >= 0x1F801100u) return (uint16_t)(psx_timers_load(&P->timers_, addr));
    return (uint16_t)(io_read32(P, addr & ~3u) >> ((addr & 2u) * 8));
}
static void io_write16(psx_m *P, uint32_t addr, uint16_t value) {
    if (addr >= 0x1F801C00u && addr < 0x1F802000u) {
        psx_spu_write16(&P->spu_, addr, value);
        return;
    }
    if (addr < 0x1F801050u && addr >= 0x1F801040u) {
        psx_joypad_write(&P->joypad_, addr, value);
        return;
    }
    if (addr < 0x1F801080u && addr >= 0x1F801070u) {
        psx_irq_write(&P->irq_, addr, value);
        return;
    }
    if (addr < 0x1F801100u && addr >= 0x1F801080u) {
        psx_dma_write(&P->dma_, addr, value);
        return;
    }
    if (addr < 0x1F801140u && addr >= 0x1F801100u) {
        psx_timers_write(&P->timers_, addr, value);
        return;
    }
    // Merge into 32-bit write for other regs.
    const uint32_t aligned = addr & ~3u;
    uint32_t cur = io_read32(P, aligned);
    if (addr & 2u) cur = (cur & 0x0000FFFFu) | ((uint32_t)(value) << 16);
    else cur = (cur & 0xFFFF0000u) | value;
    io_write32(P, aligned, cur);
}
static uint8_t io_read8(psx_m *P, uint32_t addr) {
    if (addr >= 0x1F801800u && addr <= 0x1F801803u) return (uint8_t)(psx_cdrom_load(&P->cdrom_, addr));
    if (addr >= 0x1F801040u && addr < 0x1F801050u) return (uint8_t)(psx_joypad_load(&P->joypad_, addr));
    return (uint8_t)(io_read32(P, addr & ~3u) >> ((addr & 3u) * 8));
}
static void io_write8(psx_m *P, uint32_t addr, uint8_t value) {
    if (addr >= 0x1F801800u && addr <= 0x1F801803u) {
        psx_cdrom_write(&P->cdrom_, addr, value);
        return;
    }
    if (addr >= 0x1F801040u && addr < 0x1F801050u) {
        psx_joypad_write(&P->joypad_, addr, value);
        return;
    }
    const uint32_t aligned = addr & ~3u;
    const int shift = (int)(addr & 3u) * 8;
    uint32_t cur = io_read32(P, aligned);
    cur = (cur & ~(0xFFu << shift)) | ((uint32_t)(value) << shift);
    io_write32(P, aligned, cur);
}



// ---- the devices' wiring ----
static inline void ram_w32(psx_m *P, uint32_t a, uint32_t w)
{
    a &= 0x1FFFFCu;
    P->ram_[a] = (uint8_t)w;
    P->ram_[a + 1] = (uint8_t)(w >> 8);
    P->ram_[a + 2] = (uint8_t)(w >> 16);
    P->ram_[a + 3] = (uint8_t)(w >> 24);
}
static uint32_t d_ram_read(void *u, uint32_t a)
{
    psx_m *P = (psx_m *)u;
    a &= 0x1FFFFCu;
    return (uint32_t)P->ram_[a] | ((uint32_t)P->ram_[a + 1] << 8) | ((uint32_t)P->ram_[a + 2] << 16) | ((uint32_t)P->ram_[a + 3] << 24);
}
static void d_ram_write(void *u, uint32_t a, uint32_t v) { ram_w32((psx_m *)u, a, v); }
static void d_gpu_from_ram(void *u, const uint32_t *d, int n) { psx_gpu_process_dma(&((psx_m *)u)->gpu_, d, n); }
static void d_gpu_to_ram(void *u, uint32_t addr, int n)
{
    psx_m *P = (psx_m *)u;
    for (int i = 0; i < n; i++) {
        ram_w32(P, addr, psx_gpu_load_gpuread(&P->gpu_));
        addr += 4;
    }
}
static void to_ram(psx_m *P, uint32_t addr, const uint32_t *buf, int n)
{
    for (int i = 0; i < n; i++) ram_w32(P, addr + (uint32_t)i * 4, buf[i]);
}
static uint32_t *dma_buf;             // (on the heap, as the rest)
#define DMA_BUF_WORDS 0x40000
static void d_cdrom_to_ram(void *u, uint32_t addr, int n)
{
    psx_m *P = (psx_m *)u;
    if (n < 0) n = 0;
    if (n > DMA_BUF_WORDS) n = DMA_BUF_WORDS;
    memset(dma_buf, 0, (size_t)n * 4);
    psx_cdrom_dma_read(&P->cdrom_, dma_buf, n);
    to_ram(P, addr, dma_buf, n);
}
static void d_spu_from_ram(void *u, const uint32_t *d, int n) { psx_spu_dma_write(&((psx_m *)u)->spu_, d, n); }
static void d_spu_to_ram(void *u, uint32_t addr, int n)
{
    psx_m *P = (psx_m *)u;
    if (n < 0) n = 0;
    if (n > DMA_BUF_WORDS) n = DMA_BUF_WORDS;
    memset(dma_buf, 0, (size_t)n * 4);
    psx_spu_dma_read(&P->spu_, dma_buf, n);
    to_ram(P, addr, dma_buf, n);
}
static void d_mdec_from_ram(void *u, const uint32_t *d, int n) { psx_mdec_dma_write(&((psx_m *)u)->mdec_, d, n); }
static void d_mdec_to_ram(void *u, uint32_t addr, int n)
{
    psx_m *P = (psx_m *)u;
    if (n < 0) n = 0;
    if (n > DMA_BUF_WORDS) n = DMA_BUF_WORDS;
    memset(dma_buf, 0, (size_t)n * 4);
    psx_mdec_dma_read(&P->mdec_, dma_buf, n);
    to_ram(P, addr, dma_buf, n);
}
static void cd_audio(void *u, const int16_t *s, int n) { psx_spu_push_cd_samples(&((psx_m *)u)->spu_, s, n); }
static uint8_t c_r8(void *u, uint32_t a) { return read8((psx_m *)u, a); }
static uint16_t c_r16(void *u, uint32_t a) { return read16((psx_m *)u, a); }
static uint32_t c_r32(void *u, uint32_t a) { return read32((psx_m *)u, a); }
static void c_w8(void *u, uint32_t a, uint8_t v) { write8((psx_m *)u, a, v); }
static void c_w16(void *u, uint32_t a, uint16_t v) { write16((psx_m *)u, a, v); }
static void c_w32(void *u, uint32_t a, uint32_t v) { write32((psx_m *)u, a, v); }
static bool c_irq(void *u) { return psx_irq_pending(&((psx_m *)u)->irq_); }

static void psx_wire(psx_m *P)
{
    psx_dma *d = &P->dma_;
    d->ram_read = d_ram_read;
    d->ram_write = d_ram_write;
    d->gpu_from_ram = d_gpu_from_ram;
    d->gpu_to_ram = d_gpu_to_ram;
    d->cdrom_to_ram = d_cdrom_to_ram;
    d->spu_from_ram = d_spu_from_ram;
    d->spu_to_ram = d_spu_to_ram;
    d->mdec_from_ram = d_mdec_from_ram;
    d->mdec_to_ram = d_mdec_to_ram;
    d->user = P;
    P->cdrom_.cd_audio_cb_ = cd_audio;
    P->cdrom_.cd_audio_user_ = P;
    r3000a *c = &P->cpu_;
    c->read8_ = c_r8;
    c->read16_ = c_r16;
    c->read32_ = c_r32;
    c->write8_ = c_w8;
    c->write16_ = c_w16;
    c->write32_ = c_w32;
    c->irq_pending_ = c_irq;
    c->user_ = P;
}

// ---- the console API ----
#include <stdlib.h>
#include "../common/cons.h"
#include "rom_data.h"

static const char *const kExts[] = { ".bin", ".img", NULL };
const cons_info cons_machine = {
    "psx", "Sony PlayStation", "Sony, 1994", kExts, "CD BIN", kMaxFbW, kMaxFbH,
    60.0, kSpuRate, { "X", "O", "SQUARE", "TRIANGLE" }, "START", "SELECT", CONS_NO_KEYS, 1, NULL,
};

static psx_m *X;
static bool have_bios;
static uint8_t *disc;
static uint16_t *fb565;

bool cons_init(void)
{
    X = (psx_m *)calloc(1, sizeof *X);
    fb565 = (uint16_t *)calloc(kMaxFbW * kMaxFbH, sizeof(uint16_t));
    dma_buf = (uint32_t *)calloc(DMA_BUF_WORDS, sizeof(uint32_t));
    if (!X || !fb565 || !dma_buf) return false;
    if (psx_rom_bios) {
        memcpy(X->bios_, psx_rom_bios, sizeof X->bios_);
        have_bios = true;
    }
    psx_gpu_init(&X->gpu_);
    psx_mdec_init(&X->mdec_);
    psx_joypad_defaults(&X->joypad_);
    psx_wire(X);
    X->screen_w_ = 320;
    X->screen_h_ = 240;
    psx_reset(X);
    return true;
}

void cons_reset(void) { psx_reset(X); }

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (data && size == sizeof X->bios_) {          // a BIOS (512 KB is no whole number of sectors)
        memcpy(X->bios_, data, size);
        free(data);
        have_bios = true;
        psx_reset(X);
        if (why) *why = "BIOS loaded: now the disc";
        return false;
    }
    if (!have_bios) {
        if (why) *why = "the PlayStation BIOS (scph*.bin, 512 KB) is needed: load it first";
        free(data);
        return false;
    }
    if (!data || size < kBytesPerSector) {
        if (why) *why = "not a CD image (.bin, 2352-byte sectors)";
        free(data);
        return false;
    }
    free(disc);
    disc = data;
    psx_cdrom_load_bin(&X->cdrom_, disc, size);
    return true;
}

void cons_eject(void)
{
    X->cdrom_.ntracks_ = 0;
    free(disc);
    disc = NULL;
    psx_reset(X);
}

bool cons_loaded(void) { return have_bios; }

int cons_run_frame(const int16_t **audio)
{
    X->naudio_ = 0;
    *audio = X->audio_;
    if (have_bios) psx_run_frame(X);
    else {
        X->naudio_ = kSpuRate / 60;
        memset(X->audio_, 0, sizeof X->audio_);
    }
    const int n = X->screen_w_ * X->screen_h_;
    for (int i = 0; i < n; i++) fb565[i] = cons_rgb565(X->framebuffer_[i]);
    return X->naudio_;
}

const uint16_t *cons_frame(int *w, int *h, int *stride)
{
    *w = X->screen_w_;
    *h = X->screen_h_;
    *stride = X->screen_w_;
    return fb565;
}

void cons_pad(int player, unsigned b)
{
    if (player) return;
    uint16_t v = 0xFFFF;
    if (b & CP_UP) v &= (uint16_t)~kPsxPadUp;
    if (b & CP_DOWN) v &= (uint16_t)~kPsxPadDown;
    if (b & CP_LEFT) v &= (uint16_t)~kPsxPadLeft;
    if (b & CP_RIGHT) v &= (uint16_t)~kPsxPadRight;
    if (b & CP_B1) v &= (uint16_t)~kPsxPadCross;
    if (b & CP_B2) v &= (uint16_t)~kPsxPadCircle;
    if (b & CP_B3) v &= (uint16_t)~kPsxPadSquare;
    if (b & CP_B4) v &= (uint16_t)~kPsxPadTriangle;
    if (b & CP_START) v &= (uint16_t)~kPsxPadStart;
    if (b & CP_SELECT) v &= (uint16_t)~kPsxPadSelect;
    psx_joypad_set_buttons(&X->joypad_, v);
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return have_bios ? (psx_cdrom_disc_loaded(&X->cdrom_) ? NULL : "no disc") : "no BIOS"; }
