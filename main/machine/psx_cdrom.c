#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "psx_cdrom.h"
#ifndef FILL
#define FILL(arr, v) do { for (size_t i_ = 0; i_ < sizeof(arr) / sizeof((arr)[0]); i_++) (arr)[i_] = (v); } while (0)
#define LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#endif

static void enqueue_irq(psx_cdrom *P, uint8_t irq, int delay);
static uint8_t dec_to_bcd(psx_cdrom *P, uint8_t v);
static int bcd_to_dec(psx_cdrom *P, uint8_t v);
static void lba_to_msf(psx_cdrom *P, int lba, uint8_t* mm, uint8_t* ss, uint8_t* ff);
static const psx_cd_track* track_from_loc(psx_cdrom *P, int loc);
static int total_lba(psx_cdrom *P);
static bool is_audio_cd(psx_cdrom *P);
static bool read_sector(psx_cdrom *P, int loc, uint8_t out[kBytesPerSector]);
static uint8_t status_reg(psx_cdrom *P);
static void execute_command(psx_cdrom *P, uint32_t value);
static void cmd_getstat(psx_cdrom *P);
static void cmd_setloc(psx_cdrom *P);
static void cmd_play(psx_cdrom *P);
static void cmd_readn(psx_cdrom *P);
static void cmd_motor_on(psx_cdrom *P);
static void cmd_stop(psx_cdrom *P);
static void cmd_pause(psx_cdrom *P);
static void cmd_init(psx_cdrom *P);
static void cmd_mute(psx_cdrom *P);
static void cmd_demute(psx_cdrom *P);
static void cmd_setfilter(psx_cdrom *P);
static void cmd_setmode(psx_cdrom *P);
static void cmd_getlocl(psx_cdrom *P);
static void cmd_getlocp(psx_cdrom *P);
static void cmd_setsession(psx_cdrom *P);
static void cmd_gettn(psx_cdrom *P);
static void cmd_gettd(psx_cdrom *P);
static void cmd_seekl(psx_cdrom *P);
static void cmd_seekp(psx_cdrom *P);
static void cmd_test(psx_cdrom *P);
static void cmd_getid(psx_cdrom *P);
static void cmd_reads(psx_cdrom *P);
static void cmd_readtoc(psx_cdrom *P);
static void cmd_videocd(psx_cdrom *P);
// PlayStation CD-ROM controller + disc image loader.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX




