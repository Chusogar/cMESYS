// ym2151.c - Yamaha YM2151 (OPM). C port of dsp-cpp's sound/ym2151.cpp.
#include "ym2151.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef YM_HOT
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define YM_HOT IRAM_ATTR   /* hot code out of the flash cache */
#else
#define YM_HOT
#endif
#endif

#define FREQ_SH 16
#define EG_SH 16
#define LFO_SH 10
#define FREQ_MASK 0xffffu
#define ENV_BITS 10
#define ENV_STEP 0.125
#define MAX_ATT_INDEX 0x3ff
#define MIN_ATT_INDEX 0
#define EG_ATT 4
#define EG_DEC 3
#define EG_SUS 2
#define EG_REL 1
#define EG_OFF 0
#define SIN_LEN 0x400
#define SIN_MASK 0x3ff
#define TL_RES_LEN 256
#define TL_TAB_LEN (13 * 2 * TL_RES_LEN)
#define ENV_QUIET (TL_TAB_LEN >> 3)
#define RATE_STEPS 8
#define MAX_OUT 32767
#define MIN_OUT (-32768)

// Pascal's sshr(): shifts the magnitude, so it truncates towards zero.
static inline int32_t sshr32(int32_t v, int s) { return v < 0 ? -((-v) >> s) : (v >> s); }

static const uint8_t eg_inc[19 * RATE_STEPS] = {
    0, 1, 0, 1, 0, 1, 0, 1,  0, 1, 0, 1, 1, 1, 0, 1,  0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 2, 1, 1, 1, 2,
    1, 2, 1, 2, 1, 2, 1, 2,  1, 2, 2, 2, 1, 2, 2, 2,  2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 4, 2, 2, 2, 4,  2, 4, 2, 4, 2, 4, 2, 4,  2, 4, 4, 4, 2, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4,  4, 4, 4, 8, 4, 4, 4, 8,  4, 8, 4, 8, 4, 8, 4, 8,
    4, 8, 8, 8, 4, 8, 8, 8,  8, 8, 8, 8, 8, 8, 8, 8,  16, 16, 16, 16, 16, 16, 16, 16,
    0, 0, 0, 0, 0, 0, 0, 0};

static uint8_t rate_select[128], rate_shift[128];

static const uint32_t dt2_tab[4] = {0, 384, 500, 608};

static const uint8_t dt1_tab[4 * 32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,
    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
    5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16,
    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22};

