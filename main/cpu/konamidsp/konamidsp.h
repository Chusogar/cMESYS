// konamidsp.h - Konami-1 (052001) CPU: a C port of dsp-cpp's cpu/konami
// (from dsp-emulator's konami.pas): the 6809's registers with Konami's own
// opcode map and indexing, the block moves and the SETLINES output. The
// clock given is the chip input (it runs at a quarter of it from 4 MHz up).
#ifndef KONAMIDSP_H
#define KONAMIDSP_H
#include <stdbool.h>
#include <stdint.h>

enum { KC_CLEAR = 0, KC_ASSERT, KC_HOLD, KC_PULSE };

typedef struct { bool e, f, h, i, n, z, v, c; } konamid_flags;

typedef struct konamid {
    uint8_t (*rd)(void *user, uint16_t addr);
    void (*wr)(void *user, uint16_t addr, uint8_t v);
    void (*cycles)(void *user, int n);          // after every instruction (optional)
    void (*set_lines)(void *user, uint8_t v);   // SETLINES (optional)
    void *user;
    uint8_t a, b, dp;
    uint16_t x, y, u, s, pc;
    konamid_flags cc;
    int extra_cycles;
    uint8_t irq_state, firq_state, nmi_state, nmi_latched;
} konamid;

void konamid_reset(konamid *kc);                // (the handlers set first)
int konamid_run(konamid *kc, int cycles);
static inline void konamid_set_irq(konamid *kc, int state) { kc->irq_state = (uint8_t)state; }
static inline void konamid_set_firq(konamid *kc, int state) { kc->firq_state = (uint8_t)state; }
void konamid_set_nmi(konamid *kc, int state);

#endif
