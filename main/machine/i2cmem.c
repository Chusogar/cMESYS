// i2cmem.c - 24Cxx I2C EEPROM, a C11 port of dsp-cpp's machine/i2cmem.cpp.
#include <string.h>
#include "i2cmem.h"

enum { DEVICE_ADDR, WORD_ADDR, DATA };

void i2cmem_init(i2cmem *m, uint8_t *memory, size_t size)
{
    memset(m, 0, sizeof *m);
    m->memory = memory;
    m->size = size;
    m->addr_bytes = size <= 2048 ? 1 : 2;
    memset(memory, 0xff, size);
    i2cmem_reset(m);
}

void i2cmem_reset(i2cmem *m)
{
    m->scl = false;
    m->bus_sda = true;
    m->started = false;
    m->phase = DEVICE_ADDR;
    m->bitpos = 0;
    m->shift = 0;
    m->write_mode = true;
    m->block_select = m->addr_byte_count = 0;
    m->addr_high = 0;
    m->address = 0;
    m->driving = m->driving_ack = m->pending_first_read = m->awaiting_master_ack = false;
    m->data_byte = 0;
    m->data_bitpos = 0;
}

static void prepare_read_data(i2cmem *m)
{
    m->address = (uint32_t)(m->address % m->size);
    m->data_byte = m->memory[m->address];
    m->address = (uint32_t)((m->address + 1) % m->size);
    m->data_bitpos = 0;
    m->driving = true;
    m->driving_ack = false;
}

static void handle_received_byte(i2cmem *m, uint8_t byte)
{
    switch (m->phase) {
    case DEVICE_ADDR:
        m->write_mode = (byte & 1) == 0;
        m->block_select = (byte >> 1) & 7;
        if (m->write_mode) {
            m->phase = WORD_ADDR;
            m->addr_byte_count = 0;
            m->pending_first_read = false;
        } else {
            m->phase = DATA;
            m->pending_first_read = true;
        }
        break;
    case WORD_ADDR:
        m->addr_byte_count++;
        if (m->addr_bytes == 1) {
            m->address = (uint32_t)((m->block_select << 8) | byte);
            m->phase = DATA;
        } else if (m->addr_byte_count == 1) {
            m->addr_high = byte;
        } else {
            m->address = (uint32_t)((m->addr_high << 8) | byte);
            m->phase = DATA;
        }
        break;
    default:
        m->memory[m->address % m->size] = byte;
        m->address = (uint32_t)((m->address + 1) % m->size);
        break;
    }
}

void i2cmem_write_sda(i2cmem *m, bool level)
{
    if (m->scl) {
        if (m->bus_sda && !level) {                 // START
            m->started = true;
            m->phase = DEVICE_ADDR;
            m->bitpos = 0;
            m->shift = 0;
            m->driving = m->driving_ack = false;
            m->addr_byte_count = 0;
        } else if (!m->bus_sda && level) {          // STOP
            m->started = false;
            m->driving = m->driving_ack = m->awaiting_master_ack = false;
        }
    }
    m->bus_sda = level;
}

void i2cmem_write_scl(i2cmem *m, bool level)
{
    const bool rising = !m->scl && level, falling = m->scl && !level;
    m->scl = level;
    if (!m->started) return;
    if (rising) {
        if (m->awaiting_master_ack) {
            const bool nack = m->bus_sda;
            m->awaiting_master_ack = false;
            if (!nack) prepare_read_data(m);
        } else if (!m->driving) {
            m->shift = (uint8_t)((m->shift << 1) | (m->bus_sda ? 1 : 0));
            if (++m->bitpos == 8) {
                m->bitpos = 0;
                const uint8_t byte = m->shift;
                m->shift = 0;
                handle_received_byte(m, byte);
                m->driving = m->driving_ack = true;
            }
        }
    } else if (falling) {
        if (m->driving_ack) {
            m->driving_ack = false;
            if (m->pending_first_read) {
                m->pending_first_read = false;
                prepare_read_data(m);
            } else {
                m->driving = false;
            }
        } else if (m->driving) {
            if (++m->data_bitpos == 8) {
                m->driving = false;
                m->awaiting_master_ack = true;
            }
        }
    }
}

bool i2cmem_read_sda(const i2cmem *m)
{
    if (m->driving_ack) return false;
    if (m->driving) return ((m->data_byte >> (7 - m->data_bitpos)) & 1) != 0;
    return true;
}
