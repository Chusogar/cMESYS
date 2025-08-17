#pragma once
#include <cstdint>
#include <vector>

class CpcMemory {
public:
	void reset() {
		ram.assign(128 * 1024, 0x00);
		romLower.assign(16 * 1024, 0xFF);
		romUpper.assign(16 * 1024, 0xFF);
	}
	// Load a 48KB ROM blob (32K OS + 16K BASIC) or two parts separately
	bool loadRom48(const std::vector<uint8_t> &rom48) {
		if (rom48.size() != 48 * 1024) return false;
		// OS: first 32KB -> take lower 16K for 0x0000-0x3FFF
		std::copy(rom48.begin(), rom48.begin() + 16*1024, romLower.begin());
		// BASIC: last 16KB maps to 0xC000-0xFFFF
		std::copy(rom48.begin() + 32*1024, rom48.begin() + 48*1024, romUpper.begin());
		return true;
	}
	void loadRomLower(const std::vector<uint8_t> &rom) { romLower = rom; if (romLower.size()<16*1024) romLower.resize(16*1024,0xFF); }
	void loadRomUpper(const std::vector<uint8_t> &rom) { romUpper = rom; if (romUpper.size()<16*1024) romUpper.resize(16*1024,0xFF); }

	uint8_t read(uint16_t addr) const {
		if (addr < 0x4000) return romLower[addr];
		if (addr >= 0xC000) {
			// If upper ROM present, map it
			return romUpper[addr - 0xC000];
		}
		return ram[addr];
	}
	void write(uint16_t addr, uint8_t v) {
		// ROM is not writable; write-through to RAM area
		if (addr >= 0x4000 && addr < 0xC000) {
			ram[addr] = v;
		} else if (addr >= 0xC000) {
			ram[addr] = v;
		}
	}

	uint8_t* ramPtr(uint32_t addr) { return &ram[addr % ram.size()]; }

private:
	std::vector<uint8_t> ram;
	std::vector<uint8_t> romLower;
	std::vector<uint8_t> romUpper;
};