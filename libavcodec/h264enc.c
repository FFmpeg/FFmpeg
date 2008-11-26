/*
 * H.264 encoder
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


#include "libavutil/common.h"
#include "bitstream.h"
#include "mpegvideo.h"
#include "h264data.h"

/**
 * Write out the provided data into a NAL unit.
 * @param nal_ref_idc NAL reference IDC
 * @param nal_unit_type NAL unit payload type
 * @param dest the target buffer, dst+1 == src is allowed as a special case
 * @param destsize the length of the dst array
 * @param b2 the data which should be escaped
 * @returns pointer to current position in the output buffer or NULL if an error occurred
 */
static uint8_t *h264_write_nal_unit(int nal_ref_idc, int nal_unit_type, uint8_t *dest, int *destsize,
                          PutBitContext *b2)
{
    PutBitContext b;
    int i, destpos, rbsplen, escape_count;
    uint8_t *rbsp;

    if (nal_unit_type != NAL_END_STREAM)
        put_bits(b2,1,1); // rbsp_stop_bit

    // Align b2 on a byte boundary
    align_put_bits(b2);
    rbsplen = put_bits_count(b2)/8;
    flush_put_bits(b2);
    rbsp = b2->buf;

    init_put_bits(&b,dest,*destsize);

    put_bits(&b,16,0);
    put_bits(&b,16,0x01);

    put_bits(&b,1,0); // forbidden zero bit
    put_bits(&b,2,nal_ref_idc); // nal_ref_idc
    put_bits(&b,5,nal_unit_type); // nal_unit_type

    flush_put_bits(&b);

    destpos = 5;
    escape_count= 0;

    for (i=0; i<rbsplen; i+=2)
    {
        if (rbsp[i]) continue;
        if (i>0 && rbsp[i-1]==0)
            i--;
        if (i+2<rbsplen && rbsp[i+1]==0 && rbsp[i+2]<=3)
        {
            escape_count++;
            i+=2;
        }
    }

    if(escape_count==0)
    {
        if(dest+destpos != rbsp)
        {
            memcpy(dest+destpos, rbsp, rbsplen);
            *destsize -= (rbsplen+destpos);
        }
        return dest+rbsplen+destpos;
    }

    if(rbsplen + escape_count + 1> *destsize)
    {
        av_log(NULL, AV_LOG_ERROR, "Destination buffer too small!\n");
        return NULL;
    }

    // this should be damn rare (hopefully)
    for (i = 0 ; i < rbsplen ; i++)
    {
        if (i + 2 < rbsplen && (rbsp[i] == 0 && rbsp[i+1] == 0 && rbsp[i+2] < 4))
        {
            dest[destpos++] = rbsp[i++];
            dest[destpos++] = rbsp[i];
            dest[destpos++] = 0x03; // emulation prevention byte
        }
        else
            dest[destpos++] = rbsp[i];
    }
    *destsize -= destpos;
    return dest+destpos;
}

static const uint8_t pict_type_to_golomb[7] = {-1, 2, 0, 1, -1, 4, 3};

static const uint8_t intra4x4_cbp_to_golomb[48] = {
    3, 29, 30, 17, 31, 18, 37,  8, 32, 38, 19,  9, 20, 10, 11,  2,
   16, 33, 34, 21, 35, 22, 39,  4, 36, 40, 23,  5, 24,  6,  7,  1,
   41, 42, 43, 25, 44, 26, 46, 12, 45, 47, 27, 13, 28, 14, 15,  0
};

static const uint8_t inter_cbp_to_golomb[48] = {
    0,  2,  3,  7,  4,  8, 17, 13,  5, 18,  9, 14, 10, 15, 16, 11,
    1, 32, 33, 36, 34, 37, 44, 40, 35, 45, 38, 41, 39, 42, 43, 19,
    6, 24, 25, 20, 26, 21, 46, 28, 27, 47, 22, 29, 23, 30, 31, 12
};

#define QUANT_SHIFT 22

