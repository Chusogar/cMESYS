// specnext_core.c - ZX Spectrum Next (see specnext_core.h): C port of
// dsp-cpp's drivers/computers/specnext.cpp and specnext_video.cpp.
//
// Hardware behaviour follows the TBBlue / ZX Next core (zxnext.vhd) as
// modelled by MAME's specnext driver (BSD-3-Clause, Andrei I. Holub, thanks
// to Kev Brady and Peter Ped Helcmanovsky): memory decoding, DivMMC automap,
// the Next register file, reset values and video timings come from there.
#include "specnext_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80dsp/z80dsp.h"
#include "ay8910.h"
#include "sdcard_spi.h"
#include "zxn_dma.h"

#define RAM_PAGES 256                   // 2 MB of 8 KB pages
#define MACHINE_ID 0x08                 // "Emulators"
#define CORE_VERSION 0x32               // 3.02
#define CORE_SUBVERSION 0x04
#define BOARD_ISSUE 0

// Standard Spectrum colours as RRRGGGBB, the ULA palette at reset
static const uint8_t kUlaDefault[16] = {0x00, 0x02, 0xa0, 0xa2, 0x14, 0x16, 0xb4, 0xb6,
                                        0x00, 0x03, 0xe0, 0xe7, 0x1c, 0x1f, 0xfc, 0xff};

static inline uint16_t rgb8_to_9(uint8_t v) { return (uint16_t)((v << 1) | ((v & 3) ? 1 : 0)); }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

enum { INT_LINE = 0, INT_CTC0 = 3, INT_ULA = 11, INT_COUNT = 16 };

typedef struct { int int_h, min_hactive, max_hc, int_v, min_vactive, max_vc; } video_timings;

typedef struct {
    uint8_t reg;
    int cpu_a_15_13;
    bool mreq_n, m1_n, en;
    bool automap_reset, automap_active, automap_rom3_active;
    bool retn_seen, button;
    bool instant_on, delayed_on, delayed_off;
    bool rom3_instant_on, rom3_delayed_on;
    bool nmi_instant_on, nmi_delayed_on;
    bool button_nmi, hold, held;
} divmmc_t;

typedef struct {
    bool a_0066, mreq_n, m1_n, retn_seen;
    bool enable, button;
    uint8_t mode;                       // nr_0A bits 7-6: 00 = +3, 11 = 48K, else 128K
    bool en_rd, en_wr, dis_rd, dis_wr;
    bool nmi_active, invisible, mf_enable;
} multiface_t;

typedef struct {
    uint8_t control, tconst;
    uint16_t counter;
    int prescale_acc;
    bool waiting_tc, running, waiting_trigger;
} ctc_t;

typedef struct {
    int x, y;
    bool rotate, xmirror, ymirror, h, rel_type;
    uint8_t paloff, pattern, xscale, yscale;
} sprite_data;

static struct {
    zd_z80 cpu;
    ay8910 ay[3];
    zxn_dma dma;
    sdspi sd;

    uint8_t *sram;
    const uint8_t *boot_rom;
    const uint8_t *rd[8];
    uint8_t *wr[8];
    uint8_t mmu[8];

    divmmc_t divmmc;
    bool divmmc_delayed_check;
    multiface_t mf;
    bool mf_button, drive_button;       // the NMI buttons held
    bool nr_02_mf_nmi, nr_02_divmmc_nmi;

    uint16_t *fb;                       // SN_W x SN_H, RGB333 (9 bits)
    uint32_t rgb_lut[512];
    uint16_t rgb565_lut[512];

    // Palettes: [0] ULA, [1] Layer 2, [2] sprites, [3] tilemap, [4..7] the
    // second ones; 9-bit RGB333, bit 15 = Layer 2 priority
    uint16_t palette[8][256];

    uint8_t sprite_pattern[0x4000];
    uint8_t sprite_attr[128 * 8];
    uint16_t sprite_pattern_index, sprite_attr_index, sprite_mirror_q;
    uint8_t sprite_mirror_index;
    bool sprite_mirror_inc;
    sprite_data sprite_cache[128];
    int sprite_cache_n;
    bool sprite_cache_valid;

    uint8_t copper_ram[0x800];
    uint16_t copper_pc;
    uint8_t copper_mode;
    int copper_budget;
    bool copper_frame_pending;

    ctc_t ctc[4];

    bool int_pending[INT_COUNT], int_service[INT_COUNT];
    int pulse_left;
    bool irq_asserted;

    video_timings vt;
    int line_ticks, frame_ticks, frame_tick, render_vc;
    bool ula_int_done, frame_done;
    uint64_t frame_count;
    uint8_t eff_machine_timing;
    bool eff_5060;
    int flash_counter;

    uint8_t port_fe, port_ff, port_7ffd, port_1ffd, port_dffd, port_eff7, port_e3, port_e7;
    bool port_1ffd_special_old;
    uint8_t nr_register, ay_select;
    bool layer2_en, layer2_map_wr, layer2_map_rd, layer2_map_shadow;
    uint8_t layer2_map_segment, layer2_offset;
    uint8_t ulap_mode, ulap_index;
    bool ulap_en;
    uint8_t spi_miso;

    bool bootrom_en, hard_reset_pending;
    uint8_t nr_02_reset_type;
    uint8_t nr_03_machine_type, nr_03_machine_timing;
    bool nr_03_user_dt_lock, nr_03_config_mode;
    uint8_t nr_04_romram_bank, nr_05, nr_06, nr_07_cpu_speed, nr_08, nr_09, nr_0a, nr_0b;
    uint8_t nr_10_coreid, nr_11_video_timing, nr_12_layer2_bank, nr_13_layer2_shadow_bank;
    uint8_t nr_14_global_transparent, nr_15, nr_16_l2_scrollx, nr_17_l2_scrolly;
    uint8_t clip_l2[4], clip_spr[4], clip_ula[4], clip_tm[4];
    uint8_t clip_idx_l2, clip_idx_spr, clip_idx_ula, clip_idx_tm;
    bool nr_22_line_int_en;
    uint16_t nr_23_line_int;
    uint8_t nr_26_ula_scrollx, nr_27_ula_scrolly;
    uint16_t nr_30_tm_scrollx;
    uint8_t nr_31_tm_scrolly, nr_32_lores_scrollx, nr_33_lores_scrolly;
    uint8_t nr_palette_idx;
    bool nr_palette_sub_idx;
    uint8_t nr_42_ulanext_format, nr_43, nr_stored_palette_value;
    uint8_t nr_4a_fallback, nr_4b_sprite_transparent, nr_4c_tm_transparent;
    uint16_t nr_copper_addr;
    uint8_t nr_copper_data_stored, nr_64_copper_offset;
    uint8_t nr_68, nr_6a, nr_6b, nr_6c, nr_6e, nr_6f, nr_70, nr_71, nr_7f, nr_80;
    uint8_t nr_82, nr_83, nr_84, nr_85, nr_86, nr_87, nr_88, nr_89;
    uint8_t nr_8c_altrom, nr_8f_mapping_mode;
    uint8_t nr_b8, nr_b9, nr_ba, nr_bb, nr_c0, nr_c2, nr_c3, nr_c4, nr_c6, nr_cc, nr_cd, nr_ce;
    uint8_t nr_raw[256];

    // input
    bool held[NK_COUNT];
    uint8_t key_rows[8];
    uint8_t ext_keys_b0, ext_keys_b1;
    uint8_t joy_left, joy_right;

    // audio
    int16_t audio[SN_AUDIO_MAX];
    int naudio;
    int64_t audio_acc;
    uint8_t dac[4];
    float dc_x, dc_y;
    bool turbo;

    // .nex loading
    uint8_t *pending_nex;
    uint32_t pending_nex_size;
    int os_frames;                      // frames since the firmware left config mode
    uint32_t sd_activity_last;
    int sd_busy_frames;
} N;

static void bank_update(int bank);
static void bank_update_range(int bank, int count) { for (int i = 0; i < count; ++i) bank_update(bank + i); }
static void reg_w(uint8_t reg, uint8_t value);
static uint8_t reg_r(uint8_t reg);
static void machine_reset(void);
static void render_line(int out_row);
static void int_clear(int source);
static void port_ff_w(uint8_t data) { N.port_ff = data; }

static inline uint8_t mem_read(uint16_t address)
{
    const uint8_t *p = N.rd[address >> 13];
    return p ? p[address & 0x1fff] : 0xff;
}

static inline void mem_write(uint16_t address, uint8_t value)
{
    uint8_t *p = N.wr[address >> 13];
    if (p) p[address & 0x1fff] = value;
}

static inline bool machine_type_48(void) { return N.nr_03_machine_type == 0 || N.nr_03_machine_type == 1; }
static inline bool machine_type_128(void) { return N.nr_03_machine_type == 2 || N.nr_03_machine_type == 4; }
static inline bool machine_type_p3(void) { return !machine_type_48() && !machine_type_128(); }
static inline bool pentagon_1024_en(void) { return N.nr_8f_mapping_mode == 3 && !(N.port_eff7 & 4); }
static inline bool mapping_pentagon(void) { return N.nr_8f_mapping_mode == 2 || pentagon_1024_en(); }
static inline int ticks_per_t(void) { return 8 >> N.nr_07_cpu_speed; }

// ---------------------------------------------------------------------------
// DivMMC automap state machine (specnext_divmmc)

static inline bool dm_conmem(const divmmc_t *d) { return d->reg & 0x80; }
static inline bool dm_mapram(const divmmc_t *d) { return d->reg & 0x40; }
static inline bool dm_page0(const divmmc_t *d) { return d->cpu_a_15_13 == 0; }
static inline bool dm_page1(const divmmc_t *d) { return d->cpu_a_15_13 == 1; }
static inline bool dm_automap(const divmmc_t *d)
{
    return !d->automap_reset && (d->held || (d->automap_active && (d->instant_on || (d->nmi_instant_on && d->button_nmi))) ||
                                 (d->automap_rom3_active && d->rom3_instant_on));
}
static inline bool dm_rom_en(const divmmc_t *d) { return d->en && dm_page0(d) && (dm_conmem(d) || dm_automap(d)) && !dm_mapram(d); }
static inline bool dm_ram_en(const divmmc_t *d)
{
    return d->en && ((dm_page0(d) && (dm_conmem(d) || dm_automap(d)) && dm_mapram(d)) ||
                     (dm_page1(d) && (dm_conmem(d) || dm_automap(d))));
}
static inline int dm_ram_bank(const divmmc_t *d) { return dm_page0(d) ? 3 : (d->reg & 0x0f); }
static inline bool dm_rdonly(const divmmc_t *d) { return dm_page0(d) || (dm_mapram(d) && dm_ram_bank(d) == 3); }

static void dm_clock(divmmc_t *d)
{
    if (d->automap_reset || d->retn_seen) d->button_nmi = false;
    else if (d->button) d->button_nmi = true;
    else if (d->held) d->button_nmi = false;

    if (d->automap_reset || d->retn_seen) {
        d->hold = false;
    } else if (!d->mreq_n && !d->m1_n) {
        d->hold = (d->automap_active && (d->instant_on || d->delayed_on || (d->nmi_instant_on && d->button_nmi) ||
                                         (d->nmi_delayed_on && d->button_nmi))) ||
                  (d->automap_rom3_active && (d->rom3_instant_on || d->rom3_delayed_on)) ||
                  (d->held && !(d->automap_active && d->delayed_off));
    }

    if (d->automap_reset || d->retn_seen) d->held = false;
    else if (d->mreq_n) d->held = d->hold;
}

static void dm_reset_lines(divmmc_t *d)
{
    d->mreq_n = d->m1_n = false;
    d->retn_seen = d->button = false;
    d->instant_on = d->delayed_on = d->delayed_off = false;
    d->rom3_instant_on = d->rom3_delayed_on = false;
    d->nmi_instant_on = d->nmi_delayed_on = false;
    d->hold = d->held = d->button_nmi = false;
}

// --- Multiface (the NextZXOS NMI menu ROM in SRAM pages 0x0A / 0x0B) ---------
static inline bool mf_mode_48(const multiface_t *m) { return m->mode == 3; }
static inline bool mf_mode_p3(const multiface_t *m) { return m->mode == 0; }
static inline bool mf_mode_128(const multiface_t *m) { return !mf_mode_p3(m) && !mf_mode_48(m); }
static inline bool mf_button_pulse(const multiface_t *m) { return m->button && !m->nmi_active; }
static inline bool mf_invisible_eff(const multiface_t *m) { return m->invisible && !mf_mode_48(m); }
static inline bool mf_fetch_66(const multiface_t *m) { return m->a_0066 && !m->m1_n && m->nmi_active; }
static inline bool mf_enabled(const multiface_t *m) { return m->enable && (m->mf_enable || mf_fetch_66(m)); }
static inline bool mf_port_en_(const multiface_t *m)
{
    return m->enable && m->en_rd && !mf_invisible_eff(m) && (mf_mode_128(m) || mf_mode_p3(m));
}

static void mf_clock(multiface_t *m)
{
    const bool port_io_dly = m->en_rd || m->en_wr || m->dis_rd || m->dis_wr;
    if (mf_button_pulse(m)) m->invisible = false;
    else if (((m->dis_wr && !mf_mode_p3(m)) || (m->en_wr && mf_mode_p3(m))) && !port_io_dly) m->invisible = true;
    if (mf_button_pulse(m)) m->nmi_active = true;
    else if (m->retn_seen || ((m->en_wr || m->dis_wr || (m->dis_rd && mf_mode_p3(m))) && !port_io_dly)) m->nmi_active = false;
    if (mf_fetch_66(m) && !m->mreq_n) m->mf_enable = true;
    else if (m->dis_rd || m->retn_seen) m->mf_enable = false;
    else if (m->en_rd) m->mf_enable = !mf_invisible_eff(m);
}

static void mf_defaults(void)
{
    const uint8_t mode = N.mf.mode;
    memset(&N.mf, 0, sizeof N.mf);
    N.mf.m1_n = true;
    N.mf.enable = true;
    N.mf.invisible = true;
    N.mf.mode = mode;
}

// ---------------------------------------------------------------------------
// Memory

static void mmu_w(int bank, uint8_t data)
{
    N.mmu[bank] = data;
    bank_update(bank);
}

static void mmu_x2_w(int bank, uint8_t data)
{
    mmu_w(bank, data);
    mmu_w(bank | 1, data == 0xff ? 0xff : (uint8_t)(data | 1));
}

static uint16_t port_7ffd_bank(void)
{
    const bool pent = mapping_pentagon();
    return (uint16_t)(((pent ? 0 : ((N.port_dffd >> 3) & 1)) << 6) |
                      ((!pent ? ((N.port_dffd >> 2) & 1) : (pentagon_1024_en() && (N.port_7ffd & 0x20))) << 5) |
                      ((pent ? ((N.port_7ffd >> 6) & 3) : (N.port_dffd & 3)) << 3) | (N.port_7ffd & 7));
}

static bool port_7ffd_locked(void) { return pentagon_1024_en() ? false : (N.port_7ffd & 0x20) != 0; }

static uint32_t internal_port_enable(void)
{
    uint32_t en = (uint32_t)N.nr_82 | ((uint32_t)N.nr_83 << 8) | ((uint32_t)N.nr_84 << 16) | ((uint32_t)(N.nr_85 & 0x0f) << 24);
    if (N.nr_80 & 0x80)
        en &= (uint32_t)N.nr_86 | ((uint32_t)N.nr_87 << 8) | ((uint32_t)N.nr_88 << 16) | ((uint32_t)(N.nr_89 & 0x0f) << 24);
    return en;
}
static inline bool port_en(int bit) { return (internal_port_enable() >> bit) & 1; }

