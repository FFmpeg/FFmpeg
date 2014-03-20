/*
 * Copyright (c) 2014 RISC OS Open Ltd
 * Author: Ben Avison <bavison@riscosopen.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/arm/cpu.h"
#include "libavutil/attributes.h"
#include "libavcodec/mlpdsp.h"

void ff_mlp_filter_channel_arm(int32_t *state, const int32_t *coeff,
                               int firorder, int iirorder,
                               unsigned int filter_shift, int32_t mask,
                               int blocksize, int32_t *sample_buffer);
void ff_mlp_rematrix_channel_arm(int32_t *samples,
                                 const int32_t *coeffs,
                                 const uint8_t *bypassed_lsbs,
                                 const int8_t *noise_buffer,
                                 int index,
                                 unsigned int dest_ch,
                                 uint16_t blockpos,
                                 unsigned int maxchan,
                                 int matrix_noise_shift,
                                 int access_unit_size_pow2,
                                 int32_t mask);

#define DECLARE_PACK(order,channels,shift) \
    int32_t ff_mlp_pack_output_##order##order_##channels##ch_##shift##shift_armv6(int32_t, uint16_t, int32_t (*)[], void *, uint8_t*, int8_t *, uint8_t, int);
#define ENUMERATE_PACK(order,channels,shift) \
    ff_mlp_pack_output_##order##order_##channels##ch_##shift##shift_armv6,
#define PACK_CHANNELS(macro,order,channels) \
        macro(order,channels,0) \
        macro(order,channels,1) \
        macro(order,channels,2) \
        macro(order,channels,3) \
        macro(order,channels,4) \
        macro(order,channels,5) \
        macro(order,channels,mixed)
#define PACK_ORDER(macro,order) \
        PACK_CHANNELS(macro,order,2) \
        PACK_CHANNELS(macro,order,6) \
        PACK_CHANNELS(macro,order,8)
#define PACK_ALL(macro) \
        PACK_ORDER(macro,outof) \
        PACK_ORDER(macro,in)
PACK_ALL(DECLARE_PACK)

#define ff_mlp_pack_output_outoforder_2ch_mixedshift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_mixedshift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_mixedshift_armv6 0
#if CONFIG_THUMB
#define ff_mlp_pack_output_outoforder_2ch_0shift_armv6 0
#define ff_mlp_pack_output_outoforder_2ch_1shift_armv6 0
#define ff_mlp_pack_output_outoforder_2ch_2shift_armv6 0
#define ff_mlp_pack_output_outoforder_2ch_3shift_armv6 0
#define ff_mlp_pack_output_outoforder_2ch_4shift_armv6 0
#define ff_mlp_pack_output_outoforder_2ch_5shift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_0shift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_1shift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_2shift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_3shift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_4shift_armv6 0
#define ff_mlp_pack_output_outoforder_6ch_5shift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_0shift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_1shift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_2shift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_3shift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_4shift_armv6 0
#define ff_mlp_pack_output_outoforder_8ch_5shift_armv6 0
#endif

static int32_t (*mlp_select_pack_output_armv6(uint8_t *ch_assign,
                                              int8_t *output_shift,
                                              uint8_t max_matrix_channel,
                                              int is32))(int32_t, uint16_t, int32_t (*)[], void *, uint8_t*, int8_t *, uint8_t, int)
{
    int ch_index;
    int shift = output_shift[0] < 0 || output_shift[0] > 5 ? 6 : output_shift[0];
    int inorder = 1;
    static int32_t (*const routine[2*3*7])(int32_t, uint16_t, int32_t (*)[], void *, uint8_t*, int8_t *, uint8_t, int) = {
            PACK_ALL(ENUMERATE_PACK)
    };
    int i;

    if (!is32) // don't support 16-bit output (it's not used by TrueHD)
        return ff_mlp_pack_output;

    switch (max_matrix_channel) {
    case 1:
        ch_index = 0;
        break;
    case 5:
        ch_index = 1;
        break;
    case 7:
        ch_index = 2;
        break;
    default:
        return ff_mlp_pack_output;
    }

    for (i = 0; i <= max_matrix_channel; i++) {
        if (shift != 6 && output_shift[i] != shift)
            shift = 6; // indicate mixed shifts
        if (ch_assign[i] != i)
            inorder = 0;
    }
#if CONFIG_THUMB
    if (!inorder)
        return ff_mlp_pack_output; // can't currently handle an order array except in ARM mode
#else
    if (shift == 6 && !inorder)
        return ff_mlp_pack_output; // can't currently handle both an order array and a shift array
#endif

    return routine[(inorder*3+ch_index)*7+shift];
}

av_cold void ff_mlpdsp_init_arm(MLPDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv5te(cpu_flags)) {
        c->mlp_filter_channel = ff_mlp_filter_channel_arm;
        c->mlp_rematrix_channel = ff_mlp_rematrix_channel_arm;
    }
    if (have_armv6(cpu_flags))
        c->mlp_select_pack_output = mlp_select_pack_output_armv6;
}
