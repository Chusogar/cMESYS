// gba_m.h - the Game Boy Advance machine (gba_core.c): a C11 migration of
// dsp-cpp's drivers/consoles/gba. The ARM7TDMI (cpu/arm7tdmi) with the wait
// states of every region, the PPU (video/gba_ppu), the sound
// (sound/gba_apu) with its DMA-fed FIFOs, the four timers, the four DMA
// channels, the interrupts and HALT, the keypad, and the SRAM / Flash /
// EEPROM saves found by their ID strings. The 16 KB BIOS (gba.bin) is needed.
#ifndef GBA_M_H
#define GBA_M_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../cpu/arm7tdmi/arm7tdmi.h"
#include "../../../video/gba_ppu.h"
#include "../../../sound/gba_apu.h"

enum { kClock = 16777216, kCyclesPerLine = 1232, kHDrawCycles = 960, kLines = 228, kVisibleLines = 160 };
#define GBA_FPS ((double)kClock / (double)(kCyclesPerLine * kLines))
enum { kIrqVBlank = 0, kIrqHBlank = 1, kIrqVCount = 2, kIrqTimer0 = 3, kIrqDma0 = 8, kIrqKeypad = 12 };
enum { SAVE_None, SAVE_Sram, SAVE_Flash64, SAVE_Flash128, SAVE_Eeprom };

typedef struct { uint16_t reload, control; uint32_t counter, sub; } gba_timer;
typedef struct { uint32_t src, dst, isrc, idst, icount; uint16_t count, control; } gba_dmach;

typedef struct {
    arm7tdmi cpu_;
    gba_ppu ppu_;
    gba_apu apu_;
    uint8_t bios_[0x4000];
    uint8_t ewram_[0x40000];
    uint8_t iwram_[0x8000];
    uint8_t io_[0x400];
    uint8_t pal_[0x400];
    uint8_t vram_[0x18000];
    uint8_t oam_[0x400];
    uint8_t *rom_;
    size_t rom_size_;
    uint8_t save_[0x20000];
    size_t save_size_;
    uint32_t *framebuffer_;         // kWidth x kHeight, 0xAARRGGBB
    uint64_t cycles_;
    int line_;
    bool halted_;
    uint16_t ie_, if_, ime_, dispstat_, keyinput_;
    uint32_t bios_latch_, last_fetch_, last_rom_address_;
    gba_timer timers_[4];
    gba_dmach dma_[4];
    bool dma_running_;
    uint64_t audio_acc_;
    int16_t audio_[2048];
    int naudio_;
    int save_type_;
    bool save_dirty_;
    int save_timer_, flash_state_;
    bool flash_id_, flash_erase_, flash_write_, flash_bank_cmd_;
    int flash_bank_, eeprom_addr_bits_;
    uint8_t eeprom_bits_[128];
    size_t eeprom_nbits_;
    bool eeprom_reading_;
    int eeprom_read_pos_;
    uint32_t eeprom_read_addr_;
} gba_m;

void gba_reset(gba_m *P);
void gba_run_frame(gba_m *P);
#endif
