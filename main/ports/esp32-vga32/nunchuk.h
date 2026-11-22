// nunchuk.h - on the TTGO VGA32 the "Nunchuk" of the CYD frontends is the PS/2
// keyboard (ps2.c): the cursor keys are the stick, Ctrl is Z, Alt is C. Same
// API as ports/esp32-cyd/nunchuk.h.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define NUNCHUK_SDA -1
#define NUNCHUK_SCL -1
#define NUNCHUK_MOVE_THRESHOLD 30

typedef struct {
    uint8_t jx, jy;     // 0..255, ~128 centred
    bool c, z;          // buttons (true = pressed)
    bool ok;            // a keyboard is there
} nunchuk_t;

bool nunchuk_init(int sda, int scl);
bool nunchuk_read(nunchuk_t *n);
bool nunchuk_connected(void);

static inline bool nc_left(const nunchuk_t *n)  { return n->ok && n->jx < 128 - NUNCHUK_MOVE_THRESHOLD; }
static inline bool nc_right(const nunchuk_t *n) { return n->ok && n->jx > 128 + NUNCHUK_MOVE_THRESHOLD; }
static inline bool nc_up(const nunchuk_t *n)    { return n->ok && n->jy > 128 + NUNCHUK_MOVE_THRESHOLD; }
static inline bool nc_down(const nunchuk_t *n)  { return n->ok && n->jy < 128 - NUNCHUK_MOVE_THRESHOLD; }
