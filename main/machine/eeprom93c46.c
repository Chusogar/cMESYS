// eeprom93c46.c - 93C46 serial EEPROM.
// C port of dsp-cpp machine/eeprom93c46.cpp (from eepromser.pas): command
// address width, cell width and the packing of each cell as in
// eepromser_chip.create(E93C46, bits).
#include "eeprom93c46.h"
#include <string.h>

enum { ST_IN_RESET, ST_WAIT_START, ST_WAIT_COMMAND, ST_READING, ST_WAIT_DATA, ST_WAIT_COMPLETION };
enum { CMD_INVALID, CMD_READ, CMD_WRITE, CMD_ERASE, CMD_LOCK, CMD_UNLOCK, CMD_WRITEALL, CMD_ERASEALL };
enum { EV_CS_RISING = 1, EV_CS_FALLING = 2, EV_CLK_RISING = 4, EV_CLK_FALLING = 8 };

void eeprom93c46_init(eeprom93c46_t *e, int data_bits)
{
    memset(e, 0, sizeof *e);
    memset(e->data, 0xff, sizeof e->data);
    e->data_bits = data_bits == 16 ? 16 : 8;
    e->command_address_bits = e->data_bits == 16 ? 6 : 7;
    e->address_bits = e->data_bits == 16 ? 6 : 7;
    eeprom93c46_reset(e);
}

void eeprom93c46_reset(eeprom93c46_t *e)
{
    e->state = ST_IN_RESET;
    e->locked = true;
    e->bits_accum = 0;
    e->command_address_accum = 0;
    e->command = CMD_INVALID;
    e->address = 0;
    e->shift_register = 0;
}

void eeprom93c46_set_timing(eeprom93c46_t *e, uint32_t ups)
{
    e->write_time = (uint32_t)((uint64_t)ups * 1750 / 1000000);
    e->erase_time = ups / 1000;
    e->writeall_time = e->eraseall_time = ups / 125;
}

static bool ready(const eeprom93c46_t *e) { return e->now >= e->busy_until; }

uint8_t eeprom93c46_do_read(const eeprom93c46_t *e)
{
    if (e->state == ST_WAIT_START) return ready(e);
    if (e->state == ST_READING && !(e->shift_register & 0x80000000u)) return 0;
    return 1;
}

static void write_cell(eeprom93c46_t *e, uint16_t a, uint16_t v)
{
    if (e->data_bits == 16) {
        unsigned i = (a & 0x3f) * 2u;
        e->data[i] = (uint8_t)(v >> 8);
        e->data[i + 1] = (uint8_t)v;
    } else {
        e->data[a & 0x7f] = (uint8_t)v;
    }
    e->dirty = true;
}

static uint16_t read_cell(const eeprom93c46_t *e, uint16_t a)
{
    if (e->data_bits == 16) {
        unsigned i = (a & 0x3f) * 2u;
        return (uint16_t)((e->data[i] << 8) | e->data[i + 1]);
    }
    return e->data[a & 0x7f];
}

static void parse_command_and_address(eeprom93c46_t *e)
{
    int cab = e->command_address_bits;
    e->command = CMD_INVALID;
    e->address = (uint16_t)(e->command_address_accum & ((1u << cab) - 1));
    switch (e->command_address_accum >> cab) {
    case 0:
        switch (e->address >> (cab - 2)) {
        case 0: e->command = CMD_LOCK; break;
        case 1: e->command = CMD_WRITEALL; break;
        case 2: e->command = CMD_ERASEALL; break;
        case 3: e->command = CMD_UNLOCK; break;
        }
        e->address = 0;
        break;
    case 1: e->command = CMD_WRITE; break;
    case 2: e->command = CMD_READ; break;
    case 3: e->command = CMD_ERASE; break;
    }
}

