/*
 * Copyright (c) 2007-2008 Ian Caulfield
 *               2009 Ramiro Polla
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

#include "config.h"
#include "libavutil/attributes.h"
#include "mlpdsp.h"
#include "mlp.h"

static void mlp_filter_channel(int32_t *state, const int32_t *coeff,
                               int firorder, int iirorder,
                               unsigned int filter_shift, int32_t mask,
                               int blocksize, int32_t *sample_buffer)
{
    int32_t *firbuf = state;
    int32_t *iirbuf = state + MAX_BLOCKSIZE + MAX_FIR_ORDER;
    const int32_t *fircoeff = coeff;
    const int32_t *iircoeff = coeff + MAX_FIR_ORDER;
    int i;

    for (i = 0; i < blocksize; i++) {
        int32_t residual = *sample_buffer;
        unsigned int order;
        int64_t accum = 0;
        int32_t result;

        for (order = 0; order < firorder; order++)
            accum += (int64_t) firbuf[order] * fircoeff[order];
        for (order = 0; order < iirorder; order++)
            accum += (int64_t) iirbuf[order] * iircoeff[order];

        accum  = accum >> filter_shift;
        result = (accum + residual) & mask;

        *--firbuf = result;
        *--iirbuf = result - accum;

        *sample_buffer = result;
        sample_buffer += MAX_CHANNELS;
    }
}

void ff_mlp_rematrix_channel(int32_t *samples,
                             const int32_t *coeffs,
                             const uint8_t *bypassed_lsbs,
                             const int8_t *noise_buffer,
                             int index,
                             unsigned int dest_ch,
                             uint16_t blockpos,
                             unsigned int maxchan,
                             int matrix_noise_shift,
                             int access_unit_size_pow2,
                             int32_t mask)
{
    unsigned int src_ch, i;
    int index2 = 2 * index + 1;
    for (i = 0; i < blockpos; i++) {
        int64_t accum = 0;

        for (src_ch = 0; src_ch <= maxchan; src_ch++)
            accum += (int64_t) samples[src_ch] * coeffs[src_ch];

        if (matrix_noise_shift) {
            index &= access_unit_size_pow2 - 1;
            accum += noise_buffer[index] << (matrix_noise_shift + 7);
            index += index2;
        }

        samples[dest_ch] = ((accum >> 14) & mask) + *bypassed_lsbs;
        bypassed_lsbs += MAX_CHANNELS;
        samples += MAX_CHANNELS;
    }
}

av_cold void ff_mlpdsp_init(MLPDSPContext *c)
{
    c->mlp_filter_channel = mlp_filter_channel;
    c->mlp_rematrix_channel = ff_mlp_rematrix_channel;
    if (ARCH_ARM)
        ff_mlpdsp_init_arm(c);
    if (ARCH_X86)
        ff_mlpdsp_init_x86(c);
}
