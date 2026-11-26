// fd1094d.c - see fd1094d.h. Translated from dsp-cpp's machine/fd1094
// (MAME's fd1094.cpp).
#include <stdlib.h>
#include <string.h>
#include "fd1094d.h"

static const uint16_t kMaskedOpcodes[] =
{
 0x013a,0x033a,0x053a,0x073a,0x083a,0x093a,0x0b3a,0x0d3a,0x0f3a,

 0x103a, 0x10ba,0x10fa, 0x113a,0x117a,0x11ba,0x11fa,
 0x123a, 0x12ba,0x12fa, 0x133a,0x137a,0x13ba,0x13fa,
 0x143a, 0x14ba,0x14fa, 0x153a,0x157a,0x15ba,
 0x163a, 0x16ba,0x16fa, 0x173a,0x177a,0x17ba,
 0x183a, 0x18ba,0x18fa, 0x193a,0x197a,0x19ba,
 0x1a3a, 0x1aba,0x1afa, 0x1b3a,0x1b7a,0x1bba,
 0x1c3a, 0x1cba,0x1cfa, 0x1d3a,0x1d7a,0x1dba,
 0x1e3a, 0x1eba,0x1efa, 0x1f3a,0x1f7a,0x1fba,

 0x203a,0x207a,0x20ba,0x20fa, 0x213a,0x217a,0x21ba,0x21fa,
 0x223a,0x227a,0x22ba,0x22fa, 0x233a,0x237a,0x23ba,0x23fa,
 0x243a,0x247a,0x24ba,0x24fa, 0x253a,0x257a,0x25ba,
 0x263a,0x267a,0x26ba,0x26fa, 0x273a,0x277a,0x27ba,
 0x283a,0x287a,0x28ba,0x28fa, 0x293a,0x297a,0x29ba,
 0x2a3a,0x2a7a,0x2aba,0x2afa, 0x2b3a,0x2b7a,0x2bba,
 0x2c3a,0x2c7a,0x2cba,0x2cfa, 0x2d3a,0x2d7a,0x2dba,
 0x2e3a,0x2e7a,0x2eba,0x2efa, 0x2f3a,0x2f7a,0x2fba,

 0x303a,0x307a,0x30ba,0x30fa, 0x313a,0x317a,0x31ba,0x31fa,
 0x323a,0x327a,0x32ba,0x32fa, 0x333a,0x337a,0x33ba,0x33fa,
 0x343a,0x347a,0x34ba,0x34fa, 0x353a,0x357a,0x35ba,
 0x363a,0x367a,0x36ba,0x36fa, 0x373a,0x377a,0x37ba,
 0x383a,0x387a,0x38ba,0x38fa, 0x393a,0x397a,0x39ba,
 0x3a3a,0x3a7a,0x3aba,0x3afa, 0x3b3a,0x3b7a,0x3bba,
 0x3c3a,0x3c7a,0x3cba,0x3cfa, 0x3d3a,0x3d7a,0x3dba,
 0x3e3a,0x3e7a,0x3eba,0x3efa, 0x3f3a,0x3f7a,0x3fba,

 0x41ba,0x43ba,0x44fa,0x45ba,0x46fa,0x47ba,0x49ba,0x4bba,0x4cba,0x4cfa,0x4dba,0x4fba,

 0x803a,0x807a,0x80ba,0x80fa, 0x81fa,
 0x823a,0x827a,0x82ba,0x82fa, 0x83fa,
 0x843a,0x847a,0x84ba,0x84fa, 0x85fa,
 0x863a,0x867a,0x86ba,0x86fa, 0x87fa,
 0x883a,0x887a,0x88ba,0x88fa, 0x89fa,
 0x8a3a,0x8a7a,0x8aba,0x8afa, 0x8bfa,
 0x8c3a,0x8c7a,0x8cba,0x8cfa, 0x8dfa,
 0x8e3a,0x8e7a,0x8eba,0x8efa, 0x8ffa,

 0x903a,0x907a,0x90ba,0x90fa, 0x91fa,
 0x923a,0x927a,0x92ba,0x92fa, 0x93fa,
 0x943a,0x947a,0x94ba,0x94fa, 0x95fa,
 0x963a,0x967a,0x96ba,0x96fa, 0x97fa,
 0x983a,0x987a,0x98ba,0x98fa, 0x99fa,
 0x9a3a,0x9a7a,0x9aba,0x9afa, 0x9bfa,
 0x9c3a,0x9c7a,0x9cba,0x9cfa, 0x9dfa,
 0x9e3a,0x9e7a,0x9eba,0x9efa, 0x9ffa,

 0xb03a,0xb07a,0xb0ba,0xb0fa, 0xb1fa,
 0xb23a,0xb27a,0xb2ba,0xb2fa, 0xb3fa,
 0xb43a,0xb47a,0xb4ba,0xb4fa, 0xb5fa,
 0xb63a,0xb67a,0xb6ba,0xb6fa, 0xb7fa,
 0xb83a,0xb87a,0xb8ba,0xb8fa, 0xb9fa,
 0xba3a,0xba7a,0xbaba,0xbafa, 0xbbfa,
 0xbc3a,0xbc7a,0xbcba,0xbcfa, 0xbdfa,
 0xbe3a,0xbe7a,0xbeba,0xbefa, 0xbffa,

 0xc03a,0xc07a,0xc0ba,0xc0fa, 0xc1fa,
 0xc23a,0xc27a,0xc2ba,0xc2fa, 0xc3fa,
 0xc43a,0xc47a,0xc4ba,0xc4fa, 0xc5fa,
 0xc63a,0xc67a,0xc6ba,0xc6fa, 0xc7fa,
 0xc83a,0xc87a,0xc8ba,0xc8fa, 0xc9fa,
 0xca3a,0xca7a,0xcaba,0xcafa, 0xcbfa,
 0xcc3a,0xcc7a,0xccba,0xccfa, 0xcdfa,
 0xce3a,0xce7a,0xceba,0xcefa, 0xcffa,

 0xd03a,0xd07a,0xd0ba,0xd0fa, 0xd1fa,
 0xd23a,0xd27a,0xd2ba,0xd2fa, 0xd3fa,
 0xd43a,0xd47a,0xd4ba,0xd4fa, 0xd5fa,
 0xd63a,0xd67a,0xd6ba,0xd6fa, 0xd7fa,
 0xd83a,0xd87a,0xd8ba,0xd8fa, 0xd9fa,
 0xda3a,0xda7a,0xdaba,0xdafa, 0xdbfa,
 0xdc3a,0xdc7a,0xdcba,0xdcfa, 0xddfa,
 0xde3a,0xde7a,0xdeba,0xdefa, 0xdffa
};

