#pragma once
#include <cstdint>
#include <cstring>

struct Keyboard {
	// 8 rows of 5 keys each; bits low when pressed
	uint8_t rows[8];

	void reset() { std::memset(rows, 0xFF, sizeof(rows)); }

	// Host key handling via SDL keysyms; to keep header-only we declare signature and define in cpp
	void handleHostKey(int keysym, bool pressed);

	// Read row selection (bits low indicate column selected). Port upper byte bits low select row
	uint8_t readRow(uint8_t hiByte) const {
		uint8_t v = 0x1F; // no key pressed
		for (int row = 0; row < 8; ++row) {
			if (((~hiByte) >> row) & 1) {
				v &= rows[row] & 0x1F;
			}
		}
		return v | 0xE0; // upper 3 bits floating high
	}
};