static const uint16_t phaseinc_rom[768] = {
    1299, 1300, 1301, 1302, 1303, 1304, 1305, 1306, 1308, 1309, 1310, 1311, 1313, 1314, 1315, 1316,
    1318, 1319, 1320, 1321, 1322, 1323, 1324, 1325, 1327, 1328, 1329, 1330, 1332, 1333, 1334, 1335,
    1337, 1338, 1339, 1340, 1341, 1342, 1343, 1344, 1346, 1347, 1348, 1349, 1351, 1352, 1353, 1354,
    1356, 1357, 1358, 1359, 1361, 1362, 1363, 1364, 1366, 1367, 1368, 1369, 1371, 1372, 1373, 1374,
    1376, 1377, 1378, 1379, 1381, 1382, 1383, 1384, 1386, 1387, 1388, 1389, 1391, 1392, 1393, 1394,
    1396, 1397, 1398, 1399, 1401, 1402, 1403, 1404, 1406, 1407, 1408, 1409, 1411, 1412, 1413, 1414,
    1416, 1417, 1418, 1419, 1421, 1422, 1423, 1424, 1426, 1427, 1429, 1430, 1431, 1432, 1434, 1435,
    1437, 1438, 1439, 1440, 1442, 1443, 1444, 1445, 1447, 1448, 1449, 1450, 1452, 1453, 1454, 1455,
    1458, 1459, 1460, 1461, 1463, 1464, 1465, 1466, 1468, 1469, 1471, 1472, 1473, 1474, 1476, 1477,
    1479, 1480, 1481, 1482, 1484, 1485, 1486, 1487, 1489, 1490, 1492, 1493, 1494, 1495, 1497, 1498,
    1501, 1502, 1503, 1504, 1506, 1507, 1509, 1510, 1512, 1513, 1514, 1515, 1517, 1518, 1520, 1521,
    1523, 1524, 1525, 1526, 1528, 1529, 1531, 1532, 1534, 1535, 1536, 1537, 1539, 1540, 1542, 1543,
    1545, 1546, 1547, 1548, 1550, 1551, 1553, 1554, 1556, 1557, 1558, 1559, 1561, 1562, 1564, 1565,
    1567, 1568, 1569, 1570, 1572, 1573, 1575, 1576, 1578, 1579, 1580, 1581, 1583, 1584, 1586, 1587,
    1590, 1591, 1592, 1593, 1595, 1596, 1598, 1599, 1601, 1602, 1604, 1605, 1607, 1608, 1609, 1610,
    1613, 1614, 1615, 1616, 1618, 1619, 1621, 1622, 1624, 1625, 1627, 1628, 1630, 1631, 1632, 1633,
    1637, 1638, 1639, 1640, 1642, 1643, 1645, 1646, 1648, 1649, 1651, 1652, 1654, 1655, 1656, 1657,
    1660, 1661, 1663, 1664, 1666, 1667, 1669, 1670, 1672, 1673, 1675, 1676, 1678, 1679, 1681, 1682,
    1685, 1686, 1688, 1689, 1691, 1692, 1694, 1695, 1697, 1698, 1700, 1701, 1703, 1704, 1706, 1707,
    1709, 1710, 1712, 1713, 1715, 1716, 1718, 1719, 1721, 1722, 1724, 1725, 1727, 1728, 1730, 1731,
    1734, 1735, 1737, 1738, 1740, 1741, 1743, 1744, 1746, 1748, 1749, 1751, 1752, 1754, 1755, 1757,
    1759, 1760, 1762, 1763, 1765, 1766, 1768, 1769, 1771, 1773, 1774, 1776, 1777, 1779, 1780, 1782,
    1785, 1786, 1788, 1789, 1791, 1793, 1794, 1796, 1798, 1799, 1801, 1802, 1804, 1806, 1807, 1809,
    1811, 1812, 1814, 1815, 1817, 1819, 1820, 1822, 1824, 1825, 1827, 1828, 1830, 1832, 1833, 1835,
    1837, 1838, 1840, 1841, 1843, 1845, 1846, 1848, 1850, 1851, 1853, 1854, 1856, 1858, 1859, 1861,
    1864, 1865, 1867, 1868, 1870, 1872, 1873, 1875, 1877, 1879, 1880, 1882, 1884, 1885, 1887, 1888,
    1891, 1892, 1894, 1895, 1897, 1899, 1900, 1902, 1904, 1906, 1907, 1909, 1911, 1912, 1914, 1915,
    1918, 1919, 1921, 1923, 1925, 1926, 1928, 1930, 1932, 1933, 1935, 1937, 1939, 1940, 1942, 1944,
    1946, 1947, 1949, 1951, 1953, 1954, 1956, 1958, 1960, 1961, 1963, 1965, 1967, 1968, 1970, 1972,
    1975, 1976, 1978, 1980, 1982, 1983, 1985, 1987, 1989, 1990, 1992, 1994, 1996, 1997, 1999, 2001,
    2003, 2004, 2006, 2008, 2010, 2011, 2013, 2015, 2017, 2019, 2021, 2022, 2024, 2026, 2028, 2029,
    2032, 2033, 2035, 2037, 2039, 2041, 2043, 2044, 2047, 2048, 2050, 2052, 2054, 2056, 2058, 2059,
    2062, 2063, 2065, 2067, 2069, 2071, 2073, 2074, 2077, 2078, 2080, 2082, 2084, 2086, 2088, 2089,
    2092, 2093, 2095, 2097, 2099, 2101, 2103, 2104, 2107, 2108, 2110, 2112, 2114, 2116, 2118, 2119,
    2122, 2123, 2125, 2127, 2129, 2131, 2133, 2134, 2137, 2139, 2141, 2142, 2145, 2146, 2148, 2150,
    2153, 2154, 2156, 2158, 2160, 2162, 2164, 2165, 2168, 2170, 2172, 2173, 2176, 2177, 2179, 2181,
    2185, 2186, 2188, 2190, 2192, 2194, 2196, 2197, 2200, 2202, 2204, 2205, 2208, 2209, 2211, 2213,
    2216, 2218, 2220, 2222, 2223, 2226, 2227, 2230, 2232, 2234, 2236, 2238, 2239, 2242, 2243, 2246,
    2249, 2251, 2253, 2255, 2256, 2259, 2260, 2263, 2265, 2267, 2269, 2271, 2272, 2275, 2276, 2279,
    2281, 2283, 2285, 2287, 2288, 2291, 2292, 2295, 2297, 2299, 2301, 2303, 2304, 2307, 2308, 2311,
    2315, 2317, 2319, 2321, 2322, 2325, 2326, 2329, 2331, 2333, 2335, 2337, 2338, 2341, 2342, 2345,
    2348, 2350, 2352, 2354, 2355, 2358, 2359, 2362, 2364, 2366, 2368, 2370, 2371, 2374, 2375, 2378,
    2382, 2384, 2386, 2388, 2389, 2392, 2393, 2396, 2398, 2400, 2402, 2404, 2407, 2410, 2411, 2414,
    2417, 2419, 2421, 2423, 2424, 2427, 2428, 2431, 2433, 2435, 2437, 2439, 2442, 2445, 2446, 2449,
    2452, 2454, 2456, 2458, 2459, 2462, 2463, 2466, 2468, 2470, 2472, 2474, 2477, 2480, 2481, 2484,
    2488, 2490, 2492, 2494, 2495, 2498, 2499, 2502, 2504, 2506, 2508, 2510, 2513, 2516, 2517, 2520,
    2524, 2526, 2528, 2530, 2531, 2534, 2535, 2538, 2540, 2542, 2544, 2546, 2549, 2552, 2553, 2556,
    2561, 2563, 2565, 2567, 2568, 2571, 2572, 2575, 2577, 2579, 2581, 2583, 2586, 2589, 2590, 2593,
};

