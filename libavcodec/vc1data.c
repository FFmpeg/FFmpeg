/*
 * VC-1 and WMV3 decoder
 * copyright (c) 2011 Mashiat Sarker Shakkhar
 * copyright (c) 2006 Konstantin Shishkov
 * (c) 2005 anonymous, Alex Beregszaszi, Michael Niedermayer
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

/**
 * @file
 * VC-1 tables.
 */

#include "vc1.h"
#include "vc1data.h"
#include "vlc.h"

/** Table for conversion between TTBLK and TTMB */
const int ff_vc1_ttblk_to_tt[3][8] = {
    { TT_8X4, TT_4X8, TT_8X8, TT_4X4, TT_8X4_TOP, TT_8X4_BOTTOM, TT_4X8_RIGHT, TT_4X8_LEFT },
    { TT_8X8, TT_4X8_RIGHT, TT_4X8_LEFT, TT_4X4, TT_8X4, TT_4X8, TT_8X4_BOTTOM, TT_8X4_TOP },
    { TT_8X8, TT_4X8, TT_4X4, TT_8X4_BOTTOM, TT_4X8_RIGHT, TT_4X8_LEFT, TT_8X4, TT_8X4_TOP }
};

const int ff_vc1_ttfrm_to_tt[4] = { TT_8X8, TT_8X4, TT_4X8, TT_4X4 };

/** MV P mode - the 5th element is only used for mode 1 */
const uint8_t ff_vc1_mv_pmode_table[2][5] = {
    { MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_1MV, MV_PMODE_1MV_HPEL, MV_PMODE_INTENSITY_COMP, MV_PMODE_MIXED_MV },
    { MV_PMODE_1MV, MV_PMODE_MIXED_MV, MV_PMODE_1MV_HPEL, MV_PMODE_INTENSITY_COMP, MV_PMODE_1MV_HPEL_BILIN }
};
const uint8_t ff_vc1_mv_pmode_table2[2][4] = {
    { MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_1MV, MV_PMODE_1MV_HPEL, MV_PMODE_MIXED_MV },
    { MV_PMODE_1MV, MV_PMODE_MIXED_MV, MV_PMODE_1MV_HPEL, MV_PMODE_1MV_HPEL_BILIN }
};

/* MBMODE table for interlaced frame P-picture */
const uint8_t ff_vc1_mbmode_intfrp[2][15][4] = {
    { /* 1: 4-MV, 0: non-4-MV */
        /* Type, FIELDTX, 1-MV Differential present, Residuals (CBP) present */
        /* Table 164 - Table 167 */
        { MV_PMODE_INTFR_1MV      , 0, 1, 1 },
        { MV_PMODE_INTFR_1MV      , 1, 1, 1 },
        { MV_PMODE_INTFR_1MV      , 0, 1, 0 },
        { MV_PMODE_INTFR_1MV      , 0, 0, 1 },
        { MV_PMODE_INTFR_1MV      , 1, 0, 1 },
        { MV_PMODE_INTFR_2MV_FIELD, 0, 0, 1 },
        { MV_PMODE_INTFR_2MV_FIELD, 1, 0, 1 },
        { MV_PMODE_INTFR_2MV_FIELD, 1, 0, 0 },
        { MV_PMODE_INTFR_INTRA    , 0, 0, 0 }
    },
    {
        /* Table 160 - Table 163 */
        { MV_PMODE_INTFR_1MV      , 0, 1, 1 },
        { MV_PMODE_INTFR_1MV      , 1, 1, 1 },
        { MV_PMODE_INTFR_1MV      , 0, 1, 0 },
        { MV_PMODE_INTFR_1MV      , 0, 0, 1 },
        { MV_PMODE_INTFR_1MV      , 1, 0, 1 },
        { MV_PMODE_INTFR_2MV_FIELD, 0, 0, 1 },
        { MV_PMODE_INTFR_2MV_FIELD, 1, 0, 1 },
        { MV_PMODE_INTFR_2MV_FIELD, 1, 0, 0 },
        { MV_PMODE_INTFR_4MV      , 0, 0, 1 },
        { MV_PMODE_INTFR_4MV      , 1, 0, 1 },
        { MV_PMODE_INTFR_4MV      , 0, 0, 0 },
        { MV_PMODE_INTFR_4MV_FIELD, 0, 0, 1 },
        { MV_PMODE_INTFR_4MV_FIELD, 1, 0, 1 },
        { MV_PMODE_INTFR_4MV_FIELD, 1, 0, 0 },
        { MV_PMODE_INTFR_INTRA    , 0, 0, 0 }
    }
};

const int ff_vc1_fps_nr[7] = { 24, 25, 30, 50, 60, 48, 72 },
          ff_vc1_fps_dr[2] = { 1000, 1001 };
