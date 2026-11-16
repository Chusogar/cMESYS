// md_machine.h - the Sega Mega Drive / Genesis machine: a C11 migration of
// dsp-cpp's drivers/consoles/genesis. The 68000 (Musashi), the Z80
// (cpu/z80dsp), the 315-5313 VDP (video/sega_315_5313, the SN76489 inside),
// the YM2612 (sound/ym2612), the 3-button pads, cartridge loading
// (.bin/.md/.gen/.smd, byteswapped, copier header), SRAM and the SSF2 mapper.
// The 32X (drivers/consoles/sega32x) runs on top of it through the ext hooks.
#ifndef MD_MACHINE_H
#define MD_MACHINE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../video/sega_315_5313.h"
#include "../../../sound/ym2612.h"
#include "../../../cpu/z80dsp/z80dsp.h"

#define MD_MASTER_NTSC 53693175
#define MD_MASTER_PAL 53203424
#define MD_FPS_NTSC 59.922743
#define MD_FPS_PAL 49.701460
#define MD_RATE 44100
#define MD_MAX_ROM 0x800000
#define MD_AUDIO_MAX 1024

enum { MD_JAPAN = 0, MD_USA = 1, MD_EUROPE = 2 };

typedef struct {
    // the 32X's hooks (NULL: none); return true when they took the access
    bool (*ext_read16)(uint32_t a, uint16_t *v);
    bool (*ext_write16)(uint32_t a, uint16_t v);
    bool (*ext_read8)(uint32_t a, uint8_t *v);
    bool (*ext_write8)(uint32_t a, uint8_t v);
    int32_t (*ext_audio)(void);
    void (*ext_line)(int line);        // after the VDP drew the line, before it is copied
    uint16_t *fb;                      // MD_VDP_W x MD_VDP_H (set by the caller)

    md_vdp vdp;
    ym2612 ym;
    zd_z80 z80;
    int region;
    bool pal;
    uint8_t version_reg;
    uint8_t *rom;
    size_t rom_size;                   // padded to a power of two
    uint32_t rom_mask, rom_banks[8];
    bool ssf2_mapper;
    bool sram_present, sram_enabled;
    uint32_t sram_start, sram_end;
    uint8_t sram[0x10000];
    uint8_t ram[0x10000];
    uint8_t z80_ram[0x2000];
    uint8_t io_data[16], io_ctrl[16];
    bool z80_has_bus, z80_is_reset;
    uint16_t z80_bank;
    bool hint_on, vint_on;
    int cycles_on_line, m68k_debt, z80_debt;
    bool in_cpu;
    uint64_t audio_acc;
    int16_t audio[MD_AUDIO_MAX];
    int naudio;
    unsigned pad[2];                   // CP_* bits
    bool loaded;
} md_machine;

extern md_machine MD;

void md_init(int region);
void md_set_region(int region);
void md_reset(void);
bool md_load(uint8_t *data, size_t size, const char **why);   // takes `data`
void md_eject(void);
int md_run_frame(void);                 // the samples made (MD.audio)
uint16_t md_read_word(uint32_t a);
void md_write_word(uint32_t a, uint16_t v);
uint8_t md_read_byte(uint32_t a);
void md_write_byte(uint32_t a, uint8_t v);
static inline uint32_t md_m68k_clock(void) { return (MD.pal ? MD_MASTER_PAL : MD_MASTER_NTSC) / 7; }
static inline double md_fps(void) { return MD.pal ? MD_FPS_PAL : MD_FPS_NTSC; }
#endif
