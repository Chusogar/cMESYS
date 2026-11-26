// s18_roms.h - the System 18 ROM sets, from dsp-cpp's system18_roms.h.
static const arc_rom kAstormMain[] = {
    {"epr-13182.a6", 0x40000, 0x0, 0xe31f2a1c},
    {"epr-13181.a5", 0x40000, 0x1, 0x78cd3b26},
};

static const arc_rom kAstormKey[] = {
    {"317-0154.key", 0x2000, 0x0, 0xb86b6b8f},
};

static const arc_rom kAstormTiles[] = {
    {"epr-13073.bin", 0x40000, 0x0, 0xdf5d0a61},
    {"epr-13074.bin", 0x40000, 0x40000, 0x787afab8},
    {"epr-13075.bin", 0x40000, 0x80000, 0x4e01b477},
};

static const arc_rom kAstormSprites[] = {
    {"mpr-13082.bin", 0x40000, 0x1, 0xa782b704},
    {"mpr-13089.bin", 0x40000, 0x0, 0x2a4227f0},
    {"mpr-13081.bin", 0x40000, 0x80001, 0xeb510228},
    {"mpr-13088.bin", 0x40000, 0x80000, 0x3b6b4c55},
    {"mpr-13080.bin", 0x40000, 0x100001, 0xe668eefb},
    {"mpr-13087.bin", 0x40000, 0x100000, 0x2293427d},
    {"epr-13079.bin", 0x40000, 0x180001, 0xde9221ed},
    {"epr-13086.bin", 0x40000, 0x180000, 0x8c9a71c4},
};

static const arc_rom kAstormSound[] = {
    {"epr-13083a.bin", 0x20000, 0x0, 0xe7528e06},
    {"epr-13076.bin", 0x40000, 0x80000, 0x94e6c76e},
    {"epr-13077.bin", 0x40000, 0x100000, 0xe2ec0d8d},
    {"epr-13078.bin", 0x40000, 0x180000, 0x15684dc5},
};


// Bloxeed (Japan) — MAME bloxeed
static const arc_rom kBloxeedMain[] = {
    {"epr-12911.a6", 0x20000, 0x0, 0xa481581a},
    {"epr-12910.a5", 0x20000, 0x1, 0xdd1bc3bf},
};

static const arc_rom kBloxeedKey[] = {
    {"317-0139.key", 0x2000, 0x0, 0x9aae84cb},
};

static const arc_rom kBloxeedTiles[] = {
    {"opr-12884.b1", 0x10000, 0x0, 0xe024aa33},
    {"opr-12885.b2", 0x10000, 0x10000, 0x8041b814},
    {"opr-12886.b3", 0x10000, 0x20000, 0xde32285e},
};

static const arc_rom kBloxeedSprites[] = {
    {"opr-12891.a11", 0x10000, 0x0, 0x90d31a8c},
    {"opr-12887.b11", 0x10000, 0x1, 0xf0c0f49d},
};

static const arc_rom kBloxeedSound[] = {
    {"epr-12888.a4", 0x20000, 0x0, 0x6f2fc63c},
};


// Clutch Hitter (US) — MAME cltchitr
static const arc_rom kCltchitrMain[] = {
    {"epr-13794.a4", 0x40000, 0x0, 0xc8d80233},
    {"epr-13795.a6", 0x40000, 0x1, 0xb0b60b67},
    {"epr-13784.a5", 0x40000, 0x80000, 0x80c8180d},
    {"epr-13786.a7", 0x40000, 0x80001, 0x3095dac0},
};

static const arc_rom kCltchitrKey[] = {
    {"317-0176.key", 0x2000, 0x0, 0x9b072430},
};

static const arc_rom kCltchitrTiles[] = {
    {"mpr-13773.c1", 0x80000, 0x0, 0x3fc600e5},
    {"mpr-13774.c2", 0x80000, 0x80000, 0x2411a824},
    {"mpr-13775.c3", 0x80000, 0x100000, 0xcf527bf6},
};

static const arc_rom kCltchitrSprites[] = {
    {"mpr-13779.c10", 0x80000, 0x1, 0xc707f416},
    {"mpr-13787.a10", 0x80000, 0x0, 0xf05c68c6},
    {"mpr-13780.c11", 0x80000, 0x200001, 0xa4c341e0},
    {"mpr-13788.a11", 0x80000, 0x200000, 0x0106fea6},
    {"mpr-13781.c12", 0x80000, 0x400001, 0xf33b13af},
    {"mpr-13789.a12", 0x80000, 0x400000, 0x09ba8835},
};

static const arc_rom kCltchitrSound[] = {
    {"epr-13793.c7", 0x80000, 0x0, 0xa3d31944},
    {"epr-13792.c6", 0x80000, 0x80000, 0x808f9695},
    {"epr-13791.c5", 0x80000, 0x100000, 0x35c16d80},
};


