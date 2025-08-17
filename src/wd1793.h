#pragma once
#include <cstdint>
#include <vector>
#include "trd.h"

class Wd1793 {
public:
	void reset() { cmd=0; track=0; sector=1; data=0; status=0x00; phase=Phase::Idle; fifo.clear(); pos=0; multi=false; head=0; }
	void attachImage(const TrdImage *img) { image = img; }

	// IO registers mapping per Beta Disk: base+0: status(cmd write), +2: track, +4: sector, +6: data
	uint8_t readStatus() const { return status; }
	void writeCommand(uint8_t v) { cmd = v; executeCommand(); }
	uint8_t readTrack() const { return track; }
	void writeTrack(uint8_t v) { track = v; }
	uint8_t readSector() const { return sector; }
	void writeSector(uint8_t v) { sector = v; }
	uint8_t readData() {
		if (phase == Phase::Data && pos < fifo.size()) {
			uint8_t b = fifo[pos++];
			if (pos >= fifo.size()) {
				if (multi) {
					// move to next sector, refill if possible
					if (sector < 16) { sector++; startReadSector(); }
					else { endTransfer(); }
				} else {
					endTransfer();
				}
			}
			return b;
		}
		return data;
	}
	void writeData(uint8_t v) { data = v; }

private:
	enum class Phase { Idle, Data, Result };
	Phase phase{Phase::Idle};
	uint8_t cmd{0}, track{0}, sector{1}, data{0}, status{0x00};
	uint8_t head{0};
	bool multi{false};
	const TrdImage *image{nullptr};
	std::vector<uint8_t> fifo; size_t pos{0};

	void setBusy(bool b) { if (b) status |= 0x01; else status &= ~0x01; }
	void endTransfer() { phase = Phase::Idle; status &= ~0x02; setBusy(false); pos=0; fifo.clear(); }

	void startReadSector() {
		if (!image) { status |= 0x10; endTransfer(); return; }
		std::vector<uint8_t> buf;
		if (!image->readSector(track, head, sector, 1, buf)) { status |= 0x10; endTransfer(); return; }
		fifo = std::move(buf); pos=0; phase = Phase::Data; status |= 0x02; setBusy(true); // DRQ on
	}

	void executeCommand() {
		status = 0; // clear for new cmd
		uint8_t group = cmd & 0xF0;
		if ((cmd & 0xF0) == 0x00) {
			// RESTORE/SEEK/STEP group
			if ((cmd & 0xF0) == 0x00) { track = 0; setBusy(false); }
			else if ((cmd & 0xF0) == 0x10) { track = data; setBusy(false); }
			else { setBusy(false); }
			return;
		}
		if ((group & 0xF0) == 0x80) {
			// READ SECTOR
			head = (cmd & 0x04) ? 1 : 0; multi = (cmd & 0x10) != 0; startReadSector(); return;
		}
		if ((group & 0xF0) == 0xA0) {
			// WRITE SECTOR (not supported): write-protect
			status |= 0x40; setBusy(false); return;
		}
		if ((group & 0xF0) == 0xC0) {
			// READ ADDRESS: return C H R N ST1 ST2 (we fake ST1/ST2=0)
			fifo.clear(); fifo.push_back(track); fifo.push_back(head); fifo.push_back(sector); fifo.push_back(1); fifo.push_back(0); fifo.push_back(0); pos=0; phase=Phase::Data; status |= 0x02; setBusy(true); return;
		}
		if ((group & 0xF0) == 0xD0) {
			// FORCE INTERRUPT
			endTransfer(); return;
		}
		// default
		setBusy(false);
	}
};