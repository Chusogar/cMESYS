// sega5881d.c - Sega 315-5881 stream decryption / decompression: C port of
// dsp-cpp's machine/sega_315_5881 (itself MAME's 315-5881_crypt.cpp,
// BSD-3-Clause, Andreas Naive, Olivier Galibert, David Haywood).
#include <string.h>
#include "sega5881d.h"

#define BUFFER_SIZE 2
#define FLAG_COMPRESSED 0x20000
#define FN1GK 38
#define FN2GK 32
#define BIT(x, n) ((int)(((x) >> (n)) & 1))

typedef struct {
    uint8_t table[64];
    int inputs[6];
    int outputs[2];
} sbox;

static const sbox fn1_sboxes[4][4] = {
	{   // 1st round
		{
			{
				0,3,2,2,1,3,1,2,3,2,1,2,1,2,3,1,3,2,2,0,2,1,3,0,0,3,2,3,2,1,2,0,
				2,3,1,1,2,2,1,1,1,0,2,3,3,0,2,1,1,1,1,1,3,0,3,2,1,0,1,2,0,3,1,3,
			},
			{3,4,5,7,-1,-1},
			{0,4}
		},

		{
			{
				2,2,2,0,3,3,0,1,2,2,3,2,3,0,2,2,1,1,0,3,3,2,0,2,0,1,0,1,2,3,1,1,
				0,1,3,3,1,3,3,1,2,3,2,0,0,0,2,2,0,3,1,3,0,3,2,2,0,3,0,3,1,1,0,2,
			},
			{0,1,2,5,6,7},
			{1,6}
		},

		{
			{
				0,1,3,0,3,1,1,1,1,2,3,1,3,0,2,3,3,2,0,2,1,1,2,1,1,3,1,0,0,2,0,1,
				1,3,1,0,0,3,2,3,2,0,3,3,0,0,0,0,1,2,3,3,2,0,3,2,1,0,0,0,2,2,3,3,
			},
			{0,2,5,6,7,-1},
			{2,3}
		},

		{
			{
				3,2,1,2,1,2,3,2,0,3,2,2,3,1,3,3,0,2,3,0,3,3,2,1,1,1,2,0,2,2,0,1,
				1,3,3,0,0,3,0,3,0,2,1,3,2,1,0,0,0,1,1,2,0,1,0,0,0,1,3,3,2,0,3,3,
			},
			{1,2,3,4,6,7},
			{5,7}
		},
	},
	{   // 2nd round
		{
			{
				3,3,1,2,0,0,2,2,2,1,2,1,3,1,1,3,3,0,0,3,0,3,3,2,1,1,3,2,3,2,1,3,
				2,3,0,1,3,2,0,1,2,1,3,1,2,2,3,3,3,1,2,2,0,3,1,2,2,1,3,0,3,0,1,3,
			},
			{0,1,3,4,5,7},
			{0,4}
		},

		{
			{
				2,0,1,0,0,3,2,0,3,3,1,2,1,3,0,2,0,2,0,0,0,2,3,1,3,1,1,2,3,0,3,0,
				3,0,2,0,0,2,2,1,0,2,3,3,1,3,1,0,1,3,3,0,0,1,3,1,0,2,0,3,2,1,0,1,
			},
			{0,1,3,4,6,-1},
			{1,5}
		},

		{
			{
				2,2,2,3,1,1,0,1,3,3,1,1,2,2,2,0,0,3,2,3,3,0,2,1,2,2,3,0,1,3,0,0,
				3,2,0,3,2,0,1,0,0,1,2,2,3,3,0,2,2,1,3,1,1,1,1,2,0,3,1,0,0,2,3,2,
			},
			{1,2,5,6,7,6},
			{2,7}
		},

		{
			{
				0,1,3,3,3,1,3,3,1,0,2,0,2,0,0,3,1,2,1,3,1,2,3,2,2,0,1,3,0,3,3,3,
				0,0,0,2,1,1,2,3,2,2,3,1,1,2,0,2,0,2,1,3,1,1,3,3,1,1,3,0,2,3,0,0,
			},
			{2,3,4,5,6,7},
			{3,6}
		},
	},
	{   // 3rd round
		{
			{
				0,0,1,0,1,0,0,3,2,0,0,3,0,1,0,2,0,3,0,0,2,0,3,2,2,1,3,2,2,1,1,2,
				0,0,0,3,0,1,1,0,0,2,1,0,3,1,2,2,2,0,3,1,3,0,1,2,2,1,1,1,0,2,3,1,
			},
			{1,2,3,4,5,7},
			{0,5}
		},

		{
			{
				1,2,1,0,3,1,1,2,0,0,2,3,2,3,1,3,2,0,3,2,2,3,1,1,1,1,0,3,2,0,0,1,
				1,0,0,1,3,1,2,3,0,0,2,3,3,0,1,0,0,2,3,0,1,2,0,1,3,3,3,1,2,0,2,1,
			},
			{0,2,4,5,6,7},
			{1,6}
		},

		{
			{
				0,3,0,2,1,2,0,0,1,1,0,0,3,1,1,0,0,3,0,0,2,3,3,2,3,1,2,0,0,2,3,0,
				// unused?
				255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
			},
			{0,2,4,6,7,-1},
			{2,3}
		},

		{
			{
				0,0,1,0,0,1,0,2,3,3,0,3,3,2,3,0,2,2,2,0,3,2,0,3,1,0,0,3,3,0,0,0,
				2,2,1,0,2,0,3,2,0,0,3,1,3,3,0,0,2,1,1,2,1,0,1,1,0,3,1,2,0,2,0,3,
			},
			{0,1,2,3,6,-1},
			{4,7}
		},
	},
	{   // 4th round
		{
			{
				0,3,3,3,3,3,2,0,0,1,2,0,2,2,2,2,1,1,0,2,2,1,3,2,3,2,0,1,2,3,2,1,
				3,2,2,3,1,0,1,0,0,2,0,1,2,1,2,3,1,2,1,1,2,2,1,0,1,3,2,3,2,0,3,1,
			},
			{0,1,3,4,5,6},
			{0,5}
		},

		{
			{
				0,3,0,0,2,0,3,1,1,1,2,2,2,1,3,1,2,2,1,3,2,2,3,3,0,3,1,0,3,2,0,1,
				3,0,2,0,1,0,2,1,3,3,1,2,2,0,2,3,3,2,3,0,1,1,3,3,0,2,1,3,0,2,2,3,
			},
			{0,1,2,3,5,7},
			{1,7}
		},

		{
			{
				0,1,2,3,3,3,3,1,2,0,2,3,2,1,0,1,2,2,1,2,0,3,2,0,1,1,0,1,3,1,3,1,
				3,1,0,0,1,0,0,0,0,1,2,2,1,1,3,3,1,2,3,3,3,2,3,0,2,2,1,3,3,0,2,0,
			},
			{2,3,4,5,6,7},
			{2,3}
		},

		{
			{
				0,2,1,1,3,2,0,3,1,0,1,0,3,2,1,1,2,2,0,3,1,0,1,2,2,2,3,3,0,0,0,0,
				1,2,1,0,2,1,2,2,2,3,2,3,0,1,3,0,0,1,3,0,0,1,1,0,1,0,0,0,0,2,0,1,
			},
			{0,1,2,4,6,7},
			{4,6}
		},
	},
};