// Decodes one 8 KB slot: boot ROM, ROM / altrom / config mode, DivMMC ROM and
// RAM, Layer 2 mapping and the MMU pages, separately for reads and writes.
static void bank_update(int bank)
{
    const bool is_rom = (bank >> 1) == 0;
    if (N.bootrom_en && is_rom) {
        N.rd[bank] = N.boot_rom;
        N.wr[bank] = NULL;
        return;
    }

    const bool altrom_en = N.nr_8c_altrom & 0x80;
    const bool altrom_rw = N.nr_8c_altrom & 0x40;
    const bool lock_rom1 = N.nr_8c_altrom & 0x20;
    const bool lock_rom0 = N.nr_8c_altrom & 0x10;
    const int rom_1ffd = (((N.port_1ffd >> 2) & 1) << 1) | ((N.port_7ffd >> 4) & 1);
    int sram_rom;
    bool sram_rom3, alt_128_n;
    if (machine_type_48()) {
        sram_rom = 0;
        sram_rom3 = true;
        alt_128_n = !(!lock_rom1 && lock_rom0);
    } else if (machine_type_p3()) {
        if (lock_rom1 || lock_rom0) {
            sram_rom = ((int)lock_rom1 << 1) | (int)lock_rom0;
            sram_rom3 = lock_rom1 && lock_rom0;
            alt_128_n = lock_rom1;
        } else {
            sram_rom = rom_1ffd;
            sram_rom3 = rom_1ffd == 3;
            alt_128_n = rom_1ffd & 1;
        }
    } else {
        if (lock_rom1 || lock_rom0) {
            sram_rom = lock_rom1;
            sram_rom3 = lock_rom1;
            alt_128_n = lock_rom1;
        } else {
            sram_rom = rom_1ffd & 1;
            sram_rom3 = rom_1ffd & 1;
            alt_128_n = rom_1ffd & 1;
        }
    }

    const uint8_t page = N.mmu[bank];
    const bool mem_bank5 = page == 0x0a || page == 0x0b;
    const bool mem_bank7 = page == 0x0e;
    const int mmu_a = ((1 + (page >> 5)) << 5) | (page & 0x1f);

    const int l2_off_pre = N.layer2_map_segment == 3 ? ((bank >> 1) & 3) : N.layer2_map_segment;
    const int l2_bank = (N.layer2_map_shadow ? N.nr_13_layer2_shadow_bank : N.nr_12_layer2_bank) + l2_off_pre + N.layer2_offset;
    const int l2_page = ((l2_bank << 1) | (bank & 1)) & 0xff;
    const int l2_a = ((1 + (l2_page >> 5)) << 5) | (l2_page & 0x1f);

    int pre_a, pre_override;
    bool pre_active, pre_bank5, pre_bank7, pre_rdonly;
    if (is_rom) {
        const int a13 = bank & 1;
        N.mf.enable = port_en(9);
        if (mf_enabled(&N.mf)) {        // Multiface ROM (page 0x0A) / RAM (0x0B)
            pre_a = 0x0a | a13;
            pre_active = true;
            pre_bank5 = pre_bank7 = false;
            pre_rdonly = !a13;
            pre_override = 0;
        } else if (!(mmu_a & 0x100)) {
            pre_a = mmu_a & 0xff;
            pre_active = !mem_bank5 && !mem_bank7;
            pre_bank5 = mem_bank5;
            pre_bank7 = mem_bank7;
            pre_rdonly = false;
            pre_override = 6;
        } else if (N.nr_03_config_mode) {
            pre_a = (N.nr_04_romram_bank << 1) | a13;
            pre_active = true;
            pre_bank5 = pre_bank7 = false;
            pre_rdonly = false;
            pre_override = 6;
        } else {
            pre_a = (sram_rom << 1) | a13;
            pre_active = true;
            pre_bank5 = pre_bank7 = false;
            pre_rdonly = !(altrom_en && altrom_rw);
            pre_override = 7;
        }
    } else {
        pre_a = mmu_a & 0xff;
        pre_active = !(mmu_a & 0x100) && !mem_bank5 && !mem_bank7;
        pre_bank5 = mem_bank5;
        pre_bank7 = mem_bank7;
        pre_rdonly = false;
        pre_override = (((bank & 6) != 6) && N.layer2_map_segment == 3) ? 2 : 0;
    }

    N.divmmc.cpu_a_15_13 = bank;
    N.divmmc.en = port_en(8);
    N.divmmc.automap_reset = !port_en(8) || !(N.nr_0a & 0x10);
    N.divmmc.automap_active = (pre_override & 4) != 0;

    int page_for[2] = {-1, -1};         // [0] read, [1] write
    for (int cpu_rd_n = 1; cpu_rd_n >= 0; --cpu_rd_n) {
        const bool l2_map_en = (pre_override & 2) && ((N.layer2_map_wr && cpu_rd_n) || (N.layer2_map_rd && !cpu_rd_n));
        const bool sram_altrom_en =
            !(!(pre_override & 1) || !altrom_en || (pre_rdonly && cpu_rd_n) || (!pre_rdonly && !cpu_rd_n));
        N.divmmc.automap_rom3_active = ((pre_override & 5) == 5) && !l2_map_en &&
                                       ((sram_altrom_en && alt_128_n) || (sram_rom3 && !sram_altrom_en));
        dm_clock(&N.divmmc);

        int a;
        bool active, rdonly;
        if ((pre_override & 4) && dm_rom_en(&N.divmmc)) {
            a = 8;
            active = true;
            rdonly = true;
        } else if ((pre_override & 4) && dm_ram_en(&N.divmmc)) {
            a = 0x10 | dm_ram_bank(&N.divmmc);
            active = true;
            rdonly = dm_rdonly(&N.divmmc);
        } else if (l2_map_en) {
            a = l2_a & 0xff;
            active = !(l2_a & 0x100);
            rdonly = false;
        } else if (sram_altrom_en) {
            a = 0x0c | ((int)alt_128_n << 1) | (pre_a & 1);
            active = true;
            rdonly = pre_rdonly;
        } else {
            a = pre_a;
            active = pre_active || pre_bank5 || pre_bank7;
            rdonly = pre_rdonly;
        }
        if (cpu_rd_n) page_for[1] = (active && !rdonly && a < RAM_PAGES) ? a : -1;
        else page_for[0] = (active && a < RAM_PAGES) ? a : -1;
    }
    N.rd[bank] = page_for[0] >= 0 ? &N.sram[(size_t)page_for[0] * 0x2000] : NULL;
    N.wr[bank] = page_for[1] >= 0 ? &N.sram[(size_t)page_for[1] * 0x2000] : NULL;
}

static void memory_change(uint16_t port, uint8_t data)
{
    if (N.port_1ffd & 1) {              // +3 special (all RAM) paging
        uint8_t b3 = (uint8_t)((((N.port_1ffd >> 2) & 1) || ((N.port_1ffd >> 1) & 1)) << 3);
        mmu_x2_w(0, b3);
        const uint8_t b2 = (uint8_t)((((N.port_1ffd >> 2) & 1) && ((N.port_1ffd >> 1) & 1)) << 2);
        mmu_x2_w(2, (uint8_t)(b3 | b2 | 2));
        mmu_x2_w(4, (uint8_t)(b3 | 4));
        b3 = (uint8_t)((!((N.port_1ffd >> 2) & 1) && ((N.port_1ffd >> 1) & 1)) << 3);
        mmu_x2_w(6, (uint8_t)(b3 | 6));
    } else {
        mmu_x2_w(0, (N.port_eff7 & 8) ? 0x00 : 0xff);
        if (N.port_1ffd_special_old) mmu_x2_w(2, 0x0a);
        if (N.port_1ffd_special_old) mmu_x2_w(4, 0x04);
        const bool ram_change = !(port == 0x8e && !(data & 8));
        if (N.port_1ffd_special_old || ram_change) mmu_x2_w(6, (uint8_t)(port_7ffd_bank() << 1));
    }
    N.port_1ffd_special_old = N.port_1ffd & 1;
}

static uint8_t do_m1(uint16_t address)
{
    const int bank = address >> 13;
    N.divmmc.mreq_n = true;
    dm_clock(&N.divmmc);
    N.divmmc.mreq_n = false;
    N.divmmc.m1_n = false;
    dm_clock(&N.divmmc);
    if (bank < 2) bank_update(bank);
    const uint8_t data = mem_read(address);
    N.divmmc.instant_on = N.divmmc.delayed_on = N.divmmc.delayed_off = false;
    N.divmmc.rom3_instant_on = N.divmmc.rom3_delayed_on = false;
    N.divmmc.nmi_instant_on = N.divmmc.nmi_delayed_on = false;
    N.divmmc.m1_n = true;
    N.divmmc.mreq_n = true;
    dm_clock(&N.divmmc);
    bank_update_range(0, 2);
    N.divmmc_delayed_check = true;
    return data;
}

// Opcode fetches: the DivMMC watches the M1 cycles at its entry points
static uint8_t opcode_read(void *user, uint16_t a)
{
    (void)user;
    if (a <= 0x38 && (a & 7) == 0) {
        const int b = a >> 3;
        const bool ep = (N.nr_b8 >> b) & 1, valid = (N.nr_b9 >> b) & 1, timing = (N.nr_ba >> b) & 1;
        N.divmmc.instant_on = ep && valid && timing;
        N.divmmc.delayed_on = ep && valid && !timing;
        N.divmmc.rom3_instant_on = ep && !valid && timing;
        N.divmmc.rom3_delayed_on = ep && !valid && !timing;
        return do_m1(a);
    }
    if (a >= 0x1ff8 && a <= 0x1fff) {
        N.divmmc.delayed_off = (N.nr_bb >> 6) & 1;
        return do_m1(a);
    }
    if (a >= 0x3d00 && a <= 0x3dff) {
        N.divmmc.rom3_instant_on = (N.nr_bb >> 7) & 1;
        return do_m1(a);
    }
    switch (a) {
    case 0x04c6: N.divmmc.rom3_delayed_on = (N.nr_bb >> 2) & 1; return do_m1(a);
    case 0x0562: N.divmmc.rom3_delayed_on = (N.nr_bb >> 3) & 1; return do_m1(a);
    case 0x04d7: N.divmmc.rom3_delayed_on = (N.nr_bb >> 4) & 1; return do_m1(a);
    case 0x056a: N.divmmc.rom3_delayed_on = (N.nr_bb >> 5) & 1; return do_m1(a);
    case 0x0066: {
        N.mf.m1_n = false;
        N.mf.a_0066 = true;
        N.mf.mreq_n = false;
        mf_clock(&N.mf);
        N.divmmc.nmi_instant_on = (N.nr_bb >> 1) & 1;
        N.divmmc.nmi_delayed_on = N.nr_bb & 1;
        const uint8_t data = do_m1(a);
        N.mf.a_0066 = false;
        N.mf.m1_n = true;
        mf_clock(&N.mf);
        return data;
    }
    default: break;
    }
    if (N.divmmc_delayed_check) {
        do_m1(a);
        N.divmmc_delayed_check = false;
    }
    return mem_read(a);
}

// ---------------------------------------------------------------------------
// I/O ports

static void port_e3_w(uint8_t data)
{
    N.port_e3 = (uint8_t)(data & ~0x30);
    N.divmmc.reg = N.port_e3;
    bank_update_range(0, 2);
}

static void port_e7_w(uint8_t data)
{
    const bool swap = N.nr_0a & 0x20;
    if ((data & 3) == 2) N.port_e7 = (uint8_t)(0xfc | ((int)!swap << 1) | (int)swap);
    else if ((data & 3) == 1) N.port_e7 = (uint8_t)(0xfc | ((int)swap << 1) | (int)!swap);
    else if (data == 0xfb || data == 0xf7) N.port_e7 = data;
    else if (data == 0x7f && (N.nr_03_config_mode || (N.nr_02_reset_type & 4))) N.port_e7 = 0x7f;
    else N.port_e7 = 0xff;
    sdspi_select(&N.sd, !(N.port_e7 & 1));     // SD card 0 (the only one fitted)
}

static void turbosound_address_w(uint8_t data)
{
    const bool ts = N.nr_08 & 0x02;
    if (ts && (data & 0x9c) == 0x9c)
        N.ay_select = (data & 0x02) ? ((data & 0x01) ? 0 : 1) : ((data & 0x01) ? 2 : 0);
    else if ((data & 0xe0) == 0)
        ay8910_address_w(&N.ay[ts ? N.ay_select : 0], data);
}

static void dac_w(int mask, uint8_t value)
{
    for (int i = 0; i < 4; ++i)
        if (mask & (1 << i)) N.dac[i] = value;
}

static uint8_t kempston_value(int port_37, uint8_t type, uint8_t joy)
{
    const bool md = port_37 ? type == 6 : type == 5;
    const bool kemp = port_37 ? (type == 4 || type == 6) : (type == 1 || type == 5);
    if (!kemp) return 0;
    return md ? joy : (uint8_t)(joy & 0x3f);
}

static uint8_t kempston_read(int port_37)
{
    const uint8_t joy0 = (uint8_t)((((N.nr_05 >> 3) & 1) << 2) | ((N.nr_05 >> 6) & 3));
    const uint8_t joy1 = (uint8_t)((((N.nr_05 >> 1) & 1) << 2) | ((N.nr_05 >> 4) & 3));
    if (!port_en(port_37 ? 7 : 6)) return 0;
    return (uint8_t)(kempston_value(port_37, joy0, N.joy_left) | kempston_value(port_37, joy1, N.joy_right));
}

static uint8_t ula_read(uint16_t port)
{
    uint8_t keys = 0x1f;
    for (int row = 0; row < 8; ++row)
        if (!(port & (0x100 << row))) keys &= N.key_rows[row];
    const bool issue2 = N.nr_08 & 1;
    const bool ear = issue2 ? (N.port_fe & 0x18) != 0 : (N.port_fe & 0x10) != 0;
    return (uint8_t)(0xa0 | keys | (ear ? 0x40 : 0));
}

// Multiface paging ports: $3F/$BF (+3 mode), $BF/$3F (128), $9F/$1F (48)
static void mf_ports(uint8_t type, uint8_t *enable, uint8_t *disable)
{
    *enable = 0x3f;
    *disable = 0xbf;
    if (type & 2) {
        *enable = 0x9f;
        *disable = 0x1f;
    } else if (type & 1) {
        *enable = 0xbf;
        *disable = 0x3f;
    }
}

static uint8_t mf_port_read(uint16_t port, uint8_t lsb)
{
    uint8_t en_port, dis_port;
    mf_ports(N.mf.mode, &en_port, &dis_port);
    N.mf.en_rd = port_en(9) && lsb == en_port;
    N.mf.dis_rd = port_en(9) && lsb == dis_port;
    N.mf.en_wr = N.mf.dis_wr = false;
    mf_clock(&N.mf);
    bank_update_range(0, 2);
    uint8_t data;
    if (!mf_port_en_(&N.mf)) {
        data = lsb == 0x1f ? kempston_read(0) : 0x00;
    } else if (N.mf.mode != 0) {
        data = (uint8_t)(((N.port_7ffd >> 3) & 1) << 7 | 0x7f);
    } else {
        switch (port >> 12) {
        case 0x1: data = N.port_1ffd; break;
        case 0x7: data = N.port_7ffd; break;
        case 0xd: data = N.port_dffd; break;
        case 0xe: data = (uint8_t)(N.port_eff7 & 0xc0); break;
        default: data = (uint8_t)(N.port_fe & 7); break;
        }
    }
    N.mf.en_rd = N.mf.dis_rd = false;
    return data;
}

static void mf_port_write(uint8_t lsb)
{
    uint8_t en_port, dis_port;
    mf_ports(N.mf.mode, &en_port, &dis_port);
    N.mf.en_rd = N.mf.dis_rd = false;
    N.mf.en_wr = port_en(9) && lsb == en_port;
    N.mf.dis_wr = port_en(9) && lsb == dis_port;
    mf_clock(&N.mf);
    bank_update_range(0, 2);
}

// --- CTC: four channels clocked at 28 MHz, ZC/TO of channel n triggers n+1 ---
static void int_raise(int source);

static void ctc_write(int ch, uint8_t v)
{
    ctc_t *c = &N.ctc[ch];
    if (c->waiting_tc) {
        c->tconst = v;
        c->waiting_tc = false;
        c->counter = v ? v : 256;
        c->prescale_acc = 0;
        if (!(c->control & 0x40) && (c->control & 0x08)) c->waiting_trigger = true;    // started by a trigger edge
        else c->running = true;
        return;
    }
    if (v & 1) {                        // control word
        c->control = v;
        if (v & 0x04) c->waiting_tc = true;
        if (v & 0x02) {
            c->running = false;
            if (!(v & 0x04)) c->waiting_tc = false;
        }
        if (!(v & 0x80)) int_clear(INT_CTC0 + ch);
    }
    // (vector words are ignored: the Next gives the IM2 vectors, nr_C0)
}