const uint8_t ff_vc1_pquant_table[3][32] = {
    /* Implicit quantizer */
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  6,  7,  8,  9, 10, 11, 12,
      13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 29, 31 },
    /* Explicit quantizer, pquantizer uniform */
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 },
    /* Explicit quantizer, pquantizer non-uniform */
    {  0,  1,  1,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,
      14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 31 }
};

/** @name VC-1 VLC tables
 *  @todo TODO move this into the context
 */
//@{
VLC ff_vc1_imode_vlc;
VLC ff_vc1_norm2_vlc;
VLC ff_vc1_norm6_vlc;
/* Could be optimized, one table only needs 8 bits */
VLC ff_vc1_ttmb_vlc[3];
VLC ff_vc1_mv_diff_vlc[4];
VLC ff_vc1_cbpcy_p_vlc[4];
VLC ff_vc1_icbpcy_vlc[8];
VLC ff_vc1_4mv_block_pattern_vlc[4];
VLC ff_vc1_2mv_block_pattern_vlc[4];
VLC ff_vc1_ttblk_vlc[3];
VLC ff_vc1_subblkpat_vlc[3];
VLC ff_vc1_intfr_4mv_mbmode_vlc[4];
VLC ff_vc1_intfr_non4mv_mbmode_vlc[4];
VLC ff_vc1_if_mmv_mbmode_vlc[8];
VLC ff_vc1_if_1mv_mbmode_vlc[8];
VLC ff_vc1_1ref_mvdata_vlc[4];
VLC ff_vc1_2ref_mvdata_vlc[8];

VLC ff_vc1_ac_coeff_table[8];

//@}


#if B_FRACTION_DEN == 840 // original bfraction from vc9data.h, not conforming to standard
/* bfraction is fractional, we scale to the GCD 3*5*7*8 = 840 */
const int16_t ff_vc1_bfraction_lut[23] = {
    420 /*1/2*/, 280 /*1/3*/, 560 /*2/3*/, 210 /*1/4*/,
    630 /*3/4*/, 168 /*1/5*/, 336 /*2/5*/,
    504 /*3/5*/, 672 /*4/5*/, 140 /*1/6*/, 700 /*5/6*/,
    120 /*1/7*/, 240 /*2/7*/, 360 /*3/7*/, 480 /*4/7*/,
    600 /*5/7*/, 720 /*6/7*/, 105 /*1/8*/, 315 /*3/8*/,
    525 /*5/8*/, 735 /*7/8*/,
    -1 /*inv.*/, 0 /*BI fm*/
};
#else
/* pre-computed scales for all bfractions and base=256 */
const int16_t ff_vc1_bfraction_lut[23] = {
    128 /*1/2*/,  85 /*1/3*/, 170 /*2/3*/,  64 /*1/4*/,
    192 /*3/4*/,  51 /*1/5*/, 102 /*2/5*/,
    153 /*3/5*/, 204 /*4/5*/,  43 /*1/6*/, 215 /*5/6*/,
     37 /*1/7*/,  74 /*2/7*/, 111 /*3/7*/, 148 /*4/7*/,
    185 /*5/7*/, 222 /*6/7*/,  32 /*1/8*/,  96 /*3/8*/,
    160 /*5/8*/, 224 /*7/8*/,
    -1 /*inv.*/, 0 /*BI fm*/
};
#endif

//Same as H.264
const AVRational ff_vc1_pixel_aspect[16] = {
    {   0,  1 },
    {   1,  1 },
    {  12, 11 },
    {  10, 11 },
    {  16, 11 },
    {  40, 33 },
    {  24, 11 },
    {  20, 11 },
    {  32, 11 },
    {  80, 33 },
    {  18, 11 },
    {  15, 11 },
    {  64, 33 },
    { 160, 99 },
    {   0,  1 },
    {   0,  1 }
};

const uint8_t ff_wmv3_dc_scale_table[32] = {
     0,  2,  4,  8,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13,
    14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21
};


/* DC differentials low+hi-mo, p217 are the same as in msmpeg4data .h */

/* Table 232 */
const uint8_t ff_vc1_simple_progressive_4x4_zz [16] = {
     0,     8,    16,     1,
     9,    24,    17,     2,
    10,    18,    25,     3,
    11,    26,    19,    27
};

const uint8_t ff_vc1_adv_progressive_8x4_zz [32] = { /* Table 233 */
     0,     8,     1,    16,     2,     9,    10,     3,
    24,    17,     4,    11,    18,    12,     5,    19,
    25,    13,    20,    26,    27,     6,    21,    28,
    14,    22,    29,     7,    30,    15,    23,    31
};

const uint8_t ff_vc1_adv_progressive_4x8_zz [32] = { /* Table 234 */
     0,     1,     8,     2,
     9,    16,    17,    24,
    10,    32,    25,    18,
    40,     3,    33,    26,
    48,    11,    56,    41,
    34,    49,    57,    42,
    19,    50,    27,    58,
    35,    43,    51,    59
};

