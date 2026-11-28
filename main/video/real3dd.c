// real3dd.c - Real3D Pro-1000: C port of dsp-cpp's video/real3d.cpp.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "real3dd.h"

#define PCI_ID_STEP2 0x178611DBu
#define PCI_ID_STEP1 0x16C311DBu
#define MAX_TEXELS (1024 * 1024 * 2)

static inline float as_float(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (hi < v ? hi : v); }
static inline float minf(float a, float b) { return b < a ? b : a; }
static inline float maxf(float a, float b) { return a < b ? b : a; }
static inline int mini(int a, int b) { return b < a ? b : a; }
static inline int maxi(int a, int b) { return a < b ? b : a; }

static float pro_float16(uint16_t v)
{
    const uint32_t a = (uint32_t)v << 15;
    uint32_t exponent = (a & 0x7E000000u) >> 25;
    if (exponent <= 31) exponent += 127;
    else exponent += 127 - 64;
    const uint32_t mantissa = (a & 0x1FFFFFFu) >> 2;
    return as_float((a & 0x80000000u) | (exponent << 23) | mantissa);
}

static const unsigned kDecode8x8[64] = {
    1,  0,  5,  4,  9,  8,  13, 12, 3,  2,  7,  6,  11, 10, 15, 14, 17, 16, 21, 20, 25, 24,
    29, 28, 19, 18, 23, 22, 27, 26, 31, 30, 33, 32, 37, 36, 41, 40, 45, 44, 35, 34, 39, 38,
    43, 42, 47, 46, 49, 48, 53, 52, 57, 56, 61, 60, 51, 50, 55, 54, 59, 58, 63, 62};
static const unsigned kDecode8x4[32] = {1,  0,  5,  4,  3,  2,  7,  6,  9,  8,  13, 12, 11, 10, 15, 14,
                                        17, 16, 21, 20, 19, 18, 23, 22, 25, 24, 29, 28, 27, 26, 31, 30};
static const unsigned kDecode8x2[16] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14};
static const int kMipXBase[] = {0, 1024, 1536, 1792, 1920, 1984, 2016, 2032, 2040, 2044, 2046, 2047};
static const int kMipYBase[] = {0, 512, 768, 896, 960, 992, 1008, 1016, 1020, 1022, 1023};

static const int kTapNext[16][2] = {{1, 0},  {1, 2},   {3, 9},   {4, 5},   {4, 5},   {6, 8},   {6, 7},  {4, 8},
                                    {1, 2},  {10, 0},  {11, 12}, {11, 12}, {13, 15}, {13, 14}, {11, 15}, {1, 2}};
enum { kTestLogicReset = 0, kCaptureDR = 3, kShiftDR = 4, kUpdateDR = 8, kCaptureIR = 10, kShiftIR = 11, kUpdateIR = 15 };

static void jtag_reset_devices(real3dd *g)
{
    for (int i = 0; i < 17; i++) {
        r3d_jtag *d = &g->jtag[i];
        if (d->asic) {
            d->size = 32;
            d->shift = d->idcode;
            d->ir = 3;
        } else {
            d->size = 1;
            d->shift = 0;
            d->ir = 15;
        }
    }
}

bool r3d_init(real3dd *g)
{
    memset(g, 0, sizeof *g);
    g->cull_lo = (uint32_t *)calloc(0x100000, 4);
    g->cull_hi = (uint32_t *)calloc(0x40000, 4);
    g->poly = (uint32_t *)calloc(0x100000, 4);
    g->tex = (uint16_t *)calloc(2048 * 2048, 2);
    g->fifo = (uint32_t *)malloc(0x40000 * 4);
    g->scratch = (uint16_t *)malloc(MAX_TEXELS * 2);
    g->zbuf = (float *)calloc((size_t)R3D_W * R3D_H, sizeof(float));
    g->colour = (uint32_t *)calloc((size_t)R3D_W * R3D_H, 4);
    if (!g->cull_lo || !g->cull_hi || !g->poly || !g->tex || !g->fifo || !g->scratch || !g->zbuf || !g->colour) return false;
    static const int kChain[17] = {4, 0, 1, 2, -1, 3, 3, -1, -1, -1, 2, -1, 3, 3, -1, -1, -1};
    static const uint32_t kId[5] = {0x416c3057, 0x316c4057, 0x516c5057, 0x316c6057, 0x516c7057};
    for (int i = 0; i < 17; ++i) {
        g->jtag[i].asic = kChain[i] >= 0;
        g->jtag[i].asic_index = kChain[i];
        g->jtag[i].idcode = kChain[i] >= 0 ? kId[kChain[i]] : 0;
        g->jtag[i].size = 1;
    }
    g->model_scale = g->model_alpha = 1.0f;
    g->sun_clamp = true;
    r3d_reset(g);
    return true;
}

void r3d_reset(real3dd *g)
{
    memset(g->cull_lo, 0, 0x100000 * 4);
    memset(g->cull_hi, 0, 0x40000 * 4);
    memset(g->poly, 0, 0x100000 * 4);
    memset(g->tex, 0, 2048 * 2048 * 2);
    g->fifo_n = 0;
    g->n_hi = g->n_poly = 0;
    memset(g->config, 0, sizeof g->config);
    g->vrom_fifo_idx = 0;
    g->ping_pong = g->ping_pong_copy = false;
    g->command_written = g->tilegen_draw = false;
    g->frame_ready = false;
    g->block_culling = false;
    g->dma_src = g->dma_dst = g->dma_len = g->dma_data = 0;
    g->dma_status = g->dma_config = 0;
    memset(g->modeword, 0, sizeof g->modeword);
    g->jtag_state = kTestLogicReset;
    g->jtag_last_tck = false;
    g->jtag_tdo = false;
    jtag_reset_devices(g);
    g->stats_polys = g->stats_viewports = g->stats_uploads = 0;
}

static inline bool flipped(const real3dd *g) { return g->ping_pong != g->ping_pong_copy; }

// ---------------------------------------------------------------------------
// Registers and memories

uint32_t r3d_read_register(real3dd *g, unsigned reg)
{
    if (reg == 0) return 0xfdffffffu | (g->ping_pong ? 0x02000000u : 0u);
    if (reg >= 20 && reg <= 32) return 0;
    return 0xffffffffu;
}

static void push_write(r3d_write **v, size_t *n, size_t *cap, uint32_t index, uint32_t value)
{
    if (*n == *cap) {
        const size_t nc = *cap ? *cap * 2 : 4096;
        r3d_write *p = (r3d_write *)realloc(*v, nc * sizeof **v);
        if (!p) return;
        *v = p;
        *cap = nc;
    }
    (*v)[*n].index = index;
    (*v)[*n].value = value;
    ++*n;
}

void r3d_write_culling_low(real3dd *g, uint32_t offset, uint32_t value) { g->cull_lo[(offset / 4) & 0xfffff] = value; }

void r3d_write_culling_high(real3dd *g, uint32_t offset, uint32_t value)
{
    const uint32_t index = (offset / 4) & 0x3ffff;
    if (flipped(g)) push_write(&g->pending_hi, &g->n_hi, &g->cap_hi, index, value);
    else g->cull_hi[index] = value;
}

void r3d_write_polygon_ram(real3dd *g, uint32_t offset, uint32_t value)
{
    const uint32_t index = (offset / 4) & 0xfffff;
    if (flipped(g)) push_write(&g->pending_poly, &g->n_poly, &g->cap_poly, index, value);
    else g->poly[index] = value;
}

