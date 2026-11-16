// sh2.h - Hitachi SH-2 (SH7604) as used twice in the Sega 32X: a C11 port of
// dsp-cpp's cpu/sh2. The CPU with delay slots, exceptions and IRL interrupts,
// plus the on-chip modules games touch: the division unit, the two-channel
// DMA controller (auto request and external DREQ), the free-running timer,
// the watchdog interval timer, the 4 KB cache data array used as RAM and the
// bus state controller registers.
//
// Addresses are 32-bit. Bits 31-29 select the area: 0 cached, 1 cache
// through, 2 associative purge, 3 address array, 6 data array (cache RAM),
// 7 on-chip registers ($FFFFFE00-$FFFFFFFF). External accesses (areas 0 and
// 1) reach the bus with bits 28-0 of the address.
#ifndef SH2_H
#define SH2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t (*read8)(void *user, uint32_t a);
    uint16_t (*read16)(void *user, uint32_t a);
    uint32_t (*read32)(void *user, uint32_t a);
    void (*write8)(void *user, uint32_t a, uint8_t v);
    void (*write16)(void *user, uint32_t a, uint16_t v);
    void (*write32)(void *user, uint32_t a, uint32_t v);
    bool (*dreq)(void *user, int channel);           // (NULL: never)
    void *user;
} sh2_bus;

typedef struct { uint8_t *data; uint32_t mask; bool writable; } sh2_page;
typedef struct { uint32_t sar, dar, tcr, chcr, vcr; } sh2_dma;

typedef struct {
    sh2_bus bus;
    sh2_page pages_[32];
    uint32_t r_[16];
    uint32_t pc_, pr_, sr_, gbr_, vbr_, mach_, macl_;
    int cycles_;
    uint64_t total_cycles_;
    int irl_;
    bool sleeping_, in_slot_;
    uint8_t regs_[512];
    uint8_t cache_ram_[4096];
    uint16_t frc_, ocra_, ocrb_;
    uint8_t ftcsr_;
    int frt_div_;
    uint8_t wtcsr_, wtcnt_;
    int wdt_div_;
    uint32_t dvsr_, dvdnth_, dvdntl_, dvcr_;
    sh2_dma dma_[2];
    uint32_t dmaor_;
    bool dma_busy_;
    bool periph_dirty_;
    int periph_level_, periph_vector_;
} sh2;

// a 16 MB external page (bits 28-24) backed by `data` (big-endian, a power of
// two in size, mirrored): reads and fetches from it skip the bus
void sh2_map_memory(sh2 *P, int page, uint8_t *data, uint32_t size, bool writable);
void sh2_reset(sh2 *P);                 // power-on: PC = [0], SP = [4], VBR = 0, SR = $F0
int sh2_run(sh2 *P, int cycles);        // at least `cycles` (one per instruction plus waits)
static inline void sh2_set_irl(sh2 *P, int level) { P->irl_ = level; }
void sh2_dma_request(sh2 *P, int channel);
static inline bool sh2_sleeping(const sh2 *P) { return P->sleeping_; }
static inline uint32_t sh2_pc(const sh2 *P) { return P->pc_; }
#endif
