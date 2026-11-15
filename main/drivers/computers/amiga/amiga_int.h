// amiga_int.h - what amiga_core.c and amiga_chipset.c share (internal).
#ifndef AMIGA_INT_H
#define AMIGA_INT_H
#include <stdbool.h>
#include <stdint.h>
#include "amiga_core.h"

#define AMIGA_CHIP_SIZE 0x80000u
#define AMIGA_FIRST_LINE 0x2C       // picture origin in beam coordinates (the PAL window)
#define AMIGA_FIRST_HPOS 0x81
#define AMIGA_MAX_FETCH 64

typedef struct {
    uint32_t lc, pt;
    uint16_t len, per, vol, dat;
    uint32_t words_left;
    int counter;                    // colour clocks left for the current sample
    int byte;                       // 0: high byte playing, 1: low byte
    int8_t out;                     // sample on the DAC
    bool dma_on;
    bool manual;                    // CPU-fed (AUDxDAT written with DMA off)
    bool manual_next;               // another CPU word is waiting
    uint16_t manual_dat;
} amiga_aud;

// Agnus / Denise / Paula (dsp-cpp's AmigaChipset)
typedef struct {
    uint8_t *chip;                  // chip RAM (big-endian bytes, AMIGA_CHIP_SIZE)
    uint16_t *fb;                   // picture (RGB565)

    uint16_t dmacon, intena, intreq, adkcon;
    uint16_t dsklen, dsksync;
    uint32_t dskpt;
    uint32_t cop1lc, cop2lc, coppc;
    uint16_t diwstrt, diwstop, ddfstrt, ddfstop;
    uint16_t bplcon0, bplcon1, bplcon2;
    int16_t bpl1mod, bpl2mod;
    uint32_t bplpt[6];
    uint16_t color[32];
    uint16_t line_words[6][AMIGA_MAX_FETCH];

    uint16_t bltcon0, bltcon1, bltafwm, bltalwm;
    uint32_t bltapt, bltbpt, bltcpt, bltdpt;
    int16_t bltamod, bltbmod, bltcmod, bltdmod;
    uint16_t bltadat, bltbdat, bltcdat, bltsize;
    bool bzero;

    int vpos;
    bool lof;
    uint16_t joy0dat, joy1dat;
    bool rmb;
    bool ciaa_irq, ciab_irq;
    bool cop_stopped, copper_active;

    amiga_aud aud[4];
    bool aud_irq;
    int32_t aud_sum, aud_cck;

    uint32_t sprpt[8];
    uint16_t sprpos[8], sprctl[8], sprdata[8], sprdatb[8];
    // this line's sprites (taken by the sprite DMA, drawn by render_line)
    uint16_t sl_data[8], sl_datb[8], sl_pos[8], sl_ctl[8];
    uint8_t sl_on[8];
} amiga_chips;

void ach_reset(amiga_chips *c);
uint16_t ach_read(amiga_chips *c, uint16_t reg);
void ach_write(amiga_chips *c, uint16_t reg, uint16_t value);
void ach_begin_frame(amiga_chips *c);
void ach_copper_line(amiga_chips *c, int vpos);
void ach_render_line(amiga_chips *c, int vpos);
int ach_ipl(const amiga_chips *c);
bool ach_audio_run(amiga_chips *c, int cck);
int ach_audio_take_sample(amiga_chips *c);

// from amiga_core.c: the MFM of the track under the head of the selected,
// turning drive (NULL: none) and JOYTEST
const uint16_t *amiga_track_mfm(int *words);
int amiga_disk_pos(int words);          // the word under the head now (the disk turns at 300 rpm)
void amiga_joytest(uint16_t v);
int amiga_hpos(void);                   // the beam's horizontal position (colour clocks, 0-226)
#endif
