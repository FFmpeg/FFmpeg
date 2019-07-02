/*
 * Loongson SIMD optimized h264chroma
 *
 * Copyright (c) 2018 Loongson Technology Corporation Limited
 * Copyright (c) 2018 Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#ifndef AVCODEC_MIPS_CABAC_H
#define AVCODEC_MIPS_CABAC_H

#include "libavcodec/cabac.h"
#include "libavutil/mips/mmiutils.h"
#include "config.h"

#define get_cabac_inline get_cabac_inline_mips
static av_always_inline int get_cabac_inline(CABACContext *c,
                                             uint8_t * const state){
    mips_reg tmp0, tmp1, tmp2, bit;

    __asm__ volatile (
        "lbu          %[bit],        0(%[state])                   \n\t"
        "and          %[tmp0],       %[c_range],     0xC0          \n\t"
        PTR_ADDU     "%[tmp0],       %[tmp0],        %[tmp0]       \n\t"
        PTR_ADDU     "%[tmp0],       %[tmp0],        %[tables]     \n\t"
        PTR_ADDU     "%[tmp0],       %[tmp0],        %[bit]        \n\t"
        /* tmp1: RangeLPS */
        "lbu          %[tmp1],       %[lps_off](%[tmp0])           \n\t"

        PTR_SUBU     "%[c_range],    %[c_range],     %[tmp1]       \n\t"
        PTR_SLL      "%[tmp0],       %[c_range],     0x11          \n\t"
        PTR_SUBU     "%[tmp0],       %[tmp0],        %[c_low]      \n\t"

        /* tmp2: lps_mask */
        PTR_SRA      "%[tmp2],       %[tmp0],        0x1F          \n\t"
        /* If tmp0 < 0, lps_mask ==  0xffffffff*/
        /* If tmp0 >= 0, lps_mask ==  0x00000000*/
        "beqz         %[tmp2],       1f                            \n\t"
        PTR_SLL      "%[tmp0],       %[c_range],     0x11          \n\t"
        PTR_SUBU     "%[c_low],      %[c_low],       %[tmp0]       \n\t"
        PTR_SUBU     "%[tmp0],       %[tmp1],        %[c_range]    \n\t"
        PTR_ADDU     "%[c_range],    %[c_range],     %[tmp0]       \n\t"
        "xor          %[bit],        %[bit],         %[tmp2]       \n\t"

        "1:                                                        \n\t"
        /* tmp1: *state */
        PTR_ADDU     "%[tmp0],       %[tables],      %[bit]        \n\t"
        "lbu          %[tmp1],       %[mlps_off](%[tmp0])          \n\t"
        /* tmp2: lps_mask */
        PTR_ADDU     "%[tmp0],       %[tables],      %[c_range]    \n\t"
        "lbu          %[tmp2],       %[norm_off](%[tmp0])          \n\t"

        "sb           %[tmp1],       0(%[state])                   \n\t"
        "and          %[bit],        %[bit],         0x01          \n\t"
        PTR_SLL      "%[c_range],    %[c_range],     %[tmp2]       \n\t"
        PTR_SLL      "%[c_low],      %[c_low],       %[tmp2]       \n\t"

        "and          %[tmp0],       %[c_low],       %[cabac_mask] \n\t"
        "bnez         %[tmp0],       1f                            \n\t"
        PTR_ADDIU    "%[tmp0],       %[c_low],       -0x01         \n\t"
        "xor          %[tmp0],       %[c_low],       %[tmp0]       \n\t"
        PTR_SRA      "%[tmp0],       %[tmp0],        0x0f          \n\t"
        PTR_ADDU     "%[tmp0],       %[tmp0],        %[tables]     \n\t"
        "lbu          %[tmp2],       %[norm_off](%[tmp0])          \n\t"
#if CABAC_BITS == 16
        "lbu          %[tmp0],       0(%[c_bytestream])            \n\t"
        "lbu          %[tmp1],       1(%[c_bytestream])            \n\t"
        PTR_SLL      "%[tmp0],       %[tmp0],        0x09          \n\t"
        PTR_SLL      "%[tmp1],       %[tmp1],        0x01          \n\t"
        PTR_ADDU     "%[tmp0],       %[tmp0],        %[tmp1]       \n\t"
#else
        "lbu          %[tmp0],       0(%[c_bytestream])            \n\t"
        PTR_SLL      "%[tmp0],       %[tmp0],        0x01          \n\t"
#endif
        PTR_SUBU     "%[tmp0],       %[tmp0],        %[cabac_mask] \n\t"

        "li           %[tmp1],       0x07                          \n\t"
        PTR_SUBU     "%[tmp1],       %[tmp1],        %[tmp2]       \n\t"
        PTR_SLL      "%[tmp0],       %[tmp0],        %[tmp1]       \n\t"
        PTR_ADDU     "%[c_low],      %[c_low],       %[tmp0]       \n\t"

#if !UNCHECKED_BITSTREAM_READER
        "bge          %[c_bytestream], %[c_bytestream_end], 1f     \n\t"
#endif
        PTR_ADDIU    "%[c_bytestream], %[c_bytestream],     0X02   \n\t"
        "1:                                                        \n\t"
    : [bit]"=&r"(bit), [tmp0]"=&r"(tmp0), [tmp1]"=&r"(tmp1), [tmp2]"=&r"(tmp2),
      [c_range]"+&r"(c->range), [c_low]"+&r"(c->low),
      [c_bytestream]"+&r"(c->bytestream)
    : [state]"r"(state), [tables]"r"(ff_h264_cabac_tables),
#if !UNCHECKED_BITSTREAM_READER
      [c_bytestream_end]"r"(c->bytestream_end),
#endif
      [lps_off]"i"(H264_LPS_RANGE_OFFSET),
      [mlps_off]"i"(H264_MLPS_STATE_OFFSET + 128),
      [norm_off]"i"(H264_NORM_SHIFT_OFFSET),
      [cabac_mask]"i"(CABAC_MASK)
    : "memory"
    );

    return bit;
}

#endif /* AVCODEC_MIPS_CABAC_H */
