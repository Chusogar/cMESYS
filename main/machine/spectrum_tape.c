// spectrum_tape.c - ZX Spectrum .tap / .tzx EAR player. See spectrum_tape.h.
#include "spectrum_tape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_tzx_magic[8] = { 'Z','X','T','a','p','e','!', 0x1a };

static void free_blocks(spectrum_tape *t)
{
    if (t->blocks) {
        for (size_t i = 0; i < t->block_count; i++)
            free(t->blocks[i].pulses);
        free(t->blocks);
    }
    t->blocks = NULL;
    t->block_count = t->block_cap = 0;
}

void tape_init(spectrum_tape *t)
{
    memset(t, 0, sizeof *t);
    t->phase = TAPE_PHASE_DONE;
    t->finished = true;
}

void tape_unload(spectrum_tape *t)
{
    free_blocks(t);
    free(t->data);
    free(t->loops);
    memset(t, 0, sizeof *t);
    t->phase = TAPE_PHASE_DONE;
    t->finished = true;
}

static bool push_block(spectrum_tape *t, const tape_block *b)
{
    if (t->block_count >= t->block_cap) {
        size_t ncap = t->block_cap ? t->block_cap * 2 : 16;
        tape_block *nb = realloc(t->blocks, ncap * sizeof *nb);
        if (!nb) return false;
        t->blocks = nb;
        t->block_cap = ncap;
    }
    t->blocks[t->block_count++] = *b;
    return true;
}

static void add_standard_block(spectrum_tape *t, size_t start, size_t end)
{
    tape_block b;
    memset(&b, 0, sizeof b);
    b.kind = TAPE_KIND_DATA;
    b.pilot_pulse = TAPE_PILOT_PULSE;
    b.pilot_pulses = t->data[start] < 0x80 ? TAPE_HEADER_PILOT : TAPE_DATA_PILOT;
    b.sync1 = TAPE_SYNC1_PULSE;
    b.sync2 = TAPE_SYNC2_PULSE;
    b.zero = TAPE_BIT0_PULSE;
    b.one = TAPE_BIT1_PULSE;
    b.last_byte_bits = 8;
    b.pause_cycles = TAPE_PAUSE_CYCLES;
    b.data_start = start;
    b.data_end = end;
    (void)push_block(t, &b);
}

static bool parse_tap(spectrum_tape *t)
{
    size_t offset = 0;
    while (offset + 2 <= t->data_size) {
        size_t length = (size_t)t->data[offset] | ((size_t)t->data[offset + 1] << 8);
        size_t start = offset + 2;
        if (length == 0 || start + length > t->data_size) break;
        add_standard_block(t, start, start + length);
        offset = start + length;
    }
    return t->block_count != 0;
}

static int rd8(const spectrum_tape *t, size_t off, size_t i)
{
    return (int)t->data[off + i];
}

static int rd16(const spectrum_tape *t, size_t off, size_t i)
{
    return rd8(t, off, i) | (rd8(t, off, i + 1) << 8);
}

static size_t rd24(const spectrum_tape *t, size_t off, size_t i)
{
    return (size_t)rd16(t, off, i) | ((size_t)rd8(t, off, i + 2) << 16);
}

static bool left(const spectrum_tape *t, size_t offset, size_t bytes)
{
    return offset + bytes <= t->data_size;
}

