// tms7000.h - Texas Instruments TMS7000 family (8-bit microcontroller): C
// port of dsp-cpp's cpu/tms7000 (after MAME's tms7000 core, hap / Tim
// Lindner). The internal RAM (128 / 256 bytes) and peripheral file (I/O
// control, the two timers, ports A-D) are inside; the internal mask ROM is
// the caller's. The EXL-100 / EXELTEL parts replace SWAP R ($D7) with LVDP,
// a byte read from the TMS3556 VRAM port.
#ifndef TMS7000_H
#define TMS7000_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TMS7000_CHIP = 0, TMS7020, TMS7040, TMS7041, TMS7042 };
enum { TMS7000_INT1 = 0, TMS7000_INT3 = 1 };
enum { TMS7000_PORTA = 0, TMS7000_PORTB, TMS7000_PORTC, TMS7000_PORTD };
// set_input_line: as MAME's lines (HOLD / PULSE stay until taken)
enum { TMS7000_CLEAR = 0, TMS7000_ASSERT, TMS7000_HOLD, TMS7000_PULSE };
enum { TMS7000_SR_C = 0x80, TMS7000_SR_N = 0x40, TMS7000_SR_Z = 0x20, TMS7000_SR_I = 0x10 };

typedef struct tms7000 {
    // memory outside the chip, the ports, and the cycles after each instruction
    uint8_t (*read)(void *user, uint16_t addr);
    void (*write)(void *user, uint16_t addr, uint8_t v);
    uint8_t (*port_in)(void *user, int port);
    void (*port_out)(void *user, int port, uint8_t v);
    void (*cycles)(void *user, int n);
    void *user;

    uint32_t clock;
    unsigned divider;
    bool family_70x2, exl_lvdp;
    uint16_t rom_base;
    const uint8_t *rom;
    size_t rom_size;
    int ram_size;
    uint8_t ram[256];

    int icount;
    uint16_t pc;
    uint8_t sp, sr;
    bool idle_state;
    bool irq_state[2], irq_hold[2];
    uint8_t io_control[3];
    uint8_t port_latch[4], port_ddr[4];
    uint8_t timer_data[2], timer_control[2];
    int timer_decrementer[2], timer_prescaler[2];
    uint16_t timer_capture_latch[2];
    int timer_crystal_acc[2];
} tms7000;

// The callbacks and user are set by the caller first
void tms7000_init(tms7000 *t, uint32_t clock, int chip, unsigned divider);
// The internal mask ROM (kept by pointer: 2 KB at $F800 or 4 KB at $F000)
static inline void tms7000_set_rom(tms7000 *t, const uint8_t *rom, size_t size) { t->rom = rom; t->rom_size = size; }
void tms7000_reset(tms7000 *t);
int tms7000_run(tms7000 *t, int cycles);
void tms7000_set_input_line(tms7000 *t, int line, int state);
static inline uint32_t tms7000_cpu_clock(const tms7000 *t) { return t->clock / (t->divider ? t->divider : 1); }
static inline uint8_t tms7000_ram_at(const tms7000 *t, uint8_t a) { return a < t->ram_size ? t->ram[a] : 0; }
static inline uint8_t tms7000_a(const tms7000 *t) { return t->ram[0]; }
static inline void tms7000_set_a(tms7000 *t, uint8_t v) { t->ram[0] = v; }
#endif
