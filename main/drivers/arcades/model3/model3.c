// model3.c - Sega Model 3 Step 2.1 for the common arcade frontends (arc.h):
// C port of dsp-cpp's drivers/arcade/model3 with its tile generator
// (video/model3_tilegen) and sound boards (machine/model3_sound). A PowerPC
// 603r at 166 MHz (cpu/ppc603d), the MPC106 bridge, the Real3D Pro-1000
// (video/real3dd), the 315-5649 I/O with the 93C46, the RTC, the IRQ
// controller, the CROM banks and the 315-5881 security board
// (machine/sega5881d); the sound board (68000 + two SCSPs, sound/scspd) and
// the DSB2 (68000 + MPEG layer II, sound/mpegaudiod). The picture is one
// frame late, as with dsp-cpp's threaded renderer.
// Game: Star Wars Trilogy Arcade (swtrilgy, Rev. A).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/arc.h"
#include "../../../cpu/ppc603d/ppc603d.h"
#include "../../../cpu/m68kdsp/m68kdsp.h"
#include "../../../machine/eeprom93c46.h"
#include "../../../machine/sega5881d.h"
#include "../../../video/real3dd.h"
#include "../../../sound/scspd.h"
#include "../../../sound/mpegaudiod.h"
#include "../../../rom/romfs.h"

#define W 496
#define H 384
#define FPS 57.524160
#define RATE 44100
#define RAM_SIZE 0x800000u
#define CROM_SIZE 0x800000u
#define BANKED_SIZE 0x3000000u
#define VROM_SIZE 0x4000000u
#define BACKUP_SIZE 0x20000u
#define SECURITY_SIZE 0x20000u
#define SWTRILGY_KEY 0x31272A01u
#define TG_VRAM 0x120000u
#define TG_PAL 0x100000u

static const char *const kSets[] = { "swtrilgy", NULL };
const arc_info arc_game = { "swtrilgy", "Star Wars Trilogy Arcade", "Sega / LucasArts", 1998, W, H, FPS, RATE, kSets,
                            { "GATILLO", "PULGAR", "EVENTO 1", "EVENTO 2" }, ARC_JOY_8WAY };

static struct state {
    ppcd cpu;
    real3dd gpu;
    eeprom93c46_t eeprom;
    s5881 crypt;
    uint8_t *ram, *crom, *crom_banked, *vrom, *backup, *security_ram, *blank_bank;
    uint8_t crom_bank_reg, irq_enable, midi_ctrl, input_bank, adc_channel;
    uint32_t irq_state;
    bool security_first_read;
    uint8_t mpc_regs[256];
    uint32_t pci_bus, pci_device, pci_function, pci_reg;
    arc_in in;
    int joy_x, joy_y;
    uint32_t cpu_hz;
    uint64_t frames;
    bool have_pending;
    uint32_t framebuffer[W * H], frame3d[W * H], fb_work[W * H], bottom_snap[W * H], top_snap[W * H];
    uint16_t fb[W * H];
    double audio_frac;
    int16_t audio[2048];
    int16_t stereo[4096];
    // tile generator
    uint8_t tg_vram[TG_VRAM];
    uint32_t tg_regs[64];
    int tg_offset[2][3];
    uint32_t tg_pal[2][32768];
    uint32_t tg_surface[2][W * H];
    // sound board
    m68kd snd;
    scspd scsp1, scsp2;
    uint8_t ram1[0x80000], ram2[0x80000], program[0x80000];
    uint8_t *samples;
    uint32_t samples_size, sample_bank;
    int cycle_debt, current_level;
    // DSB2
    m68kd dsb;
    uint8_t dsb_program[0x20000], dsb_ram[0x20000];
    uint8_t *mpeg_rom;
    uint32_t mpeg_size;
    mpeg_audio decoder;
    uint8_t dsb_fifo[256], dsb_fifo_r, dsb_fifo_w, cmd_latch;
    int dsb_state;
    uint32_t mp_start, mp_end;
    bool mpeg_playing, loop;
    int mp_pos, mp_limit;
    uint32_t loop_start, loop_end;
    uint8_t volume[2];
    int stereo_mode;
    double dsb_cycle_acc, dsb_timer_acc;
    int16_t pcm[1152 * 2];
    size_t pcm_n, pcm_pos;
    int pcm_rate;
    double pcm_frac;
    int16_t last_l, last_r;
} *S_;
#define S (*S_)

static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

// ---------------------------------------------------------------------------
// Tile generator

static inline uint32_t tg_word(uint32_t off) { uint32_t v; memcpy(&v, &S.tg_vram[off], 4); return v; }

