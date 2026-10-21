#pragma once
#include <stdint.h>
#include <stdbool.h>

// Galagino CYD wiring
#define NUNCHUK_SDA 22
#define NUNCHUK_SCL 27
#define NUNCHUK_MOVE_THRESHOLD 30     // dead zone, as in Galagino

typedef struct {
    uint8_t jx, jy;     // 0..255, ~128 centred
    bool c, z;          // buttons (true = pressed)
    bool ok;            // valid reading
} nunchuk_t;

bool nunchuk_init(int sda, int scl);
// Reads the controller (about 1 ms on the bus); returns false if not connected.
bool nunchuk_read(nunchuk_t *n);
bool nunchuk_connected(void);

static inline bool nc_left(const nunchuk_t *n)  { return n->ok && n->jx < 128 - NUNCHUK_MOVE_THRESHOLD; }
static inline bool nc_right(const nunchuk_t *n) { return n->ok && n->jx > 128 + NUNCHUK_MOVE_THRESHOLD; }
static inline bool nc_up(const nunchuk_t *n)    { return n->ok && n->jy > 128 + NUNCHUK_MOVE_THRESHOLD; }
static inline bool nc_down(const nunchuk_t *n)  { return n->ok && n->jy < 128 - NUNCHUK_MOVE_THRESHOLD; }