// D. D. Crew (World, 3 Players) — MAME ddcrew
static const arc_rom kDdcrewMain[] = {
    {"epr-14160.a4", 0x40000, 0x0, 0xb9f897b7},
    {"epr-14161.a6", 0x40000, 0x1, 0xbb03c1f0},
    {"mpr-14139.a5", 0x40000, 0x80000, 0x06c31531},
    {"mpr-14141.a7", 0x40000, 0x80001, 0x080a494b},
};

static const arc_rom kDdcrewKey[] = {
    {"317-0190.key", 0x2000, 0x0, 0x2d502b11},
};

static const arc_rom kDdcrewTiles[] = {
    {"epr-14127.c1", 0x40000, 0x0, 0x2228cd88},
    {"epr-14128.c2", 0x40000, 0x40000, 0xedba8e10},
    {"epr-14129.c3", 0x40000, 0x80000, 0xe8ecc305},
};

static const arc_rom kDdcrewSprites[] = {
    {"mpr-14134.c10", 0x80000, 0x1, 0x4fda6a4b},
    {"mpr-14142.a10", 0x80000, 0x0, 0x3cbf1f2a},
    {"mpr-14135.c11", 0x80000, 0x200001, 0xe9c74876},
    {"mpr-14143.a11", 0x80000, 0x200000, 0x59022c31},
    {"mpr-14136.c12", 0x80000, 0x400001, 0x720d9858},
    {"mpr-14144.a12", 0x80000, 0x400000, 0x7775fdd4},
    {"epr-14137.c13", 0x80000, 0x600001, 0x846c4265},
    {"epr-14145.a13", 0x80000, 0x600000, 0x0e76c797},
};

static const arc_rom kDdcrewSound[] = {
    {"epr-14133.c7", 0x20000, 0x0, 0xcff96665},
    {"mpr-14132.c6", 0x80000, 0x80000, 0x1fae0220},
    {"mpr-14131.c5", 0x80000, 0x100000, 0xbe5a7d0b},
    {"epr-14130.c4", 0x80000, 0x180000, 0x948f34a1},
};


// Desert Breaker (World) — MAME desertbr
static const arc_rom kDesertbrMain[] = {
    {"epr-14802.a4", 0x80000, 0x0, 0x9ab93cbc},
    {"epr-14902.a6", 0x80000, 0x1, 0x6724e7b1},
    {"epr-14793.a5", 0x80000, 0x100000, 0xdc9d7af3},
    {"epr-14795.a7", 0x80000, 0x100001, 0x7e5bf7d9},
};

static const arc_rom kDesertbrKey[] = {
    {"317-0196.key", 0x2000, 0x0, 0xcb942262},
};

static const arc_rom kDesertbrTiles[] = {
    {"mpr-14781.c1", 0x100000, 0x0, 0xc4f7d7aa},
    {"mpr-14782.c2", 0x100000, 0x100000, 0xccc98d05},
    {"mpr-14783.c3", 0x100000, 0x200000, 0xef202bec},
};

static const arc_rom kDesertbrSprites[] = {
    {"mpr-14788.c10", 0x100000, 0x1, 0xb5b05536},
    {"mpr-14796.a10", 0x100000, 0x0, 0xc033220a},
    {"mpr-14789.c11", 0x100000, 0x200001, 0x0f9bcb97},
    {"mpr-14797.a11", 0x100000, 0x200000, 0x4c301cc9},
    {"mpr-14790.c12", 0x100000, 0x400001, 0x6a07ac27},
    {"mpr-14798.a12", 0x100000, 0x400000, 0x50634625},
    {"mpr-14791.c13", 0x100000, 0x600001, 0xa4ae352b},
    {"mpr-14799.a13", 0x100000, 0x600000, 0xaeb7b025},
};

static const arc_rom kDesertbrSound[] = {
    {"epr-14787.c7", 0x40000, 0x0, 0xcc6feec7},
    {"mpr-14786.c6", 0x80000, 0x80000, 0xcc8349f2},
    {"mpr-14785.c5", 0x80000, 0x100000, 0x7babba13},
    {"mpr-14784.c4", 0x80000, 0x180000, 0x073878e4},
};


// Hammer Away (Japan, prototype) — MAME hamaway
static const arc_rom kHamawayMain[] = {
    {"4.bin", 0x40000, 0x0, 0xcc0981e1},
    {"6.bin", 0x40000, 0x1, 0xe8599ee6},
    {"5.bin", 0x40000, 0x80000, 0xfdb247fd},
    {"7.bin", 0x40000, 0x80001, 0x63711470},
};

