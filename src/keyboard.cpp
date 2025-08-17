#include "keyboard.h"

// Minimal mapping for letters, numbers, and controls
// ZX rows (from docs):
// Row 0: SHIFT Z X C V
// Row 1: A S D F G
// Row 2: Q W E R T
// Row 3: 1 2 3 4 5
// Row 4: 0 9 8 7 6
// Row 5: P O I U Y
// Row 6: ENTER L K J H
// Row 7: SPACE SYM M N B

#ifndef ZX_WITH_SDL
void Keyboard::handleHostKey(int, bool) {}
#else
#include <SDL.h>

static void set_key(uint8_t &row, int bit, bool pressed) {
	if (pressed) row &= uint8_t(~(1 << bit)); else row |= uint8_t(1 << bit);
}

void Keyboard::handleHostKey(int keysym, bool pressed) {
	switch (keysym) {
		case SDLK_LSHIFT: case SDLK_RSHIFT: set_key(rows[0], 0, pressed); break; // CAPS SHIFT
		case SDLK_z: set_key(rows[0], 1, pressed); break;
		case SDLK_x: set_key(rows[0], 2, pressed); break;
		case SDLK_c: set_key(rows[0], 3, pressed); break;
		case SDLK_v: set_key(rows[0], 4, pressed); break;
		case SDLK_a: set_key(rows[1], 0, pressed); break;
		case SDLK_s: set_key(rows[1], 1, pressed); break;
		case SDLK_d: set_key(rows[1], 2, pressed); break;
		case SDLK_f: set_key(rows[1], 3, pressed); break;
		case SDLK_g: set_key(rows[1], 4, pressed); break;
		case SDLK_q: set_key(rows[2], 0, pressed); break;
		case SDLK_w: set_key(rows[2], 1, pressed); break;
		case SDLK_e: set_key(rows[2], 2, pressed); break;
		case SDLK_r: set_key(rows[2], 3, pressed); break;
		case SDLK_t: set_key(rows[2], 4, pressed); break;
		case SDLK_1: set_key(rows[3], 0, pressed); break;
		case SDLK_2: set_key(rows[3], 1, pressed); break;
		case SDLK_3: set_key(rows[3], 2, pressed); break;
		case SDLK_4: set_key(rows[3], 3, pressed); break;
		case SDLK_5: set_key(rows[3], 4, pressed); break;
		case SDLK_0: set_key(rows[4], 0, pressed); break;
		case SDLK_9: set_key(rows[4], 1, pressed); break;
		case SDLK_8: set_key(rows[4], 2, pressed); break;
		case SDLK_7: set_key(rows[4], 3, pressed); break;
		case SDLK_6: set_key(rows[4], 4, pressed); break;
		case SDLK_p: set_key(rows[5], 0, pressed); break;
		case SDLK_o: set_key(rows[5], 1, pressed); break;
		case SDLK_i: set_key(rows[5], 2, pressed); break;
		case SDLK_u: set_key(rows[5], 3, pressed); break;
		case SDLK_y: set_key(rows[5], 4, pressed); break;
		case SDLK_RETURN: set_key(rows[6], 0, pressed); break;
		case SDLK_l: set_key(rows[6], 1, pressed); break;
		case SDLK_k: set_key(rows[6], 2, pressed); break;
		case SDLK_j: set_key(rows[6], 3, pressed); break;
		case SDLK_h: set_key(rows[6], 4, pressed); break;
		case SDLK_SPACE: set_key(rows[7], 0, pressed); break;
		case SDLK_LCTRL: case SDLK_RCTRL: set_key(rows[7], 1, pressed); break; // SYMBOL SHIFT mapped to CTRL
		case SDLK_m: set_key(rows[7], 2, pressed); break;
		case SDLK_n: set_key(rows[7], 3, pressed); break;
		case SDLK_b: set_key(rows[7], 4, pressed); break;
		default: break;
	}
}
#endif