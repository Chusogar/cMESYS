// vlm5030dsp.c - see vlm5030dsp.h
#include <string.h>
#include "vlm5030dsp.h"

enum { PH_RESET, PH_IDLE, PH_SETUP, PH_WAIT, PH_RUN, PH_STOP, PH_END };
#define FR 4

static const uint8_t kSpeed[8] = {160 / FR, 120 / FR, 80 / FR, 80 / FR, 160 / FR, 240 / FR, 200 / FR, 200 / FR};
static const uint8_t kEnergy[32] = {0, 1, 2, 3, 5, 6, 7, 9, 11, 13, 15, 17, 19, 22, 24, 27,
                                    31, 34, 38, 42, 47, 51, 57, 62, 68, 75, 82, 89, 98, 107, 116, 127};
static const uint8_t kPitch[32] = {0, 21, 22, 23, 24, 25, 26, 27, 28, 29, 31, 33, 35, 37, 39, 41,
                                   43, 45, 49, 53, 57, 61, 65, 69, 73, 77, 85, 93, 101, 109, 117, 125};
static const int kK1[64] = {
    390, 403, 414, 425, 434, 443, 450, 457, 463, 469, 474, 478, 482, 485, 488, 491,
    494, 496, 498, 499, 501, 502, 503, 504, 505, 506, 507, 507, 508, 508, 509, 509,
    -390, -376, -360, -344, -325, -305, -284, -261, -237, -211, -183, -155, -125, -95, -64, -32,
    0, 32, 64, 95, 125, 155, 183, 211, 237, 261, 284, 305, 325, 344, 360, 376};
static const int kK2[32] = {0, 50, 100, 149, 196, 241, 284, 325, 362, 396, 426, 452, 473, 490, 502, 510,
                            0, -510, -502, -490, -473, -452, -426, -396, -362, -325, -284, -241, -196, -149, -100, -50};
static const int kK3[16] = {0, 64, 128, 192, 256, 320, 384, 448, -512, -448, -384, -320, -256, -192, -128, -64};
static const int kK5[8] = {0, 128, 256, 384, -512, -384, -256, -128};

static uint8_t rom_byte(const vlm5030d *v, uint32_t a) { return v->rom_size ? v->rom[a & v->address_mask] : 0; }

static void setup_parameter(vlm5030d *v, uint8_t p)
{
    v->parameter = p;
    v->interp_step = (p & 2) ? 4 : (p & 1) ? 2 : 1;
    v->frame_size = kSpeed[(p >> 3) & 7];
    v->pitch_offset = (p & 0x80) ? -8 : (p & 0x40) ? 8 : 0;
}

void vlm5030d_reset(vlm5030d *v)
{
    v->phase = PH_RESET;
    v->address = 0;
    v->vcu_addr_h = 0;
    v->pin_bsy = 0;
    v->old_energy = v->new_energy = v->target_energy = 0;
    v->old_pitch = v->new_pitch = v->target_pitch = 0;
    v->current_energy = v->current_pitch = 0;
    memset(v->old_k, 0, sizeof v->old_k);
    memset(v->new_k, 0, sizeof v->new_k);
    memset(v->current_k, 0, sizeof v->current_k);
    memset(v->target_k, 0, sizeof v->target_k);
    v->interp_count = 0;
    v->sample_count = v->pitch_count = 0;
    memset(v->x, 0, sizeof v->x);
    v->out = 0;
    setup_parameter(v, 0);
}

void vlm5030d_init(vlm5030d *v, uint32_t clock, const uint8_t *rom, uint32_t rom_size, float amplitude)
{
    memset(v, 0, sizeof *v);
    v->clock = clock ? clock : 1;
    v->amplitude = amplitude;
    v->rom = rom;
    v->rom_size = rom_size;
    v->address_mask = rom_size ? rom_size - 1 : 0;
    v->rng = 1;
    vlm5030d_reset(v);
    v->phase = PH_IDLE;
    setup_parameter(v, 0);
}

int vlm5030d_cycles_per_sample(const vlm5030d *v, uint32_t cpu_clock)
{
    const uint32_t rate = v->clock / 440;
    return rate ? (int)((cpu_clock + rate / 2) / rate) : 440;
}

void vlm5030d_set_rst(vlm5030d *v, uint8_t pin)
{
    if (v->pin_rst == pin) return;
    if (pin == 0) {
        v->pin_rst = 0;
        setup_parameter(v, v->latch_data);
    } else {
        v->pin_rst = 1;
        if (v->pin_bsy) vlm5030d_reset(v);
    }
}

void vlm5030d_set_st(vlm5030d *v, uint8_t pin)
{
    if (v->pin_st == pin) return;
    if (pin == 0) {
        v->pin_st = 0;
        if (v->pin_vcu) v->vcu_addr_h = (uint16_t)(((uint16_t)v->latch_data << 8) + 1);
        else {
            if (v->vcu_addr_h) {
                v->address = (uint16_t)((v->vcu_addr_h & 0xff00) + v->latch_data);
                v->vcu_addr_h = 0;
            } else {
                const uint16_t t = (uint16_t)((v->latch_data & 0xfe) + ((v->latch_data & 1) << 8));
                v->address = (uint16_t)((((uint16_t)rom_byte(v, t) << 8) | rom_byte(v, t + 1u)) & v->address_mask);
            }
            v->sample_count = (uint8_t)v->frame_size;
            v->interp_count = FR;
            v->phase = PH_RUN;
        }
    } else {
        v->pin_st = 1;
        v->phase = PH_SETUP;
        v->sample_count = 1;
        v->pin_bsy = 1;
    }
}

