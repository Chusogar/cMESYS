// ql_mdv.c - Sinclair QL microdrive. See ql_mdv.h.
#include "ql_mdv.h"
#include <stdlib.h>
#include <string.h>

#define BLOCK_PREAMBLE 28

void ql_mdv_init(ql_mdv *m)
{
    memset(m, 0, sizeof *m);
}

void ql_mdv_reset(ql_mdv *m)
{
    m->clk = m->comms_in = m->comms_out = m->erase = m->write = m->motor = false;
}

static void drop_written(ql_mdv *m)
{
    for (int i = 0; i < QL_MDV_SECTORS; i++) {
        free(m->written[i]);
        m->written[i] = NULL;
    }
}

void ql_mdv_insert(ql_mdv *m, const uint8_t *image)
{
    drop_written(m);
    m->image = image;
    m->bit = 0;
    m->pair = 0;
    if (!image) m->motor = false;
}

// What the tape holds at `pair`: a sector's 14 header pairs (when its
// preamble is valid) and, after a gap, its 269 block pairs; erased elsewhere.
static ql_mdv_pair pair_at(const ql_mdv *m, int pair)
{
    const int sector = pair / QL_MDV_PAIRS_SECTOR, off = pair % QL_MDV_PAIRS_SECTOR;
    const ql_mdv_pair erased = {0, 0, 1};
    if (m->written[sector]) return m->written[sector][off];
    if (!m->image) return erased;
    const uint8_t *sec = m->image + (size_t)sector * QL_MDV_SECTOR_BYTES;
    if (sec[10] != 0xff || sec[11] != 0xff) return erased;
    if (off < QL_MDV_PAIRS_HEADER) { ql_mdv_pair p = {sec[2 * off], sec[2 * off + 1], 0}; return p; }
    const int block = off - QL_MDV_PAIRS_HEADER - QL_MDV_PAIRS_GAP1;
    if (block < 0 || block >= QL_MDV_PAIRS_BLOCK) return erased;
    if (sec[BLOCK_PREAMBLE + 10] != 0xff || sec[BLOCK_PREAMBLE + 11] != 0xff) return erased;
    ql_mdv_pair p = {sec[BLOCK_PREAMBLE + 2 * block], sec[BLOCK_PREAMBLE + 2 * block + 1], 0};
    return p;
}

static ql_mdv_pair *written_pair(ql_mdv *m, int pair)
{
    const int sector = pair / QL_MDV_PAIRS_SECTOR;
    if (!m->written[sector]) {
        ql_mdv_pair *t = malloc(sizeof(ql_mdv_pair) * QL_MDV_PAIRS_SECTOR);
        if (!t) return NULL;
        for (int i = 0; i < QL_MDV_PAIRS_SECTOR; i++) t[i] = pair_at(m, sector * QL_MDV_PAIRS_SECTOR + i);
        m->written[sector] = t;
    }
    return &m->written[sector][pair % QL_MDV_PAIRS_SECTOR];
}

void ql_mdv_clk_w(ql_mdv *m, int state)
{
    const bool level = state != 0;
    if (m->clk && !level) {
        m->comms_out = m->comms_in;
        m->motor = m->comms_out && m->image;
    }
    m->clk = level;
}

void ql_mdv_tick_bit(ql_mdv *m)
{
    if (!m->motor) return;
    if (m->write) {
        if (m->bit == 0) {
            const uint16_t v = m->tx_pop ? m->tx_pop(m->user) : 0;
            ql_mdv_pair *p = written_pair(m, m->pair);
            if (p) { p->left = (uint8_t)(v >> 8); p->right = (uint8_t)v; p->erased = 0; }
        }
    } else if (m->erase) {
        ql_mdv_pair *p = written_pair(m, m->pair);
        if (p) { p->left = p->right = 0; p->erased = 1; }
    }
    if (++m->bit == 8) {
        m->bit = 0;
        if (++m->pair >= QL_MDV_TAPE_PAIRS) m->pair = 0;
    }
}

int ql_mdv_data1(const ql_mdv *m)
{
    if (!m->comms_out || m->write) return 0;
    const ql_mdv_pair p = pair_at(m, m->pair);
    return p.erased ? 0 : (p.left >> (7 - m->bit)) & 1;
}

int ql_mdv_data2(const ql_mdv *m)
{
    if (!m->comms_out || m->write) return 0;
    const ql_mdv_pair p = pair_at(m, m->pair);
    return p.erased ? 0 : (p.right >> (7 - m->bit)) & 1;
}

int ql_mdv_gap(const ql_mdv *m)
{
    if (!m->comms_out || !m->image) return 1;
    return pair_at(m, m->pair).erased ? 1 : 0;
}
