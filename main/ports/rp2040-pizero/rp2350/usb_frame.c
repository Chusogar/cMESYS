// usb_frame.c - the 1 ms frame of the RP2350-PiZero's PIO-USB host, with a
// workaround for erratum RP2350-E9.
//
// RP2350-E9 (chips up to A2): a pad with its input buffer enabled leaks, and
// a line that has been high stays at about 2 V: the pull-down cannot bring it
// back. A USB host relies on its pull-downs to see whether something is
// plugged in (SE0: nothing; J on D+ or D-: a device's pull-up), so with the
// inputs on all the time D+ and D- both read high (SE1), never an idle state,
// and a keyboard is never seen. Pico-PIO-USB turns the inputs off for eight
// NOPs before it reads the lines: far too short to drain them (32 ns at
// 252 MHz). The same problem, and this way round it, are described in Frank
// Hoedemakers' emulators for RP2350 boards.
//
// So Pico-PIO-USB is set up without its own timer (skip_alarm_pool) and the
// frame runs from here, on the same hardware alarm, every millisecond:
// - while nothing is plugged in, the inputs stay off between frames (the
//   lines drain through the pull-downs) and are switched on just to look at
//   the lines, a microsecond after;
// - while a device is plugged in, the lines are drained for a few
//   microseconds every 16 frames, so that pulling it out reads as SE0.
#include "tusb.h"

#if CFG_TUH_RPI_PIO_USB && PICO_RP2350
#include "hardware/structs/pads_bank0.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"

#define FRAME_ALARM 2                   // (the one Pico-PIO-USB takes itself)
#define DRAIN_EVERY 16u                 // frames
#define DRAIN_US 5u

static repeating_timer_t frame_timer;
static bool e9;
static uint32_t frames;

static void __not_in_flash_func(spin_us)(uint32_t us)
{
    const uint32_t t0 = timer_hw->timerawl;
    while (timer_hw->timerawl - t0 <= us) {
    }
}

static void __not_in_flash_func(inputs)(const root_port_t *root, bool on)
{
    io_rw_32 *dp = &pads_bank0_hw->io[root->pin_dp], *dm = &pads_bank0_hw->io[root->pin_dm];
    if (on) {
        hw_set_bits(dp, PADS_BANK0_GPIO0_IE_BITS);
        hw_set_bits(dm, PADS_BANK0_GPIO0_IE_BITS);
    } else {
        hw_clear_bits(dp, PADS_BANK0_GPIO0_IE_BITS);
        hw_clear_bits(dm, PADS_BANK0_GPIO0_IE_BITS);
    }
    (void)*dp;                          // (the writes have reached the pads)
    (void)*dm;
}

static bool __not_in_flash_func(frame)(repeating_timer_t *rt)
{
    (void)rt;
    root_port_t *root = PIO_USB_ROOT_PORT(0);
    if (!e9 || !root->initialized) {
        pio_usb_host_frame();
        return true;
    }
    // plugged in: drain the lines now and then
    if (root->connected && !root->suspended && ++frames % DRAIN_EVERY == 0) {
        inputs(root, false);
        spin_us(DRAIN_US);
        inputs(root, true);
    }
    pio_usb_host_frame();
    if (!root->connected) {
        // nothing there (yet): look at the drained lines with the inputs
        // settled. A J state (D+ or D- pulled up) is a device, as
        // pio_usb_host_frame() would see it (its inputs are inverted)
        inputs(root, true);
        spin_us(1);
        const bool dp = !gpio_get(root->pin_dp), dm = !gpio_get(root->pin_dm);
        if (dp != dm) {
            root->is_fullspeed = dp;
            root->connected = true;
            root->suspended = true;     // a bus reset comes first
            root->ints |= PIO_USB_INTS_CONNECT_BITS;
        } else {
            inputs(root, false);        // drain until the next frame
        }
    }
    return true;
}

void pico_usb_frame_start(void)
{
    const uint32_t id = sysinfo_hw->chip_id;
    const uint32_t rev = (id & SYSINFO_CHIP_ID_REVISION_BITS) >> SYSINFO_CHIP_ID_REVISION_LSB;
    e9 = rev <= 2;
    alarm_pool_add_repeating_timer_us(alarm_pool_create(FRAME_ALARM, 1), -1000, frame, NULL, &frame_timer);
    printf("PIO-USB: chip revision %u, RP2350-E9 workaround %s\n", (unsigned)rev, e9 ? "on" : "off");
}
#endif
