// starwars_math.c - the Star Wars mathbox (see starwars_math.h).
#include "starwars_math.h"
#include <string.h>

enum { LAC = 0x01, READ_ACC = 0x02, HALT = 0x04, INC_BIC = 0x08, CLEAR_ACC = 0x10, LDC = 0x20, LDB = 0x40, LDA = 0x80 };

void sw_math_init(sw_math *m, const uint8_t *prom)
{
    for (int cnt = 0; cnt < 1024; cnt++) {
        int val = prom[0x0c00 + cnt] & 0x0f;
        val |= (prom[0x0800 + cnt] << 4) & 0x00f0;
        val |= (prom[0x0400 + cnt] << 8) & 0x0f00;
        val |= (prom[0x0000 + cnt] << 12) & 0xf000;
        m->prom_str[cnt] = (uint8_t)((val >> 8) & 0xff);
        m->prom_mas[cnt] = (uint8_t)(val & 0x7f);
        m->prom_am[cnt] = (uint8_t)((val >> 7) & 1);
    }
}

void sw_math_reset(sw_math *m)
{
    m->mpa = m->bic = 0;
    m->a = m->b = m->c = 0;
    m->acc = 0;
    m->divisor = m->dividend = m->dvd_shift = m->quotient_shift = 0;
    m->run_cycles = 0;
}

static void run_mproc(sw_math *m)
{
    int mptime = 0, stop = 100000;
    m->run_cycles = 1;
    while (stop > 0) {
        mptime += 5;
        const int ip15_8 = m->prom_str[m->mpa], ip7 = m->prom_am[m->mpa], ip6_0 = m->prom_mas[m->mpa];
        const int ma = ip7 == 0 ? ((ip6_0 & 3) | ((m->bic & 0x1ff) << 2)) : ip6_0;
        const int mb = ma << 1;
        const int ramword = (m->ram[mb + 1] & 0xff) | ((m->ram[mb] & 0xff) << 8);
        if (ip15_8 & CLEAR_ACC) m->acc = 0;
        if (ip15_8 & LAC) m->acc = (int32_t)((uint32_t)ramword << 16);
        if (ip15_8 & READ_ACC) {
            m->ram[mb + 1] = (uint8_t)((m->acc >> 16) & 0xff);
            m->ram[mb] = (uint8_t)((m->acc >> 24) & 0xff);
        }
        if (ip15_8 & HALT) stop = 0;
        if (ip15_8 & INC_BIC) m->bic = (m->bic + 1) & 0x1ff;
        if (ip15_8 & LDC) {
            m->c = (int16_t)ramword;
            // (as dsp-cpp's int arithmetic, wrapping)
            m->acc = (int32_t)((uint32_t)m->acc + ((((uint32_t)(int32_t)(m->a - m->b) << 1) * (uint32_t)(int32_t)m->c) << 1));
            m->a = (m->a & 0x8000) ? (int16_t)-1 : 0;
            m->b = (m->b & 0x8000) ? (int16_t)-1 : 0;
            mptime += 33;
        }
        if (ip15_8 & LDB) m->b = (int16_t)ramword;
        if (ip15_8 & LDA) m->a = (int16_t)ramword;
        const int tmp = m->mpa + 1;
        m->mpa = (m->mpa & 0x0300) | (tmp & 0x00ff);
        stop--;
    }
    m->run_cycles = (mptime + 7) / 8;
    if (m->run_cycles < 1) m->run_cycles = 1;
}

void sw_math_write(sw_math *m, uint8_t offset, uint8_t data)
{
    switch (offset & 7) {
    case 0:
        m->mpa = (int)data << 2;
        run_mproc(m);
        break;
    case 1: m->bic = (m->bic & 0x00ff) | (((int)data & 1) << 8); break;
    case 2: m->bic = (m->bic & 0x0100) | data; break;
    case 4:
        m->divisor = (uint16_t)((m->divisor & 0x00ff) | ((uint16_t)data << 8));
        m->dvd_shift = m->dividend;
        m->quotient_shift = 0;
        break;
    case 5:
        m->divisor = (uint16_t)((m->divisor & 0xff00) | data);
        for (int i = 1; i < 16; i++) {
            m->quotient_shift = (uint16_t)(m->quotient_shift << 1);
            if (((int32_t)m->dvd_shift + (int32_t)(m->divisor ^ 0xffff) + 1) & 0x10000) {
                m->quotient_shift |= 1;
                m->dvd_shift = (uint16_t)((m->dvd_shift + (m->divisor ^ 0xffff) + 1) << 1);
            } else {
                m->dvd_shift = (uint16_t)(m->dvd_shift << 1);
            }
        }
        break;
    case 6: m->dividend = (uint16_t)((m->dividend & 0x00ff) | ((uint16_t)data << 8)); break;
    case 7: m->dividend = (uint16_t)((m->dividend & 0xff00) | data); break;
    default: break;
    }
}
