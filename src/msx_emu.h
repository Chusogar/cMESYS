#pragma once
#include <cstdint>
#include <vector>
#include "z80.h"
#include "msx_memory.h"
#include "msx_vdp.h"
#include "ay.h"

class Msx {
public:
	bool init(const std::vector<uint8_t> &rom32) {
		mem.reset(); if (!mem.loadRom32(rom32)) return false;
		vdp.reset(); ay.reset(1789772/2, 44100);
		cpu.reset();
		cpu.connectMemory([this](uint16_t a){ return mem.read(a); }, [this](uint16_t a, uint8_t v){ mem.write(a,v); });
		cpu.connectIO([this](uint16_t p){ return io_read(p); }, [this](uint16_t p, uint8_t v){ io_write(p,v); });
		return true;
	}
	int step() { return cpu.step(); }
	void render(std::vector<uint32_t> &fb) { vdp.render(fb); }
private:
	Z80Cpu cpu{};
	MsxMemory mem{};
	MsxVdp vdp{};
	AY38912 ay{};

	uint8_t io_read(uint16_t p) {
		uint8_t low = p & 0xFF;
		if (low == 0x98 || low == 0x99) return vdp.in(p);
		if (low == 0xA2) return ay.readData();
		return 0xFF;
	}
	void io_write(uint16_t p, uint8_t v) {
		uint8_t low = p & 0xFF;
		if (low == 0x98 || low == 0x99) { vdp.out(p,v); return; }
		if (low == 0xA0) { ay.setIndex(v); return; }
		if (low == 0xA1) { ay.writeData(v); return; }
	}
};