static uint32_t tg_colour(int bank, uint32_t data)
{
    if (data & 0x8000) return 0;
    int r = (int)(data & 0x1f) * 255 / 31 + S.tg_offset[bank][0];
    int g = (int)((data >> 5) & 0x1f) * 255 / 31 + S.tg_offset[bank][1];
    int b = (int)((data >> 10) & 0x1f) * 255 / 31 + S.tg_offset[bank][2];
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

static void tg_write8(uint32_t offset, uint8_t value)
{
    offset %= TG_VRAM;
    S.tg_vram[offset] = value;
    if (offset >= TG_PAL) {
        const int index = (int)((offset - TG_PAL) / 4);
        const uint32_t raw = tg_word(TG_PAL + (uint32_t)index * 4);
        S.tg_pal[0][index] = tg_colour(0, raw);
        S.tg_pal[1][index] = tg_colour(1, raw);
    }
}

static void irq_clear(uint32_t bits);

static void tg_write_register(uint32_t reg, uint32_t value)
{
    reg &= 0xff;
    const uint32_t old = S.tg_regs[reg / 4];
    S.tg_regs[reg / 4] = value;
    if (reg == 0x10) irq_clear((uint8_t)(value & 0xff));
    else if ((reg == 0x40 || reg == 0x44) && old != value) {
        const int bank = reg == 0x40 ? 0 : 1;
        S.tg_offset[bank][2] = (int)(int8_t)((value >> 16) & 0xff) * 2;
        S.tg_offset[bank][1] = (int)(int8_t)((value >> 8) & 0xff) * 2;
        S.tg_offset[bank][0] = (int)(int8_t)(value & 0xff) * 2;
        for (int i = 0; i < 32768; ++i) S.tg_pal[bank][i] = tg_colour(bank, tg_word(TG_PAL + (uint32_t)i * 4));
    }
}

static void tg_begin_frame(void) { memset(S.tg_surface, 0, sizeof S.tg_surface); }

static void tg_reset(void)
{
    memset(S.tg_vram, 0, sizeof S.tg_vram);
    memset(S.tg_regs, 0, sizeof S.tg_regs);
    memset(S.tg_offset, 0, sizeof S.tg_offset);
    memset(S.tg_pal, 0, sizeof S.tg_pal);
    tg_begin_frame();
}

static void tg_draw_line(int line)
{
    if (line < 0 || line >= H) return;
    const uint32_t layer_ctrl = S.tg_regs[0x20 / 4];
    for (int pair = 1; pair >= 0; --pair) {
        const int primary = pair * 2;
        bool enabled[2], four_bit[2];
        int sx[2], sy[2];
        uint32_t *dst[2];
        for (int k = 0; k < 2; ++k) {
            const int layer = primary + k;
            const uint32_t r = S.tg_regs[0x60 / 4 + layer];
            enabled[k] = (r & 0x80000000u) != 0;
            sy[k] = (int)((r >> 16) & 0x1ff);
            if (r & 0x8000) {
                const uint32_t w = tg_word(0xF6000 + (uint32_t)layer * 0x400 + (uint32_t)(line / 2) * 4);
                sx[k] = (int)((w >> ((1 - (line & 1)) * 16)) & 0xffff);
            } else sx[k] = (int)(r & 0x3ff);
            four_bit[k] = (layer_ctrl & (1u << (12 + layer))) != 0;
            const bool above = ((layer_ctrl >> (8 + layer)) & 1) != 0;
            dst[k] = S.tg_surface[above ? 1 : 0] + (size_t)line * W;
        }
        if (!enabled[0] && !enabled[1]) continue;
        const uint32_t mask_word = tg_word(0xF7000 + (uint32_t)line * 4);
        const uint32_t line_mask = pair == 0 ? (mask_word >> 16) : (mask_word & 0xffff);
        const uint32_t *pal = S.tg_pal[pair];
        for (int x = 0; x < W;) {
            const int k = (line_mask & (1u << (15 - x / 32))) ? 0 : 1;
            const int px = (x + sx[k]) & 511;
            int run = 8 - (px & 7);
            if (32 - (x & 31) < run) run = 32 - (x & 31);
            if (W - x < run) run = W - x;
            if (!enabled[k]) {
                x += run;
                continue;
            }
            const int layer = primary + k;
            const int py = (line + sy[k]) & 511;
            const uint32_t pair_no = (uint32_t)((py >> 3) & 63) * 32 + (uint32_t)((px >> 4) & 31);
            const uint32_t data = tg_word(0xF8000 + (uint32_t)layer * 0x2000 + pair_no * 4);
            const uint32_t e = (px & 8) ? (data & 0xffff) : (data >> 16);
            const int vfine = py & 7;
            int hfine = px & 7;
            uint32_t *out = dst[k] + x;
            if (four_bit[k]) {
                const uint32_t pattern = (((e & 0x3fff) << 1) | (e >> 15)) * 32;
                const uint32_t row = tg_word((pattern + (uint32_t)vfine * 4) % TG_PAL);
                const uint32_t base = e & 0x7ff0;
                for (int i = 0; i < run; ++i, ++hfine) {
                    const uint32_t c = pal[(base | ((row >> ((7 - hfine) * 4)) & 0xf)) & 0x7fff];
                    if (c) out[i] = c;
                }
            } else {
                const uint32_t pattern = (e & 0x3fff) * 64 + (uint32_t)vfine * 8;
                const uint32_t rows[2] = {tg_word(pattern % TG_PAL), tg_word((pattern + 4) % TG_PAL)};
                const uint32_t base = e & 0x7f00;
                for (int i = 0; i < run; ++i, ++hfine) {
                    const uint32_t c = pal[(base | ((rows[hfine >> 2] >> ((3 - (hfine & 3)) * 8)) & 0xff)) & 0x7fff];
                    if (c) out[i] = c;
                }
            }
            x += run;
        }
    }
}

// ---------------------------------------------------------------------------
// Sound board and DSB2

#define SND_RAM_MASK 0x7ffffu
#define CYCLES_PER_SAMPLE 256
#define DSB_CLOCK 11000000.0
#define DSB_BLOCK 44

static void snd_set_level(int level)
{
    if (level == S.current_level) return;
    S.current_level = level;
    for (int l = 1; l < 8; ++l) m68kd_set_irq(&S.snd, l, l == level ? M68_ASSERT : M68_CLEAR);
}

static uint8_t snd_rb(void *u, uint32_t a)
{
    (void)u;
    a &= 0xffffff;
    switch ((a >> 20) & 0xf) {
    case 0x0: return S.ram1[a & SND_RAM_MASK];
    case 0x1: return scspd_read8(&S.scsp1, a & 0xfff);
    case 0x2: return S.ram2[a & SND_RAM_MASK];
    case 0x3: return scspd_read8(&S.scsp2, a & 0xfff);
    case 0x6: return S.program[a & 0x7ffff];
    case 0x8: case 0x9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf:
        return S.samples[(S.sample_bank + (a & 0x7fffff)) % S.samples_size];
    default: return 0;
    }
}

static uint16_t snd_rw(void *u, uint32_t a)
{
    (void)u;
    a &= 0xfffffe;
    switch ((a >> 20) & 0xf) {
    case 0x0: return (uint16_t)(S.ram1[a & SND_RAM_MASK] << 8 | S.ram1[(a + 1) & SND_RAM_MASK]);
    case 0x1: return scspd_read16(&S.scsp1, a & 0xfff);
    case 0x2: return (uint16_t)(S.ram2[a & SND_RAM_MASK] << 8 | S.ram2[(a + 1) & SND_RAM_MASK]);
    case 0x3: return scspd_read16(&S.scsp2, a & 0xfff);
    case 0x6: return (uint16_t)(S.program[a & 0x7ffff] << 8 | S.program[(a + 1) & 0x7ffff]);
    case 0x8: case 0x9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: {
        const size_t o = (S.sample_bank + (a & 0x7fffff)) % S.samples_size;
        return (uint16_t)(S.samples[o] << 8 | S.samples[(o + 1) % S.samples_size]);
    }
    default: return 0;
    }
}

static void snd_wb(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    a &= 0xffffff;
    switch ((a >> 20) & 0xf) {
    case 0x0: S.ram1[a & SND_RAM_MASK] = v; break;
    case 0x1: scspd_write8(&S.scsp1, a & 0xfff, v); break;
    case 0x2: S.ram2[a & SND_RAM_MASK] = v; break;
    case 0x3: scspd_write8(&S.scsp2, a & 0xfff, v); break;
    default:
        if (a == 0x400001) S.sample_bank = (v & 0x10) ? 0x800000u : 0u;
        break;
    }
}

static void snd_ww(void *u, uint32_t a, uint16_t v)
{
    (void)u;
    a &= 0xfffffe;
    switch ((a >> 20) & 0xf) {
    case 0x0: S.ram1[a & SND_RAM_MASK] = (uint8_t)(v >> 8); S.ram1[(a + 1) & SND_RAM_MASK] = (uint8_t)v; break;
    case 0x1: scspd_write16(&S.scsp1, a & 0xfff, v, 0xffff); break;
    case 0x2: S.ram2[a & SND_RAM_MASK] = (uint8_t)(v >> 8); S.ram2[(a + 1) & SND_RAM_MASK] = (uint8_t)v; break;
    case 0x3: scspd_write16(&S.scsp2, a & 0xfff, v, 0xffff); break;
    default:
        if (a == 0x400000) S.sample_bank = (v & 0x10) ? 0x800000u : 0u;
        break;
    }
}

static void start_play(void)
{
    S.loop = false;
    const uint32_t size = S.mpeg_size;
    if (S.mp_start >= size || S.mp_end > size || S.mp_end <= S.mp_start) {
        S.mpeg_playing = false;
        return;
    }
    S.mp_pos = (int)(S.mp_start * 8);
    S.mp_limit = (int)(S.mp_end * 8);
    S.mpeg_playing = true;
    S.pcm_n = 0;
    S.pcm_pos = 0;
    mpa_clear(&S.decoder);
}

enum { kIdle, kStart0, kStart1, kStart2, kEnd0, kEnd1, kEnd2, kA0, kA1, kA3, kA4, kA5, kA7, kB0, kB1, kB2, kB4, kB5, kB6 };

static void mpeg_fifo_write(uint8_t b)
{
    switch (S.dsb_state) {
    case kIdle:
        switch (b) {
        case 0x14: case 0x15: S.dsb_state = kStart0; break;
        case 0x24: case 0x25: S.dsb_state = kEnd0; break;
        case 0x74: case 0x75: start_play(); break;
        case 0x84: case 0x85:
            S.mpeg_playing = false;
            S.pcm_n = 0;
            S.pcm_pos = 0;
            break;
        case 0xa0: S.dsb_state = kA0; break;
        case 0xa1: S.dsb_state = kA1; break;
        case 0xa3: S.dsb_state = kA3; break;
        case 0xa4: S.dsb_state = kA4; break;
        case 0xa5: S.dsb_state = kA5; break;
        case 0xa7: S.dsb_state = kA7; break;
        case 0xb0: S.dsb_state = kB0; break;
        case 0xb1: S.dsb_state = kB1; break;
        case 0xb2: S.dsb_state = kB2; break;
        case 0xb4: S.dsb_state = kB4; break;
        case 0xb5: S.dsb_state = kB5; break;
        case 0xb6: S.dsb_state = kB6; break;
        default: break;
        }
        break;
    case kStart0: S.mp_start = (S.mp_start & 0x00ffff) | (uint32_t)b << 16; S.dsb_state = kStart1; break;
    case kStart1: S.mp_start = (S.mp_start & 0xff00ff) | (uint32_t)b << 8; S.dsb_state = kStart2; break;
    case kStart2:
        S.mp_start = (S.mp_start & 0xffff00) | b;
        S.dsb_state = kIdle;
        if (S.mpeg_playing) {
            S.loop = true;
            S.loop_start = S.mp_start;
            S.loop_end = S.mp_end;
        }
        break;
    case kEnd0: S.mp_end = (S.mp_end & 0x00ffff) | (uint32_t)b << 16; S.dsb_state = kEnd1; break;
    case kEnd1: S.mp_end = (S.mp_end & 0xff00ff) | (uint32_t)b << 8; S.dsb_state = kEnd2; break;
    case kEnd2:
        S.mp_end = (S.mp_end & 0xffff00) | b;
        S.stereo_mode = 0;
        S.dsb_state = kIdle;
        break;
    case kA0: S.stereo_mode = b ? 1 : 0; S.dsb_state = kIdle; break;
    case kB1: S.stereo_mode = b ? 2 : 0; S.dsb_state = kIdle; break;
    case kA4:
        S.dsb_state = kIdle;
        if (b == 0x75) start_play();
        break;
    case kB4:
        S.dsb_state = kIdle;
        if (b == 0x96) S.mpeg_playing = false;
        break;
    case kB0: case kB6: S.volume[0] = b; S.dsb_state = kIdle; break;
    case kA1: case kA7: S.volume[1] = b; S.dsb_state = kIdle; break;
    default: S.dsb_state = kIdle; break;
    }
}

static uint8_t dsb_rb(void *u, uint32_t a)
{
    (void)u;
    a &= 0xffffff;
    if (a < 0x20000) return S.dsb_program[a];
    if (a == 0xc00001) return S.cmd_latch;
    if (a == 0xc00003) return 1;
    if (a == 0xe80001) return 0x01;
    if (a >= 0xf00000 && a < 0xf20000) return S.dsb_ram[a & 0x1ffff];
    return 0;
}

static uint16_t dsb_rw(void *u, uint32_t a)
{
    (void)u;
    a &= 0xfffffe;
    if (a < 0x20000) return (uint16_t)(S.dsb_program[a] << 8 | S.dsb_program[a + 1]);
    if (a >= 0xf00000 && a < 0xf20000) return (uint16_t)(S.dsb_ram[a & 0x1ffff] << 8 | S.dsb_ram[(a & 0x1ffff) + 1]);
    if (a == 0xc00000) return S.cmd_latch;
    if (a == 0xc00002) return 1;
    if (a == 0xe80000) return 1;
    return 0;
}

static void dsb_wb(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    a &= 0xffffff;
    if (a >= 0xf00000 && a < 0xf20000) S.dsb_ram[a & 0x1ffff] = v;
    else if (a == 0xe00003) mpeg_fifo_write(v);
}

static void dsb_ww(void *u, uint32_t a, uint16_t v)
{
    (void)u;
    a &= 0xfffffe;
    if (a >= 0xf00000 && a < 0xf20000) {
        S.dsb_ram[a & 0x1ffff] = (uint8_t)(v >> 8);
        S.dsb_ram[(a & 0x1ffff) + 1] = (uint8_t)v;
    } else if (a == 0xe00002) mpeg_fifo_write((uint8_t)v);
}

static void run_dsb(int cycles)
{
    while (S.dsb_fifo_r != S.dsb_fifo_w) {
        S.cmd_latch = S.dsb_fifo[S.dsb_fifo_r++];
        m68kd_set_irq(&S.dsb, 1, M68_HOLD);
        cycles -= m68kd_run(&S.dsb, 500);
    }
    m68kd_set_irq(&S.dsb, 2, M68_HOLD);
    if (cycles > 0) m68kd_run(&S.dsb, cycles);
}

static void decode_mpeg_frame(void)
{
    S.pcm_n = 0;
    S.pcm_pos = 0;
    if (!S.mpeg_playing) return;
    short buf[1152 * 2];
    int samples = 0, rate = 0, channels = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        int pos = S.mp_pos;
        if (mpa_decode_buffer(&S.decoder, &pos, S.mp_limit, buf, &samples, &rate, &channels, 0)) {
            S.mp_pos = pos;
            if (rate > 0) S.pcm_rate = rate;
            for (int i = 0; i < samples && S.pcm_n + 2 <= 1152 * 2; ++i) {
                S.pcm[S.pcm_n++] = buf[channels == 2 ? i * 2 : i];
                S.pcm[S.pcm_n++] = buf[channels == 2 ? i * 2 + 1 : i];
            }
            return;
        }
        if (!S.loop || attempt == 1) break;
        S.mp_pos = (int)(S.loop_start * 8);
        S.mp_limit = (int)(S.loop_end * 8);
    }
    S.mpeg_playing = false;
}

