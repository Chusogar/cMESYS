// iwm.h - the Apple IWM floppy controller of the Macintoshes (MAME's
// iwm_device), with two 800K GCR drives (the internal one with the disk,
// an empty external one). A C11 migration of dsp-cpp's machine/iwm: control
// bit 4 is DRIVE ENABLE, bit 5 SELECT (1 internal, 2 external); with a
// 1.44 MB image the internal drive senses as a SuperDrive (MFD75W).
#ifndef IWM_H
#define IWM_H
#include <stdbool.h>
#include <stdint.h>
#include "mac_dsk.h"

typedef struct {
    mac_dsk disk;
    uint8_t phases, control, mode, status, data, handshake;
    bool hdsel, last_strobe, dir_out, drive_motor, stepping, mfm_mode;
    int drive, track, nibble_pos;
    int64_t cycles, tach_period;
} iwm;

void iwm_init(iwm *w);
void iwm_reset(iwm *w);
void iwm_tick(iwm *w, int cycles);
void iwm_set_hdsel(iwm *w, bool side);
uint8_t iwm_read(iwm *w, uint8_t offset);
void iwm_write(iwm *w, uint8_t offset, uint8_t data);
void iwm_disk_changed(iwm *w);          // after loading into w->disk
static inline bool iwm_motor_on(const iwm *w) { return w->drive == 1 && w->drive_motor; }
#endif