static const uint8_t lfo_noise_waveform[256] = {
    0xFF, 0xEE, 0xD3, 0x80, 0x58, 0xDA, 0x7F, 0x94, 0x9E, 0xE3, 0xFA, 0x00, 0x4D, 0xFA, 0xFF, 0x6A,
    0x7A, 0xDE, 0x49, 0xF6, 0x00, 0x33, 0xBB, 0x63, 0x91, 0x60, 0x51, 0xFF, 0x00, 0xD8, 0x7F, 0xDE,
    0xDC, 0x73, 0x21, 0x85, 0xB2, 0x9C, 0x5D, 0x24, 0xCD, 0x91, 0x9E, 0x76, 0x7F, 0x20, 0xFB, 0xF3,
    0x00, 0xA6, 0x3E, 0x42, 0x27, 0x69, 0xAE, 0x33, 0x45, 0x44, 0x11, 0x41, 0x72, 0x73, 0xDF, 0xA2,
    0x32, 0xBD, 0x7E, 0xA8, 0x13, 0xEB, 0xD3, 0x15, 0xDD, 0xFB, 0xC9, 0x9D, 0x61, 0x2F, 0xBE, 0x9D,
    0x23, 0x65, 0x51, 0x6A, 0x84, 0xF9, 0xC9, 0xD7, 0x23, 0xBF, 0x65, 0x19, 0xDC, 0x03, 0xF3, 0x24,
    0x33, 0xB6, 0x1E, 0x57, 0x5C, 0xAC, 0x25, 0x89, 0x4D, 0xC5, 0x9C, 0x99, 0x15, 0x07, 0xCF, 0xBA,
    0xC5, 0x9B, 0x15, 0x4D, 0x8D, 0x2A, 0x1E, 0x1F, 0xEA, 0x2B, 0x2F, 0x64, 0xA9, 0x50, 0x3D, 0xAB,
    0x50, 0x77, 0xE9, 0xC0, 0xAC, 0x6D, 0x3F, 0xCA, 0xCF, 0x71, 0x7D, 0x80, 0xA6, 0xFD, 0xFF, 0xB5,
    0xBD, 0x6F, 0x24, 0x7B, 0x00, 0x99, 0x5D, 0xB1, 0x48, 0xB0, 0x28, 0x7F, 0x80, 0xEC, 0xBF, 0x6F,
    0x6E, 0x39, 0x90, 0x42, 0xD9, 0x4E, 0x2E, 0x12, 0x66, 0xC8, 0xCF, 0x3B, 0x3F, 0x10, 0x7D, 0x79,
    0x00, 0xD3, 0x1F, 0x21, 0x93, 0x34, 0xD7, 0x19, 0x22, 0xA2, 0x08, 0x20, 0xB9, 0xB9, 0xEF, 0x51,
    0x99, 0xDE, 0xBF, 0xD4, 0x09, 0x75, 0xE9, 0x8A, 0xEE, 0xFD, 0xE4, 0x4E, 0x30, 0x17, 0xDF, 0xCE,
    0x11, 0xB2, 0x28, 0x35, 0xC2, 0x7C, 0x64, 0xEB, 0x91, 0x5F, 0x32, 0x0C, 0x6E, 0x00, 0xF9, 0x92,
    0x19, 0xDB, 0x8F, 0xAB, 0xAE, 0xD6, 0x12, 0xC4, 0x26, 0x62, 0xCE, 0xCC, 0x0A, 0x03, 0xE7, 0xDD,
    0xE2, 0x4D, 0x8A, 0xA6, 0x46, 0x95, 0x0F, 0x8F, 0xF5, 0x15, 0x97, 0x32, 0xD4, 0x28, 0x1E, 0x55,
};

static int16_t w_tl[TL_RES_LEN];
static uint16_t w_sin[SIN_LEN];
static uint32_t w_d1l[16];
static bool tables_made;

static inline int32_t tl_value_calc(uint32_t p);
static int16_t w_tl_full[TL_TAB_LEN];

static void make_tables(void)
{
    if (tables_made) return;
    tables_made = true;
    for (int i = 0; i < 32; ++i) { rate_select[i] = 18 * RATE_STEPS; rate_shift[i] = 0; }
    for (int i = 0; i < 48; ++i) {
        rate_select[32 + i] = (uint8_t)((i % 4) * RATE_STEPS);
        rate_shift[32 + i] = (uint8_t)(11 - i / 4);
    }
    for (int i = 0; i < 16; ++i) {
        int rate = 4 + i < 16 ? 4 + i : 16;
        rate_select[80 + i] = (uint8_t)(rate * RATE_STEPS);
        rate_shift[80 + i] = 0;
    }
    for (int i = 0; i < 32; ++i) { rate_select[96 + i] = 16 * RATE_STEPS; rate_shift[96 + i] = 0; }

    for (int x = 0; x < TL_RES_LEN; ++x) {
        double m = floor((1 << 16) / pow(2.0, (x + 1) * (ENV_STEP / 4.0) / 8.0));
        int n = (int)(uint16_t)lround(m);
        n = sshr32(n, 4);
        n = (n & 1) ? sshr32(n, 1) + 1 : sshr32(n, 1);
        n *= 4;
        w_tl[x] = (int16_t)n;
    }
    for (int i = 0; i < SIN_LEN; ++i) {
        double m = sin(((i * 2) + 1) * M_PI / SIN_LEN);
        double o = (m > 0.0) ? 8 * log(1.0 / m) / log(2.0) : 8 * log(-1.0 / m) / log(2.0);
        o /= (ENV_STEP / 4);
        int n = (int)(uint16_t)lround(2.0 * o);
        n = (n & 1) ? sshr32(n, 1) + 1 : sshr32(n, 1);
        w_sin[i] = (uint16_t)(m >= 0 ? n * 2 : n * 2 + 1);
    }
    for (int i = 0; i < 16; ++i) {
        double m = (i != 15) ? i * (4.0 / ENV_STEP) : (i + 16) * (4.0 / ENV_STEP);
        w_d1l[i] = (uint32_t)m;
    }
    for (uint32_t p = 0; p < TL_TAB_LEN; p++) w_tl_full[p] = (int16_t)tl_value_calc(p);
}

// tl_value(p) for every p < TL_TAB_LEN, made once (13 KB): w_tl_full.

static inline int32_t tl_value_calc(uint32_t p)
{
    int32_t v = sshr32((int)w_tl[(p & (2 * TL_RES_LEN - 1)) >> 1], (int)(p / (2 * TL_RES_LEN)));
    return (p & 1) ? -v : v;
}

static inline int32_t tl_value(uint32_t p) { return w_tl_full[p]; }

static inline uint32_t ym_freq(const ym2151 *c, uint32_t index)
{
    static const uint8_t row_of[33] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5,
                                       5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10};
    uint32_t row = row_of[(index >> 8) < 33 ? (index >> 8) : 32];
    uint32_t i = index - row * 768;
    if (row == 0) return (c->freq_base[0] >> 2) & 0xffffffc0u;
    if (row >= 9) return c->freq_base[767] << 5;
    int octave = (int)row - 1;
    if (octave < 2) return (c->freq_base[i] >> (2 - octave)) & 0xffffffc0u;
    return c->freq_base[i] << (octave - 2);
}

