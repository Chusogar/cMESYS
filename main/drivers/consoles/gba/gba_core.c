#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "gba_m.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void detect_save_type(gba_m *P);
static int rom_wait(gba_m *P, uint32_t address, int width, bool code);
static uint32_t open_bus(gba_m *P, uint32_t address);
static bool is_eeprom(gba_m *P, uint32_t address);
static uint8_t read8(gba_m *P, uint32_t address);
static uint16_t read16(gba_m *P, uint32_t address, bool code);
static uint32_t read32(gba_m *P, uint32_t address, bool code);
static void write8(gba_m *P, uint32_t address, uint8_t value);
static void write16(gba_m *P, uint32_t address, uint16_t value);
static void write32(gba_m *P, uint32_t address, uint32_t value);
static uint16_t io_read16(gba_m *P, uint32_t off);
static void io_write16(gba_m *P, uint32_t off, uint16_t value, uint16_t mask);
static void raise_irq(gba_m *P, int bit);
static void update_irq(gba_m *P);
static void check_keypad_irq(gba_m *P);
static void timer_overflow(gba_m *P, int t);
static void tick_timers(gba_m *P, uint64_t cycles);
static uint64_t cycles_to_timer_event(gba_m *P);
static void dma_write_control(gba_m *P, int n, uint16_t value);
static void dma_trigger(gba_m *P, int timing);
static void run_dma(gba_m *P, int n);
static uint8_t save_read8(gba_m *P, uint32_t address);
static void save_write8(gba_m *P, uint32_t address, uint8_t value);
static uint16_t eeprom_read(gba_m *P);
static void eeprom_write(gba_m *P, uint16_t value);
static void tick(gba_m *P, uint64_t cycles);
static void run_until(gba_m *P, uint64_t target);
static const size_t kBiosSize = 0x4000;

static const int kNWait[4] = {4, 3, 2, 8};
static const uint32_t kTimerPrescale[4] = {1, 64, 256, 1024};
static bool find_bytes(const uint8_t *hay, size_t n, const char *needle)
{
    const size_t m = strlen(needle);
    for (size_t i = 0; i + m <= n; i++)
        if (hay[i] == (uint8_t)needle[0] && memcmp(hay + i, needle, m) == 0) return true;
    return false;
}

static inline uint16_t io16(gba_m *P, uint32_t offset) { return (uint16_t)(P->io_[offset] | (P->io_[offset + 1] << 8)); }
static inline void set_io16(gba_m *P, uint32_t offset, uint16_t value)
{
    P->io_[offset] = (uint8_t)value;
    P->io_[offset + 1] = (uint8_t)(value >> 8);
}

