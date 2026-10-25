// tc0140syt.h - Taito TC0140SYT main <-> sound CPU mailbox.
// C port of dsp-cpp machine/tc0140syt.cpp, with the NMI as a level that
// follows the "port full" flags (as MAME's taitosnd.cpp).
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct tc0140syt {
    uint8_t slave_data[4];   // main -> sound nibbles
    uint8_t master_data[4];  // sound -> main nibbles
    uint8_t main_mode, sub_mode, status;
    bool nmi_enabled;
    bool nmi_line;           // current NMI output level
    // Called when the NMI level changes (sound CPU NMI is edge triggered).
    void (*nmi_cb)(void *user, bool state);
    // Called with the sound CPU reset line (true = held in reset).
    void (*reset_cb)(void *user, bool state);
    void *user;
} tc0140syt_t;

void tc0140syt_reset(tc0140syt_t *t);
// Main CPU side
void tc0140syt_master_port_w(tc0140syt_t *t, uint8_t v);
void tc0140syt_master_comm_w(tc0140syt_t *t, uint8_t v);
uint8_t tc0140syt_master_comm_r(tc0140syt_t *t);
// Sound CPU side
void tc0140syt_slave_port_w(tc0140syt_t *t, uint8_t v);
void tc0140syt_slave_comm_w(tc0140syt_t *t, uint8_t v);
uint8_t tc0140syt_slave_comm_r(tc0140syt_t *t);
