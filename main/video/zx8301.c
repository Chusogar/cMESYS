// zx8301.c - Sinclair ZX8301 video. See zx8301.h.
#include "zx8301.h"
#include <string.h>

void zx8301_render_row(const uint8_t *screen, uint8_t control, bool flash, int y, uint8_t *out)
{
    if ((control & 0x02) || y < 0 || y >= ZX8301_H) {
        memset(out, 0, ZX8301_W);
        return;
    }
    const uint8_t *p = screen + y * 128;
    if (!(control & 0x08)) {                    // mode 4: G bits in the first byte, R in the second
        static const uint8_t mode4[4] = {0, 2, 4, 7};
        for (int w = 0; w < 64; w++) {
            uint8_t hi = p[2 * w], lo = p[2 * w + 1];
            for (int k = 0; k < 8; k++) {
                *out++ = mode4[((hi >> 7) << 1) | (lo >> 7)];
                hi = (uint8_t)(hi << 1);
                lo = (uint8_t)(lo << 1);
            }
        }
        return;
    }
    // mode 8: G F G F.. / R B R B.., four pixels a word, two wide; a set flash
    // bit (while flashing is on) toggles holding that pixel's colour
    bool active = false;
    int held = 0;
    for (int w = 0; w < 64; w++) {
        uint8_t hi = p[2 * w], lo = p[2 * w + 1];
        for (int k = 0; k < 4; k++) {
            int col = (((hi >> 7) & 1) << 2) | (((lo >> 7) & 1) << 1) | ((lo >> 6) & 1);
            const int fl = (hi >> 6) & 1;
            if (active) col = held;
            if (fl && flash) { active = !active; held = col; }
            *out++ = (uint8_t)col;
            *out++ = (uint8_t)col;
            hi = (uint8_t)(hi << 2);
            lo = (uint8_t)(lo << 2);
        }
    }
}
