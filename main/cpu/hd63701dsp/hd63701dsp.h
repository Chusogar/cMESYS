// hd63701dsp.h - Hitachi HD63701 / Motorola 6803: C port of dsp-cpp's
// cpu/hd63701 (from dsp-emulator's m680x.pas), for the arcade MCUs and CPUs
// ported from dsp-cpp.
#ifndef HD63701DSP_H
#define HD63701DSP_H
#include <stdbool.h>
#include <stdint.h>

enum { HD_CLEAR = 0, HD_ASSERT, HD_HOLD, HD_PULSE };
enum { HD_HD63701Y = 0, HD_HD63701V, HD_M6803 };

typedef struct hd63701 {
    // memory beyond the chip's own (handlers set after hd63701_init)
    uint8_t (*read_mem)(void *user, uint16_t addr);
    void (*write_mem)(void *user, uint16_t addr, uint8_t v);
    uint8_t (*port_read[4])(void *user);
    void (*port_write[4])(void *user, uint8_t v);
    uint8_t (*portx_read[2])(void *user);
    void (*portx_write[2])(void *user, uint8_t v);
    void (*cycles)(void *user, int cycles);
    void *user;
    uint8_t a, b;
    uint16_t x, sp;
    struct { bool h, i, n, z, v, c; } cc;
    uint32_t clock;
    int type;
    uint16_t pc;
    uint8_t rom[0x4000], internal_ram[0x200];
    uint8_t port_ddr[4], port_in[4], port_out[4], portx_ddr[2], portx_in[2], portx_out[2];
    uint8_t ram_control, trcsr, tdr, tcsr, latch09, pending_tcsr;
    bool trcsr_read;
    uint32_t counter, output_compare, timer_next;
    int irq_state, nmi_request, nmi_state, halt_state, reset_state;
    int extra_cycles;
    uint16_t address, operand_word;
    uint8_t operand;
} hd63701;

void hd63701_init(hd63701 *m, uint32_t clock, int type);
void hd63701_reset(hd63701 *m);          // the ROM and handlers set first
int hd63701_run(hd63701 *m, int cycles);
static inline void hd63701_set_irq(hd63701 *m, int state) { m->irq_state = state; }
void hd63701_set_nmi(hd63701 *m, int state);
static inline void hd63701_set_reset(hd63701 *m, int state) { m->reset_state = state; }
static inline void hd63701_set_halt(hd63701 *m, int state) { m->halt_state = state; }
#endif