/* Parse TZX: keep 0x10/0x11/0x20 and useful pulse/data blocks; skip others. */
static bool parse_tzx(spectrum_tape *t)
{
    size_t offset = 10;

    while (offset < t->data_size) {
        int id = t->data[offset++];
        switch (id) {
        case 0x10: {
            if (!left(t, offset, 4)) return t->block_count != 0;
            int pause = rd16(t, offset, 0);
            size_t length = (size_t)rd16(t, offset, 2);
            if (!left(t, offset, 4 + length) || length == 0) return t->block_count != 0;
            add_standard_block(t, offset + 4, offset + 4 + length);
            t->blocks[t->block_count - 1].pause_cycles = pause * TAPE_CYCLES_PER_MS;
            offset += 4 + length;
            break;
        }
        case 0x11: {
            if (!left(t, offset, 18)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_DATA;
            b.pilot_pulse = rd16(t, offset, 0);
            b.sync1 = rd16(t, offset, 2);
            b.sync2 = rd16(t, offset, 4);
            b.zero = rd16(t, offset, 6);
            b.one = rd16(t, offset, 8);
            b.pilot_pulses = rd16(t, offset, 10);
            b.last_byte_bits = rd8(t, offset, 12);
            if (b.last_byte_bits == 0 || b.last_byte_bits > 8) b.last_byte_bits = 8;
            b.pause_cycles = rd16(t, offset, 13) * TAPE_CYCLES_PER_MS;
            size_t length = rd24(t, offset, 15);
            if (!left(t, offset, 18 + length) || length == 0) return t->block_count != 0;
            b.data_start = offset + 18;
            b.data_end = b.data_start + length;
            if (!push_block(t, &b)) return false;
            offset += 18 + length;
            break;
        }
        case 0x12: {
            if (!left(t, offset, 4)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_TONE;
            b.pilot_pulse = rd16(t, offset, 0);
            b.pilot_pulses = rd16(t, offset, 2);
            if (!push_block(t, &b)) return false;
            offset += 4;
            break;
        }
        case 0x13: {
            if (!left(t, offset, 1)) return t->block_count != 0;
            int count = rd8(t, offset, 0);
            if (!left(t, offset, 1 + (size_t)count * 2)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_PULSES;
            b.pulse_count = count;
            if (count > 0) {
                b.pulses = malloc((size_t)count * sizeof(int));
                if (!b.pulses) return false;
                for (int i = 0; i < count; i++)
                    b.pulses[i] = rd16(t, offset, 1 + (size_t)i * 2);
            }
            if (!push_block(t, &b)) {
                free(b.pulses);
                return false;
            }
            offset += 1 + (size_t)count * 2;
            break;
        }
        case 0x14: {
            if (!left(t, offset, 10)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_DATA;
            b.zero = rd16(t, offset, 0);
            b.one = rd16(t, offset, 2);
            b.last_byte_bits = rd8(t, offset, 4);
            if (b.last_byte_bits == 0 || b.last_byte_bits > 8) b.last_byte_bits = 8;
            b.pause_cycles = rd16(t, offset, 5) * TAPE_CYCLES_PER_MS;
            size_t length = rd24(t, offset, 7);
            if (!left(t, offset, 10 + length) || length == 0) return t->block_count != 0;
            b.data_start = offset + 10;
            b.data_end = b.data_start + length;
            if (!push_block(t, &b)) return false;
            offset += 10 + length;
            break;
        }
        case 0x15: {
            if (!left(t, offset, 8)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_DIRECT;
            b.sample_cycles = rd16(t, offset, 0);
            b.pause_cycles = rd16(t, offset, 2) * TAPE_CYCLES_PER_MS;
            b.last_byte_bits = rd8(t, offset, 4);
            if (b.last_byte_bits == 0 || b.last_byte_bits > 8) b.last_byte_bits = 8;
            size_t length = rd24(t, offset, 5);
            if (!left(t, offset, 8 + length) || length == 0) return t->block_count != 0;
            b.data_start = offset + 8;
            b.data_end = b.data_start + length;
            if (!push_block(t, &b)) return false;
            offset += 8 + length;
            break;
        }
        case 0x18:
        case 0x19: {
            if (!left(t, offset, 4)) return t->block_count != 0;
            size_t length = (size_t)rd16(t, offset, 0) | ((size_t)rd16(t, offset, 2) << 16);
            if (!left(t, offset, 4 + length)) return t->block_count != 0;
            offset += 4 + length;
            break;
        }
        case 0x20: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_PAUSE;
            b.pause_cycles = rd16(t, offset, 0) * TAPE_CYCLES_PER_MS;
            if (!push_block(t, &b)) return false;
            offset += 2;
            break;
        }
        case 0x21: {
            if (!left(t, offset, 1)) return t->block_count != 0;
            offset += 1 + (size_t)rd8(t, offset, 0);
            break;
        }
        case 0x22:
        case 0x27:
            break;
        case 0x23: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_JUMP;
            b.target = (int16_t)rd16(t, offset, 0);
            if (!push_block(t, &b)) return false;
            offset += 2;
            break;
        }
        case 0x24: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_LOOP_START;
            b.count = rd16(t, offset, 0);
            if (!push_block(t, &b)) return false;
            offset += 2;
            break;
        }
        case 0x25: {
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_LOOP_END;
            if (!push_block(t, &b)) return false;
            break;
        }
        case 0x26: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            offset += 2 + (size_t)rd16(t, offset, 0) * 2;
            break;
        }
        case 0x28: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            offset += 2 + (size_t)rd16(t, offset, 0);
            break;
        }
        case 0x2a: {
            if (!left(t, offset, 4)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_PAUSE;
            b.pause_cycles = 0;
            if (!push_block(t, &b)) return false;
            offset += 4;
            break;
        }
        case 0x2b: {
            if (!left(t, offset, 5)) return t->block_count != 0;
            tape_block b;
            memset(&b, 0, sizeof b);
            b.kind = TAPE_KIND_SET_LEVEL;
            b.level = rd8(t, offset, 4) != 0;
            if (!push_block(t, &b)) return false;
            offset += 5;
            break;
        }
        case 0x30: {
            if (!left(t, offset, 1)) return t->block_count != 0;
            offset += 1 + (size_t)rd8(t, offset, 0);
            break;
        }
        case 0x31: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            offset += 2 + (size_t)rd8(t, offset, 1);
            break;
        }
        case 0x32: {
            if (!left(t, offset, 2)) return t->block_count != 0;
            offset += 2 + (size_t)rd16(t, offset, 0);
            break;
        }
        case 0x33: {
            if (!left(t, offset, 1)) return t->block_count != 0;
            offset += 1 + (size_t)rd8(t, offset, 0) * 3;
            break;
        }
        case 0x35: {
            if (!left(t, offset, 20)) return t->block_count != 0;
            offset += 20 + ((size_t)rd16(t, offset, 16) | ((size_t)rd16(t, offset, 18) << 16));
            break;
        }
        case 0x5a:
            if (!left(t, offset, 9)) return t->block_count != 0;
            offset += 9;
            break;
        default:
            /* Unknown: stop parsing; keep blocks already found. */
            return t->block_count != 0;
        }
    }
    return t->block_count != 0;
}