static void execute_write_command(eeprom93c46_t *e)
{
    switch (e->command) {
    case CMD_WRITE:
        if (e->locked) { e->state = ST_IN_RESET; return; }
        write_cell(e, e->address, (uint16_t)e->shift_register);
        e->busy_until = e->now + e->write_time;
        e->state = ST_WAIT_COMPLETION;
        break;
    case CMD_WRITEALL:
        if (e->locked) { e->state = ST_IN_RESET; return; }
        for (uint16_t c = 0; c < (1u << e->address_bits); c++)
            write_cell(e, c, (uint16_t)(read_cell(e, c) & e->shift_register));
        e->busy_until = e->now + e->writeall_time;
        e->state = ST_WAIT_COMPLETION;
        break;
    default: break;
    }
}

static void execute_command(eeprom93c46_t *e)
{
    parse_command_and_address(e);
    e->bits_accum = 0;
    switch (e->command) {
    case CMD_READ:
        e->shift_register = 0;
        e->state = ST_READING;
        break;
    case CMD_WRITE:
    case CMD_WRITEALL:
        e->shift_register = 0;
        e->state = ST_WAIT_DATA;
        break;
    case CMD_ERASE:
        if (e->locked) { e->state = ST_IN_RESET; return; }
        write_cell(e, e->address, 0xffff);
        e->busy_until = e->now + e->erase_time;
        e->state = ST_WAIT_COMPLETION;
        break;
    case CMD_LOCK:
        e->locked = true;
        e->state = ST_IN_RESET;
        break;
    case CMD_UNLOCK:
        e->locked = false;
        e->state = ST_IN_RESET;
        break;
    case CMD_ERASEALL:
        if (e->locked) { e->state = ST_IN_RESET; return; }
        for (uint16_t c = 0; c < (1u << e->address_bits); c++) write_cell(e, c, 0xffff);
        e->busy_until = e->now + e->eraseall_time;
        e->state = ST_WAIT_COMPLETION;
        break;
    default: break;
    }
}

static void handle_event(eeprom93c46_t *e, int ev)
{
    switch (e->state) {
    case ST_IN_RESET:
        if (ev == EV_CS_RISING) e->state = ST_WAIT_START;
        break;
    case ST_WAIT_START:
        if (ev == EV_CLK_RISING && e->di && ready(e)) {
            e->command_address_accum = 0;
            e->bits_accum = 0;
            e->state = ST_WAIT_COMMAND;
        } else if (ev == EV_CS_FALLING) e->state = ST_IN_RESET;
        break;
    case ST_WAIT_COMMAND:
        if (ev == EV_CLK_RISING) {
            e->command_address_accum = (e->command_address_accum << 1) | e->di;
            if (++e->bits_accum == 2 + e->command_address_bits) execute_command(e);
        } else if (ev == EV_CS_FALLING) e->state = ST_IN_RESET;
        break;
    case ST_READING:
        if (ev == EV_CLK_RISING) {
            int bit_index = e->bits_accum++;
            if (bit_index == 0) {
                uint16_t cell = (uint16_t)((e->address + e->bits_accum / e->data_bits) & ((1u << e->address_bits) - 1));
                e->shift_register = (uint32_t)read_cell(e, cell) << (32 - e->data_bits);
            } else {
                e->shift_register = (e->shift_register << 1) | 1;
            }
        } else if (ev == EV_CS_FALLING) e->state = ST_IN_RESET;
        break;
    case ST_WAIT_DATA:
        if (ev == EV_CLK_RISING) {
            e->shift_register = (e->shift_register << 1) | e->di;
            if (++e->bits_accum == e->data_bits) execute_write_command(e);
        } else if (ev == EV_CS_FALLING) e->state = ST_IN_RESET;
        break;
    case ST_WAIT_COMPLETION:
        if (ev == EV_CS_FALLING) e->state = ST_IN_RESET;
        break;
    }
}

void eeprom93c46_cs_write(eeprom93c46_t *e, uint8_t s)
{
    s &= 1;
    if (s == e->cs) return;
    e->cs = s;
    handle_event(e, s ? EV_CS_RISING : EV_CS_FALLING);
}

void eeprom93c46_clk_write(eeprom93c46_t *e, uint8_t s)
{
    s &= 1;
    if (s == e->clk) return;
    e->clk = s;
    handle_event(e, s ? EV_CLK_RISING : EV_CLK_FALLING);
}

void eeprom93c46_di_write(eeprom93c46_t *e, uint8_t s) { e->di = s & 1; }