static void sound_midi_write(uint8_t data)
{
    scspd_midi_in(&S.scsp1, data);
    S.dsb_fifo[S.dsb_fifo_w++] = data;
    if (S.dsb_fifo_w == S.dsb_fifo_r) ++S.dsb_fifo_r;
}

static void sound_reset(void)
{
    memset(S.ram1, 0, sizeof S.ram1);
    memset(S.ram2, 0, sizeof S.ram2);
    memcpy(S.ram1, S.program, 16);
    S.sample_bank = 0;
    scspd_reset(&S.scsp1);
    scspd_reset(&S.scsp2);
    S.cycle_debt = 0;
    S.current_level = 0;
    m68kd_reset(&S.snd);
    memset(S.dsb_ram, 0, sizeof S.dsb_ram);
    S.dsb_fifo_r = S.dsb_fifo_w = 0;
    S.cmd_latch = 0;
    S.dsb_state = 0;
    S.mp_start = S.mp_end = 0;
    S.mpeg_playing = false;
    S.loop = false;
    S.volume[0] = S.volume[1] = 0xff;
    S.stereo_mode = 0;
    S.dsb_cycle_acc = S.dsb_timer_acc = 0;
    S.pcm_n = 0;
    S.pcm_pos = 0;
    S.pcm_frac = 0;
    S.last_l = S.last_r = 0;
    mpa_clear(&S.decoder);
    m68kd_reset(&S.dsb);
}

