#pragma once
#include <cstdint>
#include "wd1793.h"

class BetaDisk {
public:
	void reset() { wd.reset(); romcs = false; }
	void attachImage(const TrdImage *img) { wd.attachImage(img); }

	// Ports: 0x1F status/cmd, 0x3F track, 0x5F sector, 0x7F data; 0x3D ROMCS toggle (simplified)
	uint8_t in(uint16_t port) {
		uint16_t p = port & 0xFF;
		switch (p) {
			case 0x1F: return wd.readStatus();
			case 0x3F: return wd.readTrack();
			case 0x5F: return wd.readSector();
			case 0x7F: return wd.readData();
			case 0x3D: return romcs ? 1 : 0;
			default: return 0xFF;
		}
	}
	void out(uint16_t port, uint8_t value) {
		uint16_t p = port & 0xFF;
		switch (p) {
			case 0x1F: wd.writeCommand(value); break;
			case 0x3F: wd.writeTrack(value); break;
			case 0x5F: wd.writeSector(value); break;
			case 0x7F: wd.writeData(value); break;
			case 0x3D: romcs = (value & 1) != 0; break;
			default: break;
		}
	}

	bool trdosRomActive() const { return romcs; }

private:
	Wd1793 wd{};
	bool romcs{false};
};