// deco_cryptd.h - Data East's ROM encryption: a C port of dsp-cpp's
// machine/deco_crypt (from dsp-emulator's deco_decr.pas; MAME decocrpt.cpp and
// deco102.cpp).
#ifndef DECO_CRYPTD_H
#define DECO_CRYPTD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// DECO 56 / 74 graphics: each big-endian word moved within its 4 KB block,
// XORed and bit swapped. The remap is the DECO 56 address shuffle alone.
void deco56_decrypt_gfx(uint8_t *data, size_t length);
void deco74_decrypt_gfx(uint8_t *data, size_t length);
void deco56_remap_gfx(uint8_t *data, size_t length);
// DECO 102 68000 program (`words` words, the high byte the even address):
// writes the separate data and opcode views.
void deco102_decrypt_cpu(const uint16_t *rom, uint16_t *opcodes, uint16_t *data, size_t words, uint16_t address_xor,
                         uint16_t data_select_xor, uint16_t opcode_select_xor);

#endif
