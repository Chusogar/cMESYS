// s16_roms.h - the System 16A / 16B ROM sets, from dsp-cpp's system16.cpp and
// system16_roms.h (names with | alternatives; CRC 0: any).
static const arc_rom kFantzoneMain[] = {
    {"epr-7385a.43", 0x8000, 0x00000, 0x4091af42},
    {"epr-7382a.26", 0x8000, 0x00001, 0x77d67bfd},
    {"epr-7386a.42", 0x8000, 0x10000, 0xb0a67cd0},
    {"epr-7383a.25", 0x8000, 0x10001, 0x5f79b2a9},
    {"epr-7387.41", 0x8000, 0x20000, 0x0acd335d},
    {"epr-7384.24", 0x8000, 0x20001, 0xfd909341},
};
static const arc_rom kFantzoneSound[] = {{"epr-7535a.12", 0x8000, 0, 0xbc1374fa}};
static const arc_rom kFantzoneTiles[] = {
    {"epr-7388.95", 0x8000, 0x0000, 0x8eb02f6b},
    {"epr-7389.94", 0x8000, 0x8000, 0x2f4f71b8},
    {"epr-7390.93", 0x8000, 0x10000, 0xd90609c6},
};
static const arc_rom kFantzoneSprites[] = {
    {"epr-7392.10", 0x8000, 0x00000, 0x5bb7c8b6},
    {"epr-7396.11", 0x8000, 0x00001, 0x74ae4b57},
    {"epr-7393.17", 0x8000, 0x10000, 0x14fc7e82},
    {"epr-7397.18", 0x8000, 0x10001, 0xe05a1e25},
    {"epr-7394.23", 0x8000, 0x20000, 0x531ca13f},
    {"epr-7398.24", 0x8000, 0x20001, 0x68807b49},
};

static const arc_rom kShinobiMain[] = {
    {"epr-12010.43", 0x10000, 0x00000, 0x7df7f4a2},
    {"epr-12008.26", 0x10000, 0x00001, 0xf5ae64cd},
    {"epr-12011.42", 0x10000, 0x20000, 0x9d46e707},
    {"epr-12009.25", 0x10000, 0x20001, 0x7961d07e},
};
static const arc_rom kShinobiSound[] = {{"epr-11267.12", 0x8000, 0, 0xdd50b745}};
static const arc_rom kShinobiTiles[] = {
    {"epr-11264.95", 0x10000, 0x00000, 0x46627e7d},
    {"epr-11265.94", 0x10000, 0x10000, 0x87d0f321},
    {"epr-11266.93", 0x10000, 0x20000, 0xefb4af87},
};
static const arc_rom kShinobiSprites[] = {
    {"epr-11290.10", 0x10000, 0x00000, 0x611f413a},
    {"epr-11294.11", 0x10000, 0x00001, 0x5eb00fc1},
    {"epr-11291.17", 0x10000, 0x20000, 0x3c0797c0},
    {"epr-11295.18", 0x10000, 0x20001, 0x25307ef8},
    {"epr-11292.23", 0x10000, 0x40000, 0xc29ac34e},
    {"epr-11296.24", 0x10000, 0x40001, 0x04a437f8},
    {"epr-11293.29", 0x10000, 0x60000, 0x41f41063},
    {"epr-11297.30", 0x10000, 0x60001, 0xb6e1fd72},
};

static const arc_rom kAlexkiddMain[] = {
    {"epr-10447.43", 0x10000, 0x00000, 0x29e87f71},
    {"epr-10445.26", 0x10000, 0x00001, 0x25ce5b6f},
    {"epr-10448.42", 0x10000, 0x20000, 0x05baedb5},
    {"epr-10446.25", 0x10000, 0x20001, 0xcd61d23c},
};
static const arc_rom kAlexkiddSound[] = {{"epr-10434.12", 0x8000, 0, 0x77141cce}};
static const arc_rom kAlexkiddTiles[] = {
    {"epr-10431.95", 0x8000, 0x0000, 0xa7962c39},
    {"epr-10432.94", 0x8000, 0x8000, 0xdb8cd24e},
    {"epr-10433.93", 0x8000, 0x10000, 0xe163c8c2},
};
static const arc_rom kAlexkiddSprites[] = {
    {"epr-10437.10", 0x8000, 0x00000, 0x522f7618},
    {"epr-10441.11", 0x8000, 0x00001, 0x74e3a35c},
    {"epr-10438.17", 0x8000, 0x10000, 0x738a6362},
    {"epr-10442.18", 0x8000, 0x10001, 0x86cb9c14},
    {"epr-10439.23", 0x8000, 0x20000, 0xb391aca7},
    {"epr-10443.24", 0x8000, 0x20001, 0x95d32635},
    {"epr-10440.29", 0x8000, 0x30000, 0x23939508},
    {"epr-10444.30", 0x8000, 0x30001, 0x82115823},
};
static const arc_rom kAlexkiddN7751[] = {
    {"epr-10435.1", 0x8000, 0x0000, 0xad89f6e3},
    {"epr-10436.2", 0x8000, 0x8000, 0x96c76613},
};