static void init_chip_tables(ym2151 *c)
{
    double scaler = (c->clock / 64.0) / c->sample_rate;
    double mult = 1 << (FREQ_SH - 10);
    for (int i = 0; i < 768; ++i) {
        double phaseinc = phaseinc_rom[i] * scaler;
        c->freq_base[i] = (uint32_t)(phaseinc * mult) & 0xffffffc0u;
    }
    mult = 1 << FREQ_SH;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 32; ++i) {
            double hz = (dt1_tab[j * 32 + i] * (c->clock / 64.0)) / (1 << 20);
            double phaseinc = (hz * SIN_LEN) / c->sample_rate;
            c->dt1_freq[(j + 0) * 32 + i] = (int32_t)(phaseinc * mult);
            c->dt1_freq[(j + 4) * 32 + i] = -c->dt1_freq[(j + 0) * 32 + i];
        }
    for (int i = 0; i < 32; ++i) {
        double pom = (i != 31) ? i : 30;
        pom = 32 - pom;
        pom = 65536.0 / (pom * 32.0);
        c->noise_tab[i] = (uint32_t)(pom * 64 * scaler);
    }
}

void ym2151_init(ym2151 *c, uint32_t clock, uint32_t sample_rate)
{
    void (*h)(void *, bool) = c->irq_handler;
    void *u = c->user;
    memset(c, 0, sizeof *c);
    c->irq_handler = h;
    c->user = u;
    make_tables();
    c->clock = clock;
    c->sample_rate = sample_rate ? sample_rate : YM2151_DEFAULT_RATE;
    init_chip_tables(c);
    c->lfo_timer_add = (uint32_t)((1 << LFO_SH) * (clock / 64.0) / c->sample_rate);
    c->eg_timer_add = (uint32_t)((1 << EG_SH) * (clock / 64.0) / c->sample_rate);
    c->eg_timer_overflow = 3 * (1 << EG_SH);
    ym2151_reset(c);
}

static void set_irq_bit(ym2151 *c, uint8_t bit)
{
    uint8_t old = c->irq_line_state;
    c->irq_line_state |= bit;
    if (old == 0 && c->irq_handler) c->irq_handler(c->user, true);
}

static void clear_irq_bit(ym2151 *c, uint8_t bit)
{
    uint8_t old = c->irq_line_state;
    c->irq_line_state &= (uint8_t)~bit;
    if (old != 0 && c->irq_line_state == 0 && c->irq_handler) c->irq_handler(c->user, false);
}

void ym2151_clear_external_irq(ym2151 *c)
{
    c->status &= ~3u;
    c->irq_line_state = 0;
    if (c->irq_handler) c->irq_handler(c->user, false);
}

static void key_on(ym2151 *c, int op_index, uint32_t key_set)
{
    ym2151_op *op = &c->oper[op_index];
    if (op->key == 0) {
        op->phase = 0;
        op->state = EG_ATT;
        c->eg_active |= 1u << op_index;
        op->volume += (~op->volume * eg_inc[op->eg_sel_ar + ((c->eg_cnt >> op->eg_sh_ar) & 7)]) / 16;
        if (op->volume <= MIN_ATT_INDEX) {
            op->volume = MIN_ATT_INDEX;
            op->state = EG_DEC;
        }
    }
    op->key |= key_set;
}

static void key_off(ym2151_op *op, uint32_t key_clr)
{
    if (op->key != 0) {
        op->key &= ~key_clr;
        if (op->key == 0 && op->state > EG_REL) op->state = EG_REL;
    }
}

static void envelope_konkoff(ym2151 *c, int i, uint8_t v)
{
    if (v & 0x08) key_on(c, i + 0, 1); else key_off(&c->oper[i + 0], 1);
    if (v & 0x20) key_on(c, i + 1, 1); else key_off(&c->oper[i + 1], 1);
    if (v & 0x10) key_on(c, i + 2, 1); else key_off(&c->oper[i + 2], 1);
    if (v & 0x40) key_on(c, i + 3, 1); else key_off(&c->oper[i + 3], 1);
}

static void set_connect(ym2151 *c, int op_index, int ch, uint8_t v)
{
    ym2151_op *om1 = &c->oper[op_index];
    ym2151_op *om2 = &c->oper[op_index + 1];
    ym2151_op *oc1 = &c->oper[op_index + 2];
    switch (v & 7) {
    case 0: om1->connect = &c->c1; oc1->connect = &c->mem; om2->connect = &c->c2; om1->mem_connect = &c->m2; break;
    case 1: om1->connect = &c->mem; oc1->connect = &c->mem; om2->connect = &c->c2; om1->mem_connect = &c->m2; break;
    case 2: om1->connect = &c->c2; oc1->connect = &c->mem; om2->connect = &c->c2; om1->mem_connect = &c->m2; break;
    case 3: om1->connect = &c->c1; oc1->connect = &c->mem; om2->connect = &c->c2; om1->mem_connect = &c->c2; break;
    case 4: om1->connect = &c->c1; oc1->connect = &c->chanout[ch]; om2->connect = &c->c2; om1->mem_connect = &c->mem; break;
    case 5: om1->connect = NULL; oc1->connect = &c->chanout[ch]; om2->connect = &c->chanout[ch]; om1->mem_connect = &c->m2; break;
    case 6: om1->connect = &c->c1; oc1->connect = &c->chanout[ch]; om2->connect = &c->chanout[ch]; om1->mem_connect = &c->mem; break;
    default: om1->connect = &c->chanout[ch]; oc1->connect = &c->chanout[ch]; om2->connect = &c->chanout[ch]; om1->mem_connect = &c->mem; break;
    }
}