static const sbox fn2_sboxes[4][4] = {
	{   // 1st round
		{
			{
				3,3,0,1,0,1,0,0,0,3,0,0,1,3,1,2,0,3,3,3,2,1,0,1,1,1,2,2,2,3,2,2,
				2,1,3,3,1,3,1,1,0,0,1,2,0,2,2,1,1,2,3,1,2,1,3,1,2,2,0,1,3,0,2,2,
			},
			{1,3,4,5,6,7},
			{0,7}
		},

		{
			{
				0,1,3,0,1,1,2,3,2,0,0,3,2,1,3,1,3,3,0,0,1,0,0,3,0,3,3,2,3,2,0,1,
				3,2,3,2,2,1,3,1,1,1,0,3,3,2,2,1,1,2,0,2,0,1,1,0,1,0,1,1,2,0,3,0,
			},
			{0,3,5,6,5,0},
			{1,2}
		},

		{
			{
				0,2,2,1,0,1,2,1,2,0,1,2,3,3,0,1,3,1,1,2,1,2,1,3,3,2,3,3,2,1,0,1,
				0,1,0,2,0,1,1,3,2,0,3,2,1,1,1,3,2,3,0,2,3,0,2,2,1,3,0,1,1,2,2,2,
			},
			{0,2,3,4,7,-1},
			{3,4}
		},

		{
			{
				2,3,1,3,2,0,1,2,0,0,3,3,3,3,3,1,2,0,2,1,2,3,0,2,0,1,0,3,0,2,1,0,
				2,3,0,1,3,0,3,2,3,1,2,0,3,1,1,2,0,3,0,0,2,0,2,1,2,2,3,2,1,2,3,1,
			},
			{1,2,5,6,-1,-1},
			{5,6}
		},
	},
	{   // 2nd round
		{
			{
				2,3,1,3,1,0,3,3,3,2,3,3,2,0,0,3,2,3,0,3,1,1,2,3,1,1,2,2,0,1,0,0,
				2,1,0,1,2,0,1,2,0,3,1,1,2,3,1,2,0,2,0,1,3,0,1,0,2,2,3,0,3,2,3,0,
			},
			{0,1,4,5,6,7},
			{0,7}
		},

		{
			{
				0,2,2,0,2,2,0,3,2,3,2,1,3,2,3,3,1,1,0,0,3,0,2,1,1,3,3,2,3,2,0,1,
				1,2,3,0,1,0,3,0,3,1,0,2,1,2,0,3,2,3,1,2,2,0,3,2,3,0,0,1,2,3,3,3,
			},
			{0,2,3,6,7,-1},
			{1,5}
		},

		{
			{
				1,0,3,0,0,1,2,1,0,0,1,0,0,0,2,3,2,2,0,2,0,1,3,0,2,0,1,3,2,3,0,1,
				1,2,2,2,1,3,0,3,0,1,1,0,3,2,3,3,2,0,0,3,1,2,1,3,3,2,1,0,2,1,2,3,
			},
			{2,3,4,6,7,2},
			{2,3}
		},

		{
			{
				2,3,1,3,1,1,2,3,3,1,1,0,1,0,2,3,2,1,0,0,2,2,0,1,0,2,2,2,0,2,1,0,
				3,1,2,3,1,3,0,2,1,0,1,0,0,1,2,2,3,2,3,1,3,2,1,1,2,0,2,1,3,3,1,0,
			},
			{1,2,3,4,5,6},
			{4,6}
		},
	},
	{   // 3rd round
		{
			{
				0,3,0,1,3,0,0,2,1,0,1,3,2,2,2,0,3,3,3,0,2,2,0,3,0,0,2,3,0,3,2,1,
				3,3,0,3,0,2,3,3,1,1,1,0,2,2,1,1,3,0,3,1,2,0,2,0,0,0,3,2,1,1,0,0,
			},
			{1,4,5,6,7,5},
			{0,5}
		},

		{
			{
				0,3,0,1,3,0,3,1,3,2,2,2,3,0,3,2,2,1,2,2,0,3,2,2,0,0,2,1,1,3,2,3,
				2,3,3,1,2,0,1,2,2,1,0,0,0,0,2,3,1,2,0,3,1,3,1,2,3,2,1,0,3,0,0,2,
			},
			{0,2,3,4,6,7},
			{1,7}
		},

		{
			{
				2,2,0,3,0,3,1,0,1,1,2,3,2,3,1,0,0,0,3,2,2,0,2,3,1,3,2,0,3,3,1,3,
				// unused?
				255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
			},
			{1,2,4,7,2,-1},
			{2,4}
		},

		{
			{
				0,2,3,1,3,1,1,0,0,1,3,0,2,1,3,3,2,0,2,1,1,2,3,3,0,0,0,2,0,2,3,0,
				3,3,3,3,2,3,3,2,3,0,1,0,2,3,3,2,0,1,3,1,0,1,2,3,3,0,2,0,3,0,3,3,
			},
			{0,1,2,3,5,7},
			{3,6}
		},
	},
	{   // 4th round
		{
			{
				0,1,1,0,0,1,0,2,3,3,0,1,2,3,0,2,1,0,3,3,2,0,3,0,0,2,1,0,1,0,1,3,
				0,3,3,1,2,0,3,0,1,3,2,0,3,3,1,3,0,2,3,3,2,1,1,2,2,1,2,1,2,0,1,1,
			},
			{0,1,2,4,7,-1},
			{0,5}
		},

		{
			{
				2,0,0,2,3,0,2,3,3,1,1,1,2,1,1,0,0,2,1,0,0,3,1,0,0,3,3,0,1,0,1,2,
				0,2,0,2,0,1,2,3,2,1,1,0,3,3,3,3,3,3,1,0,3,0,0,2,0,3,2,0,2,2,0,1,
			},
			{0,1,3,5,6,-1},
			{1,3}
		},

		{
			{
				0,1,1,2,1,3,1,1,0,0,3,1,1,1,2,0,3,2,0,1,1,2,3,3,3,0,3,0,0,2,0,3,
				3,2,0,0,3,2,3,1,2,3,0,3,2,0,1,2,2,2,0,2,0,1,2,2,3,1,2,2,1,1,1,1,
			},
			{0,2,3,4,5,7},
			{2,7}
		},

		{
			{
				0,1,2,0,3,3,0,3,2,1,3,3,0,3,1,1,3,2,3,2,3,0,0,0,3,0,2,2,3,2,2,3,
				2,2,3,1,2,3,1,2,0,3,0,2,3,1,0,0,3,2,1,2,1,2,1,3,1,0,2,3,3,1,3,2,
			},
			{2,3,4,5,6,7},
			{4,6}
		},
	},
};

