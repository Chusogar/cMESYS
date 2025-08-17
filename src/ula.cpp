#include "ula.h"

static inline uint32_t zx_palette(bool bright, uint8_t color) {
	// Spectrum colors: 0..7, with bright flag doubling intensity
	static const uint8_t base[8][3] = {
		{0,0,0}, {0,0,205}, {205,0,0}, {205,0,205}, {0,205,0}, {0,205,205}, {205,205,0}, {205,205,205}
	};
	const uint8_t *rgb = base[color & 7];
	int mul = bright ? 255 : 170;
	uint8_t r = uint8_t((rgb[0] ? mul : 0));
	uint8_t g = uint8_t((rgb[1] ? mul : 0));
	uint8_t b = uint8_t((rgb[2] ? mul : 0));
	return 0xFF000000 | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void ULA::renderFrame(uint32_t *outArgb, int w, int h) {
	// Render 256x192 bitmap from RAM: pixel data at 0x4000..0x57FF, attributes at 0x5800..0x5AFF
	if (!mem) return;
	const uint16_t screen_base = 0x4000;
	const uint16_t attr_base = 0x5800;

	bool flashPhase = ((frameCount / 16) & 1) != 0; // approx 1.56Hz; close enough

	for (int y = 0; y < 192; ++y) {
		// Spectrum line addressing is split into thirds
		int y7 = (y & 0x07);
		int y38 = (y & 0x38) >> 3;
		int yC0 = (y & 0xC0) >> 6;
		uint16_t line_off = (y7 << 8) | (y38 << 5) | (yC0 << 11);
		uint16_t addr = screen_base + line_off;
		int attr_row = y >> 3;
		for (int xbyte = 0; xbyte < 32; ++xbyte) {
			uint8_t pix = mem->read_screen(addr + xbyte);
			uint8_t attr = mem->read_screen(attr_base + attr_row * 32 + xbyte);
			bool bright = (attr & 0x40) != 0;
			bool flash = (attr & 0x80) != 0;
			uint8_t ink = attr & 0x07;
			uint8_t paper = (attr >> 3) & 0x07;
			bool invert = flash && flashPhase;
			uint32_t colInk = zx_palette(bright, invert ? paper : ink);
			uint32_t colPaper = zx_palette(bright, invert ? ink : paper);
			for (int bit = 7; bit >= 0; --bit) {
				bool on = (pix & (1 << bit)) != 0;
				int px = xbyte * 8 + (7 - bit);
				outArgb[y * w + px] = on ? colInk : colPaper;
			}
		}
	}
}