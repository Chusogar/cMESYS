// upd7759d.c - see upd7759d.h. Translated from dsp-cpp's sound/upd7759.
#include <stddef.h>
#include "upd7759d.h"

enum { U_IDLE, U_DROPDRQ, U_START, U_FIRSTREQ, U_LASTSAMPLE, U_DUMMY1, U_ADDRMSB, U_ADDRLSB, U_DUMMY2,
       U_BLOCKHEADER, U_NIBBLECOUNT, U_NIBBLEMSN, U_NIBBLELSN };
#define UPD_FRAC_BITS 20
#define UPD_FRAC_ONE (1u << UPD_FRAC_BITS)

static const int kStep[16][16] = {
    {0, 0, 1, 2, 3, 5, 7, 10, 0, 0, -1, -2, -3, -5, -7, -10},
    {0, 1, 2, 3, 4, 6, 8, 13, 0, -1, -2, -3, -4, -6, -8, -13},
    {0, 1, 2, 4, 5, 7, 10, 15, 0, -1, -2, -4, -5, -7, -10, -15},
    {0, 1, 3, 4, 6, 9, 13, 19, 0, -1, -3, -4, -6, -9, -13, -19},
    {0, 2, 3, 5, 8, 11, 15, 23, 0, -2, -3, -5, -8, -11, -15, -23},
    {0, 2, 4, 7, 10, 14, 19, 29, 0, -2, -4, -7, -10, -14, -19, -29},
    {0, 3, 5, 8, 12, 16, 22, 33, 0, -3, -5, -8, -12, -16, -22, -33},
    {1, 4, 7, 10, 15, 20, 29, 43, -1, -4, -7, -10, -15, -20, -29, -43},
    {1, 4, 8, 13, 18, 25, 35, 53, -1, -4, -8, -13, -18, -25, -35, -53},
    {1, 6, 10, 16, 22, 31, 43, 64, -1, -6, -10, -16, -22, -31, -43, -64},
    {2, 7, 12, 19, 27, 37, 51, 76, -2, -7, -12, -19, -27, -37, -51, -76},
    {2, 9, 16, 24, 34, 46, 64, 96, -2, -9, -16, -24, -34, -46, -64, -96},
    {3, 11, 19, 29, 41, 57, 79, 117, -3, -11, -19, -29, -41, -57, -79, -117},
    {4, 13, 24, 36, 50, 69, 96, 143, -4, -13, -24, -36, -50, -69, -96, -143},
    {4, 16, 29, 44, 62, 85, 118, 175, -4, -16, -29, -44, -62, -85, -118, -175},
    {6, 20, 36, 54, 76, 104, 144, 214, -6, -20, -36, -54, -76, -104, -144, -214},
};

static const int kStateTable[16] = {-1, -1, 0, 0, 1, 2, 2, 3, -1, -1, 0, 0, 1, 2, 2, 3};


void upd7759d_init(upd7759d *u, float amp, uint32_t sample_rate)
{
    u->rom = NULL;
    u->rom_size = 0;
    u->drq_cb = NULL;
    u->amp = amp;
    u->step = 4 * UPD_FRAC_ONE;
    u->fifo_in = 0;
    u->reset_pin = 1;
    u->start = 1;
    u->resample_pos = 0;
    u->resample_inc = (float)UPD7759_CLOCK / 4.0f / (float)sample_rate;
    upd7759d_reset(u);
}

uint8_t upd7759d_busy_r(const upd7759d *u) { return u->state == U_IDLE ? 1 : 0; }

void upd7759d_reset(upd7759d *u) {
    u->pos = 0;
    u->state = U_IDLE;
    u->clocks_left = 0;
    u->nibbles_left = 0;
    u->repeat_count = 0;
    u->post_drq_state = U_IDLE;
    u->post_drq_clocks = 0;
    u->req_sample = 0;
    u->last_sample = 0;
    u->block_header = 0;
    u->sample_rate = 0;
    u->first_valid_header = 0;
    u->offset = 0;
    u->repeat_offset = 0;
    u->adpcm_state = 0;
    u->adpcm_data = 0;
    u->sample = 0;
    u->drq = 0;
}

static uint8_t rom_byte(const upd7759d *u, uint32_t offset) {
    if ((u->rom == NULL)) return u->fifo_in;
    return u->rom[offset % u->rom_size];
}

static void update_adpcm(upd7759d *u, int data) {
    u->sample += kStep[u->adpcm_state][data];
    u->adpcm_state += kStateTable[data];
    if (u->adpcm_state < 0) u->adpcm_state = 0;
    if (u->adpcm_state > 15) u->adpcm_state = 15;
}