static void begin_data_bits(spectrum_tape *t);
static void begin_pause(spectrum_tape *t, int cycles);
static void next_block(spectrum_tape *t);
static void begin_block(spectrum_tape *t, size_t index);

static bool push_loop(spectrum_tape *t, size_t block_index, int remain)
{
    if (t->loop_count >= t->loop_cap) {
        size_t ncap = t->loop_cap ? t->loop_cap * 2 : 4;
        tape_loop *nl = realloc(t->loops, ncap * sizeof *nl);
        if (!nl) return false;
        t->loops = nl;
        t->loop_cap = ncap;
    }
    t->loops[t->loop_count].block_index = block_index;
    t->loops[t->loop_count].remain = remain;
    t->loop_count++;
    return true;
}

static void begin_block(spectrum_tape *t, size_t index)
{
    for (int guard = 0; guard < 4096; guard++) {
        if (index >= t->block_count) {
            t->phase = TAPE_PHASE_DONE;
            t->finished = true;
            t->playing = false;
            return;
        }
        t->block_index = index;
        const tape_block *block = &t->blocks[index];
        switch (block->kind) {
        case TAPE_KIND_DATA:
            t->finished = false;
            if (block->pilot_pulses > 0) {
                t->phase = TAPE_PHASE_PILOT;
                t->pulses_left = block->pilot_pulses;
                t->remaining = block->pilot_pulse;
            } else if (block->sync1 > 0) {
                t->phase = TAPE_PHASE_SYNC1;
                t->remaining = block->sync1;
            } else {
                begin_data_bits(t);
            }
            return;
        case TAPE_KIND_TONE:
            t->finished = false;
            if (block->pilot_pulses <= 0) break;
            t->phase = TAPE_PHASE_PILOT;
            t->pulses_left = block->pilot_pulses;
            t->remaining = block->pilot_pulse;
            return;
        case TAPE_KIND_PULSES:
            t->finished = false;
            if (block->pulse_count <= 0 || !block->pulses) break;
            t->phase = TAPE_PHASE_PULSE_LIST;
            t->pulse_index = 1;
            t->remaining = block->pulses[0];
            return;
        case TAPE_KIND_DIRECT:
            t->finished = false;
            t->byte_index = block->data_start;
            t->bit_mask = 0x80;
            if (t->byte_index >= block->data_end) break;
            t->phase = TAPE_PHASE_DIRECT;
            t->level = (t->data[t->byte_index] & t->bit_mask) != 0;
            t->remaining = block->sample_cycles > 0 ? block->sample_cycles : 1;
            return;
        case TAPE_KIND_PAUSE:
            t->finished = false;
            begin_pause(t, block->pause_cycles > 0 ? block->pause_cycles : TAPE_STOP_CYCLES);
            return;
        case TAPE_KIND_SET_LEVEL:
            t->level = block->level;
            break;
        case TAPE_KIND_JUMP:
            index = (size_t)((int)index + (block->target != 0 ? block->target : 1));
            continue;
        case TAPE_KIND_LOOP_START:
            if (block->count > 0) (void)push_loop(t, index + 1, block->count);
            break;
        case TAPE_KIND_LOOP_END:
            if (t->loop_count > 0) {
                tape_loop *loop = &t->loops[t->loop_count - 1];
                if (--loop->remain > 0) {
                    index = loop->block_index;
                    continue;
                }
                t->loop_count--;
            }
            break;
        default:
            break;
        }
        index++;
    }
    t->phase = TAPE_PHASE_DONE;
    t->finished = true;
    t->playing = false;
}

