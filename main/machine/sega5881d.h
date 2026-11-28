// sega5881d.h - Sega 315-5881 stream decryption / decompression (Model 3
// security board): C port of dsp-cpp's machine/sega_315_5881 (from MAME,
// BSD-3-Clause, Andreas Naive, Olivier Galibert, David Haywood).
#ifndef SEGA5881D_H
#define SEGA5881D_H
#include <stdint.h>
#include <stdbool.h>

#define S5881_LINE_SIZE 512

typedef struct {
    uint16_t (*read)(void *user, uint32_t addr);   // 16-bit words of the stream
    void *user;
    uint32_t key;
    uint8_t buffer[2], lb[2][S5881_LINE_SIZE];
    uint8_t *line_buffer, *line_buffer_prev;
    uint32_t prot_cur_address;
    uint16_t subkey, dec_hist;
    uint32_t dec_header;
    bool enc_ready;
    int buffer_pos, line_buffer_pos, line_buffer_size, buffer_bit, buffer_bit2;
    uint8_t buffer2[2];
    uint16_t buffer2a;
    int block_size, block_pos, block_numlines, done_compression;
} s5881;

void s5881_init(s5881 *c, uint32_t key);         // (read / user set after)
void s5881_reset(s5881 *c);
void s5881_set_addr_low(s5881 *c, uint16_t data);
void s5881_set_addr_high(s5881 *c, uint16_t data);
void s5881_set_subkey(s5881 *c, uint16_t data);
uint16_t s5881_decrypt(s5881 *c);

#endif
