// m68kdsp.h - Motorola 68000 (and the 68010 / 68020 bits dsp-cpp has): a C
// port of dsp-cpp's cpu/m68000 (from dsp-emulator's m68000.pas), so that the
// drivers ported from dsp-cpp keep its timing. Word handlers (the byte ones
// optional: without them a byte write is a read-modify-write of its word),
// optional direct pages for the ROM / RAM, the cycle callback after every
// instruction. (cpu/m68k stays Musashi.)
#ifndef M68KDSP_H
#define M68KDSP_H
#include <stdbool.h>
#include <stdint.h>

enum { M68_CLEAR = 0, M68_ASSERT, M68_HOLD, M68_PULSE };
enum { M68K_T_68000, M68K_T_68010, M68K_T_68020 };
#define M68KD_DIRECT_PAGES 256u

typedef struct { uint32_t l; } m68r;
typedef struct { bool t, s, x, n, z, v, c; uint8_t im; } m68flags;

typedef struct m68kd {
    uint16_t (*read_word)(void *user, uint32_t addr);
    void (*write_word)(void *user, uint32_t addr, uint16_t v);
    uint8_t (*read_byte)(void *user, uint32_t addr);         // (optional)
    void (*write_byte)(void *user, uint32_t addr, uint8_t v);  // (optional)
    void (*cycles)(void *user, int n);                         // (optional)
    void (*reset_instr)(void *user);                           // RESET (optional)
    int (*irq_ack)(void *user, int level);                     // a vector, or < 0: the autovector (optional)
    void (*exception_cb)(void *user, uint32_t vector, uint32_t pc);   // (optional)
    void (*cmpild)(void *user, uint8_t reg, uint32_t imm);           // CMPI.L #imm, Dn (FD1094; optional)
    void (*rte_cb)(void *user);                                      // RTE (FD1094; optional)
    void (*irq_taken_cb)(void *user, int level);                     // an interrupt taken (FD1094; optional)
    void *user;
    m68r d[8], a[8], pc, ppc, other_sp;
    m68flags cc;
    uint32_t clock, address_mask, vbr, exceptions, pq_addr, ea;
    uint8_t type;
    bool opcode, prefetch, halted, stopped;
    uint16_t pq_val[2];
    int pq_count, cycles_;
    uint8_t irq[8], irq_lines, reset_request, halt_request;
    const uint8_t *direct_read[M68KD_DIRECT_PAGES];
    uint8_t *direct_write[M68KD_DIRECT_PAGES];
    const uint8_t *direct_odd[M68KD_DIRECT_PAGES];
} m68kd;

void m68kd_init(m68kd *mk, uint32_t clock, int type);
void m68kd_reset(m68kd *mk);                    // (the handlers set first: it reads the vectors)
int m68kd_run(m68kd *mk, int cycles);
void m68kd_set_irq(m68kd *mk, int level, int state);
static inline void m68kd_set_reset_line(m68kd *mk, int state) { mk->reset_request = (uint8_t)state; }
static inline void m68kd_set_halt_line(m68kd *mk, int state) { mk->halt_request = (uint8_t)state; }
static inline void m68kd_set_address_mask(m68kd *mk, uint32_t mask) { mk->address_mask = mask & ~1u; }
static inline void m68kd_set_prefetch(m68kd *mk, bool on) { mk->prefetch = on; mk->pq_count = 0; }
void m68kd_set_direct_page(m68kd *mk, uint32_t page, const uint8_t *read, uint8_t *write);
void m68kd_set_direct_page_pair(m68kd *mk, uint32_t page, const uint8_t *even, const uint8_t *odd);

#endif
