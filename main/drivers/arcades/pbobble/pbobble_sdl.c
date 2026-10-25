// pbobble_sdl.c - Puzzle Bobble on the desktop (SDL2), same core as the CYD.
// ROMs are compiled in (rom_data.c, made by tools/gen_roms_pbobble.py).
// The EEPROM (settings, high scores) is kept in pbobble.eep.
#include <stdio.h>
#include <string.h>
#include "sdl_port.h"
#include "pb_core.h"
#include "ym2610.h"
#include "rom_data.h"

#define AUDIO_RATE  55555           // YM2610B native rate (8 MHz / 144)

// YM2610B: the core time-stamps the register writes in YM clocks.
typedef struct { uint32_t when; uint8_t part, reg, val; } ymw_t;
static ymw_t q[16384];
static int qn;
static ym2610_t *ym;
static uint64_t synth_clock;        // YM clocks synthesized (in output samples * 144)

void pb_ym_write(uint32_t when, int part, uint8_t reg, uint8_t val)
{
    if (qn < 16384) q[qn++] = (ymw_t){when, (uint8_t)part, reg, val};
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

static void eep_load(void)
{
    FILE *f = fopen("pbobble.eep", "rb");
    if (!f) return;
    uint8_t *e = pb_eeprom(NULL);
    if (fread(e, 1, 128, f) != 128) memset(e, 0xff, 128);
    fclose(f);
}

static void eep_save(void)
{
    FILE *f = fopen("pbobble.eep", "wb");
    if (!f) return;
    fwrite(pb_eeprom(NULL), 1, 128, f);
    fclose(f);
}

int main(void)
{
    static uint16_t idx[PB_SCREEN_W * 16];
    static uint32_t rgb[PB_SCREEN_W * PB_SCREEN_H];
    static uint32_t pal[4096];
    static int16_t audio[8192];
    uint32_t pal_serial = ~0u;

    pb_init();
    eep_load();
    ym = ym2610_create(PB_SND_CLOCK, AUDIO_RATE, 6, pb_adpcma_rom, sizeof pb_adpcma_rom,
                       pb_adpcma_rom, sizeof pb_adpcma_rom);
    sdl_port_cfg cfg = {"Puzzle Bobble", PB_SCREEN_W, PB_SCREEN_H, 3, AUDIO_RATE, PB_FPS};
    if (!sdl_port_open(&cfg)) return 1;

    bool reset, *eep_dirty;
    pb_eeprom(&eep_dirty);
    long frame = 0;
    while (sdl_port_poll(&reset)) {
        if (reset) pb_reset();
        if (sdl_port_paused()) { sdl_port_present(rgb); continue; }
        uint32_t k = sdl_port_keys();
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
        sdl_port_audio(audio, synth_until(audio, 8192, pb_snd_clock()));
        if (pal_serial != pb_palette_serial) {
            pal_serial = pb_palette_serial;
            const uint16_t *p = pb_palette_ram();
            for (int i = 0; i < 4096; i++) pal[i] = pb_pal_rgb(p[i]);
        }
        for (int y = 0; y < PB_SCREEN_H; y += 16) {
            tc0180vcu_render(&pb_frame, pb_vram(), y, 16, idx, NULL);
            for (int i = 0; i < PB_SCREEN_W * 16; i++) rgb[y * PB_SCREEN_W + i] = pal[idx[i]];
        }
        sdl_port_present(rgb);
        if (*eep_dirty && ++frame % 60 == 0) { *eep_dirty = false; eep_save(); }
    }
    if (*eep_dirty) eep_save();
    sdl_port_close();
    return 0;
}
