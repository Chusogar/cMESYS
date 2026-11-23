// sega_decrypt.h - Sega's early-80s Z80 opcode scramblers: two views of the
// same ROM, `opcodes` for the M1 fetches and `data` for every other read.
// C port of dsp-cpp's machine/sega_decrypt (tables from MAME's segacrpt).
#ifndef SEGA_DECRYPT_H
#define SEGA_DECRYPT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// System 1's scrambler, per game
enum { SEGA_PITFALL2, SEGA_TEDDYBOY, SEGA_PENGO, SEGA_MRVIKING, SEGA_SEGANINJA, SEGA_UPNDOWN, SEGA_FLICKY,
       SEGA_SZAXXON, SEGA_FUTSPY };
// the type 2 chips
enum { SEGA2_315_5179, SEGA2_315_5177, SEGA2_317_000X };

// `rom` (size bytes; may be `data` itself) into data and opcodes (size bytes each)
void sega_decrypt(const uint8_t *rom, size_t size, int game, uint8_t *data, uint8_t *opcodes);
void sega_decrypt_type2(const uint8_t *rom, size_t size, int chip, int shift, uint8_t *data, uint8_t *opcodes);
// the same for the bytes at addr..addr + size - 1 (a part at a time)
void sega_decrypt_type2_part(const uint8_t *rom, size_t size, uint32_t addr, int chip, int shift, uint8_t *data,
                             uint8_t *opcodes);
#endif
