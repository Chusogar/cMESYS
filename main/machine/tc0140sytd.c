// tc0140sytd.c - see tc0140sytd.h.
#include <string.h>
#include "tc0140sytd.h"

void tc0140sytd_reset(tc0140sytd *t) {
    memset(t->slave_data, 0, 4);
    memset(t->master_data, 0, 4);
    t->main_mode = 0;
    t->sub_mode = 0;
    t->status = 0;
    t->nmi_enabled = false;
    t->nmi_req = false;
}

static void interrupt_controller(tc0140sytd *t) {
    if (t->nmi_req && t->nmi_enabled) {
        t->nmi_req = false;
        if (t->nmi_cb) t->nmi_cb(t->user);
    }
}

void tc0140sytd_port_w(tc0140sytd *t, uint8_t value) { t->main_mode = (uint8_t)(value & 0x0f); }

void tc0140sytd_comm_w(tc0140sytd *t, uint8_t value) {
    value = (uint8_t)(value & 0x0f);
    switch (t->main_mode) {
        case 0:
            t->slave_data[0] = value;
            t->main_mode = 1;
            break;
        case 1:
            t->slave_data[1] = value;
            t->main_mode = 2;
            t->status = (uint8_t)(t->status | 0x01);
            t->nmi_req = true;
            interrupt_controller(t);
            break;
        case 2:
            t->slave_data[2] = value;
            t->main_mode = 3;
            break;
        case 3:
            t->slave_data[3] = value;
            t->main_mode = 4;
            t->status = (uint8_t)(t->status | 0x02);
            t->nmi_req = true;
            interrupt_controller(t);
            break;
        case 4:
            // A high-to-low on this port resets the sound CPU.
            if (value != 0) {
                tc0140sytd_reset(t);
                if (t->reset_cb) t->reset_cb(t->user);
            }
            break;
        default: break;
    }
}

uint8_t tc0140sytd_comm_r(tc0140sytd *t) {
    switch (t->main_mode) {
        case 0:
            t->main_mode = 1;
            return t->master_data[0];
        case 1:
            t->status = (uint8_t)(t->status & ~0x04);
            t->main_mode = 2;
            return t->master_data[1];
        case 2:
            t->main_mode = 3;
            return t->master_data[2];
        case 3:
            t->status = (uint8_t)(t->status & ~0x08);
            t->main_mode = 4;
            return t->master_data[3];
        case 4: return t->status;
        default: return 0;
    }
}

void tc0140sytd_slave_port_w(tc0140sytd *t, uint8_t value) { t->sub_mode = (uint8_t)(value & 0x0f); }

void tc0140sytd_slave_comm_w(tc0140sytd *t, uint8_t value) {
    value = (uint8_t)(value & 0x0f);
    switch (t->sub_mode) {
        case 0:
            t->master_data[0] = value;
            t->sub_mode = 1;
            break;
        case 1:
            t->master_data[1] = value;
            t->sub_mode = 2;
            t->status = (uint8_t)(t->status | 0x04);
            break;
        case 2:
            t->master_data[2] = value;
            t->sub_mode = 3;
            break;
        case 3:
            t->master_data[3] = value;
            t->sub_mode = 4;
            t->status = (uint8_t)(t->status | 0x08);
            break;
        case 4: break;
        case 5: t->nmi_enabled = false; break;
        case 6: t->nmi_enabled = true; break;
        default: break;
    }
    interrupt_controller(t);
}

uint8_t tc0140sytd_slave_comm_r(tc0140sytd *t) {
    uint8_t result = 0;
    switch (t->sub_mode) {
        case 0:
            result = t->slave_data[0];
            t->sub_mode = 1;
            break;
        case 1:
            t->status = (uint8_t)(t->status & ~0x01);
            result = t->slave_data[1];
            t->sub_mode = 2;
            break;
        case 2:
            result = t->slave_data[2];
            t->sub_mode = 3;
            break;
        case 3:
            t->status = (uint8_t)(t->status & ~0x02);
            result = t->slave_data[3];
            t->sub_mode = 4;
            break;
        case 4: result = t->status; break;
        default: break;
    }
    interrupt_controller(t);
    return result;
}