static const arc_rom kAliensynMain[] = {
    {"epr-10804.43", 0x8000, 0x00000, 0x23f78b83},
    {"epr-10802.26", 0x8000, 0x00001, 0x996768bd},
    {"epr-10805.42", 0x8000, 0x10000, 0x53d7fe50},
    {"epr-10803.25", 0x8000, 0x10001, 0x0536dd33},
    {"epr-10732.41", 0x8000, 0x20000, 0xc5712bfc},
    {"epr-10729.24", 0x8000, 0x20001, 0x3e520e30},
};
static const arc_rom kAliensynKey[] = {{"317-0037.key|317-0033.key", 0x2000, 0, 0x68bb7745}};
static const arc_rom kAliensynSound[] = {{"epr-10705.12|epr-10705.b1", 0x8000, 0, 0x777b749e}};
static const arc_rom kAliensynTiles[] = {
    {"epr-10739.95|epr-10739.c9", 0x10000, 0x00000, 0xa29ec207},
    {"epr-10740.94|epr-10740.c10", 0x10000, 0x10000, 0x47f93015},
    {"epr-10741.93|epr-10741.c11", 0x10000, 0x20000, 0x4970739c},
};
static const arc_rom kAliensynSprites[] = {
    {"epr-10709.10|epr-10709.b1", 0x10000, 0x00000, 0xaddf0a90},
    {"epr-10713.11|epr-10713.b5", 0x10000, 0x00001, 0xececde3a},
    {"epr-10710.17|epr-10710.b2", 0x10000, 0x20000, 0x992369eb},
    {"epr-10714.18|epr-10714.b6", 0x10000, 0x20001, 0x91bf42fb},
    {"epr-10711.23|epr-10711.b3", 0x10000, 0x40000, 0x29166ef6},
    {"epr-10715.24|epr-10715.b7", 0x10000, 0x40001, 0xa7c57384},
    {"epr-10712.29|epr-10712.b4", 0x10000, 0x60000, 0x876ad019},
    {"epr-10716.30|epr-10716.b8", 0x10000, 0x60001, 0x40ba1d48},
};
static const arc_rom kAliensynN7751[] = {
    {"epr-10706.1|epr-10706.c1", 0x8000, 0x0000, 0xaa114acc},
    {"epr-10707.2|epr-10707.c2", 0x8000, 0x8000, 0x800c1d82},
    {"epr-10708.4|epr-10708.c3", 0x8000, 0x10000, 0x5921ef52},
};

