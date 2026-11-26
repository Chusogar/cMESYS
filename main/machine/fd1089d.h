// fd1089d.h - the Hitachi FD1089A / B 68000 encryption: a C port of
// dsp-cpp's machine/fd1089 (fd1089.pas, MAME's algorithm).
#ifndef FD1089D_H
#define FD1089D_H
#include <stdint.h>

enum { FD1089_A, FD1089_B };
// The `bytes` of program as big-endian words -> the opcode and data views.
void fd1089_decrypt(const uint16_t *src, uint16_t *opcodes, uint16_t *data, uint32_t bytes, const uint8_t *key, int type);

#endif
