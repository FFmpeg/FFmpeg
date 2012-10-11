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
#include "mlpdsp.h"
#include "mlp.h"

static void ff_mlp_filter_channel(int32_t *state, const int32_t *coeff,
                                  int firorder, int iirorder,
                                  unsigned int filter_shift, int32_t mask, int blocksize,
                                  int32_t *sample_buffer)
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

void ff_mlpdsp_init(MLPDSPContext *c)
{
    c->mlp_filter_channel = ff_mlp_filter_channel;
    if (ARCH_X86)
        ff_mlpdsp_init_x86(c);
}