static uint16_t get_bits(const vlm5030d *v, uint8_t sbit, uint8_t bits)
{
    const uint32_t a = (uint32_t)v->address + (sbit >> 3);
    uint16_t d = (uint16_t)(rom_byte(v, a) | ((uint16_t)rom_byte(v, a + 1) << 8));
    d = (uint16_t)(d >> (sbit & 7));
    return (uint16_t)(d & (0xff >> (8 - bits)));
}

static int parse_frame(vlm5030d *v)
{
    v->old_energy = v->new_energy;
    v->old_pitch = v->new_pitch;
    memcpy(v->old_k, v->new_k, sizeof v->old_k);
    const uint8_t cmd = rom_byte(v, v->address);
    if (cmd & 1) {
        v->new_energy = 0;
        v->new_pitch = 0;
        memset(v->new_k, 0, sizeof v->new_k);
        v->address = (uint16_t)((v->address + 1) & v->address_mask);
        if (cmd & 2) return 0;
        return ((cmd >> 2) + 1) * 2 * FR;
    }
    v->new_pitch = kPitch[get_bits(v, 1, 5)];
    if (v->new_pitch > 0) v->new_pitch = (uint8_t)((int)v->new_pitch + v->pitch_offset);
    v->new_energy = kEnergy[get_bits(v, 6, 5)];
    for (int i = 0; i < 6; i++) v->new_k[9 - i] = kK5[get_bits(v, (uint8_t)(11 + 3 * i), 3)];
    v->new_k[3] = kK3[get_bits(v, 29, 4)];
    v->new_k[2] = kK3[get_bits(v, 33, 4)];
    v->new_k[1] = kK2[get_bits(v, 37, 5)];
    v->new_k[0] = kK1[get_bits(v, 42, 6)];
    v->address = (uint16_t)((v->address + 6) & v->address_mask);
    return FR;
}

void vlm5030d_update_stream(vlm5030d *v)
{
    if (v->phase == PH_RUN || v->phase == PH_STOP) {
        if (v->sample_count == 0) {
            if (v->phase == PH_STOP) {
                v->phase = PH_END;
                v->sample_count = 1;
                goto phase_stop;
            }
            v->sample_count = (uint8_t)v->frame_size;
            if (v->interp_count == 0) {
                v->interp_count = parse_frame(v);
                if (v->interp_count == 0) {
                    v->interp_count = FR;
                    v->sample_count = (uint8_t)v->frame_size;
                    v->phase = PH_STOP;
                }
                v->current_energy = v->old_energy;
                v->current_pitch = v->old_pitch;
                memcpy(v->current_k, v->old_k, sizeof v->current_k);
                if (v->current_energy == 0) {
                    v->target_energy = 0;
                    v->target_pitch = (uint8_t)v->current_pitch;
                    memcpy(v->target_k, v->current_k, sizeof v->target_k);
                } else {
                    v->target_energy = v->new_energy;
                    v->target_pitch = v->new_pitch;
                    memcpy(v->target_k, v->new_k, sizeof v->target_k);
                }
            }
            v->interp_count -= v->interp_step;
            const int e = FR - (v->interp_count % FR);
            v->current_energy = (uint32_t)(v->old_energy + ((int)v->target_energy - (int)v->old_energy) * e / FR);
            if (v->old_pitch > 1) v->current_pitch = (uint32_t)(v->old_pitch + ((int)v->target_pitch - (int)v->old_pitch) * e / FR);
            for (int i = 0; i < 10; i++) v->current_k[i] = v->old_k[i] + (v->target_k[i] - v->old_k[i]) * e / FR;
        }
        int cur;
        if (v->old_energy == 0) cur = 0;
        else if (v->old_pitch <= 1) {
            v->rng = v->rng * 1103515245u + 12345u;
            cur = (v->rng & 0x100) ? (int)v->current_energy : -(int)v->current_energy;
        } else cur = v->pitch_count == 0 ? (int)v->current_energy : 0;
        int u[11];
        u[10] = cur;
        for (int i = 9; i >= 0; i--) u[i] = u[i + 1] - ((-v->current_k[i] * v->x[i]) / 512);
        for (int i = 9; i >= 1; i--) v->x[i] = v->x[i - 1] + ((-v->current_k[i - 1] * u[i - 1]) / 512);
        v->x[0] = u[0];
        v->out = u[0] * 64;
        if (v->out > 32768) v->out = 32768;
        else if (v->out < -32767) v->out = -32767;
        v->out = (int32_t)(v->out * v->amplitude);
        v->sample_count--;
        v->pitch_count++;
        if (v->pitch_count >= v->current_pitch) v->pitch_count = 0;
        return;
    }
phase_stop:
    switch (v->phase) {
    case PH_SETUP:
        if (v->sample_count <= 1) {
            v->sample_count = 0;
            v->phase = PH_WAIT;
        } else v->sample_count--;
        break;
    case PH_END:
        if (v->sample_count <= 1) {
            v->sample_count = 0;
            v->pin_bsy = 0;
            v->phase = PH_IDLE;
        } else v->sample_count--;
        break;
    default: break;
    }
    v->out = 0;
}
