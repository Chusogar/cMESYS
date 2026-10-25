// sega_315_5195.h - Sega 315-5195 memory mapper (System 16B, Out Run, X-Board...),
// as MAME's sega/315_5195.cpp. Eight regions, each placed by two registers
// (size, base); the driver says what each region holds (RAM, ROM, handlers)
// and the mapper turns it into a table of pages: the driver's bus looks
// up the page, gets the mapping and the offset in it. Anything left unmapped
// reaches the mapper's own registers (low byte of each word, every 32 bytes).
#ifndef SEGA_315_5195_H
#define SEGA_315_5195_H
#include <stdint.h>
#include <stdbool.h>

#define S16M_REGS   0       // kind: the mapper registers (map[0])
#define S16M_NONE   1       // kind: nothing, e.g. writes to ROM (map[1]); driver kinds start at 2
#define S16M_MAX    32
// Page size of the lookup tables: 64 KB (every Out Run / System 16 mapping
// fills whole 64 KB pages with its mirrors); 12 for 4 KB pages.
#ifndef S16M_PAGE_SHIFT
#define S16M_PAGE_SHIFT 16
#endif
#define S16M_PAGES  (1 << (24 - S16M_PAGE_SHIFT))

typedef struct {
    uint8_t kind;           // driver's own handler code
    uint32_t start, mirror; // covers addresses a with (a & ~mirror) in [start, end]
    uint8_t *mem;           // RAM / ROM behind it (NULL for handlers)
} s16m_map;

typedef struct sega_315_5195 sega_315_5195;
struct sega_315_5195 {
    uint8_t regs[0x20];
    uint8_t to_sound, from_sound;
    bool pbf;                       // a byte for the sound CPU is waiting
    uint8_t rpage[S16M_PAGES], wpage[S16M_PAGES];   // index in map[] of each page
    s16m_map map[S16M_MAX];
    int nmaps, cur;
    // driver callbacks
    void *user;
    void (*mapper)(void *user, sega_315_5195 *m, int index);    // fill region `index`
    void (*sound_w)(void *user, uint8_t data);                  // register 3 written (before PBF is set)
    void (*reset_w)(void *user, bool assert);                    // register 2: CPU reset
    void (*irq_w)(void *user, int level);                        // register 4: IRQ (-1 none)
    uint16_t (*bus_r)(void *user, uint32_t addr);                // register 5: copies
    void (*bus_w)(void *user, uint32_t addr, uint16_t data);
};

void s16m_reset(sega_315_5195 *m);
uint8_t s16m_read(sega_315_5195 *m, uint32_t word_offset);
void s16m_write(sega_315_5195 *m, uint32_t word_offset, uint8_t data);
// Sound CPU side: reads the byte from the main CPU (clears PBF).
static inline uint8_t s16m_pread(sega_315_5195 *m) { m->pbf = false; return m->to_sound; }

// For the mapper callback (region m->cur). `kind` 0 is reserved.
void s16m_map_ram(sega_315_5195 *m, uint32_t offset, uint32_t length, uint32_t mirror, uint8_t *mem,
                  uint8_t kind_r, uint8_t kind_w);
// ROM of rom_size bytes from offset 0 of the region; writes go to kind_w
// (S16M_NONE: ignored).
void s16m_map_rom(sega_315_5195 *m, uint32_t offset, uint32_t length, uint32_t mirror, const uint8_t *rom,
                  uint32_t rom_size, uint8_t kind_r, uint8_t kind_w);
void s16m_map_handler(sega_315_5195 *m, uint32_t offset, uint32_t length, uint32_t mirror,
                      uint8_t kind_r, uint8_t kind_w);

#endif
