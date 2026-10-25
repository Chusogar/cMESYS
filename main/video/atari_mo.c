// atari_mo.c - Atari motion objects. C port of dsp-cpp's video/atari_mo.cpp.
#include "atari_mo.h"

#include <stdlib.h>
#include <string.h>

#define NO_HOLD 123456u
#define MAX_PER_BANK 1024

static int compute_log(int value)
{
    if (value == 0) return -1;
    int log = 0;
    while ((value & 1) == 0) { log++; value >>= 1; }
    return value != 1 ? -1 : log;
}

static int round_to_powerof2(int value)
{
    if (value == 0) return 1;
    int log = 0;
    value >>= 1;
    while (value != 0) { log++; value >>= 1; }
    return 1 << (log + 1);
}

static void param_set(atari_mo_param *p, const atari_mo_entry in)
{
    p->word = 0xffff;
    for (uint16_t f = 0; f < 4; ++f)
        if (in[f] != 0) {
            if (p->word != 0xffff) return;
            p->word = f;
        }
    if (p->word == 0xffff) { p->word = 0; p->shift = 0; p->mask = 0; return; }
    p->shift = 0;
    uint16_t t = in[p->word];
    while ((t & 1) == 0) { p->shift++; t >>= 1; }
    p->mask = t;
}

static inline uint16_t param_get(const atari_mo_param *p, const uint16_t *d)
{
    return (uint16_t)((d[p->word] >> p->shift) & p->mask);
}

static void dual_set(atari_mo_dual_param *p, const atari_mo_dual_entry *in)
{
    param_set(&p->lower, in->lower);
    param_set(&p->upper, in->upper);
    uint16_t t = p->lower.mask;
    p->uppershift = 0;
    while (t != 0) { p->uppershift++; t >>= 1; }
}

static inline uint16_t dual_get(const atari_mo_dual_param *p, const uint16_t *d)
{
    return (uint16_t)(param_get(&p->lower, d) | (param_get(&p->upper, d) << p->uppershift));
}

static inline uint16_t dual_mask(const atari_mo_dual_param *p)
{
    return (uint16_t)(p->lower.mask | (p->upper.mask << p->uppershift));
}

void atari_mo_init(atari_mo *m, const atari_mo_config *cfg, int xmax, int ymax)
{
    memset(m, 0, sizeof *m);
    m->cfg = *cfg;
    m->xmax = xmax;
    m->ymax = ymax;
    param_set(&m->linkmask, cfg->link_entry);
    dual_set(&m->codemask, &cfg->code_entry);
    dual_set(&m->colormask, &cfg->color_entry);
    param_set(&m->xposmask, cfg->xpos_entry);
    param_set(&m->yposmask, cfg->ypos_entry);
    param_set(&m->widthmask, cfg->width_entry);
    param_set(&m->heightmask, cfg->height_entry);
    param_set(&m->hflipmask, cfg->hflip_entry);
    param_set(&m->vflipmask, cfg->vflip_entry);
    param_set(&m->prioritymask, cfg->priority_entry);
    param_set(&m->neighbormask, cfg->neighbor_entry);
    param_set(&m->absolutemask, cfg->absolute_entry);
    param_set(&m->specialmask, cfg->special_entry);
    m->tilexshift = compute_log(cfg->tile_width);
    m->tileyshift = compute_log(cfg->tile_height);
    m->bitmapxmask = round_to_powerof2(m->xposmask.mask) - 1;
    m->bitmapymask = round_to_powerof2(m->yposmask.mask) - 1;
    m->entrybits = compute_log(round_to_powerof2(m->linkmask.mask));
    m->list = m->active_list;
    m->bitmapwidth = m->bitmapxmask + 1;
    m->bitmapheight = m->bitmapymask + 1;
    m->slipshift = cfg->slipheight ? compute_log(cfg->slipheight) : 0;
    if (cfg->slipheight && m->cfg.maxperline == 0) {
        // MAME: every object (MAX_PER_BANK)
        m->list = (uint16_t *)malloc(MAX_PER_BANK * 4 * sizeof(uint16_t));
        if (m->list) m->cfg.maxperline = MAX_PER_BANK;
        else m->list = m->active_list;
    }
    if (m->cfg.maxperline == 0 || (m->list == m->active_list && m->cfg.maxperline > ATARI_MO_MAX_PER_LINE))
        m->cfg.maxperline = ATARI_MO_MAX_PER_LINE;
    (void)dual_mask;
}

