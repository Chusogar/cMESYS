// sega16d.h - Sega System 16 era video: a C port of dsp-cpp's video/sega16
// (from dsp-emulator's system16 units / MAME segaic16): the tilemaps and text
// layer of the 16A / 16B / Hang-On boards, the palette with its shadow and
// highlight banks, the sprite generators (16A, Hang-On, Space Harrier, 16B,
// Out Run) and the Hang-On / Out Run roads. Colours are 0xAARRGGBB; the
// drivers turn their frame into RGB565.
#ifndef SEGA16D_H
#define SEGA16D_H
#include <stdbool.h>
#include <stdint.h>

#define S16_W 320
#define S16_H 224
#define S16_MAP_W 1024
#define S16_MAP_H 512
#define S16_TEXT_W 512

uint32_t s16_argb(uint8_t r, uint8_t g, uint8_t b);
uint32_t s16_mix_shadow(uint32_t dest, uint32_t shadow);
void s16_build_palette_luts(uint8_t normal[32], uint8_t shadow[32], uint8_t hilight[32]);
static inline uint16_t s16_to565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
}

typedef struct {
    const uint8_t *tiles;                       // decoded 8x8 tiles, one byte a pixel
    int tiles_total;
    uint8_t normal[32], shadow[32], hilight[32];
    uint32_t palette[0x2001];
    uint16_t pal_ram[0x1000], tile_ram[0x8000], char_ram[0x800], sprite_ram[0x800];
    bool tile_dirty[16][0x800], text_dirty[0x800];
    uint8_t screens[16];                        // the primary pages in 0-7, the alternate sets in 8-15
    uint8_t tile_bank[8], sprite_bank[16];
    bool screen_enabled;
    uint8_t tile_banks;
    int bank_size;                              // 0x1000 (16B), 0x400 (System 18)
    int cram_words;                             // where the shadow / highlight bank starts
} s16video;

// tiles: the 3 bpp 8x8 set of n * 0x1000 tiles (malloc'd), NULL: no memory
uint8_t *s16_decode_tiles(const uint8_t *rom, int n);

void s16_init(s16video *v);                     // (bank_size 0x1000, cram_words 0x800, the LUTs)
void s16_reset(s16video *v);
void s16_set_palette_entry(s16video *v, int index, uint16_t value, bool split_shadow);
void s16_mark_tile(s16video *v, uint16_t word_offset);
void s16_screen_select_16b(s16video *v, uint16_t char_offset);   // $740-$743, 4 bit
void s16_screen_select_16a(s16video *v, uint16_t char_offset);   // $74e / $74f, 3 bit
void s16_screen_select_hangon(s16video *v, uint16_t char_offset);  // $74e / $74f, 2 bit
int s16_resolve_tile_code(const s16video *v, int code);

// low / high: S16_MAP_W x S16_MAP_H; text: S16_TEXT_W x 256 (0 transparent)
void s16_render_tile_pages(s16video *v, uint32_t *low, uint32_t *high, int first_page, bool transparent,
                           int color_shift, int code_mask, int pri_mask, bool use_tile_bank, bool extra_code_bit);
void s16_render_text(s16video *v, uint32_t *low, uint32_t *high, int color_shift, int code_mask, int pri_mask,
                     bool use_tile_bank);
void s16_blit_scrolled(const s16video *v, uint32_t *dest, const uint32_t *src, int scroll_x, int scroll_y,
                       int src_w, int src_h);
void s16_blit_text(const s16video *v, uint32_t *dest, const uint32_t *src);
// The live 16B / 18 tilemap (row / column scroll, alternate pages): which 0
// foreground, 1 background; category -1 both, else 0 / 1.
void s16_draw_tilemap_16b(const s16video *v, uint32_t *dest, uint8_t *priority, int which, int category,
                          uint8_t pri_mark, bool opaque, int color_shift, int code_mask, bool use_tile_bank);
void s16_draw_text_16b(const s16video *v, uint32_t *dest, uint8_t *priority, int category, uint8_t pri_mark,
                       int color_shift, int code_mask, bool use_tile_bank);

void s16_draw_sprites_16a(s16video *v, uint32_t *dest, const uint16_t *rom, uint32_t rom_words, int banks, int pri,
                          int pal_base, uint32_t shadow_index);
void s16_draw_sprites_hangon(s16video *v, uint32_t *dest, const uint16_t *rom, uint32_t rom_words,
                             const uint8_t *zoom, uint32_t zoom_size, int banks, int pri);
void s16_draw_sprites_sharrier(s16video *v, uint32_t *dest, const uint32_t *rom, uint32_t rom_dwords,
                               const uint8_t *zoom, uint32_t zoom_size, int banks, int pri);
void s16_draw_sprites_16b(s16video *v, uint32_t *dest, const uint16_t *rom, uint32_t rom_words, int banks, int pri,
                          uint32_t shadow_index);
void s16_mix_sprites_16b(s16video *v, uint32_t *dest, uint8_t *priority, const uint16_t *rom, uint32_t rom_words,
                         int banks, uint32_t shadow_index);
void s16_draw_sprites_outrun(s16video *v, uint32_t *dest, const uint32_t *rom, uint32_t rom_dwords, int banks,
                             int pri, uint32_t shadow_index);

// road_gfx: 256 * 2 * 512 + 512 bytes (Out Run), 256 * 512 (Hang-On)
void s16_decode_outrun_road(uint8_t *road_gfx, const uint8_t *rom, uint32_t rom_size);
void s16_decode_hangon_road(uint8_t *road_gfx, const uint8_t *rom, uint32_t rom_size);
void s16_draw_outrun_road(uint32_t *dest, const uint32_t *palette, const uint16_t *buffer, const uint8_t *road_gfx,
                          uint8_t control, uint16_t colorbase1, uint16_t colorbase2, uint16_t colorbase3,
                          uint16_t xoff, int pri);
void s16_draw_hangon_road(uint32_t *dest, const uint32_t *palette, const uint16_t *road_ram, const uint8_t *road_gfx,
                          uint16_t colorbase1, uint16_t colorbase2, int pri, bool sharrier);

#endif
