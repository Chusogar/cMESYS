// deco146d.h - the Data East 146 / 104 protection chips: a C port of
// dsp-cpp's machine/deco146 (from dsp-emulator's deco_146.pas / deco_104.pas).
// 0x400 read ports, each a scrambled (bit reordered, optionally XORed and
// NANDed) copy of a word the CPU wrote, or of one of three input ports.
#ifndef DECO146D_H
#define DECO146D_H
#include <stdbool.h>
#include <stdint.h>

enum { DECO146, DECO104 };
#define DECO146_SCRAMBLE_REVERSE 1
#define DECO146_MAGIC_ADDRESS_XOR 2
#define DECO146_SCRAMBLE_INTERLEAVE 4

typedef struct {
    int write_offset;                           // -1, -2, -3: input ports A, B, C
    uint8_t mapping[16];
    bool use_xor, use_nand;
} deco146_entry;

typedef struct {
    uint16_t (*port_a)(void *user), (*port_b)(void *user), (*port_c)(void *user);   // (optional: 0xffff)
    void (*soundlatch)(void *user, uint8_t v);  // (optional)
    void *user;
    const deco146_entry *table;
    uint8_t bankswitch_swap_read_address, xor_port, mask_port, soundlatch_port;
    uint16_t magic_read_address_xor;
    bool magic_read_address_xor_enabled, latchflag;
    uint8_t addrswap[10], region_selects[6], current_rambank;
    uint16_t nand, xor_, latchaddr, latchdata;
    uint16_t rambank0[0x80], rambank1[0x80];
} deco146d;

void deco146d_init(deco146d *d, int type, int props);   // (clears the callbacks)
void deco146d_reset(deco146d *d);
uint16_t deco146d_read(deco146d *d, uint16_t address);
// mem_mask: the bytes written (0xff00 / 0x00ff for byte accesses)
void deco146d_write(deco146d *d, uint16_t address, uint16_t data, uint16_t mem_mask);

#endif