static void detect_save_type(gba_m *P) {
    P->save_type_ = SAVE_None;
    P->save_size_ = 0;
    if (find_bytes(P->rom_, P->rom_size_, "EEPROM_V")) {
        P->save_type_ = SAVE_Eeprom;
        P->save_size_ = 0x2000; memset(P->save_, 0xFF, 0x2000);
    } else if (find_bytes(P->rom_, P->rom_size_, "FLASH1M_V")) {
        P->save_type_ = SAVE_Flash128;
        P->save_size_ = 0x20000; memset(P->save_, 0xFF, 0x20000);
    } else if (find_bytes(P->rom_, P->rom_size_, "FLASH_V") || find_bytes(P->rom_, P->rom_size_, "FLASH512_V")) {
        P->save_type_ = SAVE_Flash64;
        P->save_size_ = 0x10000; memset(P->save_, 0xFF, 0x10000);
    } else if (find_bytes(P->rom_, P->rom_size_, "SRAM_V") || find_bytes(P->rom_, P->rom_size_, "SRAM_F_V")) {
        P->save_type_ = SAVE_Sram;
        P->save_size_ = 0x8000; memset(P->save_, 0xFF, 0x8000);
    }
}
void gba_reset(gba_m *P) {
    memset(P->ewram_, 0, sizeof P->ewram_);
    memset(P->iwram_, 0, sizeof P->iwram_);
    memset(P->vram_, 0, sizeof P->vram_);
    FILL(P->io_, 0);
    FILL(P->pal_, 0);
    FILL(P->oam_, 0);
    for (int i_ = 0; i_ < kWidth * kHeight; i_++) P->framebuffer_[i_] = 0xFF000000u;
    P->cycles_ = 0;
    P->line_ = 0;
    P->halted_ = false;
    P->ie_ = P->if_ = P->ime_ = 0;
    P->dispstat_ = 0;
    P->keyinput_ = 0x3FF;
    P->bios_latch_ = 0;
    P->last_fetch_ = 0;
    P->last_rom_address_ = 0xFFFFFFFF;
    memset(P->timers_, 0, sizeof P->timers_);
    memset(P->dma_, 0, sizeof P->dma_);
    P->dma_running_ = false;
    P->audio_acc_ = 0;
    P->naudio_ = 0;
    P->flash_state_ = 0;
    P->flash_id_ = P->flash_erase_ = P->flash_write_ = P->flash_bank_cmd_ = false;
    P->flash_bank_ = 0;
    P->eeprom_nbits_ = 0;
    P->eeprom_reading_ = false;
    set_io16(P, 0x20, 0x100);  // BG2PA
    set_io16(P, 0x26, 0x100);  // BG2PD
    set_io16(P, 0x30, 0x100);
    set_io16(P, 0x36, 0x100);
    set_io16(P, 0x88, 0x200);  // SOUNDBIAS
    gba_ppu_reset(&P->ppu_);
    gba_apu_reset(&P->apu_);
    arm7_reset(&P->cpu_);
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------
static int rom_wait(gba_m *P, uint32_t address, int width, bool code) {
    const uint16_t waitcnt = io16(P, 0x204);
    const int region = (int)((address >> 25) & 3);  // 0: WS0, 1: WS1, 2: WS2
    int n, s;
    switch (region) {
        case 0: n = kNWait[(waitcnt >> 2) & 3]; s = (waitcnt & 0x10) ? 1 : 2; break;
        case 1: n = kNWait[(waitcnt >> 5) & 3]; s = (waitcnt & 0x80) ? 1 : 4; break;
        default: n = kNWait[(waitcnt >> 8) & 3]; s = (waitcnt & 0x400) ? 1 : 8; break;
    }
    const bool seq = address == P->last_rom_address_;
    P->last_rom_address_ = address + (uint32_t)(width);
    if (code && seq && (waitcnt & 0x4000)) return width == 4 ? 2 : 1;  // prefetch buffer
    int cost = 1 + (seq ? s : n);
    if (width == 4) cost += 1 + s;
    return cost;
}
static uint32_t open_bus(gba_m *P, uint32_t address) {
    // Unmapped reads return the last prefetched opcode.
    const uint32_t sh = (address & 3) * 8;
    return sh ? (P->last_fetch_ >> sh) | (P->last_fetch_ << (32 - sh)) : P->last_fetch_;
}
static bool is_eeprom(gba_m *P, uint32_t address) {
    if (P->save_type_ != SAVE_Eeprom) return false;
    if ((address >> 24) != 0x0D) return false;
    return P->rom_size_ <= 0x1000000 || address >= 0x0DFFFF00u;
}
static uint8_t read8(gba_m *P, uint32_t address) {
    switch (address >> 24) {
        case 0x00:
            P->cycles_ += 1;
            if (address < kBiosSize) {
                if (arm7_pc(&P->cpu_) >= kBiosSize) return (uint8_t)(P->bios_latch_ >> ((address & 3) * 8));
                return P->bios_[address];
            }
            return (uint8_t)(open_bus(P, address));
        case 0x02: P->cycles_ += 3; return P->ewram_[address & 0x3FFFF];
        case 0x03: P->cycles_ += 1; return P->iwram_[address & 0x7FFF];
        case 0x04: {
            P->cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off >= 0x400) return (uint8_t)(open_bus(P, address));
            return (uint8_t)(io_read16(P, off & ~1u) >> ((off & 1) * 8));
        }
        case 0x05: P->cycles_ += 1; return P->pal_[address & 0x3FF];
        case 0x06: {
            P->cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            return P->vram_[a];
        }
        case 0x07: P->cycles_ += 1; return P->oam_[address & 0x3FF];
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
            P->cycles_ += (uint64_t)(rom_wait(P, address & ~1u, 2, false));
            if (is_eeprom(P, address)) return (uint8_t)(eeprom_read(P));
            const uint32_t a = address & 0x1FFFFFF;
            if (a < P->rom_size_) return P->rom_[a];
            return (uint8_t)(((address >> 1) & 0xFFFF) >> ((address & 1) * 8));
        }
        case 0x0E: case 0x0F:
            P->cycles_ += (uint64_t)(1 + kNWait[io16(P, 0x204) & 3]);
            return save_read8(P, address);
        default:
            P->cycles_ += 1;
            return (uint8_t)(open_bus(P, address));
    }
}
static uint16_t read16(gba_m *P, uint32_t address, bool code) {
    const uint32_t unaligned = address;
    address &= ~1u;
    uint16_t value;
    switch (address >> 24) {
        case 0x00:
            P->cycles_ += 1;
            if (address < kBiosSize) {
                if (arm7_pc(&P->cpu_) >= kBiosSize && !code) {
                    value = (uint16_t)(P->bios_latch_ >> ((address & 2) * 8));
                } else {
                    value = (uint16_t)(P->bios_[address] | (P->bios_[address + 1] << 8));
                    // The protected-BIOS latch holds the prefetched opcode
                    // (executing address + 4 in Thumb, + 8 in ARM).
                    if (code) memcpy(&P->bios_latch_, &P->bios_[(address + 4) & 0x3FFC], 4);
                }
            } else {
                value = (uint16_t)(open_bus(P, address));
            }
            break;
        case 0x02: P->cycles_ += 3; value = (uint16_t)(P->ewram_[address & 0x3FFFF] | (P->ewram_[(address & 0x3FFFF) + 1] << 8)); break;
        case 0x03: P->cycles_ += 1; value = (uint16_t)(P->iwram_[address & 0x7FFF] | (P->iwram_[(address & 0x7FFF) + 1] << 8)); break;
        case 0x04: {
            P->cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            value = off < 0x400 ? io_read16(P, off) : (uint16_t)(open_bus(P, address));
            break;
        }
        case 0x05: P->cycles_ += 1; value = (uint16_t)(P->pal_[address & 0x3FF] | (P->pal_[(address & 0x3FF) + 1] << 8)); break;
        case 0x06: {
            P->cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            value = (uint16_t)(P->vram_[a] | (P->vram_[a + 1] << 8));
            break;
        }
        case 0x07: P->cycles_ += 1; value = (uint16_t)(P->oam_[address & 0x3FF] | (P->oam_[(address & 0x3FF) + 1] << 8)); break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
            P->cycles_ += (uint64_t)(rom_wait(P, address, 2, code));
            if (is_eeprom(P, address)) {
                value = eeprom_read(P);
                break;
            }
            const uint32_t a = address & 0x1FFFFFF;
            if (a + 1 < P->rom_size_) value = (uint16_t)(P->rom_[a] | (P->rom_[a + 1] << 8));
            else value = (uint16_t)(address >> 1);
            break;
        }
        case 0x0E: case 0x0F:
            P->cycles_ += (uint64_t)(1 + kNWait[io16(P, 0x204) & 3]);
            value = (uint16_t)(save_read8(P, unaligned) * 0x0101u);  // 8-bit bus
            break;
        default:
            P->cycles_ += 1;
            value = (uint16_t)(open_bus(P, address));
            break;
    }
    if (code) P->last_fetch_ = (uint32_t)(value) | ((uint32_t)(value) << 16);
    return value;
}
static uint32_t read32(gba_m *P, uint32_t address, bool code) {
    const uint32_t unaligned = address;
    address &= ~3u;
    uint32_t value;
    switch (address >> 24) {
        case 0x00:
            P->cycles_ += 1;
            if (address < kBiosSize) {
                if (arm7_pc(&P->cpu_) >= kBiosSize && !code) {
                    value = P->bios_latch_;
                } else {
                    memcpy(&value, &P->bios_[address], 4);
                    if (code) memcpy(&P->bios_latch_, &P->bios_[(address + 8) & 0x3FFC], 4);
                }
            } else {
                value = open_bus(P, address);
            }
            break;
        case 0x02: P->cycles_ += 6; memcpy(&value, &P->ewram_[address & 0x3FFFF], 4); break;
        case 0x03: P->cycles_ += 1; memcpy(&value, &P->iwram_[address & 0x7FFF], 4); break;
        case 0x04: {
            P->cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off < 0x400) value = (uint32_t)(io_read16(P, off)) | ((uint32_t)(io_read16(P, off + 2)) << 16);
            else value = open_bus(P, address);
            break;
        }
        case 0x05: P->cycles_ += 2; memcpy(&value, &P->pal_[address & 0x3FF], 4); break;
        case 0x06: {
            P->cycles_ += 2;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            memcpy(&value, &P->vram_[a], 4);
            break;
        }
        case 0x07: P->cycles_ += 1; memcpy(&value, &P->oam_[address & 0x3FF], 4); break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
            P->cycles_ += (uint64_t)(rom_wait(P, address, 4, code));
            const uint32_t a = address & 0x1FFFFFF;
            if (a + 3 < P->rom_size_) {
                memcpy(&value, &P->rom_[a], 4);
            } else {
                value = ((address >> 1) & 0xFFFF) | ((((address >> 1) + 1) & 0xFFFF) << 16);
            }
            break;
        }
        case 0x0E: case 0x0F:
            P->cycles_ += (uint64_t)(1 + kNWait[io16(P, 0x204) & 3]);
            value = save_read8(P, unaligned) * 0x01010101u;
            break;
        default:
            P->cycles_ += 1;
            value = open_bus(P, address);
            break;
    }
    if (code) P->last_fetch_ = value;
    return value;
}
static void write8(gba_m *P, uint32_t address, uint8_t value) {
    switch (address >> 24) {
        case 0x02: P->cycles_ += 3; P->ewram_[address & 0x3FFFF] = value; return;
        case 0x03: P->cycles_ += 1; P->iwram_[address & 0x7FFF] = value; return;
        case 0x04: {
            P->cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off >= 0x400) return;
            const uint16_t v = (uint16_t)(value) * 0x0101u;
            io_write16(P, off & ~1u, v, (off & 1) ? 0xFF00 : 0x00FF);
            return;
        }
        case 0x05:  // byte writes to palette RAM store the byte in both halves
            P->cycles_ += 1;
            P->pal_[address & 0x3FE] = value;
            P->pal_[(address & 0x3FE) + 1] = value;
            return;
        case 0x06: {
            P->cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            // BG VRAM: the byte lands in both halves; OBJ VRAM ignores bytes.
            const uint32_t limit = (io16(P, 0) & 7) >= 3 ? 0x14000u : 0x10000u;
            if (a < limit) {
                P->vram_[a & ~1u] = value;
                P->vram_[(a & ~1u) + 1] = value;
            }
            return;
        }
        case 0x07: P->cycles_ += 1; return;  // OAM ignores byte writes
        case 0x0E: case 0x0F:
            P->cycles_ += (uint64_t)(1 + kNWait[io16(P, 0x204) & 3]);
            save_write8(P, address, value);
            return;
        default:
            P->cycles_ += 1;
            return;
    }
}
static void write16(gba_m *P, uint32_t address, uint16_t value) {
    const uint32_t unaligned = address;
    address &= ~1u;
    switch (address >> 24) {
        case 0x02: P->cycles_ += 3; P->ewram_[address & 0x3FFFF] = (uint8_t)(value); P->ewram_[(address & 0x3FFFF) + 1] = (uint8_t)(value >> 8); return;
        case 0x03: P->cycles_ += 1; P->iwram_[address & 0x7FFF] = (uint8_t)(value); P->iwram_[(address & 0x7FFF) + 1] = (uint8_t)(value >> 8); return;
        case 0x04: {
            P->cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off < 0x400) io_write16(P, off, value, 0xFFFF);
            return;
        }
        case 0x05: P->cycles_ += 1; P->pal_[address & 0x3FF] = (uint8_t)(value); P->pal_[(address & 0x3FF) + 1] = (uint8_t)(value >> 8); return;
        case 0x06: {
            P->cycles_ += 1;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            P->vram_[a] = (uint8_t)(value);
            P->vram_[a + 1] = (uint8_t)(value >> 8);
            return;
        }
        case 0x07: P->cycles_ += 1; P->oam_[address & 0x3FF] = (uint8_t)(value); P->oam_[(address & 0x3FF) + 1] = (uint8_t)(value >> 8); return;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            P->cycles_ += (uint64_t)(rom_wait(P, address, 2, false));
            if (is_eeprom(P, address)) eeprom_write(P, value);
            return;
        case 0x0E: case 0x0F:
            P->cycles_ += (uint64_t)(1 + kNWait[io16(P, 0x204) & 3]);
            save_write8(P, unaligned, (uint8_t)(value >> ((unaligned & 1) * 8)));
            return;
        default:
            P->cycles_ += 1;
            return;
    }
}
static void write32(gba_m *P, uint32_t address, uint32_t value) {
    const uint32_t unaligned = address;
    address &= ~3u;
    switch (address >> 24) {
        case 0x02: P->cycles_ += 6; memcpy(&P->ewram_[address & 0x3FFFF], &value, 4); return;
        case 0x03: P->cycles_ += 1; memcpy(&P->iwram_[address & 0x7FFF], &value, 4); return;
        case 0x04: {
            P->cycles_ += 1;
            const uint32_t off = address & 0xFFFFFF;
            if (off < 0x400) {
                io_write16(P, off, (uint16_t)(value), 0xFFFF);
                io_write16(P, off + 2, (uint16_t)(value >> 16), 0xFFFF);
            }
            return;
        }
        case 0x05: P->cycles_ += 2; memcpy(&P->pal_[address & 0x3FF], &value, 4); return;
        case 0x06: {
            P->cycles_ += 2;
            uint32_t a = address & 0x1FFFF;
            if (a >= 0x18000) a -= 0x8000;
            memcpy(&P->vram_[a], &value, 4);
            return;
        }
        case 0x07: P->cycles_ += 1; memcpy(&P->oam_[address & 0x3FF], &value, 4); return;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            P->cycles_ += (uint64_t)(rom_wait(P, address, 4, false));
            if (is_eeprom(P, address)) eeprom_write(P, (uint16_t)(value));
            return;
        case 0x0E: case 0x0F:
            P->cycles_ += (uint64_t)(1 + kNWait[io16(P, 0x204) & 3]);
            save_write8(P, unaligned, (uint8_t)(value >> ((unaligned & 3) * 8)));
            return;
        default:
            P->cycles_ += 1;
            return;
    }
}