void r3d_write_texture_fifo(real3dd *g, uint32_t value)
{
    if (g->fifo_n < 0x100000 / 4) g->fifo[g->fifo_n++] = value;
}

static void store_texture(real3dd *g, unsigned x0, unsigned y0, unsigned width, unsigned height, const uint16_t *data,
                          bool sixteen_bit, bool write_lsb, bool write_msb, uint32_t *consumed)
{
    const unsigned tile_x = width < 8 ? width : 8;
    const unsigned tile_y = height < 8 ? height : 8;
    const unsigned *decode = tile_x == 8 ? kDecode8x8 : tile_x == 4 ? kDecode8x4 : tile_x == 2 ? kDecode8x2 : NULL;
    *consumed = 0;
    if (!decode) return;
    uint16_t *tex = g->tex;
    if (sixteen_bit) {
        for (unsigned y = y0; y < y0 + height; y += tile_y)
            for (unsigned x = x0; x < x0 + width; x += tile_x) {
                for (unsigned yy = 0; yy < tile_y; ++yy)
                    for (unsigned xx = 0; xx < tile_x; ++xx) {
                        tex[((y + yy) & 2047) * 2048 + ((x + xx) & 2047)] = data[decode[yy * tile_x + xx]];
                        ++*consumed;
                    }
                data += tile_x * tile_y;
            }
    } else {
        const unsigned select = (write_lsb ? 1u : 0u) | (write_msb ? 2u : 0u);
        static const uint16_t kMask[4] = {0xffff, 0xff00, 0x00ff, 0x0000};
        const unsigned step = (tile_x * tile_y) / 2 > 1 ? (tile_x * tile_y) / 2 : 1;
        for (unsigned y = y0; y < y0 + height; y += tile_y)
            for (unsigned x = x0; x < x0 + width; x += tile_x) {
                for (unsigned yy = 0; yy < tile_y; ++yy)
                    for (unsigned xx = 0; xx < tile_x; ++xx) {
                        if (select == 0) continue;
                        uint16_t *t = &tex[((y + yy) & 2047) * 2048 + ((x + xx) & 2047)];
                        *t &= kMask[select];
                        const unsigned shift = 8 * ((xx & 1) ^ 1);
                        const unsigned index = (yy ^ 1) * tile_x + (xx ^ 1) - (tile_x & 1);
                        uint16_t v = (uint16_t)((data[decode[index] / 2] >> shift) & 0xff);
                        v = (uint16_t)(v | (v << 8));
                        v &= (uint16_t)(kMask[select] ^ 0xffff);
                        *t |= v;
                    }
                data += step;
                *consumed += step;
            }
    }
}

static void upload_texture(real3dd *g, uint32_t header, const uint16_t *data)
{
    if (g->before_texture_write) g->before_texture_write(g->user);
    const unsigned x = 32 * (header & 0x3f), y = 32 * ((header >> 7) & 0x1f);
    const unsigned page = (header >> 20) & 1;
    unsigned width = 32u << ((header >> 14) & 7), height = 32u << ((header >> 17) & 7);
    const unsigned type = (header >> 24) & 0xff;
    const bool sixteen = (header >> 23) & 1, upper = (header >> 22) & 1, lower = (header >> 21) & 1;
    uint32_t used = 0;
    ++g->stats_uploads;
    if (width > 2048 || height > 1024) return;
    switch (type) {
    case 0x00:
    case 0x01:
        store_texture(g, x, y + page * 1024, width, height, data, sixteen, lower, upper, &used);
        data += used;
        if (type == 0x01) break;
        /* fallthrough */
    case 0x02:
        for (int i = 1; width >= 4 && height >= 4 && i < 11; ++i) {
            const unsigned mx = (unsigned)kMipXBase[i] + (x >> i), my = (unsigned)kMipYBase[i] + (y >> i);
            width /= 2;
            height /= 2;
            store_texture(g, mx, my + page * 1024, width, height, data, sixteen, lower, upper, &used);
            data += used;
        }
        break;
    default: break;
    }
}

void r3d_write_texture_port(real3dd *g, uint32_t value)
{
    if (g->vrom_fifo_idx == 2) {
        const uint32_t addr = g->vrom_fifo[0] & 0xffffff, header = g->vrom_fifo[1];
        uint16_t *data = g->scratch;
        memset(data, 0, MAX_TEXELS * 2);
        if (addr < 0x100000) {
            for (size_t i = 0; i < MAX_TEXELS / 2 && addr + i < 0x100000; ++i) {
                const uint32_t w = g->poly[addr + i];
                data[i * 2] = (uint16_t)w;
                data[i * 2 + 1] = (uint16_t)(w >> 16);
            }
        } else if ((size_t)addr < g->vrom_words) {
            size_t avail = (g->vrom_words - addr) * 2;
            if (avail > MAX_TEXELS) avail = MAX_TEXELS;
            memcpy(data, g->vrom + (size_t)addr * 4, avail * 2);
        }
        upload_texture(g, header, data);
        g->vrom_fifo_idx = 0;
    } else g->vrom_fifo[g->vrom_fifo_idx++] = value;
}

static void flush_textures(real3dd *g)
{
    const size_t n = g->fifo_n;
    if (n > 2) {
        uint16_t *data = g->scratch;
        for (size_t i = 0; i + 2 < n;) {
            const uint32_t size = (2 + g->fifo[i] / 2) / 4;
            const uint32_t header = g->fifo[i + 1];
            if (size == 0) break;
            memset(data, 0, MAX_TEXELS * 2);
            for (size_t k = 0; i + 2 + k < n && k * 2 + 1 < MAX_TEXELS; ++k) {
                data[k * 2] = (uint16_t)g->fifo[i + 2 + k];
                data[k * 2 + 1] = (uint16_t)(g->fifo[i + 2 + k] >> 16);
            }
            upload_texture(g, header, data);
            i += size;
        }
    }
    g->fifo_n = 0;
}

static void draw_frame(real3dd *g)
{
    g->tilegen_draw = false;
    g->command_written = false;
    g->block_culling = (g->modeword[0] & 4) != 0;
    g->frame_ready = true;
}

void r3d_flush(real3dd *g)
{
    g->command_written = true;
    if (!flipped(g)) {
        flush_textures(g);
        if (g->tilegen_draw) draw_frame(g);
    }
}

// ---------------------------------------------------------------------------
// DMA

uint8_t r3d_read_dma8(real3dd *g, unsigned reg)
{
    switch (reg) {
    case 0xc: return g->dma_status;
    case 0xe: return g->dma_config;
    default: return 0;
    }
}

void r3d_write_dma8(real3dd *g, unsigned reg, uint8_t value)
{
    if (reg == 0xd) {
        if (value & 1) {
            g->dma_status &= (uint8_t)~1;
            if (g->dma_irq) g->dma_irq(g->user, false);
        }
    } else if (reg == 0xe) g->dma_config = value;
}

uint32_t r3d_read_dma32(real3dd *g, unsigned reg) { return reg == 0x14 ? g->dma_data : 0; }

void r3d_write_dma32(real3dd *g, unsigned reg, uint32_t value)
{
    switch (reg) {
    case 0x00: g->dma_src = value; break;
    case 0x04: g->dma_dst = value; break;
    case 0x08:
        g->dma_len = value;
        while (g->dma_len != 0) {
            uint32_t d = g->read32 ? g->read32(g->user, g->dma_src) : 0;
            if (g->dma_config & 0x80) d = __builtin_bswap32(d);
            if (g->write32) g->write32(g->user, g->dma_dst, d);
            g->dma_src += 4;
            g->dma_dst += 4;
            --g->dma_len;
        }
        if (g->dma_config & 1) {
            g->dma_status |= 1;
            if (g->dma_irq) g->dma_irq(g->user, true);
        }
        break;
    case 0x10:
        if (value & 0x20000000) g->dma_data = PCI_ID_STEP1;
        else if (value & 0x80000000) g->dma_data = r3d_read_register(g, value & 0x3f);
        break;
    case 0x14: g->dma_data = 0xffffffff; break;
    default: break;
    }
}