static void refresh_eg(ym2151 *c, int op_index)
{
    uint32_t kc = c->oper[op_index].kc;
    for (int f = 0; f < 4; ++f) {
        ym2151_op *op = &c->oper[op_index + f];
        uint32_t v = kc >> op->ks;
        if ((op->ar + v) < (32 + 62)) {
            op->eg_sh_ar = rate_shift[(op->ar + v) & 0x7f];
            op->eg_sel_ar = rate_select[(op->ar + v) & 0x7f];
        } else {
            op->eg_sh_ar = 0;
            op->eg_sel_ar = 17 * RATE_STEPS;
        }
        op->eg_sh_d1r = rate_shift[(op->d1r + v) & 0x7f];
        op->eg_sel_d1r = rate_select[(op->d1r + v) & 0x7f];
        op->eg_sh_d2r = rate_shift[(op->d2r + v) & 0x7f];
        op->eg_sel_d2r = rate_select[(op->d2r + v) & 0x7f];
        op->eg_sh_rr = rate_shift[(op->rr + v) & 0x7f];
        op->eg_sel_rr = rate_select[(op->rr + v) & 0x7f];
    }
}

#define OP_FREQ(o, kci) ((((ym_freq(c, (kci) + (o)->dt2) + (uint32_t)(o)->dt1) * (o)->mul) >> 1))