#define BIT(x, n) ((uint16_t)(((x) >> (n)) & 1))

static uint16_t bitswap16(uint16_t val, int b15, int b14, int b13, int b12, int b11, int b10, int b9, int b8, int b7,
                          int b6, int b5, int b4, int b3, int b2, int b1, int b0)
{
    return (uint16_t)((BIT(val, b15) << 15) | (BIT(val, b14) << 14) | (BIT(val, b13) << 13) | (BIT(val, b12) << 12) |
                      (BIT(val, b11) << 11) | (BIT(val, b10) << 10) | (BIT(val, b9) << 9) | (BIT(val, b8) << 8) |
                      (BIT(val, b7) << 7) | (BIT(val, b6) << 6) | (BIT(val, b5) << 5) | (BIT(val, b4) << 4) |
                      (BIT(val, b3) << 3) | (BIT(val, b2) << 2) | (BIT(val, b1) << 1) | (BIT(val, b0) << 0));
}

static uint8_t s_masked[2][0x1000];
static bool s_masked_ready;

static void ensure_masked_lookup(void)
{
    if (s_masked_ready) return;
    memset(s_masked, 0, sizeof s_masked);
    for (size_t i = 0; i < sizeof kMaskedOpcodes / sizeof kMaskedOpcodes[0]; i++) {
        const uint16_t opcode = kMaskedOpcodes[i];
        s_masked[0][opcode >> 4] |= (uint8_t)(1 << ((opcode >> 1) & 7));
        s_masked[1][opcode >> 4] |= (uint8_t)(1 << ((opcode >> 1) & 7));
    }
    for (int opcode = 0; opcode < 65536; opcode += 2)
        if ((opcode & 0xff80) == 0x4e80 || (opcode & 0xf0f8) == 0x50c8 || (opcode & 0xf000) == 0x6000)
            s_masked[1][opcode >> 4] |= (uint8_t)(1 << ((opcode >> 1) & 7));
    s_masked_ready = true;
}