static void next_block(spectrum_tape *t)
{
    begin_block(t, t->block_index + 1);
}

static void begin_data_bits(spectrum_tape *t)
{
    const tape_block *block = &t->blocks[t->block_index];
    t->byte_index = block->data_start;
    t->bit_mask = 0x80;
    if (t->byte_index >= block->data_end) {
        next_block(t);
        return;
    }
    t->phase = TAPE_PHASE_BIT_FIRST;
    t->pulse_cycles = (t->data[t->byte_index] & t->bit_mask) ? block->one : block->zero;
    t->remaining = t->pulse_cycles;
}

static void begin_pause(spectrum_tape *t, int cycles)
{
    t->level = false;
    if (cycles <= 0) {
        next_block(t);
        return;
    }
    t->phase = TAPE_PHASE_PAUSE;
    t->remaining = cycles;
}

static void next_pulse(spectrum_tape *t)
{
    const tape_block *block = &t->blocks[t->block_index];
    switch (t->phase) {
    case TAPE_PHASE_PILOT:
        t->level = !t->level;
        if (--t->pulses_left > 0) {
            t->remaining = block->pilot_pulse;
        } else if (block->kind == TAPE_KIND_TONE) {
            begin_pause(t, block->pause_cycles);
        } else if (block->sync1 > 0) {
            t->phase = TAPE_PHASE_SYNC1;
            t->remaining = block->sync1;
        } else {
            begin_data_bits(t);
        }
        break;
    case TAPE_PHASE_SYNC1:
        t->level = !t->level;
        if (block->sync2 > 0) {
            t->phase = TAPE_PHASE_SYNC2;
            t->remaining = block->sync2;
        } else {
            begin_data_bits(t);
        }
        break;
    case TAPE_PHASE_SYNC2:
        t->level = !t->level;
        begin_data_bits(t);
        break;
    case TAPE_PHASE_BIT_FIRST:
        t->level = !t->level;
        t->phase = TAPE_PHASE_BIT_SECOND;
        t->remaining = t->pulse_cycles;
        break;
    case TAPE_PHASE_BIT_SECOND: {
        t->level = !t->level;
        t->bit_mask >>= 1;
        bool last_byte = t->byte_index + 1 >= block->data_end;
        int used_bits = last_byte ? block->last_byte_bits : 8;
        if (t->bit_mask == 0 || (0x80 >> used_bits) >= t->bit_mask) {
            t->bit_mask = 0x80;
            t->byte_index++;
        }
        if (t->byte_index >= block->data_end) {
            if (block->sync1 > 0) {
                t->phase = TAPE_PHASE_TAIL;
                t->remaining = TAPE_TAIL_PULSE;
            } else {
                begin_pause(t, block->pause_cycles);
            }
        } else {
            t->phase = TAPE_PHASE_BIT_FIRST;
            t->pulse_cycles = (t->data[t->byte_index] & t->bit_mask) ? block->one : block->zero;
            t->remaining = t->pulse_cycles;
        }
        break;
    }
    case TAPE_PHASE_TAIL:
        begin_pause(t, block->pause_cycles);
        break;
    case TAPE_PHASE_PULSE_LIST:
        t->level = !t->level;
        if ((int)t->pulse_index < block->pulse_count) {
            t->remaining = block->pulses[t->pulse_index++];
        } else {
            begin_pause(t, block->pause_cycles);
        }
        break;
    case TAPE_PHASE_DIRECT: {
        t->bit_mask >>= 1;
        bool last_byte = t->byte_index + 1 >= block->data_end;
        int used_bits = last_byte ? block->last_byte_bits : 8;
        if (t->bit_mask == 0 || (0x80 >> used_bits) >= t->bit_mask) {
            t->bit_mask = 0x80;
            t->byte_index++;
        }
        if (t->byte_index >= block->data_end) {
            begin_pause(t, block->pause_cycles);
        } else {
            t->level = (t->data[t->byte_index] & t->bit_mask) != 0;
            t->remaining = block->sample_cycles > 0 ? block->sample_cycles : 1;
        }
        break;
    }
    case TAPE_PHASE_PAUSE:
        next_block(t);
        break;
    case TAPE_PHASE_DONE:
        t->remaining = TAPE_PAUSE_CYCLES;
        break;
    default:
        t->phase = TAPE_PHASE_DONE;
        break;
    }
}