void ym2151_write_reg(ym2151 *c, uint8_t r, uint8_t v)
{
    if ((r & 0xe0) == 0x20 || (r & 0xe0) == 0x40 || (r & 0xe0) == 0xc0) c->pm_key[r & 7] = 0xffffffffu;
    int op_index = (r & 0x07) * 4 + ((r & 0x18) >> 3);
    ym2151_op *op = &c->oper[op_index];

    switch (r & 0xe0) {
    case 0x00:
        switch (r) {
        case 0x01:
            c->test = v;
            if (v & 2) c->lfo_phase = 0;
            break;
        case 0x08: envelope_konkoff(c, (v & 7) * 4, v); break;
        case 0x0f:
            c->noise = v;
            c->noise_f = c->noise_tab[v & 0x1f];
            break;
        case 0x10: c->timer_a_index = (c->timer_a_index & 0x003) | ((uint32_t)v << 2); break;
        case 0x11: c->timer_a_index = (c->timer_a_index & 0x3fc) | (v & 3); break;
        case 0x12: c->timer_b_index = v; break;
        case 0x14:
            c->irq_enable = v;
            if (v & 0x10) { c->status &= ~1u; clear_irq_bit(c, 1); }
            if (v & 0x20) { c->status &= ~2u; clear_irq_bit(c, 2); }
            if (v & 0x02) {
                if (!c->timer_b_enabled) {
                    c->timer_b_counter = (int32_t)(1024 * (256 - c->timer_b_index));
                    c->timer_b_enabled = true;
                }
            } else c->timer_b_enabled = false;
            if (v & 0x01) {
                if (!c->timer_a_enabled) {
                    c->timer_a_counter = (int32_t)(64 * (1024 - c->timer_a_index));
                    c->timer_a_enabled = true;
                }
            } else c->timer_a_enabled = false;
            break;
        case 0x18:
            c->lfo_overflow = (uint32_t)(1 << ((15 - (v >> 4)) + 3)) * (1 << LFO_SH);
            c->lfo_counter_add = 0x10 + (v & 0x0f);
            break;
        case 0x19:
            if (v & 0x80) c->pmd = (int16_t)(v & 0x7f); else c->amd = (uint8_t)(v & 0x7f);
            break;
        case 0x1b:
            c->ct = (uint8_t)(v >> 6);
            c->lfo_wsel = (uint8_t)(v & 3);
            break;
        default: break;
        }
        break;
    case 0x20: {
        op_index = (r & 7) * 4;
        op = &c->oper[op_index];
        ym2151_op *op2 = &c->oper[op_index + 1], *op3 = &c->oper[op_index + 2], *op4 = &c->oper[op_index + 3];
        switch (r & 0x18) {
        case 0x00:
            op->fb_shift = ((v >> 3) & 7) ? (uint32_t)(((v >> 3) & 7) + 6) : 0;
            c->pan[(r & 7) * 2] = (v & 0x40) ? 0xffffffffu : 0;
            c->pan[(r & 7) * 2 + 1] = (v & 0x80) ? 0xffffffffu : 0;
            c->connect[r & 7] = (uint8_t)(v & 7);
            set_connect(c, op_index, r & 7, (uint8_t)(v & 7));
            break;
        case 0x08: {
            uint32_t value = v & 0x7f;
            if (value != op->kc) {
                uint32_t kc_channel = (value - (value >> 2)) * 64;
                kc_channel += 768;
                kc_channel |= (op->kc_i & 63);
                op->kc = op2->kc = op3->kc = op4->kc = value;
                op->kc_i = op2->kc_i = op3->kc_i = op4->kc_i = kc_channel;
                uint32_t kc = value >> 2;
                op->dt1 = c->dt1_freq[(op->dt1_i + kc) & 0xff];
                op->freq = OP_FREQ(op, kc_channel);
                op2->dt1 = c->dt1_freq[(op2->dt1_i + kc) & 0xff];
                op2->freq = OP_FREQ(op2, kc_channel);
                op3->dt1 = c->dt1_freq[(op3->dt1_i + kc) & 0xff];
                op3->freq = OP_FREQ(op3, kc_channel);
                op4->dt1 = c->dt1_freq[(op4->dt1_i + kc) & 0xff];
                op4->freq = OP_FREQ(op4, kc_channel);
                refresh_eg(c, op_index);
            }
            break;
        }
        case 0x10: {
            uint32_t value = v >> 2;
            if (value != (op->kc_i & 63)) {
                uint32_t kc_channel = value | (op->kc_i & 0xffffffc0u);
                op->kc_i = op2->kc_i = op3->kc_i = op4->kc_i = kc_channel;
                op->freq = OP_FREQ(op, kc_channel);
                op2->freq = OP_FREQ(op2, kc_channel);
                op3->freq = OP_FREQ(op3, kc_channel);
                op4->freq = OP_FREQ(op4, kc_channel);
            }
            break;
        }
        default:
            op->pms = (v >> 4) & 7;
            op->ams = v & 3;
            break;
        }
        break;
    }
    case 0x40: {
        uint32_t olddt1_i = op->dt1_i, oldmul = op->mul;
        op->dt1_i = (uint32_t)(v & 0x70) << 1;
        op->mul = (v & 0x0f) ? (uint32_t)((v & 0x0f) << 1) : 1;
        if (olddt1_i != op->dt1_i) op->dt1 = c->dt1_freq[(op->dt1_i + (op->kc >> 2)) & 0xff];
        if (olddt1_i != op->dt1_i || oldmul != op->mul) op->freq = OP_FREQ(op, op->kc_i);
        break;
    }
    case 0x60:
        op->tl = (uint32_t)(v & 0x7f) << (ENV_BITS - 7);
        break;
    case 0x80: {
        uint32_t oldks = op->ks, oldar = op->ar;
        op->ks = 5 - (v >> 6);
        op->ar = (v & 0x1f) ? (uint32_t)(32 + ((v & 0x1f) << 1)) : 0;
        if (op->ar != oldar || op->ks != oldks) {
            if ((op->ar + (op->kc >> op->ks)) < (32 + 62)) {
                op->eg_sh_ar = rate_shift[(op->ar + (op->kc >> op->ks)) & 0x7f];
                op->eg_sel_ar = rate_select[(op->ar + (op->kc >> op->ks)) & 0x7f];
            } else {
                op->eg_sh_ar = 0;
                op->eg_sel_ar = 17 * RATE_STEPS;
            }
        }
        if (op->ks != oldks) {
            op->eg_sh_d1r = rate_shift[(op->d1r + (op->kc >> op->ks)) & 0x7f];
            op->eg_sel_d1r = rate_select[(op->d1r + (op->kc >> op->ks)) & 0x7f];
            op->eg_sh_d2r = rate_shift[(op->d2r + (op->kc >> op->ks)) & 0x7f];
            op->eg_sel_d2r = rate_select[(op->d2r + (op->kc >> op->ks)) & 0x7f];
            op->eg_sh_rr = rate_shift[(op->rr + (op->kc >> op->ks)) & 0x7f];
            op->eg_sel_rr = rate_select[(op->rr + (op->kc >> op->ks)) & 0x7f];
        }
        break;
    }
    case 0xa0:
        op->am_mask = (v & 0x80) ? 0xffffffffu : 0;
        op->d1r = (v & 0x1f) ? (uint32_t)(32 + ((v & 0x1f) << 1)) : 0;
        op->eg_sh_d1r = rate_shift[(op->d1r + (op->kc >> op->ks)) & 0x7f];
        op->eg_sel_d1r = rate_select[(op->d1r + (op->kc >> op->ks)) & 0x7f];
        break;
    case 0xc0: {
        uint32_t olddt2 = op->dt2;
        op->dt2 = dt2_tab[(v >> 6) & 3];
        if (op->dt2 != olddt2) op->freq = OP_FREQ(op, op->kc_i);
        op->d2r = (v & 0x1f) ? (uint32_t)(32 + ((v & 0x1f) << 1)) : 0;
        op->eg_sh_d2r = rate_shift[(op->d2r + (op->kc >> op->ks)) & 0x7f];
        op->eg_sel_d2r = rate_select[(op->d2r + (op->kc >> op->ks)) & 0x7f];
        break;
    }
    default:
        op->d1l = w_d1l[(v >> 4) & 0x0f];
        op->rr = (uint32_t)(34 + ((v & 0x0f) << 2));
        op->eg_sh_rr = rate_shift[(op->rr + (op->kc >> op->ks)) & 0x7f];
        op->eg_sel_rr = rate_select[(op->rr + (op->kc >> op->ks)) & 0x7f];
        break;
    }
}

void ym2151_reset(ym2151 *c)
{
    c->eg_active = 0;
    for (int i = 0; i < 32; ++i) {
        memset(&c->oper[i], 0, sizeof c->oper[i]);
        c->oper[i].volume = MAX_ATT_INDEX;
        c->oper[i].kc_i = 768;
    }
    c->eg_timer = c->eg_cnt = 0;
    c->lfo_timer = c->lfo_counter = c->lfo_phase = 0;
    c->lfo_wsel = 0;
    c->pmd = 0;
    c->amd = 0;
    c->lfa = 0;
    c->lfp = 0;
    c->test = 0;
    c->irq_enable = 0;
    c->timer_a_index = c->timer_b_index = 0;
    c->timer_a_enabled = c->timer_b_enabled = false;
    c->timer_a_counter = c->timer_b_counter = 0;
    c->noise = c->noise_rng = c->noise_p = 0;
    c->noise_f = c->noise_tab[0];
    c->csm_req = 0;
    c->status = 0;
    for (int ch = 0; ch < 8; ++ch) c->pm_key[ch] = 0xffffffffu;
    c->irq_line_state = 0;
    memset(c->chanout, 0, sizeof c->chanout);
    c->m2 = c->c1 = c->c2 = c->mem = 0;
    ym2151_write_reg(c, 0x1b, 0);
    ym2151_write_reg(c, 0x18, 0);
    for (int i = 0x20; i <= 0xff; ++i) ym2151_write_reg(c, (uint8_t)i, 0);
    for (int ch = 0; ch < 16; ++ch) c->pan[ch] = 0xffffffffu;
}

