// iwm.c - Apple IWM with two Sony drives: C11 migration of dsp-cpp's
// machine/iwm.cpp (see iwm.h).
#include "iwm.h"

#include <string.h>

static int rpm_for_track(int track)
{
    if (track <= 15) return 394;
    if (track <= 31) return 429;
    if (track <= 47) return 472;
    if (track <= 63) return 525;
    return 590;
}

static void select_track(iwm *w)
{
    w->tach_period = (int64_t)7833600 * 60 / ((int64_t)rpm_for_track(w->track) * 120);
    if (w->tach_period < 1) w->tach_period = 1;
    w->nibble_pos = 0;
}

void iwm_init(iwm *w)
{
    memset(w, 0, sizeof *w);
    mac_dsk_init(&w->disk);
    iwm_reset(w);
}

void iwm_reset(iwm *w)
{
    w->phases = w->control = w->mode = w->status = w->data = 0;
    w->handshake = 0xbf;
    w->last_strobe = false;
    w->dir_out = true;
    w->drive_motor = w->stepping = w->mfm_mode = false;
    w->drive = w->track = w->nibble_pos = 0;
    w->cycles = 0;
    select_track(w);
}

void iwm_disk_changed(iwm *w) { select_track(w); }

void iwm_set_hdsel(iwm *w, bool side)
{
    if (w->hdsel == side) return;
    w->hdsel = side;
    w->nibble_pos = 0;
}

void iwm_tick(iwm *w, int cycles)
{
    w->cycles += cycles;
    w->stepping = false;
}

static void update_devsel(iwm *w)
{
    // MAME iwm_device::control: while active, bit 5 picks drive 2, else 1
    const int drive = (w->control & 0x10) ? ((w->control & 0x20) ? 2 : 1) : 0;
    if (drive == w->drive) return;
    w->drive = drive;
    w->nibble_pos = 0;
}

static uint8_t next_nibble(iwm *w)
{
    if (w->drive != 1 || !w->drive_motor || !w->disk.loaded) return 0;
    int n;
    const uint8_t *nib = mac_dsk_nibbles(&w->disk, w->track, w->hdsel ? 1 : 0, &n);
    if (!nib) return 0;
    if (w->nibble_pos < 0 || w->nibble_pos >= n) w->nibble_pos = 0;
    const uint8_t v = nib[w->nibble_pos++];
    if (w->nibble_pos >= n) w->nibble_pos = 0;
    return v;
}

static int sense(const iwm *w)
{
    // MAME: status bit 7 is (!floppy || floppy->wpt_r()); no drive: pulled up
    if (w->drive == 0) return 1;
    const int reg = (w->phases & 7) | (w->hdsel ? 8 : 0);
    const bool internal = w->drive == 1, has_disk = internal && w->disk.loaded, motor = internal && w->drive_motor;
    switch (reg) {
    case 0x0: return w->dir_out ? 0 : 1;                    // direction
    case 0x1: return (internal && w->stepping) ? 0 : 1;     // step
    case 0x2: return motor ? 0 : 1;                         // motor (0: running)
    case 0x3: return 1;                                     // eject / disk changed
    case 0x4: case 0xc: return 0;
    case 0x5: return (internal && w->disk.hd) ? 1 : 0;      // a SuperDrive with HD media
    case 0x6: return 1;                                     // double sided
    case 0x7: return 0;                                     // drive present
    case 0x8: return has_disk ? 0 : 1;                      // no disk in place
    case 0x9: return 1;                                     // not write protected
    case 0xa: return (internal && w->track != 0) ? 1 : 0;   // not on track 0
    case 0xb: {                                             // tachometer: 120 a turn
        if (!has_disk || !motor) return 0;
        const int64_t phase = w->tach_period <= 0 ? 0 : w->cycles / w->tach_period;
        return (int)(phase & 1);
    }
    case 0xd: return (internal && w->disk.hd && w->mfm_mode) ? 1 : 0;   // MFM mode on
    case 0xe: return (has_disk && motor) ? 0 : 1;           // not ready
    case 0xf: return 1;                                     // new interface
    default: return 0;
    }
}

static void strobe_command(iwm *w)
{
    // MAME mac_floppy_device::seek_phase_w on LSTRB's rising edge (the
    // internal drive: the only one with a disk)
    if (w->drive != 1) return;
    const int reg = (w->phases & 7) | (w->hdsel ? 8 : 0);
    switch (reg) {
    case 0x0: w->dir_out = true; break;
    case 0x1:
        if (w->dir_out) {
            if (w->track < 79) w->track++;
        } else if (w->track > 0) {
            w->track--;
        }
        w->stepping = true;
        select_track(w);
        break;
    case 0x2: w->drive_motor = true; break;
    case 0x4: w->dir_out = false; break;
    case 0x6: w->drive_motor = false; break;
    case 0x7: break;        // eject: the Plus ROM strobes it at boot without a disk
    case 0x9: if (w->disk.hd) w->mfm_mode = true; break;
    case 0xd: w->mfm_mode = false; break;
    default: break;
    }
}

static uint8_t access(iwm *w, uint8_t offset, uint8_t data, bool is_write)
{
    offset &= 0x0f;
    if (offset < 8) {
        if (offset & 1) w->phases |= (uint8_t)(1 << (offset >> 1));
        else w->phases &= (uint8_t)~(1 << (offset >> 1));
        const bool strobe = (w->phases & 0x08) != 0;
        if (strobe && !w->last_strobe) strobe_command(w);
        w->last_strobe = strobe;
    } else {
        if (offset & 1) w->control |= (uint8_t)(1 << (offset >> 1));
        else w->control &= (uint8_t)~(1 << (offset >> 1));
        update_devsel(w);
    }
    const bool enable = (w->control & 0x10) != 0;
    if (enable) w->status = (uint8_t)((w->status & 0xdf) | 0x20);
    else w->status &= 0xdf;
    if (is_write && (offset & 1) && (w->control & 0xc0) == 0xc0) {
        if (enable) {
            w->data = data;
            w->handshake = 0xc0;
        } else {
            w->mode = data;
            w->status = (uint8_t)((w->status & 0xe0) | (w->mode & 0x1f));
        }
    }
    switch (w->control & 0xc0) {
    case 0x00:
        if (enable) {
            w->data = next_nibble(w);
            w->handshake = 0xbf;
            return w->data;
        }
        return 0xff;
    case 0x40: return (uint8_t)((w->status & 0x7f) | (sense(w) ? 0x80 : 0x00));
    case 0x80: return w->handshake;
    default: return 0xff;
    }
}

uint8_t iwm_read(iwm *w, uint8_t offset) { return access(w, offset, 0, false); }
void iwm_write(iwm *w, uint8_t offset, uint8_t data) { access(w, offset, data, true); }
