// specnext_core.h - ZX Spectrum Next (TBBlue core 3.02, "Emulators" machine
// id 8, 2 MB RAM): C port of dsp-cpp's drivers/computers/specnext.
//
// Z80N at 3.5 / 7 / 14 / 28 MHz, 2 MB of SRAM in 8 KB pages behind the MMU
// (the 128K / +3 / Pentagon paging translated onto it), the boot ROM, config
// mode, DivMMC automap, the Multiface, the Next registers, the ULA (Timex
// modes, ULANext, ULA+), LoRes, Layer 2 (256x192, 320x256, 640x256), the
// 40 / 80 column tilemap, 128 sprites with anchors and relatives, the copper,
// the zxnDMA, the CTC, the hardware IM2 interrupt controller, three AY
// (TurboSound), four DACs, the beeper and an SD card in SPI mode.
//
// Booting needs the boot ROM (boot-30204.bin of MAME's tbblue set) and an
// SD card image with the system (TBBLUE.FW, NextZXOS): the boot ROM loads
// the firmware from the card, the firmware the NextZXOS ROMs into SRAM, as
// on the real board. A .nex program is loaded once NextZXOS has started.
//
// The picture: the 320x256 VGA area (paper + 32 pixel border) drawn 640
// pixels across, so the hi-res modes keep every pixel (each line shown twice).
#ifndef SPECNEXT_CORE_H
#define SPECNEXT_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include "blkfile.h"

#define SN_W 640
#define SN_H 256
#define SN_CLOCK 28000000
#if defined(ESP_PLATFORM) || defined(PICO_ON_DEVICE) || defined(PICO_BAND)
#define SN_AUDIO_RATE 22050
#else
#define SN_AUDIO_RATE 44100
#endif
#define SN_AUDIO_MAX 2048
#define SN_MEDIA_EXTS { ".img", ".mmc", ".hdf", ".sd", ".nex", NULL }
#define SN_MEDIA_LABEL "IMG MMC HDF NEX"

// Keys: the 40 of the Spectrum matrix (row * 8 + bit, rows as port $FE's
// address lines A8..A15) and the Next's extra keys (nextregs $B0 / $B1:
// each one a Spectrum combination too), and a few PC conveniences.
enum {
    NK_CAPS_SHIFT = 0x00, NK_Z = 0x01, NK_X = 0x02, NK_C = 0x03, NK_V = 0x04,
    NK_A = 0x08, NK_S = 0x09, NK_D = 0x0a, NK_F = 0x0b, NK_G = 0x0c,
    NK_Q = 0x10, NK_W = 0x11, NK_E = 0x12, NK_R = 0x13, NK_T = 0x14,
    NK_1 = 0x18, NK_2 = 0x19, NK_3 = 0x1a, NK_4 = 0x1b, NK_5 = 0x1c,
    NK_0 = 0x20, NK_9 = 0x21, NK_8 = 0x22, NK_7 = 0x23, NK_6 = 0x24,
    NK_P = 0x28, NK_O = 0x29, NK_I = 0x2a, NK_U = 0x2b, NK_Y = 0x2c,
    NK_ENTER = 0x30, NK_L = 0x31, NK_K = 0x32, NK_J = 0x33, NK_H = 0x34,
    NK_SPACE = 0x38, NK_SYM_SHIFT = 0x39, NK_M = 0x3a, NK_N = 0x3b, NK_B = 0x3c,
    // the Next's extra keys ($B0 bits 7..0, $B1 bits 7..0)
    NK_SEMICOLON = 0x40, NK_QUOTE, NK_COMMA, NK_PERIOD, NK_UP, NK_DOWN, NK_LEFT, NK_RIGHT,
    NK_DELETE = 0x48, NK_EDIT, NK_BREAK, NK_INV_VIDEO, NK_TRUE_VIDEO, NK_GRAPH, NK_CAPS_LOCK, NK_EXTEND,
    // PC keys typed as Symbol Shift combinations
    NK_MINUS = 0x50, NK_EQUALS, NK_PLUS, NK_SLASH, NK_ASTERISK,
    NK_COUNT = 0x58
};
// Joystick bits (Kempston / MD order)
enum { SN_JOY_RIGHT = 1, SN_JOY_LEFT = 2, SN_JOY_DOWN = 4, SN_JOY_UP = 8, SN_JOY_FIRE = 16, SN_JOY_FIRE2 = 32,
       SN_JOY_FIRE3 = 64, SN_JOY_START = 128 };

// The boot ROM (8 KB, kept); false: out of memory (2 MB)
bool specnext_init(const uint8_t *boot_rom);
void specnext_hard_reset(void);         // power on
void specnext_soft_reset(void);         // the RESET button
void specnext_nmi_buttons(bool multiface, bool drive);   // the two NMI buttons (held)
int specnext_run_frame(const int16_t **audio);
double specnext_fps(void);
void specnext_set_turbo(bool on);       // (no sound then)
void specnext_key(int code, bool down);
void specnext_keys_clear(void);
void specnext_joy(int port, uint8_t bits);   // port 0 (left) or 1
void specnext_render_row(int y, uint32_t *out);    // SN_W dots, 0xRRGGBB
void specnext_render_row565(int y, uint16_t *out);
// The SD card (taken: closed on eject); NULL: in (and a hard reset)
const char *specnext_insert_sd(blkfile *f);
void specnext_eject_sd(void);
bool specnext_sd_inserted(void);
bool specnext_sd_busy(void);            // sectors read or written in the last frames
// A .nex program (copied): now, or once NextZXOS has started with an SD card
const char *specnext_load_nex(const uint8_t *data, uint32_t size);
// An SD image (.img .mmc .hdf .sd) or a .nex, by its name
const char *specnext_load_file(const char *path);
#endif
