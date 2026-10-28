// ql_core.h - Sinclair QL (UK, JS ROM): 68008 + ZX8301 video + ZX8302
// peripheral chip + 8749 IPC (keyboard, sound, serial) + 2 microdrives.
// Port of dsp-cpp's drivers/computers/ql (from MAME's sinclair/ql.cpp).
//
// The 68008 runs on Musashi (68000 timing, as dsp-cpp); the IPC, the serial
// clock (BAUDx4) and the microdrive bit clock advance in lockstep with it,
// a slice of a few instructions at a time.
#ifndef QL_CORE_H
#define QL_CORE_H

#include <stdint.h>
#include <stdbool.h>

#define QL_W            512
#define QL_H            256
#define QL_CPU_CLOCK    7500000
#define QL_IPC_CLOCK    11000000
#define QL_FPS          50.08
#define QL_FRAME_CYCLES 149760          // 7.5 MHz / 50.08 Hz
#define QL_AUDIO_RATE   22050
#define QL_AUDIO_MAX    480             // samples per frame (at most)
#define QL_ROM_BYTES    0xc000          // JS: ql.js 0000.ic33 + ql.js 8000.ic34
#define QL_IPC_BYTES    0x800           // ipc8049.ic24
#define QL_RAM_BLOCKS   4               // 128 KB at $20000, as 4 x 32 KB

// Keyboard matrix: row (0-7, MAME's Y0..Y7) and bit, active high.
#define QL_KEY(row, bit) (((row) << 3) | (bit))
enum {
    QLK_F4 = QL_KEY(0, 0), QLK_F1 = QL_KEY(0, 1), QLK_5 = QL_KEY(0, 2), QLK_F2 = QL_KEY(0, 3),
    QLK_F3 = QL_KEY(0, 4), QLK_F5 = QL_KEY(0, 5), QLK_4 = QL_KEY(0, 6), QLK_7 = QL_KEY(0, 7),
    QLK_ENTER = QL_KEY(1, 0), QLK_LEFT = QL_KEY(1, 1), QLK_UP = QL_KEY(1, 2), QLK_ESC = QL_KEY(1, 3),
    QLK_RIGHT = QL_KEY(1, 4), QLK_BACKSLASH = QL_KEY(1, 5), QLK_SPACE = QL_KEY(1, 6), QLK_DOWN = QL_KEY(1, 7),
    QLK_RBRACKET = QL_KEY(2, 0), QLK_Z = QL_KEY(2, 1), QLK_PERIOD = QL_KEY(2, 2), QLK_C = QL_KEY(2, 3),
    QLK_B = QL_KEY(2, 4), QLK_POUND = QL_KEY(2, 5), QLK_M = QL_KEY(2, 6), QLK_QUOTE = QL_KEY(2, 7),
    QLK_LBRACKET = QL_KEY(3, 0), QLK_CAPS = QL_KEY(3, 1), QLK_K = QL_KEY(3, 2), QLK_S = QL_KEY(3, 3),
    QLK_F = QL_KEY(3, 4), QLK_EQUALS = QL_KEY(3, 5), QLK_G = QL_KEY(3, 6), QLK_SEMICOLON = QL_KEY(3, 7),
    QLK_L = QL_KEY(4, 0), QLK_3 = QL_KEY(4, 1), QLK_H = QL_KEY(4, 2), QLK_1 = QL_KEY(4, 3),
    QLK_A = QL_KEY(4, 4), QLK_P = QL_KEY(4, 5), QLK_D = QL_KEY(4, 6), QLK_J = QL_KEY(4, 7),
    QLK_9 = QL_KEY(5, 0), QLK_W = QL_KEY(5, 1), QLK_I = QL_KEY(5, 2), QLK_TAB = QL_KEY(5, 3),
    QLK_R = QL_KEY(5, 4), QLK_MINUS = QL_KEY(5, 5), QLK_Y = QL_KEY(5, 6), QLK_O = QL_KEY(5, 7),
    QLK_8 = QL_KEY(6, 0), QLK_2 = QL_KEY(6, 1), QLK_6 = QL_KEY(6, 2), QLK_Q = QL_KEY(6, 3),
    QLK_E = QL_KEY(6, 4), QLK_0 = QL_KEY(6, 5), QLK_T = QL_KEY(6, 6), QLK_U = QL_KEY(6, 7),
    QLK_SHIFT = QL_KEY(7, 0), QLK_CTRL = QL_KEY(7, 1), QLK_ALT = QL_KEY(7, 2), QLK_X = QL_KEY(7, 3),
    QLK_V = QL_KEY(7, 4), QLK_SLASH = QL_KEY(7, 5), QLK_N = QL_KEY(7, 6), QLK_COMMA = QL_KEY(7, 7),
};

// rom: 48 KB system ROM; ipc: 2 KB 8749 program; ram: 4 blocks of 32 KB
// (separate, so the ESP32 can place them where it has room). Both ROMs are
// read in place (flash is fine). unix_time starts the real time clock.
void ql_init(const uint8_t *rom, const uint8_t *ipc, uint8_t *const ram[QL_RAM_BLOCKS], uint32_t unix_time);
void ql_reset(void);
// One 50.08 Hz frame (vsync interrupt at its end). Returns the number of
// audio samples (QL_AUDIO_RATE, mono) left in *audio.
int ql_run_frame(const int16_t **audio);

// Keyboard (QLK_* codes), all keys up with ql_keys_clear().
void ql_key(int code, bool down);
void ql_keys_clear(void);

// Microdrive cartridges: drive 0 = mdv1, 1 = mdv2. image: a QLAY .mdv
// (174930 bytes, kept by the caller), NULL takes the cartridge out.
void ql_cart_insert(int drive, const uint8_t *image);
// On-demand cartridge (SD file, etc.). fn NULL ejects. See ql_mdv_insert_reader.
typedef int (*ql_cart_sector_fn)(void *user, unsigned sector, uint8_t dest[686]);
void ql_cart_insert_reader(int drive, ql_cart_sector_fn fn, void *user);
bool ql_cart_loaded(int drive);
bool ql_cart_motor(int drive);
// Presses F1 (monitor) at the start-up prompt after a reset: with a cartridge
// in mdv1, QDOS then loads and runs its BOOT program.
#define QL_AUTOBOOT_FRAME 110
void ql_autoboot(void);
bool ql_autobooting(void);
// The MDV button: puts image in mdv1 (NULL takes it out), resets the QL and
// boots it (ql_autoboot).
void ql_mount(const uint8_t *image);
void ql_mount_reader(ql_cart_sector_fn fn, void *user);

// Picture: the 32 KB screen shown, the ZX8301 control register and the
// flash phase (mode 8); zx8301_render_row() turns rows into colours.
const uint8_t *ql_screen(void);
uint8_t ql_video_control(void);
bool ql_flash(void);
// Rows of the shown screen written since the last call (bit y of 256 bits;
// everything after a mode / screen change or a flash toggle in mode 8).
void ql_take_dirty_rows(uint32_t rows[8]);

uint32_t ql_pc(void);
uint16_t ql_ipc_pc(void);
extern volatile uint32_t ql_diag_slices;
uint8_t ql_peek(uint32_t address);

#endif