static void advance_state(upd7759d *u) {
    switch (u->state) {
        case U_IDLE:
            u->clocks_left = 4;
            break;
        case U_DROPDRQ:
            u->drq = 0;
            u->clocks_left = u->post_drq_clocks;
            u->state = u->post_drq_state;
            break;
        case U_START:
            u->req_sample = (u->rom == NULL) ? 0x10 : u->fifo_in;
            u->clocks_left = 70;
            u->state = U_FIRSTREQ;
            break;
        case U_FIRSTREQ:
            u->drq = 1;
            u->clocks_left = 44;
            u->state = U_LASTSAMPLE;
            break;
        case U_LASTSAMPLE:
            u->last_sample = (u->rom == NULL) ? u->fifo_in : rom_byte(u, 0);
            u->drq = 1;
            u->clocks_left = 28;
            u->state = (u->req_sample > u->last_sample) ? U_IDLE : U_DUMMY1;
            break;
        case U_DUMMY1:
            u->drq = 1;
            u->clocks_left = 32;
            u->state = U_ADDRMSB;
            break;
        case U_ADDRMSB:
            u->offset = (uint32_t)((u->rom == NULL) ? u->fifo_in : rom_byte(u, (uint32_t)(u->req_sample) * 2 + 5))
                      << 9;
            u->drq = 1;
            u->clocks_left = 44;
            u->state = U_ADDRLSB;
            break;
        case U_ADDRLSB:
            u->offset |= (uint32_t)((u->rom == NULL) ? u->fifo_in : rom_byte(u, (uint32_t)(u->req_sample) * 2 + 6))
                       << 1;
            u->drq = 1;
            u->clocks_left = 36;
            u->state = U_DUMMY2;
            break;
        case U_DUMMY2:
            u->offset += 1;
            u->first_valid_header = 0;
            u->drq = 1;
            u->clocks_left = 36;
            u->state = U_BLOCKHEADER;
            break;
        case U_BLOCKHEADER:
            if (u->repeat_count != 0) {
                u->repeat_count--;
                u->offset = u->repeat_offset;
            }
            u->block_header = (u->rom == NULL) ? u->fifo_in : rom_byte(u, u->offset & 0x1ffff);
            u->offset++;
            u->drq = 1;
            switch (u->block_header & 0xc0) {
                case 0x00:
                    u->clocks_left = 1024 * ((u->block_header & 0x3f) + 1);
                    u->state = (u->block_header == 0 && u->first_valid_header) ? U_IDLE : U_BLOCKHEADER;
                    u->sample = 0;
                    u->adpcm_state = 0;
                    break;
                case 0x40:
                    u->sample_rate = (uint8_t)((u->block_header & 0x3f) + 1);
                    u->nibbles_left = 256;
                    u->clocks_left = 36;
                    u->state = U_NIBBLEMSN;
                    break;
                case 0x80:
                    u->sample_rate = (uint8_t)((u->block_header & 0x3f) + 1);
                    u->clocks_left = 36;
                    u->state = U_NIBBLECOUNT;
                    break;
                default:
                    u->repeat_count = (uint8_t)((u->block_header & 7) + 1);
                    u->repeat_offset = u->offset;
                    u->clocks_left = 36;
                    u->state = U_BLOCKHEADER;
                    break;
            }
            if (u->block_header != 0) u->first_valid_header = 1;
            break;
        case U_NIBBLECOUNT:
            u->nibbles_left = (uint16_t)(((u->rom == NULL) ? u->fifo_in : rom_byte(u, u->offset & 0x1ffff)) + 1);
            u->offset++;
            u->drq = 1;
            u->clocks_left = 36;
            u->state = U_NIBBLEMSN;
            break;
        case U_NIBBLEMSN:
            u->adpcm_data = (u->rom == NULL) ? u->fifo_in : rom_byte(u, u->offset & 0x1ffff);
            u->offset++;
            update_adpcm(u, u->adpcm_data >> 4);
            u->drq = 1;
            u->clocks_left = u->sample_rate * 4;
            u->nibbles_left--;
            u->state = (u->nibbles_left == 0) ? U_BLOCKHEADER : U_NIBBLELSN;
            break;
        case U_NIBBLELSN:
            update_adpcm(u, u->adpcm_data & 15);
            u->clocks_left = u->sample_rate * 4;
            u->nibbles_left--;
            u->state = (u->nibbles_left == 0) ? U_BLOCKHEADER : U_NIBBLEMSN;
            break;
        default:
            break;
    }
    if (u->drq & 1) {
        u->post_drq_state = u->state;
        u->post_drq_clocks = u->clocks_left - 21;
        u->state = U_DROPDRQ;
        u->clocks_left = 21;
    }
}

void upd7759d_reset_w(upd7759d *u, uint8_t data) {
    const uint8_t old = u->reset_pin;
    u->reset_pin = data ? 1 : 0;
    if (old && !u->reset_pin) upd7759d_reset(u);
}

void upd7759d_start_w(upd7759d *u, uint8_t data) {
    const uint8_t old = u->start;
    u->start = data ? 1 : 0;
    if (u->state == U_IDLE && !old && u->start && u->reset_pin) u->state = U_START;
}

void upd7759d_port_w(upd7759d *u, uint8_t data) { u->fifo_in = data; }

int32_t upd7759d_update(upd7759d *u) {
    int clocks_left = u->clocks_left;
    uint32_t pos = u->pos;
    int out = 0;
    u->resample_pos += u->resample_inc;
    int num_samples = (int)(u->resample_pos);
    u->resample_pos -= (float)(num_samples);
    if (u->state != U_IDLE) {
        while (num_samples != 0) {
            out = u->sample << 7;
            pos += u->step;
            while (pos >= UPD_FRAC_ONE) {
                int clocks_this = (int)(pos >> UPD_FRAC_BITS);
                if (clocks_this > clocks_left) clocks_this = clocks_left;
                pos -= (uint32_t)(clocks_this) * UPD_FRAC_ONE;
                clocks_left -= clocks_this;
                if (clocks_left == 0) {
                    const uint8_t old_drq = u->drq;
                    advance_state(u);
                    if (old_drq != u->drq && u->drq_cb) u->drq_cb(u->user, u->drq);
                    if (u->state == U_IDLE) break;
                    clocks_left = u->clocks_left;
                    out = (out + u->sample * 64) / 2;
                }
            }
            num_samples--;
        }
    }
    u->clocks_left = clocks_left;
    u->pos = pos;
    out = (int)((float)(out) * u->amp);
    return out < -0x7fff ? -0x7fff : out > 0x7fff ? 0x7fff : out;
}