static void clear_cache(fd1094d *f)
{
    for (int i = 0; i < 256; i++) {
        free(f->cache[i]);
        f->cache[i] = NULL;
    }
    f->cache_src = NULL;
    f->cache_bytes = 0;
}

void fd1094d_set_key(fd1094d *f, const uint8_t *key, size_t size)
{
    memset(f->key, 0, sizeof f->key);
    if (key && size > 0) {
        memcpy(f->key, key, size < FD1094_KEY_SIZE ? size : FD1094_KEY_SIZE);
        f->key_ready = true;
    } else f->key_ready = false;
    clear_cache(f);
}

void fd1094d_change_state(fd1094d *f, int newstate)
{
    switch (newstate & 0x300) {
    case 0x0000: f->state = (uint8_t)(newstate & 0xff); break;
    case FD1094_STATE_RESET:
        f->state = (uint8_t)(newstate & 0xff);
        f->irq_mode = false;
        break;
    case FD1094_STATE_IRQ: f->irq_mode = true; break;
    case FD1094_STATE_RTE: f->irq_mode = false; break;
    default: break;
    }
}

void fd1094d_reset(fd1094d *f)
{
    clear_cache(f);
    fd1094d_change_state(f, FD1094_STATE_RESET);
}

void fd1094d_on_cmpild(fd1094d *f, uint8_t reg, uint32_t data)
{
    if (reg == 0 && (data & 0xffff) == 0xffff) fd1094d_change_state(f, (int)(data >> 16));
}