static void sound_run(int samples, int16_t *out)
{
    const double dsb_per_sample = DSB_CLOCK / SCSP_RATE;
    for (int s = 0; s < samples; ++s) {
        if (s % DSB_BLOCK == 0) {
            S.dsb_cycle_acc += dsb_per_sample * DSB_BLOCK;
            const int n = (int)S.dsb_cycle_acc;
            S.dsb_cycle_acc -= n;
            run_dsb(n);
        }
        int32_t l1, r1, l2, r2;
        scspd_sample(&S.scsp1, &l1, &r1);
        scspd_sample(&S.scsp2, &l2, &r2);
        snd_set_level(scspd_irq_level(&S.scsp1));
        const int want = CYCLES_PER_SAMPLE - S.cycle_debt;
        const int ran = want > 0 ? m68kd_run(&S.snd, want) : 0;
        S.cycle_debt = ran - want;
        if (S.cycle_debt > 4 * CYCLES_PER_SAMPLE) S.cycle_debt = 0;

        int32_t ml = 0, mr = 0;
        S.pcm_frac += (double)S.pcm_rate / SCSP_RATE;
        while (S.pcm_frac >= 1.0) {
            S.pcm_frac -= 1.0;
            if (S.pcm_pos < S.pcm_n / 2) {
                S.last_l = S.pcm[S.pcm_pos * 2];
                S.last_r = S.pcm[S.pcm_pos * 2 + 1];
                ++S.pcm_pos;
            } else {
                decode_mpeg_frame();
                if (S.pcm_n) {
                    S.last_l = S.pcm[0];
                    S.last_r = S.pcm[1];
                    S.pcm_pos = 1;
                } else S.last_l = S.last_r = 0;
            }
        }
        if (S.mpeg_playing || S.pcm_pos < S.pcm_n / 2) {
            int32_t nl = S.last_l, nr = S.last_r;
            if (S.pcm_pos < S.pcm_n / 2) {
                nl = S.pcm[S.pcm_pos * 2];
                nr = S.pcm[S.pcm_pos * 2 + 1];
            }
            const int32_t il = (int32_t)(S.last_l + (nl - S.last_l) * S.pcm_frac);
            const int32_t ir = (int32_t)(S.last_r + (nr - S.last_r) * S.pcm_frac);
            int32_t src_l = il, src_r = ir;
            uint8_t vl = S.volume[0], vr = S.volume[1];
            if (S.stereo_mode == 1) {
                src_r = il;
                vr = S.volume[0];
            } else if (S.stereo_mode == 2) {
                src_l = ir;
                vl = S.volume[1];
            }
            ml = src_l * vl / 255;
            mr = src_r * vr / 255;
        }
        int32_t left = l1 + l2 + ml, right = r1 + r2 + mr;
        left = left < -32768 ? -32768 : left > 32767 ? 32767 : left;
        right = right < -32768 ? -32768 : right > 32767 ? 32767 : right;
        out[s * 2] = (int16_t)left;
        out[s * 2 + 1] = (int16_t)right;
    }
}

// ---------------------------------------------------------------------------
// IRQ controller, I/O

static void update_irq(void)
{
    const bool line = (S.irq_state & S.irq_enable & 0xff) != 0 || (S.irq_state & ~0xffu) != 0;
    ppcd_set_irq(&S.cpu, line);
}
static void irq_assert(uint32_t bits) { S.irq_state |= bits; update_irq(); }
static void irq_clear(uint32_t bits) { S.irq_state &= ~bits; update_irq(); }

static void set_crom_bank(uint8_t value)
{
    S.crom_bank_reg = value;
    const uint32_t idx = (~value) & 0xf;
    const size_t off = (size_t)idx * 0x800000;
    ppcd_set_region_data(&S.cpu, 2, off + 0x800000 <= BANKED_SIZE ? S.crom_banked + off : S.blank_bank);
}

static uint8_t read_inputs(unsigned reg)
{
    reg &= 0x3f;
    switch (reg) {
    case 0x00: return S.input_bank;
    case 0x04: {
        uint8_t d = 0xff;
        if ((S.input_bank & 1) == 0) {
            if (S.in.coin1) d &= (uint8_t)~0x01;
            if (S.in.coin2) d &= (uint8_t)~0x02;
            if (S.in.service) d &= (uint8_t)~0x04;
            if (S.in.p[0] & AP_START) d &= (uint8_t)~0x10;
            if (S.in.p[1] & AP_START) d &= (uint8_t)~0x20;
        } else d = (uint8_t)((d & 0xdf) | (eeprom93c46_do_read(&S.eeprom) ? 0x20 : 0));
        return d;
    }
    case 0x08: {
        uint8_t d = 0xff;
        if (S.in.p[0] & AP_B1) d &= (uint8_t)~0x20;
        if (S.in.p[0] & AP_B2) d &= (uint8_t)~0x10;
        if (S.in.p[0] & AP_B3) d &= (uint8_t)~0x01;
        if (S.in.p[0] & AP_B4) d &= (uint8_t)~0x02;
        return d;
    }
    case 0x18: return 0x7f;
    case 0x34: return 0;
    case 0x3c: {
        uint8_t v = 0;
        const int ch = S.adc_channel & 7;
        if (ch == 0) v = (uint8_t)S.joy_y;
        else if (ch == 1) v = (uint8_t)S.joy_x;
        S.adc_channel = (uint8_t)((S.adc_channel + 1) & 7);
        return v;
    }
    default: return 0xff;
    }
}

static void write_inputs(unsigned reg, uint8_t value)
{
    switch (reg & 0x3f) {
    case 0x00:
        eeprom93c46_di_write(&S.eeprom, (value >> 5) & 1);
        eeprom93c46_cs_write(&S.eeprom, (value >> 6) & 1);
        eeprom93c46_clk_write(&S.eeprom, (value >> 7) & 1);
        S.input_bank = value;
        break;
    case 0x3c: S.adc_channel = value & 7; break;
    default: break;
    }
}

static uint8_t read_system(unsigned reg)
{
    switch (reg & 0x3f) {
    case 0x08: return S.crom_bank_reg;
    case 0x14: return S.irq_enable;
    case 0x18: return (uint8_t)(S.irq_state & 0xff);
    case 0x10: return (uint8_t)(S.gpu.jtag_tdo ? 0x20 : 0);
    default: return 0xff;
    }
}

static void write_system(unsigned reg, uint8_t value)
{
    switch (reg & 0x3f) {
    case 0x08: set_crom_bank(value); break;
    case 0x14:
        S.irq_enable = value;
        update_irq();
        break;
    case 0x18: irq_clear(value); break;
    case 0x0c: r3d_jtag_write(&S.gpu, (value >> 6) & 1, (value >> 2) & 1, (value >> 5) & 1, (value >> 7) & 1); break;
    default: break;
    }
}

