/*
 * Loongson  optimized cabac
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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

#include "libavcodec/cabac.h"
#include "cabac.h"

#define decode_significance decode_significance_loongarch
static int decode_significance_loongarch(CABACContext *c, int max_coeff,
    uint8_t *significant_coeff_ctx_base, int *index, int64_t last_off)
{
    void *end = significant_coeff_ctx_base + max_coeff - 1;
    int64_t minusstart = -(int64_t)significant_coeff_ctx_base;
    int64_t minusindex = 4 - (int64_t)index;
    int64_t bit, tmp0, tmp1, tmp2, one = 1;
    uint8_t *state = significant_coeff_ctx_base;

    __asm__ volatile(
    "3:"
#if UNCHECKED_BITSTREAM_READER
    GET_CABAC_LOONGARCH_UNCBSR
#else
    GET_CABAC_LOONGARCH
#endif
    "blt     %[bit],          %[one],            4f               \n\t"
    "add.d   %[state],        %[state],          %[last_off]      \n\t"
#if UNCHECKED_BITSTREAM_READER
    GET_CABAC_LOONGARCH_UNCBSR
#else
    GET_CABAC_LOONGARCH
#endif
    "sub.d   %[state],        %[state],          %[last_off]      \n\t"
    "add.d   %[tmp0],         %[state],          %[minusstart]    \n\t"
    "st.w    %[tmp0],         %[index],          0                \n\t"
    "bge     %[bit],          %[one],            5f               \n\t"
    "addi.d  %[index],        %[index],          4                \n\t"
    "4:                                                           \n\t"
    "addi.d  %[state],        %[state],          1                \n\t"
    "blt     %[state],        %[end],            3b               \n\t"
    "add.d   %[tmp0],         %[state],          %[minusstart]    \n\t"
    "st.w    %[tmp0],         %[index],          0                \n\t"
    "5:                                                           \n\t"
    "add.d   %[tmp0],         %[index],          %[minusindex]    \n\t"
    "srli.d  %[tmp0],         %[tmp0],           2                \n\t"
    : [bit]"=&r"(bit), [tmp0]"=&r"(tmp0), [tmp1]"=&r"(tmp1), [tmp2]"=&r"(tmp2),
      [c_range]"+&r"(c->range), [c_low]"+&r"(c->low), [state]"+&r"(state),
      [c_bytestream]"+&r"(c->bytestream), [index]"+&r"(index)
    : [tables]"r"(ff_h264_cabac_tables), [end]"r"(end), [one]"r"(one),
      [minusstart]"r"(minusstart), [minusindex]"r"(minusindex),
      [last_off]"r"(last_off),
#if !UNCHECKED_BITSTREAM_READER
      [c_bytestream_end]"r"(c->bytestream_end),
#endif
      [lps_off]"i"(H264_LPS_RANGE_OFFSET),
      [mlps_off]"i"(H264_MLPS_STATE_OFFSET + 128),
      [norm_off]"i"(H264_NORM_SHIFT_OFFSET),
      [cabac_mask]"r"(CABAC_MASK)
    : "memory"
    );

    return (int)tmp0;
}

#define decode_significance_8x8 decode_significance_8x8_loongarch
static int decode_significance_8x8_loongarch(
    CABACContext *c, uint8_t *significant_coeff_ctx_base,
    int *index, uint8_t *last_coeff_ctx_base, const uint8_t *sig_off)
{
    int64_t minusindex = 4 - (int64_t)index;
    int64_t bit, tmp0, tmp1, tmp2, one = 1, end =  63, last = 0;
    uint8_t *state = 0;
    int64_t flag_offset = H264_LAST_COEFF_FLAG_OFFSET_8x8_OFFSET;

    __asm__ volatile(
    "3:                                                              \n\t"
    "ldx.bu   %[tmp0],     %[sig_off],       %[last]                 \n\t"
    "add.d    %[state],    %[tmp0], %[significant_coeff_ctx_base]    \n\t"
#if UNCHECKED_BITSTREAM_READER
    GET_CABAC_LOONGARCH_UNCBSR
#else
    GET_CABAC_LOONGARCH
#endif
    "blt      %[bit],      %[one],           4f                      \n\t"
    "add.d    %[tmp0],     %[tables],        %[flag_offset]          \n\t"
    "ldx.bu   %[tmp1],     %[tmp0],          %[last]                 \n\t"
    "add.d    %[state],    %[tmp1],    %[last_coeff_ctx_base]        \n\t"
#if UNCHECKED_BITSTREAM_READER
    GET_CABAC_LOONGARCH_UNCBSR
#else
    GET_CABAC_LOONGARCH
#endif
    "st.w    %[last],      %[index],         0                       \n\t"
    "bge     %[bit],       %[one],           5f                      \n\t"
    "addi.d  %[index],     %[index],         4                       \n\t"
    "4:                                                              \n\t"
    "addi.d  %[last],      %[last],          1                       \n\t"
    "blt     %[last],      %[end],           3b                      \n\t"
    "st.w    %[last],      %[index],         0                       \n\t"
    "5:                                                              \n\t"
    "add.d   %[tmp0],      %[index],         %[minusindex]           \n\t"
    "srli.d  %[tmp0],      %[tmp0],          2                       \n\t"
    : [bit]"=&r"(bit), [tmp0]"=&r"(tmp0), [tmp1]"=&r"(tmp1),
      [tmp2]"=&r"(tmp2), [c_range]"+&r"(c->range),
      [c_low]"+&r"(c->low), [state]"+&r"(state), [last]"+&r"(last),
      [c_bytestream]"+&r"(c->bytestream), [index]"+&r"(index)
    : [tables]"r"(ff_h264_cabac_tables), [end]"r"(end),
      [one]"r"(one), [minusindex]"r"(minusindex),
      [last_coeff_ctx_base]"r"(last_coeff_ctx_base),
      [flag_offset]"r"(flag_offset),
#if !UNCHECKED_BITSTREAM_READER
      [c_bytestream_end]"r"(c->bytestream_end),
#endif
      [lps_off]"i"(H264_LPS_RANGE_OFFSET), [sig_off]"r"(sig_off),
      [mlps_off]"i"(H264_MLPS_STATE_OFFSET + 128),
      [norm_off]"i"(H264_NORM_SHIFT_OFFSET),
      [cabac_mask]"r"(CABAC_MASK),
      [significant_coeff_ctx_base]"r"(significant_coeff_ctx_base)
    );

    return (int)tmp0;
}
