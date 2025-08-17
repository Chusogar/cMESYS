#pragma once
#include <cstdint>
#include <functional>

struct Z80Cpu {
	// 8-bit registers
	uint8_t a{0}, f{0};
	uint8_t b{0}, c{0}, d{0}, e{0}, h{0}, l{0};
	uint8_t a2{0}, f2{0}, b2{0}, c2{0}, d2{0}, e2{0}, h2{0}, l2{0};
	uint16_t ix{0}, iy{0}, sp{0}, pc{0};
	uint8_t i{0}, r{0};
	bool iff1{false}, iff2{false};
	uint8_t im{0};
	bool halted{false};

	// Memory/IO hooks
	std::function<uint8_t(uint16_t)> memRead;
	std::function<void(uint16_t, uint8_t)> memWrite;
	std::function<uint8_t(uint16_t)> ioRead;
	std::function<void(uint16_t, uint8_t)> ioWrite;

	// Timing
	int tstates{0};

	void connectMemory(std::function<uint8_t(uint16_t)> rd,
					   std::function<void(uint16_t,uint8_t)> wr) {
		memRead = std::move(rd);
		memWrite = std::move(wr);
	}
	void connectIO(std::function<uint8_t(uint16_t)> in,
				  std::function<void(uint16_t,uint8_t)> out) {
		ioRead = std::move(in);
		ioWrite = std::move(out);
	}

	void reset();
	int step(); // execute one instruction, return t-states consumed

	// Interrupts
	void nmi();
	void irq(bool level); // level-triggered maskable

	// Helpers
	inline uint16_t af() const { return (uint16_t(a) << 8) | f; }
	inline uint16_t bc() const { return (uint16_t(b) << 8) | c; }
	inline uint16_t de() const { return (uint16_t(d) << 8) | e; }
	inline uint16_t hl() const { return (uint16_t(h) << 8) | l; }
	inline void set_af(uint16_t v) { a = uint8_t(v >> 8); f = uint8_t(v & 0xFF); }
	inline void set_bc(uint16_t v) { b = uint8_t(v >> 8); c = uint8_t(v & 0xFF); }
	inline void set_de(uint16_t v) { d = uint8_t(v >> 8); e = uint8_t(v & 0xFF); }
	inline void set_hl(uint16_t v) { h = uint8_t(v >> 8); l = uint8_t(v & 0xFF); }

	// Flags bits
	enum : uint8_t {
		FLAG_C = 0x01,
		FLAG_N = 0x02,
		FLAG_PV = 0x04,
		FLAG_X = 0x08,
		FLAG_H = 0x10,
		FLAG_Y = 0x20,
		FLAG_Z = 0x40,
		FLAG_S = 0x80,
	};
};