static uint8_t read_rtc(unsigned reg)
{
    static time_t rtc_time;
    static struct tm t;
    const time_t now = time(NULL);
    if (now != rtc_time) {
        rtc_time = now;
        const struct tm *lt = localtime(&now);
        if (lt) t = *lt;
    }
    switch (reg & 0xf) {
    case 0: return (uint8_t)((t.tm_sec % 10) & 0xf);
    case 1: return (uint8_t)((t.tm_sec / 10) & 0x7);
    case 2: return (uint8_t)((t.tm_min % 10) & 0xf);
    case 3: return (uint8_t)((t.tm_min / 10) & 0x7);
    case 4: return (uint8_t)((t.tm_hour % 10) & 0xf);
    case 5: return (uint8_t)((t.tm_hour / 10) & 0x7);
    case 6: return (uint8_t)((t.tm_mday % 10) & 0xf);
    case 7: return (uint8_t)((t.tm_mday / 10) & 0x3);
    case 8: return (uint8_t)(((t.tm_mon + 1) % 10) & 0xf);
    case 9: return (uint8_t)(((t.tm_mon + 1) / 10) & 0x1);
    case 10: return (uint8_t)((t.tm_year % 10) & 0xf);
    case 11: return (uint8_t)(((t.tm_year % 100) / 10) & 0xf);
    case 12: return (uint8_t)(t.tm_wday & 0x7);
    default: return 0;
    }
}

static uint32_t read_security(unsigned reg)
{
    switch (reg) {
    case 0x00: return 0;
    case 0x1c:
        if (S.security_first_read) {
            S.security_first_read = false;
            return 0xffff0000u;
        }
        return (uint32_t)s5881_decrypt(&S.crypt) << 16;
    default: return 0xffffffffu;
    }
}

static void write_security(unsigned reg, uint32_t value)
{
    switch (reg) {
    case 0x10:
    case 0x14:
        s5881_set_addr_low(&S.crypt, 0);
        s5881_set_addr_high(&S.crypt, 0);
        S.security_first_read = true;
        break;
    case 0x18: {
        const uint16_t sub = (uint16_t)(value >> 16);
        s5881_set_subkey(&S.crypt, (uint16_t)((sub >> 8) | (sub << 8)));
        break;
    }
    default: break;
    }
}

static uint16_t crypt_read(void *u, uint32_t addr)
{
    (void)u;
    return addr < 0x8000 ? be16(&S.security_ram[(size_t)addr * 4]) : 0;
}

static uint32_t pci_config_data_read(void)
{
    if (S.pci_device == 0) return be32(&S.mpc_regs[S.pci_reg & 0xfc]);
    if (S.pci_device == 13) return r3d_pci_config_read(&S.gpu, (S.pci_reg >> 2) & 0x3c);
    return 0;
}

// ---------------------------------------------------------------------------
// The PowerPC bus

static uint32_t bus_read32(void *u, uint32_t a);
static void bus_write32(void *u, uint32_t a, uint32_t v);

static uint8_t bus_read8(void *u, uint32_t a)
{
    if (a < RAM_SIZE) return S.ram[a];
    switch (a >> 24) {
    case 0xff:
        if (a >= 0xff800000u) return S.crom[a & 0x7fffff];
        return (uint8_t)(bus_read32(u, a & ~3u) >> (8 * (3 - (a & 3))));
    case 0xc2: return r3d_read_dma8(&S.gpu, a & 0xff);
    case 0xf0:
    case 0xfe:
        switch ((a >> 16) & 0xff) {
        case 0x04: return read_inputs(a & 0x3f);
        case 0x08:
            if ((a & 0xf) == 0) return 0x00;
            if ((a & 0xf) == 4) return 0x83;
            return 0;
        case 0x0c:
        case 0x0d: return S.backup[a & 0x1ffff];
        case 0x10: return read_system(a & 0x3f);
        case 0x14:
            if ((a & 3) == 1) return 0x03;
            if ((a & 3) == 0) return read_rtc((a >> 2) & 0xf);
            return 0;
        default: break;
        }
        break;
    case 0xf1:
        if (a < 0xf1120000u) return S.tg_vram[(a & 0x1fffff) % TG_VRAM];
        break;
    default: break;
    }
    return 0xff;
}

static uint16_t bus_read16(void *u, uint32_t a)
{
    if (a & 1) return (uint16_t)(bus_read8(u, a) << 8 | bus_read8(u, a + 1));
    if (a < RAM_SIZE) return be16(&S.ram[a]);
    switch (a >> 24) {
    case 0xff:
        if (a >= 0xff800000u) return be16(&S.crom[a & 0x7fffff]);
        return (uint16_t)(bus_read32(u, a & ~3u) >> (16 * (1 - ((a >> 1) & 1))));
    case 0xf0:
    case 0xfe: {
        const uint32_t sub = (a >> 16) & 0xff;
        if (sub == 0x0c || sub == 0x0d) return be16(&S.backup[a & 0x1ffff]);
        if (sub >= 0xe0 && sub <= 0xef) {
            const uint32_t d = pci_config_data_read();
            return (uint16_t)(d >> (16 * (1 - ((a >> 1) & 1))));
        }
        break;
    }
    case 0xf1:
        if (a < 0xf1120000u) return (uint16_t)(bus_read8(u, a) << 8 | bus_read8(u, a + 1));
        break;
    default: break;
    }
    return 0xffff;
}

static uint32_t bus_read32(void *u, uint32_t a)
{
    if (a & 3) {
        const uint32_t hi = bus_read16(u, a);
        return hi << 16 | bus_read16(u, a + 2);
    }
    if (a < RAM_SIZE) return be32(&S.ram[a]);
    switch (a >> 24) {
    case 0xff:
        if (a >= 0xff800000u) return be32(&S.crom[a & 0x7fffff]);
        {
            const uint32_t idx = (~(uint32_t)S.crom_bank_reg) & 0xf;
            const size_t off = (size_t)idx * 0x800000 + (a & 0x7fffff);
            return off + 4 <= BANKED_SIZE ? be32(&S.crom_banked[off]) : 0xffffffffu;
        }
    case 0x84: return __builtin_bswap32(r3d_read_register(&S.gpu, a & 0x3f));
    case 0xc2: return __builtin_bswap32(r3d_read_dma32(&S.gpu, a & 0xff));
    case 0xf0:
    case 0xfe: {
        const uint32_t sub = (a >> 16) & 0xff;
        switch (sub) {
        case 0x04: {
            const uint32_t b0 = read_inputs((a & 0x3f) + 0), b1 = read_inputs((a & 0x3f) + 1);
            const uint32_t b2 = read_inputs((a & 0x3f) + 2), b3 = read_inputs((a & 0x3f) + 3);
            return b0 << 24 | b1 << 16 | b2 << 8 | b3;
        }
        case 0x0c:
        case 0x0d: return be32(&S.backup[a & 0x1ffff]);
        case 0x10:
            return (uint32_t)read_system((a & 0x3f) + 0) << 24 | (uint32_t)read_system((a & 0x3f) + 1) << 16 |
                   (uint32_t)read_system((a & 0x3f) + 2) << 8 | read_system((a & 0x3f) + 3);
        case 0x14: return (uint32_t)read_rtc((a >> 2) & 0xf) << 24 | 0x00030000u;
        case 0x18:
        case 0x19: return be32(&S.security_ram[a & 0x1ffff]);
        case 0x1a: return read_security(a & 0x3f);
        default:
            if (sub >= 0xe0 && sub <= 0xef) return pci_config_data_read();
            break;
        }
        break;
    }
    case 0xf1:
        if (a < 0xf1120000u)
            return (uint32_t)bus_read8(u, a) << 24 | (uint32_t)bus_read8(u, a + 1) << 16 | (uint32_t)bus_read8(u, a + 2) << 8 |
                   bus_read8(u, a + 3);
        if (a >= 0xf1180000u && a < 0xf1180100u) return __builtin_bswap32(S.tg_regs[(a & 0xff) / 4]);
        break;
    default: break;
    }
    return 0xffffffffu;
}

