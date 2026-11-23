// sw_core.c - Star Wars / The Empire Strikes Back (see sw_core.h): C port of
// dsp-cpp's drivers/arcade/starwars.cpp.
#include "sw_core.h"
#include "rom_data.h"
#include "../../../cpu/m6809dsp/m6809dsp.h"
#include "../../../machine/mos6532.h"
#include "../../../machine/starwars_math.h"
#include "../../../sound/pokey.h"
#include "../../../sound/tms5220.h"
#include "../../../video/avg_starwars.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MASTER_CLOCK 12096000
#define CPU_CLOCK (MASTER_CLOCK / 8)
#define CLOCK_3K (MASTER_CLOCK / 4096)
#define IRQS_PER_FRAME 6
#define IRQ_CYCLES ((int)((double)CPU_CLOCK / ((double)CLOCK_3K / 12.0) + 0.5))
#define SLICE 152
#define YOKE_CENTRE 0xa8        // the yoke's pots (yoke_adc)
#define YOKE_HALF 87

typedef struct {
    int game;
    m6809d main_cpu, sound_cpu;
    pokey pk[4];
    tms5220 tms;
    mos6532 riot;
    sw_math math;
    avg_starwars avg;
    const uint8_t *main_pages[0x11];
    const uint8_t *sound_pages[8];
    const uint8_t *vector_rom;
    uint8_t vector_ram[0x3000];
    uint8_t work_ram[0x800];
    uint8_t sound_ram[0x800];
    uint8_t nvram[0x100];
    // the picture
    int width, height;
    uint32_t *fb;               // the picture converted (sw_framebuffer)
    uint8_t *picture;
    uint32_t palette[256];
    uint8_t decay[256];
    uint32_t sample_rate;
    bool draw_enabled, audio_enabled;
    int draw_every, draw_count;  // (sw_set_draw_every)
    bool idle_skip;             // (the boards: see run_main / run_sound)
    // the board
    uint8_t in0, in1, dsw0, dsw1, analog_x, analog_y, adc_value;
    int adc_channel;
    int slapstic_state, slapstic_bank, slapstic_loaded_bank;
    uint8_t bank, outlatch, sound_latch, main_latch;
    bool sound_pending, main_pending;
    uint32_t prng;
    // audio
    int16_t *audio;
    int naudio;
    int64_t audio_accumulator, pokey_sum, sub_accumulator;
    int32_t sub_acc32;          // (the boards' sub_accumulator)
    int pokey_cycles, pokey_pending, sub_samples;
    double dc_in, dc_out;
    float dcf_in, dcf_out;      // (the boards' filter)
} sw_state;

static sw_state *S;

static inline uint8_t rom_byte(uint32_t offset)
{
    const uint8_t *page = S->main_pages[offset >> 13];     // (offsets below 0x22000)
    return page ? page[offset & 0x1fff] : 0;
}

// ---- the slapstic (ESB), as dsp-cpp's (after MAME's slapstic.cpp, 137412-101)
enum { kIdle = 0, kActive, kAltValid, kAltSelect, kAltCommit, kBitLoad, kBitSetOdd, kBitSetEven };

static void slapstic_access(uint16_t address)
{
    const bool inside = (address & 0xe000) == 0x8000;
    const uint16_t off = (uint16_t)(address & 0x1fff);
#define IN(mask, value) (inside && (off & (mask)) == (value))
    const bool reset = inside && off == 0;
    switch (S->slapstic_state) {
    case kIdle:
        if (reset) S->slapstic_state = kActive;
        break;
    case kActive:
        if (!inside) break;
        if (off == 0x0080 || off == 0x0090 || off == 0x00a0 || off == 0x00b0) {
            S->slapstic_bank = (off >> 4) & 3;
            S->slapstic_state = kIdle;
        } else if (IN(0x1f00, 0x1e00)) {
            S->slapstic_state = kAltValid;
        } else if (IN(0x1ff0, 0x1540)) {
            S->slapstic_state = kBitLoad;
        }
        break;
    case kAltValid:
        if (reset) S->slapstic_state = kActive;
        else if (!inside && (address & 0x1fff) == 0x1fff) S->slapstic_state = kAltSelect;
        else S->slapstic_state = kActive;
        break;
    case kAltSelect:
        if (reset) {
            S->slapstic_state = kActive;
        } else if (IN(0x1ffc, 0x1b5c)) {
            S->slapstic_loaded_bank = off & 3;
            S->slapstic_state = kAltCommit;
        } else {
            S->slapstic_state = kActive;
        }
        break;
    case kAltCommit:
        if (reset) {
            S->slapstic_state = kActive;
        } else if (IN(0x1fcf, 0x0080)) {
            S->slapstic_bank = S->slapstic_loaded_bank;
            S->slapstic_state = kIdle;
        }
        break;
    case kBitLoad:
        if (reset) {
            S->slapstic_state = kActive;
        } else if (IN(0x1fcf, 0x0080)) {
            S->slapstic_loaded_bank = S->slapstic_bank;
            S->slapstic_state = kBitSetOdd;
        }
        break;
    case kBitSetOdd:
    case kBitSetEven: {
        const bool odd = S->slapstic_state == kBitSetOdd;
        const int next = odd ? kBitSetEven : kBitSetOdd;
        if (reset) {
            S->slapstic_state = kActive;
        } else if (IN(0x1ff3, odd ? 0x1540 : 0x1543)) {
            S->slapstic_loaded_bank &= ~1;
            S->slapstic_state = next;
        } else if (IN(0x1ff3, odd ? 0x1541 : 0x1542)) {
            S->slapstic_loaded_bank |= 1;
            S->slapstic_state = next;
        } else if (IN(0x1ff3, odd ? 0x1542 : 0x1541)) {
            S->slapstic_loaded_bank &= ~2;
            S->slapstic_state = next;
        } else if (IN(0x1ff3, odd ? 0x1543 : 0x1540)) {
            S->slapstic_loaded_bank |= 2;
            S->slapstic_state = next;
        } else if (IN(0x1ff8, 0x1550)) {
            S->slapstic_bank = S->slapstic_loaded_bank;
            S->slapstic_state = kIdle;
        }
        break;
    }
    default:
        S->slapstic_state = kIdle;
        break;
    }
#undef IN
}