static void timer_a_expired(ym2151 *c)
{
    if (c->irq_enable & 0x04) { c->status |= 1; set_irq_bit(c, 1); }
    if (c->irq_enable & 0x80) c->csm_req = 2;
}

static void timer_b_expired(ym2151 *c)
{
    if (c->irq_enable & 0x08) { c->status |= 2; set_irq_bit(c, 2); }
}

void YM_HOT ym2151_run_timers(ym2151 *c, int cycles)
{
    if (c->timer_a_enabled) {
        c->timer_a_counter -= cycles;
        while (c->timer_a_counter <= 0) {
            c->timer_a_counter += (int32_t)(64 * (1024 - c->timer_a_index));
            timer_a_expired(c);
        }
    }
    if (c->timer_b_enabled) {
        c->timer_b_counter -= cycles;
        while (c->timer_b_counter <= 0) {
            c->timer_b_counter += (int32_t)(1024 * (256 - c->timer_b_index));
            timer_b_expired(c);
        }
    }
}

static inline int32_t op_calc(const ym2151_op *op, uint32_t env, int32_t pm)
{
    // ((int64)(phase & ~0xffff) + (pm << 15)) >> 16 rounded towards zero, in
    // 32 bits: that sum is 32768 * (2 * (int16)(phase >> 16) + pm).
    int32_t k = 2 * (int32_t)(int16_t)(op->phase >> 16) + pm;
    uint32_t p = (env << 3) + w_sin[(k / 2) & SIN_MASK];
    return (p >= TL_TAB_LEN) ? 0 : tl_value(p);
}

static inline int32_t op_calc1(const ym2151_op *op, uint32_t env, int32_t pm)
{
    int32_t i = (int32_t)(op->phase & ~FREQ_MASK) + pm;
    uint32_t p = (env << 3) + w_sin[sshr32(i, FREQ_SH) & SIN_MASK];
    return (p >= TL_TAB_LEN) ? 0 : tl_value(p);
}

static inline uint32_t volume_calc(const ym2151_op *op, uint32_t am)
{
    return op->tl + (uint32_t)op->volume + (am & op->am_mask);
}

static inline void chan_calc(ym2151 *c, int ch)
{
    uint32_t am = 0;
    c->m2 = c->c1 = c->c2 = c->mem = 0;
    ym2151_op *op = &c->oper[ch * 4], *op2 = op + 1, *op3 = op + 2, *op4 = op + 3;

    *op->mem_connect = op->mem_value;
    if (op->ams != 0) am = c->lfa << (op->ams - 1);
    uint32_t env = volume_calc(op, am);

    int32_t out = op->fb_out_prev + op->fb_out_curr;
    op->fb_out_prev = op->fb_out_curr;
    if (op->connect == NULL) c->mem = c->c1 = c->c2 = op->fb_out_prev;
    else *op->connect = op->fb_out_prev;
    op->fb_out_curr = 0;
    if (env < ENV_QUIET) {
        if (op->fb_shift == 0) out = 0;
        op->fb_out_curr = op_calc1(op, env, out << op->fb_shift);
    }
    env = volume_calc(op2, am);
    if (env < ENV_QUIET) *op2->connect += op_calc(op2, env, c->m2);
    env = volume_calc(op3, am);
    if (env < ENV_QUIET) *op3->connect += op_calc(op3, env, c->c1);
    env = volume_calc(op4, am);
    if (ch == 7 && (c->noise & 0x80)) {
        uint32_t noiseout = 0;
        if (env < 0x3ff) noiseout = (env ^ 0x3ff) * 2;
        if (c->noise_rng & 0x10000) c->chanout[7] += (int32_t)noiseout;
        else c->chanout[7] -= (int32_t)noiseout;
    } else if (env < ENV_QUIET) {
        c->chanout[ch] += op_calc(op4, env, c->c2);
    }
    op->mem_value = c->mem;
}

static inline void advance_eg(ym2151 *c)
{
    c->eg_timer += c->eg_timer_add;
    while (c->eg_timer >= c->eg_timer_overflow) {
        c->eg_timer -= c->eg_timer_overflow;
        c->eg_cnt++;
        for (uint32_t m = c->eg_active; m; m &= m - 1) {
            const int i = __builtin_ctz(m);
            ym2151_op *op = &c->oper[i];
            switch (op->state) {
            case EG_ATT:
                if ((c->eg_cnt & ((1u << op->eg_sh_ar) - 1)) == 0) {
                    op->volume += (~op->volume * eg_inc[op->eg_sel_ar + ((c->eg_cnt >> op->eg_sh_ar) & 7)]) / 16;
                    if (op->volume <= MIN_ATT_INDEX) { op->volume = MIN_ATT_INDEX; op->state = EG_DEC; }
                }
                break;
            case EG_DEC:
                if ((c->eg_cnt & ((1u << op->eg_sh_d1r) - 1)) == 0) {
                    op->volume += eg_inc[op->eg_sel_d1r + ((c->eg_cnt >> op->eg_sh_d1r) & 7)];
                    if ((uint32_t)op->volume >= op->d1l) op->state = EG_SUS;
                }
                break;
            case EG_SUS:
                if ((c->eg_cnt & ((1u << op->eg_sh_d2r) - 1)) == 0) {
                    op->volume += eg_inc[op->eg_sel_d2r + ((c->eg_cnt >> op->eg_sh_d2r) & 7)];
                    if (op->volume >= MAX_ATT_INDEX) { op->volume = MAX_ATT_INDEX; op->state = EG_OFF; c->eg_active &= ~(1u << i); }
                }
                break;
            case EG_REL:
                if ((c->eg_cnt & ((1u << op->eg_sh_rr) - 1)) == 0) {
                    op->volume += eg_inc[op->eg_sel_rr + ((c->eg_cnt >> op->eg_sh_rr) & 7)];
                    if (op->volume >= MAX_ATT_INDEX) { op->volume = MAX_ATT_INDEX; op->state = EG_OFF; c->eg_active &= ~(1u << i); }
                }
                break;
            default: break;
            }
        }
    }
}