static void bus_write8(void *u, uint32_t a, uint8_t v)
{
    (void)u;
    if (a < RAM_SIZE) {
        S.ram[a] = v;
        return;
    }
    switch (a >> 24) {
    case 0xc2: r3d_write_dma8(&S.gpu, a & 0xff, v); break;
    case 0xf0:
    case 0xfe:
        switch ((a >> 16) & 0xff) {
        case 0x04: write_inputs(a & 0x3f, v); break;
        case 0x08:
            if ((a & 0xf) == 0) {
                sound_midi_write(v);
                irq_clear(0x40);
            } else if ((a & 0xf) == 4) {
                S.midi_ctrl = v;
                if ((v & 0x20) == 0) irq_clear(0x40);
            }
            break;
        case 0x0c:
        case 0x0d: S.backup[a & 0x1ffff] = v; break;
        case 0x10: write_system(a & 0x3f, v); break;
        default: break;
        }
        break;
    case 0xf1:
        if (a < 0xf1120000u) tg_write8(a & 0x1fffff, v);
        break;
    default: break;
    }
}

static void bus_write16(void *u, uint32_t a, uint16_t v)
{
    if (a & 1) {
        bus_write8(u, a, (uint8_t)(v >> 8));
        bus_write8(u, a + 1, (uint8_t)v);
        return;
    }
    if (a < RAM_SIZE) {
        put_be16(&S.ram[a], v);
        return;
    }
    switch (a >> 24) {
    case 0xf0:
    case 0xfe: {
        const uint32_t sub = (a >> 16) & 0xff;
        if (sub == 0x0c || sub == 0x0d) put_be16(&S.backup[a & 0x1ffff], v);
        break;
    }
    case 0xf1:
        if (a < 0xf1120000u) {
            tg_write8(a & 0x1fffff, (uint8_t)(v >> 8));
            tg_write8((a + 1) & 0x1fffff, (uint8_t)v);
        }
        break;
    default: break;
    }
}

static void bus_write32(void *u, uint32_t a, uint32_t v)
{
    if (a & 3) {
        bus_write16(u, a, (uint16_t)(v >> 16));
        bus_write16(u, a + 2, (uint16_t)v);
        return;
    }
    if (a < RAM_SIZE) {
        put_be32(&S.ram[a], v);
        return;
    }
    const uint32_t lv = __builtin_bswap32(v);
    switch (a >> 24) {
    case 0x88: r3d_flush(&S.gpu); break;
    case 0x8c: r3d_write_culling_low(&S.gpu, a & 0x3fffff, lv); break;
    case 0x8e: r3d_write_culling_high(&S.gpu, a & 0xfffff, lv); break;
    case 0x90: r3d_write_texture_port(&S.gpu, lv); break;
    case 0x94: r3d_write_texture_fifo(&S.gpu, lv); break;
    case 0x98: r3d_write_polygon_ram(&S.gpu, a & 0x3fffff, lv); break;
    case 0x9c: r3d_write_config(&S.gpu, a, lv); break;
    case 0xc2: r3d_write_dma32(&S.gpu, a & 0xff, lv); break;
    case 0xf0:
    case 0xfe: {
        const uint32_t sub = (a >> 16) & 0xff;
        switch (sub) {
        case 0x04:
            for (int i = 0; i < 4; ++i) write_inputs((a & 0x3f) + (unsigned)i, (uint8_t)(v >> (24 - 8 * i)));
            break;
        case 0x0c:
        case 0x0d: put_be32(&S.backup[a & 0x1ffff], v); break;
        case 0x10:
            for (int i = 0; i < 4; ++i) write_system((a & 0x3f) + (unsigned)i, (uint8_t)(v >> (24 - 8 * i)));
            break;
        case 0x18:
        case 0x19: put_be32(&S.security_ram[a & 0x1ffff], v); break;
        case 0x1a: write_security(a & 0x3f, v); break;
        default:
            if (a >= 0xfec00000u && a < 0xfee00000u) {
                S.pci_bus = (lv >> 16) & 0xff;
                S.pci_device = (lv >> 11) & 0x1f;
                S.pci_function = (lv >> 8) & 7;
                S.pci_reg = lv & 0xff;
            } else if (a >= 0xfee00000u && a < 0xfef00000u) {
                if (S.pci_device == 0) put_be32(&S.mpc_regs[S.pci_reg & 0xfc], v);
            }
            break;
        }
        break;
    }
    case 0xf1:
        if (a < 0xf1120000u) {
            for (int i = 0; i < 4; ++i) tg_write8((a + (uint32_t)i) & 0x1fffff, (uint8_t)(v >> (24 - 8 * i)));
        } else if (a >= 0xf1180000u && a < 0xf1180100u) {
            tg_write_register(a & 0xff, lv);
            if (a == 0xf118000cu) r3d_tilegen_draw_frame(&S.gpu);
        }
        break;
    default: break;
    }
}

static void gpu_dma_irq(void *u, bool on)
{
    (void)u;
    if (on) irq_assert(0x100);
    else irq_clear(0x100);
}

// ---------------------------------------------------------------------------
// ROMs

typedef struct { const char *name; uint32_t size, crc; } spec;

static bool load_interleaved64(const spec *files, uint8_t *dest, uint32_t dest_size)
{
    uint8_t *data = (uint8_t *)malloc(0x400000);
    if (!data) return false;
    for (int f = 0; f < 4; ++f) {
        if (!romfs_get_into(kSets, files[f].name, files[f].crc, data, files[f].size)) {
            romfs_missing(kSets, files[f].name);
            free(data);
            return false;
        }
        for (uint32_t i = 0; i + 1 < files[f].size; i += 2) {
            const uint32_t off = (i / 2) * 8 + (uint32_t)f * 2;
            if (off + 1 >= dest_size) break;
            dest[off] = data[i + 1];
            dest[off + 1] = data[i];
        }
    }
    free(data);
    return true;
}

// ROM_LOAD16_WORD_SWAP images: the dumps hold little-endian words.
static uint8_t *get_swapped(const char *name, uint32_t *len)
{
    uint8_t *p = romfs_get(kSets, name, 0, 0, len);
    if (!p) return NULL;
    for (uint32_t i = 0; i + 1 < *len; i += 2) {
        const uint8_t t = p[i];
        p[i] = p[i + 1];
        p[i + 1] = t;
    }
    return p;
}

