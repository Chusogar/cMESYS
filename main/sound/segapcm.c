// segapcm.c - Sega 315-5218 PCM, see segapcm.h. Register map (MAME):
//   voice n at n*8 (+0x80 for the second half), only these are not plain RAM:
//   +02 left volume, +03 right volume, +04/05 loop address (bits 8-23),
//   +06 end address (bits 16-23), +07 address step,
//   +84/85 current address (bits 8-23), +86 control (bit 0 off, bit 1 no loop,
//   bits 4-6 bank).
#include "segapcm.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PCM_HOT IRAM_ATTR
#else
#define PCM_HOT
#endif

void segapcm_init(segapcm *p)
{
    uint8_t (*rd)(void *, uint32_t) = p->read_rom;
    void *u = p->user;
    memset(p, 0, sizeof *p);
    p->read_rom = rd;
    p->user = u;
    memset(p->ram, 0xff, sizeof p->ram);
    // the voices start with all their bits set, as MAME's (stopped)
    for (int i = 0; i < 16; i++) {
        segapcm_voice *v = &p->v[i];
        v->addr = 0xffff00;
        v->loop = 0xffff;
        v->end = v->freq = v->lvol = v->rvol = v->ctrl = 0xff;
    }
}

uint8_t segapcm_read(segapcm *p, uint8_t off)
{
    segapcm_voice *v = &p->v[(off >> 3) & 15];
    switch (off & 0x87) {
    case 0x02: return v->lvol;
    case 0x03: return v->rvol;
    case 0x04: return (uint8_t)v->loop;
    case 0x05: return (uint8_t)(v->loop >> 8);
    case 0x06: return v->end;
    case 0x07: return v->freq;
    case 0x84: return (uint8_t)(v->addr >> 8);
    case 0x85: return (uint8_t)(v->addr >> 16);
    case 0x86: return v->ctrl;
    default: return p->ram[off];
    }
}

void segapcm_write(segapcm *p, uint8_t off, uint8_t data)
{
    segapcm_voice *v = &p->v[(off >> 3) & 15];
    switch (off & 0x87) {
    case 0x02: v->lvol = data; break;
    case 0x03: v->rvol = data; break;
    case 0x04: v->loop = (uint16_t)((v->loop & 0xff00) | data); break;
    case 0x05: v->loop = (uint16_t)((v->loop & 0x00ff) | (data << 8)); break;
    case 0x06: v->end = data; break;
    case 0x07: v->freq = data; break;
    case 0x84: v->addr = (v->addr & ~0x00ff00u) | ((uint32_t)data << 8); break;
    case 0x85: v->addr = (v->addr & ~0xff0000u) | ((uint32_t)data << 16); break;
    case 0x86: v->ctrl = data; break;
    default: p->ram[off] = data; break;
    }
}

void PCM_HOT segapcm_tick(segapcm *p)
{
    int32_t l = 0, r = 0;
    for (int i = 0; i < 16; i++) {
        segapcm_voice *v = &p->v[i];
        if (v->ctrl & 1) { v->addr &= 0xffff00; continue; }
        if ((v->addr >> 16) == ((v->end + 1u) & 0xff)) {
            if (v->ctrl & 2) { v->ctrl |= 1; continue; }
            v->addr = (uint32_t)v->loop << 8;
        }
        const uint32_t bank = (uint32_t)(v->ctrl & 0x70) << 12;
        const int s = (int)p->read_rom(p->user, bank + (v->addr >> 8)) - 0x80;
        l += s * (v->lvol & 0x7f);
        r += s * (v->rvol & 0x7f);
        v->addr = (v->addr + v->freq) & 0xffffff;
    }
    p->out_l = l;
    p->out_r = r;
}

bool segapcm_active(const segapcm *p)
{
    for (int i = 0; i < 16; i++)
        if (!(p->v[i].ctrl & 1)) return true;
    return false;
}