static const arc_rom kWb3Main[] = {
    {"epr-12120.43", 0x10000, 0x00000, 0xcbd8c99b},
    {"epr-12118.26", 0x10000, 0x00001, 0xe9a3280c},
    {"epr-12121.42", 0x10000, 0x20000, 0x5e44c0a9},
    {"epr-12119.25", 0x10000, 0x20001, 0x01ed3ef9},
};
static const arc_rom kWb3Key[] = {{"317-0086.key", 0x2000, 0, 0x5b8e7076}};
static const arc_rom kWb3Sound[] = {{"epr-12089.12|epr-12089.bin", 0x8000, 0, 0x8321eb0b}};
static const arc_rom kWb3Tiles[] = {
    {"epr-12086.95|epr-12086.bin", 0x10000, 0x00000, 0x45b949df},
    {"epr-12087.94|epr-12087.bin", 0x10000, 0x10000, 0x6f0396b7},
    {"epr-12088.83|epr-12088.bin", 0x10000, 0x20000, 0xba8c0749},
};
static const arc_rom kWb3Sprites[] = {
    {"epr-12090.10|epr-12090.b1", 0x10000, 0x00000, 0xaeeecfca},
    {"epr-12094.11|epr-12094.b5", 0x10000, 0x00001, 0x615e4927},
    {"epr-12091.17|epr-12091.b2", 0x10000, 0x20000, 0x8409a243},
    {"epr-12095.18|epr-12095.b6", 0x10000, 0x20001, 0xe774ec2c},
    {"epr-12092.23|epr-12092.b3", 0x10000, 0x40000, 0x5c2f0d90},
    {"epr-12096.24|epr-12096.b7", 0x10000, 0x40001, 0x0cd59d6e},
    {"epr-12093.29|epr-12093.b4", 0x10000, 0x60000, 0x4891e7bb},
    {"epr-12097.30|epr-12097.b8", 0x10000, 0x60001, 0xe645902c},
};

static const arc_rom kN7751Mcu[] = {{"7751.bin", 0x400, 0, 0x6a9534fc}};
static const arc_rom kShinobiN7751[] = {{"epr-11268.1", 0x8000, 0, 0x6d7966da}};

static const arc_rom kTetrisMain[] = {
    // dsp-emulator decrypted names, plus MAME 0.221 tetrisd bootleg files (same CRCs).
    {"xepr12201.rom|bootleg_epr-12200.rom", 0x8000, 1, 0x343c0670},
    {"xepr12200.rom|bootleg_epr-12201.rom", 0x8000, 0, 0x0b694740},
};
static const arc_rom kTetrisEncrypted[] = {   // (as MAME's tetris: 12201 the high bytes)
    {"epr-12201.rom", 0x8000, 0, 0},
    {"epr-12200.rom", 0x8000, 1, 0},
};
static const arc_rom kTetrisKey[] = {{"317-0093.key", 0x2000, 0, 0xe0064442}};
static const arc_rom kTetrisSound[] = {{"epr-12205.rom", 0x8000, 0, 0x6695dc99}};
static const arc_rom kTetrisTiles[] = {
    {"epr-12202.rom", 0x10000, 0x00000, 0x2f7da741},
    {"epr-12203.rom", 0x10000, 0x10000, 0xa6e58ec5},
    {"epr-12204.rom", 0x10000, 0x20000, 0x0ae98e23},
};
static const arc_rom kTetrisSprites[] = {
    {"epr-12169.b1", 0x8000, 0, 0xdacc6165},
    {"epr-12170.b5", 0x8000, 1, 0x87354e42},
};

static const arc_rom kAltbeastMain[] = {
    {"epr-11907.a7", 0x20000, 0, 0x29e0c3ad},
    {"epr-11906.a5", 0x20000, 1, 0x4c9e9cd8},
};
static const arc_rom kAltbeastSound[] = {
    {"epr-11671.a10", 0x8000, 0x0000, 0x2b71343b},
    {"opr-11672.a11", 0x20000, 0x8000, 0xbbd7f460},
    {"opr-11673.a12", 0x20000, 0x28000, 0x400c4a36},
};
static const arc_rom kAltbeastMcu[] = {{"317-0078.c2", 0x1000, 0, 0x8101925f}};
static const arc_rom kAltbeastTiles[] = {
    {"opr-11674.a14", 0x20000, 0x00000, 0xa57a66d5},
    {"opr-11675.a15", 0x20000, 0x20000, 0x2ef2f144},
    {"opr-11676.a16", 0x20000, 0x40000, 0x0c04acac},
};
static const arc_rom kAltbeastSprites[] = {
    {"epr-11677.b1", 0x20000, 1, 0xa01425cd},
    {"epr-11681.b5", 0x20000, 0, 0xd9e03363},
    {"epr-11678.b2", 0x20000, 0x40001, 0x17a9fc53},
    {"epr-11682.b6", 0x20000, 0x40000, 0xe3f77c5e},
    {"epr-11679.b3", 0x20000, 0x80001, 0x14dcc245},
    {"epr-11683.b7", 0x20000, 0x80000, 0xf9a60f06},
    {"epr-11680.b4", 0x20000, 0xc0001, 0xf43dcdec},
    {"epr-11684.b8", 0x20000, 0xc0000, 0xb20c0edb},
};



