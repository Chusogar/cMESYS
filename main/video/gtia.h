// gtia.h - Atari CTIA/GTIA (C014805): a C11 migration of dsp-cpp's
// video/gtia. Turns ANTIC's playfield codes into colours, overlays the
// players and missiles, resolves priority, latches collisions; reads the
// joystick triggers and the console keys (START / SELECT / OPTION).
#ifndef GTIA_H
#define GTIA_H

#include <stdint.h>

// Playfield source codes ANTIC passes to gtia_pixel()
enum { GTIA_PF_BAK = 0, GTIA_PF0 = 1, GTIA_PF1 = 2, GTIA_PF2 = 3, GTIA_PF3 = 4, GTIA_PF_HI2 = 5 };

typedef struct gtia {
    uint8_t (*console)(void *user);             // bit0 START, bit1 SELECT, bit2 OPTION (active low)
    uint8_t (*trigger)(void *user, int n);      // joystick trigger n, active low in bit 0
    void *user;

    uint8_t colpm[4], colpf[4], colbk;
    uint8_t grafp[4], grafm;
    uint8_t hposp[4], hposm[4];
    uint8_t sizep[4], sizem;
    uint8_t prior, vdelay, gractl;
    uint8_t m2pf[4], p2pf[4], m2pl[4], p2pl[4];
    uint8_t player_mask[256], missile_mask[256];
} gtia;

void gtia_reset(gtia *g);                       // (keeps the callbacks)
uint8_t gtia_read(gtia *g, uint16_t offset);
void gtia_write(gtia *g, uint16_t offset, uint8_t value);
void gtia_begin_line(gtia *g, int line);
uint8_t gtia_pixel(gtia *g, uint8_t pf, int hx); // a palette index (the Atari colour byte)
static inline int gtia_prior_mode(const gtia *g) { return (g->prior >> 6) & 3; }

#endif