void tape_rewind(spectrum_tape *t)
{
    t->level = false;
    t->playing = false;
    t->loop_count = 0;
    t->finished = t->block_count == 0;
    t->phase = TAPE_PHASE_DONE;
    t->remaining = TAPE_PAUSE_CYCLES;
    if (t->block_count > 0) begin_block(t, 0);
}

bool tape_load_mem(spectrum_tape *t, const uint8_t *data, size_t size)
{
    tape_unload(t);
    tape_init(t);
    if (!data || size < 3) return false;

    t->data = malloc(size);
    if (!t->data) return false;
    memcpy(t->data, data, size);
    t->data_size = size;

    bool tzx = size > 10 && memcmp(t->data, k_tzx_magic, 8) == 0;
    bool ok = tzx ? parse_tzx(t) : parse_tap(t);
    if (!ok) {
        tape_unload(t);
        return false;
    }
    t->loaded = true;
    tape_rewind(t);
    return true;
}

bool tape_load_file(spectrum_tape *t, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) { free(buf); return false; }
    bool ok = tape_load_mem(t, buf, n);
    free(buf);
    return ok;
}

void tape_play(spectrum_tape *t)
{
    if (!t->loaded) return;
    if (t->finished || t->phase == TAPE_PHASE_DONE) tape_rewind(t);
    t->playing = true;
}

void tape_stop(spectrum_tape *t)
{
    t->playing = false;
}

int tape_ear_bit(spectrum_tape *t, int tstates)
{
    if (t->playing && t->phase != TAPE_PHASE_DONE && tstates > 0) {
        t->remaining -= tstates;
        while (t->remaining <= 0 && t->phase != TAPE_PHASE_DONE) {
            int leftover = t->remaining;
            next_pulse(t);
            t->remaining += leftover;
        }
    }
    return t->level ? 1 : 0;
}