// ---- the picture: 8 bits a pixel (colour << 5 | brightness), each vector
// list drawn as the AVG runs it over the last one dimmed (the phosphor).
// The AVG's 250 x 280 screen fills the 4:3 picture as on the cabinet's
// monitor, and the lines are drawn from the beam's exact positions, one
// pixel wide, so that the vector font keeps the shapes of its letters.
static void decay_picture(void)
{
    uint8_t *p = S->picture;
    if (!p) return;
    const size_t n = (size_t)S->width * (size_t)S->height;
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t four;
        memcpy(&four, p + i, 4);
        if (four == 0) continue;            // mostly black: four at a time
        p[i] = S->decay[p[i]];
        p[i + 1] = S->decay[p[i + 1]];
        p[i + 2] = S->decay[p[i + 2]];
        p[i + 3] = S->decay[p[i + 3]];
    }
    for (; i < n; ++i) p[i] = S->decay[p[i]];
}

static inline void plot(int x, int y, int level, int top)
{
    if ((unsigned)x >= (unsigned)S->width || (unsigned)y >= (unsigned)S->height) return;
    uint8_t *p = &S->picture[(size_t)y * (size_t)S->width + (size_t)x];
    if (level > (*p & 31)) *p = (uint8_t)(top | level);
}

static void line_sink(void *user, const avg_line *l)
{
    (void)user;
    if (!S->draw_enabled || !S->picture) return;
    // the ends in the picture's pixels
    const float kx = (float)S->width / (AVG_VIS_W * 65536.0f), ky = (float)S->height / (AVG_VIS_H * 65536.0f);
    float x0 = (float)l->x0 * kx, y0 = (float)l->y0 * ky, x1 = (float)l->x1 * kx, y1 = (float)l->y1 * ky;
    // clipped to the picture (Liang-Barsky): the parts off it are not walked
    const float xmin = -0.5f, ymin = -0.5f, xmax = (float)S->width - 0.51f, ymax = (float)S->height - 0.51f;
    float dx = x1 - x0, dy = y1 - y0, t0 = 0.0f, t1 = 1.0f;
    const float p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
    for (int k = 0; k < 4; k++) {
        if (p[k] == 0.0f) {
            if (q[k] < 0.0f) return;
        } else {
            const float r = q[k] / p[k];
            if (p[k] < 0.0f) {
                if (r > t1) return;
                if (r > t0) t0 = r;
            } else {
                if (r < t0) return;
                if (r < t1) t1 = r;
            }
        }
    }
    if (t1 < 1.0f) x1 = x0 + t1 * dx, y1 = y0 + t1 * dy;
    if (t0 > 0.0f) x0 += t0 * dx, y0 += t0 * dy;
    dx = x1 - x0;
    dy = y1 - y0;
    const float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const int n = (int)((adx > ady ? adx : ady) + 0.5f);
    const int level = ((l->intensity < 255 ? l->intensity : 255) * 31 + 127) / 255;
    const int top = (l->color & 7) << 5;
    // one pixel a step along the longer axis (16.16)
    int32_t x = (int32_t)(x0 * 65536.0f) + 0x8000, y = (int32_t)(y0 * 65536.0f) + 0x8000;
    if (n <= 0) {
        plot(x >> 16, y >> 16, level, top);
        return;
    }
    const int32_t sx = (int32_t)(dx * 65536.0f / (float)n), sy = (int32_t)(dy * 65536.0f / (float)n);
    for (int i = 0; i <= n; i++, x += sx, y += sy) plot(x >> 16, y >> 16, level, top);
}

