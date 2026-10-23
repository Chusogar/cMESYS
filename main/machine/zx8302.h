// zx8302.h - Sinclair ZX8302 "Tony" of the QL: real time clock, the serial
// link to the IPC (8049), the interrupt register and the microdrive
// controller. C port of dsp-cpp's machine/zx8302.
#ifndef ZX8302_H
#define ZX8302_H

#include <stdint.h>
#include <stdbool.h>

enum {
    ZX8302_INT_GAP = 0x01, ZX8302_INT_INTERFACE = 0x02, ZX8302_INT_TRANSMIT = 0x04,
    ZX8302_INT_FRAME = 0x08, ZX8302_INT_EXTERNAL = 0x10,
    ZX8302_ST_TXFULL = 0x02, ZX8302_ST_RXFULL = 0x04, ZX8302_ST_MDVGAP = 0x08,
};

typedef struct zx8302 {
    // outputs (user = the machine)
    void (*ipl1l)(void *user, int state);       // interrupt line to the CPU (IPL1L: level 2)
    void (*comdata)(void *user, int state);     // COMDATA to the IPC
    void (*mdseld)(void *user, int state);      // microdrive select data
    void (*mdselck)(void *user, int state);     // microdrive select clock
    void (*mdrdw)(void *user, int state);       // read / write
    void (*erase)(void *user, int state);
    void (*mdv_burst)(void *user);              // a pair was read: present the next one now
    void *user;

    uint8_t idr, tcr, irq, irq_mask;
    uint32_t ctr;                               // RTC: seconds since 1961
    uint8_t status;
    uint8_t mdv_data[2], mdv_shift[2];
    int mdv_bit_count, mdv_sync;
    uint8_t mdv_tx[2];
    int mdv_tx_count;
    uint8_t mdv_ctrl;
    int comdata_from_ipc, comdata_to_cpu, comdata_to_ipc;
    int ipc_state;
    bool ipc_busy, baudx4;
} zx8302;

// rtc_seconds: Unix time to start the clock from (0: 1 Jan 1961)
void zx8302_reset(zx8302 *z, uint32_t unix_time);
uint8_t zx8302_rtc_r(const zx8302 *z, uint32_t offset);
static inline void zx8302_control_w(zx8302 *z, uint8_t v) { z->tcr = v; }
uint8_t zx8302_status_r(const zx8302 *z);
void zx8302_ipc_command_w(zx8302 *z, uint8_t v);
void zx8302_irq_acknowledge_w(zx8302 *z, uint8_t v);
void zx8302_mdv_control_w(zx8302 *z, uint8_t v);
void zx8302_data_w(zx8302 *z, uint8_t v);
uint8_t zx8302_mdv_track_r(zx8302 *z, uint32_t offset);
uint16_t zx8302_mdv_tx_pop(zx8302 *z);
void zx8302_vsync(zx8302 *z);
void zx8302_comctl(zx8302 *z);                  // COMCTL pulse from the IPC
static inline void zx8302_comdata_w(zx8302 *z, int s) { z->comdata_from_ipc = s; }
void zx8302_mdv_raw1_w(zx8302 *z, int s);
void zx8302_mdv_raw2_w(zx8302 *z, int s);
void zx8302_mdv_gap_w(zx8302 *z, int s);
static inline void zx8302_tick_rtc(zx8302 *z) { z->ctr++; }
static inline void zx8302_tick_baudx4(zx8302 *z) { z->baudx4 = !z->baudx4; }
static inline bool zx8302_mdv_rx_full(const zx8302 *z) { return z->status & ZX8302_ST_RXFULL; }
static inline bool zx8302_mdv_delivering(const zx8302 *z) { return z->mdv_sync == 2; }

#endif
