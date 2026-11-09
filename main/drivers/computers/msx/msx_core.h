// msx_core.h - MSX1 and MSX2, a C11 migration of dsp-cpp's
// drivers/computers/msx1 and msx2 (from the DSP emulator). Z80 at 3.58 MHz
// (cpu/z80dsp), the slots (A8, the MSX2's expanded slot 3 at FFFF and its
// memory mapper at FC-FF), the 8255, the AY-3-8910 (joysticks), the VDP
// (video/tms9918 on the MSX1, video/v9938 on the MSX2), the cartridge slot
// (machine/msx_cart: megaROM mappers and the Konami SCC), the disk ROM with
// its WD2793 (machine/msx_disk, on both when the ROM is there), the RTC
// (MSX2) and the cassette.
//
// Differences from dsp-cpp: the cassette is played through the BIOS (its
// TAPION / TAPIN / TAPIOF calls are answered from the file, as fMSX does):
// .cas files and the MSX blocks (0x4B) of .tsx / .tzx; and loading one
// types CLOAD / RUN"CAS:" / BLOAD"CAS:",R in the keyboard buffer once BASIC
// waits for a key. The MSX1 also takes the disk ROM (slot 2).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zx_pbuf.h"

#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define MSX_SMALL 1
#endif

#ifdef MSX_SMALL
#define MSX_PIC_W       320             /* 256 x 212 in the middle, the rest border */
#define MSX_PIC_H       240
#define MSX_AUDIO_RATE  22050
#else
#define MSX_PIC_W       512             /* 512 half dots x 212 lines, each line twice */
#define MSX_PIC_H       424
#define MSX_AUDIO_RATE  44100
#endif
#define MSX_AUDIO_MAX   1024
#define MSX_CLOCK       3579545

enum { MSX_1 = 1, MSX_2 = 2 };

/* Keys: row * 8 + bit of the keyboard matrix (international layout) */
#define MSX_KEY(row, bit) (((row) << 3) | (bit))
enum {
    MSXK_0 = MSX_KEY(0, 0), MSXK_1, MSXK_2, MSXK_3, MSXK_4, MSXK_5, MSXK_6, MSXK_7,
    MSXK_8 = MSX_KEY(1, 0), MSXK_9, MSXK_MINUS, MSXK_EQUALS, MSXK_BACKSLASH, MSXK_LBRACKET, MSXK_RBRACKET, MSXK_SEMICOLON,
    MSXK_QUOTE = MSX_KEY(2, 0), MSXK_GRAVE, MSXK_COMMA, MSXK_PERIOD, MSXK_SLASH, MSXK_DEAD, MSXK_A, MSXK_B,
    MSXK_C = MSX_KEY(3, 0), MSXK_D, MSXK_E, MSXK_F, MSXK_G, MSXK_H, MSXK_I, MSXK_J,
    MSXK_K = MSX_KEY(4, 0), MSXK_L, MSXK_M, MSXK_N, MSXK_O, MSXK_P, MSXK_Q, MSXK_R,
    MSXK_S = MSX_KEY(5, 0), MSXK_T, MSXK_U, MSXK_V, MSXK_W, MSXK_X, MSXK_Y, MSXK_Z,
    MSXK_SHIFT = MSX_KEY(6, 0), MSXK_CTRL, MSXK_GRAPH, MSXK_CAPS, MSXK_CODE, MSXK_F1, MSXK_F2, MSXK_F3,
    MSXK_F4 = MSX_KEY(7, 0), MSXK_F5, MSXK_ESC, MSXK_TAB, MSXK_STOP, MSXK_BS, MSXK_SELECT, MSXK_RETURN,
    MSXK_SPACE = MSX_KEY(8, 0), MSXK_HOME, MSXK_INS, MSXK_DEL, MSXK_LEFT, MSXK_UP, MSXK_DOWN, MSXK_RIGHT,
    MSXK_KP_MUL = MSX_KEY(9, 0), MSXK_KP_PLUS, MSXK_KP_DIV, MSXK_KP0, MSXK_KP1, MSXK_KP2, MSXK_KP3, MSXK_KP4,
    MSXK_KP5 = MSX_KEY(10, 0), MSXK_KP6, MSXK_KP7, MSXK_KP8, MSXK_KP9, MSXK_KP_MINUS, MSXK_KP_COMMA, MSXK_KP_PERIOD,
};
/* Joystick bits (active high here) */
enum { MSX_JOY_UP = 1, MSX_JOY_DOWN = 2, MSX_JOY_LEFT = 4, MSX_JOY_RIGHT = 8, MSX_JOY_A = 16, MSX_JOY_B = 32 };

#define MSX_MEDIA_EXTS { ".rom", ".mx1", ".mx2", ".dsk", ".cas", ".tsx", ".tzx", NULL }
#define MSX_MEDIA_LABEL "ROM DSK CAS TSX"

typedef struct msx msx;

/* ram_kb: the MSX2's memory mapper (64, 128 or 256; the MSX1 has 64);
 * vram_kb: the MSX2's VRAM (64 or 128) */
msx *msx_create(int model, int ram_kb, int vram_kb, int audio_rate);
void msx_destroy(msx *m);
/* bios 32 KB; sub 16 KB (MSX2); disk 16 KB or NULL */
void msx_set_roms(msx *m, const uint8_t *bios, const uint8_t *sub, const uint8_t *disk);
void msx_reset(msx *m);
int msx_run_frame(msx *m, const int16_t **audio);
double msx_fps(const msx *m);
int msx_model(const msx *m);

void msx_keys_clear(msx *m);
void msx_key(msx *m, int code, bool down);
void msx_joy(msx *m, int port, uint8_t bits);     /* port 1 or 2 */
/* Text into the BIOS keyboard buffer (\r = RETURN), once BASIC reads keys */
void msx_type(msx *m, const char *text);

/* Row y (0 .. MSX_PIC_H-1) of the picture, 0xRRGGBB or RGB565 */
void msx_render_row(const msx *m, int y, uint32_t *out);
void msx_render_row565(const msx *m, int y, uint16_t *out);
void msx_set_rgb565(msx *m, uint16_t (*fn)(uint8_t r, uint8_t g, uint8_t b));
/* The boards: rows written straight into the board's own picture (fn gives
 * row y of MSX_PIC_W RGB565 pixels) instead of kept for msx_render_row565 */
void msx_set_row_out(msx *m, uint16_t *(*fn)(int y));

bool msx_load_pbuf(msx *m, zx_pbuf *pb, const char *name);      /* takes the pages over */
bool msx_load_file(msx *m, const char *path);
const char *msx_last_error(const msx *m);
bool msx_has_disk_rom(const msx *m);
void msx_eject_cart(msx *m);
/* Frees what a file of this name will replace (the cartridge, the tape)
 * before it is read: the boards have no room for both */
void msx_drop_media_for(msx *m, const char *name);
void msx_set_turbo(msx *m, bool on);       /* no sound */