// ---- memory
static uint8_t avg_read(void *user, uint16_t address)
{
    (void)user;
    if (address < 0x3000) return S->vector_ram[address];
    if (address < 0x4000) return S->vector_rom ? S->vector_rom[address - 0x3000] : 0;
    return 0;
}

static void set_sound_pending(bool pending)
{
    S->sound_pending = pending;
    mos6532_set_pa7(&S->riot, pending);
}

static uint8_t main_read_raw(uint16_t address)
{
    const bool esb = S->game == SW_GAME_ESB;
    switch (address >> 12) {
    case 0x0: case 0x1: case 0x2:
        return S->vector_ram[address];
    case 0x3:
        return S->vector_rom ? S->vector_rom[address - 0x3000] : 0;
    case 0x5:
        return S->math.ram[address & 0xfff];
    case 0x6: case 0x7:
        return rom_byte((S->bank ? 0x10000u : 0x6000u) + (address & 0x1fff));
    case 0x8: case 0x9:
        if (esb) return rom_byte(0x14000u + (uint32_t)S->slapstic_bank * 0x2000u + (address & 0x1fff));
        return rom_byte(address);
    case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf:
        if (esb) return rom_byte((S->bank ? 0x1c000u : 0xa000u) + (address - 0xa000u));
        return rom_byte(address);
    default:
        break;
    }
    if (address >= 0x4800) return S->work_ram[address & 0x7ff];
    if (address >= 0x4300 && address <= 0x431f) return S->in0;
    if (address >= 0x4320 && address <= 0x433f) {
        uint8_t value = (uint8_t)(S->in1 & 0x3f);
        if (avg_done(&S->avg)) value |= 0x40;
        if (sw_math_running(&S->math)) value |= 0x80;
        return value;
    }
    if (address >= 0x4340 && address <= 0x435f) return S->dsw0;
    if (address >= 0x4360 && address <= 0x437f) return S->dsw1;
    if (address >= 0x4380 && address <= 0x439f) return S->adc_value;
    if (address == 0x4400) {
        S->main_pending = false;
        return S->main_latch;
    }
    if (address == 0x4401) return (uint8_t)((S->sound_pending ? 0x80 : 0) | (S->main_pending ? 0x40 : 0));
    if (address >= 0x4500 && address <= 0x45ff) return S->nvram[address & 0xff];
    if (address == 0x4700) return sw_math_div_reh(&S->math);
    if (address == 0x4701) return sw_math_div_rel(&S->math);
    if (address == 0x4703) return (uint8_t)((S->prng >> 8) & 0xff);
    return 0xff;
}

static uint8_t main_read(void *user, uint16_t address)
{
    (void)user;
    const uint8_t value = main_read_raw(address);
    if (S->game == SW_GAME_ESB) slapstic_access(address);
    return value;
}

static void main_dummy(void *user, uint16_t address)
{
    (void)user;
    slapstic_access(address);
}

static void main_write(void *user, uint16_t address, uint8_t value)
{
    (void)user;
    if (S->game == SW_GAME_ESB) slapstic_access(address);
    if (address < 0x3000) { S->vector_ram[address] = value; return; }
    if (address == 0x4400) {
        S->sound_latch = value;
        set_sound_pending(true);
        return;
    }
    if (address >= 0x4500 && address <= 0x45ff) { S->nvram[address & 0xff] = value; return; }
    if (address >= 0x4600 && address <= 0x461f) {
        // (a list not drawn is not run: the AVG stays halted, "done", as it
        // is at once anyway; each list sets its own scale and colours)
        if (S->draw_every > 1 && ++S->draw_count % S->draw_every != 0) return;
        if (S->draw_enabled) decay_picture();
        avg_go(&S->avg);
        return;
    }
    if (address >= 0x4620 && address <= 0x463f) { avg_vg_reset(&S->avg); return; }
    if (address >= 0x4660 && address <= 0x467f) { m6809d_set_irq(&S->main_cpu, M6809D_CLEAR); return; }
    if (address >= 0x4680 && address <= 0x469f) {
        const int bit = address & 7;
        if (value & 0x80) S->outlatch |= (uint8_t)(1u << bit);
        else S->outlatch &= (uint8_t)~(1u << bit);
        if (bit == 4) S->bank = (value & 0x80) ? 1 : 0;
        return;
    }
    if (address >= 0x46c0 && address <= 0x46c3) {
        S->adc_channel = address & 3;
        S->adc_value = S->adc_channel == 0 ? S->analog_y : S->adc_channel == 1 ? S->analog_x : 0;
        return;
    }
    if (address == 0x46e0) {
        set_sound_pending(false);
        S->main_pending = false;
        m6809d_reset(&S->sound_cpu);
        return;
    }
    if (address >= 0x4700 && address <= 0x4707) { sw_math_write(&S->math, (uint8_t)(address & 7), value); return; }
    if (address >= 0x4800 && address <= 0x4fff) { S->work_ram[address & 0x7ff] = value; return; }
    if (address >= 0x5000 && address <= 0x5fff) { S->math.ram[address & 0xfff] = value; return; }
}