uint16_t fd1094_decrypt_one(uint32_t address, uint16_t val, const uint8_t *main_key, uint8_t state, bool vector_fetch)
{
    ensure_masked_lookup();

    uint8_t gkey1 = main_key[1];
    uint8_t gkey2 = main_key[2];
    uint8_t gkey3 = main_key[3];
    if (state & 0x01) {
        gkey1 ^= 0x04;
        gkey2 ^= 0x80;
        gkey3 ^= 0x80;
    }
    if (state & 0x02) {
        gkey1 ^= 0x01;
        gkey2 ^= 0x10;
        gkey3 ^= 0x01;
    }
    if (state & 0x04) {
        gkey1 ^= 0x80;
        gkey2 ^= 0x40;
        gkey3 ^= 0x04;
    }
    if (state & 0x08) {
        gkey1 ^= 0x20;
        gkey2 ^= 0x02;
        gkey3 ^= 0x20;
    }
    if (state & 0x10) {
        gkey1 ^= 0x02;
        gkey1 ^= 0x40;
        gkey2 ^= 0x08;
    }
    if (state & 0x20) {
        gkey1 ^= 0x08;
        gkey3 ^= 0x08;
        gkey3 ^= 0x10;
    }
    if (state & 0x40) {
        gkey1 ^= 0x10;
        gkey2 ^= 0x20;
        gkey2 ^= 0x04;
    }
    if (state & 0x80) {
        gkey2 ^= 0x01;
        gkey3 ^= 0x02;
        gkey3 ^= 0x40;
    }

    uint8_t mainkey;
    if ((address & 0x0ffc) == 0 && address >= 4)
        mainkey = main_key[(address & 0x1fff) | 0x1000];
    else
        mainkey = main_key[address & 0x1fff];

    uint8_t key_F = (address & 0x1000) ? BIT(mainkey, 7) : BIT(mainkey, 6);

    if (vector_fetch) {
        if (address <= 3) gkey3 = 0x00;
        if (address <= 2) gkey2 = 0x00;
        if (address <= 1) gkey1 = 0x00;
        if (address <= 1) key_F = 0;
    }

    const uint8_t global_xor0 = (uint8_t)(1 ^ BIT(gkey1, 5));
    const uint8_t global_xor1 = (uint8_t)(1 ^ BIT(gkey1, 2));
    const uint8_t global_swap2 = (uint8_t)(1 ^ BIT(gkey1, 0));
    const uint8_t global_swap0a = (uint8_t)(1 ^ BIT(gkey2, 5));
    const uint8_t global_swap0b = (uint8_t)(1 ^ BIT(gkey2, 2));
    const uint8_t global_swap3 = (uint8_t)(1 ^ BIT(gkey3, 6));
    const uint8_t global_swap1 = (uint8_t)(1 ^ BIT(gkey3, 4));
    const uint8_t global_swap4 = (uint8_t)(1 ^ BIT(gkey3, 2));

    const uint8_t key_0a = (uint8_t)(BIT(mainkey, 0) ^ BIT(gkey3, 1));
    const uint8_t key_0b = (uint8_t)(BIT(mainkey, 0) ^ BIT(gkey1, 7));
    const uint8_t key_0c = (uint8_t)(BIT(mainkey, 0) ^ BIT(gkey1, 1));
    const uint8_t key_1a = (uint8_t)(BIT(mainkey, 1) ^ BIT(gkey2, 7));
    const uint8_t key_1b = (uint8_t)(BIT(mainkey, 1) ^ BIT(gkey1, 3));
    const uint8_t key_2a = (uint8_t)(BIT(mainkey, 2) ^ BIT(gkey3, 7));
    const uint8_t key_2b = (uint8_t)(BIT(mainkey, 2) ^ BIT(gkey1, 4));
    const uint8_t key_3a = (uint8_t)(BIT(mainkey, 3) ^ BIT(gkey2, 0));
    const uint8_t key_3b = (uint8_t)(BIT(mainkey, 3) ^ BIT(gkey3, 3));
    const uint8_t key_4a = (uint8_t)(BIT(mainkey, 4) ^ BIT(gkey2, 3));
    const uint8_t key_4b = (uint8_t)(BIT(mainkey, 4) ^ BIT(gkey3, 0));
    const uint8_t key_5a = (uint8_t)(BIT(mainkey, 5) ^ BIT(gkey3, 5));
    const uint8_t key_5b = (uint8_t)(BIT(mainkey, 5) ^ BIT(gkey1, 6));
    const uint8_t key_6a = (uint8_t)(BIT(mainkey, 6) ^ BIT(gkey2, 1));
    const uint8_t key_6b = (uint8_t)(BIT(mainkey, 6) ^ BIT(gkey2, 6));
    const uint8_t key_7a = (uint8_t)(BIT(mainkey, 7) ^ BIT(gkey2, 4));

    if (val & 0x8000) {
        val = bitswap16(val, 15, 9, 10, 13, 3, 12, 0, 14, 6, 5, 2, 11, 8, 1, 4, 7);
        if (!global_xor1)
            if (~val & 0x0800) val ^= 0x3002;
        if (~val & 0x0020) val ^= 0x0044;
        if (!key_1b)
            if (~val & 0x0400) val ^= 0x0890;
        if (!global_swap2)
            if (!key_0c) val ^= 0x0308;
        val ^= 0x6561;
        if (!key_2b) val = bitswap16(val, 15, 10, 13, 12, 11, 14, 9, 8, 7, 6, 0, 4, 3, 2, 1, 5);
    }

    if (val & 0x4000) {
        val = bitswap16(val, 13, 14, 7, 0, 8, 6, 4, 2, 1, 15, 3, 11, 12, 10, 5, 9);
        if (!global_xor0)
            if (val & 0x0010) val ^= 0x0468;
        if (!key_3a)
            if (val & 0x0100) val ^= 0x0081;
        if (!key_6a)
            if (val & 0x0004) val ^= 0x0100;
        if (!key_5b)
            if (!key_0b) val ^= 0x3012;
        val ^= 0x3523;
        if (!global_swap0b) val = bitswap16(val, 2, 14, 13, 12, 9, 10, 11, 8, 7, 6, 5, 4, 3, 15, 1, 0);
    }

    if (val & 0x2000) {
        val = bitswap16(val, 10, 2, 13, 7, 8, 0, 3, 14, 6, 15, 1, 11, 9, 4, 5, 12);
        if (!key_4a)
            if (val & 0x0800) val ^= 0x010c;
        if (!key_1a)
            if (val & 0x0080) val ^= 0x1000;
        if (!key_7a)
            if (val & 0x0400) val ^= 0x0a21;
        if (!key_4b)
            if (!key_0a) val ^= 0x0080;
        if (!global_swap0a)
            if (!key_6b) val ^= 0xc000;
        val ^= 0x99a5;
        if (!key_5b) val = bitswap16(val, 15, 14, 13, 12, 11, 1, 9, 8, 7, 10, 5, 6, 3, 2, 4, 0);
    }

    if (val & 0xe000) {
        val = bitswap16(val, 15, 13, 14, 5, 6, 0, 9, 10, 4, 11, 1, 2, 12, 3, 7, 8);
        val ^= 0x17ff;
        if (!global_swap4) val = bitswap16(val, 15, 14, 13, 6, 11, 10, 9, 5, 7, 12, 8, 4, 3, 2, 1, 0);
        if (!global_swap3) val = bitswap16(val, 13, 15, 14, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        if (!global_swap2) val = bitswap16(val, 15, 14, 13, 12, 11, 2, 9, 8, 10, 6, 5, 4, 3, 0, 1, 7);
        if (!key_3b) val = bitswap16(val, 15, 14, 13, 12, 11, 10, 4, 8, 7, 6, 5, 9, 1, 2, 3, 0);
        if (!key_2a) val = bitswap16(val, 13, 14, 15, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        if (!global_swap1) val = bitswap16(val, 15, 14, 13, 12, 9, 8, 11, 10, 7, 6, 5, 4, 3, 2, 1, 0);
        if (!key_5a) val = bitswap16(val, 15, 14, 13, 12, 11, 10, 9, 8, 4, 5, 7, 6, 3, 2, 1, 0);
        if (!global_swap0a) val = bitswap16(val, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 0, 3, 2, 1);
    }

    val = bitswap16(val, 12, 15, 14, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

    if ((val & 0xb080) == 0x8000) val ^= 0x4000;
    if ((val & 0xf000) == 0xc000) val ^= 0x0080;
    if ((val & 0xb100) == 0x0000) val ^= 0x4000;

    if ((s_masked[key_F][val >> 4] >> ((val >> 1) & 7)) & 1) val = 0xffff;
    return val;
}


const uint16_t *fd1094d_opcodes(fd1094d *f, const uint16_t *src, uint32_t bytes)
{
    if (!f->key_ready || !src || bytes == 0) return src;
    if (f->cache_src != src || f->cache_bytes != bytes) {
        clear_cache(f);
        f->cache_src = src;
        f->cache_bytes = bytes;
    }
    const uint8_t st = fd1094d_state(f);
    if (!f->cache[st]) {
        if (!(f->cache[st] = (uint16_t *)malloc(bytes))) return src;
        ensure_masked_lookup();
        for (uint32_t off = 0; off < bytes; off += 2)
            f->cache[st][off / 2] = fd1094_decrypt_one(off / 2, src[off / 2], f->key, st, off < 8);
    }
    return f->cache[st];
}
