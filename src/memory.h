#pragma once
#include <cstdint>
#include <cstring>

struct Memory {
	// 16KB ROM + 48KB RAM
	uint8_t rom[16 * 1024];
	uint8_t ram[48 * 1024];

	void reset() {
		std::memset(rom, 0xFF, sizeof(rom));
		std::memset(ram, 0x00, sizeof(ram));
	}

	void load_rom(const uint8_t *data, size_t size) {
		if (size > sizeof(rom)) size = sizeof(rom);
		std::memcpy(rom, data, size);
	}

	inline uint8_t read(uint16_t addr) const {
		if (addr < 0x4000) return rom[addr];
		return ram[addr - 0x4000];
	}
	inline void write(uint16_t addr, uint8_t v) {
		if (addr < 0x4000) return; // ROM
		ram[addr - 0x4000] = v;
	}

	inline uint8_t *ramPtr(uint16_t addr) { return &ram[addr - 0x4000]; }
};