static void build_active_list(atari_mo *m, const uint16_t *sprite_ram, uint16_t link)
{
    const uint16_t *bankbase = sprite_ram + ((size_t)m->bank << (m->entrybits + 2));
    int current = 0;
    uint8_t visited[MAX_PER_BANK / 8];
    memset(visited, 0, sizeof visited);
    for (int f = 0; f < m->cfg.maxperline; ++f) {
        int entry_start = current;
        if (!m->cfg.split) {
            const uint16_t *src = bankbase + (size_t)link * 4;
            for (int i = 0; i < 4; ++i) m->list[current++] = src[i];
        } else {
            const uint16_t *src = bankbase + link;
            for (int i = 0; i < 4; ++i) m->list[current++] = src[(size_t)i << m->entrybits];
        }
        visited[(link & (MAX_PER_BANK - 1)) >> 3] |= (uint8_t)(1 << (link & 7));
        link = m->cfg.linked ? param_get(&m->linkmask, &m->list[entry_start])
                             : (uint16_t)((link + 1) & m->linkmask.mask);
        if (visited[(link & (MAX_PER_BANK - 1)) >> 3] & (1 << (link & 7))) break;
    }
    m->active_last = current;
}

static void render_object(atari_mo *m, const uint16_t *e, int xscroll, int yscroll, int prio,
                          const atari_mo_lookup *lk, atari_mo_draw_tile draw_tile, void *user)
{
    const int priority = param_get(&m->prioritymask, e);
    if (priority != prio && prio != -1) return;

    const uint16_t rawcode = dual_get(&m->codemask, e);
    int code = lk->code(user, rawcode);
    int color = lk->color(user, dual_get(&m->colormask, e)) << 4;
    int gfx = lk->gfx ? lk->gfx(user, rawcode >> 8) : 0;
    int xpos = param_get(&m->xposmask, e);
    int ypos = -(int)param_get(&m->yposmask, e);
    const bool hflip = param_get(&m->hflipmask, e) != 0;
    const bool vflip = param_get(&m->vflipmask, e) != 0;
    const int width = param_get(&m->widthmask, e) + 1;
    const int height = param_get(&m->heightmask, e) + 1;

    color += m->cfg.palettebase;
    if (param_get(&m->absolutemask, e) == 0) {
        xpos -= xscroll;
        ypos -= yscroll;
    }
    ypos -= height << m->tileyshift;

    if (m->next_xpos != NO_HOLD) xpos = (int)m->next_xpos;
    m->next_xpos = NO_HOLD;
    if (param_get(&m->neighbormask, e) != 0) {
        if (!m->cfg.nextneighbor) xpos = (int)m->last_xpos + m->cfg.tile_width;
        else m->next_xpos = (uint32_t)(xpos + m->cfg.tile_width);
    }
    m->last_xpos = (uint32_t)xpos;

    xpos &= m->bitmapxmask;
    ypos &= m->bitmapymask;

    if (m->specialmask.mask != 0 && param_get(&m->specialmask, e) == m->cfg.specialvalue) return;

    int xadv = m->cfg.tile_width;
    if (hflip) {
        xpos += (width - 1) << m->tilexshift;
        xadv = -xadv;
    }
    int yadv = m->cfg.tile_height;
    if (vflip) {
        ypos += (height - 1) << m->tileyshift;
        yadv = -yadv;
    }
#define VISIBLE(sx, sy) (((sx) < m->xmax || (sx) > 503) && ((sy) < m->ymax || (sy) > 503))
    if (!m->cfg.swapxy) {
        int sy = ypos;
        for (int y = 0; y < height; ++y) {
            int sx = xpos;
            for (int x = 0; x < width; ++x) {
                if (VISIBLE(sx, sy)) draw_tile(user, code, color, hflip, vflip, sx, sy, gfx, priority);
                sx += xadv;
                code++;
            }
            sy += yadv;
        }
    } else {
        int sx = xpos;
        for (int x = 0; x < width; ++x) {
            int sy = ypos;
            for (int y = 0; y < height; ++y) {
                if (VISIBLE(sx, sy)) draw_tile(user, code, color, hflip, vflip, sx, sy, gfx, priority);
                sy += yadv;
                code++;
            }
            sx += xadv;
        }
    }
#undef VISIBLE
}

void atari_mo_draw(atari_mo *m, const uint16_t *sprite_ram, int xscroll, int yscroll, int prio,
                   const atari_mo_lookup *lk, atari_mo_draw_tile draw_tile, void *user)
{
    build_active_list(m, sprite_ram, 0);
    m->next_xpos = NO_HOLD;
    if (m->active_last < 4) return;
    if (m->cfg.reverse) {
        for (int cur = m->active_last - 4;; cur -= 4) {
            render_object(m, &m->list[cur], xscroll, yscroll, prio, lk, draw_tile, user);
            if (cur == 0) break;
        }
    } else {
        for (int cur = 0;; cur += 4) {
            render_object(m, &m->list[cur], xscroll, yscroll, prio, lk, draw_tile, user);
            if (cur + 4 >= m->active_last) break;
        }
    }
}