const uint8_t ff_vc1_adv_interlaced_8x8_zz [64] = { /* Table 235 */
     0,     8,     1,    16,    24,     9,     2,    32,
    40,    48,    56,    17,    10,     3,    25,    18,
    11,     4,    33,    41,    49,    57,    26,    34,
    42,    50,    58,    19,    12,     5,    27,    20,
    13,     6,    35,    28,    21,    14,     7,    15,
    22,    29,    36,    43,    51,    59,    60,    52,
    44,    37,    30,    23,    31,    38,    45,    53,
    61,    62,    54,    46,    39,    47,    55,    63
};

const uint8_t ff_vc1_adv_interlaced_8x4_zz [32] = { /* Table 236 */
     0,     8,    16,    24,     1,     9,     2,    17,
    25,    10,     3,    18,    26,     4,    11,    19,
    12,     5,    13,    20,    27,     6,    21,    28,
    14,    22,    29,     7,    30,    15,    23,    31
};

const uint8_t ff_vc1_adv_interlaced_4x8_zz [32] = { /* Table 237 */
     0,     1,     2,     8,
    16,     9,    24,    17,
    10,     3,    32,    40,
    48,    56,    25,    18,
    33,    26,    41,    34,
    49,    57,    11,    42,
    19,    50,    27,    58,
    35,    43,    51,    59
};

const uint8_t ff_vc1_adv_interlaced_4x4_zz [16] = { /* Table 238 */
     0,     8,    16,    24,
     1,     9,    17,     2,
    25,    10,    18,     3,
    26,    11,    19,    27
};


/* DQScale as specified in 8.1.3.9 - almost identical to 0x40000/i */
const int32_t ff_vc1_dqscale[63] = {
    0x40000, 0x20000, 0x15555, 0x10000, 0xCCCD, 0xAAAB, 0x9249, 0x8000,
     0x71C7,  0x6666,  0x5D17,  0x5555, 0x4EC5, 0x4925, 0x4444, 0x4000,
     0x3C3C,  0x38E4,  0x35E5,  0x3333, 0x30C3, 0x2E8C, 0x2C86, 0x2AAB,
     0x28F6,  0x2762,  0x25ED,  0x2492, 0x234F, 0x2222, 0x2108, 0x2000,
     0x1F08,  0x1E1E,  0x1D42,  0x1C72, 0x1BAD, 0x1AF3, 0x1A42, 0x199A,
     0x18FA,  0x1862,  0x17D0,  0x1746, 0x16C1, 0x1643, 0x15CA, 0x1555,
     0x14E6,  0x147B,  0x1414,  0x13B1, 0x1352, 0x12F7, 0x129E, 0x1249,
     0x11F7,  0x11A8,  0x115B,  0x1111, 0x10C9, 0x1084, 0x1041
};

/* P Interlaced field picture MV predictor scaling values (Table 114) */
const uint16_t ff_vc1_field_mvpred_scales[2][7][4] = {
// Refdist:
//      0       1       2       3 or greater
  { // current field is first
    { 128,    192,    213,    224 },   // SCALEOPP
    { 512,    341,    307,    293 },   // SCALESAME1
    { 219,    236,    242,    245 },   // SCALESAME2
    {  32,     48,     53,     56 },   // SCALEZONE1_X
    {   8,     12,     13,     14 },   // SCALEZONE1_Y
    {  37,     20,     14,     11 },   // ZONE1OFFSET_X
    {  10,      5,      4,      3 }    // ZONE1OFFSET_Y
  },
  { // current field is second
    { 128,     64,     43,     32 },   // SCALEOPP
    { 512,   1024,   1536,   2048 },   // SCALESAME1
    { 219,    204,    200,    198 },   // SCALESAME2
    {  32,     16,     11,      8 },   // SCALEZONE1_X
    {   8,      4,      3,      2 },   // SCALEZONE1_Y
    {  37,     52,     56,     58 },   // ZONE1OFFSET_X
    {  10,     13,     14,     15 }    // ZONE1OFFSET_Y
  }
};

/* B Interlaced field picture backward MV predictor scaling values for first field (Table 115) */
const uint16_t ff_vc1_b_field_mvpred_scales[7][4] = {
    // BRFD:
    //  0       1       2       3 or greater
    { 171,    205,    219,    228 },   // SCALESAME
    { 384,    320,    299,    288 },   // SCALEOPP1
    { 230,    239,    244,    246 },   // SCALEOPP2
    {  43,     51,     55,     57 },   // SCALEZONE1_X
    {  11,     13,     14,     14 },   // SCALEZONE1_Y
    {  26,     17,     12,     10 },   // ZONE1OFFSET_X
    {   7,      4,      3,      3 }    // ZONE1OFFSET_Y
};
