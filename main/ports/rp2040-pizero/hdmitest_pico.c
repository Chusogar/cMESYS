// hdmitest_pico.c - a board test for the RP2040-PiZero / RP2350-PiZero, no
// game: colour bars with a square moving over them (HDMI), the last key or
// pad button pressed on the screen (USB) and a tone while one is held
// (sound: PWM on the RP2040, the TV's on the RP2350). Build with
// -DGAME=hdmitest. The PSRAM is not set up (PICO_VIDEO_ONLY).
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico_port.h"

#define RATE 22050
static volatile int box_x, box_y;
static const uint16_t bars[8] = { 0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000 };

static void __not_in_flash_func(render)(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    for (int i = 0; i < n; i++, out += PICO_W) {
        const int yy = y + i;
        for (int x = 0; x < PICO_W; x++) {
            uint16_t c = yy < 200 ? bars[x / 40] : (uint16_t)(((x * 31 / PICO_W) << 11) | ((x * 63 / PICO_W) << 5) | (x * 31 / PICO_W));
            if (x >= box_x && x < box_x + 24 && yy >= box_y && yy < box_y + 24) c = 0x8410;
            out[x] = c;
        }
    }
}

static void loop(void)
{
    int dx = 2, dy = 1, last = -1;
    uint32_t phase = 0, frames = 0;
    bool osd = true;
    static int16_t tone[RATE / 50];
    char line[PICO_OSD_COLS + 1];
    for (;;) {
        pico_port_poll();
        int k;
        while ((k = pico_port_next_press()) >= 0) {
            last = k;
            printf("key: HID usage 0x%02x\n", k);
            if (k == 0x29) osd = !osd;             // Esc: the bars / the text
        }
        const uint32_t pad = pico_port_keys();
        bool held = pad != 0 || (last >= 0 && pico_port_key(last));
        if (frames++ % 10 == 0) {
            // the USB's state on the screen: no UART needed
            pico_osd_clear();
            pico_osd_text(0, "HDMI TEST", true);
            snprintf(line, sizeof line, "LAST KEY: HID 0X%02X  PAD BITS: %04X", last < 0 ? 0 : last, (unsigned)pad);
            pico_osd_text(2, line, false);
            pico_port_usb_info(line, sizeof line);
            pico_osd_text(4, "USB:", false);
            pico_osd_text(5, line, false);
            pico_osd_text(7, "HOLD A KEY: 441 HZ SINE TONE", false);
            pico_osd_text(8, "ESC: COLOUR BARS / THIS TEXT", false);
            pico_osd_show(osd);
        }
        box_x += dx;
        box_y += dy;
        if (box_x < 0 || box_x > PICO_W - 24) dx = -dx;
        if (box_y < 0 || box_y > 200 - 24) dy = -dy;
        for (int i = 0; i < RATE / 50; i++) {
            phase = (phase + 1) % 50;                 // 441 Hz
            tone[i] = held ? (int16_t)(8000 * sinf(phase * (2 * 3.14159265f / 50))) : 0;
        }
        pico_port_audio(tone, RATE / 50);
        pico_port_frame_done();
    }
}

int main(void)
{
    pico_port_cfg cfg = { "HDMI test", render, NULL, RATE, 50.0, true };
    pico_port_open(&cfg);
    pico_port_run(loop);
}
