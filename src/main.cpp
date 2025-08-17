#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

#ifndef ZX_HEADLESS
#include <SDL.h>
#endif

#include "z80.h"
#include "memory.h"
#include "ula.h"
#include "keyboard.h"
#include "beeper.h"
#include "snapshot_sna.h"

static bool load_file_to_buffer(const std::string &path, std::vector<uint8_t> &out) {
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

struct ZXMachine {
	Z80Cpu cpu{};
	Memory memory{};
	ULA ula{};
	Keyboard keyboard{};
	Beeper beeper{};
	uint64_t tstate_counter{0};

	bool init(const std::string &romPath) {
		std::vector<uint8_t> rom;
		if (!load_file_to_buffer(romPath, rom)) {
			std::fprintf(stderr, "Failed to load ROM from %s\n", romPath.c_str());
			return false;
		}
		if (rom.size() != 16384) {
			std::fprintf(stderr, "ROM must be exactly 16KB (16384 bytes).\n");
			return false;
		}
		memory.reset();
		memory.load_rom(rom.data(), rom.size());

		cpu.reset();
		cpu.connectMemory(
			[this](uint16_t addr) { return memory.read(addr); },
			[this](uint16_t addr, uint8_t value) { memory.write(addr, value); }
		);
		cpu.connectIO(
			[this](uint16_t port) { return this->io_read(port); },
			[this](uint16_t port, uint8_t value) { this->io_write(port, value); }
		);

		ula.reset();
		ula.connectMemory(&memory);
		keyboard.reset();
		beeper.reset(44100); // default; will be updated by actual audio rate

		return true;
	}

	uint8_t io_read(uint16_t port) {
		// 0xFE is keyboard/ear/ULA
		if ((port & 0x0001) == 0) {
			uint8_t k = keyboard.readRow(static_cast<uint8_t>((port >> 8) & 0xFF));
			uint8_t earMic = 0x40; // EAR bit 6: floating high if no tape
			return (k & 0x1F) | earMic | (ula.borderColour() & 0x07) << 0; // lower bits ignored on read in real HW
		}
		return 0xFF; // floating bus default
	}

	void io_write(uint16_t port, uint8_t value) {
		if ((port & 0x0001) == 0) {
			ula.setBorderColour(value & 0x07);
			beeper.setLevel((value & 0x10) ? 1.0f : 0.0f);
		}
	}

	int stepInstruction() {
		int t = cpu.step();
		tstate_counter += static_cast<uint64_t>(t);
		ula.tick(static_cast<uint32_t>(t));
		return t;
	}
};

#ifndef ZX_WITH_SDL
int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "Usage: %s <48k_rom_file> [snapshot.sna]\n", argv[0]);
		return 1;
	}
	ZXMachine zx;
	if (!zx.init(argv[1])) return 1;
	if (argc >= 3) {
		std::vector<uint8_t> buf;
		if (load_file_to_buffer(argv[2], buf)) {
			loadSNA48(buf, zx.cpu, zx.memory);
		} else {
			std::fprintf(stderr, "Warning: failed to load snapshot: %s\n", argv[2]);
		}
	}
	for (;;) {
		zx.stepInstruction();
	}
	return 0;
}
#else

struct SdlContext {
	SDL_Window *win{nullptr};
	SDL_Renderer *ren{nullptr};
	SDL_Texture *tex{nullptr};
	SDL_AudioDeviceID audioDev{0};
	int scale{3};
	int texW{256};
	int texH{192};
	std::vector<uint32_t> framebuffer; // ARGB8888
};

static void audio_callback(void *userdata, Uint8 *stream, int len) {
	ZXMachine *zx = reinterpret_cast<ZXMachine*>(userdata);
	int16_t *out = reinterpret_cast<int16_t*>(stream);
	int samples = len / sizeof(int16_t);
	for (int i = 0; i < samples; ++i) {
		float v = zx->beeper.sample(); // -1..+1
		int16_t s = static_cast<int16_t>(v * 3000); // quiet level
		out[i] = s;
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "Usage: %s <48k_rom_file> [snapshot.sna]\n", argv[0]);
		return 1;
	}
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	SdlContext sdl;
	ZXMachine zx;
	if (!zx.init(argv[1])) return 1;

	if (argc >= 3) {
		std::vector<uint8_t> buf;
		if (load_file_to_buffer(argv[2], buf)) {
			loadSNA48(buf, zx.cpu, zx.memory);
		} else {
			std::fprintf(stderr, "Warning: failed to load snapshot: %s\n", argv[2]);
		}
	}

	sdl.framebuffer.resize(sdl.texW * sdl.texH);
	sdl.win = SDL_CreateWindow("ZX Spectrum 48K", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, sdl.texW * sdl.scale, sdl.texH * sdl.scale, SDL_WINDOW_RESIZABLE);
	sdl.ren = SDL_CreateRenderer(sdl.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	sdl.tex = SDL_CreateTexture(sdl.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sdl.texW, sdl.texH);

	SDL_AudioSpec want{}; SDL_AudioSpec have{};
	want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_callback; want.userdata = &zx;
	sdl.audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (sdl.audioDev != 0) {
		SDL_PauseAudioDevice(sdl.audioDev, 0);
		zx.beeper.reset(have.freq);
	}

	bool running = true;
	auto last = std::chrono::high_resolution_clock::now();
	const int tstatesPerFrame = 3500000 / 50; // 70,000
	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) running = false;
			if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
				bool down = (e.type == SDL_KEYDOWN);
				zx.keyboard.handleHostKey(e.key.keysym.sym, down);
			}
		}

		int tstates = 0;
		while (tstates < tstatesPerFrame) {
			int t = zx.stepInstruction();
			tstates += t;
		}
		zx.ula.renderFrame(sdl.framebuffer.data(), sdl.texW, sdl.texH);

		SDL_UpdateTexture(sdl.tex, nullptr, sdl.framebuffer.data(), sdl.texW * sizeof(uint32_t));
		SDL_RenderClear(sdl.ren);
		SDL_RenderCopy(sdl.ren, sdl.tex, nullptr, nullptr);
		SDL_RenderPresent(sdl.ren);
	}

	if (sdl.audioDev) SDL_CloseAudioDevice(sdl.audioDev);
	if (sdl.tex) SDL_DestroyTexture(sdl.tex);
	if (sdl.ren) SDL_DestroyRenderer(sdl.ren);
	if (sdl.win) SDL_DestroyWindow(sdl.win);
	SDL_Quit();
	return 0;
}
#endif