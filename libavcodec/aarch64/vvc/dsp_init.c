/*
 * VVC filters DSP
 *
 * Copyright (C) 2024 Zhao Zhili
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

#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/aarch64/h26x/dsp.h"
#include "libavcodec/vvc/dsp.h"
#include "libavcodec/vvc/dec.h"
#include "libavcodec/vvc/ctu.h"

#define BIT_DEPTH 8
#include "alf_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "alf_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "alf_template.c"
#undef BIT_DEPTH

void ff_vvc_dsp_init_aarch64(VVCDSPContext *const c, const int bd)
{
    int cpu_flags = av_get_cpu_flags();
    if (!have_neon(cpu_flags))
        return;

    if (bd == 8) {
        for (int i = 0; i < FF_ARRAY_ELEMS(c->sao.band_filter); i++)
            c->sao.band_filter[i] = ff_h26x_sao_band_filter_8x8_8_neon;
        c->sao.edge_filter[0] = ff_vvc_sao_edge_filter_8x8_8_neon;
        for (int i = 1; i < FF_ARRAY_ELEMS(c->sao.edge_filter); i++)
            c->sao.edge_filter[i] = ff_vvc_sao_edge_filter_16x16_8_neon;
        c->alf.filter[LUMA] = alf_filter_luma_8_neon;
        c->alf.filter[CHROMA] = alf_filter_chroma_8_neon;
    } else if (bd == 10) {
        c->alf.filter[LUMA] = alf_filter_luma_10_neon;
        c->alf.filter[CHROMA] = alf_filter_chroma_10_neon;
    } else if (bd == 12) {
        c->alf.filter[LUMA] = alf_filter_luma_12_neon;
        c->alf.filter[CHROMA] = alf_filter_chroma_12_neon;
    }
}
