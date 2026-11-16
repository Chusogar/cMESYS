// ym2612.c - Yamaha YM2612 (OPN2): a C11 port of dsp-cpp's sound/ym2612.
#include "ym2612.h"

void ym2612_init(ym2612 *y, uint32_t clock, float amplitude) {
    opn_init(&y->opn, 6, clock, YM2612_RATE);
    y->amplitude = amplitude;
    ym2612_reset(y);
}

void ym2612_reset(ym2612 *y) {
    opn_core *O = &y->opn;
    opn_prescaler_w(O, 0, 1);       // the fixed /72 FM prescaler
    opn_irq_mask_set(O, 0x03);
    opn_write_mode(O, 0x27, 0x30);
    O->eg_timer_ = 0.0;
    O->eg_cnt_ = 0;
    opn_status_reset(O, 0xff);
    O->mode_ = 0;
    opn_reset_timers(O);
    opn_reset_channels(O);
    for (int part = 0; part < 2; part++)
        for (int reg = 0xb6; reg >= 0x30; reg--) opn_write_reg(O, reg, 0, part * 3);
    for (int reg = 0x26; reg >= 0x20; reg--) opn_write_mode(O, reg, 0);
    y->address[0] = y->address[1] = 0;
    y->dac_data = 0x80;
    y->dac_enable = false;
    for (int ch = 0; ch < 6; ch++) {
        O->channels_[ch].pan_left = true;
        O->channels_[ch].pan_right = true;
    }
}

void ym2612_write(ym2612 *y, int port, uint8_t value) {
    int part = (port >> 1) & 1;
    if ((port & 1) == 0) {
        y->address[part] = value;
        y->opn.address_ = value;
        return;
    }
    uint8_t a = y->address[part];
    if (part == 0) {
        if (a == 0x2a) { y->dac_data = value; return; }
        if (a == 0x2b) { y->dac_enable = (value & 0x80) != 0; return; }
        if (a < 0x30) { opn_write_mode(&y->opn, a, value); return; }
        opn_write_reg(&y->opn, a, value, 0);
        return;
    }
    if (a >= 0x30) opn_write_reg(&y->opn, a, value, 3);
}

int32_t ym2612_update(ym2612 *y) {
    opn_core *O = &y->opn;
    for (int ch = 0; ch < 6; ch++) {
        if (ch == 2 && (O->mode_ & 0xc0)) {
            opn_channel *c2 = &O->channels_[2];
            if (c2->slot[OPN_SLOT1].incr == -1) {
                opn_refresh_fc_eg_slot(O, &c2->slot[OPN_SLOT1], (int)O->sl3_.fc[1], O->sl3_.kcode[1]);
                opn_refresh_fc_eg_slot(O, &c2->slot[OPN_SLOT2], (int)O->sl3_.fc[2], O->sl3_.kcode[2]);
                opn_refresh_fc_eg_slot(O, &c2->slot[OPN_SLOT3], (int)O->sl3_.fc[0], O->sl3_.kcode[0]);
                opn_refresh_fc_eg_slot(O, &c2->slot[OPN_SLOT4], (int)c2->fc, c2->kcode);
            }
        } else {
            opn_refresh_fc_eg_chan(O, &O->channels_[ch]);
        }
    }
    opn_clear_bus(O);
    opn_advance_envelopes(O);
    int32_t mix = 0;
    for (int ch = 0; ch < 6; ch++) {
        if (ch == 5 && y->dac_enable) {
            mix += ((int32_t)y->dac_data - 0x80) << 6;
            continue;
        }
        opn_chan_calc(O, &O->channels_[ch]);
        int32_t s = O->bus_[4 + ch];
        if (O->channels_[ch].pan_left || O->channels_[ch].pan_right) mix += s;
    }
    int32_t out = (int32_t)((float)mix * y->amplitude);
    if (out > 0x7fff) out = 0x7fff;
    if (out < -0x7fff) out = -0x7fff;
    opn_internal_timer_a(O, &O->channels_[2]);
    opn_internal_timer_b(O);
    return out;
}