static uint8_t ctc_read(int ch) { return (uint8_t)(N.ctc[ch].counter & 0xff); }

static void ctc_count(int ch)
{
    ctc_t *c = &N.ctc[ch];
    if (--c->counter == 0) {
        c->counter = c->tconst ? c->tconst : 256;
        if (c->control & 0x80) int_raise(INT_CTC0 + ch);
        if (ch < 3) {
            ctc_t *n = &N.ctc[ch + 1];
            if (n->running && (n->control & 0x40)) {
                ctc_count(ch + 1);
            } else if (n->waiting_trigger) {
                n->waiting_trigger = false;
                n->running = true;
            }
        }
    }
}

static void ctc_tick(int ticks)
{
    for (int ch = 0; ch < 4; ++ch) {
        ctc_t *c = &N.ctc[ch];
        if (!c->running || (c->control & 0x40)) continue;       // counter mode counts triggers
        const int pre = (c->control & 0x20) ? 256 : 16;
        c->prescale_acc += ticks;
        while (c->prescale_acc >= pre) {
            c->prescale_acc -= pre;
            ctc_count(ch);
        }
    }
}

static void ctc_defaults(void)
{
    memset(N.ctc, 0, sizeof N.ctc);
    for (int i = 0; i < 4; i++) {
        N.ctc[i].control = 0x03;
        N.ctc[i].counter = 256;
    }
}

// --- sprites: port $303B / $57 / $5B and the nextreg attribute mirror ------------
static void sprite_io_w(uint16_t port, uint8_t v)
{
    bool attr_num_change = false;
    if (port == 0x5b) {
        N.sprite_pattern[N.sprite_pattern_index] = v;
        N.sprite_pattern_index = (uint16_t)((N.sprite_pattern_index + 1) & 0x3fff);
    } else if (port == 0x303b) {
        N.sprite_pattern_index = (uint16_t)(((v & 0x3f) << 8) | (v & 0x80));
        N.sprite_attr_index = (uint16_t)((v & 0x7f) << 3);
        attr_num_change = true;
    } else if (port == 0x57) {
        N.sprite_cache_valid = false;
        N.sprite_attr[N.sprite_attr_index] = v;
        const bool by8 = (N.sprite_attr_index & 4) || ((N.sprite_attr_index & 7) == 3 && !(v & 0x40));
        if (!by8) {
            N.sprite_attr_index = (uint16_t)((N.sprite_attr_index + 1) & 0x3ff);
        } else {
            N.sprite_attr_index = (uint16_t)((((N.sprite_attr_index >> 3) + 1) & 0x7f) << 3);
            attr_num_change = true;
        }
    }
    if (attr_num_change && (N.nr_09 & 0x10))
        N.sprite_mirror_q = (uint16_t)((N.sprite_attr_index >> 3) | (N.sprite_pattern_index & 0x80));
}

static void sprite_mirror_w(uint8_t v)
{
    if (N.sprite_mirror_index <= 4) {
        N.sprite_cache_valid = false;
        N.sprite_attr[((N.sprite_mirror_q & 0x7f) << 3) | N.sprite_mirror_index] = v;
    }
    bool changed = false;
    if (N.sprite_mirror_index == 7) {
        N.sprite_mirror_q = v;
        changed = true;
    } else if (N.sprite_mirror_inc) {
        N.sprite_mirror_q = (uint16_t)(((N.sprite_mirror_q + 1) & 0x7f) | (N.sprite_pattern_index & 0x80));
        changed = true;
    }
    if (changed && (N.nr_09 & 0x10)) {
        N.sprite_pattern_index = (uint16_t)(((N.sprite_mirror_q & 0x3f) << 8) | (N.sprite_mirror_q & 0x80));
        N.sprite_attr_index = (uint16_t)((N.sprite_mirror_q & 0x7f) << 3);
    }
}

// --- NMIs: the Multiface ("M1") and DivMMC ("drive") buttons and nextreg 02's -----
static void nmi_request(void)
{
    if (N.nr_03_config_mode) return;
    const bool mf_nmi = (N.mf_button || N.nr_02_mf_nmi) && (N.nr_06 & 0x08);
    const bool div_nmi = (N.drive_button || N.nr_02_divmmc_nmi) && (N.nr_06 & 0x10);
    N.mf.button = mf_nmi;
    mf_clock(&N.mf);
    N.divmmc.button = div_nmi;
    dm_clock(&N.divmmc);
    if (mf_nmi || div_nmi) zd_set_nmi(&N.cpu, ZD_ASSERT);
}

static void leave_nmi(void)
{
    N.mf.retn_seen = true;
    mf_clock(&N.mf);
    N.mf.retn_seen = false;
    mf_clock(&N.mf);
    N.divmmc.retn_seen = true;
    dm_clock(&N.divmmc);
    N.divmmc.retn_seen = false;
    dm_clock(&N.divmmc);
    N.nr_02_mf_nmi = N.nr_02_divmmc_nmi = false;
    bank_update_range(0, 2);
    zd_set_nmi(&N.cpu, ZD_CLEAR);
}

// --- the I/O bus -----------------------------------------------------------------------
static uint8_t io_in(void *user, uint16_t port)
{
    (void)user;
    const uint8_t lo = (uint8_t)port;
    if (!(port & 1)) return ula_read(port);
    switch (port & 0x0fff) {
    case 0x0adf: return 0xff;           // Kempston mouse buttons (none pressed)
    case 0x0bdf: return 0x00;           // mouse X
    case 0x0fdf: return 0x00;           // mouse Y
    default: break;
    }
    switch (lo) {
    case 0x37: return kempston_read(1);
    case 0x6b: return zxn_dma_read(&N.dma);
    case 0x0b: return zxn_dma_read(&N.dma);
    case 0xeb: {
        const uint8_t v = N.spi_miso;
        N.spi_miso = port_en(11) ? sdspi_exchange(&N.sd, 0xff) : 0xff;
        return v;
    }
    case 0xe3: return port_en(8) ? (uint8_t)(N.port_e3 & ~0x30) : 0x00;
    case 0x1f: case 0x3f: case 0x9f: case 0xbf: return mf_port_read(port, lo);
    case 0xff: return ((N.nr_08 & 0x04) && port_en(0)) ? N.port_ff : 0xff;
    default: break;
    }
    switch (port) {
    case 0xff3b:
        if (!port_en(24)) return 0;
        if (N.ulap_mode == 0) {
            const uint16_t v = N.palette[(N.nr_43 & 0x40) ? 4 : 0][0xc0 | N.ulap_index];
            return (uint8_t)(((v >> 3) & 7) << 5 | ((v >> 6) & 7) << 2 | ((v >> 1) & 3));   // GGGRRRBB
        }
        return N.ulap_en ? 1 : 0;
    case 0x303b: return 0x00;           // sprite status: no collision / overflow reported
    case 0x253b: return reg_r(N.nr_register);
    case 0x243b: return N.nr_register;
    case 0x123b:
        return (uint8_t)((N.layer2_map_segment << 6) | ((int)N.layer2_map_shadow << 3) | ((int)N.layer2_map_rd << 2) |
                         ((int)N.layer2_en << 1) | (int)N.layer2_map_wr);
    case 0x133b: return 0x00;           // UART: nothing received, transmitter idle
    case 0x143b: case 0x153b: case 0x163b: return 0x00;
    case 0x103b: case 0x113b: return port_en(10) ? 0xff : 0x00;     // I2C lines idle high
    case 0xbff5: return (N.nr_08 & 0x02) ? N.ay_select : 0;
    default: break;
    }
    if ((port & 0xf8ff) == 0x183b) {
        const int ch = (port >> 8) & 7;
        return (port_en(27) && ch < 4) ? ctc_read(ch) : 0x00;
    }
    if (lo == 0xfd) {
        if ((port & 0xc000) == 0xc000 && (port & 0xf000) != 0xd000)
            return ay8910_data_r(&N.ay[(N.nr_08 & 0x02) ? N.ay_select : 0]);
        return 0xff;
    }
    return 0xff;
}