static const int fn1_game_key_scheduling[FN1GK][2] = {
	{1,29},  {1,71},  {2,4},   {2,54},  {3,8},   {4,56},  {4,73},  {5,11},
	{6,51},  {7,92},  {8,89},  {9,9},   {9,39},  {9,58},  {10,90}, {11,6},
	{12,64}, {13,49}, {14,44}, {15,40}, {16,69}, {17,15}, {18,23}, {18,43},
	{19,82}, {20,81}, {21,32}, {22,5},  {23,66}, {24,13}, {24,45}, {25,12},
	{25,35}, {26,61}, {27,10}, {27,59}, {28,25}, {29,86}
};

static const int fn2_game_key_scheduling[FN2GK][2] = {
	{0,0},   {1,3},   {2,11},  {3,20},  {4,22},  {5,23},  {6,29},  {7,38},
	{8,39},  {9,55},  {9,86},  {9,87},  {10,50}, {11,57}, {12,59}, {13,61},
	{14,63}, {15,67}, {16,72}, {17,83}, {18,88}, {19,94}, {20,35}, {21,17},
	{22,6},  {23,85}, {24,16}, {25,25}, {26,92}, {27,47}, {28,28}, {29,90}
};

static const int fn1_sequence_key_scheduling[20][2] = {
	{0,52},  {1,34},  {2,17},  {3,36}, {4,84},  {4,88},  {5,57},  {6,48},
	{6,68},  {7,76},  {8,83},  {9,30}, {10,22}, {10,41}, {11,38}, {12,55},
	{13,74}, {14,19}, {14,80}, {15,26}
};

