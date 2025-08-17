#include "tape.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END);
	long size = std::ftell(f);
	if (size < 0) { std::fclose(f); return false; }
	std::rewind(f);
	out.resize(static_cast<size_t>(size));
	size_t rd = std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	return rd == out.size();
}

bool Tape::load(const std::string &path) {
	stdBlocks.clear();
	rewind();
	// Detect by header or extension
	std::vector<uint8_t> buf;
	if (!read_file(path, buf)) return false;
	if (buf.size() >= 10 && std::memcmp(buf.data(), "ZXTape!", 7) == 0) {
		// TZX
		// write to temp and parse from memory in loadTZX by path? We'll parse from buffer here for simplicity
		// Reopen from memory-less path handling is fine: implement loadTZX using buffer copy
	}
	// Simple dispatch: check header first, else by extension
	auto lower_ext = [&]() -> std::string {
		size_t dot = path.find_last_of('.');
		std::string ext = (dot==std::string::npos)?std::string():path.substr(dot+1);
		for (char &ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
		return ext;
	}();
	bool ok = false;
	if (buf.size() >= 10 && std::memcmp(buf.data(), "ZXTape!", 7) == 0) {
		// TZX from buffer
		// Use a separate buffer-based loader below
		stdBlocks.clear();
		// Minimal parser
		size_t p = 10; // skip header (7 bytes + 0x1A + verMajor + verMinor)
		while (p < buf.size()) {
			uint8_t id = buf[p++];
			switch (id) {
				case 0x10: {
					if (p + 4 > buf.size()) { p = buf.size(); break; }
					uint16_t pauseMs = uint16_t(buf[p]) | (uint16_t(buf[p+1]) << 8); p += 2;
					uint16_t len = uint16_t(buf[p]) | (uint16_t(buf[p+1]) << 8); p += 2;
					if (p + len > buf.size()) { p = buf.size(); break; }
					TapeBlockStd blk; blk.pauseMs = pauseMs; blk.data.insert(blk.data.end(), buf.begin()+p, buf.begin()+p+len); p += len;
					stdBlocks.push_back(std::move(blk));
					break; }
				case 0x20: { // Pause (ms)
					if (p + 2 > buf.size()) { p = buf.size(); break; }
					uint16_t pauseMs = uint16_t(buf[p]) | (uint16_t(buf[p+1]) << 8); p += 2;
					TapeBlockStd blk; blk.pauseMs = pauseMs; blk.data.clear(); // empty data signifies pure pause
					stdBlocks.push_back(std::move(blk));
					break; }
				case 0x21: // Group start
				case 0x22: // Group end
				case 0x30: // Text description
				case 0x31: { // Message block
					if (id == 0x22) { /* no data */ break; }
					if (p >= buf.size()) { p = buf.size(); break; }
					uint8_t l = buf[p++]; p += l; if (p > buf.size()) p = buf.size();
					break; }
				case 0x32: { // Archive info
					if (p + 2 > buf.size()) { p = buf.size(); break; }
					uint16_t total = uint16_t(buf[p]) | (uint16_t(buf[p+1]) << 8); p += 2; p += total; if (p > buf.size()) p = buf.size();
					break; }
				default: {
					// Unsupported block: try to skip using generic length if available
					// Many blocks have 3 or 4 byte lengths; but without full spec use bail-out
					p = buf.size();
					break; }
			}
		}
		ok = !stdBlocks.empty();
	} else if (lower_ext == "tap") {
		// TAP: many blocks (len, data)
		stdBlocks.clear();
		size_t p = 0;
		while (p + 2 <= buf.size()) {
			uint16_t len = uint16_t(buf[p]) | (uint16_t(buf[p+1]) << 8); p += 2;
			if (p + len > buf.size()) break;
			TapeBlockStd blk; blk.pauseMs = 1000; blk.data.insert(blk.data.end(), buf.begin()+p, buf.begin()+p+len); p += len;
			stdBlocks.push_back(std::move(blk));
		}
		ok = !stdBlocks.empty();
	} else if (lower_ext == "tzx") {
		// If ext tzx but header missing, still try header parse above? We'll call recursive by simulating header read; reuse the header path failsafe
		// For safety, reuse the same parser that looked for header. If header missing, fail.
		ok = false; // invalid tzx without header
	}

	if (ok) {
		rewind();
	}
	return ok;
}

void Tape::rewind() {
	playing = false;
	currentBlockIndex = 0;
	stage = Stage::Idle;
	earLevel = true;
	currentPulseRemaining = 0;
	remainingPilotPulses = 0;
	dataByteIndex = 0;
	dataBitMask = 0x80;
	pauseRemainingTstates = 0;
}

static inline uint32_t ms_to_tstates(int clockHz, uint32_t ms) {
	// clockHz tstates per second
	return (uint64_t(clockHz) * ms) / 1000;
}

void Tape::startNextBlock() {
	if (currentBlockIndex >= stdBlocks.size()) {
		stage = Stage::Idle;
		playing = false;
		return;
	}
	const TapeBlockStd &blk = stdBlocks[currentBlockIndex];
	if (blk.data.empty()) {
		// Pure pause
		pauseRemainingTstates = ms_to_tstates(clockHz, blk.pauseMs);
		stage = Stage::Pause;
		return;
	}
	beginStdBlock(blk);
}

void Tape::beginStdBlock(const TapeBlockStd &blk) {
	// Standard timings (tstates)
	const uint32_t PILOT = 2168;
	const uint32_t SYNC1 = 667;
	const uint32_t SYNC2 = 735;
	const uint32_t ZERO = 855;
	const uint32_t ONE = 1710;
	(void)ZERO; (void)ONE; // silence unused warnings in some builds

	// Determine header/data by first byte (0x00 header, 0xFF data)
	bool isHeader = !blk.data.empty() && (blk.data[0] == 0x00);
	remainingPilotPulses = isHeader ? 8063u : 3223u;
	currentPulseRemaining = PILOT;
	earLevel = !earLevel; // start by toggling to generate first edge
	stage = Stage::Pilot;
	dataByteIndex = 0;
	dataBitMask = 0x80;
	pauseRemainingTstates = ms_to_tstates(clockHz, blk.pauseMs);

	// Store sync durations in state by reusing currentPulseRemaining when pilot ends
	(void)SYNC1; (void)SYNC2;
}

void Tape::advanceDataBit(const TapeBlockStd &blk) {
	// Standard timings
	const uint32_t ZERO = 855;
	const uint32_t ONE = 1710;
	uint8_t byte = blk.data[dataByteIndex];
	uint32_t dur = (byte & dataBitMask) ? ONE : ZERO;
	currentPulseRemaining = dur;
	earLevel = !earLevel;
	// Toggle once now; on next completion we'll toggle again for the second half
}

void Tape::tick(uint32_t tstates) {
	if (!playing) return;
	while (tstates > 0) {
		if (stage == Stage::Idle) {
			startNextBlock();
			if (stage == Stage::Idle) return; // nothing to do
		}
		const TapeBlockStd &blk = stdBlocks[currentBlockIndex];
		switch (stage) {
			case Stage::Pilot: {
				uint32_t step = (tstates < currentPulseRemaining) ? tstates : currentPulseRemaining;
				currentPulseRemaining -= step;
				tstates -= step;
				if (currentPulseRemaining == 0) {
					// toggle and schedule next
					earLevel = !earLevel;
					if (--remainingPilotPulses == 0) {
						// move to sync1 (667) then sync2 (735)
						currentPulseRemaining = 667; stage = Stage::Sync1;
					} else {
						currentPulseRemaining = 2168;
					}
				}
				break; }
			case Stage::Sync1: {
				uint32_t step = (tstates < currentPulseRemaining) ? tstates : currentPulseRemaining;
				currentPulseRemaining -= step; tstates -= step;
				if (currentPulseRemaining == 0) { earLevel = !earLevel; currentPulseRemaining = 735; stage = Stage::Sync2; }
				break; }
			case Stage::Sync2: {
				uint32_t step = (tstates < currentPulseRemaining) ? tstates : currentPulseRemaining;
				currentPulseRemaining -= step; tstates -= step;
				if (currentPulseRemaining == 0) {
					earLevel = !earLevel;
					stage = Stage::Data;
					dataByteIndex = 0; dataBitMask = 0x80;
					// schedule first half of first bit
					advanceDataBit(blk);
				}
				break; }
			case Stage::Data: {
				uint32_t step = (tstates < currentPulseRemaining) ? tstates : currentPulseRemaining;
				currentPulseRemaining -= step; tstates -= step;
				if (currentPulseRemaining == 0) {
					// toggle at end of half-bit
					earLevel = !earLevel;
					// Each bit is two equal pulses. We can track using a toggle: if the last action was second half, move to next bit
					static bool secondHalf = false;
					if (!secondHalf) {
						// schedule second half of same bit
						uint8_t byte = blk.data[dataByteIndex];
						uint32_t dur = (byte & dataBitMask) ? 1710 : 855;
						currentPulseRemaining = dur; secondHalf = true;
					} else {
						// move to next bit
						secondHalf = false;
						dataBitMask >>= 1;
						if (dataBitMask == 0) { dataBitMask = 0x80; dataByteIndex++; }
						if (dataByteIndex >= blk.data.size()) {
							// Done data; go to pause
							stage = Stage::Pause;
							// Keep ear at last level; schedule pause
							// pauseRemainingTstates already set in beginStdBlock
							currentPulseRemaining = 0;
						} else {
							advanceDataBit(blk);
						}
					}
				}
				break; }
			case Stage::Pause: {
				if (pauseRemainingTstates == 0) {
					// advance to next block
					currentBlockIndex++;
					stage = Stage::Idle;
					break;
				}
				uint32_t step = (tstates < pauseRemainingTstates) ? tstates : pauseRemainingTstates;
				pauseRemainingTstates -= step; tstates -= step;
				// Keep ear stable high during pause
				earLevel = true;
				break; }
			case Stage::Idle: default: {
				// shouldn't get here
				tstates = 0; break;
			}
		}
	}
}