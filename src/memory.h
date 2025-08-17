#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

struct Memory {
	enum class Model { ZX48, ZX128, ZXPlus3, Pentagon, Scorpion };

	// ROMs
	std::vector<std::vector<uint8_t>> roms; // each 16KB
	std::vector<uint8_t> trdosRom; // 16KB TR-DOS ROM if present
	std::vector<uint8_t> if1Rom;   // 8K or 16K IF1 ROM (we overlay 16K window)
	// 48K RAM for ZX48 compatibility
	uint8_t ram[48 * 1024];
	// 128K banks (8 x 16KB)
	uint8_t ramBank[8][16 * 1024];

	// Paging registers/state
	Model model{Model::ZX48};
	uint8_t reg7FFD{0};
	uint8_t reg1FFD{0};
	bool pagingLocked{false};
	bool romcs{false}; // TR-DOS ROMCS
	bool if1Active{false};
	bool turboMode{false};

	void reset() {
		roms.clear();
		roms.resize(1); roms[0].assign(16*1024, 0xFF);
		trdosRom.clear();
		if1Rom.clear();
		std::memset(ram, 0x00, sizeof(ram));
		for (int b=0;b<8;++b) std::memset(ramBank[b], 0x00, sizeof(ramBank[b]));
		model = Model::ZX48;
		reg7FFD = 0; reg1FFD = 0; pagingLocked = false; romcs = false; if1Active = false; turboMode=false;
	}

	void load_rom(const uint8_t *data, size_t size) {
		if (size == 16384) {
			model = Model::ZX48;
			roms.resize(1); roms[0].assign(data, data + size);
		} else if (size == 32768) {
			model = Model::ZX128;
			roms.resize(2);
			roms[0].assign(data, data + 16384);
			roms[1].assign(data + 16384, data + 32768);
		} else if (size == 65536) {
			model = Model::ZXPlus3;
			roms.resize(4);
			for (int i=0;i<4;++i) roms[i].assign(data + i*16384, data + (i+1)*16384);
		} else {
			model = Model::ZX48;
			roms.resize(1); roms[0].assign(data, data + (size<16384?size:16384));
		}
	}

	void load_trdos_rom(const uint8_t *data, size_t size) { trdosRom.assign(data, data + (size<16384?size:16384)); }
	void load_if1_rom(const uint8_t *data, size_t size) {
		if1Rom.assign(data, data + (size<16384?size:16384));
	}

	inline uint8_t currentRomIndex() const {
		if (model == Model::ZX48) return 0;
		uint8_t low = (reg7FFD >> 4) & 1;
		if (model == Model::ZX128) return low & 1;
		uint8_t hi = (reg1FFD >> 1) & 1;
		uint8_t idx = (hi<<1) | low;
		if (idx >= roms.size()) idx = idx % roms.size();
		return idx;
	}

	inline uint8_t bankAtC000() const { return reg7FFD & 0x07; }
	inline bool shadowScreenEnabled() const { return (reg7FFD & 0x08) != 0; }
	inline bool specialPaging() const { return (reg1FFD & 0x01) != 0; }

	inline uint8_t read(uint16_t addr) const {
		// Interface 1 ROM overlay highest priority if active
		if (if1Active && addr < 0x4000 && !if1Rom.empty()) return if1Rom[addr];
		// TR-DOS ROM overlay if ROMCS and present
		if (romcs && addr < 0x4000 && !trdosRom.empty()) return trdosRom[addr];
		switch (model) {
			case Model::ZX48:
				if (addr < 0x4000) return roms[0][addr];
				return ram[addr - 0x4000];
			case Model::ZX128:
			case Model::ZXPlus3:
			case Model::Pentagon:
			case Model::Scorpion: {
				if (addr < 0x4000) {
					uint8_t romIdx = currentRomIndex();
					return roms[romIdx][addr];
				} else if (addr < 0x8000) {
					return ramBank[5][addr - 0x4000];
				} else if (addr < 0xC000) {
					return ramBank[2][addr - 0x8000];
				} else {
					return ramBank[bankAtC000()][addr - 0xC000];
				}
			}
		}
		return 0xFF;
	}
	inline void write(uint16_t addr, uint8_t v) {
		switch (model) {
			case Model::ZX48:
				if (addr >= 0x4000) ram[addr - 0x4000] = v; /* ROM ignored */
				break;
			case Model::ZX128:
			case Model::ZXPlus3:
			case Model::Pentagon:
			case Model::Scorpion: {
				if (addr < 0x4000) {
					// ROM ignored
				} else if (addr < 0x8000) {
					ramBank[5][addr - 0x4000] = v;
				} else if (addr < 0xC000) {
					ramBank[2][addr - 0x8000] = v;
				} else {
					ramBank[bankAtC000()][addr - 0xC000] = v;
				}
				break;
			}
		}
	}

	inline uint8_t read_screen(uint16_t addr) const {
		if (model == Model::ZX48) return read(addr);
		uint8_t bank = shadowScreenEnabled() ? 7 : 5;
		return ramBank[bank][addr - 0x4000];
	}

	// IO paging
	void out7FFD(uint8_t value) {
		if (pagingLocked) return;
		reg7FFD = value;
		if (value & 0x20) pagingLocked = true;
	}
	void out1FFD(uint8_t value) { reg1FFD = value; }
	void setRomcs(bool on) { romcs = on; }
	void setIf1(bool on) { if1Active = on; }
};