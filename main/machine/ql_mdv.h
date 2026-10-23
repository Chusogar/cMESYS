// ql_mdv.h - Sinclair QL microdrive: a QLAY cartridge image (255 sectors of
// 686 bytes, 174930 bytes) going past the head as a timeline of byte pairs
// (sector header, gap, block, gap) at 100 kbit/s. C port of dsp-cpp's
// machine/ql_mdv. The image is read where it is (flash is fine); a sector
// the QL writes to gets its own copy of its tape (~1 KB, allocated then).
#ifndef QL_MDV_H
#define QL_MDV_H

#include <stdint.h>
#include <stdbool.h>

#define QL_MDV_SECTORS      255
#define QL_MDV_SECTOR_BYTES 686
#define QL_MDV_IMAGE_BYTES  (QL_MDV_SECTORS * QL_MDV_SECTOR_BYTES)
#define QL_MDV_PAIRS_HEADER 14
#define QL_MDV_PAIRS_GAP1   35
#define QL_MDV_PAIRS_BLOCK  269
#define QL_MDV_PAIRS_GAP2   60
#define QL_MDV_PAIRS_SECTOR (QL_MDV_PAIRS_HEADER + QL_MDV_PAIRS_GAP1 + QL_MDV_PAIRS_BLOCK + QL_MDV_PAIRS_GAP2)
#define QL_MDV_TAPE_PAIRS   (QL_MDV_SECTORS * QL_MDV_PAIRS_SECTOR)
#define QL_MDV_BIT_RATE     100000

typedef struct { uint8_t left, right, erased; } ql_mdv_pair;

typedef struct ql_mdv {
    uint16_t (*tx_pop)(void *user);         // next pair to write (ZX8302)
    void *user;
    const uint8_t *image;                   // NULL: no cartridge
    ql_mdv_pair *written[QL_MDV_SECTORS];   // sectors written to (NULL until then)
    bool clk, comms_in, comms_out, erase, write, motor;
    int bit, pair;
} ql_mdv;

void ql_mdv_init(ql_mdv *m);
void ql_mdv_reset(ql_mdv *m);
// Inserts a cartridge (image: QL_MDV_IMAGE_BYTES, kept by the caller) or
// takes it out (NULL); what was written to the old one is dropped.
void ql_mdv_insert(ql_mdv *m, const uint8_t *image);
static inline bool ql_mdv_loaded(const ql_mdv *m) { return m->image != 0; }
void ql_mdv_clk_w(ql_mdv *m, int state);
static inline void ql_mdv_comms_in_w(ql_mdv *m, int s) { m->comms_in = s != 0; }
static inline void ql_mdv_erase_w(ql_mdv *m, int s) { m->erase = s != 0; }
static inline void ql_mdv_read_write_w(ql_mdv *m, int s) { m->write = s != 0; }
static inline bool ql_mdv_selected(const ql_mdv *m) { return m->comms_out; }
static inline int ql_mdv_comms_out(const ql_mdv *m) { return m->comms_out ? 1 : 0; }
void ql_mdv_tick_bit(ql_mdv *m);
int ql_mdv_data1(const ql_mdv *m);
int ql_mdv_data2(const ql_mdv *m);
int ql_mdv_gap(const ql_mdv *m);

#endif