static void sync_pokeys(void)
{
    if (!S->audio_enabled) {
        S->pokey_pending = 0;
        return;
    }
    if (S->pokey_pending > 0) {
        for (int i = 0; i < 4; i++) pokey_run(&S->pk[i], S->pokey_pending);
        S->pokey_pending = 0;
    }
}

static void quad_pokey_w(uint16_t offset, uint8_t data)
{
    sync_pokeys();
    const int off = (int)offset & 0x3f;
    const int pokey_num = (off >> 3) & ~0x04;
    const int control = (off & 0x20) >> 2;
    const int pokey_reg = (off % 8) | control;
    pokey_write(&S->pk[pokey_num & 3], (uint16_t)pokey_reg, data);
}

static uint8_t sound_read(void *user, uint16_t address)
{
    (void)user;
    if (address >= 0x4000) {
        const uint8_t *page = S->sound_pages[address >> 13];
        return page ? page[address & 0x1fff] : 0;
    }
    if (address >= 0x0800 && address <= 0x0fff) {
        set_sound_pending(false);
        return S->sound_latch;
    }
    if (address >= 0x1000 && address <= 0x107f) return mos6532_ram_read(&S->riot, (uint8_t)address);
    if (address >= 0x1080 && address <= 0x109f) return mos6532_io_read(&S->riot, (uint8_t)address);
    if (address >= 0x2000 && address <= 0x27ff) return S->sound_ram[address & 0x7ff];
    return 0xff;
}

static void sound_write(void *user, uint16_t address, uint8_t value)
{
    (void)user;
    if (address <= 0x07ff) {
        S->main_latch = value;
        S->main_pending = true;
        return;
    }
    if (address >= 0x1000 && address <= 0x107f) { mos6532_ram_write(&S->riot, (uint8_t)address, value); return; }
    if (address >= 0x1080 && address <= 0x109f) { mos6532_io_write(&S->riot, (uint8_t)address, value); return; }
    if (address >= 0x1800 && address <= 0x183f) { quad_pokey_w(address, value); return; }
    if (address >= 0x2000 && address <= 0x27ff) S->sound_ram[address & 0x7ff] = value;
}

static void on_main_cycles(void *user, int cycles)
{
    (void)user;
    sw_math_tick(&S->math, cycles);
    S->prng = ((S->prng << 1) | (1u ^ (((S->prng >> 22) ^ (S->prng >> 4)) & 1u))) & 0x7fffffu;
}

// The boards (no double-precision FPU): the same filter in float
static void emit_sample_f(float pokeys)
{
    {
        const float mix = pokeys + 0.5f * (float)tms5220_update(&S->tms, S->sample_rate);
        const float out = 0.997f * (S->dcf_out + mix - S->dcf_in);
        S->dcf_out = out;
        S->dcf_in = mix;
        int v = (int)(out < 0 ? out - 0.5f : out + 0.5f);
        if (v < -32768) v = -32768;
        if (v > 32767) v = 32767;
        if (S->audio && S->naudio < SW_AUDIO_MAX) S->audio[S->naudio++] = (int16_t)v;
    }
}

static void emit_sample(double pokeys)
{
    const double mix = pokeys + 0.5 * (double)tms5220_update(&S->tms, S->sample_rate);
    S->dc_out = 0.997 * (S->dc_out + mix - S->dc_in);
    S->dc_in = mix;
    long v = lround(S->dc_out);
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    if (S->audio && S->naudio < SW_AUDIO_MAX) S->audio[S->naudio++] = (int16_t)v;
}

