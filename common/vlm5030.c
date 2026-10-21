#include "vlm5030.h"
#include <string.h>

#define PH_RESET 0
#define PH_IDLE  1
#define PH_SETUP 2
#define PH_WAIT  3
#define PH_RUN   4
#define PH_STOP  5
#define PH_END   6
#define FR_SIZE  4

#define IP_SIZE_SLOWER (240/FR_SIZE)
#define IP_SIZE_SLOW   (200/FR_SIZE)
#define IP_SIZE_NORMAL (160/FR_SIZE)
#define IP_SIZE_FAST   (120/FR_SIZE)
#define IP_SIZE_FASTER ( 80/FR_SIZE)

static const int speed_table[8] = {
    IP_SIZE_NORMAL, IP_SIZE_FAST, IP_SIZE_FASTER, IP_SIZE_FASTER,
    IP_SIZE_NORMAL, IP_SIZE_SLOWER, IP_SIZE_SLOW, IP_SIZE_SLOW
};
static const int energytable[0x20] = {
    0,1,2,3,5,6,7,9,11,13,15,17,19,22,24,27,
    31,34,38,42,47,51,57,62,68,75,82,89,98,107,116,127
};
static const int pitchtable[0x20] = {
    0,21,22,23,24,25,26,27,28,29,31,33,35,37,39,41,
    43,45,49,53,57,61,65,69,73,77,85,93,101,109,117,125
};
static const int K1_table[0x40] = {
    390,403,414,425,434,443,450,457,463,469,474,478,482,485,488,491,
    494,496,498,499,501,502,503,504,505,506,507,507,508,508,509,509,
   -390,-376,-360,-344,-325,-305,-284,-261,-237,-211,-183,-155,-125,-95,-64,-32,
      0,32,64,95,125,155,183,211,237,261,284,305,325,344,360,376
};
static const int K2_table[0x20] = {
      0,50,100,149,196,241,284,325,362,396,426,452,473,490,502,510,
      0,-510,-502,-490,-473,-452,-426,-396,-362,-325,-284,-241,-196,-149,-100,-50
};
static const int K3_table[0x10] = {
      0,64,128,192,256,320,384,448,-512,-448,-384,-320,-256,-192,-128,-64
};
static const int K5_table[0x08] = { 0,128,256,384,-512,-384,-256,-128 };

