/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "h263dsp_mips.h"
#include "libavutil/mips/generic_macros_msa.h"

static int32_t sum_u8src_16width_msa(const uint8_t *src, int32_t stride)
{
    uint32_t sum = 0;
    v16u8 in0, in1, in2, in3, in4, in5, in6, in7;
    v16u8 in8, in9, in10, in11, in12, in13, in14, in15;

    LD_UB8(src, stride, in0, in1, in2, in3, in4, in5, in6, in7);
    src += (8 * stride);
    LD_UB8(src, stride, in8, in9, in10, in11, in12, in13, in14, in15);

    HADD_UB4_UB(in0, in1, in2, in3, in0, in1, in2, in3);
    HADD_UB4_UB(in4, in5, in6, in7, in4, in5, in6, in7);
    HADD_UB4_UB(in8, in9, in10, in11, in8, in9, in10, in11);
    HADD_UB4_UB(in12, in13, in14, in15, in12, in13, in14, in15);

    sum = HADD_UH_U32(in0);
    sum += HADD_UH_U32(in1);
    sum += HADD_UH_U32(in2);
    sum += HADD_UH_U32(in3);
    sum += HADD_UH_U32(in4);
    sum += HADD_UH_U32(in5);
    sum += HADD_UH_U32(in6);
    sum += HADD_UH_U32(in7);
    sum += HADD_UH_U32(in8);
    sum += HADD_UH_U32(in9);
    sum += HADD_UH_U32(in10);
    sum += HADD_UH_U32(in11);
    sum += HADD_UH_U32(in12);
    sum += HADD_UH_U32(in13);
    sum += HADD_UH_U32(in14);
    sum += HADD_UH_U32(in15);

    return sum;
}

int ff_pix_sum_msa(const uint8_t *pix, ptrdiff_t line_size)
{
    return sum_u8src_16width_msa(pix, line_size);
}