// ---------------------------------------------------------------------------
// I/O registers
// ---------------------------------------------------------------------------
static uint16_t io_read16(gba_m *P, uint32_t off) {
    switch (off) {
        case 0x004: return P->dispstat_;
        case 0x006: return (uint16_t)(P->line_);
        case 0x100: case 0x104: case 0x108: case 0x10C:
            return (uint16_t)(P->timers_[(off - 0x100) / 4].counter);
        case 0x102: case 0x106: case 0x10A: case 0x10E:
            return P->timers_[(off - 0x102) / 4].control;
        case 0x130: return P->keyinput_;
        case 0x200: return P->ie_;
        case 0x202: return P->if_;
        case 0x208: return P->ime_;
        case 0x0BA: case 0x0C6: case 0x0D2: case 0x0DE:
            return P->dma_[(off - 0xBA) / 12].control;
        case 0x0B8: case 0x0C4: case 0x0D0: case 0x0DC:
            return 0;  // DMA word count is write-only
        default:
            break;
    }
    if (off >= 0x060 && off < 0x0B0) {
        return (uint16_t)(gba_apu_read8(&P->apu_, off - 0x60) | (gba_apu_read8(&P->apu_, off - 0x60 + 1) << 8));
    }
    if (off >= 0x0B0 && off < 0x0E0) return 0;  // DMA addresses: write-only
    return io16(P, off);
}
static void io_write16(gba_m *P, uint32_t off, uint16_t value, uint16_t mask) {
    const uint16_t merged = (uint16_t)((io16(P, off) & ~mask) | (value & mask));
    if (off >= 0x060 && off < 0x0B0) {
        if (mask & 0x00FF) gba_apu_write8(&P->apu_, off - 0x60, (uint8_t)(value));
        if (mask & 0xFF00) gba_apu_write8(&P->apu_, off - 0x60 + 1, (uint8_t)(value >> 8));
        set_io16(P, off, merged);
        return;
    }
    switch (off) {
        case 0x004:
            P->dispstat_ = (uint16_t)((P->dispstat_ & 0x0007) | (merged & 0xFF38));
            return;
        case 0x006:
            return;  // VCOUNT is read-only
        case 0x028: case 0x02A: case 0x02C: case 0x02E:
            set_io16(P, off, merged);
            gba_ppu_latch_reference(&P->ppu_, 0);
            return;
        case 0x038: case 0x03A: case 0x03C: case 0x03E:
            set_io16(P, off, merged);
            gba_ppu_latch_reference(&P->ppu_, 1);
            return;
        case 0x100: case 0x104: case 0x108: case 0x10C:
            P->timers_[(off - 0x100) / 4].reload = merged;
            set_io16(P, off, merged);
            return;
        case 0x102: case 0x106: case 0x10A: case 0x10E: {
            gba_timer *t = &P->timers_[(off - 0x102) / 4];
            const uint16_t old = t->control;
            t->control = merged & 0x00C7;
            if (!(old & 0x80) && (t->control & 0x80)) {
                t->counter = t->reload;
                t->sub = 0;
            }
            return;
        }
        case 0x0BA: case 0x0C6: case 0x0D2: case 0x0DE:
            dma_write_control(P, (int)((off - 0xBA) / 12), merged);
            return;
        case 0x130:
            return;  // KEYINPUT is read-only
        case 0x132:
            set_io16(P, off, merged);
            check_keypad_irq(P);
            return;
        case 0x200:
            P->ie_ = merged & 0x3FFF;
            update_irq(P);
            return;
        case 0x202:
            P->if_ = (uint16_t)(P->if_ & ~(value & mask));  // write 1 to acknowledge
            update_irq(P);
            return;
        case 0x208:
            P->ime_ = merged & 1;
            update_irq(P);
            return;
        case 0x300:
            set_io16(P, off, (uint16_t)(merged & 0x00FF));
            if (mask & 0xFF00) P->halted_ = true;  // HALTCNT (bit 7 = stop, treated as halt)
            return;
        default:
            break;
    }
    if (off >= 0x0B0 && off < 0x0E0) {
        set_io16(P, off, merged);
        const int n = (int)((off - 0xB0) / 12);
        const uint32_t base = 0xB0 + (uint32_t)(n) * 12;
        gba_dmach *d = &P->dma_[(size_t)(n)];
        d->src = (uint32_t)(io16(P, base)) | ((uint32_t)(io16(P, base + 2)) << 16);
        d->dst = (uint32_t)(io16(P, base + 4)) | ((uint32_t)(io16(P, base + 6)) << 16);
        d->count = io16(P, base + 8);
        return;
    }
    set_io16(P, off, merged);
}
static void raise_irq(gba_m *P, int bit) {
    P->if_ = (uint16_t)(P->if_ | (1u << bit));
    update_irq(P);
}
static void update_irq(gba_m *P) {
    arm7_set_irq(&P->cpu_, (P->ime_ & 1) && (P->ie_ & P->if_ & 0x3FFF));
}
static void check_keypad_irq(gba_m *P) {
    const uint16_t cnt = io16(P, 0x132);
    if (!(cnt & 0x4000)) return;
    const uint16_t pressed = (uint16_t)(~P->keyinput_ & 0x3FF);
    const uint16_t select = cnt & 0x3FF;
    const bool hit = (cnt & 0x8000) ? (pressed & select) == select && select : (pressed & select) != 0;
    if (hit) raise_irq(P, kIrqKeypad);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
static void timer_overflow(gba_m *P, int t) {
    gba_timer *tm = &P->timers_[(size_t)(t)];
    tm->counter = tm->reload;
    if (tm->control & 0x40) raise_irq(P, kIrqTimer0 + t);
    if (t < 2) gba_apu_timer_overflow(&P->apu_, t);
    if (t < 3) {
        gba_timer *next = &P->timers_[(size_t)(t + 1)];
        if ((next->control & 0x80) && (next->control & 0x04)) {
            if (++next->counter > 0xFFFF) timer_overflow(P, t + 1);
        }
    }
}
static void tick_timers(gba_m *P, uint64_t cycles) {
    for (int t = 0; t < 4; t++) {
        gba_timer *tm = &P->timers_[(size_t)(t)];
        if (!(tm->control & 0x80)) continue;
        if (t > 0 && (tm->control & 0x04)) continue;  // count-up: driven by the previous timer
        const uint32_t prescale = kTimerPrescale[tm->control & 3];
        uint64_t total = tm->sub + cycles;
        uint64_t inc = total / prescale;
        tm->sub = (uint32_t)(total % prescale);
        while (inc > 0) {
            const uint64_t room = 0x10000u - tm->counter;
            if (inc >= room) {
                inc -= room;
                timer_overflow(P, t);
            } else {
                tm->counter += (uint32_t)(inc);
                inc = 0;
            }
        }
    }
}
static uint64_t cycles_to_timer_event(gba_m *P) {
    uint64_t best = UINT64_MAX;
    for (int t = 0; t < 4; t++) {
        const gba_timer *tm = &P->timers_[(size_t)(t)];
        if (!(tm->control & 0x80)) continue;
        if (t > 0 && (tm->control & 0x04)) continue;
        const uint64_t prescale = kTimerPrescale[tm->control & 3];
        const uint64_t c = (0x10000u - tm->counter) * prescale - tm->sub;
        best = MIN(best, MAX(c, 1));
    }
    return best;
}

// ---------------------------------------------------------------------------
// DMA
// ---------------------------------------------------------------------------
static void dma_write_control(gba_m *P, int n, uint16_t value) {
    gba_dmach *d = &P->dma_[(size_t)(n)];
    const uint16_t old = d->control;
    d->control = value & (n == 3 ? 0xFFE0 : 0xF7E0);
    set_io16(P, 0xBA + (uint32_t)(n) * 12, d->control);
    if ((old & 0x8000) || !(d->control & 0x8000)) return;
    const uint32_t src_mask = n == 0 ? 0x07FFFFFFu : 0x0FFFFFFFu;
    const uint32_t dst_mask = n == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu;
    d->isrc = d->src & src_mask;
    d->idst = d->dst & dst_mask;
    const uint32_t count = n == 3 ? d->count : (d->count & 0x3FFFu);
    d->icount = count ? count : (n == 3 ? 0x10000u : 0x4000u);
    if (((d->control >> 12) & 3) == 0) run_dma(P, n);
}
static void dma_trigger(gba_m *P, int timing) {
    for (int n = 0; n < 4; n++) {
        const gba_dmach *d = &P->dma_[(size_t)(n)];
        if ((d->control & 0x8000) && ((d->control >> 12) & 3) == timing) run_dma(P, n);
    }
}
static void run_dma(gba_m *P, int n) {
    gba_dmach *d = &P->dma_[(size_t)(n)];
    const int timing = (d->control >> 12) & 3;
    const bool fifo = timing == 3 && (n == 1 || n == 2);
    const bool word = fifo || (d->control & 0x0400);
    const int dst_ctl = (d->control >> 5) & 3;
    const int src_ctl = (d->control >> 7) & 3;
    const int size = word ? 4 : 2;
    const int32_t src_step = src_ctl == 0 ? size : src_ctl == 1 ? -size : 0;
    const int32_t dst_step = fifo ? 0 : (dst_ctl == 0 || dst_ctl == 3) ? size : dst_ctl == 1 ? -size : 0;
    const uint32_t count = fifo ? 4 : d->icount;
    if (n == 3 && P->save_type_ == SAVE_Eeprom && (is_eeprom(P, d->idst) || is_eeprom(P, d->isrc))) {
        // 6-bit (512 B) or 14-bit (8 KiB) EEPROM, from the bit-stream length.
        if (count == 9 || count == 73) P->eeprom_addr_bits_ = 6;
        else if (count == 17 || count == 81) P->eeprom_addr_bits_ = 14;
    }
    P->cycles_ += 2;
    uint32_t src = d->isrc, dst = d->idst;
    for (uint32_t i = 0; i < count; i++) {
        if (word) write32(P, dst & ~3u, read32(P, src & ~3u, false));
        else write16(P, dst & ~1u, read16(P, src & ~1u, false));
        src += (uint32_t)(src_step);
        dst += (uint32_t)(dst_step);
    }
    d->isrc = src;
    d->idst = dst;
    if (d->control & 0x4000) raise_irq(P, kIrqDma0 + n);
    if ((d->control & 0x0200) && timing != 0) {
        if (!fifo) {
            const uint32_t reload = n == 3 ? d->count : (d->count & 0x3FFFu);
            d->icount = reload ? reload : (n == 3 ? 0x10000u : 0x4000u);
        }
        if (dst_ctl == 3) d->idst = d->dst & (n == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu);
    } else {
        d->control &= 0x7FFF;
        set_io16(P, 0xBA + (uint32_t)(n) * 12, d->control);
    }
}

// ---------------------------------------------------------------------------
// Cartridge saves
// ---------------------------------------------------------------------------
static uint8_t save_read8(gba_m *P, uint32_t address) {
    const uint32_t a = address & 0xFFFF;
    switch (P->save_type_) {
        case SAVE_Sram: return P->save_[a & 0x7FFF];
        case SAVE_Flash64:
        case SAVE_Flash128:
            if (P->flash_id_ && a < 2) {
                if (P->save_type_ == SAVE_Flash128) return a == 0 ? 0x62 : 0x13;  // Sanyo
                return a == 0 ? 0x32 : 0x1B;                                       // Panasonic
            }
            return P->save_[(size_t)(P->flash_bank_) * 0x10000 + a];
        default:
            return 0xFF;
    }
}
static void save_write8(gba_m *P, uint32_t address, uint8_t value) {
    const uint32_t a = address & 0xFFFF;
    if (P->save_type_ == SAVE_Sram) {
        P->save_[a & 0x7FFF] = value;
        P->save_dirty_ = true;
        return;
    }
    if (P->save_type_ != SAVE_Flash64 && P->save_type_ != SAVE_Flash128) return;
    if (P->flash_write_) {
        P->save_[(size_t)(P->flash_bank_) * 0x10000 + a] = value;
        P->flash_write_ = false;
        P->save_dirty_ = true;
        return;
    }
    if (P->flash_bank_cmd_) {
        if (a == 0) P->flash_bank_ = P->save_type_ == SAVE_Flash128 ? (value & 1) : 0;
        P->flash_bank_cmd_ = false;
        return;
    }
    if (P->flash_state_ == 0 && a == 0x5555 && value == 0xAA) {
        P->flash_state_ = 1;
    } else if (P->flash_state_ == 1 && a == 0x2AAA && value == 0x55) {
        P->flash_state_ = 2;
    } else if (P->flash_state_ == 2 && a == 0x5555) {
        switch (value) {
            case 0x90: P->flash_id_ = true; break;
            case 0xF0: P->flash_id_ = false; break;
            case 0x80: P->flash_erase_ = true; break;
            case 0x10:
                if (P->flash_erase_) {
                    memset(P->save_, 0xFF, P->save_size_);
                    P->save_dirty_ = true;
                }
                P->flash_erase_ = false;
                break;
            case 0xA0: P->flash_write_ = true; break;
            case 0xB0: P->flash_bank_cmd_ = true; break;
            default: break;
        }
        P->flash_state_ = 0;
    } else if (P->flash_state_ == 2 && value == 0x30 && P->flash_erase_) {
        const size_t base = (size_t)(P->flash_bank_) * 0x10000 + (a & 0xF000);
        memset(P->save_ + base, 0xFF, 0x1000);
        P->save_dirty_ = true;
        P->flash_erase_ = false;
        P->flash_state_ = 0;
    } else {
        if (value == 0xF0) P->flash_id_ = false;
        P->flash_state_ = 0;
    }
}
static uint16_t eeprom_read(gba_m *P) {
    if (!P->eeprom_reading_) return 1;  // ready
    const int pos = P->eeprom_read_pos_++;
    if (pos < 4) return 0;
    const int bit = pos - 4;
    if (bit >= 63) P->eeprom_reading_ = false;
    const uint32_t byte = (P->eeprom_read_addr_ * 8 + (uint32_t)(bit / 8)) & ((uint32_t)(P->save_size_) - 1);
    return (P->save_[byte] >> (7 - (bit & 7))) & 1;
}
static void eeprom_write(gba_m *P, uint16_t value) {
    if (P->eeprom_nbits_ < LEN(P->eeprom_bits_)) P->eeprom_bits_[P->eeprom_nbits_++] = (uint8_t)(value & 1);
    if (P->eeprom_nbits_ < 2) return;
    const size_t ab = (size_t)(P->eeprom_addr_bits_);
    const bool read_request = P->eeprom_bits_[0] == 1 && P->eeprom_bits_[1] == 1;
    const bool write_request = P->eeprom_bits_[0] == 1 && P->eeprom_bits_[1] == 0;
    if (!read_request && !write_request) {
        P->eeprom_nbits_ = 0;
        return;
    }
    const size_t need = read_request ? 2 + ab + 1 : 2 + ab + 64 + 1;
    if (P->eeprom_nbits_ < need) return;
    uint32_t address = 0;
    for (size_t i = 0; i < ab; i++) address = (address << 1) | P->eeprom_bits_[2 + i];
    if (ab == 14) address &= 0x3FF;
    if (read_request) {
        P->eeprom_read_addr_ = address;
        P->eeprom_reading_ = true;
        P->eeprom_read_pos_ = 0;
    } else {
        for (int byte = 0; byte < 8; byte++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) v = (uint8_t)((v << 1) | P->eeprom_bits_[2 + ab + (size_t)(byte * 8 + b)]);
            P->save_[(address * 8 + (uint32_t)(byte)) & ((uint32_t)(P->save_size_) - 1)] = v;
        }
        P->save_dirty_ = true;
        P->eeprom_reading_ = false;
    }
    P->eeprom_nbits_ = 0;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
static void tick(gba_m *P, uint64_t cycles) {
    tick_timers(P, cycles);
    P->audio_acc_ += cycles;
    while (P->audio_acc_ >= (uint64_t)(kCyclesPerSample)) {
        P->audio_acc_ -= kCyclesPerSample;
        { const int16_t smp_ = gba_apu_sample(&P->apu_); if (P->naudio_ < (int)LEN(P->audio_)) P->audio_[P->naudio_++] = smp_; }
    }
}
static void run_until(gba_m *P, uint64_t target) {
    while (P->cycles_ < target) {
        if (P->halted_) {
            if (P->ie_ & P->if_ & 0x3FFF) {
                P->halted_ = false;
                continue;
            }
            uint64_t step = target - P->cycles_;
            step = MIN(step, cycles_to_timer_event(P));
            step = MIN(step, kCyclesPerSample);
            P->cycles_ += step;
            tick(P, step);
            continue;
        }
        const uint64_t before = P->cycles_;
        arm7_step(&P->cpu_);
        if (P->cycles_ == before) P->cycles_++;
        tick(P, P->cycles_ - before);
    }
}
void gba_run_frame(gba_m *P) {
    uint64_t line_start = P->cycles_;
    for (int line = 0; line < kLines; line++) {
        P->line_ = line;
        // V-count match
        const bool match = (P->dispstat_ >> 8) == line;
        P->dispstat_ = (uint16_t)((P->dispstat_ & ~0x4) | (match ? 0x4 : 0));
        if (match && (P->dispstat_ & 0x20)) raise_irq(P, kIrqVCount);
        if (line == 0) gba_ppu_start_frame(&P->ppu_);
        if (line == kVisibleLines) {
            P->dispstat_ |= 1;
            if (P->dispstat_ & 0x08) raise_irq(P, kIrqVBlank);
            dma_trigger(P, 1);
        }
        if (line == kLines - 1) P->dispstat_ &= ~1;
        run_until(P, line_start + kHDrawCycles);
        P->dispstat_ |= 2;
        if (P->dispstat_ & 0x10) raise_irq(P, kIrqHBlank);
        if (line < kVisibleLines) {
            gba_ppu_render_line(&P->ppu_, line, P->framebuffer_ + line * kWidth);
            gba_ppu_end_line(&P->ppu_);
            dma_trigger(P, 2);
        }
        run_until(P, line_start + kCyclesPerLine);
        P->dispstat_ &= ~2;
        line_start += kCyclesPerLine;
    }
    if (P->save_dirty_ && ++P->save_timer_ >= 300) {
        P->save_timer_ = 0;
        P->save_dirty_ = false;       // (the frontend owns the .sav file)
    }
}

// ---- the console API ----
#include <stdlib.h>
#include "../common/cons.h"
#include "rom_data.h"

static const char *const kExts[] = { ".gba", ".agb", ".bin", NULL };
const cons_info cons_machine = {
    "gba", "Game Boy Advance", "Nintendo, 2001", kExts, "GBA", kWidth, kHeight,
    GBA_FPS, kSampleRate, { "A", "B", "L", "R" }, "START", "SELECT", CONS_NO_KEYS, 1, NULL,
};

static gba_m G;
static bool have_bios;
static uint16_t fb565[kWidth * kHeight];

static uint8_t b_read8(void *u, uint32_t a) { return read8((gba_m *)u, a); }
static uint16_t b_read16(void *u, uint32_t a, bool c) { return read16((gba_m *)u, a, c); }
static uint32_t b_read32(void *u, uint32_t a, bool c) { return read32((gba_m *)u, a, c); }
static void b_write8(void *u, uint32_t a, uint8_t v) { write8((gba_m *)u, a, v); }
static void b_write16(void *u, uint32_t a, uint16_t v) { write16((gba_m *)u, a, v); }
static void b_write32(void *u, uint32_t a, uint32_t v) { write32((gba_m *)u, a, v); }
static void b_idle(void *u, int cycles) { ((gba_m *)u)->cycles_ += (uint64_t)cycles; }

// sound DMA: DMA 1 / 2 in "special" timing aimed at FIFO A / B
static void request_fifo(void *u, int ch)
{
    gba_m *P = (gba_m *)u;
    for (int n = 1; n <= 2; n++) {
        const gba_dmach *d = &P->dma_[n];
        if (!(d->control & 0x8000) || ((d->control >> 12) & 3) != 3) continue;
        if ((d->dst & 0x0FFFFFFF) != 0x040000A0u + (uint32_t)ch * 4) continue;
        run_dma(P, n);
    }
}

bool cons_init(void)
{
    G.framebuffer_ = (uint32_t *)malloc(kWidth * kHeight * sizeof(uint32_t));
    if (!G.framebuffer_) return false;
    if (gba_rom_bios) {
        memcpy(G.bios_, gba_rom_bios, kBiosSize);
        have_bios = true;
    }
    G.cpu_.bus_ = (arm_bus){ b_read8, b_read16, b_read32, b_write8, b_write16, b_write32, b_idle, &G };
    arm7_init(&G.cpu_);
    G.ppu_.io_ = G.io_;
    G.ppu_.pal_ = G.pal_;
    G.ppu_.vram_ = G.vram_;
    G.ppu_.oam_ = G.oam_;
    G.apu_.request_fifo = request_fifo;
    G.apu_.user = &G;
    G.keyinput_ = 0x3FF;
    G.last_rom_address_ = 0xFFFFFFFF;
    G.eeprom_addr_bits_ = 6;
    gba_reset(&G);
    return true;
}

void cons_reset(void) { gba_reset(&G); }

bool cons_load(uint8_t *data, size_t size, const char *name, const char **why)
{
    (void)name;
    if (data && size == kBiosSize && !(size >= 0xC0 && data[0xB2] == 0x96)) {   // a BIOS (no cartridge header)
        memcpy(G.bios_, data, kBiosSize);
        free(data);
        have_bios = true;
        gba_reset(&G);
        if (why) *why = "BIOS loaded: now the cartridge";
        return false;
    }
    if (!have_bios) {
        if (why) *why = "the GBA BIOS (gba_bios.bin, 16 KB) is needed: load it first";
        free(data);
        return false;
    }
    if (!data || size < 0xC0) {
        if (why) *why = "not a Game Boy Advance cartridge (too small)";
        free(data);
        return false;
    }
    if (size > 0x2000000) size = 0x2000000;
    free(G.rom_);
    G.rom_ = data;
    G.rom_size_ = size;
    detect_save_type(&G);
    G.save_dirty_ = false;
    gba_reset(&G);
    return true;
}

void cons_eject(void)
{
    free(G.rom_);
    G.rom_ = NULL;
    G.rom_size_ = 0;
    G.save_size_ = 0;
    G.save_type_ = SAVE_None;
    gba_reset(&G);
}

bool cons_loaded(void) { return G.rom_size_ != 0; }

int cons_run_frame(const int16_t **audio)
{
    G.naudio_ = 0;
    *audio = G.audio_;
    if (G.rom_size_) gba_run_frame(&G);
    else {
        G.naudio_ = (int)(kSampleRate / GBA_FPS);
        memset(G.audio_, 0, G.naudio_ * sizeof(int16_t));
    }
    for (int i = 0; i < kWidth * kHeight; i++) fb565[i] = cons_rgb565(G.framebuffer_[i]);
    return G.naudio_;
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
    uint16_t k = 0x3FF;
    if (b & CP_B1) k &= ~0x001;              // A
    if (b & CP_B2) k &= ~0x002;              // B
    if (b & CP_SELECT) k &= ~0x004;
    if (b & CP_START) k &= ~0x008;
    if (b & CP_RIGHT) k &= ~0x010;
    if (b & CP_LEFT) k &= ~0x020;
    if (b & CP_UP) k &= ~0x040;
    if (b & CP_DOWN) k &= ~0x080;
    if (b & CP_B4) k &= ~0x100;              // R
    if (b & CP_B3) k &= ~0x200;              // L
    G.keyinput_ = k;
    check_keypad_irq(&G);
}

void cons_key(int key, bool down) { (void)key, (void)down; }
const char *cons_status(void) { return have_bios ? NULL : "no BIOS"; }