static const arc_rom kHamawayTiles[] = {
    {"c10.bin", 0x40000, 0x0, 0xc55cb5cf},
    {"1.bin", 0x40000, 0x40000, 0x33be003f},
    {"c11.bin", 0x40000, 0x80000, 0x37787915},
    {"2.bin", 0x40000, 0xc0000, 0x60ca5c9f},
    {"c12.bin", 0x40000, 0x100000, 0xf12f1cf3},
    {"3.bin", 0x40000, 0x140000, 0x520aa7ae},
};

static const arc_rom kHamawaySprites[] = {
    {"c17.bin", 0x40000, 0x1, 0xaa28d7aa},
    {"10.bin", 0x40000, 0x0, 0xc4c95161},
    {"c18.bin", 0x40000, 0x80001, 0x0f8fe8bb},
    {"11.bin", 0x40000, 0x80000, 0x2b5eacbc},
    {"c19.bin", 0x40000, 0x100001, 0x3c616caa},
    {"12.bin", 0x40000, 0x100000, 0xc7bbd579},
};

static const arc_rom kHamawaySound[] = {
    {"c16.bin", 0x40000, 0x0, 0x913cc18c},
    {"c15.bin", 0x40000, 0x80000, 0xb53694fc},
};


// Laser Ghost (World) — MAME lghost
static const arc_rom kLghostMain[] = {
    {"epr-13429.a4", 0x40000, 0x0, 0x09bd65c0},
    {"epr-13430.a6", 0x40000, 0x1, 0x51009fe0},
    {"epr-13411.a5", 0x40000, 0x80000, 0x5160167b},
    {"epr-13413.a7", 0x40000, 0x80001, 0x656b3bd8},
};

static const arc_rom kLghostKey[] = {
    {"317-0166.key", 0x2000, 0x0, 0x8379961f},
};

static const arc_rom kLghostTiles[] = {
    {"epr-13414.c1", 0x40000, 0x0, 0xdada2419},
    {"epr-13415.c2", 0x40000, 0x40000, 0xbbb62c48},
    {"epr-13416.c3", 0x40000, 0x80000, 0x1d11dbae},
};

static const arc_rom kLghostSprites[] = {
    {"epr-13603.a10", 0x80000, 0x0, 0x5350a94e},
    {"epr-13604.c10", 0x80000, 0x1, 0x4009c8e5},
    {"mpr-13421.a11", 0x80000, 0x200000, 0x2fc75890},
    {"mpr-13424.c11", 0x80000, 0x200001, 0xfb98d920},
    {"mpr-13422.a12", 0x80000, 0x400000, 0x48a0754d},
    {"mpr-13425.c12", 0x80000, 0x400001, 0xf8252589},
    {"mpr-13423.a13", 0x80000, 0x600000, 0x335bbc9d},
    {"mpr-13426.c13", 0x80000, 0x600001, 0x5cfb1e25},
};

static const arc_rom kLghostSound[] = {
    {"epr-13417.c7", 0x20000, 0x0, 0xcd7beb49},
    {"mpr-13420.c6", 0x40000, 0x80000, 0x3de0dee4},
    {"mpr-13419.c5", 0x40000, 0x100000, 0xe7021b0a},
    {"mpr-13418.c4", 0x40000, 0x180000, 0x0732594d},
};


// Michael Jackson's Moonwalker (World) — MAME mwalk
static const arc_rom kMwalkMain[] = {
    {"epr-13235.a6", 0x40000, 0x0, 0x6983e129},
    {"epr-13234.a5", 0x40000, 0x1, 0xc9fd20f2},
};

static const arc_rom kMwalkKey[] = {
    {"317-0159.key", 0x2000, 0x0, 0x507838f0},
};

static const arc_rom kMwalkTiles[] = {
    {"mpr-13216.b1", 0x40000, 0x0, 0x862d2c03},
    {"mpr-13217.b2", 0x40000, 0x40000, 0x7d1ac3ec},
    {"mpr-13218.b3", 0x40000, 0x80000, 0x56d3393c},
};

static const arc_rom kMwalkSprites[] = {
    {"mpr-13224.b11", 0x40000, 0x1, 0xc59f107b},
    {"mpr-13231.a11", 0x40000, 0x0, 0xa5e96346},
    {"mpr-13223.b10", 0x40000, 0x80001, 0x364f60ff},
    {"mpr-13230.a10", 0x40000, 0x80000, 0x9550091f},
    {"mpr-13222.b9", 0x40000, 0x100001, 0x523df3ed},
    {"mpr-13229.a9", 0x40000, 0x100000, 0xf40dc45d},
    {"epr-13221.b8", 0x40000, 0x180001, 0x9ae7546a},
    {"epr-13228.a8", 0x40000, 0x180000, 0xde3786be},
};