static void on_sound_cycles(void *user, int cycles)
{
    (void)user;
    if (!S->audio_enabled) {
        mos6532_tick(&S->riot, cycles);
        S->sub_accumulator += (int64_t)cycles * S->sample_rate;
        while (S->sub_accumulator >= (int64_t)CPU_CLOCK) {
            S->sub_accumulator -= CPU_CLOCK;
            tms5220_update(&S->tms, S->sample_rate);
        }
        return;
    }
    if (S->sample_rate != SW_AUDIO_RATE) {
        // (the boards: called for every instruction, so kept short)
        if (S->riot.timer_count > cycles) S->riot.timer_count -= cycles;
        else mos6532_tick(&S->riot, cycles);
        S->pokey_pending += cycles;
        S->sub_acc32 += cycles * (int32_t)S->sample_rate * 2;
        while (S->sub_acc32 >= CPU_CLOCK) {
            S->sub_acc32 -= CPU_CLOCK;
            sync_pokeys();
            S->pokey_sum += pokey_update(&S->pk[0]) + pokey_update(&S->pk[1]) + pokey_update(&S->pk[2]) +
                            pokey_update(&S->pk[3]);
            if (++S->sub_samples == 2) {
                emit_sample_f((float)(int32_t)S->pokey_sum * 0.5f);
                S->pokey_sum = 0;
                S->sub_samples = 0;
            }
        }
        return;
    }
    for (int i = 0; i < 4; i++) pokey_run(&S->pk[i], cycles);
    mos6532_tick(&S->riot, cycles);
    S->pokey_sum += (int64_t)(pokey_update(&S->pk[0]) + pokey_update(&S->pk[1]) + pokey_update(&S->pk[2]) +
                              pokey_update(&S->pk[3])) * cycles;
    S->pokey_cycles += cycles;
    S->audio_accumulator += (int64_t)cycles * S->sample_rate;
    while (S->audio_accumulator >= CPU_CLOCK) {
        S->audio_accumulator -= CPU_CLOCK;
        const double pokeys = S->pokey_cycles ? (double)S->pokey_sum / (double)S->pokey_cycles : 0.0;
        S->pokey_sum = 0;
        S->pokey_cycles = 0;
        emit_sample(pokeys);
    }
}

// ---- the RIOT
static uint8_t riot_pa_in(void *user)
{
    (void)user;
    uint8_t value = 0x10;   // not self-test
    if (tms5220_readyq(&S->tms)) value |= 0x04;
    if (S->main_pending) value |= 0x40;
    if (S->sound_pending) value |= 0x80;
    return value;
}

static void riot_pa_out(void *user, uint8_t v)
{
    (void)user;
    tms5220_strobe_ws_rs(&S->tms, (uint8_t)(v & 3));
}

static uint8_t riot_pb_in(void *user)
{
    (void)user;
    return tms5220_status(&S->tms);
}

static void riot_pb_out(void *user, uint8_t v)
{
    (void)user;
    tms5220_set_data_latch(&S->tms, v);
    tms5220_write_data(&S->tms, v);
}

static void riot_irq(void *user, bool on)
{
    (void)user;
    m6809d_set_irq(&S->sound_cpu, on ? M6809D_ASSERT : M6809D_CLEAR);
}

// ---- the machine
static bool map_roms(int game)
{
    memset(S->main_pages, 0, sizeof S->main_pages);
    memset(S->sound_pages, 0, sizeof S->sound_pages);
    const uint8_t *avg_prom, *math_prom;
    if (game == SW_GAME_ESB) {
        const uint8_t *m = esb_main_rom, *sl = esb_slap_rom, *sn = esb_snd_rom;
        if (!m || !sl || !sn || !esb_vec_rom || !esb_avg_prom || !esb_math_prom) return false;
        // each 16K ROM: the low 8K in the fixed map, the high 8K in the second bank
        static const uint8_t lo[4] = {0x6000 >> 13, 0xa000 >> 13, 0xc000 >> 13, 0xe000 >> 13};
        static const uint8_t hi[4] = {0x10000 >> 13, 0x1c000 >> 13, 0x1e000 >> 13, 0x20000 >> 13};
        for (int i = 0; i < 4; i++) {
            S->main_pages[lo[i]] = m + i * 0x4000;
            S->main_pages[hi[i]] = m + i * 0x4000 + 0x2000;
        }
        for (int i = 0; i < 4; i++) S->main_pages[(0x14000 >> 13) + i] = sl + i * 0x2000;
        S->vector_rom = esb_vec_rom;
        // 16K each: the low half at $4000/$6000, the high one at $C000/$E000
        for (int i = 0; i < 2; i++) {
            S->sound_pages[2 + i] = sn + i * 0x4000;
            S->sound_pages[6 + i] = sn + i * 0x4000 + 0x2000;
        }
        avg_prom = esb_avg_prom;
        math_prom = esb_math_prom;
    } else {
        const uint8_t *m = sw_main_rom, *sn = sw_snd_rom;
        if (!m || !sn || !sw_vec_rom || !sw_avg_prom || !sw_math_prom) return false;
        // 136021.214 (16K): low half at $6000 (bank 0), high half in bank 1
        S->main_pages[0x6000 >> 13] = m;
        S->main_pages[0x10000 >> 13] = m + 0x2000;
        for (int i = 0; i < 4; i++) S->main_pages[4 + i] = m + 0x4000 + i * 0x2000;
        S->vector_rom = sw_vec_rom;
        // 8K each at $4000/$6000, mirrored at $C000/$E000
        for (int i = 0; i < 2; i++) S->sound_pages[2 + i] = S->sound_pages[6 + i] = sn + i * 0x2000;
        avg_prom = sw_avg_prom;
        math_prom = sw_math_prom;
    }
    avg_set_prom(&S->avg, avg_prom, 0x100);
    sw_math_init(&S->math, math_prom);
    return true;
}

