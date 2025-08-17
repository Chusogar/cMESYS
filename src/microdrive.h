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
	void reset() { motor=false; pos=0; bitPos=0; }
	void mount(const MicrodriveCart *c) { cart=c; pos=0; bitPos=0; }
	
	// Ports (simplified): 0xE7 data, 0xEB control (motor), 0xEF status
	uint8_t in(uint16_t port) {
		uint8_t p = uint8_t(port & 0xFF);
		if (p == 0xE7) {
			return readBitByte();
		} else if (p == 0xEF) {
			return motor ? 0x01 : 0x00; // status: motor running
		}
		return 0xFF;
	}
	void out(uint16_t port, uint8_t val) {
		uint8_t p = uint8_t(port & 0xFF);
		if (p == 0xEB) {
			motor = (val & 0x01) != 0;
		}
	}

	bool earActive() const { return motor; }

private:
	const MicrodriveCart *cart{nullptr};
	bool motor{false};
	size_t pos{0};
	uint8_t bitPos{0};
	uint8_t readBitByte() {
		if (!motor || !cart || cart->bytes().empty()) return 0xFF;
		uint8_t b = cart->bytes()[pos % cart->bytes().size()];
		// return a pseudo-serial bit stream assembled into bytes; advance bit position
		uint8_t bit = (b >> (bitPos & 7)) & 1;
		bitPos++;
		if ((bitPos & 7) == 0) pos++;
		return bit ? 0xFF : 0x00;
	}
};