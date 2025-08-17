#pragma once
#include <cstdint>
#include <vector>

class MsxMemory {
public:
	void reset() {
		ram.assign(64 * 1024, 0x00);
		romLower.assign(16 * 1024, 0xFF);
		romUpper.assign(16 * 1024, 0xFF);
	}
	// Load a 32KB BIOS+BASIC combined ROM: first 16K to 0x0000-0x3FFF, last 16K to 0x8000-0xBFFF or 0xC000-0xFFFF depending; we map to 0xC000.
	bool loadRom32(const std::vector<uint8_t> &rom) {
		if (rom.size() != 32 * 1024) return false;
		std::copy(rom.begin(), rom.begin() + 16*1024, romLower.begin());
		std::copy(rom.begin() + 16*1024, rom.begin() + 32*1024, romUpper.begin());
		return true;
	}
	uint8_t read(uint16_t addr) const {
		if (addr < 0x4000) return romLower[addr];
		if (addr >= 0xC000) return romUpper[addr - 0xC000];
		return ram[addr];
	}
	void write(uint16_t addr, uint8_t v) {
		// RAM writes; ignore writes to ROM
		if (addr >= 0x4000 && addr < 0xC000) ram[addr] = v;
		else if (addr >= 0xC000) ram[addr] = v;
	}
private:
	std::vector<uint8_t> ram;
	std::vector<uint8_t> romLower;
	std::vector<uint8_t> romUpper;
};