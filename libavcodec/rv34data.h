/*
 * RealVideo 4 decoder
 * copyright (c) 2007 Konstantin Shishkov
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
 * miscellaneous RV30/40 tables
 */

#ifndef AVCODEC_RV34DATA_H
#define AVCODEC_RV34DATA_H

#include <stdint.h>

/**
 * number of ones in nibble minus one
 */
static const uint8_t rv34_count_ones[16] = {
    0, 0, 0, 1, 0, 1, 1, 2, 0, 1, 1, 2, 1, 2, 2, 3
};

/**
 * values used to reconstruct coded block pattern
 */
static const uint8_t rv34_cbp_code[16] = {
    0x00, 0x20, 0x10, 0x30, 0x02, 0x22, 0x12, 0x32,
    0x01, 0x21, 0x11, 0x31, 0x03, 0x23, 0x13, 0x33
};

/**
 * precalculated results of division by three and modulo three for values 0-107
 *
 * A lot of four-tuples in RV40 are represented as c0*27+c1*9+c2*3+c3.
 * This table allows conversion from a value back to a vector.
 */
static const uint8_t modulo_three_table[108][4] = {
 { 0, 0, 0, 0 }, { 0, 0, 0, 1 }, { 0, 0, 0, 2 }, { 0, 0, 1, 0 },
 { 0, 0, 1, 1 }, { 0, 0, 1, 2 }, { 0, 0, 2, 0 }, { 0, 0, 2, 1 },
 { 0, 0, 2, 2 }, { 0, 1, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 0, 2 },
 { 0, 1, 1, 0 }, { 0, 1, 1, 1 }, { 0, 1, 1, 2 }, { 0, 1, 2, 0 },
 { 0, 1, 2, 1 }, { 0, 1, 2, 2 }, { 0, 2, 0, 0 }, { 0, 2, 0, 1 },
 { 0, 2, 0, 2 }, { 0, 2, 1, 0 }, { 0, 2, 1, 1 }, { 0, 2, 1, 2 },
 { 0, 2, 2, 0 }, { 0, 2, 2, 1 }, { 0, 2, 2, 2 }, { 1, 0, 0, 0 },
 { 1, 0, 0, 1 }, { 1, 0, 0, 2 }, { 1, 0, 1, 0 }, { 1, 0, 1, 1 },
 { 1, 0, 1, 2 }, { 1, 0, 2, 0 }, { 1, 0, 2, 1 }, { 1, 0, 2, 2 },
 { 1, 1, 0, 0 }, { 1, 1, 0, 1 }, { 1, 1, 0, 2 }, { 1, 1, 1, 0 },
 { 1, 1, 1, 1 }, { 1, 1, 1, 2 }, { 1, 1, 2, 0 }, { 1, 1, 2, 1 },
 { 1, 1, 2, 2 }, { 1, 2, 0, 0 }, { 1, 2, 0, 1 }, { 1, 2, 0, 2 },
 { 1, 2, 1, 0 }, { 1, 2, 1, 1 }, { 1, 2, 1, 2 }, { 1, 2, 2, 0 },
 { 1, 2, 2, 1 }, { 1, 2, 2, 2 }, { 2, 0, 0, 0 }, { 2, 0, 0, 1 },
 { 2, 0, 0, 2 }, { 2, 0, 1, 0 }, { 2, 0, 1, 1 }, { 2, 0, 1, 2 },
 { 2, 0, 2, 0 }, { 2, 0, 2, 1 }, { 2, 0, 2, 2 }, { 2, 1, 0, 0 },
 { 2, 1, 0, 1 }, { 2, 1, 0, 2 }, { 2, 1, 1, 0 }, { 2, 1, 1, 1 },
 { 2, 1, 1, 2 }, { 2, 1, 2, 0 }, { 2, 1, 2, 1 }, { 2, 1, 2, 2 },
 { 2, 2, 0, 0 }, { 2, 2, 0, 1 }, { 2, 2, 0, 2 }, { 2, 2, 1, 0 },
 { 2, 2, 1, 1 }, { 2, 2, 1, 2 }, { 2, 2, 2, 0 }, { 2, 2, 2, 1 },
 { 2, 2, 2, 2 }, { 3, 0, 0, 0 }, { 3, 0, 0, 1 }, { 3, 0, 0, 2 },
 { 3, 0, 1, 0 }, { 3, 0, 1, 1 }, { 3, 0, 1, 2 }, { 3, 0, 2, 0 },
 { 3, 0, 2, 1 }, { 3, 0, 2, 2 }, { 3, 1, 0, 0 }, { 3, 1, 0, 1 },
 { 3, 1, 0, 2 }, { 3, 1, 1, 0 }, { 3, 1, 1, 1 }, { 3, 1, 1, 2 },
 { 3, 1, 2, 0 }, { 3, 1, 2, 1 }, { 3, 1, 2, 2 }, { 3, 2, 0, 0 },
 { 3, 2, 0, 1 }, { 3, 2, 0, 2 }, { 3, 2, 1, 0 }, { 3, 2, 1, 1 },
 { 3, 2, 1, 2 }, { 3, 2, 2, 0 }, { 3, 2, 2, 1 }, { 3, 2, 2, 2 },
};

/**
 * quantizer values used for AC and DC coefficients in chroma blocks
 */
static const uint8_t rv34_chroma_quant[2][32] = {
 {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
   16, 17, 17, 18, 19, 20, 20, 21, 22, 22, 23, 23, 24, 24, 25, 25 },
 {  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,
   14, 15, 15, 16, 17, 18, 18, 19, 20, 20, 21, 21, 22, 22, 23, 23 }
};

/**
 * This table is used for dequantizing.
 */
static const uint16_t rv34_qscale_tab[32] = {
  60,   67,   76,   85,   96,  108,  121,  136,
 152,  171,  192,  216,  242,  272,  305,  341,
 383,  432,  481,  544,  606,  683,  767,  854,
 963, 1074, 1212, 1392, 1566, 1708, 1978, 2211
};

/**
 * 4x4 dezigzag pattern
 */
static const uint8_t rv34_dezigzag[16] = {
  0,  1,  8, 16,
  9,  2,  3, 10,
 17, 24, 25, 18,
 11, 19, 26, 27
};

/**
 * tables used to translate a quantizer value into a VLC set for decoding
 * The first table is used for intraframes.
 */
static const uint8_t rv34_quant_to_vlc_set[2][31] = {
 { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
   2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 0 },
 { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3,
   3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6 },
};

/**
 * table for obtaining the quantizer difference
 * @todo Use with modified_quant_tab from h263data.h.
 */
static const uint8_t rv34_dquant_tab[2][32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
{
    0, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9,10,11,12,13,14,15,16,17,18,18,19,20,21,22,23,24,25,26,27,28
},{
    0, 2, 3, 4, 5, 6, 7, 8, 9,10,11,13,14,15,16,17,18,19,20,21,22,24,25,26,27,28,29,30,31,31,31,26
}
};

/**
 * maximum number of macroblocks for each of the possible slice offset sizes
 * @todo This is the same as ff_mba_max, maybe use it instead.
 */
static const uint16_t rv34_mb_max_sizes[6] = { 0x2F, 0x62, 0x18B, 0x62F, 0x18BF, 0x23FF };
/**
 * bits needed to code the slice offset for the given size
 * @todo This is the same as ff_mba_length, maybe use it instead.
 */
static const uint8_t rv34_mb_bits_sizes[6] = { 6, 7, 9, 11, 13, 14 };

#endif /* AVCODEC_RV34DATA_H */
