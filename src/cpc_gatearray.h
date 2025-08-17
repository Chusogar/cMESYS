#pragma once
#include <cstdint>
#include <vector>
#include "cpc_memory.h"

class CpcGateArray {
public:
	void reset(CpcMemory *mem) { memory=mem; border=0; }
	void write(uint8_t val) {
		// Palette and mode control ignored for stub
		border = val & 0x1F;
	}
	void renderFrame(std::vector<uint32_t> &out, int w, int h) {
		// Stub: fill with border color and simple test pattern using RAM
		uint32_t col = 0xFF000000 | ((border*12)&0xFF) << 16;
		out.assign(w*h, col);
	}
private:
	CpcMemory *memory{nullptr};
	uint8_t border{0};
};