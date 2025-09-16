/*
 * Copyright (C) 2012 Michael Niedermayer <michaelni@gmx.at>
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

#include <libavutil/attributes.h>
#include <libavutil/common.h>

#include "vf_idetdsp.h"

int ff_idet_filter_line_c(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w)
{
    int x;
    int ret=0;

    for(x=0; x<w; x++){
        int v = (*a++ + *c++) - 2 * *b++;
        ret += FFABS(v);
    }

    return ret;
}

int ff_idet_filter_line_c_16bit(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w)
{
    int x;
    int ret=0;

    const uint16_t *a16 = (const uint16_t *) a;
    const uint16_t *b16 = (const uint16_t *) b;
    const uint16_t *c16 = (const uint16_t *) c;

    for(x=0; x<w; x++){
        int v = (*a16++ + *c16++) - 2 * *b16++;
        ret += FFABS(v);
    }

    return ret;
}

void av_cold ff_idet_dsp_init(IDETDSPContext *dsp, int depth)
{
    dsp->filter_line = depth > 8 ? ff_idet_filter_line_c_16bit : ff_idet_filter_line_c;
#if ARCH_X86
    ff_idet_dsp_init_x86(dsp, depth);
#endif
}