static const int fn2_sequence_key_scheduling[16] = {77,34,8,42,36,27,69,66,13,9,79,31,49,7,24,64};

static const int fn2_middle_result_scheduling[16] = {1,10,44,68,74,78,81,95,2,4,30,40,41,51,53,58};
static const uint8_t trees[9][2][32] = {
	{
		{0x01,0x10,0x0f,0x05,0xc4,0x13,0x87,0x0a,0xcc,0x81,0xce,0x0c,0x86,0x0e,0x84,0xc2,
			0x11,0xc1,0xc3,0xcf,0x15,0xc8,0xcd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc7,0x02,0x03,0x04,0x80,0x06,0x07,0x08,0x09,0xc9,0x0b,0x0d,0x82,0x83,0x85,0xc0,
			0x12,0xc6,0xc5,0x14,0x16,0xca,0xcb,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x02,0x80,0x05,0x04,0x81,0x10,0x15,0x82,0x09,0x83,0x0b,0x0c,0x0d,0xdc,0x0f,0xde,
			0x1c,0xcf,0xc5,0xdd,0x86,0x16,0x87,0x18,0x19,0x1a,0xda,0xca,0xc9,0x1e,0xce,0xff,},
		{0x01,0x17,0x03,0x0a,0x08,0x06,0x07,0xc2,0xd9,0xc4,0xd8,0xc8,0x0e,0x84,0xcb,0x85,
			0x11,0x12,0x13,0x14,0xcd,0x1b,0xdb,0xc7,0xc0,0xc1,0x1d,0xdf,0xc3,0xc6,0xcc,0xff,},
	},
	{
		{0xc6,0x80,0x03,0x0b,0x05,0x07,0x82,0x08,0x15,0xdc,0xdd,0x0c,0xd9,0xc2,0x14,0x10,
			0x85,0x86,0x18,0x16,0xc5,0xc4,0xc8,0xc9,0xc0,0xcc,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0x01,0x02,0x12,0x04,0x81,0x06,0x83,0xc3,0x09,0x0a,0x84,0x11,0x0d,0x0e,0x0f,0x19,
			0xca,0xc1,0x13,0xd8,0xda,0xdb,0x17,0xde,0xcd,0xcb,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x01,0x80,0x0d,0x04,0x05,0x15,0x83,0x08,0xd9,0x10,0x0b,0x0c,0x84,0x0e,0xc0,0x14,
			0x12,0xcb,0x13,0xca,0xc8,0xc2,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc5,0x02,0x03,0x07,0x81,0x06,0x82,0xcc,0x09,0x0a,0xc9,0x11,0xc4,0x0f,0x85,0xd8,
			0xda,0xdb,0xc3,0xdc,0xdd,0xc1,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x01,0x80,0x06,0x0c,0x05,0x81,0xd8,0x84,0x09,0xdc,0x0b,0x0f,0x0d,0x0e,0x10,0xdb,
			0x11,0xca,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc4,0x02,0x03,0x04,0xcb,0x0a,0x07,0x08,0xd9,0x82,0xc8,0x83,0xc0,0xc1,0xda,0xc2,
			0xc9,0xc3,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x01,0x02,0x06,0x0a,0x83,0x0b,0x07,0x08,0x09,0x82,0xd8,0x0c,0xd9,0xda,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc3,0x80,0x03,0x04,0x05,0x81,0xca,0xc8,0xdb,0xc9,0xc0,0xc1,0x0d,0xc2,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x01,0x02,0x03,0x04,0x81,0x07,0x08,0xd8,0xda,0xd9,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc2,0x80,0x05,0xc9,0xc8,0x06,0x82,0xc0,0x09,0xc1,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x01,0x80,0x04,0xc8,0xc0,0xd9,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc1,0x02,0x03,0x81,0x05,0xd8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
	{
		{0x01,0xd8,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
		{0xc0,0x80,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,},
	},
};

static int bitswap16(int val, const int *b)
{
    int r = 0;
    for (int i = 0; i < 16; i++) r = (r << 1) | ((val >> b[i]) & 1);
    return r;
}

static int feistel_function(int input, const sbox *sboxes, uint32_t subkeys)
{
    int result = 0;
    for (int m = 0; m < 4; ++m) {
        int aux = 0;
        for (int k = 0; k < 6; ++k)
            if (sboxes[m].inputs[k] != -1) aux |= BIT(input, sboxes[m].inputs[k]) << k;
        aux = sboxes[m].table[(aux ^ subkeys) & 0x3f];
        for (int k = 0; k < 2; ++k) result |= BIT(aux, k) << sboxes[m].outputs[k];
        subkeys >>= 6;
    }
    return result;
}

static uint16_t block_decrypt(uint32_t game_key, uint16_t sequence_key, uint16_t counter, uint16_t data)
{
    static const int sw1[16] = {5, 12, 14, 13, 9, 3, 6, 4, 8, 1, 15, 11, 0, 7, 10, 2};
    static const int sw2[16] = {14, 3, 8, 12, 13, 7, 15, 4, 6, 2, 9, 5, 11, 0, 1, 10};
    static const int sw3[16] = {15, 7, 6, 14, 13, 12, 5, 4, 3, 2, 11, 10, 9, 1, 0, 8};
    int j, aux, aux2, A, B, middle_result;
    uint32_t fn1_subkeys[4] = {0, 0, 0, 0}, fn2_subkeys[4] = {0, 0, 0, 0};
    for (j = 0; j < FN1GK; ++j)
        if (BIT(game_key, fn1_game_key_scheduling[j][0]) != 0) {
            aux = fn1_game_key_scheduling[j][1] % 24;
            aux2 = fn1_game_key_scheduling[j][1] / 24;
            fn1_subkeys[aux2] ^= (1u << aux);
        }
    for (j = 0; j < FN2GK; ++j)
        if (BIT(game_key, fn2_game_key_scheduling[j][0]) != 0) {
            aux = fn2_game_key_scheduling[j][1] % 24;
            aux2 = fn2_game_key_scheduling[j][1] / 24;
            fn2_subkeys[aux2] ^= (1u << aux);
        }
    for (j = 0; j < 20; ++j)
        if (BIT(sequence_key, fn1_sequence_key_scheduling[j][0]) != 0) {
            aux = fn1_sequence_key_scheduling[j][1] % 24;
            aux2 = fn1_sequence_key_scheduling[j][1] / 24;
            fn1_subkeys[aux2] ^= (1u << aux);
        }
    for (j = 0; j < 16; ++j)
        if (BIT(sequence_key, j) != 0) {
            aux = fn2_sequence_key_scheduling[j] % 24;
            aux2 = fn2_sequence_key_scheduling[j] / 24;
            fn2_subkeys[aux2] ^= (1u << aux);
        }
    aux = bitswap16(counter, sw1);
    B = aux >> 8;
    A = (aux & 0xff) ^ feistel_function(B, fn1_sboxes[0], fn1_subkeys[0]);
    B ^= feistel_function(A, fn1_sboxes[1], fn1_subkeys[1]);
    A ^= feistel_function(B, fn1_sboxes[2], fn1_subkeys[2]);
    B ^= feistel_function(A, fn1_sboxes[3], fn1_subkeys[3]);
    middle_result = (B << 8) | A;
    for (j = 0; j < 16; ++j)
        if (BIT(middle_result, j) != 0) {
            aux = fn2_middle_result_scheduling[j] % 24;
            aux2 = fn2_middle_result_scheduling[j] / 24;
            fn2_subkeys[aux2] ^= (1u << aux);
        }
    aux = bitswap16(data, sw2);
    B = aux >> 8;
    A = (aux & 0xff) ^ feistel_function(B, fn2_sboxes[0], fn2_subkeys[0]);
    B ^= feistel_function(A, fn2_sboxes[1], fn2_subkeys[1]);
    A ^= feistel_function(B, fn2_sboxes[2], fn2_subkeys[2]);
    B ^= feistel_function(A, fn2_sboxes[3], fn2_subkeys[3]);
    aux = (B << 8) | A;
    return (uint16_t)bitswap16(aux, sw3);
}

void s5881_init(s5881 *c, uint32_t key)
{
    memset(c, 0, sizeof *c);
    c->key = key;
    c->line_buffer = c->lb[0];
    c->line_buffer_prev = c->lb[1];
    s5881_reset(c);
}

void s5881_reset(s5881 *c)
{
    memset(c->buffer, 0, sizeof c->buffer);
    memset(c->line_buffer, 0, S5881_LINE_SIZE);
    memset(c->line_buffer_prev, 0, S5881_LINE_SIZE);
    c->prot_cur_address = 0;
    c->subkey = 0;
    c->dec_hist = 0;
    c->dec_header = 0;
    c->enc_ready = false;
    c->buffer_pos = 0;
    c->line_buffer_pos = 0;
    c->line_buffer_size = 0;
    c->buffer_bit = 0;
}

static uint16_t get_decrypted_16(s5881 *c)
{
    const uint16_t enc = c->read(c->user, c->prot_cur_address);
    const uint16_t dec = block_decrypt(c->key, c->subkey, (uint16_t)c->prot_cur_address, enc);
    const uint16_t res = (uint16_t)((dec & 3) | (c->dec_hist & 0xfffc));
    c->dec_hist = dec;
    c->prot_cur_address++;
    return res;
}

static void enc_start(s5881 *c)
{
    c->block_pos = 0;
    c->done_compression = 0;
    c->buffer_pos = BUFFER_SIZE;
    if (c->buffer_bit2 < 14) c->dec_header = (uint32_t)(c->buffer2a & 0x0003) << 16;
    else {
        c->dec_hist = 0;
        c->dec_header = (uint32_t)get_decrypted_16(c) << 16;
    }
    c->dec_header |= get_decrypted_16(c);
    c->block_numlines = (int)((c->dec_header & 0x000000ff) >> 0) + 1;
    const int blocky = (int)((c->dec_header & 0x0001ff00) >> 8) + 1;
    c->block_size = c->block_numlines * blocky;
    if (c->dec_header & FLAG_COMPRESSED) {
        c->line_buffer_size = blocky;
        c->line_buffer_pos = c->line_buffer_size;
        c->buffer_bit = 7;
        c->buffer_bit2 = 15;
    }
    c->enc_ready = true;
}

static void enc_fill(s5881 *c)
{
    for (int i = 0; i != BUFFER_SIZE; i += 2) {
        const uint16_t val = get_decrypted_16(c);
        c->buffer[i] = (uint8_t)val;
        c->buffer[i + 1] = (uint8_t)(val >> 8);
        c->block_pos += 2;
        if (!(c->dec_header & FLAG_COMPRESSED) && c->block_pos == c->block_size) enc_start(c);
    }
    c->buffer_pos = 0;
}

static int get_compressed_bit(s5881 *c)
{
    if (c->buffer_bit2 == 15) {
        c->buffer_bit2 = 0;
        c->buffer2a = get_decrypted_16(c);
        c->buffer2[0] = (uint8_t)c->buffer2a;
        c->buffer2[1] = (uint8_t)(c->buffer2a >> 8);
        c->buffer_pos = 0;
    } else c->buffer_bit2++;
    const int res = (c->buffer2[(c->buffer_pos & 1) ^ 1] >> c->buffer_bit) & 1;
    c->buffer_bit--;
    if (c->buffer_bit == -1) {
        c->buffer_bit = 7;
        c->buffer_pos++;
    }
    return res;
}

static void line_fill(s5881 *c)
{
    uint8_t *lp = c->line_buffer, *lc = c->line_buffer_prev;
    c->line_buffer = lc;
    c->line_buffer_prev = lp;
    c->line_buffer_pos = 0;
    for (int i = 0; i != c->line_buffer_size;) {
        const int slot = i ? i < c->line_buffer_size - 7 ? 1 : (i & 7) + 1 : 0;
        uint32_t tmp = 0;
        while (!(tmp & 0x80))
            tmp = get_compressed_bit(c) ? trees[slot][1][tmp] : trees[slot][0][tmp];
        if (tmp != 0xff) {
            const int count = (int)(tmp & 7) + 1;
            if (tmp & 0x40) {
                static const int offsets[4] = {0, 1, 0, -1};
                const int offset = offsets[(tmp & 0x18) >> 3];
                for (int j = 0; j != count; j++) {
                    int src = ((i + offset) % c->line_buffer_size) ^ 1;
                    if (src < 0) src = 0;           // (dsp-cpp reads before the buffer here)
                    lc[i ^ 1] = lp[src];
                    i++;
                }
            } else {
                uint8_t byte = 0;
                for (int k = 0; k < 8; k++) byte = (uint8_t)((byte << 1) | get_compressed_bit(c));
                for (int j = 0; j != count; j++) lc[(i++) ^ 1] = byte;
            }
        }
    }
    c->block_pos++;
    if (c->block_numlines == c->block_pos) c->done_compression = 1;
}

uint16_t s5881_decrypt(s5881 *c)
{
    const uint8_t *base;
    if (!c->enc_ready) enc_start(c);
    if (c->dec_header & FLAG_COMPRESSED) {
        if (c->line_buffer_pos == c->line_buffer_size) {
            if (c->done_compression == 1) enc_start(c);
            line_fill(c);
        }
        base = c->line_buffer + c->line_buffer_pos;
        c->line_buffer_pos += 2;
    } else {
        if (c->buffer_pos == BUFFER_SIZE) enc_fill(c);
        base = c->buffer + c->buffer_pos;
        c->buffer_pos += 2;
    }
    return (uint16_t)((base[0] << 8) | base[1]);
}

void s5881_set_addr_low(s5881 *c, uint16_t d) { c->prot_cur_address = (c->prot_cur_address & 0xffff0000u) | d; c->enc_ready = false; }
void s5881_set_addr_high(s5881 *c, uint16_t d)
{
    c->prot_cur_address = (c->prot_cur_address & 0x0000ffffu) | ((uint32_t)d << 16);
    c->enc_ready = false;
    c->buffer_bit = 7;
    c->buffer_bit2 = 15;
}
void s5881_set_subkey(s5881 *c, uint16_t d) { c->subkey = d; c->enc_ready = false; }