bool sw_init(int game, int pic_w, int pic_h)
{
    if (!S) {
        S = (sw_state *)calloc(1, sizeof *S);
        if (!S) return false;
    } else {
        free(S->fb);
        free(S->picture);
        int16_t *keep = S->audio;
        memset(S, 0, sizeof *S);
        S->audio = keep;
    }
    S->game = game == SW_GAME_ESB ? SW_GAME_ESB : SW_GAME_STARWARS;
    if (!sw_roms_get(S->game) || !map_roms(S->game)) return false;
    S->sample_rate = SW_AUDIO_RATE;
    S->draw_enabled = S->audio_enabled = true;
    S->draw_every = 1;
    S->dsw0 = 0x98;
    S->dsw1 = 0x02;
    S->width = pic_w > 0 && pic_h > 0 ? pic_w : SW_W;
    S->height = pic_w > 0 && pic_h > 0 ? pic_h : SW_H;
    // colour (3 bits) times brightness (5 bits); a list later 6/10 as bright
    for (int i = 0; i < 256; ++i) {
        const uint32_t c = avg_color111(i >> 5);
        const int level = i & 31;
        uint32_t v = 0;
        for (int sh = 0; sh <= 16; sh += 8) v |= (uint32_t)(((int)((c >> sh) & 0xff) * level / 31) & 0xff) << sh;
        S->palette[i] = 0xff000000u | v;
        const int next = level * 6 / 10;
        S->decay[i] = (uint8_t)(next ? ((i & 0xe0) | next) : 0);
    }
    S->main_cpu.read = main_read;
    S->main_cpu.write = main_write;
    S->main_cpu.cycles = on_main_cycles;
    if (S->game == SW_GAME_ESB) S->main_cpu.dummy_read = main_dummy;
    S->sound_cpu.read = sound_read;
    S->sound_cpu.write = sound_write;
    S->sound_cpu.cycles = on_sound_cycles;
    S->riot.pa_in = riot_pa_in;
    S->riot.pa_out = riot_pa_out;
    S->riot.pb_in = riot_pb_in;
    S->riot.pb_out = riot_pb_out;
    S->riot.irq = riot_irq;
    for (int i = 0; i < 4; i++) {
        pokey_init(&S->pk[i]);
        S->pk[i].amplitude = 0.20f;
    }
    tms5220_init(&S->tms, MASTER_CLOCK / 2 / 9, false);
    S->avg.read = avg_read;
    S->avg.sink = line_sink;
    avg_init(&S->avg);
    // the picture last (after the ROMs read from storage)
    S->picture = (uint8_t *)calloc((size_t)S->width * (size_t)S->height, 1);
    if (!S->picture) return false;
    sw_reset();
    return true;
}

void sw_reset(void)
{
    memset(S->vector_ram, 0, sizeof S->vector_ram);
    memset(S->work_ram, 0, sizeof S->work_ram);
    memset(S->sound_ram, 0, sizeof S->sound_ram);
    memset(S->math.ram, 0, sizeof S->math.ram);
    sw_math_reset(&S->math);
    memset(S->nvram, 0, sizeof S->nvram);
    m6809d_reset(&S->main_cpu);
    m6809d_reset(&S->sound_cpu);
    for (int i = 0; i < 4; i++) pokey_reset(&S->pk[i]);
    tms5220_reset(&S->tms);
    mos6532_reset(&S->riot);
    avg_reset(&S->avg);
    S->bank = 0;
    S->slapstic_state = 0;
    S->slapstic_bank = 3;
    S->slapstic_loaded_bank = 0;
    S->outlatch = 0;
    S->sound_latch = S->main_latch = 0;
    S->sound_pending = S->main_pending = false;
    mos6532_set_pa7(&S->riot, false);
    S->analog_x = S->analog_y = YOKE_CENTRE;
    S->adc_value = YOKE_CENTRE;
    S->adc_channel = 0;
    S->prng = 1;
    S->audio_accumulator = S->pokey_sum = S->sub_accumulator = 0;
    S->sub_acc32 = 0;
    S->pokey_cycles = S->pokey_pending = S->sub_samples = 0;
    S->dc_in = S->dc_out = 0.0;
    S->dcf_in = S->dcf_out = 0.0f;
    S->in0 = 0xff;
    S->in1 = 0x3f;
    tms5220_strobe_ws_rs(&S->tms, 3);
    if (S->picture) memset(S->picture, 0, (size_t)S->width * (size_t)S->height);
}

