/*
 * RV 1.0 compatible encoder.
 * Copyright (c) 2000 Gerard Lantau.
 *
 * The licence of this code is contained in file LICENCE found in the
 * same archive 
 */

const unsigned char vlc_dc_table[256] = {
    0, 1, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,

    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,

    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

const unsigned char vlc_dc_lum_code[9] = {
    0x4, 0x0, 0x1, 0x5, 0x6, 0xe, 0x1e, 0x3e, 0x7e,
};
const unsigned char vlc_dc_lum_bits[9] = {
    3, 2, 2, 3, 3, 4, 5, 6, 7,
};

const unsigned char vlc_dc_chroma_code[9] = {
    0x0, 0x1, 0x2, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe,
};
const unsigned char vlc_dc_chroma_bits[9] = {
    2, 2, 2, 3, 4, 5, 6, 7, 8,
};

/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#define HUFF_MAXRUN	32
#define HUFF_MAXLEVEL	41

static const int huff_maxlevel[HUFF_MAXRUN] = { 41, 19, 6, 5, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };

static const UINT8 huff_table0[41] = { 0x0, 0x6, 0x8, 0xa, 0xc, 0x4c, 0x42, 0x14, 0x3a, 0x30, 0x26, 0x20, 0x34, 0x32, 0x30, 0x2e, 0x3e, 0x3c, 0x3a, 0x38, 0x36, 0x34, 0x32, 0x30, 0x2e, 0x2c, 0x2a, 0x28, 0x26, 0x24, 0x22, 0x20, 0x30, 0x2e, 0x2c, 0x2a, 0x28, 0x26, 0x24, 0x22, 0x20 };
static const UINT8 huff_bits0[41] = { 0, 3, 5, 6, 8, 9, 9, 11, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16 };

static const UINT8 huff_table1[19] = { 0x0, 0x6, 0xc, 0x4a, 0x18, 0x36, 0x2c, 0x2a, 0x3e, 0x3c, 0x3a, 0x38, 0x36, 0x34, 0x32, 0x26, 0x24, 0x22, 0x20 };
static const UINT8 huff_bits1[19] = { 0, 4, 7, 9, 11, 13, 14, 14, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17 };

static const UINT8 huff_table2[6] = { 0x0, 0xa, 0x8, 0x16, 0x28, 0x28 };
static const UINT8 huff_bits2[6] = { 0, 5, 8, 11, 13, 14 };

static const UINT8 huff_table3[5] = { 0x0, 0xe, 0x48, 0x38, 0x26 };
static const UINT8 huff_bits3[5] = { 0, 6, 9, 13, 14 };

static const UINT8 huff_table4[4] = { 0x0, 0xc, 0x1e, 0x24 };
static const UINT8 huff_bits4[4] = { 0, 6, 11, 13 };

static const UINT8 huff_table5[4] = { 0x0, 0xe, 0x12, 0x24 };
static const UINT8 huff_bits5[4] = { 0, 7, 11, 14 };

static const UINT8 huff_table6[4] = { 0x0, 0xa, 0x3c, 0x28 };
static const UINT8 huff_bits6[4] = { 0, 7, 13, 17 };

static const UINT8 huff_table7[3] = { 0x0, 0x8, 0x2a };
static const UINT8 huff_bits7[3] = { 0, 7, 13 };

static const UINT8 huff_table8[3] = { 0x0, 0xe, 0x22 };
static const UINT8 huff_bits8[3] = { 0, 8, 13 };

static const UINT8 huff_table9[3] = { 0x0, 0xa, 0x22 };
static const UINT8 huff_bits9[3] = { 0, 8, 14 };

static const UINT8 huff_table10[3] = { 0x0, 0x4e, 0x20 };
static const UINT8 huff_bits10[3] = { 0, 9, 14 };

static const UINT8 huff_table11[3] = { 0x0, 0x46, 0x34 };
static const UINT8 huff_bits11[3] = { 0, 9, 17 };

static const UINT8 huff_table12[3] = { 0x0, 0x44, 0x32 };
static const UINT8 huff_bits12[3] = { 0, 9, 17 };

static const UINT8 huff_table13[3] = { 0x0, 0x40, 0x30 };
static const UINT8 huff_bits13[3] = { 0, 9, 17 };

static const UINT8 huff_table14[3] = { 0x0, 0x1c, 0x2e };
static const UINT8 huff_bits14[3] = { 0, 11, 17 };

static const UINT8 huff_table15[3] = { 0x0, 0x1a, 0x2c };
static const UINT8 huff_bits15[3] = { 0, 11, 17 };

static const UINT8 huff_table16[3] = { 0x0, 0x10, 0x2a };
static const UINT8 huff_bits16[3] = { 0, 11, 17 };

static const UINT8 huff_table17[2] = { 0x0, 0x3e };
static const UINT8 huff_bits17[2] = { 0, 13 };

static const UINT8 huff_table18[2] = { 0x0, 0x34 };
static const UINT8 huff_bits18[2] = { 0, 13 };

static const UINT8 huff_table19[2] = { 0x0, 0x32 };
static const UINT8 huff_bits19[2] = { 0, 13 };

static const UINT8 huff_table20[2] = { 0x0, 0x2e };
static const UINT8 huff_bits20[2] = { 0, 13 };

static const UINT8 huff_table21[2] = { 0x0, 0x2c };
static const UINT8 huff_bits21[2] = { 0, 13 };

static const UINT8 huff_table22[2] = { 0x0, 0x3e };
static const UINT8 huff_bits22[2] = { 0, 14 };

static const UINT8 huff_table23[2] = { 0x0, 0x3c };
static const UINT8 huff_bits23[2] = { 0, 14 };

static const UINT8 huff_table24[2] = { 0x0, 0x3a };
static const UINT8 huff_bits24[2] = { 0, 14 };

static const UINT8 huff_table25[2] = { 0x0, 0x38 };
static const UINT8 huff_bits25[2] = { 0, 14 };

static const UINT8 huff_table26[2] = { 0x0, 0x36 };
static const UINT8 huff_bits26[2] = { 0, 14 };

static const UINT8 huff_table27[2] = { 0x0, 0x3e };
static const UINT8 huff_bits27[2] = { 0, 17 };

static const UINT8 huff_table28[2] = { 0x0, 0x3c };
static const UINT8 huff_bits28[2] = { 0, 17 };

static const UINT8 huff_table29[2] = { 0x0, 0x3a };
static const UINT8 huff_bits29[2] = { 0, 17 };

static const UINT8 huff_table30[2] = { 0x0, 0x38 };
static const UINT8 huff_bits30[2] = { 0, 17 };

static const UINT8 huff_table31[2] = { 0x0, 0x36 };
static const UINT8 huff_bits31[2] = { 0, 17 };

static const UINT8 *huff_table[32] = { huff_table0, huff_table1, huff_table2, huff_table3, huff_table4, huff_table5, huff_table6, huff_table7, huff_table8, huff_table9, huff_table10, huff_table11, huff_table12, huff_table13, huff_table14, huff_table15, huff_table16, huff_table17, huff_table18, huff_table19, huff_table20, huff_table21, huff_table22, huff_table23, huff_table24, huff_table25, huff_table26, huff_table27, huff_table28, huff_table29, huff_table30, huff_table31 };

static const UINT8 *huff_bits[32] = { huff_bits0, huff_bits1, huff_bits2, huff_bits3, huff_bits4, huff_bits5, huff_bits6, huff_bits7, huff_bits8, huff_bits9, huff_bits10, huff_bits11, huff_bits12, huff_bits13, huff_bits14, huff_bits15, huff_bits16, huff_bits17, huff_bits18, huff_bits19, huff_bits20, huff_bits21, huff_bits22, huff_bits23, huff_bits24, huff_bits25, huff_bits26, huff_bits27, huff_bits28, huff_bits29, huff_bits30, huff_bits31 };

static const UINT8 mbAddrIncrTable[][2] = {
    {0x0, 0},
    {0x1, 1},
    {0x3, 3},
    {0x2, 3},
    {0x3, 4},
    {0x2, 4},
    {0x3, 5},
    {0x2, 5},
    {0x7, 7},
    {0x6, 7},
    {0xb, 8},
    {0xa, 8},
    {0x9, 8},
    {0x8, 8},
    {0x7, 8},
    {0x6, 8},
    {0x17, 10},
    {0x16, 10},
    {0x15, 10},
    {0x14, 10},
    {0x13, 10},
    {0x12, 10},
    {0x23, 11},
    {0x22, 11},
    {0x21, 11},
    {0x20, 11},
    {0x1f, 11},
    {0x1e, 11},
    {0x1d, 11},
    {0x1c, 11},
    {0x1b, 11},
    {0x1a, 11},
    {0x19, 11},
    {0x18, 11}};

static const UINT8 mbPatTable[][2] = {
    {0x0, 0},
    {0xb, 5},
    {0x9, 5},
    {0xd, 6},
    {0xd, 4},
    {0x17, 7},
    {0x13, 7},
    {0x1f, 8},
    {0xc, 4},
    {0x16, 7},
    {0x12, 7},
    {0x1e, 8},
    {0x13, 5},
    {0x1b, 8},
    {0x17, 8},
    {0x13, 8},
    {0xb, 4},
    {0x15, 7},
    {0x11, 7},
    {0x1d, 8},
    {0x11, 5},
    {0x19, 8},
    {0x15, 8},
    {0x11, 8},
    {0xf, 6},
    {0xf, 8},
    {0xd, 8},
    {0x3, 9},
    {0xf, 5},
    {0xb, 8},
    {0x7, 8},
    {0x7, 9},
    {0xa, 4},
    {0x14, 7},
    {0x10, 7},
    {0x1c, 8},
    {0xe, 6},
    {0xe, 8},
    {0xc, 8},
    {0x2, 9},
    {0x10, 5},
    {0x18, 8},
    {0x14, 8},
    {0x10, 8},
    {0xe, 5},
    {0xa, 8},
    {0x6, 8},
    {0x6, 9},
    {0x12, 5},
    {0x1a, 8},
    {0x16, 8},
    {0x12, 8},
    {0xd, 5},
    {0x9, 8},
    {0x5, 8},
    {0x5, 9},
    {0xc, 5},
    {0x8, 8},
    {0x4, 8},
    {0x4, 9},
    {0x7, 3},
    {0xa, 5},	/* grrr... 61, 62, 63 added - Kevin */
    {0x8, 5},
    {0xc, 6}
};

const UINT8 zigzag_direct[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static unsigned char const default_intra_matrix[64] = {
	8, 16, 19, 22, 26, 27, 29, 34,
	16, 16, 22, 24, 27, 29, 34, 37,
	19, 22, 26, 27, 29, 34, 34, 38,
	22, 22, 26, 27, 29, 34, 37, 40,
	22, 26, 27, 29, 32, 35, 40, 48,
	26, 27, 29, 32, 35, 40, 48, 58,
	26, 27, 29, 34, 38, 46, 56, 69,
	27, 29, 35, 38, 46, 56, 69, 83
};

/* XXX: could hardcode this matrix */
static unsigned char const default_non_intra_matrix[64] = {
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
};

static unsigned char const frame_rate_tab[9] = {
    0, 24, 24, 25, 30, 30, 50, 60, 60,
};
