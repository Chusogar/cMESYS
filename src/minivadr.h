#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include "z80.h"

class MiniVadr {
public:
	bool init(const std::vector<uint8_t> &rom) {
		if (rom.size() < 0x2000) return false;
		std::fill(ram.begin(), ram.end(), 0);
		std::copy(rom.begin(), rom.begin() + 0x2000, this->rom.begin());
		cpu.reset();
		cpu.connectMemory(
			[this](uint16_t a){ return memRead(a); },
			[this](uint16_t a, uint8_t v){ memWrite(a,v); }
		);
		cpu.connectIO(
			[this](uint16_t p){ return ioRead(p); },
			[this](uint16_t p, uint8_t v){ ioWrite(p,v); }
		);
		return true;
	}
	int step() { return cpu.step(); }
	void render(std::vector<uint32_t> &fb) {
		fb.resize(256*256);
		for (int y=0;y<256;++y) {
			for (int x=0;x<256;++x) {
				uint32_t idx = y*256 + x;
				uint32_t off = y*32 + (x>>3);
				uint8_t b = vram[(off) & 0x1FFF];
				bool on = (b >> (7-(x&7))) & 1;
				fb[idx] = on ? 0xFFFFFFFFu : 0xFF000000u;
			}
		}
	}
	void setInputs(uint8_t v) { inputs = v; }
private:
	Z80Cpu cpu{};
	std::array<uint8_t, 0x2000> rom{};   // 8KB
	std::array<uint8_t, 0x2000> vram{};  // 8KB @0x4000
	std::array<uint8_t, 0x2000> ram{};   // 8KB @0x6000
	uint8_t inputs{0xFF};

	uint8_t memRead(uint16_t a) {
		if (a < 0x2000) return rom[a];
		if (a >= 0x4000 && a < 0x6000) return vram[a-0x4000];
		if (a >= 0x6000 && a < 0x8000) return ram[a-0x6000];
		return 0xFF;
	}
	void memWrite(uint16_t a, uint8_t v) {
		if (a >= 0x4000 && a < 0x6000) { vram[a-0x4000] = v; return; }
		if (a >= 0x6000 && a < 0x8000) { ram[a-0x6000] = v; return; }
	}
	uint8_t ioRead(uint16_t p) { (void)p; return inputs; }
	void ioWrite(uint16_t p, uint8_t v) { (void)p; inputs = v; }
};