static const arc_rom kMwalkSound[] = {
    {"epr-13225.a4", 0x20000, 0x0, 0x56c2e82b},
    {"mpr-13219.b4", 0x40000, 0x80000, 0x19e2061f},
    {"mpr-13220.b5", 0x40000, 0x100000, 0x58d4d9ce},
    {"mpr-13249.b6", 0x40000, 0x180000, 0x623edc5d},
};

static const arc_rom kMwalkMcu[] = {
    {"315-5437.ic4", 0x1000, 0x0, 0x4bf63bc1},
};


// Pontoon — MAME pontoon
static const arc_rom kPontoonMain[] = {
    {"epr-13175.a6", 0x40000, 0x0, 0xa2a5d0f5},
    {"epr-13174.a5", 0x40000, 0x1, 0xdb976b13},
};

static const arc_rom kPontoonKey[] = {
    {"317-0153.key", 0x2000, 0x0, 0xbcac8c7a},
};

static const arc_rom kPontoonTiles[] = {
    {"epr-13097.b1", 0x40000, 0x0, 0x6474b245},
    {"epr-13098.b2", 0x40000, 0x40000, 0x89fc9a9b},
    {"epr-13099.b3", 0x40000, 0x80000, 0x790e0ac6},
};

static const arc_rom kPontoonSprites[] = {
    {"epr-13173.b11", 0x40000, 0x1, 0x40a0ddfa},
    {"epr-13176.a11", 0x40000, 0x0, 0x1184fbd2},
};

static const arc_rom kPontoonSound[] = {
    {"epr-12826a.a4", 0x20000, 0x0, 0xd41e2a3f},
};


// Shadow Dancer (World) — MAME shdancer
static const arc_rom kShdancerMain[] = {
    {"epr-12774b.a6", 0x40000, 0x0, 0x3d5b3fa9},
    {"epr-12773b.a5", 0x40000, 0x1, 0x2596004e},
};

static const arc_rom kShdancerTiles[] = {
    {"mpr-12712.b1", 0x40000, 0x0, 0x9bdabe3d},
    {"mpr-12713.b2", 0x40000, 0x40000, 0x852d2b1c},
    {"mpr-12714.b3", 0x40000, 0x80000, 0x448226ce},
};

static const arc_rom kShdancerSprites[] = {
    {"mpr-12719.b11", 0x40000, 0x1, 0xd6888534},
    {"mpr-12726.a11", 0x40000, 0x0, 0xff344945},
    {"mpr-12718.b10", 0x40000, 0x80001, 0xba2efc0c},
    {"mpr-12725.a10", 0x40000, 0x80000, 0x268a0c17},
    {"mpr-12717.b9", 0x40000, 0x100001, 0xc81cc4f8},
    {"mpr-12724.a9", 0x40000, 0x100000, 0x0f4903dc},
    {"epr-12716.b8", 0x40000, 0x180001, 0xa870e629},
    {"epr-12723.a8", 0x40000, 0x180000, 0xc606cf90},
};

static const arc_rom kShdancerSound[] = {
    {"epr-12987.a4", 0x20000, 0x0, 0xd1c020cc},
    {"mpr-12715.b4", 0x40000, 0x80000, 0x07051a52},
};


// Wally wo Sagase! (rev B, Japan) — MAME wwallyj
static const arc_rom kWwallyjMain[] = {
    {"epr-14730b.a4", 0x40000, 0x0, 0xe72bc17a},
    {"epr-14731b.a6", 0x40000, 0x1, 0x6e3235b9},
};

static const arc_rom kWwallyjKey[] = {
    {"317-0197b.key", 0x2000, 0x0, 0xf5b7c5b4},
};

static const arc_rom kWwallyjTiles[] = {
    {"mpr-14719.c1", 0x40000, 0x0, 0x8b58c743},
    {"mpr-14720.c2", 0x40000, 0x40000, 0xf96d19f4},
    {"mpr-14721.c3", 0x40000, 0x80000, 0xc4ced91d},
};

static const arc_rom kWwallyjSprites[] = {
    {"mpr-14726.c10", 0x100000, 0x1, 0x7213d1d3},
    {"mpr-14732.a10", 0x100000, 0x0, 0x04ced549},
    {"mpr-14727.c11", 0x100000, 0x200001, 0x3b74e0f0},
    {"mpr-14733.a11", 0x100000, 0x200000, 0x6da0444f},
    {"mpr-14728.c12", 0x80000, 0x400001, 0x5b921587},
    {"mpr-14734.a12", 0x80000, 0x400000, 0x6f3f5ed9},
};

static const arc_rom kWwallyjSound[] = {
    {"epr-14725.c7", 0x20000, 0x0, 0x2b29684f},
    {"mpr-14724.c6", 0x80000, 0x80000, 0x47cbea86},
    {"mpr-14723.c5", 0x80000, 0x100000, 0xbc5adc27},
    {"mpr-14722.c4", 0x80000, 0x180000, 0x1bd081f8},
};


