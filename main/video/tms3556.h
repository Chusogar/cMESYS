// tms3556.h - TI TMS3556 VDP (EXL-100 / EXELTEL): C port of dsp-cpp's
// video/tms3556 (after MAME's, Raphael Nabet). 32 KB of VRAM (64 KB
// addressed), text (40 x 25 of 10-line characters, four pattern tables,
// double width / height, blink), bitmap (320 x 250, 8 colours) and mixed
// modes. The picture is 336 x 252 with the border, one line drawn per call
// of tms3556_line (313 lines a frame); each dot is a colour 0-7 (bit 0 red,
// 1 green, 2 blue).
#ifndef TMS3556_VDP_H
#define TMS3556_VDP_H
#include <stdbool.h>
#include <stdint.h>

#define TMS3556_W 336
#define TMS3556_H 252
#define TMS3556_LINES 313

typedef struct {
    uint8_t *vram;                  // 64 KB
    uint8_t *fb;                    // TMS3556_W x TMS3556_H colours
    uint8_t control_regs[8];
    uint16_t address_regs[8];
    uint8_t reg, reg2;
    bool row_col_written, bamp_written, init_read;
    uint16_t colrow;
    bool dma_read;                  // the ACMPXY mode: read (else write)
    uint16_t acmpxy, acmp;
    int scanline, blink, blink_count, bg_color, name_offset, cg_flag, char_line_counter;
    uint8_t dbl_h_phase[40];
} tms3556;

void tms3556_init(tms3556 *v, uint8_t *vram64k, uint8_t *fb);
void tms3556_reset(tms3556 *v);
uint8_t tms3556_vram_r(tms3556 *v);
void tms3556_vram_w(tms3556 *v, uint8_t d);
uint8_t tms3556_reg_r(tms3556 *v);
void tms3556_reg_w(tms3556 *v, uint8_t d);
uint8_t tms3556_initptr_r(tms3556 *v);
void tms3556_line(tms3556 *v);       // the next scan line
static inline uint32_t tms3556_rgb(uint8_t c)
{
    return 0xff000000u | ((c & 1) ? 0xff0000u : 0) | ((c & 2) ? 0xff00u : 0) | ((c & 4) ? 0xffu : 0);
}
#endif
