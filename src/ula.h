#pragma once
#include <cstdint>
#include "memory.h"

struct ULA {
	Memory *mem{nullptr};
	uint8_t border{0};
	uint32_t frameCount{0};
	uint32_t tstateInFrame{0};

	void connectMemory(Memory *m) { mem = m; }
	void reset() { border = 0; frameCount = 0; tstateInFrame = 0; }

	void setBorderColour(uint8_t c) { border = c & 0x07; }
	uint8_t borderColour() const { return border; }

	void tick(uint32_t tstates) {
		// Not cycle accurate; accumulate and roll every frame (70,000 tstates)
		tstateInFrame += tstates;
		const uint32_t perFrame = 3500000 / 50;
		if (tstateInFrame >= perFrame) {
			tstateInFrame -= perFrame;
			frameCount++;
		}
	}

	void renderFrame(uint32_t *outArgb, int w, int h);
};