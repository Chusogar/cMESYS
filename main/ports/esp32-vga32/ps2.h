// ps2.h - the TTGO VGA32's PS/2 keyboard and mouse (ps2.c) as the CYD
// frontends' inputs.
//
// Keyboard: the cursor keys, Ctrl and Alt are the Nunchuk (stick, Z, C); F1
// is the BOOT button; letters, digits, symbols, Enter, Backspace, Tab and
// F2-F12 (as a terminal's ESC [ n ~) are typed on the serial port the
// frontends read (uart_read_bytes on UART0 is wrapped). Mouse: the pointer
// on the screen; the left button held is a touch there.
#pragma once
#include <stdbool.h>

void ps2_start(void);
bool ps2_mouse_touch(int *x, int *y);   // framebuffer coordinates
bool ps2_boot_held(void);
int ps2_menu_char(void);                // a typed character, or -1
void ps2_flush_keys(void);
void vga32_pointer(bool show, int x, int y);     // (vga32.c)
