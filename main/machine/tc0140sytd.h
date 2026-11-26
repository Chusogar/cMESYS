// tc0140sytd.h - Taito TC0140SYT main <-> sound mailbox, exactly as dsp-cpp's
// machine/tc0140syt (taito_sound.pas): the NMI is a pulse when the master
// fills a port pair, and a write of non-zero in mode 4 resets the sound CPU.
// (machine/tc0140syt.c keeps MAME's level-NMI variant.)
#ifndef TC0140SYTD_H
#define TC0140SYTD_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t slave_data[4], master_data[4], main_mode, sub_mode, status;
    bool nmi_enabled, nmi_req;
    void (*nmi_cb)(void *user);
    void (*reset_cb)(void *user);
    void *user;
} tc0140sytd;

void tc0140sytd_reset(tc0140sytd *t);
void tc0140sytd_port_w(tc0140sytd *t, uint8_t value);
void tc0140sytd_comm_w(tc0140sytd *t, uint8_t value);
uint8_t tc0140sytd_comm_r(tc0140sytd *t);
void tc0140sytd_slave_port_w(tc0140sytd *t, uint8_t value);
void tc0140sytd_slave_comm_w(tc0140sytd *t, uint8_t value);
uint8_t tc0140sytd_slave_comm_r(tc0140sytd *t);

#endif
