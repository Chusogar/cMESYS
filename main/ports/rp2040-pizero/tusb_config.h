#pragma once

// TinyUSB configuration of the RP2040-PiZero port: USB host on the native
// USB-C port (marked USB) for keyboards and HID gamepads, through a hub too.
// The board's PIO-USB port cannot be used with the HDMI output on the RP2040
// (not enough PIO state machines and DMA bandwidth), so the native port is
// the host: power the board and the pad through an OTG/Y cable on it. To
// flash again, hold BOOT while plugging it in.

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif
#define CFG_TUSB_DEBUG 0

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 0
#define BOARD_TUH_RHPORT 0
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_HOST
#define CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)
#define CFG_TUH_HID 4
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))