static bool load_roms(void)
{
    static const spec kCrom[4] = {{"epr-21382a.20", 0x200000, 0x69BAF117}, {"epr-21381a.19", 0x200000, 0x2DD34E28},
                                  {"epr-21380a.18", 0x200000, 0x780FB4E7}, {"epr-21379a.17", 0x200000, 0x24DC1555}};
    static const spec kCrom0[4] = {{"mpr-21342.04", 0x400000, 0x339525CE}, {"mpr-21341.03", 0x400000, 0xB2A269E4},
                                   {"mpr-21340.02", 0x400000, 0xAD36040E}, {"mpr-21339.01", 0x400000, 0xC0CE5037}};
    static const spec kCrom1[4] = {{"mpr-21346.08", 0x400000, 0xC8733594}, {"mpr-21345.07", 0x400000, 0x6C183A21},
                                   {"mpr-21344.06", 0x400000, 0x87453D76}, {"mpr-21343.05", 0x400000, 0x12552D07}};
    static const spec kCrom2[4] = {{"mpr-21350.12", 0x400000, 0x486195E7}, {"mpr-21349.11", 0x400000, 0x3D39454B},
                                   {"mpr-21348.10", 0x400000, 0x1F7CC5F5}, {"mpr-21347.09", 0x400000, 0xECB6B934}};
    static const spec kVrom[16] = {
        {"mpr-21359.26", 0x400000, 0x34EF4122}, {"mpr-21360.27", 0x400000, 0x2882B95E}, {"mpr-21361.28", 0x400000, 0x9B61C3C1},
        {"mpr-21362.29", 0x400000, 0x01A92169}, {"mpr-21363.30", 0x400000, 0xE7D18FED}, {"mpr-21364.31", 0x400000, 0xCB6A5468},
        {"mpr-21365.32", 0x400000, 0xAD5449D8}, {"mpr-21366.33", 0x400000, 0xDEFB6B95}, {"mpr-21367.34", 0x400000, 0xDFD51029},
        {"mpr-21368.35", 0x400000, 0xAE90FD21}, {"mpr-21369.36", 0x400000, 0xBF17EEB4}, {"mpr-21370.37", 0x400000, 0x2321592A},
        {"mpr-21371.38", 0x400000, 0xA68782FD}, {"mpr-21372.39", 0x400000, 0xFC3F4E8B}, {"mpr-21373.40", 0x400000, 0xB76AD261},
        {"mpr-21374.41", 0x400000, 0xAE6C4D28}};
    if (!load_interleaved64(kCrom, S.crom, CROM_SIZE)) return false;
    if (!load_interleaved64(kCrom0, S.crom_banked, 0x1000000)) return false;
    if (!load_interleaved64(kCrom1, S.crom_banked + 0x1000000, 0x1000000)) return false;
    if (!load_interleaved64(kCrom2, S.crom_banked + 0x2000000, 0x1000000)) return false;

    uint8_t *data = (uint8_t *)malloc(0x400000);
    if (!data) return false;
    for (int f = 0; f < 16; ++f) {
        if (!romfs_get_into(kSets, kVrom[f].name, kVrom[f].crc, data, kVrom[f].size)) {
            romfs_missing(kSets, kVrom[f].name);
            free(data);
            return false;
        }
        for (uint32_t i = 0; i + 1 < kVrom[f].size; i += 2) {
            const size_t off = (size_t)(i / 2) * 32 + (size_t)f * 2;
            S.vrom[off] = data[i];
            S.vrom[off + 1] = data[i + 1];
        }
    }

    // Sound board and DSB2 (missing ROMs only mute them).
    uint32_t len = 0;
    uint8_t *p = get_swapped("epr-21383.21", &len);
    memset(S.program, 0xff, sizeof S.program);
    if (p) memcpy(S.program, p, len < sizeof S.program ? len : sizeof S.program);
    free(p);
    S.samples_size = 0x800000;
    S.samples = (uint8_t *)calloc(1, S.samples_size);
    if (!S.samples) {
        free(data);
        return false;
    }
    uint32_t l1 = 0, l2 = 0;
    uint8_t *s1 = get_swapped("mpr-21355.22", &l1), *s2 = get_swapped("mpr-21357.24", &l2);
    if (s1) memcpy(S.samples, s1, l1 < 0x400000 ? l1 : 0x400000);
    if (s2) memcpy(S.samples + 0x400000, s2, l2 < 0x400000 ? l2 : 0x400000);
    free(s1);
    free(s2);
    p = get_swapped("epr-21384.2", &len);
    memset(S.dsb_program, 0xff, sizeof S.dsb_program);
    if (p) memcpy(S.dsb_program, p, len < sizeof S.dsb_program ? len : sizeof S.dsb_program);
    free(p);
    static const char *const kMpeg[4] = {"mpr-21375.18", "mpr-21376.20", "mpr-21377.22", "mpr-21378.24"};
    S.mpeg_size = 0x1000000;
    S.mpeg_rom = (uint8_t *)calloc(1, S.mpeg_size);
    if (!S.mpeg_rom) {
        free(data);
        return false;
    }
    for (int i = 0; i < 4; i++) {
        p = romfs_get(kSets, kMpeg[i], 0, 0, &len);
        if (p) memcpy(S.mpeg_rom + (size_t)i * 0x400000, p, len < 0x400000 ? len : 0x400000);
        free(p);
    }
    free(data);
    mpa_init(&S.decoder, S.mpeg_rom, MPA_L2, false, 0);

    // The force feedback drive board is not emulated: skip its set-up (MAME).
    put_be32(&S.crom[0x043dc], 0x48000090);
    put_be32(&S.crom[0xf6e44], 0x60000000);
    romfs_release();
    return true;
}

// ---------------------------------------------------------------------------

bool arc_init(void)
{
    if (!(S_ = (struct state *)calloc(1, sizeof(struct state)))) return false;
    S.ram = (uint8_t *)calloc(1, RAM_SIZE);
    S.crom = (uint8_t *)malloc(CROM_SIZE);
    S.crom_banked = (uint8_t *)malloc(BANKED_SIZE);
    S.vrom = (uint8_t *)calloc(1, VROM_SIZE);
    S.backup = (uint8_t *)calloc(1, BACKUP_SIZE);
    S.security_ram = (uint8_t *)calloc(1, SECURITY_SIZE);
    S.blank_bank = (uint8_t *)malloc(0x800000);
    if (!S.ram || !S.crom || !S.crom_banked || !S.vrom || !S.backup || !S.security_ram || !S.blank_bank) return false;
    memset(S.crom, 0xff, CROM_SIZE);
    memset(S.crom_banked, 0xff, BANKED_SIZE);
    memset(S.blank_bank, 0xff, 0x800000);
    if (!r3d_init(&S.gpu)) return false;
    if (!load_roms()) return false;

    ppcd_init(&S.cpu, PPC_PVR_603R);
    S.cpu.read8 = bus_read8;
    S.cpu.read16 = bus_read16;
    S.cpu.read32 = bus_read32;
    S.cpu.write8 = bus_write8;
    S.cpu.write16 = bus_write16;
    S.cpu.write32 = bus_write32;
    ppcd_add_fast_region(&S.cpu, 0x00000000, RAM_SIZE, S.ram, true);
    ppcd_add_fast_region(&S.cpu, 0xff800000u, CROM_SIZE, S.crom, false);
    ppcd_add_fast_region(&S.cpu, 0xff000000u, 0x800000, S.crom_banked, false);
    ppcd_add_fast_region(&S.cpu, 0xf1000000u, 0x100000, S.tg_vram, true);
    ppcd_add_fast_region(&S.cpu, 0xf00c0000u, BACKUP_SIZE, S.backup, true);
    ppcd_add_fast_region(&S.cpu, 0xfe0c0000u, BACKUP_SIZE, S.backup, true);
    S.cpu_hz = 166000000;
    {
        const int div = (int)((double)S.cpu_hz / 16666666.0 + 0.5);
        ppcd_set_timer_divider(&S.cpu, div > 1 ? div : 1);
    }

    S.gpu.read32 = bus_read32;
    S.gpu.write32 = bus_write32;
    S.gpu.dma_irq = gpu_dma_irq;
    eeprom93c46_init(&S.eeprom, 16);
    s5881_init(&S.crypt, SWTRILGY_KEY);
    S.crypt.read = crypt_read;

    scspd_init(&S.scsp1);
    scspd_init(&S.scsp2);
    scspd_set_ram(&S.scsp1, S.ram1, SND_RAM_MASK);
    scspd_set_ram(&S.scsp2, S.ram2, SND_RAM_MASK);
    m68kd_init(&S.snd, 11289600, M68K_T_68000);
    S.snd.read_word = snd_rw;
    S.snd.write_word = snd_ww;
    S.snd.read_byte = snd_rb;
    S.snd.write_byte = snd_wb;
    m68kd_set_address_mask(&S.snd, 0xffffff);
    m68kd_init(&S.dsb, 11000000, M68K_T_68000);
    S.dsb.read_word = dsb_rw;
    S.dsb.write_word = dsb_ww;
    S.dsb.read_byte = dsb_rb;
    S.dsb.write_byte = dsb_wb;
    m68kd_set_address_mask(&S.dsb, 0xffffff);
    S.pcm_rate = 32000;
    S.joy_x = S.joy_y = 0x80;
    arc_reset();
    return true;
}

