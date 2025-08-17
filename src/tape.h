#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct TapeBlockStd {
	std::vector<uint8_t> data;
	uint16_t pauseMs{1000};
};

struct TapeEvent {
	// Not used externally; helper for pulses if needed
	uint32_t tstates;
	bool level;
};

class Tape {
public:
	void reset(int cpuClockHz) {
		clockHz = cpuClockHz;
		rewind();
	}

	bool load(const std::string &path);
	void play() { playing = true; }
	void stop() { playing = false; }
	void rewind();
	bool isPlaying() const { return playing; }

	void tick(uint32_t tstates);
	bool earBit() const { return earLevel; }

private:
	// Parsed content (standard-speed like blocks)
	std::vector<TapeBlockStd> stdBlocks;
	int clockHz{3500000};
	bool playing{false};
	bool earLevel{true}; // true -> high (bit 6 set)

	// Current playback state
	size_t currentBlockIndex{0};
	// Stages within a standard block
	enum class Stage { Idle, Pilot, Sync1, Sync2, Data, Pause };
	Stage stage{Stage::Idle};
	uint32_t currentPulseRemaining{0};
	uint32_t remainingPilotPulses{0};
	size_t dataByteIndex{0};
	uint8_t dataBitMask{0x80};
	uint32_t pauseRemainingTstates{0};

	// Helpers
	void startNextBlock();
	void beginStdBlock(const TapeBlockStd &blk);
	void advanceDataBit(const TapeBlockStd &blk);

	// Parsing
	bool loadTAP(const std::string &path);
	bool loadTZX(const std::string &path);
};