static void io_out(void *user, uint16_t port, uint8_t v)
{
    (void)user;
    const uint8_t lo = (uint8_t)port;
    if (!(port & 1)) {
        N.port_fe = v;                  // border, MIC, speaker
        return;
    }
    switch (lo) {
    case 0x6b: zxn_dma_write(&N.dma, v, false); return;
    case 0x0b: zxn_dma_write(&N.dma, v, true); return;
    case 0xeb: N.spi_miso = port_en(11) ? sdspi_exchange(&N.sd, v) : 0xff; return;
    case 0xe3:
        if (port_en(8)) port_e3_w((uint8_t)((N.port_e3 & 0x40) | v));
        return;
    case 0xe7: port_e7_w(v); return;
    case 0x5b:
    case 0x57:
        if (port_en(14)) sprite_io_w(lo, v);
        return;
    case 0x1f:
        mf_port_write(lo);
        if ((N.nr_08 & 0x08) && port_en(17)) dac_w(0xf, v);
        return;
    case 0x3f:
        mf_port_write(lo);
        if ((N.nr_08 & 0x08) && port_en(19)) dac_w(0x9, v);
        return;
    case 0x9f: case 0xbf: mf_port_write(lo); return;
    case 0x0f: case 0xf3: if (N.nr_08 & 0x08) dac_w(0x2, v); return;
    case 0x4f: case 0xf9: if (N.nr_08 & 0x08) dac_w(0x4, v); return;
    case 0x5f: if (N.nr_08 & 0x08) dac_w(0x8, v); return;
    case 0xf1: if (N.nr_08 & 0x08) dac_w(0x1, v); return;
    case 0xb3: if ((N.nr_08 & 0x08) && port_en(22)) dac_w(0x6, v); return;
    case 0xdf: if ((N.nr_08 & 0x08) && port_en(23)) dac_w(0x9, v); return;
    case 0xfb: if ((N.nr_08 & 0x08) && port_en(21)) dac_w(0x9, v); return;
    case 0xff:
        if (port_en(0)) port_ff_w(v);
        return;
    default: break;
    }
    switch (port) {
    case 0xff3b:
        if (!port_en(24)) return;
        if (N.ulap_mode == 0) {         // ULA+ entry GGGRRRBB into the ULA palette at 0xC0+
            const uint16_t c9 = (uint16_t)(((v >> 2) & 7) << 6 | ((v >> 5) & 7) << 3 | ((v & 3) << 1) | ((v & 3) ? 1 : 0));
            N.palette[(N.nr_43 & 0x40) ? 4 : 0][0xc0 | N.ulap_index] = c9;
        } else if (N.ulap_mode == 1) {
            N.ulap_en = v & 1;
        }
        return;
    case 0xbf3b:
        if (port_en(24)) {
            N.ulap_mode = (uint8_t)((v >> 6) & 3);
            if (N.ulap_mode == 0) N.ulap_index = (uint8_t)(v & 0x3f);
        }
        return;
    case 0x303b:
        if (port_en(14)) sprite_io_w(0x303b, v);
        return;
    case 0x253b: reg_w(N.nr_register, v); return;
    case 0x243b: N.nr_register = v; return;
    case 0x123b:
        if (!(v & 0x10)) {
            N.layer2_en = v & 0x02;
            N.layer2_map_wr = v & 0x01;
            N.layer2_map_rd = v & 0x04;
            N.layer2_map_shadow = v & 0x08;
            N.layer2_map_segment = (uint8_t)((v >> 6) & 3);
        } else {
            N.layer2_offset = (uint8_t)(v & 7);
        }
        bank_update_range(0, 6);
        return;
    case 0x133b: case 0x143b: case 0x153b: case 0x163b: return;     // UART
    case 0x103b: case 0x113b: return;                               // I2C
    default: break;
    }
    if ((port & 0xf8ff) == 0x183b) {
        const int ch = (port >> 8) & 7;
        if (port_en(27) && ch < 4) ctc_write(ch, v);
        return;
    }
    if ((port & 0xf0ff) == 0xe0f7) {
        if (port_en(26)) {
            N.port_eff7 = v;
            memory_change(0xeff7, v);
        }
        return;
    }
    if (lo == 0xfd) {
        const int hi = port >> 12;
        if (port & 0x8000) {
            if (hi == 0xd) {            // $DFFD
                if (port_en(2)) {
                    if (!port_7ffd_locked()) {
                        N.port_dffd = v;
                        memory_change(0xdffd, v);
                    }
                } else {
                    turbosound_address_w(v);
                }
            } else if (port & 0x4000) { // $FFFD
                if (port_en(16)) turbosound_address_w(v);
            } else {                    // $BFFD
                if (port_en(16)) ay8910_data_w(&N.ay[(N.nr_08 & 0x02) ? N.ay_select : 0], v);
            }
            return;
        }
        if (hi == 1) {                  // $1FFD
            if (port_en(3) && !port_7ffd_locked()) {
                N.port_1ffd = v;
                memory_change(0x1ffd, v);
            }
            return;
        }
        if (hi == 2 || hi == 3) return; // +3 FDC: not fitted
        const bool p3_timing = (N.nr_03_machine_timing & 3) == 3;
        if (port_en(1) && ((port & 0x4000) || !p3_timing) && !port_7ffd_locked()) {
            N.port_7ffd = v;
            memory_change(0x7ffd, v);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Next registers

// Palette written: nr_43 bits 6-4 (ULA, Layer 2, sprites, tilemap, then the second ones)
static int palette_select(uint8_t nr_43) { return (nr_43 >> 4) & 7; }

static void palette_write(uint8_t priority, uint16_t value9)
{
    const int p = palette_select(N.nr_43);
    uint16_t v = (uint16_t)(value9 & 0x1ff);
    if ((p & 3) == 1 && (priority & 2)) v |= 0x8000;   // a Layer 2 priority colour
    N.palette[p][N.nr_palette_idx] = v;
}

static uint16_t palette_read(void) { return N.palette[palette_select(N.nr_43)][N.nr_palette_idx]; }

static int irq_pulse_cycles(void)
{
    const uint8_t t = N.eff_machine_timing;
    return ((t & 4) || ((t & 2) && !(t & 1))) ? 36 : 32;
}

static int vpos_to_cvc(int vpos)
{
    const int lines = N.vt.max_vc + 1;
    return (vpos - N.vt.min_vactive + N.nr_64_copper_offset + lines) % lines;
}

static int cvc_to_vpos(int cvc)
{
    const int lines = N.vt.max_vc + 1;
    return ((cvc + N.vt.min_vactive - N.nr_64_copper_offset) % lines + lines) % lines;
}

static uint8_t reg_r(uint8_t reg)
{
    switch (reg) {
    case 0x00: return MACHINE_ID;
    case 0x01: return CORE_VERSION;
    case 0x02: return (uint8_t)(((int)N.nr_02_mf_nmi << 3) | ((int)N.nr_02_divmmc_nmi << 2) | (N.nr_02_reset_type & 3));
    case 0x03:
        return (uint8_t)(((int)N.nr_palette_sub_idx << 7) | (N.nr_03_machine_timing << 4) | ((int)N.nr_03_user_dt_lock << 3) |
                         N.nr_03_machine_type);
    case 0x04: return N.nr_04_romram_bank;
    case 0x05: return (uint8_t)((N.nr_05 & ~0x05) | (N.eff_5060 ? 4 : 0));
    case 0x06: return N.nr_06;
    case 0x07: return (uint8_t)((N.nr_07_cpu_speed << 4) | N.nr_07_cpu_speed);
    case 0x08: return (uint8_t)((N.nr_08 & 0x7f) | (port_7ffd_locked() ? 0 : 0x80));
    case 0x09: return N.nr_09;
    case 0x0a: return N.nr_0a;
    case 0x0b: return N.nr_0b;
    case 0x0e: return CORE_SUBVERSION;
    case 0x0f: return BOARD_ISSUE;
    case 0x10: return (uint8_t)(N.nr_10_coreid << 2);
    case 0x11: return N.nr_11_video_timing;
    case 0x12: return N.nr_12_layer2_bank;
    case 0x13: return N.nr_13_layer2_shadow_bank;
    case 0x14: return N.nr_14_global_transparent;
    case 0x15: return N.nr_15;
    case 0x16: return N.nr_16_l2_scrollx;
    case 0x17: return N.nr_17_l2_scrolly;
    case 0x18: return N.clip_l2[N.clip_idx_l2];
    case 0x19: return N.clip_spr[N.clip_idx_spr];
    case 0x1a: return N.clip_ula[N.clip_idx_ula];
    case 0x1b: return N.clip_tm[N.clip_idx_tm];
    case 0x1c: return (uint8_t)((N.clip_idx_tm << 6) | (N.clip_idx_ula << 4) | (N.clip_idx_spr << 2) | N.clip_idx_l2);
    case 0x1e:
    case 0x1f: {
        const int vc = imin(N.frame_tick / imax(1, N.line_ticks), N.vt.max_vc);
        const int cvc = vpos_to_cvc(vc);
        return reg == 0x1e ? (uint8_t)(cvc >> 8) : (uint8_t)cvc;
    }
    case 0x20:
        return (uint8_t)(((int)N.int_pending[INT_LINE] << 7) | ((int)N.int_pending[INT_ULA] << 6) |
                         ((int)N.int_pending[INT_CTC0] | ((int)N.int_pending[INT_CTC0 + 1] << 1) |
                          ((int)N.int_pending[INT_CTC0 + 2] << 2) | ((int)N.int_pending[INT_CTC0 + 3] << 3)));
    case 0x22:
        return (uint8_t)(((int)(N.int_pending[INT_ULA] && !(N.nr_c0 & 1)) << 7) | ((int)((N.port_ff & 0x40) != 0) << 2) |
                         ((int)N.nr_22_line_int_en << 1) | ((N.nr_23_line_int >> 8) & 1));
    case 0x23: return (uint8_t)N.nr_23_line_int;
    case 0x26: return N.nr_26_ula_scrollx;
    case 0x27: return N.nr_27_ula_scrolly;
    case 0x28: return N.nr_stored_palette_value;
    case 0x2f: return (uint8_t)((N.nr_30_tm_scrollx >> 8) & 3);
    case 0x30: return (uint8_t)N.nr_30_tm_scrollx;
    case 0x31: return N.nr_31_tm_scrolly;
    case 0x32: return N.nr_32_lores_scrollx;
    case 0x33: return N.nr_33_lores_scrolly;
    case 0x34: return (uint8_t)(N.sprite_mirror_q & 0x7f);
    case 0x40: return N.nr_palette_idx;
    case 0x41: return (uint8_t)(palette_read() >> 1);
    case 0x42: return N.nr_42_ulanext_format;
    case 0x43: return N.nr_43;
    case 0x44: {
        const uint16_t v = palette_read();
        return (uint8_t)(((v & 0x8000) ? 0x80 : 0) | (v & 1));
    }
    case 0x4a: return N.nr_4a_fallback;
    case 0x4b: return N.nr_4b_sprite_transparent;
    case 0x4c: return N.nr_4c_tm_transparent;
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        return N.mmu[reg - 0x50];
    case 0x61: return (uint8_t)N.nr_copper_addr;
    case 0x62: return (uint8_t)((N.copper_mode << 6) | ((N.nr_copper_addr >> 8) & 7));
    case 0x64: return N.nr_64_copper_offset;
    case 0x68: return (uint8_t)((N.nr_68 & ~0x08) | (N.ulap_en ? 0x08 : 0));
    case 0x69: return (uint8_t)(((int)N.layer2_en << 7) | (((N.port_7ffd >> 3) & 1) << 6) | (N.port_ff & 0x3f));
    case 0x6a: return N.nr_6a;
    case 0x6b: return N.nr_6b;
    case 0x6c: return N.nr_6c;
    case 0x6e: return N.nr_6e;
    case 0x6f: return N.nr_6f;
    case 0x70: return N.nr_70;
    case 0x71: return N.nr_71;
    case 0x7f: return N.nr_7f;
    case 0x80: return N.nr_80;
    case 0x82: return N.nr_82;
    case 0x83: return N.nr_83;
    case 0x84: return N.nr_84;
    case 0x85: return N.nr_85;
    case 0x86: return N.nr_86;
    case 0x87: return N.nr_87;
    case 0x88: return N.nr_88;
    case 0x89: return N.nr_89;
    case 0x8c: return N.nr_8c_altrom;
    case 0x8e:
        return (uint8_t)(((N.port_dffd & 1) << 7) | ((N.port_7ffd & 7) << 4) | 0x08 | ((N.port_1ffd & 1) << 2) |
                         (((N.port_1ffd >> 2) & 1) << 1) |
                         (int)(((N.port_7ffd & 0x10) && !(N.port_1ffd & 1)) || ((N.port_1ffd & 2) && (N.port_1ffd & 1))));
    case 0x8f: return N.nr_8f_mapping_mode;
    case 0xb0: return N.ext_keys_b0;
    case 0xb1: return N.ext_keys_b1;
    case 0xb8: return N.nr_b8;
    case 0xb9: return N.nr_b9;
    case 0xba: return N.nr_ba;
    case 0xbb: return N.nr_bb;
    case 0xc0: return (uint8_t)((N.nr_c0 & 0xe9) | ((N.cpu.im & 3) << 1));
    case 0xc2: return N.nr_c2;
    case 0xc3: return N.nr_c3;
    case 0xc4: return (uint8_t)((N.nr_c4 & 0x80) | ((int)N.nr_22_line_int_en << 1) | (int)!(N.port_ff & 0x40));
    case 0xc5: {
        uint8_t v = 0;
        for (int ch = 0; ch < 4; ++ch)
            if (N.ctc[ch].control & 0x80) v |= (uint8_t)(1 << ch);
        return v;
    }
    case 0xc6: return N.nr_c6;
    case 0xc8: return (uint8_t)(((int)N.int_pending[INT_LINE] << 1) | (int)N.int_pending[INT_ULA]);
    case 0xc9: {
        uint8_t v = 0;
        for (int ch = 0; ch < 4; ++ch)
            if (N.int_pending[INT_CTC0 + ch]) v |= (uint8_t)(1 << ch);
        return v;
    }
    case 0xca: return 0;
    case 0xcc: return N.nr_cc;
    case 0xcd: return N.nr_cd;
    case 0xce: return N.nr_ce;
    default: return N.nr_raw[reg];
    }
}

static void cpu_reset(void) { zd_reset(&N.cpu); }

static void nr_02_w(uint8_t v)
{
    N.nr_02_mf_nmi = v & 0x08;
    N.nr_02_divmmc_nmi = v & 0x04;
    nmi_request();
    if (v & 0x02) {
        N.hard_reset_pending = true;
        machine_reset();
        cpu_reset();
    } else if (v & 0x01) {
        N.nr_02_reset_type = (uint8_t)((((N.nr_02_reset_type >> 2) & 1) << 1) | ((N.nr_02_reset_type >> 1) & 1) |
                                       (N.nr_02_reset_type & 1));
        machine_reset();
        cpu_reset();
    }
}

static void nr_07_w(uint8_t v)
{
    N.nr_07_cpu_speed = (uint8_t)(v & 3);
    N.dma.ticks_per_t = ticks_per_t();
}

static void copper_mode_w(uint8_t mode)
{
    if (mode == N.copper_mode) return;
    N.copper_mode = mode;
    N.copper_budget = 0;
    if (mode == 1 || mode == 3) N.copper_pc = 0;
}

static void reg_w(uint8_t reg, uint8_t v)
{
    switch (reg) {
    case 0x02: nr_02_w(v); break;
    case 0x03:
        N.bootrom_en = false;
        if ((v & 0x80) && !N.nr_03_user_dt_lock && !(v & 0x08)) {
            static const uint8_t timing[8] = {1, 1, 2, 3, 4, 3, 3, 3};
            N.nr_03_machine_timing = timing[(v >> 4) & 7];
        }
        N.nr_03_user_dt_lock = N.nr_03_user_dt_lock ^ ((v >> 3) & 1);
        if (N.nr_03_config_mode && (v & 7) >= 1 && (v & 7) <= 4) N.nr_03_machine_type = (uint8_t)(v & 7);
        if ((v & 7) == 7) N.nr_03_config_mode = true;
        else if ((v & 7) != 0) N.nr_03_config_mode = false;
        bank_update_range(0, 8);
        break;
    case 0x04:
        N.nr_04_romram_bank = (uint8_t)(v & 0x7f);
        bank_update_range(0, 2);
        break;
    case 0x05: N.nr_05 = v; break;
    case 0x06: N.nr_06 = N.nr_03_config_mode ? v : (uint8_t)((v & ~0x04) | (N.nr_06 & 0x04)); break;
    case 0x07: nr_07_w(v); break;
    case 0x08:
        N.nr_08 = (uint8_t)(v & 0x7f);
        if (v & 0x80) N.port_7ffd &= (uint8_t)~0x20;
        break;
    case 0x09:
        N.nr_09 = v;
        if (v & 0x08) port_e3_w((uint8_t)(N.port_e3 & ~0x40));
        break;
    case 0x0a:
        if (N.nr_03_config_mode) {
            N.nr_0a = (uint8_t)((v & 0xc0) | ((v & 0x04) ? (v & 0x20) : (N.nr_0a & 0x20)) | (v & 0x1f));
            N.mf.mode = (uint8_t)(v >> 6);
        } else {
            N.nr_0a = (uint8_t)((N.nr_0a & 0xe0) | (v & 0x1f));
        }
        bank_update_range(0, 2);
        break;
    case 0x0b: N.nr_0b = v; break;
    case 0x10:
        if (N.nr_03_config_mode && !(v & 0x10) && (v & 0x0f) != 0x0f) N.nr_10_coreid = (uint8_t)(v & 0x0f);
        if (v & 0x80) {                 // flash boot: restart the FPGA (a hard reset)
            N.hard_reset_pending = true;
            machine_reset();
            cpu_reset();
        }
        break;
    case 0x11:
        if (N.nr_03_config_mode) N.nr_11_video_timing = 0;  // VGA 0 only
        break;
    case 0x12:
        N.nr_12_layer2_bank = (uint8_t)(v & 0x7f);
        bank_update_range(0, 6);
        break;
    case 0x13:
        N.nr_13_layer2_shadow_bank = (uint8_t)(v & 0x7f);
        bank_update_range(0, 6);
        break;
    case 0x14: N.nr_14_global_transparent = v; break;
    case 0x15:
        N.nr_15 = v;
        N.sprite_cache_valid = false;
        break;
    case 0x16: N.nr_16_l2_scrollx = v; break;
    case 0x17: N.nr_17_l2_scrolly = v; break;
    case 0x18: N.clip_l2[N.clip_idx_l2] = v; N.clip_idx_l2 = (uint8_t)((N.clip_idx_l2 + 1) & 3); break;
    case 0x19: N.clip_spr[N.clip_idx_spr] = v; N.clip_idx_spr = (uint8_t)((N.clip_idx_spr + 1) & 3); break;
    case 0x1a: N.clip_ula[N.clip_idx_ula] = v; N.clip_idx_ula = (uint8_t)((N.clip_idx_ula + 1) & 3); break;
    case 0x1b: N.clip_tm[N.clip_idx_tm] = v; N.clip_idx_tm = (uint8_t)((N.clip_idx_tm + 1) & 3); break;
    case 0x1c:
        if (v & 1) N.clip_idx_l2 = 0;
        if (v & 2) N.clip_idx_spr = 0;
        if (v & 4) N.clip_idx_ula = 0;
        if (v & 8) N.clip_idx_tm = 0;
        break;
    case 0x22:
        N.nr_22_line_int_en = v & 0x02;
        N.nr_23_line_int = (uint16_t)((N.nr_23_line_int & 0xff) | ((v & 1) << 8));
        port_ff_w((uint8_t)((N.port_ff & 0xbf) | ((v & 0x04) << 4)));
        break;
    case 0x23: N.nr_23_line_int = (uint16_t)((N.nr_23_line_int & 0x100) | v); break;
    case 0x26: N.nr_26_ula_scrollx = v; break;
    case 0x27: N.nr_27_ula_scrolly = v; break;
    case 0x2c: dac_w(0x2, v); break;
    case 0x2d: dac_w(0x9, v); break;
    case 0x2e: dac_w(0x4, v); break;
    case 0x2f: N.nr_30_tm_scrollx = (uint16_t)((N.nr_30_tm_scrollx & 0xff) | ((v & 3) << 8)); break;
    case 0x30: N.nr_30_tm_scrollx = (uint16_t)((N.nr_30_tm_scrollx & 0x300) | v); break;
    case 0x31: N.nr_31_tm_scrolly = v; break;
    case 0x32: N.nr_32_lores_scrollx = v; break;
    case 0x33: N.nr_33_lores_scrolly = v; break;
    case 0x34: sprite_mirror_w(v); break;
    case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
    case 0x75: case 0x76: case 0x77: case 0x78: case 0x79:
        N.sprite_mirror_inc = (reg & 0x40) != 0;
        N.sprite_mirror_index = (uint8_t)((reg & 0x3f) - 0x35);
        sprite_mirror_w(v);
        break;
    case 0x40:
        N.nr_palette_idx = v;
        N.nr_palette_sub_idx = false;
        break;
    case 0x41:
        palette_write(0, rgb8_to_9(v));
        if (!(N.nr_43 & 0x80)) ++N.nr_palette_idx;
        N.nr_palette_sub_idx = false;
        break;
    case 0x42: N.nr_42_ulanext_format = v; break;
    case 0x43:
        N.nr_43 = v;
        N.nr_palette_sub_idx = false;
        break;
    case 0x44:
        if (!N.nr_palette_sub_idx) {
            N.nr_stored_palette_value = v;
        } else {
            palette_write((uint8_t)((v >> 6) & 3), (uint16_t)((N.nr_stored_palette_value << 1) | (v & 1)));
            if (!(N.nr_43 & 0x80)) ++N.nr_palette_idx;
        }
        N.nr_palette_sub_idx = !N.nr_palette_sub_idx;
        break;
    case 0x4a: N.nr_4a_fallback = v; break;
    case 0x4b: N.nr_4b_sprite_transparent = v; break;
    case 0x4c: N.nr_4c_tm_transparent = (uint8_t)(v & 0x0f); break;
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        mmu_w(reg - 0x50, v);
        break;
    case 0x60:
    case 0x63: {
        const bool write8 = reg == 0x60;
        if (!(N.nr_copper_addr & 1)) {
            if (write8) N.copper_ram[N.nr_copper_addr] = v;
            N.nr_copper_data_stored = v;
        } else {
            if (!write8) N.copper_ram[N.nr_copper_addr & 0x7fe] = N.nr_copper_data_stored;
            N.copper_ram[N.nr_copper_addr] = v;
        }
        N.nr_copper_addr = (uint16_t)((N.nr_copper_addr + 1) & 0x7ff);
        break;
    }
    case 0x61: N.nr_copper_addr = (uint16_t)((N.nr_copper_addr & 0x700) | v); break;
    case 0x62:
        copper_mode_w((uint8_t)((v >> 6) & 3));
        N.nr_copper_addr = (uint16_t)((N.nr_copper_addr & 0xff) | ((v & 7) << 8));
        break;
    case 0x64: N.nr_64_copper_offset = v; break;
    case 0x68:
        N.nr_68 = v;
        N.ulap_en = v & 0x08;
        break;
    case 0x69:
        port_ff_w((uint8_t)((N.port_ff & 0xc0) | (v & 0x3f)));
        N.port_7ffd = (uint8_t)((N.port_7ffd & ~0x08) | ((v >> 3) & 0x08));
        N.layer2_en = v & 0x80;
        break;
    case 0x6a: N.nr_6a = (uint8_t)(v & 0x3f); break;
    case 0x6b: N.nr_6b = v; break;
    case 0x6c: N.nr_6c = v; break;
    case 0x6e: N.nr_6e = (uint8_t)(v & 0xbf); break;
    case 0x6f: N.nr_6f = (uint8_t)(v & 0xbf); break;
    case 0x70: N.nr_70 = (uint8_t)(v & 0x3f); break;
    case 0x71: N.nr_71 = (uint8_t)(v & 1); break;
    case 0x7f: N.nr_7f = v; break;
    case 0x80: N.nr_80 = v; break;
    case 0x82: N.nr_82 = v; break;
    case 0x83: N.nr_83 = v; bank_update_range(0, 2); break;
    case 0x84: N.nr_84 = v; break;
    case 0x85: N.nr_85 = (uint8_t)(v & 0x8f); break;
    case 0x86: N.nr_86 = v; break;
    case 0x87: N.nr_87 = v; break;
    case 0x88: N.nr_88 = v; break;
    case 0x89: N.nr_89 = (uint8_t)(v & 0x8f); break;
    case 0x8c:
        N.nr_8c_altrom = v;
        bank_update_range(0, 2);
        break;
    case 0x8e:
        if (v & 0x08) {
            N.port_dffd = (uint8_t)((N.port_dffd & ~0x08 & ~0x07) | ((v >> 7) & 1));
            N.port_7ffd = (uint8_t)((N.port_7ffd & ~0x07) | ((v >> 4) & 7));
        }
        if (!(v & 0x04)) N.port_7ffd = (uint8_t)((N.port_7ffd & ~0x10) | ((v & 1) << 4));
        N.port_1ffd = (uint8_t)((N.port_1ffd & ~0x07) | (((v >> 1) & 1) << 2) | ((v & 1) << 1) | ((v >> 2) & 1));
        memory_change(0x8e, v);
        bank_update_range(0, 2);
        break;
    case 0x8f:
        N.nr_8f_mapping_mode = (uint8_t)(v & 3);
        memory_change(0x8f, v);
        break;
    case 0xb8: N.nr_b8 = v; break;
    case 0xb9: N.nr_b9 = v; break;
    case 0xba: N.nr_ba = v; break;
    case 0xbb: N.nr_bb = v; break;
    case 0xc0:
        N.nr_c0 = v;
        N.cpu.nmi_stackless = v & 0x08;
        break;
    case 0xc2: N.nr_c2 = v; break;
    case 0xc3: N.nr_c3 = v; break;
    case 0xc4:
        N.nr_c4 = v;
        N.nr_22_line_int_en = v & 0x02;
        port_ff_w((uint8_t)((N.port_ff & 0xbf) | ((~v & 1) << 6)));
        break;
    case 0xc5:
        for (int ch = 0; ch < 4; ++ch) {
            ctc_t *c = &N.ctc[ch];
            c->control = (uint8_t)(((v >> ch) & 1) ? (c->control | 0x80) : (c->control & ~0x80));
        }
        break;
    case 0xc6: N.nr_c6 = v; break;
    case 0xc8:                          // writing 1s clears the pending interrupts
        if (v & 1) int_clear(INT_ULA);
        if (v & 2) int_clear(INT_LINE);
        break;
    case 0xc9:
        for (int ch = 0; ch < 4; ++ch)
            if (v & (1 << ch)) int_clear(INT_CTC0 + ch);
        break;
    case 0xcc: N.nr_cc = v; break;
    case 0xcd: N.nr_cd = v; break;
    case 0xce: N.nr_ce = v; break;
    default: N.nr_raw[reg] = v; break;
    }
    if (reg != 0x34 && !(reg >= 0x35 && reg <= 0x39) && !(reg >= 0x75 && reg <= 0x79)) {
        N.sprite_mirror_index = 7;
        N.sprite_mirror_inc = false;
    }
}

static void nextreg_w_cb(void *user, uint8_t reg, uint8_t v) { (void)user; reg_w(reg, v); }
static uint8_t nextreg_r_cb(void *user, uint8_t reg) { (void)user; return reg_r(reg); }

// ---------------------------------------------------------------------------
// Interrupt controller (the hardware IM2 daisy chain: line, CTC 0-3, ULA)

static uint8_t int_vector(int source)
{
    if (!(N.nr_c0 & 1)) return 0xff;
    return (uint8_t)((N.nr_c0 & 0xe0) | (source << 1));
}

static void int_raise(int source)
{
    if (N.int_service[source] && (N.nr_c0 & 1)) return;   // being serviced
    N.int_pending[source] = true;
}

static void int_clear(int source) { N.int_pending[source] = false; }

static void update_irq(void)
{
    int src = -1;
    for (int s = 0; s < INT_COUNT; ++s) {
        if (N.int_service[s] && (N.nr_c0 & 1)) break;
        if (N.int_pending[s]) {
            src = s;
            break;
        }
    }
    const bool assert_line = src >= 0;
    if (assert_line != N.irq_asserted || assert_line) {
        N.irq_asserted = assert_line;
        zd_set_irq(&N.cpu, assert_line ? ZD_ASSERT : ZD_CLEAR, assert_line ? int_vector(src) : 0xff);
    }
}

static void irq_acknowledge(void *user)
{
    (void)user;
    for (int s = 0; s < INT_COUNT; ++s) {
        if (N.int_service[s] && (N.nr_c0 & 1)) break;
        if (N.int_pending[s]) {
            N.int_pending[s] = false;
            if (N.nr_c0 & 1) N.int_service[s] = true;
            zd_set_irq(&N.cpu, ZD_ASSERT, int_vector(s));
            return;
        }
    }
}

static void irq_reti(void)
{
    for (int s = 0; s < INT_COUNT; ++s) {
        if (N.int_service[s]) {
            N.int_service[s] = false;
            return;
        }
    }
}

static void ret_cb(void *user, bool reti)
{
    (void)user;
    if (reti) irq_reti();
    else leave_nmi();                   // RETN
}

// ---------------------------------------------------------------------------
// Video timing and the per-instruction event loop

static void update_video_mode(void)
{
    const uint8_t t = N.nr_03_machine_timing;
    const bool hz60 = (N.nr_05 & 0x04) != 0;
    if (t & 4) {                        // Pentagon (always 50 Hz)
        N.vt = (video_timings){448 + 3 - 12, 128, 447, 319, 80, 319};
    } else if (t & 2) {                 // 128K / +3
        const int int_h = (t & 1) ? 136 + 2 - 12 : 136 + 4 - 12;
        N.vt = hz60 ? (video_timings){int_h, 136, 455, 0, 40, 263} : (video_timings){int_h, 136, 455, 1, 64, 310};
    } else {                            // 48K
        N.vt = hz60 ? (video_timings){116, 128, 447, 0, 40, 263} : (video_timings){116, 128, 447, 0, 64, 311};
    }
    N.eff_machine_timing = t;
    N.eff_5060 = hz60 && !(t & 4);
    N.line_ticks = (N.vt.max_hc + 1) * 4;
    N.frame_ticks = N.line_ticks * (N.vt.max_vc + 1);
}

double specnext_fps(void) { return N.frame_ticks > 0 ? (double)SN_CLOCK / (double)N.frame_ticks : 50.0; }

static void copper_run(int ticks)
{
    if (N.copper_mode == 0) return;
    N.copper_budget += ticks;
    const int vc = imin(N.frame_tick / N.line_ticks, N.vt.max_vc);
    const int hc = (N.frame_tick % N.line_ticks) / 4;
    for (int guard = 0; N.copper_budget >= 2 && guard < 1024; ++guard) {
        const uint16_t ins = (uint16_t)((N.copper_ram[N.copper_pc << 1] << 8) | N.copper_ram[(N.copper_pc << 1) | 1]);
        if (ins & 0x8000) {             // WAIT line, h*8
            const int wv = ins & 0x1ff;
            const int wh = ((ins >> 9) & 0x3f) << 3;
            if (wv > N.vt.max_vc || wh > N.vt.max_hc) {
                N.copper_budget = 0;    // never satisfied
                return;
            }
            // the copper's coordinates: lines from the first paper line
            // (plus nr_64), horizontal from the left edge of the paper
            int cur_h = hc - N.vt.min_hactive;
            int cur_v = vpos_to_cvc(vc);
            if (cur_h < 0) {
                cur_h += N.vt.max_hc + 1;
                cur_v = (cur_v + N.vt.max_vc) % (N.vt.max_vc + 1);
            }
            if (cur_v == wv && cur_h >= wh) {
                N.copper_pc = (uint16_t)((N.copper_pc + 1) & 0x3ff);
                N.copper_budget -= 1;
                continue;
            }
            N.copper_budget = 0;
            return;
        }
        N.copper_pc = (uint16_t)((N.copper_pc + 1) & 0x3ff);
        N.copper_budget -= 2;
        if (ins & 0x7f00) reg_w((uint8_t)((ins >> 8) & 0x7f), (uint8_t)ins);
    }
    if (N.copper_budget > 64) N.copper_budget = 64;
}

static void mix_audio(int ticks)
{
    N.audio_acc += (int64_t)ticks * SN_AUDIO_RATE;
    while (N.audio_acc >= (int64_t)SN_CLOCK) {
        N.audio_acc -= (int64_t)SN_CLOCK;
        if (N.turbo) continue;
        // three AYs (the second and third with TurboSound), the beeper / MIC
        // bits and the four DACs, then a DC-blocking filter
        int32_t s = ay8910_render(&N.ay[0]);
        if (N.nr_08 & 0x02) s += ay8910_render(&N.ay[1]) + ay8910_render(&N.ay[2]);
        s = s * 3 / 4;
        s += ((N.port_fe & 0x10) ? 5000 : 0) + ((N.port_fe & 0x08) ? 800 : 0);
        if (N.nr_08 & 0x08)
            for (int i = 0; i < 4; i++) s += ((int)N.dac[i] - 0x80) * 24;
        const float x = (float)s;
        N.dc_y = x - N.dc_x + 0.9975f * N.dc_y;
        N.dc_x = x;
        int32_t o = (int32_t)N.dc_y;
        if (o < -32768) o = -32768;
        if (o > 32767) o = 32767;
        if (N.naudio < SN_AUDIO_MAX) N.audio[N.naudio++] = (int16_t)o;
    }
}

static void line_event(int vc)
{
    const int out_row = vc - (N.vt.min_vactive - 32);
    if (out_row >= 0 && out_row < SN_H) render_line(out_row);
    if (N.nr_22_line_int_en && N.nr_23_line_int <= N.vt.max_vc) {
        const int target = cvc_to_vpos(N.nr_23_line_int ? N.nr_23_line_int - 1 : N.vt.max_vc);
        if (vc == target) {
            int_raise(INT_LINE);
            if (!(N.nr_c0 & 1)) N.pulse_left = irq_pulse_cycles();
        }
    }
}

static const char *load_nex_now(const uint8_t *data, uint32_t size);

static void check_os_started(void)
{
    if (N.bootrom_en || N.nr_03_config_mode) return;
    ++N.os_frames;
    if (N.pending_nex && N.os_frames >= 250 && !N.divmmc.held && !(N.port_e3 & 0x80)) {
        uint8_t *nex = N.pending_nex;
        const uint32_t n = N.pending_nex_size;
        N.pending_nex = NULL;
        const char *err = load_nex_now(nex, n);
        if (err) fprintf(stderr, "specnext: %s\n", err);
        free(nex);
    }
}

static void frame_start(void)
{
    ++N.frame_count;
    if (++N.flash_counter >= 32) N.flash_counter = 0;
    if (N.nr_03_machine_timing != N.eff_machine_timing || ((N.nr_05 & 4) != 0) != N.eff_5060) {
        update_video_mode();
        N.frame_tick = imin(N.frame_tick, N.frame_ticks - 1);
    }
    N.render_vc = 0;
    N.ula_int_done = false;
    N.copper_frame_pending = true;
    check_os_started();
}

static void advance(int ticks)
{
    N.frame_tick += ticks;
    for (;;) {
        const int render_tick = N.render_vc * N.line_ticks + (N.vt.min_hactive + 256) * 4;
        if (N.render_vc > N.vt.max_vc || N.frame_tick < render_tick) break;
        line_event(N.render_vc);
        ++N.render_vc;
    }
    if (!N.ula_int_done && N.frame_tick >= N.vt.int_v * N.line_ticks + N.vt.int_h * 4) {
        N.ula_int_done = true;
        if (!(N.port_ff & 0x40)) {
            int_raise(INT_ULA);
            if (!(N.nr_c0 & 1)) N.pulse_left = irq_pulse_cycles();
        }
    }
    if (N.copper_frame_pending && N.frame_tick >= cvc_to_vpos(0) * N.line_ticks + N.vt.int_h * 4) {
        N.copper_frame_pending = false;
        if (N.copper_mode == 3) {
            N.copper_pc = 0;
            N.copper_budget = 0;
        }
    }
    copper_run(ticks);
    ctc_tick(ticks);
    zxn_dma_advance(&N.dma, ticks);
    mix_audio(ticks);
    if (N.frame_tick >= N.frame_ticks) {
        N.frame_tick -= N.frame_ticks;
        N.frame_done = true;
        frame_start();
    }
}

static void on_cycles(void *user, int t_states)
{
    (void)user;
    if (N.pulse_left > 0) {
        N.pulse_left -= t_states;
        if (N.pulse_left <= 0 && !(N.nr_c0 & 1)) {
            int_clear(INT_ULA);
            int_clear(INT_LINE);
            N.int_service[INT_ULA] = N.int_service[INT_LINE] = false;
        }
    }
    advance(t_states * ticks_per_t());
    // a running DMA owns the bus: the CPU waits until the burst is done
    for (int guard = 0; zxn_dma_active(&N.dma) && guard < 4096; ++guard) {
        N.dma.ticks_per_t = ticks_per_t();
        const int used = zxn_dma_run(&N.dma, N.line_ticks);
        if (used <= 0) break;
        advance(used);
    }
    update_irq();
}

// ---------------------------------------------------------------------------
// Reset

// Values the FPGA sets on power up / hard reset only (reset_hard())
static void reset_hard_registers(void)
{
    N.hard_reset_pending = false;
    N.bootrom_en = true;
    N.nr_80 = 0;
    N.nr_8c_altrom = 0;
    N.nr_8f_mapping_mode = 0;
    N.nr_03_machine_timing = 3;
    N.nr_03_user_dt_lock = false;
    N.nr_03_config_mode = true;
    N.nr_03_machine_type = 3;
    N.nr_04_romram_bank = 0;
    N.nr_05 = 0x40;                     // joystick 0 = Kempston 1, joystick 1 = Sinclair 2, 50 Hz
    N.nr_06 = 0xa0;                     // hotkeys enabled
    N.nr_08 = 0x10;                     // internal speaker
    N.nr_09 = 0;
    N.nr_0a = 0x11;                     // DivMMC automap, mouse dpi 1
    N.nr_10_coreid = 0;
    N.mf.mode = 0;
    N.nr_7f = 0xff;
    N.nr_82 = N.nr_83 = N.nr_84 = 0xff;
    N.nr_85 = 0x8f;
    N.nr_86 = N.nr_87 = N.nr_88 = 0xff;
    N.nr_89 = 0x8f;
    N.nr_02_reset_type = 4;
    N.eff_machine_timing = 3;
    N.eff_5060 = false;
    memset(N.nr_raw, 0, sizeof N.nr_raw);
    // palettes: the ULA's the Spectrum colours, the others RRRGGGBB = index
    for (int p = 0; p < 8; ++p)
        for (int i = 0; i < 256; ++i) N.palette[p][i] = rgb8_to_9((p & 3) == 0 ? kUlaDefault[i & 15] : (uint8_t)i);
    memset(N.sram, 0, (size_t)RAM_PAGES * 0x2000);
}

static void set_clip_defaults(void)
{
    static const uint8_t l2[4] = {0x00, 0xff, 0x00, 0xbf}, tm[4] = {0x00, 0x9f, 0x00, 0xff};
    memcpy(N.clip_l2, l2, 4);
    memcpy(N.clip_spr, l2, 4);
    memcpy(N.clip_ula, l2, 4);
    memcpy(N.clip_tm, tm, 4);
}

static void machine_reset(void)
{
    if (N.hard_reset_pending) reset_hard_registers();

    for (int i = 0; i < 3; ++i) {
        ay8910_reset(&N.ay[i]);
        ay8910_address_w(&N.ay[i], 7);
        ay8910_data_w(&N.ay[i], 0xff);
    }
    memset(N.dac, 0x80, sizeof N.dac);
    port_ff_w(0);
    N.port_fe = 0;
    N.divmmc_delayed_check = false;
    dm_reset_lines(&N.divmmc);
    mf_defaults();
    N.nr_02_mf_nmi = N.nr_02_divmmc_nmi = false;
    N.cpu.nmi_stackless = false;
    zxn_dma_reset(&N.dma);
    memset(N.int_pending, 0, sizeof N.int_pending);
    memset(N.int_service, 0, sizeof N.int_service);
    N.pulse_left = 0;
    N.irq_asserted = false;
    zd_set_irq(&N.cpu, ZD_CLEAR, 0xff);
    zd_set_nmi(&N.cpu, ZD_CLEAR);
    N.nr_c2 = N.nr_c3 = 0;
    N.nr_80 = (uint8_t)((N.nr_80 << 4) | (N.nr_80 & 0x0f));
    N.nr_8c_altrom = (uint8_t)((N.nr_8c_altrom << 4) | (N.nr_8c_altrom & 0x0f));
    port_e7_w(0xff);

    N.port_7ffd = 0;
    N.port_1ffd = 0;
    N.port_1ffd_special_old = false;
    N.port_dffd = 0;
    N.port_eff7 = 0;
    N.layer2_en = N.layer2_map_wr = N.layer2_map_rd = N.layer2_map_shadow = false;
    N.layer2_map_segment = N.layer2_offset = 0;
    N.port_e3 = 0;
    N.ulap_mode = N.ulap_index = 0;
    N.ulap_en = false;
    N.nr_register = 0x24;
    nr_07_w(0);
    N.nr_06 |= 0xa0;
    N.nr_08 &= (uint8_t)~0x40;
    N.nr_09 &= (uint8_t)~0x10;
    N.nr_0b = 0x01;
    N.nr_12_layer2_bank = 8;
    N.nr_13_layer2_shadow_bank = 11;
    N.nr_14_global_transparent = 0xe3;
    N.nr_15 = 0;
    N.nr_16_l2_scrollx = N.nr_17_l2_scrolly = 0;
    set_clip_defaults();
    N.clip_idx_l2 = N.clip_idx_spr = N.clip_idx_ula = N.clip_idx_tm = 0;
    N.nr_22_line_int_en = false;
    N.nr_23_line_int = 0;
    N.nr_26_ula_scrollx = N.nr_27_ula_scrolly = 0;
    N.nr_30_tm_scrollx = 0;
    N.nr_31_tm_scrolly = N.nr_32_lores_scrollx = N.nr_33_lores_scrolly = 0;
    N.nr_palette_idx = 0;
    N.nr_palette_sub_idx = false;
    N.nr_42_ulanext_format = 7;
    N.nr_43 = 0;
    N.nr_stored_palette_value = 0;
    N.nr_4a_fallback = 0xe3;
    N.nr_4b_sprite_transparent = 0xe3;
    N.nr_4c_tm_transparent = 0x0f;
    N.copper_mode = 0;
    N.copper_pc = 0;
    N.copper_budget = 0;
    N.nr_copper_addr = 0;
    N.nr_copper_data_stored = 0;
    N.nr_64_copper_offset = 0;
    N.nr_68 = 0;
    N.nr_6a = 0;
    N.nr_6b = 0;
    N.nr_6c = 0;
    N.nr_6e = 0x2c;
    N.nr_6f = 0x0c;
    N.nr_70 = 0;
    N.nr_71 = 0;
    if (N.nr_85 & 0x80) {
        N.nr_82 = N.nr_83 = N.nr_84 = 0xff;
        N.nr_85 = (uint8_t)((N.nr_85 & 0x80) | 0x0f);
    }
    if (!(N.nr_89 & 0x80)) {
        N.nr_86 = N.nr_87 = N.nr_88 = 0xff;
        N.nr_89 = (uint8_t)((N.nr_89 & 0x80) | 0x0f);
    }
    N.nr_b8 = 0x83;
    N.nr_b9 = 0x01;
    N.nr_ba = 0x00;
    N.nr_bb = 0xcd;
    N.nr_c0 = 0;
    N.nr_c4 = 0x80;
    N.nr_c6 = 0;
    N.nr_cc = N.nr_cd = N.nr_ce = 0;
    if (N.nr_03_config_mode) N.bootrom_en = true;

    memset(N.sprite_attr, 0, sizeof N.sprite_attr);
    N.sprite_attr_index = N.sprite_pattern_index = 0;
    N.sprite_mirror_q = 0;
    N.sprite_mirror_index = 7;
    N.sprite_mirror_inc = false;
    N.sprite_cache_valid = false;
    memset(N.copper_ram, 0, sizeof N.copper_ram);
    ctc_defaults();

    mmu_x2_w(0, 0xff);
    mmu_x2_w(2, 0x0a);
    mmu_x2_w(4, 0x04);
    mmu_x2_w(6, 0x00);
    bank_update_range(0, 8);

    N.ay_select = 0;
    update_video_mode();
    N.frame_tick = 0;
    N.render_vc = 0;
    N.ula_int_done = false;
    N.copper_frame_pending = false;
}

void specnext_hard_reset(void)
{
    N.hard_reset_pending = true;
    machine_reset();
    cpu_reset();
    N.os_frames = -1;
}

void specnext_soft_reset(void)
{
    machine_reset();
    cpu_reset();
}

// ---------------------------------------------------------------------------
// Video: ULA / LoRes, tilemap, Layer 2 and sprites drawn into line buffers at
// 640 pixels, mixed with the layer priority (nr_15) and blend modes, the
// global transparency (nr_14) and the fallback colour (nr_4A)

#define TRANSPARENT 0x8000
#define FLAG 0x4000                     // tilemap: below the ULA; Layer 2: priority colour

static inline bool opaque(uint16_t v) { return !(v & TRANSPARENT); }
static inline bool global_transparent(uint16_t rgb9) { return (rgb9 >> 1) == N.nr_14_global_transparent; }
static inline uint16_t ula_row_offset(int y) { return (uint16_t)(((y & 7) << 8) | ((y & 0x38) << 2) | ((y & 0xc0) << 5)); }
static inline const uint8_t *bank5(void) { return &N.sram[0x40000 + 5 * 0x4000]; }
static inline const uint8_t *bank7(void) { return &N.sram[0x40000 + 7 * 0x4000]; }

static inline uint16_t mix_ch(uint16_t a, uint16_t b, int bias, int shift)
{
    int v = ((a >> shift) & 7) + ((b >> shift) & 7) - bias;
    if (v < 0) v = 0;
    if (v > 7) v = 7;
    return (uint16_t)(v << shift);
}
static inline uint16_t mix_add(uint16_t a, uint16_t b, int bias)
{
    return (uint16_t)(mix_ch(a, b, bias, 6) | mix_ch(a, b, bias, 3) | mix_ch(a, b, bias, 0));
}

typedef struct {
    bool ulanext, ulap;
    const uint16_t *pal;
    int ink_mask, pap_shift;
    uint16_t fallback;
} ula_ctx;

static inline uint16_t ula_colour(const ula_ctx *u, int idx)
{
    const uint16_t c = idx < 0 ? u->fallback : (uint16_t)(u->pal[idx] & 0x1ff);
    return global_transparent(c) ? TRANSPARENT : c;
}

// {paper, ink} palette indices (-1: the fallback colour)
static inline void ula_parse(const ula_ctx *u, uint8_t attr, int *paper, int *ink)
{
    if (u->ulanext) {
        *ink = attr & u->ink_mask;
        *paper = N.nr_42_ulanext_format == 0xff ? -1 : ((attr >> u->pap_shift) | 0x80) & 0xff;
    } else if (u->ulap) {
        *ink = 0xc0 | ((attr & 0xc0) >> 2) | (attr & 7);
        *paper = 0xc8 | ((attr & 0xc0) >> 2) | ((attr >> 3) & 7);
    } else {
        *ink = ((attr & 0x40) >> 3) | (attr & 7);
        *paper = (((attr & 0x40) >> 3) | ((attr >> 3) & 7)) | 0x10;
    }
}

static void render_lores(int row, uint16_t *out)
{
    const uint16_t *pal = N.palette[(N.nr_43 & 0x02) ? 4 : 0];
    const int y = row - 32;
    const int ys = ((y + N.nr_33_lores_scrolly) % 192) >> 1;
    const bool radastan = N.nr_6a & 0x20;
    const bool dfile = ((N.port_ff & 1) != 0) != ((N.nr_6a & 0x10) != 0);
    const int pal_off = ((N.ulap_en && !(N.nr_43 & 1)) ? 0xc : 0) | (N.nr_6a & 0x0f);
    const uint8_t *scr = bank5();
    for (int sx = 64; sx < 576; ++sx) {
        const int x4 = (sx - 64 + N.nr_32_lores_scrollx * 2 + 512) & 511;
        const int px = x4 >> 2;
        int idx;
        if (radastan) {
            const uint8_t b = scr[(px >> 1) + 64 * ys + (dfile ? 0x2000 : 0)];
            idx = (pal_off << 4) | ((px & 1) ? (b & 0x0f) : (b >> 4));
        } else {
            idx = scr[ys < 48 ? ys * 128 + px : 0x2000 + (ys - 48) * 128 + px];
        }
        const uint16_t c = (uint16_t)(pal[idx & 0xff] & 0x1ff);
        out[sx] = global_transparent(c) ? TRANSPARENT : c;
    }
}

static void render_ula(int row, uint16_t *out)
{
    ula_ctx u;
    u.ulanext = N.nr_43 & 0x01;
    u.ulap = N.ulap_en && !u.ulanext;
    u.pal = N.palette[(N.nr_43 & 0x02) ? 4 : 0];
    const bool shadow = N.port_7ffd & 0x08;
    const int mode = shadow ? 0 : (N.port_ff & 7);
    u.fallback = (uint16_t)((N.nr_4a_fallback << 1) | ((N.nr_4a_fallback & 3) ? 1 : 0));
    u.ink_mask = 0xff;
    u.pap_shift = 8;
    switch (N.nr_42_ulanext_format) {
    case 0x01: u.ink_mask = 0x01; u.pap_shift = 1; break;
    case 0x03: u.ink_mask = 0x03; u.pap_shift = 2; break;
    case 0x07: u.ink_mask = 0x07; u.pap_shift = 3; break;
    case 0x0f: u.ink_mask = 0x0f; u.pap_shift = 4; break;
    case 0x1f: u.ink_mask = 0x1f; u.pap_shift = 5; break;
    case 0x3f: u.ink_mask = 0x3f; u.pap_shift = 6; break;
    case 0x7f: u.ink_mask = 0x7f; u.pap_shift = 7; break;
    default: break;
    }

    // border
    const int border = N.port_fe & 7;
    int bpaper, bink;
    if (u.ulanext) ula_parse(&u, (uint8_t)(border << u.pap_shift), &bpaper, &bink);
    else if (mode == 6) ula_parse(&u, (uint8_t)(0x40 | (~N.port_ff & 0x38)), &bpaper, &bink);
    else ula_parse(&u, (uint8_t)(border << 3), &bpaper, &bink);
    const uint16_t border_c = ula_colour(&u, bpaper);

    const int y = row - 32;
    const bool paper_line = y >= 0 && y < 192;
    for (int sx = 0; sx < SN_W; ++sx) out[sx] = (paper_line && sx >= 64 && sx < 576) ? TRANSPARENT : border_c;
    if (!paper_line) return;

    const uint8_t clip_y2 = (N.clip_ula[3] & 0xc0) == 0xc0 ? 0xbf : N.clip_ula[3];
    if (y < N.clip_ula[2] || y > clip_y2) return;
    const int cx1 = N.clip_ula[0] * 2, cx2 = N.clip_ula[1] * 2 + 1;

    if (N.nr_15 & 0x80) {               // LoRes replaces the ULA paper
        render_lores(row, out);
        for (int sx = 64; sx < 576; ++sx)
            if (sx - 64 < cx1 || sx - 64 > cx2) out[sx] = TRANSPARENT;
        return;
    }

    const uint8_t *scr = shadow ? bank7() : bank5();
    if (mode == 1) scr += 0x2000;
    const int ys = (y + N.nr_27_ula_scrolly) % 192;
    const uint16_t roff = ula_row_offset(ys);
    const int fine = (N.nr_68 & 0x04) ? 1 : 0;
    const bool flash_on = !u.ulanext && !u.ulap && N.flash_counter >= 16;

    if (mode == 6) {                    // Timex 512x192
        int paper, ink;
        ula_parse(&u, (uint8_t)(0x40 | (~N.port_ff & 0x38) | ((N.port_ff >> 3) & 7)), &paper, &ink);
        const uint16_t cp = ula_colour(&u, paper), ci = ula_colour(&u, ink);
        const uint8_t *base = shadow ? bank7() : bank5();
        for (int sx = 64; sx < 576; ++sx) {
            const int rel = sx - 64;
            if (rel < cx1 || rel > cx2) continue;
            const int x = (rel + N.nr_26_ula_scrollx * 2 + fine) & 511;
            const int column = x >> 3;
            const uint8_t byte = base[roff + (column >> 1) + ((column & 1) ? 0x2000 : 0)];
            out[sx] = ((byte >> (7 - (x & 7))) & 1) ? ci : cp;
        }
        return;
    }

    int last_attr = -1;
    uint16_t c_ink = 0, c_paper = 0;
    for (int sx = 64; sx < 576; ++sx) {
        const int rel = sx - 64;
        if (rel < cx1 || rel > cx2) continue;
        const int x = ((rel + N.nr_26_ula_scrollx * 2 + fine) & 511) >> 1;
        const uint8_t byte = scr[roff + (x >> 3)];
        const uint8_t attr = mode == 2 ? scr[0x2000 + roff + (x >> 3)] : scr[0x1800 + ((ys >> 3) << 5) + (x >> 3)];
        bool bit = (byte >> (7 - (x & 7))) & 1;
        if (flash_on && (attr & 0x80)) bit = !bit;
        if (attr != last_attr) {
            int paper, ink;
            ula_parse(&u, attr, &paper, &ink);
            c_ink = ula_colour(&u, ink);
            c_paper = ula_colour(&u, paper);
            last_attr = attr;
        }
        out[sx] = bit ? c_ink : c_paper;
    }
}

static void fill_transparent(uint16_t *out)
{
    for (int sx = 0; sx < SN_W; ++sx) out[sx] = TRANSPARENT;
}

static void render_tilemap(int row, uint16_t *out)
{
    fill_transparent(out);
    const uint8_t ctrl = N.nr_6b;
    if (!(ctrl & 0x80)) return;
    if (row < N.clip_tm[2] || row > N.clip_tm[3]) return;
    const bool cols80 = ctrl & 0x40;
    const bool noattr = ctrl & 0x20;
    const uint16_t *pal = N.palette[(ctrl & 0x10) ? 7 : 3];
    const bool text = ctrl & 0x08;
    const bool t512 = ctrl & 0x02;
    const bool on_top = ctrl & 0x01;
    const uint8_t *map = (N.nr_6e & 0x80) ? bank7() : bank5();
    const int map_mask = (N.nr_6e & 0x80) ? 0x1fff : 0x3fff;
    const int map_off = (N.nr_6e & 0x3f) << 8;
    const uint8_t *tiles = (N.nr_6f & 0x80) ? bank7() : bank5();
    const int tile_mask = (N.nr_6f & 0x80) ? 0x1fff : 0x3fff;
    const int tile_off = (N.nr_6f & 0x3f) << 8;
    const int ys = (row + N.nr_31_tm_scrolly) & 255;
    const int trow = ys >> 3, py = ys & 7;
    const int cols = cols80 ? 80 : 40;
    const int x1 = N.clip_tm[0] * 2, x2 = imin(N.clip_tm[1] * 2 + 1, 319);
    const uint8_t tm_transp = N.nr_4c_tm_transparent & 0x0f;

    int cached_col = -1;
    uint8_t tile_lo = 0, attr = 0;
    for (int sx = 0; sx < SN_W; ++sx) {
        const int x320 = sx >> 1;
        if (x320 < x1 || x320 > x2) continue;
        const int tx = cols80 ? (sx + N.nr_30_tm_scrollx) % 640 : (x320 + N.nr_30_tm_scrollx) % 320;
        const int tcol = tx >> 3, pxx = tx & 7;
        if (tcol != cached_col) {
            cached_col = tcol;
            const int index = trow * cols + tcol;
            const int addr = map_off + index * (noattr ? 1 : 2);
            tile_lo = map[addr & map_mask];
            attr = noattr ? N.nr_6c : map[(addr + 1) & map_mask];
        }
        int tile = tile_lo;
        if (t512) tile |= (attr & 1) << 8;
        const bool below = !t512 && (attr & 1) && !on_top;
        uint16_t v;
        if (text) {
            const uint8_t def = tiles[(tile_off + tile * 8 + py) & tile_mask];
            const int idx = (attr & 0xfe) | ((def >> (7 - pxx)) & 1);
            const uint16_t c = (uint16_t)(pal[idx] & 0x1ff);
            v = global_transparent(c) ? TRANSPARENT : c;
        } else {
            const int fx = (attr & 0x08) ? 7 - pxx : pxx;
            const int fy = (attr & 0x04) ? 7 - py : py;
            int col = fx, r = fy;
            if (attr & 0x02) {
                col = fy;
                r = 7 - fx;
            }
            const uint8_t b = tiles[(tile_off + tile * 32 + r * 4 + (col >> 1)) & tile_mask];
            const int nib = (col & 1) ? (b & 0x0f) : (b >> 4);
            v = nib == tm_transp ? TRANSPARENT : (uint16_t)(pal[(attr & 0xf0) | nib] & 0x1ff);
        }
        if (opaque(v) && below) v |= FLAG;
        out[sx] = v;
    }
}

static inline void l2_put(uint16_t *out, const uint16_t *pal, int sx, int idx)
{
    const uint16_t e = pal[idx & 0xff];
    const uint16_t c = (uint16_t)(e & 0x1ff);
    if (global_transparent(c)) return;
    out[sx] = (uint16_t)(c | ((e & 0x8000) ? FLAG : 0));
}

static void render_layer2(int row, uint16_t *out)
{
    fill_transparent(out);
    if (!N.layer2_en) return;
    const int res = (N.nr_70 >> 4) & 3;
    const int pal_off = N.nr_70 & 0x0f;
    const uint16_t *pal = N.palette[(N.nr_43 & 0x04) ? 5 : 1];
    const size_t mask = (size_t)RAM_PAGES * 0x2000 - 1;
    const size_t base = 0x40000 + (size_t)N.nr_12_layer2_bank * 0x4000;
    const int scroll_x = ((N.nr_71 & 1) << 8) | N.nr_16_l2_scrollx;
    const int scroll_y = N.nr_17_l2_scrolly;

    if (res == 0) {                     // 256x192, row major
        const int y = row - 32;
        if (y < 0 || y >= 192) return;
        if (y < N.clip_l2[2] || y > imin(N.clip_l2[3], 191)) return;
        int ys = y + scroll_y;
        if (scroll_y >= 192) ys -= 192;
        else ys %= 192;
        for (int x = N.clip_l2[0]; x <= N.clip_l2[1]; ++x) {
            const int xs = (x + scroll_x) & 255;
            const uint8_t pix = N.sram[(base + (size_t)ys * 256 + (size_t)xs) & mask];
            const int idx = (pix + (pal_off << 4)) & 0xff;
            const int sx = (x + 32) * 2;
            l2_put(out, pal, sx, idx);
            out[sx + 1] = out[sx];
        }
        return;
    }
    // 320x256 (8 bpp) and 640x256 (4 bpp), column major
    if (row < N.clip_l2[2] || row > imin(N.clip_l2[3], 255)) return;
    const int ys = (row + scroll_y) & 255;
    const int x1 = N.clip_l2[0] * 2;
    const int x2 = imin(N.clip_l2[1] + 1, 160) * 2 - 1;
    const bool scrollover = scroll_x >= 320;
    for (int x = x1; x <= x2; ++x) {
        int xs = x + scroll_x;
        if (scrollover) xs -= 320;
        else xs %= 320;
        const uint8_t pix = N.sram[(base + (size_t)xs * 256 + (size_t)ys) & mask];
        if (res == 1) {
            l2_put(out, pal, x * 2, (pix + (pal_off << 4)) & 0xff);
            out[x * 2 + 1] = out[x * 2];
        } else {
            l2_put(out, pal, x * 2, (pal_off << 4) | (pix >> 4));
            l2_put(out, pal, x * 2 + 1, (pal_off << 4) | (pix & 0x0f));
        }
    }
}

// The visible sprites with anchors and relatives resolved (specnext_sprites)
static void build_sprite_cache(void)
{
    N.sprite_cache_n = 0;
    N.sprite_cache_valid = true;
    int anchor = -1;
    bool anchor_vis = false;
    for (int i = 0; i < 128; ++i) {
        const uint8_t *a = &N.sprite_attr[i * 8];
        const bool relative = (a[3] & 0x40) && ((a[4] >> 6) == 1);
        bool visible = a[3] & 0x80;
        if (relative) visible = visible && anchor_vis;
        else anchor_vis = visible;
        if (!visible) continue;
        if (relative && anchor < 0) continue;

        uint8_t cur[5] = {0};
        uint8_t anchor_pattern = 0;
        if (!relative) {
            memcpy(cur, a, 5);
        } else {
            const sprite_data *an = &N.sprite_cache[anchor];
            const bool ar = an->rel_type && an->rotate;
            const bool axm = an->rel_type && an->xmirror;
            const bool aym = an->rel_type && an->ymirror;
            const int axs = an->rel_type ? an->xscale : 0;
            const int ays = an->rel_type ? an->yscale : 0;
            const uint8_t x0 = ar ? a[1] : a[0];
            const uint8_t y0 = ar ? a[0] : a[1];
            const uint8_t x1 = (ar != axm) ? (uint8_t)(~x0 + 1) : x0;
            const uint8_t y1 = aym ? (uint8_t)(~y0 + 1) : y0;
            const int x2 = ((((x1 & 0x80) << 1) | x1) << axs) & 0x1ff;
            const int y2 = ((((y1 & 0x80) << 1) | y1) << ays) & 0x1ff;
            const int x3 = (an->x + x2) & 0x1ff;
            const int y3 = (an->y + y2) & 0x1ff;
            const uint8_t paloff = (a[2] & 1) ? (uint8_t)((an->paloff + (a[2] >> 4)) & 0x0f) : (uint8_t)(a[2] >> 4);
            const bool xm = ar ? (((a[2] >> 2) & 1) != ((a[2] >> 1) & 1)) : ((a[2] >> 3) & 1);
            const bool ym = ar ? (((a[2] >> 3) & 1) != ((a[2] >> 1) & 1)) : ((a[2] >> 2) & 1);
            cur[0] = (uint8_t)x3;
            cur[1] = (uint8_t)y3;
            cur[2] = an->rel_type ? (uint8_t)((paloff << 4) | ((int)(axm != xm) << 3) | ((int)(aym != ym) << 2) |
                                              ((int)(ar != (((a[2] >> 1) & 1) != 0)) << 1) | ((x3 >> 8) & 1))
                                  : (uint8_t)((paloff << 4) | (a[2] & 0x0e) | ((x3 >> 8) & 1));
            cur[3] = (uint8_t)(0x80 | 0x40 | (a[3] & 0x3f));
            cur[4] = an->rel_type ? (uint8_t)(((int)an->h << 7) | ((a[4] & 0x20) ? 0x40 : 0) | (axs << 3) | (ays << 1) |
                                              ((y3 >> 8) & 1))
                                  : (uint8_t)(((int)an->h << 7) | ((a[4] & 0x20) ? 0x40 : 0) | (a[4] & 0x1e) | ((y3 >> 8) & 1));
            anchor_pattern = an->pattern;
        }
        const uint8_t ext = (a[3] & 0x40) ? cur[4] : 0;
        sprite_data s;
        memset(&s, 0, sizeof s);
        s.y = ((ext & 1) << 8) | cur[1];
        s.x = ((cur[2] & 1) << 8) | cur[0];
        s.rotate = cur[2] & 0x02;
        s.ymirror = cur[2] & 0x04;
        s.xmirror = cur[2] & 0x08;
        s.paloff = (uint8_t)(cur[2] >> 4);
        s.h = (ext & 0x80) && (a[3] & 0x40);
        const bool n6 = (ext & 0x40) && s.h;
        s.pattern = (uint8_t)(((cur[3] & 0x3f) << 1) | (int)n6);
        if (relative && (a[4] & 1)) s.pattern = (uint8_t)((s.pattern + anchor_pattern) & 0x7f);
        s.yscale = (uint8_t)((ext >> 1) & 3);
        s.xscale = (uint8_t)((ext >> 3) & 3);
        s.rel_type = (a[4] & 0x20) && (a[3] & 0x40);
        N.sprite_cache[N.sprite_cache_n++] = s;
        if (!relative) anchor = N.sprite_cache_n - 1;
    }
}

static void render_sprites(int row, uint16_t *out)
{
    fill_transparent(out);
    if (!(N.nr_15 & 0x01)) return;
    if (!N.sprite_cache_valid) build_sprite_cache();
    const uint16_t *pal = N.palette[(N.nr_43 & 0x08) ? 6 : 2];
    int cx1 = 0, cx2 = 319, cy1 = 0, cy2 = 255;
    if (!(N.nr_15 & 0x02)) {            // clipped to the paper area
        cx1 = N.clip_spr[0] + 32;
        cx2 = N.clip_spr[1] + 32;
        cy1 = N.clip_spr[2] + 32;
        cy2 = N.clip_spr[3] + 32;
    } else if (N.nr_15 & 0x20) {
        cx1 = N.clip_spr[0] * 2;
        cx2 = N.clip_spr[1] * 2 + 1;
        cy1 = N.clip_spr[2];
        cy2 = N.clip_spr[3];
    }
    if (row < cy1 || row > cy2) return;
    cx2 = imin(cx2, 319);

    const int n = N.sprite_cache_n;
    const bool zero_on_top = N.nr_15 & 0x40;
    for (int k = 0; k < n; ++k) {
        const sprite_data *s = &N.sprite_cache[zero_on_top ? n - 1 - k : k];
        int dy0 = s->y & 0x1ff;
        if (dy0 > 255) dy0 -= 512;
        const int h = 16 << s->yscale;
        const int dy = row - dy0;
        if (dy < 0 || dy >= h) continue;
        int dx0 = s->x & 0x1ff;
        if (dx0 > 319) dx0 -= 512;
        const int w = 16 << s->xscale;
        const int sy = dy >> s->yscale;
        const uint8_t transp = s->h ? (uint8_t)(N.nr_4b_sprite_transparent & 0x0f) : N.nr_4b_sprite_transparent;
        for (int dx = 0; dx < w; ++dx) {
            const int x = dx0 + dx;
            if (x < cx1 || x > cx2) continue;
            const int sxp = dx >> s->xscale;
            const int fx = s->xmirror ? 15 - sxp : sxp;
            const int fy = s->ymirror ? 15 - sy : sy;
            int col = fx, r = fy;
            if (s->rotate) {
                col = fy;
                r = 15 - fx;
            }
            int idx;
            if (s->h) {
                const uint8_t b = N.sprite_pattern[((size_t)s->pattern * 128 + (size_t)r * 8 + (size_t)(col >> 1)) & 0x3fff];
                const int nib = (col & 1) ? (b & 0x0f) : (b >> 4);
                if (nib == transp) continue;
                idx = (s->paloff << 4) | nib;
            } else {
                const uint8_t p = N.sprite_pattern[((size_t)(s->pattern >> 1) * 256 + (size_t)r * 16 + (size_t)col) & 0x3fff];
                if (p == transp) continue;
                idx = (p + (s->paloff << 4)) & 0xff;
            }
            const uint16_t c = (uint16_t)(pal[idx] & 0x1ff);
            out[x * 2] = out[x * 2 + 1] = c;
        }
    }
}

static void render_line(int out_row)
{
    static uint16_t ula[SN_W], tm[SN_W], l2[SN_W], spr[SN_W];
    const bool ula_en = !(N.nr_68 & 0x80);
    if (ula_en) render_ula(out_row, ula);
    else fill_transparent(ula);
    render_tilemap(out_row, tm);
    render_layer2(out_row, l2);
    render_sprites(out_row, spr);

    const bool tm_en = N.nr_6b & 0x80;
    const bool stencil = (N.nr_68 & 0x01) && ula_en && tm_en;
    const uint16_t fallback = (uint16_t)((N.nr_4a_fallback << 1) | ((N.nr_4a_fallback & 3) ? 1 : 0));
    const int priority = (N.nr_15 >> 2) & 7;
    const int blend_src = (N.nr_68 >> 5) & 3;
    uint16_t *dst = &N.fb[(size_t)out_row * SN_W];
    static const uint8_t order[6][3] = {{0, 1, 2}, {1, 0, 2}, {0, 2, 1}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};

    for (int sx = 0; sx < SN_W; ++sx) {
        const uint16_t U = ula[sx], T = tm[sx], L = l2[sx], S = spr[sx];
        uint16_t ut;
        if (!tm_en) ut = U;
        else if (stencil) ut = (opaque(U) && opaque(T)) ? (uint16_t)((U & T) & 0x1ff) : TRANSPARENT;
        else if (opaque(T) && !(T & FLAG)) ut = T;
        else if (opaque(U)) ut = U;
        else ut = T;
        const bool l2_top = opaque(L) && (L & FLAG);
        uint16_t c = TRANSPARENT;
        if (l2_top) {
            c = L;
        } else if (priority < 6) {
            const uint16_t layers[3] = {S, L, ut};
            for (int i = 0; i < 3; ++i) {
                const uint16_t v = layers[order[priority][i]];
                if (opaque(v)) {
                    c = v;
                    break;
                }
            }
        } else if (blend_src == 1) {    // blending off: S, U|T, L
            c = opaque(S) ? S : opaque(ut) ? ut : L;
        } else {
            const uint16_t ub = blend_src == 0 ? U : blend_src == 3 ? T : ut;
            const uint16_t top = blend_src == 0 ? (tm_en ? T : TRANSPARENT) : blend_src == 3 ? U : TRANSPARENT;
            uint16_t mixed;
            if (opaque(ub) && opaque(L)) mixed = mix_add(ub, L, priority == 7 ? 5 : 0);
            else if (opaque(L)) mixed = L;
            else mixed = ub;
            c = opaque(S) ? S : opaque(top) ? top : mixed;
        }
        dst[sx] = opaque(c) ? (uint16_t)(c & 0x1ff) : fallback;
    }
}

void specnext_render_row(int y, uint32_t *out)
{
    const uint16_t *src = &N.fb[(size_t)y * SN_W];
    for (int x = 0; x < SN_W; x++) out[x] = N.rgb_lut[src[x] & 0x1ff];
}

void specnext_render_row565(int y, uint16_t *out)
{
    const uint16_t *src = &N.fb[(size_t)y * SN_W];
    for (int x = 0; x < SN_W; x++) out[x] = N.rgb565_lut[src[x] & 0x1ff];
}

// ---------------------------------------------------------------------------
// Input

void specnext_key(int code, bool down)
{
    if (code >= 0 && code < NK_COUNT) N.held[code] = down;
}

void specnext_keys_clear(void) { memset(N.held, 0, sizeof N.held); }

void specnext_joy(int port, uint8_t bits)
{
    if (port == 0) N.joy_left = bits;
    else N.joy_right = bits;
}

void specnext_nmi_buttons(bool multiface, bool drive)
{
    if (multiface != N.mf_button || drive != N.drive_button) {
        N.mf_button = multiface;
        N.drive_button = drive;
        nmi_request();
    }
}

static inline void press(int row, int bit) { N.key_rows[row] &= (uint8_t)~(1 << bit); }

static void key_joy(uint8_t type, uint8_t j)
{
    if (!j) return;
    switch (type) {
    case 0:                             // Sinclair 2: 6 left, 7 right, 8 down, 9 up, 0 fire
        if (j & 0x02) press(4, 4);
        if (j & 0x01) press(4, 3);
        if (j & 0x04) press(4, 2);
        if (j & 0x08) press(4, 1);
        if (j & 0x10) press(4, 0);
        break;
    case 3:                             // Sinclair 1: 1 left, 2 right, 3 down, 4 up, 5 fire
        if (j & 0x02) press(3, 0);
        if (j & 0x01) press(3, 1);
        if (j & 0x04) press(3, 2);
        if (j & 0x08) press(3, 3);
        if (j & 0x10) press(3, 4);
        break;
    case 2:                             // cursor: 5 left, 6 down, 7 up, 8 right, 0 fire
        if (j & 0x02) press(3, 4);
        if (j & 0x04) press(4, 4);
        if (j & 0x08) press(4, 3);
        if (j & 0x01) press(4, 2);
        if (j & 0x10) press(4, 0);
        break;
    default: break;
    }
}

static void apply_inputs(void)
{
    memset(N.key_rows, 0x1f, sizeof N.key_rows);
    for (int c = 0; c < 0x40; c++)
        if (N.held[c] && (c & 7) < 5) press(c >> 3, c & 7);

    // the Next's extra keys: reported in nr_B0 / nr_B1, and (unless nr_68
    // bit 4 cancels it) typed as their Spectrum combinations
    static const int8_t combo[16][4] = {
        {7, 1, 5, 1},                   // ;  = SS+O
        {7, 1, 5, 0},                   // "  = SS+P
        {7, 1, 7, 3},                   // ,  = SS+N
        {7, 1, 7, 2},                   // .  = SS+M
        {0, 0, 4, 3},                   // up = CS+7
        {0, 0, 4, 4},                   // down = CS+6
        {0, 0, 3, 4},                   // left = CS+5
        {0, 0, 4, 2},                   // right = CS+8
        {0, 0, 4, 0},                   // DELETE = CS+0
        {0, 0, 3, 0},                   // EDIT = CS+1
        {0, 0, 7, 0},                   // BREAK = CS+SPACE
        {0, 0, 3, 3},                   // INV VIDEO = CS+4
        {0, 0, 3, 2},                   // TRUE VIDEO = CS+3
        {0, 0, 4, 1},                   // GRAPH = CS+9
        {0, 0, 3, 1},                   // CAPS LOCK = CS+2
        {0, 0, 7, 1},                   // EXTEND MODE = CS+SS
    };
    const bool cancel = N.nr_68 & 0x10;
    N.ext_keys_b0 = N.ext_keys_b1 = 0;
    for (int i = 0; i < 16; i++) {
        if (!N.held[NK_SEMICOLON + i]) continue;
        if (i < 8) N.ext_keys_b0 |= (uint8_t)(0x80 >> i);
        else N.ext_keys_b1 |= (uint8_t)(0x80 >> (i - 8));
        if (cancel) continue;
        press(combo[i][0], combo[i][1]);
        press(combo[i][2], combo[i][3]);
    }
    if (!cancel) {
        if (N.held[NK_MINUS]) { press(7, 1); press(6, 3); }      // - = SS+J
        if (N.held[NK_EQUALS]) { press(7, 1); press(6, 1); }     // = = SS+L
        if (N.held[NK_PLUS]) { press(7, 1); press(6, 2); }       // + = SS+K
        if (N.held[NK_SLASH]) { press(7, 1); press(0, 4); }      // / = SS+V
        if (N.held[NK_ASTERISK]) { press(7, 1); press(7, 4); }   // * = SS+B
    }
    // Sinclair / cursor joystick modes press keys
    key_joy((uint8_t)((((N.nr_05 >> 3) & 1) << 2) | ((N.nr_05 >> 6) & 3)), N.joy_left);
    key_joy((uint8_t)((((N.nr_05 >> 1) & 1) << 2) | ((N.nr_05 >> 4) & 3)), N.joy_right);
}

// ---------------------------------------------------------------------------
// The frame

void specnext_set_turbo(bool on) { N.turbo = on; }

int specnext_run_frame(const int16_t **audio)
{
    apply_inputs();
    N.naudio = 0;
    N.frame_done = false;
    for (int guard = 0; !N.frame_done && guard < 1000000; ++guard) {
        int remaining = (N.frame_ticks - N.frame_tick) / imax(1, ticks_per_t());
        if (remaining < 1) remaining = 1;
        if (remaining > 4096) remaining = 4096;
        zd_run(&N.cpu, remaining);
    }
    // the SD light: sectors moved in the last frames
    const uint32_t act = N.sd.sectors_read + N.sd.sectors_written;
    if (act != N.sd_activity_last) N.sd_busy_frames = 8;
    else if (N.sd_busy_frames) N.sd_busy_frames--;
    N.sd_activity_last = act;
    if (audio) *audio = N.audio;
    return N.naudio;
}

// ---------------------------------------------------------------------------
// Media

const char *specnext_insert_sd(blkfile *f)
{
    if (!sdspi_insert(&N.sd, f)) return "SD card image too small";
    specnext_hard_reset();
    return NULL;
}

void specnext_eject_sd(void) { sdspi_eject(&N.sd); }
bool specnext_sd_inserted(void) { return N.sd.inserted; }
bool specnext_sd_busy(void) { return N.sd_busy_frames > 0; }

// NEX programs (https://wiki.specnext.dev/NEX_file_format)
static const char *load_nex_now(const uint8_t *data, uint32_t size)
{
    if (size < 512 || memcmp(data, "Next", 4) != 0) return "not a NEX file";
    const uint8_t *h = data;
    uint32_t pos = 512;
#define TAKE(n) (pos + (n) > size ? NULL : (pos += (n), data + pos - (n)))
#define BANK_PTR(bank) (&N.sram[0x40000 + (size_t)(bank) * 0x4000])

    // the machine as NEXLOAD leaves it: the Next registers back to their reset
    // values (unless the file keeps them), ROM 3 (48K BASIC), DivMMC out
    const uint8_t screen = h[10];
    const uint8_t border = h[11];
    const uint16_t sp = (uint16_t)(h[12] | (h[13] << 8));
    const uint16_t pc = (uint16_t)(h[14] | (h[15] << 8));
    const uint8_t *banks = h + 18;
    const bool preserve = h[134] != 0;
    const uint8_t entry_bank = h[139];
    const uint16_t file_handle = (uint16_t)(h[140] | (h[141] << 8));
    const uint8_t screen2 = h[152];
    const bool has_copper = h[153] != 0;
    static const uint8_t l2c[4] = {0x00, 0x9f, 0x00, 0xff};

    if (!preserve) {
        N.layer2_en = false;
        N.nr_15 = 0;
        N.nr_16_l2_scrollx = N.nr_17_l2_scrolly = 0;
        N.nr_14_global_transparent = 0xe3;
        N.nr_4b_sprite_transparent = 0xe3;
        N.nr_4c_tm_transparent = 0x0f;
        set_clip_defaults();
        N.nr_26_ula_scrollx = N.nr_27_ula_scrolly = 0;
        N.nr_30_tm_scrollx = 0;
        N.nr_31_tm_scrolly = N.nr_32_lores_scrollx = N.nr_33_lores_scrolly = 0;
        N.nr_42_ulanext_format = 7;
        N.nr_43 = 0;
        N.nr_68 = 0;
        N.nr_6a = 0;
        N.nr_6b = 0;
        N.nr_6c = 0;
        N.nr_6e = 0x2c;
        N.nr_6f = 0x0c;
        N.nr_70 = 0;
        N.nr_71 = 0;
        N.copper_mode = 0;
        N.ulap_en = false;
        N.nr_22_line_int_en = false;
        N.nr_c0 = 0;
        ctc_defaults();
        for (int p = 0; p < 8; ++p)
            for (int i = 0; i < 256; ++i) N.palette[p][i] = rgb8_to_9((p & 3) == 0 ? kUlaDefault[i & 15] : (uint8_t)i);
        memset(N.sprite_attr, 0, sizeof N.sprite_attr);
        N.sprite_cache_valid = false;
    }
    N.nr_12_layer2_bank = 9;
    zxn_dma_reset(&N.dma);
    port_e3_w(0);
    dm_reset_lines(&N.divmmc);
    N.layer2_map_wr = N.layer2_map_rd = N.layer2_map_shadow = false;
    N.layer2_map_segment = N.layer2_offset = 0;
    N.port_ff = 0;

    // loading screens
    const bool has_palette = !(screen & 0x80) && (screen & 0x05);
    if (has_palette || ((screen & 0x40) && (screen2 == 1 || screen2 == 2) && !(screen & 0x80))) {
        const uint8_t *pal = TAKE(512);
        if (!pal) return "truncated NEX file";
        const int target = (screen & 0x04) ? 0 : 1;      // LoRes uses the ULA palette
        for (int i = 0; i < 256; ++i) {
            const uint16_t v = (uint16_t)((pal[i * 2] << 1) | (pal[i * 2 + 1] & 1));
            N.palette[target][i] = (uint16_t)(v | ((target == 1 && (pal[i * 2 + 1] & 0x80)) ? 0x8000 : 0));
        }
    }
    if (screen & 0x01) {                // Layer 2 256x192
        const uint8_t *p = TAKE(49152);
        if (!p) return "truncated NEX file";
        memcpy(BANK_PTR(9), p, 49152);
        N.layer2_en = true;
    }
    if (screen & 0x02) {                // ULA
        const uint8_t *p = TAKE(6912);
        if (!p) return "truncated NEX file";
        memcpy(BANK_PTR(5), p, 6912);
    }
    if (screen & 0x04) {                // LoRes
        const uint8_t *p = TAKE(12288);
        if (!p) return "truncated NEX file";
        memcpy(BANK_PTR(5), p, 6144);
        memcpy(BANK_PTR(5) + 0x2000, p + 6144, 6144);
        N.nr_15 |= 0x80;
    }
    if (screen & 0x08) {                // Timex hi-res
        const uint8_t *p = TAKE(12288);
        if (!p) return "truncated NEX file";
        memcpy(BANK_PTR(5), p, 6144);
        memcpy(BANK_PTR(5) + 0x2000, p + 6144, 6144);
        N.port_ff = (uint8_t)(0x06 | (h[138] & 0x38));
    }
    if (screen & 0x10) {                // Timex hi-colour
        const uint8_t *p = TAKE(12288);
        if (!p) return "truncated NEX file";
        memcpy(BANK_PTR(5), p, 6144);
        memcpy(BANK_PTR(5) + 0x2000, p + 6144, 6144);
        N.port_ff = 0x02;
    }
    if ((screen & 0x40) && (screen2 == 1 || screen2 == 2)) {    // Layer 2 320x256 / 640x256
        const uint8_t *p = TAKE(81920);
        if (!p) return "truncated NEX file";
        memcpy(BANK_PTR(9), p, 81920);
        N.nr_70 = (uint8_t)(screen2 == 1 ? 0x10 : 0x20);
        memcpy(N.clip_l2, l2c, 4);
        N.layer2_en = true;
    }
    if (has_copper) {
        const uint8_t *p = TAKE(2048);
        if (!p) return "truncated NEX file";
        memcpy(N.copper_ram, p, 2048);
        N.copper_pc = 0;
        N.copper_mode = 3;
    }
    // the banks: 5, 2, 0, 1, 3, 4, 6, 7, 8, ...
    static const int order[6] = {5, 2, 0, 1, 3, 4};
    for (int i = 0; i < 112; ++i) {
        const int bank = i < 6 ? order[i] : i;
        if (!banks[bank]) continue;
        const uint8_t *p = TAKE(0x4000);
        if (!p) return "truncated NEX file";
        if (0x40000 + (size_t)(bank + 1) * 0x4000 <= (size_t)RAM_PAGES * 0x2000) memcpy(BANK_PTR(bank), p, 0x4000);
    }
#undef TAKE
#undef BANK_PTR

    // paging: ROM 3, banks 5 / 2 and the entry bank at $C000
    N.port_7ffd = 0x10;
    N.port_1ffd = 0x04;
    N.port_dffd = 0;
    N.port_eff7 = 0;
    N.port_1ffd_special_old = false;
    mmu_x2_w(0, 0xff);
    mmu_x2_w(2, 0x0a);
    mmu_x2_w(4, 0x04);
    mmu_x2_w(6, (uint8_t)(entry_bank * 2));
    bank_update_range(0, 8);

    N.port_fe = (uint8_t)(border & 7);
    if (file_handle >= 0x4000) mem_write(file_handle, 0xff);     // no esxDOS handle to pass on
    else if (file_handle != 0) N.cpu.c = N.cpu.b = 0xff;

    memset(N.int_pending, 0, sizeof N.int_pending);
    memset(N.int_service, 0, sizeof N.int_service);
    N.pulse_left = 0;
    N.cpu.halted = false;
    N.cpu.sp = sp;
    N.cpu.im = 1;
    N.cpu.iff1 = N.cpu.iff2 = false;
    if (pc) N.cpu.pc = pc;
    return NULL;
}

const char *specnext_load_nex(const uint8_t *data, uint32_t size)
{
    if (size < 512 || memcmp(data, "Next", 4) != 0) return "not a NEX file";
    if (!N.sd.inserted) return load_nex_now(data, size);
    free(N.pending_nex);
    N.pending_nex = (uint8_t *)malloc(size);    // loaded once NextZXOS has started
    if (!N.pending_nex) return "out of memory";
    memcpy(N.pending_nex, data, size);
    N.pending_nex_size = size;
    return NULL;
}

static bool ends_with_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), k = strlen(ext);
    if (n < k) return false;
    for (size_t i = 0; i < k; i++) {
        char c = s[n - k + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

const char *specnext_load_file(const char *path)
{
    if (ends_with_ci(path, ".nex")) {
        FILE *f = fopen(path, "rb");
        if (!f) return "cannot open the file";
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *d = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
        const bool ok = d && fread(d, 1, (size_t)n, f) == (size_t)n;
        fclose(f);
        const char *err = ok ? specnext_load_nex(d, (uint32_t)n) : "cannot read the file";
        free(d);
        return err;
    }
    blkfile b;
    if (!blkfile_open(&b, path)) return "cannot open the SD card image";
    const char *err = specnext_insert_sd(&b);
    if (err) blkfile_close(&b);
    return err;
}

// ---------------------------------------------------------------------------

static uint8_t mem_r_cb(void *user, uint16_t a) { (void)user; return mem_read(a); }
static void mem_w_cb(void *user, uint16_t a, uint8_t v) { (void)user; mem_write(a, v); }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)); }

bool specnext_init(const uint8_t *boot_rom)
{
    if (!N.sram) N.sram = (uint8_t *)malloc((size_t)RAM_PAGES * 0x2000);
    if (!N.fb) N.fb = (uint16_t *)calloc((size_t)SN_W * SN_H, sizeof(uint16_t));
    if (!N.sram || !N.fb) return false;
    N.boot_rom = boot_rom;
    for (int c = 0; c < 512; ++c) {
#define X3(v) ((uint32_t)(((v) << 5) | ((v) << 2) | ((v) >> 1)))
        const uint32_t r = X3((c >> 6) & 7), g = X3((c >> 3) & 7), b = X3(c & 7);
#undef X3
        N.rgb_lut[c] = (r << 16) | (g << 8) | b;
        N.rgb565_lut[c] = rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    zd_z80 *z = &N.cpu;
    memset(z, 0, sizeof *z);
    z->read = mem_r_cb;
    z->write = mem_w_cb;
    z->opcode_read = opcode_read;
    z->in = io_in;
    z->out = io_out;
    z->cycles = on_cycles;
    z->irq_ack = irq_acknowledge;
    z->z80n = true;
    z->nextreg_w = nextreg_w_cb;
    z->nextreg_r = nextreg_r_cb;
    z->ret_cb = ret_cb;
    zd_init(z);
    for (int i = 0; i < 3; i++) {
        ay8910_init(&N.ay[i], 1750000, SN_AUDIO_RATE);
        ay8910_set_amplitude(&N.ay[i], 1.0f);
    }
    sdspi_init(&N.sd);
    N.dma.mem_read = mem_r_cb;
    N.dma.mem_write = mem_w_cb;
    N.dma.io_read = io_in;
    N.dma.io_write = io_out;
    N.dma.ticks_per_t = 8;
    specnext_hard_reset();
    return true;
}

