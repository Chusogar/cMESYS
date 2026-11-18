// mac_rtc.h - the Macintosh Plus's 343-0042 1-bit clock and parameter RAM,
// on VIA PB0-PB2 (a C11 migration of dsp-cpp's machine/mac_rtc).
#ifndef MAC_RTC_H
#define MAC_RTC_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t mem[32];
    uint32_t seconds;
    bool ce, clk, data_in, data_out;
    int bit, state;
    uint8_t cmd, data;
} mac_rtc;

void mac_rtc_reset(mac_rtc *r);
void mac_rtc_ce(mac_rtc *r, bool level);
void mac_rtc_clk(mac_rtc *r, bool level);
static inline void mac_rtc_data_w(mac_rtc *r, bool level) { r->data_in = level; }
static inline bool mac_rtc_data_r(const mac_rtc *r) { return r->ce ? r->data_out : true; }
void mac_rtc_tick_second(mac_rtc *r);
#endif
