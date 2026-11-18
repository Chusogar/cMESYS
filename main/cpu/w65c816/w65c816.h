// w65c816.h - WDC 65C816 (the SNES's 5A22 core): a C11 port of dsp-cpp's
// cpu/w65c816, with its cycle counts (the driver adds the memory speeds).
#ifndef W65C816_H
#define W65C816_H
#include <stdbool.h>
#include <stdint.h>

#ifndef IRQ_CLEAR
enum { IRQ_CLEAR = 0, IRQ_ASSERT, IRQ_HOLD, IRQ_PULSE };   // IrqLine
#endif

typedef struct { bool n, v, z, c, i, d, m, x; } w65c816_flags;

typedef struct {
    uint8_t (*read)(void *user, uint32_t addr);           // 24-bit
    void (*write)(void *user, uint32_t addr, uint8_t v);
    void (*cycles)(void *user, int n);                    // after every instruction (optional)
    uint32_t (*vector_handler)(void *user, uint32_t v);   // (optional)
    void (*wdm)(void *user, uint8_t signature);           // WDM $42 xx (optional: the IIGS's SmartPort)
    void *user;
    uint16_t a, x, y, sp, d;
    uint8_t pbr, dbr;
    w65c816_flags p;
    // private
    bool e_;
    uint16_t pc_;
    uint32_t clock_;
    int irq_request_, nmi_request_;
    bool nmi_latched_, stopped_, waiting_, end_slice_;
    int cycles_;
    bool bank0_mode_;
} w65c816;

void w65c816_init(w65c816 *P, uint32_t clock);   // the power-on registers (handlers kept)
void w65c816_reset(w65c816 *P);
int w65c816_run(w65c816 *P, int cycles);
int w65c816_step(w65c816 *P);
void w65c816_set_nmi(w65c816 *P, int state);
void w65c816_set_emulation(w65c816 *P, bool e);
uint8_t w65c816_get_p(w65c816 *P);
void w65c816_set_p(w65c816 *P, uint8_t value);
static inline void w65c816_set_irq(w65c816 *P, int state) { P->irq_request_ = state; }
static inline void w65c816_end_timeslice(w65c816 *P) { P->end_slice_ = true; }
static inline uint32_t w65c816_pc(const w65c816 *P) { return ((uint32_t)P->pbr << 16) | P->pc_; }
static inline bool w65c816_waiting(const w65c816 *P) { return P->waiting_; }
static inline bool w65c816_stopped(const w65c816 *P) { return P->stopped_; }
static inline void w65c816_clear_halt(w65c816 *P) { P->stopped_ = P->waiting_ = false; }
#endif