static const int quant_coeff[52][16] = {
    { 419430, 258111, 419430, 258111, 258111, 167772, 258111, 167772, 419430, 258111, 419430, 258111, 258111, 167772, 258111, 167772,},
    { 381300, 239675, 381300, 239675, 239675, 149131, 239675, 149131, 381300, 239675, 381300, 239675, 239675, 149131, 239675, 149131,},
    { 322639, 209715, 322639, 209715, 209715, 134218, 209715, 134218, 322639, 209715, 322639, 209715, 209715, 134218, 209715, 134218,},
    { 299593, 186414, 299593, 186414, 186414, 116711, 186414, 116711, 299593, 186414, 299593, 186414, 186414, 116711, 186414, 116711,},
    { 262144, 167772, 262144, 167772, 167772, 107374, 167772, 107374, 262144, 167772, 262144, 167772, 167772, 107374, 167772, 107374,},
    { 233017, 145889, 233017, 145889, 145889,  92564, 145889,  92564, 233017, 145889, 233017, 145889, 145889,  92564, 145889,  92564,},
    { 209715, 129056, 209715, 129056, 129056,  83886, 129056,  83886, 209715, 129056, 209715, 129056, 129056,  83886, 129056,  83886,},
    { 190650, 119837, 190650, 119837, 119837,  74565, 119837,  74565, 190650, 119837, 190650, 119837, 119837,  74565, 119837,  74565,},
    { 161319, 104858, 161319, 104858, 104858,  67109, 104858,  67109, 161319, 104858, 161319, 104858, 104858,  67109, 104858,  67109,},
    { 149797,  93207, 149797,  93207,  93207,  58356,  93207,  58356, 149797,  93207, 149797,  93207,  93207,  58356,  93207,  58356,},
    { 131072,  83886, 131072,  83886,  83886,  53687,  83886,  53687, 131072,  83886, 131072,  83886,  83886,  53687,  83886,  53687,},
    { 116508,  72944, 116508,  72944,  72944,  46282,  72944,  46282, 116508,  72944, 116508,  72944,  72944,  46282,  72944,  46282,},
    { 104858,  64528, 104858,  64528,  64528,  41943,  64528,  41943, 104858,  64528, 104858,  64528,  64528,  41943,  64528,  41943,},
    {  95325,  59919,  95325,  59919,  59919,  37283,  59919,  37283,  95325,  59919,  95325,  59919,  59919,  37283,  59919,  37283,},
    {  80660,  52429,  80660,  52429,  52429,  33554,  52429,  33554,  80660,  52429,  80660,  52429,  52429,  33554,  52429,  33554,},
    {  74898,  46603,  74898,  46603,  46603,  29178,  46603,  29178,  74898,  46603,  74898,  46603,  46603,  29178,  46603,  29178,},
    {  65536,  41943,  65536,  41943,  41943,  26844,  41943,  26844,  65536,  41943,  65536,  41943,  41943,  26844,  41943,  26844,},
    {  58254,  36472,  58254,  36472,  36472,  23141,  36472,  23141,  58254,  36472,  58254,  36472,  36472,  23141,  36472,  23141,},
    {  52429,  32264,  52429,  32264,  32264,  20972,  32264,  20972,  52429,  32264,  52429,  32264,  32264,  20972,  32264,  20972,},
    {  47663,  29959,  47663,  29959,  29959,  18641,  29959,  18641,  47663,  29959,  47663,  29959,  29959,  18641,  29959,  18641,},
    {  40330,  26214,  40330,  26214,  26214,  16777,  26214,  16777,  40330,  26214,  40330,  26214,  26214,  16777,  26214,  16777,},
    {  37449,  23302,  37449,  23302,  23302,  14589,  23302,  14589,  37449,  23302,  37449,  23302,  23302,  14589,  23302,  14589,},
    {  32768,  20972,  32768,  20972,  20972,  13422,  20972,  13422,  32768,  20972,  32768,  20972,  20972,  13422,  20972,  13422,},
    {  29127,  18236,  29127,  18236,  18236,  11570,  18236,  11570,  29127,  18236,  29127,  18236,  18236,  11570,  18236,  11570,},
    {  26214,  16132,  26214,  16132,  16132,  10486,  16132,  10486,  26214,  16132,  26214,  16132,  16132,  10486,  16132,  10486,},
    {  23831,  14980,  23831,  14980,  14980,   9321,  14980,   9321,  23831,  14980,  23831,  14980,  14980,   9321,  14980,   9321,},
    {  20165,  13107,  20165,  13107,  13107,   8389,  13107,   8389,  20165,  13107,  20165,  13107,  13107,   8389,  13107,   8389,},
    {  18725,  11651,  18725,  11651,  11651,   7294,  11651,   7294,  18725,  11651,  18725,  11651,  11651,   7294,  11651,   7294,},
    {  16384,  10486,  16384,  10486,  10486,   6711,  10486,   6711,  16384,  10486,  16384,  10486,  10486,   6711,  10486,   6711,},
    {  14564,   9118,  14564,   9118,   9118,   5785,   9118,   5785,  14564,   9118,  14564,   9118,   9118,   5785,   9118,   5785,},
    {  13107,   8066,  13107,   8066,   8066,   5243,   8066,   5243,  13107,   8066,  13107,   8066,   8066,   5243,   8066,   5243,},
    {  11916,   7490,  11916,   7490,   7490,   4660,   7490,   4660,  11916,   7490,  11916,   7490,   7490,   4660,   7490,   4660,},
    {  10082,   6554,  10082,   6554,   6554,   4194,   6554,   4194,  10082,   6554,  10082,   6554,   6554,   4194,   6554,   4194,},
    {   9362,   5825,   9362,   5825,   5825,   3647,   5825,   3647,   9362,   5825,   9362,   5825,   5825,   3647,   5825,   3647,},
    {   8192,   5243,   8192,   5243,   5243,   3355,   5243,   3355,   8192,   5243,   8192,   5243,   5243,   3355,   5243,   3355,},
    {   7282,   4559,   7282,   4559,   4559,   2893,   4559,   2893,   7282,   4559,   7282,   4559,   4559,   2893,   4559,   2893,},
    {   6554,   4033,   6554,   4033,   4033,   2621,   4033,   2621,   6554,   4033,   6554,   4033,   4033,   2621,   4033,   2621,},
    {   5958,   3745,   5958,   3745,   3745,   2330,   3745,   2330,   5958,   3745,   5958,   3745,   3745,   2330,   3745,   2330,},
    {   5041,   3277,   5041,   3277,   3277,   2097,   3277,   2097,   5041,   3277,   5041,   3277,   3277,   2097,   3277,   2097,},
    {   4681,   2913,   4681,   2913,   2913,   1824,   2913,   1824,   4681,   2913,   4681,   2913,   2913,   1824,   2913,   1824,},
    {   4096,   2621,   4096,   2621,   2621,   1678,   2621,   1678,   4096,   2621,   4096,   2621,   2621,   1678,   2621,   1678,},
    {   3641,   2280,   3641,   2280,   2280,   1446,   2280,   1446,   3641,   2280,   3641,   2280,   2280,   1446,   2280,   1446,},
    {   3277,   2016,   3277,   2016,   2016,   1311,   2016,   1311,   3277,   2016,   3277,   2016,   2016,   1311,   2016,   1311,},
    {   2979,   1872,   2979,   1872,   1872,   1165,   1872,   1165,   2979,   1872,   2979,   1872,   1872,   1165,   1872,   1165,},
    {   2521,   1638,   2521,   1638,   1638,   1049,   1638,   1049,   2521,   1638,   2521,   1638,   1638,   1049,   1638,   1049,},
    {   2341,   1456,   2341,   1456,   1456,    912,   1456,    912,   2341,   1456,   2341,   1456,   1456,    912,   1456,    912,},
    {   2048,   1311,   2048,   1311,   1311,    839,   1311,    839,   2048,   1311,   2048,   1311,   1311,    839,   1311,    839,},
    {   1820,   1140,   1820,   1140,   1140,    723,   1140,    723,   1820,   1140,   1820,   1140,   1140,    723,   1140,    723,},
    {   1638,   1008,   1638,   1008,   1008,    655,   1008,    655,   1638,   1008,   1638,   1008,   1008,    655,   1008,    655,},
    {   1489,    936,   1489,    936,    936,    583,    936,    583,   1489,    936,   1489,    936,    936,    583,    936,    583,},
    {   1260,    819,   1260,    819,    819,    524,    819,    524,   1260,    819,   1260,    819,    819,    524,    819,    524,},
    {   1170,    728,   1170,    728,    728,    456,    728,    456,   1170,    728,   1170,    728,    728,    456,    728,    456,},
};