// Golden Axe (MCU, matches Pascal dsp-emulator)
static const arc_rom kGoldnaxeMain[] = {
    {"epr-12545.ic2", 0x40000, 0x00000, 0xa97c4e4d},
    {"epr-12544.ic1", 0x40000, 0x00001, 0x5e38f668},
};
static const arc_rom kGoldnaxeSound[] = {
    {"epr-12390.ic8", 0x8000, 0x0000, 0x399fc5f5},
    {"mpr-12384.ic6", 0x20000, 0x8000, 0x6218d8e7},
};
static const arc_rom kGoldnaxeMcu[] = {
    {"317-0123a.c2", 0x1000, 0x0000, 0xcf19e7d4},
};
static const arc_rom kGoldnaxeTiles[] = {
    {"epr-12385.ic19", 0x20000, 0x00000, 0xb8a4e7e0},
    {"epr-12386.ic20", 0x20000, 0x20000, 0x25d7d779},
    {"epr-12387.ic21", 0x20000, 0x40000, 0xc7fcadf3},
};
static const arc_rom kGoldnaxeSprites[] = {
    {"mpr-12378.ic9", 0x40000, 0x00001, 0x119e5a82},
    {"mpr-12379.ic12", 0x40000, 0x00000, 0x1a0e8c57},
    {"mpr-12380.ic10", 0x40000, 0x80001, 0xbb2c0853},
    {"mpr-12381.ic13", 0x40000, 0x80000, 0x81ba6ecc},
    {"mpr-12382.ic11", 0x40000, 0x100001, 0x81601c6f},
    {"mpr-12383.ic14", 0x40000, 0x100000, 0x5dbacf7a},
};

// Dynamite Dux (FD1094 MAME set)
static const arc_rom kDduxMain[] = {
    {"epr-11191.a7", 0x20000, 0x00000, 0x500e400a},
    {"epr-11190.a5", 0x20000, 0x00001, 0x2a698308},
    {"epr-11915.a8", 0x20000, 0x40000, 0xd8ed3132},
    {"epr-11913.a6", 0x20000, 0x40001, 0x30c6cb92},
};
static const arc_rom kDduxKey[] = {
    {"317-0096.key", 0x2000, 0x0000, 0x6fd7d26e},
};
static const arc_rom kDduxSound[] = {
    {"epr-11916.a10", 0x8000, 0x0000, 0x7ab541cf},
};
static const arc_rom kDduxTiles[] = {
    {"mpr-11917.a14", 0x10000, 0x00000, 0x6f772190},
    {"mpr-11918.a15", 0x10000, 0x10000, 0xc731db95},
    {"mpr-11919.a16", 0x10000, 0x20000, 0x64d5a491},
};
static const arc_rom kDduxSprites[] = {
    {"mpr-11920.b1", 0x20000, 0x00001, 0xe5d1e3cd},
    {"mpr-11922.b5", 0x20000, 0x00000, 0x70b0c4dd},
    {"mpr-11921.b2", 0x20000, 0x40001, 0x61d2358c},
    {"mpr-11923.b6", 0x20000, 0x40000, 0xc9ffe47d},
};

// E-Swat (FD1094)
static const arc_rom kEswatMain[] = {
    {"epr-12659.a2", 0x40000, 0x00000, 0xc5ab2db9},
    {"epr-12658.a1", 0x40000, 0x00001, 0xaf40bd71},
};
static const arc_rom kEswatKey[] = {
    {"317-0130.key", 0x2000, 0x0000, 0xba7b717b},
};
static const arc_rom kEswatSound[] = {
    {"epr-12617.a13", 0x8000, 0x0000, 0x7efecf23},
    {"mpr-12616.a11", 0x40000, 0x8000, 0x254347c2},
};
static const arc_rom kEswatTiles[] = {
    {"mpr-12624.b11", 0x40000, 0x00000, 0x375a5ec4},
    {"mpr-12625.b12", 0x40000, 0x40000, 0x3b8c757e},
    {"mpr-12626.b13", 0x40000, 0x80000, 0x3efca25c},
};
static const arc_rom kEswatSprites[] = {
    {"mpr-12618.b1", 0x40000, 0x00001, 0x0d1530bf},
    {"mpr-12621.b4", 0x40000, 0x00000, 0x18ff0799},
    {"mpr-12619.b2", 0x40000, 0x80001, 0x32069246},
    {"mpr-12622.b5", 0x40000, 0x80000, 0xa3dfe436},
    {"mpr-12620.b3", 0x40000, 0x100001, 0xf6b096e0},
    {"mpr-12623.b6", 0x40000, 0x100000, 0x6773fef6},
};

