#pragma once
#include <cstdint>

class AY38912 {
public:
	void reset(int clockHz, int sampleRate) {
		psgClockHz = clockHz; sr = sampleRate; for (int i=0;i<16;++i) regs[i]=0; sel=0; phaseA=phaseB=phaseC=0; cntN=0; noiseLfsr=0x1FFFF; envPos=0; envPeriod=1; outA=outB=outC=false; noiseOut=false; }
	void setIndex(uint8_t idx) { sel = idx & 0x0F; }
	void writeData(uint8_t value) { regs[sel] = value; if (sel == 0x0D) { envPos = 0; } }
	uint8_t readData() const { return regs[sel]; }

	void tickTstates(uint32_t tstates) {
		// AY clock is CPU clock / 2 typically on Spectrum 128/+3
		uint32_t ayTicks = tstates; // approximate 1:1 for simplicity; fine-tuning can divide by 2
		for (uint32_t i=0;i<ayTicks;++i) {
			stepTone(0, regs[0] | ((regs[1]&0x0F)<<8), phaseA, outA);
			stepTone(1, regs[2] | ((regs[3]&0x0F)<<8), phaseB, outB);
			stepTone(2, regs[4] | ((regs[5]&0x0F)<<8), phaseC, outC);
			stepNoise();
			stepEnv();
		}
	}

	float sample() const {
		auto chanLevel = [&](int chan)->float{
			bool tone = (chan==0?outA:(chan==1?outB:outC));
			bool enableTone = ((regs[7] & (1<<chan)) == 0);
			bool enableNoise = ((regs[7] & (1<<(chan+3))) == 0);
			bool gateTone = enableTone ? tone : true; // if tone disabled, treated as 1
			bool gateNoise = enableNoise ? !noiseOut : true; // invert noiseOut to taste
			bool gate = gateTone && gateNoise;
			uint8_t volReg = regs[8+chan] & 0x0F;
			bool useEnv = (regs[8+chan] & 0x10) != 0;
			int level = useEnv ? (envLevel & 0x0F) : volReg;
			float amp = gate ? (level/15.0f) : 0.0f;
			return amp;
		};
		float a = chanLevel(0), b = chanLevel(1), c = chanLevel(2);
		float mix = (a + b + c) / 3.0f;
		return mix * 0.6f; // conservative
	}

private:
	int psgClockHz{1750000};
	int sr{44100};
	uint8_t regs[16]{};
	uint8_t sel{0};
	// Tone counters
	uint32_t phaseA{0}, phaseB{0}, phaseC{0};
	bool outA{false}, outB{false}, outC{false};
	// Noise
	uint32_t cntN{0};
	uint32_t noiseLfsr{0x1FFFF};
	bool noiseOut{false};
	// Envelope (very simplified)
	uint32_t envPos{0};
	uint32_t envPeriod{1};
	uint8_t envLevel{0};

	void stepTone(int ch, uint16_t period, uint32_t &phase, bool &out) {
		if (period == 0) period = 1;
		if (++phase >= period) { phase = 0; out = !out; }
	}
	void stepNoise() {
		uint16_t period = regs[6] & 0x1F; if (period == 0) period = 1;
		if (++cntN >= period) {
			cntN = 0;
			// 17-bit LFSR taps 0 and 3 (AY compatible-ish)
			uint32_t bit = ((noiseLfsr ^ (noiseLfsr >> 3)) & 1);
			noiseLfsr = (noiseLfsr >> 1) | (bit << 16);
			noiseOut = (noiseLfsr & 1) != 0;
		}
	}
	void stepEnv() {
		uint16_t per = (uint16_t(regs[0x0B]) | (uint16_t(regs[0x0C]) << 8)); if (per == 0) per = 1;
		if (++envPos >= per) { envPos = 0; // simplistic saw
			uint8_t shape = regs[0x0D] & 0x0F;
			if (shape & 0x08) { // continue
				if (envLevel > 0) envLevel--; else envLevel = 15;
			} else {
				if (envLevel < 15) envLevel++; else envLevel = 0;
			}
		}
	}
};