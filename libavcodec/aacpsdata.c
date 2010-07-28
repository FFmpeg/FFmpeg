/*
 * MPEG-4 Parametric Stereo data tables
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

static const uint8_t huff_iid_df1_bits[] = {
    18, 18, 18, 18, 18, 18, 18, 18, 18, 17, 18, 17, 17, 16, 16, 15, 14, 14,
    13, 12, 12, 11, 10, 10,  8,  7,  6,  5,  4,  3,  1,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 11, 12, 13, 14, 14, 15, 16, 16, 17, 17, 18, 17, 18, 18,
    18, 18, 18, 18, 18, 18, 18,
};

static const uint32_t huff_iid_df1_codes[] = {
    0x01FEB4, 0x01FEB5, 0x01FD76, 0x01FD77, 0x01FD74, 0x01FD75, 0x01FE8A,
    0x01FE8B, 0x01FE88, 0x00FE80, 0x01FEB6, 0x00FE82, 0x00FEB8, 0x007F42,
    0x007FAE, 0x003FAF, 0x001FD1, 0x001FE9, 0x000FE9, 0x0007EA, 0x0007FB,
    0x0003FB, 0x0001FB, 0x0001FF, 0x00007C, 0x00003C, 0x00001C, 0x00000C,
    0x000000, 0x000001, 0x000001, 0x000002, 0x000001, 0x00000D, 0x00001D,
    0x00003D, 0x00007D, 0x0000FC, 0x0001FC, 0x0003FC, 0x0003F4, 0x0007EB,
    0x000FEA, 0x001FEA, 0x001FD6, 0x003FD0, 0x007FAF, 0x007F43, 0x00FEB9,
    0x00FE83, 0x01FEB7, 0x00FE81, 0x01FE89, 0x01FE8E, 0x01FE8F, 0x01FE8C,
    0x01FE8D, 0x01FEB2, 0x01FEB3, 0x01FEB0, 0x01FEB1,
};

static const uint8_t huff_iid_dt1_bits[] = {
    16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15, 15, 14, 14, 13,
    13, 13, 12, 12, 11, 10,  9,  9,  7,  6,  5,  3,  1,  2,  5,  6,  7,  8,
     9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16,
};

static const uint16_t huff_iid_dt1_codes[] = {
    0x004ED4, 0x004ED5, 0x004ECE, 0x004ECF, 0x004ECC, 0x004ED6, 0x004ED8,
    0x004F46, 0x004F60, 0x002718, 0x002719, 0x002764, 0x002765, 0x00276D,
    0x0027B1, 0x0013B7, 0x0013D6, 0x0009C7, 0x0009E9, 0x0009ED, 0x0004EE,
    0x0004F7, 0x000278, 0x000139, 0x00009A, 0x00009F, 0x000020, 0x000011,
    0x00000A, 0x000003, 0x000001, 0x000000, 0x00000B, 0x000012, 0x000021,
    0x00004C, 0x00009B, 0x00013A, 0x000279, 0x000270, 0x0004EF, 0x0004E2,
    0x0009EA, 0x0009D8, 0x0013D7, 0x0013D0, 0x0027B2, 0x0027A2, 0x00271A,
    0x00271B, 0x004F66, 0x004F67, 0x004F61, 0x004F47, 0x004ED9, 0x004ED7,
    0x004ECD, 0x004ED2, 0x004ED3, 0x004ED0, 0x004ED1,
};

static const uint8_t huff_iid_df0_bits[] = {
    17, 17, 17, 17, 16, 15, 13, 10,  9,  7,  6,  5,  4,  3,  1,  3,  4,  5,
     6,  6,  8, 11, 13, 14, 14, 15, 17, 18, 18,
};

static const uint32_t huff_iid_df0_codes[] = {
    0x01FFFB, 0x01FFFC, 0x01FFFD, 0x01FFFA, 0x00FFFC, 0x007FFC, 0x001FFD,
    0x0003FE, 0x0001FE, 0x00007E, 0x00003C, 0x00001D, 0x00000D, 0x000005,
    0x000000, 0x000004, 0x00000C, 0x00001C, 0x00003D, 0x00003E, 0x0000FE,
    0x0007FE, 0x001FFC, 0x003FFC, 0x003FFD, 0x007FFD, 0x01FFFE, 0x03FFFE,
    0x03FFFF,
};

static const uint8_t huff_iid_dt0_bits[] = {
    19, 19, 19, 20, 20, 20, 17, 15, 12, 10,  8,  6,  4,  2,  1,  3,  5,  7,
     9, 11, 13, 14, 17, 19, 20, 20, 20, 20, 20,
};

static const uint32_t huff_iid_dt0_codes[] = {
    0x07FFF9, 0x07FFFA, 0x07FFFB, 0x0FFFF8, 0x0FFFF9, 0x0FFFFA, 0x01FFFD,
    0x007FFE, 0x000FFE, 0x0003FE, 0x0000FE, 0x00003E, 0x00000E, 0x000002,
    0x000000, 0x000006, 0x00001E, 0x00007E, 0x0001FE, 0x0007FE, 0x001FFE,
    0x003FFE, 0x01FFFC, 0x07FFF8, 0x0FFFFB, 0x0FFFFC, 0x0FFFFD, 0x0FFFFE,
    0x0FFFFF,
};

static const uint8_t huff_icc_df_bits[] = {
    14, 14, 12, 10, 7, 5, 3, 1, 2, 4, 6, 8, 9, 11, 13,
};

static const uint16_t huff_icc_df_codes[] = {
    0x3FFF, 0x3FFE, 0x0FFE, 0x03FE, 0x007E, 0x001E, 0x0006, 0x0000,
    0x0002, 0x000E, 0x003E, 0x00FE, 0x01FE, 0x07FE, 0x1FFE,
};

static const uint8_t huff_icc_dt_bits[] = {
    14, 13, 11, 9, 7, 5, 3, 1, 2, 4, 6, 8, 10, 12, 14,
};

static const uint16_t huff_icc_dt_codes[] = {
    0x3FFE, 0x1FFE, 0x07FE, 0x01FE, 0x007E, 0x001E, 0x0006, 0x0000,
    0x0002, 0x000E, 0x003E, 0x00FE, 0x03FE, 0x0FFE, 0x3FFF,
};

static const uint8_t huff_ipd_df_bits[] = {
    1, 3, 4, 4, 4, 4, 4, 4,
};

static const uint8_t huff_ipd_df_codes[] = {
    0x01, 0x00, 0x06, 0x04, 0x02, 0x03, 0x05, 0x07,
};

static const uint8_t huff_ipd_dt_bits[] = {
    1, 3, 4, 5, 5, 4, 4, 3,
};

static const uint8_t huff_ipd_dt_codes[] = {
    0x01, 0x02, 0x02, 0x03, 0x02, 0x00, 0x03, 0x03,
};

static const uint8_t huff_opd_df_bits[] = {
    1, 3, 4, 4, 5, 5, 4, 3,
};

static const uint8_t huff_opd_df_codes[] = {
    0x01, 0x01, 0x06, 0x04, 0x0F, 0x0E, 0x05, 0x00,
};

static const uint8_t huff_opd_dt_bits[] = {
    1, 3, 4, 5, 5, 4, 4, 3,
};

static const uint8_t huff_opd_dt_codes[] = {
    0x01, 0x02, 0x01, 0x07, 0x06, 0x00, 0x02, 0x03,
};

static const int8_t huff_offset[] = {
    30, 30,
    14, 14,
    7, 7,
    0, 0,
    0, 0,
};

///Table 8.48
static const int8_t k_to_i_20[] = {
     1,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 14, 15,
    15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19
};
///Table 8.49
static const int8_t k_to_i_34[] = {
     0,  1,  2,  3,  4,  5,  6,  6,  7,  2,  1,  0, 10, 10,  4,  5,  6,  7,  8,
     9, 10, 11, 12,  9, 14, 11, 12, 13, 14, 15, 16, 13, 16, 17, 18, 19, 20, 21,
    22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 27, 28, 28, 28, 29, 29, 29,
    30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33
};

static const float g1_Q2[] = {
    0.0f,  0.01899487526049f, 0.0f, -0.07293139167538f,
    0.0f,  0.30596630545168f, 0.5f
};