static uint8_t rbyte(VLM5030 *v, int a)
{
    if (!v->rom || v->rom_size <= 0) return 0xff;
    return v->rom[a & (v->rom_size - 1)];
}
static int get_bits(VLM5030 *v, int sbit, int bits)
{
    int off = v->address + (sbit >> 3);
    int data = rbyte(v, off) | (rbyte(v, off + 1) << 8);
    data >>= (sbit & 7);
    data &= (0xff >> (8 - bits));
    return data;
}
static void setup_parameter(VLM5030 *v, uint8_t p)
{
    v->parameter = p;
    if (p & 2) v->interp_step = 4;
    else if (p & 1) v->interp_step = 2;
    else v->interp_step = 1;
    v->frame_size = speed_table[(p >> 3) & 7];
    if (p & 0x80) v->pitch_offset = -8;
    else if (p & 0x40) v->pitch_offset = 8;
    else v->pitch_offset = 0;
}
static void reset_core(VLM5030 *v)
{
    v->phase = PH_RESET;
    v->address = 0;
    v->vcu_addr_h = 0;
    v->pin_bsy = 0;
    v->old_energy = v->old_pitch = 0;
    v->new_energy = v->new_pitch = 0;
    v->current_energy = v->current_pitch = 0;
    v->target_energy = v->target_pitch = 0;
    memset(v->old_k,0,sizeof(v->old_k)); memset(v->new_k,0,sizeof(v->new_k));
    memset(v->current_k,0,sizeof(v->current_k)); memset(v->target_k,0,sizeof(v->target_k));
    memset(v->x,0,sizeof(v->x));
    v->interp_count = v->sample_count = v->pitch_count = 0;
    v->out_sample = 0; v->out_div = 0; v->rng = 0x12345678;
    setup_parameter(v,0);
}
static int parse_frame(VLM5030 *v)
{
    for(int i=0;i<10;i++){v->old_k[i]=v->new_k[i];}
    v->old_energy = v->new_energy;
    v->old_pitch = v->new_pitch;
    uint8_t cmd = rbyte(v, v->address);
    if (cmd & 1) {
        v->new_pitch = 0; v->new_energy = 0; memset(v->new_k,0,sizeof(v->new_k));
        v->address++;
        if (cmd & 2) return 0;
        return (((cmd >> 2) + 1) * 2) * FR_SIZE;
    }
    v->new_pitch = (pitchtable[get_bits(v,1,5)] + v->pitch_offset) & 0xff;
    v->new_energy = energytable[get_bits(v,6,5)];
    v->new_k[9]=K5_table[get_bits(v,11,3)]; v->new_k[8]=K5_table[get_bits(v,14,3)];
    v->new_k[7]=K5_table[get_bits(v,17,3)]; v->new_k[6]=K5_table[get_bits(v,20,3)];
    v->new_k[5]=K5_table[get_bits(v,23,3)]; v->new_k[4]=K5_table[get_bits(v,26,3)];
    v->new_k[3]=K3_table[get_bits(v,29,4)]; v->new_k[2]=K3_table[get_bits(v,33,4)];
    v->new_k[1]=K2_table[get_bits(v,37,5)]; v->new_k[0]=K1_table[get_bits(v,42,6)];
    v->address += 6;
    return FR_SIZE;
}
static int noise_bit(VLM5030 *v)
{
    uint32_t b = ((v->rng >> 0) ^ (v->rng >> 2) ^ (v->rng >> 3) ^ (v->rng >> 5)) & 1;
    v->rng = (v->rng >> 1) | (b << 31);
    return v->rng & 1;
}
static int clock_sample(VLM5030 *v)
{
    if (v->phase == PH_SETUP) {
        if (--v->sample_count <= 0) { v->sample_count = 0; v->phase = PH_WAIT; }
        return 0;
    }
    if (v->phase == PH_END) {
        if (--v->sample_count <= 0) { v->sample_count = 0; v->pin_bsy = 0; v->phase = PH_IDLE; }
        return 0;
    }
    if (v->phase != PH_RUN && v->phase != PH_STOP) return 0;

    if (v->sample_count <= 0) {
        if (v->phase == PH_STOP) { v->phase = PH_END; v->sample_count = 1; return 0; }
        v->sample_count = v->frame_size;
        if (v->interp_count <= 0) {
            v->interp_count = parse_frame(v);
            if (v->interp_count == 0) { v->interp_count = FR_SIZE; v->sample_count = v->frame_size; v->phase = PH_STOP; }
            v->current_energy = v->old_energy; v->current_pitch = v->old_pitch;
            for(int i=0;i<10;i++) v->current_k[i]=v->old_k[i];
            if (v->current_energy == 0) {
                v->target_energy = 0; v->target_pitch = v->current_pitch;
                for(int i=0;i<10;i++) v->target_k[i]=v->current_k[i];
            } else {
                v->target_energy = v->new_energy; v->target_pitch = v->new_pitch;
                for(int i=0;i<10;i++) v->target_k[i]=v->new_k[i];
            }
        }
        v->interp_count -= v->interp_step;
        int interp = FR_SIZE - (v->interp_count % FR_SIZE);
        v->current_energy = v->old_energy + (v->target_energy - v->old_energy) * interp / FR_SIZE;
        if (v->old_pitch > 1) v->current_pitch = v->old_pitch + (v->target_pitch - v->old_pitch) * interp / FR_SIZE;
        for(int i=0;i<10;i++) v->current_k[i] = v->old_k[i] + (v->target_k[i]-v->old_k[i]) * interp / FR_SIZE;
    }

    int current_val;
    if (v->old_energy == 0) current_val = 0;
    else if (v->old_pitch <= 1) current_val = noise_bit(v) ? v->current_energy : -v->current_energy;
    else current_val = (v->pitch_count == 0) ? v->current_energy : 0;

    int u[11]; u[10] = current_val;
    for(int i=9;i>=0;i--) u[i] = u[i+1] - ((-v->current_k[i] * v->x[i]) / 512);
    for(int i=9;i>=1;i--) v->x[i] = v->x[i-1] + ((-v->current_k[i-1] * u[i-1]) / 512);
    v->x[0] = u[0];

    v->sample_count--;
    v->pitch_count++;
    if (v->current_pitch > 0 && v->pitch_count >= v->current_pitch) v->pitch_count = 0;

    if (u[0] > 511) u[0] = 511; else if (u[0] < -511) u[0] = -511;
    return u[0] << 6;
}

void vlm5030_init(VLM5030 *v, const uint8_t *rom, int rom_size)
{
    memset(v,0,sizeof(*v)); v->rom=rom; v->rom_size=rom_size; reset_core(v); v->phase=PH_IDLE;
}
void vlm5030_write_data(VLM5030 *v, uint8_t data) { v->data = data; }
void vlm5030_write_ctrl(VLM5030 *v, uint8_t data)
{
    uint8_t st = data & 1, rst = (data >> 1) & 1, vcu = (data >> 2) & 1;
    v->ctrl = data;
    if (v->prev_rst != rst) {
        if (!rst) setup_parameter(v, v->data);
        else if (v->pin_bsy) reset_core(v);
        v->prev_rst = rst;
    }
    v->prev_vcu = vcu;
    if (v->prev_st != st) {
        if (!st) {
            if (vcu) v->vcu_addr_h = ((int)v->data << 8) + 0x01;
            else {
                if (v->vcu_addr_h) { v->address = (v->vcu_addr_h & 0xff00) + v->data; v->vcu_addr_h = 0; }
                else { int table = (v->data & 0xfe) + ((v->data & 1) << 8); v->address = (rbyte(v, table) << 8) | rbyte(v, table+1); }
                v->sample_count = v->frame_size; v->interp_count = FR_SIZE; v->phase = PH_RUN;
            }
        } else { v->phase = PH_SETUP; v->sample_count = 1; v->pin_bsy = 1; }
        v->prev_st = st;
    }
}
uint8_t vlm5030_busy(const VLM5030 *v) { return v->pin_bsy ? 1 : 0; }
void vlm5030_update(VLM5030 *v, int16_t *dst, int samples)
{
    /* MAME stream rate ~= clock/440 = 8135 Hz. Para 44.1 kHz repetimos muestra. */
    for(int i=0;i<samples;i++) {
        if (--v->out_div <= 0) { v->out_sample = clock_sample(v); v->out_div = 5; }
        int out = dst[i] + v->out_sample;
        if (out > 32767) out = 32767; else if (out < -32768) out = -32768;
        dst[i] = (int16_t)out;
    }
}