static void put_le32(int off, uint32_t v)
{
    for (int i = 0; i < 4; ++i) S.mpc_regs[off + i] = (uint8_t)(v >> (8 * i));
}

void arc_reset(void)
{
    S.have_pending = false;
    memset(S.ram, 0, RAM_SIZE);
    set_crom_bank(0xff);
    S.irq_enable = 0;
    S.irq_state = 0;
    S.midi_ctrl = 0;
    S.input_bank = 0;
    S.adc_channel = 0;
    S.security_first_read = true;
    s5881_reset(&S.crypt);
    eeprom93c46_reset(&S.eeprom);
    tg_reset();
    r3d_reset(&S.gpu);
    r3d_set_vrom(&S.gpu, S.vrom, VROM_SIZE);
    memset(S.mpc_regs, 0, sizeof S.mpc_regs);
    S.mpc_regs[0] = 0x57;
    S.mpc_regs[1] = 0x10;
    S.mpc_regs[2] = 0x02;
    S.mpc_regs[3] = 0x00;
    put_le32(0x04, 0x00800006);
    put_le32(0x08, 0x00060000);
    put_le32(0x0c, 0x00000800);
    put_le32(0x70, 0x00cd0000);
    put_le32(0xa8, 0x0010ff00);
    put_le32(0xac, 0x060c000c);
    put_le32(0xb8, 0x04000000);
    put_le32(0xc0, 0x00000100);
    put_le32(0xe0, 0x00420fff);
    put_le32(0xe8, 0x00200000);
    put_le32(0xf0, 0x0000ff02);
    put_le32(0xf4, 0x00030000);
    put_le32(0xfc, 0x00000010);
    S.pci_bus = S.pci_device = S.pci_function = S.pci_reg = 0;
    ppcd_reset(&S.cpu);
    ppcd_set_irq(&S.cpu, false);
    sound_reset();
    S.audio_frac = 0;
    S.frames = 0;
}

static void mix_layers(uint32_t *dst)
{
    const uint32_t *bottom = S.bottom_snap, *top = S.top_snap;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        uint32_t c = bottom[i] ? bottom[i] : 0xff000000u;
        const uint32_t g = S.frame3d[i], ga = g >> 24;
        if (ga == 0xff) c = g;
        else if (ga) {
            const uint32_t inv = 255 - ga;
            uint32_t r = ((g >> 16) & 0xff) + ((c >> 16) & 0xff) * inv / 255;
            uint32_t gg = ((g >> 8) & 0xff) + ((c >> 8) & 0xff) * inv / 255;
            uint32_t b = (g & 0xff) + (c & 0xff) * inv / 255;
            if (r > 255) r = 255;
            if (gg > 255) gg = 255;
            if (b > 255) b = 255;
            c = 0xff000000u | r << 16 | gg << 8 | b;
        }
        if (top[i]) c = top[i];
        dst[i] = c | 0xff000000u;
    }
}

// dsp-cpp renders frame N on a thread while N + 1 runs and shows it then:
// the same picture, one frame late, done here at once.
static void compose(void)
{
    if (S.have_pending) memcpy(S.framebuffer, S.fb_work, sizeof S.framebuffer);
    r3d_prepare(&S.gpu);
    memcpy(S.bottom_snap, S.tg_surface[0], sizeof S.bottom_snap);
    memcpy(S.top_snap, S.tg_surface[1], sizeof S.top_snap);
    r3d_rasterize(&S.gpu, S.frame3d);
    mix_layers(S.fb_work);
    S.have_pending = true;
}

static void approach(int *v, int target)
{
    const int step = 24;
    if (*v < target) *v = *v + step < target ? *v + step : target;
    else if (*v > target) *v = *v - step > target ? *v - step : target;
}

int arc_run_frame(const int16_t **audio)
{
    const unsigned p = S.in.p[0];
    approach(&S.joy_x, (p & AP_LEFT) ? 0x00 : (p & AP_RIGHT) ? 0xff : 0x80);
    approach(&S.joy_y, (p & AP_UP) ? 0x00 : (p & AP_DOWN) ? 0xff : 0x80);

    const int frame_cycles = (int)((double)S.cpu_hz / FPS);
    const int line_cycles = frame_cycles / 424;
    int vblank_cycles = line_cycles * 40;

    tg_begin_frame();
    r3d_begin_vblank(&S.gpu);
    while ((S.irq_enable & 0x02) && (S.irq_state & 0x02) && vblank_cycles > 1000) {
        ppcd_run(&S.cpu, 1000);
        vblank_cycles -= 1000;
    }
    for (int count = 0; (S.midi_ctrl & 0x20) && (S.irq_enable & 0x40) && count < 128 && vblank_cycles > 1000; ++count) {
        irq_assert(0x40);
        ppcd_run(&S.cpu, 1000);
        vblank_cycles -= 1000;
    }
    if (vblank_cycles > 0) ppcd_run(&S.cpu, vblank_cycles);
    irq_assert(0x0d);

    const uint32_t flip_line = S.tg_regs[0x08 / 4];
    for (int line = 0; line < 384; ++line) {
        if ((uint32_t)line == flip_line) r3d_flip_ping_pong(&S.gpu);
        if (line == 383) irq_assert(0x02);
        tg_draw_line(line);
        ppcd_run(&S.cpu, line_cycles);
    }

    compose();
    ++S.frames;
    for (int i = 0; i < W * H; i++) S.fb[i] = arc_rgb565(S.framebuffer[i]);

    S.audio_frac += 44100.0 / FPS;
    int n = (int)S.audio_frac;
    S.audio_frac -= n;
    if (n > 2048) n = 2048;
    sound_run(n, S.stereo);
    for (int i = 0; i < n; ++i) S.audio[i] = (int16_t)(((int32_t)S.stereo[i * 2] + S.stereo[i * 2 + 1]) / 2);
    if (audio) *audio = S.audio;
    return n;
}

const uint16_t *arc_frame(void) { return S.fb; }

void arc_input(const arc_in *in) { S.in = *in; }

void arc_dip(int bank, uint8_t v)
{
    (void)bank;
    (void)v;
}