//FIXME need to check that this does not overflow signed 32 bit for low qp, I am not sure, it's very close
//FIXME check that gcc inlines this (and optimizes intra & separate_dc stuff away)
static inline int quantize_c(DCTELEM *block, uint8_t *scantable, int qscale,
                             int intra, int separate_dc)
{
    int i;
    const int * const quant_3Btable = quant_coeff[qscale];
    const int bias = intra ? (1 << QUANT_SHIFT) / 3 : (1 << QUANT_SHIFT) / 6;
    const unsigned int threshold1 = (1 << QUANT_SHIFT) - bias - 1;
    const unsigned int threshold2 = (threshold1 << 1);
    int last_non_zero;

    if (separate_dc) {
        if (qscale <= 18) {
            //avoid overflows
            const int dc_bias = intra ? (1 << (QUANT_SHIFT - 2)) / 3 : (1 << (QUANT_SHIFT - 2)) / 6;
            const unsigned int dc_threshold1 = (1 << (QUANT_SHIFT - 2)) - dc_bias - 1;
            const unsigned int dc_threshold2 = (dc_threshold1 << 1);

            int level = block[0]*quant_coeff[qscale+18][0];
            if (((unsigned)(level + dc_threshold1)) > dc_threshold2) {
                if (level > 0) {
                    level = (dc_bias + level) >> (QUANT_SHIFT - 2);
                    block[0] = level;
                } else {
                    level = (dc_bias - level) >> (QUANT_SHIFT - 2);
                    block[0] = -level;
                }
//                last_non_zero = i;
            } else {
                block[0] = 0;
            }
        } else {
            const int dc_bias = intra ? (1 << (QUANT_SHIFT + 1)) / 3 : (1 << (QUANT_SHIFT + 1)) / 6;
            const unsigned int dc_threshold1 = (1 << (QUANT_SHIFT + 1)) - dc_bias - 1;
            const unsigned int dc_threshold2 = (dc_threshold1 << 1);

            int level = block[0]*quant_table[0];
            if (((unsigned)(level + dc_threshold1)) > dc_threshold2) {
                if (level > 0) {
                    level = (dc_bias + level) >> (QUANT_SHIFT + 1);
                    block[0] = level;
                } else {
                    level = (dc_bias - level) >> (QUANT_SHIFT + 1);
                    block[0] = -level;
                }
//                last_non_zero = i;
            } else {
                block[0] = 0;
            }
        }
        last_non_zero = 0;
        i = 1;
    } else {
        last_non_zero = -1;
        i = 0;
    }

    for (; i < 16; i++) {
        const int j = scantable[i];
        int level = block[j]*quant_table[j];

//        if (   bias+level >= (1 << (QMAT_SHIFT - 3))
//            || bias-level >= (1 << (QMAT_SHIFT - 3))) {
        if (((unsigned)(level + threshold1)) > threshold2) {
            if (level > 0) {
                level = (bias + level) >> QUANT_SHIFT;
                block[j] = level;
            } else {
                level = (bias - level) >> QUANT_SHIFT;
                block[j] = -level;
            }
            last_non_zero = i;
        } else {
            block[j] = 0;
        }
    }

    return last_non_zero;
}