static const int kCpuClock = 33868800;
void psx_cdrom_reset(psx_cdrom *P) {
    fifo8_clear(&P->param_);
    fifo8_clear(&P->response_);
    secbuf_clear(&P->current_sector_);
    secbuf_clear(&P->last_read_sector_);
    P->busy_ = false;
    P->ie_ = 0;
    P->if_ = 0;
    P->index_ = 0;
    P->stat_ = 0;
    P->seek_loc_ = 0;
    P->read_loc_ = 0;
    P->double_speed_ = false;
    P->xa_adpcm_ = false;
    P->sector_raw_ = false;
    P->ignore_bit_ = false;
    P->xa_filter_ = false;
    P->report_ = false;
    P->auto_pause_ = false;
    P->cdda_ = false;
    P->muted_ = false;
    P->lid_open_ = (P->ntracks_ == 0);
    P->mode_ = CD_MODE_IDLE;
    P->counter_ = 0;
    fifoirq_clear(&P->irq_queue_);
}
static void enqueue_irq(psx_cdrom *P, uint8_t irq, int delay) {
    fifoirq_push_back(&P->irq_queue_, (psx_delayed_irq){delay, irq});
}
static uint8_t dec_to_bcd(psx_cdrom *P, uint8_t v) { return (uint8_t)(v + 6 * (v / 10)); }
static int bcd_to_dec(psx_cdrom *P, uint8_t v) { return v - 6 * (v >> 4); }
static void lba_to_msf(psx_cdrom *P, int lba, uint8_t* mm, uint8_t* ss, uint8_t* ff) {
    *ff = (uint8_t)(lba % 75);
    lba /= 75;
    *ss = (uint8_t)(lba % 60);
    lba /= 60;
    *mm = (uint8_t)(lba);
}
static const psx_cd_track* track_from_loc(psx_cdrom *P, int loc) {
    for (int i = 0; i < P->ntracks_; i++) {
        if (P->tracks_[i].lba_end >= loc) return &P->tracks_[i];
    }
    return &P->tracks_[0];
}
static int total_lba(psx_cdrom *P) {
    int lba = 150;
    for (int i = 0; i < P->ntracks_; i++) lba += P->tracks_[i].lba;
    return lba;
}
static bool is_audio_cd(psx_cdrom *P) {
    return !(P->ntracks_ == 0) && P->tracks_[0].is_audio;
}
static bool read_sector(psx_cdrom *P, int loc, uint8_t out[kBytesPerSector]) {
    if ((P->ntracks_ == 0)) {
        memset(out, 0, kBytesPerSector);
        return false;
    }
    const psx_cd_track* track = track_from_loc(P, loc);
    P->track_change_ = (loc == track->lba_end);
    int position = loc - track->lba_start;
    if (position < 0) position = 0;
    const size_t idx = (size_t)(track->number - 1);
    const psx_cd_data* data = &P->track_data_[idx];
    const size_t off = (size_t)(position) * kBytesPerSector;
    if (off + kBytesPerSector > data->size) {
        memset(out, 0, kBytesPerSector);
        return false;
    }
    memcpy(out, data->data + off, kBytesPerSector);
    return true;
}
bool psx_cdrom_tick(psx_cdrom *P, int cycles) {
    P->counter_ += cycles;
    if (!fifoirq_empty(&P->irq_queue_)) {
        fifoirq_frontp(&P->irq_queue_)->delay -= cycles;
    }
    if (!fifoirq_empty(&P->irq_queue_) && P->if_ == 0 && fifoirq_frontp(&P->irq_queue_)->delay <= 0) {
        P->if_ |= fifoirq_frontp(&P->irq_queue_)->interrupt;
        fifoirq_pop_front(&P->irq_queue_);
    }
    if ((P->if_ & P->ie_) != 0) {
        P->busy_ = false;
        return true;
    }

    switch (P->mode_) {
        case CD_MODE_IDLE:
            P->counter_ = 0;
            return false;
        case CD_MODE_SEEK:
            if (P->counter_ < kCpuClock / 3 || !fifoirq_empty(&P->irq_queue_)) return false;
            P->counter_ = 0;
            P->mode_ = CD_MODE_IDLE;
            P->stat_ = (uint8_t)(P->stat_ & ~0x40);
            fifo8_push_back(&P->response_, P->stat_);
            enqueue_irq(P, 2, 50000);
            break;
        case CD_MODE_READ:
        case CD_MODE_PLAY: {
            const int period = kCpuClock / (P->double_speed_ ? 150 : 75);
            if (P->counter_ < period || !fifoirq_empty(&P->irq_queue_)) return false;
            P->counter_ = 0;
            uint8_t sector[kBytesPerSector];
            read_sector(P, P->read_loc_++, sector);
            if (P->mode_ == CD_MODE_PLAY) {
                // CDDA: audio path stubbed; optional report interrupt.
                if (P->auto_pause_ && P->track_change_) {
                    fifo8_push_back(&P->response_, P->stat_);
                    enqueue_irq(P, 4, 1);
                    P->stat_ = 0x2;
                    P->mode_ = CD_MODE_IDLE;
                }
                return false;
            }
            P->hdr_mm_ = sector[12];
            P->hdr_ss_ = sector[13];
            P->hdr_ff_ = sector[14];
            P->hdr_mode_ = sector[15];
            P->sub_file_ = sector[16];
            P->sub_channel_ = sector[17];
            P->sub_mode_ = sector[18];
            P->sub_coding_ = sector[19];
            // XA realtime audio → SPU CD input (do not deliver sector to CPU).
            const bool form2 = (P->sub_mode_ & 0x20) != 0;
            const bool is_audio = (P->sub_mode_ & 0x4) != 0;
            const bool realtime = (P->sub_mode_ & 0x40) != 0;
            if (P->xa_adpcm_ && form2 && realtime && is_audio) {
                if (P->xa_filter_ && (P->filter_file_ != P->sub_file_ || P->filter_channel_ != P->sub_channel_)) {
                    return false;
                }
                if (!P->muted_ && P->cd_audio_cb_) {
                    static int16_t pcm[XA_MAX_OUT];
                    const int npcm = decode_xa_adpcm_sector(sector, P->sub_coding_, pcm);
                    if (npcm) P->cd_audio_cb_(P->cd_audio_user_, pcm, npcm);
                }
                return false;
            }
            if (!P->sector_raw_) {
                secbuf_fill(&P->last_read_sector_, sector + 24, 0x800);
            } else {
                secbuf_fill(&P->last_read_sector_, sector + 12, kBytesPerSector - 12);
            }
            fifo8_push_back(&P->response_, P->stat_);
            enqueue_irq(P, 1, 50000);
            break;
        }
        case CD_MODE_TOC: {
            const int period = kCpuClock / (P->double_speed_ ? 150 : 75);
            if (P->counter_ < period || !fifoirq_empty(&P->irq_queue_)) return false;
            P->mode_ = CD_MODE_IDLE;
            fifo8_push_back(&P->response_, P->stat_);
            enqueue_irq(P, 2, 50000);
            P->counter_ = 0;
            break;
        }
    }
    return false;
}
static uint8_t status_reg(psx_cdrom *P) {
    int stat = 0;
    // ProjectPSX has a precedence bug (`isBusy ? 1 : 0 << 7`); use correct busy bit.
    if (P->busy_) stat |= 1 << 7;
    if (secbuf_has_data(&P->current_sector_)) stat |= 1 << 6;
    if (!fifo8_empty(&P->response_)) stat |= 1 << 5;
    if (fifo8_size(&P->param_) < 16) stat |= 1 << 4;
    if (fifo8_empty(&P->param_)) stat |= 1 << 3;
    if (P->xa_adpcm_) stat |= 1 << 2;
    stat |= P->index_;
    return (uint8_t)(stat);
}
uint32_t psx_cdrom_load(psx_cdrom *P, uint32_t addr) {
    switch (addr) {
        case 0x1F801800: return status_reg(P);
        case 0x1F801801:
            if (!fifo8_empty(&P->response_)) {
                const uint8_t b = fifo8_front(&P->response_);
                fifo8_pop_front(&P->response_);
                return b;
            }
            return 0xFF;
        case 0x1F801802:
            return secbuf_has_data(&P->current_sector_) ? secbuf_read_byte(&P->current_sector_) : 0;
        case 0x1F801803:
            if (P->index_ == 0 || P->index_ == 2) return (uint32_t)(0xE0 | P->ie_);
            if (P->index_ == 1 || P->index_ == 3) return (uint32_t)(0xE0 | P->if_);
            return 0;
        default: return 0;
    }
}
void psx_cdrom_write(psx_cdrom *P, uint32_t addr, uint32_t value) {
    switch (addr) {
        case 0x1F801800:
            P->index_ = (uint8_t)(value & 3);
            break;
        case 0x1F801801:
            if (P->index_ == 0) execute_command(P, value);
            break;
        case 0x1F801802:
            if (P->index_ == 0) fifo8_push_back(&P->param_, (uint8_t)(value));
            else if (P->index_ == 1) P->ie_ = (uint8_t)(value & 0x1F);
            break;
        case 0x1F801803:
            if (P->index_ == 0) {
                if ((value & 0x80) != 0) {
                    if (secbuf_has_data(&P->current_sector_)) return;
                    secbuf_fill(&P->current_sector_, secbuf_bytes(&P->last_read_sector_), secbuf_byte_size(&P->last_read_sector_));
                } else {
                    secbuf_clear(&P->current_sector_);
                }
            } else if (P->index_ == 1) {
                P->if_ &= (uint8_t)(~(value & 0x1F));
                if (!fifoirq_empty(&P->irq_queue_) && fifoirq_frontp(&P->irq_queue_)->delay <= 0) {
                    P->if_ |= fifoirq_frontp(&P->irq_queue_)->interrupt;
                    fifoirq_pop_front(&P->irq_queue_);
                }
                if ((value & 0x40) == 0x40) fifo8_clear(&P->param_);
            }
            break;
        default:
            break;
    }
}
void psx_cdrom_dma_read(psx_cdrom *P, uint32_t* dest, int words) {
    secbuf_read_words(&P->current_sector_, dest, words);
}
static void execute_command(psx_cdrom *P, uint32_t value) {
    fifoirq_clear(&P->irq_queue_);
    fifo8_clear(&P->response_);
    P->busy_ = true;
    switch (value) {
        case 0x01: cmd_getstat(P); break;
        case 0x02: cmd_setloc(P); break;
        case 0x03: cmd_play(P); break;
        case 0x06: cmd_readn(P); break;
        case 0x07: cmd_motor_on(P); break;
        case 0x08: cmd_stop(P); break;
        case 0x09: cmd_pause(P); break;
        case 0x0A: cmd_init(P); break;
        case 0x0B: cmd_mute(P); break;
        case 0x0C: cmd_demute(P); break;
        case 0x0D: cmd_setfilter(P); break;
        case 0x0E: cmd_setmode(P); break;
        case 0x10: cmd_getlocl(P); break;
        case 0x11: cmd_getlocp(P); break;
        case 0x12: cmd_setsession(P); break;
        case 0x13: cmd_gettn(P); break;
        case 0x14: cmd_gettd(P); break;
        case 0x15: cmd_seekl(P); break;
        case 0x16: cmd_seekp(P); break;
        case 0x19: cmd_test(P); break;
        case 0x1A: cmd_getid(P); break;
        case 0x1B: cmd_reads(P); break;
        case 0x1E: cmd_readtoc(P); break;
        case 0x1F: cmd_videocd(P); break;
        default:
            if (value >= 0x50 && value <= 0x57) enqueue_irq(P, 5, 50000);
            else P->busy_ = false;
            break;
    }
}
static void cmd_getstat(psx_cdrom *P) {
    if (!P->lid_open_) {
        P->stat_ = (uint8_t)(P->stat_ & ~0x18);
        P->stat_ |= 0x2;
    }
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_setloc(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    const uint8_t mm = fifo8_front(&P->param_); fifo8_pop_front(&P->param_);
    const uint8_t ss = fifo8_front(&P->param_); fifo8_pop_front(&P->param_);
    const uint8_t ff = fifo8_front(&P->param_); fifo8_pop_front(&P->param_);
    P->seek_loc_ = bcd_to_dec(P, ff) + bcd_to_dec(P, ss) * 75 + bcd_to_dec(P, mm) * 60 * 75;
    if (P->seek_loc_ < 0) P->seek_loc_ = 0;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_play(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    if (!fifo8_empty(&P->param_) && fifo8_front(&P->param_) != 0) {
        const int track = bcd_to_dec(P, fifo8_front(&P->param_));
        fifo8_pop_front(&P->param_);
        if (is_audio_cd(P)) P->read_loc_ = P->seek_loc_ = P->tracks_[track - 1].lba_start;
        else if (track < P->ntracks_) P->read_loc_ = P->seek_loc_ = P->tracks_[track].lba_start;
    } else {
        P->read_loc_ = P->seek_loc_;
    }
    P->stat_ = 0x82;
    P->mode_ = CD_MODE_PLAY;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_readn(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    P->read_loc_ = P->seek_loc_;
    P->stat_ = 0x22;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    P->mode_ = CD_MODE_READ;
}
static void cmd_motor_on(psx_cdrom *P) {
    P->stat_ = 0x2;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 2, 50000);
}
static void cmd_stop(psx_cdrom *P) {
    P->stat_ = 0x2;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    P->stat_ = 0;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 2, 50000);
    P->mode_ = CD_MODE_IDLE;
}
static void cmd_pause(psx_cdrom *P) {
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    P->stat_ = 0x2;
    P->mode_ = CD_MODE_IDLE;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 2, 50000);
}
static void cmd_init(psx_cdrom *P) {
    P->stat_ = 0x2;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 2, 50000);
}
static void cmd_mute(psx_cdrom *P) {
    P->muted_ = true;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_demute(psx_cdrom *P) {
    P->muted_ = false;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_setfilter(psx_cdrom *P) {
    P->filter_file_ = fifo8_front(&P->param_); fifo8_pop_front(&P->param_);
    P->filter_channel_ = fifo8_front(&P->param_); fifo8_pop_front(&P->param_);
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_setmode(psx_cdrom *P) {
    const uint8_t mode = fifo8_front(&P->param_); fifo8_pop_front(&P->param_);
    P->double_speed_ = ((mode >> 7) & 1) != 0;
    P->xa_adpcm_ = ((mode >> 6) & 1) != 0;
    P->sector_raw_ = ((mode >> 5) & 1) != 0;
    P->ignore_bit_ = ((mode >> 4) & 1) != 0;
    P->xa_filter_ = ((mode >> 3) & 1) != 0;
    P->report_ = ((mode >> 2) & 1) != 0;
    P->auto_pause_ = ((mode >> 1) & 1) != 0;
    P->cdda_ = (mode & 1) != 0;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_getlocl(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    fifo8_push_back(&P->response_, P->hdr_mm_);
    fifo8_push_back(&P->response_, P->hdr_ss_);
    fifo8_push_back(&P->response_, P->hdr_ff_);
    fifo8_push_back(&P->response_, P->hdr_mode_);
    fifo8_push_back(&P->response_, P->sub_file_);
    fifo8_push_back(&P->response_, P->sub_channel_);
    fifo8_push_back(&P->response_, P->sub_mode_);
    fifo8_push_back(&P->response_, P->sub_coding_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_getlocp(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    const psx_cd_track* track = track_from_loc(P, P->read_loc_);
    uint8_t mm, ss, ff, amm, ass, aff;
    lba_to_msf(P, P->read_loc_ - track->lba_start, &mm, &ss, &ff);
    lba_to_msf(P, P->read_loc_, &amm, &ass, &aff);
    fifo8_push_back(&P->response_, track->number);
    fifo8_push_back(&P->response_, 1);
    fifo8_push_back(&P->response_, dec_to_bcd(P, mm));
    fifo8_push_back(&P->response_, dec_to_bcd(P, ss));
    fifo8_push_back(&P->response_, dec_to_bcd(P, ff));
    fifo8_push_back(&P->response_, dec_to_bcd(P, amm));
    fifo8_push_back(&P->response_, dec_to_bcd(P, ass));
    fifo8_push_back(&P->response_, dec_to_bcd(P, aff));
    enqueue_irq(P, 3, 50000);
}
static void cmd_setsession(psx_cdrom *P) {
    fifo8_clear(&P->param_);
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    P->stat_ = 0x42;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 2, 50000);
}
static void cmd_gettn(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    fifo8_push_back(&P->response_, P->stat_);
    fifo8_push_back(&P->response_, 1);
    fifo8_push_back(&P->response_, dec_to_bcd(P, (uint8_t)(P->ntracks_)));
    enqueue_irq(P, 3, 50000);
}
static void cmd_gettd(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    const int track = bcd_to_dec(P, fifo8_front(&P->param_));
    fifo8_pop_front(&P->param_);
    uint8_t mm, ss, ff;
    if (track == 0) lba_to_msf(P, total_lba(P), &mm, &ss, &ff);
    else lba_to_msf(P, P->tracks_[track - 1].lba_start, &mm, &ss, &ff);
    fifo8_push_back(&P->response_, P->stat_);
    fifo8_push_back(&P->response_, dec_to_bcd(P, mm));
    fifo8_push_back(&P->response_, dec_to_bcd(P, ss));
    enqueue_irq(P, 3, 50000);
}
static void cmd_seekl(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    P->read_loc_ = P->seek_loc_;
    P->stat_ = 0x42;
    P->mode_ = CD_MODE_SEEK;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_seekp(psx_cdrom *P) { cmd_seekl(P); }
static void cmd_test(psx_cdrom *P) {
    const uint8_t command = fifo8_front(&P->param_);
    fifo8_pop_front(&P->param_);
    fifo8_clear(&P->response_);
    switch (command) {
        case 0x04:
            P->stat_ = 0x2;
            fifo8_push_back(&P->response_, P->stat_);
            enqueue_irq(P, 3, 50000);
            break;
        case 0x05:
            fifo8_push_back(&P->response_, 0);
            fifo8_push_back(&P->response_, 0);
            enqueue_irq(P, 3, 50000);
            break;
        case 0x20:
            fifo8_push_back(&P->response_, 0x94);
            fifo8_push_back(&P->response_, 0x09);
            fifo8_push_back(&P->response_, 0x19);
            fifo8_push_back(&P->response_, 0xC0);
            enqueue_irq(P, 3, 50000);
            break;
        case 0x22: {
            const char* s = "for US/AEP";
            for (const char* p = s; *p; ++p) fifo8_push_back(&P->response_, (uint8_t)(*p));
            enqueue_irq(P, 3, 50000);
            break;
        }
        case 0x60:
            fifo8_push_back(&P->response_, 0);
            enqueue_irq(P, 3, 50000);
            break;
        default:
            break;
    }
}
static void cmd_getid(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    P->stat_ = 0x42;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
    if (is_audio_cd(P)) {
        const uint8_t r[] = {0x0A, 0x90, 0, 0, 0, 0, 0, 0};
        for (size_t ri = 0; ri < sizeof r; ri++) fifo8_push_back(&P->response_, r[ri]);
        enqueue_irq(P, 5, 50000);
        return;
    }
    // Region from license sector (LBA 4): Amer/Euro/Japa → SCEA/SCEE/SCEI.
    // Falls back to SCEA when the sector cannot be read.
    uint8_t region = 'A';
    uint8_t sector[kBytesPerSector];
    if (read_sector(P, 4 + 150, sector)) {
        // Mode2 Form1 user data at offset 24.
        const char* text = (const char*)(sector + 24);
        if (strstr(text, "Europe") != NULL) region = 'E';
        else if (strstr(text, "Japan") != NULL) region = 'I';
        else if (strstr(text, "Amer") != NULL) region = 'A';
    }
    const uint8_t r[] = {0x02, 0x00, 0x20, 0x00, 0x53, 0x43, 0x45, region};
    for (size_t ri = 0; ri < sizeof r; ri++) fifo8_push_back(&P->response_, r[ri]);
    enqueue_irq(P, 2, 50000);
}
static void cmd_reads(psx_cdrom *P) { cmd_readn(P); }
static void cmd_readtoc(psx_cdrom *P) {
    if (P->lid_open_) {
        fifo8_push_back(&P->response_, 0x11); fifo8_push_back(&P->response_, 0x80);
        enqueue_irq(P, 5, 50000); return;
    }
    P->mode_ = CD_MODE_TOC;
    fifo8_push_back(&P->response_, P->stat_);
    enqueue_irq(P, 3, 50000);
}
static void cmd_videocd(psx_cdrom *P) {
    fifo8_push_back(&P->response_, 0x11);
    fifo8_push_back(&P->response_, 0x40);
    enqueue_irq(P, 5, 50000);
}


bool psx_cdrom_load_bin(psx_cdrom *P, const uint8_t *data, size_t size)
{
    P->ntracks_ = 0;
    if (!data || size < kBytesPerSector) return false;
    psx_cd_track *t = &P->tracks_[0];
    memset(t, 0, sizeof *t);
    t->size = (int64_t)size;
    t->number = 1;
    t->lba = (int)(size / kBytesPerSector);
    t->lba_start = 150;
    t->lba_end = t->lba;
    t->is_audio = false;
    P->track_data_[0].data = data;
    P->track_data_[0].size = size;
    P->ntracks_ = 1;
    P->lid_open_ = false;
    psx_cdrom_reset(P);
    P->lid_open_ = false;
    return true;
}