// Passing Shot (FD1094)
static const arc_rom kPassshtMain[] = {
    {"epr-11871.a4", 0x10000, 0x00000, 0x0f9ccea5},
    {"epr-11870.a1", 0x10000, 0x00001, 0xdf43ebcf},
};
static const arc_rom kPassshtKey[] = {
    {"317-0080.key", 0x2000, 0x0000, 0x222d016f},
};
static const arc_rom kPassshtSound[] = {
    {"epr-11857.a7", 0x8000, 0x00000, 0x789edc06},
    {"epr-11858.a8", 0x8000, 0x08000, 0x08ab0018},
    {"epr-11859.a9", 0x8000, 0x18000, 0x8673e01b},
    {"epr-11860.a10", 0x8000, 0x28000, 0x10263746},
    {"epr-11861.a11", 0x8000, 0x38000, 0x38b54a71},
};
static const arc_rom kPassshtTiles[] = {
    {"opr-11854.b9", 0x10000, 0x00000, 0xd31c0b6c},
    {"opr-11855.b10", 0x10000, 0x10000, 0xb78762b4},
    {"opr-11856.b11", 0x10000, 0x20000, 0xea49f666},
};
static const arc_rom kPassshtSprites[] = {
    {"opr-11862.b1", 0x10000, 0x00001, 0xb6e94727},
    {"opr-11865.b5", 0x10000, 0x00000, 0x17e8d5d5},
    {"opr-11863.b2", 0x10000, 0x20001, 0x3e670098},
    {"opr-11866.b6", 0x10000, 0x20000, 0x50eb71cc},
    {"opr-11864.b3", 0x10000, 0x40001, 0x05733ca8},
    {"opr-11867.b7", 0x10000, 0x40000, 0x81e49697},
};

// Aurail (unencrypted, matches Pascal)
static const arc_rom kAurailMain[] = {
    {"epr-13577.a7", 0x20000, 0x00000, 0x6701b686},
    {"epr-13576.a5", 0x20000, 0x00001, 0x1e428d94},
    {"epr-13447.a8", 0x20000, 0x40000, 0x70a52167},
    {"epr-13445.a6", 0x20000, 0x40001, 0x28dfc3dd},
};
static const arc_rom kAurailSound[] = {
    {"epr-13448.a10", 0x8000, 0x0000, 0xb5183fb9},
    {"mpr-13449.a11", 0x20000, 0x8000, 0xd3d9aaf9},
};
static const arc_rom kAurailTiles[] = {
    {"mpr-13450.a14", 0x20000, 0x00000, 0x0fc4a7a8},
    {"mpr-13465.b14", 0x20000, 0x20000, 0xe08135e0},
    {"mpr-13451.a15", 0x20000, 0x40000, 0x1c49852f},
    {"mpr-13466.b15", 0x20000, 0x60000, 0xe14c6684},
    {"mpr-13452.a16", 0x20000, 0x80000, 0x047bde5e},
    {"mpr-13467.b16", 0x20000, 0xa0000, 0x6309fec4},
};
static const arc_rom kAurailSprites[] = {
    {"mpr-13453.b1", 0x20000, 0x00001, 0x5fa0a9f8},
    {"mpr-13457.b5", 0x20000, 0x00000, 0x0d1b54da},
    {"mpr-13454.b2", 0x20000, 0x40001, 0x5f6b33b1},
    {"mpr-13458.b6", 0x20000, 0x40000, 0xbad340c3},
    {"mpr-13455.b3", 0x20000, 0x80001, 0x4e80520b},
    {"mpr-13459.b7", 0x20000, 0x80000, 0x7e9165ac},
    {"mpr-13456.b4", 0x20000, 0xc0001, 0x5733c428},
    {"mpr-13460.b8", 0x20000, 0xc0000, 0x66b8f9b3},
    {"mpr-13440.a1", 0x20000, 0x100001, 0x4f370b2b},
    {"mpr-13461.b10", 0x20000, 0x100000, 0xf76014bf},
    {"mpr-13441.a2", 0x20000, 0x140001, 0x37cf9cb4},
    {"mpr-13462.b11", 0x20000, 0x140000, 0x1061e7da},
    {"mpr-13442.a3", 0x20000, 0x180001, 0x049698ef},
    {"mpr-13463.b12", 0x20000, 0x180000, 0x7dbcfbf1},
    {"mpr-13443.a4", 0x20000, 0x1c0001, 0x77a8989e},
    {"mpr-13464.b13", 0x20000, 0x1c0000, 0x551df422},
};

