// zx_internal.h - the machine state shared by the ZX Spectrum family drivers:
//   zx48.c     Spectrum48k  (dsp-cpp drivers/computers/spectrum)
//   zx128.c    Spectrum128k and Spectrum3 (spectrum_128k, spectrum_3)
//   zxclone.c  ZxClone: Pentagon, Pentagon 1024, Scorpion ZS-256 (zx_clone)
//   zxspec_core.c  the public API, the picture, sound and media
#ifndef ZX_INTERNAL_H
#define ZX_INTERNAL_H
#include <stdlib.h>
#include <string.h>
#include "zxspec_core.h"
#include "z80dsp/z80dsp.h"
#include "zx_tape.h"
#include "zx_rzx.h"
#include "zx_beta.h"
#include "zx_nec765.h"
#include "zx_snap.h"
#include "ay8910.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ZX_HOT IRAM_ATTR
#else
#define ZX_HOT
#endif

#define ZX_BANK 0x4000
#define ZX_MAX_T_LINE 228
#define ZX_MAX_PAGES 64

typedef enum { ZK_48, ZK_128, ZK_PLUS3, ZK_CLONE } zx_kind;

struct zxspec {
    zx_model_t model;
    zx_kind kind;
    zd_z80 cpu;
    zx_tape tape;
    zx_rzx rzx;
    zx_beta beta;           // clones
    zx_nec765 fdc;          // +3
    ay8910 ay[2];
    int nay;                // AY chips (2 on the 128K / +3: turbosound)
    bool ay1_used;          // the second one has been selected
    uint8_t ay_select;
    const char *error;

    // timing (per model)
    uint32_t clock;
    int t_line, lines, t_frame;
    int paper_first;        // first paper line (64 / 63 / 80)
    int pic_first;          // line drawn as picture row 0 (16 / 15 / 32)
    int left_t;             // left border: T-states left_t .. left_t+23 of the previous line
    double fps;

    // memory
    const uint8_t *rom[4];  // 16 KB ROM pages
    uint8_t *ram[ZX_MAX_PAGES];
    int ram_pages;
    // 48K: ram[5] ram[2] ram[0] are $4000 / $8000 / $C000
    // 128K / +3: marco = banks at the four slots (8+ = ROM page n-8)
    uint8_t marco[4];
    uint8_t pantalla;       // display bank (5 / 7)
    uint8_t port_7ffd, port_1ffd, port_dffd;
    bool paging_enabled;    // 128K / +3 (7FFD bit 5 locks)
    bool special_paging;    // +3 all-RAM
    // clones
    uint8_t ram3, rom_page;
    bool page0_ram, paging_locked, nmi_pending, magic_down, gluk_present, port_compat;
    // Interface 2 cartridge (128K / +3)
    uint8_t *if2_rom;       // 32 KB
    bool if2_switched;
    int64_t if2_delay;
    // +3 disk
    bool disk_present;
    bool tape_held;         // paused by the user: the ROM loader does not restart it

    // ULA
    uint8_t border, speaker, ear;
    uint8_t keys[8];
    uint8_t joy, sinclair;
    bool kempston_enabled;
    bool flash;
    int flash_count;
    int line, t_in_line, frame_t, instr_t_flushed;
    int last_mem_t;         // +3: T-state in the instruction of the last memory access
    uint8_t latch_pix[32], latch_attr[32];
    uint32_t latch_mask;
    uint8_t border_rows[2][ZX_MAX_T_LINE];
    uint8_t *border_cur, *border_prev;
    uint8_t border_seed;

    // ULA+ (48K / 128K / +3)
    bool ulaplus_enabled, ulaplus_active;
    uint8_t ulaplus_mode, ulaplus_last_reg;
    uint8_t ulaplus_pal[64];
    uint32_t palette_ext[80];   // 0x00RRGGBB: 0-15 classic, 16-79 ULA+
    uint16_t pal565[80];
    uint16_t (*rgb565)(uint8_t r, uint8_t g, uint8_t b);   // the display's RGB565 (CYD: byte-swapped, BGR)

    // picture: palette_ext indices, packed two a byte on the boards
    uint8_t *pic;
    int pic_w, pic_h, crop_x, crop_y;
    bool packed;
    uint8_t line_px[352];

    // sound
    int audio_rate;
    int64_t audio_acc;
    int16_t beeper_level;
    int16_t audio[ZX_AUDIO_MAX + 16];
    int naudio;
};

// zxspec_core.c: shared ULA pieces
extern const uint16_t zx_scr_table[192];
extern const uint8_t zx_empty_page[ZX_BANK];
void zx_start_border_frame(zxspec *m);
void zx_next_border_line(zxspec *m);
uint8_t zx_border_index(const zxspec *m);
void zx_emit_line(zxspec *m, int line);         // render_line of every model
void zx_on_cycles(zxspec *m, int cycles);       // the beam, tape, sound
int zx_ula_time(zxspec *m);                     // beam to the T-state inside the instruction
void zx_sync_to(zxspec *m, int t_in_instr);     // beam to a given T-state of the instruction
void zx_on_insn_cycles(void *user, int cycles); // Z80 cycle handler (48K / 128K / +3)
void zx_push_audio(zxspec *m, int cycles);
void zx_tape_cycles(zxspec *m, int cycles);
void zx_ulaplus_out(zxspec *m, uint16_t port, uint8_t value);
bool zx_ulaplus_in(zxspec *m, uint16_t port, uint8_t *result);
uint8_t zx_keys_read(const zxspec *m, uint16_t port);

// The beam only has to be brought to the current T-state before what
// depends on it: a contended access (its waits), screen memory (always
// contended), ports. Other accesses leave the T-states to be counted with the
// next of those or at the end of the instruction (the same result, far fewer
// calls), except while a loaded tape waits for the ROM loader: its start
// looks at PC at every step, as dsp-cpp's does.
static inline bool zx_must_sync(const zxspec *m)
{
    return m->tape.loaded && !(m->tape.playing && !m->tape.paused) && !m->tape_held;
}

// per model: set the timing, handlers and chips; reset; snapshots
void zx48_setup(zxspec *m);
void zx48_reset(zxspec *m);
void zx128_setup(zxspec *m);
void zx128_reset(zxspec *m);
void zx128_apply_snap(zxspec *m, const zx_snap *s);
void zxclone_setup(zxspec *m);
void zxclone_reset(zxspec *m);
void zxclone_apply_snap(zxspec *m, const zx_snap *s);
void zxclone_magic(zxspec *m);

#endif
