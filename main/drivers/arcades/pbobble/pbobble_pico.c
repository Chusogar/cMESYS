// pbobble_pico.c - Puzzle Bobble (Taito B System, 1994) on the Waveshare
// RP2040-PiZero (HDMI). Same core as the CYD and the desktop; core 1 draws
// the 320x224 picture from the TC0180VCU's RAM and frame buffer.
//
// Keys: 5 coin, 1 start, Left / Right aim, Z shoot, F3 reset; USB pad:
// A shoot, SELECT coin, START start. (The EEPROM is not kept.)
#include <string.h>
#include "pico_port.h"
#include "pb_core.h"
#include "ym2610.h"
#include "rom_data.h"

#define AUDIO_RATE  55555           // YM2610B native rate (8 MHz / 144)
#define Y0 ((PICO_H - PB_SCREEN_H) / 2)

// YM2610B: the core time-stamps the register writes in YM clocks.
typedef struct { uint32_t when; uint8_t part, reg, val; } ymw_t;
#define QMAX 256
static ymw_t q[QMAX];
static int qn;
static ym2610_t *ym;
static uint64_t synth_clock;

void pb_ym_write(uint32_t when, int part, uint8_t reg, uint8_t val)
{
    if (qn < QMAX) q[qn++] = (ymw_t){when, (uint8_t)part, reg, val};
    else ym2610_write(ym, part, reg, val);          // (full: written now)
}

static int synth_until(int16_t *out, int cap, uint64_t end_clock)
{
    int n = 0, qi = 0;
    while (synth_clock + 144 <= end_clock && n < cap) {
        while (qi < qn && (int32_t)(q[qi].when - (uint32_t)synth_clock) <= 0) {
            ym2610_write(ym, q[qi].part, q[qi].reg, q[qi].val);
            qi++;
        }
        ym2610_render(ym, &out[n++], 1, pb_sound_gain());
        synth_clock += 144;
    }
    for (; qi < qn; qi++) ym2610_write(ym, q[qi].part, q[qi].reg, q[qi].val);
    qn = 0;
    return n;
}

static uint16_t idx[PB_SCREEN_W * PICO_BAND], scratch[PB_SCREEN_W * PICO_BAND];

// (no room for a colour table: made from the palette RAM pixel by pixel)
static inline uint16_t colour(const uint16_t *p, int i)
{
    uint32_t rgb = pb_pal_rgb(p[i & 0xfff]);
    return pico_rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}
static int16_t audio[1200];

static void render(void *user, int y, int n, uint16_t *out)
{
    (void)user;
    int top = y - Y0, i = 0;
    for (; i < n && top + i < 0; i++) pico_fill(out + i * PICO_W, PICO_W, 0);
    int first = top + i, rows = n - i;
    if (first + rows > PB_SCREEN_H) rows = PB_SCREEN_H - first;
    if (rows > 0) {
        tc0180vcu_render(&pb_frame, pb_vram(), first, rows, idx, scratch);
        const uint16_t *p = pb_palette_ram();
        for (int r = 0; r < rows; r++, i++)
            for (int x = 0; x < PB_SCREEN_W; x++) out[i * PICO_W + x] = colour(p, idx[r * PB_SCREEN_W + x]);
    }
    for (; i < n; i++) pico_fill(out + i * PICO_W, PICO_W, 0);
}

static void game(void)
{
    for (;;) {
        if (pico_port_poll()) pb_reset();
        uint32_t k = pico_port_keys();
        uint8_t coin = 0xff, start = 0xff, a = 0xff, b = 0xff;
        if (k & SP_COIN1) coin &= (uint8_t)~PB_COIN1;
        if (k & SP_COIN2) coin &= (uint8_t)~PB_COIN2;
        if (k & SP_START1) start &= (uint8_t)~PB_START1;
        if (k & SP_START2) start &= (uint8_t)~PB_START2;
        if (k & SP_B1) a &= (uint8_t)~PB_B1;
        if (k & SP_B2) a &= (uint8_t)~PB_B2;
        if (k & SP_UP) b &= (uint8_t)~PB_UP;
        if (k & SP_DOWN) b &= (uint8_t)~PB_DOWN;
        if (k & SP_LEFT) b &= (uint8_t)~PB_LEFT;
        if (k & SP_RIGHT) b &= (uint8_t)~PB_RIGHT;
        pb_in.coin = coin; pb_in.start = start; pb_in.p12a = a; pb_in.p12b = b;
        pb_run_frame();
        int n = synth_until(audio, 1200, pb_snd_clock());
        pico_port_audio(audio, n);
        pico_port_frame_done();
    }
}

int main(void)
{
    pb_init();
    ym = ym2610_create(PB_SND_CLOCK, AUDIO_RATE, 6, pb_adpcma_rom, sizeof pb_adpcma_rom,
                       pb_adpcma_rom, sizeof pb_adpcma_rom);
    pico_port_cfg cfg = { "Puzzle Bobble", render, NULL, AUDIO_RATE, PB_FPS, false };
    pico_port_open(&cfg);
    pico_port_run(game);
}
