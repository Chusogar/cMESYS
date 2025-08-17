#pragma once
#include <cstdint>
#include <vector>

class MsxVdp {
public:
	void reset() { ctrl=0; vram.assign(0x4000, 0); }
	uint8_t in(uint16_t port) { if ((port & 0xFF) == 0x98) { return vram[dataPtr++ & (vram.size()-1)]; } return 0xFF; }
	void out(uint16_t port, uint8_t val) { if ((port & 0xFF) == 0x98) { vram[dataPtr++ & (vram.size()-1)] = val; } else if ((port & 0xFF) == 0x99) { ctrl = val; } }
	void render(std::vector<uint32_t> &fb) { fb.assign(256*192, 0xFF000000 | (ctrl*12345)); }
private:
	std::vector<uint8_t> vram;
	uint16_t dataPtr{0};
	uint8_t ctrl{0};
};