int sw_game(void) { return S ? S->game : SW_GAME_STARWARS; }

const char *sw_title(void) { return sw_game() == SW_GAME_ESB ? "The Empire Strikes Back" : "Star Wars"; }

// ---- Idle loops (the boards). Both CPUs spend most of their time in a
// loop waiting for an interrupt to set a flag: the main one for the next of
// the 6 IRQs a frame, the sound one for the RIOT's timer or the main CPU's
// command. There whole turns of the loop are not emulated but applied: the
// same cycles a instruction (the PRNG, the mathbox, the RIOT, the POKEYs and
// the samples see them as before), the registers as the loop leaves them,
// and only while nothing can interrupt it. The result is the same as running
// it (checked frame by frame and sample by sample).
typedef struct {
    uint16_t sw_head, esb_head;     // the loop's first instruction in each game
    uint16_t sw_end, esb_end;       // its last
    uint8_t n_sw, n_esb;            // its instructions
    uint8_t cyc_sw[10], cyc_esb[10];
} idle_loop;

// main: LDX #0 / [ORCC #$50] / LDD #$F800 / LDA #$48 / TFR A,DP /
//       [ANDCC #$AF] / LSR <flag / BCC loop
static const idle_loop main_idle = {0x6005, 0x61b6, 0x6011, 0x61c6, 6, 8,
                                    {3, 3, 2, 6, 6, 3}, {3, 3, 3, 2, 6, 3, 6, 3}};
// sound: LDA <$00 / CMPA #$3F / BNE / TFR DP,A / CMPA #$10 / BNE /
//        LEAX -1,X / BEQ / LSR <$0B / BCC loop
static const idle_loop sound_idle = {0x7d4c, 0xfd42, 0x7d5e, 0xfd54, 10, 10,
                                     {4, 2, 3, 6, 2, 3, 5, 3, 6, 3}, {4, 2, 3, 6, 2, 3, 5, 3, 6, 3}};

static inline bool quiet_cpu(const m6809d *c)
{
    return c->irq_state_ == M6809D_CLEAR && c->firq_state_ == M6809D_CLEAR && c->nmi_request_ == M6809D_CLEAR &&
           !c->cwai_;
}

// Up to the loop's head (if it is in it): the cycles run
static int to_head(m6809d *c, const idle_loop *l, bool esb, int budget)
{
    const uint16_t head = esb ? l->esb_head : l->sw_head, end = esb ? l->esb_end : l->sw_end;
    int done = 0;
    for (int i = 0; i < 12 && done < budget && c->pc_ != head && c->pc_ > head && c->pc_ <= end; i++)
        done += m6809d_run(c, 1);
    return done;
}

static int run_main(int budget)
{
    m6809d *c = &S->main_cpu;
    if (!S->idle_skip) return m6809d_run(c, budget);
    const bool esb = S->game == SW_GAME_ESB;
    const idle_loop *l = &main_idle;
    int done = to_head(c, l, esb, budget);
    const uint8_t flag = esb ? 0x44 : 0x3d;     // the flag at $48xx the IRQ sets
    if (c->pc_ == (esb ? l->esb_head : l->sw_head) && S->work_ram[flag] == 0 && quiet_cpu(c)) {
        const uint8_t *cyc = esb ? l->cyc_esb : l->cyc_sw;
        const int n_ins = esb ? l->n_esb : l->n_sw;
        int turn = 0;
        for (int i = 0; i < n_ins; i++) turn += cyc[i];
        const int turns = (budget - done) / turn;
        if (turns > 0) {
            for (int t = 0; t < turns; t++)
                for (int i = 0; i < n_ins; i++) on_main_cycles(NULL, cyc[i]);
            done += turns * turn;
            if (esb) {
                slapstic_access(c->pc_);        // (outside its window: as all the loop's)
                c->cc.i = c->cc.f = 0;
            }
            c->x = 0;
            c->a = 0x48;
            c->b = 0;
            c->dp = 0x48;
            c->cc.n = 0, c->cc.z = 1, c->cc.v = 0, c->cc.c = 0;
        }
    }
    if (done < budget) done += m6809d_run(c, budget - done);
    return done;
}

