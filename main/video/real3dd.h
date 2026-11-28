// real3dd.h - Real3D Pro-1000 (Sega Model 3 Step 2.x 3D board): C port of
// dsp-cpp's video/real3d: the culling / polygon / texture memories, the
// ping-pong and update-buffer protocol, DMA, the JTAG chain of the ASICs and
// the software renderer of the scene graph (viewports, culling nodes with
// matrices, LOD, models, sun lighting, fog, 12 texel formats, translucency).
// All memories hold the board's "logical" (byte reversed) 32-bit words.
#ifndef REAL3DD_H
#define REAL3DD_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define R3D_W 496
#define R3D_H 384

typedef struct {
    float x, y, z, nx, ny, nz, u, v, shade;
} r3d_vtx;

typedef struct {
    float r, g, b, a;
    bool textured;
    int tx, ty, tw, th, page, format;
    bool mirror_u, mirror_v, alpha_test, texture_alpha, poly_alpha, node_alpha;
    bool lighting, fixed_shading, high_priority, inverted;
    float fog_intensity;
    int vp;
} r3d_poly;

typedef struct {
    r3d_poly p;
    r3d_vtx v[4];
    int n;
} r3d_deferred;

typedef struct {
    float x, y, w, h, l, r, b, t;
    float sun[3], sun_intensity, ambient;
    float fog_rgb[3], fog_density, fog_start, fog_ambient, scroll_fog;
    int priority;
} r3d_viewport;

typedef struct { uint32_t index, value; } r3d_write;

typedef struct {
    bool asic;
    int asic_index;
    uint32_t idcode;
    uint8_t ir;
    int size;
    uint64_t shift;
} r3d_jtag;

typedef struct { float m[16]; } r3d_mat;

typedef struct real3dd {
    // host
    uint32_t (*read32)(void *u, uint32_t a);
    void (*write32)(void *u, uint32_t a, uint32_t v);
    void (*dma_irq)(void *u, bool on);
    void (*before_texture_write)(void *u);
    void *user;

    uint32_t *cull_lo, *cull_hi, *poly;     // 1M, 256K, 1M words
    uint16_t *tex;                          // 2048 x 2048
    uint32_t *fifo;
    size_t fifo_n;
    const uint8_t *vrom;
    size_t vrom_words;
    uint16_t *scratch;                      // texture upload source (2M texels)

    r3d_write *pending_hi, *pending_poly;
    size_t n_hi, cap_hi, n_poly, cap_poly;
    uint32_t config[4], vrom_fifo[2];
    int vrom_fifo_idx;

    bool ping_pong, ping_pong_copy, command_written, tilegen_draw, frame_ready, block_culling;
    uint32_t dma_src, dma_dst, dma_len, dma_data;
    uint8_t dma_status, dma_config;

    r3d_jtag jtag[17];
    uint32_t modeword[5];
    int jtag_state;
    bool jtag_last_tck, jtag_tdo;

    // renderer
    float *zbuf;
    uint32_t *colour;
    r3d_mat mat;
    const uint32_t *matrix_base, *lod_table;
    uint32_t color_table;
    float model_scale, model_alpha;
    bool disable_culling;
    int tex_off_x, tex_off_y, tex_page;
    float planes[8];
    bool sun_clamp, prepared_blocked;
    r3d_viewport *viewports;
    size_t n_vp, cap_vp;
    r3d_deferred *polys[4];
    size_t n_polys[4], cap_polys[4];
    int stats_polys, stats_viewports, stats_uploads, cur_polys;
} real3dd;

bool r3d_init(real3dd *g);
void r3d_reset(real3dd *g);
static inline void r3d_set_vrom(real3dd *g, const uint8_t *d, size_t size) { g->vrom = d; g->vrom_words = size / 4; }

uint32_t r3d_read_register(real3dd *g, unsigned reg);
void r3d_flush(real3dd *g);
void r3d_write_culling_low(real3dd *g, uint32_t offset, uint32_t value);
void r3d_write_culling_high(real3dd *g, uint32_t offset, uint32_t value);
void r3d_write_texture_port(real3dd *g, uint32_t value);
void r3d_write_texture_fifo(real3dd *g, uint32_t value);
void r3d_write_polygon_ram(real3dd *g, uint32_t offset, uint32_t value);
static inline void r3d_write_config(real3dd *g, uint32_t reg, uint32_t v) { g->config[(reg & 0xf) / 4] = v; }
uint8_t r3d_read_dma8(real3dd *g, unsigned reg);
void r3d_write_dma8(real3dd *g, unsigned reg, uint8_t value);
uint32_t r3d_read_dma32(real3dd *g, unsigned reg);
void r3d_write_dma32(real3dd *g, unsigned reg, uint32_t value);
uint32_t r3d_pci_config_read(real3dd *g, unsigned reg);
void r3d_jtag_write(real3dd *g, bool tck, bool tms, bool tdi, bool trst);
void r3d_begin_vblank(real3dd *g);
void r3d_flip_ping_pong(real3dd *g);
void r3d_tilegen_draw_frame(real3dd *g);
// prepare(): walks the scene graph; rasterize(): draws the prepared lists
// into out (ARGB, alpha 0 = no 3D pixel, colour premultiplied).
void r3d_prepare(real3dd *g);
void r3d_rasterize(real3dd *g, uint32_t *out);

#endif
