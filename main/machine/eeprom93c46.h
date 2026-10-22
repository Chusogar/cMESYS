// eeprom93c46.h - 93C46 serial EEPROM (128 x 8 or 64 x 16).
// C port of dsp-cpp machine/eeprom93c46.cpp (from eepromser.pas).
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct eeprom93c46 {
    uint8_t data[0x80];          // cells; 16-bit mode: big-endian words
    int data_bits;               // 8 or 16
    int command_address_bits;
    int address_bits;
    int state, command;
    uint8_t cs, clk, di;
    uint8_t bits_accum;
    uint32_t shift_register;
    uint32_t command_address_accum;
    uint16_t address;
    bool locked;
    bool dirty;                  // set on every write to the cells
    // Write/erase cycle timing (as MAME's eeprom_base_device): the caller keeps
    // 'now' up to date in its own time unit and sets the operation times in it
    // (all 0 = always ready). During a cycle DO reads 0 (busy) after CS rises.
    uint64_t now, busy_until;
    uint32_t write_time, writeall_time, erase_time, eraseall_time;
} eeprom93c46_t;

// data_bits: 8 or 16. The cells start erased (0xff).
void eeprom93c46_init(eeprom93c46_t *e, int data_bits);
void eeprom93c46_reset(eeprom93c46_t *e);
uint8_t eeprom93c46_do_read(const eeprom93c46_t *e);
// Typical 93C46 timing in units per second (1750 us write, 1 ms erase, 8 ms all)
void eeprom93c46_set_timing(eeprom93c46_t *e, uint32_t units_per_second);
void eeprom93c46_cs_write(eeprom93c46_t *e, uint8_t state);
void eeprom93c46_clk_write(eeprom93c46_t *e, uint8_t state);
void eeprom93c46_di_write(eeprom93c46_t *e, uint8_t state);
