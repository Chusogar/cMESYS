#include <cstdio>
#include <vector>
#include <string>
#include <SDL2/SDL.h>
#include "minivadr.h"

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
	FILE *f = std::fopen(path.c_str(), "rb"); if (!f) return false;
	std::fseek(f, 0, SEEK_END); long size = std::ftell(f); if (size < 0) { std::fclose(f); return false; }
	std::rewind(f); out.resize((size_t)size); size_t rd = std::fread(out.data(),1,out.size(),f); std::fclose(f); return rd==out.size();
}

int main(int argc, char **argv) {
	if (argc < 2) { std::fprintf(stderr, "Usage: %s minivadr.rom\n", argv[0]); return 1; }
	std::vector<uint8_t> rom; if (!read_file(argv[1], rom)) { std::fprintf(stderr, "Failed to read ROM\n"); return 1; }
	MiniVadr mv; if (!mv.init(rom)) { std::fprintf(stderr, "Init failed\n"); return 1; }
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "SDL init fail: %s\n", SDL_GetError()); return 1; }
	SDL_Window *win = SDL_CreateWindow("MiniVadr", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 256*3, 256*3, 0);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 256);
	std::vector<uint32_t> fb(256*256);

	bool running=true; while (running) {
		SDL_Event e; while (SDL_PollEvent(&e)) { if (e.type==SDL_QUIT) running=false; if (e.type==SDL_KEYDOWN||e.type==SDL_KEYUP){ bool down=e.type==SDL_KEYDOWN; uint8_t in=0xFF; if (!down) in=0xFF; else { if (e.key.keysym.sym==SDLK_LEFT) in&=~0x01; if (e.key.keysym.sym==SDLK_RIGHT) in&=~0x02; if (e.key.keysym.sym==SDLK_SPACE) in&=~0x10; } mv.setInputs(in); } }
		for (int i=0;i<1000;++i) mv.step();
		mv.render(fb);
		SDL_UpdateTexture(tex,nullptr,fb.data(),256*4); SDL_RenderClear(ren); SDL_RenderCopy(ren,tex,nullptr,nullptr); SDL_RenderPresent(ren);
	}
	SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return 0;
}