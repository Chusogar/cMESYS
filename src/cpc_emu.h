#pragma once
#include <cstdint>
#include <vector>
#include "z80.h"
#include "cpc_memory.h"
#include "cpc_gatearray.h"

class Cpc6128 {
public:
	bool init(const std::vector<uint8_t> &rom48) {
		mem.reset();
		if (!mem.loadRom48(rom48)) return false;
		ga.reset(&mem);
		cpu.reset();
		cpu.connectMemory(
			[this](uint16_t a){ return mem.read(a); },
			[this](uint16_t a, uint8_t v){ mem.write(a,v); }
		);
		cpu.connectIO(
			[this](uint16_t p){ return io_read(p); },
			[this](uint16_t p, uint8_t v){ io_write(p,v); }
		);
		return true;
	}
	uint8_t io_read(uint16_t port) {
		// CPC I/O decoding: GA at 0x7Fxx write, CRTC, PSG, FDC not yet
		return 0xFF;
	}
	void io_write(uint16_t port, uint8_t val) {
		if ((port & 0x2000) == 0 && (port & 0x0400)) {
			// Gate Array write: bit13=0, bit10=1. This is simplified
			ga.write(val);
		}
	}
	int step() { return cpu.step(); }
	void render(std::vector<uint32_t> &fb, int w, int h) { ga.renderFrame(fb,w,h); }

private:
	Z80Cpu cpu{};
	CpcMemory mem{};
	CpcGateArray ga{};
};