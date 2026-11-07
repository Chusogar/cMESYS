// mos6569.h - MOS 6569 VIC-II (PAL) as dsp-cpp's machine/mos6566 (from
// mos6566.pas of the DSP emulator): the registers, raster interrupt, bad
// lines (40 cycles stolen), text / multicolour text / extended colour /
// bitmap / multicolour bitmap a line at a time, sprites with priorities and
// collisions. C11 port.
#ifndef MOS6569_H
#define MOS6569_H
#include <stdbool.h>
#include <stdint.h>

#define VIC_W 384                       // the picture: 384 x 270
#define VIC_H 270
#define VIC_VISIBLE_X 48

extern const uint32_t vic_palette[16];  // 0xRRGGBB
extern const uint16_t vic_palette565[16];

typedef struct {
    uint8_t (*mem)(void *user, uint16_t addr);  // the VIC's bus (VA14/15 merged in)
    void (*irq)(void *user, bool on);
    void *user;
    const uint8_t *color_ram;

    uint16_t linea, irq_raster;
    uint8_t ctrl1, ctrl2, irq_flag, irq_mask;
    uint8_t me, mxe, mye, mdp, mmc;
    uint8_t ec, b0c, b1c, b2c, b3c, mm0, mm1;
    uint8_t mx[8], my[8], sc[8];
    uint8_t mx8, vbase;
    uint16_t cia_va;
    uint16_t vc, vc_base, rc;
    bool bad_lines;
    uint8_t clx_spr, clx_bgr;
    uint8_t spr_coll[VIC_W], fore_mask[VIC_W];
} mos6569;

void mos6569_reset(mos6569 *v);
uint8_t mos6569_read(mos6569 *v, uint8_t reg);
void mos6569_write(mos6569 *v, uint8_t reg, uint8_t value);
static inline void mos6569_changed_va(mos6569 *v, uint16_t va) { v->cia_va = (uint16_t)((va & 3) << 14); }
// Raster line 0..311; row (VIC_W palette indices) or NULL; returns the cycles stolen
int mos6569_update_line(mos6569 *v, int line, uint8_t *row);

#endif