// Riot City (sprites remapped by driver like Golden Axe)
static const arc_rom kRiotcityMain[] = {
    {"epr-14612.a7", 0x20000, 0x00000, 0xa1b331ec},
    {"epr-14610.a5", 0x20000, 0x00001, 0xcd4f2c50},
    {"epr-14613.a8", 0x20000, 0x40000, 0x0659df4c},
    {"epr-14611.a6", 0x20000, 0x40001, 0xd9e6f80b},
};
static const arc_rom kRiotcitySound[] = {
    {"epr-14614.a10", 0x10000, 0x0000, 0xc65cc69a},
    {"epr-14615.a11", 0x20000, 0x10000, 0x46653db1},
};
static const arc_rom kRiotcityTiles[] = {
    {"epr-14616.a14", 0x20000, 0x00000, 0x46d30368},
    {"epr-14625.b14", 0x20000, 0x20000, 0xabfb80fe},
    {"epr-14617.a15", 0x20000, 0x40000, 0x884e40f9},
    {"epr-14626.b15", 0x20000, 0x60000, 0x4ef55846},
    {"epr-14618.a16", 0x20000, 0x80000, 0x00eb260e},
    {"epr-14627.b16", 0x20000, 0xa0000, 0x961e5f82},
};
static const arc_rom kRiotcitySprites[] = {
    {"epr-14619.b1", 0x40000, 0x00001, 0x6f2b5ef7},
    {"epr-14622.b5", 0x40000, 0x00000, 0x7ca7e40d},
    {"epr-14620.b2", 0x40000, 0x80001, 0x66183333},
    {"epr-14623.b6", 0x40000, 0x80000, 0x98630049},
    {"epr-14621.b3", 0x40000, 0x100001, 0xc0f2820e},
    {"epr-14624.b7", 0x40000, 0x100000, 0xd1a68448},
};

// SDI (sdib Pascal set, sdib.zip names with MAME sdi alternates)
static const arc_rom kSdiMain[] = {
    {"epr-10986a.a4", 0x8000, 0x00000, 0x3e136215},
    {"epr-10984a.a1", 0x8000, 0x00001, 0x44bf3cf5},
    {"epr-10987a.a5", 0x8000, 0x10000, 0xcfd79404},
    {"epr-10985a.a2", 0x8000, 0x10001, 0x1c21a03f},
    {"epr-10829.a6", 0x8000, 0x20000, 0xa431ab08},
    {"epr-10826.a3", 0x8000, 0x20001, 0x2ed8e4b7},
};
static const arc_rom kSdiKey[] = {
    {"317-0028.key", 0x2000, 0x0000, 0x1514662f},
};
static const arc_rom kSdiSound[] = {
    {"epr-10775.a7|10775.a7", 0x8000, 0x0000, 0x4cbd55a8},
};
static const arc_rom kSdiTiles[] = {
    {"epr-10772.b9", 0x10000, 0x00000, 0x182b6301},
    {"epr-10773.b10", 0x10000, 0x10000, 0x8f7129a2},
    {"epr-10774.b11", 0x10000, 0x20000, 0x4409411f},
};
static const arc_rom kSdiSprites[] = {
    {"epr-10760.b1|10760.b1", 0x10000, 0x00001, 0x70de327b},
    {"epr-10763.b5|10763.b5", 0x10000, 0x00000, 0x99ec5cb5},
    {"epr-10761.b2|10761.b2", 0x10000, 0x20001, 0x4e80f80d},
    {"epr-10764.b6|10764.b6", 0x10000, 0x20000, 0x602da5d5},
    {"epr-10762.b3|10762.b3", 0x10000, 0x40001, 0x464b5f78},
    {"epr-10765.b7|10765.b7", 0x10000, 0x40000, 0x0a73a057},
};

