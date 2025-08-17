#pragma once
#include <cstdint>
#include <vector>
#include "trd.h"

class Wd1793 {
public:
	void reset() { cmd=0; track=0; sector=1; data=0; status=0x00; phase=Phase::Idle; fifo.clear(); pos=0; }
	void attachImage(const TrdImage *img) { image = img; }

	// IO registers mapping per Beta Disk: base+0: status(cmd write), +2: track, +4: sector, +6: data
	uint8_t readStatus() const { return status; }
	void writeCommand(uint8_t v) { cmd = v; startCommand(); }
	uint8_t readTrack() const { return track; }
	void writeTrack(uint8_t v) { track = v; }
	uint8_t readSector() const { return sector; }
	void writeSector(uint8_t v) { sector = v; }
	uint8_t readData() {
		if (phase == Phase::Data && pos < fifo.size()) {
			uint8_t b = fifo[pos++];
			if (pos >= fifo.size()) { phase = Phase::Idle; status = 0x00; }
			return b;
		}
		return data;
	}
	void writeData(uint8_t v) { data = v; }

private:
	enum class Phase { Idle, Data };
	Phase phase{Phase::Idle};
	uint8_t cmd{0}, track{0}, sector{1}, data{0}, status{0x00};
	const TrdImage *image{nullptr};
	std::vector<uint8_t> fifo; size_t pos{0};

	void startCommand() {
		uint8_t c = cmd & 0xF0;
		if (c == 0x80 && image) {
			// READ SECTOR: use track register as cylinder; head 0; sector and N=1
			std::vector<uint8_t> buf;
			uint8_t head = 0; // Beta Disk will set head via command bit, but we simplify
			if (image->readSector(track, head, sector, 1, buf)) {
				fifo = std::move(buf); pos = 0; phase = Phase::Data; status = 0x01; // DRQ
			} else {
				status = 0x40; // Not found
			}
		} else {
			status = 0x00;
		}
	}
};