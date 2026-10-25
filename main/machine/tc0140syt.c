// tc0140syt.c - Taito TC0140SYT main <-> sound CPU mailbox.
// C port of dsp-cpp machine/tc0140syt.cpp (itself from taito_sound.pas).
// Four nibble ports in each direction plus a status register. The sound CPU
// NMI is asserted while a main -> sound port pair is full and NMIs are enabled.
#include "tc0140syt.h"
#include <string.h>

#define PORT01_FULL         0x01
#define PORT23_FULL         0x02
#define PORT01_FULL_MASTER  0x04
#define PORT23_FULL_MASTER  0x08

static void update_nmi(tc0140syt_t *t)
{
    bool st = (t->status & (PORT01_FULL | PORT23_FULL)) && t->nmi_enabled;
    if (st != t->nmi_line) {
        t->nmi_line = st;
        if (t->nmi_cb) t->nmi_cb(t->user, st);
    }
}

void tc0140syt_reset(tc0140syt_t *t)
{
    memset(t->slave_data, 0, sizeof t->slave_data);
    memset(t->master_data, 0, sizeof t->master_data);
    t->main_mode = t->sub_mode = t->status = 0;
    t->nmi_enabled = false;
    t->nmi_line = false;
}

void tc0140syt_master_port_w(tc0140syt_t *t, uint8_t v) { t->main_mode = v & 0x0f; }

void tc0140syt_master_comm_w(tc0140syt_t *t, uint8_t v)
{
    v &= 0x0f;
    switch (t->main_mode) {
    case 0: case 2:
        t->slave_data[t->main_mode++] = v;
        break;
    case 1:
        t->slave_data[t->main_mode++] = v;
        t->status |= PORT01_FULL;
        update_nmi(t);
        break;
    case 3:
        t->slave_data[t->main_mode++] = v;
        t->status |= PORT23_FULL;
        update_nmi(t);
        break;
    case 4:     // high-to-low on this port resets the sound CPU
        if (t->reset_cb) t->reset_cb(t->user, v != 0);
        break;
    default: break;
    }
}

uint8_t tc0140syt_master_comm_r(tc0140syt_t *t)
{
    uint8_t r = 0;
    switch (t->main_mode) {
    case 0: case 2:
        r = t->master_data[t->main_mode++];
        break;
    case 1:
        r = t->master_data[t->main_mode++];
        t->status &= (uint8_t)~PORT01_FULL_MASTER;
        break;
    case 3:
        r = t->master_data[t->main_mode++];
        t->status &= (uint8_t)~PORT23_FULL_MASTER;
        break;
    case 4:
        r = t->status;
        break;
    default: break;
    }
    return r;
}

void tc0140syt_slave_port_w(tc0140syt_t *t, uint8_t v) { t->sub_mode = v & 0x0f; }

void tc0140syt_slave_comm_w(tc0140syt_t *t, uint8_t v)
{
    v &= 0x0f;
    switch (t->sub_mode) {
    case 0: case 2:
        t->master_data[t->sub_mode++] = v;
        break;
    case 1:
        t->master_data[t->sub_mode++] = v;
        t->status |= PORT01_FULL_MASTER;
        break;
    case 3:
        t->master_data[t->sub_mode++] = v;
        t->status |= PORT23_FULL_MASTER;
        break;
    case 5: t->nmi_enabled = false; update_nmi(t); break;
    case 6: t->nmi_enabled = true; update_nmi(t); break;
    default: break;
    }
}

uint8_t tc0140syt_slave_comm_r(tc0140syt_t *t)
{
    uint8_t r = 0;
    switch (t->sub_mode) {
    case 0: case 2:
        r = t->slave_data[t->sub_mode++];
        break;
    case 1:
        r = t->slave_data[t->sub_mode++];
        t->status &= (uint8_t)~PORT01_FULL;
        update_nmi(t);
        break;
    case 3:
        r = t->slave_data[t->sub_mode++];
        t->status &= (uint8_t)~PORT23_FULL;
        update_nmi(t);
        break;
    case 4:
        r = t->status;
        break;
    default: break;
    }
    return r;
}
