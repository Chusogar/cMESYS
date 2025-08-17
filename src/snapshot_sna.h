#pragma once
#include <cstdint>
#include <vector>
#include "z80.h"
#include "memory.h"

// Load a 48K SNA snapshot into CPU and memory
// Returns true on success
inline bool loadSNA48(const std::vector<uint8_t> &buf, Z80Cpu &cpu, Memory &mem) {
	// 48K SNA is 27-byte header + 49152 bytes RAM
	if (buf.size() < 27 + 49152) return false;
	const uint8_t *p = buf.data();
	// Order: I, HL', DE', BC', AF', HL, DE, BC, IY, IX, IFF2, R, AF, SP, IM, Border
	cpu.i = p[0];
	cpu.l2 = p[1]; cpu.h2 = p[2];
	cpu.e2 = p[3]; cpu.d2 = p[4];
	cpu.c2 = p[5]; cpu.b2 = p[6];
	cpu.f2 = p[7]; cpu.a2 = p[8];
	cpu.l = p[9]; cpu.h = p[10];
	cpu.e = p[11]; cpu.d = p[12];
	cpu.c = p[13]; cpu.b = p[14];
	cpu.iy = uint16_t(p[15]) | (uint16_t(p[16]) << 8);
	cpu.ix = uint16_t(p[17]) | (uint16_t(p[18]) << 8);
	cpu.iff2 = (p[19] != 0);
	cpu.r = p[20];
	cpu.f = p[21]; cpu.a = p[22];
	cpu.sp = uint16_t(p[23]) | (uint16_t(p[24]) << 8);
	cpu.im = p[25] & 0x03;
	// p[26] border color ignored here; caller may set ULA

	// Copy RAM
	std::memcpy(mem.ram, p + 27, 49152);

	// PC is not stored in 48K SNA; retrieve from stack
	uint8_t lo = mem.read(cpu.sp++);
	uint8_t hi = mem.read(cpu.sp++);
	cpu.pc = uint16_t(lo) | (uint16_t(hi) << 8);
	cpu.iff1 = cpu.iff2;
	cpu.halted = false;
	return true;
}