// ---------------------------------------------------------------------------
// SLIP bands (MAME atari_motion_objects_device::draw / render_object)
// ---------------------------------------------------------------------------
static void render_object_clip(atari_mo *m, const uint16_t *e, int xscroll, int yscroll, int cy0, int cy1,
                               const atari_mo_lookup *lk, atari_mo_draw_tile_clip draw_tile, void *user)
{
    const uint16_t rawcode = dual_get(&m->codemask, e);
    int code = lk->code(user, rawcode);
    int color = (lk->color(user, dual_get(&m->colormask, e)) << 4) + m->cfg.palettebase;
    int gfx = lk->gfx ? lk->gfx(user, rawcode >> 8) : 0;
    int xpos = param_get(&m->xposmask, e);
    int ypos = -(int)param_get(&m->yposmask, e);
    const bool hflip = param_get(&m->hflipmask, e) != 0;
    const bool vflip = param_get(&m->vflipmask, e) != 0;
    const int width = param_get(&m->widthmask, e) + 1;
    const int height = param_get(&m->heightmask, e) + 1;
    const int priority = param_get(&m->prioritymask, e);
    const int tw = m->cfg.tile_width, th = m->cfg.tile_height;
    if (param_get(&m->absolutemask, e) == 0) { xpos -= xscroll; ypos -= yscroll; }
    ypos -= height << m->tileyshift;
    if (m->next_xpos != NO_HOLD) xpos = (int)m->next_xpos;
    m->next_xpos = NO_HOLD;
    if (param_get(&m->neighbormask, e) != 0) {
        if (!m->cfg.nextneighbor) xpos = (int)m->last_xpos + tw;
        else m->next_xpos = (uint32_t)(xpos + tw);
    }
    m->last_xpos = (uint32_t)xpos;
    xpos &= m->bitmapxmask;
    ypos &= m->bitmapymask;
    if (xpos >= m->xmax) xpos -= m->bitmapwidth;
    if (ypos >= m->ymax) ypos -= m->bitmapheight;
    if (m->specialmask.mask != 0 && param_get(&m->specialmask, e) == m->cfg.specialvalue) return;
    int xadv = tw, yadv = th;
    if (hflip) { xpos += (width - 1) << m->tilexshift; xadv = -xadv; }
    if (vflip) { ypos += (height - 1) << m->tileyshift; yadv = -yadv; }
    for (int y = 0, sy = ypos; y < height; y++, sy += yadv) {
        if (sy <= cy0 - th) { code += width; continue; }
        else if (sy > cy1) break;
        for (int x = 0, sx = xpos; x < width; x++, sx += xadv, code++) {
            if (sx <= -tw || sx >= m->xmax) continue;
            draw_tile(user, code, color, hflip, vflip, sx, sy, gfx, priority, cy0, cy1);
        }
    }
}

void atari_mo_draw_slip(atari_mo *m, const uint16_t *sprite_ram, const uint16_t *slip_ram,
                        int xscroll, int yscroll, int y0, int y1,
                        const atari_mo_lookup *lk, atari_mo_draw_tile_clip draw_tile, void *user)
{
    const int off = m->cfg.slipoffset;
    int startband = ((y0 + yscroll - off) & m->bitmapymask) >> m->slipshift;
    int stopband = ((y1 + yscroll - off) & m->bitmapymask) >> m->slipshift;
    if (startband > stopband) startband -= m->bitmapheight >> m->slipshift;
    if (m->slipshift == 0) stopband = startband;
    const int sliprammask = (m->bitmapheight >> m->slipshift) - 1;
    for (int band = startband; band <= stopband; band++) {
        int cy0 = y0, cy1 = y1, link = 0;
        if (m->slipshift) {
            link = (slip_ram[band & sliprammask] >> m->linkmask.shift) & m->linkmask.mask;
            int top = ((band << m->slipshift) - yscroll + off) & m->bitmapymask;
            if (top >= m->ymax) top -= m->bitmapheight;
            int bottom = top + (1 << m->slipshift) - 1;
            if (top > cy0) cy0 = top;
            if (bottom < cy1) cy1 = bottom;
            if (cy0 > cy1) continue;
        }
        build_active_list(m, sprite_ram, (uint16_t)link);
        m->next_xpos = NO_HOLD;
        if (m->active_last < 4) continue;
        if (m->cfg.reverse) {
            for (int cur = m->active_last - 4;; cur -= 4) {
                render_object_clip(m, &m->list[cur], xscroll, yscroll, cy0, cy1, lk, draw_tile, user);
                if (cur == 0) break;
            }
        } else {
            for (int cur = 0;; cur += 4) {
                render_object_clip(m, &m->list[cur], xscroll, yscroll, cy0, cy1, lk, draw_tile, user);
                if (cur + 4 >= m->active_last) break;
            }
        }
    }
}
