// fd1094d.h - the Hitachi FD1094 encrypted 68000: a C port of dsp-cpp's
// machine/fd1094 (MAME's fd1094.cpp). The opcodes are decrypted per state
// (the CMPI.L #$xxxxFFFF,D0 the program runs, the IRQ / RTE switches), a
// whole ROM image per state, cached.
#ifndef FD1094D_H
#define FD1094D_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD1094_KEY_SIZE 0x2000
#define FD1094_STATE_RESET 0x0100
#define FD1094_STATE_IRQ 0x0200
#define FD1094_STATE_RTE 0x0300

typedef struct {
    uint8_t key[FD1094_KEY_SIZE];
    bool key_ready, irq_mode;
    uint8_t state;
    uint16_t *cache[256];
    const uint16_t *cache_src;
    uint32_t cache_bytes;
} fd1094d;

void fd1094d_set_key(fd1094d *f, const uint8_t *key, size_t size);
void fd1094d_reset(fd1094d *f);
void fd1094d_change_state(fd1094d *f, int newstate);
void fd1094d_on_cmpild(fd1094d *f, uint8_t reg, uint32_t data);
static inline void fd1094d_on_irq(fd1094d *f) { fd1094d_change_state(f, FD1094_STATE_IRQ); }
static inline void fd1094d_on_rte(fd1094d *f) { fd1094d_change_state(f, FD1094_STATE_RTE); }
static inline uint8_t fd1094d_state(const fd1094d *f) { return f->irq_mode ? f->key[0] : f->state; }
uint16_t fd1094_decrypt_one(uint32_t word_address, uint16_t val, const uint8_t *main_key, uint8_t state, bool vector_fetch);
// The decrypted opcodes of `src` (`bytes` long) for the current state
const uint16_t *fd1094d_opcodes(fd1094d *f, const uint16_t *src, uint32_t bytes);

#endif
