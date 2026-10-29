// namco5x.h - Namco 51xx / 52xx / 53xx / 54xx custom chips: MB8843/MB8844
// MCUs with their port wiring. C port of dsp-cpp's machine/namco5{1,2,3,4}
// (after MAME's namco51.cpp ... namco54.cpp). Each runs its ROM (1 KB, read
// in place) on cpu/mb88xx.
#ifndef NAMCO5X_H
#define NAMCO5X_H
#include <stdint.h>
#include <stdbool.h>
#include "mb88xx/mb88xx.h"

// 51xx: I/O and coins. in(user, port 0-3) gives the 4-bit input ports.
typedef struct {
    mb88 cpu;
    uint8_t port_o, rw;
    uint8_t (*in)(void *user, int port);
    void *user;
} namco51;
void namco51_init(namco51 *n, const uint8_t *rom);
void namco51_reset(namco51 *n);
static inline void namco51_set_reset(namco51 *n, bool running) { mb88_set_reset_line(&n->cpu, !running); }
static inline void namco51_chip_select(namco51 *n, bool on) { mb88_set_irq(&n->cpu, on); }
static inline void namco51_set_rw(namco51 *n, bool read) { n->rw = read ? 1 : 0; }
static inline void namco51_write(namco51 *n, uint8_t d) { n->port_o = d; }
static inline uint8_t namco51_read(const namco51 *n) { return n->port_o; }
static inline void namco51_vblank(namco51 *n, bool state) { mb88_set_tc(&n->cpu, !state); }

// 52xx: sample player; P0-P3 is a 4-bit DAC. rom_read(user, address): the samples.
typedef struct {
    mb88 cpu;
    uint8_t latched_cmd, dac;
    uint16_t address;
    uint8_t (*rom_read)(void *user, uint16_t address);
    void *user;
} namco52;
void namco52_init(namco52 *n, const uint8_t *rom);
void namco52_reset(namco52 *n);
static inline void namco52_set_reset(namco52 *n, bool running) { mb88_set_reset_line(&n->cpu, !running); }
static inline void namco52_chip_select(namco52 *n, bool on) { mb88_set_irq(&n->cpu, on); }
static inline void namco52_write(namco52 *n, uint8_t d) { n->latched_cmd = d; }

// 53xx: I/O (steering and DIP switches on Pole Position).
typedef struct {
    mb88 cpu;
    uint8_t port_o;
    uint8_t (*in)(void *user, int port);
    void *user;
} namco53;
void namco53_init(namco53 *n, const uint8_t *rom);
void namco53_reset(namco53 *n);
static inline void namco53_set_reset(namco53 *n, bool running) { mb88_set_reset_line(&n->cpu, !running); }
static inline void namco53_chip_select(namco53 *n, bool on) { mb88_set_irq(&n->cpu, on); }
static inline uint8_t namco53_read(const namco53 *n) { return n->port_o; }

// 54xx: noise / tone generator, three 4-bit DACs.
typedef struct {
    mb88 cpu;
    uint8_t latched_cmd, chanl1, chanl2, chanl3;
} namco54;
void namco54_init(namco54 *n, const uint8_t *rom);
void namco54_reset(namco54 *n);
static inline void namco54_set_reset(namco54 *n, bool running) { mb88_set_reset_line(&n->cpu, !running); }
static inline void namco54_chip_select(namco54 *n, bool on) { mb88_set_irq(&n->cpu, on); }
static inline void namco54_write(namco54 *n, uint8_t d) { n->latched_cmd = d; }

#endif
