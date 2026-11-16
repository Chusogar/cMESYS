// i2cmem.h - 24C08 / 24C256 serial EEPROM (I2C), a C11 port of dsp-cpp's
// machine/i2cmem (the ColecoVision's Super Game Module cartridges Black
// Onix and Boxxle). The memory is the caller's (1 KB or 32 KB).
#ifndef I2CMEM_H
#define I2CMEM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *memory;
    size_t size;
    int addr_bytes;
    bool scl, bus_sda, started;
    int phase, bitpos;
    uint8_t shift;
    bool write_mode;
    int block_select, addr_byte_count;
    uint8_t addr_high;
    uint32_t address;
    bool driving, driving_ack, pending_first_read, awaiting_master_ack;
    uint8_t data_byte;
    int data_bitpos;
} i2cmem;

void i2cmem_init(i2cmem *m, uint8_t *memory, size_t size);   // 1024: 24C08, 32768: 24C256
void i2cmem_reset(i2cmem *m);
void i2cmem_write_scl(i2cmem *m, bool level);
void i2cmem_write_sda(i2cmem *m, bool level);
bool i2cmem_read_sda(const i2cmem *m);
#endif