// Cotton (FD1094 MAME main ROMs, Pascal gfx/sound)
static const arc_rom kCottonMain[] = {
    {"epr-13921a.a7", 0x20000, 0x00000, 0xf047a037},
    {"epr-13919a.a5", 0x20000, 0x00001, 0x651108b1},
    {"epr-13922a.a8", 0x20000, 0x40000, 0x1ca248c5},
    {"epr-13920a.a6", 0x20000, 0x40001, 0xfa3610f9},
};
static const arc_rom kCottonKey[] = {
    {"317-0181a.key", 0x2000, 0x0000, 0x5c419b36},
};
static const arc_rom kCottonSound[] = {
    {"epr-13892.a10", 0x8000, 0x0000, 0xfdfbe6ad},
    {"opr-13893.a11", 0x20000, 0x8000, 0x384233df},
};
static const arc_rom kCottonTiles[] = {
    {"opr-13862.a14", 0x20000, 0x00000, 0xa47354b6},
    {"opr-13877.b14", 0x20000, 0x20000, 0xd38424b5},
    {"opr-13863.a15", 0x20000, 0x40000, 0x8c990026},
    {"opr-13878.b15", 0x20000, 0x60000, 0x21c15b8a},
    {"opr-13864.a16", 0x20000, 0x80000, 0xd2b175bf},
    {"opr-13879.b16", 0x20000, 0xa0000, 0xb9d62531},
};
static const arc_rom kCottonSprites[] = {
    {"opr-13865.b1", 0x20000, 0x00001, 0x7024f404},
    {"opr-13869.b5", 0x20000, 0x00000, 0xab4b3468},
    {"opr-13866.b2", 0x20000, 0x40001, 0x6169bba4},
    {"opr-13870.b6", 0x20000, 0x40000, 0x69b41ac3},
    {"opr-13867.b3", 0x20000, 0x80001, 0xb014f02d},
    {"opr-13871.b7", 0x20000, 0x80000, 0x0801cf02},
    {"opr-13868.b4", 0x20000, 0xc0001, 0xe62a7cd6},
    {"opr-13872.b8", 0x20000, 0xc0000, 0xf066f315},
    {"opr-13852.a1", 0x20000, 0x100001, 0x943aba8b},
    {"opr-13873.b10", 0x20000, 0x100000, 0x1bd145f3},
    {"opr-13853.a2", 0x20000, 0x140001, 0x7ea93200},
    {"opr-13874.b11", 0x20000, 0x140000, 0x4fd59bff},
    {"opr-13891.a3", 0x20000, 0x180001, 0xc6b3c414},
    {"opr-13894.b12", 0x20000, 0x180000, 0xe3d0bee2},
    {"opr-13855.a4", 0x20000, 0x1c0001, 0x856f3ee2},
    {"opr-13876.b13", 0x20000, 0x1c0000, 0x1c5ffad8},
};

// Bay Route (FD1094)
static const arc_rom kBayrouteMain[] = {
    {"epr-12517.a7", 0x20000, 0x00000, 0x436728a9},
    {"epr-12516.a5", 0x20000, 0x00001, 0x4ff0353f},
    {"epr-12458.a8", 0x20000, 0x40000, 0xe7c7476a},
    {"epr-12456.a6", 0x20000, 0x40001, 0x25dc2eaf},
};
static const arc_rom kBayrouteKey[] = {
    {"317-0116.key", 0x2000, 0x0000, 0x8778ee49},
};
static const arc_rom kBayrouteSound[] = {
    {"epr-12459.a10", 0x8000, 0x0000, 0x3e1d29d0},
    {"mpr-12460.a11", 0x20000, 0x8000, 0x0bae570d},
    {"mpr-12461.a12", 0x20000, 0x28000, 0xb03b8b46},
};
static const arc_rom kBayrouteTiles[] = {
    {"opr-12462.a14", 0x10000, 0x00000, 0xa19943b5},
    {"opr-12463.a15", 0x10000, 0x10000, 0x62f8200d},
    {"opr-12464.a16", 0x10000, 0x20000, 0xc8c59703},
};
static const arc_rom kBayrouteSprites[] = {
    {"mpr-12465.b1", 0x20000, 0x00001, 0x11d61b45},
    {"mpr-12467.b5", 0x20000, 0x00000, 0xc3b4e4c0},
    {"mpr-12466.b2", 0x20000, 0x40001, 0xa57f236f},
    {"mpr-12468.b6", 0x20000, 0x40000, 0xd89c77de},
};