uint32_t r3d_pci_config_read(real3dd *g, unsigned reg)
{
    (void)g;
    return reg == 0 ? __builtin_bswap32(PCI_ID_STEP2) : 0;
}

// ---------------------------------------------------------------------------
// JTAG

void r3d_jtag_write(real3dd *g, bool tck, bool tms, bool tdi, bool trst)
{
    if (!trst) {
        jtag_reset_devices(g);
        return;
    }
    if (tck == g->jtag_last_tck) return;
    g->jtag_last_tck = tck;
    if (tck) {
        switch (g->jtag_state) {
        case kTestLogicReset: jtag_reset_devices(g); break;
        case kCaptureDR:
            for (int i = 0; i < 17; i++) {
                r3d_jtag *d = &g->jtag[i];
                if (d->asic && d->ir == 3) { d->size = 32; d->shift = d->idcode; }
                else if (d->asic && d->ir == 8) { d->size = 32; d->shift = g->modeword[d->asic_index]; }
                else { d->size = 1; d->shift = 0; }
            }
            break;
        case kCaptureIR:
            for (int i = 0; i < 17; i++) {
                r3d_jtag *d = &g->jtag[i];
                if (d->asic) { d->size = 5; d->shift = 1; }
                else { d->size = 4; d->shift = 0x9; }
            }
            break;
        case kShiftDR:
        case kShiftIR: {
            bool bit = tdi;
            for (int i = 0; i < 17; i++) {
                r3d_jtag *d = &g->jtag[i];
                const bool out = d->shift & 1;
                d->shift >>= 1;
                if (bit) d->shift |= (uint64_t)1 << (d->size - 1);
                bit = out;
            }
            break;
        }
        default: break;
        }
        g->jtag_state = kTapNext[g->jtag_state][tms ? 1 : 0];
    } else {
        switch (g->jtag_state) {
        case kShiftDR:
        case kShiftIR: g->jtag_tdo = g->jtag[16].shift & 1; break;
        case kUpdateDR:
            for (int i = 0; i < 17; i++)
                if (g->jtag[i].asic && g->jtag[i].ir == 8) g->modeword[g->jtag[i].asic_index] = (uint32_t)g->jtag[i].shift;
            break;
        case kUpdateIR:
            for (int i = 0; i < 17; i++) g->jtag[i].ir = (uint8_t)g->jtag[i].shift;
            break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Frame protocol

void r3d_begin_vblank(real3dd *g)
{
    g->ping_pong_copy = g->ping_pong;
    for (size_t i = 0; i < g->n_hi; i++) g->cull_hi[g->pending_hi[i].index] = g->pending_hi[i].value;
    for (size_t i = 0; i < g->n_poly; i++) g->poly[g->pending_poly[i].index] = g->pending_poly[i].value;
    g->n_hi = g->n_poly = 0;
    if (g->command_written) flush_textures(g);
    if (g->tilegen_draw && g->command_written) draw_frame(g);
}

void r3d_flip_ping_pong(real3dd *g)
{
    g->ping_pong = !g->ping_pong;
    g->tilegen_draw = false;
    g->command_written = false;
}

void r3d_tilegen_draw_frame(real3dd *g)
{
    g->tilegen_draw = true;
    if (!flipped(g) && g->command_written) draw_frame(g);
}

// ---------------------------------------------------------------------------
// Scene traversal

static const uint32_t *cull_ptr(const real3dd *g, uint32_t addr)
{
    addr &= 0xffffff;
    if (addr >= 0x800000 && addr < 0x840000) return &g->cull_hi[addr & 0x3ffff];
    if (addr < 0x100000) return &g->cull_lo[addr];
    return NULL;
}

static const uint32_t *model_ptr(const real3dd *g, uint32_t addr)
{
    addr &= 0xffffff;
    if (addr < 0x100000) return &g->poly[addr];
    if ((size_t)addr + 16 < g->vrom_words) return (const uint32_t *)(const void *)(g->vrom + (size_t)addr * 4);
    return NULL;
}

static void mat_mul(float *out, const float *a, const float *b)
{
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] + a[2 * 4 + rr] * b[c * 4 + 2] +
                            a[3 * 4 + rr] * b[c * 4 + 3];
    memcpy(out, r, sizeof r);
}

static void mult_matrix(real3dd *g, uint32_t index)
{
    if (!g->matrix_base) return;
    const uint32_t *s = g->matrix_base + (size_t)index * 12;
    const uint32_t *lo_end = g->cull_lo + 0x100000, *hi_end = g->cull_hi + 0x40000;
    const bool in_lo = s >= g->cull_lo && s + 12 <= lo_end;
    const bool in_hi = s >= g->cull_hi && s + 12 <= hi_end;
    if (!in_lo && !in_hi) return;
    float m[16];
    m[0] = as_float(s[3]);  m[4] = as_float(s[4]);  m[8] = as_float(s[5]);   m[12] = as_float(s[0]);
    m[1] = as_float(s[6]);  m[5] = as_float(s[7]);  m[9] = as_float(s[8]);   m[13] = as_float(s[1]);
    m[2] = as_float(s[9]);  m[6] = as_float(s[10]); m[10] = as_float(s[11]); m[14] = as_float(s[2]);
    m[3] = 0;               m[7] = 0;               m[11] = 0;               m[15] = 1;
    mat_mul(g->mat.m, g->mat.m, m);
}

static void swapf(float *a, float *b) { const float t = *a; *a = *b; *b = t; }

static void reset_matrix_rotation(real3dd *g)
{
    float m[16];
    memcpy(m, g->mat.m, sizeof m);
    swapf(&m[1], &m[4]);
    swapf(&m[2], &m[8]);
    swapf(&m[6], &m[9]);
    m[12] = m[13] = m[14] = 0;
    m[3] = m[7] = m[11] = 0;
    m[15] = 1;
    const float s1 = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float s2 = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float s3 = sqrtf(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    if (s1 > 0) { m[0] /= s1; m[1] /= s1; m[2] /= s1; }
    if (s2 > 0) { m[4] /= s2; m[5] /= s2; m[6] /= s2; }
    if (s3 > 0) { m[8] /= s3; m[9] /= s3; m[10] /= s3; }
    mat_mul(g->mat.m, g->mat.m, m);
}

static void descend_node(real3dd *g, uint32_t addr, int depth);
static void descend_list(real3dd *g, uint32_t addr, int depth);
static void draw_model(real3dd *g, uint32_t addr);

static void descend_ptr(real3dd *g, uint32_t addr, int depth)
{
    if ((addr & 0xffffff) == 0) return;
    switch ((addr >> 24) & 5) {
    case 0: descend_node(g, addr & 0xffffff, depth + 1); break;
    case 1: draw_model(g, addr & 0xffffff); break;
    case 4: descend_list(g, addr & 0xffffff, depth + 1); break;
    default: break;
    }
}

static void descend_list(real3dd *g, uint32_t addr, int depth)
{
    const uint32_t *list = cull_ptr(g, addr);
    if (!list) return;
    for (int i = 0; i < 4096; ++i) {
        if (list[i] & 0x01000000) break;
        descend_node(g, list[i] & 0xffffff, depth + 1);
        if (list[i] & 0x02000000) break;
    }
}

static void render_viewport(real3dd *g, uint32_t addr, int priority, int depth)
{
    for (int guard = 0; guard < 256; ++guard) {
        if ((addr & 0xffffff) == 0) return;
        const uint32_t *vp = cull_ptr(g, addr);
        if (!vp) return;
        const int vp_priority = (int)((vp[0] >> 3) & 3);
        const bool disabled = (vp[0] & 0x20) != 0;
        if (vp_priority == priority) {
            r3d_viewport s;
            memset(&s, 0, sizeof s);
            s.priority = vp_priority;
            s.x = (float)(vp[0x1a] & 0xffff) / 16.0f;
            s.y = (float)(vp[0x1a] >> 16) / 16.0f;
            s.w = (float)(vp[0x14] & 0xffff) / 4.0f;
            s.h = (float)(vp[0x14] >> 16) / 4.0f;
            const float cv = as_float(vp[0x8]), cw = as_float(vp[0x9]);
            const float io = as_float(vp[0xa]), jo = as_float(vp[0xb]);
            for (int i = 0; i < 8; ++i) g->planes[i] = as_float(vp[0xc + i]);
            s.l = (0.0f - jo) / cv;
            s.r = (1.0f - jo) / cv;
            s.b = -(1.0f - io) / cw;
            s.t = io / cw;
            s.sun[0] = as_float(vp[0x05]);
            s.sun[1] = -as_float(vp[0x06]);
            s.sun[2] = -as_float(vp[0x04]);
            s.sun_intensity = clampf(as_float(vp[0x07]), 0.0f, 1.0f);
            if (isnan(s.sun_intensity)) s.sun_intensity = 0;
            s.ambient = (float)((vp[0x24] >> 8) & 0xff) / 255.0f;
            s.fog_rgb[0] = (float)((vp[0x22] >> 16) & 0xff) / 255.0f;
            s.fog_rgb[1] = (float)((vp[0x22] >> 8) & 0xff) / 255.0f;
            s.fog_rgb[2] = (float)(vp[0x22] & 0xff) / 255.0f;
            s.fog_density = fabsf(as_float(vp[0x23]));
            if (!isfinite(s.fog_density)) s.fog_density = 0;
            s.fog_start = (float)(int16_t)(vp[0x25] & 0xffff) / 255.0f;
            s.fog_ambient = (float)((vp[0x25] >> 16) & 0xff) / 255.0f;
            s.scroll_fog = (float)(vp[0x20] & 0xff) / 255.0f;
            const bool sane = isfinite(s.l) && isfinite(s.r) && isfinite(s.b) && isfinite(s.t) && s.r != s.l && s.t != s.b &&
                              s.w > 0 && s.h > 0;
            if (sane) {
                if (g->n_vp == g->cap_vp) {
                    const size_t nc = g->cap_vp ? g->cap_vp * 2 : 16;
                    r3d_viewport *p = (r3d_viewport *)realloc(g->viewports, nc * sizeof *p);
                    if (!p) return;
                    g->viewports = p;
                    g->cap_vp = nc;
                }
                g->viewports[g->n_vp++] = s;
                ++g->stats_viewports;
                g->lod_table = cull_ptr(g, vp[0x17] & 0xffffff);
                g->model_scale = 1.0f;
                g->model_alpha = 1.0f;
                g->disable_culling = false;
                g->tex_off_x = g->tex_off_y = g->tex_page = 0;
                memset(g->mat.m, 0, sizeof g->mat.m);
                g->mat.m[4] = 1.0f;
                g->mat.m[9] = -1.0f;
                g->mat.m[2] = -1.0f;
                g->mat.m[15] = 1.0f;
                g->matrix_base = cull_ptr(g, vp[0x16] & 0xffffff);
                mult_matrix(g, 0);
                if (!disabled && ((vp[0x02] >> 24) & 5) == 0) descend_ptr(g, vp[0x02], depth);
            }
        }
        if (vp[0x01] == 0x01000000) return;
        addr = vp[0x01];
    }
}

static void descend_node(real3dd *g, uint32_t addr, int depth)
{
    if (depth > 48 || g->cur_polys > 200000) return;
    const uint32_t *node = cull_ptr(g, addr);
    if (!node) return;
    if ((node[0] & 3) == 0) return;
    if ((node[0] & 0x300) == 0x300) return;

    const uint32_t child = node[7] & 0x7ffffff, sibling = node[8] & 0x1ffffff;
    const uint32_t matrix_index = node[3] & 0xfff, lod_index = (node[3] >> 12) & 0x7f;

    if ((node[0] & 7) != 6 && !(sibling & 0x1000000) && sibling) descend_node(g, sibling, depth + 1);

    if (node[0] & 4) g->color_table = ((node[3] >> 19) | ((node[7] >> 28) << 13) | ((node[8] >> 25) << 17)) & 0xfffff;

    const float saved_scale = g->model_scale, saved_alpha = g->model_alpha;
    const bool saved_disable = g->disable_culling;
    const int sx = g->tex_off_x, sy = g->tex_off_y, sp = g->tex_page;
    const r3d_mat saved_mat = g->mat;

    if (node[1] & 1) g->model_scale = as_float(node[1] & ~3u);
    if (node[1] & 2) g->disable_culling = true;
    if (node[2] & 0x8000) {
        g->tex_off_x = 32 * (int)((node[2] >> 7) & 0x3f);
        g->tex_off_y = 32 * (int)(node[2] & 0x1f);
        g->tex_page = (int)((node[2] & 0x4000) >> 14);
    }
    if (node[0] & 0x10) {
        const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, as_float(node[4]), as_float(node[5]), as_float(node[6]), 1};
        mat_mul(g->mat.m, g->mat.m, t);
    } else if (matrix_index) mult_matrix(g, matrix_index);
    if (node[0] & 0x80) reset_matrix_rotation(g);

    const float x = g->mat.m[12], y = g->mat.m[13], z = g->mat.m[14];
    const float cull_radius = pro_float16((uint16_t)(node[9] & 0xffff)) * g->model_scale;
    const float blend_radius = pro_float16((uint16_t)(node[9] >> 16)) * g->model_scale;
    const float *pl = g->planes;
    const bool outside = (z * pl[0] - x * pl[1]) > cull_radius || (z * pl[2] + y * pl[3]) > cull_radius ||
                         (z * pl[4] - x * pl[5]) > cull_radius || (z * pl[6] + y * pl[7]) > cull_radius;
    float lod_scale = g->disable_culling ? 3.4e38f : blend_radius / sqrtf(x * x + y * y + z * z);
    if (!(lod_scale >= 0)) lod_scale = 0;
    float del[4] = {0, 0, 0, 0}, blend[4] = {1, 1, 1, 1};
    if (g->lod_table) {
        const uint32_t *e = g->lod_table + (size_t)lod_index * 8;
        for (int i = 0; i < 4; ++i) {
            del[i] = as_float(e[i * 2]);
            blend[i] = as_float(e[i * 2 + 1]);
            if (!isfinite(del[i])) del[i] = 0;
            if (!isfinite(blend[i])) blend[i] = 1;
        }
    }

    if (g->disable_culling || (!outside && lod_scale >= del[3])) {
        if (node[0] & 0x08) {
            const uint32_t *lod = cull_ptr(g, child);
            if (lod) {
                int level = 0;
                for (; level < 3; ++level)
                    if (lod_scale >= del[level] && (lod[level] & 0x1000000)) break;
                const float parent_alpha = g->model_alpha;
                float node_alpha = clampf(blend[level] * (lod_scale - del[level]), 0.0f, 1.0f);
                if (node_alpha > 31.0f / 32.0f) node_alpha = 1.0f;
                else if (node_alpha < 1.0f / 32.0f) node_alpha = 0.0f;
                g->model_alpha *= node_alpha;
                const bool nodes = (node[3] & 0x20000000) != 0;
                if (nodes) descend_node(g, lod[level] & 0xffffff, depth + 1);
                else draw_model(g, lod[level] & 0xffffff);
                if (node_alpha < 1.0f && level != 3) {
                    g->model_alpha = (1.0f - node_alpha) * parent_alpha;
                    if (nodes) descend_node(g, lod[level + 1] & 0xffffff, depth + 1);
                    else draw_model(g, lod[level + 1] & 0xffffff);
                }
            }
        } else {
            const float node_alpha = clampf(blend[3] * (lod_scale - del[3]), 0.0f, 1.0f);
            g->model_alpha *= node_alpha;
            descend_ptr(g, child, depth);
        }
    }

    g->mat = saved_mat;
    g->model_scale = saved_scale;
    g->model_alpha = saved_alpha;
    g->disable_culling = saved_disable;
    g->tex_off_x = sx;
    g->tex_off_y = sy;
    g->tex_page = sp;
}

typedef struct {
    float x, y, z, nx, ny, nz, shade;
    uint16_t u, v;
} r3d_prev;

static r3d_prev g_prev[4];               // (dsp-cpp: a thread_local of draw_model, kept between calls)

static void push_poly(real3dd *g, int pri, const r3d_deferred *d)
{
    if (g->n_polys[pri] == g->cap_polys[pri]) {
        const size_t nc = g->cap_polys[pri] ? g->cap_polys[pri] * 2 : 1024;
        r3d_deferred *p = (r3d_deferred *)realloc(g->polys[pri], nc * sizeof *p);
        if (!p) return;
        g->polys[pri] = p;
        g->cap_polys[pri] = nc;
    }
    g->polys[pri][g->n_polys[pri]++] = *d;
}

static void draw_model(real3dd *g, uint32_t addr)
{
    const uint32_t *h = model_ptr(g, addr);
    if (!h || g->model_alpha <= 0.0f) return;
    const uint32_t *end = addr < 0x100000 ? g->poly + 0x100000 : (const uint32_t *)(const void *)(g->vrom + g->vrom_words * 4);
    const int vp_index = (int)g->n_vp - 1;
    const r3d_viewport *vs = &g->viewports[vp_index];
    const float *M = g->mat.m;
    const float inv_scale = g->model_scale != 0 ? 1.0f / g->model_scale : 1.0f;
    static const int kSharedCount[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
    r3d_prev cur[4];
    memcpy(cur, g_prev, sizeof cur);

    for (int guard = 0; guard < 65536; ++guard) {
        if (h + 7 > end) return;
        if (h[6] == 0) return;
        const int num = (h[0] & 0x40) ? 4 : 3;
        const int shared = kSharedCount[h[0] & 0xf];
        const uint32_t *vdata = h + 7;
        if (vdata + (num - shared) * 4 > end) return;

        r3d_poly p;
        memset(&p, 0, sizeof p);
        p.r = p.g = p.b = p.a = 1;
        p.tw = p.th = 32;
        p.lighting = true;
        const bool smooth = (h[1] & 0x8) != 0, fixed = (h[1] & 0x20) != 0;
        const float uv_scale = (h[1] & 0x40) ? 1.0f : 0.125f;
        p.textured = (h[6] & 0x400) != 0;
        p.alpha_test = (h[6] & 0x80000000u) != 0;
        p.texture_alpha = (h[6] & 7) != 0;
        p.poly_alpha = (h[6] & 0x800000) == 0;
        p.lighting = (h[6] & 0x10000) == 0;
        p.fixed_shading = fixed && !smooth;
        p.high_priority = (h[6] & 0x10) != 0;
        p.fog_intensity = (float)((h[6] >> 11) & 0x1f) / 16.0f;
        const int tw = 32 << (((h[3] >> 3) & 7) >= 6 ? 0 : ((h[3] >> 3) & 7));
        const int th = 32 << ((h[3] & 7) >= 6 ? 0 : (h[3] & 7));
        p.tw = tw;
        p.th = th;
        p.tx = ((32 * (int)(((h[4] & 0x1f) << 1) | ((h[5] >> 7) & 1))) + g->tex_off_x) & 2047;
        p.ty = ((32 * (int)(h[5] & 0x1f)) + g->tex_off_y) & 2047;
        p.page = (int)((h[4] & 0x40) >> 6) ^ g->tex_page;
        int format = (int)((h[6] >> 7) & 7);
        if (p.alpha_test && format >= 1 && format <= 4) format += 7;
        if (format == 7) p.alpha_test = false;
        p.format = format;
        p.mirror_u = (h[2] & 2) != 0;
        p.mirror_v = (h[2] & 1) != 0;
        uint32_t rgb;
        if (h[1] & 2) rgb = h[4] >> 8;
        else {
            const uint32_t idx = (h[4] >> 8) & 0xfff;
            rgb = g->poly[(g->color_table + idx) & 0xfffff] & 0xffffff;
        }
        const float cscale = (h[4] & 0x80) ? 16.0f / 255.0f : 1.0f / 255.0f;
        p.r = (float)((rgb >> 16) & 0xff) * cscale;
        p.g = (float)((rgb >> 8) & 0xff) * cscale;
        p.b = (float)(rgb & 0xff) * cscale;
        p.inverted = ((h[6] >> 24) & 0x7f) == 2;
        int alpha8 = (h[6] & 0x800000) ? 255 : (int)(((h[6] >> 18) & 0x3f) * 255 / 32);
        if ((h[0] & 0x200) && !(h[0] & 0x100)) alpha8 /= 2;
        if (alpha8 > 255) alpha8 = 255;
        p.a = (float)alpha8 / 255.0f * g->model_alpha;
        p.node_alpha = g->model_alpha < 1.0f;
        p.vp = vp_index;

        const float fnx = (float)((int32_t)h[1] >> 8) / 4194304.0f;
        const float fny = (float)((int32_t)h[2] >> 8) / 4194304.0f;
        const float fnz = (float)((int32_t)h[3] >> 8) / 4194304.0f;

        int j = 0;
        for (int i = 0; i < 4; ++i)
            if (h[0] & (1u << i)) cur[j++] = g_prev[i];
        for (int i = 0; i < num && !smooth; ++i) {
            cur[i].nx = fnx;
            cur[i].ny = fny;
            cur[i].nz = fnz;
        }
        for (; j < num; ++j) {
            const uint32_t ix = vdata[0], iy = vdata[1], iz = vdata[2], it = vdata[3];
            cur[j].x = (float)((int32_t)ix >> 8) / 2048.0f;
            cur[j].y = (float)((int32_t)iy >> 8) / 2048.0f;
            cur[j].z = (float)((int32_t)iz >> 8) / 2048.0f;
            if (smooth) {
                cur[j].nx = (float)(int8_t)(ix & 0xff) / 128.0f;
                cur[j].ny = (float)(int8_t)(iy & 0xff) / 128.0f;
                cur[j].nz = (float)(int8_t)(iz & 0xff) / 128.0f;
            } else {
                cur[j].nx = fnx;
                cur[j].ny = fny;
                cur[j].nz = fnz;
            }
            cur[j].shade = (h[0] & 0x80) ? (float)(ix & 0xff) / 255.0f : (float)(int8_t)(ix & 0xff) / 128.0f;
            cur[j].u = (uint16_t)(it >> 16);
            cur[j].v = (uint16_t)(it & 0xffff);
            vdata += 4;
        }
        memcpy(g_prev, cur, sizeof cur);

        const bool discard = (h[0] & 0x300) == 0x300, last = (h[1] & 4) != 0;
        const uint32_t *next = h + 7 + (num - shared) * 4;

        if (!discard && alpha8 > 0 && p.a > 0.0f) {
            r3d_deferred d;
            d.p = p;
            d.n = num;
            for (int i = 0; i < num; ++i) {
                const r3d_prev *c = &cur[i];
                r3d_vtx *v = &d.v[i];
                v->x = M[0] * c->x + M[4] * c->y + M[8] * c->z + M[12];
                v->y = M[1] * c->x + M[5] * c->y + M[9] * c->z + M[13];
                v->z = M[2] * c->x + M[6] * c->y + M[10] * c->z + M[14];
                v->nx = (M[0] * c->nx + M[4] * c->ny + M[8] * c->nz) * inv_scale;
                v->ny = (M[1] * c->nx + M[5] * c->ny + M[9] * c->nz) * inv_scale;
                v->nz = (M[2] * c->nx + M[6] * c->ny + M[10] * c->nz) * inv_scale;
                v->shade = c->shade;
                v->u = (float)c->u * uv_scale / (float)tw;
                v->v = (float)c->v * uv_scale / (float)th;
            }
            const float vnx = M[0] * fnx + M[4] * fny + M[8] * fnz;
            const float vny = M[1] * fnx + M[5] * fny + M[9] * fnz;
            const float vnz = M[2] * fnx + M[6] * fny + M[10] * fnz;
            const float facing = d.v[0].x * vnx + d.v[0].y * vny + d.v[0].z * vnz;
            const bool double_sided = (h[1] & 0x10) != 0;
            if (facing <= 0.0f || double_sided) {
                if (facing > 0.0f)
                    for (int i = 0; i < num; ++i) {
                        d.v[i].nx = -d.v[i].nx;
                        d.v[i].ny = -d.v[i].ny;
                        d.v[i].nz = -d.v[i].nz;
                    }
                push_poly(g, vs->priority, &d);
                ++g->cur_polys;
            }
        }
        if (last) return;
        h = next;
    }
}

// ---------------------------------------------------------------------------
// Rasterizer

typedef struct {
    float sx, sy, iw, uw, vw, r, g, b, fog;
} r3d_rv;

static uint32_t *g_lut;

static uint32_t lut_c(float x) { return (uint32_t)(clampf(x, 0.0f, 1.0f) * 255.0f + 0.5f); }

static const uint32_t *texel_lut(void)
{
    if (g_lut) return g_lut;
    uint32_t *t = (uint32_t *)malloc(((size_t)12 << 16) * 4);
    if (!t) return NULL;
    for (int type = 0; type < 12; ++type)
        for (uint32_t v = 0; v < 65536; ++v) {
            float r = 0, gg = 0, b = 0, a = 0;
            switch (type) {
            case 0:
                r = (float)((v >> 10) & 31) / 31.0f;
                gg = (float)((v >> 5) & 31) / 31.0f;
                b = (float)(v & 31) / 31.0f;
                a = 1.0f - (float)((v >> 15) & 1);
                break;
            case 1: r = gg = b = (float)(v & 15) / 15.0f; a = (float)((v >> 4) & 15) / 15.0f; break;
            case 2: a = (float)(v & 15) / 15.0f; r = gg = b = (float)((v >> 4) & 15) / 15.0f; break;
            case 3: r = gg = b = (float)((v >> 8) & 15) / 15.0f; a = (float)((v >> 12) & 15) / 15.0f; break;
            case 4: a = (float)((v >> 8) & 15) / 15.0f; r = gg = b = (float)((v >> 12) & 15) / 15.0f; break;
            case 5:
                r = gg = b = (float)(v & 0xff) / 255.0f;
                a = (v & 0xff) == 0xff ? 0.0f : 1.0f;
                break;
            case 6:
                r = gg = b = (float)((v >> 8) & 0xff) / 255.0f;
                a = ((v >> 8) & 0xff) == 0xff ? 0.0f : 1.0f;
                break;
            case 7:
                r = (float)((v >> 12) & 15) / 15.0f;
                gg = (float)((v >> 8) & 15) / 15.0f;
                b = (float)((v >> 4) & 15) / 15.0f;
                a = (float)(v & 15) / 15.0f;
                break;
            default: {
                const uint32_t n = (v >> ((type - 8) * 4)) & 15;
                r = gg = b = (float)n / 15.0f;
                a = n == 15 ? 0.0f : 1.0f;
                break;
            }
            }
            t[((size_t)type << 16) | v] = lut_c(a) << 24 | lut_c(r) << 16 | lut_c(gg) << 8 | lut_c(b);
        }
    g_lut = t;
    return t;
}

static inline int wrap_texel(int i, int size, bool mirror)
{
    if (mirror) {
        const int m = i & (2 * size - 1);
        return m < size ? m : 2 * size - 1 - m;
    }
    return i & (size - 1);
}

static inline uint32_t lerp_rgba(uint32_t a, uint32_t b, uint32_t f)
{
    const uint32_t rb = ((((b & 0x00ff00ffu) * f) + ((a & 0x00ff00ffu) * (256 - f))) >> 8) & 0x00ff00ffu;
    const uint32_t ag = ((((b >> 8) & 0x00ff00ffu) * f + ((a >> 8) & 0x00ff00ffu) * (256 - f))) & 0xff00ff00u;
    return rb | ag;
}

typedef struct {
    r3d_vtx v;
    float r, g, b, fog;
} r3d_lv;

static void raster_poly(real3dd *g, const r3d_vtx *in, int n, const r3d_poly *p, bool translucent, int band_y0, int band_y1)
{
    const r3d_viewport *vs = &g->viewports[p->vp];
    const float kNear = 1.0f / 1024.0f;
    r3d_lv lv[4];
    for (int i = 0; i < n; ++i) {
        lv[i].v = in[i];
        float intensity = 1.0f;
        if (p->lighting) {
            float sf = p->fixed_shading ? in[i].shade : vs->sun[0] * in[i].nx + vs->sun[1] * in[i].ny + vs->sun[2] * in[i].nz;
            sf = clampf(sf, -1.0f, 1.0f);
            if (g->sun_clamp || p->a < 0.999f) sf = maxf(sf, 0.0f);
            intensity = sf * vs->sun_intensity + vs->ambient;
        }
        lv[i].r = lv[i].g = lv[i].b = intensity;
        const float z = -in[i].z;
        lv[i].fog = p->fog_intensity * clampf(vs->fog_start + z * vs->fog_density, 0.0f, 1.0f);
    }

    r3d_lv clipped[8];
    int cn = 0;
    for (int i = 0; i < n; ++i) {
        const r3d_lv *a = &lv[i], *b = &lv[(i + 1) % n];
        const bool ain = a->v.z <= -kNear, bin = b->v.z <= -kNear;
        if (ain) clipped[cn++] = *a;
        if (ain != bin) {
            const float t = (-kNear - a->v.z) / (b->v.z - a->v.z);
            r3d_lv c;
            memset(&c, 0, sizeof c);
#define LERP(x, y) ((x) + ((y) - (x)) * t)
            c.v.x = LERP(a->v.x, b->v.x);
            c.v.y = LERP(a->v.y, b->v.y);
            c.v.z = -kNear;
            c.v.u = LERP(a->v.u, b->v.u);
            c.v.v = LERP(a->v.v, b->v.v);
            c.r = LERP(a->r, b->r);
            c.g = LERP(a->g, b->g);
            c.b = LERP(a->b, b->b);
            c.fog = LERP(a->fog, b->fog);
#undef LERP
            clipped[cn++] = c;
        }
    }
    if (cn < 3) return;

    r3d_rv rv[8];
    const float rl = vs->r - vs->l, tb = vs->t - vs->b;
    for (int i = 0; i < cn; ++i) {
        const r3d_lv *c = &clipped[i];
        const float iw = 1.0f / -c->v.z;
        const float nx = (2.0f * c->v.x * iw - vs->r - vs->l) / rl;
        const float ny = (2.0f * c->v.y * iw - vs->t - vs->b) / tb;
        rv[i].sx = vs->x + (nx + 1.0f) * 0.5f * vs->w;
        rv[i].sy = vs->y + (1.0f - ny) * 0.5f * vs->h;
        rv[i].iw = iw;
        rv[i].uw = c->v.u * iw;
        rv[i].vw = c->v.v * iw;
        rv[i].r = c->r;
        rv[i].g = c->g;
        rv[i].b = c->b;
        rv[i].fog = c->fog;
        if (!isfinite(rv[i].sx) || !isfinite(rv[i].sy)) return;
    }

    const int clip_x0 = maxi(0, (int)floorf(vs->x));
    const int clip_y0 = maxi(band_y0, (int)floorf(vs->y));
    const int clip_x1 = mini(R3D_W, (int)ceilf(vs->x + vs->w));
    const int clip_y1 = mini(band_y1, (int)ceilf(vs->y + vs->h));
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return;

    const float fog_r = vs->fog_rgb[0] * vs->fog_ambient * 255.0f;
    const float fog_g = vs->fog_rgb[1] * vs->fog_ambient * 255.0f;
    const float fog_b = vs->fog_rgb[2] * vs->fog_ambient * 255.0f;
    const int page_base = (p->page & 1) * 1024;
    const uint32_t *lut = texel_lut() + ((size_t)p->format << 16);
    const uint16_t *tex = g->tex;
    const float base_r = p->r * 255.0f, base_g = p->g * 255.0f, base_b = p->b * 255.0f;
    const float tw = (float)p->tw, th = (float)p->th;
    int max_level = 0;
    for (int m = mini(p->tw, p->th); m > 4; m >>= 1) ++max_level;
    max_level = mini(max_level, 10);

    for (int t = 1; t + 1 < cn; ++t) {
        const r3d_rv *v0 = &rv[0], *v1 = &rv[t], *v2 = &rv[t + 1];
        const float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);
        if (!(fabsf(area) >= 1e-6f)) continue;
        const float inv_area = 1.0f / area;
        const float miny = minf(minf(v0->sy, v1->sy), v2->sy);
        const float maxy = maxf(maxf(v0->sy, v1->sy), v2->sy);
        const int y0 = maxi(clip_y0, (int)ceilf(miny - 0.5f));
        const int y1 = mini(clip_y1 - 1, (int)ceilf(maxy - 0.5f) - 1);
        if (y0 > y1) continue;

#define GRAD(a0, a1, a2, dx, dy)                                                              \
    do {                                                                                      \
        dx = (((a1) - (a0)) * (v2->sy - v0->sy) - ((a2) - (a0)) * (v1->sy - v0->sy)) * inv_area; \
        dy = (((a2) - (a0)) * (v1->sx - v0->sx) - ((a1) - (a0)) * (v2->sx - v0->sx)) * inv_area; \
    } while (0)
        float iw_dx, iw_dy, uw_dx, uw_dy, vw_dx, vw_dy, r_dx, r_dy, g_dx, g_dy, b_dx, b_dy, f_dx, f_dy;
        GRAD(v0->iw, v1->iw, v2->iw, iw_dx, iw_dy);
        GRAD(v0->uw, v1->uw, v2->uw, uw_dx, uw_dy);
        GRAD(v0->vw, v1->vw, v2->vw, vw_dx, vw_dy);
        GRAD(v0->r, v1->r, v2->r, r_dx, r_dy);
        GRAD(v0->g, v1->g, v2->g, g_dx, g_dy);
        GRAD(v0->b, v1->b, v2->b, b_dx, b_dy);
        GRAD(v0->fog, v1->fog, v2->fog, f_dx, f_dy);
#undef GRAD

        const float sign = area > 0 ? 1.0f : -1.0f;
        const r3d_rv *edges[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};

        for (int y = y0; y <= y1; ++y) {
            const float py = (float)y + 0.5f;
            float lo = (float)clip_x0, hi = (float)(clip_x1 - 1);
            bool empty = false;
            for (int e = 0; e < 3; e++) {
                const r3d_rv *a = edges[e][0], *b = edges[e][1];
                const float k = -(b->sy - a->sy) * sign;
                const float c = ((b->sx - a->sx) * (py - a->sy) + (b->sy - a->sy) * a->sx) * sign;
                if (k > 0) lo = maxf(lo, ceilf(-c / k - 0.5f));
                else if (k < 0) hi = minf(hi, floorf(-c / k - 0.5f));
                else if (c < 0) empty = true;
            }
            if (empty || lo > hi) continue;
            const int xa = (int)lo, xb = (int)hi;

            uint32_t *row = g->colour + (size_t)y * R3D_W;
            float *zrow = g->zbuf + (size_t)y * R3D_W;
            const float dx0 = (float)xa + 0.5f - v0->sx, dy0 = py - v0->sy;
            float iw = v0->iw + iw_dx * dx0 + iw_dy * dy0;
            float uw = v0->uw + uw_dx * dx0 + uw_dy * dy0;
            float vw = v0->vw + vw_dx * dx0 + vw_dy * dy0;
            float lr = v0->r + r_dx * dx0 + r_dy * dy0;
            float lg = v0->g + g_dx * dx0 + g_dy * dy0;
            float lb = v0->b + b_dx * dx0 + b_dy * dy0;
            float fg = v0->fog + f_dx * dx0 + f_dy * dy0;
            for (int x = xa; x <= xb; ++x, iw += iw_dx, uw += uw_dx, vw += vw_dx, lr += r_dx, lg += g_dx, lb += b_dx, fg += f_dx) {
                if (iw <= 0 || (translucent ? iw <= zrow[x] : iw < zrow[x])) continue;
                float cr = base_r, cg = base_g, cb = base_b, ca = p->a;
                if (p->textured) {
                    const float w = 1.0f / iw;
                    const float un = uw * w, vn = vw * w;
                    int level = 0;
                    if (max_level > 0) {
                        const float dudx = (uw_dx - un * iw_dx) * w * tw, dvdx = (vw_dx - vn * iw_dx) * w * th;
                        const float dudy = (uw_dy - un * iw_dy) * w * tw, dvdy = (vw_dy - vn * iw_dy) * w * th;
                        const float rho2 = maxf(dudx * dudx + dvdx * dvdx, dudy * dudy + dvdy * dvdy);
                        if (rho2 >= 4.0f && isfinite(rho2)) level = mini(max_level, ilogbf(rho2) >> 1);
                    }
                    const int lw = p->tw >> level, lh = p->th >> level;
                    const int bx = kMipXBase[level] + (p->tx >> level);
                    const int by = kMipYBase[level] + (p->ty >> level);
                    const int mx = 2047 >> level, my = 1023 >> level;
                    float u = un * (float)lw - 0.5f;
                    float v = vn * (float)lh - 0.5f;
                    u = clampf(u, -1.0e9f, 1.0e9f);
                    v = clampf(v, -1.0e9f, 1.0e9f);
                    const float fu = floorf(u), fv = floorf(v);
                    const int iu = (int)((int64_t)fu & 0x3fffffff), iv = (int)((int64_t)fv & 0x3fffffff);
                    const uint32_t wu = (uint32_t)((u - fu) * 256.0f), wv = (uint32_t)((v - fv) * 256.0f);
                    const int tx0 = kMipXBase[level] + ((bx - kMipXBase[level] + wrap_texel(iu, lw, p->mirror_u)) & mx);
                    const int tx1 = kMipXBase[level] + ((bx - kMipXBase[level] + wrap_texel(iu + 1, lw, p->mirror_u)) & mx);
                    const int ty0 = page_base + kMipYBase[level] + ((by - kMipYBase[level] + wrap_texel(iv, lh, p->mirror_v)) & my);
                    const int ty1 = page_base + kMipYBase[level] + ((by - kMipYBase[level] + wrap_texel(iv + 1, lh, p->mirror_v)) & my);
                    const uint32_t t00 = lut[tex[(size_t)(ty0 & 2047) * 2048 + (size_t)(tx0 & 2047)]];
                    const uint32_t t10 = lut[tex[(size_t)(ty0 & 2047) * 2048 + (size_t)(tx1 & 2047)]];
                    const uint32_t t01 = lut[tex[(size_t)(ty1 & 2047) * 2048 + (size_t)(tx0 & 2047)]];
                    const uint32_t t11 = lut[tex[(size_t)(ty1 & 2047) * 2048 + (size_t)(tx1 & 2047)]];
                    const uint32_t tc = lerp_rgba(lerp_rgba(t00, t10, wu), lerp_rgba(t01, t11, wu), wv);
                    float ta = (float)(tc >> 24) * (1.0f / 255.0f);
                    if (p->alpha_test && ta < 32.0f / 255.0f) continue;
                    if (p->texture_alpha) {
                        if (!translucent) {
                            if (ta < 1.0f) continue;
                        } else if (ta * p->a >= 1.0f) continue;
                    }
                    if (!p->texture_alpha && !p->alpha_test) ta = 1.0f;
                    const uint32_t trgb = p->inverted ? ~tc : tc;
                    cr *= (float)((trgb >> 16) & 0xff) * (1.0f / 255.0f);
                    cg *= (float)((trgb >> 8) & 0xff) * (1.0f / 255.0f);
                    cb *= (float)(trgb & 0xff) * (1.0f / 255.0f);
                    ca *= ta;
                }
                if (ca < 1.0f / 32.0f) continue;
                if (p->lighting) {
                    cr *= lr;
                    cg *= lg;
                    cb *= lb;
                }
                cr = clampf(cr, 0.0f, 255.0f);
                cg = clampf(cg, 0.0f, 255.0f);
                cb = clampf(cb, 0.0f, 255.0f);
                const float fog = clampf(fg, 0.0f, 1.0f);
                if (fog > 0) {
                    cr += (fog_r - cr) * fog;
                    cg += (fog_g - cg) * fog;
                    cb += (fog_b - cb) * fog;
                }
                if (!translucent) {
                    zrow[x] = iw;
                    row[x] = 0xff000000u | (uint32_t)(cr + 0.5f) << 16 | (uint32_t)(cg + 0.5f) << 8 | (uint32_t)(cb + 0.5f);
                } else {
                    const float a = minf(ca, 1.0f), ia = 1.0f - a;
                    const uint32_t d = row[x];
                    const float oa = a * 255.0f + (float)(d >> 24) * ia;
                    const float orr = cr * a + (float)((d >> 16) & 0xff) * ia;
                    const float og = cg * a + (float)((d >> 8) & 0xff) * ia;
                    const float ob = cb * a + (float)(d & 0xff) * ia;
                    row[x] = (uint32_t)(minf(oa, 255.0f) + 0.5f) << 24 | (uint32_t)(minf(orr, 255.0f) + 0.5f) << 16 |
                             (uint32_t)(minf(og, 255.0f) + 0.5f) << 8 | (uint32_t)(minf(ob, 255.0f) + 0.5f);
                }
            }
        }
    }
}

void r3d_prepare(real3dd *g)
{
    g->stats_polys = 0;
    g->stats_viewports = 0;
    g->cur_polys = 0;
    g->n_vp = 0;
    for (int i = 0; i < 4; i++) g->n_polys[i] = 0;
    g->prepared_blocked = g->block_culling;
    if (g->block_culling) return;
    g->sun_clamp = (g->modeword[3] & 0x40000) == 0;
    for (int pri = 0; pri < 4; ++pri) render_viewport(g, 0x800000, pri, 0);
    for (int i = 0; i < 4; i++) g->stats_polys += (int)g->n_polys[i];
}

void r3d_rasterize(real3dd *g, uint32_t *out)
{
    const size_t npx = (size_t)R3D_W * R3D_H;
    memset(out, 0, npx * 4);
    memset(g->colour, 0, npx * 4);
    if (g->prepared_blocked) return;

    for (int pri = 0; pri < 4; ++pri) {
        const r3d_viewport *best = NULL;
        for (size_t i = 0; i < g->n_vp; i++) {
            const r3d_viewport *vs = &g->viewports[i];
            if (vs->priority == pri && vs->scroll_fog > 0 && (!best || vs->scroll_fog > best->scroll_fog)) best = vs;
        }
        if (!best) continue;
        const float a = best->scroll_fog;
        const uint32_t px = (uint32_t)(a * 255.0f + 0.5f) << 24 |
                            (uint32_t)(best->fog_rgb[0] * best->fog_ambient * a * 255.0f + 0.5f) << 16 |
                            (uint32_t)(best->fog_rgb[1] * best->fog_ambient * a * 255.0f + 0.5f) << 8 |
                            (uint32_t)(best->fog_rgb[2] * best->fog_ambient * a * 255.0f + 0.5f);
        const int x0 = maxi(0, (int)best->x), x1 = mini(R3D_W, (int)(best->x + best->w));
        const int y0 = maxi(0, (int)best->y), y1 = mini(R3D_H, (int)(best->y + best->h));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) g->colour[(size_t)y * R3D_W + (size_t)x] = px;
        break;
    }

    const int y0 = 0, y1 = R3D_H;
    for (int pri = 0; pri < 4; ++pri) {
        const r3d_deferred *list = g->polys[pri];
        const size_t n = g->n_polys[pri];
        if (!n) continue;
        for (int overlay = 0; overlay < 2; ++overlay) {
            for (size_t i = (size_t)y0 * R3D_W; i < (size_t)y1 * R3D_W; i++) g->zbuf[i] = 0.0f;
            bool any = false;
            for (size_t i = 0; i < n; i++) {
                const r3d_deferred *d = &list[i];
                if (d->p.high_priority != (overlay == 1)) continue;
                any = true;
                if (d->p.poly_alpha || d->p.node_alpha) continue;
                raster_poly(g, d->v, d->n, &d->p, false, y0, y1);
            }
            if (!any) continue;
            for (size_t i = 0; i < n; i++) {
                const r3d_deferred *d = &list[i];
                if (d->p.high_priority != (overlay == 1)) continue;
                if (!d->p.texture_alpha && !d->p.poly_alpha && !d->p.node_alpha) continue;
                raster_poly(g, d->v, d->n, &d->p, true, y0, y1);
            }
        }
    }
    memcpy(out, g->colour, npx * 4);
}
