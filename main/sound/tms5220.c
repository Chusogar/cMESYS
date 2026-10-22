// tms5220.c - TI TMS5220 LPC speech synthesizer. C port of dsp-cpp's
// sound/tms5220.cpp (US patent 4,331,836 timing, decapped coefficient ROM).
#include "tms5220.h"

#include <string.h>

#define ENERGY_BITS 4
#define PITCH_BITS 6
#define CLOCKS_PER_SAMPLE 80
#define IO_READY_CLOCKS 16

static const int k_bits[TMS5220_NUM_K] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
static const uint16_t k_energy[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};
static const uint16_t k_pitch[64] = {
    0,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30, 31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  44,  46,  48,
    50, 52,  53,  56,  58,  60,  62,  65,  68,  70,  72,  76,  78,  80,  84,  86,
    91, 94,  98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159};
static const int16_t k_k[TMS5220_NUM_K][32] = {
    {-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452,
     -445, -437, -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436},
    {-328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
     248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506},
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409},
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307}};
static const int8_t k_chirp[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a,
    0x32, 0x3b, 0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const int k_interp[8] = {0, 3, 3, 3, 2, 2, 1, 1};
static const int k_reload[4] = {0, 2, 4, 6};

static int coefficient(int index, int value)
{
    int hi = (1 << k_bits[index]) - 1;
    if (value < 0) value = 0;
    if (value > hi) value = hi;
    return k_k[index][value];
}

static inline int32_t matrix_multiply(int32_t a, int32_t b)
{
    while (a > 511) a -= 1024;
    while (a < -512) a += 1024;
    while (b > 16383) b -= 32768;
    while (b < -16384) b += 32768;
    return (a * b) >> 9;
}

static int16_t clip_analog(int32_t s)
{
    int32_t c = s < -2048 ? -2048 : (s > 2047 ? 2047 : s);
    c &= ~0xf;
    return (int16_t)((c << 4) | ((c & 0x7f0) >> 3) | ((c & 0x400) >> 10));
}

static bool talk_status(const tms5220 *t) { return t->spen || t->talkd; }

static void set_interrupt_state(tms5220 *t, bool state)
{
    t->irq_pin = state;
}

static void set_idle_frame(tms5220 *t)
{
    t->new_energy_idx = 0;
    t->new_pitch_idx = 0;
    for (int i = 0; i < 4; ++i) t->new_k_idx[i] = 0;
    for (int i = 4; i < 7; ++i) t->new_k_idx[i] = 0x0f;
    for (int i = 7; i < TMS5220_NUM_K; ++i) t->new_k_idx[i] = 0x07;
}

static void update_fifo_status_and_ints(tms5220 *t)
{
    if (t->fifo_count <= 8) {
        if (!t->buffer_low) {
            t->buffer_low = true;
            set_interrupt_state(t, true);
        }
    } else {
        t->buffer_low = false;
    }
    if (t->fifo_count == 0) {
        if (!t->buffer_empty) {
            t->buffer_empty = true;
            set_interrupt_state(t, true);
        }
        if (t->ddis) t->talk = t->spen = false;
    } else {
        t->buffer_empty = false;
    }
    if (t->previous_talk_status && !talk_status(t)) {
        set_interrupt_state(t, true);
        t->ddis = false;
    }
    t->previous_talk_status = talk_status(t);
}

static void chip_reset(tms5220 *t)
{
    memset(t->fifo, 0, sizeof t->fifo);
    t->fifo_head = t->fifo_tail = t->fifo_count = t->fifo_bits_taken = 0;
    t->spen = t->ddis = t->talk = t->talkd = t->previous_talk_status = false;
    set_interrupt_state(t, false);
    t->buffer_empty = t->buffer_low = true;
    t->data_latched = false;
    t->io_ready_delay = 0;
    t->new_energy_idx = t->current_energy = t->previous_energy = 0;
    t->new_pitch_idx = t->current_pitch = 0;
    t->zpar = t->uv_zpar = false;
    memset(t->new_k_idx, 0, sizeof t->new_k_idx);
    memset(t->current_k, 0, sizeof t->current_k);
    t->inhibit = true;
    t->pitch_zero = false;
    t->subcycle = t->pc = t->pitch_count = 0;
    t->c_variant_rate = 0;
    t->olde = t->oldp = true;
    t->ip = k_reload[t->c_variant_rate & 3];
    t->excitation_data = 0;
    t->rng = 0x1fff;
    memset(t->u, 0, sizeof t->u);
    memset(t->x, 0, sizeof t->x);
    t->out_sample = 0;
    t->cycle_acc = 0;
}

void tms5220_reset(tms5220 *t)
{
    t->rs_ws = 0x03;
    t->io_ready = true;
    t->write_latch = 0;
    chip_reset(t);
}

void tms5220_init(tms5220 *t, uint32_t clock, bool tms5220c)
{
    memset(t, 0, sizeof *t);
    t->clock = clock ? clock : 640000;
    t->rate_control = tms5220c;
    tms5220_reset(t);
}

static void process_command(tms5220 *t, uint8_t cmd)
{
    switch (cmd & 0x70) {
    case 0x00:
    case 0x20:
        if (t->rate_control) t->c_variant_rate = cmd & 0x0f;
        break;
    case 0x50:
        t->spen = true;
        t->ddis = false;
        t->zpar = t->uv_zpar = t->olde = t->oldp = true;
        set_idle_frame(t);
        break;
    case 0x60:
        memset(t->fifo, 0, sizeof t->fifo);
        t->fifo_head = t->fifo_tail = t->fifo_count = t->fifo_bits_taken = 0;
        t->ddis = true;
        t->zpar = t->uv_zpar = t->olde = t->oldp = true;
        set_idle_frame(t);
        break;
    case 0x70:
        chip_reset(t);
        break;
    default:
        break;
    }
    update_fifo_status_and_ints(t);
}

static void data_write(tms5220 *t, uint8_t value)
{
    bool old_buffer_low = t->buffer_low;
    if (t->ddis) {
        if (t->fifo_count < TMS5220_FIFO) {
            t->fifo[t->fifo_tail] = value;
            t->fifo_tail = (t->fifo_tail + 1) % TMS5220_FIFO;
            ++t->fifo_count;
            update_fifo_status_and_ints(t);
            if (!t->spen && old_buffer_low && !t->buffer_low) {
                t->zpar = t->uv_zpar = t->olde = t->oldp = true;
                t->spen = true;
                set_idle_frame(t);
            }
        }
    } else {
        process_command(t, value);
    }
    t->data_latched = false;
    t->io_ready = true;
}

void tms5220_write_data(tms5220 *t, uint8_t value)
{
    t->write_latch = value;
    t->data_latched = true;
    data_write(t, value);
}

uint8_t tms5220_status(const tms5220 *t)
{
    return (uint8_t)((talk_status(t) ? 0x80 : 0) | (t->buffer_low ? 0x40 : 0) | (t->buffer_empty ? 0x20 : 0));
}

bool tms5220_readyq(const tms5220 *t)
{
    return !(((t->fifo_count < TMS5220_FIFO) || !t->ddis) && t->io_ready);
}

static void apply_rs_ws(tms5220 *t, bool new_wsq, bool new_rsq)
{
    uint8_t nv = (uint8_t)((new_rsq ? 0x02 : 0) | (new_wsq ? 0x01 : 0));
    if (nv == t->rs_ws) return;
    uint8_t falling = (uint8_t)((t->rs_ws ^ nv) & ~nv);
    t->rs_ws = nv;
    switch (nv) {
    case 0x00:
        if (t->rate_control) tms5220_reset(t);
        return;
    case 0x03:
        return;
    case 0x02:
        if (!(falling & 0x01)) return;
        t->io_ready = false;
        t->io_ready_delay = IO_READY_CLOCKS;
        return;
    case 0x01:
        if (!(falling & 0x02)) return;
        set_interrupt_state(t, false);
        t->io_ready = false;
        t->io_ready_delay = IO_READY_CLOCKS;
        return;
    default:
        return;
    }
}

void tms5220_set_wsq(tms5220 *t, bool level) { apply_rs_ws(t, level, (t->rs_ws & 0x02) != 0); }
void tms5220_set_rsq(tms5220 *t, bool level) { apply_rs_ws(t, (t->rs_ws & 0x01) != 0, level); }

static void service_io_ready(tms5220 *t)
{
    if (t->rs_ws == 0x02) {
        if (t->ddis && t->fifo_count >= TMS5220_FIFO) {
            t->io_ready_delay = IO_READY_CLOCKS;
            return;
        }
        if (t->data_latched) {
            t->data_latched = false;
            data_write(t, t->write_latch);
            return;
        }
    }
    t->io_ready = true;
}

static int read_bits(tms5220 *t, int count)
{
    int value = 0;
    if (!t->ddis) return (1 << count) - 1;
    while (count--) {
        value = (value << 1) | ((t->fifo[t->fifo_head] >> t->fifo_bits_taken) & 1);
        ++t->fifo_bits_taken;
        if (t->fifo_bits_taken >= 8) {
            t->fifo_bits_taken = 0;
            t->fifo[t->fifo_head] = 0;
            if (t->fifo_count > 0) {
                --t->fifo_count;
                t->fifo_head = (t->fifo_head + 1) % TMS5220_FIFO;
                update_fifo_status_and_ints(t);
            }
        }
    }
    return value;
}

static void parse_frame(tms5220 *t)
{
    t->zpar = t->uv_zpar = false;
    if (t->rate_control && (t->c_variant_rate & 0x04)) t->ip = k_reload[read_bits(t, 2) & 3];
    else t->ip = k_reload[t->c_variant_rate & 3];

    update_fifo_status_and_ints(t);
    if (t->ddis && t->buffer_empty) return;
    t->new_energy_idx = read_bits(t, ENERGY_BITS);
    update_fifo_status_and_ints(t);
    if (t->ddis && t->buffer_empty) return;
    if (t->new_energy_idx == 0 || t->new_energy_idx == 0x0f) return;

    int repeat = read_bits(t, 1);
    t->new_pitch_idx = read_bits(t, PITCH_BITS);
    t->uv_zpar = t->new_pitch_idx == 0;
    update_fifo_status_and_ints(t);
    if (t->ddis && t->buffer_empty) return;
    if (repeat) return;
    for (int i = 0; i < 4; ++i) {
        t->new_k_idx[i] = read_bits(t, k_bits[i]);
        update_fifo_status_and_ints(t);
        if (t->ddis && t->buffer_empty) return;
    }
    if (t->new_pitch_idx == 0) return;
    for (int i = 4; i < TMS5220_NUM_K; ++i) {
        t->new_k_idx[i] = read_bits(t, k_bits[i]);
        update_fifo_status_and_ints(t);
        if (t->ddis && t->buffer_empty) return;
    }
}

static int32_t lattice_filter(tms5220 *t)
{
    t->u[10] = matrix_multiply(t->previous_energy, (int32_t)t->excitation_data << 6);
    for (int i = 9; i >= 0; --i) t->u[i] = t->u[i + 1] - matrix_multiply(t->current_k[i], t->x[i]);
    for (int i = 9; i >= 1; --i) t->x[i] = t->x[i - 1] + matrix_multiply(t->current_k[i - 1], t->u[i - 1]);
    t->x[0] = t->u[0];
    t->previous_energy = t->current_energy;
    return t->u[0];
}

static void advance_counters(tms5220 *t, bool speaking)
{
    ++t->subcycle;
    if (t->subcycle == 2 && t->pc == 12) {
        if (t->ip == 7) {
            if (speaking) {
                if (t->inhibit) t->pitch_zero = true;
                t->olde = t->new_energy_idx == 0;
                t->oldp = t->new_pitch_idx == 0;
            }
            t->talkd = t->talk;
            update_fifo_status_and_ints(t);
            if (!t->talk && t->spen) t->talk = true;
        }
        t->subcycle = 1;
        t->pc = 0;
        t->ip = (t->ip + 1) & 7;
    } else if (t->subcycle == 3) {
        t->subcycle = 1;
        ++t->pc;
    }
}

static void process_sample(tms5220 *t)
{
    if (!t->talkd) {
        advance_counters(t, false);
        t->out_sample = -1;
        return;
    }
    if (t->ip == 0 && t->pc == 12 && t->subcycle == 1) {
        t->ip = k_reload[t->c_variant_rate & 3];
        parse_frame(t);
        if (t->new_energy_idx == 0x0f) {
            t->talk = t->spen = false;
            update_fifo_status_and_ints(t);
        }
        bool unvoiced = t->new_pitch_idx == 0, silence = t->new_energy_idx == 0;
        t->inhibit = (t->oldp != unvoiced) || (t->olde && !silence) || (t->oldp && silence);
    } else if (t->subcycle == 2) {
        int gate = (t->inhibit && t->ip != 0) ? 0 : 1;
        int shift = k_interp[t->ip];
        switch (t->pc) {
        case 0:
            if (t->ip == 0) t->pitch_zero = false;
            t->current_energy = t->zpar ? 0
                : t->current_energy + ((((int)k_energy[t->new_energy_idx] - t->current_energy) * gate) >> shift);
            break;
        case 1:
            t->current_pitch = t->zpar ? 0
                : t->current_pitch + ((((int)k_pitch[t->new_pitch_idx] - t->current_pitch) * gate) >> shift);
            break;
        default:
            if (t->pc <= 11) {
                int k = t->pc - 2;
                bool zero = (k < 4) ? t->zpar : t->uv_zpar;
                int target = coefficient(k, t->new_k_idx[k]);
                t->current_k[k] = zero ? 0 : t->current_k[k] + (((target - t->current_k[k]) * gate) >> shift);
            }
            break;
        }
    }

    if (t->oldp) t->excitation_data = (t->rng & 1) ? ~0x3f : 0x40;
    else t->excitation_data = k_chirp[t->pitch_count < 51 ? t->pitch_count : 51];

    for (int i = 0; i < 20; ++i) {
        uint16_t bit = (uint16_t)(((t->rng >> 12) ^ (t->rng >> 3) ^ (t->rng >> 2) ^ t->rng) & 1);
        t->rng = (uint16_t)((t->rng << 1) | bit);
    }

    int32_t s = lattice_filter(t);
    while (s > 16383) s -= 32768;
    while (s < -16384) s += 32768;
    t->out_sample = clip_analog(s);

    advance_counters(t, true);
    ++t->pitch_count;
    if (t->pitch_count >= t->current_pitch || t->pitch_zero) t->pitch_count = 0;
    t->pitch_count &= 0x1ff;
}

void tms5220_tick(tms5220 *t, int cycles)
{
    if (cycles <= 0) return;
    if (t->io_ready_delay > 0) {
        t->io_ready_delay -= cycles;
        if (t->io_ready_delay <= 0) {
            t->io_ready_delay = 0;
            service_io_ready(t);
        }
    }
    t->cycle_acc += cycles;
    while (t->cycle_acc >= CLOCKS_PER_SAMPLE) {
        t->cycle_acc -= CLOCKS_PER_SAMPLE;
        process_sample(t);
    }
}