// Sonic Boom (FD1094)
static const arc_rom kSonicbomMain[] = {
    {"epr-11342.a4", 0x10000, 0x00000, 0x454693f1},
    {"epr-11340.a1", 0x10000, 0x00001, 0x03ba3fed},
    {"epr-11343.a5", 0x10000, 0x20000, 0xedfeb7d4},
    {"epr-11341.a2", 0x10000, 0x20001, 0x0338f771},
};
static const arc_rom kSonicbomKey[] = {
    {"317-0053.key", 0x2000, 0x0000, 0x91c80c88},
};
static const arc_rom kSonicbomSound[] = {
    {"epr-11347.a7", 0x8000, 0x0000, 0xb41f0ced},
    {"epr-11348.a8", 0x8000, 0x8000, 0x89924588},
    {"epr-11349.a9", 0x8000, 0x10000, 0x8e4b6204},
};
static const arc_rom kSonicbomTiles[] = {
    {"opr-11344.b9", 0x10000, 0x00000, 0x59a9f940},
    {"opr-11345.b10", 0x10000, 0x10000, 0xb44c068b},
    {"opr-11346.b11", 0x10000, 0x20000, 0xe5ada66c},
};
static const arc_rom kSonicbomSprites[] = {
    {"opr-11350.b1", 0x10000, 0x00001, 0x525ba1df},
    {"opr-11354.b5", 0x10000, 0x00000, 0x793fa3ac},
    {"opr-11351.b2", 0x10000, 0x20001, 0x63b1f1ca},
    {"opr-11355.b6", 0x10000, 0x20000, 0xfe0fa332},
    {"opr-11352.b3", 0x10000, 0x40001, 0x047fa4b0},
    {"opr-11356.b7", 0x10000, 0x40000, 0xaea3c39d},
    {"opr-11353.b4", 0x10000, 0x60001, 0x4e0791f8},
    {"opr-11357.b8", 0x10000, 0x60000, 0xa7c5ea41},
};

// Time Scanner (matches Pascal)
static const arc_rom kTimescanMain[] = {
    {"epr-10853.a4", 0x8000, 0x00000, 0x24d7c5fb},
    {"epr-10850.a1", 0x8000, 0x00001, 0xf1575732},
    {"epr-10854.a5", 0x8000, 0x10000, 0x82d0b237},
    {"epr-10851.a2", 0x8000, 0x10001, 0xf5ce271b},
    {"epr-10855.a6", 0x8000, 0x20000, 0x63e95a53},
    {"epr-10852.a3", 0x8000, 0x20001, 0x7cd1382b},
};
static const arc_rom kTimescanSound[] = {
    {"epr-10562.a7", 0x8000, 0x0000, 0x3f5028bf},
    {"epr-10563.a8", 0x8000, 0x10000, 0x9db7eddf},
};
static const arc_rom kTimescanTiles[] = {
    {"epr-10543.b9", 0x8000, 0x0000, 0x07dccc37},
    {"epr-10544.b10", 0x8000, 0x8000, 0x84fb9a3a},
    {"epr-10545.b11", 0x8000, 0x10000, 0xc8694bc0},
};
static const arc_rom kTimescanSprites[] = {
    {"epr-10548.b1", 0x8000, 0x00001, 0xaa150735},
    {"epr-10552.b5", 0x8000, 0x00000, 0x6fcbb9f7},
    {"epr-10549.b2", 0x8000, 0x20001, 0x2f59f067},
    {"epr-10553.b6", 0x8000, 0x20000, 0x8a220a9f},
    {"epr-10550.b3", 0x8000, 0x40001, 0xf05069ff},
    {"epr-10554.b7", 0x8000, 0x40000, 0xdc64f809},
    {"epr-10551.b4", 0x8000, 0x60001, 0x435d811f},
    {"epr-10555.b8", 0x8000, 0x60000, 0x2143c471},
};

