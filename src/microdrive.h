#pragma once
#include <cstdint>
#include <vector>
#include <string>

class MicrodriveCart {
public:
	bool load(const std::string &path);
	bool isLoaded() const { return !data.empty(); }
	const std::vector<uint8_t>& bytes() const { return data; }
private:
	std::vector<uint8_t> data;
};

class Interface1 {
public:
	void reset() { motor=false; pos=0; bitPos=0; bitClockTstates=0; lastBit=0; }
	void mount(const MicrodriveCart *c) { cart=c; pos=0; bitPos=0; bitClockTstates=0; }
	
	// Ports (simplified): 0xE7 data, 0xEB control (motor), 0xEF status, 0xE3 IF1 ROM control (bit0)
	uint8_t in(uint16_t port) {
		uint8_t p = uint8_t(port & 0xFF);
		if (p == 0xE7) { return readBitByte(); }
		if (p == 0xEF) { return motor ? 0x01 : 0x00; }
		return 0xFF;
	}
	void out(uint16_t port, uint8_t val) {
		uint8_t p = uint8_t(port & 0xFF);
		if (p == 0xEB) { motor = (val & 0x01) != 0; }
	}

	void tick(uint32_t tstates) {
		// Advance a very rough bit clock when motor runs
		if (!motor || !cart || cart->bytes().empty()) return;
		bitClockTstates += tstates;
		const uint32_t bitPeriod = 100; // fake ~35kHz at 3.5MHz
		while (bitClockTstates >= bitPeriod) {
			bitClockTstates -= bitPeriod;
			advanceBit();
		}
	}

	bool earActive() const { return motor && lastBit != 0; }

private:
	const MicrodriveCart *cart{nullptr};
	bool motor{false};
	size_t pos{0};
	uint8_t bitPos{0};
	uint32_t bitClockTstates{0};
	uint8_t lastBit{0};

	void advanceBit() {
		uint8_t b = cart->bytes()[pos % cart->bytes().size()];
		lastBit = (b >> (bitPos & 7)) & 1;
		bitPos++;
		if ((bitPos & 7) == 0) pos++;
	}
	uint8_t readBitByte() {
		// Return last latched bit expanded to byte
		return lastBit ? 0xFF : 0x00;
	}
};