static int run_sound(int budget)
{
    m6809d *c = &S->sound_cpu;
    if (!S->idle_skip) return m6809d_run(c, budget);
    const bool esb = S->game == SW_GAME_ESB;
    const idle_loop *l = &sound_idle;
    int done = to_head(c, l, esb, budget);
    if (c->pc_ == (esb ? l->esb_head : l->sw_head) && c->dp == 0x10 && S->riot.ram[0] == 0x3f &&
        S->riot.ram[0x0b] == 0 && quiet_cpu(c)) {
        const uint8_t *cyc = esb ? l->cyc_esb : l->cyc_sw;
        const int n_ins = esb ? l->n_esb : l->n_sw;
        int turn = 0;
        for (int i = 0; i < n_ins; i++) turn += cyc[i];
        int turns = 0;
        // each turn whole, X kept above 0, and no RIOT interrupt within it
        while (done + turn <= budget && c->x - turns > 1 &&
               !(S->riot.timer_counting && S->riot.ie_timer && S->riot.timer_count <= turn) &&
               c->irq_state_ == M6809D_CLEAR) {
            for (int i = 0; i < n_ins; i++) on_sound_cycles(NULL, cyc[i]);
            done += turn;
            turns++;
        }
        if (turns > 0) {
            c->x = (uint16_t)(c->x - turns);
            c->a = 0x10;
            c->cc.n = 0, c->cc.z = 1, c->cc.v = 0, c->cc.c = 0;
        }
    }
    if (done < budget) done += m6809d_run(c, budget - done);
    return done;
}

int sw_run_frame(int16_t *audio)
{
    S->audio = audio;
    S->naudio = 0;
    sync_pokeys();
    for (int irq = 0; irq < IRQS_PER_FRAME; irq++) {
        m6809d_set_irq(&S->main_cpu, M6809D_ASSERT);
        int done_main = 0, done_sound = 0;
        while (done_main < IRQ_CYCLES || done_sound < IRQ_CYCLES) {
            int target = (done_main > done_sound ? done_main : done_sound) + SLICE;
            if (target > IRQ_CYCLES) target = IRQ_CYCLES;
            if (done_main < target) done_main += run_main(target - done_main);
            if (done_sound < target) done_sound += run_sound(target - done_sound);
        }
    }
    S->audio = NULL;
    return S->naudio;
}

// The yoke's pots: the games start with their range calibrated for the
// cabinet's yoke, about $51..$FE with the centre at $A8 (they widen it, as
// they are pushed past it, only while playing), so the yoke's 0..255 goes
// into that range and its centre puts the crosshair in the centre from the
// first frame (0x80 straight to the ADC left it low and to the left until the
// yoke had been pushed to both ends).
static uint8_t yoke_adc(uint8_t v)
{
    return (uint8_t)(YOKE_CENTRE + (((int)v - 0x80) * YOKE_HALF) / 0x80);
}

void sw_set_inputs(const sw_inputs *in)
{
    S->in0 = 0xff;
    S->in1 = 0x3f;
    if (in->coin2) S->in0 &= (uint8_t)~0x01;
    if (in->coin1) S->in0 &= (uint8_t)~0x02;
    if (in->fire2) S->in0 &= (uint8_t)~0x40;
    if (in->fire1) S->in0 &= (uint8_t)~0x80;
    if (in->fire3) S->in1 &= (uint8_t)~0x10;
    if (in->start) S->in1 &= (uint8_t)~0x20;
    S->analog_x = yoke_adc(in->x);
    S->analog_y = yoke_adc((uint8_t)(255 - in->y));     // (the pot reads more as it goes up)
}

void sw_set_audio_rate(int rate)
{
    if (rate > 0 && rate < SW_AUDIO_RATE) {
        S->sample_rate = (uint32_t)rate;
        // the boards: a POKEY with every channel at volume 0 stops its
        // counters (pokey.h: approx_mute) instead of counting at 1.5 MHz,
        // and the CPUs' idle loops are skipped (the same results)
        for (int i = 0; i < 4; i++) S->pk[i].approx_mute = true;
        S->idle_skip = true;     // (the idle loops applied, not run: run_main)
    }
}

int sw_audio_rate(void) { return (int)S->sample_rate; }
void sw_set_audio_enabled(bool on) { S->audio_enabled = on; }
void sw_set_draw_every(int n) { S->draw_every = n > 1 ? n : 1; }
void sw_set_draw_enabled(bool on) { S->draw_enabled = on; }
int sw_width(void) { return S->width; }
int sw_height(void) { return S->height; }
const uint8_t *sw_picture(void) { return S->picture; }
const uint32_t *sw_palette(void) { return S->palette; }

const uint32_t *sw_framebuffer(void)
{
    const size_t n = (size_t)S->width * (size_t)S->height;
    if (!S->fb) S->fb = (uint32_t *)malloc(n * 4);
    if (!S->fb) return NULL;
    for (size_t i = 0; i < n; ++i) S->fb[i] = S->palette[S->picture[i]];
    return S->fb;
}
