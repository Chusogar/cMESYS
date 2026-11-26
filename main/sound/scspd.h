// scspd.h - Yamaha YMF292-F "SCSP" (Model 2/3 sound boards): C port of
// dsp-cpp's sound/scsp (slot / EG / LFO / DSP code from MAME's scsp.cpp and
// scspdsp.cpp, BSD-3-Clause, ElSemi, R. Belmont). Timers advance one tick
// per 44.1 kHz output sample.
#ifndef SCSPD_H
#define SCSPD_H
#include <stdint.h>
#include <stdbool.h>

#define SCSP_RATE 44100

typedef struct {
    int volume;
    int state;
    int step;
    int ar, d1r, d2r, rr, dl;
    uint8_t eghold, lplink;
} scsp_eg;

typedef struct {
    uint16_t phase;
    uint32_t phase_step;
    const int *table, *scale;
} scsp_lfo;

typedef struct {
    uint16_t data[0x10];
    uint8_t backwards, active;
    uint32_t cur_addr, nxt_addr, step;
    scsp_eg eg;
    scsp_lfo plfo, alfo;
    int slot;
    int16_t prev;
} scsp_slot;

typedef struct {
    uint32_t rbp, rbl;
    int16_t coef[64];
    uint16_t madrs[32];
    uint16_t mpro[128 * 4];
    int32_t temp[128], mems[32];
    uint32_t dec;
    int32_t mixs[16];
    int16_t exts[2], efreg[16];
    bool stopped;
    int last_step;
} scsp_dsp;

typedef struct scspd {
    uint8_t *ram;
    uint32_t ram_mask;
    uint16_t regs[0x30 / 2];
    scsp_slot slots[32];
    int16_t ringbuf[128];
    uint8_t bufptr;
    int16_t *rbufdst;
    uint8_t irq_tima, irq_timbc, irq_midi, irq_cpu, irq_dma;
    uint8_t latched_mslc;
    uint16_t latched_mslc_data;
    uint8_t midi_stack[256];
    uint8_t midi_w, midi_r;
    int tim_cnt[3];
    struct { uint32_t dmea; uint16_t drga, dtlg; uint8_t dgate, ddir; } dma;
    scsp_dsp dsp;
    int32_t eg_table[0x400];
    int lpan[0x10000], rpan[0x10000];
    int ar_table[64], dr_table[64];
    int plfo_tri[256], plfo_sqr[256], plfo_saw[256], plfo_noi[256];
    int alfo_tri[256], alfo_sqr[256], alfo_saw[256], alfo_noi[256];
    int pscales[8][256], ascales[8][256];
    uint32_t noise;
} scspd;

void scspd_init(scspd *c);
void scspd_reset(scspd *c);
static inline void scspd_set_ram(scspd *c, uint8_t *ram, uint32_t mask) { c->ram = ram; c->ram_mask = mask; }
uint16_t scspd_read16(scspd *c, uint32_t offset);
void scspd_write16(scspd *c, uint32_t offset, uint16_t data, uint16_t mem_mask);
static inline uint8_t scspd_read8(scspd *c, uint32_t offset)
{
    const uint16_t w = scspd_read16(c, offset & ~1u);
    return (offset & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
static inline void scspd_write8(scspd *c, uint32_t offset, uint8_t data)
{
    if (offset & 1) scspd_write16(c, offset & ~1u, data, 0x00ff);
    else scspd_write16(c, offset & ~1u, (uint16_t)(data << 8), 0xff00);
}
void scspd_midi_in(scspd *c, uint8_t data);
void scspd_sample(scspd *c, int32_t *left, int32_t *right);
int scspd_irq_level(scspd *c);

#endif