static inline void advance(ym2151 *c)
{
    if (c->test & 2) {
        c->lfo_phase = 0;
    } else {
        c->lfo_timer += c->lfo_timer_add;
        if (c->lfo_timer >= c->lfo_overflow) {
            c->lfo_timer -= c->lfo_overflow;
            c->lfo_counter += c->lfo_counter_add;
            c->lfo_phase = (c->lfo_phase + (c->lfo_counter >> 4)) & 255;
            c->lfo_counter &= 15;
        }
    }
    int i = (int)c->lfo_phase, a = 0, p = 0;
    switch (c->lfo_wsel) {
    case 0: a = 255 - i; p = (i < 128) ? i : i - 255; break;
    case 1: if (i < 128) { a = 255; p = 128; } else { a = 0; p = -128; } break;
    case 2:
        a = (i < 128) ? 255 - (i * 2) : (i * 2) - 256;
        if (i < 64) p = i * 2;
        else if (i < 128) p = 255 - i * 2;
        else if (i < 192) p = 256 - i * 2;
        else p = i * 2 - 511;
        break;
    default: a = lfo_noise_waveform[i]; p = a - 128; break;
    }
    c->lfa = (uint32_t)(a * c->amd / 128);
    c->lfp = (int32_t)(p * c->pmd / 128);

    c->noise_p += c->noise_f;
    uint32_t shifts = c->noise_p >> 16;
    c->noise_p &= 0xffff;
    while (shifts != 0) {
        uint32_t j = ((c->noise_rng ^ (c->noise_rng >> 3)) & 1) ^ 1;
        c->noise_rng = (j << 16) | (c->noise_rng >> 1);
        shifts--;
    }

    for (int ch = 0; ch < 8; ++ch) {
        ym2151_op *op = &c->oper[ch * 4], *op2 = op + 1, *op3 = op + 2, *op4 = op + 3;
        int32_t mod_ind = 0;
        if (op->pms != 0) {
            mod_ind = c->lfp;
            mod_ind = (op->pms < 6) ? sshr32(mod_ind, (int)(6 - op->pms)) : (mod_ind << (op->pms - 5));
        }
        if (op->pms != 0 && mod_ind != 0) {
            uint32_t kc_channel = op->kc_i + (uint32_t)mod_ind;
            uint32_t *inc = c->pm_inc[ch];
            if (c->pm_key[ch] != kc_channel) {
                c->pm_key[ch] = kc_channel;
                inc[0] = (uint32_t)sshr32((int32_t)((ym_freq(c, kc_channel + op->dt2) + (uint32_t)op->dt1) * op->mul), 1);
                inc[1] = (uint32_t)sshr32((int32_t)((ym_freq(c, kc_channel + op2->dt2) + (uint32_t)op2->dt1) * op2->mul), 1);
                inc[2] = (uint32_t)sshr32((int32_t)((ym_freq(c, kc_channel + op3->dt2) + (uint32_t)op3->dt1) * op3->mul), 1);
                inc[3] = (uint32_t)sshr32((int32_t)((ym_freq(c, kc_channel + op4->dt2) + (uint32_t)op4->dt1) * op4->mul), 1);
            }
            op->phase += inc[0];
            op2->phase += inc[1];
            op3->phase += inc[2];
            op4->phase += inc[3];
        } else {
            op->phase += op->freq;
            op2->phase += op2->freq;
            op3->phase += op3->freq;
            op4->phase += op4->freq;
        }
    }

    if (c->csm_req != 0) {
        if (c->csm_req == 2) {
            for (int k = 0; k < 32; ++k) key_on(c, k, 2);
            c->csm_req = 1;
        } else {
            for (int k = 0; k < 32; ++k) key_off(&c->oper[k], 2);
            c->csm_req = 0;
        }
    }
}

int32_t YM_HOT ym2151_update(ym2151 *c)
{
    advance_eg(c);
    memset(c->chanout, 0, sizeof c->chanout);
    for (int ch = 0; ch < 8; ++ch) {
        // A channel with its four operators off and its feedback/delay state
        // at zero adds nothing and changes nothing: skip it.
        const ym2151_op *o = &c->oper[ch * 4];
        if ((o[0].state | o[1].state | o[2].state | o[3].state) == EG_OFF &&
            (o->fb_out_prev | o->fb_out_curr | o->mem_value) == 0)
            continue;
        chan_calc(c, ch);
    }

    int32_t outl = 0, outr = 0;
    for (int ch = 0; ch < 8; ++ch) {
        outl += c->chanout[ch] & (int32_t)c->pan[ch * 2];
        outr += c->chanout[ch] & (int32_t)c->pan[ch * 2 + 1];
    }
    if (outl < MIN_OUT) outl = MIN_OUT; else if (outl > MAX_OUT) outl = MAX_OUT;
    if (outr < MIN_OUT) outr = MIN_OUT; else if (outr > MAX_OUT) outr = MAX_OUT;
    c->out_left = outl;
    c->out_right = outr;
    advance(c);
    int32_t mono = outl + outr;
    if (mono < -32767) mono = -32767; else if (mono > 32767) mono = 32767;
    return mono;
}
