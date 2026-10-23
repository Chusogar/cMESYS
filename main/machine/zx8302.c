// zx8302.c - Sinclair ZX8302. See zx8302.h.
#include "zx8302.h"

#define RTC_UNIX_ADJUST 283996800u      // 1961-01-01 -> 1970-01-01
enum { IPC_START, IPC_DATA, IPC_STOP };
enum { MODE_MDV = 0x10, MODE_MASK = 0x18 };
enum { MDV_IDLE, MDV_SEARCH, MDV_DELIVER };

void zx8302_reset(zx8302 *z, uint32_t unix_time)
{
    z->idr = 1;
    z->tcr = 0;
    z->irq = z->irq_mask = 0;
    z->ctr = unix_time + RTC_UNIX_ADJUST;
    z->status = ZX8302_ST_MDVGAP;
    z->mdv_data[0] = z->mdv_data[1] = 0;
    z->mdv_shift[0] = z->mdv_shift[1] = 0;
    z->mdv_bit_count = 0;
    z->mdv_sync = MDV_IDLE;
    z->mdv_tx[0] = z->mdv_tx[1] = 0;
    z->mdv_tx_count = 0;
    z->mdv_ctrl = 0;
    z->comdata_from_ipc = z->comdata_to_cpu = z->comdata_to_ipc = 1;
    z->ipc_state = IPC_START;
    z->ipc_busy = false;
    z->baudx4 = false;
}

static void trigger(zx8302 *z, uint8_t line)
{
    z->irq |= line;
    if (z->ipl1l) z->ipl1l(z->user, 1);
}

static void transmit_ipc(zx8302 *z)
{
    switch (z->ipc_state) {
    case IPC_START:
        if (z->comdata) z->comdata(z->user, z->idr & 1);
        z->ipc_busy = true;
        z->ipc_state = IPC_DATA;
        break;
    case IPC_DATA:
        z->comdata_to_ipc = (z->idr >> 1) & 1;
        if (z->comdata) z->comdata(z->user, z->comdata_to_ipc);
        z->ipc_state = IPC_STOP;
        break;
    default:
        if (z->comdata) z->comdata(z->user, (z->idr >> 2) & 1);
        z->ipc_busy = false;
        break;
    }
}

uint8_t zx8302_rtc_r(const zx8302 *z, uint32_t offset)
{
    return (uint8_t)(z->ctr >> (24 - (offset & 3) * 8));
}

uint8_t zx8302_status_r(const zx8302 *z)
{
    uint8_t d = z->status;
    if (z->ipc_busy) d |= 0x40;
    if (z->comdata_to_cpu) d |= 0x80;
    return d;
}

void zx8302_ipc_command_w(zx8302 *z, uint8_t v)
{
    z->idr = v;
    z->ipc_state = IPC_START;
    transmit_ipc(z);
}

void zx8302_irq_acknowledge_w(zx8302 *z, uint8_t v)
{
    z->irq_mask = v & 0xe0;
    z->irq &= (uint8_t)~(v & 0x1f);
    if (!z->irq && z->ipl1l) z->ipl1l(z->user, 0);
    if ((z->irq_mask & 0x20) && (z->status & ZX8302_ST_MDVGAP)) trigger(z, ZX8302_INT_GAP);
}

void zx8302_mdv_control_w(zx8302 *z, uint8_t v)
{
    if (z->mdseld) z->mdseld(z->user, v & 1);
    if (z->mdselck) z->mdselck(z->user, (v >> 1) & 1);
    if (z->mdrdw) z->mdrdw(z->user, (v >> 2) & 1);
    if (z->erase) z->erase(z->user, (v >> 3) & 1);
    // JS writes $02 after every gap (and again between a block header and
    // its data): arm on the level, clear RX and hunt for the FF/FF preamble.
    if (v & 0x02) {
        z->status &= (uint8_t)~ZX8302_ST_RXFULL;
        z->mdv_sync = MDV_SEARCH;
        z->mdv_bit_count = 0;
        z->mdv_shift[0] = z->mdv_shift[1] = 0;
    }
    z->mdv_ctrl = v;
    if (!(v & 0x04)) {
        z->mdv_tx_count = 0;
        z->status &= (uint8_t)~ZX8302_ST_TXFULL;
    }
}

void zx8302_data_w(zx8302 *z, uint8_t v)
{
    if ((z->tcr & MODE_MASK) == MODE_MDV) {
        if (z->mdv_tx_count < 2) z->mdv_tx[z->mdv_tx_count++] = v;
        if (z->mdv_tx_count == 2) z->status |= ZX8302_ST_TXFULL;
        return;
    }
    z->status |= ZX8302_ST_TXFULL;
}

uint16_t zx8302_mdv_tx_pop(zx8302 *z)
{
    uint16_t pair = 0;
    if (z->mdv_tx_count == 2) {
        pair = (uint16_t)((z->mdv_tx[0] << 8) | z->mdv_tx[1]);
        z->mdv_tx_count = 0;
        z->status &= (uint8_t)~ZX8302_ST_TXFULL;
    }
    return pair;
}

void zx8302_mdv_raw1_w(zx8302 *z, int s)
{
    z->mdv_shift[0] = (uint8_t)((z->mdv_shift[0] << 1) | (s & 1));
}

void zx8302_mdv_raw2_w(zx8302 *z, int s)
{
    z->mdv_shift[1] = (uint8_t)((z->mdv_shift[1] << 1) | (s & 1));
    if (z->mdv_sync == MDV_IDLE) return;
    if (z->mdv_sync == MDV_SEARCH) {
        if (z->mdv_shift[0] == 0xff && z->mdv_shift[1] == 0xff) {
            z->mdv_sync = MDV_DELIVER;
            z->mdv_bit_count = 0;
        }
        return;
    }
    if (++z->mdv_bit_count < 8) return;
    z->mdv_bit_count = 0;
    z->mdv_data[0] = z->mdv_shift[0];
    z->mdv_data[1] = z->mdv_shift[1];
    z->status |= ZX8302_ST_RXFULL;
}

void zx8302_mdv_gap_w(zx8302 *z, int s)
{
    const bool was = z->status & ZX8302_ST_MDVGAP;
    if (s) {
        z->status |= ZX8302_ST_MDVGAP;
        // RX and DELIVER stay: the last pair of a record is read after it
        if (!was && (z->irq_mask & 0x20)) trigger(z, ZX8302_INT_GAP);
    } else {
        z->status &= (uint8_t)~ZX8302_ST_MDVGAP;
    }
}

uint8_t zx8302_mdv_track_r(zx8302 *z, uint32_t offset)
{
    const int track = offset & 1;
    const uint8_t d = z->mdv_data[track];
    if (track == 1) {
        z->status &= (uint8_t)~ZX8302_ST_RXFULL;
        // JS polls RX-full for only ~20 instructions between pairs: present
        // the next pair as soon as this one is consumed
        if (z->mdv_burst) z->mdv_burst(z->user);
    }
    return d;
}

void zx8302_vsync(zx8302 *z) { trigger(z, ZX8302_INT_FRAME); }

void zx8302_comctl(zx8302 *z)
{
    transmit_ipc(z);
    z->comdata_to_cpu = z->comdata_from_ipc;
}
