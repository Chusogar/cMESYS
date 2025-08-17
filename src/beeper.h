#pragma once
#include <cstdint>

struct Beeper {
	int sampleRate{44100};
	float level{0.0f};
	float phase{0.0f};
	float lastOut{0.0f};

	void reset(int rate) { sampleRate = rate; level = 0.0f; phase = 0.0f; lastOut = 0.0f; }
	void setLevel(float l) { level = l; }
	float sample() {
		// Simple RC to smooth transitions; not cycle accurate
		float target = level > 0.5f ? 1.0f : -1.0f;
		lastOut += (target - lastOut) * 0.1f;
		return lastOut;
	}
};